// jlang/tests/test_sql.cpp - jlang/Sql.h against a real MariaDB server.
//
// Server: 127.0.0.1:3306, user aion / password aion with all privileges (override with the
// JLANG_SQL_URL / JLANG_SQL_USER / JLANG_SQL_PASSWORD environment variables; the URL must end
// with '/'). Every test creates its own database (jlang_test_*) and drops it at the end. When
// the server is unreachable the tests print a note and pass.
#include "jtest.h"

#include <jlang/Sql.h>

#if __has_include(<jlang/Time.h>)  // the tests need java.sql.Timestamp
#include <jlang/Time.h>

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <limits>
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

std::string env(const char* name, const char* def) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' ? std::string(v) : std::string(def);
}
String baseUrl() { return String(env("JLANG_SQL_URL", "jdbc:mysql://127.0.0.1:3306/")); }
String user() { return String(env("JLANG_SQL_USER", "aion")); }
String password() { return String(env("JLANG_SQL_PASSWORD", "aion")); }

bool serverUp() {
    static int state = -1;
    if (state < 0) {
        try {
            DriverManager::getConnection(baseUrl(), user(), password())->close();
            state = 1;
        } catch (SQLException& e) {
            std::fprintf(stderr, "  (MariaDB not reachable, SQL tests skipped: %s)\n", std::string(e.getMessage()).c_str());
            state = 0;
        }
    }
    return state == 1;
}

void adminExec(const String& sql) {
    Connection* c = DriverManager::getConnection(baseUrl(), user(), password());
    JFINALLY { c->close(); };
    c->createStatement()->executeUpdate(sql);
}

// A scratch database for one test.
struct TestDb {
    String name;
    explicit TestDb(const char* n) : name(str("jlang_test_", n)) {
        adminExec(str("DROP DATABASE IF EXISTS ", name));
        adminExec(str("CREATE DATABASE ", name, " DEFAULT CHARACTER SET utf8mb4"));
    }
    ~TestDb() {
        try {
            adminExec(str("DROP DATABASE IF EXISTS ", name));
        } catch (...) {
        }
    }
    String url(const char* params = "") const { return str(baseUrl(), name, "?useUnicode=true&characterEncoding=UTF-8", params); }
    Connection* connect(const char* params = "") const { return DriverManager::getConnection(url(params), user(), password()); }
};

struct ThreadArgs : public virtual Object {
    std::function<void()> fn;
};
void runThread(void* a) {
    static_cast<ThreadArgs*>(a)->fn();
}
uint64_t spawn(std::function<void()> fn) {
    auto* a = new ThreadArgs();
    a->fn = std::move(fn);
    return gc::startNativeThread(&runThread, a);
}

// Switches the default time zone for both the C library (test helpers) and jlang
// (TimeZone::getDefault re-reads $TZ after setDefault(nullptr)).
void setTz(const char* tz) {
    if (tz == nullptr) unsetenv("TZ");
    else setenv("TZ", tz, 1);
    tzset();
    TimeZone::setDefault(nullptr);
}

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

int32_t countRows(Connection* c, const String& sql) {
    ResultSet* rs = c->createStatement()->executeQuery(sql);
    rs->next();
    return rs->getInt(1);
}

std::string state(SQLException& e) {
    return e.getSQLState().isNull() ? std::string("null") : std::string(e.getSQLState());
}

}  // namespace

// ---------------------------------------------------------------------------------------
JTEST(SqlCrudAllTypes) {
    if (!serverUp()) return;
    TestDb db("crud");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    Statement* st = c->createStatement();
    st->executeUpdate(
        "CREATE TABLE t (id INT PRIMARY KEY, b TINYINT(1), by8 TINYINT, sh SMALLINT, i INT, l BIGINT, f FLOAT, d DOUBLE,"
        " s VARCHAR(100), bl BLOB, ts TIMESTAMP NULL, dt DATETIME, e ENUM('MALE','FEMALE'), n INT)");
    PreparedStatement* ps = c->prepareStatement("INSERT INTO t VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    JCHECK_EQ(ps->getParameterCount(), 14);
    ps->setInt(1, 1);
    ps->setBoolean(2, true);
    ps->setByte(3, static_cast<int8_t>(-128));
    ps->setShort(4, static_cast<int16_t>(-32768));
    ps->setInt(5, INT32_MIN);
    ps->setLong(6, INT64_MAX);
    ps->setFloat(7, 1.25f);
    ps->setDouble(8, -2.5e-7);
    ps->setString(9, "hello");
    ps->setBytes(10, Array<int8_t>::of({1, 2, 3}));
    ps->setTimestamp(11, new Timestamp(localMillis(2011, 6, 21, 10, 0, 0)));
    ps->setTimestamp(12, new Timestamp(localMillis(2010, 1, 1, 2, 0, 0)));
    ps->setString(13, "FEMALE");
    ps->setNull(14, Types::INTEGER);
    JCHECK_EQ(ps->executeUpdate(), 1);
    // row 2: everything NULL
    ps->setInt(1, 2);
    ps->setNull(2, Types::BIT);
    ps->setNull(3, Types::TINYINT);
    ps->setNull(4, Types::SMALLINT);
    ps->setNull(5, Types::INTEGER);
    ps->setNull(6, Types::BIGINT);
    ps->setNull(7, Types::FLOAT);
    ps->setNull(8, Types::DOUBLE);
    ps->setString(9, String());
    ps->setBytes(10, nullptr);
    ps->setTimestamp(11, nullptr);
    ps->setNull(12, Types::TIMESTAMP);
    ps->setString(13, String());
    ps->setObject(14, nullptr);
    JCHECK_EQ(ps->executeUpdate(), 1);
    ps->close();

    ResultSet* rs = st->executeQuery("SELECT * FROM t ORDER BY id");
    JCHECK(rs->next());
    JCHECK_EQ(rs->getInt("id"), 1);
    JCHECK_EQ(rs->getBoolean("b"), true);
    JCHECK_EQ(rs->getByte("by8"), static_cast<int8_t>(-128));
    JCHECK_EQ(rs->getShort("sh"), static_cast<int16_t>(-32768));
    JCHECK_EQ(rs->getInt("i"), INT32_MIN);
    JCHECK_EQ(rs->getLong("l"), INT64_MAX);
    JCHECK_EQ(rs->getFloat("f"), 1.25f);
    JCHECK_EQ(rs->getDouble("d"), -2.5e-7);
    JCHECK_EQ(rs->getString("s"), String("hello"));
    Array<int8_t>* bytes = rs->getBytes("bl");
    JCHECK(bytes != nullptr && bytes->length == 3 && (*bytes)[2] == 3);
    JCHECK_EQ(rs->getTimestamp("ts")->getTime(), localMillis(2011, 6, 21, 10, 0, 0));
    JCHECK_EQ(rs->getTimestamp("dt")->getTime(), localMillis(2010, 1, 1, 2, 0, 0));
    JCHECK_EQ(rs->getString("dt"), String("2010-01-01 02:00:00.0"));
    JCHECK_EQ(rs->getString("e"), String("FEMALE"));
    JCHECK_EQ(rs->getInt("n"), 0);
    JCHECK(rs->wasNull());
    // by index
    JCHECK_EQ(rs->getInt(5), INT32_MIN);
    JCHECK(!rs->wasNull());
    JCHECK_EQ(rs->getString(9), String("hello"));
    // getObject
    JCHECK(instanceof<Integer>(rs->getObject("i")));
    JCHECK(instanceof<Long>(rs->getObject("l")));
    JCHECK(instanceof<Timestamp>(rs->getObject("dt")));
    JCHECK(rs->getObject("n") == nullptr);

    JCHECK(rs->next());
    JCHECK_EQ(rs->getBoolean("b"), false);
    JCHECK(rs->wasNull());
    JCHECK_EQ(rs->getByte("by8"), static_cast<int8_t>(0));
    JCHECK(rs->wasNull());
    JCHECK_EQ(rs->getShort("sh"), static_cast<int16_t>(0));
    JCHECK_EQ(rs->getInt("i"), 0);
    JCHECK(rs->wasNull());
    JCHECK_EQ(rs->getLong("l"), INT64_C(0));
    JCHECK_EQ(rs->getFloat("f"), 0.0f);
    JCHECK_EQ(rs->getDouble("d"), 0.0);
    JCHECK(rs->getString("s").isNull());
    JCHECK(rs->wasNull());
    JCHECK(rs->getBytes("bl") == nullptr);
    JCHECK(rs->getTimestamp("ts") == nullptr);
    JCHECK(rs->wasNull());
    JCHECK(rs->getTimestamp("dt") == nullptr);
    JCHECK(rs->getString("e").isNull());
    JCHECK(!rs->next());
    rs->close();

    // update / delete counts (matched rows, like Connector/J)
    JCHECK_EQ(st->executeUpdate("UPDATE t SET s = 'x' WHERE id > 0"), 2);
    JCHECK_EQ(st->executeUpdate("UPDATE t SET s = 'x' WHERE id > 0"), 2);
    JCHECK_EQ(st->executeUpdate("DELETE FROM t WHERE id = 2"), 1);
    JCHECK_EQ(countRows(c, "SELECT COUNT(*) FROM t"), 1);
    // setObject with boxes
    PreparedStatement* po = c->prepareStatement("SELECT ?, ?, ?, ?, ?, ?");
    po->setObject(1, Integer::valueOf(42));
    po->setObject(2, Long::valueOf(INT64_C(-7)));
    po->setObject(3, Boolean::valueOf(true));
    po->setObject(4, Double::valueOf(0.5));
    po->setObject(5, box(String("o'k")));
    po->setObject(6, new Timestamp(localMillis(2012, 12, 21, 12, 0, 0)));
    ResultSet* r2 = po->executeQuery();
    JCHECK(r2->next());
    JCHECK_EQ(r2->getInt(1), 42);
    JCHECK_EQ(r2->getLong(2), INT64_C(-7));
    JCHECK_EQ(r2->getInt(3), 1);
    JCHECK_EQ(r2->getDouble(4), 0.5);
    JCHECK_EQ(r2->getString(5), String("o'k"));
    JCHECK_EQ(r2->getString(6), String("2012-12-21 12:00:00"));
}

// ---------------------------------------------------------------------------------------
JTEST(SqlEscapingAndParameters) {
    if (!serverUp()) return;
    TestDb db("escape");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    c->createStatement()->executeUpdate("CREATE TABLE t (id INT AUTO_INCREMENT PRIMARY KEY, s TEXT, b BLOB) CHARSET utf8mb4");
    std::vector<String> samples = {
        String("plain"),
        String("it's"),
        String("double \"quote\""),
        String("back\\slash\\"),
        String("trailing backslash \\"),
        String(std::string("nul\0byte", 8)),
        String("cr\rlf\n"),
        String("ctrl-z \x1a end"),
        String("percent % underscore _"),
        String("'; DROP TABLE t; -- "),
        String("question ? mark"),
        String("unicode éè 中文 ЖИ \xF0\x9F\x98\x80 emoji"),
        String(""),
    };
    PreparedStatement* ins = c->prepareStatement("INSERT INTO t (s, b) VALUES (?, ?)");
    for (auto& s : samples) {
        ins->setString(1, s);
        ins->setBytes(2, s.getBytes());
        JCHECK_EQ(ins->executeUpdate(), 1);
    }
    ResultSet* rs = c->createStatement()->executeQuery("SELECT s, b, LENGTH(s) FROM t ORDER BY id");
    for (auto& s : samples) {
        JCHECK(rs->next());
        JCHECK_EQ(rs->getString(1), s);
        Array<int8_t>* b = rs->getBytes(2);
        JCHECK(b != nullptr && String(b) == s);
        JCHECK_EQ(rs->getInt(3), s.length());
    }
    JCHECK(!rs->next());
    // All 256 byte values through setBytes and a WHERE comparison.
    auto* all = new Array<int8_t>(256);
    for (int i = 0; i < 256; i++) (*all)[i] = static_cast<int8_t>(i);
    PreparedStatement* pb = c->prepareStatement("INSERT INTO t (s, b) VALUES ('bin', ?)");
    pb->setBytes(1, all);
    pb->executeUpdate();
    PreparedStatement* sel = c->prepareStatement("SELECT COUNT(*) FROM t WHERE b = ?");
    sel->setBytes(1, all);
    ResultSet* r2 = sel->executeQuery();
    JCHECK(r2->next());
    JCHECK_EQ(r2->getInt(1), 1);
    // '?' inside literals, identifiers and comments is not a placeholder.
    PreparedStatement* q = c->prepareStatement(
        "SELECT '?' AS a, \"?\" AS `b?`, ? AS c /* ? */ -- ?\n, ? # ?\n FROM t WHERE s = 'bin'");
    JCHECK_EQ(q->getParameterCount(), 2);
    q->setString(1, "one");
    q->setInt(2, 2);
    ResultSet* r3 = q->executeQuery();
    JCHECK(r3->next());
    JCHECK_EQ(r3->getString("a"), String("?"));
    JCHECK_EQ(r3->getString("b?"), String("?"));
    JCHECK_EQ(r3->getString("c"), String("one"));
    JCHECK_EQ(r3->getInt(4), 2);
    // Parameter errors.
    PreparedStatement* p2 = c->prepareStatement("SELECT ?");
    try {
        p2->setInt(2, 1);
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("S1009"));
        JCHECK_EQ(e.getMessage(), String("Parameter index out of range (2 > number of parameters, which is 1)."));
    }
    try {
        p2->executeQuery();
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("07001"));
    }
    try {
        p2->setDouble(1, std::numeric_limits<double>::infinity());
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(e.getMessage(), String("'Infinity' is not a valid numeric or approximate numeric value"));
    }
}

JTEST(SqlNoBackslashEscapesMode) {
    if (!serverUp()) return;
    TestDb db("nobackslash");
    Connection* c = db.connect("&sessionVariables=sql_mode='NO_BACKSLASH_ESCAPES,STRICT_TRANS_TABLES'");
    JFINALLY { c->close(); };
    c->createStatement()->executeUpdate("CREATE TABLE t (s VARCHAR(100))");
    PreparedStatement* ps = c->prepareStatement("INSERT INTO t VALUES (?)");
    String tricky("a'b\\c\"d\\");
    ps->setString(1, tricky);
    ps->executeUpdate();
    ResultSet* rs = c->createStatement()->executeQuery("SELECT s, '\\' AS lit FROM t");
    JCHECK(rs->next());
    JCHECK_EQ(rs->getString(1), tricky);
    JCHECK_EQ(rs->getString("lit"), String("\\"));
}

// ---------------------------------------------------------------------------------------
JTEST(SqlBatch) {
    if (!serverUp()) return;
    TestDb db("batch");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    c->createStatement()->executeUpdate("CREATE TABLE f (a INT, b INT, PRIMARY KEY (a, b))");
    // MySQL5FriendListDAO.addFriends
    PreparedStatement* stmt = c->prepareStatement("INSERT INTO f (a, b) VALUES (?, ?)");
    stmt->setInt(1, 1);
    stmt->setInt(2, 2);
    stmt->addBatch();
    stmt->setInt(1, 2);
    stmt->setInt(2, 1);
    stmt->addBatch();
    Array<int32_t>* counts = stmt->executeBatch();
    JCHECK_EQ(counts->length, 2);
    JCHECK_EQ((*counts)[0], 1);
    JCHECK_EQ((*counts)[1], 1);
    JCHECK_EQ(stmt->executeBatch()->length, 0);  // batch cleared
    // Failure in the middle: continueBatchOnError (default) runs the rest.
    stmt->setInt(1, 3);
    stmt->setInt(2, 3);
    stmt->addBatch();
    stmt->setInt(1, 1);
    stmt->setInt(2, 2);  // duplicate
    stmt->addBatch();
    stmt->setInt(1, 4);
    stmt->setInt(2, 4);
    stmt->addBatch();
    try {
        stmt->executeBatch();
        JCHECK(false);
    } catch (BatchUpdateException& e) {
        JCHECK_EQ(e.getErrorCode(), 1062);
        Array<int32_t>* uc = e.getUpdateCounts();
        JCHECK_EQ(uc->length, 3);
        JCHECK_EQ((*uc)[0], 1);
        JCHECK_EQ((*uc)[1], Statement::EXECUTE_FAILED);
        JCHECK_EQ((*uc)[2], 1);
    }
    JCHECK_EQ(countRows(c, "SELECT COUNT(*) FROM f"), 4);
    // continueBatchOnError=false: stops at the error with the counts so far.
    Connection* c2 = db.connect("&continueBatchOnError=false");
    JFINALLY { c2->close(); };
    PreparedStatement* s2 = c2->prepareStatement("INSERT INTO f (a, b) VALUES (?, ?)");
    s2->setInt(1, 5);
    s2->setInt(2, 5);
    s2->addBatch();
    s2->setInt(1, 1);
    s2->setInt(2, 2);
    s2->addBatch();
    s2->setInt(1, 6);
    s2->setInt(2, 6);
    s2->addBatch();
    try {
        s2->executeBatch();
        JCHECK(false);
    } catch (BatchUpdateException& e) {
        JCHECK_EQ(e.getUpdateCounts()->length, 1);
    }
    JCHECK_EQ(countRows(c, "SELECT COUNT(*) FROM f WHERE a = 6"), 0);
    // Statement batch with generated keys.
    c->createStatement()->executeUpdate("CREATE TABLE g (id INT AUTO_INCREMENT PRIMARY KEY, v INT)");
    Statement* sb = c->createStatement();
    sb->addBatch("INSERT INTO g (v) VALUES (1)");
    sb->addBatch("INSERT INTO g (v) VALUES (2), (3)");
    sb->addBatch("UPDATE g SET v = v + 1");
    Array<int32_t>* c3 = sb->executeBatch();
    JCHECK_EQ(c3->length, 3);
    JCHECK_EQ((*c3)[1], 2);
    JCHECK_EQ((*c3)[2], 3);
    ResultSet* keys = sb->getGeneratedKeys();
    std::string k;
    while (keys->next()) k += std::to_string(keys->getInt(1)) + ",";
    JCHECK_EQ(k, std::string("1,2,3,"));
}

// ---------------------------------------------------------------------------------------
JTEST(SqlTransactionsAndSavepoints) {
    if (!serverUp()) return;
    TestDb db("tx");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    Statement* st = c->createStatement();
    st->executeUpdate("CREATE TABLE t (id INT PRIMARY KEY) ENGINE=InnoDB");
    JCHECK(c->getAutoCommit());
    try {
        c->commit();
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(e.getMessage(), String("Can't call commit when autocommit=true"));
    }
    // commons Transaction: setAutoCommit(false) ... commit(); setAutoCommit(true)
    c->setAutoCommit(false);
    JCHECK(!c->getAutoCommit());
    st->executeUpdate("INSERT INTO t VALUES (1)");
    Savepoint* sp = c->setSavepoint("before_two");
    JCHECK_EQ(sp->getSavepointName(), String("before_two"));
    st->executeUpdate("INSERT INTO t VALUES (2)");
    c->rollback(sp);
    c->releaseSavepoint(sp);  // no-op (Connector/J 5.1)
    Savepoint* anon = c->setSavepoint();
    JCHECK(anon->getSavepointName().length() > 0);
    st->executeUpdate("INSERT INTO t VALUES (3)");
    c->rollback(anon);
    c->commit();
    JCHECK_EQ(countRows(c, "SELECT COUNT(*) FROM t"), 1);
    st->executeUpdate("INSERT INTO t VALUES (4)");
    // Visible to another session only after commit.
    Connection* other = db.connect();
    JFINALLY { other->close(); };
    JCHECK_EQ(countRows(other, "SELECT COUNT(*) FROM t"), 1);
    c->rollback();
    JCHECK_EQ(countRows(c, "SELECT COUNT(*) FROM t"), 1);
    st->executeUpdate("INSERT INTO t VALUES (5)");
    c->setAutoCommit(true);  // commits the open transaction (MySQL semantics)
    JCHECK_EQ(countRows(other, "SELECT COUNT(*) FROM t"), 2);
    try {
        c->rollback();
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("08003"));
    }
    try {
        c->setSavepoint("");
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("S1009"));
    }
    // Isolation levels.
    c->setTransactionIsolation(Connection::TRANSACTION_READ_COMMITTED);
    JCHECK_EQ(c->getTransactionIsolation(), Connection::TRANSACTION_READ_COMMITTED);
    c->setTransactionIsolation(Connection::TRANSACTION_REPEATABLE_READ);
    JCHECK_EQ(c->getTransactionIsolation(), Connection::TRANSACTION_REPEATABLE_READ);
    // Catalog.
    JCHECK_EQ(c->getCatalog(), db.name);
}

// ---------------------------------------------------------------------------------------
JTEST(SqlGeneratedKeys) {
    if (!serverUp()) return;
    TestDb db("keys");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    c->createStatement()->executeUpdate("CREATE TABLE s (spawn_id INT AUTO_INCREMENT PRIMARY KEY, v INT)");
    // MySQL5SpawnDAO: prepareStatement(sql, RETURN_GENERATED_KEYS), execute(), getGeneratedKeys(), getInt(1)
    PreparedStatement* stmt = c->prepareStatement("INSERT INTO `s`(`v`) VALUES (?)", Statement::RETURN_GENERATED_KEYS);
    stmt->setInt(1, 10);
    stmt->execute();
    ResultSet* rs = stmt->getGeneratedKeys();
    int32_t spawnId = 0;
    if (rs->next()) spawnId = rs->getInt(1);
    rs->close();
    stmt->close();
    JCHECK_EQ(spawnId, 1);
    // multi-row insert with auto_increment_increment = 5
    Connection* c5 = db.connect("&sessionVariables=auto_increment_increment=5");
    JFINALLY { c5->close(); };
    PreparedStatement* m = c5->prepareStatement("INSERT INTO s (v) VALUES (1), (2), (3)", Statement::RETURN_GENERATED_KEYS);
    JCHECK_EQ(m->executeUpdate(), 3);
    ResultSet* k = m->getGeneratedKeys();
    std::vector<int64_t> keys;
    while (k->next()) keys.push_back(k->getLong("GENERATED_KEY"));
    JCHECK_EQ(keys.size(), static_cast<size_t>(3));
    if (keys.size() == 3) {
        JCHECK_EQ(keys[1] - keys[0], INT64_C(5));
        JCHECK_EQ(keys[2] - keys[1], INT64_C(5));
    }
    // Statement.executeUpdate(sql, RETURN_GENERATED_KEYS)
    Statement* st = c->createStatement();
    st->executeUpdate("INSERT INTO s (v) VALUES (99)", Statement::RETURN_GENERATED_KEYS);
    ResultSet* k2 = st->getGeneratedKeys();
    JCHECK(k2->next());
    JCHECK(k2->getInt(1) > 1);
    // not requested
    PreparedStatement* plain = c->prepareStatement("INSERT INTO s (v) VALUES (1)");
    plain->executeUpdate();
    JCHECK_THROWS(SQLException, plain->getGeneratedKeys());
}

// ---------------------------------------------------------------------------------------
JTEST(SqlColumnLabelsAndMetaData) {
    if (!serverUp()) return;
    TestDb db("labels");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    Statement* st = c->createStatement();
    st->executeUpdate("CREATE TABLE players (id INT, name VARCHAR(20), last_online TIMESTAMP NULL)");
    st->executeUpdate("CREATE TABLE legion_members (player_id INT, rank INT)");
    st->executeUpdate("INSERT INTO players VALUES (7, 'Bob', '2011-01-01 00:00:00')");
    st->executeUpdate("INSERT INTO legion_members VALUES (7, 3)");
    // MySQL5LegionMemberDAO: getTimestamp("players.last_online") on a join
    ResultSet* rs = st->executeQuery(
        "SELECT * FROM legion_members, players WHERE legion_members.player_id = players.id");
    JCHECK(rs->next());
    JCHECK_EQ(rs->getInt("Player_ID"), 7);
    JCHECK_EQ(rs->getInt("players.id"), 7);
    JCHECK_EQ(rs->getString("PLAYERS.NAME"), String("Bob"));
    JCHECK(rs->getTimestamp("players.last_online") != nullptr);
    JCHECK_EQ(rs->findColumn("rank"), 2);
    try {
        rs->getInt("nope");
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("S0022"));
        JCHECK_EQ(e.getMessage(), String("Column 'nope' not found."));
    }
    ResultSetMetaData* md = rs->getMetaData();
    JCHECK_EQ(md->getColumnCount(), 5);
    JCHECK_EQ(md->getColumnName(3), String("id"));
    JCHECK_EQ(md->getTableName(3), String("players"));
    JCHECK_EQ(md->getColumnType(5), Types::TIMESTAMP);
    JCHECK_EQ(md->getColumnTypeName(4), String("VARCHAR"));
    // duplicate labels: the first column wins
    ResultSet* d = st->executeQuery("SELECT 1 AS x, 2 AS X");
    JCHECK(d->next());
    JCHECK_EQ(d->getInt("x"), 1);
    // re-executing a Statement closes its previous ResultSet (JDBC)
    JCHECK_THROWS(SQLException, rs->next());
    ResultSet* a = st->executeQuery("SELECT name AS n FROM players p");
    ResultSetMetaData* am = a->getMetaData();
    JCHECK_EQ(am->getColumnLabel(1), String("n"));
    JCHECK_EQ(am->getColumnName(1), String("name"));
    JCHECK(a->next());
    JCHECK_EQ(a->getString("p.n"), String("Bob"));
    // DatabaseMetaData (DatabaseFactory / MySQL5DAOUtils.supports)
    DatabaseMetaData* dmd = c->getMetaData();
    JCHECK_EQ(dmd->getDatabaseProductName(), String("MySQL"));
    JCHECK_EQ(dmd->getDatabaseMajorVersion(), 5);
    JCHECK(dmd->getDatabaseMinorVersion() >= 0);
    JCHECK(dmd->getDatabaseProductVersion().length() > 0);
    JCHECK_EQ(dmd->getDriverName(), String("MySQL-AB JDBC Driver"));
    JCHECK(dmd->getUserName().startsWith(user()));
    JCHECK_EQ(dmd->getURL(), db.url());
    std::fprintf(stderr, "  server: %s -> %s %d.%d\n", std::string(dmd->getDatabaseProductVersion()).c_str(),
                 std::string(dmd->getDatabaseProductName()).c_str(), dmd->getDatabaseMajorVersion(),
                 dmd->getDatabaseMinorVersion());
}

// ---------------------------------------------------------------------------------------
JTEST(SqlScrollAndStatementLifecycle) {
    if (!serverUp()) return;
    TestDb db("scroll");
    Connection* c = db.connect();
    Statement* st = c->createStatement();
    st->executeUpdate("CREATE TABLE t (id INT)");
    st->executeUpdate("INSERT INTO t VALUES (3), (1), (2)");
    // MySQL5IdViewDAO.getUsedIDs
    c->setReadOnly(true);
    PreparedStatement* statement =
        c->prepareStatement("SELECT `id` AS `id` FROM t ORDER BY id", ResultSet::TYPE_SCROLL_INSENSITIVE, ResultSet::CONCUR_READ_ONLY);
    ResultSet* rs = statement->executeQuery();
    rs->last();
    int32_t count = rs->getRow();
    rs->beforeFirst();
    auto* ids = new Array<int32_t>(count);
    for (int32_t i = 0; i < count; i++) {
        rs->next();
        (*ids)[i] = rs->getInt("id");
    }
    statement->close();
    JCHECK_EQ(count, 3);
    JCHECK_EQ((*ids)[0], 1);
    JCHECK_EQ((*ids)[2], 3);
    JCHECK_THROWS(SQLException, c->prepareStatement("DELETE FROM t")->executeUpdate());
    c->setReadOnly(false);
    // DB.select closes the connection before the statement: both must be harmless.
    PreparedStatement* ps = c->prepareStatement("SELECT id FROM t");
    ResultSet* r = ps->executeQuery();
    JCHECK(r->next());
    c->close();
    ps->close();
    r->close();
    JCHECK(ps->isClosed());
    JCHECK(c->isClosed());
    JCHECK_THROWS(SQLException, r->next());
    JCHECK_THROWS(SQLException, ps->executeQuery());
    try {
        c->createStatement();
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("08003"));
    }
    c->close();
}

// ---------------------------------------------------------------------------------------
JTEST(SqlTimestampsAcrossTimeZones) {
    if (!serverUp()) return;
    TestDb db("tz");
    const char* savedTz = std::getenv("TZ");
    std::string saved = savedTz ? savedTz : "";
    for (const char* tz : {"UTC", "Europe/Berlin", "America/New_York"}) {
        setTz(tz);
        Connection* c = db.connect();
        JFINALLY { c->close(); };
        Statement* st = c->createStatement();
        st->executeUpdate("DROP TABLE IF EXISTS t");
        st->executeUpdate("CREATE TABLE t (id INT, dt DATETIME, ts TIMESTAMP NULL, d DATE)");
        PreparedStatement* ps = c->prepareStatement("INSERT INTO t VALUES (?, ?, ?, ?)");
        // Round trips of instants (non-ambiguous local times), including the current time
        // (millis are dropped: Connector/J 5.1.13 sends whole seconds).
        std::vector<int64_t> instants = {INT64_C(0), INT64_C(1308650400000), INT64_C(946684799000),
                                         INT64_C(1301189400000), INT64_C(1319938200000) + 3600000,
                                         (System::currentTimeMillis() / 1000) * 1000};
        int id = 0;
        for (int64_t t : instants) {
            ps->setInt(1, ++id);
            ps->setTimestamp(2, new Timestamp(t));
            if (t > 0) ps->setTimestamp(3, new Timestamp(t));  // TIMESTAMP starts at 00:00:01 UTC
            else ps->setNull(3, Types::TIMESTAMP);
            ps->setTimestamp(4, new Timestamp(t));
            ps->executeUpdate();
        }
        ResultSet* rs = st->executeQuery("SELECT * FROM t ORDER BY id");
        for (int64_t t : instants) {
            JCHECK(rs->next());
            JCHECK_EQ(rs->getTimestamp("dt")->getTime(), t);
            if (t > 0) JCHECK_EQ(rs->getTimestamp("ts")->getTime(), t);
            // DATE: local midnight of the local day
            time_t secs = static_cast<time_t>(t / 1000);
            struct tm lt;
            localtime_r(&secs, &lt);
            JCHECK_EQ(rs->getTimestamp("d")->getTime(), localMillis(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, 0, 0, 0));
        }
        // The stored text is the local wall time (as the JVM default zone would format it).
        PreparedStatement* q = c->prepareStatement("SELECT CAST(? AS CHAR)");
        q->setTimestamp(1, new Timestamp(INT64_C(1308650400000)));
        ResultSet* r = q->executeQuery();
        JCHECK(r->next());
        time_t secs = 1308650400;
        struct tm lt;
        localtime_r(&secs, &lt);
        char want[32];
        std::strftime(want, sizeof want, "%Y-%m-%d %H:%M:%S", &lt);
        JCHECK_EQ(r->getString(1), String(want));
        // DATETIME text -> getString in Timestamp.toString() form
        ResultSet* s = st->executeQuery("SELECT CAST('2011-06-21 10:00:00' AS DATETIME), CAST('2011-06-21 10:00:00.25' AS DATETIME(2))");
        JCHECK(s->next());
        JCHECK_EQ(s->getString(1), String("2011-06-21 10:00:00.0"));
        JCHECK_EQ(s->getString(2), String("2011-06-21 10:00:00.25"));
        JCHECK_EQ(s->getTimestamp(2)->getTime(), localMillis(2011, 6, 21, 10, 0, 0) + 250);
        JCHECK_EQ(nanosOf(s->getTimestamp(2)), 250000000);
    }
    // DST edge cases in Berlin, java.util.GregorianCalendar semantics.
    setTz("Europe/Berlin");
    {
        Connection* c = db.connect();
        JFINALLY { c->close(); };
        ResultSet* g = c->createStatement()->executeQuery(
            "SELECT CAST('2011-03-27 02:30:00' AS DATETIME), CAST('2011-10-30 02:30:00' AS DATETIME)");
        JCHECK(g->next());
        JCHECK_EQ(g->getTimestamp(1)->getTime(), INT64_C(1301189400000));  // gap: 03:30 CEST
        JCHECK_EQ(g->getTimestamp(2)->getTime(), INT64_C(1319938200000));  // overlap: 02:30 CET
        JCHECK_EQ(g->getString(1), String("2011-03-27 03:30:00.0"));
    }
    if (savedTz != nullptr) setTz(saved.c_str());
    else setTz(nullptr);
}

JTEST(SqlZeroDates) {
    if (!serverUp()) return;
    TestDb db("zero");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    Statement* st = c->createStatement();
    // players.creation_date / last_online default to '0000-00-00 00:00:00' in au_server_gs.sql
    st->executeUpdate("CREATE TABLE p (id INT, last_online TIMESTAMP NOT NULL DEFAULT '0000-00-00 00:00:00')");
    st->executeUpdate("INSERT INTO p (id) VALUES (1)");
    ResultSet* rs = st->executeQuery("SELECT last_online FROM p");
    JCHECK(rs->next());
    JCHECK(rs->getTimestamp("last_online") == nullptr);  // jlang default: convertToNull
    JCHECK(rs->wasNull());
    JCHECK(rs->getString(1).isNull());
    Connection* ex = db.connect("&zeroDateTimeBehavior=exception");
    JFINALLY { ex->close(); };
    ResultSet* r2 = ex->createStatement()->executeQuery("SELECT last_online FROM p");
    JCHECK(r2->next());
    JCHECK_THROWS(SQLException, r2->getTimestamp(1));
    Connection* ro = db.connect("&zeroDateTimeBehavior=round");
    JFINALLY { ro->close(); };
    ResultSet* r3 = ro->createStatement()->executeQuery("SELECT last_online FROM p");
    JCHECK(r3->next());
    JCHECK_EQ(r3->getString(1), String("0001-01-01 00:00:00.0"));
    // Legacy TIMESTAMP rules (jlangLegacyTimestamps): NULL into a NOT NULL TIMESTAMP column
    // stores the current time, as on the MySQL 5.x servers the Java code was written for.
    PreparedStatement* up = c->prepareStatement("UPDATE p SET last_online = ? WHERE id = 1");
    up->setTimestamp(1, nullptr);
    JCHECK_EQ(up->executeUpdate(), 1);
    ResultSet* r4 = st->executeQuery("SELECT last_online FROM p");
    JCHECK(r4->next());
    Timestamp* now = r4->getTimestamp(1);
    JCHECK(now != nullptr && std::llabs(now->getTime() - System::currentTimeMillis()) < 120000);
}

// ---------------------------------------------------------------------------------------
JTEST(SqlCallableStatement) {
    if (!serverUp()) return;
    TestDb db("call");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    c->createStatement()->executeUpdate("CREATE TABLE a (name VARCHAR(20), credits INT)");
    c->createStatement()->executeUpdate("INSERT INTO a VALUES ('x', 5), ('y', 7)");
    c->createStatement()->executeUpdate(
        "CREATE PROCEDURE credits_of(IN n VARCHAR(20)) BEGIN SELECT credits FROM a WHERE name = n; END");
    // commons DB.call: prepareCall + CallReadStH.setParams + executeQuery + handleRead
    for (const char* sql : {"{call credits_of(?)}", "CALL credits_of(?)", "{ CALL credits_of(?) }"}) {
        CallableStatement* stmt = c->prepareCall(sql);
        stmt->setString(1, "y");
        ResultSet* rset = stmt->executeQuery();
        JCHECK(rset->next());
        JCHECK_EQ(rset->getInt("credits"), 7);
        JCHECK(!rset->next());
        c->close();
        stmt->close();
        c = db.connect();
    }
    // The connection stays usable after a CALL (all results consumed).
    JCHECK_EQ(countRows(c, "SELECT COUNT(*) FROM a"), 2);
}

// ---------------------------------------------------------------------------------------
JTEST(SqlErrorsAndUrls) {
    if (!serverUp()) return;
    TestDb db("errors");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    try {
        c->createStatement()->executeQuery("SELECT * FROM missing_table");
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(e.getErrorCode(), 1146);
        JCHECK_EQ(state(e), std::string("42S02"));
    }
    try {
        c->createStatement()->executeUpdate("THIS IS NOT SQL");
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(e.getErrorCode(), 1064);
        JCHECK_EQ(state(e), std::string("42000"));
    }
    // The connection survives errors.
    JCHECK_EQ(countRows(c, "SELECT 1"), 1);
    // Wrong password / unknown database / no server / bad URL.
    try {
        DriverManager::getConnection(db.url(), user(), "definitely-wrong");
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(e.getErrorCode(), 1045);
        JCHECK_EQ(state(e), std::string("28000"));
    }
    try {
        DriverManager::getConnection(str(baseUrl(), "jlang_no_such_db"), user(), password());
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(e.getErrorCode(), 1049);
    }
    try {
        DriverManager::getConnection("jdbc:mysql://127.0.0.1:1/x?connectTimeout=2000", user(), password());
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("08S01"));
    }
    try {
        DriverManager::getConnection("jdbc:postgresql://localhost/x", user(), password());
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("08001"));
    }
    try {
        DriverManager::getConnection(db.url("&autoReconnect=maybe"), user(), password());
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("S1009"));
    }
    // user/password in the URL; URL-encoded values; localhost + default port.
    Connection* u = DriverManager::getConnection(str("jdbc:mysql://localhost/", db.name, "?user=", user(), "&password=", password()));
    JCHECK(!u->isClosed());
    u->close();
    JCHECK(u->isClosed());
    // isValid
    JCHECK(c->isValid(1));
}

// ---------------------------------------------------------------------------------------
JTEST(SqlReconnectAfterKill) {
    if (!serverUp()) return;
    TestDb db("reconnect");
    auto killConnection = [&](Connection* victim) {
        int64_t id = 0;
        {
            ResultSet* rs = victim->createStatement()->executeQuery("SELECT CONNECTION_ID()");
            rs->next();
            id = rs->getLong(1);
        }
        adminExec(str("KILL ", id));
    };
    // Without autoReconnect: the failure closes the connection (Connector/J).
    Connection* c = db.connect();
    killConnection(c);
    try {
        c->createStatement()->executeQuery("SELECT 1");
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("08S01"));
    }
    JCHECK(c->isClosed());
    JCHECK(!c->isValid(1));
    try {
        c->createStatement()->executeQuery("SELECT 1");
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(state(e), std::string("08003"));
    }
    c->close();
    // autoReconnect=true: the failing call throws, the next one reconnects and restores the
    // auto-commit mode.
    Connection* a = db.connect("&autoReconnect=true");
    JFINALLY { a->close(); };
    a->setAutoCommit(false);
    killConnection(a);
    JCHECK_THROWS(SQLException, a->createStatement()->executeQuery("SELECT 1"));
    JCHECK(!a->isClosed());
    ResultSet* rs = a->createStatement()->executeQuery("SELECT @@autocommit");
    JCHECK(rs->next());
    JCHECK_EQ(rs->getInt(1), 0);
    a->rollback();
}

// ---------------------------------------------------------------------------------------
JTEST(SqlPoolBasics) {
    if (!serverUp()) return;
    TestDb db("pool");
    auto* ds = new DataSource(db.url(), user(), password());
    JFINALLY { ds->close(); };
    ds->setMaxIdle(2);    // DatabaseConfig.DATABASE_CONNECTIONS_MIN
    ds->setMaxActive(3);  // DatabaseConfig.DATABASE_CONNECTIONS_MAX
    JCHECK_EQ(ds->getNumActive(), 0);
    JCHECK_EQ(ds->getNumIdle(), 0);
    // DatabaseFactory.init: metadata of the first connection.
    Connection* first = ds->getConnection();
    DatabaseMetaData* dmd = first->getMetaData();
    JCHECK_EQ(dmd->getDatabaseProductName(), String("MySQL"));
    JCHECK_EQ(dmd->getDatabaseMajorVersion(), 5);
    first->createStatement()->executeUpdate("CREATE TABLE t (id INT PRIMARY KEY) ENGINE=InnoDB");
    first->close();
    JCHECK_EQ(ds->getNumActive(), 0);
    JCHECK_EQ(ds->getNumIdle(), 1);
    // Reuse (LIFO): the same server session comes back.
    Connection* c1 = ds->getConnection();
    int64_t id1;
    {
        ResultSet* rs = c1->createStatement()->executeQuery("SELECT CONNECTION_ID()");
        rs->next();
        id1 = rs->getLong(1);
    }
    // Open transaction and open statements are reset on return (dbcp passivateObject:
    // rollback, autocommit back to true, statements closed).
    c1->setAutoCommit(false);
    c1->createStatement()->executeUpdate("INSERT INTO t VALUES (1)");
    PreparedStatement* leaked = c1->prepareStatement("SELECT * FROM t");
    ResultSet* leakedRs = leaked->executeQuery();
    c1->close();
    c1->close();  // second close: no-op (dbcp PoolGuardConnectionWrapper)
    JCHECK(leaked->isClosed());
    JCHECK_THROWS(SQLException, leakedRs->next());
    try {
        c1->createStatement();
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(e.getMessage(), String("Connection is closed."));
    }
    Connection* c2 = ds->getConnection();
    {
        ResultSet* rs = c2->createStatement()->executeQuery("SELECT CONNECTION_ID(), @@autocommit");
        rs->next();
        JCHECK_EQ(rs->getLong(1), id1);
        JCHECK_EQ(rs->getInt(2), 1);
    }
    JCHECK(c2->getAutoCommit());
    JCHECK_EQ(countRows(c2, "SELECT COUNT(*) FROM t"), 0);  // rolled back
    // The read-only flag is reset when borrowed (activateObject). dbcp skips the rollback of a
    // read-only connection and setAutoCommit(true) then commits it (MySQL semantics).
    c2->setAutoCommit(false);
    c2->createStatement()->executeUpdate("INSERT INTO t VALUES (2)");
    c2->setReadOnly(true);
    c2->close();
    c2 = ds->getConnection();
    JCHECK(!c2->isReadOnly());
    JCHECK_EQ(countRows(c2, "SELECT COUNT(*) FROM t"), 1);
    // maxIdle: returning more than maxIdle closes the extra sessions.
    Connection* c3 = ds->getConnection();
    Connection* c4 = ds->getConnection();
    JCHECK_EQ(ds->getNumActive(), 3);
    c2->close();
    c3->close();
    c4->close();
    JCHECK_EQ(ds->getNumActive(), 0);
    JCHECK_EQ(ds->getNumIdle(), 2);
    // FAIL / timeout when exhausted.
    Connection* h[3];
    for (auto& x : h) x = ds->getConnection();
    ds->setMaxWait(200);
    int64_t t0 = System::currentTimeMillis();
    try {
        ds->getConnection();
        JCHECK(false);
    } catch (SQLException& e) {
        JCHECK_EQ(e.getMessage(), String("Cannot get a connection, pool error Timeout waiting for idle object"));
    }
    JCHECK(System::currentTimeMillis() - t0 >= 150);
    ds->setWhenExhaustedAction(DataSource::WHEN_EXHAUSTED_FAIL);
    JCHECK_THROWS(SQLException, ds->getConnection());
    ds->setWhenExhaustedAction(DataSource::WHEN_EXHAUSTED_BLOCK);
    ds->setMaxWait(-1);
    // BLOCK: a waiting borrower gets the connection returned by another thread.
    std::atomic<int> got{0};
    uint64_t th = spawn([&] {
        try {
            Connection* w = ds->getConnection();
            got = 1;
            w->close();
        } catch (...) {
            got = -1;
        }
    });

    for (int i = 0; i < 20 && got.load() == 0; i++) sync::sleep(10);
    JCHECK_EQ(got.load(), 0);  // still blocked
    h[0]->close();
    gc::joinNativeThread(th);
    JCHECK_EQ(got.load(), 1);
    h[1]->close();
    h[2]->close();
    // preparePool / minIdle
    ds->setMaxIdle(8);
    ds->setMinIdle(4);
    ds->preparePool();
    JCHECK_EQ(ds->getNumIdle(), 3);  // bounded by maxActive (3)
    // Validation of idle sessions that the server dropped (jlang addition).
    Connection* v = ds->getConnection();
    int64_t vid;
    {
        ResultSet* rs = v->createStatement()->executeQuery("SELECT CONNECTION_ID()");
        rs->next();
        vid = rs->getLong(1);
    }
    v->close();
    adminExec(str("KILL ", vid));
    ds->setValidationIdleMillis(0);
    sync::sleep(5);
    Connection* v2 = ds->getConnection();
    JCHECK_EQ(countRows(v2, "SELECT 1"), 1);
    v2->close();
    // close(): idle sessions closed, borrowing fails, borrowed ones are closed on return.
    Connection* last = ds->getConnection();
    ds->close();
    JCHECK_EQ(ds->getNumIdle(), 0);
    JCHECK_THROWS(IllegalStateException, ds->getConnection());
    JCHECK_EQ(countRows(last, "SELECT 1"), 1);
    last->close();
    JCHECK_EQ(ds->getNumActive(), 0);
    JCHECK_THROWS(UnsupportedOperationException, ds->getConnection("a", "b"));
}

// ---------------------------------------------------------------------------------------
namespace {
std::atomic<int32_t> g_poolErrors{0};
std::atomic<int32_t> g_poolOps{0};
}  // namespace

JTEST(SqlPoolManyThreads) {
    if (!serverUp()) return;
    TestDb db("threads");
    auto* ds = new DataSource(db.url(), user(), password());
    JFINALLY { ds->close(); };
    ds->setMaxIdle(5);
    ds->setMaxActive(5);
    {
        Connection* c = ds->getConnection();
        JFINALLY { c->close(); };
        c->createStatement()->executeUpdate(
            "CREATE TABLE counters (id INT PRIMARY KEY, n INT, note VARCHAR(64), ts DATETIME) ENGINE=InnoDB");
        PreparedStatement* ps = c->prepareStatement("INSERT INTO counters VALUES (?, 0, '', NOW())");
        for (int i = 0; i < 16; i++) {
            ps->setInt(1, i);
            ps->addBatch();
        }
        ps->executeBatch();
    }
    constexpr int kThreads = 16;
    constexpr int kIters = 150;
    std::atomic<bool> stop{false};
    uint64_t collector = spawn([&] {
        while (!stop.load()) {
            gc::collect();
            sync::sleep(2);
        }
    });
    std::vector<uint64_t> threads;
    for (int t = 0; t < kThreads; t++) {
        threads.push_back(spawn([ds, t] {
            for (int i = 0; i < kIters; i++) {
                Connection* con = nullptr;
                try {
                    JFINALLY {
                        if (con != nullptr) con->close();
                    };
                    con = ds->getConnection();
                    PreparedStatement* up = con->prepareStatement("UPDATE counters SET n = n + 1, note = ?, ts = ? WHERE id = ?");
                    up->setString(1, str("thread ", t, " iteration ", i, " 'quoted' é"));
                    up->setTimestamp(2, new Timestamp(System::currentTimeMillis()));
                    up->setInt(3, t);
                    if (up->executeUpdate() != 1) g_poolErrors++;
                    PreparedStatement* sel = con->prepareStatement("SELECT n, note FROM counters WHERE id = ?");
                    sel->setInt(1, t);
                    ResultSet* rs = sel->executeQuery();
                    if (!rs->next() || rs->getInt("n") != i + 1 || !rs->getString("note").startsWith(str("thread ", t, " ")))
                        g_poolErrors++;
                    if (i % 10 == 0) {
                        con->setAutoCommit(false);
                        con->createStatement()->executeUpdate(str("UPDATE counters SET note = 'tx' WHERE id = ", t));
                        con->rollback();
                    }
                    g_poolOps++;
                } catch (Throwable& e) {
                    std::fprintf(stderr, "  thread %d: %s\n", t, e.what());
                    g_poolErrors++;
                }
            }
        }));
    }
    for (uint64_t th : threads) gc::joinNativeThread(th);
    stop = true;
    gc::joinNativeThread(collector);
    JCHECK_EQ(g_poolErrors.load(), 0);
    JCHECK_EQ(g_poolOps.load(), kThreads * kIters);
    JCHECK_EQ(ds->getNumActive(), 0);
    JCHECK(ds->getNumIdle() <= 5);
    Connection* c = ds->getConnection();
    JFINALLY { c->close(); };
    JCHECK_EQ(countRows(c, "SELECT SUM(n) FROM counters"), kThreads * kIters);
}

// ---------------------------------------------------------------------------------------
// Server start-up loads large tables (items, spawns, drops): reading many rows by label must
// stay cheap.
JTEST(SqlBulkRead) {
    if (!serverUp()) return;
    TestDb db("bulk");
    Connection* c = db.connect();
    JFINALLY { c->close(); };
    Statement* st = c->createStatement();
    st->executeUpdate(
        "CREATE TABLE items (itemUniqueId INT PRIMARY KEY, itemId INT, itemCount BIGINT, itemCreator VARCHAR(50),"
        " itemCreationTime TIMESTAMP NOT NULL DEFAULT '2010-01-01 00:00:01', isEquiped TINYINT(1), slot INT)");
    constexpr int kRows = 20000;
    for (int base = 0; base < kRows; base += 1000) {
        StringBuilder* sb = new StringBuilder("INSERT INTO items (itemUniqueId, itemId, itemCount, itemCreator, isEquiped, slot) VALUES ");
        for (int i = base; i < base + 1000; i++) {
            if (i > base) sb->append(",");
            sb->append(str("(", i, ",", 100000 + i, ",", INT64_C(1) << 33, ",'crafter", i, "',", i & 1, ",", i % 17, ")"));
        }
        st->executeUpdate(sb->toString());
    }
    int64_t t0 = System::currentTimeMillis();
    PreparedStatement* ps = c->prepareStatement("SELECT * FROM items WHERE itemUniqueId >= ?");
    ps->setInt(1, 0);
    ResultSet* rs = ps->executeQuery();
    int64_t sum = 0;
    int rows = 0;
    while (rs->next()) {
        sum += rs->getInt("itemUniqueId");
        sum += rs->getInt("itemId");
        sum += rs->getLong("itemCount") >> 33;
        sum += rs->getString("itemCreator").length();
        sum += rs->getTimestamp("itemCreationTime")->getTime() > 0 ? 1 : 0;
        sum += rs->getBoolean("isEquiped") ? 1 : 0;
        sum += rs->getInt("slot");
        rows++;
    }
    int64_t elapsed = System::currentTimeMillis() - t0;
    JCHECK_EQ(rows, kRows);
    JCHECK(sum > 0);
    std::fprintf(stderr, "  read %d rows x 7 columns by label in %lld ms\n", rows, static_cast<long long>(elapsed));
    JCHECK(elapsed < 20000);
}

// ---------------------------------------------------------------------------------------
JTEST(SqlPoolInterruptAndGrow) {
    if (!serverUp()) return;
    TestDb db("poolint");
    auto* ds = new DataSource(db.url(), user(), password());
    JFINALLY { ds->close(); };
    ds->setMaxActive(1);
    Connection* held = ds->getConnection();
    // A borrower blocked on an exhausted pool is interruptible (commons-pool rethrows the
    // InterruptedException after restoring the flag; PoolingDataSource wraps it).
    std::atomic<sync::InterruptState*> waiterState{nullptr};
    std::atomic<int> outcome{0};
    uint64_t th = spawn([&] {
        waiterState = sync::current();
        try {
            ds->getConnection();
            outcome = 1;
        } catch (SQLException& e) {
            bool flagged = sync::isInterrupted(sync::current());
            outcome = e.getMessage().equals("Cannot get a connection, general error") && flagged ? 2 : 3;
        }
    });
    while (waiterState.load() == nullptr) sync::sleep(1);
    sync::sleep(50);
    sync::interrupt(waiterState.load());
    gc::joinNativeThread(th);
    JCHECK_EQ(outcome.load(), 2);
    JCHECK_EQ(ds->getNumActive(), 1);
    // GROW: exceeds maxActive instead of blocking.
    ds->setWhenExhaustedAction(DataSource::WHEN_EXHAUSTED_GROW);
    Connection* extra = ds->getConnection();
    JCHECK_EQ(ds->getNumActive(), 2);
    extra->close();
    held->close();
    JCHECK_EQ(ds->getNumActive(), 0);
    // Lowering maxIdle closes surplus idle sessions.
    JCHECK_EQ(ds->getNumIdle(), 2);
    ds->setMaxIdle(1);
    JCHECK_EQ(ds->getNumIdle(), 1);
    ds->setMaxIdle(0);
    JCHECK_EQ(ds->getNumIdle(), 0);
    Connection* c = ds->getConnection();
    c->close();
    JCHECK_EQ(ds->getNumIdle(), 0);  // maxIdle 0: every returned session is closed
}

#endif  // __has_include(<jlang/Time.h>)
