// jlang/Time.h - java.util.Date, java.sql.Timestamp (and java.sql.Date), java.util.TimeZone,
// java.util.Locale, java.util.Calendar / GregorianCalendar, java.text.SimpleDateFormat
// (DateFormat) and java.text.DecimalFormat (NumberFormat), java.math.RoundingMode.
//
// All classes are GC objects held by pointer (CONVENTIONS §2), with Java's method names and
// semantics:
//
//   jlang::Calendar* c = jlang::Calendar::getInstance();
//   c->set(jlang::Calendar::HOUR_OF_DAY, 5);
//   int64_t t = c->getTimeInMillis();
//   auto* ts = new jlang::Timestamp(t);           // ts->toString() == "2011-06-21 05:00:00.0"
//   jlang::String s = (new jlang::SimpleDateFormat("yyyy-MM-dd HH:mm:ss"))->format(new jlang::Date());
//
// Time zones come from the system tz database (TZif files under /usr/share/zoneinfo, or
// $TZDIR). The default zone is taken from $TZ, /etc/timezone or the /etc/localtime link, like
// the JVM does, and can be changed with TimeZone::setDefault. Offsets before 1900 use the
// zone's current standard offset (as Java's ZoneInfo does). Calendar implements Java's
// GregorianCalendar exactly (lenient field normalization, field resolution by most recently
// set fields, Julian calendar before 1582-10-15, week rules of the default locale: US,
// firstDayOfWeek = SUNDAY, minimalDaysInFirstWeek = 1).
//
// Text uses English (Locale.US) symbols for every locale.
#pragma once

#include <jlang/jlang.h>

#include <cstdint>
#include <vector>

namespace jlang {

class TimeZone;
class Locale;
class Calendar;
class Date;

namespace detail {
struct ZoneData;  // compiled zone rules (time_zone.cpp)
}

// ---------------------------------------------------------------------------------------
// java.math.RoundingMode (value-class enum, CONVENTIONS §7)
class RoundingMode final {
public:
    enum class Value : int32_t { _NULL = -1, UP, DOWN, CEILING, FLOOR, HALF_UP, HALF_DOWN, HALF_EVEN, UNNECESSARY };
    static const RoundingMode UP, DOWN, CEILING, FLOOR, HALF_UP, HALF_DOWN, HALF_EVEN, UNNECESSARY;
    constexpr RoundingMode() noexcept : v_(Value::_NULL) {}
    constexpr RoundingMode(std::nullptr_t) noexcept : v_(Value::_NULL) {}
    constexpr explicit RoundingMode(Value v) noexcept : v_(v) {}
    constexpr operator Value() const noexcept { return v_; }
    constexpr bool operator==(const RoundingMode& o) const noexcept { return v_ == o.v_; }
    constexpr bool operator==(std::nullptr_t) const noexcept { return v_ == Value::_NULL; }
    constexpr int32_t ordinal() const noexcept { return static_cast<int32_t>(v_); }
    String name() const;
    String toString() const { return name(); }
    int32_t compareTo(RoundingMode o) const noexcept { return ordinal() - o.ordinal(); }
    bool equals(RoundingMode o) const noexcept { return v_ == o.v_; }
    int32_t hashCode() const noexcept { return ordinal(); }
    static Array<RoundingMode>* values();
    static RoundingMode valueOf(const String& name);

private:
    Value v_;
};
inline constexpr RoundingMode RoundingMode::UP{RoundingMode::Value::UP};
inline constexpr RoundingMode RoundingMode::DOWN{RoundingMode::Value::DOWN};
inline constexpr RoundingMode RoundingMode::CEILING{RoundingMode::Value::CEILING};
inline constexpr RoundingMode RoundingMode::FLOOR{RoundingMode::Value::FLOOR};
inline constexpr RoundingMode RoundingMode::HALF_UP{RoundingMode::Value::HALF_UP};
inline constexpr RoundingMode RoundingMode::HALF_DOWN{RoundingMode::Value::HALF_DOWN};
inline constexpr RoundingMode RoundingMode::HALF_EVEN{RoundingMode::Value::HALF_EVEN};
inline constexpr RoundingMode RoundingMode::UNNECESSARY{RoundingMode::Value::UNNECESSARY};

// ---------------------------------------------------------------------------------------
// java.util.Locale (minimal: identity, names; all text formatting uses English symbols)
class Locale : public virtual Object {
public:
    explicit Locale(const String& language);
    Locale(const String& language, const String& country);
    Locale(const String& language, const String& country, const String& variant);

    static Locale* const ENGLISH;
    static Locale* const US;
    static Locale* const UK;
    static Locale* const GERMAN;
    static Locale* const GERMANY;
    static Locale* const FRENCH;
    static Locale* const FRANCE;
    static Locale* const ITALIAN;
    static Locale* const ITALY;
    static Locale* const JAPANESE;
    static Locale* const JAPAN;
    static Locale* const KOREAN;
    static Locale* const KOREA;
    static Locale* const CHINESE;
    static Locale* const CHINA;
    static Locale* const ROOT;

    // The JVM default (Locale.US unless set; the JVM default for en_US systems).
    static Locale* getDefault();
    static void setDefault(Locale* l);
    static Array<Locale*>* getAvailableLocales();

    String getLanguage() { return language_; }
    String getCountry() { return country_; }
    String getVariant() { return variant_; }
    String getDisplayLanguage();
    String getDisplayCountry();
    String getDisplayName();
    // "en_US", "en", "_US", "" (Java Locale.toString)
    String toString() override;
    bool equals(Object* o) override;
    int32_t hashCode() override;
    Object* clone() override { return new Locale(*this); }

private:
    String language_, country_, variant_;
};

// Inline (partially ordered) definitions: initialized before the static variables of every
// translation unit that includes this header, so static initializers may use them.
inline Locale* const Locale::ENGLISH = new Locale(String("en"), String(""));
inline Locale* const Locale::US = new Locale(String("en"), String("US"));
inline Locale* const Locale::UK = new Locale(String("en"), String("GB"));
inline Locale* const Locale::GERMAN = new Locale(String("de"), String(""));
inline Locale* const Locale::GERMANY = new Locale(String("de"), String("DE"));
inline Locale* const Locale::FRENCH = new Locale(String("fr"), String(""));
inline Locale* const Locale::FRANCE = new Locale(String("fr"), String("FR"));
inline Locale* const Locale::ITALIAN = new Locale(String("it"), String(""));
inline Locale* const Locale::ITALY = new Locale(String("it"), String("IT"));
inline Locale* const Locale::JAPANESE = new Locale(String("ja"), String(""));
inline Locale* const Locale::JAPAN = new Locale(String("ja"), String("JP"));
inline Locale* const Locale::KOREAN = new Locale(String("ko"), String(""));
inline Locale* const Locale::KOREA = new Locale(String("ko"), String("KR"));
inline Locale* const Locale::CHINESE = new Locale(String("zh"), String(""));
inline Locale* const Locale::CHINA = new Locale(String("zh"), String("CN"));
inline Locale* const Locale::ROOT = new Locale(String(""), String(""));

// ---------------------------------------------------------------------------------------
// java.util.TimeZone (tz database rules; see the header comment)
class TimeZone : public virtual Object {
public:
    static constexpr int32_t SHORT = 0;
    static constexpr int32_t LONG = 1;

    // TimeZone.getDefault(): a clone of the default zone (like Java).
    static TimeZone* getDefault();
    // null resets to the system default.
    static void setDefault(TimeZone* zone);
    // "GMT", "UTC", custom "GMT+hh[:mm]" ids, or tz database ids ("Europe/Berlin"). Unknown
    // ids return a GMT zone (like Java).
    static TimeZone* getTimeZone(const String& id);
    static Array<String>* getAvailableIDs();

    virtual String getID() { return id_; }
    virtual void setID(const String& id);
    // Total offset (raw + DST) from UTC at the given UTC time, in milliseconds.
    virtual int32_t getOffset(int64_t date);
    // Java's legacy getOffset(era, year, month, day, dayOfWeek, milliseconds in day).
    virtual int32_t getOffset(int32_t era, int32_t year, int32_t month, int32_t day, int32_t dayOfWeek,
                              int32_t milliseconds);
    // Current standard offset (Java: the last rule's raw offset).
    virtual int32_t getRawOffset();
    virtual void setRawOffset(int32_t offsetMillis);
    virtual int32_t getDSTSavings();
    virtual bool useDaylightTime();
    virtual bool observesDaylightTime() { return useDaylightTime(); }
    virtual bool inDaylightTime(Date* date);
    virtual bool hasSameRules(TimeZone* other);
    // Display names in English ("CET"/"CEST", "Central European Time"...).
    String getDisplayName();
    String getDisplayName(Locale* locale);
    String getDisplayName(bool daylight, int32_t style);
    String getDisplayName(bool daylight, int32_t style, Locale* locale);

    bool equals(Object* o) override;
    int32_t hashCode() override;
    String toString() override;
    Object* clone() override;

    // ---- jlang extensions (used by Calendar/SimpleDateFormat)
    // Offsets at the UTC instant: *raw = standard offset, *dst = daylight saving amount (ms).
    void offsetsAtUtc(int64_t utcMillis, int32_t* raw, int32_t* dst);
    // Offsets for a local wall-clock time, resolving gaps/overlaps like Java (standard time
    // is assumed in both cases).
    void offsetsAtWall(int64_t wallMillis, int32_t* raw, int32_t* dst);
    // Short name at an instant ("CEST" in summer), as used by Date.toString and "z".
    String shortNameAt(int64_t utcMillis);
    String longNameAt(int64_t utcMillis);

    explicit TimeZone(detail::ZoneData* data);

private:
    detail::ZoneData* data_;
    String id_;
};

// ---------------------------------------------------------------------------------------
// java.util.Date (also used for java.sql.Date)
class Date : public virtual Object, public virtual Comparable<Date*> {
public:
    Date();                        // now
    explicit Date(int64_t date);   // milliseconds since the epoch
    // Deprecated Java constructors (local time; year - 1900, month 0-11).
    Date(int32_t year, int32_t month, int32_t date);
    Date(int32_t year, int32_t month, int32_t date, int32_t hrs, int32_t min);
    Date(int32_t year, int32_t month, int32_t date, int32_t hrs, int32_t min, int32_t sec);

    virtual int64_t getTime();
    virtual void setTime(int64_t time);
    virtual bool before(Date* when);
    virtual bool after(Date* when);
    int32_t compareTo(Date* anotherDate) override;
    bool equals(Object* obj) override;
    int32_t hashCode() override;
    // "EEE MMM dd HH:mm:ss zzz yyyy" in the default zone: "Tue Jun 21 10:00:00 CEST 2011"
    String toString() override;
    Object* clone() override { return new Date(*this); }

    // Deprecated local-time accessors (default zone).
    int32_t getYear();   // year - 1900
    int32_t getMonth();  // 0-11
    int32_t getDate();   // 1-31
    int32_t getDay();    // 0 = Sunday
    int32_t getHours();
    int32_t getMinutes();
    int32_t getSeconds();
    int32_t getTimezoneOffset();  // minutes, UTC - local
    void setYear(int32_t year);
    void setMonth(int32_t month);
    void setDate(int32_t date);
    void setHours(int32_t hours);
    void setMinutes(int32_t minutes);
    void setSeconds(int32_t seconds);
    String toGMTString();     // "21 Jun 2011 10:00:00 GMT"
    String toLocaleString();  // "Jun 21, 2011 10:00:00 AM"

protected:
    int64_t fastTime_ = 0;

private:
    int32_t localField(int32_t field);
    void setLocalField(int32_t field, int32_t value);
};

// ---------------------------------------------------------------------------------------
// java.sql.Timestamp: a Date with nanosecond precision. getTime() includes the milliseconds
// of the nanos; toString() is "yyyy-mm-dd hh:mm:ss.fffffffff" (trailing zeros dropped).
class Timestamp : public Date {
public:
    explicit Timestamp(int64_t time);
    // Deprecated Java constructor (year - 1900, month 0-11).
    Timestamp(int32_t year, int32_t month, int32_t date, int32_t hour, int32_t minute, int32_t second,
              int32_t nano);

    // "yyyy-[m]m-[d]d hh:mm:ss[.f...]" in the default zone; IllegalArgumentException otherwise.
    static Timestamp* valueOf(const String& s);

    int64_t getTime() override;
    void setTime(int64_t time) override;
    int32_t getNanos() { return nanos_; }
    void setNanos(int32_t n);  // IllegalArgumentException if not in [0, 999999999]
    bool equals(Timestamp* ts);
    bool equals(Object* ts) override;
    int32_t hashCode() override { return Date::hashCode(); }
    bool before(Timestamp* ts);
    bool after(Timestamp* ts);
    using Date::after;
    using Date::before;
    int32_t compareTo(Timestamp* ts);
    int32_t compareTo(Date* o) override;
    String toString() override;
    Object* clone() override { return new Timestamp(*this); }

private:
    int32_t nanos_ = 0;
};

// ---------------------------------------------------------------------------------------
// java.util.Calendar (implemented as Java's GregorianCalendar; getInstance() returns a
// GregorianCalendar in the default zone).
class Calendar : public virtual Object, public virtual Comparable<Calendar*> {
public:
    static constexpr int32_t ERA = 0;
    static constexpr int32_t YEAR = 1;
    static constexpr int32_t MONTH = 2;
    static constexpr int32_t WEEK_OF_YEAR = 3;
    static constexpr int32_t WEEK_OF_MONTH = 4;
    static constexpr int32_t DATE = 5;
    static constexpr int32_t DAY_OF_MONTH = 5;
    static constexpr int32_t DAY_OF_YEAR = 6;
    static constexpr int32_t DAY_OF_WEEK = 7;
    static constexpr int32_t DAY_OF_WEEK_IN_MONTH = 8;
    static constexpr int32_t AM_PM = 9;
    static constexpr int32_t HOUR = 10;
    static constexpr int32_t HOUR_OF_DAY = 11;
    static constexpr int32_t MINUTE = 12;
    static constexpr int32_t SECOND = 13;
    static constexpr int32_t MILLISECOND = 14;
    static constexpr int32_t ZONE_OFFSET = 15;
    static constexpr int32_t DST_OFFSET = 16;
    static constexpr int32_t FIELD_COUNT = 17;

    static constexpr int32_t SUNDAY = 1;
    static constexpr int32_t MONDAY = 2;
    static constexpr int32_t TUESDAY = 3;
    static constexpr int32_t WEDNESDAY = 4;
    static constexpr int32_t THURSDAY = 5;
    static constexpr int32_t FRIDAY = 6;
    static constexpr int32_t SATURDAY = 7;

    static constexpr int32_t JANUARY = 0;
    static constexpr int32_t FEBRUARY = 1;
    static constexpr int32_t MARCH = 2;
    static constexpr int32_t APRIL = 3;
    static constexpr int32_t MAY = 4;
    static constexpr int32_t JUNE = 5;
    static constexpr int32_t JULY = 6;
    static constexpr int32_t AUGUST = 7;
    static constexpr int32_t SEPTEMBER = 8;
    static constexpr int32_t OCTOBER = 9;
    static constexpr int32_t NOVEMBER = 10;
    static constexpr int32_t DECEMBER = 11;
    static constexpr int32_t UNDECIMBER = 12;

    static constexpr int32_t AM = 0;
    static constexpr int32_t PM = 1;

    static constexpr int32_t ALL_STYLES = 0;
    static constexpr int32_t SHORT = 1;
    static constexpr int32_t LONG = 2;

    static Calendar* getInstance();
    static Calendar* getInstance(TimeZone* zone);
    static Calendar* getInstance(Locale* locale);
    static Calendar* getInstance(TimeZone* zone, Locale* locale);

    // ---- fields
    virtual int32_t get(int32_t field);
    virtual void set(int32_t field, int32_t value);
    void set(int32_t year, int32_t month, int32_t date);
    void set(int32_t year, int32_t month, int32_t date, int32_t hourOfDay, int32_t minute);
    void set(int32_t year, int32_t month, int32_t date, int32_t hourOfDay, int32_t minute, int32_t second);
    virtual void add(int32_t field, int32_t amount);
    virtual void roll(int32_t field, bool up);
    virtual void roll(int32_t field, int32_t amount);
    void clear();
    void clear(int32_t field);
    bool isSet(int32_t field);

    // ---- time
    Date* getTime();
    void setTime(Date* date);
    int64_t getTimeInMillis();
    void setTimeInMillis(int64_t millis);

    // ---- comparisons (Java: before/after take Object; false unless it is a Calendar)
    bool before(Object* when);
    bool after(Object* when);
    int32_t compareTo(Calendar* anotherCalendar) override;
    bool equals(Object* obj) override;
    int32_t hashCode() override;
    String toString() override;
    Object* clone() override;

    // ---- limits
    virtual int32_t getMinimum(int32_t field);
    virtual int32_t getMaximum(int32_t field);
    virtual int32_t getGreatestMinimum(int32_t field);
    virtual int32_t getLeastMaximum(int32_t field);
    virtual int32_t getActualMinimum(int32_t field);
    virtual int32_t getActualMaximum(int32_t field);

    // ---- settings
    void setLenient(bool lenient) { lenient_ = lenient; }
    bool isLenient() { return lenient_; }
    void setFirstDayOfWeek(int32_t value);
    int32_t getFirstDayOfWeek() { return firstDayOfWeek_; }
    void setMinimalDaysInFirstWeek(int32_t value);
    int32_t getMinimalDaysInFirstWeek() { return minimalDaysInFirstWeek_; }
    TimeZone* getTimeZone();
    void setTimeZone(TimeZone* value);
    int32_t getWeekYear();
    bool isWeekDateSupported() { return true; }
    // Month / weekday / era / AM_PM names in English (style SHORT or LONG).
    String getDisplayName(int32_t field, int32_t style, Locale* locale);

    // jlang: the gregorian cutover (Java default -12219292800000, 1582-10-15).
    void setGregorianChange(Date* date);
    Date* getGregorianChange();
    bool isLeapYear(int32_t year);

protected:
    Calendar();
    Calendar(TimeZone* zone, Locale* locale);

    // Java's internal state (see java.util.Calendar / GregorianCalendar)
    int32_t fields_[FIELD_COUNT] = {};
    int32_t stamp_[FIELD_COUNT] = {};
    int64_t time_ = 0;
    bool isTimeSet_ = false;
    bool areFieldsSet_ = false;
    bool areAllFieldsSet_ = false;
    bool lenient_ = true;
    bool sharedZone_ = false;
    bool calsysKnown_ = false;
    bool calsysGregorian_ = true;
    TimeZone* zone_ = nullptr;
    int32_t firstDayOfWeek_ = SUNDAY;
    int32_t minimalDaysInFirstWeek_ = 1;
    int32_t nextStamp_ = 2;
    int64_t cutover_ = -12219292800000LL;  // gregorianCutover (UTC ms): 1582-10-15
    int64_t cutoverFixedDate_ = 577736;    // its fixed date
    int32_t cutoverYear_ = 1582;
    int32_t cutoverYearJulian_ = 1582;

public:  // implementation helpers (time_calendar.cpp); not part of the Java API
    void complete();
    void computeFieldsFromTime();
    void computeTimeFromFields();
    int32_t computeFieldsMask(int32_t fieldMask, int32_t tzMask);
    int32_t internalGet(int32_t f) { return fields_[f]; }
    void internalSet(int32_t f, int32_t v) { fields_[f] = v; }
    int32_t selectFields();
    int64_t getFixedDateFor(bool gregorian, int32_t year, int32_t fieldMask);
    int64_t currentFixedDate();
    int32_t monthLength(int32_t month, int32_t year);
    int32_t weekNumber(int64_t fixedDay1, int64_t fixedDate);
    void pinDayOfMonth();
    int32_t internalGetEra();
    bool isCutoverYear(int32_t normalizedYear);
    void setFieldsComputed(int32_t mask);
    void setFieldsNormalized(int32_t mask);
    int32_t getSetStateFields();
    void adjustStamp();
    void invalidateWeekFields();
    Calendar* normalizedCalendar();
    static int64_t millisOf(Calendar* c);
};

// java.util.GregorianCalendar
class GregorianCalendar : public Calendar {
public:
    static constexpr int32_t BC = 0;
    static constexpr int32_t AD = 1;

    GregorianCalendar();
    explicit GregorianCalendar(TimeZone* zone);
    explicit GregorianCalendar(Locale* locale);
    GregorianCalendar(TimeZone* zone, Locale* locale);
    GregorianCalendar(int32_t year, int32_t month, int32_t dayOfMonth);
    GregorianCalendar(int32_t year, int32_t month, int32_t dayOfMonth, int32_t hourOfDay, int32_t minute);
    GregorianCalendar(int32_t year, int32_t month, int32_t dayOfMonth, int32_t hourOfDay, int32_t minute,
                      int32_t second);

    Object* clone() override;

private:
    void initFields(int32_t year, int32_t month, int32_t dayOfMonth, int32_t hourOfDay, int32_t minute,
                    int32_t second, int32_t millis);
};

// ---------------------------------------------------------------------------------------
// java.text.SimpleDateFormat (also java.text.DateFormat). Pattern letters: G y Y M L w W D d
// F E u a H k K h m s S z Z X, quoting with '...' and ''. Throws IllegalArgumentException for
// illegal pattern characters, like Java. parse() throws ParseException("Unparseable date: ...").
class SimpleDateFormat : public virtual Object {
public:
    // DateFormat style constants
    static constexpr int32_t FULL = 0;
    static constexpr int32_t LONG = 1;
    static constexpr int32_t MEDIUM = 2;
    static constexpr int32_t SHORT = 3;
    static constexpr int32_t DEFAULT = MEDIUM;

    SimpleDateFormat();  // "M/d/yy h:mm a"
    explicit SimpleDateFormat(const String& pattern);
    SimpleDateFormat(const String& pattern, Locale* locale);

    // DateFormat factories (US patterns)
    static SimpleDateFormat* getInstance();
    static SimpleDateFormat* getDateInstance();
    static SimpleDateFormat* getDateInstance(int32_t style);
    static SimpleDateFormat* getDateInstance(int32_t style, Locale* locale);
    static SimpleDateFormat* getTimeInstance();
    static SimpleDateFormat* getTimeInstance(int32_t style);
    static SimpleDateFormat* getTimeInstance(int32_t style, Locale* locale);
    static SimpleDateFormat* getDateTimeInstance();
    static SimpleDateFormat* getDateTimeInstance(int32_t dateStyle, int32_t timeStyle);
    static SimpleDateFormat* getDateTimeInstance(int32_t dateStyle, int32_t timeStyle, Locale* locale);

    String format(Date* date);
    String format(int64_t millis);  // Format.format(Number) semantics
    String format(Object* obj);     // Date or Number; IllegalArgumentException otherwise
    Date* parse(const String& source);
    // Parses from *pos; on success advances *pos; on failure returns nullptr and sets
    // *errorIndex (when not null) like ParsePosition.
    Date* parse(const String& source, int32_t* pos, int32_t* errorIndex = nullptr);
    Object* parseObject(const String& source) { return parse(source); }

    void applyPattern(const String& pattern);
    String toPattern() { return pattern_; }
    void setLenient(bool lenient) { calendar_->setLenient(lenient); }
    bool isLenient() { return calendar_->isLenient(); }
    void setTimeZone(TimeZone* zone) { calendar_->setTimeZone(zone); }
    TimeZone* getTimeZone() { return calendar_->getTimeZone(); }
    Calendar* getCalendar() { return calendar_; }
    void setCalendar(Calendar* c) { calendar_ = c; }
    void set2DigitYearStart(Date* startDate);
    Date* get2DigitYearStart();

    bool equals(Object* obj) override;
    int32_t hashCode() override;
    Object* clone() override;

private:
    struct Token {
        char32_t letter;  // 0 = literal text
        int32_t count;
        String text;
    };
    void compile();
    void subFormat(String& out, const Token& t, Calendar* cal);

    String pattern_;
    [[maybe_unused]] Locale* locale_ = nullptr;
    Calendar* calendar_ = nullptr;
    std::vector<Token> tokens_;
    int64_t defaultCenturyStart_ = 0;
    int32_t defaultCenturyStartYear_ = 0;
};
using DateFormat = SimpleDateFormat;

// ---------------------------------------------------------------------------------------
// java.text.DecimalFormat (also java.text.NumberFormat). Patterns: prefix/suffix with quotes,
// ';' negative subpattern, '#', '0', ',', '.', 'E' exponent, '%' (x100), '‰' (x1000),
// '¤' ("$" / "USD"). Formatting of doubles is exact (Java 8+: the shortest digits of the
// double, rounded with the exact binary value), HALF_EVEN by default. English symbols.
class DecimalFormat : public virtual Object {
public:
    DecimalFormat();  // "#,##0.###"
    explicit DecimalFormat(const String& pattern);
    DecimalFormat(const String& pattern, Locale* locale);

    // NumberFormat factories
    static DecimalFormat* getInstance();
    static DecimalFormat* getInstance(Locale* locale);
    static DecimalFormat* getNumberInstance();
    static DecimalFormat* getNumberInstance(Locale* locale);
    static DecimalFormat* getIntegerInstance();  // "#,##0", HALF_EVEN, parseIntegerOnly
    static DecimalFormat* getIntegerInstance(Locale* locale);
    static DecimalFormat* getPercentInstance();
    static DecimalFormat* getPercentInstance(Locale* locale);
    static DecimalFormat* getCurrencyInstance();
    static DecimalFormat* getCurrencyInstance(Locale* locale);

    String format(double number);
    String format(float number) { return format(static_cast<double>(number)); }
    String format(int64_t number);
    String format(int32_t number) { return format(static_cast<int64_t>(number)); }
    String format(int16_t number) { return format(static_cast<int64_t>(number)); }
    String format(int8_t number) { return format(static_cast<int64_t>(number)); }
    String format(Object* number);  // a boxed Number; IllegalArgumentException otherwise
    // Returns a Long when the value is integral and fits, else a Double (Java semantics).
    Number* parse(const String& source);
    Number* parse(const String& source, int32_t* pos);

    void applyPattern(const String& pattern);
    String toPattern();

    int32_t getMaximumFractionDigits() { return maxFrac_; }
    int32_t getMinimumFractionDigits() { return minFrac_; }
    int32_t getMaximumIntegerDigits() { return maxInt_; }
    int32_t getMinimumIntegerDigits() { return minInt_; }
    void setMaximumFractionDigits(int32_t v);
    void setMinimumFractionDigits(int32_t v);
    void setMaximumIntegerDigits(int32_t v);
    void setMinimumIntegerDigits(int32_t v);
    bool isGroupingUsed() { return groupingUsed_; }
    void setGroupingUsed(bool v) { groupingUsed_ = v; }
    int32_t getGroupingSize() { return groupingSize_; }
    void setGroupingSize(int32_t v) { groupingSize_ = static_cast<int8_t>(v); }
    bool isDecimalSeparatorAlwaysShown() { return decimalSeparatorAlwaysShown_; }
    void setDecimalSeparatorAlwaysShown(bool v) { decimalSeparatorAlwaysShown_ = v; }
    bool isParseIntegerOnly() { return parseIntegerOnly_; }
    void setParseIntegerOnly(bool v) { parseIntegerOnly_ = v; }
    RoundingMode getRoundingMode() { return roundingMode_; }
    void setRoundingMode(RoundingMode mode);
    int32_t getMultiplier() { return multiplier_; }
    void setMultiplier(int32_t v) { multiplier_ = v; }
    String getPositivePrefix() { return posPrefix_; }
    String getPositiveSuffix() { return posSuffix_; }
    String getNegativePrefix() { return negPrefix_; }
    String getNegativeSuffix() { return negSuffix_; }
    void setPositivePrefix(const String& v) { posPrefix_ = v; posPrefixPattern_ = nullptr; }
    void setPositiveSuffix(const String& v) { posSuffix_ = v; posSuffixPattern_ = nullptr; }
    void setNegativePrefix(const String& v) { negPrefix_ = v; negPrefixPattern_ = nullptr; }
    void setNegativeSuffix(const String& v) { negSuffix_ = v; negSuffixPattern_ = nullptr; }

    bool equals(Object* obj) override;
    int32_t hashCode() override;
    Object* clone() override { return new DecimalFormat(*this); }

private:
    void formatDigits(String& out, bool negative, const std::string& digits, int32_t decimalAt, bool isInteger);

    String posPrefix_, posSuffix_, negPrefix_ = "-", negSuffix_;
    String posPrefixPattern_, posSuffixPattern_, negPrefixPattern_, negSuffixPattern_;
    int32_t minInt_ = 1, maxInt_ = 2147483647, minFrac_ = 0, maxFrac_ = 3;
    int8_t groupingSize_ = 3;
    bool groupingUsed_ = true;
    bool decimalSeparatorAlwaysShown_ = false;
    bool parseIntegerOnly_ = false;
    bool useExponential_ = false;
    int8_t minExponentDigits_ = 0;
    int32_t multiplier_ = 1;
    RoundingMode roundingMode_ = RoundingMode::HALF_EVEN;
};
using NumberFormat = DecimalFormat;

}  // namespace jlang

template<>
struct std::hash<jlang::RoundingMode> {
    size_t operator()(jlang::RoundingMode r) const noexcept { return static_cast<size_t>(r.ordinal()); }
};
