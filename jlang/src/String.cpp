// java.lang.String, string conversion of values, StringBuilder, StringCharacterIterator,
// StringUtils.
#include <jlang/Array.h>
#include <jlang/Boxes.h>
#include <jlang/Exceptions.h>
#include <jlang/String.h>
#include <jlang/Util.h>

#include "StringInternal.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace jlang {

// ---------------------------------------------------------------------------------------
// UTF-8 / UTF-16 helpers

namespace utf {

char32_t decode(const char* p, size_t n, size_t& i) noexcept {
    const auto* s = reinterpret_cast<const unsigned char*>(p);
    unsigned c = s[i];
    if (c < 0x80) {
        i++;
        return c;
    }
    int len;
    char32_t cp;
    if (c >= 0xC2 && c <= 0xDF) {
        len = 2;
        cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        cp = c & 0x07;
    } else {
        i++;
        return 0xFFFD;
    }
    if (i + static_cast<size_t>(len) > n) {
        i++;
        return 0xFFFD;
    }
    for (int k = 1; k < len; k++) {
        unsigned cc = s[i + static_cast<size_t>(k)];
        if ((cc & 0xC0) != 0x80) {
            i++;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += static_cast<size_t>(len);
    return cp;
}

void encode(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        encode(out, 0xFFFD);
    }
}

// Appends one UTF-16 unit; a low surrogate following an encoded lone high surrogate is
// merged into one 4-byte sequence.
void appendUnit(std::string& out, char16_t c) {
    if (c >= 0xDC00 && c <= 0xDFFF && out.size() >= 3) {
        auto b0 = static_cast<unsigned char>(out[out.size() - 3]);
        auto b1 = static_cast<unsigned char>(out[out.size() - 2]);
        if (b0 == 0xED && (b1 & 0xF0) == 0xA0) {
            size_t i = out.size() - 3;
            char32_t high = decode(out.data(), out.size(), i);
            out.resize(out.size() - 3);
            encode(out, 0x10000 + ((high - 0xD800) << 10) + (c - 0xDC00));
            return;
        }
    }
    encode(out, c);
}

bool isAscii(const std::string& s) noexcept {
    for (unsigned char c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

int32_t utf16Length(const char* p, size_t n) noexcept {
    int32_t len = 0;
    size_t i = 0;
    while (i < n) {
        if (static_cast<unsigned char>(p[i]) < 0x80) {
            i++;
            len++;
            continue;
        }
        char32_t cp = decode(p, n, i);
        len += cp >= 0x10000 ? 2 : 1;
    }
    return len;
}

}  // namespace utf

// ---------------------------------------------------------------------------------------
// Java Float.toString / Double.toString

namespace {

// Shortest decimal digits of v (> 0, finite) and the exponent e such that v ~ 0.d1d2... ;
// returned as digits + scientific exponent (d1.d2... x 10^sciExp). Follows Java 19+:
// shortest length, closest to v; when the shortest has one digit, two-digit decimals are
// candidates too (Double.MIN_VALUE -> 4.9E-324).
template<class F>
void shortestDigits(F v, std::string& digits, int& sciExp) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
    std::string s(buf, r.ptr);
    size_t e = s.find('e');
    std::string mant = s.substr(0, e);
    int exp = std::atoi(s.c_str() + e + 1);
    digits.clear();
    for (char c : mant) {
        if (c != '.') digits.push_back(c);
    }
    if (digits.size() == 1) {
        auto r2 = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific, 1);
        std::string s2(buf, r2.ptr);
        F back{};
        std::from_chars(buf, r2.ptr, back);
        if (back == v) {
            size_t e2 = s2.find('e');
            std::string d2;
            for (size_t k = 0; k < e2; k++) {
                if (s2[k] != '.') d2.push_back(s2[k]);
            }
            int exp2 = std::atoi(s2.c_str() + e2 + 1);
            while (d2.size() > 1 && d2.back() == '0') d2.pop_back();
            digits = d2;
            exp = exp2;
        }
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    sciExp = exp;
}

template<class F>
void javaFloatingToString(std::string& out, F v) {
    if (v != v) {
        out.append("NaN");
        return;
    }
    if (std::isinf(v)) {
        out.append(v > 0 ? "Infinity" : "-Infinity");
        return;
    }
    if (v == 0) {
        out.append(std::signbit(v) ? "-0.0" : "0.0");
        return;
    }
    if (v < 0) {
        out.push_back('-');
        v = -v;
    }
    std::string d;
    int e;
    shortestDigits(v, d, e);
    const int n = static_cast<int>(d.size());
    if (e >= -3 && e < 7) {
        if (e >= 0) {
            for (int k = 0; k <= e; k++) out.push_back(k < n ? d[static_cast<size_t>(k)] : '0');
            out.push_back('.');
            if (n > e + 1) {
                out.append(d, static_cast<size_t>(e + 1), std::string::npos);
            } else {
                out.push_back('0');
            }
        } else {
            out.append("0.");
            for (int k = 0; k < -e - 1; k++) out.push_back('0');
            out.append(d);
        }
    } else {
        out.push_back(d[0]);
        out.push_back('.');
        if (n > 1) {
            out.append(d, 1, std::string::npos);
        } else {
            out.push_back('0');
        }
        out.push_back('E');
        out.append(std::to_string(e));
    }
}

}  // namespace

String floatToString(float v) {
    std::string s;
    javaFloatingToString(s, v);
    return String(std::move(s));
}

String doubleToString(double v) {
    std::string s;
    javaFloatingToString(s, v);
    return String(std::move(s));
}

// ---------------------------------------------------------------------------------------
// appenders

namespace detail {

void appendInt(std::string& out, int64_t v) {
    char buf[24];
    auto r = std::to_chars(buf, buf + sizeof buf, v);
    out.append(buf, r.ptr);
}

void appendUInt(std::string& out, uint64_t v) {
    char buf[24];
    auto r = std::to_chars(buf, buf + sizeof buf, v);
    out.append(buf, r.ptr);
}

void appendFloat(std::string& out, float v) { javaFloatingToString(out, v); }
void appendDouble(std::string& out, double v) { javaFloatingToString(out, v); }
void appendChar16(std::string& out, char16_t c) { utf::appendUnit(out, c); }
void appendCodePoint(std::string& out, char32_t c) { utf::encode(out, c); }

void appendObject(std::string& out, Object* o) {
    if (o == nullptr) {
        out.append("null", 4);
        return;
    }
    String s = o->toString();
    if (s.isNull()) {
        out.append("null", 4);
    } else {
        out.append(s);
    }
}

void appendString(std::string& out, const String& s) {
    if (s.isNull()) {
        out.append("null", 4);
    } else {
        out.append(static_cast<const std::string&>(s));
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------------------
// String: construction from arrays

namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return s;
}

enum class Charset { UTF8, LATIN1, ASCII, UTF16LE, UTF16BE, UTF16 };

Charset charsetFor(const String& name) {
    std::string n = lowerAscii(std::string(name));
    n.erase(std::remove(n.begin(), n.end(), '_'), n.end());
    n.erase(std::remove(n.begin(), n.end(), '-'), n.end());
    if (n == "utf8") return Charset::UTF8;
    if (n == "iso88591" || n == "latin1" || n == "iso8859_1" || n == "l1" || n == "cp819") return Charset::LATIN1;
    if (n == "usascii" || n == "ascii") return Charset::ASCII;
    if (n == "utf16le" || n == "unicodelittleunmarked") return Charset::UTF16LE;
    if (n == "utf16be" || n == "unicodebigunmarked") return Charset::UTF16BE;
    if (n == "utf16" || n == "unicode") return Charset::UTF16;
    throw UnsupportedEncodingException(name);
}

String decodeBytes(const int8_t* p, int32_t len, Charset cs) {
    std::string out;
    const auto* b = reinterpret_cast<const unsigned char*>(p);
    switch (cs) {
        case Charset::UTF8: {
            // Validate: malformed input becomes U+FFFD like Java's decoder.
            size_t i = 0, n = static_cast<size_t>(len);
            out.reserve(n);
            while (i < n) {
                if (b[i] < 0x80) {
                    out.push_back(static_cast<char>(b[i++]));
                    continue;
                }
                char32_t cp = utf::decode(reinterpret_cast<const char*>(b), n, i);
                utf::encode(out, cp);
            }
            break;
        }
        case Charset::LATIN1:
            for (int32_t i = 0; i < len; i++) utf::encode(out, b[i]);
            break;
        case Charset::ASCII:
            for (int32_t i = 0; i < len; i++) utf::encode(out, b[i] < 0x80 ? b[i] : 0xFFFD);
            break;
        case Charset::UTF16LE:
        case Charset::UTF16BE:
        case Charset::UTF16: {
            bool le = cs == Charset::UTF16LE;
            int32_t i = 0;
            if (cs == Charset::UTF16 && len >= 2) {
                if (b[0] == 0xFF && b[1] == 0xFE) {
                    le = true;
                    i = 2;
                } else if (b[0] == 0xFE && b[1] == 0xFF) {
                    i = 2;
                }
            }
            for (; i + 1 < len; i += 2) {
                auto u = static_cast<char16_t>(le ? (b[i] | (b[i + 1] << 8)) : ((b[i] << 8) | b[i + 1]));
                utf::appendUnit(out, u);
            }
            if (i < len) utf::encode(out, 0xFFFD);
            break;
        }
    }
    return String(std::move(out));
}

void checkRange(int32_t offset, int32_t count, int32_t length) {
    if (offset < 0 || count < 0 || offset > length - count) {
        throw StringIndexOutOfBoundsException(
            str("offset ", offset, ", count ", count, ", length ", length));
    }
}

String fromChars(const char16_t* p, int32_t n) {
    std::string out;
    out.reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; i++) utf::appendUnit(out, p[i]);
    return String(std::move(out));
}

}  // namespace

String::String(Array<int8_t>* bytes) : String(bytes, 0, bytes == nullptr ? 0 : bytes->length) {}

String::String(Array<int8_t>* bytes, int32_t offset, int32_t length) : null_(false) {
    if (bytes == nullptr) detail::throwNullPointerException();
    checkRange(offset, length, bytes->length);
    *this = decodeBytes(bytes->data() + offset, length, Charset::UTF8);
}

String::String(Array<int8_t>* bytes, const String& charsetName)
    : String(bytes, 0, bytes == nullptr ? 0 : bytes->length, charsetName) {}

String::String(Array<int8_t>* bytes, int32_t offset, int32_t length, const String& charsetName) : null_(false) {
    if (bytes == nullptr) detail::throwNullPointerException();
    checkRange(offset, length, bytes->length);
    *this = decodeBytes(bytes->data() + offset, length, charsetFor(charsetName));
}

String::String(Array<char16_t>* chars) : null_(false) {
    if (chars == nullptr) detail::throwNullPointerException();
    *this = fromChars(chars->data(), chars->length);
}

String::String(Array<char16_t>* chars, int32_t offset, int32_t count) : null_(false) {
    if (chars == nullptr) detail::throwNullPointerException();
    checkRange(offset, count, chars->length);
    *this = fromChars(chars->data() + offset, count);
}

String::String(StringBuilder* sb) : null_(false) {
    if (sb == nullptr) detail::throwNullPointerException();
    std::string::assign(sb->buffer());
}

String String::valueOf(Array<char16_t>* chars) { return String(chars); }
String String::valueOf(Array<char16_t>* chars, int32_t offset, int32_t count) { return String(chars, offset, count); }

String String::fromUtf16(const char16_t* s, size_t n) { return fromChars(s, static_cast<int32_t>(n)); }

std::u16string String::toUtf16() const {
    std::u16string out;
    out.reserve(size());
    size_t i = 0, n = size();
    const char* p = data();
    while (i < n) {
        char32_t cp = utf::decode(p, n, i);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

int32_t String::utf16Length() const noexcept { return utf::utf16Length(data(), size()); }

Array<char16_t>* String::toCharArray() const {
    std::u16string u = toUtf16();
    auto* a = new Array<char16_t>(static_cast<int32_t>(u.size()));
    std::copy(u.begin(), u.end(), a->data());
    return a;
}

Array<int8_t>* String::getBytes() const {
    auto* a = new Array<int8_t>(length());
    std::memcpy(a->data(), data(), size());
    return a;
}

Array<int8_t>* String::getBytes(const String& charsetName) const {
    Charset cs = charsetFor(charsetName);
    std::string out;
    switch (cs) {
        case Charset::UTF8:
            return getBytes();
        case Charset::LATIN1:
        case Charset::ASCII: {
            const char32_t limit = cs == Charset::LATIN1 ? 0x100 : 0x80;
            std::u16string u = toUtf16();
            for (char16_t c : u) out.push_back(c < limit ? static_cast<char>(c) : '?');
            break;
        }
        case Charset::UTF16LE:
        case Charset::UTF16BE:
        case Charset::UTF16: {
            std::u16string u = toUtf16();
            bool le = cs == Charset::UTF16LE;
            if (cs == Charset::UTF16) {
                out.push_back(static_cast<char>(0xFE));
                out.push_back(static_cast<char>(0xFF));
            }
            for (char16_t c : u) {
                char lo = static_cast<char>(c & 0xFF), hi = static_cast<char>(c >> 8);
                if (le) {
                    out.push_back(lo);
                    out.push_back(hi);
                } else {
                    out.push_back(hi);
                    out.push_back(lo);
                }
            }
            break;
        }
    }
    auto* a = new Array<int8_t>(static_cast<int32_t>(out.size()));
    std::memcpy(a->data(), out.data(), out.size());
    return a;
}

// ---------------------------------------------------------------------------------------
// String: queries

char16_t String::charAt(int32_t index) const {
    if (index < 0 || index >= length()) {
        throw StringIndexOutOfBoundsException(str("Index ", index, " out of bounds for length ", length()));
    }
    auto c = static_cast<unsigned char>((*this)[static_cast<size_t>(index)]);
    if (c < 0x80) return c;
    if ((c & 0xC0) == 0x80) return u'\uFFFD';
    size_t i = static_cast<size_t>(index);
    char32_t cp = utf::decode(data(), size(), i);
    if (cp >= 0x10000) return static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10));
    return static_cast<char16_t>(cp);
}

int32_t String::codePointAt(int32_t index) const {
    if (index < 0 || index >= length()) {
        throw StringIndexOutOfBoundsException(str("Index ", index, " out of bounds for length ", length()));
    }
    size_t i = static_cast<size_t>(index);
    return static_cast<int32_t>(utf::decode(data(), size(), i));
}

bool String::equals(const String& o) const noexcept {
    if (o.null_) return null_;
    return static_cast<const std::string&>(*this) == static_cast<const std::string&>(o);
}

bool String::equals(const char* o) const noexcept {
    if (o == nullptr) return false;
    return static_cast<const std::string&>(*this) == o;
}

bool String::equals(Object* o) const {
    auto* b = dynamic_cast<StringBox*>(o);
    return b != nullptr && equals(b->value);
}

namespace {

// Java's case-insensitive char comparison: toUpperCase, then toLowerCase.
inline int32_t foldCase(char32_t c) {
    if (c < 0x80) {
        if (c >= 'A' && c <= 'Z') return static_cast<int32_t>(c + 32);
        return static_cast<int32_t>(c);
    }
    return Character::toLowerCase(Character::toUpperCase(static_cast<int32_t>(c)));
}

int32_t compareIgnoreCaseImpl(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        char32_t ca = utf::decode(a.data(), a.size(), i);
        char32_t cb = utf::decode(b.data(), b.size(), j);
        if (ca != cb) {
            int32_t fa = foldCase(ca), fb = foldCase(cb);
            if (fa != fb) return fa - fb;
        }
    }
    return utf::utf16Length(a.data() + i, a.size() - i) - utf::utf16Length(b.data() + j, b.size() - j);
}

}  // namespace

bool String::equalsIgnoreCase(const String& o) const noexcept {
    if (o.null_) return false;
    if (size() == o.size()) {
        bool ascii = true;
        for (size_t k = 0; k < size(); k++) {
            auto a = static_cast<unsigned char>((*this)[k]), b = static_cast<unsigned char>(o[k]);
            if (a >= 0x80 || b >= 0x80) {
                ascii = false;
                break;
            }
            if (a != b) {
                if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a + 32);
                if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b + 32);
                if (a != b) return false;
            }
        }
        if (ascii) return true;
    }
    return compareIgnoreCaseImpl(*this, o) == 0;
}

int32_t String::compareTo(const String& o) const noexcept {
    const std::string& a = *this;
    const std::string& b = o;
    size_t n = std::min(a.size(), b.size());
    size_t k = 0;
    while (k < n && a[k] == b[k]) k++;
    if (k == n) {
        return utf::utf16Length(a.data() + k, a.size() - k) - utf::utf16Length(b.data() + k, b.size() - k);
    }
    auto ca = static_cast<unsigned char>(a[k]), cb = static_cast<unsigned char>(b[k]);
    if (ca < 0x80 && cb < 0x80) return static_cast<int32_t>(ca) - static_cast<int32_t>(cb);
    // Back up to the start of the (common-prefix-aligned) characters and compare UTF-16 units.
    size_t s = k;
    while (s > 0 && (static_cast<unsigned char>(a[s]) & 0xC0) == 0x80) s--;
    size_t i = s, j = s;
    char32_t x = utf::decode(a.data(), a.size(), i);
    char32_t y = utf::decode(b.data(), b.size(), j);
    auto unit = [](char32_t cp, int which) -> int32_t {
        if (cp < 0x10000) return which == 0 ? static_cast<int32_t>(cp) : -1;
        cp -= 0x10000;
        return which == 0 ? static_cast<int32_t>(0xD800 + (cp >> 10)) : static_cast<int32_t>(0xDC00 + (cp & 0x3FF));
    };
    if (unit(x, 0) != unit(y, 0)) return unit(x, 0) - unit(y, 0);
    return unit(x, 1) - unit(y, 1);
}

int32_t String::compareToIgnoreCase(const String& o) const noexcept { return compareIgnoreCaseImpl(*this, o); }

bool String::regionMatches(int32_t toffset, const String& other, int32_t ooffset, int32_t len) const noexcept {
    return regionMatches(false, toffset, other, ooffset, len);
}

bool String::regionMatches(bool ignoreCase, int32_t toffset, const String& other, int32_t ooffset,
                           int32_t len) const noexcept {
    if (ooffset < 0 || toffset < 0 || toffset > length() - len || ooffset > other.length() - len) return false;
    if (len <= 0) return true;
    String a(std::string_view(*this).substr(static_cast<size_t>(toffset), static_cast<size_t>(len)));
    String b(std::string_view(other).substr(static_cast<size_t>(ooffset), static_cast<size_t>(len)));
    return ignoreCase ? a.equalsIgnoreCase(b) : a.equals(b);
}

bool String::startsWith(const String& prefix) const noexcept {
    return size() >= prefix.size() && std::memcmp(data(), prefix.data(), prefix.size()) == 0;
}

bool String::startsWith(const String& prefix, int32_t toffset) const noexcept {
    if (toffset < 0 || static_cast<size_t>(toffset) > size() || size() - static_cast<size_t>(toffset) < prefix.size()) {
        return false;
    }
    return std::memcmp(data() + toffset, prefix.data(), prefix.size()) == 0;
}

bool String::endsWith(const String& suffix) const noexcept {
    return size() >= suffix.size() && std::memcmp(data() + size() - suffix.size(), suffix.data(), suffix.size()) == 0;
}

namespace {
std::string encodeChar(int32_t ch) {
    std::string s;
    if (ch >= 0 && ch <= 0x10FFFF) utf::encode(s, static_cast<char32_t>(ch));
    return s;
}
}  // namespace

int32_t String::indexOf(int32_t ch) const noexcept { return indexOf(ch, 0); }

int32_t String::indexOf(int32_t ch, int32_t fromIndex) const noexcept {
    if (fromIndex < 0) fromIndex = 0;
    if (fromIndex >= length()) return -1;
    if (ch >= 0 && ch < 0x80) {
        const void* p = std::memchr(data() + fromIndex, ch, size() - static_cast<size_t>(fromIndex));
        return p == nullptr ? -1 : static_cast<int32_t>(static_cast<const char*>(p) - data());
    }
    std::string enc = encodeChar(ch);
    if (enc.empty()) return -1;
    size_t r = std::string::find(enc, static_cast<size_t>(fromIndex));
    return r == npos ? -1 : static_cast<int32_t>(r);
}

int32_t String::indexOf(const String& s) const noexcept { return indexOf(s, 0); }

int32_t String::indexOf(const String& s, int32_t fromIndex) const noexcept {
    if (fromIndex < 0) fromIndex = 0;
    if (fromIndex >= length()) return s.empty() ? length() : -1;
    size_t r = std::string::find(static_cast<const std::string&>(s), static_cast<size_t>(fromIndex));
    return r == npos ? -1 : static_cast<int32_t>(r);
}

int32_t String::lastIndexOf(int32_t ch) const noexcept { return lastIndexOf(ch, length() - 1); }

int32_t String::lastIndexOf(int32_t ch, int32_t fromIndex) const noexcept {
    if (fromIndex < 0) return -1;
    if (fromIndex >= length()) fromIndex = length() - 1;
    std::string enc = encodeChar(ch);
    if (enc.empty()) return -1;
    size_t r = std::string::rfind(enc, static_cast<size_t>(fromIndex));
    return r == npos ? -1 : static_cast<int32_t>(r);
}

int32_t String::lastIndexOf(const String& s) const noexcept { return lastIndexOf(s, length()); }

int32_t String::lastIndexOf(const String& s, int32_t fromIndex) const noexcept {
    if (fromIndex < 0) return -1;
    size_t r = std::string::rfind(static_cast<const std::string&>(s), static_cast<size_t>(fromIndex));
    return r == npos ? -1 : static_cast<int32_t>(r);
}

bool String::contains(const String& s) const noexcept { return std::string::find(static_cast<const std::string&>(s)) != npos; }

String String::substring(int32_t beginIndex) const { return substring(beginIndex, length()); }

String String::substring(int32_t beginIndex, int32_t endIndex) const {
    const int32_t len = length();
    if (beginIndex < 0 || endIndex > len || beginIndex > endIndex) {
        throw StringIndexOutOfBoundsException(str("begin ", beginIndex, ", end ", endIndex, ", length ", len));
    }
    return String(data() + beginIndex, static_cast<size_t>(endIndex - beginIndex));
}

String String::subSequence(int32_t beginIndex, int32_t endIndex) const { return substring(beginIndex, endIndex); }

namespace {
template<bool Upper>
String changeCase(const String& s) {
    std::string out;
    if (utf::isAscii(s)) {
        out = s;
        for (char& c : out) {
            if (Upper) {
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
            } else {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
            }
        }
        return String(std::move(out));
    }
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        char32_t cp = utf::decode(s.data(), s.size(), i);
        // Unconditional multi-char mappings of Java's String case conversion (SpecialCasing).
        if (Upper) {
            const char* special = nullptr;
            switch (cp) {
                case 0xDF: special = "SS"; break;
                case 0x149: special = "\u02BCN"; break;
                case 0xFB00: special = "FF"; break;
                case 0xFB01: special = "FI"; break;
                case 0xFB02: special = "FL"; break;
                case 0xFB03: special = "FFI"; break;
                case 0xFB04: special = "FFL"; break;
                case 0xFB05:
                case 0xFB06: special = "ST"; break;
                default: break;
            }
            if (special != nullptr) {
                out.append(special);
                continue;
            }
        } else if (cp == 0x130) {
            out.append("i\u0307");
            continue;
        }
        int32_t m = Upper ? Character::toUpperCase(static_cast<int32_t>(cp)) : Character::toLowerCase(static_cast<int32_t>(cp));
        utf::encode(out, static_cast<char32_t>(m));
    }
    return String(std::move(out));
}
}  // namespace

String String::toLowerCase() const { return null_ ? String("") : changeCase<false>(*this); }
String String::toLowerCase(Locale*) const { return toLowerCase(); }
String String::toUpperCase() const { return null_ ? String("") : changeCase<true>(*this); }
String String::toUpperCase(Locale*) const { return toUpperCase(); }

String String::trim() const {
    size_t b = 0, e = size();
    while (b < e && static_cast<unsigned char>((*this)[b]) <= ' ') b++;
    while (e > b && static_cast<unsigned char>((*this)[e - 1]) <= ' ') e--;
    return String(data() + b, e - b);
}

String String::concat(const String& s) const {
    String r(null_ ? String("") : *this);
    r.append(s);
    return r;
}

int32_t String::hashCode() const noexcept {
    uint32_t h = 0;
    size_t i = 0, n = size();
    const char* p = data();
    while (i < n) {
        auto c = static_cast<unsigned char>(p[i]);
        if (c < 0x80) {
            h = 31 * h + c;
            i++;
            continue;
        }
        char32_t cp = utf::decode(p, n, i);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            h = 31 * h + (0xD800 + (cp >> 10));
            h = 31 * h + (0xDC00 + (cp & 0x3FF));
        } else {
            h = 31 * h + cp;
        }
    }
    return static_cast<int32_t>(h);
}

String String::replace(char16_t oldChar, char16_t newChar) const {
    if (oldChar == newChar) return null_ ? String("") : *this;
    if (oldChar < 0x80 && newChar < 0x80) {
        std::string out(*this);
        for (char& c : out) {
            if (c == static_cast<char>(oldChar)) c = static_cast<char>(newChar);
        }
        return String(std::move(out));
    }
    std::string out;
    size_t i = 0;
    while (i < size()) {
        char32_t cp = utf::decode(data(), size(), i);
        if (cp == oldChar) {
            utf::appendUnit(out, newChar);
        } else {
            utf::encode(out, cp);
        }
    }
    return String(std::move(out));
}

String String::replace(const String& target, const String& replacement) const {
    std::string out;
    const std::string& s = *this;
    if (target.empty()) {
        // Java: the replacement goes before every char and at the end.
        size_t i = 0;
        out.append(replacement);
        while (i < s.size()) {
            size_t start = i;
            utf::decode(s.data(), s.size(), i);
            out.append(s, start, i - start);
            out.append(replacement);
        }
        return String(std::move(out));
    }
    size_t pos = 0;
    for (;;) {
        size_t f = s.find(target, pos);
        if (f == npos) break;
        out.append(s, pos, f - pos);
        out.append(replacement);
        pos = f + target.size();
    }
    if (pos == 0) return null_ ? String("") : *this;
    out.append(s, pos, npos);
    return String(std::move(out));
}

// ---------------------------------------------------------------------------------------
// regex

namespace detail {

namespace {
const char* posixClass(const std::string& name) {
    static const std::unordered_map<std::string, const char*> classes = {
        {"Lower", "a-z"}, {"Upper", "A-Z"}, {"ASCII", "\\x00-\\x7F"}, {"Alpha", "a-zA-Z"},
        {"Digit", "0-9"}, {"Alnum", "a-zA-Z0-9"}, {"Punct", "!-/:-@\\[-`{-~"},
        {"Graph", "!-~"}, {"Print", " -~"}, {"Blank", " \\t"}, {"Cntrl", "\\x00-\\x1F\\x7F"},
        {"XDigit", "0-9a-fA-F"}, {"Space", " \\t\\n\\x0B\\f\\r"},
        {"L", "a-zA-Z\\u00C0-\\uFFFF"}, {"IsL", "a-zA-Z\\u00C0-\\uFFFF"},
        {"IsAlphabetic", "a-zA-Z\\u00C0-\\uFFFF"}, {"IsLetter", "a-zA-Z\\u00C0-\\uFFFF"},
        {"Lu", "A-Z"}, {"Ll", "a-z"}, {"IsUppercase", "A-Z"}, {"IsLowercase", "a-z"},
        {"N", "0-9"}, {"Nd", "0-9"}, {"IsDigit", "0-9"}, {"javaLowerCase", "a-z"},
        {"javaUpperCase", "A-Z"}, {"javaWhitespace", " \\t\\n\\x0B\\f\\r\\x1C-\\x1F"},
        {"IsWhite_Space", " \\t\\n\\x0B\\f\\r"}, {"Z", " "}, {"Zs", " "}, {"P", "!-/:-@\\[-`{-~"},
    };
    auto it = classes.find(name);
    return it == classes.end() ? nullptr : it->second;
}
}  // namespace

std::string javaRegexToEcma(const std::string& re, bool* icase, bool* dotall, bool* multiline) {
    std::string out;
    out.reserve(re.size() + 8);
    bool inClass = false;
    size_t i = 0;
    const size_t n = re.size();
    // leading inline flags (?i) (?s) (?m) (?is) ...
    while (i + 2 < n && re[i] == '(' && re[i + 1] == '?') {
        size_t j = i + 2;
        bool ok = true;
        std::string flags;
        while (j < n && re[j] != ')') {
            if (re[j] != 'i' && re[j] != 's' && re[j] != 'm' && re[j] != 'u' && re[j] != 'x' && re[j] != 'd') {
                ok = false;
                break;
            }
            flags.push_back(re[j]);
            j++;
        }
        if (!ok || j >= n || flags.empty()) break;
        for (char f : flags) {
            if (f == 'i' && icase) *icase = true;
            if (f == 's' && dotall) *dotall = true;
            if (f == 'm' && multiline) *multiline = true;
        }
        i = j + 1;
    }
    const bool dot = dotall != nullptr && *dotall;
    while (i < n) {
        char c = re[i];
        if (c == '\\' && i + 1 < n) {
            char d = re[i + 1];
            if (d == 'Q') {
                size_t e = re.find("\\E", i + 2);
                std::string lit = re.substr(i + 2, e == std::string::npos ? std::string::npos : e - i - 2);
                for (char lc : lit) {
                    if (std::strchr("\\^$.|?*+()[]{}/-", lc) != nullptr) out.push_back('\\');
                    out.push_back(lc);
                }
                i = e == std::string::npos ? n : e + 2;
                continue;
            }
            if ((d == 'p' || d == 'P') && i + 2 < n) {
                std::string name;
                size_t j = i + 2;
                if (re[j] == '{') {
                    size_t e = re.find('}', j);
                    if (e == std::string::npos) e = n;
                    name = re.substr(j + 1, e - j - 1);
                    j = e + 1;
                } else {
                    name = std::string(1, re[j]);
                    j++;
                }
                if (name.rfind("Is", 0) == 0 && posixClass(name) == nullptr) name = name.substr(2);
                if (name.rfind("In", 0) == 0) name = name.substr(2);
                const char* cls = posixClass(name);
                std::string body = cls != nullptr ? cls : "\\s\\S";
                if (inClass) {
                    out.append(body);  // negation inside a class is not representable
                } else {
                    out.append(d == 'P' ? "[^" : "[").append(body).append("]");
                }
                i = j;
                continue;
            }
            if (d == 'A') {
                out.push_back('^');
                i += 2;
                continue;
            }
            if (d == 'Z' || d == 'z') {
                out.push_back('$');
                i += 2;
                continue;
            }
            if (d == 'h') {
                out.append(inClass ? " \\t\\xA0" : "[ \\t\\xA0]");
                i += 2;
                continue;
            }
            if (d == 'R' && !inClass) {
                out.append("(?:\\r\\n|[\\n\\r\\u2028\\u2029\\x0B\\f\\x85])");
                i += 2;
                continue;
            }
            if (d == 'e') {
                out.append("\\x1B");
                i += 2;
                continue;
            }
            if (d == 'a') {
                out.append("\\x07");
                i += 2;
                continue;
            }
            out.push_back(c);
            out.push_back(d);
            i += 2;
            continue;
        }
        if (inClass) {
            if (c == ']') inClass = false;
            if (c == '[' && i + 1 < n && re[i + 1] != ':') {
                out.append("\\[");  // Java allows nested '[' (union); treat literally
                i++;
                continue;
            }
            out.push_back(c);
            i++;
            continue;
        }
        if (c == '[') {
            inClass = true;
            out.push_back(c);
            i++;
            if (i < n && re[i] == '^') {
                out.push_back('^');
                i++;
            }
            if (i < n && re[i] == ']') {  // leading ']' is literal in Java
                out.append("\\]");
                i++;
            }
            continue;
        }
        if (c == '.' && dot) {
            out.append("[\\s\\S]");
            i++;
            continue;
        }
        if (c == '(' && i + 2 < n && re[i + 1] == '?') {
            if (re[i + 2] == '>') {  // atomic group
                out.append("(?:");
                i += 3;
                continue;
            }
            if (re[i + 2] == '<' && i + 3 < n && re[i + 3] != '=' && re[i + 3] != '!') {  // named group
                size_t e = re.find('>', i + 3);
                out.push_back('(');
                i = e == std::string::npos ? n : e + 1;
                continue;
            }
        }
        if ((c == '*' || c == '+' || c == '?' || c == '}') && i + 1 < n && re[i + 1] == '+') {
            out.push_back(c);  // possessive -> greedy
            i += 2;
            continue;
        }
        out.push_back(c);
        i++;
    }
    return out;
}

const std::regex& compiledRegex(const String& javaRegex) {
    static std::mutex mu;
    static auto* cache = new std::unordered_map<std::string, std::regex*>();
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache->find(javaRegex);
    if (it != cache->end()) return *it->second;
    bool icase = false, dotall = false, multiline = false;
    std::string ecma = javaRegexToEcma(javaRegex, &icase, &dotall, &multiline);
    auto flags = std::regex::ECMAScript;
    if (icase) flags |= std::regex::icase;
    if (multiline) flags |= std::regex::multiline;
    std::regex* re;
    try {
        re = new std::regex(ecma, flags);
    } catch (std::regex_error& e) {
        throw IllegalArgumentException(str(e.what(), " near index 0\n", javaRegex));
    }
    if (cache->size() > 512) cache->clear();
    cache->emplace(std::string(javaRegex), re);
    return *re;
}

// Java Matcher.appendReplacement syntax: $n / ${n} group references, \x escapes.
void appendJavaReplacement(std::string& out, const std::smatch& m, const std::string& repl) {
    for (size_t i = 0; i < repl.size(); i++) {
        char c = repl[i];
        if (c == '\\') {
            i++;
            if (i >= repl.size()) throw IllegalArgumentException(String("character to be escaped is missing"));
            out.push_back(repl[i]);
        } else if (c == '$') {
            i++;
            if (i >= repl.size()) throw IllegalArgumentException(String("Illegal group reference: group index is missing"));
            if (repl[i] == '{') {
                size_t e = repl.find('}', i);
                if (e == std::string::npos) throw IllegalArgumentException(String("named capturing group is missing trailing '}'"));
                std::string name = repl.substr(i + 1, e - i - 1);
                int g = 0;
                for (char d : name) {
                    if (d < '0' || d > '9') throw IllegalArgumentException(str("No group with name {", name, "}"));
                    g = g * 10 + (d - '0');
                }
                if (static_cast<size_t>(g) >= m.size()) throw IndexOutOfBoundsException(str("No group ", g));
                out.append(m[static_cast<size_t>(g)].str());
                i = e;
            } else {
                if (repl[i] < '0' || repl[i] > '9') throw IllegalArgumentException(String("Illegal group reference"));
                size_t g = static_cast<size_t>(repl[i] - '0');
                if (g >= m.size()) throw IndexOutOfBoundsException(str("No group ", static_cast<int32_t>(g)));
                // Java takes further digits while the group number stays valid.
                while (i + 1 < repl.size() && repl[i + 1] >= '0' && repl[i + 1] <= '9') {
                    size_t ng = g * 10 + static_cast<size_t>(repl[i + 1] - '0');
                    if (ng >= m.size()) break;
                    g = ng;
                    i++;
                }
                if (m[g].matched) out.append(m[g].str());
            }
        } else {
            out.push_back(c);
        }
    }
}

namespace {
// Java String.split fast path: a single char that is not a regex metachar, or an escaped
// non-alphanumeric char.
bool splitFastChar(const String& regex, char& ch) {
    if (regex.size() == 1 && std::strchr(".$|()[{^?*+\\", regex[0]) == nullptr) {
        ch = regex[0];
        return static_cast<unsigned char>(ch) < 0x80;
    }
    if (regex.size() == 2 && regex[0] == '\\') {
        char c = regex[1];
        bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!alnum && static_cast<unsigned char>(c) < 0x80) {
            ch = c;
            return true;
        }
    }
    return false;
}
}  // namespace

// A regex without metacharacters matches itself literally.
bool isLiteralRegex(const String& regex) {
    if (regex.empty()) return false;
    for (char c : static_cast<const std::string&>(regex)) {
        if (std::strchr(".$|()[]{}^?*+\\", c) != nullptr) return false;
    }
    return true;
}

std::vector<String> splitToVector(const String& s, const String& regex, int32_t limit) {
    std::vector<String> list;
    const bool limited = limit > 0;
    char ch;
    if (!splitFastChar(regex, ch) && isLiteralRegex(regex)) {
        // Pattern.split with a literal pattern (positive-width, non-overlapping matches).
        const std::string& str = s;
        const std::string& lit = regex;
        size_t index = 0;
        bool matched = false;
        for (size_t f = str.find(lit); f != std::string::npos; f = str.find(lit, f + lit.size())) {
            if (!limited || static_cast<int32_t>(list.size()) < limit - 1) {
                list.push_back(String(str.data() + index, f - index));
                index = f + lit.size();
                matched = true;
            } else {
                list.push_back(String(str.data() + index, str.size() - index));
                index = f + lit.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            list.push_back(s.isNull() ? String("") : s);
            return list;
        }
        if (!limited || static_cast<int32_t>(list.size()) < limit) list.push_back(String(str.data() + index, str.size() - index));
        if (limit == 0) {
            while (!list.empty() && list.back().empty()) list.pop_back();
        }
        return list;
    }
    if (splitFastChar(regex, ch)) {
        size_t off = 0;
        size_t next;
        const std::string& str = s;
        while ((next = str.find(ch, off)) != std::string::npos) {
            if (!limited || static_cast<int32_t>(list.size()) < limit - 1) {
                list.push_back(String(str.data() + off, next - off));
                off = next + 1;
            } else {
                list.push_back(String(str.data() + off, str.size() - off));
                off = str.size();
                break;
            }
        }
        if (off == 0) {
            list.clear();
            list.push_back(s.isNull() ? String("") : s);
            return list;
        }
        if (!limited || static_cast<int32_t>(list.size()) < limit) list.push_back(String(str.data() + off, str.size() - off));
    } else {
        const std::regex& re = compiledRegex(regex);
        const std::string& str = s;
        size_t index = 0;
        bool matchedAny = false;
        for (auto it = std::sregex_iterator(str.begin(), str.end(), re); it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            size_t mStart = static_cast<size_t>(m.position(0));
            size_t mEnd = mStart + static_cast<size_t>(m.length(0));
            if (!limited || static_cast<int32_t>(list.size()) < limit - 1) {
                if (index == 0 && mStart == 0 && mStart == mEnd) continue;  // no leading empty string
                list.push_back(String(str.data() + index, mStart - index));
                index = mEnd;
                matchedAny = true;
            } else if (static_cast<int32_t>(list.size()) == limit - 1) {
                list.push_back(String(str.data() + index, str.size() - index));
                index = mEnd;
                matchedAny = true;
            }
        }
        if (index == 0 && !matchedAny) {
            list.clear();
            list.push_back(s.isNull() ? String("") : s);
            return list;
        }
        if (!limited || static_cast<int32_t>(list.size()) < limit) list.push_back(String(str.data() + index, str.size() - index));
    }
    if (limit == 0) {
        while (!list.empty() && list.back().empty()) list.pop_back();
    }
    return list;
}

}  // namespace detail

Array<String>* String::split(const String& regex) const { return split(regex, 0); }

Array<String>* String::split(const String& regex, int32_t limit) const {
    std::vector<String> v = detail::splitToVector(*this, regex, limit);
    auto* a = new Array<String>(static_cast<int32_t>(v.size()));
    for (size_t i = 0; i < v.size(); i++) a->data()[i] = std::move(v[i]);
    return a;
}

namespace {
String regexReplace(const String& s, const String& regex, const String& replacement, bool all) {
    if (detail::isLiteralRegex(regex) && replacement.find_first_of("$\\") == std::string::npos) {
        // literal pattern and replacement: no regex engine needed
        const std::string& str = s;
        size_t f = str.find(regex);
        if (f == std::string::npos) return s.isNull() ? String("") : s;
        if (all) return s.replace(regex, replacement);
        std::string out(str, 0, f);
        out.append(replacement).append(str, f + regex.size(), std::string::npos);
        return String(std::move(out));
    }
    const std::regex& re = detail::compiledRegex(regex);
    const std::string& str = s;
    std::string out;
    size_t last = 0;
    bool any = false;
    for (auto it = std::sregex_iterator(str.begin(), str.end(), re); it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        size_t mStart = static_cast<size_t>(m.position(0));
        out.append(str, last, mStart - last);
        detail::appendJavaReplacement(out, m, replacement);
        last = mStart + static_cast<size_t>(m.length(0));
        any = true;
        if (!all) break;
    }
    if (!any) return s.isNull() ? String("") : s;
    out.append(str, last, std::string::npos);
    return String(std::move(out));
}
}  // namespace

String String::replaceAll(const String& regex, const String& replacement) const {
    return regexReplace(*this, regex, replacement, true);
}

String String::replaceFirst(const String& regex, const String& replacement) const {
    return regexReplace(*this, regex, replacement, false);
}

bool String::matches(const String& regex) const {
    const std::regex& re = detail::compiledRegex(regex);
    return std::regex_match(static_cast<const std::string&>(*this), re);
}

// ---------------------------------------------------------------------------------------
// StringBuilder

StringBuilder::StringBuilder(int32_t capacity) {
    if (capacity < 0) throw NegativeArraySizeException(str(capacity));
    buf_.reserve(static_cast<size_t>(capacity));
}

StringBuilder* StringBuilder::append(Array<char16_t>* chars) {
    if (chars == nullptr) detail::throwNullPointerException();
    for (char16_t c : *chars) utf::appendUnit(buf_, c);
    return this;
}

StringBuilder* StringBuilder::append(Array<char16_t>* chars, int32_t offset, int32_t len) {
    if (chars == nullptr) detail::throwNullPointerException();
    if (offset < 0 || len < 0 || offset > chars->length - len) {
        throw IndexOutOfBoundsException(str("offset ", offset, ", count ", len, ", length ", chars->length));
    }
    for (int32_t i = 0; i < len; i++) utf::appendUnit(buf_, chars->data()[offset + i]);
    return this;
}

StringBuilder* StringBuilder::append(const String& s, int32_t start, int32_t end) {
    const String& src = s.isNull() ? String("null") : s;
    if (start < 0 || start > end || end > src.length()) {
        throw IndexOutOfBoundsException(str("start ", start, ", end ", end, ", length ", src.length()));
    }
    buf_.append(src.data() + start, static_cast<size_t>(end - start));
    return this;
}

StringBuilder* StringBuilder::appendCodePoint(int32_t cp) {
    if (cp < 0 || cp > 0x10FFFF) throw IllegalArgumentException(str("Not a valid Unicode code point: 0x", Integer::toHexString(cp)));
    utf::encode(buf_, static_cast<char32_t>(cp));
    return this;
}

StringBuilder* StringBuilder::insertRaw(int32_t offset, const std::string& s) {
    if (offset < 0 || offset > length()) throw StringIndexOutOfBoundsException(str("offset ", offset, ", length ", length()));
    buf_.insert(static_cast<size_t>(offset), s);
    return this;
}

char16_t StringBuilder::charAt(int32_t index) {
    if (index < 0 || index >= length()) {
        throw StringIndexOutOfBoundsException(str("index ", index, ",length ", length()));
    }
    auto c = static_cast<unsigned char>(buf_[static_cast<size_t>(index)]);
    if (c < 0x80) return c;
    if ((c & 0xC0) == 0x80) return u'\uFFFD';
    size_t i = static_cast<size_t>(index);
    char32_t cp = utf::decode(buf_.data(), buf_.size(), i);
    if (cp >= 0x10000) return static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10));
    return static_cast<char16_t>(cp);
}

void StringBuilder::setCharAt(int32_t index, char16_t ch) {
    if (index < 0 || index >= length()) {
        throw StringIndexOutOfBoundsException(str("index ", index, ",length ", length()));
    }
    if (ch < 0x80 && static_cast<unsigned char>(buf_[static_cast<size_t>(index)]) < 0x80) {
        buf_[static_cast<size_t>(index)] = static_cast<char>(ch);
        return;
    }
    // Replace the whole character starting at index by the UTF-8 encoding of ch.
    size_t i = static_cast<size_t>(index);
    size_t j = i;
    utf::decode(buf_.data(), buf_.size(), j);
    std::string enc;
    utf::encode(enc, ch);
    buf_.replace(i, j - i, enc);
}

StringBuilder* StringBuilder::deleteCharAt(int32_t index) {
    if (index < 0 || index >= length()) {
        throw StringIndexOutOfBoundsException(str("index ", index, ",length ", length()));
    }
    size_t i = static_cast<size_t>(index);
    size_t j = i;
    if (static_cast<unsigned char>(buf_[i]) < 0x80 || (static_cast<unsigned char>(buf_[i]) & 0xC0) == 0x80) {
        j = i + 1;
    } else {
        utf::decode(buf_.data(), buf_.size(), j);
    }
    buf_.erase(i, j - i);
    return this;
}

StringBuilder* StringBuilder::delete_(int32_t start, int32_t end) {
    const int32_t len = length();
    if (end > len) end = len;
    if (start < 0 || start > end) {
        throw StringIndexOutOfBoundsException(str("start ", start, ", end ", end, ", length ", len));
    }
    buf_.erase(static_cast<size_t>(start), static_cast<size_t>(end - start));
    return this;
}

StringBuilder* StringBuilder::replace(int32_t start, int32_t end, const String& s) {
    const int32_t len = length();
    if (start < 0 || start > len || start > end) {
        throw StringIndexOutOfBoundsException(str("start ", start, ", end ", end, ", length ", len));
    }
    if (end > len) end = len;
    buf_.replace(static_cast<size_t>(start), static_cast<size_t>(end - start), s);
    return this;
}

StringBuilder* StringBuilder::reverse() {
    // Reverse by characters (code points) so the result stays valid UTF-8.
    std::vector<std::pair<size_t, size_t>> chars;
    size_t i = 0;
    while (i < buf_.size()) {
        size_t s = i;
        utf::decode(buf_.data(), buf_.size(), i);
        chars.emplace_back(s, i - s);
    }
    std::string out;
    out.reserve(buf_.size());
    for (auto it = chars.rbegin(); it != chars.rend(); ++it) out.append(buf_, it->first, it->second);
    buf_.swap(out);
    return this;
}

void StringBuilder::setLength(int32_t newLength) {
    if (newLength < 0) throw StringIndexOutOfBoundsException(newLength);
    buf_.resize(static_cast<size_t>(newLength), '\0');
}

int32_t StringBuilder::indexOf(const String& s) { return indexOf(s, 0); }
int32_t StringBuilder::indexOf(const String& s, int32_t fromIndex) {
    if (fromIndex < 0) fromIndex = 0;
    if (fromIndex > length()) return s.empty() ? length() : -1;
    size_t r = buf_.find(s, static_cast<size_t>(fromIndex));
    return r == std::string::npos ? -1 : static_cast<int32_t>(r);
}
int32_t StringBuilder::lastIndexOf(const String& s) { return lastIndexOf(s, length()); }
int32_t StringBuilder::lastIndexOf(const String& s, int32_t fromIndex) {
    if (fromIndex < 0) return -1;
    size_t r = buf_.rfind(s, static_cast<size_t>(fromIndex));
    return r == std::string::npos ? -1 : static_cast<int32_t>(r);
}

String StringBuilder::substring(int32_t start) { return substring(start, length()); }
String StringBuilder::substring(int32_t start, int32_t end) {
    if (start < 0 || end > length() || start > end) {
        throw StringIndexOutOfBoundsException(str("start ", start, ", end ", end, ", length ", length()));
    }
    return String(buf_.data() + start, static_cast<size_t>(end - start));
}

void StringBuilder::ensureCapacity(int32_t minimumCapacity) {
    if (minimumCapacity > 0) buf_.reserve(static_cast<size_t>(minimumCapacity));
}

String StringBuilder::toString() { return String(buf_); }

// ---------------------------------------------------------------------------------------
// StringCharacterIterator

StringCharacterIterator::StringCharacterIterator(const String& text) : StringCharacterIterator(text, 0) {}
StringCharacterIterator::StringCharacterIterator(const String& text, int32_t pos) : text_(text.toUtf16()), pos_(pos) {
    if (pos < 0 || pos > getEndIndex()) throw IllegalArgumentException(String("Invalid position"));
}
void StringCharacterIterator::setText(const String& text) {
    text_ = text.toUtf16();
    pos_ = 0;
}
char16_t StringCharacterIterator::first() {
    pos_ = 0;
    return current();
}
char16_t StringCharacterIterator::last() {
    pos_ = text_.empty() ? 0 : static_cast<int32_t>(text_.size()) - 1;
    return current();
}
char16_t StringCharacterIterator::current() {
    if (pos_ >= 0 && pos_ < static_cast<int32_t>(text_.size())) return text_[static_cast<size_t>(pos_)];
    return DONE;
}
char16_t StringCharacterIterator::next() {
    if (pos_ < static_cast<int32_t>(text_.size()) - 1) {
        pos_++;
        return text_[static_cast<size_t>(pos_)];
    }
    pos_ = static_cast<int32_t>(text_.size());
    return DONE;
}
char16_t StringCharacterIterator::previous() {
    if (pos_ > 0) {
        pos_--;
        return text_[static_cast<size_t>(pos_)];
    }
    return DONE;
}
char16_t StringCharacterIterator::setIndex(int32_t position) {
    if (position < 0 || position > static_cast<int32_t>(text_.size())) throw IllegalArgumentException(String("Invalid index"));
    pos_ = position;
    return current();
}

// ---------------------------------------------------------------------------------------
// StringUtils

bool StringUtils::isBlank(const String& s) {
    if (s.isNull()) return true;
    size_t i = 0;
    while (i < s.size()) {
        char32_t cp = utf::decode(s.data(), s.size(), i);
        if (!Character::isWhitespace(static_cast<int32_t>(cp))) return false;
    }
    return true;
}

String StringUtils::capitalize(const String& s) {
    if (s.isNull() || s.isEmpty()) return s;
    size_t i = 0;
    char32_t cp = utf::decode(s.data(), s.size(), i);
    std::string out;
    utf::encode(out, static_cast<char32_t>(Character::toUpperCase(static_cast<int32_t>(cp))));
    out.append(s, i, std::string::npos);
    return String(std::move(out));
}

}  // namespace jlang
