// java.text.SimpleDateFormat (a port of OpenJDK 8's compile/subFormat/subParse and
// CalendarBuilder, with the English DateFormatSymbols).
#include "time_internal.h"

#include <cstring>
#include <string>

namespace jlang {

namespace detail {
TimeZone* defaultTimeZoneRef();
}

using namespace detail::tm;

namespace {

// "GyMdkHmsSEDFwWahKzZYuXL"
constexpr char16_t PATTERN_CHARS[] = u"GyMdkHmsSEDFwWahKzZYuXL";
enum PatternIndex {
    P_ERA = 0,
    P_YEAR,
    P_MONTH,
    P_DAY_OF_MONTH,
    P_HOUR_OF_DAY1,
    P_HOUR_OF_DAY0,
    P_MINUTE,
    P_SECOND,
    P_MILLISECOND,
    P_DAY_OF_WEEK,
    P_DAY_OF_YEAR,
    P_DAY_OF_WEEK_IN_MONTH,
    P_WEEK_OF_YEAR,
    P_WEEK_OF_MONTH,
    P_AM_PM,
    P_HOUR1,
    P_HOUR0,
    P_ZONE_NAME,
    P_ZONE_VALUE,
    P_WEEK_YEAR,
    P_ISO_DAY_OF_WEEK,
    P_ISO_ZONE,
    P_MONTH_STANDALONE
};
constexpr int32_t WEEK_YEAR = Calendar::FIELD_COUNT;           // 17
constexpr int32_t ISO_DAY_OF_WEEK = Calendar::FIELD_COUNT + 1;  // 18
const int32_t PATTERN_INDEX_TO_CALENDAR_FIELD[] = {
    Calendar::ERA,         Calendar::YEAR,         Calendar::MONTH,       Calendar::DATE,
    Calendar::HOUR_OF_DAY, Calendar::HOUR_OF_DAY,  Calendar::MINUTE,      Calendar::SECOND,
    Calendar::MILLISECOND, Calendar::DAY_OF_WEEK,  Calendar::DAY_OF_YEAR, Calendar::DAY_OF_WEEK_IN_MONTH,
    Calendar::WEEK_OF_YEAR, Calendar::WEEK_OF_MONTH, Calendar::AM_PM,     Calendar::HOUR,
    Calendar::HOUR,        Calendar::ZONE_OFFSET,  Calendar::ZONE_OFFSET, WEEK_YEAR,
    ISO_DAY_OF_WEEK,       Calendar::ZONE_OFFSET,  Calendar::MONTH};

int32_t patternIndexOf(char16_t c) {
    for (int32_t i = 0; PATTERN_CHARS[i] != 0; i++)
        if (PATTERN_CHARS[i] == c) return i;
    return -1;
}

inline bool isAsciiLetter(char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }
inline bool isDigit16(char16_t c) { return c >= u'0' && c <= u'9'; }

// java.util.CalendarBuilder
struct CalendarBuilder {
    static constexpr int32_t MAX_FIELD = Calendar::FIELD_COUNT + 1;
    int32_t stamp[MAX_FIELD] = {};
    int32_t value[MAX_FIELD] = {};
    int32_t nextStamp = 2;
    int32_t maxFieldIndex = -1;

    static int32_t toCalendarDayOfWeek(int32_t isoDayOfWeek) {
        if (isoDayOfWeek < 1 || isoDayOfWeek > 7) return isoDayOfWeek;
        return isoDayOfWeek == 7 ? Calendar::SUNDAY : isoDayOfWeek + 1;
    }
    CalendarBuilder& set(int32_t index, int32_t v) {
        if (index == ISO_DAY_OF_WEEK) {
            index = Calendar::DAY_OF_WEEK;
            v = toCalendarDayOfWeek(v);
        }
        stamp[index] = nextStamp++;
        value[index] = v;
        if (index > maxFieldIndex && index < Calendar::FIELD_COUNT) maxFieldIndex = index;
        return *this;
    }
    CalendarBuilder& clear(int32_t index) {
        stamp[index] = value[index] = 0;
        return *this;
    }
    bool isSet(int32_t index) { return stamp[index] > 0; }
    CalendarBuilder& addYear(int32_t v) {
        value[Calendar::YEAR] += v;
        value[WEEK_YEAR] += v;
        return *this;
    }
    Calendar* establish(Calendar* cal) {
        bool weekDate = isSet(WEEK_YEAR) && stamp[WEEK_YEAR] > stamp[Calendar::YEAR];
        cal->clear();
        for (int32_t s = 2; s < nextStamp; s++) {
            for (int32_t index = 0; index <= maxFieldIndex; index++) {
                if (stamp[index] == s) {
                    cal->set(index, value[index]);
                    break;
                }
            }
        }
        if (weekDate) {
            int32_t weekOfYear = isSet(Calendar::WEEK_OF_YEAR) ? value[Calendar::WEEK_OF_YEAR] : 1;
            int32_t dayOfWeek = isSet(Calendar::DAY_OF_WEEK) ? value[Calendar::DAY_OF_WEEK] : cal->getFirstDayOfWeek();
            // Calendar.setWeekDate(weekYear, weekOfYear, dayOfWeek)
            cal->set(Calendar::DAY_OF_WEEK, cal->getFirstDayOfWeek());
            cal->set(Calendar::YEAR, value[WEEK_YEAR]);
            cal->set(Calendar::WEEK_OF_YEAR, 1);
            cal->set(Calendar::MONTH, Calendar::JANUARY);
            cal->set(Calendar::DAY_OF_MONTH, 1);
            // anchor on the first week of the week year
            auto* tmp = dynamic_cast<Calendar*>(cal->clone());
            tmp->setLenient(true);
            tmp->clear();
            tmp->set(Calendar::YEAR, value[WEEK_YEAR]);
            tmp->set(Calendar::WEEK_OF_YEAR, weekOfYear);
            tmp->set(Calendar::DAY_OF_WEEK, dayOfWeek);
            cal->setTimeInMillis(tmp->getTimeInMillis());
        }
        return cal;
    }
};

// Parses an optionally signed integer (DecimalFormat with parseIntegerOnly and no grouping)
// starting at pos, not beyond limit. Returns false when no digits.
bool parseInteger(const std::u16string& text, int32_t& pos, int32_t limit, int64_t& out) {
    int32_t i = pos;
    bool neg = false;
    if (i < limit && text[i] == u'-') {
        neg = true;
        i++;
    }
    int32_t digitsStart = i;
    int64_t v = 0;
    while (i < limit && isDigit16(text[i])) {
        v = v * 10 + (text[i] - u'0');
        i++;
    }
    if (i == digitsStart) return false;
    out = neg ? -v : v;
    pos = i;
    return true;
}

bool regionMatchesIgnoreCase(const std::u16string& text, int32_t start, const std::u16string& s) {
    if (start < 0 || start + static_cast<int32_t>(s.size()) > static_cast<int32_t>(text.size())) return false;
    for (size_t k = 0; k < s.size(); k++) {
        char16_t a = text[start + k], b = s[k];
        if (a == b) continue;
        auto lower = [](char16_t c) -> char16_t { return (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c + 32) : c; };
        if (lower(a) != lower(b)) return false;
    }
    return true;
}

std::u16string u16(const char* s) {
    std::u16string r;
    while (*s) r.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s++)));
    return r;
}

int32_t matchString(const std::u16string& text, int32_t start, int32_t field, const char* const* data, int32_t count,
                    CalendarBuilder& calb) {
    int32_t i = field == Calendar::DAY_OF_WEEK ? 1 : 0;
    int32_t bestMatchLength = 0, bestMatch = -1;
    for (; i < count; ++i) {
        std::u16string d = u16(data[i]);
        int32_t length = static_cast<int32_t>(d.size());
        if (length > bestMatchLength && regionMatchesIgnoreCase(text, start, d)) {
            bestMatch = i;
            bestMatchLength = length;
        }
    }
    if (bestMatch >= 0) {
        calb.set(field, bestMatch);
        return start + bestMatchLength;
    }
    return -start;
}

int32_t subParseNumericZone(const std::u16string& text, int32_t start, int32_t sign, int32_t count, bool colon,
                            CalendarBuilder& calb) {
    int32_t index = start;
    const int32_t n = static_cast<int32_t>(text.size());
    auto at = [&](int32_t k) -> int32_t { return k < n ? text[k] : -1; };
    do {
        int32_t c = at(index++);
        if (c < 0 || !isDigit16(static_cast<char16_t>(c))) break;
        int32_t hours = c - '0';
        c = at(index++);
        if (c < 0) break;
        if (isDigit16(static_cast<char16_t>(c))) {
            hours = hours * 10 + (c - '0');
        } else {
            if (count > 0 || !colon) break;
            --index;
        }
        if (hours > 23) break;
        int32_t minutes = 0;
        if (count != 1) {
            c = at(index++);
            if (c < 0) break;
            if (colon) {
                if (c != ':') break;
                c = at(index++);
                if (c < 0) break;
            }
            if (!isDigit16(static_cast<char16_t>(c))) break;
            minutes = c - '0';
            c = at(index++);
            if (c < 0 || !isDigit16(static_cast<char16_t>(c))) break;
            minutes = minutes * 10 + (c - '0');
            if (minutes > 59) break;
        }
        minutes += hours * 60;
        calb.set(Calendar::ZONE_OFFSET, minutes * 60000 * sign).set(Calendar::DST_OFFSET, 0);
        return index;
    } while (false);
    return 1 - index;
}

struct ZoneAbbr {
    const char* name;
    int32_t raw;  // minutes
    int32_t dst;  // minutes
};
const ZoneAbbr ZONE_ABBRS[] = {
    {"UTC", 0, 0},        {"GMT", 0, 0},       {"WET", 0, 0},        {"WEST", 0, 60},     {"BST", 0, 60},
    {"CET", 60, 0},       {"CEST", 60, 60},    {"MET", 60, 0},       {"MEST", 60, 60},    {"EET", 120, 0},
    {"EEST", 120, 60},    {"MSK", 180, 0},     {"EST", -300, 0},     {"EDT", -300, 60},   {"CST", -360, 0},
    {"CDT", -360, 60},    {"MST", -420, 0},    {"MDT", -420, 60},    {"PST", -480, 0},    {"PDT", -480, 60},
    {"AKST", -540, 0},    {"AKDT", -540, 60},  {"HST", -600, 0},     {"KST", 540, 0},     {"KDT", 540, 60},
    {"JST", 540, 0},      {"IST", 330, 0},     {"AEST", 600, 0},     {"AEDT", 600, 60},   {"NZST", 720, 0},
    {"NZDT", 720, 60},
};

int32_t subParseZoneString(const std::u16string& text, int32_t start, CalendarBuilder& calb, TimeZone* zone) {
    // the formatter's zone names first (long, then short), then a table of abbreviations
    struct Cand {
        std::u16string name;
        int32_t raw, dst;
    };
    std::vector<Cand> cands;
    int32_t raw = zone->getRawOffset();
    int32_t save = zone->getDSTSavings();
    cands.push_back({String(zone->getDisplayName(false, TimeZone::LONG)).toUtf16(), raw, 0});
    cands.push_back({String(zone->getDisplayName(false, TimeZone::SHORT)).toUtf16(), raw, 0});
    if (zone->useDaylightTime()) {
        cands.push_back({String(zone->getDisplayName(true, TimeZone::LONG)).toUtf16(), raw, save});
        cands.push_back({String(zone->getDisplayName(true, TimeZone::SHORT)).toUtf16(), raw, save});
    }
    for (const auto& z : ZONE_ABBRS) cands.push_back({u16(z.name), z.raw * 60000, z.dst * 60000});
    int32_t best = -1;
    size_t bestLen = 0;
    for (size_t i = 0; i < cands.size(); i++) {
        if (cands[i].name.size() > bestLen && regionMatchesIgnoreCase(text, start, cands[i].name)) {
            best = static_cast<int32_t>(i);
            bestLen = cands[i].name.size();
        }
    }
    if (best < 0) return -start;
    calb.set(Calendar::ZONE_OFFSET, cands[best].raw).set(Calendar::DST_OFFSET, cands[best].dst);
    return start + static_cast<int32_t>(bestLen);
}

void zeroPaddingNumber(std::u16string& out, int64_t value, int32_t minDigits, int32_t maxDigits) {
    std::string s;
    if (value < 0) {
        s.push_back('-');
        value = -value;
    }
    std::string digits;
    appendPadded(digits, value, 1);
    if (maxDigits == 2 && digits.size() > 2) digits = digits.substr(digits.size() - 2);
    while (static_cast<int32_t>(digits.size()) < minDigits) digits.insert(digits.begin(), '0');
    s += digits;
    for (char ch : s) out.push_back(static_cast<char16_t>(ch));
}

void appendAscii(std::u16string& out, const char* s) {
    while (*s) out.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s++)));
}
void appendUtf8(std::u16string& out, const String& s) { out += s.toUtf16(); }

}  // namespace

// ---------------------------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat() : SimpleDateFormat(String("M/d/yy h:mm a"), Locale::getDefault()) {}
SimpleDateFormat::SimpleDateFormat(const String& pattern) : SimpleDateFormat(pattern, Locale::getDefault()) {}

SimpleDateFormat::SimpleDateFormat(const String& pattern, Locale* locale) : pattern_(pattern), locale_(locale) {
    if (pattern == nullptr || locale == nullptr) detail::throwNullPointerException();
    calendar_ = Calendar::getInstance(locale);
    compile();
    // default century: 80 years before now
    auto* c = Calendar::getInstance();
    c->add(Calendar::YEAR, -80);
    defaultCenturyStart_ = c->getTimeInMillis();
    defaultCenturyStartYear_ = c->get(Calendar::YEAR);
}

static const char* const DATE_STYLES[] = {"EEEE, MMMM d, yyyy", "MMMM d, yyyy", "MMM d, yyyy", "M/d/yy"};
static const char* const TIME_STYLES[] = {"h:mm:ss a z", "h:mm:ss a z", "h:mm:ss a", "h:mm a"};

static void checkStyle(int32_t style) {
    if (style < 0 || style > 3) throw IllegalArgumentException(str("Illegal date style ", style));
}

SimpleDateFormat* SimpleDateFormat::getInstance() { return getDateTimeInstance(SHORT, SHORT); }
SimpleDateFormat* SimpleDateFormat::getDateInstance() { return getDateInstance(DEFAULT); }
SimpleDateFormat* SimpleDateFormat::getDateInstance(int32_t style) { return getDateInstance(style, Locale::getDefault()); }
SimpleDateFormat* SimpleDateFormat::getDateInstance(int32_t style, Locale* locale) {
    checkStyle(style);
    return new SimpleDateFormat(String(DATE_STYLES[style]), locale);
}
SimpleDateFormat* SimpleDateFormat::getTimeInstance() { return getTimeInstance(DEFAULT); }
SimpleDateFormat* SimpleDateFormat::getTimeInstance(int32_t style) { return getTimeInstance(style, Locale::getDefault()); }
SimpleDateFormat* SimpleDateFormat::getTimeInstance(int32_t style, Locale* locale) {
    checkStyle(style);
    return new SimpleDateFormat(String(TIME_STYLES[style]), locale);
}
SimpleDateFormat* SimpleDateFormat::getDateTimeInstance() { return getDateTimeInstance(DEFAULT, DEFAULT); }
SimpleDateFormat* SimpleDateFormat::getDateTimeInstance(int32_t dateStyle, int32_t timeStyle) {
    return getDateTimeInstance(dateStyle, timeStyle, Locale::getDefault());
}
SimpleDateFormat* SimpleDateFormat::getDateTimeInstance(int32_t dateStyle, int32_t timeStyle, Locale* locale) {
    checkStyle(dateStyle);
    checkStyle(timeStyle);
    return new SimpleDateFormat(str(DATE_STYLES[dateStyle], " ", TIME_STYLES[timeStyle]), locale);
}

void SimpleDateFormat::applyPattern(const String& pattern) {
    if (pattern == nullptr) detail::throwNullPointerException();
    String old = pattern_;
    pattern_ = pattern;
    try {
        compile();
    } catch (...) {
        pattern_ = old;
        compile();
        throw;
    }
}

// SimpleDateFormat.compile: tokens of pattern letters and literal text.
void SimpleDateFormat::compile() {
    std::u16string p = pattern_.toUtf16();
    std::vector<Token> tokens;
    std::u16string literal;
    bool inQuote = false;
    int32_t lastTag = -1;
    int32_t count = 0;
    auto flushField = [&]() {
        if (count != 0) {
            tokens.push_back(Token{static_cast<char32_t>(PATTERN_CHARS[lastTag]), count, String()});
            count = 0;
            lastTag = -1;
        }
    };
    auto flushLiteral = [&]() {
        if (!literal.empty()) {
            tokens.push_back(Token{0, static_cast<int32_t>(literal.size()), String::fromUtf16(literal)});
            literal.clear();
        }
    };
    const size_t len = p.size();
    for (size_t i = 0; i < len; i++) {
        char16_t c = p[i];
        if (c == u'\'') {
            if (i + 1 < len && p[i + 1] == u'\'') {
                i++;
                flushField();
                literal.push_back(u'\'');
                continue;
            }
            if (!inQuote) {
                flushField();
                inQuote = true;
            } else {
                inQuote = false;
            }
            continue;
        }
        if (inQuote) {
            literal.push_back(c);
            continue;
        }
        if (!isAsciiLetter(c)) {
            flushField();
            literal.push_back(c);
            continue;
        }
        int32_t tag = patternIndexOf(c);
        if (tag == -1) {
            std::u16string bad(1, c);
            throw IllegalArgumentException(str("Illegal pattern character '", String::fromUtf16(bad), "'"));
        }
        flushLiteral();
        if (lastTag == -1 || lastTag == tag) {
            lastTag = tag;
            count++;
            continue;
        }
        flushField();
        lastTag = tag;
        count = 1;
    }
    if (inQuote) throw IllegalArgumentException(String("Unterminated quote"));
    flushField();
    flushLiteral();
    tokens_ = std::move(tokens);
}

void SimpleDateFormat::subFormat(String& out0, const Token& t, Calendar* calendar) {
    std::u16string out;
    int32_t patternCharIndex = patternIndexOf(static_cast<char16_t>(t.letter));
    int32_t count = t.count;
    const int32_t maxIntCount = 2147483647;
    int32_t field = PATTERN_INDEX_TO_CALENDAR_FIELD[patternCharIndex];
    int32_t value;
    if (field == WEEK_YEAR) {
        value = calendar->getWeekYear();
    } else if (field == ISO_DAY_OF_WEEK) {
        int32_t dow = calendar->get(Calendar::DAY_OF_WEEK);
        value = dow == Calendar::SUNDAY ? 7 : dow - 1;
    } else {
        value = calendar->get(field);
    }
    switch (patternCharIndex) {
        case P_ERA:
            appendAscii(out, value >= 0 && value < 2 ? ERA_NAMES[value] : "");
            break;
        case P_WEEK_YEAR:
        case P_YEAR:
            if (count != 2) zeroPaddingNumber(out, value, count, maxIntCount);
            else zeroPaddingNumber(out, value, 2, 2);
            break;
        case P_MONTH:
        case P_MONTH_STANDALONE:
            if (count >= 4) appendAscii(out, MONTH_NAMES[value]);
            else if (count == 3) appendAscii(out, SHORT_MONTH_NAMES[value]);
            else zeroPaddingNumber(out, value + 1, count, maxIntCount);
            break;
        case P_HOUR_OF_DAY1:
            if (value == 0) zeroPaddingNumber(out, calendar->getMaximum(Calendar::HOUR_OF_DAY) + 1, count, maxIntCount);
            else zeroPaddingNumber(out, value, count, maxIntCount);
            break;
        case P_DAY_OF_WEEK:
            appendAscii(out, count >= 4 ? WEEKDAY_NAMES[value] : SHORT_WEEKDAY_NAMES[value]);
            break;
        case P_AM_PM:
            appendAscii(out, AMPM_NAMES[value]);
            break;
        case P_HOUR1:
            if (value == 0) zeroPaddingNumber(out, calendar->getLeastMaximum(Calendar::HOUR) + 1, count, maxIntCount);
            else zeroPaddingNumber(out, value, count, maxIntCount);
            break;
        case P_ZONE_NAME: {
            TimeZone* tz = calendar->getTimeZone();
            bool daylight = calendar->get(Calendar::DST_OFFSET) != 0;
            appendUtf8(out, tz->getDisplayName(daylight, count < 4 ? TimeZone::SHORT : TimeZone::LONG));
            break;
        }
        case P_ZONE_VALUE: {
            value = (calendar->get(Calendar::ZONE_OFFSET) + calendar->get(Calendar::DST_OFFSET)) / 60000;
            // CalendarUtils.sprintf0d(num, width) with width 4 digits after the sign
            int32_t num = (value / 60) * 100 + (value % 60);
            if (value >= 0) out.push_back(u'+');
            else {
                out.push_back(u'-');
                num = -num;
            }
            std::string s;
            appendPadded(s, num, 4);
            for (char ch : s) out.push_back(static_cast<char16_t>(ch));
            break;
        }
        case P_ISO_ZONE: {
            value = calendar->get(Calendar::ZONE_OFFSET) + calendar->get(Calendar::DST_OFFSET);
            if (value == 0) {
                out.push_back(u'Z');
                break;
            }
            value /= 60000;
            if (value >= 0) {
                out.push_back(u'+');
            } else {
                out.push_back(u'-');
                value = -value;
            }
            std::string s;
            appendPadded(s, value / 60, 2);
            if (count != 1) {
                if (count == 3) s.push_back(':');
                appendPadded(s, value % 60, 2);
            }
            for (char ch : s) out.push_back(static_cast<char16_t>(ch));
            break;
        }
        default:
            zeroPaddingNumber(out, value, count, maxIntCount);
            break;
    }
    out0 += String::fromUtf16(out);
}

String SimpleDateFormat::format(Date* date) {
    if (date == nullptr) detail::throwNullPointerException();
    calendar_->setTime(date);
    String out("");
    for (const Token& t : tokens_) {
        if (t.letter == 0) out += t.text;
        else subFormat(out, t, calendar_);
    }
    return out;
}

String SimpleDateFormat::format(int64_t millis) {
    Date d(millis);
    return format(&d);
}

String SimpleDateFormat::format(Object* obj) {
    if (auto* d = dynamic_cast<Date*>(obj)) return format(d);
    if (auto* n = dynamic_cast<Number*>(obj)) return format(n->longValue());
    throw IllegalArgumentException(String("Cannot format given Object as a Date"));
}

namespace {

struct ParseCtx {
    const std::u16string& text;
    Calendar* calendar;
    CalendarBuilder& calb;
    bool* ambiguousYear;
    int32_t defaultCenturyStartYear;
    int32_t* errorIndex;
};

// SimpleDateFormat.subParse; returns the new position or -1 (errorIndex set).
int32_t subParseImpl(ParseCtx& ctx, int32_t start, int32_t patternCharIndex, int32_t count, bool obeyCount) {
    const std::u16string& text = ctx.text;
    const int32_t textLength = static_cast<int32_t>(text.size());
    int64_t number = 0;
    bool haveNumber = false;
    int32_t value = 0;
    int32_t pos = start;
    int32_t field = PATTERN_INDEX_TO_CALENDAR_FIELD[patternCharIndex];
    for (;;) {
        if (pos >= textLength) {
            *ctx.errorIndex = start;
            return -1;
        }
        char16_t c = text[pos];
        if (c != u' ' && c != u'\t') break;
        ++pos;
    }
    int32_t actualStart = pos;
    CalendarBuilder& calb = ctx.calb;
    do {  // "parsing:" block; break = failure
        if (patternCharIndex == P_HOUR_OF_DAY1 || patternCharIndex == P_HOUR1 ||
            (patternCharIndex == P_MONTH && count <= 2) || patternCharIndex == P_YEAR ||
            patternCharIndex == P_WEEK_YEAR) {
            if (obeyCount) {
                if ((start + count) > textLength) break;
                haveNumber = parseInteger(text, pos, start + count, number);
            } else {
                haveNumber = parseInteger(text, pos, textLength, number);
            }
            if (!haveNumber) break;
            value = static_cast<int32_t>(number);
        }
        int32_t index;
        switch (patternCharIndex) {
            case P_ERA:
                if ((index = matchString(text, start, Calendar::ERA, ERA_NAMES, 2, calb)) > 0) return index;
                pos = start;
                break;
            case P_WEEK_YEAR:
            case P_YEAR:
                if (count <= 2 && (pos - actualStart) == 2 && isDigit16(text[actualStart]) &&
                    isDigit16(text[actualStart + 1])) {
                    int32_t ambiguousTwoDigitYear = ctx.defaultCenturyStartYear % 100;
                    *ctx.ambiguousYear = value == ambiguousTwoDigitYear;
                    value += (ctx.defaultCenturyStartYear / 100) * 100 + (value < ambiguousTwoDigitYear ? 100 : 0);
                }
                calb.set(field, value);
                return pos;
            case P_MONTH:
            case P_MONTH_STANDALONE: {
                if (count <= 2 && patternCharIndex == P_MONTH) {
                    calb.set(Calendar::MONTH, value - 1);
                    return pos;
                }
                if (patternCharIndex == P_MONTH_STANDALONE && count <= 2) {
                    if (!parseInteger(text, pos, obeyCount ? std::min(textLength, start + count) : textLength, number))
                        break;
                    calb.set(Calendar::MONTH, static_cast<int32_t>(number) - 1);
                    return pos;
                }
                int32_t newStart;
                if ((newStart = matchString(text, start, Calendar::MONTH, MONTH_NAMES, 13, calb)) > 0) return newStart;
                if ((newStart = matchString(text, start, Calendar::MONTH, SHORT_MONTH_NAMES, 13, calb)) > 0) return newStart;
                pos = start;
                break;
            }
            case P_HOUR_OF_DAY1:
                if (!ctx.calendar->isLenient()) {
                    if (value < 1 || value > 24) break;
                }
                if (value == ctx.calendar->getMaximum(Calendar::HOUR_OF_DAY) + 1) value = 0;
                calb.set(Calendar::HOUR_OF_DAY, value);
                return pos;
            case P_DAY_OF_WEEK: {
                int32_t newStart;
                if ((newStart = matchString(text, start, Calendar::DAY_OF_WEEK, WEEKDAY_NAMES, 8, calb)) > 0) return newStart;
                if ((newStart = matchString(text, start, Calendar::DAY_OF_WEEK, SHORT_WEEKDAY_NAMES, 8, calb)) > 0)
                    return newStart;
                pos = start;
                break;
            }
            case P_AM_PM:
                if ((index = matchString(text, start, Calendar::AM_PM, AMPM_NAMES, 2, calb)) > 0) return index;
                pos = start;
                break;
            case P_HOUR1:
                if (!ctx.calendar->isLenient()) {
                    if (value < 1 || value > 12) break;
                }
                if (value == ctx.calendar->getLeastMaximum(Calendar::HOUR) + 1) value = 0;
                calb.set(Calendar::HOUR, value);
                return pos;
            case P_ZONE_NAME:
            case P_ZONE_VALUE: {
                int32_t sign = 0;
                if (pos >= textLength) break;
                char16_t c = text[pos];
                if (c == u'+') sign = 1;
                else if (c == u'-') sign = -1;
                if (sign == 0) {
                    static const std::u16string GMT = u"GMT";
                    if ((c == u'G' || c == u'g') && (textLength - start) >= 3 && regionMatchesIgnoreCase(text, start, GMT)) {
                        pos = start + 3;
                        if ((textLength - pos) > 0) {
                            c = text[pos];
                            if (c == u'+') sign = 1;
                            else if (c == u'-') sign = -1;
                        }
                        if (sign == 0) {
                            calb.set(Calendar::ZONE_OFFSET, 0).set(Calendar::DST_OFFSET, 0);
                            return pos;
                        }
                        int32_t i = subParseNumericZone(text, ++pos, sign, 0, true, calb);
                        if (i > 0) return i;
                        pos = -i;
                    } else {
                        int32_t i = subParseZoneString(text, pos, calb, ctx.calendar->getTimeZone());
                        if (i > 0) return i;
                        pos = -i;
                    }
                } else {
                    int32_t i = subParseNumericZone(text, ++pos, sign, 0, false, calb);
                    if (i > 0) return i;
                    pos = -i;
                }
                break;
            }
            case P_ISO_ZONE: {
                if ((textLength - pos) <= 0) break;
                int32_t sign;
                char16_t c = text[pos];
                if (c == u'Z') {
                    calb.set(Calendar::ZONE_OFFSET, 0).set(Calendar::DST_OFFSET, 0);
                    return ++pos;
                }
                if (c == u'+') sign = 1;
                else if (c == u'-') sign = -1;
                else {
                    ++pos;
                    break;
                }
                int32_t i = subParseNumericZone(text, ++pos, sign, count, count == 3, calb);
                if (i > 0) return i;
                pos = -i;
                break;
            }
            default: {
                bool ok;
                if (obeyCount) {
                    if ((start + count) > textLength) break;
                    ok = parseInteger(text, pos, start + count, number);
                } else {
                    ok = parseInteger(text, pos, textLength, number);
                }
                if (ok) {
                    calb.set(field, static_cast<int32_t>(number));
                    return pos;
                }
                break;
            }
        }
    } while (false);
    *ctx.errorIndex = pos;
    return -1;
}

}  // namespace

Date* SimpleDateFormat::parse(const String& source, int32_t* ppos, int32_t* errorIndex) {
    std::u16string text = source.toUtf16();
    int32_t start = *ppos;
    int32_t oldStart = start;
    int32_t textLength = static_cast<int32_t>(text.size());
    int32_t errIdx = -1;
    bool ambiguousYear = false;
    CalendarBuilder calb;
    ParseCtx ctx{text, calendar_, calb, &ambiguousYear, defaultCenturyStartYear_, &errIdx};
    for (size_t ti = 0; ti < tokens_.size(); ti++) {
        const Token& t = tokens_[ti];
        if (t.letter == 0) {
            std::u16string lit = t.text.toUtf16();
            for (char16_t ch : lit) {
                if (start >= textLength || text[start] != ch) {
                    if (errorIndex != nullptr) *errorIndex = start;
                    *ppos = oldStart;
                    return nullptr;
                }
                start++;
            }
            continue;
        }
        bool obeyCount = false;
        if (ti + 1 < tokens_.size() && tokens_[ti + 1].letter != 0) obeyCount = true;
        start = subParseImpl(ctx, start, patternIndexOf(static_cast<char16_t>(t.letter)), t.count, obeyCount);
        if (start < 0) {
            if (errorIndex != nullptr) *errorIndex = errIdx;
            *ppos = oldStart;
            return nullptr;
        }
    }
    try {
        int64_t millis = calb.establish(calendar_)->getTimeInMillis();
        if (ambiguousYear && millis < defaultCenturyStart_) {
            millis = calb.addYear(100).establish(calendar_)->getTimeInMillis();
        }
        *ppos = start;
        return new Date(millis);
    } catch (IllegalArgumentException&) {
        if (errorIndex != nullptr) *errorIndex = start;
        *ppos = oldStart;
        return nullptr;
    }
}

Date* SimpleDateFormat::parse(const String& source) {
    if (source == nullptr) detail::throwNullPointerException();
    int32_t pos = 0;
    int32_t errorIndex = -1;
    Date* result = parse(source, &pos, &errorIndex);
    if (pos == 0) throw ParseException(str("Unparseable date: \"", source, "\""), errorIndex);
    return result;
}

void SimpleDateFormat::set2DigitYearStart(Date* startDate) {
    if (startDate == nullptr) detail::throwNullPointerException();
    auto* c = Calendar::getInstance(calendar_->getTimeZone());
    c->setTime(startDate);
    defaultCenturyStart_ = startDate->getTime();
    defaultCenturyStartYear_ = c->get(Calendar::YEAR);
}

Date* SimpleDateFormat::get2DigitYearStart() { return new Date(defaultCenturyStart_); }

bool SimpleDateFormat::equals(Object* obj) {
    auto* o = dynamic_cast<SimpleDateFormat*>(obj);
    return o != nullptr && pattern_.equals(o->pattern_);
}

int32_t SimpleDateFormat::hashCode() { return pattern_.hashCode(); }

Object* SimpleDateFormat::clone() {
    auto* c = new SimpleDateFormat(*this);
    c->calendar_ = dynamic_cast<Calendar*>(calendar_->clone());
    return c;
}

}  // namespace jlang
