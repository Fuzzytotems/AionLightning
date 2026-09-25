// Internal helpers shared by the jlang Time.h implementation files (time_*.cpp).
// Calendar arithmetic follows sun.util.calendar (Gregorian and Julian "fixed dates":
// day 1 = January 1, 1 (Gregorian)).
#pragma once

#include <jlang/Time.h>

#include <cstdint>
#include <string>

namespace jlang::detail::tm {

constexpr int64_t ONE_SECOND = 1000;
constexpr int64_t ONE_MINUTE = 60 * ONE_SECOND;
constexpr int64_t ONE_HOUR = 60 * ONE_MINUTE;
constexpr int64_t ONE_DAY = 24 * ONE_HOUR;
constexpr int64_t EPOCH_OFFSET = 719163;  // fixed date of 1970-01-01 (Gregorian)
constexpr int32_t EPOCH_YEAR = 1970;
constexpr int64_t UTC1900_MS = -2208988800000LL;  // 1900-01-01T00:00Z

inline int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
inline int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

inline bool isGregorianLeapYear(int64_t y) { return (floorMod(y, 4) == 0) && ((y % 100 != 0) || (floorMod(y, 400) == 0)); }
inline bool isJulianLeapYear(int64_t y) { return floorMod(y, 4) == 0; }

// month is 1-based (1..12), day of month may be out of range (linear).
int64_t gregorianFixedDate(int64_t year, int32_t month, int64_t dayOfMonth);
int64_t julianFixedDate(int64_t year, int32_t month, int64_t dayOfMonth);
int32_t gregorianYearFromFixed(int64_t fixedDate);
// Normalized (astronomical) year, 1-based month and day of month from a fixed date.
void gregorianFromFixed(int64_t fixedDate, int32_t* year, int32_t* month, int32_t* dayOfMonth);
void julianFromFixed(int64_t fixedDate, int32_t* year, int32_t* month, int32_t* dayOfMonth);
// 1 = Sunday ... 7 = Saturday
inline int32_t dayOfWeekFromFixed(int64_t fixedDate) { return static_cast<int32_t>(floorMod(fixedDate, 7)) + 1; }
inline int64_t dayOfWeekDateOnOrBefore(int64_t fixedDate, int32_t dayOfWeek) {
    int64_t fd = fixedDate - (dayOfWeek - 1);
    return fixedDate - floorMod(fd, 7);
}
int32_t gregorianMonthLength(int64_t year, int32_t month0);  // month 0-based
int32_t julianMonthLength(int64_t year, int32_t month0);

// Local (wall) fields of an instant in a zone, Julian before the cutover (Java Date semantics).
struct LocalFields {
    int32_t year;       // normalized (astronomical) year
    int32_t month;      // 0-11
    int32_t day;        // 1-31
    int32_t dayOfWeek;  // 1 = Sunday
    int32_t hour, minute, second, millis;
    int32_t rawOffset, dstOffset;
};
LocalFields localFields(int64_t utcMillis, TimeZone* zone);

extern const char* const MONTH_NAMES[13];
extern const char* const SHORT_MONTH_NAMES[13];
extern const char* const WEEKDAY_NAMES[8];  // 1-based
extern const char* const SHORT_WEEKDAY_NAMES[8];
extern const char* const AMPM_NAMES[2];
extern const char* const ERA_NAMES[2];

// Zero-padded decimal ("%0*d" for non-negative values; '-' then padding for negatives).
void appendPadded(std::string& out, int64_t value, int32_t minDigits);

}  // namespace jlang::detail::tm
