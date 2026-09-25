// Tests for <jlang/Time.h>: Date, Timestamp, Calendar/GregorianCalendar, TimeZone,
// SimpleDateFormat, DecimalFormat/NumberFormat.
//
// TimeParityWithJava runs the same computations as data/time/TimeRef.java (in the same order) and
// compares every line with the output of OpenJDK (data/time/time_ref.inc), for the zones UTC,
// Europe/Berlin, America/New_York and Asia/Seoul.
#include "jtest.h"

#include <jlang/Time.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace jlang;

#include "data/time/time_ref.inc"

namespace {

std::vector<std::pair<std::string, std::string>>* g_out;

void p(const std::string& k, const String& v) { g_out->emplace_back(k, std::string(v)); }
template<class T>
void p(const std::string& k, const T& v) {
    p(k, str(v));
}

String fields(Calendar* c) {
    String s("");
    for (int32_t f = 0; f < Calendar::FIELD_COUNT; f++) {
        if (f > 0) s += ",";
        s += c->get(f);
    }
    return s;
}

const int64_t TIMES[] = {0LL,
                         1LL,
                         -1LL,
                         999LL,
                         1000LL,
                         1308650400000LL,
                         1308650400123LL,
                         1300000000000LL,
                         -1300000000000LL,
                         253402300799999LL,
                         1711846800000LL,
                         1711846799999LL,
                         1729990800000LL,
                         1729990799999LL,
                         951782400000LL,
                         978307200000LL,
                         1104537600000LL,
                         1230681600000LL,
                         1293753600000LL,
                         1609459199999LL,
                         -62135596800000LL + 86400000LL * 400,
                         4102444800000LL,
                         1167609600000LL,
                         1199145600000LL};

const char* const FORMATS[] = {"H:mm:ss",
                               "yyyy-MM-dd HH-mm-ss",
                               "yyyy-MM-dd HH:mm:ss",
                               "dd MMM HH:mm:ss,SSS",
                               "dd MMM yyyy HH:mm:ss,SSS",
                               "yyyy-dd-MM HH:mm:ss",
                               "EEE MMM dd HH:mm:ss zzz yyyy",
                               "EEEE, MMMM d, yyyy h:mm a",
                               "yy-M-d k K h a",
                               "Z",
                               "z",
                               "D w W F",
                               "'''quoted''' 'text' G 'o''clock'",
                               "yyyyy.MMMMM.dd GGG hh:mm aaa",
                               "S SS SSS SSSS",
                               "E EE EEE EEEE",
                               "M MM MMM MMMM",
                               "y yy yyy yyyy",
                               "h hh H HH k kk K KK",
                               "yyyyMMddHHmmss",
                               "'T'HH'h'mm",
                               "zzzz",
                               "ZZZZ"};

String b(bool v) { return String(v ? "true" : "false"); }

void runReference() {
    Locale::setDefault(Locale::US);
    const char* zones[] = {"UTC", "Europe/Berlin", "America/New_York", "Asia/Seoul"};
    for (const char* zone : zones) {
        TimeZone::setDefault(TimeZone::getTimeZone(String(zone)));
        std::string z = std::string(zone) + ":";
        for (int64_t t : TIMES) {
            std::string k = z + std::to_string(t) + ":";
            Date* d = new Date(t);
            p(k + "date", d->toString());
            p(k + "datehash", d->hashCode());
            p(k + "dateget", str(d->getYear(), ",", d->getMonth(), ",", d->getDate(), ",", d->getDay(), ",",
                                 d->getHours(), ",", d->getMinutes(), ",", d->getSeconds(), ",", d->getTimezoneOffset()));
            Timestamp* ts = new Timestamp(t);
            p(k + "ts", str(ts->toString(), " ", ts->getTime(), " ", ts->getNanos(), " ", ts->hashCode()));
            Calendar* c = Calendar::getInstance();
            c->setTimeInMillis(t);
            p(k + "cal", fields(c));
            p(k + "calmax", str(c->getActualMaximum(Calendar::DAY_OF_MONTH), ",", c->getActualMaximum(Calendar::DAY_OF_YEAR),
                                ",", c->getActualMaximum(Calendar::WEEK_OF_YEAR), ",",
                                c->getActualMaximum(Calendar::WEEK_OF_MONTH), ",",
                                c->getActualMinimum(Calendar::DAY_OF_MONTH), ",",
                                c->getActualMaximum(Calendar::DAY_OF_WEEK_IN_MONTH)));
            for (size_t i = 0; i < sizeof FORMATS / sizeof FORMATS[0]; i++) {
                p(k + "fmt" + std::to_string(i), (new SimpleDateFormat(String(FORMATS[i])))->format(d));
            }
        }

        Calendar* c = Calendar::getInstance();
        c->setTimeInMillis(1308650400123LL);
        c->set(Calendar::DAY_OF_MONTH, 35);
        p(z + "set1", str(c->getTimeInMillis(), " ", fields(c)));
        c->set(Calendar::MONTH, 13);
        c->set(Calendar::HOUR_OF_DAY, 25);
        c->set(Calendar::MINUTE, -5);
        p(z + "set2", str(c->getTimeInMillis(), " ", fields(c)));
        c->set(Calendar::YEAR, 2011);
        c->set(Calendar::MONTH, Calendar::JANUARY);
        c->set(Calendar::DATE, 31);
        c->add(Calendar::MONTH, 1);
        p(z + "add1", str(c->getTimeInMillis(), " ", fields(c)));
        c->set(2012, Calendar::FEBRUARY, 29);
        c->add(Calendar::YEAR, 1);
        p(z + "add2", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::DAY_OF_MONTH, 400);
        p(z + "add3", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::HOUR, 30);
        p(z + "add4", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::MINUTE, -100000);
        p(z + "add5", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::WEEK_OF_YEAR, 3);
        p(z + "add6", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::MILLISECOND, 12345678);
        p(z + "add7", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::SECOND, 86400 * 180);
        p(z + "add8", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::DAY_OF_YEAR, -1000);
        p(z + "add9", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::HOUR_OF_DAY, 24 * 100 + 5);
        p(z + "add10", str(c->getTimeInMillis(), " ", fields(c)));
        c->add(Calendar::DAY_OF_WEEK, 10);
        p(z + "add11", str(c->getTimeInMillis(), " ", fields(c)));
        c->roll(Calendar::MONTH, 5);
        p(z + "roll1", str(c->getTimeInMillis(), " ", fields(c)));
        c->roll(Calendar::DAY_OF_MONTH, 20);
        p(z + "roll2", str(c->getTimeInMillis(), " ", fields(c)));
        c->roll(Calendar::HOUR_OF_DAY, 5);
        p(z + "roll3", str(c->getTimeInMillis(), " ", fields(c)));
        c->roll(Calendar::MONTH, true);
        p(z + "roll4", str(c->getTimeInMillis(), " ", fields(c)));
        c->roll(Calendar::YEAR, -3);
        p(z + "roll5", str(c->getTimeInMillis(), " ", fields(c)));
        c->roll(Calendar::MINUTE, 75);
        p(z + "roll6", str(c->getTimeInMillis(), " ", fields(c)));
        c->roll(Calendar::DAY_OF_YEAR, 200);
        p(z + "roll7", str(c->getTimeInMillis(), " ", fields(c)));
        c->roll(Calendar::HOUR, -7);
        p(z + "roll8", str(c->getTimeInMillis(), " ", fields(c)));

        Calendar* g1 = new GregorianCalendar(2011, 11, 15);
        Calendar* g2 = new GregorianCalendar(2012, 0, 5);
        p(z + "greg", str(g1->getTimeInMillis(), " ", g2->getTimeInMillis(), " ", b(g1->before(g2)), " ",
                          b(g1->after(g2)), " ", b(g2->after(g1)), " ", b(g1->equals(g2)), " ",
                          b(g1->equals(g1->clone())), " ", fields(g1)));
        Calendar* g3 = new GregorianCalendar(2011, 5, 21, 10, 30);
        Calendar* g4 = new GregorianCalendar(2011, 5, 21, 10, 30, 45);
        p(z + "greg2", str(g3->getTimeInMillis(), " ", g4->getTimeInMillis(), " ", g4->getTime()->toString()));

        Calendar* dst = Calendar::getInstance();
        dst->clear();
        dst->set(2024, Calendar::MARCH, 31, 2, 30, 0);
        p(z + "gap", str(dst->getTimeInMillis(), " ", fields(dst)));
        dst->clear();
        dst->set(2024, Calendar::OCTOBER, 27, 2, 30, 0);
        p(z + "overlap", str(dst->getTimeInMillis(), " ", fields(dst)));
        dst->clear();
        dst->set(2024, Calendar::NOVEMBER, 3, 1, 30, 0);
        p(z + "overlapNY", str(dst->getTimeInMillis(), " ", fields(dst)));
        dst->clear();
        dst->set(2024, Calendar::MARCH, 10, 2, 30, 0);
        p(z + "gapNY", str(dst->getTimeInMillis(), " ", fields(dst)));
        dst->clear();
        dst->set(Calendar::YEAR, 2010);
        p(z + "clear", str(dst->getTimeInMillis(), " ", fields(dst)));
        dst->clear();
        p(z + "clear2", dst->getTimeInMillis());

        Calendar* w = Calendar::getInstance();
        w->setTimeInMillis(1308650400123LL);
        w->set(Calendar::DAY_OF_WEEK, Calendar::MONDAY);
        p(z + "dow", str(w->getTimeInMillis(), " ", fields(w)));
        w->set(Calendar::HOUR, 3);
        w->set(Calendar::AM_PM, Calendar::PM);
        p(z + "ampm", str(w->getTimeInMillis(), " ", fields(w)));
        w->set(Calendar::WEEK_OF_YEAR, 1);
        p(z + "woy", str(w->getTimeInMillis(), " ", fields(w)));
        w->set(Calendar::DAY_OF_YEAR, 300);
        p(z + "doy", str(w->getTimeInMillis(), " ", fields(w)));
        w->set(Calendar::DAY_OF_WEEK_IN_MONTH, -1);
        w->set(Calendar::DAY_OF_WEEK, Calendar::FRIDAY);
        p(z + "dowim", str(w->getTimeInMillis(), " ", fields(w)));
        w->set(Calendar::WEEK_OF_MONTH, 2);
        p(z + "wom", str(w->getTimeInMillis(), " ", fields(w)));
        w->setTime(new Date(1300000000000LL));
        p(z + "settime", str(w->getTime()->getTime(), " ", fields(w)));
        Calendar* gs = Calendar::getInstance();
        gs->setTimeInMillis(1308650400123LL);
        gs->set(Calendar::YEAR, gs->get(Calendar::YEAR));
        gs->set(Calendar::MONTH, 1);
        gs->set(Calendar::DATE, 30);
        gs->set(Calendar::HOUR_OF_DAY, 7);
        gs->set(Calendar::MINUTE, 8);
        gs->set(Calendar::SECOND, 9);
        p(z + "guild", str(gs->getTimeInMillis(), " ", fields(gs), " ", b(gs->after(c)), " ", b(gs->before(c))));

        const char* parses[][2] = {
            {"yyyy-MM-dd HH:mm:ss", "2011-06-21 10:00:00"},
            {"yyyy-MM-dd HH:mm:ss", "2011-13-45 25:61:61"},
            {"dd MMM yyyy", "05 Feb 2011"},
            {"dd MMM yyyy", "05 february 2011"},
            {"H:mm:ss", "7:05:09"},
            {"yyyy-MM-dd", "2011-06-21extra"},
            {"yy-MM-dd", "11-06-21"},
            {"yy-MM-dd", "95-06-21"},
            {"EEE, d MMM yyyy HH:mm:ss Z", "Tue, 21 Jun 2011 10:00:00 +0200"},
            {"EEE, d MMM yyyy HH:mm:ss z", "Tue, 21 Jun 2011 10:00:00 GMT-03:30"},
            {"EEE, d MMM yyyy HH:mm:ss z", "Tue, 21 Jun 2011 10:00:00 UTC"},
            {"yyyyMMddHHmmss", "20110621100000"},
            {"yyyy-MM-dd HH:mm:ss", "xyz"},
            {"yyyy-MM-dd HH:mm:ss", "2011-06-21"},
            {"yyyy-MM-dd h:mm a", "2011-06-21 12:15 AM"},
            {"yyyy-MM-dd h:mm a", "2011-06-21 12:15 pm"},
            {"yyyy-MM-dd HH:mm:ss.SSS", "2011-06-21 10:00:00.5"},
            {"yyyy-MM-dd'T'HH:mm", "2011-06-21T10:07"},
            {"MMM d, yyyy", "Jun 1, 2011"},
            {"d/M/y", "1/2/11"},
            {"d/M/y", "1/2/2011"},
            {"yyyy-MM-dd", " 2011-06-21"},
            {"yyyy-MM-dd", "-2011-06-21"},
        };
        for (size_t i = 0; i < sizeof parses / sizeof parses[0]; i++) {
            auto* f = new SimpleDateFormat(String(parses[i][0]));
            try {
                Date* d = f->parse(String(parses[i][1]));
                p(z + "parse" + std::to_string(i), d->getTime());
            } catch (ParseException& e) {
                p(z + "parse" + std::to_string(i), str("ParseException ", e.getMessage(), " @", e.getErrorOffset()));
            }
        }
        auto* strict = new SimpleDateFormat(String("yyyy-MM-dd HH:mm:ss"));
        strict->setLenient(false);
        try {
            p(z + "strict", strict->parse(String("2011-13-45 25:61:61"))->getTime());
        } catch (ParseException& e) {
            p(z + "strict", str("ParseException ", e.getMessage(), " @", e.getErrorOffset()));
        }
        p(z + "strict2", strict->parse(String("2011-12-31 23:59:59"))->getTime());
        auto* utcFmt = new SimpleDateFormat(String("yyyy-MM-dd HH:mm:ss zzz"));
        utcFmt->setTimeZone(TimeZone::getTimeZone(String("GMT+05:30")));
        p(z + "tzfmt", utcFmt->format(new Date(1308650400123LL)));
        p(z + "tzpat", utcFmt->toPattern());

        const char* tsv[] = {"2011-06-21 10:00:00",        "2011-06-21 10:00:00.0",   "2011-06-21 10:00:00.123456789",
                             "2011-6-1 1:2:3.5",           "1969-12-31 23:59:59.999", "2011-06-21 10:00:00.000001",
                             "2011-06-21",                 "bad",                     "2011-06-21 10:00:00.1234567890",
                             "2011-02-30 10:00:00"};
        for (size_t i = 0; i < sizeof tsv / sizeof tsv[0]; i++) {
            try {
                Timestamp* ts = Timestamp::valueOf(String(tsv[i]));
                p(z + "tsv" + std::to_string(i), str(ts->toString(), " ", ts->getTime(), " ", ts->getNanos()));
            } catch (IllegalArgumentException& e) {
                p(z + "tsv" + std::to_string(i), str("IAE ", e.getMessage()));
            }
        }
        Timestamp* t1 = new Timestamp(1308650400123LL);
        Timestamp* t2 = new Timestamp(1308650400123LL);
        t2->setNanos(123000001);
        Date* dd = new Date(1308650400123LL);
        p(z + "tscmp", str(t1->compareTo(t2), " ", t2->compareTo(t1), " ", t1->compareTo(dd), " ", b(t1->equals(t2)), " ",
                           b(t1->equals(new Timestamp(1308650400123LL))), " ", b(t1->equals(static_cast<Object*>(dd))),
                           " ", b(dd->equals(t1)), " ", b(t1->before(t2)), " ", b(t2->after(t1)), " ", dd->compareTo(t1),
                           " ", t2->toString(), " ", t2->getTime(), " ", b(dd->before(t2)), " ", b(dd->after(t2))));
        Timestamp* t3 = new Timestamp(-1500LL);
        p(z + "tsneg", str(t3->toString(), " ", t3->getTime(), " ", t3->getNanos()));
        t3->setTime(-999LL);
        p(z + "tsneg2", str(t3->toString(), " ", t3->getTime(), " ", t3->getNanos()));
        try {
            t3->setNanos(1000000000);
        } catch (IllegalArgumentException& e) {
            p(z + "tsnanos", str("IAE ", e.getMessage()));
        }
        p(z + "datecmp", str((new Date(5))->compareTo(new Date(6)), " ", (new Date(6))->compareTo(new Date(5)), " ",
                             (new Date(5))->compareTo(new Date(5)), " ", b((new Date(5))->before(new Date(6))), " ",
                             b((new Date(5))->after(new Date(6))), " ", b((new Date(5))->equals(new Date(5)))));
        TimeZone* tz = TimeZone::getDefault();
        p(z + "tz", str(tz->getID(), " ", tz->getRawOffset(), " ", tz->getOffset(1308650400123LL), " ",
                        tz->getOffset(1293753600000LL), " ", b(tz->inDaylightTime(new Date(1308650400123LL))), " ",
                        b(tz->useDaylightTime()), " ", tz->getDSTSavings(), " ",
                        tz->getDisplayName(false, TimeZone::SHORT), " ", tz->getDisplayName(true, TimeZone::SHORT)));
    }

    TimeZone::setDefault(TimeZone::getTimeZone(String("UTC")));
    const char* ids[] = {"GMT", "UTC", "GMT+2", "GMT+02:00", "GMT-0530", "GMT+14", "Nonexistent/Zone", "Europe/Berlin", "CET", "EST"};
    for (const char* id : ids) {
        TimeZone* tz = TimeZone::getTimeZone(String(id));
        p(std::string("zone:") + id, str(tz->getID(), " ", tz->getRawOffset(), " ", tz->getOffset(1308650400123LL)));
    }

    const double ds[] = {0, 1, -1, 0.5, 1.5, 2.5, -2.5, 0.125, 0.135, 12345.6789, 1234567.891, 99.995, 0.0001, 1e-10,
                         123456789012.0, 1e15, 3.14159, 0.8055, 1024, 524288.75, -0.0, NAN, INFINITY, -INFINITY, 1e20,
                         7.0E-5};
    const char* pats[] = {" (0.0000'%')", " # 'KB'", "#,##0", "0.00", "#,##0.###", "0.#", "#.##", "000", "#,###", "#",
                          "0", "00.00", "#,##0.00;(#,##0.00)", "0.00%", "'#'#", "#,##,###", "0.###E0", "00.###E0",
                          "##0.#####E0", "#0.0#", "¤#,##0.00", "#,##0.0#;-#", ".00", "#.", "0000.0000"};
    for (size_t i = 0; i < sizeof pats / sizeof pats[0]; i++) {
        auto* df = new DecimalFormat(String(pats[i]));
        String sb("");
        for (double d : ds) sb += str("[", df->format(d), "]");
        p("df" + std::to_string(i), sb);
        p("dfl" + std::to_string(i), str("[", df->format(static_cast<int64_t>(0)), "][", df->format(static_cast<int64_t>(1234567)),
                                         "][", df->format(static_cast<int64_t>(-98765)), "][", df->format(INT64_MAX), "][",
                                         df->format(INT64_MIN), "]"));
        p("dfp" + std::to_string(i), df->toPattern());
    }
    DecimalFormat* nf = NumberFormat::getInstance(Locale::ENGLISH);
    p("nf", str(nf->format(static_cast<int64_t>(1234567)), " ", nf->format(static_cast<int64_t>(-1234567)), " ",
                nf->format(static_cast<int64_t>(0)), " ", nf->format(1234.5678), " ", nf->format(0.0005), " ",
                nf->format(0.0004), " ", nf->format(-0.0004), " ", nf->format(INT64_MIN), " ", nf->format(1e18)));
    DecimalFormat* nf2 = NumberFormat::getInstance(Locale::ENGLISH);
    nf2->setMaximumFractionDigits(1);
    nf2->setMinimumFractionDigits(1);
    p("nf2", str(nf2->format(2.25), " ", nf2->format(2.35), " ", nf2->format(1234567), " ", nf2->format(0.05)));
    nf2->setGroupingUsed(false);
    nf2->setMinimumIntegerDigits(3);
    p("nf3", str(nf2->format(2.25), " ", nf2->format(1234567), " ", nf2->format(-5)));
    DecimalFormat* nf4 = NumberFormat::getIntegerInstance(Locale::US);
    p("nf4", str(nf4->format(2.5), " ", nf4->format(3.5), " ", nf4->format(-2.5), " ", nf4->format(1234567.89)));
    DecimalFormat* nf5 = NumberFormat::getPercentInstance(Locale::US);
    p("nf5", str(nf5->format(0.256), " ", nf5->format(1.5), " ", nf5->format(-0.004)));
    auto* df = new DecimalFormat(String("#,##0.00"));
    p("dfparse", str(df->parse(String("1,234.56")), " ", df->parse(String("-7")), " ", df->parse(String("12abc")), " ",
                     df->parse(String("1,234"))));
    try {
        df->parse(String("abc"));
    } catch (ParseException& e) {
        p("dfparseerr", str(e.getMessage(), " @", e.getErrorOffset()));
    }
    auto* dfr = new DecimalFormat(String("0.00"));
    dfr->setRoundingMode(RoundingMode::HALF_UP);
    p("dfhalfup", str(dfr->format(2.345), " ", dfr->format(2.355), " ", dfr->format(-2.345), " ", dfr->format(0.125)));
}

}  // namespace

JTEST(TimeParityWithJava) {
    std::vector<std::pair<std::string, std::string>> out;
    g_out = &out;
    TimeZone* savedZone = TimeZone::getDefault();
    runReference();
    TimeZone::setDefault(savedZone);
    std::string all;
    for (const char* chunk : kTimeRef) all += chunk;
    std::map<std::string, std::string> expected;
    std::vector<std::string> order;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t nl = all.find('\n', pos);
        if (nl == std::string::npos) nl = all.size();
        std::string line = all.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty()) continue;
        size_t eq = line.find('=');
        expected[line.substr(0, eq)] = line.substr(eq + 1);
        order.push_back(line.substr(0, eq));
    }
    JCHECK_EQ(out.size(), order.size());
    int mismatches = 0;
    for (auto& [k, v] : out) {
        auto it = expected.find(k);
        if (it == expected.end()) {
            jtest::fail(__FILE__, __LINE__, "unexpected key " + k);
            continue;
        }
        if (it->second != v) {
            if (++mismatches <= 60) jtest::fail(__FILE__, __LINE__, k + "\n      java: " + it->second + "\n      c++ : " + v);
        }
    }
    if (mismatches > 60) jtest::fail(__FILE__, __LINE__, std::to_string(mismatches) + " mismatches in total");
}

JTEST(TimeCalendarBasics) {
    TimeZone* saved = TimeZone::getDefault();
    TimeZone::setDefault(TimeZone::getTimeZone(String("Europe/Berlin")));
    Calendar* c = Calendar::getInstance();
    c->clear();
    c->set(2011, Calendar::JANUARY, 31, 12, 0, 0);
    c->add(Calendar::MONTH, 1);
    JCHECK_EQ(c->get(Calendar::DAY_OF_MONTH), 28);
    c->set(Calendar::DAY_OF_MONTH, 35);  // lenient: rolls into March
    JCHECK_EQ(c->get(Calendar::MONTH), Calendar::MARCH);
    JCHECK_EQ(c->get(Calendar::DAY_OF_MONTH), 7);
    Calendar* cl = dynamic_cast<Calendar*>(c->clone());
    JCHECK(cl != nullptr && cl->equals(c) && cl != c);
    cl->add(Calendar::SECOND, 1);
    JCHECK(cl->after(c) && c->before(cl) && !c->before(static_cast<Object*>(cl->getTime())));
    JCHECK_THROWS(IllegalArgumentException, c->add(Calendar::ZONE_OFFSET, 1));
    c->setLenient(false);
    c->set(Calendar::MONTH, 12);
    JCHECK_THROWS(IllegalArgumentException, c->getTimeInMillis());
    TimeZone::setDefault(saved);
}

JTEST(TimeTimestampValueOf) {
    TimeZone* saved = TimeZone::getDefault();
    TimeZone::setDefault(TimeZone::getTimeZone(String("UTC")));
    Timestamp* ts = Timestamp::valueOf(String("2011-06-21 10:00:00"));
    JCHECK_EQ(ts->toString(), String("2011-06-21 10:00:00.0"));
    JCHECK_EQ(ts->getTime(), INT64_C(1308650400000));
    JCHECK_THROWS(IllegalArgumentException, Timestamp::valueOf(String("2011/06/21 10:00:00")));
    JCHECK_THROWS(NumberFormatException, Timestamp::valueOf(String("20x1-06-21 10:00:00")));
    TimeZone::setDefault(saved);
}

// Dates before AD 1 and far outside the usual range (reference values from Java 8, TZ=UTC).
// java.util.Date and Timestamp print the year of era, Calendar normalizes lenient months in
// the Julian calendar.
JTEST(TimeAncientAndBceDates) {
    TimeZone* saved = TimeZone::getDefault();
    TimeZone::setDefault(TimeZone::getTimeZone(String("UTC")));
    struct Row {
        int64_t t;
        const char* date;
        int32_t year;
        const char* ts;
        const char* gmt;
    };
    const Row rows[] = {
        {INT64_C(-62167392000000), "Thu Jan 01 00:00:00 UTC 1", -1899, "0001-01-01 00:00:00.0", "1 Jan 1 00:00:00 GMT"},
        {INT64_C(-65310019200000), "Wed Jun 01 00:00:00 UTC 101", -1799, "0101-06-01 00:00:00.0", "1 Jun 101 00:00:00 GMT"},
        {INT64_C(-353215296000000), "Thu Apr 01 00:00:00 UTC 9224", 7324, "9224-04-01 00:00:00.0", "1 Apr 9224 00:00:00 GMT"},
        {INT64_C(-62135769600001), "Fri Dec 31 23:59:59 UTC 1", -1899, "0001-12-31 23:59:59.999", "31 Dec 1 23:59:59 GMT"},
        {INT64_C(-58546195200000), "Sun Oct 01 00:00:00 UTC 114", -1786, "0114-10-01 00:00:00.0", "1 Oct 114 00:00:00 GMT"},
        {INT64_C(-12219292800001), "Thu Oct 04 23:59:59 UTC 1582", -318, "1582-10-04 23:59:59.999", "4 Oct 1582 23:59:59 GMT"},
        {INT64_C(253402300800000), "Sat Jan 01 00:00:00 UTC 10000", 8100, "10000-01-01 00:00:00.0", "1 Jan 10000 00:00:00 GMT"},
    };
    for (const Row& r : rows) {
        Date* d = new Date(r.t);
        JCHECK_EQ(d->toString(), String(r.date));
        JCHECK_EQ(d->getYear(), r.year);
        JCHECK_EQ((new Timestamp(r.t))->toString(), String(r.ts));
        JCHECK_EQ(d->toGMTString(), String(r.gmt));
    }
    // deprecated constructors and setYear take a proleptic year: 1900 + -1901 = -1 = 2 BC
    Date* d = new Date(-1901, 0, 1);
    JCHECK_EQ(d->getTime(), INT64_C(-62198928000000));
    JCHECK_EQ(d->toString(), String("Wed Jan 01 00:00:00 UTC 2"));
    JCHECK_EQ(d->getYear(), -1898);
    d = new Date(0);
    d->setYear(-1901);
    JCHECK_EQ(d->getTime(), INT64_C(-62198928000000));
    Timestamp* ts = new Timestamp(-1901, 0, 1, 0, 0, 0, 5);
    JCHECK_EQ(ts->getTime(), INT64_C(-62198928000000));
    JCHECK_EQ(ts->toString(), String("0002-01-01 00:00:00.000000005"));
    // Connector/J style lenient set(): months past December roll into later Julian years
    struct CalRow {
        int32_t y, m, d;
        int64_t t;
        int32_t era, year;
    };
    const CalRow cals[] = {
        {112, 33, 1, INT64_C(-58546195200000), 1, 114}, {99, 29, 1, INT64_C(-58966963200000), 1, 101},
        {-1, 29, 1, INT64_C(-62122723200000), 1, 1},    {0, 0, 1, INT64_C(-62167392000000), 0, 1},
        {-100, 5, 1, INT64_C(-65310019200000), 0, 101}, {-9223, 3, 1, INT64_C(-353215296000000), 0, 9224},
        {1582, 9, 10, INT64_C(-12218860800000), 1, 1582},
    };
    Calendar* c = Calendar::getInstance();
    c->setTimeInMillis(INT64_C(1308650400123));  // a reused calendar, as the SQL layer does
    for (const CalRow& r : cals) {
        c->clear();
        c->set(r.y, r.m, r.d, 0, 0, 0);
        JCHECK_EQ(c->getTimeInMillis(), r.t);
        JCHECK_EQ(c->get(Calendar::ERA), r.era);
        JCHECK_EQ(c->get(Calendar::YEAR), r.year);
    }
    TimeZone::setDefault(saved);
}

JTEST(TimeSimpleDateFormatErrors) {
    JCHECK_THROWS(IllegalArgumentException, new SimpleDateFormat(String("yyyy-qq")));
    auto* f = new SimpleDateFormat(String("yyyy-MM-dd"));
    JCHECK_THROWS(ParseException, f->parse(String("20110621")));
    JCHECK_THROWS(IllegalArgumentException, f->format(static_cast<Object*>(new StringBuilder())));
    // Number formatted as a date (Format.format(Object) semantics)
    f->setTimeZone(TimeZone::getTimeZone(String("UTC")));
    JCHECK_EQ(f->format(static_cast<Object*>(Long::valueOf(0))), String("1970-01-01"));
}

JTEST(TimeDecimalFormatErrors) {
    JCHECK_THROWS(IllegalArgumentException, new DecimalFormat(String("#,##0.0.0")));
    JCHECK_THROWS(IllegalArgumentException, new DecimalFormat(String("0%%")));
    auto* df = new DecimalFormat(String("0.00"));
    df->setRoundingMode(RoundingMode::UNNECESSARY);
    JCHECK_EQ(df->format(1.5), String("1.50"));
    JCHECK_THROWS(ArithmeticException, df->format(1.555));
    JCHECK_EQ(RoundingMode::valueOf(String("HALF_EVEN")), RoundingMode::HALF_EVEN);
    JCHECK_EQ(RoundingMode::HALF_UP.name(), String("HALF_UP"));
}
