// log4j emulation, part 2: layouts (SimpleLayout, PatternLayout, EnhancedPatternLayout), the
// pattern parsers of log4j 1.2.16 (classic single-letter and "enhanced" conversion words) and
// a java.text.SimpleDateFormat formatter for %d. See <jlang/Log.h>.
#include <jlang/Log.h>

#include "log_internal.h"

#include <cctype>
#include <climits>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>

namespace jlang::log4j {

// =======================================================================================
// SimpleDateFormat
namespace impl {

namespace {

const char* const kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                               "July",    "August",   "September", "October", "November", "December"};
const char* const kMonthsShort[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kDaysShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char kPatternChars[] = "GyMdkHmsSEDFwWahKzZYuXL";

void pad(std::string& out, int64_t value, int minDigits) {
    std::string d = std::to_string(value < 0 ? -value : value);
    if (value < 0) out.push_back('-');
    for (int i = static_cast<int>(d.size()); i < minDigits; i++) out.push_back('0');
    out += d;
}

bool isLeap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

}  // namespace

DateFormatter::DateFormatter(const std::string& pattern) {
    bool inQuote = false;
    std::string quoted;
    size_t n = pattern.size();
    auto addLiteral = [&](const std::string& t) {
        if (!tokens_.empty() && tokens_.back().letter == 0) tokens_.back().text += t;
        else tokens_.push_back(Token{0, 0, t});
    };
    for (size_t i = 0; i < n; i++) {
        char c = pattern[i];
        if (c == '\'') {
            if (i + 1 < n && pattern[i + 1] == '\'') {  // '' is a quote, inside or outside quotes
                i++;
                if (inQuote) quoted.push_back('\'');
                else addLiteral("'");
                continue;
            }
            if (inQuote) {
                addLiteral(quoted);
                quoted.clear();
            }
            inQuote = !inQuote;
            continue;
        }
        if (inQuote) {
            quoted.push_back(c);
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            addLiteral(std::string(1, c));
            continue;
        }
        if (std::strchr(kPatternChars, c) == nullptr)
            throw IllegalArgumentException(str("Illegal pattern character '", c, "'"));
        int count = 1;
        while (i + 1 < n && pattern[i + 1] == c) {
            count++;
            i++;
        }
        if (c == 'X' && count > 3) throw IllegalArgumentException(str("invalid ISO 8601 format: length=", count));
        tokens_.push_back(Token{c, count, std::string()});
    }
    if (inQuote) throw IllegalArgumentException(String("Unterminated quote"));
}

void DateFormatter::setFixedZone(int32_t offsetMinutes, const std::string& name) {
    fixedZone_ = true;
    fixedOffset_ = offsetMinutes;
    fixedName_ = name;
}

void DateFormatter::format(int64_t millis, std::string& out) const {
    int64_t secs = floorDiv(millis, 1000);
    int ms = static_cast<int>(millis - secs * 1000);
    std::tm tm{};
    long gmtoff = 0;
    std::string zone;
    if (fixedZone_) {
        std::time_t t = static_cast<std::time_t>(secs + static_cast<int64_t>(fixedOffset_) * 60);
        gmtime_r(&t, &tm);
        gmtoff = static_cast<long>(fixedOffset_) * 60;
        zone = fixedName_;
    } else {
        std::time_t t = static_cast<std::time_t>(secs);
        localtime_r(&t, &tm);
        gmtoff = tm.tm_gmtoff;
        zone = tm.tm_zone != nullptr ? tm.tm_zone : "";
    }
    int64_t year = tm.tm_year + 1900;
    int yday = tm.tm_yday;  // 0-based
    int wday = tm.tm_wday;  // 0 = Sunday
    // Week numbers with Sunday as first day of week and 1 minimal day in the first week (US).
    int jan1wday = ((wday - yday) % 7 + 7) % 7;
    int weekOfYear = (yday + jan1wday) / 7 + 1;
    int64_t weekYear = year;
    int daysInYear = isLeap(year) ? 366 : 365;
    int nextJan1wday = (jan1wday + daysInYear) % 7;
    if (weekOfYear >= 52 && nextJan1wday != 0 && yday >= daysInYear - nextJan1wday) {
        weekOfYear = 1;
        weekYear = year + 1;
    }
    int mday = tm.tm_mday;
    int month1wday = ((wday - (mday - 1)) % 7 + 7) % 7;
    int weekOfMonth = (mday - 1 + month1wday) / 7 + 1;
    int hour = tm.tm_hour;

    for (const Token& tk : tokens_) {
        int c = tk.count;
        switch (tk.letter) {
            case 0: out += tk.text; break;
            case 'G': out += year > 0 ? "AD" : "BC"; break;
            case 'y':
            case 'Y': {
                int64_t y = tk.letter == 'y' ? year : weekYear;
                if (y <= 0) y = 1 - y;
                if (c == 2) pad(out, y % 100, 2);
                else pad(out, y, c);
                break;
            }
            case 'M':
            case 'L':
                if (c >= 4) out += kMonths[tm.tm_mon];
                else if (c == 3) out += kMonthsShort[tm.tm_mon];
                else pad(out, tm.tm_mon + 1, c);
                break;
            case 'd': pad(out, mday, c); break;
            case 'D': pad(out, yday + 1, c); break;
            case 'F': pad(out, (mday - 1) / 7 + 1, c); break;
            case 'E': out += c >= 4 ? kDays[wday] : kDaysShort[wday]; break;
            case 'u': pad(out, wday == 0 ? 7 : wday, c); break;
            case 'a': out += hour < 12 ? "AM" : "PM"; break;
            case 'H': pad(out, hour, c); break;
            case 'k': pad(out, hour == 0 ? 24 : hour, c); break;
            case 'K': pad(out, hour % 12, c); break;
            case 'h': pad(out, hour % 12 == 0 ? 12 : hour % 12, c); break;
            case 'm': pad(out, tm.tm_min, c); break;
            case 's': pad(out, tm.tm_sec, c); break;
            case 'S': pad(out, ms, c); break;
            case 'w': pad(out, weekOfYear, c); break;
            case 'W': pad(out, weekOfMonth, c); break;
            case 'z': out += zone; break;
            case 'Z':
            case 'X': {
                long off = gmtoff / 60;
                if (tk.letter == 'X' && off == 0) {
                    out.push_back('Z');
                    break;
                }
                out.push_back(off < 0 ? '-' : '+');
                if (off < 0) off = -off;
                pad(out, off / 60, 2);
                if (tk.letter == 'X' && c == 1) break;
                if (tk.letter == 'X' && c == 3) out.push_back(':');
                pad(out, off % 60, 2);
                break;
            }
            default: break;
        }
    }
}

// =======================================================================================
// Pattern converters

struct FormattingInfo {
    int32_t min = 0;
    int32_t max = INT32_MAX;
    bool leftAlign = false;
};

class PatternConverter : public virtual Object {
public:
    PatternConverter* next = nullptr;
    FormattingInfo fi;
    bool handlesThrowable = false;
    // Appends the value; false means "null" (classic layouts then only pad).
    virtual bool convert(LoggingEvent* e, std::string& out) = 0;
    void format(LoggingEvent* e, std::string& out);
};

namespace {

size_t codePoints(const std::string& s, size_t from) {
    size_t n = 0;
    for (size_t i = from; i < s.size(); i++)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) n++;
    return n;
}

}  // namespace

void PatternConverter::format(LoggingEvent* e, std::string& out) {
    size_t start = out.size();
    if (!convert(e, out)) {
        out.resize(start);
        if (fi.min > 0) out.append(static_cast<size_t>(fi.min), ' ');
        return;
    }
    size_t len = codePoints(out, start);
    if (len > static_cast<size_t>(fi.max)) {
        // keep the last `max` code points
        size_t drop = len - static_cast<size_t>(fi.max);
        size_t pos = start;
        while (drop > 0 && pos < out.size()) {
            pos++;
            while (pos < out.size() && (static_cast<unsigned char>(out[pos]) & 0xC0) == 0x80) pos++;
            drop--;
        }
        out.erase(start, pos - start);
    } else if (len < static_cast<size_t>(fi.min)) {
        size_t padn = static_cast<size_t>(fi.min) - len;
        if (fi.leftAlign) out.append(padn, ' ');
        else out.insert(start, padn, ' ');
    }
}

namespace {

class LiteralConverter : public PatternConverter {
public:
    explicit LiteralConverter(std::string t) : text(std::move(t)) {}
    bool convert(LoggingEvent*, std::string& out) override {
        out += text;
        return true;
    }
    std::string text;
};

class MessageConverter : public PatternConverter {
public:
    explicit MessageConverter(bool enhanced) : enhanced_(enhanced) {}
    bool convert(LoggingEvent* e, std::string& out) override {
        String m = e->getRenderedMessage();
        if (m == nullptr) {
            if (!enhanced_) return false;
            out += "null";
            return true;
        }
        out += m;
        return true;
    }

private:
    bool enhanced_;
};

class LevelConverter : public PatternConverter {
public:
    bool convert(LoggingEvent* e, std::string& out) override {
        Level* l = e->getLevel();
        if (l == nullptr) return false;
        out += l->toString();
        return true;
    }
};

// Name abbreviation: classic precision / enhanced NameAbbreviator.
struct Abbreviator {
    enum Kind { NONE, MAX_ELEMENTS, DROP_ELEMENTS, PATTERN } kind = NONE;
    int count = 0;
    struct Fragment {
        int charCount;
        char ellipsis;
    };
    std::vector<Fragment> fragments;

    void apply(std::string& buf, size_t nameStart) const {
        switch (kind) {
            case NONE: return;
            case MAX_ELEMENTS: {
                long end = static_cast<long>(buf.size()) - 1;
                for (int i = count; i > 0; i--) {
                    if (end - 1 < static_cast<long>(nameStart)) return;
                    size_t p = buf.rfind('.', static_cast<size_t>(end - 1));
                    if (p == std::string::npos || p < nameStart) return;
                    end = static_cast<long>(p);
                }
                buf.erase(nameStart, static_cast<size_t>(end + 1) - nameStart);
                return;
            }
            case DROP_ELEMENTS: {
                int i = count;
                for (size_t pos = buf.find('.', nameStart); pos != std::string::npos; pos = buf.find('.', pos + 1)) {
                    if (--i == 0) {
                        buf.erase(nameStart, pos + 1 - nameStart);
                        break;
                    }
                }
                return;
            }
            case PATTERN: {
                size_t pos = nameStart;
                auto frag = [&](const Fragment& f, size_t startPos) -> long {
                    size_t nextDot = buf.find('.', startPos);
                    if (nextDot == std::string::npos) return -1;
                    if (nextDot - startPos > static_cast<size_t>(f.charCount)) {
                        buf.erase(startPos + static_cast<size_t>(f.charCount),
                                  nextDot - startPos - static_cast<size_t>(f.charCount));
                        nextDot = startPos + static_cast<size_t>(f.charCount);
                        if (f.ellipsis != '\0') {
                            buf.insert(nextDot, 1, f.ellipsis);
                            nextDot++;
                        }
                    }
                    return static_cast<long>(nextDot + 1);
                };
                long p = static_cast<long>(pos);
                for (size_t i = 0; i + 1 < fragments.size() && p >= 0 && static_cast<size_t>(p) < buf.size(); i++)
                    p = frag(fragments[i], static_cast<size_t>(p));
                while (p >= 0 && static_cast<size_t>(p) < buf.size()) p = frag(fragments.back(), static_cast<size_t>(p));
                return;
            }
        }
    }

    // Classic PatternLayout %c{n}: positive precision only.
    static Abbreviator classic(const String& opt) {
        Abbreviator a;
        if (opt == nullptr) return a;
        int r = 0;
        try {
            r = Integer::parseInt(opt);
            if (r <= 0) {
                LogLog::error(str("Precision option (", opt, ") isn't a positive integer."));
                r = 0;
            }
        } catch (NumberFormatException& e) {
            LogLog::error(str("Category option \"", opt, "\" not a decimal integer."), e);
        }
        if (r > 0) {
            a.kind = MAX_ELEMENTS;
            a.count = r;
        }
        return a;
    }

    // EnhancedPatternLayout NameAbbreviator.getAbbreviator(pattern).
    static Abbreviator enhanced(const String& pattern) {
        Abbreviator a;
        if (pattern == nullptr || pattern.isEmpty()) return a;
        std::string t = pattern.trim();
        if (t.empty()) return a;
        bool neg = t[0] == '-';
        size_t i = neg ? 1 : 0;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
        if (i == t.size()) {
            int elements = 0;
            try {
                elements = Integer::parseInt(String(t));
            } catch (NumberFormatException&) {
                return a;
            }
            if (elements >= 0) {
                a.kind = MAX_ELEMENTS;
                a.count = elements;
            } else {
                a.kind = DROP_ELEMENTS;
                a.count = -elements;
            }
            return a;
        }
        a.kind = PATTERN;
        size_t pos = 0;
        while (pos < t.size()) {
            size_t ellipsisPos = pos;
            int charCount;
            if (t[pos] == '*') {
                charCount = INT32_MAX;
                ellipsisPos++;
            } else if (t[pos] >= '0' && t[pos] <= '9') {
                charCount = t[pos] - '0';
                ellipsisPos++;
            } else {
                charCount = 0;
            }
            char ellipsis = '\0';
            if (ellipsisPos < t.size()) {
                ellipsis = t[ellipsisPos];
                if (ellipsis == '.') ellipsis = '\0';
            }
            a.fragments.push_back(Fragment{charCount, ellipsis});
            pos = t.find('.', pos);
            if (pos == std::string::npos) break;
            pos++;
        }
        return a;
    }
};

class LoggerNameConverter : public PatternConverter {
public:
    LoggerNameConverter(Abbreviator a, bool enhanced) : abbrev_(std::move(a)), enhanced_(enhanced) {}
    bool convert(LoggingEvent* e, std::string& out) override {
        String n = e->getLoggerName();
        if (n == nullptr) {
            if (!enhanced_) return false;
            n = String("null");
        }
        size_t start = out.size();
        out += n;
        abbrev_.apply(out, start);
        return true;
    }

private:
    Abbreviator abbrev_;
    bool enhanced_;
};

class ClassNameConverter : public PatternConverter {
public:
    explicit ClassNameConverter(Abbreviator a) : abbrev_(std::move(a)) {}
    bool convert(LoggingEvent* e, std::string& out) override {
        LocationInfo* li = e->getLocationInformation();
        size_t start = out.size();
        out += li != nullptr ? li->getClassName() : LocationInfo::NA;
        abbrev_.apply(out, start);
        return true;
    }

private:
    Abbreviator abbrev_;
};

class DateConverter : public PatternConverter {
public:
    explicit DateConverter(DateFormatter* f) : fmt_(f) {}
    bool convert(LoggingEvent* e, std::string& out) override {
        fmt_->format(e->timeStamp, out);
        return true;
    }

private:
    DateFormatter* fmt_;
};

class LocationConverter : public PatternConverter {
public:
    enum Kind { FULL, FILE_, LINE, METHOD };
    LocationConverter(Kind k, bool enhanced) : kind_(k), enhanced_(enhanced) {}
    bool convert(LoggingEvent* e, std::string& out) override {
        LocationInfo* li = e->getLocationInformation();
        if (li == nullptr) return false;
        switch (kind_) {
            case FULL:
                if (li->fullInfo == nullptr) {
                    if (!enhanced_) return false;
                    out += "null";
                } else {
                    out += li->fullInfo;
                }
                break;
            case FILE_: out += li->getFileName(); break;
            case LINE: out += li->getLineNumber(); break;
            case METHOD: out += li->getMethodName(); break;
        }
        return true;
    }

private:
    Kind kind_;
    bool enhanced_;
};

class LineSepConverter : public PatternConverter {
public:
    bool convert(LoggingEvent*, std::string& out) override {
        out += "\n";
        return true;
    }
};

class RelativeTimeConverter : public PatternConverter {
public:
    bool convert(LoggingEvent* e, std::string& out) override {
        out += std::to_string(e->timeStamp - LoggingEvent::getStartTime());
        return true;
    }
};

class ThreadConverter : public PatternConverter {
public:
    bool convert(LoggingEvent* e, std::string& out) override {
        out += e->getThreadName();
        return true;
    }
};

class NdcConverter : public PatternConverter {
public:
    explicit NdcConverter(bool enhanced) : enhanced_(enhanced) {}
    bool convert(LoggingEvent* e, std::string& out) override {
        String n = e->getNDC();
        if (n == nullptr) {
            if (!enhanced_) return false;
            out += "null";
            return true;
        }
        out += n;
        return true;
    }

private:
    bool enhanced_;
};

class MdcConverter : public PatternConverter {
public:
    MdcConverter(String key, bool enhanced) : key_(std::move(key)), enhanced_(enhanced) {}
    bool convert(LoggingEvent* e, std::string& out) override {
        if (key_ == nullptr) {
            out += "{}";
            return true;
        }
        Object* v = e->getMDC(key_);
        if (v == nullptr) return enhanced_;
        out += v->toString();
        return true;
    }

private:
    String key_;
    bool enhanced_;
};

class ThrowableConverter : public PatternConverter {
public:
    explicit ThrowableConverter(int maxLines) : maxLines_(maxLines) { handlesThrowable = true; }
    bool convert(LoggingEvent* e, std::string& out) override {
        if (maxLines_ == 0) return true;
        ThrowableInformation* ti = e->getThrowableInformation();
        if (ti == nullptr) return true;
        Array<String>* rep = ti->getThrowableStrRep();
        int32_t length = rep->length;
        if (maxLines_ < 0) length += maxLines_;
        else if (length > maxLines_) length = maxLines_;
        for (int32_t i = 0; i < length; i++) {
            out += (*rep)[i];
            out += "\n";
        }
        return true;
    }

private:
    int maxLines_;
};

// ---- date converter construction (classic and enhanced rules)

const char* const kIso8601 = "yyyy-MM-dd HH:mm:ss,SSS";
const char* const kAbsolute = "HH:mm:ss,SSS";
const char* const kDateAndTime = "dd MMM yyyy HH:mm:ss,SSS";

std::string namedDatePattern(const String& opt) {
    if (opt == nullptr || opt.equalsIgnoreCase("ISO8601")) return kIso8601;
    if (opt.equalsIgnoreCase("ABSOLUTE")) return kAbsolute;
    if (opt.equalsIgnoreCase("DATE")) return kDateAndTime;
    return opt;
}

bool parseFixedZone(const std::string& id, int32_t& offset, std::string& name) {
    if (id == "UTC" || id == "GMT" || id == "Z" || id == "UT") {
        offset = 0;
        name = id == "Z" ? "UTC" : id;
        return true;
    }
    std::string s = id;
    if (s.rfind("GMT", 0) == 0 || s.rfind("UTC", 0) == 0) s = s.substr(3);
    if (s.empty() || (s[0] != '+' && s[0] != '-')) return false;
    int sign = s[0] == '-' ? -1 : 1;
    std::string digits;
    for (size_t i = 1; i < s.size(); i++)
        if (s[i] != ':') digits.push_back(s[i]);
    for (char c : digits)
        if (c < '0' || c > '9') return false;
    int h = 0, m = 0;
    if (digits.size() <= 2) h = std::atoi(digits.c_str());
    else if (digits.size() == 4) {
        h = std::atoi(digits.substr(0, 2).c_str());
        m = std::atoi(digits.substr(2).c_str());
    } else {
        return false;
    }
    offset = sign * (h * 60 + m);
    char buf[32];
    std::snprintf(buf, sizeof buf, "GMT%c%02d:%02d", sign < 0 ? '-' : '+', h, m);
    name = buf;
    return true;
}

PatternConverter* makeDateConverter(const std::vector<String>& options, bool enhanced) {
    String opt = options.empty() ? String(nullptr) : options[0];
    std::string pattern = namedDatePattern(opt);
    DateFormatter* f;
    try {
        f = new DateFormatter(pattern);
    } catch (IllegalArgumentException& e) {
        if (enhanced) LogLog::warn(str("Could not instantiate SimpleDateFormat with pattern ", opt), e);
        else LogLog::error(str("Could not instantiate SimpleDateFormat with ", opt), e);
        f = new DateFormatter(kIso8601);
    }
    if (enhanced && options.size() > 1) {
        int32_t off = 0;
        std::string name;
        if (parseFixedZone(options[1].trim(), off, name)) f->setFixedZone(off, name);
        else LogLog::warn(str("Time zone [", options[1], "] is not supported; using the local time zone."));
    }
    return new DateConverter(f);
}

int throwableMaxLines(const std::vector<String>& options) {
    int maxLines = INT32_MAX;
    if (!options.empty()) {
        if (options[0].equals("none")) maxLines = 0;
        else if (options[0].equals("short")) maxLines = 1;
        else {
            try {
                maxLines = Integer::parseInt(options[0]);
            } catch (NumberFormatException&) {
            }
        }
    }
    return maxLines;
}

// ---- classic log4j 1.2 PatternParser

struct ChainBuilder {
    PatternConverter* head = nullptr;
    PatternConverter* tail = nullptr;
    bool handlesThrowable = false;
    void add(PatternConverter* c) {
        if (c->handlesThrowable) handlesThrowable = true;
        if (head == nullptr) head = tail = c;
        else {
            tail->next = c;
            tail = c;
        }
    }
};

PatternConverter* parseClassic(const std::string& pattern) {
    enum State { LITERAL, CONVERTER, DOT, MIN, MAX };
    ChainBuilder chain;
    std::string lit;
    FormattingInfo fi;
    State state = LITERAL;
    size_t i = 0;
    const size_t n = pattern.size();
    auto extractOption = [&]() -> String {
        if (i < n && pattern[i] == '{') {
            size_t end = pattern.find('}', i);
            if (end != std::string::npos && end > i) {
                String r(pattern.substr(i + 1, end - i - 1));
                i = end + 1;
                return r;
            }
        }
        return String(nullptr);
    };
    auto addConverter = [&](PatternConverter* pc) {
        lit.clear();
        chain.add(pc);
        state = LITERAL;
        fi = FormattingInfo();
    };
    auto finalize = [&](char c) {
        PatternConverter* pc = nullptr;
        switch (c) {
            case 'c': pc = new LoggerNameConverter(Abbreviator::classic(extractOption()), false); break;
            case 'C': pc = new ClassNameConverter(Abbreviator::classic(extractOption())); break;
            case 'd': {
                std::vector<String> opts;
                String o = extractOption();
                if (o != nullptr) opts.push_back(o);
                pc = makeDateConverter(opts, false);
                break;
            }
            case 'F': pc = new LocationConverter(LocationConverter::FILE_, false); break;
            case 'l': pc = new LocationConverter(LocationConverter::FULL, false); break;
            case 'L': pc = new LocationConverter(LocationConverter::LINE, false); break;
            case 'm': pc = new MessageConverter(false); break;
            case 'M': pc = new LocationConverter(LocationConverter::METHOD, false); break;
            case 'p': pc = new LevelConverter(); break;
            case 'r': pc = new RelativeTimeConverter(); break;
            case 't': pc = new ThreadConverter(); break;
            case 'x': pc = new NdcConverter(false); break;
            case 'X': pc = new MdcConverter(extractOption(), false); break;
            default:
                LogLog::error(str("Unexpected char [", c, "] at position ", static_cast<int64_t>(i),
                                  " in conversion patterrn."));
                addConverter(new LiteralConverter(lit));  // default formatting
                return;
        }
        pc->fi = fi;
        addConverter(pc);
    };
    while (i < n) {
        char c = pattern[i++];
        switch (state) {
            case LITERAL:
                if (i == n) {
                    lit.push_back(c);
                    continue;
                }
                if (c == '%') {
                    switch (pattern[i]) {
                        case '%':
                            lit.push_back(c);
                            i++;
                            break;
                        case 'n':
                            lit += "\n";
                            i++;
                            break;
                        default:
                            if (!lit.empty()) chain.add(new LiteralConverter(lit));
                            lit.clear();
                            lit.push_back(c);
                            state = CONVERTER;
                            fi = FormattingInfo();
                    }
                } else {
                    lit.push_back(c);
                }
                break;
            case CONVERTER:
                lit.push_back(c);
                if (c == '-') fi.leftAlign = true;
                else if (c == '.') state = DOT;
                else if (c >= '0' && c <= '9') {
                    fi.min = c - '0';
                    state = MIN;
                } else {
                    finalize(c);
                }
                break;
            case MIN:
                lit.push_back(c);
                if (c >= '0' && c <= '9') fi.min = fi.min * 10 + (c - '0');
                else if (c == '.') state = DOT;
                else finalize(c);
                break;
            case DOT:
                lit.push_back(c);
                if (c >= '0' && c <= '9') {
                    fi.max = c - '0';
                    state = MAX;
                } else {
                    LogLog::error(str("Error occured in position ", static_cast<int64_t>(i),
                                      ".\n Was expecting digit, instead got char \"", c, "\"."));
                    state = LITERAL;
                }
                break;
            case MAX:
                lit.push_back(c);
                if (c >= '0' && c <= '9') fi.max = fi.max * 10 + (c - '0');
                else {
                    finalize(c);
                    state = LITERAL;
                }
                break;
        }
    }
    if (!lit.empty()) chain.add(new LiteralConverter(lit));
    return chain.head;
}

// ---- EnhancedPatternLayout's PatternParser (conversion words)

using ConverterFactory = std::function<PatternConverter*(const std::vector<String>&)>;

const std::map<std::string, ConverterFactory>& enhancedRules() {
    static const std::map<std::string, ConverterFactory>* rules = [] {
        auto* m = new std::map<std::string, ConverterFactory>();
        auto logger = [](const std::vector<String>& o) -> PatternConverter* {
            return new LoggerNameConverter(Abbreviator::enhanced(o.empty() ? String(nullptr) : o[0]), true);
        };
        auto cls = [](const std::vector<String>& o) -> PatternConverter* {
            return new ClassNameConverter(Abbreviator::enhanced(o.empty() ? String(nullptr) : o[0]));
        };
        auto date = [](const std::vector<String>& o) -> PatternConverter* { return makeDateConverter(o, true); };
        auto file = [](const std::vector<String>&) -> PatternConverter* {
            return new LocationConverter(LocationConverter::FILE_, true);
        };
        auto line = [](const std::vector<String>&) -> PatternConverter* {
            return new LocationConverter(LocationConverter::LINE, true);
        };
        auto method = [](const std::vector<String>&) -> PatternConverter* {
            return new LocationConverter(LocationConverter::METHOD, true);
        };
        auto full = [](const std::vector<String>&) -> PatternConverter* {
            return new LocationConverter(LocationConverter::FULL, true);
        };
        auto message = [](const std::vector<String>&) -> PatternConverter* { return new MessageConverter(true); };
        auto nl = [](const std::vector<String>&) -> PatternConverter* { return new LineSepConverter(); };
        auto level = [](const std::vector<String>&) -> PatternConverter* { return new LevelConverter(); };
        auto rel = [](const std::vector<String>&) -> PatternConverter* { return new RelativeTimeConverter(); };
        auto thread = [](const std::vector<String>&) -> PatternConverter* { return new ThreadConverter(); };
        auto ndc = [](const std::vector<String>&) -> PatternConverter* { return new NdcConverter(true); };
        auto props = [](const std::vector<String>& o) -> PatternConverter* {
            return new MdcConverter(o.empty() ? String(nullptr) : o[0], true);
        };
        auto thr = [](const std::vector<String>& o) -> PatternConverter* {
            return new ThrowableConverter(throwableMaxLines(o));
        };
        (*m)["c"] = logger;
        (*m)["logger"] = logger;
        (*m)["C"] = cls;
        (*m)["class"] = cls;
        (*m)["d"] = date;
        (*m)["date"] = date;
        (*m)["F"] = file;
        (*m)["file"] = file;
        (*m)["l"] = full;
        (*m)["L"] = line;
        (*m)["line"] = line;
        (*m)["m"] = message;
        (*m)["message"] = message;
        (*m)["n"] = nl;
        (*m)["M"] = method;
        (*m)["method"] = method;
        (*m)["p"] = level;
        (*m)["level"] = level;
        (*m)["r"] = rel;
        (*m)["relative"] = rel;
        (*m)["t"] = thread;
        (*m)["thread"] = thread;
        (*m)["x"] = ndc;
        (*m)["ndc"] = ndc;
        (*m)["X"] = props;
        (*m)["properties"] = props;
        (*m)["throwable"] = thr;
        return m;
    }();
    return *rules;
}

bool identStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool identPart(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

PatternConverter* parseEnhanced(const std::string& pattern) {
    enum State { LITERAL, CONVERTER, DOT, MIN, MAX };
    ChainBuilder chain;
    std::string lit;
    FormattingInfo fi;
    State state = LITERAL;
    size_t i = 0;
    const size_t n = pattern.size();
    auto finalize = [&](char c) {
        // extractConverter
        std::string id(1, c);
        if (identStart(static_cast<unsigned char>(c))) {
            while (i < n && identPart(static_cast<unsigned char>(pattern[i]))) {
                id.push_back(pattern[i]);
                lit.push_back(pattern[i]);
                i++;
            }
        }
        // extractOptions
        std::vector<String> options;
        while (i < n && pattern[i] == '{') {
            size_t end = pattern.find('}', i);
            if (end == std::string::npos) break;
            options.push_back(String(pattern.substr(i + 1, end - i - 1)));
            i = end + 1;
        }
        // createConverter: longest matching prefix of the conversion word
        const auto& rules = enhancedRules();
        const ConverterFactory* factory = nullptr;
        size_t matched = 0;
        for (size_t len = id.size(); len > 0 && factory == nullptr; len--) {
            auto it = rules.find(id.substr(0, len));
            if (it != rules.end()) {
                factory = &it->second;
                matched = len;
            }
        }
        if (factory == nullptr) {
            LogLog::error(str("Unrecognized format specifier [", id, "]"));
            LogLog::error(str("Unrecognized conversion specifier [", id, "] starting at position ",
                              static_cast<int64_t>(i), " in conversion pattern."));
            chain.add(new LiteralConverter(lit));
        } else {
            PatternConverter* pc = (*factory)(options);
            pc->fi = fi;
            chain.add(pc);
            std::string rest = id.substr(matched);  // unmatched tail of the word stays literal
            if (!rest.empty()) chain.add(new LiteralConverter(rest));
        }
        lit.clear();
        state = LITERAL;
        fi = FormattingInfo();
    };
    while (i < n) {
        char c = pattern[i++];
        switch (state) {
            case LITERAL:
                if (i == n) {
                    lit.push_back(c);
                    continue;
                }
                if (c == '%') {
                    if (pattern[i] == '%') {
                        lit.push_back(c);
                        i++;
                    } else {
                        if (!lit.empty()) chain.add(new LiteralConverter(lit));
                        lit.clear();
                        lit.push_back(c);
                        state = CONVERTER;
                        fi = FormattingInfo();
                    }
                } else {
                    lit.push_back(c);
                }
                break;
            case CONVERTER:
                lit.push_back(c);
                if (c == '-') fi.leftAlign = true;
                else if (c == '.') state = DOT;
                else if (c >= '0' && c <= '9') {
                    fi.min = c - '0';
                    state = MIN;
                } else {
                    finalize(c);
                }
                break;
            case MIN:
                lit.push_back(c);
                if (c >= '0' && c <= '9') fi.min = fi.min * 10 + (c - '0');
                else if (c == '.') state = DOT;
                else finalize(c);
                break;
            case DOT:
                lit.push_back(c);
                if (c >= '0' && c <= '9') {
                    fi.max = c - '0';
                    state = MAX;
                } else {
                    LogLog::error(str("Error occured in position ", static_cast<int64_t>(i),
                                      ".\n Was expecting digit, instead got char \"", c, "\"."));
                    state = LITERAL;
                }
                break;
            case MAX:
                lit.push_back(c);
                if (c >= '0' && c <= '9') fi.max = fi.max * 10 + (c - '0');
                else finalize(c);
                break;
        }
    }
    if (!lit.empty()) chain.add(new LiteralConverter(lit));
    return chain.head;
}

}  // namespace

}  // namespace impl

// =======================================================================================
// Layouts

String SimpleLayout::format(LoggingEvent* event) {
    return str(event->getLevel()->toString(), " - ", event->getRenderedMessage(), LINE_SEP);
}

PatternLayout::PatternLayout() : PatternLayout(false, DEFAULT_CONVERSION_PATTERN) {}

PatternLayout::PatternLayout(const String& pattern) : PatternLayout(false, pattern) {}

PatternLayout::PatternLayout(bool enhanced, const String& pattern) : enhanced_(enhanced) { compile(pattern); }

void PatternLayout::setConversionPattern(String conversionPattern) { compile(conversionPattern); }

void PatternLayout::compile(const String& conversionPattern) {
    String p = conversionPattern == nullptr ? DEFAULT_CONVERSION_PATTERN : conversionPattern;
    impl::PatternConverter* head = enhanced_ ? impl::parseEnhanced(p) : impl::parseClassic(p);
    bool handles = false;
    for (auto* c = head; c != nullptr; c = c->next) handles = handles || c->handlesThrowable;
    pattern_ = conversionPattern;
    handlesExceptions_.store(handles);
    head_.store(head, std::memory_order_release);
}

String PatternLayout::getConversionPattern() { return pattern_; }

String PatternLayout::format(LoggingEvent* event) {
    std::string out;
    for (auto* c = head_.load(std::memory_order_acquire); c != nullptr; c = c->next) c->format(event, out);
    return String(std::move(out));
}

bool PatternLayout::setOption(String name, String value) {
    if (optionIs(name, "conversionPattern")) {
        setConversionPattern(value);
        return true;
    }
    return Layout::setOption(name, value);
}

EnhancedPatternLayout::EnhancedPatternLayout() : PatternLayout(true, DEFAULT_CONVERSION_PATTERN) {}

EnhancedPatternLayout::EnhancedPatternLayout(const String& pattern) : PatternLayout(true, pattern) {}

void EnhancedPatternLayout::setConversionPattern(String conversionPattern) {
    compile(conversionPattern == nullptr ? conversionPattern : impl::convertSpecialChars(conversionPattern));
}

}  // namespace jlang::log4j
