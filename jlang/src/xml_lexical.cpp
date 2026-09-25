// jlang/src/xml_lexical.cpp - JAXB RI 2.3 lexical rules (DatatypeConverterImpl), see Jaxb.h.
#include "xml_internal.h"

#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <locale.h>

namespace jlang::xml {

namespace detail {

void throwNumberFormat(std::string_view prefix, std::string_view lexical) {
    std::string m(prefix);
    m.append(lexical);
    throw ::jlang::NumberFormatException(String(m));
}

void throwNumberFormatNull() { throw ::jlang::NumberFormatException(); }

namespace {

// Java's "For input string: \"s\"" (NumberFormatException.forInputString, radix 10).
[[noreturn]] void throwForInputString(std::string_view s) {
    std::string m = "For input string: \"";
    m.append(s);
    m.push_back('"');
    throw ::jlang::NumberFormatException(String(m));
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isHexDigit(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool isDigitOrPeriodOrSign(char c) { return isDigit(c) || c == '+' || c == '-' || c == '.'; }

// Validates the subset of java.lang.FloatingDecimal.readJavaFormatString grammar that can
// reach it after JAXB's screening (no NaN/Infinity, no f/F/d/D suffix since the last
// character is a digit, sign or '.'):
//   [+-]? ( digits [. digits?] | . digits ) ([eE] [+-]? digits)?
//   [+-]? 0[xX] ( hexdigits [.] | hexdigits? . hexdigits ) [pP] [+-]? digits
bool validJavaFloat(std::string_view s) {
    size_t i = 0, n = s.size();
    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    if (i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
        size_t intDigits = 0, fracDigits = 0;
        while (i < n && isHexDigit(s[i])) i++, intDigits++;
        if (i < n && s[i] == '.') {
            i++;
            while (i < n && isHexDigit(s[i])) i++, fracDigits++;
        }
        if (intDigits + fracDigits == 0) return false;
        if (i >= n || (s[i] != 'p' && s[i] != 'P')) return false;
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        size_t expDigits = 0;
        while (i < n && isDigit(s[i])) i++, expDigits++;
        return expDigits > 0 && i == n;
    }
    size_t intDigits = 0, fracDigits = 0;
    while (i < n && isDigit(s[i])) i++, intDigits++;
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && isDigit(s[i])) i++, fracDigits++;
    }
    if (intDigits + fracDigits == 0) return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        size_t expDigits = 0;
        while (i < n && isDigit(s[i])) i++, expDigits++;
        if (expDigits == 0) return false;
    }
    return i == n;
}

locale_t cLocale() {
    static locale_t loc = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
    return loc;
}

}  // namespace
}  // namespace detail

// ---------------------------------------------------------------------------------------

int32_t parseInt(std::string_view s) {
    int32_t sign = 1;
    uint32_t r = 0;  // wraps like Java int arithmetic
    for (char ch : s) {
        if (isXmlWhitespace(ch)) continue;
        if (ch >= '0' && ch <= '9') {
            r = r * 10u + static_cast<uint32_t>(ch - '0');
            continue;
        }
        if (ch == '-') {
            sign = -1;
            continue;
        }
        if (ch == '+') continue;
        detail::throwNumberFormat("Not a number: ", s);
    }
    return static_cast<int32_t>(r * static_cast<uint32_t>(sign));
}

int16_t parseShort(std::string_view s) { return static_cast<int16_t>(parseInt(s)); }
int8_t parseByte(std::string_view s) { return static_cast<int8_t>(parseInt(s)); }

int64_t parseLong(std::string_view lexical) {
    std::string_view s = trimXml(lexical);
    // removeOptionalPlus
    if (s.size() > 1 && s[0] == '+') {
        s = s.substr(1);
        if (!(detail::isDigit(s[0]) || s[0] == '.')) detail::throwNumberFormatNull();
    }
    // Long.parseLong(s, 10)
    size_t n = s.size();
    if (n == 0) detail::throwForInputString(s);
    size_t i = 0;
    bool negative = false;
    if (s[0] == '-' || s[0] == '+') {
        negative = s[0] == '-';
        if (n == 1) detail::throwForInputString(s);
        i = 1;
    }
    // Accumulate negatively, as Java does, so Long.MIN_VALUE parses.
    const int64_t limit = negative ? INT64_MIN : -INT64_MAX;
    const int64_t multmin = limit / 10;
    int64_t result = 0;
    for (; i < n; i++) {
        char c = s[i];
        if (!detail::isDigit(c)) detail::throwForInputString(s);
        int64_t d = c - '0';
        if (result < multmin) detail::throwForInputString(s);
        result *= 10;
        if (result < limit + d) detail::throwForInputString(s);
        result -= d;
    }
    return negative ? result : -result;
}

float parseFloat(std::string_view lexical) {
    std::string_view s = trimXml(lexical);
    if (s == "NaN") return std::nanf("");
    if (s == "INF") return INFINITY;
    if (s == "-INF") return -INFINITY;
    if (s.empty() || !detail::isDigitOrPeriodOrSign(s.front()) || !detail::isDigitOrPeriodOrSign(s.back()))
        detail::throwNumberFormatNull();
    if (!detail::validJavaFloat(s)) detail::throwForInputString(s);
    std::string tmp(s);
    return strtof_l(tmp.c_str(), nullptr, detail::cLocale());
}

double parseDouble(std::string_view lexical) {
    std::string_view s = trimXml(lexical);
    if (s == "NaN") return std::nan("");
    if (s == "INF") return HUGE_VAL;
    if (s == "-INF") return -HUGE_VAL;
    if (s.empty() || !detail::isDigitOrPeriodOrSign(s.front()) || !detail::isDigitOrPeriodOrSign(s.back()))
        throw ::jlang::NumberFormatException(String(s));
    if (!detail::validJavaFloat(s)) detail::throwForInputString(s);
    std::string tmp(s);
    return strtod_l(tmp.c_str(), nullptr, detail::cLocale());
}

std::optional<bool> parseBooleanOrNull(std::string_view literal) {
    // Line-by-line port of DatatypeConverterImpl._parseBoolean (jaxb-impl 2.3.9), including
    // its StringIndexOutOfBoundsException for a lone "t"/"f".
    const size_t len = literal.size();
    auto charAt = [&](size_t i) -> char {
        if (i >= len) {
            throw ::jlang::StringIndexOutOfBoundsException(
                String("Index ") + static_cast<int32_t>(i) + " out of bounds for length " + static_cast<int32_t>(len));
        }
        return literal[i];
    };
    if (len == 0) return std::nullopt;
    size_t i = 0;
    bool value = false;
    char ch;
    while (isXmlWhitespace(ch = charAt(i++)) && i < len) {
    }
    size_t strIndex = 0;
    switch (ch) {
        case '1':
            value = true;
            break;
        case '0':
            value = false;
            break;
        case 't': {
            static constexpr char strTrue[] = "rue";
            do {
                ch = charAt(i++);
            } while (strTrue[strIndex++] == ch && i < len && strIndex < 3);
            if (strIndex == 3 && strTrue[strIndex - 1] == ch) {
                value = true;
                break;
            }
            return std::nullopt;
        }
        case 'f': {
            static constexpr char strFalse[] = "alse";
            do {
                ch = charAt(i++);
            } while (strFalse[strIndex++] == ch && i < len && strIndex < 4);
            if (strIndex == 4 && strFalse[strIndex - 1] == ch) {
                value = false;
                break;
            }
            return std::nullopt;
        }
        default:
            break;
    }
    while (i < len && isXmlWhitespace(literal[i])) ++i;
    if (i == len) return value;
    return std::nullopt;
}

String printFloat(float v) {
    if (std::isnan(v)) return "NaN";
    if (v == INFINITY) return "INF";
    if (v == -INFINITY) return "-INF";
    return String::valueOf(v);
}

String printDouble(double v) {
    if (std::isnan(v)) return "NaN";
    if (v == HUGE_VAL) return "INF";
    if (v == -HUGE_VAL) return "-INF";
    return String::valueOf(v);
}

}  // namespace jlang::xml
