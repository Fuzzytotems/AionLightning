// jlang/src/sql_time.cpp - date/time glue between jlang/Sql.h and <jlang/Time.h>
// (java.sql.Timestamp, java.util.Date, java.util.GregorianCalendar in the default TimeZone).
// It is the only SQL file that needs the complete Time.h types; the rest of the SQL layer works
// with forward declarations.
#include "sql_internal.h"

#include <jlang/Time.h>

#include <cstdio>
#include <string>

namespace jlang::sql_detail {

Calendar* newLocalCalendar() {
    return Calendar::getInstance();  // GregorianCalendar in the default zone
}

int64_t localToMillis(Calendar* cal, int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute,
                      int32_t second) {
    // Connector/J TimeUtil.fastTimestampCreate / fastDateCreate / fastTimeCreate
    cal->clear();
    cal->set(year, month - 1, day, hour, minute, second);
    return cal->getTimeInMillis();
}

Fields millisToLocal(Calendar* cal, int64_t millis) {
    cal->setTimeInMillis(millis);
    Fields f{};
    f.year = cal->get(Calendar::YEAR);
    f.month = cal->get(Calendar::MONTH) + 1;
    f.day = cal->get(Calendar::DAY_OF_MONTH);
    f.hour = cal->get(Calendar::HOUR_OF_DAY);
    f.minute = cal->get(Calendar::MINUTE);
    f.second = cal->get(Calendar::SECOND);
    f.millis = cal->get(Calendar::MILLISECOND);
    return f;
}

String timestampToString(Calendar* cal, int64_t millis, int32_t nanos) {
    // java.sql.Timestamp.toString(): the date part of a Timestamp holds whole seconds.
    int64_t secondsMillis = millis - (((millis % 1000) + 1000) % 1000);
    Fields f = millisToLocal(cal, secondsMillis);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04lld-%02d-%02d %02d:%02d:%02d.", static_cast<long long>(f.year), f.month, f.day,
                  f.hour, f.minute, f.second);
    std::string s = buf;
    if (nanos == 0) {
        s += '0';
    } else {
        char nb[16];
        std::snprintf(nb, sizeof nb, "%09d", nanos);
        std::string n = nb;
        n.erase(n.find_last_not_of('0') + 1);
        s += n;
    }
    return String(std::move(s));
}

String dateToString(Calendar* cal, int64_t millis) {
    Fields f = millisToLocal(cal, millis);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04lld-%02d-%02d", static_cast<long long>(f.year), f.month, f.day);
    return String(buf);
}

String timeToString(Calendar* cal, int64_t millis) {
    Fields f = millisToLocal(cal, millis);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", f.hour, f.minute, f.second);
    return String(buf);
}

Timestamp* makeTimestamp(int64_t millis, int32_t nanos) {
    auto* ts = new Timestamp(millis);
    if (nanos >= 0) ts->setNanos(nanos);
    return ts;
}

int64_t timestampMillis(Timestamp* ts) {
    return ts->getTime();
}

int32_t timestampNanos(Timestamp* ts) {
    return ts->getNanos();
}

bool dateMillis(Object* o, int64_t& millis) {
    if (auto* d = dynamic_cast<Date*>(o)) {
        millis = d->getTime();
        return true;
    }
    return false;
}

Timestamp* asTimestamp(Object* o) {
    return dynamic_cast<Timestamp*>(o);
}

Object* timestampObject(Timestamp* ts) {
    return ts;
}

}  // namespace jlang::sql_detail
