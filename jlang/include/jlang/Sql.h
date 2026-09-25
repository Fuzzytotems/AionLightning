// jlang/Sql.h - java.sql / javax.sql (JDBC) over MariaDB Connector/C, with the semantics of
// MySQL Connector/J 5.1.13 (the driver the Java servers used) and a connection pool that
// replaces commons-dbcp 1.4 + commons-pool 1.5 (PoolingDataSource over GenericObjectPool).
//
//   Java                                      C++
//   java.sql.Connection                       jlang::Connection*
//   java.sql.Statement                        jlang::Statement*
//   java.sql.PreparedStatement                jlang::PreparedStatement*
//   java.sql.CallableStatement                jlang::CallableStatement*
//   java.sql.ResultSet                        jlang::ResultSet*
//   java.sql.ResultSetMetaData                jlang::ResultSetMetaData*
//   java.sql.DatabaseMetaData                 jlang::DatabaseMetaData*
//   java.sql.Savepoint                        jlang::Savepoint*
//   java.sql.Types                            jlang::Types            (Types.NULL -> Types::NULL_)
//   java.sql.SQLException                     jlang::SQLException     (core; getErrorCode/getSQLState)
//   java.sql.BatchUpdateException             jlang::BatchUpdateException
//   javax.sql.DataSource / dbcp PoolingDataSource   jlang::DataSource* (the pool itself)
//   java.sql.DriverManager                    jlang::DriverManager
//   java.sql.Timestamp                        jlang::Timestamp* (include <jlang/Time.h> to use it)
//
// Usage is the Java code with -> and jlang types:
//
//   jlang::Connection* con = nullptr;
//   { JFINALLY { DatabaseFactory::close(con); };
//     con = DatabaseFactory::getConnection();
//     jlang::PreparedStatement* st = con->prepareStatement("SELECT * FROM players WHERE id = ?");
//     st->setInt(1, id);
//     jlang::ResultSet* rs = st->executeQuery();
//     while (rs->next()) { name = rs->getString("name"); ... }
//     st->close();
//   }
//
// DatabaseFactory (dbcp GenericObjectPool/PoolableConnectionFactory/PoolingDataSource) becomes
//
//   dataSource = new jlang::DataSource(DATABASE_URL, DATABASE_USER, DATABASE_PASSWORD);
//   dataSource->setMaxIdle(DATABASE_CONNECTIONS_MIN);   // connectionPool.setMaxIdle(...)
//   dataSource->setMaxActive(DATABASE_CONNECTIONS_MAX); // connectionPool.setMaxActive(...)
//   Connection* c = dataSource->getConnection();        // borrow; c->close() gives it back
//   dataSource->getNumActive(); dataSource->getNumIdle(); dataSource->close();
//
// Semantics (Connector/J 5.1.13 defaults; see docs in each class):
//   * PreparedStatement parameters are substituted on the client, like useServerPrepStmts=false:
//     '?' outside quotes/backquotes/comments become escaped literals. Unset parameters throw
//     SQLException 07001 "No value specified for parameter N" at execution.
//   * Update counts are "found rows" (rows matched), not "changed rows" (useAffectedRows=false).
//   * Result sets are read completely into memory (like Connector/J's default), so every
//     ResultSet is scrollable (first/last/absolute/relative/previous/getRow work for any type).
//   * Column labels are matched case-insensitively; "alias.column" (table alias + label) works
//     too. Numeric getters parse the text protocol exactly like Connector/J (including its
//     range checks: getByte on 200 throws SQLException 22003).
//   * getTimestamp()/setTimestamp() interpret DATETIME/TIMESTAMP values in the local time zone
//     (the JVM default zone in Java, TZ / /etc/localtime here), like Connector/J without
//     useTimezone. Zero dates ('0000-00-00 00:00:00') follow zeroDateTimeBehavior; the jlang
//     default is convertToNull (Connector/J's default was exception), overridable in the URL.
//   * close() of Connection, Statement and ResultSet never throws and is idempotent (safe in
//     JFINALLY). Closing a Connection closes its statements, which close their result sets.
//   * Threads: a Connection (and its statements/result sets) is used by one thread at a time,
//     as in JDBC; the DataSource is thread-safe.
//
// Connection URL: jdbc:mysql://host[:port][/database][?key=value&...]. Recognized keys
// (others are ignored, as Connector/J ignores unknown ones):
//   user, password            credentials (override the ones passed to getConnection)
//   useUnicode, characterEncoding   accepted; the connection always talks utf8mb4 because
//                             jlang::String is UTF-8 (the server converts to column charsets)
//   autoReconnect             true: after a communication failure the failing call throws
//                             (SQLState 08S01) and the next call reconnects (Connector/J)
//   zeroDateTimeBehavior      exception | convertToNull (jlang default) | round
//   connectTimeout, socketTimeout   milliseconds (0 = none, the default)
//   useAffectedRows           false (default): update counts = matched rows (CLIENT_FOUND_ROWS)
//   jdbcCompliantTruncation   true (default): adds STRICT_TRANS_TABLES to the session sql_mode
//                             and range-checks numeric getters
//   tinyInt1isBit, emptyStringsConvertToZero, continueBatchOnError, allowNanAndInf,
//   yearIsDateType, allowMultiQueries, sessionVariables (a=1,b=2)   as in Connector/J
//   jlangLegacyTimestamps     true (default): SET SESSION explicit_defaults_for_timestamp=0 on
//                             servers that have it, i.e. MySQL 5.1/5.5 TIMESTAMP rules (NULL
//                             into a NOT NULL TIMESTAMP stores the current time), which the
//                             Java code was written against
//   jlangSocket               path of a unix socket to use instead of TCP (default: TCP, like
//                             Connector/J)
#pragma once

#include <jlang/jlang.h>

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace jlang {

class Connection;
class Statement;
class PreparedStatement;
class CallableStatement;
class ResultSet;
class ResultSetMetaData;
class DatabaseMetaData;
class Savepoint;
class DataSource;

namespace sql_detail {
class Config;
class Session;
class ResultMeta;
class RowData;
class ExecResult;
struct BatchEntry;
}  // namespace sql_detail

// ---------------------------------------------------------------------------------------
// java.sql.Types
class Types final {
public:
    static constexpr int32_t BIT = -7;
    static constexpr int32_t TINYINT = -6;
    static constexpr int32_t SMALLINT = 5;
    static constexpr int32_t INTEGER = 4;
    static constexpr int32_t BIGINT = -5;
    static constexpr int32_t FLOAT = 6;
    static constexpr int32_t REAL = 7;
    static constexpr int32_t DOUBLE = 8;
    static constexpr int32_t NUMERIC = 2;
    static constexpr int32_t DECIMAL = 3;
    static constexpr int32_t CHAR = 1;
    static constexpr int32_t VARCHAR = 12;
    static constexpr int32_t LONGVARCHAR = -1;
    static constexpr int32_t DATE = 91;
    static constexpr int32_t TIME = 92;
    static constexpr int32_t TIMESTAMP = 93;
    static constexpr int32_t BINARY = -2;
    static constexpr int32_t VARBINARY = -3;
    static constexpr int32_t LONGVARBINARY = -4;
    static constexpr int32_t NULL_ = 0;  // java.sql.Types.NULL (NULL is a C macro)
    static constexpr int32_t OTHER = 1111;
    static constexpr int32_t JAVA_OBJECT = 2000;
    static constexpr int32_t DISTINCT = 2001;
    static constexpr int32_t STRUCT = 2002;
    static constexpr int32_t ARRAY = 2003;
    static constexpr int32_t BLOB = 2004;
    static constexpr int32_t CLOB = 2005;
    static constexpr int32_t REF = 2006;
    static constexpr int32_t DATALINK = 70;
    static constexpr int32_t BOOLEAN = 16;
    static constexpr int32_t ROWID = -8;
    static constexpr int32_t NCHAR = -15;
    static constexpr int32_t NVARCHAR = -9;
    static constexpr int32_t LONGNVARCHAR = -16;
    static constexpr int32_t NCLOB = 2011;
    static constexpr int32_t SQLXML = 2009;
};

// ---------------------------------------------------------------------------------------
// java.sql.BatchUpdateException: thrown by executeBatch(). getUpdateCounts() has one entry per
// batched command (Statement::EXECUTE_FAILED = -3 for failed ones) when the batch continued
// after the error (continueBatchOnError, the default), or the counts of the commands executed
// before the error otherwise.
class BatchUpdateException : public SQLException {
public:
    BatchUpdateException(const String& reason, const String& sqlState, int32_t vendorCode,
                         Array<int32_t>* updateCounts)
        : SQLException(reason, sqlState, vendorCode), counts_(updateCounts) {}
    Array<int32_t>* getUpdateCounts() { return *counts_; }
    JLANG_THROWABLE(BatchUpdateException)

private:
    detail::Pinned<Array<int32_t>*> counts_;
};

// ---------------------------------------------------------------------------------------
// java.sql.Savepoint (Connector/J MysqlSavepoint): only named savepoints; getSavepointId()
// throws SQLException S1C00 "Only named savepoints are supported." (as Connector/J does).
class Savepoint : public virtual Object {
public:
    virtual int32_t getSavepointId() = 0;
    virtual String getSavepointName() = 0;
};

// ---------------------------------------------------------------------------------------
// java.sql.ResultSetMetaData (column indexes are 1-based; out of range -> SQLException S1002).
class ResultSetMetaData : public virtual Object {
public:
    int32_t getColumnCount();
    String getColumnLabel(int32_t column);      // the alias (AS name), else the column name
    String getColumnName(int32_t column);       // the original column name (Connector/J 5.1)
    String getTableName(int32_t column);        // original table name
    String getCatalogName(int32_t column);      // database
    String getSchemaName(int32_t column);       // "" (MySQL has no schemas)
    int32_t getColumnType(int32_t column);      // java.sql.Types value
    String getColumnTypeName(int32_t column);   // "INT", "VARCHAR", "INT UNSIGNED", ...
    int32_t getColumnDisplaySize(int32_t column);
    int32_t getPrecision(int32_t column);
    int32_t getScale(int32_t column);
    int32_t isNullable(int32_t column);         // columnNoNulls 0 / columnNullable 1
    bool isAutoIncrement(int32_t column);
    bool isSigned(int32_t column);
    bool isCaseSensitive(int32_t column);
    bool isReadOnly(int32_t column);

    static constexpr int32_t columnNoNulls = 0;
    static constexpr int32_t columnNullable = 1;
    static constexpr int32_t columnNullableUnknown = 2;

    explicit ResultSetMetaData(sql_detail::ResultMeta* meta) : meta_(meta) {}

private:
    sql_detail::ResultMeta* meta_;
};

// ---------------------------------------------------------------------------------------
// java.sql.ResultSet
//
// All rows are in memory: navigation methods work on every result set regardless of the
// requested type. Getters take a 1-based column index or a column label (case-insensitive;
// "tableAlias.label" also works); an unknown label throws SQLException S0022
// "Column 'x' not found.". Reading while not on a row throws SQLException S1000
// ("Before start of result set", "After end of result set",
// "Illegal operation on empty result set."). Conversions follow Connector/J 5.1 on the text
// protocol:
//   * getInt/getLong/getShort/getByte/getFloat/getDouble: SQL NULL -> 0 (wasNull() true);
//     '' -> 0; text is parsed (integers stop at the first non-digit: '12abc' -> 12, '1.9' -> 1
//     for getInt); values out of the type's range throw SQLException 22003; unparsable text
//     throws S1009 "Invalid value for getInt() - 'abc'".
//   * getBoolean: numeric columns: v == -1 || v > 0; text: first char t/y/1 or "-1".
//   * getString: raw text, except DATETIME/TIMESTAMP -> Timestamp.toString() format
//     ('2011-06-21 10:00:00.0'), DATE -> 'yyyy-mm-dd', YEAR -> 'yyyy-01-01', BIT -> number.
//   * getBytes: the raw column bytes (BLOB data; UTF-8 for text columns).
//   * getTimestamp: DATETIME/TIMESTAMP/DATE/TIME text interpreted in the local time zone;
//     zero dates per zeroDateTimeBehavior; nullptr for SQL NULL.
class ResultSet : public virtual Object {
public:
    static constexpr int32_t FETCH_FORWARD = 1000;
    static constexpr int32_t FETCH_REVERSE = 1001;
    static constexpr int32_t FETCH_UNKNOWN = 1002;
    static constexpr int32_t TYPE_FORWARD_ONLY = 1003;
    static constexpr int32_t TYPE_SCROLL_INSENSITIVE = 1004;
    static constexpr int32_t TYPE_SCROLL_SENSITIVE = 1005;
    static constexpr int32_t CONCUR_READ_ONLY = 1007;
    static constexpr int32_t CONCUR_UPDATABLE = 1008;
    static constexpr int32_t HOLD_CURSORS_OVER_COMMIT = 1;
    static constexpr int32_t CLOSE_CURSORS_AT_COMMIT = 2;

    // Navigation.
    bool next();
    bool previous();
    bool first();
    bool last();
    void beforeFirst();
    void afterLast();
    bool absolute(int32_t row);   // row 0 -> SQLException S1009
    bool relative(int32_t rows);
    int32_t getRow();             // 1-based, 0 when not on a row
    bool isBeforeFirst();
    bool isAfterLast();
    bool isFirst();
    bool isLast();

    // Idempotent, never throws. Operations after close throw SQLException S1000
    // "Operation not allowed after ResultSet closed".
    void close() noexcept;
    bool isClosed() noexcept { return closed_; }
    bool wasNull();
    int32_t findColumn(const String& columnLabel);
    int32_t findColumn(const char* columnLabel);  // same, without building a String
    ResultSetMetaData* getMetaData();
    Statement* getStatement();
    int32_t getType() { return type_; }
    int32_t getConcurrency() { return concurrency_; }
    void setFetchSize(int32_t rows) { fetchSize_ = rows; }
    int32_t getFetchSize() { return fetchSize_; }
    void setFetchDirection(int32_t) {}
    int32_t getFetchDirection() { return FETCH_FORWARD; }
    SQLException* getWarnings() { return nullptr; }
    void clearWarnings() {}

    String getString(int32_t columnIndex);
    String getString(const String& columnLabel) { return getString(findColumn(columnLabel)); }
    String getString(const char* columnLabel) { return getString(findColumn(columnLabel)); }
    bool getBoolean(int32_t columnIndex);
    bool getBoolean(const String& columnLabel) { return getBoolean(findColumn(columnLabel)); }
    bool getBoolean(const char* columnLabel) { return getBoolean(findColumn(columnLabel)); }
    int8_t getByte(int32_t columnIndex);
    int8_t getByte(const String& columnLabel) { return getByte(findColumn(columnLabel)); }
    int8_t getByte(const char* columnLabel) { return getByte(findColumn(columnLabel)); }
    int16_t getShort(int32_t columnIndex);
    int16_t getShort(const String& columnLabel) { return getShort(findColumn(columnLabel)); }
    int16_t getShort(const char* columnLabel) { return getShort(findColumn(columnLabel)); }
    int32_t getInt(int32_t columnIndex);
    int32_t getInt(const String& columnLabel) { return getInt(findColumn(columnLabel)); }
    int32_t getInt(const char* columnLabel) { return getInt(findColumn(columnLabel)); }
    int64_t getLong(int32_t columnIndex);
    int64_t getLong(const String& columnLabel) { return getLong(findColumn(columnLabel)); }
    int64_t getLong(const char* columnLabel) { return getLong(findColumn(columnLabel)); }
    float getFloat(int32_t columnIndex);
    float getFloat(const String& columnLabel) { return getFloat(findColumn(columnLabel)); }
    float getFloat(const char* columnLabel) { return getFloat(findColumn(columnLabel)); }
    double getDouble(int32_t columnIndex);
    double getDouble(const String& columnLabel) { return getDouble(findColumn(columnLabel)); }
    double getDouble(const char* columnLabel) { return getDouble(findColumn(columnLabel)); }
    // nullptr for SQL NULL.
    Array<int8_t>* getBytes(int32_t columnIndex);
    Array<int8_t>* getBytes(const String& columnLabel) { return getBytes(findColumn(columnLabel)); }
    Array<int8_t>* getBytes(const char* columnLabel) { return getBytes(findColumn(columnLabel)); }
    // nullptr for SQL NULL (and for zero dates with zeroDateTimeBehavior=convertToNull).
    Timestamp* getTimestamp(int32_t columnIndex);
    Timestamp* getTimestamp(const String& columnLabel) { return getTimestamp(findColumn(columnLabel)); }
    Timestamp* getTimestamp(const char* columnLabel) { return getTimestamp(findColumn(columnLabel)); }
    // getObject: boxed value by column type (Integer, Long, Float, Double, Boolean for BIT(1),
    // String (boxed) for text/decimal, Array<int8_t>* for binary, Timestamp* for date/time
    // types); nullptr for SQL NULL.
    Object* getObject(int32_t columnIndex);
    Object* getObject(const String& columnLabel) { return getObject(findColumn(columnLabel)); }
    Object* getObject(const char* columnLabel) { return getObject(findColumn(columnLabel)); }

    // --- implementation -----------------------------------------------------------------
    ResultSet(Statement* owner, sql_detail::Config* cfg, sql_detail::ExecResult* r, int32_t type,
              int32_t concurrency);
    ResultSet* nextResultSet_ = nullptr;  // following result of a multi-result execution
    bool reallyResult() const noexcept { return meta_ != nullptr; }
    int64_t updateCount() const noexcept { return updateCount_; }
    int64_t updateId() const noexcept { return updateId_; }
    const String& serverInfo() const noexcept { return info_; }
    char firstCharOfQuery_ = 0;

private:
    void checkClosed();
    void checkRowPos();
    void checkColumnBounds(int32_t columnIndex);
    void setRowPositionValidity();
    bool cellIsNull(int32_t col0);
    const char* cell(int32_t col0, int64_t* len);
    String cellString(int32_t col0);
    String getStringInternal(int32_t columnIndex, bool checkDateTypes);
    int32_t convertToZeroWithEmptyCheck();
    int64_t getLongInternal(int32_t columnIndex, bool overflowCheck);
    int64_t numericBits(int32_t columnIndex);
    bool byteArrayToBoolean(int32_t col0);
    [[noreturn]] void throwRangeException(const String& value, int32_t columnIndex, int32_t jdbcType);
    int32_t parseIntAsDouble(int32_t columnIndex, const String& val);
    int64_t parseLongAsDouble(int32_t col0, const String& val);
    int16_t parseShortAsDouble(int32_t columnIndex, const String& val);
    void checkForIntegerTruncation(int32_t col0, int32_t value);
    void checkForLongTruncation(int32_t col0, int64_t value);
    Timestamp* timestampFromCell(int32_t col0);
    Timestamp* timestampFromString(int32_t columnIndex, String value);
    int32_t findColumnIndex(std::string_view columnLabel);
    Calendar* calendar();
    String dateStringFromString(int32_t columnIndex, const String& value);
    String timeStringFromString(int32_t columnIndex, const String& value);

    Statement* owner_;
    sql_detail::Config* cfg_;
    sql_detail::ResultMeta* meta_;
    sql_detail::RowData* rows_;
    int64_t updateCount_ = -1;
    int64_t updateId_ = 0;
    String info_;
    int32_t index_ = -1;
    int32_t type_;
    int32_t concurrency_;
    int32_t fetchSize_ = 0;
    bool closed_ = false;
    bool wasNull_ = false;
    bool onValidRow_ = false;
    const char* invalidRowReason_ = nullptr;
    Calendar* cal_ = nullptr;  // session calendar (default zone), created on first use
};

// ---------------------------------------------------------------------------------------
// java.sql.Statement
class Statement : public virtual Object {
public:
    static constexpr int32_t CLOSE_CURRENT_RESULT = 1;
    static constexpr int32_t KEEP_CURRENT_RESULT = 2;
    static constexpr int32_t CLOSE_ALL_RESULTS = 3;
    static constexpr int32_t SUCCESS_NO_INFO = -2;
    static constexpr int32_t EXECUTE_FAILED = -3;
    static constexpr int32_t RETURN_GENERATED_KEYS = 1;
    static constexpr int32_t NO_GENERATED_KEYS = 2;

    // executeQuery on INSERT/UPDATE/DELETE/DROP/CREATE/ALTER throws SQLException S1009; a
    // statement without a result set yields a ResultSet whose next() throws S1000.
    virtual ResultSet* executeQuery(const String& sql);
    // executeUpdate on a SELECT throws SQLException 01S03. Returns the update count (rows
    // matched for UPDATE).
    virtual int32_t executeUpdate(const String& sql);
    virtual int32_t executeUpdate(const String& sql, int32_t autoGeneratedKeys);
    // true if the first result is a result set.
    virtual bool execute(const String& sql);
    virtual bool execute(const String& sql, int32_t autoGeneratedKeys);
    virtual void addBatch(const String& sql);
    virtual void clearBatch();
    // Executes the batch serially (like Connector/J without rewriteBatchedStatements).
    // Throws BatchUpdateException; an empty batch returns an empty array.
    virtual Array<int32_t>* executeBatch();
    // The current result set (nullptr if the current result is an update count).
    ResultSet* getResultSet();
    // The current update count, -1 if the current result is a result set or there is none.
    int32_t getUpdateCount();
    bool getMoreResults();
    bool getMoreResults(int32_t current);
    // AUTO_INCREMENT values of the last execution (column "GENERATED_KEY", one row per
    // inserted row: LAST_INSERT_ID() + i * @@auto_increment_increment). Requires
    // RETURN_GENERATED_KEYS, else SQLException S1009.
    ResultSet* getGeneratedKeys();
    // Idempotent, never throws; closes the statement's result sets.
    void close() noexcept;
    bool isClosed() noexcept { return closed_; }
    Connection* getConnection() { return conn_; }
    void setFetchSize(int32_t rows) { fetchSize_ = rows; }  // no-op (results are buffered)
    int32_t getFetchSize() { return fetchSize_; }
    void setFetchDirection(int32_t) {}
    int32_t getFetchDirection() { return ResultSet::FETCH_FORWARD; }
    void setQueryTimeout(int32_t seconds) { queryTimeout_ = seconds; }  // no-op
    int32_t getQueryTimeout() { return queryTimeout_; }
    void setMaxRows(int32_t max);   // rows beyond max are dropped from result sets (0 = all)
    int32_t getMaxRows() { return maxRows_; }
    void setEscapeProcessing(bool) {}
    void setPoolable(bool) {}
    void cancel() {}
    int32_t getResultSetType() { return rsType_; }
    int32_t getResultSetConcurrency() { return rsConcurrency_; }
    SQLException* getWarnings() { return nullptr; }
    void clearWarnings() {}

    // --- implementation -----------------------------------------------------------------
    Statement(Connection* conn, int32_t resultSetType, int32_t resultSetConcurrency);
    void closeFromConnection() noexcept;  // Connection::close: closes without unregistering
    void setRetrieveGeneratedKeys(bool v) { retrieveGeneratedKeys_ = v; }

protected:
    void checkClosed();
    sql_detail::Session* session();
    void closeResults(bool includeGeneratedKeys) noexcept;
    ResultSet* generatedKeysInternal(int64_t numKeys, bool track = true);
    void trackOpenResult(ResultSet* rs);
    int32_t executeUpdateInternal(const String& sql, bool returnGeneratedKeys, bool isBatch);
    bool executeInternal(const String& sql, bool returnGeneratedKeys);
    bool hasDeadlockOrTimeoutRolledBackTx(SQLException& e);

    Connection* conn_;
    ResultSet* results_ = nullptr;
    std::vector<ResultSet*> openResults_;
    std::vector<sql_detail::BatchEntry*> batch_;
    int64_t lastInsertId_ = 0;
    int64_t updateCount_ = -1;
    int32_t rsType_;
    int32_t rsConcurrency_;
    int32_t fetchSize_ = 0;
    int32_t queryTimeout_ = 0;
    int32_t maxRows_ = 0;
    bool closed_ = false;
    bool retrieveGeneratedKeys_ = false;
    bool lastQueryIsOnDupKeyUpdate_ = false;
    std::vector<std::string> batchedKeys_;  // GENERATED_KEY values collected by executeBatch
    bool haveBatchedKeys_ = false;
};

// ---------------------------------------------------------------------------------------
// java.sql.PreparedStatement (client-side parameter substitution, Connector/J
// useServerPrepStmts=false). Parameter indexes are 1-based; out of range -> SQLException
// S1009 "Parameter index out of range (2 > number of parameters, which is 1).".
//   setBoolean -> 1/0; setByte/Short/Int/Long -> decimal; setFloat/setDouble -> Java's
//   Float/Double.toString with an explicit exponent sign ('1.0E+10'); setDouble(NaN/Infinity)
//   throws S1009, setFloat(NaN) sends NaN (the server rejects it); setString -> quoted and
//   escaped (mysql_real_escape_string), null -> NULL; setBytes -> x'hex', null -> NULL;
//   setTimestamp -> 'yyyy-MM-dd HH:mm:ss' in the local time zone (Connector/J 5.1.13 sends no
//   fractional seconds), null -> NULL; setNull -> NULL; setObject -> by the boxed type.
class PreparedStatement : public Statement {
public:
    using Statement::addBatch;
    using Statement::execute;
    using Statement::executeQuery;
    using Statement::executeUpdate;

    virtual ResultSet* executeQuery();
    virtual int32_t executeUpdate();
    virtual bool execute();
    virtual void addBatch();
    Array<int32_t>* executeBatch() override;
    void clearParameters();
    int32_t getParameterCount() { return static_cast<int32_t>(values_.size()); }

    void setNull(int32_t parameterIndex, int32_t sqlType);
    void setBoolean(int32_t parameterIndex, bool x);
    void setByte(int32_t parameterIndex, int8_t x);
    void setShort(int32_t parameterIndex, int16_t x);
    void setInt(int32_t parameterIndex, int32_t x);
    void setLong(int32_t parameterIndex, int64_t x);
    void setFloat(int32_t parameterIndex, float x);
    void setDouble(int32_t parameterIndex, double x);
    void setString(int32_t parameterIndex, const String& x);
    void setBytes(int32_t parameterIndex, Array<int8_t>* x);
    void setTimestamp(int32_t parameterIndex, Timestamp* x);
    // Boxed values: Integer/Long/Short/Byte/Float/Double/Boolean, boxed String, Timestamp,
    // Array<int8_t>*; nullptr -> NULL. Other objects: their toString() as a string.
    void setObject(int32_t parameterIndex, Object* x);

    // --- implementation -----------------------------------------------------------------
    PreparedStatement(Connection* conn, const String& sql, int32_t resultSetType,
                      int32_t resultSetConcurrency);
    const String& getSql() { return sql_; }

protected:
    void setInternal(int32_t parameterIndex, std::string value);
    void setTimestampMillis(int32_t parameterIndex, int64_t millis);
    std::string fillSendPacket(const std::vector<std::string>& values,
                               const std::vector<bool>& isSet);
    int32_t executeUpdateInternal(const std::vector<std::string>& values,
                                  const std::vector<bool>& isSet, bool isBatch);
    bool isSelectQuery();

    String sql_;
    std::vector<std::string> staticSql_;  // sql split at the '?' placeholders
    std::vector<std::string> values_;
    std::vector<bool> isSet_;
    char firstCharOfStmt_ = 0;
    bool isOnDuplicateKeyUpdate_ = false;
    Calendar* tsCal_ = nullptr;  // setTimestamp's calendar (Connector/J's per-statement tsdf)
};

// ---------------------------------------------------------------------------------------
// java.sql.CallableStatement: "CALL proc(?, ...)" (or "{call proc(?, ...)}") with IN
// parameters, executed like a PreparedStatement; executeQuery() returns the procedure's first
// result set, getMoreResults() moves to the following ones. OUT parameters are not supported.
class CallableStatement : public PreparedStatement {
public:
    CallableStatement(Connection* conn, const String& sql, int32_t resultSetType,
                      int32_t resultSetConcurrency);
    // Rewrites the JDBC escape "{call p(...)}" / "{CALL p(...)}" to "CALL p(...)".
    static String nativeCallSql(const String& sql);
};

// ---------------------------------------------------------------------------------------
// java.sql.DatabaseMetaData (the parts the code base uses, plus a few informational ones).
// Emulates Connector/J 5.1 against the server: product name "MySQL"; MariaDB 10+ announces
// itself as "5.5.5-10.x.y-MariaDB" so the major/minor versions are 5 and 5 (which is what
// makes the MySQL5 DAOs register); a real MySQL server reports its own version.
class DatabaseMetaData : public virtual Object {
public:
    String getDatabaseProductName();     // "MySQL"
    String getDatabaseProductVersion();  // "5.5.5-10.11.14-MariaDB-..." / "8.0.35"
    int32_t getDatabaseMajorVersion();
    int32_t getDatabaseMinorVersion();
    String getDriverName();              // "MySQL-AB JDBC Driver"
    String getDriverVersion();
    int32_t getDriverMajorVersion() { return 5; }
    int32_t getDriverMinorVersion() { return 1; }
    int32_t getJDBCMajorVersion() { return 4; }
    int32_t getJDBCMinorVersion() { return 0; }
    String getURL();
    String getUserName();                // SELECT USER(), e.g. "aion@localhost"
    String getIdentifierQuoteString() { return "`"; }
    bool supportsTransactions() { return true; }
    bool supportsSavepoints() { return true; }
    bool supportsBatchUpdates() { return true; }
    bool supportsGetGeneratedKeys() { return true; }
    Connection* getConnection() { return conn_; }

    explicit DatabaseMetaData(Connection* conn) : conn_(conn) {}

private:
    Connection* conn_;
};

// ---------------------------------------------------------------------------------------
// java.sql.Connection
//
// A Connection from DriverManager owns its server session; a Connection from a DataSource is
// a handle on a pooled session: close() closes the handle's statements, rolls back an open
// transaction, restores auto-commit and returns the session to the pool (commons-dbcp). Every
// operation on a closed Connection throws SQLException (08003 "No operations allowed after
// connection closed."; pooled handles: "Connection is closed.").
class Connection : public virtual Object {
public:
    static constexpr int32_t TRANSACTION_NONE = 0;
    static constexpr int32_t TRANSACTION_READ_UNCOMMITTED = 1;
    static constexpr int32_t TRANSACTION_READ_COMMITTED = 2;
    static constexpr int32_t TRANSACTION_REPEATABLE_READ = 4;
    static constexpr int32_t TRANSACTION_SERIALIZABLE = 8;

    Statement* createStatement();
    Statement* createStatement(int32_t resultSetType, int32_t resultSetConcurrency);
    PreparedStatement* prepareStatement(const String& sql);
    // autoGeneratedKeys: Statement::RETURN_GENERATED_KEYS enables getGeneratedKeys().
    PreparedStatement* prepareStatement(const String& sql, int32_t autoGeneratedKeys);
    PreparedStatement* prepareStatement(const String& sql, int32_t resultSetType,
                                        int32_t resultSetConcurrency);
    CallableStatement* prepareCall(const String& sql);
    CallableStatement* prepareCall(const String& sql, int32_t resultSetType,
                                   int32_t resultSetConcurrency);
    String nativeSQL(const String& sql) { return sql; }

    // Sends SET autocommit=0/1 (committing an open transaction when switching to true).
    void setAutoCommit(bool autoCommit);
    bool getAutoCommit();
    // SQLException "Can't call commit when autocommit=true" in auto-commit mode.
    void commit();
    // SQLException 08003 "Can't call rollback when autocommit=true" in auto-commit mode.
    void rollback();
    // ROLLBACK TO SAVEPOINT `name` (no auto-commit check, like Connector/J).
    void rollback(Savepoint* savepoint);
    Savepoint* setSavepoint();                  // unnamed: a generated unique name
    Savepoint* setSavepoint(const String& name);  // null/empty name -> SQLException S1009
    // No-op, exactly like Connector/J 5.1 (the savepoint stays until the transaction ends).
    void releaseSavepoint(Savepoint* savepoint);
    // Flag only (Connector/J 5.1 against a 5.5 server): executeUpdate/execute of non-SELECTs
    // then throw S1009 "Connection is read-only. ..."; pooled handles start read-write.
    void setReadOnly(bool readOnly);
    bool isReadOnly();
    void setTransactionIsolation(int32_t level);  // SET SESSION TRANSACTION ISOLATION LEVEL ...
    int32_t getTransactionIsolation();
    void setCatalog(const String& catalog);       // USE `catalog`
    String getCatalog();
    DatabaseMetaData* getMetaData();
    SQLException* getWarnings() { return nullptr; }
    void clearWarnings() {}
    void setHoldability(int32_t) {}
    int32_t getHoldability() { return ResultSet::HOLD_CURSORS_OVER_COMMIT; }

    // Idempotent and never throws (closes statements/result sets; pooled: back to the pool).
    void close() noexcept;
    // True after close(), and after a communication failure without autoReconnect.
    bool isClosed() noexcept;
    // Pings the server; on failure the connection is closed (Connector/J abortInternal) and
    // false returned. The timeout is not applied (Connector/C cannot change the read timeout of
    // an open handle; the URL's socketTimeout applies).
    bool isValid(int32_t timeoutSeconds);

    // --- implementation -----------------------------------------------------------------
    Connection(sql_detail::Session* session, DataSource* pool);
    sql_detail::Session* session();              // checks closed
    sql_detail::Session* sessionOrNull() noexcept { return session_; }
    void registerStatement(Statement* s);
    void unregisterStatement(Statement* s) noexcept;
    String getURL();

private:
    [[noreturn]] void throwClosed();
    sql_detail::Session* session_;
    DataSource* pool_;
    std::vector<Statement*> statements_;
    bool closed_ = false;
};

// ---------------------------------------------------------------------------------------
// javax.sql.DataSource: a connection pool with the behavior of commons-dbcp 1.4
// PoolingDataSource over a commons-pool 1.5 GenericObjectPool (defaults: maxActive 8,
// maxIdle 8, minIdle 0, whenExhaustedAction BLOCK, maxWait -1 = forever, LIFO idle list,
// connections created lazily).
//
//   getConnection(): an idle session (most recently returned first) or a new one while fewer
//   than maxActive are borrowed; otherwise blocks until one is returned (BLOCK), fails
//   (FAIL: SQLException "Cannot get a connection, pool error Pool exhausted") or creates
//   one anyway (GROW). maxWait >= 0 limits the wait (SQLException "Cannot get a connection,
//   pool error Timeout waiting for idle object"). A closed pool throws IllegalStateException
//   "Pool not open". Connection failures propagate as the SQLException of the driver.
//   Borrowed connections start with autoCommit = defaultAutoCommit (true) and read-write.
//   Returned connections: statements closed, open transaction rolled back, autoCommit
//   restored; kept idle unless maxIdle idle ones exist already, then closed.
//
// jlang addition (a deviation from dbcp, which never validated with the Java configuration):
// a session idle for longer than validationIdleMillis (default 30000; -1 disables) is pinged
// when borrowed and replaced if the server dropped it (wait_timeout), so the first query after
// a long idle period does not fail.
class DataSource : public virtual Object {
public:
    static constexpr int8_t WHEN_EXHAUSTED_FAIL = 0;
    static constexpr int8_t WHEN_EXHAUSTED_BLOCK = 1;
    static constexpr int8_t WHEN_EXHAUSTED_GROW = 2;

    // Validates the URL (SQLException for a malformed one); does not connect.
    DataSource(const String& url, const String& user, const String& password);

    Connection* getConnection();
    // UnsupportedOperationException, like PoolingDataSource.
    Connection* getConnection(const String& user, const String& password);

    void setMaxActive(int32_t maxActive);   // < 0: unlimited
    int32_t getMaxActive();
    void setMaxIdle(int32_t maxIdle);       // < 0: unlimited
    int32_t getMaxIdle();
    void setMinIdle(int32_t minIdle);       // honored by preparePool()
    int32_t getMinIdle();
    void setMaxWait(int64_t maxWaitMillis);  // < 0: wait forever
    int64_t getMaxWait();
    void setWhenExhaustedAction(int8_t action);
    int8_t getWhenExhaustedAction();
    void setDefaultAutoCommit(bool v);
    bool getDefaultAutoCommit();
    void setDefaultReadOnly(bool v);
    bool getDefaultReadOnly();
    void setValidationIdleMillis(int64_t millis);
    int64_t getValidationIdleMillis();

    int32_t getNumActive();
    int32_t getNumIdle();
    // Creates sessions until getNumIdle() >= minIdle (GenericObjectPool.preparePool()).
    void preparePool();
    // Closes the idle sessions and refuses further borrows; borrowed ones are closed when
    // returned. Idempotent, never throws.
    void close() noexcept;
    bool isClosed();
    String getUrl() { return url_; }
    String getUsername() { return user_; }

    // --- implementation -----------------------------------------------------------------
    void returnSession(sql_detail::Session* s) noexcept;  // Connection::close of a handle

private:
    sql_detail::Session* createSession();
    void destroySession(sql_detail::Session* s) noexcept;

    String url_;
    String user_;
    String password_;
    sql_detail::Config* config_;
    std::deque<sql_detail::Session*> idle_;
    int32_t numActive_ = 0;
    int32_t maxActive_ = 8;
    int32_t maxIdle_ = 8;
    int32_t minIdle_ = 0;
    int64_t maxWait_ = -1;
    int64_t validationIdleMillis_ = 30000;
    int8_t whenExhausted_ = WHEN_EXHAUSTED_BLOCK;
    bool defaultAutoCommit_ = true;
    bool defaultReadOnly_ = false;
    bool closed_ = false;
};

// Alias for org.apache.commons.dbcp.PoolingDataSource (jdkmap maps both to jlang::DataSource).
using PoolingDataSource = DataSource;

// ---------------------------------------------------------------------------------------
// java.sql.DriverManager: unpooled connections. The URL must start with "jdbc:mysql://"
// (other URLs: SQLException 08001 "No suitable driver found for ...").
class DriverManager final {
public:
    static Connection* getConnection(const String& url, const String& user, const String& password);
    static Connection* getConnection(const String& url);  // user/password from the URL
    static void setLoginTimeout(int32_t seconds);
    static int32_t getLoginTimeout();
};

}  // namespace jlang
