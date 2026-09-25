// java.text.DecimalFormat / NumberFormat (a port of OpenJDK 8's applyPattern, toPattern,
// subformat and subparse, with English DecimalFormatSymbols) and java.math.RoundingMode.
//
// Doubles are formatted like Java 8+: the shortest decimal digits that round-trip (Java's
// FloatingDecimal digits); when fewer digits are displayed, the value is rounded using the
// exact binary value of the double (so 0.125 -> "0.12" but 0.135 -> "0.14" with HALF_EVEN).
#include "time_internal.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace jlang {

// ---------------------------------------------------------------------------------------
// RoundingMode

static const char* const ROUNDING_MODE_NAMES[] = {"UP",        "DOWN",      "CEILING",   "FLOOR",
                                                  "HALF_UP",   "HALF_DOWN", "HALF_EVEN", "UNNECESSARY"};

String RoundingMode::name() const {
    if (v_ == Value::_NULL) return String("null");
    return String(ROUNDING_MODE_NAMES[ordinal()]);
}

Array<RoundingMode>* RoundingMode::values() {
    auto* a = new Array<RoundingMode>(8);
    for (int32_t i = 0; i < 8; i++) (*a)[i] = RoundingMode(static_cast<Value>(i));
    return a;
}

RoundingMode RoundingMode::valueOf(const String& name) {
    for (int32_t i = 0; i < 8; i++)
        if (name == ROUNDING_MODE_NAMES[i]) return RoundingMode(static_cast<Value>(i));
    throw IllegalArgumentException(str("No enum constant java.math.RoundingMode.", name));
}

// ---------------------------------------------------------------------------------------
namespace {

constexpr char16_t QUOTE = u'\'';
constexpr char16_t CURRENCY_SIGN = u'¤';
constexpr char16_t PATTERN_ZERO_DIGIT = u'0';
constexpr char16_t PATTERN_GROUPING_SEPARATOR = u',';
constexpr char16_t PATTERN_DECIMAL_SEPARATOR = u'.';
constexpr char16_t PATTERN_PER_MILLE = u'‰';
constexpr char16_t PATTERN_PERCENT = u'%';
constexpr char16_t PATTERN_DIGIT = u'#';
constexpr char16_t PATTERN_SEPARATOR = u';';
constexpr char16_t PATTERN_EXPONENT = u'E';
constexpr char16_t PATTERN_MINUS = u'-';

constexpr int32_t MAXIMUM_INTEGER_DIGITS = 2147483647;
constexpr int32_t MAXIMUM_FRACTION_DIGITS = 2147483647;
constexpr int32_t DOUBLE_INTEGER_DIGITS = 309;
constexpr int32_t DOUBLE_FRACTION_DIGITS = 340;

const char16_t NAN_SYMBOL[] = u"�";
const char16_t INFINITY_SYMBOL[] = u"∞";

// Digits of a non-negative value: value = 0.d1d2d3... x 10^decimalAt ("" = zero).
struct DigitList {
    std::string digits;
    int32_t decimalAt = 0;
    bool isZero() const { return digits.empty(); }
};

void stripTrailingZeros(std::string& d) {
    while (!d.empty() && d.back() == '0') d.pop_back();
}

// Exact decimal digits of a finite positive double.
DigitList exactDigits(double x) {
    static thread_local char buf[1200];
    int n = std::snprintf(buf, sizeof buf, "%.800e", x);
    DigitList r;
    const char* e = std::strchr(buf, 'e');
    r.digits.reserve(810);
    for (const char* p = buf; p < e && p < buf + n; p++)
        if (*p >= '0' && *p <= '9') r.digits.push_back(*p);
    r.decimalAt = std::atoi(e + 1) + 1;
    stripTrailingZeros(r.digits);
    return r;
}

// Shortest round-trip digits of a finite positive double.
DigitList shortestDigits(double x) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);
    *res.ptr = '\0';
    DigitList r;
    const char* e = std::strchr(buf, 'e');
    for (const char* p = buf; p < e; p++)
        if (*p >= '0' && *p <= '9') r.digits.push_back(*p);
    r.decimalAt = std::atoi(e + 1) + 1;
    stripTrailingZeros(r.digits);
    return r;
}

// Keeps the first `keep` digits (keep may be 0), rounding with `mode` using the discarded digits.
void roundDigits(DigitList& dl, int32_t keep, RoundingMode mode, bool isNegative) {
    if (keep < 0) {
        dl.digits.clear();
        dl.decimalAt = 0;
        return;
    }
    if (static_cast<size_t>(keep) >= dl.digits.size()) return;
    std::string kept = dl.digits.substr(0, static_cast<size_t>(keep));
    char first = dl.digits[static_cast<size_t>(keep)];
    bool restNonZero = false;
    for (size_t i = static_cast<size_t>(keep) + 1; i < dl.digits.size(); i++)
        if (dl.digits[i] != '0') {
            restNonZero = true;
            break;
        }
    bool anyNonZero = first != '0' || restNonZero;
    bool up = false;
    switch (static_cast<RoundingMode::Value>(mode)) {
        case RoundingMode::Value::UP:
            up = anyNonZero;
            break;
        case RoundingMode::Value::DOWN:
            up = false;
            break;
        case RoundingMode::Value::CEILING:
            up = anyNonZero && !isNegative;
            break;
        case RoundingMode::Value::FLOOR:
            up = anyNonZero && isNegative;
            break;
        case RoundingMode::Value::HALF_UP:
            up = first >= '5';
            break;
        case RoundingMode::Value::HALF_DOWN:
            up = first > '5' || (first == '5' && restNonZero);
            break;
        case RoundingMode::Value::UNNECESSARY:
            if (anyNonZero)
                throw ArithmeticException(String("Rounding needed with the rounding mode being set to RoundingMode.UNNECESSARY"));
            break;
        case RoundingMode::Value::HALF_EVEN:
        default:
            up = first > '5' || (first == '5' && (restNonZero || (keep > 0 && ((kept.back() - '0') % 2) != 0)));
            break;
    }
    if (up) {
        int32_t i = keep - 1;
        for (; i >= 0; --i) {
            if (kept[static_cast<size_t>(i)] == '9') {
                kept[static_cast<size_t>(i)] = '0';
            } else {
                kept[static_cast<size_t>(i)]++;
                break;
            }
        }
        if (i < 0) {
            kept.insert(kept.begin(), '1');
            dl.decimalAt++;
        }
    }
    stripTrailingZeros(kept);
    dl.digits = kept;
    if (dl.digits.empty()) dl.decimalAt = 0;
}

// DigitList.set(isNegative, double, maximumDigits, fixedPoint)
DigitList doubleDigitList(double x, int32_t maximumDigits, bool fixedPoint, RoundingMode mode, bool isNegative) {
    DigitList dl;
    if (x == 0.0) return dl;
    DigitList s = shortestDigits(x);
    int32_t keep = fixedPoint ? maximumDigits + s.decimalAt : maximumDigits;
    if (keep >= static_cast<int32_t>(s.digits.size())) return s;
    if (fixedPoint && keep < 0) return dl;  // underflow to zero
    DigitList e = exactDigits(x);
    int32_t keepExact = fixedPoint ? maximumDigits + e.decimalAt : maximumDigits;
    roundDigits(e, keepExact, mode, isNegative);
    return e;
}

// |value| of a signed 128-bit integer as a DigitList, rounded to maximumDigits (0 = no rounding).
DigitList integerDigitList(unsigned __int128 v, int32_t maximumDigits, RoundingMode mode, bool isNegative) {
    DigitList dl;
    if (v == 0) return dl;
    char buf[64];
    int n = 0;
    while (v > 0) {
        buf[n++] = static_cast<char>('0' + static_cast<int>(v % 10));
        v /= 10;
    }
    for (int i = n - 1; i >= 0; --i) dl.digits.push_back(buf[i]);
    dl.decimalAt = n;
    stripTrailingZeros(dl.digits);
    if (maximumDigits > 0) roundDigits(dl, maximumDigits, mode, isNegative);
    return dl;
}

void appendUtf16(std::u16string& out, const String& s) { out += s.toUtf16(); }

// DecimalFormat.expandAffix: a quote marks the next char as special (%, per mille, currency, -).
String expandAffix(const String& pattern) {
    std::u16string p = pattern.toUtf16();
    std::u16string out;
    for (size_t i = 0; i < p.size();) {
        char16_t c = p[i++];
        if (c == QUOTE && i < p.size()) {
            c = p[i++];
            switch (c) {
                case CURRENCY_SIGN:
                    if (i < p.size() && p[i] == CURRENCY_SIGN) {
                        ++i;
                        out += u"USD";
                    } else {
                        out += Locale::getDefault()->getCountry() == "US" ? std::u16string(u"$") : std::u16string(u"¤");
                    }
                    continue;
                case PATTERN_PERCENT:
                    c = u'%';
                    break;
                case PATTERN_PER_MILLE:
                    c = u'‰';
                    break;
                case PATTERN_MINUS:
                    c = u'-';
                    break;
                default:
                    break;
            }
        }
        out.push_back(c);
    }
    return String::fromUtf16(out);
}

// toPattern helper: append an expanded affix with quoting.
void appendAffixLiteral(std::u16string& buffer, const std::u16string& affix) {
    bool needQuote = false;
    for (char16_t c : affix) {
        if (c == PATTERN_ZERO_DIGIT || c == PATTERN_GROUPING_SEPARATOR || c == PATTERN_DECIMAL_SEPARATOR ||
            c == PATTERN_PERCENT || c == PATTERN_PER_MILLE || c == PATTERN_DIGIT || c == PATTERN_SEPARATOR ||
            c == PATTERN_MINUS || c == CURRENCY_SIGN) {
            needQuote = true;
            break;
        }
    }
    if (needQuote) buffer.push_back(u'\'');
    for (char16_t c : affix) {
        buffer.push_back(c);
        if (c == u'\'') buffer.push_back(c);
    }
    if (needQuote) buffer.push_back(u'\'');
}

void appendAffixPattern(std::u16string& buffer, const String& affixPattern, const String& expAffix) {
    if (affixPattern == nullptr) {
        appendAffixLiteral(buffer, expAffix.toUtf16());
        return;
    }
    std::u16string ap = affixPattern.toUtf16();
    size_t i;
    for (size_t pos = 0; pos < ap.size(); pos = i) {
        size_t q = ap.find(QUOTE, pos);
        if (q == std::u16string::npos) {
            appendAffixLiteral(buffer, ap.substr(pos));
            break;
        }
        if (q > pos) appendAffixLiteral(buffer, ap.substr(pos, q - pos));
        i = q + 1;
        if (i >= ap.size()) break;
        char16_t c = ap[i++];
        if (c == QUOTE) {
            buffer.push_back(c);
        } else if (c == CURRENCY_SIGN && i < ap.size() && ap[i] == CURRENCY_SIGN) {
            ++i;
            buffer.push_back(c);
        }
        buffer.push_back(c);
    }
}

bool regionMatches(const std::u16string& text, size_t pos, const std::u16string& s) {
    return pos + s.size() <= text.size() && text.compare(pos, s.size(), s) == 0;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// DecimalFormat

DecimalFormat::DecimalFormat() : DecimalFormat(String("#,##0.###")) {}
DecimalFormat::DecimalFormat(const String& pattern) { applyPattern(pattern); }
DecimalFormat::DecimalFormat(const String& pattern, Locale* locale) {
    (void)locale;
    applyPattern(pattern);
}

DecimalFormat* DecimalFormat::getInstance() { return getNumberInstance(Locale::getDefault()); }
DecimalFormat* DecimalFormat::getInstance(Locale* locale) { return getNumberInstance(locale); }
DecimalFormat* DecimalFormat::getNumberInstance() { return getNumberInstance(Locale::getDefault()); }
DecimalFormat* DecimalFormat::getNumberInstance(Locale* locale) { return new DecimalFormat(String("#,##0.###"), locale); }
DecimalFormat* DecimalFormat::getIntegerInstance() { return getIntegerInstance(Locale::getDefault()); }
DecimalFormat* DecimalFormat::getIntegerInstance(Locale* locale) {
    auto* f = new DecimalFormat(String("#,##0.###"), locale);
    f->setMaximumFractionDigits(0);
    f->setDecimalSeparatorAlwaysShown(false);
    f->setParseIntegerOnly(true);
    return f;
}
DecimalFormat* DecimalFormat::getPercentInstance() { return getPercentInstance(Locale::getDefault()); }
DecimalFormat* DecimalFormat::getPercentInstance(Locale* locale) { return new DecimalFormat(String("#,##0%"), locale); }
DecimalFormat* DecimalFormat::getCurrencyInstance() { return getCurrencyInstance(Locale::getDefault()); }
DecimalFormat* DecimalFormat::getCurrencyInstance(Locale* locale) {
    return new DecimalFormat(String("¤#,##0.00;(¤#,##0.00)"), locale);
}

void DecimalFormat::setMaximumIntegerDigits(int32_t v) {
    maxInt_ = std::min(std::max(0, v), MAXIMUM_INTEGER_DIGITS);
    if (minInt_ > maxInt_) minInt_ = maxInt_;
}
void DecimalFormat::setMinimumIntegerDigits(int32_t v) {
    minInt_ = std::min(std::max(0, v), MAXIMUM_INTEGER_DIGITS);
    if (minInt_ > maxInt_) maxInt_ = minInt_;
}
void DecimalFormat::setMaximumFractionDigits(int32_t v) {
    maxFrac_ = std::min(std::max(0, v), MAXIMUM_FRACTION_DIGITS);
    if (minFrac_ > maxFrac_) minFrac_ = maxFrac_;
}
void DecimalFormat::setMinimumFractionDigits(int32_t v) {
    minFrac_ = std::min(std::max(0, v), MAXIMUM_FRACTION_DIGITS);
    if (maxFrac_ < minFrac_) maxFrac_ = minFrac_;
}
void DecimalFormat::setRoundingMode(RoundingMode mode) {
    if (mode == nullptr) detail::throwNullPointerException();
    roundingMode_ = mode;
}

// DecimalFormat.applyPattern(pattern, false)
void DecimalFormat::applyPattern(const String& patternStr) {
    if (patternStr == nullptr) detail::throwNullPointerException();
    std::u16string pattern = patternStr.toUtf16();
    const size_t plen = pattern.size();
    bool gotNegative = false;
    decimalSeparatorAlwaysShown_ = false;
    useExponential_ = false;
    auto malformed = [&](const char* what) -> IllegalArgumentException {
        return IllegalArgumentException(str(what, " in pattern \"", patternStr, "\""));
    };
    size_t start = 0;
    for (int j = 1; j >= 0 && start < plen; --j) {
        bool inQuote = false;
        std::u16string prefix, suffix;
        int32_t decimalPos = -1;
        int32_t multiplier = 1;
        int32_t digitLeftCount = 0, zeroDigitCount = 0, digitRightCount = 0;
        int32_t groupingCount = -1;
        int phase = 0;
        std::u16string* affix = &prefix;
        for (size_t pos = start; pos < plen; ++pos) {
            char16_t ch = pattern[pos];
            switch (phase) {
                case 0:
                case 2:
                    if (inQuote) {
                        if (ch == QUOTE) {
                            if ((pos + 1) < plen && pattern[pos + 1] == QUOTE) {
                                ++pos;
                                *affix += u"''";
                            } else {
                                inQuote = false;
                            }
                            continue;
                        }
                    } else {
                        if (ch == PATTERN_DIGIT || ch == PATTERN_ZERO_DIGIT || ch == PATTERN_GROUPING_SEPARATOR ||
                            ch == PATTERN_DECIMAL_SEPARATOR) {
                            phase = 1;
                            --pos;
                            continue;
                        } else if (ch == CURRENCY_SIGN) {
                            bool doubled = (pos + 1) < plen && pattern[pos + 1] == CURRENCY_SIGN;
                            if (doubled) ++pos;
                            *affix += doubled ? u"'¤¤" : u"'¤";
                            continue;
                        } else if (ch == QUOTE) {
                            if ((pos + 1) < plen && pattern[pos + 1] == QUOTE) {
                                ++pos;
                                *affix += u"''";
                            } else {
                                inQuote = true;
                            }
                            continue;
                        } else if (ch == PATTERN_SEPARATOR) {
                            if (phase == 0 || j == 0) {
                                throw IllegalArgumentException(
                                    str("Unquoted special character '", String::fromUtf16(std::u16string(1, ch)),
                                        "' in pattern \"", patternStr, "\""));
                            }
                            start = pos + 1;
                            pos = plen;
                            continue;
                        } else if (ch == PATTERN_PERCENT) {
                            if (multiplier != 1) throw malformed("Too many percent/per mille characters");
                            multiplier = 100;
                            *affix += u"'%";
                            continue;
                        } else if (ch == PATTERN_PER_MILLE) {
                            if (multiplier != 1) throw malformed("Too many percent/per mille characters");
                            multiplier = 1000;
                            *affix += u"'‰";
                            continue;
                        } else if (ch == PATTERN_MINUS) {
                            *affix += u"'-";
                            continue;
                        }
                    }
                    affix->push_back(ch);
                    break;
                case 1:
                    // The negative subpattern only specifies the negative prefix and suffix:
                    // its phase 1 characters are skipped (JDK 9+ behavior).
                    if (j == 0) {
                        while (pos < plen) {
                            char16_t c2 = pattern[pos];
                            if (c2 == PATTERN_DIGIT || c2 == PATTERN_ZERO_DIGIT || c2 == PATTERN_GROUPING_SEPARATOR ||
                                c2 == PATTERN_DECIMAL_SEPARATOR || c2 == PATTERN_EXPONENT) {
                                ++pos;
                            } else {
                                --pos;
                                phase = 2;
                                affix = &suffix;
                                break;
                            }
                        }
                        continue;
                    }
                    if (ch == PATTERN_DIGIT) {
                        if (zeroDigitCount > 0) ++digitRightCount;
                        else ++digitLeftCount;
                        if (groupingCount >= 0 && decimalPos < 0) ++groupingCount;
                    } else if (ch == PATTERN_ZERO_DIGIT) {
                        if (digitRightCount > 0) throw malformed("Unexpected '0'");
                        ++zeroDigitCount;
                        if (groupingCount >= 0 && decimalPos < 0) ++groupingCount;
                    } else if (ch == PATTERN_GROUPING_SEPARATOR) {
                        groupingCount = 0;
                    } else if (ch == PATTERN_DECIMAL_SEPARATOR) {
                        if (decimalPos >= 0) throw malformed("Multiple decimal separators");
                        decimalPos = digitLeftCount + zeroDigitCount + digitRightCount;
                    } else if (ch == PATTERN_EXPONENT) {
                        if (useExponential_) throw malformed("Multiple exponential symbols");
                        useExponential_ = true;
                        minExponentDigits_ = 0;
                        pos = pos + 1;
                        while (pos < plen && pattern[pos] == PATTERN_ZERO_DIGIT) {
                            ++minExponentDigits_;
                            ++pos;
                        }
                        if ((digitLeftCount + zeroDigitCount) < 1 || minExponentDigits_ < 1)
                            throw malformed("Malformed exponential");
                        phase = 2;
                        affix = &suffix;
                        --pos;
                        continue;
                    } else {
                        phase = 2;
                        affix = &suffix;
                        --pos;
                        continue;
                    }
                    break;
            }
        }
        if (zeroDigitCount == 0 && digitLeftCount > 0 && decimalPos >= 0) {
            int32_t n = decimalPos;
            if (n == 0) ++n;
            digitRightCount = digitLeftCount - n;
            digitLeftCount = n - 1;
            zeroDigitCount = 1;
        }
        if ((decimalPos < 0 && digitRightCount > 0) ||
            (decimalPos >= 0 && (decimalPos < digitLeftCount || decimalPos > (digitLeftCount + zeroDigitCount))) ||
            groupingCount == 0 || inQuote) {
            throw IllegalArgumentException(str("Malformed pattern \"", patternStr, "\""));
        }
        if (j == 1) {
            posPrefixPattern_ = String::fromUtf16(prefix);
            posSuffixPattern_ = String::fromUtf16(suffix);
            negPrefixPattern_ = posPrefixPattern_;
            negSuffixPattern_ = posSuffixPattern_;
            int32_t digitTotalCount = digitLeftCount + zeroDigitCount + digitRightCount;
            int32_t effectiveDecimalPos = decimalPos >= 0 ? decimalPos : digitTotalCount;
            setMinimumIntegerDigits(effectiveDecimalPos - digitLeftCount);
            setMaximumIntegerDigits(useExponential_ ? digitLeftCount + minInt_ : MAXIMUM_INTEGER_DIGITS);
            setMaximumFractionDigits(decimalPos >= 0 ? (digitTotalCount - decimalPos) : 0);
            setMinimumFractionDigits(decimalPos >= 0 ? (digitLeftCount + zeroDigitCount - decimalPos) : 0);
            groupingUsed_ = groupingCount > 0;
            groupingSize_ = static_cast<int8_t>((groupingCount > 0) ? groupingCount : 0);
            multiplier_ = multiplier;
            decimalSeparatorAlwaysShown_ = decimalPos == 0 || decimalPos == digitTotalCount;
        } else {
            negPrefixPattern_ = String::fromUtf16(prefix);
            negSuffixPattern_ = String::fromUtf16(suffix);
            gotNegative = true;
        }
    }
    if (plen == 0) {
        posPrefixPattern_ = posSuffixPattern_ = String("");
        setMinimumIntegerDigits(0);
        setMaximumIntegerDigits(MAXIMUM_INTEGER_DIGITS);
        setMinimumFractionDigits(0);
        setMaximumFractionDigits(MAXIMUM_FRACTION_DIGITS);
    }
    if (!gotNegative || (negPrefixPattern_.equals(posPrefixPattern_) && negSuffixPattern_.equals(posSuffixPattern_))) {
        negSuffixPattern_ = posSuffixPattern_;
        negPrefixPattern_ = str("'-", posPrefixPattern_);
    }
    posPrefix_ = expandAffix(posPrefixPattern_);
    posSuffix_ = expandAffix(posSuffixPattern_);
    negPrefix_ = expandAffix(negPrefixPattern_);
    negSuffix_ = expandAffix(negSuffixPattern_);
}

String DecimalFormat::toPattern() {
    std::u16string result;
    int32_t maxInt = std::min(maxInt_, DOUBLE_INTEGER_DIGITS);
    int32_t minInt = std::min(minInt_, DOUBLE_INTEGER_DIGITS);
    int32_t maxFrac = std::min(maxFrac_, DOUBLE_FRACTION_DIGITS);
    int32_t minFrac = std::min(minFrac_, DOUBLE_FRACTION_DIGITS);
    for (int j = 1; j >= 0; --j) {
        if (j == 1) appendAffixPattern(result, posPrefixPattern_, posPrefix_);
        else appendAffixPattern(result, negPrefixPattern_, negPrefix_);
        int32_t digitCount = useExponential_ ? maxInt : std::max<int32_t>(groupingSize_, minInt) + 1;
        for (int32_t i = digitCount; i > 0; --i) {
            if (i != digitCount && groupingUsed_ && groupingSize_ != 0 && i % groupingSize_ == 0)
                result.push_back(PATTERN_GROUPING_SEPARATOR);
            result.push_back(i <= minInt ? PATTERN_ZERO_DIGIT : PATTERN_DIGIT);
        }
        if (maxFrac > 0 || decimalSeparatorAlwaysShown_) result.push_back(PATTERN_DECIMAL_SEPARATOR);
        for (int32_t i = 0; i < maxFrac; ++i) result.push_back(i < minFrac ? PATTERN_ZERO_DIGIT : PATTERN_DIGIT);
        if (useExponential_) {
            result.push_back(PATTERN_EXPONENT);
            for (int32_t i = 0; i < minExponentDigits_; ++i) result.push_back(PATTERN_ZERO_DIGIT);
        }
        if (j == 1) {
            appendAffixPattern(result, posSuffixPattern_, posSuffix_);
            bool sameSuffix = (negSuffixPattern_ == nullptr && posSuffixPattern_ == nullptr && negSuffix_.equals(posSuffix_)) ||
                              (negSuffixPattern_ != nullptr && negSuffixPattern_.equals(posSuffixPattern_));
            if (sameSuffix) {
                if ((negPrefixPattern_ != nullptr && posPrefixPattern_ != nullptr &&
                     negPrefixPattern_.equals(str("'-", posPrefixPattern_))) ||
                    (negPrefixPattern_ == nullptr && posPrefixPattern_ == nullptr && negPrefix_.equals(str("-", posPrefix_))))
                    break;
            }
            result.push_back(PATTERN_SEPARATOR);
        } else {
            appendAffixPattern(result, negSuffixPattern_, negSuffix_);
        }
    }
    return String::fromUtf16(result);
}

// DecimalFormat.subformat
void DecimalFormat::formatDigits(String& out0, bool isNegative, const std::string& digitsIn, int32_t decimalAtIn,
                                 bool isInteger) {
    std::u16string result;
    DigitList dl{digitsIn, decimalAtIn};
    const int32_t maxIntDigits = std::min(maxInt_, DOUBLE_INTEGER_DIGITS);
    const int32_t minIntDigits = std::min(minInt_, DOUBLE_INTEGER_DIGITS);
    const int32_t maxFraDigits = std::min(maxFrac_, DOUBLE_FRACTION_DIGITS);
    const int32_t minFraDigits = std::min(minFrac_, DOUBLE_FRACTION_DIGITS);
    const int32_t count = static_cast<int32_t>(dl.digits.size());
    if (dl.isZero()) dl.decimalAt = 0;
    appendUtf16(result, isNegative ? negPrefix_ : posPrefix_);
    if (useExponential_) {
        int32_t exponent = dl.decimalAt;
        int32_t repeat = maxIntDigits;
        int32_t minimumIntegerDigits = minIntDigits;
        if (repeat > 1 && repeat > minIntDigits) {
            if (exponent >= 1) exponent = ((exponent - 1) / repeat) * repeat;
            else exponent = ((exponent - repeat) / repeat) * repeat;
            minimumIntegerDigits = 1;
        } else {
            exponent -= minimumIntegerDigits;
        }
        int64_t minimumDigits = static_cast<int64_t>(minIntDigits) + minFraDigits;
        int32_t integerDigits = dl.isZero() ? minimumIntegerDigits : dl.decimalAt - exponent;
        if (minimumDigits < integerDigits) minimumDigits = integerDigits;
        int64_t totalDigits = count;
        if (minimumDigits > totalDigits) totalDigits = minimumDigits;
        for (int64_t i = 0; i < totalDigits; ++i) {
            if (i == integerDigits) result.push_back(u'.');
            result.push_back(i < count ? static_cast<char16_t>(dl.digits[static_cast<size_t>(i)]) : u'0');
        }
        if (decimalSeparatorAlwaysShown_ && totalDigits == integerDigits) result.push_back(u'.');
        result.push_back(u'E');
        if (dl.isZero()) exponent = 0;
        if (exponent < 0) {
            exponent = -exponent;
            result.push_back(u'-');
        }
        std::string e;
        detail::tm::appendPadded(e, exponent, minExponentDigits_);
        for (char c : e) result.push_back(static_cast<char16_t>(c));
    } else {
        int32_t n = minIntDigits;
        int32_t digitIndex = 0;
        if (dl.decimalAt > 0 && n < dl.decimalAt) n = dl.decimalAt;
        if (n > maxIntDigits) {
            n = maxIntDigits;
            digitIndex = dl.decimalAt - n;
        }
        size_t sizeBeforeIntegerPart = result.size();
        for (int32_t i = n - 1; i >= 0; --i) {
            if (i < dl.decimalAt && digitIndex < count) {
                result.push_back(static_cast<char16_t>(dl.digits[static_cast<size_t>(digitIndex++)]));
            } else {
                result.push_back(u'0');
            }
            if (groupingUsed_ && i > 0 && groupingSize_ != 0 && (i % groupingSize_ == 0)) result.push_back(u',');
        }
        bool fractionPresent = (minFraDigits > 0) || (!isInteger && digitIndex < count);
        if (!fractionPresent && result.size() == sizeBeforeIntegerPart) result.push_back(u'0');
        if (decimalSeparatorAlwaysShown_ || fractionPresent) result.push_back(u'.');
        for (int32_t i = 0; i < maxFraDigits; ++i) {
            if (i >= minFraDigits && (isInteger || digitIndex >= count)) break;
            if (-1 - i > (dl.decimalAt - 1)) {
                result.push_back(u'0');
                continue;
            }
            if (!isInteger && digitIndex < count) {
                result.push_back(static_cast<char16_t>(dl.digits[static_cast<size_t>(digitIndex++)]));
            } else {
                result.push_back(u'0');
            }
        }
    }
    appendUtf16(result, isNegative ? negSuffix_ : posSuffix_);
    out0 += String::fromUtf16(result);
}

String DecimalFormat::format(double number) {
    String out("");
    if (std::isnan(number) || (std::isinf(number) && multiplier_ == 0)) {
        out += String::fromUtf16(NAN_SYMBOL);
        return out;
    }
    bool isNegative = ((number < 0.0) || (number == 0.0 && std::signbit(number))) ^ (multiplier_ < 0);
    if (multiplier_ != 1) number *= multiplier_;
    if (std::isinf(number)) {
        out += isNegative ? negPrefix_ : posPrefix_;
        out += String::fromUtf16(INFINITY_SYMBOL);
        out += isNegative ? negSuffix_ : posSuffix_;
        return out;
    }
    if (number < 0 || (number == 0.0 && std::signbit(number))) number = -number;
    const int32_t maxIntDigits = std::min(maxInt_, DOUBLE_INTEGER_DIGITS);
    const int32_t maxFraDigits = std::min(maxFrac_, DOUBLE_FRACTION_DIGITS);
    DigitList dl = doubleDigitList(number, useExponential_ ? maxIntDigits + maxFraDigits : maxFraDigits, !useExponential_,
                                   roundingMode_, isNegative);
    formatDigits(out, isNegative, dl.digits, dl.decimalAt, false);
    return out;
}

String DecimalFormat::format(int64_t number) {
    String out("");
    bool isNegative = number < 0;
    unsigned __int128 mag = isNegative ? static_cast<unsigned __int128>(-(static_cast<__int128>(number)))
                                       : static_cast<unsigned __int128>(number);
    int64_t m = multiplier_;
    if (m < 0) {
        m = -m;
        isNegative = !isNegative;
    }
    mag *= static_cast<unsigned __int128>(m);
    if (mag == 0) isNegative = false;
    const int32_t maxIntDigits = std::min(maxInt_, DOUBLE_INTEGER_DIGITS);
    const int32_t maxFraDigits = std::min(maxFrac_, DOUBLE_FRACTION_DIGITS);
    DigitList dl = integerDigitList(mag, useExponential_ ? maxIntDigits + maxFraDigits : 0, roundingMode_, isNegative);
    formatDigits(out, isNegative, dl.digits, dl.decimalAt, true);
    return out;
}

String DecimalFormat::format(Object* number) {
    if (auto* d = dynamic_cast<Double*>(number)) return format(d->value);
    if (auto* f = dynamic_cast<Float*>(number)) return format(static_cast<double>(f->value));
    if (auto* n = dynamic_cast<Number*>(number)) return format(n->longValue());
    throw IllegalArgumentException(String("Cannot format given Object as a Number"));
}

// DecimalFormat.parse (subparse)
Number* DecimalFormat::parse(const String& source, int32_t* ppos) {
    std::u16string text = source.toUtf16();
    size_t position = static_cast<size_t>(*ppos);
    const size_t oldStart = position;
    std::u16string nan = NAN_SYMBOL;
    if (regionMatches(text, position, nan)) {
        *ppos = static_cast<int32_t>(position + nan.size());
        return Double::valueOf(std::nan(""));
    }
    std::u16string pp = posPrefix_.toUtf16(), np = negPrefix_.toUtf16();
    std::u16string ps = posSuffix_.toUtf16(), ns = negSuffix_.toUtf16();
    bool gotPositive = regionMatches(text, position, pp);
    bool gotNegative = regionMatches(text, position, np);
    if (gotPositive && gotNegative) {
        if (pp.size() > np.size()) gotNegative = false;
        else if (pp.size() < np.size()) gotPositive = false;
    }
    if (gotPositive) position += pp.size();
    else if (gotNegative) position += np.size();
    else return nullptr;
    std::string digits;
    int32_t decimalAt = -1;
    bool infinity = false;
    std::u16string inf = INFINITY_SYMBOL;
    if (regionMatches(text, position, inf)) {
        position += inf.size();
        infinity = true;
    } else {
        bool sawDecimal = false, sawDigit = false, sawExponent = false;
        int64_t exponent = 0;
        decimalAt = 0;
        size_t backup = static_cast<size_t>(-1);
        for (; position < text.size(); ++position) {
            char16_t ch = text[position];
            if (ch >= u'0' && ch <= u'9') {
                backup = static_cast<size_t>(-1);
                sawDigit = true;
                if (digits.empty() && ch == u'0') {
                    if (sawDecimal) --decimalAt;  // leading zero after the point
                    continue;
                }
                digits.push_back(static_cast<char>(ch));
                if (!sawDecimal) ++decimalAt;
            } else if (!parseIntegerOnly_ && ch == u'.' && !sawDecimal) {
                sawDecimal = true;
                backup = static_cast<size_t>(-1);
            } else if (groupingUsed_ && ch == u',' && !sawDecimal) {
                if (backup == static_cast<size_t>(-1)) backup = position;
            } else if (ch == u'E' && !sawExponent && sawDigit) {
                size_t p = position + 1;
                bool eneg = false;
                if (p < text.size() && (text[p] == u'-' || text[p] == u'+')) {
                    eneg = text[p] == u'-';
                    p++;
                }
                size_t q = p;
                int64_t ev = 0;
                while (q < text.size() && text[q] >= u'0' && text[q] <= u'9') {
                    if (ev < 100000000) ev = ev * 10 + (text[q] - u'0');
                    q++;
                }
                if (q > p) {
                    exponent = eneg ? -ev : ev;
                    position = q;
                    sawExponent = true;
                }
                break;
            } else {
                break;
            }
        }
        if (backup != static_cast<size_t>(-1)) position = backup;
        if (!sawDigit) {
            *ppos = static_cast<int32_t>(oldStart);
            return nullptr;
        }
        if (digits.empty()) decimalAt = 0;
        else decimalAt += static_cast<int32_t>(exponent);
        stripTrailingZeros(digits);
    }
    if (gotPositive) gotPositive = regionMatches(text, position, ps);
    if (gotNegative) gotNegative = regionMatches(text, position, ns);
    if (gotPositive && gotNegative) {
        if (ps.size() > ns.size()) gotNegative = false;
        else if (ps.size() < ns.size()) gotPositive = false;
    }
    if (gotPositive == gotNegative) return nullptr;
    position += gotPositive ? ps.size() : ns.size();
    *ppos = static_cast<int32_t>(position);
    const bool positive = gotPositive;
    if (infinity) return Double::valueOf(positive ? INFINITY : -INFINITY);
    // integral and fits into a long?
    bool fits = false;
    int64_t longResult = 0;
    if (digits.empty()) {
        fits = positive || parseIntegerOnly_;
    } else if (decimalAt >= static_cast<int32_t>(digits.size()) && decimalAt <= 19) {
        std::string full = digits + std::string(static_cast<size_t>(decimalAt) - digits.size(), '0');
        unsigned __int128 v = 0;
        for (char c : full) v = v * 10 + static_cast<unsigned>(c - '0');
        if (v <= static_cast<unsigned __int128>(INT64_MAX) || (!positive && v == static_cast<unsigned __int128>(INT64_MAX) + 1)) {
            fits = true;
            longResult = positive ? static_cast<int64_t>(v) : static_cast<int64_t>(-static_cast<__int128>(v));
        }
    }
    double doubleResult = 0;
    if (!fits) {
        std::string s = digits.empty() ? std::string("0") : ("0." + digits + "e" + std::to_string(decimalAt));
        doubleResult = std::strtod(s.c_str(), nullptr);
        if (!positive) doubleResult = -doubleResult;
    }
    bool gotDouble = !fits;
    if (multiplier_ != 1) {
        if (gotDouble) {
            doubleResult /= multiplier_;
        } else if (longResult % multiplier_ == 0) {
            longResult /= multiplier_;
        } else {
            doubleResult = static_cast<double>(longResult) / multiplier_;
            gotDouble = true;
        }
        if (gotDouble) {
            longResult = d2l(doubleResult);
            gotDouble = ((doubleResult != static_cast<double>(longResult)) || (doubleResult == 0.0 && std::signbit(doubleResult))) &&
                        !parseIntegerOnly_;
        }
    }
    if (gotDouble) return Double::valueOf(doubleResult);
    return Long::valueOf(longResult);
}

Number* DecimalFormat::parse(const String& source) {
    if (source == nullptr) detail::throwNullPointerException();
    int32_t pos = 0;
    Number* result = parse(source, &pos);
    if (result == nullptr || pos == 0) throw ParseException(str("Unparseable number: \"", source, "\""), pos);
    return result;
}

bool DecimalFormat::equals(Object* obj) {
    auto* o = dynamic_cast<DecimalFormat*>(obj);
    if (o == nullptr) return false;
    return posPrefix_.equals(o->posPrefix_) && posSuffix_.equals(o->posSuffix_) && negPrefix_.equals(o->negPrefix_) &&
           negSuffix_.equals(o->negSuffix_) && minInt_ == o->minInt_ && maxInt_ == o->maxInt_ && minFrac_ == o->minFrac_ &&
           maxFrac_ == o->maxFrac_ && groupingSize_ == o->groupingSize_ && groupingUsed_ == o->groupingUsed_ &&
           multiplier_ == o->multiplier_ && useExponential_ == o->useExponential_ &&
           decimalSeparatorAlwaysShown_ == o->decimalSeparatorAlwaysShown_ && roundingMode_ == o->roundingMode_;
}

int32_t DecimalFormat::hashCode() { return maxInt_ * 37 + posPrefix_.hashCode(); }

}  // namespace jlang
