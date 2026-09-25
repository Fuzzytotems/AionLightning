// jlang/src/sql_statement.cpp - java.sql.Statement, PreparedStatement (client-side parameter
// substitution) and CallableStatement with Connector/J 5.1.13 semantics. See jlang/Sql.h.
#include "sql_internal.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace jlang {

using namespace sql_detail;

namespace {

int32_t truncateCount(int64_t v) {
    return v > INT32_MAX ? INT32_MAX : static_cast<int32_t>(v);
}

void checkNullOrEmptyQuery(const String& sql) {
    if (sql.isNull()) throwSql("Can not issue NULL query.", "S1009");
    if (sql.length() == 0) throwSql("Can not issue empty query.", "S1009");
}

// StatementImpl.checkForDml
void checkForDml(std::string_view sql, char firstStatementChar) {
    if (firstStatementChar == 'I' || firstStatementChar == 'U' || firstStatementChar == 'D' ||
        firstStatementChar == 'A' || firstStatementChar == 'C') {
        if (startsWithKeywordIgnoringComments(sql, "INSERT") || startsWithKeywordIgnoringComments(sql, "UPDATE") ||
            startsWithKeywordIgnoringComments(sql, "DELETE") || startsWithKeywordIgnoringComments(sql, "DROP") ||
            startsWithKeywordIgnoringComments(sql, "CREATE") || startsWithKeywordIgnoringComments(sql, "ALTER")) {
            throwSql("Can not issue data manipulation statements with executeQuery().", "S1009");
        }
    }
}

// StatementImpl.getRecordCountFromInfo ("Records: 3  Duplicates: 1  Warnings: 0" -> 2)
int32_t recordCountFromInfo(std::string_view info) {
    size_t i = 0;
    auto number = [&]() -> int64_t {
        while (i < info.size() && !(info[i] >= '0' && info[i] <= '9')) i++;
        int64_t v = 0;
        bool any = false;
        while (i < info.size() && info[i] >= '0' && info[i] <= '9') {
            v = v * 10 + (info[i] - '0');
            i++;
            any = true;
        }
        if (!any) throw NumberFormatException(String("For input string: \"\""));
        return v;
    };
    int64_t records = number();
    int64_t duplicates = number();
    return static_cast<int32_t>(records - duplicates);
}

// StringUtils.fixDecimalExponent: "1.0E10" -> "1.0E+10"
std::string fixDecimalExponent(const String& s) {
    std::string r(s);
    size_t e = r.find('E');
    if (e == std::string::npos) e = r.find('e');
    if (e != std::string::npos && r.size() > e + 1 && r[e + 1] != '-' && r[e + 1] != '+') r.insert(e + 1, "+");
    return r;
}

bool isDeadlockOrLockTableFull(SQLException& e) {
    int32_t code = e.getErrorCode();
    return code == 1213 || code == 1206;  // ER_LOCK_DEADLOCK, ER_LOCK_TABLE_FULL
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Statement
Statement::Statement(Connection* conn, int32_t resultSetType, int32_t resultSetConcurrency)
    : conn_(conn), rsType_(resultSetType), rsConcurrency_(resultSetConcurrency) {}

void Statement::checkClosed() {
    if (closed_) throwSql("No operations allowed after statement closed.", "08003");
}

Session* Statement::session() {
    checkClosed();
    return conn_->session();
}

void Statement::setMaxRows(int32_t max) {
    checkClosed();
    if (max < 0 || max > 50000000) throwSql("setMaxRows() out of range. ", "S1009");
    maxRows_ = max;
}

void Statement::closeResults(bool includeGeneratedKeys) noexcept {
    for (ResultSet* r = results_; r != nullptr; r = r->nextResultSet_) r->close();
    if (includeGeneratedKeys) {
        for (ResultSet* r : openResults_) r->close();
        openResults_.clear();
    }
}

void Statement::close() noexcept {
    if (closed_) return;
    closeFromConnection();
    if (conn_ != nullptr) conn_->unregisterStatement(this);
}

void Statement::closeFromConnection() noexcept {
    closed_ = true;
    closeResults(true);
    try {
        batch_.clear();
        batchedKeys_.clear();
    } catch (...) {
    }
}

namespace {
ResultSet* makeResults(Statement* owner, Config* cfg, ExecResult* r, int32_t type, int32_t conc, char firstChar) {
    ResultSet* first = nullptr;
    ResultSet* prev = nullptr;
    for (; r != nullptr; r = r->next) {
        auto* rs = new ResultSet(owner, cfg, r, type, conc);
        rs->firstCharOfQuery_ = firstChar;
        if (first == nullptr) first = rs;
        else prev->nextResultSet_ = rs;
        prev = rs;
    }
    return first;
}
}  // namespace

ResultSet* Statement::executeQuery(const String& sql) {
    checkClosed();
    Session* s = session();
    retrieveGeneratedKeys_ = false;
    haveBatchedKeys_ = false;
    checkNullOrEmptyQuery(sql);
    char first = firstNonWsCharUc(sql, findStartOfStatement(sql));
    checkForDml(sql, first);
    if (results_ != nullptr) closeResults(false);
    ExecResult* r = s->execute(sql, maxRows_);
    results_ = makeResults(this, s->cfg, r, rsType_, rsConcurrency_, first);
    lastInsertId_ = results_->updateId();
    return results_;
}

int32_t Statement::executeUpdate(const String& sql) {
    haveBatchedKeys_ = false;
    return executeUpdateInternal(sql, false, false);
}

int32_t Statement::executeUpdate(const String& sql, int32_t autoGeneratedKeys) {
    haveBatchedKeys_ = false;
    return executeUpdateInternal(sql, autoGeneratedKeys == RETURN_GENERATED_KEYS, false);
}

int32_t Statement::executeUpdateInternal(const String& sql, bool returnGeneratedKeys, bool isBatch) {
    (void)isBatch;
    checkClosed();
    Session* s = session();
    char firstStatementChar = firstAlphaCharUc(sql, findStartOfStatement(sql));
    retrieveGeneratedKeys_ = returnGeneratedKeys;
    lastQueryIsOnDupKeyUpdate_ = false;
    checkNullOrEmptyQuery(sql);
    if (s->readOnly) {
        throwSql("Connection is read-only. Queries leading to data modification are not allowed.", "S1009");
    }
    if (startsWithIgnoreCaseAndWs(sql, "select")) throwSql("Can not issue SELECT via executeUpdate().", "01S03");
    if (results_ != nullptr) closeResults(false);
    ExecResult* r = s->execute(sql, 0);
    results_ = makeResults(this, s->cfg, r, rsType_, rsConcurrency_, firstStatementChar);
    updateCount_ = results_->updateCount();
    lastInsertId_ = results_->updateId();
    return truncateCount(updateCount_);
}

bool Statement::execute(const String& sql) {
    return executeInternal(sql, false);
}

bool Statement::execute(const String& sql, int32_t autoGeneratedKeys) {
    return executeInternal(sql, autoGeneratedKeys == RETURN_GENERATED_KEYS);
}

bool Statement::executeInternal(const String& sql, bool returnGeneratedKeys) {
    checkClosed();
    Session* s = session();
    retrieveGeneratedKeys_ = returnGeneratedKeys;
    haveBatchedKeys_ = false;
    checkNullOrEmptyQuery(sql);
    char firstNonWsChar = firstAlphaCharUc(sql, findStartOfStatement(sql));
    lastQueryIsOnDupKeyUpdate_ = returnGeneratedKeys && firstNonWsChar == 'I' && onDuplicateKeyLocation(sql) != -1;
    if (firstNonWsChar != 'S' && s->readOnly) {
        throwSql("Connection is read-only. Queries leading to data modification are not allowed.", "S1009");
    }
    if (results_ != nullptr) closeResults(false);
    ExecResult* r = s->execute(sql, maxRows_);
    results_ = makeResults(this, s->cfg, r, rsType_, rsConcurrency_, firstNonWsChar);
    lastInsertId_ = results_->updateId();
    return results_->reallyResult();
}

void Statement::addBatch(const String& sql) {
    checkClosed();
    if (sql.isNull()) return;
    auto* e = new BatchEntry();
    e->isSql = true;
    e->sql = sql;
    batch_.push_back(e);
}

void Statement::clearBatch() {
    batch_.clear();
}

bool Statement::hasDeadlockOrTimeoutRolledBackTx(SQLException& e) {
    return isDeadlockOrLockTableFull(e);
}

Array<int32_t>* Statement::executeBatch() {
    checkClosed();
    Session* s = session();
    if (s->readOnly) {
        throwSql("Connection is read-only. Queries leading to data modification are not allowed.", "S1009");
    }
    if (results_ != nullptr) closeResults(false);
    if (batch_.empty()) return new Array<int32_t>(0);
    std::vector<BatchEntry*> entries;
    entries.swap(batch_);  // clearBatch() in Java's finally
    retrieveGeneratedKeys_ = true;
    batchedKeys_.clear();
    int32_t n = static_cast<int32_t>(entries.size());
    auto* counts = new Array<int32_t>(n);
    for (int32_t i = 0; i < n; i++) (*counts)[i] = EXECUTE_FAILED;
    bool failed = false;
    String failMsg, failState;
    int32_t failCode = 0;
    for (int32_t i = 0; i < n; i++) {
        try {
            const std::string& sql = entries[static_cast<size_t>(i)]->sql;
            (*counts)[i] = executeUpdateInternal(String(sql), true, true);
            ResultSet* keys = generatedKeysInternal(onDuplicateKeyLocation(sql) != -1 ? 1 : updateCount_, false);
            while (keys->next()) batchedKeys_.push_back(std::string(keys->getString(1)));
        } catch (SQLException& ex) {
            (*counts)[i] = EXECUTE_FAILED;
            if (s->cfg->continueBatchOnError && !hasDeadlockOrTimeoutRolledBackTx(ex)) {
                failed = true;
                failMsg = ex.getMessage();
                failState = ex.getSQLState();
                failCode = ex.getErrorCode();
                continue;
            }
            auto* partial = new Array<int32_t>(i);
            bool deadlock = hasDeadlockOrTimeoutRolledBackTx(ex);
            for (int32_t k = 0; k < i; k++) (*partial)[k] = deadlock ? EXECUTE_FAILED : (*counts)[k];
            haveBatchedKeys_ = true;
            throw BatchUpdateException(ex.getMessage(), ex.getSQLState(), ex.getErrorCode(), partial);
        }
    }
    haveBatchedKeys_ = true;
    if (failed) throw BatchUpdateException(failMsg, failState, failCode, counts);
    return counts;
}

ResultSet* Statement::getResultSet() {
    return results_ != nullptr && results_->reallyResult() ? results_ : nullptr;
}

int32_t Statement::getUpdateCount() {
    if (results_ == nullptr || results_->reallyResult()) return -1;
    return truncateCount(results_->updateCount());
}

bool Statement::getMoreResults() {
    return getMoreResults(CLOSE_CURRENT_RESULT);
}

bool Statement::getMoreResults(int32_t current) {
    if (results_ == nullptr) return false;
    ResultSet* next = results_->nextResultSet_;
    switch (current) {
        case CLOSE_CURRENT_RESULT: results_->close(); break;
        case CLOSE_ALL_RESULTS:
            results_->close();
            for (ResultSet* r : openResults_) r->close();
            openResults_.clear();
            break;
        case KEEP_CURRENT_RESULT: break;
        default: throwSql("Illegal flag for getMoreResults(int).", "S1009");
    }
    results_ = next;
    if (results_ == nullptr || results_->reallyResult()) {
        updateCount_ = -1;
        lastInsertId_ = -1;
    } else {
        updateCount_ = results_->updateCount();
        lastInsertId_ = results_->updateId();
    }
    return results_ != nullptr && results_->reallyResult();
}

void Statement::trackOpenResult(ResultSet* rs) {
    // Keep only open result sets (a statement reused many times must not accumulate them).
    size_t w = 0;
    for (size_t i = 0; i < openResults_.size(); i++) {
        if (!openResults_[i]->isClosed()) openResults_[w++] = openResults_[i];
    }
    openResults_.resize(w);
    openResults_.push_back(rs);
}

ResultSet* Statement::generatedKeysInternal(int64_t numKeys, bool track) {
    int64_t beginAt = lastInsertId_;
    std::vector<std::string> cells;
    if (results_ != nullptr) {
        const String& info = results_->serverInfo();
        if (numKeys > 0 && results_->firstCharOfQuery_ == 'R' && !info.isNull() && info.length() > 0) {
            numKeys = recordCountFromInfo(info);
        }
        if (beginAt != 0 && numKeys > 0) {
            int64_t inc = 1;
            if (numKeys > 1) {
                Session* s = conn_ != nullptr ? conn_->sessionOrNull() : nullptr;
                if (s != nullptr) inc = s->getAutoIncrementIncrement();
            }
            for (int64_t i = 0; i < numKeys; i++) {
                if (beginAt > 0) cells.push_back(std::to_string(beginAt));
                else cells.push_back(std::to_string(static_cast<uint64_t>(beginAt)));
                beginAt += inc;
            }
        }
    }
    auto* er = new ExecResult();
    er->meta = ResultMeta::generatedKeys();
    er->rows = RowData::ofStrings(cells);
    Config* cfg = conn_ != nullptr && conn_->sessionOrNull() != nullptr ? conn_->sessionOrNull()->cfg : new Config();
    auto* rs = new ResultSet(this, cfg, er, ResultSet::TYPE_FORWARD_ONLY, ResultSet::CONCUR_READ_ONLY);
    if (track) trackOpenResult(rs);
    return rs;
}

ResultSet* Statement::getGeneratedKeys() {
    if (!retrieveGeneratedKeys_) {
        throwSql("Generated keys not requested. You need to specify Statement.RETURN_GENERATED_KEYS to "
                 "Statement.executeUpdate() or Connection.prepareStatement().",
                 "S1009");
    }
    if (!haveBatchedKeys_) {
        if (lastQueryIsOnDupKeyUpdate_) return generatedKeysInternal(1);
        return generatedKeysInternal(getUpdateCount());
    }
    auto* er = new ExecResult();
    er->meta = ResultMeta::generatedKeys();
    er->rows = RowData::ofStrings(batchedKeys_);
    Config* cfg = conn_ != nullptr && conn_->sessionOrNull() != nullptr ? conn_->sessionOrNull()->cfg : new Config();
    auto* rs = new ResultSet(this, cfg, er, ResultSet::TYPE_FORWARD_ONLY, ResultSet::CONCUR_READ_ONLY);
    trackOpenResult(rs);
    return rs;
}

// ---------------------------------------------------------------------------------------
// PreparedStatement
PreparedStatement::PreparedStatement(Connection* conn, const String& sql, int32_t resultSetType,
                                     int32_t resultSetConcurrency)
    : Statement(conn, resultSetType, resultSetConcurrency) {
    if (sql.isNull()) throwSql("SQL String can not be NULL", "S1009");
    sql_ = sql;
    Session* s = conn->session();
    bool noBackslashEscapes = s->noBackslashEscapes();
    // Connector/J PreparedStatement.ParseInfo
    std::string_view q(sql_);
    const char quotedIdentifierChar = '`';
    bool inQuotes = false;
    char quoteChar = 0;
    bool inQuotedId = false;
    size_t lastParmEnd = 0;
    size_t len = q.size();
    for (size_t i = static_cast<size_t>(findStartOfStatement(q)); i < len; ++i) {
        char c = q[i];
        if (firstCharOfStmt_ == 0 && std::isalpha(static_cast<unsigned char>(c))) {
            firstCharOfStmt_ = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (!noBackslashEscapes && c == '\\' && i < len - 1) {
            ++i;
            continue;
        }
        if (!inQuotes && c == quotedIdentifierChar) {
            inQuotedId = !inQuotedId;
        } else if (!inQuotedId) {
            if (inQuotes) {
                if ((c == '\'' || c == '"') && c == quoteChar) {
                    if (i < len - 1 && q[i + 1] == quoteChar) {
                        ++i;
                        continue;
                    }
                    inQuotes = false;
                    quoteChar = 0;
                }
            } else {
                if (c == '#' || (c == '-' && i + 1 < len && q[i + 1] == '-')) {
                    size_t endOfStmt = len - 1;
                    while (i < endOfStmt && (c = q[i]) != '\r' && c != '\n') ++i;
                    continue;
                }
                if (c == '/' && i + 1 < len) {
                    char cNext = q[i + 1];
                    if (cNext == '*') {
                        i += 2;
                        for (size_t j = i; j < len; ++j) {
                            ++i;
                            cNext = q[j];
                            if (cNext != '*' || j + 1 >= len || q[j + 1] != '/') continue;
                            if (++i < len) c = q[i];
                            break;
                        }
                    }
                } else if (c == '\'' || c == '"') {
                    inQuotes = true;
                    quoteChar = c;
                }
            }
        }
        if (c == '?' && !inQuotes && !inQuotedId && i < len) {
            staticSql_.emplace_back(q.substr(lastParmEnd, i - lastParmEnd));
            lastParmEnd = i + 1;
        }
    }
    staticSql_.emplace_back(lastParmEnd <= len ? q.substr(lastParmEnd) : std::string_view());
    values_.resize(staticSql_.size() - 1);
    isSet_.assign(staticSql_.size() - 1, false);
    isOnDuplicateKeyUpdate_ = onDuplicateKeyLocation(q) != -1;
}

bool PreparedStatement::isSelectQuery() {
    return startsWithKeywordIgnoringComments(sql_, "SELECT");
}

std::string PreparedStatement::fillSendPacket(const std::vector<std::string>& values, const std::vector<bool>& isSet) {
    size_t total = 0;
    for (size_t i = 0; i < values.size(); i++) {
        if (!isSet[i]) throwSql(str("No value specified for parameter ", static_cast<int32_t>(i + 1)), "07001");
        total += values[i].size();
    }
    for (auto& s : staticSql_) total += s.size();
    std::string out;
    out.reserve(total);
    for (size_t i = 0; i < values.size(); i++) {
        out += staticSql_[i];
        out += values[i];
    }
    out += staticSql_.back();
    return out;
}

void PreparedStatement::setInternal(int32_t parameterIndex, std::string value) {
    checkClosed();
    int32_t count = static_cast<int32_t>(values_.size());
    if (parameterIndex < 1) {
        throwSql(str("Parameter index out of range (", parameterIndex, " < 1 )."), "S1009");
    }
    if (parameterIndex > count) {
        throwSql(str("Parameter index out of range (", parameterIndex, " > number of parameters, which is ", count, ")."),
                 "S1009");
    }
    values_[static_cast<size_t>(parameterIndex - 1)] = std::move(value);
    isSet_[static_cast<size_t>(parameterIndex - 1)] = true;
}

void PreparedStatement::clearParameters() {
    checkClosed();
    for (size_t i = 0; i < values_.size(); i++) {
        values_[i].clear();
        isSet_[i] = false;
    }
}

void PreparedStatement::setNull(int32_t parameterIndex, int32_t sqlType) {
    (void)sqlType;
    setInternal(parameterIndex, "null");
}

void PreparedStatement::setBoolean(int32_t parameterIndex, bool x) {
    setInternal(parameterIndex, x ? "1" : "0");
}

void PreparedStatement::setByte(int32_t parameterIndex, int8_t x) {
    setInternal(parameterIndex, std::to_string(static_cast<int>(x)));
}

void PreparedStatement::setShort(int32_t parameterIndex, int16_t x) {
    setInternal(parameterIndex, std::to_string(static_cast<int>(x)));
}

void PreparedStatement::setInt(int32_t parameterIndex, int32_t x) {
    setInternal(parameterIndex, std::to_string(x));
}

void PreparedStatement::setLong(int32_t parameterIndex, int64_t x) {
    setInternal(parameterIndex, std::to_string(x));
}

void PreparedStatement::setFloat(int32_t parameterIndex, float x) {
    setInternal(parameterIndex, fixDecimalExponent(Float::toString(x)));
}

void PreparedStatement::setDouble(int32_t parameterIndex, double x) {
    checkClosed();
    Session* s = session();
    if (!s->cfg->allowNanAndInf && (std::isinf(x) || std::isnan(x))) {
        throwSql(str("'", Double::toString(x), "' is not a valid numeric or approximate numeric value"), "S1009");
    }
    setInternal(parameterIndex, fixDecimalExponent(Double::toString(x)));
}

void PreparedStatement::setString(int32_t parameterIndex, const String& x) {
    if (x.isNull()) {
        setNull(parameterIndex, Types::CHAR);
        return;
    }
    checkClosed();
    Session* s = session();
    setInternal(parameterIndex, s->quote(x));
}

void PreparedStatement::setBytes(int32_t parameterIndex, Array<int8_t>* x) {
    if (x == nullptr) {
        setNull(parameterIndex, Types::BINARY);
        return;
    }
    if (closed_) throwSql("PreparedStatement has been closed. No further operations allowed.", "S1009");
    static const char kHex[] = "0123456789ABCDEF";
    std::string v;
    v.reserve(static_cast<size_t>(x->length) * 2 + 3);
    v += "x'";
    const int8_t* d = x->data();
    for (int32_t i = 0; i < x->length; i++) {
        unsigned b = static_cast<uint8_t>(d[i]);
        v += kHex[b >> 4];
        v += kHex[b & 15];
    }
    v += '\'';
    setInternal(parameterIndex, std::move(v));
}

void PreparedStatement::setTimestamp(int32_t parameterIndex, Timestamp* x) {
    if (x == nullptr) {
        setNull(parameterIndex, Types::TIMESTAMP);
        return;
    }
    setTimestampMillis(parameterIndex, timestampMillis(x));
}

void PreparedStatement::setTimestampMillis(int32_t parameterIndex, int64_t millis) {
    checkClosed();
    // SimpleDateFormat("''yyyy-MM-dd HH:mm:ss''") in the default time zone (no fraction).
    if (tsCal_ == nullptr) tsCal_ = newLocalCalendar();
    Fields f = millisToLocal(tsCal_, millis);
    int64_t y = f.year <= 0 ? 1 - f.year : f.year;
    char buf[64];
    std::snprintf(buf, sizeof buf, "'%04lld-%02d-%02d %02d:%02d:%02d'", static_cast<long long>(y), f.month, f.day,
                  f.hour, f.minute, f.second);
    setInternal(parameterIndex, buf);
}

void PreparedStatement::setObject(int32_t parameterIndex, Object* x) {
    if (x == nullptr) {
        setNull(parameterIndex, Types::OTHER);
        return;
    }
    if (auto* v = dynamic_cast<Byte*>(x)) return setByte(parameterIndex, v->byteValue());
    if (auto* v = dynamic_cast<Short*>(x)) return setShort(parameterIndex, v->shortValue());
    if (auto* v = dynamic_cast<Integer*>(x)) return setInt(parameterIndex, v->intValue());
    if (auto* v = dynamic_cast<Long*>(x)) return setLong(parameterIndex, v->longValue());
    if (auto* v = dynamic_cast<Float*>(x)) return setFloat(parameterIndex, v->floatValue());
    if (auto* v = dynamic_cast<Double*>(x)) return setDouble(parameterIndex, v->doubleValue());
    if (auto* v = dynamic_cast<Boolean*>(x)) return setBoolean(parameterIndex, v->booleanValue());
    if (auto* v = dynamic_cast<Array<int8_t>*>(x)) return setBytes(parameterIndex, v);
    if (Timestamp* v = asTimestamp(x)) return setTimestamp(parameterIndex, v);
    int64_t millis;
    if (dateMillis(x, millis)) return setTimestampMillis(parameterIndex, millis);  // java.util.Date
    setString(parameterIndex, x->toString());  // boxed String, Character, BigInteger, ...
}

ResultSet* PreparedStatement::executeQuery() {
    checkClosed();
    Session* s = session();
    checkForDml(sql_, firstCharOfStmt_);
    haveBatchedKeys_ = false;
    std::string packet = fillSendPacket(values_, isSet_);
    if (results_ != nullptr) closeResults(false);
    ExecResult* r = s->execute(packet, maxRows_);
    results_ = makeResults(this, s->cfg, r, rsType_, rsConcurrency_, firstCharOfStmt_);
    lastInsertId_ = results_->updateId();
    return results_;
}

int32_t PreparedStatement::executeUpdate() {
    haveBatchedKeys_ = false;
    return executeUpdateInternal(values_, isSet_, false);
}

int32_t PreparedStatement::executeUpdateInternal(const std::vector<std::string>& values, const std::vector<bool>& isSet,
                                                 bool isBatch) {
    (void)isBatch;
    checkClosed();
    Session* s = session();
    if (s->readOnly) {
        throwSql("Connection is read-only. Queries leading to data modification are not allowed", "S1009");
    }
    if (firstCharOfStmt_ == 'S' && isSelectQuery()) throwSql("Can not issue executeUpdate() for SELECTs", "01S03");
    if (results_ != nullptr) closeResults(false);
    lastQueryIsOnDupKeyUpdate_ = false;
    std::string packet = fillSendPacket(values, isSet);
    ExecResult* r = s->execute(packet, 0);
    results_ = makeResults(this, s->cfg, r, rsType_, rsConcurrency_, firstCharOfStmt_);
    updateCount_ = results_->updateCount();
    lastInsertId_ = results_->updateId();
    return truncateCount(updateCount_);
}

bool PreparedStatement::execute() {
    checkClosed();
    Session* s = session();
    if (s->readOnly && firstCharOfStmt_ != 'S') {
        throwSql("Connection is read-only. Queries leading to data modification are not allowed", "S1009");
    }
    lastQueryIsOnDupKeyUpdate_ = retrieveGeneratedKeys_ && isOnDuplicateKeyUpdate_;
    haveBatchedKeys_ = false;
    std::string packet = fillSendPacket(values_, isSet_);
    ExecResult* r = s->execute(packet, maxRows_);
    results_ = makeResults(this, s->cfg, r, rsType_, rsConcurrency_, firstCharOfStmt_);
    lastInsertId_ = results_->updateId();
    return results_->reallyResult();
}

void PreparedStatement::addBatch() {
    checkClosed();
    for (size_t i = 0; i < values_.size(); i++) {
        if (!isSet_[i]) throwSql(str("No value specified for parameter ", static_cast<int32_t>(i + 1)), "07001");
    }
    auto* e = new BatchEntry();
    e->values = values_;
    e->isSet = isSet_;
    batch_.push_back(e);
}

Array<int32_t>* PreparedStatement::executeBatch() {
    checkClosed();
    Session* s = session();
    if (s->readOnly) {
        throwSql("Connection is read-only. Queries leading to data modification are not allowed", "S1009");
    }
    if (batch_.empty()) return new Array<int32_t>(0);
    std::vector<BatchEntry*> entries;
    entries.swap(batch_);  // clearBatch() in Java's finally
    int32_t n = static_cast<int32_t>(entries.size());
    auto* counts = new Array<int32_t>(n);
    for (int32_t i = 0; i < n; i++) (*counts)[i] = EXECUTE_FAILED;
    bool collectKeys = retrieveGeneratedKeys_;
    if (collectKeys) batchedKeys_.clear();
    bool failed = false;
    String failMsg, failState;
    int32_t failCode = 0;
    for (int32_t i = 0; i < n; i++) {
        BatchEntry* e = entries[static_cast<size_t>(i)];
        if (e->isSql) {
            (*counts)[i] = Statement::executeUpdateInternal(String(e->sql), false, true);
            continue;
        }
        try {
            (*counts)[i] = executeUpdateInternal(e->values, e->isSet, true);
            if (collectKeys) {
                ResultSet* keys = generatedKeysInternal(isOnDuplicateKeyUpdate_ ? 1 : updateCount_, false);
                while (keys->next()) batchedKeys_.push_back(std::string(keys->getString(1)));
            }
        } catch (SQLException& ex) {
            (*counts)[i] = EXECUTE_FAILED;
            if (s->cfg->continueBatchOnError && !hasDeadlockOrTimeoutRolledBackTx(ex)) {
                failed = true;
                failMsg = ex.getMessage();
                failState = ex.getSQLState();
                failCode = ex.getErrorCode();
                continue;
            }
            auto* partial = new Array<int32_t>(i);
            for (int32_t k = 0; k < i; k++) (*partial)[k] = (*counts)[k];
            haveBatchedKeys_ = collectKeys;
            throw BatchUpdateException(ex.getMessage(), ex.getSQLState(), ex.getErrorCode(), partial);
        }
    }
    haveBatchedKeys_ = collectKeys;
    if (failed) throw BatchUpdateException(failMsg, failState, failCode, counts);
    return counts;
}

// ---------------------------------------------------------------------------------------
// CallableStatement
String CallableStatement::nativeCallSql(const String& sql) {
    if (sql.isNull()) return sql;
    std::string_view s(sql);
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos || s[b] != '{') return sql;
    size_t e = s.find_last_not_of(" \t\r\n");
    if (e == std::string_view::npos || s[e] != '}') return sql;
    std::string_view inner = s.substr(b + 1, e - b - 1);
    size_t k = inner.find_first_not_of(" \t\r\n");
    if (k == std::string_view::npos) return sql;
    inner = inner.substr(k);
    if (inner.size() >= 4 && (inner.substr(0, 4) == "call" || inner.substr(0, 4) == "CALL" || inner.substr(0, 4) == "Call")) {
        return String(str("CALL", std::string(inner.substr(4))));
    }
    return sql;
}

CallableStatement::CallableStatement(Connection* conn, const String& sql, int32_t resultSetType,
                                     int32_t resultSetConcurrency)
    : PreparedStatement(conn, nativeCallSql(sql), resultSetType, resultSetConcurrency) {}

}  // namespace jlang
