// java.util.Date, java.sql.Timestamp, java.util.Calendar / GregorianCalendar.
// The Calendar code is a port of OpenJDK 8's java.util.Calendar and GregorianCalendar
// (field stamps, selectFields, computeTime/computeFields, add, roll, getActualMaximum).
#include "time_internal.h"

#include <climits>
#include <cstring>

namespace jlang {

namespace detail {
TimeZone* defaultTimeZoneRef();
}

using namespace detail::tm;

namespace {

constexpr int32_t UNSET = 0;
constexpr int32_t COMPUTED = 1;
constexpr int32_t MINIMUM_USER_STAMP = 2;
constexpr int32_t ALL_FIELDS = (1 << Calendar::FIELD_COUNT) - 1;

constexpr int32_t ERA_MASK = 1 << Calendar::ERA;
constexpr int32_t YEAR_MASK = 1 << Calendar::YEAR;
constexpr int32_t MONTH_MASK = 1 << Calendar::MONTH;
constexpr int32_t WEEK_OF_YEAR_MASK = 1 << Calendar::WEEK_OF_YEAR;
constexpr int32_t WEEK_OF_MONTH_MASK = 1 << Calendar::WEEK_OF_MONTH;
constexpr int32_t DAY_OF_MONTH_MASK = 1 << Calendar::DAY_OF_MONTH;
constexpr int32_t DAY_OF_YEAR_MASK = 1 << Calendar::DAY_OF_YEAR;
constexpr int32_t DAY_OF_WEEK_MASK = 1 << Calendar::DAY_OF_WEEK;
constexpr int32_t DAY_OF_WEEK_IN_MONTH_MASK = 1 << Calendar::DAY_OF_WEEK_IN_MONTH;
constexpr int32_t AM_PM_MASK = 1 << Calendar::AM_PM;
constexpr int32_t HOUR_MASK = 1 << Calendar::HOUR;
constexpr int32_t HOUR_OF_DAY_MASK = 1 << Calendar::HOUR_OF_DAY;
constexpr int32_t MINUTE_MASK = 1 << Calendar::MINUTE;
constexpr int32_t SECOND_MASK = 1 << Calendar::SECOND;
constexpr int32_t MILLISECOND_MASK = 1 << Calendar::MILLISECOND;
constexpr int32_t ZONE_OFFSET_MASK = 1 << Calendar::ZONE_OFFSET;
constexpr int32_t DST_OFFSET_MASK = 1 << Calendar::DST_OFFSET;

constexpr int32_t BCE = 0;
constexpr int32_t CE = 1;

const int32_t MIN_VALUES[Calendar::FIELD_COUNT] = {
    BCE, 1, Calendar::JANUARY, 1, 0, 1, 1, Calendar::SUNDAY, 1, Calendar::AM, 0, 0, 0, 0, 0,
    static_cast<int32_t>(-13 * ONE_HOUR), 0};
const int32_t LEAST_MAX_VALUES[Calendar::FIELD_COUNT] = {
    CE, 292269054, Calendar::DECEMBER, 52, 4, 28, 365, Calendar::SATURDAY, 4, Calendar::PM, 11, 23, 59, 59, 999,
    static_cast<int32_t>(14 * ONE_HOUR), static_cast<int32_t>(20 * ONE_MINUTE)};
const int32_t MAX_VALUES[Calendar::FIELD_COUNT] = {
    CE, 292278994, Calendar::DECEMBER, 53, 6, 31, 366, Calendar::SATURDAY, 6, Calendar::PM, 11, 23, 59, 59, 999,
    static_cast<int32_t>(14 * ONE_HOUR), static_cast<int32_t>(2 * ONE_HOUR)};

const char* const FIELD_NAMES[Calendar::FIELD_COUNT] = {
    "ERA",         "YEAR",   "MONTH",       "WEEK_OF_YEAR", "WEEK_OF_MONTH", "DAY_OF_MONTH",
    "DAY_OF_YEAR", "DAY_OF_WEEK", "DAY_OF_WEEK_IN_MONTH", "AM_PM", "HOUR", "HOUR_OF_DAY",
    "MINUTE",      "SECOND", "MILLISECOND", "ZONE_OFFSET",  "DST_OFFSET"};

inline int32_t aggregateStamp(int32_t a, int32_t b) {
    if (a == UNSET || b == UNSET) return UNSET;
    return a > b ? a : b;
}
inline bool isFieldSet(int32_t fieldMask, int32_t field) { return (fieldMask & (1 << field)) != 0; }

inline void checkField(int32_t field) {
    if (field < 0 || field >= Calendar::FIELD_COUNT) throw ArrayIndexOutOfBoundsException(field);
}

int32_t rolledValue(int32_t value, int32_t amount, int32_t min, int32_t max) {
    int32_t range = max - min + 1;
    amount %= range;
    int32_t n = value + amount;
    if (n > max) n -= range;
    else if (n < min) n += range;
    return n;
}

// Week data (Java CalendarData): most of Europe uses Monday/4, the rest Sunday/1.
void weekCountData(Locale* locale, int32_t* first, int32_t* minDays) {
    *first = Calendar::SUNDAY;
    *minDays = 1;
    if (locale == nullptr) return;
    static const char* const EU[] = {"AD", "AT", "BE", "BG", "CH", "CZ", "DE", "DK", "EE", "ES", "FI", "FO",
                                     "FR", "GB", "GF", "GG", "GI", "GP", "HU", "IE", "IM", "IS", "IT", "JE",
                                     "LI", "LT", "LU", "MC", "MQ", "NL", "NO", "PL", "PT", "RE", "RU", "SE",
                                     "SJ", "SK", "SM", "VA"};
    String c = locale->getCountry();
    for (const char* e : EU)
        if (c == e) {
            *first = Calendar::MONDAY;
            *minDays = 4;
            return;
        }
}

}  // namespace

// =======================================================================================
// Calendar

Calendar::Calendar() : Calendar(detail::defaultTimeZoneRef(), Locale::getDefault()) { sharedZone_ = true; }

Calendar::Calendar(TimeZone* zone, Locale* locale) : zone_(zone) {
    weekCountData(locale, &firstDayOfWeek_, &minimalDaysInFirstWeek_);
}

Calendar* Calendar::getInstance() { return new GregorianCalendar(); }
Calendar* Calendar::getInstance(TimeZone* zone) { return new GregorianCalendar(zone); }
Calendar* Calendar::getInstance(Locale* locale) { return new GregorianCalendar(locale); }
Calendar* Calendar::getInstance(TimeZone* zone, Locale* locale) { return new GregorianCalendar(zone, locale); }

int32_t Calendar::get(int32_t field) {
    checkField(field);
    complete();
    return fields_[field];
}

void Calendar::set(int32_t field, int32_t value) {
    checkField(field);
    if (areFieldsSet_ && !areAllFieldsSet_) computeFieldsFromTime();
    fields_[field] = value;
    isTimeSet_ = false;
    areFieldsSet_ = false;
    stamp_[field] = nextStamp_++;
    if (nextStamp_ == INT_MAX) adjustStamp();
}

void Calendar::set(int32_t year, int32_t month, int32_t date) {
    set(YEAR, year);
    set(MONTH, month);
    set(DATE, date);
}

void Calendar::set(int32_t year, int32_t month, int32_t date, int32_t hourOfDay, int32_t minute) {
    set(YEAR, year);
    set(MONTH, month);
    set(DATE, date);
    set(HOUR_OF_DAY, hourOfDay);
    set(MINUTE, minute);
}

void Calendar::set(int32_t year, int32_t month, int32_t date, int32_t hourOfDay, int32_t minute, int32_t second) {
    set(YEAR, year);
    set(MONTH, month);
    set(DATE, date);
    set(HOUR_OF_DAY, hourOfDay);
    set(MINUTE, minute);
    set(SECOND, second);
}

void Calendar::clear() {
    for (int32_t i = 0; i < FIELD_COUNT; i++) {
        fields_[i] = 0;
        stamp_[i] = UNSET;
    }
    areAllFieldsSet_ = areFieldsSet_ = false;
    isTimeSet_ = false;
}

void Calendar::clear(int32_t field) {
    checkField(field);
    fields_[field] = 0;
    stamp_[field] = UNSET;
    areAllFieldsSet_ = areFieldsSet_ = false;
    isTimeSet_ = false;
}

bool Calendar::isSet(int32_t field) {
    checkField(field);
    return stamp_[field] != UNSET;
}

void Calendar::adjustStamp() {
    int32_t max = MINIMUM_USER_STAMP;
    int32_t newStamp = MINIMUM_USER_STAMP;
    for (;;) {
        int32_t min = INT_MAX;
        for (int32_t v : stamp_) {
            if (v >= newStamp && min > v) min = v;
            if (max < v) max = v;
        }
        if (max != min && min == INT_MAX) break;
        for (int32_t& v : stamp_)
            if (v == min) v = newStamp;
        newStamp++;
        if (min == max) break;
    }
    nextStamp_ = newStamp;
}

int32_t Calendar::getSetStateFields() {
    int32_t mask = 0;
    for (int32_t i = 0; i < FIELD_COUNT; i++)
        if (stamp_[i] != UNSET) mask |= 1 << i;
    return mask;
}

void Calendar::setFieldsComputed(int32_t mask) {
    if (mask == ALL_FIELDS) {
        for (int32_t i = 0; i < FIELD_COUNT; i++) stamp_[i] = COMPUTED;
        areFieldsSet_ = areAllFieldsSet_ = true;
    } else {
        for (int32_t i = 0; i < FIELD_COUNT; i++) {
            if ((mask & 1) == 1) {
                stamp_[i] = COMPUTED;
            } else if (areAllFieldsSet_ && stamp_[i] == UNSET) {
                areAllFieldsSet_ = false;
            }
            mask >>= 1;
        }
    }
}

void Calendar::setFieldsNormalized(int32_t mask) {
    if (mask != ALL_FIELDS) {
        for (int32_t i = 0; i < FIELD_COUNT; i++) {
            if ((mask & 1) == 0) stamp_[i] = fields_[i] = 0;
            mask >>= 1;
        }
    }
    areFieldsSet_ = true;
    areAllFieldsSet_ = false;
}

void Calendar::complete() {
    if (!isTimeSet_) {
        computeTimeFromFields();
        isTimeSet_ = true;
    }
    if (!areFieldsSet_ || !areAllFieldsSet_) {
        computeFieldsFromTime();
        areAllFieldsSet_ = areFieldsSet_ = true;
    }
}

void Calendar::computeFieldsFromTime() {
    int32_t mask;
    if (areFieldsSet_ && !areAllFieldsSet_) {
        mask = getSetStateFields();
        int32_t fieldMask = ~mask & ALL_FIELDS;
        if (fieldMask != 0 || !calsysKnown_) {
            mask |= computeFieldsMask(fieldMask, mask & (ZONE_OFFSET_MASK | DST_OFFSET_MASK));
        }
    } else {
        mask = ALL_FIELDS;
        computeFieldsMask(mask, 0);
    }
    setFieldsComputed(mask);
}

int32_t Calendar::internalGetEra() { return stamp_[ERA] != UNSET ? fields_[ERA] : CE; }

bool Calendar::isLeapYear(int32_t year) {
    if ((year & 3) != 0) return false;
    if (year > cutoverYear_) return (year % 100 != 0) || (year % 400 == 0);
    if (year < cutoverYearJulian_) return true;
    bool gregorian;
    if (cutoverYear_ == cutoverYearJulian_) {
        int32_t y, m, d;
        gregorianFromFixed(cutoverFixedDate_, &y, &m, &d);
        gregorian = m < 3;
    } else {
        gregorian = year == cutoverYear_;
    }
    return gregorian ? (year % 100 != 0) || (year % 400 == 0) : true;
}

int32_t Calendar::monthLength(int32_t month, int32_t year) {
    static const int32_t ML[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    static const int32_t LML[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return isLeapYear(year) ? LML[month] : ML[month];
}

bool Calendar::isCutoverYear(int32_t normalizedYear) {
    int32_t cy = calsysGregorian_ ? cutoverYear_ : cutoverYearJulian_;
    return normalizedYear == cy;
}

int32_t Calendar::weekNumber(int64_t fixedDay1, int64_t fixedDate) {
    int64_t fixedDay1st = dayOfWeekDateOnOrBefore(fixedDay1 + 6, firstDayOfWeek_);
    int32_t ndays = static_cast<int32_t>(fixedDay1st - fixedDay1);
    if (ndays >= minimalDaysInFirstWeek_) fixedDay1st -= 7;
    int32_t normalizedDayOfPeriod = static_cast<int32_t>(fixedDate - fixedDay1st);
    if (normalizedDayOfPeriod >= 0) return normalizedDayOfPeriod / 7 + 1;
    return static_cast<int32_t>(floorDiv(normalizedDayOfPeriod, 7)) + 1;
}

// GregorianCalendar.computeFields(int fieldMask, int tzMask)
int32_t Calendar::computeFieldsMask(int32_t fieldMask, int32_t tzMask) {
    int32_t zoneOffsets[2] = {0, 0};
    int32_t zoneOffset = 0;
    if (tzMask != (ZONE_OFFSET_MASK | DST_OFFSET_MASK)) {
        zone_->offsetsAtUtc(time_, &zoneOffsets[0], &zoneOffsets[1]);
        zoneOffset = zoneOffsets[0] + zoneOffsets[1];
    }
    if (tzMask != 0) {
        if (isFieldSet(tzMask, ZONE_OFFSET)) zoneOffsets[0] = fields_[ZONE_OFFSET];
        if (isFieldSet(tzMask, DST_OFFSET)) zoneOffsets[1] = fields_[DST_OFFSET];
        zoneOffset = zoneOffsets[0] + zoneOffsets[1];
    }
    int64_t fixedDate = zoneOffset / ONE_DAY;
    int32_t timeOfDay = static_cast<int32_t>(zoneOffset % ONE_DAY);
    fixedDate += time_ / ONE_DAY;
    timeOfDay += static_cast<int32_t>(time_ % ONE_DAY);
    if (timeOfDay >= ONE_DAY) {
        timeOfDay -= static_cast<int32_t>(ONE_DAY);
        ++fixedDate;
    } else {
        while (timeOfDay < 0) {
            timeOfDay += static_cast<int32_t>(ONE_DAY);
            --fixedDate;
        }
    }
    fixedDate += EPOCH_OFFSET;

    int32_t era = CE;
    int32_t year, month1, dayOfMonth, normYear;
    if (fixedDate >= cutoverFixedDate_) {
        gregorianFromFixed(fixedDate, &normYear, &month1, &dayOfMonth);
        year = normYear;
        if (year <= 0) {
            year = 1 - year;
            era = BCE;
        }
        calsysGregorian_ = true;
    } else {
        julianFromFixed(fixedDate, &normYear, &month1, &dayOfMonth);
        year = normYear;
        if (year <= 0) {
            year = 1 - year;
            era = BCE;
        }
        calsysGregorian_ = false;
    }
    calsysKnown_ = true;

    fields_[ERA] = era;
    fields_[YEAR] = year;
    int32_t mask = fieldMask | (ERA_MASK | YEAR_MASK);
    int32_t month = month1 - 1;

    if ((fieldMask & (MONTH_MASK | DAY_OF_MONTH_MASK | DAY_OF_WEEK_MASK)) != 0) {
        fields_[MONTH] = month;
        fields_[DAY_OF_MONTH] = dayOfMonth;
        fields_[DAY_OF_WEEK] = dayOfWeekFromFixed(fixedDate);
        mask |= MONTH_MASK | DAY_OF_MONTH_MASK | DAY_OF_WEEK_MASK;
    }

    if ((fieldMask & (HOUR_OF_DAY_MASK | AM_PM_MASK | HOUR_MASK | MINUTE_MASK | SECOND_MASK | MILLISECOND_MASK)) != 0) {
        if (timeOfDay != 0) {
            int32_t hours = timeOfDay / static_cast<int32_t>(ONE_HOUR);
            fields_[HOUR_OF_DAY] = hours;
            fields_[AM_PM] = hours / 12;
            fields_[HOUR] = hours % 12;
            int32_t r = timeOfDay % static_cast<int32_t>(ONE_HOUR);
            fields_[MINUTE] = r / static_cast<int32_t>(ONE_MINUTE);
            r %= static_cast<int32_t>(ONE_MINUTE);
            fields_[SECOND] = r / 1000;
            fields_[MILLISECOND] = r % 1000;
        } else {
            fields_[HOUR_OF_DAY] = 0;
            fields_[AM_PM] = AM;
            fields_[HOUR] = 0;
            fields_[MINUTE] = 0;
            fields_[SECOND] = 0;
            fields_[MILLISECOND] = 0;
        }
        mask |= HOUR_OF_DAY_MASK | AM_PM_MASK | HOUR_MASK | MINUTE_MASK | SECOND_MASK | MILLISECOND_MASK;
    }

    if ((fieldMask & (ZONE_OFFSET_MASK | DST_OFFSET_MASK)) != 0) {
        fields_[ZONE_OFFSET] = zoneOffsets[0];
        fields_[DST_OFFSET] = zoneOffsets[1];
        mask |= ZONE_OFFSET_MASK | DST_OFFSET_MASK;
    }

    if ((fieldMask & (DAY_OF_YEAR_MASK | WEEK_OF_YEAR_MASK | WEEK_OF_MONTH_MASK | DAY_OF_WEEK_IN_MONTH_MASK)) != 0) {
        bool greg = calsysGregorian_;
        int64_t fixedDateJan1 = greg ? gregorianFixedDate(normYear, 1, 1) : julianFixedDate(normYear, 1, 1);
        int32_t dayOfYear = static_cast<int32_t>(fixedDate - fixedDateJan1) + 1;
        int64_t fixedDateMonth1 = fixedDate - dayOfMonth + 1;
        int32_t cutoverYear = greg ? cutoverYear_ : cutoverYearJulian_;
        int32_t relativeDayOfMonth = dayOfMonth - 1;
        if (normYear == cutoverYear) {
            // The cutover year: Jan 1 / the 1st of the month may not exist in this calendar.
            if (cutoverYearJulian_ <= cutoverYear_) {
                // first day of the year in the hybrid calendar
                int64_t jan1Julian = julianFixedDate(normYear, 1, 1);
                int64_t jan1Greg = gregorianFixedDate(normYear, 1, 1);
                fixedDateJan1 = (fixedDate >= cutoverFixedDate_ && jan1Greg >= cutoverFixedDate_) ? jan1Greg
                                : (jan1Julian < cutoverFixedDate_ ? jan1Julian : cutoverFixedDate_);
                if (fixedDate >= cutoverFixedDate_) {
                    int64_t m1 = gregorianFixedDate(normYear, month1, 1);
                    fixedDateMonth1 = m1 >= cutoverFixedDate_ ? m1 : cutoverFixedDate_;
                    int32_t jy, jm, jd;
                    julianFromFixed(cutoverFixedDate_ - 1, &jy, &jm, &jd);
                    if (jm == month1 && jy == normYear) fixedDateMonth1 = julianFixedDate(jy, jm, 1);
                }
            }
            int32_t realDayOfYear = static_cast<int32_t>(fixedDate - fixedDateJan1) + 1;
            dayOfYear = realDayOfYear;
            relativeDayOfMonth = static_cast<int32_t>(fixedDate - fixedDateMonth1);
        }
        fields_[DAY_OF_YEAR] = dayOfYear;
        fields_[DAY_OF_WEEK_IN_MONTH] = relativeDayOfMonth / 7 + 1;

        int32_t weekOfYear = weekNumber(fixedDateJan1, fixedDate);
        if (weekOfYear == 0) {
            int64_t fixedDec31 = fixedDateJan1 - 1;
            int64_t prevJan1 = fixedDateJan1 - 365;
            if (normYear > (cutoverYear + 1)) {
                if (isGregorianLeapYear(normYear - 1)) --prevJan1;
            } else if (normYear <= cutoverYearJulian_) {
                if (isJulianLeapYear(normYear - 1)) --prevJan1;
            } else {
                int32_t prevYear = normYear - 1;
                if (prevYear == cutoverYear_) {
                    prevJan1 = julianFixedDate(prevYear, 1, 1);
                    if (prevJan1 >= cutoverFixedDate_) prevJan1 = gregorianFixedDate(prevYear, 1, 1);
                } else if (prevYear <= cutoverYearJulian_) {
                    prevJan1 = julianFixedDate(prevYear, 1, 1);
                } else {
                    prevJan1 = gregorianFixedDate(prevYear, 1, 1);
                }
            }
            weekOfYear = weekNumber(prevJan1, fixedDec31);
        } else {
            if (normYear > cutoverYear_ || normYear < (cutoverYearJulian_ - 1)) {
                if (weekOfYear >= 52) {
                    int64_t nextJan1 = fixedDateJan1 + 365;
                    bool leap = greg ? isGregorianLeapYear(normYear) : isJulianLeapYear(normYear);
                    if (leap) nextJan1++;
                    int64_t nextJan1st = dayOfWeekDateOnOrBefore(nextJan1 + 6, firstDayOfWeek_);
                    int32_t ndays = static_cast<int32_t>(nextJan1st - nextJan1);
                    if (ndays >= minimalDaysInFirstWeek_ && fixedDate >= (nextJan1st - 7)) weekOfYear = 1;
                }
            } else {
                int64_t nextJan1;
                if (normYear + 1 <= cutoverYearJulian_) nextJan1 = julianFixedDate(normYear + 1, 1, 1);
                else nextJan1 = gregorianFixedDate(normYear + 1, 1, 1);
                if (normYear + 1 == cutoverYear_ && nextJan1 < cutoverFixedDate_) {
                    // Jan 1 of the cutover year may be skipped
                    int64_t g = gregorianFixedDate(normYear + 1, 1, 1);
                    nextJan1 = g >= cutoverFixedDate_ ? g : cutoverFixedDate_;
                }
                int64_t nextJan1st = dayOfWeekDateOnOrBefore(nextJan1 + 6, firstDayOfWeek_);
                int32_t ndays = static_cast<int32_t>(nextJan1st - nextJan1);
                if (ndays >= minimalDaysInFirstWeek_ && fixedDate >= (nextJan1st - 7)) weekOfYear = 1;
            }
        }
        fields_[WEEK_OF_YEAR] = weekOfYear;
        fields_[WEEK_OF_MONTH] = weekNumber(fixedDateMonth1, fixedDate);
        mask |= DAY_OF_YEAR_MASK | WEEK_OF_YEAR_MASK | WEEK_OF_MONTH_MASK | DAY_OF_WEEK_IN_MONTH_MASK;
    }
    return mask;
}

// Calendar.selectFields()
int32_t Calendar::selectFields() {
    int32_t fieldMask = YEAR_MASK;
    if (stamp_[ERA] != UNSET) fieldMask |= ERA_MASK;

    int32_t dowStamp = stamp_[DAY_OF_WEEK];
    int32_t monthStamp = stamp_[MONTH];
    int32_t domStamp = stamp_[DAY_OF_MONTH];
    int32_t womStamp = aggregateStamp(stamp_[WEEK_OF_MONTH], dowStamp);
    int32_t dowimStamp = aggregateStamp(stamp_[DAY_OF_WEEK_IN_MONTH], dowStamp);
    int32_t doyStamp = stamp_[DAY_OF_YEAR];
    int32_t woyStamp = aggregateStamp(stamp_[WEEK_OF_YEAR], dowStamp);

    int32_t bestStamp = domStamp;
    if (womStamp > bestStamp) bestStamp = womStamp;
    if (dowimStamp > bestStamp) bestStamp = dowimStamp;
    if (doyStamp > bestStamp) bestStamp = doyStamp;
    if (woyStamp > bestStamp) bestStamp = woyStamp;

    if (bestStamp == UNSET) {
        womStamp = stamp_[WEEK_OF_MONTH];
        dowimStamp = std::max(stamp_[DAY_OF_WEEK_IN_MONTH], dowStamp);
        woyStamp = stamp_[WEEK_OF_YEAR];
        bestStamp = std::max(std::max(womStamp, dowimStamp), woyStamp);
        if (bestStamp == UNSET) bestStamp = domStamp = monthStamp;
    }

    if (bestStamp == domStamp || (bestStamp == womStamp && stamp_[WEEK_OF_MONTH] >= stamp_[WEEK_OF_YEAR]) ||
        (bestStamp == dowimStamp && stamp_[DAY_OF_WEEK_IN_MONTH] >= stamp_[WEEK_OF_YEAR])) {
        fieldMask |= MONTH_MASK;
        if (bestStamp == domStamp) {
            fieldMask |= DAY_OF_MONTH_MASK;
        } else {
            if (dowStamp != UNSET) fieldMask |= DAY_OF_WEEK_MASK;
            if (womStamp == dowimStamp) {
                if (stamp_[WEEK_OF_MONTH] >= stamp_[DAY_OF_WEEK_IN_MONTH]) fieldMask |= WEEK_OF_MONTH_MASK;
                else fieldMask |= DAY_OF_WEEK_IN_MONTH_MASK;
            } else {
                if (bestStamp == womStamp) {
                    fieldMask |= WEEK_OF_MONTH_MASK;
                } else if (stamp_[DAY_OF_WEEK_IN_MONTH] != UNSET) {
                    fieldMask |= DAY_OF_WEEK_IN_MONTH_MASK;
                }
            }
        }
    } else {
        if (bestStamp == doyStamp) {
            fieldMask |= DAY_OF_YEAR_MASK;
        } else {
            if (dowStamp != UNSET) fieldMask |= DAY_OF_WEEK_MASK;
            fieldMask |= WEEK_OF_YEAR_MASK;
        }
    }

    int32_t hourOfDayStamp = stamp_[HOUR_OF_DAY];
    int32_t hourStamp = aggregateStamp(stamp_[HOUR], stamp_[AM_PM]);
    bestStamp = (hourStamp > hourOfDayStamp) ? hourStamp : hourOfDayStamp;
    if (bestStamp == UNSET) bestStamp = std::max(stamp_[HOUR], stamp_[AM_PM]);
    if (bestStamp != UNSET) {
        if (bestStamp == hourOfDayStamp) {
            fieldMask |= HOUR_OF_DAY_MASK;
        } else {
            fieldMask |= HOUR_MASK;
            if (stamp_[AM_PM] != UNSET) fieldMask |= AM_PM_MASK;
        }
    }
    if (stamp_[MINUTE] != UNSET) fieldMask |= MINUTE_MASK;
    if (stamp_[SECOND] != UNSET) fieldMask |= SECOND_MASK;
    if (stamp_[MILLISECOND] != UNSET) fieldMask |= MILLISECOND_MASK;
    if (stamp_[ZONE_OFFSET] >= MINIMUM_USER_STAMP) fieldMask |= ZONE_OFFSET_MASK;
    if (stamp_[DST_OFFSET] >= MINIMUM_USER_STAMP) fieldMask |= DST_OFFSET_MASK;
    return fieldMask;
}

// GregorianCalendar.getFixedDate(BaseCalendar cal, int year, int fieldMask)
int64_t Calendar::getFixedDateFor(bool gregorian, int32_t year, int32_t fieldMask) {
    int32_t month = JANUARY;
    if (isFieldSet(fieldMask, MONTH)) {
        month = fields_[MONTH];
        if (month > DECEMBER) {
            year += month / 12;
            month %= 12;
        } else if (month < JANUARY) {
            year += static_cast<int32_t>(floorDiv(month, 12));
            month = static_cast<int32_t>(floorMod(month, 12));
        }
    }
    auto fixedOf = [&](int32_t y, int32_t m1, int32_t d) {
        return gregorian ? gregorianFixedDate(y, m1, d) : julianFixedDate(y, m1, d);
    };
    int64_t fixedDate = fixedOf(year, month + 1, 1);
    if (isFieldSet(fieldMask, MONTH)) {
        if (isFieldSet(fieldMask, DAY_OF_MONTH)) {
            if (stamp_[DAY_OF_MONTH] != UNSET) {
                fixedDate += fields_[DAY_OF_MONTH];
                fixedDate--;
            }
        } else {
            if (isFieldSet(fieldMask, WEEK_OF_MONTH)) {
                int64_t firstDayOfWeek = dayOfWeekDateOnOrBefore(fixedDate + 6, firstDayOfWeek_);
                if ((firstDayOfWeek - fixedDate) >= minimalDaysInFirstWeek_) firstDayOfWeek -= 7;
                if (isFieldSet(fieldMask, DAY_OF_WEEK))
                    firstDayOfWeek = dayOfWeekDateOnOrBefore(firstDayOfWeek + 6, fields_[DAY_OF_WEEK]);
                fixedDate = firstDayOfWeek + 7 * (static_cast<int64_t>(fields_[WEEK_OF_MONTH]) - 1);
            } else {
                int32_t dayOfWeek = isFieldSet(fieldMask, DAY_OF_WEEK) ? fields_[DAY_OF_WEEK] : firstDayOfWeek_;
                int32_t dowim = isFieldSet(fieldMask, DAY_OF_WEEK_IN_MONTH) ? fields_[DAY_OF_WEEK_IN_MONTH] : 1;
                if (dowim >= 0) {
                    fixedDate = dayOfWeekDateOnOrBefore(fixedDate + (7 * static_cast<int64_t>(dowim)) - 1, dayOfWeek);
                } else {
                    int32_t lastDate = monthLength(month, year) + (7 * (dowim + 1));
                    fixedDate = dayOfWeekDateOnOrBefore(fixedDate + lastDate - 1, dayOfWeek);
                }
            }
        }
    } else {
        if (year == cutoverYear_ && gregorian && fixedDate < cutoverFixedDate_ && cutoverYear_ != cutoverYearJulian_) {
            fixedDate = cutoverFixedDate_;
        }
        if (isFieldSet(fieldMask, DAY_OF_YEAR)) {
            fixedDate += fields_[DAY_OF_YEAR];
            fixedDate--;
        } else {
            int64_t firstDayOfWeek = dayOfWeekDateOnOrBefore(fixedDate + 6, firstDayOfWeek_);
            if ((firstDayOfWeek - fixedDate) >= minimalDaysInFirstWeek_) firstDayOfWeek -= 7;
            if (isFieldSet(fieldMask, DAY_OF_WEEK)) {
                int32_t dayOfWeek = fields_[DAY_OF_WEEK];
                if (dayOfWeek != firstDayOfWeek_) firstDayOfWeek = dayOfWeekDateOnOrBefore(firstDayOfWeek + 6, dayOfWeek);
            }
            fixedDate = firstDayOfWeek + 7 * (static_cast<int64_t>(fields_[WEEK_OF_YEAR]) - 1);
        }
    }
    return fixedDate;
}

// GregorianCalendar.computeTime()
void Calendar::computeTimeFromFields() {
    int32_t originalFields[FIELD_COUNT];
    if (!lenient_) {
        for (int32_t field = 0; field < FIELD_COUNT; field++) {
            int32_t value = fields_[field];
            if (stamp_[field] >= MINIMUM_USER_STAMP) {
                if (value < getMinimum(field) || value > getMaximum(field)) throw IllegalArgumentException(String(FIELD_NAMES[field]));
            }
            originalFields[field] = value;
        }
    }
    int32_t fieldMask = selectFields();
    int32_t year = stamp_[YEAR] != UNSET ? fields_[YEAR] : EPOCH_YEAR;
    int32_t era = internalGetEra();
    if (era == BCE) year = 1 - year;
    else if (era != CE) throw IllegalArgumentException(String("Invalid era"));
    if (year <= 0 && stamp_[ERA] == UNSET) {
        fieldMask |= ERA_MASK;
        setFieldsComputed(ERA_MASK);
    }

    int64_t timeOfDay = 0;
    if (isFieldSet(fieldMask, HOUR_OF_DAY)) {
        timeOfDay += static_cast<int64_t>(fields_[HOUR_OF_DAY]);
    } else {
        timeOfDay += fields_[HOUR];
        if (isFieldSet(fieldMask, AM_PM)) timeOfDay += 12 * static_cast<int64_t>(fields_[AM_PM]);
    }
    timeOfDay *= 60;
    timeOfDay += fields_[MINUTE];
    timeOfDay *= 60;
    timeOfDay += fields_[SECOND];
    timeOfDay *= 1000;
    timeOfDay += fields_[MILLISECOND];

    int64_t fixedDate = timeOfDay / ONE_DAY;
    timeOfDay %= ONE_DAY;
    while (timeOfDay < 0) {
        timeOfDay += ONE_DAY;
        --fixedDate;
    }

    {
        int64_t gfd, jfd;
        if (year > cutoverYear_ && year > cutoverYearJulian_) {
            gfd = fixedDate + getFixedDateFor(true, year, fieldMask);
            if (gfd >= cutoverFixedDate_) {
                fixedDate = gfd;
                goto calculated;
            }
            jfd = fixedDate + getFixedDateFor(false, year, fieldMask);
        } else if (year < cutoverYear_ && year < cutoverYearJulian_) {
            jfd = fixedDate + getFixedDateFor(false, year, fieldMask);
            if (jfd < cutoverFixedDate_) {
                fixedDate = jfd;
                goto calculated;
            }
            gfd = jfd;
        } else {
            jfd = fixedDate + getFixedDateFor(false, year, fieldMask);
            gfd = fixedDate + getFixedDateFor(true, year, fieldMask);
        }
        if (isFieldSet(fieldMask, DAY_OF_YEAR) || isFieldSet(fieldMask, WEEK_OF_YEAR)) {
            if (cutoverYear_ == cutoverYearJulian_) {
                fixedDate = jfd;
                goto calculated;
            } else if (year == cutoverYear_) {
                fixedDate = gfd;
                goto calculated;
            }
        }
        if (gfd >= cutoverFixedDate_) {
            if (jfd >= cutoverFixedDate_) {
                fixedDate = gfd;
            } else {
                fixedDate = (calsysGregorian_ || !calsysKnown_) ? gfd : jfd;
            }
        } else {
            if (jfd < cutoverFixedDate_) {
                fixedDate = jfd;
            } else {
                if (!lenient_) throw IllegalArgumentException(String("the specified date doesn't exist"));
                fixedDate = jfd;
            }
        }
    }
calculated:
    int64_t millis = (fixedDate - EPOCH_OFFSET) * ONE_DAY + timeOfDay;

    int32_t zoneOffsets[2] = {0, 0};
    int32_t tzMask = fieldMask & (ZONE_OFFSET_MASK | DST_OFFSET_MASK);
    if (tzMask != (ZONE_OFFSET_MASK | DST_OFFSET_MASK)) zone_->offsetsAtWall(millis, &zoneOffsets[0], &zoneOffsets[1]);
    if (tzMask != 0) {
        if (isFieldSet(tzMask, ZONE_OFFSET)) zoneOffsets[0] = fields_[ZONE_OFFSET];
        if (isFieldSet(tzMask, DST_OFFSET)) zoneOffsets[1] = fields_[DST_OFFSET];
    }
    millis -= static_cast<int64_t>(zoneOffsets[0]) + zoneOffsets[1];
    time_ = millis;

    int32_t mask = computeFieldsMask(fieldMask | getSetStateFields(), tzMask);

    if (!lenient_) {
        for (int32_t field = 0; field < FIELD_COUNT; field++) {
            if (stamp_[field] < MINIMUM_USER_STAMP) continue;
            if (originalFields[field] != fields_[field]) {
                String s = str(originalFields[field], " -> ", fields_[field]);
                std::memcpy(fields_, originalFields, sizeof fields_);
                throw IllegalArgumentException(str(FIELD_NAMES[field], ": ", s));
            }
        }
    }
    setFieldsNormalized(mask);
}

Date* Calendar::getTime() { return new Date(getTimeInMillis()); }

void Calendar::setTime(Date* date) {
    if (date == nullptr) detail::throwNullPointerException();
    setTimeInMillis(date->getTime());
}

int64_t Calendar::getTimeInMillis() {
    if (!isTimeSet_) {
        computeTimeFromFields();
        isTimeSet_ = true;
    }
    return time_;
}

void Calendar::setTimeInMillis(int64_t millis) {
    if (time_ == millis && isTimeSet_ && areFieldsSet_ && areAllFieldsSet_) return;
    time_ = millis;
    isTimeSet_ = true;
    areFieldsSet_ = false;
    computeFieldsFromTime();
    areAllFieldsSet_ = areFieldsSet_ = true;
}

int64_t Calendar::millisOf(Calendar* c) {
    if (c->isTimeSet_) return c->time_;
    auto* cal = dynamic_cast<Calendar*>(c->clone());
    cal->setLenient(true);
    return cal->getTimeInMillis();
}

bool Calendar::before(Object* when) {
    auto* c = dynamic_cast<Calendar*>(when);
    return c != nullptr && compareTo(c) < 0;
}

bool Calendar::after(Object* when) {
    auto* c = dynamic_cast<Calendar*>(when);
    return c != nullptr && compareTo(c) > 0;
}

int32_t Calendar::compareTo(Calendar* anotherCalendar) {
    if (anotherCalendar == nullptr) detail::throwNullPointerException();
    int64_t a = millisOf(this), b = millisOf(anotherCalendar);
    return a > b ? 1 : (a == b ? 0 : -1);
}

bool Calendar::equals(Object* obj) {
    if (obj == this) return true;
    auto* that = dynamic_cast<Calendar*>(obj);
    if (that == nullptr) return false;
    return millisOf(this) == millisOf(that) && lenient_ == that->lenient_ && firstDayOfWeek_ == that->firstDayOfWeek_ &&
           minimalDaysInFirstWeek_ == that->minimalDaysInFirstWeek_ && zone_->equals(that->zone_) &&
           cutover_ == that->cutover_;
}

int32_t Calendar::hashCode() {
    int32_t otheritems = (lenient_ ? 1 : 0) | (firstDayOfWeek_ << 1) | (minimalDaysInFirstWeek_ << 4) |
                         (zone_->hashCode() << 7);
    int64_t t = millisOf(this);
    int32_t h = static_cast<int32_t>(t) ^ static_cast<int32_t>(t >> 32) ^ otheritems;
    return h ^ static_cast<int32_t>(cutoverFixedDate_);
}

String Calendar::toString() {
    String s("java.util.GregorianCalendar[time=");
    if (isTimeSet_) s += time_;
    else s += "?";
    s += str(",areFieldsSet=", areFieldsSet_, ",areAllFieldsSet=", areAllFieldsSet_, ",lenient=", lenient_,
             ",zone=", zone_, ",firstDayOfWeek=", firstDayOfWeek_, ",minimalDaysInFirstWeek=", minimalDaysInFirstWeek_);
    for (int32_t i = 0; i < FIELD_COUNT; i++) {
        s += str(",", FIELD_NAMES[i], "=");
        if (stamp_[i] != UNSET) s += fields_[i];
        else s += "?";
    }
    s += "]";
    return s;
}

Object* Calendar::clone() { return Object::clone(); }

TimeZone* Calendar::getTimeZone() {
    if (sharedZone_) {
        zone_ = dynamic_cast<TimeZone*>(zone_->clone());
        sharedZone_ = false;
    }
    return zone_;
}

void Calendar::setTimeZone(TimeZone* value) {
    if (value == nullptr) detail::throwNullPointerException();
    zone_ = value;
    sharedZone_ = false;
    areAllFieldsSet_ = areFieldsSet_ = false;
}

void Calendar::setFirstDayOfWeek(int32_t value) {
    if (firstDayOfWeek_ == value) return;
    firstDayOfWeek_ = value;
    invalidateWeekFields();
}

void Calendar::setMinimalDaysInFirstWeek(int32_t value) {
    if (minimalDaysInFirstWeek_ == value) return;
    minimalDaysInFirstWeek_ = value;
    invalidateWeekFields();
}

void Calendar::invalidateWeekFields() {
    if (stamp_[WEEK_OF_MONTH] != COMPUTED && stamp_[WEEK_OF_YEAR] != COMPUTED) return;
    auto* cal = dynamic_cast<Calendar*>(clone());
    cal->setLenient(true);
    cal->clear(WEEK_OF_MONTH);
    cal->clear(WEEK_OF_YEAR);
    if (stamp_[WEEK_OF_MONTH] == COMPUTED) fields_[WEEK_OF_MONTH] = cal->get(WEEK_OF_MONTH);
    if (stamp_[WEEK_OF_YEAR] == COMPUTED) fields_[WEEK_OF_YEAR] = cal->get(WEEK_OF_YEAR);
}

int64_t Calendar::currentFixedDate() {
    int64_t local = time_ + fields_[ZONE_OFFSET] + fields_[DST_OFFSET];
    return floorDiv(local, ONE_DAY) + EPOCH_OFFSET;
}

void Calendar::pinDayOfMonth() {
    int32_t year = fields_[YEAR];
    int32_t monthLen;
    if (year > cutoverYear_ || year < cutoverYearJulian_) {
        monthLen = monthLength(fields_[MONTH], internalGetEra() == BCE ? 1 - year : year);
    } else {
        monthLen = normalizedCalendar()->getActualMaximum(DAY_OF_MONTH);
    }
    if (fields_[DAY_OF_MONTH] > monthLen) set(DAY_OF_MONTH, monthLen);
}

Calendar* Calendar::normalizedCalendar() {
    if (areFieldsSet_ && areAllFieldsSet_ && isTimeSet_) return this;
    auto* gc = dynamic_cast<Calendar*>(clone());
    gc->setLenient(true);
    gc->complete();
    return gc;
}

// GregorianCalendar.add
void Calendar::add(int32_t field, int32_t amount) {
    if (amount == 0) return;
    if (field < 0 || field >= ZONE_OFFSET) throw IllegalArgumentException();
    complete();
    if (field == YEAR) {
        int32_t year = fields_[YEAR];
        if (internalGetEra() == CE) {
            year += amount;
            if (year > 0) {
                set(YEAR, year);
            } else {
                set(YEAR, 1 - year);
                set(ERA, BCE);
            }
        } else {
            year -= amount;
            if (year > 0) {
                set(YEAR, year);
            } else {
                set(YEAR, 1 - year);
                set(ERA, CE);
            }
        }
        pinDayOfMonth();
    } else if (field == MONTH) {
        int32_t month = fields_[MONTH] + amount;
        int32_t year = fields_[YEAR];
        int32_t y_amount = month >= 0 ? month / 12 : (month + 1) / 12 - 1;
        if (y_amount != 0) {
            if (internalGetEra() == CE) {
                year += y_amount;
                if (year > 0) {
                    set(YEAR, year);
                } else {
                    set(YEAR, 1 - year);
                    set(ERA, BCE);
                }
            } else {
                year -= y_amount;
                if (year > 0) {
                    set(YEAR, year);
                } else {
                    set(YEAR, 1 - year);
                    set(ERA, CE);
                }
            }
        }
        if (month >= 0) {
            set(MONTH, month % 12);
        } else {
            month %= 12;
            if (month < 0) month += 12;
            set(MONTH, JANUARY + month);
        }
        pinDayOfMonth();
    } else if (field == ERA) {
        int32_t era = fields_[ERA] + amount;
        if (era < 0) era = 0;
        if (era > 1) era = 1;
        set(ERA, era);
    } else {
        int64_t delta = amount;
        int64_t timeOfDay = 0;
        switch (field) {
            case HOUR:
            case HOUR_OF_DAY:
                delta *= 60 * 60 * 1000;
                break;
            case MINUTE:
                delta *= 60 * 1000;
                break;
            case SECOND:
                delta *= 1000;
                break;
            case MILLISECOND:
                break;
            case WEEK_OF_YEAR:
            case WEEK_OF_MONTH:
            case DAY_OF_WEEK_IN_MONTH:
                delta *= 7;
                break;
            case DAY_OF_MONTH:
            case DAY_OF_YEAR:
            case DAY_OF_WEEK:
                break;
            case AM_PM:
                delta = amount / 2;
                timeOfDay = 12 * (amount % 2);
                break;
        }
        if (field >= HOUR) {
            setTimeInMillis(time_ + delta);
            return;
        }
        int64_t fd = currentFixedDate();
        timeOfDay += fields_[HOUR_OF_DAY];
        timeOfDay *= 60;
        timeOfDay += fields_[MINUTE];
        timeOfDay *= 60;
        timeOfDay += fields_[SECOND];
        timeOfDay *= 1000;
        timeOfDay += fields_[MILLISECOND];
        if (timeOfDay >= ONE_DAY) {
            fd++;
            timeOfDay -= ONE_DAY;
        } else if (timeOfDay < 0) {
            fd--;
            timeOfDay += ONE_DAY;
        }
        fd += delta;
        int32_t zoneOffset = fields_[ZONE_OFFSET] + fields_[DST_OFFSET];
        setTimeInMillis((fd - EPOCH_OFFSET) * ONE_DAY + timeOfDay - zoneOffset);
        zoneOffset -= fields_[ZONE_OFFSET] + fields_[DST_OFFSET];
        if (zoneOffset != 0) {
            setTimeInMillis(time_ + zoneOffset);
            int64_t fd2 = currentFixedDate();
            if (fd2 != fd) setTimeInMillis(time_ - zoneOffset);
        }
    }
}

void Calendar::roll(int32_t field, bool up) { roll(field, up ? +1 : -1); }

// GregorianCalendar.roll (non-cutover years; the cutover year uses the same rules)
void Calendar::roll(int32_t field, int32_t amount) {
    if (amount == 0) return;
    if (field < 0 || field >= ZONE_OFFSET) throw IllegalArgumentException();
    complete();
    int32_t min = getMinimum(field);
    int32_t max = getMaximum(field);
    switch (field) {
        case AM_PM:
        case ERA:
        case YEAR:
        case MINUTE:
        case SECOND:
        case MILLISECOND:
            break;
        case HOUR:
        case HOUR_OF_DAY: {
            int32_t unit = max + 1;
            int32_t h = fields_[field];
            int32_t nh = (h + amount) % unit;
            if (nh < 0) nh += unit;
            time_ += ONE_HOUR * (nh - h);
            LocalFields d = localFields(time_, zone_);
            if (fields_[DAY_OF_MONTH] != d.day) {
                // keep the date; restore the wall clock hour
                int64_t fdOld = currentFixedDate();
                int64_t wall = (fdOld - EPOCH_OFFSET) * ONE_DAY + ONE_HOUR * d.hour + ONE_MINUTE * d.minute +
                               1000 * d.second + d.millis;
                if (field == HOUR) wall += 12 * ONE_HOUR;
                int32_t r, ds;
                zone_->offsetsAtWall(wall, &r, &ds);
                time_ = wall - r - ds;
                d = localFields(time_, zone_);
            }
            int32_t hourOfDay = d.hour;
            fields_[field] = hourOfDay % unit;
            if (field == HOUR) {
                fields_[HOUR_OF_DAY] = hourOfDay;
            } else {
                fields_[AM_PM] = hourOfDay / 12;
                fields_[HOUR] = hourOfDay % 12;
            }
            fields_[ZONE_OFFSET] = d.rawOffset;
            fields_[DST_OFFSET] = d.dstOffset;
            return;
        }
        case MONTH: {
            int32_t mon = (fields_[MONTH] + amount) % 12;
            if (mon < 0) mon += 12;
            set(MONTH, mon);
            int32_t y = internalGetEra() == BCE ? 1 - fields_[YEAR] : fields_[YEAR];
            int32_t monthLen = monthLength(mon, y);
            if (fields_[DAY_OF_MONTH] > monthLen) set(DAY_OF_MONTH, monthLen);
            return;
        }
        case WEEK_OF_YEAR: {
            int32_t y = internalGetEra() == BCE ? 1 - fields_[YEAR] : fields_[YEAR];
            max = getActualMaximum(WEEK_OF_YEAR);
            set(DAY_OF_WEEK, fields_[DAY_OF_WEEK]);
            int32_t woy = fields_[WEEK_OF_YEAR];
            int32_t value = woy + amount;
            int32_t weekYear = getWeekYear();
            if (weekYear == y) {
                if (value > min && value < max) {
                    set(WEEK_OF_YEAR, value);
                    return;
                }
                int64_t fd = currentFixedDate();
                int64_t day1 = fd - (7 * static_cast<int64_t>(woy - min));
                auto yearOf = [&](int64_t f) {
                    int32_t yy, mm, dd;
                    if (f >= cutoverFixedDate_) gregorianFromFixed(f, &yy, &mm, &dd);
                    else julianFromFixed(f, &yy, &mm, &dd);
                    return yy;
                };
                if (yearOf(day1) != y) min++;
                fd += 7 * static_cast<int64_t>(max - fields_[WEEK_OF_YEAR]);
                if (yearOf(fd) != y) max--;
            } else {
                if (weekYear > y) {
                    if (amount < 0) amount++;
                    woy = max;
                } else {
                    if (amount > 0) amount -= woy - max;
                    woy = min;
                }
            }
            set(field, rolledValue(woy, amount, min, max));
            return;
        }
        case WEEK_OF_MONTH: {
            int32_t dow = fields_[DAY_OF_WEEK] - firstDayOfWeek_;
            if (dow < 0) dow += 7;
            int64_t fd = currentFixedDate();
            int64_t month1 = fd - fields_[DAY_OF_MONTH] + 1;
            int32_t y = internalGetEra() == BCE ? 1 - fields_[YEAR] : fields_[YEAR];
            int32_t monthLen = monthLength(fields_[MONTH], y);
            int64_t monthDay1st = dayOfWeekDateOnOrBefore(month1 + 6, firstDayOfWeek_);
            if (static_cast<int32_t>(monthDay1st - month1) >= minimalDaysInFirstWeek_) monthDay1st -= 7;
            max = getActualMaximum(field);
            int32_t value = rolledValue(fields_[field], amount, 1, max) - 1;
            int64_t nfd = monthDay1st + value * 7 + dow;
            if (nfd < month1) nfd = month1;
            else if (nfd >= (month1 + monthLen)) nfd = month1 + monthLen - 1;
            set(DAY_OF_MONTH, static_cast<int32_t>(nfd - month1) + 1);
            return;
        }
        case DAY_OF_MONTH: {
            int32_t y = internalGetEra() == BCE ? 1 - fields_[YEAR] : fields_[YEAR];
            max = monthLength(fields_[MONTH], y);
            break;
        }
        case DAY_OF_YEAR:
            max = getActualMaximum(field);
            break;
        case DAY_OF_WEEK: {
            int32_t weekOfYear = fields_[WEEK_OF_YEAR];
            if (weekOfYear > 1 && weekOfYear < 52) {
                set(WEEK_OF_YEAR, weekOfYear);
                max = SATURDAY;
                break;
            }
            amount %= 7;
            if (amount == 0) return;
            int64_t fd = currentFixedDate();
            int64_t dowFirst = dayOfWeekDateOnOrBefore(fd, firstDayOfWeek_);
            fd += amount;
            if (fd < dowFirst) fd += 7;
            else if (fd >= dowFirst + 7) fd -= 7;
            int32_t yy, mm, dd;
            if (fd >= cutoverFixedDate_) gregorianFromFixed(fd, &yy, &mm, &dd);
            else julianFromFixed(fd, &yy, &mm, &dd);
            set(ERA, yy <= 0 ? BCE : CE);
            set(yy <= 0 ? 1 - yy : yy, mm - 1, dd);
            return;
        }
        case DAY_OF_WEEK_IN_MONTH: {
            min = 1;
            int32_t dom = fields_[DAY_OF_MONTH];
            int32_t y = internalGetEra() == BCE ? 1 - fields_[YEAR] : fields_[YEAR];
            int32_t monthLen = monthLength(fields_[MONTH], y);
            int32_t lastDays = monthLen % 7;
            max = monthLen / 7;
            int32_t x = (dom - 1) % 7;
            if (x < lastDays) max++;
            set(DAY_OF_WEEK, fields_[DAY_OF_WEEK]);
            break;
        }
    }
    set(field, rolledValue(fields_[field], amount, min, max));
}

int32_t Calendar::getMinimum(int32_t field) {
    checkField(field);
    return MIN_VALUES[field];
}
int32_t Calendar::getMaximum(int32_t field) {
    checkField(field);
    return MAX_VALUES[field];
}
int32_t Calendar::getGreatestMinimum(int32_t field) {
    checkField(field);
    return MIN_VALUES[field];
}
int32_t Calendar::getLeastMaximum(int32_t field) {
    checkField(field);
    return LEAST_MAX_VALUES[field];
}

int32_t Calendar::getActualMinimum(int32_t field) {
    checkField(field);
    if (field == DAY_OF_MONTH) {
        Calendar* gc = normalizedCalendar();
        int32_t y = gc->internalGetEra() == BCE ? 1 - gc->fields_[YEAR] : gc->fields_[YEAR];
        if (y == cutoverYear_ || y == cutoverYearJulian_) {
            int64_t fd = gc->currentFixedDate();
            int64_t month1 = fd - gc->fields_[DAY_OF_MONTH] + 1;
            (void)month1;
        }
    }
    return getMinimum(field);
}

int32_t Calendar::getActualMaximum(int32_t field) {
    checkField(field);
    const int32_t fieldsForFixedMax = ERA_MASK | DAY_OF_WEEK_MASK | HOUR_MASK | AM_PM_MASK | HOUR_OF_DAY_MASK |
                                      MINUTE_MASK | SECOND_MASK | MILLISECOND_MASK | ZONE_OFFSET_MASK | DST_OFFSET_MASK;
    if ((fieldsForFixedMax & (1 << field)) != 0) return getMaximum(field);
    Calendar* gc = normalizedCalendar();
    int32_t normYear = gc->internalGetEra() == BCE ? 1 - gc->fields_[YEAR] : gc->fields_[YEAR];
    bool greg = gc->calsysGregorian_;
    auto fixedOf = [&](int32_t y, int32_t m1, int32_t d) {
        return greg ? gregorianFixedDate(y, m1, d) : julianFixedDate(y, m1, d);
    };
    auto leap = [&](int32_t y) { return greg ? isGregorianLeapYear(y) : isJulianLeapYear(y); };
    int32_t value = getMaximum(field);
    switch (field) {
        case MONTH:
            value = DECEMBER;
            break;
        case DAY_OF_MONTH:
            value = greg ? gregorianMonthLength(normYear, gc->fields_[MONTH])
                         : julianMonthLength(normYear, gc->fields_[MONTH]);
            break;
        case DAY_OF_YEAR:
            value = leap(normYear) ? 366 : 365;
            break;
        case WEEK_OF_YEAR: {
            int64_t jan1 = fixedOf(normYear, 1, 1);
            int32_t dayOfWeek = dayOfWeekFromFixed(jan1);
            dayOfWeek -= firstDayOfWeek_;
            if (dayOfWeek < 0) dayOfWeek += 7;
            value = 52;
            int32_t magic = dayOfWeek + minimalDaysInFirstWeek_ - 1;
            if ((magic == 6) || (leap(normYear) && (magic == 5 || magic == 12))) value++;
            break;
        }
        case WEEK_OF_MONTH: {
            int32_t month = gc->fields_[MONTH];
            int64_t first = fixedOf(normYear, month + 1, 1);
            int32_t dayOfWeek = dayOfWeekFromFixed(first);
            int32_t monthLen = greg ? gregorianMonthLength(normYear, month) : julianMonthLength(normYear, month);
            dayOfWeek -= firstDayOfWeek_;
            if (dayOfWeek < 0) dayOfWeek += 7;
            int32_t nDaysFirstWeek = 7 - dayOfWeek;
            value = 3;
            if (nDaysFirstWeek >= minimalDaysInFirstWeek_) value++;
            monthLen -= nDaysFirstWeek + 7 * 3;
            if (monthLen > 0) {
                value++;
                if (monthLen > 7) value++;
            }
            break;
        }
        case DAY_OF_WEEK_IN_MONTH: {
            int32_t month = gc->fields_[MONTH];
            int32_t dow = gc->fields_[DAY_OF_WEEK];
            int32_t ndays = greg ? gregorianMonthLength(normYear, month) : julianMonthLength(normYear, month);
            int32_t dow1 = dayOfWeekFromFixed(fixedOf(normYear, month + 1, 1));
            int32_t x = dow - dow1;
            if (x < 0) x += 7;
            ndays -= x;
            value = (ndays + 6) / 7;
            break;
        }
        case YEAR: {
            // Java: the maximum year depends on the offset within the year of this calendar.
            value = (gc->internalGetEra() == CE) ? 292278994 : 292269055;
            break;
        }
        default:
            break;
    }
    return value;
}

int32_t Calendar::getWeekYear() {
    int32_t year = get(YEAR);
    if (internalGetEra() == BCE) year = 1 - year;
    int32_t weekOfYear = fields_[WEEK_OF_YEAR];
    if (fields_[MONTH] == JANUARY) {
        if (weekOfYear >= 52) --year;
    } else {
        if (weekOfYear == 1) ++year;
    }
    return year;
}

String Calendar::getDisplayName(int32_t field, int32_t style, Locale* locale) {
    (void)locale;
    int32_t v = get(field);
    bool shortStyle = style == SHORT;
    switch (field) {
        case ERA:
            return String(ERA_NAMES[v == BCE ? 0 : 1]);
        case MONTH:
            return String(shortStyle ? SHORT_MONTH_NAMES[v] : MONTH_NAMES[v]);
        case DAY_OF_WEEK:
            return String(shortStyle ? SHORT_WEEKDAY_NAMES[v] : WEEKDAY_NAMES[v]);
        case AM_PM:
            return String(AMPM_NAMES[v]);
        default:
            return String();
    }
}

void Calendar::setGregorianChange(Date* date) {
    if (date == nullptr) detail::throwNullPointerException();
    int64_t cutoverTime = date->getTime();
    if (cutoverTime == cutover_) return;
    complete();
    cutover_ = cutoverTime;
    cutoverFixedDate_ = floorDiv(cutoverTime, ONE_DAY) + EPOCH_OFFSET;
    if (cutoverTime == INT64_MAX) cutoverFixedDate_++;
    int32_t y, m, d;
    gregorianFromFixed(cutoverFixedDate_, &y, &m, &d);
    cutoverYear_ = y;
    julianFromFixed(cutoverFixedDate_ - 1, &y, &m, &d);
    cutoverYearJulian_ = y;
    if (time_ < cutover_) {
        areFieldsSet_ = false;
        areAllFieldsSet_ = false;
    }
}

Date* Calendar::getGregorianChange() { return new Date(cutover_); }

// =======================================================================================
// GregorianCalendar

GregorianCalendar::GregorianCalendar() : Calendar(detail::defaultTimeZoneRef(), Locale::getDefault()) {
    sharedZone_ = true;
    setTimeInMillis(System::currentTimeMillis());
}

GregorianCalendar::GregorianCalendar(TimeZone* zone) : Calendar(zone, Locale::getDefault()) {
    setTimeInMillis(System::currentTimeMillis());
}

GregorianCalendar::GregorianCalendar(Locale* locale) : Calendar(detail::defaultTimeZoneRef(), locale) {
    sharedZone_ = true;
    setTimeInMillis(System::currentTimeMillis());
}

GregorianCalendar::GregorianCalendar(TimeZone* zone, Locale* locale) : Calendar(zone, locale) {
    setTimeInMillis(System::currentTimeMillis());
}

GregorianCalendar::GregorianCalendar(int32_t year, int32_t month, int32_t dayOfMonth)
    : Calendar(detail::defaultTimeZoneRef(), Locale::getDefault()) {
    sharedZone_ = true;
    initFields(year, month, dayOfMonth, 0, 0, 0, 0);
}

GregorianCalendar::GregorianCalendar(int32_t year, int32_t month, int32_t dayOfMonth, int32_t hourOfDay,
                                     int32_t minute)
    : Calendar(detail::defaultTimeZoneRef(), Locale::getDefault()) {
    sharedZone_ = true;
    initFields(year, month, dayOfMonth, hourOfDay, minute, 0, 0);
}

GregorianCalendar::GregorianCalendar(int32_t year, int32_t month, int32_t dayOfMonth, int32_t hourOfDay,
                                     int32_t minute, int32_t second)
    : Calendar(detail::defaultTimeZoneRef(), Locale::getDefault()) {
    sharedZone_ = true;
    initFields(year, month, dayOfMonth, hourOfDay, minute, second, 0);
}

void GregorianCalendar::initFields(int32_t year, int32_t month, int32_t dayOfMonth, int32_t hourOfDay,
                                   int32_t minute, int32_t second, int32_t millis) {
    set(YEAR, year);
    set(MONTH, month);
    set(DAY_OF_MONTH, dayOfMonth);
    if (hourOfDay >= 12 && hourOfDay <= 23) {
        fields_[AM_PM] = PM;
        fields_[HOUR] = hourOfDay - 12;
    } else {
        fields_[HOUR] = hourOfDay;
    }
    setFieldsComputed(HOUR_MASK | AM_PM_MASK);
    set(HOUR_OF_DAY, hourOfDay);
    set(MINUTE, minute);
    set(SECOND, second);
    fields_[MILLISECOND] = millis;
}

Object* GregorianCalendar::clone() {
    auto* c = new GregorianCalendar(*this);
    if (!sharedZone_) c->zone_ = dynamic_cast<TimeZone*>(zone_->clone());
    return c;
}

// =======================================================================================
// Date

Date::Date() : fastTime_(System::currentTimeMillis()) {}
Date::Date(int64_t date) : fastTime_(date) {}

static int64_t localDateMillis(int32_t year, int32_t month, int32_t date, int32_t hrs, int32_t min, int32_t sec) {
    auto* c = new GregorianCalendar(detail::defaultTimeZoneRef());
    c->clear();
    c->set(year + 1900, month, date, hrs, min, sec);
    return c->getTimeInMillis();
}

Date::Date(int32_t year, int32_t month, int32_t date) : fastTime_(localDateMillis(year, month, date, 0, 0, 0)) {}
Date::Date(int32_t year, int32_t month, int32_t date, int32_t hrs, int32_t min)
    : fastTime_(localDateMillis(year, month, date, hrs, min, 0)) {}
Date::Date(int32_t year, int32_t month, int32_t date, int32_t hrs, int32_t min, int32_t sec)
    : fastTime_(localDateMillis(year, month, date, hrs, min, sec)) {}

int64_t Date::getTime() { return fastTime_; }
void Date::setTime(int64_t time) { fastTime_ = time; }

bool Date::before(Date* when) {
    if (when == nullptr) detail::throwNullPointerException();
    return getTime() < when->getTime();
}
bool Date::after(Date* when) {
    if (when == nullptr) detail::throwNullPointerException();
    return getTime() > when->getTime();
}
int32_t Date::compareTo(Date* anotherDate) {
    if (anotherDate == nullptr) detail::throwNullPointerException();
    int64_t a = getTime(), b = anotherDate->getTime();
    return a < b ? -1 : (a == b ? 0 : 1);
}
bool Date::equals(Object* obj) {
    auto* d = dynamic_cast<Date*>(obj);
    return d != nullptr && getTime() == d->getTime();
}
int32_t Date::hashCode() {
    int64_t ht = getTime();
    return static_cast<int32_t>(ht) ^ static_cast<int32_t>(ht >> 32);
}

// java.util.Date reports the year of era (BaseCalendar.Date.getYear()): 1 BC prints as 1.
static int32_t yearOfEra(const LocalFields& f) { return f.year <= 0 ? 1 - f.year : f.year; }

String Date::toString() {
    TimeZone* zone = detail::defaultTimeZoneRef();
    LocalFields f = localFields(fastTime_, zone);
    std::string s;
    s.reserve(32);
    s += SHORT_WEEKDAY_NAMES[f.dayOfWeek];
    s += ' ';
    s += SHORT_MONTH_NAMES[f.month];
    s += ' ';
    appendPadded(s, f.day, 2);
    s += ' ';
    appendPadded(s, f.hour, 2);
    s += ':';
    appendPadded(s, f.minute, 2);
    s += ':';
    appendPadded(s, f.second, 2);
    s += ' ';
    s += std::string(zone->shortNameAt(fastTime_));
    s += ' ';
    appendPadded(s, yearOfEra(f), 1);
    return String(std::move(s));
}

int32_t Date::localField(int32_t field) {
    LocalFields f = localFields(fastTime_, detail::defaultTimeZoneRef());
    switch (field) {
        case Calendar::YEAR:
            return yearOfEra(f) - 1900;
        case Calendar::MONTH:
            return f.month;
        case Calendar::DATE:
            return f.day;
        case Calendar::DAY_OF_WEEK:
            return f.dayOfWeek - 1;
        case Calendar::HOUR_OF_DAY:
            return f.hour;
        case Calendar::MINUTE:
            return f.minute;
        case Calendar::SECOND:
            return f.second;
        case Calendar::ZONE_OFFSET:
            return -(f.rawOffset + f.dstOffset) / 60000;
    }
    return 0;
}

void Date::setLocalField(int32_t field, int32_t value) {
    auto* c = new GregorianCalendar(detail::defaultTimeZoneRef());
    c->setTimeInMillis(fastTime_);
    int32_t y = c->get(Calendar::YEAR), mo = c->get(Calendar::MONTH), d = c->get(Calendar::DATE);
    int32_t h = c->get(Calendar::HOUR_OF_DAY), mi = c->get(Calendar::MINUTE), s = c->get(Calendar::SECOND);
    int32_t ms = c->get(Calendar::MILLISECOND);
    if (c->get(Calendar::ERA) == 0) y = 1 - y;
    switch (field) {
        case Calendar::YEAR: y = value; break;
        case Calendar::MONTH: mo = value; break;
        case Calendar::DATE: d = value; break;
        case Calendar::HOUR_OF_DAY: h = value; break;
        case Calendar::MINUTE: mi = value; break;
        case Calendar::SECOND: s = value; break;
    }
    c->clear();
    if (y <= 0) {
        c->set(Calendar::ERA, 0);
        y = 1 - y;
    }
    c->set(y, mo, d, h, mi, s);
    c->set(Calendar::MILLISECOND, ms);
    setTime(c->getTimeInMillis());
}

int32_t Date::getYear() { return localField(Calendar::YEAR); }
int32_t Date::getMonth() { return localField(Calendar::MONTH); }
int32_t Date::getDate() { return localField(Calendar::DATE); }
int32_t Date::getDay() { return localField(Calendar::DAY_OF_WEEK); }
int32_t Date::getHours() { return localField(Calendar::HOUR_OF_DAY); }
int32_t Date::getMinutes() { return localField(Calendar::MINUTE); }
int32_t Date::getSeconds() { return localField(Calendar::SECOND); }
int32_t Date::getTimezoneOffset() { return localField(Calendar::ZONE_OFFSET); }
void Date::setYear(int32_t year) { setLocalField(Calendar::YEAR, year + 1900); }
void Date::setMonth(int32_t month) { setLocalField(Calendar::MONTH, month); }
void Date::setDate(int32_t date) { setLocalField(Calendar::DATE, date); }
void Date::setHours(int32_t hours) { setLocalField(Calendar::HOUR_OF_DAY, hours); }
void Date::setMinutes(int32_t minutes) { setLocalField(Calendar::MINUTE, minutes); }
void Date::setSeconds(int32_t seconds) { setLocalField(Calendar::SECOND, seconds); }

String Date::toGMTString() {
    TimeZone* gmt = TimeZone::getTimeZone(String("GMT"));
    LocalFields f = localFields(getTime(), gmt);
    std::string s;
    appendPadded(s, f.day, 1);
    s += ' ';
    s += SHORT_MONTH_NAMES[f.month];
    s += ' ';
    appendPadded(s, yearOfEra(f), 1);
    s += ' ';
    appendPadded(s, f.hour, 2);
    s += ':';
    appendPadded(s, f.minute, 2);
    s += ':';
    appendPadded(s, f.second, 2);
    s += " GMT";
    return String(std::move(s));
}

String Date::toLocaleString() { return (new SimpleDateFormat(String("MMM d, yyyy h:mm:ss a")))->format(this); }

// =======================================================================================
// Timestamp

Timestamp::Timestamp(int64_t time) : Date((time / 1000) * 1000) {
    nanos_ = static_cast<int32_t>((time % 1000) * 1000000);
    if (nanos_ < 0) {
        nanos_ = 1000000000 + nanos_;
        Date::setTime(((time / 1000) - 1) * 1000);
    }
}

Timestamp::Timestamp(int32_t year, int32_t month, int32_t date, int32_t hour, int32_t minute, int32_t second,
                     int32_t nano)
    : Date(year, month, date, hour, minute, second) {
    if (nano > 999999999 || nano < 0) throw IllegalArgumentException(String("nanos > 999999999 or < 0"));
    nanos_ = nano;
}

void Timestamp::setTime(int64_t time) {
    Date::setTime((time / 1000) * 1000);
    nanos_ = static_cast<int32_t>((time % 1000) * 1000000);
    if (nanos_ < 0) {
        nanos_ = 1000000000 + nanos_;
        Date::setTime(((time / 1000) - 1) * 1000);
    }
}

int64_t Timestamp::getTime() { return Date::getTime() + (nanos_ / 1000000); }

void Timestamp::setNanos(int32_t n) {
    if (n > 999999999 || n < 0) throw IllegalArgumentException(String("nanos > 999999999 or < 0"));
    nanos_ = n;
}

bool Timestamp::equals(Timestamp* ts) {
    if (ts == nullptr) return false;
    return getTime() == ts->getTime() && nanos_ == ts->nanos_;
}

bool Timestamp::equals(Object* ts) {
    auto* t = dynamic_cast<Timestamp*>(ts);
    return t != nullptr && equals(t);
}

bool Timestamp::before(Timestamp* ts) { return compareTo(ts) < 0; }
bool Timestamp::after(Timestamp* ts) { return compareTo(ts) > 0; }

int32_t Timestamp::compareTo(Timestamp* ts) {
    if (ts == nullptr) detail::throwNullPointerException();
    int64_t thisTime = getTime();
    int64_t anotherTime = ts->getTime();
    int32_t i = (thisTime < anotherTime ? -1 : (thisTime == anotherTime ? 0 : 1));
    if (i == 0) {
        if (nanos_ > ts->nanos_) return 1;
        if (nanos_ < ts->nanos_) return -1;
    }
    return i;
}

int32_t Timestamp::compareTo(Date* o) {
    if (auto* ts = dynamic_cast<Timestamp*>(o)) return compareTo(ts);
    if (o == nullptr) detail::throwNullPointerException();
    Timestamp other(o->getTime());
    return compareTo(&other);
}

String Timestamp::toString() {
    LocalFields f = localFields(Date::getTime(), detail::defaultTimeZoneRef());
    std::string s;
    appendPadded(s, yearOfEra(f), 4);
    s += '-';
    appendPadded(s, f.month + 1, 2);
    s += '-';
    appendPadded(s, f.day, 2);
    s += ' ';
    appendPadded(s, f.hour, 2);
    s += ':';
    appendPadded(s, f.minute, 2);
    s += ':';
    appendPadded(s, f.second, 2);
    s += '.';
    if (nanos_ == 0) {
        s += '0';
    } else {
        std::string n;
        appendPadded(n, nanos_, 9);
        size_t e = n.size();
        while (e > 1 && n[e - 1] == '0') e--;
        s.append(n, 0, e);
    }
    return String(std::move(s));
}

Timestamp* Timestamp::valueOf(const String& str0) {
    static const char* const formatError = "Timestamp format must be yyyy-mm-dd hh:mm:ss[.fffffffff]";
    if (str0 == nullptr) throw IllegalArgumentException(String("null string"));
    String s = str0.trim();
    int32_t dividingSpace = s.indexOf(u' ');
    if (dividingSpace <= 0) throw IllegalArgumentException(String(formatError));
    String date_s = s.substring(0, dividingSpace);
    String time_s = s.substring(dividingSpace + 1);
    int32_t firstDash = date_s.indexOf(u'-');
    int32_t secondDash = date_s.indexOf(u'-', firstDash + 1);
    int32_t firstColon = time_s.indexOf(u':');
    int32_t secondColon = time_s.indexOf(u':', firstColon + 1);
    int32_t period = time_s.indexOf(u'.', secondColon + 1);
    int32_t year = 0, month = 0, day = 0, hour, minute, second, a_nanos = 0;
    bool parsedDate = false;
    if (firstDash > 0 && secondDash > 0 && secondDash < date_s.length() - 1) {
        String yyyy = date_s.substring(0, firstDash);
        String mm = date_s.substring(firstDash + 1, secondDash);
        String dd = date_s.substring(secondDash + 1);
        if (yyyy.length() == 4 && (mm.length() >= 1 && mm.length() <= 2) && (dd.length() >= 1 && dd.length() <= 2)) {
            year = Integer::parseInt(yyyy);
            month = Integer::parseInt(mm);
            day = Integer::parseInt(dd);
            if ((month >= 1 && month <= 12) && (day >= 1 && day <= 31)) parsedDate = true;
        }
    }
    if (!parsedDate) throw IllegalArgumentException(String(formatError));
    if ((firstColon > 0) && (secondColon > 0) && (secondColon < time_s.length() - 1)) {
        hour = Integer::parseInt(time_s.substring(0, firstColon));
        minute = Integer::parseInt(time_s.substring(firstColon + 1, secondColon));
        if ((period > 0) && (period < time_s.length() - 1)) {
            second = Integer::parseInt(time_s.substring(secondColon + 1, period));
            String nanos_s = time_s.substring(period + 1);
            if (nanos_s.length() > 9) throw IllegalArgumentException(String(formatError));
            if (!(nanos_s[0] >= '0' && nanos_s[0] <= '9')) throw IllegalArgumentException(String(formatError));
            std::string padded(nanos_s);
            padded.append(9 - padded.size(), '0');
            a_nanos = Integer::parseInt(String(padded));
        } else if (period > 0) {
            throw IllegalArgumentException(String(formatError));
        } else {
            second = Integer::parseInt(time_s.substring(secondColon + 1));
        }
    } else {
        throw IllegalArgumentException(String(formatError));
    }
    return new Timestamp(year - 1900, month - 1, day, hour, minute, second, a_nanos);
}

}  // namespace jlang
