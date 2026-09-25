// java.util.TimeZone (tz database TZif reader + POSIX TZ rules), java.util.Locale and the
// calendar arithmetic helpers of time_internal.h.
#include "time_internal.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace jlang {
namespace detail {
namespace tm {

const char* const MONTH_NAMES[13] = {"January", "February", "March",     "April",   "May",      "June", "July",
                                     "August",  "September", "October", "November", "December", ""};
const char* const SHORT_MONTH_NAMES[13] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
                                           "Aug", "Sep", "Oct", "Nov", "Dec", ""};
const char* const WEEKDAY_NAMES[8] = {"", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const SHORT_WEEKDAY_NAMES[8] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const AMPM_NAMES[2] = {"AM", "PM"};
const char* const ERA_NAMES[2] = {"BC", "AD"};

static const int32_t ACCUMULATED_DAYS_IN_MONTH[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
static const int32_t DAYS_IN_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

int64_t gregorianFixedDate(int64_t year, int32_t month, int64_t dayOfMonth) {
    int64_t prevyear = year - 1;
    int64_t days = dayOfMonth;
    if (prevyear >= 0) {
        days += (365 * prevyear) + (prevyear / 4) - (prevyear / 100) + (prevyear / 400) + ((367 * month - 362) / 12);
    } else {
        days += (365 * prevyear) + floorDiv(prevyear, 4) - floorDiv(prevyear, 100) + floorDiv(prevyear, 400) +
                floorDiv(367 * month - 362, 12);
    }
    if (month > 2) days -= isGregorianLeapYear(year) ? 1 : 2;
    return days;
}

int64_t julianFixedDate(int64_t year, int32_t month, int64_t dayOfMonth) {
    constexpr int64_t JULIAN_EPOCH = -1;
    int64_t y = year;
    int64_t days = JULIAN_EPOCH - 1 + (365 * (y - 1)) + dayOfMonth;
    if (y > 0) {
        days += (y - 1) / 4;
    } else {
        days += floorDiv(y - 1, 4);
    }
    if (month > 0) {
        days += ((367 * static_cast<int64_t>(month)) - 362) / 12;
    } else {
        days += floorDiv((367 * static_cast<int64_t>(month)) - 362, 12);
    }
    if (month > 2) days -= isJulianLeapYear(year) ? 1 : 2;
    return days;
}

int32_t gregorianYearFromFixed(int64_t fixedDate) {
    int64_t d0 = fixedDate - 1;
    int64_t n400 = floorDiv(d0, 146097);
    int64_t d1 = floorMod(d0, 146097);
    int64_t n100 = d1 / 36524;
    int64_t d2 = d1 % 36524;
    int64_t n4 = d2 / 1461;
    int64_t d3 = d2 % 1461;
    int64_t n1 = d3 / 365;
    int64_t year = 400 * n400 + 100 * n100 + 4 * n4 + n1;
    if (!(n100 == 4 || n1 == 4)) ++year;
    return static_cast<int32_t>(year);
}

void gregorianFromFixed(int64_t fixedDate, int32_t* yearOut, int32_t* monthOut, int32_t* domOut) {
    int32_t year = gregorianYearFromFixed(fixedDate);
    int64_t jan1 = gregorianFixedDate(year, 1, 1);
    bool isLeap = isGregorianLeapYear(year);
    int64_t priorDays = fixedDate - jan1;
    int64_t mar1 = jan1 + 31 + 28 + (isLeap ? 1 : 0);
    if (fixedDate >= mar1) priorDays += isLeap ? 1 : 2;
    int64_t month = 12 * priorDays + 373;
    month = (month > 0) ? month / 367 : floorDiv(month, 367);
    int64_t month1 = jan1 + ACCUMULATED_DAYS_IN_MONTH[std::clamp<int64_t>(month - 1, 0, 11)];
    if (isLeap && month >= 3) month1++;
    *yearOut = year;
    *monthOut = static_cast<int32_t>(month);
    *domOut = static_cast<int32_t>(fixedDate - month1) + 1;
}

void julianFromFixed(int64_t fixedDate, int32_t* yearOut, int32_t* monthOut, int32_t* domOut) {
    constexpr int64_t JULIAN_EPOCH = -1;
    int64_t fd = 4 * (fixedDate - JULIAN_EPOCH) + 1464;
    int64_t year = (fd >= 0) ? fd / 1461 : floorDiv(fd, 1461);
    int64_t priorDays = fixedDate - julianFixedDate(year, 1, 1);
    bool isLeap = isJulianLeapYear(year);
    if (fixedDate >= julianFixedDate(year, 3, 1)) priorDays += isLeap ? 1 : 2;
    int64_t month = 12 * priorDays + 373;
    month = (month > 0) ? month / 367 : floorDiv(month, 367);
    *yearOut = static_cast<int32_t>(year);
    *monthOut = static_cast<int32_t>(month);
    *domOut = static_cast<int32_t>(fixedDate - julianFixedDate(year, static_cast<int32_t>(month), 1)) + 1;
}

int32_t gregorianMonthLength(int64_t year, int32_t month0) {
    if (month0 == 1 && isGregorianLeapYear(year)) return 29;
    return DAYS_IN_MONTH[month0];
}
int32_t julianMonthLength(int64_t year, int32_t month0) {
    if (month0 == 1 && isJulianLeapYear(year)) return 29;
    return DAYS_IN_MONTH[month0];
}

void appendPadded(std::string& out, int64_t value, int32_t minDigits) {
    char buf[32];
    bool neg = value < 0;
    uint64_t u = neg ? (0 - static_cast<uint64_t>(value)) : static_cast<uint64_t>(value);
    int n = 0;
    do {
        buf[n++] = static_cast<char>('0' + (u % 10));
        u /= 10;
    } while (u != 0);
    if (neg) out.push_back('-');
    for (int i = n; i < minDigits; i++) out.push_back('0');
    while (n > 0) out.push_back(buf[--n]);
}

LocalFields localFields(int64_t utcMillis, TimeZone* zone) {
    LocalFields f{};
    zone->offsetsAtUtc(utcMillis, &f.rawOffset, &f.dstOffset);
    int64_t local = utcMillis + f.rawOffset + f.dstOffset;
    int64_t fixedDate = floorDiv(local, ONE_DAY) + EPOCH_OFFSET;
    int64_t tod = floorMod(local, ONE_DAY);
    constexpr int64_t CUTOVER_FIXED = 577736;
    int32_t y, m, d;
    if (fixedDate >= CUTOVER_FIXED) gregorianFromFixed(fixedDate, &y, &m, &d);
    else julianFromFixed(fixedDate, &y, &m, &d);
    f.year = y;
    f.month = m - 1;
    f.day = d;
    f.dayOfWeek = dayOfWeekFromFixed(fixedDate);
    f.hour = static_cast<int32_t>(tod / ONE_HOUR);
    f.minute = static_cast<int32_t>((tod / ONE_MINUTE) % 60);
    f.second = static_cast<int32_t>((tod / ONE_SECOND) % 60);
    f.millis = static_cast<int32_t>(tod % 1000);
    return f;
}

}  // namespace tm

// ---------------------------------------------------------------------------------------
// Zone rules
namespace {

struct PosixRule {
    struct DateRule {
        char kind = 0;  // 'J' (1..365, no Feb 29), 'N' (0..365), 'M' (month.week.day)
        int32_t m = 0, w = 0, d = 0, n = 0;
        int32_t time = 7200;  // seconds of local time
    };
    bool valid = false;
    std::string stdName, dstName;
    int32_t stdOff = 0;  // seconds east of UTC
    int32_t dstOff = 0;
    bool hasDst = false;
    DateRule start, end;
};

bool parsePosixName(const char*& p, std::string& out) {
    if (*p == '<') {
        const char* q = std::strchr(p, '>');
        if (q == nullptr) return false;
        out.assign(p + 1, q);
        p = q + 1;
        return true;
    }
    const char* s = p;
    while (std::isalpha(static_cast<unsigned char>(*p))) p++;
    if (p - s < 3) return false;
    out.assign(s, p);
    return true;
}

// [+-]hh[:mm[:ss]] -> seconds
bool parsePosixTime(const char*& p, int32_t& out) {
    int sign = 1;
    if (*p == '+') p++;
    else if (*p == '-') {
        sign = -1;
        p++;
    }
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    int32_t h = 0;
    while (std::isdigit(static_cast<unsigned char>(*p))) h = h * 10 + (*p++ - '0');
    int32_t m = 0, s = 0;
    if (*p == ':') {
        p++;
        while (std::isdigit(static_cast<unsigned char>(*p))) m = m * 10 + (*p++ - '0');
        if (*p == ':') {
            p++;
            while (std::isdigit(static_cast<unsigned char>(*p))) s = s * 10 + (*p++ - '0');
        }
    }
    out = sign * (h * 3600 + m * 60 + s);
    return true;
}

bool parsePosixDateRule(const char*& p, PosixRule::DateRule& r) {
    if (*p == 'M') {
        p++;
        r.kind = 'M';
        if (std::sscanf(p, "%d.%d.%d", &r.m, &r.w, &r.d) != 3) return false;
        while (*p && *p != '/' && *p != ',') p++;
    } else if (*p == 'J') {
        p++;
        r.kind = 'J';
        r.n = std::atoi(p);
        while (std::isdigit(static_cast<unsigned char>(*p))) p++;
    } else if (std::isdigit(static_cast<unsigned char>(*p))) {
        r.kind = 'N';
        r.n = std::atoi(p);
        while (std::isdigit(static_cast<unsigned char>(*p))) p++;
    } else {
        return false;
    }
    r.time = 7200;
    if (*p == '/') {
        p++;
        if (!parsePosixTime(p, r.time)) return false;
    }
    return true;
}

PosixRule parsePosix(const std::string& s) {
    PosixRule r;
    const char* p = s.c_str();
    if (!parsePosixName(p, r.stdName)) return r;
    int32_t off;
    if (!parsePosixTime(p, off)) return r;
    r.stdOff = -off;
    if (*p == '\0') {
        r.valid = true;
        return r;
    }
    if (!parsePosixName(p, r.dstName)) return r;
    r.hasDst = true;
    r.dstOff = r.stdOff + 3600;
    if (*p != ',' && *p != '\0') {
        if (!parsePosixTime(p, off)) return r;
        r.dstOff = -off;
    }
    if (*p == '\0') {  // no rule: US default rule (M3.2.0,M11.1.0)
        r.start.kind = 'M'; r.start.m = 3; r.start.w = 2; r.start.d = 0;
        r.end.kind = 'M'; r.end.m = 11; r.end.w = 1; r.end.d = 0;
        r.valid = true;
        return r;
    }
    if (*p != ',') return r;
    p++;
    if (!parsePosixDateRule(p, r.start)) return r;
    if (*p != ',') return r;
    p++;
    if (!parsePosixDateRule(p, r.end)) return r;
    r.valid = true;
    return r;
}

// Local seconds since the epoch of the rule's transition in `year` (wall clock of the offset
// in effect before the transition).
int64_t ruleLocalSeconds(int64_t year, const PosixRule::DateRule& r) {
    using namespace tm;
    int64_t day;  // fixed date
    if (r.kind == 'M') {
        int64_t first = gregorianFixedDate(year, r.m, 1);
        int32_t dow1 = dayOfWeekFromFixed(first) - 1;  // 0 = Sunday
        int64_t dom = 1 + ((r.d - dow1) % 7 + 7) % 7 + 7 * (r.w - 1);
        int32_t len = gregorianMonthLength(year, r.m - 1);
        while (dom > len) dom -= 7;
        day = first + dom - 1;
    } else if (r.kind == 'J') {
        int64_t n = r.n;  // 1..365, Feb 29 never counted
        if (isGregorianLeapYear(year) && n >= 60) n++;
        day = gregorianFixedDate(year, 1, 1) + n - 1;
    } else {
        day = gregorianFixedDate(year, 1, 1) + r.n;
    }
    return (day - EPOCH_OFFSET) * 86400 + r.time;
}

}  // namespace

struct ZoneData {
    std::string id;
    bool fixed = false;
    int32_t fixedOffset = 0;  // seconds
    std::vector<int64_t> trans;  // UTC seconds
    std::vector<uint8_t> idx;
    struct TT {
        int32_t off;  // seconds
        bool dst;
        std::string abbr;
    };
    std::vector<TT> types;
    std::vector<int32_t> transRaw;  // standard offset in effect after each transition
    int32_t type0Raw = 0;
    PosixRule rule;
    int32_t rawNow = 0;  // seconds (Java getRawOffset)

    struct Info {
        int32_t raw;  // seconds
        int32_t dst;  // seconds
        bool isDst;
        const std::string* abbr;
    };

    Info ruleInfo(int64_t s) const {
        Info in{rule.stdOff, 0, false, &rule.stdName};
        if (!rule.hasDst) return in;
        int64_t year;
        {
            int64_t fd = tm::floorDiv(s + rule.stdOff, 86400) + tm::EPOCH_OFFSET;
            year = tm::gregorianYearFromFixed(fd);
        }
        int64_t startUtc = ruleLocalSeconds(year, rule.start) - rule.stdOff;
        int64_t endUtc = ruleLocalSeconds(year, rule.end) - rule.dstOff;
        bool dst;
        if (startUtc < endUtc) dst = s >= startUtc && s < endUtc;
        else dst = !(s >= endUtc && s < startUtc);
        if (dst) {
            in.dst = rule.dstOff - rule.stdOff;
            in.isDst = true;
            in.abbr = &rule.dstName;
        }
        return in;
    }

    Info info(int64_t utcMillis) const {
        if (fixed) return Info{fixedOffset, 0, false, &id};
        if (utcMillis < tm::UTC1900_MS) return Info{rawNow, 0, false, rule.valid ? &rule.stdName : nullptr};
        int64_t s = tm::floorDiv(utcMillis, 1000);
        if (trans.empty() || s < trans.front()) {
            if (trans.empty() && rule.valid) return ruleInfo(s);
            if (types.empty()) return Info{rawNow, 0, false, nullptr};
            const TT& t = types[0];
            return Info{type0Raw, t.off - type0Raw, t.dst, &t.abbr};
        }
        if (s >= trans.back() && rule.valid) return ruleInfo(s);
        size_t i = static_cast<size_t>(std::upper_bound(trans.begin(), trans.end(), s) - trans.begin()) - 1;
        const TT& t = types[idx[i]];
        return Info{transRaw[i], t.off - transRaw[i], t.dst, &t.abbr};
    }
};

namespace {

std::mutex& zoneLock() {
    static std::mutex* m = new std::mutex();
    return *m;
}
std::map<std::string, ZoneData*>& zoneCache() {
    static std::map<std::string, ZoneData*>* m = new std::map<std::string, ZoneData*>();
    return *m;
}
std::atomic<TimeZone*> g_defaultZone{nullptr};
TimeZone* g_defaultZoneRoot = nullptr;  // keeps the default zone reachable for the collector

uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
int64_t be64(const unsigned char* p) {
    return static_cast<int64_t>((uint64_t(be32(p)) << 32) | be32(p + 4));
}

std::vector<std::string> zoneDirs() {
    std::vector<std::string> dirs;
    if (const char* d = std::getenv("TZDIR"); d != nullptr && *d) dirs.push_back(d);
    dirs.push_back("/usr/share/zoneinfo");
    dirs.push_back("/usr/lib/zoneinfo");
    dirs.push_back("/usr/share/lib/zoneinfo");
    return dirs;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

void computeRawOffsets(ZoneData* z) {
    // standard offset of each transition: the offset of the nearest non-DST type before it
    // (else after it); for type 0 (before the first transition) the same search from 0.
    size_t n = z->trans.size();
    z->transRaw.assign(n, 0);
    auto nonDstBefore = [&](size_t i, int32_t& out) -> bool {
        for (size_t k = i + 1; k-- > 0;) {
            const auto& t = z->types[z->idx[k]];
            if (!t.dst) {
                out = t.off;
                return true;
            }
        }
        return false;
    };
    auto nonDstAfter = [&](size_t i, int32_t& out) -> bool {
        for (size_t k = i; k < n; k++) {
            const auto& t = z->types[z->idx[k]];
            if (!t.dst) {
                out = t.off;
                return true;
            }
        }
        return false;
    };
    for (size_t i = 0; i < n; i++) {
        const auto& t = z->types[z->idx[i]];
        if (!t.dst) {
            z->transRaw[i] = t.off;
            continue;
        }
        int32_t r;
        if (nonDstBefore(i, r) || nonDstAfter(i, r)) z->transRaw[i] = r;
        else z->transRaw[i] = t.off - 3600;
    }
    if (!z->types.empty()) {
        const auto& t0 = z->types[0];
        z->type0Raw = t0.off;
        if (t0.dst) {
            int32_t r;
            z->type0Raw = (n > 0 && nonDstAfter(0, r)) ? r : t0.off - 3600;
        }
    }
    if (z->rule.valid) z->rawNow = z->rule.stdOff;
    else if (n > 0) z->rawNow = z->transRaw[n - 1];
    else z->rawNow = z->type0Raw;
}

ZoneData* parseTzif(const std::string& id, const std::string& data) {
    const auto* p = reinterpret_cast<const unsigned char*>(data.data());
    size_t len = data.size();
    if (len < 44 || std::memcmp(p, "TZif", 4) != 0) return nullptr;
    char version = static_cast<char>(p[4]);
    auto counts = [&](const unsigned char* h, uint32_t c[6]) {
        for (int i = 0; i < 6; i++) c[i] = be32(h + 20 + 4 * i);
    };
    uint32_t c[6];  // isutcnt, isstdcnt, leapcnt, timecnt, typecnt, charcnt
    counts(p, c);
    size_t v1size = c[3] * 5 + c[4] * 6 + c[5] + c[2] * 8 + c[1] + c[0];
    const unsigned char* h = p;
    int timeSize = 4;
    if (version >= '2' && 44 + v1size + 44 <= len) {
        h = p + 44 + v1size;
        if (std::memcmp(h, "TZif", 4) != 0) return nullptr;
        counts(h, c);
        timeSize = 8;
    }
    size_t need = 44 + c[3] * timeSize + c[3] + c[4] * 6 + c[5];
    if (static_cast<size_t>(h - p) + need > len) return nullptr;
    const unsigned char* q = h + 44;
    auto* z = new ZoneData();
    z->id = id;
    z->trans.resize(c[3]);
    for (uint32_t i = 0; i < c[3]; i++) {
        z->trans[i] = timeSize == 8 ? be64(q) : static_cast<int32_t>(be32(q));
        q += timeSize;
    }
    z->idx.resize(c[3]);
    for (uint32_t i = 0; i < c[3]; i++) z->idx[i] = *q++;
    const unsigned char* tt = q;
    q += c[4] * 6;
    const char* chars = reinterpret_cast<const char*>(q);
    for (uint32_t i = 0; i < c[4]; i++) {
        ZoneData::TT t;
        t.off = static_cast<int32_t>(be32(tt + 6 * i));
        t.dst = tt[6 * i + 4] != 0;
        uint8_t ai = tt[6 * i + 5];
        t.abbr = ai < c[5] ? std::string(chars + ai) : std::string();
        z->types.push_back(t);
    }
    for (auto& ix : z->idx)
        if (ix >= z->types.size()) ix = 0;
    if (z->types.empty()) return nullptr;
    // footer
    if (timeSize == 8) {
        size_t off = static_cast<size_t>(h - p) + 44 + c[3] * 8 + c[3] + c[4] * 6 + c[5] + c[2] * 12 + c[1] + c[0];
        if (off < len && p[off] == '\n') {
            size_t e = data.find('\n', off + 1);
            if (e != std::string::npos) z->rule = parsePosix(data.substr(off + 1, e - off - 1));
        }
    }
    computeRawOffsets(z);
    return z;
}

ZoneData* makeFixed(const std::string& id, int32_t offsetSeconds) {
    auto* z = new ZoneData();
    z->id = id;
    z->fixed = true;
    z->fixedOffset = offsetSeconds;
    z->rawNow = offsetSeconds;
    return z;
}

// Java's parseCustomTimeZone: GMT[+-]hh[[:]mm] -> "GMT+hh:mm"
ZoneData* parseCustom(const std::string& id) {
    if (id.size() < 5 || id.compare(0, 3, "GMT") != 0) return nullptr;
    size_t i = 3;
    bool neg = false;
    if (id[i] == '-') neg = true;
    else if (id[i] != '+') return nullptr;
    i++;
    auto digits = [&](size_t& k, int& ndig) {
        int v = 0;
        ndig = 0;
        while (k < id.size() && std::isdigit(static_cast<unsigned char>(id[k]))) {
            v = v * 10 + (id[k] - '0');
            k++;
            ndig++;
        }
        return v;
    };
    int nd;
    int hours = digits(i, nd);
    int minutes = 0;
    if (nd == 0) return nullptr;
    if (i < id.size() && id[i] == ':') {
        if (nd > 2) return nullptr;
        i++;
        int nd2;
        minutes = digits(i, nd2);
        if (nd2 != 2) return nullptr;
    } else if (nd > 2) {
        if (nd != 3 && nd != 4) return nullptr;
        minutes = hours % 100;
        hours /= 100;
    }
    if (i != id.size()) return nullptr;
    if (hours > 23 || minutes > 59) return nullptr;
    char buf[48];
    std::snprintf(buf, sizeof buf, "GMT%c%02d:%02d", neg ? '-' : '+', hours, minutes);
    int32_t off = (hours * 3600 + minutes * 60) * (neg ? -1 : 1);
    return makeFixed(buf, off);
}

ZoneData* loadZone(const std::string& id) {
    if (id == "GMT") return makeFixed("GMT", 0);
    if (id.compare(0, 3, "GMT") == 0 && id.size() > 3 && (id[3] == '+' || id[3] == '-')) return parseCustom(id);
    if (id.empty() || id.find("..") != std::string::npos || id[0] == '/') return nullptr;
    for (const auto& dir : zoneDirs()) {
        std::string data;
        if (readFile(dir + "/" + id, data)) {
            if (ZoneData* z = parseTzif(id, data)) return z;
        }
    }
    return nullptr;
}

ZoneData* findZone(const std::string& id) {
    std::lock_guard<std::mutex> g(zoneLock());
    auto& cache = zoneCache();
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    ZoneData* z = loadZone(id);
    cache[id] = z;  // cache misses too
    return z;
}

std::string trimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

ZoneData* systemDefaultZone() {
    std::string id;
    if (const char* tz = std::getenv("TZ"); tz != nullptr && *tz) {
        id = tz;
        if (id[0] == ':') id = id.substr(1);
        if (!id.empty() && id[0] == '/') {
            size_t k = id.find("zoneinfo/");
            if (k != std::string::npos) id = id.substr(k + 9);
            else {
                std::string data;
                if (readFile(id, data))
                    if (ZoneData* z = parseTzif(id, data)) return z;
            }
        }
        if (ZoneData* z = findZone(id)) return z;
        // a POSIX TZ string ("CET-1CEST,M3.5.0,M10.5.0/3")
        PosixRule r = parsePosix(id);
        if (r.valid) {
            auto* z = new ZoneData();
            z->id = id;
            z->rule = r;
            z->rawNow = r.stdOff;
            return z;
        }
        return findZone("GMT");
    }
    std::string data;
    if (readFile("/etc/timezone", data)) {
        std::string line = trimmed(data.substr(0, data.find('\n')));
        if (!line.empty())
            if (ZoneData* z = findZone(line)) return z;
    }
    char buf[4096];
    ssize_t n = ::readlink("/etc/localtime", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string target(buf);
        size_t k = target.find("zoneinfo/");
        if (k != std::string::npos)
            if (ZoneData* z = findZone(target.substr(k + 9))) return z;
    }
    return findZone("GMT");
}

bool isAlphaName(const std::string& s) {
    if (s.empty()) return false;
    for (char ch : s)
        if (!std::isalpha(static_cast<unsigned char>(ch))) return false;
    return true;
}

std::string gmtName(int32_t offsetMillis) {
    int32_t m = offsetMillis / 60000;
    char sign = '+';
    if (m < 0) {
        sign = '-';
        m = -m;
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "GMT%c%02d:%02d", sign, m / 60, m % 60);
    return buf;
}

struct LongName {
    const char* shortStd;
    const char* longStd;
    const char* longDst;
};
const LongName LONG_NAMES[] = {
    {"UTC", "Coordinated Universal Time", "Coordinated Universal Time"},
    {"GMT", "Greenwich Mean Time", "British Summer Time"},
    {"CET", "Central European Time", "Central European Summer Time"},
    {"EET", "Eastern European Time", "Eastern European Summer Time"},
    {"WET", "Western European Time", "Western European Summer Time"},
    {"MSK", "Moscow Standard Time", "Moscow Daylight Time"},
    {"EST", "Eastern Standard Time", "Eastern Daylight Time"},
    {"CST", "Central Standard Time", "Central Daylight Time"},
    {"MST", "Mountain Standard Time", "Mountain Daylight Time"},
    {"PST", "Pacific Standard Time", "Pacific Daylight Time"},
    {"AKST", "Alaska Standard Time", "Alaska Daylight Time"},
    {"HST", "Hawaii Standard Time", "Hawaii Daylight Time"},
    {"KST", "Korea Standard Time", "Korea Daylight Time"},
    {"JST", "Japan Standard Time", "Japan Daylight Time"},
    {"IST", "India Standard Time", "India Daylight Time"},
    {"AEST", "Australian Eastern Standard Time (New South Wales)", "Australian Eastern Daylight Time (New South Wales)"},
};

}  // namespace
}  // namespace detail

// ---------------------------------------------------------------------------------------
// TimeZone

TimeZone::TimeZone(detail::ZoneData* data) : data_(data), id_(String(data->id)) {}

TimeZone* TimeZone::getTimeZone(const String& id) {
    detail::ZoneData* z = detail::findZone(std::string(id));
    if (z == nullptr) z = detail::findZone("GMT");
    return new TimeZone(z);
}

static TimeZone* defaultZoneRef() {
    TimeZone* z = detail::g_defaultZone.load(std::memory_order_acquire);
    if (z != nullptr) return z;
    detail::ZoneData* d = detail::systemDefaultZone();
    std::lock_guard<std::mutex> g(detail::zoneLock());
    z = detail::g_defaultZone.load(std::memory_order_relaxed);
    if (z == nullptr) {
        z = new TimeZone(d);
        detail::g_defaultZoneRoot = z;
        detail::g_defaultZone.store(z, std::memory_order_release);
    }
    return z;
}

namespace detail {
TimeZone* defaultTimeZoneRef() { return defaultZoneRef(); }
}  // namespace detail

TimeZone* TimeZone::getDefault() { return dynamic_cast<TimeZone*>(defaultZoneRef()->clone()); }

void TimeZone::setDefault(TimeZone* zone) {
    std::lock_guard<std::mutex> g(detail::zoneLock());
    TimeZone* z = zone == nullptr ? nullptr : dynamic_cast<TimeZone*>(zone->clone());
    detail::g_defaultZoneRoot = z;
    detail::g_defaultZone.store(z, std::memory_order_release);
}

Array<String>* TimeZone::getAvailableIDs() {
    std::vector<std::string> ids;
    namespace fs = std::filesystem;
    for (const auto& dir : detail::zoneDirs()) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            std::string rel = it->path().string().substr(dir.size() + 1);
            if (it->is_directory(ec)) {
                if (rel == "posix" || rel == "right") it.disable_recursion_pending();
                continue;
            }
            std::ifstream in(it->path(), std::ios::binary);
            char magic[4] = {};
            in.read(magic, 4);
            if (std::memcmp(magic, "TZif", 4) == 0 && rel != "localtime" && rel != "posixrules" &&
                rel != "Factory")
                ids.push_back(rel);
        }
        break;
    }
    std::sort(ids.begin(), ids.end());
    auto* arr = new Array<String>(static_cast<int32_t>(ids.size()));
    for (size_t i = 0; i < ids.size(); i++) (*arr)[static_cast<int32_t>(i)] = String(ids[i]);
    return arr;
}

void TimeZone::setID(const String& id) { id_ = id; }

void TimeZone::offsetsAtUtc(int64_t utcMillis, int32_t* raw, int32_t* dst) {
    auto in = data_->info(utcMillis);
    *raw = in.raw * 1000;
    *dst = in.dst * 1000;
}

void TimeZone::offsetsAtWall(int64_t wall, int32_t* raw, int32_t* dst) {
    using namespace detail::tm;
    if (data_->fixed) {
        *raw = data_->fixedOffset * 1000;
        *dst = 0;
        return;
    }
    auto a = data_->info(wall - ONE_DAY);
    auto b = data_->info(wall + ONE_DAY);
    int32_t ta = (a.raw + a.dst) * 1000, tb = (b.raw + b.dst) * 1000;
    // Java's ZoneInfo.getOffsetsByWall: the offset after a transition applies from the
    // transition's wall time in that offset on; gaps and overlaps resolve to standard time.
    auto pick = a;
    if (ta != tb || a.raw != b.raw) {
        auto c = data_->info(wall - tb);
        pick = ((c.raw + c.dst) * 1000 == tb) ? c : a;
    } else {
        pick = data_->info(wall - ta);
    }
    *raw = pick.raw * 1000;
    *dst = pick.dst * 1000;
}

int32_t TimeZone::getOffset(int64_t date) {
    int32_t r, d;
    offsetsAtUtc(date, &r, &d);
    return r + d;
}

int32_t TimeZone::getOffset(int32_t era, int32_t year, int32_t month, int32_t day, int32_t dayOfWeek,
                            int32_t milliseconds) {
    (void)dayOfWeek;
    using namespace detail::tm;
    if (era == 0) year = 1 - year;
    int64_t fd = gregorianFixedDate(year, month + 1, day);
    int64_t wall = (fd - EPOCH_OFFSET) * ONE_DAY + milliseconds;
    int32_t raw = getRawOffset();
    return getOffset(wall - raw);
}

int32_t TimeZone::getRawOffset() { return data_->rawNow * 1000; }

void TimeZone::setRawOffset(int32_t offsetMillis) {
    // Java ZoneInfo.setRawOffset adjusts the rules; approximate with a fixed zone.
    auto* z = new detail::ZoneData(*data_);
    z->fixed = true;
    z->fixedOffset = offsetMillis / 1000;
    z->rawNow = z->fixedOffset;
    data_ = z;
}

int32_t TimeZone::getDSTSavings() {
    if (!useDaylightTime()) return 0;
    return (data_->rule.dstOff - data_->rule.stdOff) * 1000;
}

bool TimeZone::useDaylightTime() { return !data_->fixed && data_->rule.valid && data_->rule.hasDst; }

bool TimeZone::inDaylightTime(Date* date) {
    if (date == nullptr) detail::throwNullPointerException();
    auto in = data_->info(date->getTime());
    return in.dst != 0;
}

bool TimeZone::hasSameRules(TimeZone* other) {
    if (other == nullptr) return false;
    if (other->data_ == data_) return true;
    return getRawOffset() == other->getRawOffset() && useDaylightTime() == other->useDaylightTime() &&
           data_->trans == other->data_->trans && getDSTSavings() == other->getDSTSavings();
}

String TimeZone::getDisplayName() { return getDisplayName(false, LONG); }
String TimeZone::getDisplayName(Locale* locale) {
    (void)locale;
    return getDisplayName(false, LONG);
}
String TimeZone::getDisplayName(bool daylight, int32_t style, Locale* locale) {
    (void)locale;
    return getDisplayName(daylight, style);
}

String TimeZone::getDisplayName(bool daylight, int32_t style) {
    const detail::ZoneData* z = data_;
    if (z->fixed) {
        if (z->id == "GMT") return String(style == LONG ? "Greenwich Mean Time" : "GMT");
        return String(detail::gmtName(z->fixedOffset * 1000));
    }
    std::string stdName = z->rule.valid ? z->rule.stdName : std::string();
    std::string dstName = z->rule.valid ? z->rule.dstName : std::string();
    if (stdName.empty() && !z->types.empty()) {
        // no footer: the last standard/DST abbreviations of the table
        for (size_t i = z->idx.size(); i-- > 0;) {
            const auto& t = z->types[z->idx[i]];
            if (!t.dst && stdName.empty()) stdName = t.abbr;
            if (t.dst && dstName.empty()) dstName = t.abbr;
        }
        if (stdName.empty()) stdName = z->types[0].abbr;
    }
    if (dstName.empty()) {
        // historical DST abbreviation (e.g. KDT), else the Java-style "xDT" guess
        for (size_t i = z->idx.size(); i-- > 0;) {
            const auto& t = z->types[z->idx[i]];
            if (t.dst) {
                dstName = t.abbr;
                break;
            }
        }
    }
    const std::string& n = daylight && !dstName.empty() ? dstName : stdName;
    int32_t off = z->rawNow * 1000 + (daylight ? 3600000 : 0);
    if (!detail::isAlphaName(n)) return String(detail::gmtName(off));
    if (style == LONG) {
        for (const auto& ln : detail::LONG_NAMES)
            if (stdName == ln.shortStd) return String(daylight ? ln.longDst : ln.longStd);
        return String(detail::gmtName(off));
    }
    return String(n);
}

String TimeZone::shortNameAt(int64_t utcMillis) {
    auto in = data_->info(utcMillis);
    if (data_->fixed) return getDisplayName(false, SHORT);
    if (data_->rule.valid && !data_->rule.stdName.empty()) {
        // Java prints the zone's current names, chosen by the DST flag of the instant
        bool daylight = in.dst != 0;
        const std::string& n = daylight ? data_->rule.dstName : data_->rule.stdName;
        if (!n.empty() && detail::isAlphaName(n)) return String(n);
    }
    if (in.abbr != nullptr && detail::isAlphaName(*in.abbr)) return String(*in.abbr);
    return String(detail::gmtName((in.raw + in.dst) * 1000));
}

String TimeZone::longNameAt(int64_t utcMillis) {
    auto in = data_->info(utcMillis);
    return getDisplayName(in.dst != 0, LONG);
}

bool TimeZone::equals(Object* o) {
    auto* tz = dynamic_cast<TimeZone*>(o);
    if (tz == nullptr) return false;
    return id_.equals(tz->id_) && hasSameRules(tz);
}

int32_t TimeZone::hashCode() { return id_.hashCode() ^ getRawOffset(); }

String TimeZone::toString() {
    return str("sun.util.calendar.ZoneInfo[id=\"", id_, "\",offset=", getRawOffset(), ",dstSavings=", getDSTSavings(),
               ",useDaylight=", useDaylightTime(), ",transitions=", static_cast<int32_t>(data_->trans.size()), "]");
}

Object* TimeZone::clone() { return new TimeZone(*this); }

// ---------------------------------------------------------------------------------------
// Locale

Locale::Locale(const String& language) : Locale(language, String(""), String("")) {}
Locale::Locale(const String& language, const String& country) : Locale(language, country, String("")) {}
Locale::Locale(const String& language, const String& country, const String& variant)
    : language_(language == nullptr ? String("") : language.toLowerCase()),
      country_(country == nullptr ? String("") : country.toUpperCase()),
      variant_(variant == nullptr ? String("") : variant) {}

namespace {
std::atomic<Locale*> g_defaultLocale{nullptr};
Locale* g_defaultLocaleRoot = nullptr;
}  // namespace

Locale* Locale::getDefault() {
    Locale* l = g_defaultLocale.load(std::memory_order_acquire);
    return l != nullptr ? l : US;
}

void Locale::setDefault(Locale* l) {
    if (l == nullptr) detail::throwNullPointerException("Can't set default locale to NULL");
    g_defaultLocaleRoot = l;
    g_defaultLocale.store(l, std::memory_order_release);
}

Array<Locale*>* Locale::getAvailableLocales() {
    Locale* all[] = {ENGLISH, US, UK, GERMAN, GERMANY, FRENCH, FRANCE, ITALIAN, ITALY, JAPANESE, JAPAN, KOREAN, KOREA,
                     CHINESE, CHINA};
    auto* arr = new Array<Locale*>(static_cast<int32_t>(sizeof all / sizeof all[0]));
    for (int32_t i = 0; i < arr->length; i++) (*arr)[i] = all[i];
    return arr;
}

String Locale::getDisplayLanguage() {
    static const char* const names[][2] = {{"en", "English"}, {"de", "German"},   {"fr", "French"},
                                           {"it", "Italian"}, {"ja", "Japanese"}, {"ko", "Korean"},
                                           {"zh", "Chinese"}, {"ru", "Russian"},  {"es", "Spanish"}};
    for (const auto& n : names)
        if (language_ == n[0]) return String(n[1]);
    return language_;
}

String Locale::getDisplayCountry() {
    static const char* const names[][2] = {{"US", "United States"}, {"GB", "United Kingdom"}, {"DE", "Germany"},
                                           {"FR", "France"},        {"IT", "Italy"},          {"JP", "Japan"},
                                           {"KR", "South Korea"},   {"CN", "China"},          {"RU", "Russia"}};
    for (const auto& n : names)
        if (country_ == n[0]) return String(n[1]);
    return country_;
}

String Locale::getDisplayName() {
    String lang = getDisplayLanguage();
    String ctry = getDisplayCountry();
    if (ctry.isEmpty()) return lang;
    if (lang.isEmpty()) return ctry;
    return str(lang, " (", ctry, ")");
}

String Locale::toString() {
    bool l = !language_.isEmpty(), c = !country_.isEmpty(), v = !variant_.isEmpty();
    String r("");
    r += language_;
    if (c || (l && v)) r += str("_", country_);
    if (v && (l || c)) r += str("_", variant_);
    return r;
}

bool Locale::equals(Object* o) {
    if (o == this) return true;
    auto* l = dynamic_cast<Locale*>(o);
    return l != nullptr && language_.equals(l->language_) && country_.equals(l->country_) &&
           variant_.equals(l->variant_);
}

int32_t Locale::hashCode() { return language_.hashCode() * 31 * 31 + country_.hashCode() * 31 + variant_.hashCode(); }

}  // namespace jlang
