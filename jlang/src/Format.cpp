// java.util.Formatter subset behind jlang::String::format / PrintStream::printf.
#include <jlang/Boxes.h>
#include <jlang/Exceptions.h>
#include <jlang/String.h>

#include "StringInternal.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <string>

namespace jlang::detail {

namespace {

struct Spec {
    int index = -1;  // explicit 1-based argument index, 0 = relative '<', -1 = ordinary
    bool left = false, alt = false, plus = false, space = false, zero = false, group = false, paren = false;
    int width = -1;
    int precision = -1;
    char conv = 0;
    bool upper = false;
    std::string text;  // the specifier as written (for error messages)
};

[[noreturn]] void formatError(const std::string& kind, const std::string& detail) {
    throw IllegalArgumentException(String(kind + ": " + detail));
}

const char* javaTypeName(const FormatArg& a) {
    switch (a.kind) {
        case FormatArg::BOOL: return "java.lang.Boolean";
        case FormatArg::INT:
        case FormatArg::UINT:
            switch (a.bits) {
                case 8: return "java.lang.Byte";
                case 16: return "java.lang.Short";
                case 64: return "java.lang.Long";
                default: return "java.lang.Integer";
            }
        case FormatArg::FLOAT: return "java.lang.Float";
        case FormatArg::DOUBLE: return "java.lang.Double";
        case FormatArg::CHAR: return "java.lang.Character";
        case FormatArg::STR: return "java.lang.String";
        case FormatArg::OBJ: return "java.lang.Object";
        default: return "null";
    }
}

[[noreturn]] void conversionMismatch(char conv, const FormatArg& a) {
    formatError("IllegalFormatConversionException",
                std::string(1, conv) + " != " + (a.kind == FormatArg::OBJ ? std::string(a.o->getClass()->getName()) : javaTypeName(a)));
}

// Unboxes Integer/Long/.../Character/Boolean/StringBox objects into primitive kinds.
FormatArg normalize(const FormatArg& a) {
    if (a.kind != FormatArg::OBJ) return a;
    FormatArg r;
    Object* o = a.o;
    if (auto* v = dynamic_cast<Integer*>(o)) {
        r.kind = FormatArg::INT; r.i = v->value; r.bits = 32;
    } else if (auto* v = dynamic_cast<Long*>(o)) {
        r.kind = FormatArg::INT; r.i = v->value; r.bits = 64;
    } else if (auto* v = dynamic_cast<Short*>(o)) {
        r.kind = FormatArg::INT; r.i = v->value; r.bits = 16;
    } else if (auto* v = dynamic_cast<Byte*>(o)) {
        r.kind = FormatArg::INT; r.i = v->value; r.bits = 8;
    } else if (auto* v = dynamic_cast<Float*>(o)) {
        r.kind = FormatArg::FLOAT; r.d = v->value;
    } else if (auto* v = dynamic_cast<Double*>(o)) {
        r.kind = FormatArg::DOUBLE; r.d = v->value;
    } else if (auto* v = dynamic_cast<Character*>(o)) {
        r.kind = FormatArg::CHAR; r.c = v->value;
    } else if (auto* v = dynamic_cast<Boolean*>(o)) {
        r.kind = FormatArg::BOOL; r.b = v->value;
    } else if (auto* v = dynamic_cast<StringBox*>(o)) {
        r.kind = FormatArg::STR; r.s = v->value;
    } else {
        return a;
    }
    return r;
}

int32_t displayLength(const std::string& s) { return utf::utf16Length(s.data(), s.size()); }

void pad(std::string& out, const std::string& body, const Spec& sp) {
    int32_t len = displayLength(body);
    if (sp.width <= len) {
        out.append(body);
        return;
    }
    std::string fill(static_cast<size_t>(sp.width - len), ' ');
    if (sp.left) {
        out.append(body).append(fill);
    } else {
        out.append(fill).append(body);
    }
}

std::string upperAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    }
    return s;
}

std::string toJavaString(const FormatArg& a) {
    switch (a.kind) {
        case FormatArg::NUL: return "null";
        case FormatArg::BOOL: return a.b ? "true" : "false";
        case FormatArg::INT: return std::to_string(a.i);
        case FormatArg::UINT: return std::to_string(a.u);
        case FormatArg::FLOAT: return std::string(floatToString(static_cast<float>(a.d)));
        case FormatArg::DOUBLE: return std::string(doubleToString(a.d));
        case FormatArg::CHAR: {
            std::string s;
            utf::encode(s, a.c);
            return s;
        }
        case FormatArg::STR: return std::string(a.s);
        case FormatArg::OBJ: {
            String s = a.o->toString();
            return s.isNull() ? std::string("null") : std::string(s);
        }
    }
    return "null";
}

int32_t javaHashCode(const FormatArg& a) {
    switch (a.kind) {
        case FormatArg::BOOL: return a.b ? 1231 : 1237;
        case FormatArg::INT: return a.bits == 64 ? Long::hashCode(a.i) : static_cast<int32_t>(a.i);
        case FormatArg::UINT: return Long::hashCode(static_cast<int64_t>(a.u));
        case FormatArg::FLOAT: return Float::floatToIntBits(static_cast<float>(a.d));
        case FormatArg::DOUBLE: return Double::hashCode(a.d);
        case FormatArg::CHAR: return static_cast<int32_t>(a.c);
        case FormatArg::STR: return a.s.hashCode();
        case FormatArg::OBJ: return a.o->hashCode();
        default: return 0;
    }
}

std::string truncateChars(const std::string& s, int precision) {
    if (precision < 0) return s;
    size_t i = 0;
    int count = 0;
    while (i < s.size() && count < precision) {
        utf::decode(s.data(), s.size(), i);
        count++;
    }
    return s.substr(0, i);
}

void groupDigits(std::string& intPart) {
    if (intPart.size() <= 3) return;
    std::string out;
    int first = static_cast<int>(intPart.size() % 3);
    if (first == 0) first = 3;
    out.append(intPart, 0, static_cast<size_t>(first));
    for (size_t k = static_cast<size_t>(first); k < intPart.size(); k += 3) {
        out.push_back(',');
        out.append(intPart, k, 3);
    }
    intPart.swap(out);
}

// Applies sign flags, zero padding and width to a formatted magnitude.
void emitNumber(std::string& out, bool negative, std::string magnitude, const Spec& sp) {
    std::string prefix, suffix;
    if (negative) {
        if (sp.paren) {
            prefix = "(";
            suffix = ")";
        } else {
            prefix = "-";
        }
    } else if (sp.plus) {
        prefix = "+";
    } else if (sp.space) {
        prefix = " ";
    }
    if (sp.zero && sp.width > 0) {
        int32_t len = static_cast<int32_t>(prefix.size() + magnitude.size() + suffix.size());
        if (len < sp.width) magnitude.insert(0, static_cast<size_t>(sp.width - len), '0');
    }
    std::string body = prefix + magnitude + suffix;
    if (sp.upper) body = upperAscii(body);
    pad(out, body, sp);
}

// ---- decimal digit machinery for %f %e %g (Java rounds the shortest repr HALF_UP)

struct Decimal {
    std::string d;  // significant digits, no leading zeros (empty = zero)
    int point = 0;  // value = 0.d * 10^point
};

Decimal toDecimal(double v) {
    Decimal r;
    if (v == 0) return r;
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
    std::string s(buf, res.ptr);
    size_t e = s.find('e');
    for (size_t k = 0; k < e; k++) {
        if (s[k] != '.') r.d.push_back(s[k]);
    }
    int exp = std::atoi(s.c_str() + e + 1);
    while (r.d.size() > 1 && r.d.back() == '0') r.d.pop_back();
    r.point = exp + 1;
    return r;
}

// Keeps `keep` significant digits, rounding half-up.
void roundDigits(Decimal& x, int keep) {
    if (keep < 0) {
        x.d.clear();
        return;
    }
    if (static_cast<int>(x.d.size()) <= keep) return;
    bool up = x.d[static_cast<size_t>(keep)] >= '5';
    x.d.resize(static_cast<size_t>(keep));
    if (!up) {
        while (!x.d.empty() && x.d.back() == '0') x.d.pop_back();
        return;
    }
    int k = keep - 1;
    while (k >= 0 && x.d[static_cast<size_t>(k)] == '9') {
        x.d[static_cast<size_t>(k)] = '0';
        k--;
    }
    if (k < 0) {
        x.d.insert(x.d.begin(), '1');
        x.point++;
    } else {
        x.d[static_cast<size_t>(k)]++;
    }
    while (!x.d.empty() && x.d.back() == '0') x.d.pop_back();
}

char digitAt(const Decimal& x, int pos) {  // pos = index into d (may be out of range -> '0')
    if (pos < 0 || pos >= static_cast<int>(x.d.size())) return '0';
    return x.d[static_cast<size_t>(pos)];
}

std::string fixedMagnitude(double mag, int prec, bool group, bool alt) {
    Decimal x = toDecimal(mag);
    if (!x.d.empty()) roundDigits(x, x.point + prec);
    std::string intPart;
    if (x.d.empty() || x.point <= 0) {
        intPart = "0";
    } else {
        for (int k = 0; k < x.point; k++) intPart.push_back(digitAt(x, k));
    }
    if (group) groupDigits(intPart);
    std::string s = intPart;
    if (prec > 0 || alt) s.push_back('.');
    for (int k = 0; k < prec; k++) s.push_back(x.d.empty() ? '0' : digitAt(x, x.point + k));
    return s;
}

std::string sciMagnitude(double mag, int prec, bool alt, bool upper) {
    Decimal x = toDecimal(mag);
    int exp = 0;
    if (!x.d.empty()) {
        roundDigits(x, prec + 1);
        exp = x.point - 1;
    }
    std::string s;
    s.push_back(x.d.empty() ? '0' : x.d[0]);
    if (prec > 0 || alt) s.push_back('.');
    for (int k = 1; k <= prec; k++) s.push_back(digitAt(x, k));
    s.push_back(upper ? 'E' : 'e');
    s.push_back(exp < 0 ? '-' : '+');
    int ae = exp < 0 ? -exp : exp;
    if (ae < 10) s.push_back('0');
    s.append(std::to_string(ae));
    return s;
}

void formatFloating(std::string& out, const Spec& sp, const FormatArg& a) {
    double v = a.d;  // a float argument is widened to double, as Java's Formatter does
    if (std::isnan(v) || std::isinf(v)) {
        std::string body = std::isnan(v) ? "NaN" : (v > 0 ? (sp.plus ? "+Infinity" : (sp.space ? " Infinity" : "Infinity"))
                                                          : (sp.paren ? "(Infinity)" : "-Infinity"));
        if (sp.upper) body = upperAscii(body);
        pad(out, body, sp);
        return;
    }
    const bool neg = std::signbit(v);
    const double mag = std::fabs(v);
    std::string m;
    switch (sp.conv) {
        case 'f':
            m = fixedMagnitude(mag, sp.precision < 0 ? 6 : sp.precision, sp.group, sp.alt);
            break;
        case 'e':
            m = sciMagnitude(mag, sp.precision < 0 ? 6 : sp.precision, sp.alt, sp.upper);
            break;
        case 'g': {
            int prec = sp.precision < 0 ? 6 : (sp.precision == 0 ? 1 : sp.precision);
            if (mag == 0) {
                m = fixedMagnitude(0, prec - 1, sp.group, sp.alt);
                break;
            }
            Decimal x = toDecimal(mag);
            roundDigits(x, prec);
            int e = x.point - 1;
            if (e >= -4 && e < prec) {
                m = fixedMagnitude(mag, prec - 1 - e, sp.group, sp.alt);
            } else {
                m = sciMagnitude(mag, prec - 1, sp.alt, sp.upper);
            }
            break;
        }
        default:
            formatError("UnknownFormatConversionException", "Conversion = '" + std::string(1, sp.conv) + "'");
    }
    emitNumber(out, neg, m, sp);
}

void formatInteger(std::string& out, const Spec& sp, const FormatArg& a) {
    if (sp.conv == 'd') {
        bool neg;
        std::string mag;
        if (a.kind == FormatArg::UINT) {
            neg = false;
            mag = std::to_string(a.u);
        } else {
            neg = a.i < 0;
            uint64_t u = neg ? (0 - static_cast<uint64_t>(a.i)) : static_cast<uint64_t>(a.i);
            mag = std::to_string(u);
        }
        if (sp.group) groupDigits(mag);
        emitNumber(out, neg, mag, sp);
        return;
    }
    // 'x' / 'o': negative values print as unsigned of the argument's width.
    uint64_t u = a.kind == FormatArg::UINT ? a.u : static_cast<uint64_t>(a.i);
    if (a.kind == FormatArg::INT && a.bits < 64) u &= (uint64_t{1} << a.bits) - 1;
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof buf, u, sp.conv == 'x' ? 16 : 8);
    std::string mag(buf, r.ptr);
    std::string prefix;
    if (sp.alt) prefix = sp.conv == 'x' ? "0x" : "0";
    if (sp.zero && sp.width > 0) {
        int32_t len = static_cast<int32_t>(prefix.size() + mag.size());
        if (len < sp.width) mag.insert(0, static_cast<size_t>(sp.width - len), '0');
    }
    std::string body = prefix + mag;
    if (sp.upper) body = upperAscii(body);
    pad(out, body, sp);
}

}  // namespace

String formatImpl(const String& fmt, const FormatArg* args, size_t n) {
    std::string out;
    const std::string& f = fmt;
    size_t i = 0;
    int ordinary = 0;
    int last = -1;
    while (i < f.size()) {
        char c = f[i];
        if (c != '%') {
            out.push_back(c);
            i++;
            continue;
        }
        Spec sp;
        size_t start = i;
        i++;
        if (i >= f.size()) formatError("UnknownFormatConversionException", "Conversion = '%'");
        // argument index: digits followed by '$'
        size_t j = i;
        while (j < f.size() && f[j] >= '0' && f[j] <= '9') j++;
        if (j < f.size() && f[j] == '$' && j > i) {
            sp.index = std::atoi(f.substr(i, j - i).c_str());
            i = j + 1;
        }
        // flags
        for (; i < f.size(); i++) {
            char fl = f[i];
            if (fl == '-') sp.left = true;
            else if (fl == '#') sp.alt = true;
            else if (fl == '+') sp.plus = true;
            else if (fl == ' ') sp.space = true;
            else if (fl == '0') sp.zero = true;
            else if (fl == ',') sp.group = true;
            else if (fl == '(') sp.paren = true;
            else if (fl == '<') sp.index = 0;
            else break;
        }
        // width
        j = i;
        while (j < f.size() && f[j] >= '0' && f[j] <= '9') j++;
        if (j > i) {
            sp.width = std::atoi(f.substr(i, j - i).c_str());
            i = j;
        }
        // precision
        if (i < f.size() && f[i] == '.') {
            i++;
            j = i;
            while (j < f.size() && f[j] >= '0' && f[j] <= '9') j++;
            if (j == i) formatError("IllegalFormatPrecisionException", f.substr(start, j - start));
            sp.precision = std::atoi(f.substr(i, j - i).c_str());
            i = j;
        }
        if (i >= f.size()) formatError("UnknownFormatConversionException", "Conversion = '%'");
        sp.conv = f[i++];
        sp.text = f.substr(start, i - start);
        if ((sp.left || sp.zero) && sp.width < 0) formatError("MissingFormatWidthException", sp.text);
        if (sp.conv >= 'A' && sp.conv <= 'Z' && sp.conv != 'T') {
            sp.upper = true;
            sp.conv = static_cast<char>(sp.conv + 32);
        }
        if (sp.conv == 'n') {
            out.push_back('\n');
            continue;
        }
        if (sp.conv == '%') {
            Spec p = sp;
            p.upper = false;
            pad(out, "%", p);
            continue;
        }
        if (std::strchr("bhscdoxefga", sp.conv) == nullptr) {
            formatError("UnknownFormatConversionException", "Conversion = '" + std::string(1, f[i - 1]) + "'");
        }
        // pick the argument
        int argIndex;
        if (sp.index > 0) {
            argIndex = sp.index - 1;
        } else if (sp.index == 0) {
            if (last < 0) formatError("MissingFormatArgumentException", "Format specifier '" + sp.text + "'");
            argIndex = last;
        } else {
            argIndex = ordinary++;
        }
        if (argIndex < 0 || static_cast<size_t>(argIndex) >= n) {
            formatError("MissingFormatArgumentException", "Format specifier '" + sp.text + "'");
        }
        last = argIndex;
        const FormatArg a = normalize(args[argIndex]);
        switch (sp.conv) {
            case 'b': {
                std::string s = a.kind == FormatArg::NUL ? "false" : (a.kind == FormatArg::BOOL ? (a.b ? "true" : "false") : "true");
                s = truncateChars(s, sp.precision);
                pad(out, sp.upper ? upperAscii(s) : s, sp);
                break;
            }
            case 'h': {
                std::string s = a.kind == FormatArg::NUL ? "null" : std::string(Integer::toHexString(javaHashCode(a)));
                s = truncateChars(s, sp.precision);
                pad(out, sp.upper ? upperAscii(s) : s, sp);
                break;
            }
            case 's': {
                std::string s = truncateChars(toJavaString(a), sp.precision);
                if (sp.upper) s = std::string(String(s).toUpperCase());
                pad(out, s, sp);
                break;
            }
            case 'c': {
                if (a.kind == FormatArg::NUL) {
                    pad(out, "null", sp);
                    break;
                }
                char32_t cp;
                if (a.kind == FormatArg::CHAR) {
                    cp = a.c;
                } else if (a.kind == FormatArg::INT && a.bits <= 32) {
                    cp = static_cast<char32_t>(a.i);
                } else {
                    conversionMismatch(sp.conv, a);
                }
                std::string s;
                utf::encode(s, cp);
                if (sp.upper) s = std::string(String(s).toUpperCase());
                pad(out, s, sp);
                break;
            }
            case 'd':
            case 'o':
            case 'x': {
                if (a.kind == FormatArg::NUL) {
                    pad(out, "null", sp);
                    break;
                }
                if (a.kind != FormatArg::INT && a.kind != FormatArg::UINT) conversionMismatch(sp.conv, a);
                formatInteger(out, sp, a);
                break;
            }
            case 'e':
            case 'f':
            case 'g':
            case 'a': {
                if (a.kind == FormatArg::NUL) {
                    pad(out, "null", sp);
                    break;
                }
                if (a.kind != FormatArg::FLOAT && a.kind != FormatArg::DOUBLE) conversionMismatch(sp.conv, a);
                if (sp.conv == 'a') formatError("UnknownFormatConversionException", "Conversion = 'a' (unsupported)");
                formatFloating(out, sp, a);
                break;
            }
        }
    }
    return String(std::move(out));
}

}  // namespace jlang::detail
