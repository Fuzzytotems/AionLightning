// jlang/tests/test_sql_oracle.cpp - behavior comparison with MySQL Connector/J 5.1.13.
//
// The scenario below is a line-by-line port of a Java program that ran the same statements
// through Connector/J 5.1.13 (the driver of the Java servers) against MariaDB 10.11; its output
// is embedded in sql_oracle_expected.inc (TZ=UTC and TZ=Europe/Berlin, zeroDateTimeBehavior
// exception and convertToNull). The test runs the scenario through jlang/Sql.h and requires
// identical output, except for the documented deviations listed in kKnownDeviations.
//
// Needs the local MariaDB server (user aion/aion, see test_sql.cpp); skipped when unreachable.
#include "jtest.h"

#include <jlang/Sql.h>

#if __has_include(<jlang/Time.h>)  // the tests need java.sql.Timestamp
#include <jlang/Time.h>

#include <cstdlib>
#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace jlang;

namespace {

// Timestamp.getNanos() when <jlang/Time.h> provides it, else derived from getTime().
template<class T>
int32_t nanosOf(T* ts) {
    if constexpr (requires { ts->getNanos(); }) {
        return ts->getNanos();
    } else {
        int64_t m = ts->getTime() % 1000;
        return static_cast<int32_t>((m < 0 ? m + 1000 : m) * 1000000);
    }
}

#include "sql_oracle_expected.inc"

std::string* g_out = nullptr;

std::string hex(Array<int8_t>* b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (int32_t i = 0; i < b->length; i++) {
        unsigned v = static_cast<uint8_t>((*b)[i]);
        s += d[v >> 4];
        s += d[v & 15];
    }
    return s;
}

// java.sql.Timestamp.toString() in the default zone. Written out with Calendar fields instead of
// calling ts->toString() so that years BC print as Java prints them (year of era).
std::string javaTs(Timestamp* ts) {
    Calendar* cal = Calendar::getInstance();
    int64_t ms = ts->getTime();
    cal->setTimeInMillis(ms - (((ms % 1000) + 1000) % 1000));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.", cal->get(Calendar::YEAR),
                  cal->get(Calendar::MONTH) + 1, cal->get(Calendar::DAY_OF_MONTH), cal->get(Calendar::HOUR_OF_DAY),
                  cal->get(Calendar::MINUTE), cal->get(Calendar::SECOND));
    std::string s = buf;
    int32_t nanos = nanosOf(ts);
    if (nanos == 0) {
        s += "0";
    } else {
        char nb[16];
        std::snprintf(nb, sizeof nb, "%09d", nanos);
        std::string n = nb;
        n.erase(n.find_last_not_of('0') + 1);
        s += n;
    }
    return s;
}

std::string fmt(int32_t v) { return std::to_string(v); }
std::string fmt(int64_t v) { return std::to_string(v); }
std::string fmt(int16_t v) { return std::to_string(v); }
std::string fmt(int8_t v) { return std::to_string(static_cast<int>(v)); }
std::string fmt(bool v) { return v ? "true" : "false"; }
std::string fmt(float v) { return std::string(Float::toString(v)); }
std::string fmt(double v) { return std::string(Double::toString(v)); }
std::string fmt(const String& v) { return v.isNull() ? std::string("null") : std::string(v); }
std::string fmt(const char* v) { return v; }
std::string fmt(const std::string& v) { return v; }
std::string fmt(Array<int8_t>* v) { return v == nullptr ? std::string("null") : "bytes:" + hex(v); }
std::string fmt(Timestamp* v) {
    return v == nullptr ? std::string("null") : "ts:" + javaTs(v) + "@" + std::to_string(v->getTime());
}
std::string fmt(Array<int32_t>* v) {
    std::string s = "[";
    for (int32_t i = 0; i < v->length; i++) {
        if (i) s += ", ";
        s += std::to_string((*v)[i]);
    }
    return s + "]";
}
std::string fmt(ResultSet* v) { return v == nullptr ? "null" : "obj"; }
std::string fmt(Object* v) { return v == nullptr ? "null" : "obj"; }

std::string ex(SQLException& e) {
    return "EX[" + fmt(e.getSQLState()) + "|" + std::to_string(e.getErrorCode()) + "|" + fmt(e.getMessage()) + "]";
}

template<class F>
void p(const std::string& label, F f) {
    std::string s;
    try {
        s = fmt(f());
    } catch (BatchUpdateException& e) {
        s = ex(e) + fmt(e.getUpdateCounts());
    } catch (SQLException& e) {
        s = ex(e);
    }
    std::string esc;
    for (char c : s) {
        if (c == '\\') esc += "\\\\";
        else if (c == '\n') esc += "\\n";
        else if (c == '\r') esc += "\\r";
        else if (c == '\0') esc += "\\0";
        else esc += c;
    }
    *g_out += label + " = " + esc + "\n";
}

// Local wall time -> millis (mktime; only used for unambiguous times).
int64_t localMillis(int y, int mo, int d, int h, int mi, int s) {
    struct tm t{};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    t.tm_isdst = -1;
    return static_cast<int64_t>(mktime(&t)) * 1000;
}
Timestamp* tsValueOf(int y, int mo, int d, int h, int mi, int s, int32_t nanos) {
    auto* ts = new Timestamp(localMillis(y, mo, d, h, mi, s) + nanos / 1000000);
    ts->setNanos(nanos);
    return ts;
}

const char* COLS[] = {"ti", "ti1", "si", "i", "bi", "f", "d", "dc", "vc", "tx", "bl",
                      "vb", "dt", "dt6", "ts", "dte", "tm", "yr", "en", "b1", "b8"};

void dumpRow(ResultSet* rs, const std::string& tag) {
    for (const char* cc : COLS) {
        String c(cc);
        std::string l = tag + "." + cc;
        p(l + ".getString", [&] { return rs->getString(c); });
        p(l + ".wasNull", [&] { return rs->wasNull(); });
        p(l + ".getInt", [&] { return rs->getInt(c); });
        p(l + ".getLong", [&] { return rs->getLong(c); });
        p(l + ".getShort", [&] { return rs->getShort(c); });
        p(l + ".getByte", [&] { return rs->getByte(c); });
        p(l + ".getBoolean", [&] { return rs->getBoolean(c); });
        p(l + ".getFloat", [&] { return rs->getFloat(c); });
        p(l + ".getDouble", [&] { return rs->getDouble(c); });
        p(l + ".getBytes", [&] { return rs->getBytes(c); });
        p(l + ".getTimestamp", [&] { return rs->getTimestamp(c); });
    }
}

class NoSuchSavepoint final : public Savepoint {
public:
    int32_t getSavepointId() override { return 0; }
    String getSavepointName() override { return "nosuch"; }
};

void runScenario(const std::string& extra) {
    Connection* admin = DriverManager::getConnection("jdbc:mysql://127.0.0.1:3306/", "aion", "aion");
    admin->createStatement()->executeUpdate("DROP DATABASE IF EXISTS jlang_test_oracle");
    admin->createStatement()->executeUpdate("CREATE DATABASE jlang_test_oracle");
    String url = str("jdbc:mysql://127.0.0.1:3306/jlang_test_oracle?useUnicode=true&characterEncoding=UTF-8", extra);
    Connection* c = DriverManager::getConnection(url, "aion", "aion");
    const char* tz = std::getenv("TZ");
    *g_out += std::string("TZ = ") + (tz ? tz : "") + "\n";
    Statement* st = c->createStatement();
    st->executeUpdate("DROP TABLE IF EXISTS t_types");
    st->executeUpdate(
        "CREATE TABLE t_types (id INT AUTO_INCREMENT PRIMARY KEY, ti TINYINT, ti1 TINYINT(1), si SMALLINT, i INT, bi BIGINT,"
        " f FLOAT, d DOUBLE, dc DECIMAL(10,3), vc VARCHAR(100), tx TEXT, bl BLOB, vb VARBINARY(20), dt DATETIME, dt6 DATETIME(6), ts TIMESTAMP NULL,"
        " dte DATE, tm TIME, yr YEAR, en ENUM('MALE','FEMALE'), b1 BIT(1), b8 BIT(8)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    PreparedStatement* ps = c->prepareStatement(
        "INSERT INTO t_types (ti,ti1,si,i,bi,f,d,dc,vc,tx,bl,vb,dt,dt6,ts,dte,tm,yr,en,b1,b8) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        Statement::RETURN_GENERATED_KEYS);
    ps->setByte(1, -5);
    ps->setBoolean(2, true);
    ps->setShort(3, 1234);
    ps->setInt(4, -2000000000);
    ps->setLong(5, INT64_C(9000000000000000000));
    ps->setFloat(6, 0.1f);
    ps->setDouble(7, 1.0E-5);
    ps->setString(8, "12.5");
    ps->setString(9, "it's \"q\" \\ back\nnl 日本");
    ps->setString(10, "text");
    ps->setBytes(11, Array<int8_t>::of({0, 1, static_cast<int8_t>(0xff), 39, 92, 10}));
    ps->setBytes(12, new Array<int8_t>(0));
    ps->setTimestamp(13, new Timestamp(INT64_C(1308650400123)));
    ps->setTimestamp(14, tsValueOf(2011, 6, 21, 10, 0, 0, 500000000));
    ps->setTimestamp(15, tsValueOf(2011, 1, 2, 3, 4, 5, 0));
    ps->setString(16, "2011-02-03");
    ps->setString(17, "04:05:06");
    ps->setInt(18, 2011);
    ps->setString(19, "FEMALE");
    ps->setBoolean(20, true);
    ps->setInt(21, 200);
    p("insert1.executeUpdate", [&] { return ps->executeUpdate(); });
    ResultSet* gk = ps->getGeneratedKeys();
    p("insert1.gk.next", [&] { return gk->next(); });
    p("insert1.gk.getInt1", [&] { return gk->getInt(1); });
    p("insert1.gk.label", [&] { return gk->getMetaData()->getColumnLabel(1); });
    p("insert1.gk.getLongLabel", [&] { return gk->getLong("GENERATED_KEY"); });
    p("insert1.gk.next2", [&] { return gk->next(); });
    for (int32_t i = 1; i <= 21; i++) ps->setNull(i, Types::NULL_);
    p("insert2.executeUpdate", [&] { return ps->executeUpdate(); });
    p("insert3", [&] {
        return st->executeUpdate(
            "INSERT INTO t_types (ti,ti1,si,i,bi,f,d,dc,vc,tx,bl,vb,dt,dt6,ts,dte,tm,yr,en,b1,b8) VALUES "
            "(127,2,-32768,2147483647,-9223372036854775808,-1.5e30,1e300,-0.5,'true','yes','1','-1','1999-12-31 23:59:59','1999-12-31 23:59:59.999999','2038-01-19 03:14:07','1970-01-01','-838:59:59',1901,'MALE',b'0',b'11111111')");
    });
    p("insert4", [&] {
        return st->executeUpdate(
            "INSERT INTO t_types (ti,ti1,si,i,bi,f,d,dc,vc,tx,bl,vb,dt,dt6,ts,dte,tm,yr,en,b1,b8) VALUES "
            "(-1,-1,0,0,0,0,0,0,'','  42  ','abc','0','0000-00-00 00:00:00','0000-00-00 00:00:00',NULL,'0000-00-00','00:00:00',0,NULL,NULL,b'1')");
    });
    p("insert5", [&] { return st->executeUpdate("INSERT INTO t_types (ti1,vc,tx,bl,vb) VALUES (-2,'no','1.9','9.9','x')"); });
    p("insert6", [&] {
        return st->executeUpdate("INSERT INTO t_types (vc,tx,bl,vb) VALUES ('2147483648','-2147483649','1e3','T')");
    });
    p("insert7", [&] { return st->executeUpdate("INSERT INTO t_types (vc,tx,bl,vb) VALUES ('128','-129','32768','abc')"); });
    ResultSet* rs = st->executeQuery("SELECT * FROM t_types ORDER BY id");
    int row = 0;
    while (rs->next()) {
        row++;
        dumpRow(rs, "row" + std::to_string(row));
    }
    rs->close();

    // navigation
    PreparedStatement* nav = c->prepareStatement("SELECT id, vc AS label, t.i FROM t_types t ORDER BY id",
                                                 ResultSet::TYPE_SCROLL_INSENSITIVE, ResultSet::CONCUR_READ_ONLY);
    ResultSet* r = nav->executeQuery();
    p("nav.getRow0", [&] { return r->getRow(); });
    p("nav.isBeforeFirst", [&] { return r->isBeforeFirst(); });
    p("nav.getIntBeforeFirst", [&] { return r->getInt(1); });
    p("nav.last", [&] { return r->last(); });
    p("nav.getRowLast", [&] { return r->getRow(); });
    p("nav.isLast", [&] { return r->isLast(); });
    p("nav.next", [&] { return r->next(); });
    p("nav.isAfterLast", [&] { return r->isAfterLast(); });
    p("nav.getRowAfter", [&] { return r->getRow(); });
    p("nav.getIntAfter", [&] { return r->getInt(1); });
    p("nav.first", [&] { return r->first(); });
    p("nav.getRowFirst", [&] { return r->getRow(); });
    p("nav.isFirst", [&] { return r->isFirst(); });
    p("nav.absolute3", [&] { return r->absolute(3); });
    p("nav.absolute3.id", [&] { return r->getInt("ID"); });
    p("nav.absolute-1", [&] { return r->absolute(-1); });
    p("nav.absolute-1.row", [&] { return r->getRow(); });
    p("nav.absolute0", [&] { return r->absolute(0); });
    p("nav.absolute99", [&] { return r->absolute(99); });
    p("nav.previous", [&] { return r->previous(); });
    p("nav.previous.row", [&] { return r->getRow(); });
    p("nav.relative-2", [&] { return r->relative(-2); });
    p("nav.relative-2.row", [&] { return r->getRow(); });
    r->beforeFirst();
    p("nav.beforeFirst.next", [&] { return r->next(); });
    p("nav.label", [&] { return r->getString("label"); });
    p("nav.LABEL", [&] { return r->getString("LABEL"); });
    p("nav.vc", [&] { return r->getString("vc"); });
    p("nav.t.i", [&] { return r->getString("t.i"); });
    p("nav.t_types.i", [&] { return r->getString("t_types.i"); });
    p("nav.i", [&] { return r->getString("i"); });
    p("nav.findColumn.label", [&] { return r->findColumn("Label"); });
    p("nav.findColumn.nope", [&] { return r->findColumn("nope"); });
    p("nav.getInt0", [&] { return r->getInt(0); });
    p("nav.getInt4", [&] { return r->getInt(4); });
    p("nav.md.count", [&] { return r->getMetaData()->getColumnCount(); });
    p("nav.md.name2", [&] { return r->getMetaData()->getColumnName(2); });
    p("nav.md.label2", [&] { return r->getMetaData()->getColumnLabel(2); });
    p("nav.md.table3", [&] { return r->getMetaData()->getTableName(3); });
    p("nav.md.type1", [&] { return r->getMetaData()->getColumnType(1); });
    p("nav.md.typename2", [&] { return r->getMetaData()->getColumnTypeName(2); });
    r->close();
    p("nav.closed.next", [&] { return r->next(); });
    p("nav.closed.getInt", [&] { return r->getInt(1); });

    ResultSet* e = st->executeQuery("SELECT * FROM t_types WHERE id < 0");
    p("empty.isBeforeFirst", [&] { return e->isBeforeFirst(); });
    p("empty.next", [&] { return e->next(); });
    p("empty.getRow", [&] { return e->getRow(); });
    p("empty.isAfterLast", [&] { return e->isAfterLast(); });
    p("empty.getInt", [&] { return e->getInt(1); });

    // errors
    p("err.syntax", [&] { return st->executeQuery("SELEC 1"); });
    p("err.unknownTable", [&] { return st->executeQuery("SELECT * FROM nope"); });
    p("err.unknownColumn", [&] { return st->executeQuery("SELECT nope FROM t_types"); });
    p("err.dup", [&] { return st->executeUpdate("INSERT INTO t_types (id) VALUES (1)"); });
    p("err.executeQueryInsert", [&] { return c->prepareStatement("  insert into t_types (id) values (100)")->executeQuery(); });
    p("err.executeQueryComment", [&] { return c->prepareStatement("/* x */ DELETE FROM t_types WHERE id=100")->executeQuery(); });
    p("err.executeUpdateSelect", [&] { return c->prepareStatement("SELECT 1")->executeUpdate(); });
    p("err.executeUpdateSelect2", [&] { return st->executeUpdate("select 1"); });
    p("err.unsetParam", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT ?, ?");
        x->setInt(1, 1);
        return x->executeQuery();
    });
    p("err.paramIndexHigh", [&]() -> Object* {
        PreparedStatement* x = c->prepareStatement("SELECT ?");
        x->setInt(2, 1);
        return nullptr;
    });
    p("err.paramIndexLow", [&]() -> Object* {
        PreparedStatement* x = c->prepareStatement("SELECT ?");
        x->setInt(0, 1);
        return nullptr;
    });
    p("err.commitAuto", [&]() -> Object* {
        c->commit();
        return nullptr;
    });
    p("err.rollbackAuto", [&]() -> Object* {
        c->rollback();
        return nullptr;
    });
    p("err.gkNotRequested", [&] {
        PreparedStatement* x = c->prepareStatement("INSERT INTO t_types (i) VALUES (1)");
        x->executeUpdate();
        return x->getGeneratedKeys();
    });
    p("err.savepointNull", [&] { return static_cast<Object*>(c->setSavepoint(String())); });
    p("err.doubleNaN", [&]() -> Object* {
        PreparedStatement* x = c->prepareStatement("SELECT ?");
        x->setDouble(1, std::numeric_limits<double>::quiet_NaN());
        return nullptr;
    });
    p("err.floatNaN", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT ?");
        x->setFloat(1, std::numeric_limits<float>::quiet_NaN());
        return x->executeQuery();
    });
    p("err.closedStmt", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT 1");
        x->close();
        x->close();
        return x->executeQuery();
    });
    p("err.closedStmtSet", [&]() -> Object* {
        PreparedStatement* x = c->prepareStatement("SELECT ?");
        x->close();
        x->setInt(1, 1);
        return nullptr;
    });
    p("err.rsAfterStmtClose", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT 1");
        ResultSet* y = x->executeQuery();
        x->close();
        return y->next();
    });
    p("err.rsReexec", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT 1");
        ResultSet* y = x->executeQuery();
        x->executeQuery();
        return y->next();
    });

    // parameter substitution / parser
    p("param.quotes", [&] {
        PreparedStatement* x = c->prepareStatement(
            "SELECT '?', \"?\", `id`, ? /* ? */, ? -- ?\n , ? # ?\n , 'a\\'?' FROM t_types WHERE id = 1");
        x->setInt(1, 1);
        x->setString(2, "two");
        x->setNull(3, Types::VARCHAR);
        ResultSet* y = x->executeQuery();
        y->next();
        return str(y->getString(1), "|", y->getString(2), "|", y->getString(3), "|", y->getString(4), "|", y->getString(5),
                   "|", y->getString(6), "|", y->getString(7));
    });
    p("param.floats", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT ?, ?, ?, ?, ?");
        x->setFloat(1, 1.0e10f);
        x->setFloat(2, 1.0e-10f);
        x->setDouble(3, 123456789.0);
        x->setDouble(4, 1.0e-300);
        x->setFloat(5, 3.4028235e38f);
        ResultSet* y = x->executeQuery();
        y->next();
        return str(y->getString(1), "|", y->getString(2), "|", y->getString(3), "|", y->getString(4), "|", y->getString(5),
                   "|", y->getFloat(5), "|", y->getDouble(3));
    });
    p("param.unicode", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT ?, CHAR_LENGTH(?), ?");
        x->setString(1, "éè 中文 Ж");
        x->setString(2, "éè 中文 Ж");
        x->setString(3, String(std::string("\0\032\r", 3)));
        ResultSet* y = x->executeQuery();
        y->next();
        return str(y->getString(1), "|", y->getInt(2), "|", hex(y->getBytes(3)));
    });
    p("param.bytes", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT ?, HEX(?)");
        auto* b = new Array<int8_t>(256);
        for (int i = 0; i < 256; i++) (*b)[i] = static_cast<int8_t>(i);
        x->setBytes(1, b);
        x->setBytes(2, b);
        ResultSet* y = x->executeQuery();
        y->next();
        return str(hex(y->getBytes(1)) == hex(b), "|", y->getString(2).length());
    });
    p("param.nullString", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT ? IS NULL, ? IS NULL");
        x->setString(1, String());
        x->setTimestamp(2, nullptr);
        ResultSet* y = x->executeQuery();
        y->next();
        return str(y->getInt(1), "|", y->getInt(2));
    });
    p("param.clear", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT ?");
        x->setInt(1, 5);
        x->clearParameters();
        return x->executeQuery();
    });
    p("param.ts", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT CAST(? AS CHAR)");
        x->setTimestamp(1, new Timestamp(INT64_C(1308650400999)));
        ResultSet* y = x->executeQuery();
        y->next();
        return y->getString(1);
    });
    p("param.expr", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT ? + ?, ? - ?");
        x->setInt(1, 5);
        x->setInt(2, -3);
        x->setLong(3, 5);
        x->setLong(4, -3);
        ResultSet* y = x->executeQuery();
        y->next();
        return str(y->getString(1), "|", y->getString(2));
    });

    p("expr.types", [&] {
        ResultSet* y = st->executeQuery(
            "SELECT 1, 1.5, 'x', NOW() IS NOT NULL, NULL, COUNT(*), 1=1, CAST(1 AS UNSIGNED), 18446744073709551615");
        y->next();
        ResultSetMetaData* m = y->getMetaData();
        std::string s;
        for (int32_t i = 1; i <= m->getColumnCount(); i++) {
            s += fmt(m->getColumnLabel(i)) + ":" + std::to_string(m->getColumnType(i)) + ":" + fmt(y->getString(i)) + " ";
        }
        return s;
    });
    p("expr.bigUnsignedLong", [&] {
        ResultSet* y = st->executeQuery("SELECT 18446744073709551615");
        y->next();
        return y->getLong(1);
    });
    p("expr.bigUnsignedDouble", [&] {
        ResultSet* y = st->executeQuery("SELECT 18446744073709551615");
        y->next();
        return y->getDouble(1);
    });
    p("expr.bool", [&] {
        ResultSet* y = st->executeQuery("SELECT 1=1, 1=0, 2, -1, 0.5, 1.5, 'Y', 'N', NULL");
        y->next();
        std::string s;
        for (int32_t i = 1; i <= 9; i++) s += fmt(y->getBoolean(i)) + ",";
        return s;
    });

    p("gk.multi", [&] {
        PreparedStatement* x = c->prepareStatement("INSERT INTO t_types (i) VALUES (1),(2),(3)", Statement::RETURN_GENERATED_KEYS);
        int32_t n = x->executeUpdate();
        ResultSet* y = x->getGeneratedKeys();
        std::string s = std::to_string(n) + ":";
        while (y->next()) s += std::to_string(y->getLong(1)) + ",";
        return s;
    });
    p("gk.none", [&] {
        PreparedStatement* x = c->prepareStatement("UPDATE t_types SET i = 7 WHERE id = -1", Statement::RETURN_GENERATED_KEYS);
        int32_t n = x->executeUpdate();
        ResultSet* y = x->getGeneratedKeys();
        std::string s = std::to_string(n) + ":";
        while (y->next()) s += std::to_string(y->getLong(1)) + ",";
        return s;
    });
    p("gk.execute", [&] {
        PreparedStatement* x = c->prepareStatement("INSERT INTO t_types (i) VALUES (9)", Statement::RETURN_GENERATED_KEYS);
        bool b = x->execute();
        ResultSet* y = x->getGeneratedKeys();
        std::string s = fmt(b) + ":" + std::to_string(x->getUpdateCount()) + ":";
        while (y->next()) s += std::to_string(y->getLong(1)) + ",";
        return s;
    });

    p("exec.select", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT 1");
        bool b = x->execute();
        return fmt(b) + ":" + std::to_string(x->getUpdateCount()) + ":" + fmt(x->getResultSet() != nullptr);
    });
    p("exec.update", [&] {
        PreparedStatement* x = c->prepareStatement("UPDATE t_types SET i = i WHERE id > 0");
        bool b = x->execute();
        return fmt(b) + ":" + std::to_string(x->getUpdateCount()) + ":" + fmt(x->getResultSet() != nullptr);
    });
    p("exec.updateChanged", [&] { return c->prepareStatement("UPDATE t_types SET i = 77 WHERE id = 1")->executeUpdate(); });
    p("exec.updateUnchanged", [&] { return c->prepareStatement("UPDATE t_types SET i = 77 WHERE id = 1")->executeUpdate(); });
    p("exec.dupKeyUpdate", [&] {
        return c->prepareStatement("INSERT INTO t_types (id, i) VALUES (1, 78) ON DUPLICATE KEY UPDATE i = 78")->executeUpdate();
    });
    p("exec.set", [&] { return c->prepareStatement("SET @a = 1")->executeQuery()->next(); });

    p("batch", [&] {
        PreparedStatement* x = c->prepareStatement("INSERT INTO t_types (id, i) VALUES (?, ?)");
        x->setInt(1, 1000);
        x->setInt(2, 1);
        x->addBatch();
        x->setInt(1, 1001);
        x->setInt(2, 2);
        x->addBatch();
        return x->executeBatch();
    });
    p("batch.err", [&] {
        PreparedStatement* x = c->prepareStatement("INSERT INTO t_types (id, i) VALUES (?, ?)");
        x->setInt(1, 1002);
        x->setInt(2, 1);
        x->addBatch();
        x->setInt(1, 1000);
        x->setInt(2, 2);
        x->addBatch();
        x->setInt(1, 1003);
        x->setInt(2, 2);
        x->addBatch();
        return x->executeBatch();
    });
    p("batch.after", [&] {
        ResultSet* y = st->executeQuery("SELECT COUNT(*) FROM t_types WHERE id IN (1002, 1003)");
        y->next();
        return y->getInt(1);
    });
    p("batch.empty", [&] { return c->prepareStatement("SELECT 1")->executeBatch(); });
    p("batch.stmt", [&] {
        Statement* x = c->createStatement();
        x->addBatch("UPDATE t_types SET i = 1 WHERE id = 1000");
        x->addBatch("DELETE FROM t_types WHERE id = 1001");
        return x->executeBatch();
    });

    c->setAutoCommit(false);
    p("tx.autocommit", [&] { return c->getAutoCommit(); });
    st->executeUpdate("INSERT INTO t_types (id, i) VALUES (2000, 1)");
    Savepoint* sp = c->setSavepoint("sp1");
    p("tx.sp.name", [&] { return sp->getSavepointName(); });
    p("tx.sp.id", [&] { return sp->getSavepointId(); });
    st->executeUpdate("INSERT INTO t_types (id, i) VALUES (2001, 1)");
    c->rollback(sp);
    p("tx.afterRollbackSp", [&] {
        ResultSet* y = st->executeQuery("SELECT COUNT(*) FROM t_types WHERE id IN (2000, 2001)");
        y->next();
        return y->getInt(1);
    });
    c->releaseSavepoint(sp);
    p("tx.rollbackReleased", [&] {
        c->rollback(sp);
        return "ok";
    });
    c->commit();
    Savepoint* sp2 = c->setSavepoint();
    p("tx.unnamed.id", [&] { return sp2->getSavepointId(); });
    st->executeUpdate("INSERT INTO t_types (id, i) VALUES (2002, 1)");
    c->rollback();
    c->setAutoCommit(true);
    p("tx.final", [&] {
        ResultSet* y = st->executeQuery("SELECT COUNT(*) FROM t_types WHERE id IN (2000, 2001, 2002)");
        y->next();
        return y->getInt(1);
    });
    p("tx.rollbackBadSp", [&] {
        c->setAutoCommit(false);
        JFINALLY { c->setAutoCommit(true); };
        c->rollback(new NoSuchSavepoint());
        return "ok";
    });

    c->setReadOnly(true);
    p("ro.isReadOnly", [&] { return c->isReadOnly(); });
    p("ro.select", [&] {
        ResultSet* y = c->prepareStatement("SELECT 1")->executeQuery();
        y->next();
        return y->getInt(1);
    });
    p("ro.executeUpdate", [&] { return c->prepareStatement("UPDATE t_types SET i = 1 WHERE id = -1")->executeUpdate(); });
    p("ro.execute", [&] { return c->prepareStatement("UPDATE t_types SET i = 1 WHERE id = -1")->execute(); });
    p("ro.executeSelect", [&] { return c->prepareStatement("SELECT 1")->execute(); });
    c->setReadOnly(false);

    p("zero.string", [&] {
        ResultSet* y = st->executeQuery("SELECT dt, dte FROM t_types WHERE id = 4");
        y->next();
        String a = y->getString(1);
        bool wn = y->wasNull();
        return str(a, "|", wn, "|", y->getString(2));
    });
    p("zero.ts", [&] {
        ResultSet* y = st->executeQuery("SELECT dt FROM t_types WHERE id = 4");
        y->next();
        return y->getTimestamp(1);
    });
    p("zero.obj", [&] {
        ResultSet* y = st->executeQuery("SELECT dt FROM t_types WHERE id = 4");
        y->next();
        Object* o = y->getObject(1);
        return o == nullptr ? std::string("null") : fmt(jlang::cast<Timestamp>(o));
    });

    p("dst.gap", [&] {
        ResultSet* y = st->executeQuery("SELECT CAST('2011-03-27 02:30:00' AS DATETIME)");
        y->next();
        return y->getTimestamp(1);
    });
    p("dst.overlap", [&] {
        ResultSet* y = st->executeQuery("SELECT CAST('2011-10-30 02:30:00' AS DATETIME)");
        y->next();
        return y->getTimestamp(1);
    });
    p("dst.set", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT CAST(? AS CHAR)");
        x->setTimestamp(1, new Timestamp(INT64_C(1301189400000)));
        ResultSet* y = x->executeQuery();
        y->next();
        return y->getString(1);
    });

    st->executeUpdate("DROP PROCEDURE IF EXISTS p_twice");
    st->executeUpdate(
        "CREATE PROCEDURE p_twice(IN x INT) BEGIN SELECT x * 2 AS doubled, 'a' AS s; SELECT x + 1 AS inc; END");
    p("call.escape", [&] {
        CallableStatement* x = c->prepareCall("{call p_twice(?)}");
        x->setInt(1, 21);
        ResultSet* y = x->executeQuery();
        y->next();
        std::string s = std::to_string(y->getInt("doubled"));
        s += "|" + fmt(y->getString("s"));
        s += "|" + fmt(x->getMoreResults());
        ResultSet* z = x->getResultSet();
        z->next();
        s += "|" + std::to_string(z->getInt(1));
        s += "|" + fmt(x->getMoreResults());
        s += "|" + std::to_string(x->getUpdateCount());
        s += "|" + fmt(x->getMoreResults());
        s += "|" + std::to_string(x->getUpdateCount());
        return s;
    });
    p("call.plain", [&] {
        CallableStatement* x = c->prepareCall("CALL p_twice(?)");
        x->setInt(1, 5);
        bool b = x->execute();
        ResultSet* y = x->getResultSet();
        y->next();
        return fmt(b) + "|" + std::to_string(y->getInt(1));
    });
    p("call.afterwards", [&] {
        ResultSet* y = st->executeQuery("SELECT 7");
        y->next();
        return y->getInt(1);
    });
    p("dao.union", [&] {
        PreparedStatement* x = c->prepareStatement(
            "SELECT `id` AS `id` FROM t_types WHERE id < 3 UNION SELECT i FROM t_types WHERE id = 3",
            ResultSet::TYPE_SCROLL_INSENSITIVE, ResultSet::CONCUR_READ_ONLY);
        ResultSet* y = x->executeQuery();
        y->last();
        int32_t n = y->getRow();
        y->beforeFirst();
        std::string s = std::to_string(n) + ":";
        for (int32_t i = 0; i < n; i++) {
            y->next();
            s += std::to_string(y->getInt("id")) + ",";
        }
        return s;
    });
    p("dao.count", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT count(id) as bkcount FROM t_types WHERE ? = vc AND id > ?");
        x->setString(1, "no");
        x->setInt(2, 0);
        ResultSet* y = x->executeQuery();
        y->next();
        return y->getInt("bkcount");
    });
    p("dao.stmtGetConnection", [&] {
        PreparedStatement* x = c->prepareStatement("SELECT 1");
        bool same = x->getConnection() == c;
        x->close();
        return fmt(same) + "|" + fmt(x->isClosed());
    });

    p("conn.isValid", [&] { return c->isValid(1); });
    p("conn.isClosed", [&] { return c->isClosed(); });
    st->executeUpdate("DROP TABLE t_types");
    c->close();
    p("conn.isClosedAfter", [&] { return c->isClosed(); });
    p("conn.isValidAfter", [&] { return c->isValid(1); });
    p("conn.closeTwice", [&] {
        c->close();
        return "ok";
    });
    p("conn.useAfterClose", [&] { return static_cast<Object*>(c->prepareStatement("SELECT 1")); });
    p("stmt.useAfterConnClose", [&] { return st->executeQuery("SELECT 1"); });
    admin->createStatement()->executeUpdate("DROP DATABASE jlang_test_oracle");
    admin->close();
}

// Lines whose Connector/J output is not reproduced, with the reason (see Sql.h / the report).
const std::set<std::string>& knownDeviations() {
    static const std::set<std::string> s = {
        // Binary data as text: Connector/J decodes BLOB/BIT bytes (and, in "Cannot convert
        // value" messages, all bytes) as ISO-8859-1/Cp1252; jlang keeps the raw bytes / UTF-8.
        "row1.bl.getString", "row1.bl.getInt", "row1.bl.getLong", "row1.bl.getShort", "row1.bl.getByte",
        "row1.bl.getFloat", "row1.bl.getDouble", "row1.bl.getTimestamp", "row1.b8.getTimestamp",
        "row3.b8.getTimestamp", "row1.vc.getTimestamp",
        // Connector/J 5.1.13 mangles 6-digit fractions in getTimestamp (reads nanos 483647);
        // jlang returns the correct 999999000.
        "row3.dt6.getTimestamp",
    };
    return s;
}

std::map<std::string, std::string> parseLines(const std::string& text) {
    std::map<std::string, std::string> m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find(" = ");
        if (eq == std::string::npos) continue;
        m[line.substr(0, eq)] = line.substr(eq + 3);
    }
    return m;
}

bool serverAvailable() {
    try {
        DriverManager::getConnection("jdbc:mysql://127.0.0.1:3306/", "aion", "aion")->close();
        return true;
    } catch (SQLException& e) {
        std::fprintf(stderr, "  (skipped: MariaDB not reachable: %s)\n", std::string(e.getMessage()).c_str());
        return false;
    }
}

void compareRun(const char* tz, const std::string& extra, const char* expected) {
    std::string savedTz = std::getenv("TZ") ? std::getenv("TZ") : "";
    bool hadTz = std::getenv("TZ") != nullptr;
    setenv("TZ", tz, 1);
    tzset();
    TimeZone::setDefault(nullptr);  // re-read $TZ
    std::string out;
    g_out = &out;
    runScenario(extra);
    g_out = nullptr;
    if (hadTz) setenv("TZ", savedTz.c_str(), 1);
    else unsetenv("TZ");
    tzset();
    TimeZone::setDefault(nullptr);
    auto want = parseLines(expected);
    auto got = parseLines(out);
    int mismatches = 0;
    for (auto& [k, v] : want) {
        if (k == "TZ") continue;
        if (knownDeviations().count(k) != 0) {
            auto it = got.find(k);
            if (std::getenv("JLANG_SQL_ORACLE_VERBOSE") != nullptr) {
                std::fprintf(stderr, "  deviation %s %s\n      java: %s\n     jlang: %s\n", k.c_str(),
                             it != got.end() && it->second == v ? "(matches)" : "", v.c_str(),
                             it != got.end() ? it->second.c_str() : "<missing>");
            }
            continue;
        }
        auto it = got.find(k);
        if (it == got.end()) {
            jtest::fail(__FILE__, __LINE__, std::string(tz) + extra + ": missing " + k);
            mismatches++;
        } else if (it->second != v) {
            jtest::fail(__FILE__, __LINE__, std::string(tz) + extra + ": " + k + "\n      java: " + v + "\n     jlang: " + it->second);
            mismatches++;
        }
    }
    for (auto& [k, v] : got) {
        if (want.find(k) == want.end()) jtest::fail(__FILE__, __LINE__, "unexpected line " + k);
    }
    std::fprintf(stderr, "  %s%s: %zu lines compared, %d mismatches\n", tz, extra.c_str(), want.size(), mismatches);
}

}  // namespace

JTEST(SqlOracleConnectorJ) {
    if (!serverAvailable()) return;
    compareRun("UTC", "&zeroDateTimeBehavior=exception", kOracleUtc);
    compareRun("Europe/Berlin", "&zeroDateTimeBehavior=exception", kOracleBerlin);
    compareRun("UTC", "&zeroDateTimeBehavior=convertToNull", kOracleUtcCtn);
    compareRun("Europe/Berlin", "&zeroDateTimeBehavior=convertToNull", kOracleBerlinCtn);
}

#endif  // __has_include(<jlang/Time.h>)
