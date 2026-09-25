// Boxed primitives and their static utilities (java.lang.Integer, Long, ... Character).
#include <jlang/Boxes.h>
#include <jlang/Exceptions.h>
#include <jlang/String.h>
#include <jlang/Util.h>

#include "StringInternal.h"

#include <bit>
#include <cerrno>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <locale.h>
#include <mutex>
#include <string>
#include <wctype.h>

namespace jlang {

namespace {

[[noreturn]] void nfeNull() { throw NumberFormatException(String("Cannot parse null string: null")); }

void checkRadix(int32_t radix) {
    if (radix < Character::MIN_RADIX) {
        throw NumberFormatException(str("radix ", radix, " less than Character.MIN_RADIX"));
    }
    if (radix > Character::MAX_RADIX) {
        throw NumberFormatException(str("radix ", radix, " greater than Character.MAX_RADIX"));
    }
}

// Java's parseInt/parseLong loop (accumulates negatively to reach MIN_VALUE).
template<class T>
T parseIntegral(const String& s, int32_t radix) {
    if (s.isNull()) nfeNull();
    checkRadix(radix);
    const size_t len = s.size();
    if (len == 0) detail::throwNumberFormatForInput(s, radix);
    bool negative = false;
    size_t i = 0;
    T limit = -std::numeric_limits<T>::max();
    const auto first = static_cast<unsigned char>(s[0]);
    if (first < '0') {
        if (first == '-') {
            negative = true;
            limit = std::numeric_limits<T>::min();
        } else if (first != '+') {
            detail::throwNumberFormatForInput(s, radix);
        }
        if (len == 1) detail::throwNumberFormatForInput(s, radix);
        i++;
    }
    const T multmin = limit / radix;
    T result = 0;
    while (i < len) {
        auto c = static_cast<unsigned char>(s[i]);
        int32_t digit;
        if (c < 0x80) {
            digit = Character::digit(c, radix);
            i++;
        } else {
            char32_t cp = utf::decode(s.data(), len, i);
            digit = Character::digit(static_cast<int32_t>(cp), radix);
        }
        if (digit < 0 || result < multmin) detail::throwNumberFormatForInput(s, radix);
        result *= radix;
        if (result < limit + digit) detail::throwNumberFormatForInput(s, radix);
        result -= digit;
    }
    return negative ? result : -result;
}

template<class T>
T decodeIntegral(const String& nm) {
    if (nm.isNull()) nfeNull();
    if (nm.isEmpty()) throw NumberFormatException(String("Zero length string"));
    int32_t radix = 10;
    int32_t index = 0;
    bool negative = false;
    char first = nm[0];
    if (first == '-') {
        negative = true;
        index++;
    } else if (first == '+') {
        index++;
    }
    if (nm.startsWith("0x", index) || nm.startsWith("0X", index)) {
        index += 2;
        radix = 16;
    } else if (nm.startsWith("#", index)) {
        index++;
        radix = 16;
    } else if (nm.startsWith("0", index) && nm.length() > 1 + index) {
        index++;
        radix = 8;
    }
    if (nm.startsWith("-", index) || nm.startsWith("+", index)) {
        throw NumberFormatException(String("Sign character in wrong position"));
    }
    try {
        T result = parseIntegral<T>(nm.substring(index), radix);
        return negative ? static_cast<T>(-result) : result;
    } catch (NumberFormatException&) {
        // MIN_VALUE: the magnitude alone overflows; parse again with the sign.
        String constant = negative ? str("-", nm.substring(index)) : nm.substring(index);
        return parseIntegral<T>(constant, radix);
    }
}

template<class U>
String unsignedToString(U v, int shift) {
    const U mask = static_cast<U>((1u << shift) - 1);
    char buf[70];
    int pos = 70;
    do {
        buf[--pos] = "0123456789abcdef"[static_cast<int>(v & mask)];
        v = static_cast<U>(v >> shift);
    } while (v != 0);
    return String(buf + pos, static_cast<size_t>(70 - pos));
}

template<class T>
String signedToString(T v, int32_t radix) {
    if (radix < Character::MIN_RADIX || radix > Character::MAX_RADIX) radix = 10;
    char buf[72];
    auto r = std::to_chars(buf, buf + sizeof buf, v, radix);
    return String(buf, static_cast<size_t>(r.ptr - buf));
}

void rangeError(const String& s, int32_t radix) {
    throw NumberFormatException(str("Value out of range. Value:\"", s, "\" Radix:", radix));
}

// ---- floating-point parsing (Java FloatingDecimal.readJavaFormatString syntax)

bool isJavaWhitespaceTrim(char c) { return static_cast<unsigned char>(c) <= ' '; }

// Returns the cleaned text for from_chars, or sets special (NaN/Inf) and returns empty.
template<class F>
F parseFloating(const String& input) {
    if (input.isNull()) throw NullPointerException();
    std::string s(input);
    size_t b = 0, e = s.size();
    while (b < e && isJavaWhitespaceTrim(s[b])) b++;
    while (e > b && isJavaWhitespaceTrim(s[e - 1])) e--;
    s = s.substr(b, e - b);
    if (s.empty()) throw NumberFormatException(String("empty String"));
    size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = s[0] == '-';
        i = 1;
    }
    std::string rest = s.substr(i);
    auto bad = [&]() { detail::throwNumberFormatForInput(input); };
    if (rest == "NaN") return std::numeric_limits<F>::quiet_NaN();
    if (rest == "Infinity") return neg ? -std::numeric_limits<F>::infinity() : std::numeric_limits<F>::infinity();
    // optional type suffix
    if (!rest.empty() && std::strchr("fFdD", rest.back()) != nullptr) {
        bool hex = rest.size() > 1 && rest[0] == '0' && (rest[1] == 'x' || rest[1] == 'X');
        if (!hex || rest.find_first_of("pP") != std::string::npos) rest.pop_back();
    }
    if (rest.empty()) bad();
    F value{};
    if (rest.size() > 2 && rest[0] == '0' && (rest[1] == 'x' || rest[1] == 'X')) {
        std::string h = rest.substr(2);
        size_t p = h.find_first_of("pP");
        if (p == std::string::npos) bad();  // Java requires the binary exponent
        // validate: hexdigits [. hexdigits] p [sign] digits
        bool anyDigit = false;
        size_t k = 0;
        for (; k < p; k++) {
            if (std::isxdigit(static_cast<unsigned char>(h[k]))) {
                anyDigit = true;
            } else if (h[k] != '.') {
                bad();
            }
        }
        if (!anyDigit) bad();
        size_t q = p + 1;
        if (q < h.size() && (h[q] == '+' || h[q] == '-')) q++;
        if (q >= h.size()) bad();
        for (; q < h.size(); q++) {
            if (!std::isdigit(static_cast<unsigned char>(h[q]))) bad();
        }
        auto r = std::from_chars(h.data(), h.data() + h.size(), value, std::chars_format::hex);
        if (r.ptr != h.data() + h.size()) {
            if (r.ec != std::errc::result_out_of_range) bad();
            value = static_cast<F>(std::strtod(("0x" + h).c_str(), nullptr));
        }
    } else {
        // validate: digits [. digits] [eE [sign] digits], at least one mantissa digit
        size_t k = 0;
        bool anyDigit = false;
        while (k < rest.size() && std::isdigit(static_cast<unsigned char>(rest[k]))) {
            k++;
            anyDigit = true;
        }
        if (k < rest.size() && rest[k] == '.') {
            k++;
            while (k < rest.size() && std::isdigit(static_cast<unsigned char>(rest[k]))) {
                k++;
                anyDigit = true;
            }
        }
        if (!anyDigit) bad();
        if (k < rest.size() && (rest[k] == 'e' || rest[k] == 'E')) {
            k++;
            if (k < rest.size() && (rest[k] == '+' || rest[k] == '-')) k++;
            if (k >= rest.size() || !std::isdigit(static_cast<unsigned char>(rest[k]))) bad();
            while (k < rest.size() && std::isdigit(static_cast<unsigned char>(rest[k]))) k++;
        }
        if (k != rest.size()) bad();
        auto r = std::from_chars(rest.data(), rest.data() + rest.size(), value, std::chars_format::general);
        if (r.ec == std::errc::result_out_of_range) {
            // overflow -> Infinity, underflow -> 0 (strtod rounds correctly)
            double d = std::strtod(rest.c_str(), nullptr);
            if constexpr (std::is_same_v<F, float>) {
                value = std::strtof(rest.c_str(), nullptr);
                (void)d;
            } else {
                value = d;
            }
        } else if (r.ptr != rest.data() + rest.size()) {
            bad();
        }
    }
    return neg ? -value : value;
}

template<class F, class Bits>
String hexFloatString(F v) {
    constexpr int mantBits = std::is_same_v<F, float> ? 23 : 52;
    constexpr int bias = std::is_same_v<F, float> ? 127 : 1023;
    if (v != v) return String("NaN");
    if (std::isinf(v)) return String(v > 0 ? "Infinity" : "-Infinity");
    Bits bits = std::bit_cast<Bits>(v);
    std::string out;
    if (bits >> (sizeof(Bits) * 8 - 1)) out.push_back('-');
    Bits mag = bits & ((Bits{1} << (sizeof(Bits) * 8 - 1)) - 1);
    if (mag == 0) return String(out + "0x0.0p0");
    int exp = static_cast<int>(mag >> mantBits);
    Bits frac = mag & ((Bits{1} << mantBits) - 1);
    const bool subnormal = exp == 0;
    // Java prints the significand in hex, 6 digits for float (shifted left by 1) and 13 for double.
    constexpr int hexDigits = std::is_same_v<F, float> ? 6 : 13;
    Bits shifted = std::is_same_v<F, float> ? static_cast<Bits>(frac << 1) : frac;
    std::string h;
    for (int k = hexDigits - 1; k >= 0; k--) h.push_back("0123456789abcdef"[(shifted >> (4 * k)) & 0xF]);
    while (h.size() > 1 && h.back() == '0') h.pop_back();
    out += subnormal ? "0x0." : "0x1.";
    out += h;
    out += "p";
    out += std::to_string(subnormal ? 1 - bias : exp - bias);
    return String(out);
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Integer

Integer::Integer(const String& s) : value(parseInt(s, 10)) {}
bool Integer::equals(Object* o) {
    auto* x = dynamic_cast<Integer*>(o);
    return x != nullptr && x->value == value;
}
String Integer::toString() { return toString(value); }

Integer* Integer::valueOf(int32_t v) {
    static Integer** cache = [] {
        auto** c = new Integer*[256];
        for (int k = 0; k < 256; k++) c[k] = new Integer(k - 128);
        return c;
    }();
    if (v >= -128 && v <= 127) return cache[v + 128];
    return new Integer(v);
}
Integer* Integer::valueOf(const String& s) { return valueOf(parseInt(s, 10)); }
Integer* Integer::valueOf(const String& s, int32_t radix) { return valueOf(parseInt(s, radix)); }
int32_t Integer::parseInt(const String& s) { return parseIntegral<int32_t>(s, 10); }
int32_t Integer::parseInt(const String& s, int32_t radix) { return parseIntegral<int32_t>(s, radix); }
Integer* Integer::decode(const String& s) { return valueOf(decodeIntegral<int32_t>(s)); }
String Integer::toString(int32_t v) { return signedToString(v, 10); }
String Integer::toString(int32_t v, int32_t radix) { return signedToString(v, radix); }
String Integer::toHexString(int32_t v) { return unsignedToString(static_cast<uint32_t>(v), 4); }
String Integer::toOctalString(int32_t v) { return unsignedToString(static_cast<uint32_t>(v), 3); }
String Integer::toBinaryString(int32_t v) { return unsignedToString(static_cast<uint32_t>(v), 1); }
int32_t Integer::bitCount(int32_t v) { return std::popcount(static_cast<uint32_t>(v)); }
int32_t Integer::reverse(int32_t v) {
    uint32_t x = static_cast<uint32_t>(v);
    x = ((x & 0x55555555u) << 1) | ((x >> 1) & 0x55555555u);
    x = ((x & 0x33333333u) << 2) | ((x >> 2) & 0x33333333u);
    x = ((x & 0x0F0F0F0Fu) << 4) | ((x >> 4) & 0x0F0F0F0Fu);
    return reverseBytes(static_cast<int32_t>(x));
}
int32_t Integer::reverseBytes(int32_t v) { return static_cast<int32_t>(__builtin_bswap32(static_cast<uint32_t>(v))); }
int32_t Integer::highestOneBit(int32_t v) {
    return v == 0 ? 0 : static_cast<int32_t>(uint32_t{1} << (31 - std::countl_zero(static_cast<uint32_t>(v))));
}
int32_t Integer::numberOfLeadingZeros(int32_t v) { return std::countl_zero(static_cast<uint32_t>(v)); }
int32_t Integer::numberOfTrailingZeros(int32_t v) { return std::countr_zero(static_cast<uint32_t>(v)); }
int32_t Integer::rotateLeft(int32_t v, int32_t d) { return static_cast<int32_t>(std::rotl(static_cast<uint32_t>(v), d & 31)); }
int32_t Integer::rotateRight(int32_t v, int32_t d) { return static_cast<int32_t>(std::rotr(static_cast<uint32_t>(v), d & 31)); }

// ---------------------------------------------------------------------------------------
// Long

Long::Long(const String& s) : value(parseLong(s, 10)) {}
bool Long::equals(Object* o) {
    auto* x = dynamic_cast<Long*>(o);
    return x != nullptr && x->value == value;
}
String Long::toString() { return toString(value); }
Long* Long::valueOf(int64_t v) {
    static Long** cache = [] {
        auto** c = new Long*[256];
        for (int k = 0; k < 256; k++) c[k] = new Long(k - 128);
        return c;
    }();
    if (v >= -128 && v <= 127) return cache[v + 128];
    return new Long(v);
}
Long* Long::valueOf(const String& s) { return valueOf(parseLong(s, 10)); }
Long* Long::valueOf(const String& s, int32_t radix) { return valueOf(parseLong(s, radix)); }
int64_t Long::parseLong(const String& s) { return parseIntegral<int64_t>(s, 10); }
int64_t Long::parseLong(const String& s, int32_t radix) { return parseIntegral<int64_t>(s, radix); }
Long* Long::decode(const String& s) { return valueOf(decodeIntegral<int64_t>(s)); }
String Long::toString(int64_t v) { return signedToString(v, 10); }
String Long::toString(int64_t v, int32_t radix) { return signedToString(v, radix); }
String Long::toHexString(int64_t v) { return unsignedToString(static_cast<uint64_t>(v), 4); }
String Long::toOctalString(int64_t v) { return unsignedToString(static_cast<uint64_t>(v), 3); }
String Long::toBinaryString(int64_t v) { return unsignedToString(static_cast<uint64_t>(v), 1); }
int32_t Long::bitCount(int64_t v) { return std::popcount(static_cast<uint64_t>(v)); }
int64_t Long::reverse(int64_t v) {
    uint64_t x = static_cast<uint64_t>(v);
    x = ((x & 0x5555555555555555ull) << 1) | ((x >> 1) & 0x5555555555555555ull);
    x = ((x & 0x3333333333333333ull) << 2) | ((x >> 2) & 0x3333333333333333ull);
    x = ((x & 0x0F0F0F0F0F0F0F0Full) << 4) | ((x >> 4) & 0x0F0F0F0F0F0F0F0Full);
    return reverseBytes(static_cast<int64_t>(x));
}
int64_t Long::reverseBytes(int64_t v) { return static_cast<int64_t>(__builtin_bswap64(static_cast<uint64_t>(v))); }
int64_t Long::highestOneBit(int64_t v) {
    return v == 0 ? 0 : static_cast<int64_t>(uint64_t{1} << (63 - std::countl_zero(static_cast<uint64_t>(v))));
}
int32_t Long::numberOfLeadingZeros(int64_t v) { return std::countl_zero(static_cast<uint64_t>(v)); }
int32_t Long::numberOfTrailingZeros(int64_t v) { return std::countr_zero(static_cast<uint64_t>(v)); }
int64_t Long::rotateLeft(int64_t v, int32_t d) { return static_cast<int64_t>(std::rotl(static_cast<uint64_t>(v), d & 63)); }
int64_t Long::rotateRight(int64_t v, int32_t d) { return static_cast<int64_t>(std::rotr(static_cast<uint64_t>(v), d & 63)); }

// ---------------------------------------------------------------------------------------
// Short / Byte

Short::Short(const String& s) : value(parseShort(s, 10)) {}
bool Short::equals(Object* o) {
    auto* x = dynamic_cast<Short*>(o);
    return x != nullptr && x->value == value;
}
String Short::toString() { return toString(value); }
Short* Short::valueOf(int16_t v) {
    static Short** cache = [] {
        auto** c = new Short*[256];
        for (int k = 0; k < 256; k++) c[k] = new Short(static_cast<int16_t>(k - 128));
        return c;
    }();
    if (v >= -128 && v <= 127) return cache[v + 128];
    return new Short(v);
}
Short* Short::valueOf(const String& s) { return valueOf(parseShort(s, 10)); }
Short* Short::valueOf(const String& s, int32_t radix) { return valueOf(parseShort(s, radix)); }
int16_t Short::parseShort(const String& s) { return parseShort(s, 10); }
int16_t Short::parseShort(const String& s, int32_t radix) {
    int32_t i = Integer::parseInt(s, radix);
    if (i < MIN_VALUE || i > MAX_VALUE) rangeError(s, radix);
    return static_cast<int16_t>(i);
}
Short* Short::decode(const String& s) {
    int32_t i = Integer::decode(s)->value;
    if (i < MIN_VALUE || i > MAX_VALUE) {
        throw NumberFormatException(str("Value ", i, " out of range from input ", s));
    }
    return valueOf(static_cast<int16_t>(i));
}
String Short::toString(int16_t v) { return Integer::toString(v); }
int16_t Short::reverseBytes(int16_t v) {
    auto u = static_cast<uint16_t>(v);
    return static_cast<int16_t>(static_cast<uint16_t>((u << 8) | (u >> 8)));
}

Byte::Byte(const String& s) : value(parseByte(s, 10)) {}
bool Byte::equals(Object* o) {
    auto* x = dynamic_cast<Byte*>(o);
    return x != nullptr && x->value == value;
}
String Byte::toString() { return toString(value); }
Byte* Byte::valueOf(int8_t v) {
    static Byte** cache = [] {
        auto** c = new Byte*[256];
        for (int k = 0; k < 256; k++) c[k] = new Byte(static_cast<int8_t>(k - 128));
        return c;
    }();
    return cache[v + 128];
}
Byte* Byte::valueOf(const String& s) { return valueOf(parseByte(s, 10)); }
Byte* Byte::valueOf(const String& s, int32_t radix) { return valueOf(parseByte(s, radix)); }
int8_t Byte::parseByte(const String& s) { return parseByte(s, 10); }
int8_t Byte::parseByte(const String& s, int32_t radix) {
    int32_t i = Integer::parseInt(s, radix);
    if (i < MIN_VALUE || i > MAX_VALUE) rangeError(s, radix);
    return static_cast<int8_t>(i);
}
Byte* Byte::decode(const String& s) {
    int32_t i = Integer::decode(s)->value;
    if (i < MIN_VALUE || i > MAX_VALUE) {
        throw NumberFormatException(str("Value ", i, " out of range from input ", s));
    }
    return valueOf(static_cast<int8_t>(i));
}
String Byte::toString(int8_t v) { return Integer::toString(v); }

// ---------------------------------------------------------------------------------------
// Float / Double

Float::Float(const String& s) : value(parseFloat(s)) {}
int32_t Float::intValue() { return f2i(value); }
int64_t Float::longValue() { return f2l(value); }
bool Float::equals(Object* o) {
    auto* x = dynamic_cast<Float*>(o);
    return x != nullptr && floatToIntBits(x->value) == floatToIntBits(value);
}
String Float::toString() { return floatToString(value); }
Float* Float::valueOf(const String& s) { return new Float(parseFloat(s)); }
float Float::parseFloat(const String& s) { return parseFloating<float>(s); }
String Float::toString(float v) { return floatToString(v); }
String Float::toHexString(float v) { return hexFloatString<float, uint32_t>(v); }
int32_t Float::floatToIntBits(float v) {
    if (v != v) return 0x7fc00000;
    return std::bit_cast<int32_t>(v);
}
int32_t Float::floatToRawIntBits(float v) { return std::bit_cast<int32_t>(v); }
float Float::intBitsToFloat(int32_t bits) { return std::bit_cast<float>(bits); }
int32_t Float::compare(float a, float b) {
    if (a < b) return -1;
    if (a > b) return 1;
    int32_t x = floatToIntBits(a), y = floatToIntBits(b);
    return x == y ? 0 : (x < y ? -1 : 1);
}
float Float::max(float a, float b) {
    if (a != a) return a;
    if (a == 0.0f && b == 0.0f && std::signbit(a)) return b;
    return a >= b ? a : b;
}
float Float::min(float a, float b) {
    if (a != a) return a;
    if (a == 0.0f && b == 0.0f && std::signbit(b)) return b;
    return a <= b ? a : b;
}

Double::Double(const String& s) : value(parseDouble(s)) {}
int32_t Double::intValue() { return d2i(value); }
int64_t Double::longValue() { return d2l(value); }
bool Double::equals(Object* o) {
    auto* x = dynamic_cast<Double*>(o);
    return x != nullptr && doubleToLongBits(x->value) == doubleToLongBits(value);
}
String Double::toString() { return doubleToString(value); }
Double* Double::valueOf(const String& s) { return new Double(parseDouble(s)); }
double Double::parseDouble(const String& s) { return parseFloating<double>(s); }
String Double::toString(double v) { return doubleToString(v); }
String Double::toHexString(double v) { return hexFloatString<double, uint64_t>(v); }
int64_t Double::doubleToLongBits(double v) {
    if (v != v) return 0x7ff8000000000000LL;
    return std::bit_cast<int64_t>(v);
}
int64_t Double::doubleToRawLongBits(double v) { return std::bit_cast<int64_t>(v); }
double Double::longBitsToDouble(int64_t bits) { return std::bit_cast<double>(bits); }
int32_t Double::compare(double a, double b) {
    if (a < b) return -1;
    if (a > b) return 1;
    int64_t x = doubleToLongBits(a), y = doubleToLongBits(b);
    return x == y ? 0 : (x < y ? -1 : 1);
}
double Double::max(double a, double b) {
    if (a != a) return a;
    if (a == 0.0 && b == 0.0 && std::signbit(a)) return b;
    return a >= b ? a : b;
}
double Double::min(double a, double b) {
    if (a != a) return a;
    if (a == 0.0 && b == 0.0 && std::signbit(b)) return b;
    return a <= b ? a : b;
}

// ---------------------------------------------------------------------------------------
// Boolean / Character / StringBox

bool Boolean::equals(Object* o) {
    auto* x = dynamic_cast<Boolean*>(o);
    return x != nullptr && x->value == value;
}

bool Character::equals(Object* o) {
    auto* x = dynamic_cast<Character*>(o);
    return x != nullptr && x->value == value;
}

Character* Character::valueOf(char16_t c) {
    static Character** cache = [] {
        auto** t = new Character*[128];
        for (int k = 0; k < 128; k++) t[k] = new Character(static_cast<char16_t>(k));
        return t;
    }();
    if (c < 128) return cache[c];
    return new Character(c);
}

String Character::toString(char16_t c) {
    std::string s;
    utf::appendUnit(s, c);
    return String(std::move(s));
}

bool StringBox::equals(Object* o) {
    auto* x = dynamic_cast<StringBox*>(o);
    return x != nullptr && x->value.equals(value);
}

namespace {

locale_t utf8Locale() {
    static locale_t loc = [] {
        locale_t l = newlocale(LC_CTYPE_MASK, "C.UTF-8", static_cast<locale_t>(nullptr));
        if (l == static_cast<locale_t>(nullptr)) l = newlocale(LC_CTYPE_MASK, "C.utf8", static_cast<locale_t>(nullptr));
        if (l == static_cast<locale_t>(nullptr)) l = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", static_cast<locale_t>(nullptr));
        return l;
    }();
    return loc;
}

// Unicode decimal digit (Nd) blocks in the BMP: each starts at the listed zero.
const int32_t kDigitZeros[] = {0x0030, 0x0660, 0x06F0, 0x07C0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66,
                               0x0BE6, 0x0C66, 0x0CE6, 0x0D66, 0x0DE6, 0x0E50, 0x0ED0, 0x0F20, 0x1040,
                               0x1090, 0x17E0, 0x1810, 0x1946, 0x19D0, 0x1A80, 0x1A90, 0x1B50, 0x1BB0,
                               0x1C40, 0x1C50, 0xA620, 0xA8D0, 0xA900, 0xA9D0, 0xA9F0, 0xAA50, 0xABF0,
                               0xFF10};

int32_t decimalDigitValue(int32_t cp) {
    if (cp >= '0' && cp <= '9') return cp - '0';
    if (cp < 0x660) return -1;
    for (int32_t z : kDigitZeros) {
        if (cp >= z && cp < z + 10) return cp - z;
    }
    return -1;
}

}  // namespace

bool Character::isDigit(int32_t cp) { return decimalDigitValue(cp) >= 0; }

bool Character::isLetter(int32_t cp) {
    if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    if (isDigit(cp)) return false;
    locale_t l = utf8Locale();
    if (l != static_cast<locale_t>(nullptr)) return iswalpha_l(static_cast<wint_t>(cp), l) != 0;
    return (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7) || (cp >= 0x370 && cp <= 0x52F);
}

bool Character::isLetterOrDigit(int32_t cp) { return isLetter(cp) || isDigit(cp); }
bool Character::isAlphabetic(int32_t cp) { return isLetter(cp); }

bool Character::isUpperCase(int32_t cp) {
    if (cp < 0x80) return cp >= 'A' && cp <= 'Z';
    locale_t l = utf8Locale();
    if (l != static_cast<locale_t>(nullptr)) return iswupper_l(static_cast<wint_t>(cp), l) != 0;
    return toLowerCase(cp) != cp;
}

bool Character::isLowerCase(int32_t cp) {
    if (cp < 0x80) return cp >= 'a' && cp <= 'z';
    locale_t l = utf8Locale();
    if (l != static_cast<locale_t>(nullptr)) return iswlower_l(static_cast<wint_t>(cp), l) != 0;
    return toUpperCase(cp) != cp;
}

bool Character::isWhitespace(int32_t cp) {
    switch (cp) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        case 0x1C: case 0x1D: case 0x1E: case 0x1F: case 0x20:
        case 0x1680: case 0x2028: case 0x2029: case 0x205F: case 0x3000:
            return true;
        default:
            return (cp >= 0x2000 && cp <= 0x2006) || (cp >= 0x2008 && cp <= 0x200A);
    }
}

bool Character::isSpaceChar(int32_t cp) {
    return cp == 0x20 || cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 ||
           cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

bool Character::isJavaIdentifierStart(int32_t cp) { return isLetter(cp) || cp == '_' || cp == '$'; }
bool Character::isJavaIdentifierPart(int32_t cp) { return isLetterOrDigit(cp) || cp == '_' || cp == '$'; }

int32_t Character::toUpperCase(int32_t cp) {
    if (cp < 0x80) return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
    if (cp == 0xB5) return 0x39C;  // micro sign
    locale_t l = utf8Locale();
    if (l != static_cast<locale_t>(nullptr)) return static_cast<int32_t>(towupper_l(static_cast<wint_t>(cp), l));
    if ((cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) || (cp >= 0x3B1 && cp <= 0x3C9 && cp != 0x3C2) || (cp >= 0x430 && cp <= 0x44F)) return cp - 32;
    if (cp >= 0x450 && cp <= 0x45F) return cp - 80;
    return cp;
}

int32_t Character::toLowerCase(int32_t cp) {
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
    locale_t l = utf8Locale();
    if (l != static_cast<locale_t>(nullptr)) return static_cast<int32_t>(towlower_l(static_cast<wint_t>(cp), l));
    if ((cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) || (cp >= 0x391 && cp <= 0x3A9) || (cp >= 0x410 && cp <= 0x42F)) return cp + 32;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 80;
    return cp;
}

int32_t Character::digit(int32_t cp, int32_t radix) {
    if (radix < MIN_RADIX || radix > MAX_RADIX) return -1;
    int32_t v = -1;
    if (cp >= '0' && cp <= '9') {
        v = cp - '0';
    } else if (cp >= 'a' && cp <= 'z') {
        v = cp - 'a' + 10;
    } else if (cp >= 'A' && cp <= 'Z') {
        v = cp - 'A' + 10;
    } else if (cp >= 0xFF21 && cp <= 0xFF3A) {
        v = cp - 0xFF21 + 10;
    } else if (cp >= 0xFF41 && cp <= 0xFF5A) {
        v = cp - 0xFF41 + 10;
    } else {
        v = decimalDigitValue(cp);
    }
    return v < radix ? v : -1;
}

int32_t Character::getNumericValue(int32_t cp) {
    int32_t v = digit(cp, MAX_RADIX);
    return v;
}

char16_t Character::forDigit(int32_t digit, int32_t radix) {
    if (digit >= radix || digit < 0) return u'\0';
    if (radix < MIN_RADIX || radix > MAX_RADIX) return u'\0';
    if (digit < 10) return static_cast<char16_t>(u'0' + digit);
    return static_cast<char16_t>(u'a' - 10 + digit);
}

}  // namespace jlang
