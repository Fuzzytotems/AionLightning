// jlang/src/sql_internal.h - private declarations shared by the jlang/src/sql*.cpp files
// (jlang/Sql.h implementation over MariaDB Connector/C). Not a public header: it is included
// only by jlang/src/sql*.cpp. The Connector/C headers themselves are included by the .cpp
// files only; here the handle types are forward-declared.
#pragma once

#include <jlang/Sql.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

struct st_mysql;
struct st_mysql_res;

namespace jlang::sql_detail {

// MySQL field types (enum_field_types) used by the conversions.
enum MysqlType : int32_t {
    T_DECIMAL = 0, T_TINY = 1, T_SHORT = 2, T_LONG = 3, T_FLOAT = 4, T_DOUBLE = 5, T_NULL = 6,
    T_TIMESTAMP = 7, T_LONGLONG = 8, T_INT24 = 9, T_DATE = 10, T_TIME = 11, T_DATETIME = 12,
    T_YEAR = 13, T_NEWDATE = 14, T_VARCHAR = 15, T_BIT = 16, T_NEWDECIMAL = 246, T_ENUM = 247,
    T_SET = 248, T_TINY_BLOB = 249, T_MEDIUM_BLOB = 250, T_LONG_BLOB = 251, T_BLOB = 252,
    T_VAR_STRING = 253, T_STRING = 254, T_GEOMETRY = 255
};

enum ZeroDateBehavior : int32_t { ZERO_EXCEPTION = 0, ZERO_CONVERT_TO_NULL = 1, ZERO_ROUND = 2 };

// ---------------------------------------------------------------------------------------
// Parsed connection URL + properties.
class Config : public virtual Object {
public:
    String url;
    std::string host = "localhost";
    unsigned port = 3306;
    std::string database;
    std::string socket;  // jlangSocket
    std::string user;
    std::string password;
    bool autoReconnect = false;
    int32_t zeroDateBehavior = ZERO_CONVERT_TO_NULL;
    bool jdbcCompliantTruncation = true;
    bool tinyInt1isBit = true;
    bool emptyStringsConvertToZero = true;
    bool useAffectedRows = false;
    bool allowMultiQueries = false;
    bool continueBatchOnError = true;
    bool allowNanAndInf = false;
    bool yearIsDateType = true;
    bool legacyTimestamps = true;
    uint32_t connectTimeoutMs = 0;
    uint32_t socketTimeoutMs = 0;
    std::string sessionVariables;

    // Throws SQLException for a malformed URL or an invalid property value.
    static Config* parse(const String& url, const String& user, const String& password);
    static bool acceptsURL(const String& url);
};

// ---------------------------------------------------------------------------------------
// Column metadata (copied from MYSQL_FIELD, with Connector/J's java.sql.Types mapping).
struct ColumnInfo {
    String label;     // MYSQL_FIELD.name (alias)
    String name;      // org_name
    String table;     // table (alias)
    String orgTable;  // org_table
    String db;
    int32_t mysqlType = 0;
    int32_t sqlType = 0;
    uint32_t flags = 0;
    uint32_t charsetnr = 0;
    uint32_t decimals = 0;
    uint64_t length = 0;
    bool singleBit = false;
    bool isUnsigned() const noexcept { return (flags & 32) != 0; }
    bool isBinaryFlag() const noexcept { return (flags & 128) != 0; }
};

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>()(s); }
};
using IndexMap = std::unordered_map<std::string, int32_t, StringHash, std::equal_to<>>;

class ResultMeta : public virtual Object {
public:
    std::vector<ColumnInfo> cols;
    // findColumn support: label and "table.label" (case-insensitive, first column wins).
    int32_t find(std::string_view name);  // 0-based index or -1

    static ResultMeta* generatedKeys();

private:
    void buildMaps();
    bool mapsBuilt_ = false;
    IndexMap labels_;
    IndexMap fullNames_;
    IndexMap cache_;  // exact spelling -> index
};

// All rows of a result, in pointer-free GC memory. Cell (r, c) is data[offs[i], +lens[i])
// with i = r * ncols + c; lens[i] == -1 for SQL NULL.
class RowData : public virtual Object {
public:
    int32_t nrows = 0;
    int32_t ncols = 0;
    char* data = nullptr;
    int64_t* offs = nullptr;
    int64_t* lens = nullptr;

    // Copies a stored MYSQL_RES (at most maxRows rows when maxRows > 0).
    static RowData* copy(st_mysql_res* res, int32_t maxRows);
    // One column of text cells (generated keys).
    static RowData* ofStrings(const std::vector<std::string>& cells);
};

// One result of an execution: a result set (meta != nullptr) or an OK packet.
class ExecResult : public virtual Object {
public:
    ResultMeta* meta = nullptr;
    RowData* rows = nullptr;
    int64_t updateCount = -1;
    int64_t insertId = 0;
    String info;
    ExecResult* next = nullptr;
};

// ---------------------------------------------------------------------------------------
// A server session (one MYSQL handle). Used by one thread at a time (JDBC); the mutex makes
// concurrent misuse (e.g. close from another thread) safe instead of corrupting the handle.
class Session : public virtual Object {
public:
    explicit Session(Config* cfg);

    Config* cfg;
    std::recursive_mutex mu;
    bool autoCommit = true;
    bool readOnly = false;
    bool closed = false;
    bool broken = false;             // communication failure seen
    String serverVersion;            // as Connector/J reports it ("5.5.5-10.11.14-MariaDB")
    int32_t major = 0, minor = 0, subminor = 0;
    bool mariadb = false;
    std::atomic<int64_t> lastUsedMillis{0};
    int64_t autoIncrementIncrement = -1;  // lazily read
    std::string catalog;             // current database

    void connect();                  // throws SQLException
    void close() noexcept;           // idempotent
    // Closed by close() or dead after a communication failure without autoReconnect.
    bool isClosed() noexcept;
    bool versionMeetsMinimum(int32_t ma, int32_t mi, int32_t sub) const noexcept;

    // Runs sql and collects all its results. maxRows > 0 truncates result sets.
    ExecResult* execute(std::string_view sql, int32_t maxRows = 0);
    // Runs sql, discarding results.
    void executeSimple(std::string_view sql);
    bool ping(int32_t timeoutSeconds) noexcept;
    int64_t getAutoIncrementIncrement();
    bool noBackslashEscapes() noexcept;
    // Quotes and escapes a UTF-8 string literal: 'it\'s'.
    std::string quote(std::string_view s);

private:
    void ensureOpen();
    void openHandle();
    void initSession();
    [[noreturn]] void throwError(const char* context = nullptr);
    st_mysql* mysql_ = nullptr;
    std::vector<std::string> initSql_;
};

// ---------------------------------------------------------------------------------------
// Batched command (Statement.addBatch(sql) or PreparedStatement.addBatch()).
struct BatchEntry {
    bool isSql = false;
    std::string sql;
    std::vector<std::string> values;
    std::vector<bool> isSet;
};

// ---------------------------------------------------------------------------------------
// Helpers shared by the sql*.cpp files.
[[noreturn]] void throwSql(const String& message, const String& sqlState, int32_t code = 0);
int64_t nowMillis() noexcept;
void ensureThreadInit() noexcept;  // mysql_library_init once + mysql_thread_init per thread

// Date/time glue (sql_time.cpp, the only SQL file that includes <jlang/Time.h>).
//
// Local times use java.util.GregorianCalendar in the default TimeZone (Connector/J does the same
// with its session calendar): lenient field normalization, a wall time in a DST gap moves
// forward, an ambiguous one resolves to standard time, Julian calendar before 1582-10-15. A
// Calendar is created per ResultSet / PreparedStatement (single-threaded, like JDBC objects).
Calendar* newLocalCalendar();
// cal.clear(); cal.set(year, month - 1, day, hour, minute, second); cal.getTimeInMillis()
int64_t localToMillis(Calendar* cal, int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute,
                      int32_t second);
struct Fields {
    int64_t year;  // year of era, as SimpleDateFormat "yyyy" prints it
    int32_t month, day, hour, minute, second, millis;
};
Fields millisToLocal(Calendar* cal, int64_t millis);
// java.sql.Timestamp.toString() / java.sql.Date.toString() / java.sql.Time.toString() of an
// instant in cal's zone (the year printed is the year of era, as Java's Date.getYear() + 1900).
String timestampToString(Calendar* cal, int64_t millis, int32_t nanos);
String dateToString(Calendar* cal, int64_t millis);
String timeToString(Calendar* cal, int64_t millis);
// new Timestamp(millis) followed by setNanos(nanos) (nanos < 0: keep the millis' nanos).
Timestamp* makeTimestamp(int64_t millis, int32_t nanos);
int64_t timestampMillis(Timestamp* ts);
int32_t timestampNanos(Timestamp* ts);
// Object is a java.util.Date (not a Timestamp): its getTime(); else false.
bool dateMillis(Object* o, int64_t& millis);
// Object is a Timestamp.
Timestamp* asTimestamp(Object* o);
Object* timestampObject(Timestamp* ts);

// Connector/J StringUtils.getInt/getLong/getShort over bytes [offset, endPos): false where Java
// throws NumberFormatException.
bool cjGetInt(const char* buf, int64_t len, int64_t offset, int64_t endPos, int32_t& out) noexcept;
bool cjGetLong(const char* buf, int64_t len, int64_t offset, int64_t endPos, int64_t& out) noexcept;
bool cjGetShort(const char* buf, int64_t len, int16_t& out) noexcept;

// SQL text scanning (Connector/J StatementImpl/StringUtils helpers).
int32_t findStartOfStatement(std::string_view sql) noexcept;
char firstAlphaCharUc(std::string_view sql, int32_t start) noexcept;
char firstNonWsCharUc(std::string_view sql, int32_t start) noexcept;
// Leading comments stripped, then startsWithIgnoreCaseAndWs(keyword).
bool startsWithKeywordIgnoringComments(std::string_view sql, std::string_view keyword) noexcept;
bool startsWithIgnoreCaseAndWs(std::string_view sql, std::string_view keyword) noexcept;
int32_t onDuplicateKeyLocation(std::string_view sql) noexcept;

}  // namespace jlang::sql_detail
