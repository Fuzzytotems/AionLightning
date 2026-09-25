// jlang/IOCharset.cpp - java.nio.charset.Charset and the byte <-> UTF-16 codecs with the JDK's
// malformed/unmappable replacement rules (sun.nio.cs.UTF_8, ISO_8859_1, US_ASCII, UTF_16*).
#include <jlang/IOBase.h>
#include <jlang/Nio.h>

#include <cstring>
#include <string>

namespace jlang {

namespace {

constexpr char16_t kRepl = 0xFFFD;

inline bool isCont(uint8_t b) { return (b & 0xc0) == 0x80; }
inline bool isSurrogate(char16_t c) { return c >= 0xD800 && c <= 0xDFFF; }
inline bool isHigh(char16_t c) { return c >= 0xD800 && c <= 0xDBFF; }
inline bool isLow(char16_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// Decodes UTF-8 from p[0..n). Returns the number of bytes consumed; when !endOfInput a trailing
// incomplete (but so far valid) sequence is left unconsumed.
size_t decodeUtf8(const uint8_t* p, size_t n, std::u16string& out, bool endOfInput) {
    size_t i = 0;
    while (i < n) {
        uint8_t b1 = p[i];
        if (b1 < 0x80) {
            out.push_back(b1);
            i++;
            continue;
        }
        size_t rem = n - i;
        if (b1 >= 0xC2 && b1 <= 0xDF) {  // 2 bytes
            if (rem < 2) {
                if (!endOfInput) return i;
                out.push_back(kRepl);
                return n;
            }
            if (!isCont(p[i + 1])) {
                out.push_back(kRepl);
                i += 1;
                continue;
            }
            out.push_back(static_cast<char16_t>(((b1 & 0x1f) << 6) | (p[i + 1] & 0x3f)));
            i += 2;
            continue;
        }
        if ((b1 & 0xF0) == 0xE0) {  // 3 bytes
            if (rem < 3) {
                if (rem > 1) {
                    uint8_t b2 = p[i + 1];
                    if ((b1 == 0xE0 && (b2 & 0xe0) == 0x80) || !isCont(b2)) {
                        out.push_back(kRepl);
                        i += 1;
                        continue;
                    }
                }
                if (!endOfInput) return i;
                out.push_back(kRepl);
                return n;
            }
            uint8_t b2 = p[i + 1], b3 = p[i + 2];
            if ((b1 == 0xE0 && (b2 & 0xe0) == 0x80) || !isCont(b2) || !isCont(b3)) {
                // malformedN(3)
                size_t len = ((b1 == 0xE0 && (b2 & 0xe0) == 0x80) || !isCont(b2)) ? 1 : 2;
                out.push_back(kRepl);
                i += len;
                continue;
            }
            char16_t c = static_cast<char16_t>(((b1 & 0x0f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f));
            if (isSurrogate(c)) {
                out.push_back(kRepl);
                i += 3;
                continue;
            }
            out.push_back(c);
            i += 3;
            continue;
        }
        if ((b1 & 0xF8) == 0xF0) {  // 4 bytes (F0..F7)
            if (rem < 4) {
                if (b1 > 0xF4 || (rem > 1 && ((b1 == 0xF0 && (p[i + 1] < 0x90 || p[i + 1] > 0xBF)) ||
                                              (b1 == 0xF4 && (p[i + 1] & 0xF0) != 0x80) || !isCont(p[i + 1])))) {
                    out.push_back(kRepl);
                    i += 1;
                    continue;
                }
                if (rem > 2 && !isCont(p[i + 2])) {
                    out.push_back(kRepl);
                    i += 2;
                    continue;
                }
                if (!endOfInput) return i;
                out.push_back(kRepl);
                return n;
            }
            uint8_t b2 = p[i + 1], b3 = p[i + 2], b4 = p[i + 3];
            uint32_t uc = (static_cast<uint32_t>(b1 & 0x07) << 18) | (static_cast<uint32_t>(b2 & 0x3f) << 12) |
                          (static_cast<uint32_t>(b3 & 0x3f) << 6) | (b4 & 0x3f);
            if (!isCont(b2) || !isCont(b3) || !isCont(b4) || uc < 0x10000 || uc > 0x10FFFF) {
                // malformedN(4)
                size_t len;
                if (b1 > 0xF4 || (b1 == 0xF0 && (b2 < 0x90 || b2 > 0xBF)) || (b1 == 0xF4 && (b2 & 0xF0) != 0x80) ||
                    !isCont(b2))
                    len = 1;
                else if (!isCont(b3))
                    len = 2;
                else
                    len = 3;
                out.push_back(kRepl);
                i += len;
                continue;
            }
            uc -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 | (uc >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 | (uc & 0x3ff)));
            i += 4;
            continue;
        }
        // 80..C1, F8..FF
        out.push_back(kRepl);
        i += 1;
    }
    return i;
}

size_t decodeUtf16(const uint8_t* p, size_t n, std::u16string& out, bool endOfInput, bool big) {
    size_t i = 0;
    auto rd = [&](size_t k) -> char16_t {
        return big ? static_cast<char16_t>((p[k] << 8) | p[k + 1]) : static_cast<char16_t>((p[k + 1] << 8) | p[k]);
    };
    while (n - i >= 2) {
        char16_t c = rd(i);
        if (c == 0xFFFE) {  // reversed mark
            out.push_back(kRepl);
            i += 2;
            continue;
        }
        if (isSurrogate(c)) {
            if (isHigh(c)) {
                if (n - i < 4) break;
                char16_t c2 = rd(i + 2);
                if (!isLow(c2)) {
                    out.push_back(kRepl);
                    i += 4;
                    continue;
                }
                out.push_back(c);
                out.push_back(c2);
                i += 4;
                continue;
            }
            out.push_back(kRepl);  // unpaired low surrogate
            i += 2;
            continue;
        }
        out.push_back(c);
        i += 2;
    }
    if (i < n && endOfInput) {
        out.push_back(kRepl);
        return n;
    }
    return i;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

void putUnit(std::string& out, char16_t c, bool big) {
    if (big) {
        out.push_back(static_cast<char>(c >> 8));
        out.push_back(static_cast<char>(c & 0xff));
    } else {
        out.push_back(static_cast<char>(c & 0xff));
        out.push_back(static_cast<char>(c >> 8));
    }
}

// Encodes one "character" (a code point, or a malformed lone surrogate = cp 0xFFFFFFFF).
void encodeOne(int32_t kind, uint32_t cp, char16_t unit, char16_t unit2, int nunits, std::string& out) {
    const bool malformed = cp == 0xFFFFFFFFu;
    switch (kind) {
        case Charset::UTF_8_KIND:
            if (malformed) out.push_back('?');
            else appendUtf8(out, cp);
            break;
        case Charset::ISO_8859_1_KIND:
            out.push_back(!malformed && cp <= 0xFF ? static_cast<char>(cp) : '?');
            break;
        case Charset::US_ASCII_KIND:
            out.push_back(!malformed && cp <= 0x7F ? static_cast<char>(cp) : '?');
            break;
        default: {
            const bool big = kind != Charset::UTF_16LE_KIND;
            if (malformed) {
                putUnit(out, kRepl, big);
            } else {
                putUnit(out, unit, big);
                if (nunits == 2) putUnit(out, unit2, big);
            }
            break;
        }
    }
}

}  // namespace

// =======================================================================================
// detail codecs

namespace detail {

void CharDecoderState::decode(const uint8_t* p, std::size_t n, std::u16string& out, bool endOfInput) {
    // Prepend carried bytes.
    std::string buf;
    const uint8_t* src = p;
    size_t len = n;
    if (npend > 0) {
        buf.assign(reinterpret_cast<const char*>(pend), static_cast<size_t>(npend));
        buf.append(reinterpret_cast<const char*>(p), n);
        src = reinterpret_cast<const uint8_t*>(buf.data());
        len = buf.size();
        npend = 0;
    }
    size_t used = 0;
    switch (kind) {
        case Charset::UTF_8_KIND:
            used = decodeUtf8(src, len, out, endOfInput);
            break;
        case Charset::ISO_8859_1_KIND:
            for (size_t i = 0; i < len; i++) out.push_back(src[i]);
            used = len;
            break;
        case Charset::US_ASCII_KIND:
            for (size_t i = 0; i < len; i++) out.push_back(src[i] < 0x80 ? static_cast<char16_t>(src[i]) : kRepl);
            used = len;
            break;
        case Charset::UTF_16BE_KIND:
            used = decodeUtf16(src, len, out, endOfInput, true);
            break;
        case Charset::UTF_16LE_KIND:
            used = decodeUtf16(src, len, out, endOfInput, false);
            break;
        default: {  // UTF-16 with BOM detection
            size_t start = 0;
            if (utf16Order == 0) {
                if (len < 2) {
                    if (!endOfInput) break;
                    if (len == 1) out.push_back(kRepl);
                    used = len;
                    break;
                }
                if (src[0] == 0xFE && src[1] == 0xFF) {
                    utf16Order = 1;
                    start = 2;
                } else if (src[0] == 0xFF && src[1] == 0xFE) {
                    utf16Order = 2;
                    start = 2;
                } else {
                    utf16Order = 1;
                }
            }
            used = start + decodeUtf16(src + start, len - start, out, endOfInput, utf16Order == 1);
            break;
        }
    }
    size_t left = len - used;
    if (left > 0) {
        // Only possible when !endOfInput: at most 3 bytes of an incomplete sequence.
        if (left > sizeof pend) left = sizeof pend;
        std::memcpy(pend, src + (len - left), left);
        npend = static_cast<int32_t>(left);
    }
}

void CharEncoderState::encode(const char16_t* p, std::size_t n, std::string& out, bool endOfInput) {
    if (kind == Charset::UTF_16_KIND && !bomWritten && (n > 0 || pendingHigh != 0)) {
        out.push_back(static_cast<char>(0xFE));
        out.push_back(static_cast<char>(0xFF));
        bomWritten = true;
    }
    size_t i = 0;
    if (pendingHigh != 0) {
        if (n == 0) {
            if (endOfInput) {
                encodeOne(kind, 0xFFFFFFFFu, 0, 0, 1, out);
                pendingHigh = 0;
            }
            return;
        }
        char16_t hi = pendingHigh;
        pendingHigh = 0;
        if (isLow(p[0])) {
            uint32_t cp = 0x10000 + ((static_cast<uint32_t>(hi) - 0xD800) << 10) + (p[0] - 0xDC00);
            encodeOne(kind, cp, hi, p[0], 2, out);
            i = 1;
        } else {
            encodeOne(kind, 0xFFFFFFFFu, 0, 0, 1, out);
        }
    }
    for (; i < n; i++) {
        char16_t c = p[i];
        if (!isSurrogate(c)) {
            if (kind == Charset::UTF_8_KIND && c < 0x80) out.push_back(static_cast<char>(c));
            else encodeOne(kind, c, c, 0, 1, out);
            continue;
        }
        if (isHigh(c)) {
            if (i + 1 < n) {
                if (isLow(p[i + 1])) {
                    uint32_t cp = 0x10000 + ((static_cast<uint32_t>(c) - 0xD800) << 10) + (p[i + 1] - 0xDC00);
                    encodeOne(kind, cp, c, p[i + 1], 2, out);
                    i++;
                    continue;
                }
                encodeOne(kind, 0xFFFFFFFFu, 0, 0, 1, out);
                continue;
            }
            if (!endOfInput) {
                pendingHigh = c;
                return;
            }
            encodeOne(kind, 0xFFFFFFFFu, 0, 0, 1, out);
            continue;
        }
        encodeOne(kind, 0xFFFFFFFFu, 0, 0, 1, out);  // lone low surrogate
    }
}

std::u16string decodeBytes(int32_t kind, const uint8_t* p, std::size_t n) {
    CharDecoderState d;
    d.kind = kind;
    std::u16string out;
    out.reserve(n);
    d.decode(p, n, out, true);
    return out;
}

std::string encodeChars(int32_t kind, const char16_t* p, std::size_t n) {
    CharEncoderState e;
    e.kind = kind;
    std::string out;
    out.reserve(n);
    e.encode(p, n, out, true);
    return out;
}

namespace {
// Valid UTF-8 without surrogate code points (so the text is identical to Java's UTF-8 bytes).
bool isPlainUtf8(const std::string& s) {
    const auto* p = reinterpret_cast<const uint8_t*>(s.data());
    size_t n = s.size(), i = 0;
    while (i < n) {
        uint8_t b = p[i];
        if (b < 0x80) {
            i++;
        } else if (b >= 0xC2 && b <= 0xDF) {
            if (i + 1 >= n || !isCont(p[i + 1])) return false;
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 2 >= n || !isCont(p[i + 1]) || !isCont(p[i + 2])) return false;
            if (b == 0xE0 && p[i + 1] < 0xA0) return false;
            if (b == 0xED && p[i + 1] >= 0xA0) return false;  // surrogate
            i += 3;
        } else if (b >= 0xF0 && b <= 0xF4) {
            if (i + 3 >= n || !isCont(p[i + 1]) || !isCont(p[i + 2]) || !isCont(p[i + 3])) return false;
            if (b == 0xF0 && p[i + 1] < 0x90) return false;
            if (b == 0xF4 && p[i + 1] >= 0x90) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}
bool isAscii(const std::string& s) {
    for (unsigned char c : s)
        if (c >= 0x80) return false;
    return true;
}
}  // namespace

std::string encodeString(int32_t kind, const String& s) {
    const std::string& raw = s;
    if (kind == Charset::UTF_8_KIND && isPlainUtf8(raw)) return raw;
    if ((kind == Charset::ISO_8859_1_KIND || kind == Charset::US_ASCII_KIND) && isAscii(raw)) return raw;
    std::u16string u = s.toUtf16();
    return encodeChars(kind, u.data(), u.size());
}

String decodeToString(int32_t kind, const uint8_t* p, std::size_t n) {
    if (kind == Charset::UTF_8_KIND || kind == Charset::US_ASCII_KIND || kind == Charset::ISO_8859_1_KIND) {
        bool ascii = true;
        for (size_t i = 0; i < n; i++)
            if (p[i] >= 0x80) {
                ascii = false;
                break;
            }
        if (ascii) return String(reinterpret_cast<const char*>(p), n);
        if (kind == Charset::UTF_8_KIND) {
            std::string tmp(reinterpret_cast<const char*>(p), n);
            if (isPlainUtf8(tmp)) return String(std::move(tmp));
        }
    }
    std::u16string u = decodeBytes(kind, p, n);
    return String::fromUtf16(u);
}

}  // namespace detail

// =======================================================================================
// Charset

namespace {

struct CharsetEntry {
    Charset::Kind kind;
    const char* name;
    const char* historical;
    const char* aliases[12];
};

const CharsetEntry kCharsets[] = {
    {Charset::UTF_8_KIND, "UTF-8", "UTF8", {"UTF8", "unicode-1-1-utf-8", nullptr}},
    {Charset::ISO_8859_1_KIND,
     "ISO-8859-1",
     "ISO8859_1",
     {"iso-ir-100", "ISO_8859-1", "latin1", "l1", "IBM819", "cp819", "csISOLatin1", "819", "IBM-819",
      "ISO8859_1", "ISO_8859-1:1987", nullptr}},
    {Charset::US_ASCII_KIND,
     "US-ASCII",
     "ASCII",
     {"iso-ir-6", "ANSI_X3.4-1986", "ISO_646.irv:1991", "ASCII", "ISO646-US", "us", "IBM367", "cp367",
      "csASCII", "default", "646", nullptr}},
    {Charset::UTF_16BE_KIND, "UTF-16BE", "UnicodeBigUnmarked", {"UTF_16BE", "ISO-10646-UCS-2", "X-UTF-16BE", "UnicodeBigUnmarked", nullptr}},
    {Charset::UTF_16LE_KIND, "UTF-16LE", "UnicodeLittleUnmarked", {"UTF_16LE", "X-UTF-16LE", "UnicodeLittleUnmarked", nullptr}},
    {Charset::UTF_16_KIND, "UTF-16", "UTF-16", {"UTF_16", "utf16", "unicode", "UnicodeBig", nullptr}},
};

bool equalsIgnoreCaseAscii(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

// Charset.checkName
void checkName(const String& s) {
    const std::string& n = s;
    if (n.empty()) throw IllegalCharsetNameException(s);
    for (size_t i = 0; i < n.size(); i++) {
        char c = n[i];
        if (c >= 'A' && c <= 'Z') continue;
        if (c >= 'a' && c <= 'z') continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '-' && i != 0) continue;
        if (c == '+' && i != 0) continue;
        if (c == ':' && i != 0) continue;
        if (c == '_' && i != 0) continue;
        if (c == '.' && i != 0) continue;
        throw IllegalCharsetNameException(s);
    }
}

Charset* instance(int idx) {
    static Charset* table[6] = {
        new Charset(kCharsets[0].kind, kCharsets[0].name), new Charset(kCharsets[1].kind, kCharsets[1].name),
        new Charset(kCharsets[2].kind, kCharsets[2].name), new Charset(kCharsets[3].kind, kCharsets[3].name),
        new Charset(kCharsets[4].kind, kCharsets[4].name), new Charset(kCharsets[5].kind, kCharsets[5].name),
    };
    return table[idx];
}

int findCharset(const String& name) {
    const std::string& n = name;
    for (int i = 0; i < 6; i++) {
        if (equalsIgnoreCaseAscii(n, kCharsets[i].name)) return i;
        for (const char* const* a = kCharsets[i].aliases; *a != nullptr; a++)
            if (equalsIgnoreCaseAscii(n, *a)) return i;
    }
    return -1;
}

}  // namespace

Charset::Charset(Kind kind, const char* name) : kind_(kind), name_(name) {}

Charset* Charset::lookup(const String& charsetName) {
    if (charsetName.isNull()) throw IllegalArgumentException(String("Null charset name"));
    int i = findCharset(charsetName);
    if (i >= 0) return instance(i);
    checkName(charsetName);
    return nullptr;
}

Charset* Charset::forName(const String& charsetName) {
    Charset* cs = lookup(charsetName);
    if (cs == nullptr) throw UnsupportedCharsetException(charsetName);
    return cs;
}

Charset* Charset::forNameIO(const String& charsetName) {
    if (charsetName.isNull()) throw NullPointerException(String("charsetName"));
    int i = findCharset(charsetName);
    if (i < 0) throw UnsupportedEncodingException(charsetName);
    return instance(i);
}

bool Charset::isSupported(const String& charsetName) { return lookup(charsetName) != nullptr; }

Charset* Charset::defaultCharset() { return instance(0); }

String Charset::historicalName() {
    for (const auto& e : kCharsets)
        if (e.kind == kind_) return String(e.historical);
    return name_;
}

ByteBuffer* Charset::encode(const String& s) {
    std::string bytes = detail::encodeString(kind_, s);
    auto* a = new Array<int8_t>(static_cast<int32_t>(bytes.size()));
    if (!bytes.empty()) std::memcpy(a->data(), bytes.data(), bytes.size());
    return ByteBuffer::wrap(a);
}

CharBuffer* Charset::decode(ByteBuffer* bb) {
    int32_t n = bb->remaining();
    auto* tmp = new Array<int8_t>(n);
    bb->get(tmp);
    std::u16string u = detail::decodeBytes(kind_, reinterpret_cast<const uint8_t*>(tmp->data()), static_cast<size_t>(n));
    auto* chars = new Array<char16_t>(static_cast<int32_t>(u.size()));
    if (!u.empty()) std::memcpy(chars->data(), u.data(), u.size() * sizeof(char16_t));
    return CharBuffer::wrap(chars);
}

Array<int8_t>* Charset::encodeToArray(const String& s) {
    std::string bytes = detail::encodeString(kind_, s);
    auto* a = new Array<int8_t>(static_cast<int32_t>(bytes.size()));
    if (!bytes.empty()) std::memcpy(a->data(), bytes.data(), bytes.size());
    return a;
}

String Charset::decodeToString(const int8_t* bytes, int32_t length) {
    return detail::decodeToString(kind_, reinterpret_cast<const uint8_t*>(bytes), static_cast<size_t>(length));
}

String Charset::decodeToString(Array<int8_t>* bytes, int32_t offset, int32_t length) {
    if (offset < 0 || length < 0 || offset > bytes->length - length)
        throw IndexOutOfBoundsException(str("Range [", offset, ", ", offset, " + ", length,
                                            ") out of bounds for length ", bytes->length));
    return decodeToString(bytes->data() + offset, length);
}

int32_t Charset::compareTo(Charset* that) { return name_.compareToIgnoreCase(that->name_); }

bool Charset::equals(Object* o) {
    if (o == static_cast<Object*>(this)) return true;
    Charset* c = dynamic_cast<Charset*>(o);
    return c != nullptr && name_.equals(c->name_);
}

Charset* StandardCharsets::UTF_8() { return instance(0); }
Charset* StandardCharsets::ISO_8859_1() { return instance(1); }
Charset* StandardCharsets::US_ASCII() { return instance(2); }
Charset* StandardCharsets::UTF_16BE() { return instance(3); }
Charset* StandardCharsets::UTF_16LE() { return instance(4); }
Charset* StandardCharsets::UTF_16() { return instance(5); }

}  // namespace jlang
