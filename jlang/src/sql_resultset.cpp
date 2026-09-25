// jlang/src/sql_resultset.cpp - java.sql.ResultSet and ResultSetMetaData with Connector/J
// 5.1.13 text-protocol conversions (ResultSetImpl, ByteArrayRow, StringUtils). See jlang/Sql.h.
#include "sql_internal.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace jlang {

using namespace sql_detail;

namespace {

constexpr const char* kClosed = "Operation not allowed after ResultSet closed";

// Connector/J ResultSetImpl.MIN_DIFF_PREC / MAX_DIFF_PREC.
const double kMinDiffPrec = static_cast<double>(std::numeric_limits<float>::denorm_min()) - 1.4E-45;
const double kMaxDiffPrec = static_cast<double>(std::numeric_limits<float>::max()) - 3.4028235E38;

bool hasExponent(const char* p, int64_t len) {
    for (int64_t i = 0; i < len; i++)
        if (p[i] == 'e' || p[i] == 'E') return true;
    return false;
}
bool hasChar(const String& s, char c) {
    return std::string_view(s).find(c) != std::string_view::npos;
}

struct BadValue {};  // Java NumberFormatException/IllegalArgumentException inside date parsing

int32_t digits(const char* p, int64_t len, int64_t b, int64_t e) {
    int32_t v;
    if (!cjGetInt(p, len, b, e, v)) throw BadValue();
    return v;
}
// Integer.parseInt(s.substring(b, e)) (strict: optional sign and digits only).
int32_t strictInt(std::string_view s, size_t b, size_t e) {
    if (e > s.size() || b > e) throw BadValue();
    try {
        return Integer::parseInt(String(s.substr(b, e - b)));
    } catch (NumberFormatException&) {
        throw BadValue();
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------
// ResultSetMetaData
namespace {
ColumnInfo& metaField(ResultMeta* m, int32_t column) {
    if (m == nullptr || column < 1 || column > static_cast<int32_t>(m->cols.size())) {
        throw SQLException(String("Column index out of range."), String("S1002"));
    }
    return m->cols[static_cast<size_t>(column - 1)];
}
}  // namespace

int32_t ResultSetMetaData::getColumnCount() {
    return meta_ == nullptr ? 0 : static_cast<int32_t>(meta_->cols.size());
}
String ResultSetMetaData::getColumnLabel(int32_t column) {
    return metaField(meta_, column).label;
}
String ResultSetMetaData::getColumnName(int32_t column) {
    ColumnInfo& f = metaField(meta_, column);
    return f.name.isEmpty() ? f.label : f.name;
}
String ResultSetMetaData::getTableName(int32_t column) {
    return metaField(meta_, column).orgTable;
}
String ResultSetMetaData::getCatalogName(int32_t column) {
    return metaField(meta_, column).db;
}
String ResultSetMetaData::getSchemaName(int32_t column) {
    (void)metaField(meta_, column);
    return String("");
}
int32_t ResultSetMetaData::getColumnType(int32_t column) {
    return metaField(meta_, column).sqlType;
}
String ResultSetMetaData::getColumnTypeName(int32_t column) {
    ColumnInfo& f = metaField(meta_, column);
    bool u = f.isUnsigned();
    switch (f.mysqlType) {
        case T_BIT: return "BIT";
        case T_DECIMAL:
        case T_NEWDECIMAL: return u ? "DECIMAL UNSIGNED" : "DECIMAL";
        case T_TINY: return u ? "TINYINT UNSIGNED" : "TINYINT";
        case T_SHORT: return u ? "SMALLINT UNSIGNED" : "SMALLINT";
        case T_LONG: return u ? "INT UNSIGNED" : "INT";
        case T_FLOAT: return u ? "FLOAT UNSIGNED" : "FLOAT";
        case T_DOUBLE: return u ? "DOUBLE UNSIGNED" : "DOUBLE";
        case T_NULL: return "NULL";
        case T_TIMESTAMP: return "TIMESTAMP";
        case T_LONGLONG: return u ? "BIGINT UNSIGNED" : "BIGINT";
        case T_INT24: return u ? "MEDIUMINT UNSIGNED" : "MEDIUMINT";
        case T_DATE: return "DATE";
        case T_TIME: return "TIME";
        case T_DATETIME: return "DATETIME";
        case T_TINY_BLOB: return "TINYBLOB";
        case T_MEDIUM_BLOB: return "MEDIUMBLOB";
        case T_LONG_BLOB: return "LONGBLOB";
        case T_BLOB: return f.isBinaryFlag() ? "BLOB" : "TEXT";
        case T_VARCHAR: return "VARCHAR";
        case T_VAR_STRING: return f.sqlType == Types::VARBINARY ? "VARBINARY" : "VARCHAR";
        case T_STRING: return f.sqlType == Types::BINARY ? "BINARY" : "CHAR";
        case T_ENUM: return "ENUM";
        case T_YEAR: return "YEAR";
        case T_SET: return "SET";
        case T_GEOMETRY: return "GEOMETRY";
        default: return "UNKNOWN";
    }
}
int32_t ResultSetMetaData::getColumnDisplaySize(int32_t column) {
    ColumnInfo& f = metaField(meta_, column);
    uint64_t len = f.length > static_cast<uint64_t>(INT32_MAX) ? INT32_MAX : f.length;
    // utf8mb4 results: up to 4 bytes per character for text columns.
    bool text = f.charsetnr != 63 && f.mysqlType != T_BIT && !(f.mysqlType >= 1 && f.mysqlType <= 5) &&
                f.mysqlType != T_LONGLONG && f.mysqlType != T_INT24 && f.mysqlType != T_NEWDECIMAL &&
                f.mysqlType != T_DECIMAL;
    return static_cast<int32_t>(text ? len / 4 : len);
}
int32_t ResultSetMetaData::getPrecision(int32_t column) {
    ColumnInfo& f = metaField(meta_, column);
    int32_t len = f.length > static_cast<uint64_t>(INT32_MAX) ? INT32_MAX : static_cast<int32_t>(f.length);
    if (f.sqlType == Types::DECIMAL || f.sqlType == Types::NUMERIC) {
        int32_t adjust = f.isUnsigned() ? 0 : -1;
        return f.decimals > 0 ? len - 1 + adjust : len + adjust;
    }
    return len;
}
int32_t ResultSetMetaData::getScale(int32_t column) {
    ColumnInfo& f = metaField(meta_, column);
    return (f.sqlType == Types::DECIMAL || f.sqlType == Types::NUMERIC) ? static_cast<int32_t>(f.decimals) : 0;
}
int32_t ResultSetMetaData::isNullable(int32_t column) {
    return (metaField(meta_, column).flags & 1) != 0 ? columnNoNulls : columnNullable;
}
bool ResultSetMetaData::isAutoIncrement(int32_t column) {
    return (metaField(meta_, column).flags & 512) != 0;
}
bool ResultSetMetaData::isSigned(int32_t column) {
    ColumnInfo& f = metaField(meta_, column);
    switch (f.sqlType) {
        case Types::TINYINT:
        case Types::BIGINT:
        case Types::NUMERIC:
        case Types::DECIMAL:
        case Types::INTEGER:
        case Types::SMALLINT:
        case Types::FLOAT:
        case Types::REAL:
        case Types::DOUBLE: return !f.isUnsigned();
        default: return false;
    }
}
bool ResultSetMetaData::isCaseSensitive(int32_t column) {
    ColumnInfo& f = metaField(meta_, column);
    switch (f.sqlType) {
        case Types::CHAR:
        case Types::VARCHAR:
        case Types::LONGVARCHAR: return f.charsetnr == 63 || f.isBinaryFlag();
        default: return false;
    }
}
bool ResultSetMetaData::isReadOnly(int32_t column) {
    ColumnInfo& f = metaField(meta_, column);
    return f.name.isEmpty() && f.orgTable.isEmpty();
}

// ---------------------------------------------------------------------------------------
// ResultSet: construction, navigation
ResultSet::ResultSet(Statement* owner, Config* cfg, ExecResult* r, int32_t type, int32_t concurrency)
    : owner_(owner), cfg_(cfg), meta_(r ? r->meta : nullptr), rows_(r ? r->rows : nullptr), type_(type),
      concurrency_(concurrency) {
    if (r != nullptr) {
        updateCount_ = r->updateCount;
        updateId_ = r->insertId;
        info_ = r->info;
    }
    setRowPositionValidity();
}

Calendar* ResultSet::calendar() {
    if (cal_ == nullptr) cal_ = newLocalCalendar();
    return cal_;
}

void ResultSet::checkClosed() {
    if (closed_) throw SQLException(String(kClosed), String("S1000"));
}

void ResultSet::setRowPositionValidity() {
    int32_t n = rows_ ? rows_->nrows : 0;
    if (!reallyResult()) {
        invalidRowReason_ = "ResultSet is from UPDATE. No Data.";
        onValidRow_ = false;
    } else if (n == 0) {
        invalidRowReason_ = "Illegal operation on empty result set.";
        onValidRow_ = false;
    } else if (index_ == -1) {
        invalidRowReason_ = "Before start of result set";
        onValidRow_ = false;
    } else if (index_ >= n) {
        invalidRowReason_ = "After end of result set";
        onValidRow_ = false;
    } else {
        invalidRowReason_ = nullptr;
        onValidRow_ = true;
    }
}

void ResultSet::checkRowPos() {
    checkClosed();
    if (!onValidRow_) throw SQLException(String(invalidRowReason_), String("S1000"));
    int32_t n = rows_ ? rows_->nrows : 0;
    if (index_ < 0 || index_ >= n) throw SQLException(String("After end of result set"), String("S1000"));
}

void ResultSet::checkColumnBounds(int32_t columnIndex) {
    int32_t n = meta_ ? static_cast<int32_t>(meta_->cols.size()) : 0;
    if (columnIndex < 1) {
        throw SQLException(str("Column Index out of range, ", columnIndex, " < 1."), String("S1009"));
    }
    if (columnIndex > n) {
        throw SQLException(str("Column Index out of range, ", columnIndex, " > ", n, ". "), String("S1009"));
    }
}

bool ResultSet::next() {
    checkClosed();
    if (!reallyResult()) throw SQLException(String("ResultSet is from UPDATE. No Data."), String("S1000"));
    bool b;
    int32_t n = rows_->nrows;
    if (n == 0) {
        b = false;
    } else {
        index_++;
        b = index_ < n;
        if (index_ > n) index_ = n;  // RowDataStatic keeps counting; clamping is unobservable
    }
    setRowPositionValidity();
    return b;
}

bool ResultSet::previous() {
    checkClosed();
    int32_t rowIndex = index_;
    bool b;
    if (rowIndex - 1 >= 0) {
        index_ = rowIndex - 1;
        b = true;
    } else if (rowIndex - 1 == -1) {
        index_ = -1;
        b = false;
    } else {
        b = false;
    }
    setRowPositionValidity();
    return b;
}

bool ResultSet::first() {
    checkClosed();
    bool b = true;
    if (rows_ == nullptr || rows_->nrows == 0) {
        b = false;
    } else {
        index_ = 0;
    }
    setRowPositionValidity();
    return b;
}

bool ResultSet::last() {
    checkClosed();
    bool b = true;
    if (rows_ == nullptr || rows_->nrows == 0) {
        b = false;
    } else {
        index_ = rows_->nrows - 1;
    }
    setRowPositionValidity();
    return b;
}

void ResultSet::beforeFirst() {
    checkClosed();
    if (rows_ == nullptr || rows_->nrows == 0) return;
    index_ = -1;
    setRowPositionValidity();
}

void ResultSet::afterLast() {
    checkClosed();
    if (rows_ != nullptr && rows_->nrows != 0) index_ = rows_->nrows;
    setRowPositionValidity();
}

bool ResultSet::absolute(int32_t row) {
    checkClosed();
    bool b;
    int32_t n = rows_ ? rows_->nrows : 0;
    if (n == 0) {
        b = false;
    } else {
        if (row == 0) throw SQLException(String("Cannot absolute position to row 0"), String("S1009"));
        if (row == 1) {
            b = first();
        } else if (row == -1) {
            b = last();
        } else if (row > n) {
            afterLast();
            b = false;
        } else if (row < 0) {
            int32_t newRowPosition = n + row + 1;
            if (newRowPosition <= 0) {
                beforeFirst();
                b = false;
            } else {
                b = absolute(newRowPosition);
            }
        } else {
            index_ = row - 1;
            b = true;
        }
    }
    setRowPositionValidity();
    return b;
}

bool ResultSet::relative(int32_t rows) {
    checkClosed();
    int32_t n = rows_ ? rows_->nrows : 0;
    if (n == 0) {
        setRowPositionValidity();
        return false;
    }
    int64_t idx = static_cast<int64_t>(index_) + rows;
    if (idx < -1) idx = -1;  // (Connector/J lets the index run below -1; clamped here)
    if (idx > n) idx = n;
    index_ = static_cast<int32_t>(idx);
    setRowPositionValidity();
    return !(index_ >= n) && !(index_ == -1);
}

int32_t ResultSet::getRow() {
    checkClosed();
    int32_t n = rows_ ? rows_->nrows : 0;
    if (index_ < 0 || index_ >= n || n == 0) return 0;
    return index_ + 1;
}

bool ResultSet::isBeforeFirst() {
    checkClosed();
    return index_ == -1 && rows_ != nullptr && rows_->nrows != 0;
}

bool ResultSet::isAfterLast() {
    checkClosed();
    return index_ >= (rows_ ? rows_->nrows : 0);
}

bool ResultSet::isFirst() {
    checkClosed();
    return index_ == 0;
}

bool ResultSet::isLast() {
    checkClosed();
    int32_t n = rows_ ? rows_->nrows : 0;
    return n != 0 && index_ == n - 1;
}

void ResultSet::close() noexcept {
    closed_ = true;
    rows_ = nullptr;
}

bool ResultSet::wasNull() {
    return wasNull_;
}

int32_t ResultSet::findColumnIndex(std::string_view columnLabel) {
    checkClosed();
    int32_t idx = meta_ ? meta_->find(columnLabel) : -1;
    if (idx < 0) throw SQLException(str("Column '", String(columnLabel), "' not found."), String("S0022"));
    return idx + 1;
}

int32_t ResultSet::findColumn(const String& columnLabel) {
    if (columnLabel.isNull()) {
        checkClosed();
        throw NullPointerException();
    }
    return findColumnIndex(columnLabel);
}

int32_t ResultSet::findColumn(const char* columnLabel) {
    if (columnLabel == nullptr) {
        checkClosed();
        throw NullPointerException();
    }
    return findColumnIndex(columnLabel);
}

ResultSetMetaData* ResultSet::getMetaData() {
    checkClosed();
    return new ResultSetMetaData(meta_ ? meta_ : new ResultMeta());
}

Statement* ResultSet::getStatement() {
    if (closed_) {
        throw SQLException(
            String("Operation not allowed on closed ResultSet. Statements can be retained over result set closure by "
                   "setting the connection property \"retainStatementAfterResultSetClose\" to \"true\"."),
            String("S1000"));
    }
    return owner_;
}

// ---------------------------------------------------------------------------------------
// Cell access
bool ResultSet::cellIsNull(int32_t col0) {
    return rows_->lens[static_cast<size_t>(index_) * rows_->ncols + col0] < 0;
}

const char* ResultSet::cell(int32_t col0, int64_t* len) {
    size_t i = static_cast<size_t>(index_) * rows_->ncols + col0;
    *len = rows_->lens[i];
    return rows_->data + rows_->offs[i];
}

String ResultSet::cellString(int32_t col0) {
    int64_t len;
    const char* p = cell(col0, &len);
    if (len < 0) return String();
    return String(p, static_cast<size_t>(len));
}

int32_t ResultSet::convertToZeroWithEmptyCheck() {
    if (cfg_->emptyStringsConvertToZero) return 0;
    throw SQLException(String("Can't convert empty string ('') to numeric"), String("22018"));
}

void ResultSet::throwRangeException(const String& value, int32_t columnIndex, int32_t jdbcType) {
    String datatype;
    switch (jdbcType) {
        case Types::TINYINT: datatype = "TINYINT"; break;
        case Types::SMALLINT: datatype = "SMALLINT"; break;
        case Types::INTEGER: datatype = "INTEGER"; break;
        case Types::BIGINT: datatype = "BIGINT"; break;
        case Types::REAL: datatype = "REAL"; break;
        case Types::FLOAT: datatype = "FLOAT"; break;
        case Types::DOUBLE: datatype = "DOUBLE"; break;
        case Types::DECIMAL: datatype = "DECIMAL"; break;
        default: datatype = str(" (JDBC type '", jdbcType, "')");
    }
    throw SQLException(str("'", value, "' in column '", columnIndex, "' is outside valid range for the datatype ",
                           datatype, "."),
                       String("22003"));
}

int64_t ResultSet::numericBits(int32_t columnIndex) {
    int64_t len;
    const char* p = cell(columnIndex - 1, &len);
    ColumnInfo& f = meta_->cols[static_cast<size_t>(columnIndex - 1)];
    if (len <= 0) {
        // Java: value[0] on an empty array (ArrayIndexOutOfBounds) for single-bit fields.
        if (f.singleBit || len == 0) return 0;
    }
    if (f.singleBit || len == 1) return static_cast<int8_t>(p[0]);
    int64_t v = 0;
    for (int64_t i = 0; i < len; i++) v = (v << 8) | static_cast<unsigned char>(p[i]);
    return v;
}

bool ResultSet::byteArrayToBoolean(int32_t col0) {
    int64_t len;
    const char* p = cell(col0, &len);
    if (len < 0) {
        wasNull_ = true;
        return false;
    }
    wasNull_ = false;
    if (len == 0) return false;
    int8_t b = static_cast<int8_t>(p[0]);
    if (b == '1') return true;
    if (b == '0') return false;
    return b == -1 || b > 0;
}

// ---------------------------------------------------------------------------------------
// getString
String ResultSet::getString(int32_t columnIndex) {
    return getStringInternal(columnIndex, true);
}

String ResultSet::getStringInternal(int32_t columnIndex, bool checkDateTypes) {
    checkRowPos();
    checkColumnBounds(columnIndex);
    int32_t col0 = columnIndex - 1;
    if (cellIsNull(col0)) {
        wasNull_ = true;
        return String();
    }
    wasNull_ = false;
    ColumnInfo& f = meta_->cols[static_cast<size_t>(col0)];
    if (f.mysqlType == T_BIT) {
        if (f.singleBit) {
            int64_t len;
            const char* p = cell(col0, &len);
            if (len == 0) return String::valueOf(convertToZeroWithEmptyCheck());
            return String::valueOf(static_cast<int32_t>(static_cast<int8_t>(p[0])));
        }
        return String::valueOf(numericBits(columnIndex));
    }
    String s = cellString(col0);
    if (f.mysqlType == T_YEAR) {
        if (!cfg_->yearIsDateType) return s;
        String d = dateStringFromString(columnIndex, s);
        wasNull_ = d.isNull();
        return d;
    }
    if (checkDateTypes) {
        switch (f.sqlType) {
            case Types::TIME: {
                String t = timeStringFromString(columnIndex, s);
                wasNull_ = t.isNull();
                return t;
            }
            case Types::DATE: {
                String d = dateStringFromString(columnIndex, s);
                wasNull_ = d.isNull();
                return d;
            }
            case Types::TIMESTAMP: {
                Timestamp* ts = timestampFromString(columnIndex, s);
                if (ts == nullptr) {
                    wasNull_ = true;
                    return String();
                }
                wasNull_ = false;
                return timestampToString(calendar(), timestampMillis(ts), timestampNanos(ts));
            }
            default: break;
        }
    }
    return s;
}

// ---------------------------------------------------------------------------------------
// Numeric getters (ResultSetImpl with useFastIntParsing=true, jdbcCompliantTruncation)
int32_t ResultSet::parseIntAsDouble(int32_t columnIndex, const String& val) {
    if (val.isNull()) return 0;
    double d = Double::parseDouble(val);
    if (cfg_->jdbcCompliantTruncation && (d < -2.147483648E9 || d > 2.147483647E9)) {
        throwRangeException(Double::toString(d), columnIndex, Types::INTEGER);
    }
    return d2i(d);
}

int64_t ResultSet::parseLongAsDouble(int32_t col0, const String& val) {
    if (val.isNull()) return 0;
    double d = Double::parseDouble(val);
    if (cfg_->jdbcCompliantTruncation && (d < -9.223372036854776E18 || d > 9.223372036854776E18)) {
        throwRangeException(val, col0 + 1, Types::BIGINT);
    }
    return d2l(d);
}

int16_t ResultSet::parseShortAsDouble(int32_t columnIndex, const String& val) {
    if (val.isNull()) return 0;
    double d = Double::parseDouble(val);
    if (cfg_->jdbcCompliantTruncation && (d < -32768.0 || d > 32767.0)) {
        throwRangeException(Double::toString(d), columnIndex, Types::SMALLINT);
    }
    return static_cast<int16_t>(d2i(d));
}

void ResultSet::checkForIntegerTruncation(int32_t col0, int32_t value) {
    if (cfg_->jdbcCompliantTruncation && (value == INT32_MIN || value == INT32_MAX)) {
        String s = cellString(col0);
        int64_t v = Long::parseLong(s);
        if (v < INT32_MIN || v > INT32_MAX) throwRangeException(s, col0 + 1, Types::INTEGER);
    }
}

void ResultSet::checkForLongTruncation(int32_t col0, int64_t value) {
    if (value == INT64_MIN || value == INT64_MAX) {
        String s = cellString(col0);
        double d = Double::parseDouble(s);
        if (d < -9.223372036854776E18 || d > 9.223372036854776E18) throwRangeException(s, col0 + 1, Types::BIGINT);
    }
}

int32_t ResultSet::getInt(int32_t columnIndex) {
    checkRowPos();
    int32_t col0 = columnIndex - 1;
    checkColumnBounds(columnIndex);
    wasNull_ = cellIsNull(col0);
    if (wasNull_) return 0;
    int64_t len;
    const char* p = cell(col0, &len);
    if (len == 0) return convertToZeroWithEmptyCheck();
    ColumnInfo& f = meta_->cols[static_cast<size_t>(col0)];
    if (!hasExponent(p, len)) {
        int32_t v;
        bool parsed = cjGetInt(p, len, 0, len, v);
        if (parsed) {
            try {
                checkForIntegerTruncation(col0, v);
                return v;
            } catch (NumberFormatException&) {
                // Long.parseLong inside the check failed: handled like an unparsable value.
            }
        }
        String s = cellString(col0);
        try {
            return parseIntAsDouble(columnIndex, s);
        } catch (NumberFormatException&) {
            if (f.mysqlType == T_BIT) {
                int64_t bv = numericBits(columnIndex);
                if (cfg_->jdbcCompliantTruncation && (bv < INT32_MIN || bv > INT32_MAX)) {
                    throwRangeException(String::valueOf(bv), columnIndex, Types::INTEGER);
                }
                return static_cast<int32_t>(bv);
            }
            throw SQLException(str("Invalid value for getInt() - '", s, "'"), String("S1009"));
        }
    }
    String val = getString(columnIndex);
    try {
        if (!val.isNull()) {
            if (val.length() == 0) return convertToZeroWithEmptyCheck();
            if (!hasChar(val, 'e') && !hasChar(val, 'E') && !hasChar(val, '.')) {
                int32_t v = Integer::parseInt(val);
                checkForIntegerTruncation(col0, v);
                return v;
            }
            int32_t v = parseIntAsDouble(columnIndex, val);
            checkForIntegerTruncation(col0, v);
            return v;
        }
        return 0;
    } catch (NumberFormatException&) {
        try {
            return parseIntAsDouble(columnIndex, val);
        } catch (NumberFormatException&) {
            if (f.mysqlType == T_BIT) {
                int64_t v = numericBits(columnIndex);
                if (cfg_->jdbcCompliantTruncation && (v < INT32_MIN || v > INT32_MAX)) {
                    throwRangeException(String::valueOf(v), columnIndex, Types::INTEGER);
                }
                return static_cast<int32_t>(v);
            }
            throw SQLException(str("Invalid value for getInt() - '", val, "'"), String("S1009"));
        }
    }
}

int64_t ResultSet::getLong(int32_t columnIndex) {
    return getLongInternal(columnIndex, true);
}

int64_t ResultSet::getLongInternal(int32_t columnIndex, bool overflowCheck) {
    checkRowPos();
    int32_t col0 = columnIndex - 1;
    checkColumnBounds(columnIndex);
    wasNull_ = cellIsNull(col0);
    if (wasNull_) return 0;
    int64_t len;
    const char* p = cell(col0, &len);
    if (len == 0) return convertToZeroWithEmptyCheck();
    ColumnInfo& f = meta_->cols[static_cast<size_t>(col0)];
    if (!hasExponent(p, len)) {
        int64_t v;
        if (cjGetLong(p, len, 0, len, v)) {
            try {
                if (overflowCheck) checkForLongTruncation(col0, v);
                return v;
            } catch (NumberFormatException&) {
            }
        }
        String s = cellString(col0);
        try {
            return parseLongAsDouble(col0, s);
        } catch (NumberFormatException&) {
            if (f.mysqlType == T_BIT) return numericBits(columnIndex);
            throw SQLException(str("Invalid value for getLong() - '", s, "'"), String("S1009"));
        }
    }
    String val = getString(columnIndex);
    try {
        if (!val.isNull()) {
            if (val.length() == 0) return convertToZeroWithEmptyCheck();
            if (!hasChar(val, 'e') && !hasChar(val, 'E')) {
                int64_t v = Long::parseLong(val.trim());
                if (overflowCheck && cfg_->jdbcCompliantTruncation) checkForLongTruncation(col0, v);
                return v;
            }
            return parseLongAsDouble(col0, val);
        }
        return 0;
    } catch (NumberFormatException&) {
        try {
            return parseLongAsDouble(col0, val);
        } catch (NumberFormatException&) {
            throw SQLException(str("Invalid value for getLong() - '", val, "'"), String("S1009"));
        }
    }
}

int16_t ResultSet::getShort(int32_t columnIndex) {
    checkRowPos();
    checkColumnBounds(columnIndex);
    int32_t col0 = columnIndex - 1;
    int64_t len;
    const char* p = cell(col0, &len);
    wasNull_ = len < 0;
    if (wasNull_) return 0;
    if (len == 0) return static_cast<int16_t>(convertToZeroWithEmptyCheck());
    ColumnInfo& f = meta_->cols[static_cast<size_t>(col0)];
    if (!hasExponent(p, len)) {
        int16_t v;
        if (cjGetShort(p, len, v)) {
            if (!cfg_->jdbcCompliantTruncation || (v != INT16_MIN && v != INT16_MAX)) return v;
            String s = cellString(col0);
            try {
                int64_t lv = Long::parseLong(s);
                if (lv < -32768 || lv > 32767) throwRangeException(s, columnIndex, Types::SMALLINT);
                return v;
            } catch (NumberFormatException&) {
            }
        }
        String s = cellString(col0);
        try {
            return parseShortAsDouble(columnIndex, s);
        } catch (NumberFormatException&) {
            if (f.mysqlType == T_BIT) {
                int64_t bv = numericBits(columnIndex);
                if (cfg_->jdbcCompliantTruncation && (bv < -32768 || bv > 32767)) {
                    throwRangeException(String::valueOf(bv), columnIndex, Types::SMALLINT);
                }
                return static_cast<int16_t>(bv);
            }
            throw SQLException(str("Invalid value for getShort() - '", s, "'"), String("S1009"));
        }
    }
    String val = getString(columnIndex);
    try {
        if (!val.isNull()) {
            if (val.length() == 0) return static_cast<int16_t>(convertToZeroWithEmptyCheck());
            if (!hasChar(val, 'e') && !hasChar(val, 'E') && !hasChar(val, '.')) {
                String t = val.trim();
                int16_t v = Short::parseShort(t);
                if (cfg_->jdbcCompliantTruncation && (v == INT16_MIN || v == INT16_MAX)) {
                    int64_t lv = Long::parseLong(t);
                    if (lv < -32768 || lv > 32767) throwRangeException(t, columnIndex, Types::SMALLINT);
                }
                return v;
            }
            return parseShortAsDouble(columnIndex, val);
        }
        return 0;
    } catch (NumberFormatException&) {
        try {
            return parseShortAsDouble(columnIndex, val);
        } catch (NumberFormatException&) {
            if (f.mysqlType == T_BIT) {
                int64_t bv = numericBits(columnIndex);
                if (cfg_->jdbcCompliantTruncation && (bv < -32768 || bv > 32767)) {
                    throwRangeException(String::valueOf(bv), columnIndex, Types::SMALLINT);
                }
                return static_cast<int16_t>(bv);
            }
            throw SQLException(str("Invalid value for getShort() - '", val, "'"), String("S1009"));
        }
    }
}

int8_t ResultSet::getByte(int32_t columnIndex) {
    String stringVal = getString(columnIndex);
    if (wasNull_ || stringVal.isNull()) return 0;
    if (stringVal.length() == 0) return static_cast<int8_t>(convertToZeroWithEmptyCheck());
    String s = stringVal.trim();
    try {
        if (hasChar(s, '.')) {
            double d = Double::parseDouble(s);
            if (cfg_->jdbcCompliantTruncation && (d < -128.0 || d > 127.0)) {
                throwRangeException(s, columnIndex, Types::TINYINT);
            }
            return static_cast<int8_t>(d2i(d));
        }
        int64_t v = Long::parseLong(s);
        if (cfg_->jdbcCompliantTruncation && (v < -128 || v > 127)) {
            throwRangeException(String::valueOf(v), columnIndex, Types::TINYINT);
        }
        return static_cast<int8_t>(v);
    } catch (NumberFormatException&) {
        throw SQLException(str("Value '", s, "' is out of range [-127,127]"), String("S1009"));
    }
}

double ResultSet::getDouble(int32_t columnIndex) {
    String s = getString(columnIndex);
    try {
        if (s.isNull()) return 0.0;
        if (s.length() == 0) return convertToZeroWithEmptyCheck();
        return Double::parseDouble(s);
    } catch (NumberFormatException&) {
        if (meta_->cols[static_cast<size_t>(columnIndex - 1)].mysqlType == T_BIT) {
            return static_cast<double>(numericBits(columnIndex));
        }
        throw SQLException(str("Bad format for number '", s, "' in column ", columnIndex, "."), String("S1009"));
    }
}

float ResultSet::getFloat(int32_t columnIndex) {
    String val = getString(columnIndex);
    try {
        if (!val.isNull()) {
            if (val.length() == 0) return static_cast<float>(convertToZeroWithEmptyCheck());
            float f = Float::parseFloat(val);
            if (cfg_->jdbcCompliantTruncation && (f == std::numeric_limits<float>::denorm_min() ||
                                                  f == std::numeric_limits<float>::max())) {
                double d = Double::parseDouble(val);
                if (d < static_cast<double>(1.4E-45f) - kMinDiffPrec || d > 3.4028234663852886E38 - kMaxDiffPrec) {
                    throwRangeException(Double::toString(d), columnIndex, Types::FLOAT);
                }
            }
            return f;
        }
        return 0.0f;
    } catch (NumberFormatException&) {
        try {
            double d = Double::parseDouble(val);
            float vf = static_cast<float>(d);
            if (cfg_->jdbcCompliantTruncation && std::isinf(vf)) {
                throwRangeException(Double::toString(d), columnIndex, Types::FLOAT);
            }
            return vf;
        } catch (NumberFormatException&) {
            throw SQLException(str("Invalid value for getFloat() - '", val, "' in column ", columnIndex),
                               String("S1009"));
        }
    }
}

bool ResultSet::getBoolean(int32_t columnIndex) {
    checkColumnBounds(columnIndex);
    int32_t col0 = columnIndex - 1;
    ColumnInfo& f = meta_->cols[static_cast<size_t>(col0)];
    if (f.mysqlType == T_BIT) {
        checkRowPos();
        return byteArrayToBoolean(col0);
    }
    wasNull_ = false;
    switch (f.sqlType) {
        case Types::BOOLEAN:
        case Types::BIT:
        case Types::TINYINT:
        case Types::BIGINT:
        case Types::NUMERIC:
        case Types::DECIMAL:
        case Types::INTEGER:
        case Types::SMALLINT:
        case Types::FLOAT:
        case Types::REAL:
        case Types::DOUBLE: {
            int64_t v = getLongInternal(columnIndex, false);
            return v == -1 || v > 0;
        }
        default: break;
    }
    if (f.sqlType == Types::BINARY || f.sqlType == Types::VARBINARY || f.sqlType == Types::LONGVARBINARY ||
        f.sqlType == Types::BLOB) {
        checkRowPos();
        return byteArrayToBoolean(col0);
    }
    String s = getString(columnIndex);
    if (!s.isNull() && s.length() > 0) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        return c == 't' || c == 'y' || c == '1' || s.equals("-1");
    }
    return false;
}

Array<int8_t>* ResultSet::getBytes(int32_t columnIndex) {
    checkRowPos();
    checkColumnBounds(columnIndex);
    int32_t col0 = columnIndex - 1;
    int64_t len;
    const char* p = cell(col0, &len);
    wasNull_ = len < 0;
    if (wasNull_) return nullptr;
    auto* a = new Array<int8_t>(static_cast<int32_t>(len));
    if (len > 0) std::memcpy(a->data(), p, static_cast<size_t>(len));
    return a;
}

// ---------------------------------------------------------------------------------------
// Date/time conversions
namespace {

Timestamp* zeroTimestamp(Config* cfg, const String& value, int32_t columnIndex, Calendar* cal) {
    switch (cfg->zeroDateBehavior) {
        case ZERO_CONVERT_TO_NULL: return nullptr;
        case ZERO_EXCEPTION:
            throw SQLException(str("Cannot convert value '", value, "' from column ", columnIndex, " to TIMESTAMP."),
                               String("S1009"));
        default: return makeTimestamp(localToMillis(cal, 1, 1, 1, 0, 0, 0), 0);
    }
}

// Connector/J ResultSetRow.getTimestampFast / ResultSetImpl.getTimestampFromString. Throws
// BadValue for values Java rejects.
Timestamp* parseTimestamp(const char* p, int64_t length, int32_t mysqlType, bool stringMode, Config* cfg,
                          const String& original, int32_t columnIndex, Calendar* cal) {
    std::string_view sv(p, static_cast<size_t>(length));
    if (stringMode) {
        if (!sv.empty() && sv[0] == '0' &&
            (sv == "0000-00-00" || sv == "0000-00-00 00:00:00" || sv == "00000000000000" || sv == "0")) {
            return zeroTimestamp(cfg, original, columnIndex, cal);
        }
    } else {
        bool onlyTimePresent = sv.find(':') != std::string_view::npos;
        bool allZero = true;
        for (char b : sv) {
            if (b == ' ' || b == '-' || b == '/') onlyTimePresent = false;
            if (b == '0' || b == ' ' || b == ':' || b == '-' || b == '/' || b == '.') continue;
            allZero = false;
            break;
        }
        if (!onlyTimePresent && allZero) return zeroTimestamp(cfg, original, columnIndex, cal);
    }
    if (mysqlType == T_YEAR) throw BadValue();  // Connector/J 5.1.13 fails on YEAR columns
    if (length > 0 && p[length - 1] == '.') length--;
    auto num = [&](int64_t b, int64_t e) -> int32_t {
        if (stringMode) return strictInt(std::string_view(p, static_cast<size_t>(length)), static_cast<size_t>(b), static_cast<size_t>(e));
        return digits(p, length, b, e);
    };
    int32_t year = 0, month = 0, day = 0, hour = 0, minutes = 0, seconds = 0, nanos = 0;
    switch (length) {
        case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 29: {
            if (stringMode && length == 29) throw BadValue();
            year = num(0, 4);
            month = num(5, 7);
            day = num(8, 10);
            hour = num(11, 13);
            minutes = num(14, 16);
            seconds = num(17, 19);
            nanos = 0;
            if (length > 19) {
                int64_t decimalIndex = -1;
                for (int64_t i = 0; i < length; i++)
                    if (p[i] == '.') decimalIndex = i;
                if (decimalIndex != -1) {
                    if (decimalIndex + 2 <= length) {
                        nanos = num(decimalIndex + 1, length);
                        int64_t numDigits = length - (decimalIndex + 1);
                        if (numDigits < 9) {
                            int32_t factor = 1;
                            for (int64_t k = 0; k < 9 - numDigits; k++) factor *= 10;
                            nanos *= factor;
                        }
                    } else {
                        throw BadValue();
                    }
                }
            }
            break;
        }
        case 14:
            year = num(0, 4);
            month = num(4, 6);
            day = num(6, 8);
            hour = num(8, 10);
            minutes = num(10, 12);
            seconds = num(12, 14);
            break;
        case 12:
            year = num(0, 2);
            if (year <= 69) year += 100;
            year += 1900;
            month = num(2, 4);
            day = num(4, 6);
            hour = num(6, 8);
            minutes = num(8, 10);
            seconds = num(10, 12);
            break;
        case 10: {
            bool hasDash = std::string_view(p, 10).find('-') != std::string_view::npos;
            if (mysqlType == T_DATE || hasDash) {
                year = num(0, 4);
                month = num(5, 7);
                day = num(8, 10);
                hour = 0;
                minutes = 0;
            } else {
                year = num(0, 2);
                if (year <= 69) year += 100;
                month = num(2, 4);
                day = num(4, 6);
                hour = num(6, 8);
                minutes = num(8, 10);
                year += 1900;
            }
            break;
        }
        case 8: {
            bool hasColon = std::string_view(p, 8).find(':') != std::string_view::npos;
            if (hasColon) {
                hour = num(0, 2);
                minutes = num(3, 5);
                seconds = num(6, 8);
                year = 1970;
                month = 1;
                day = 1;
            } else {
                year = num(0, 4);
                month = num(4, 6);
                day = num(6, 8);
                year -= 1900;
                month--;
            }
            break;
        }
        case 6:
            year = num(0, 2);
            if (year <= 69) year += 100;
            year += 1900;
            month = num(2, 4);
            day = num(4, 6);
            break;
        case 4:
            year = num(0, 2);
            if (year <= 69) year += 100;
            month = num(2, 4);
            day = 1;
            break;
        case 2:
            year = num(0, 2);
            if (year <= 69) year += 100;
            year += 1900;
            month = 1;
            day = 1;
            break;
        default: throw BadValue();
    }
    if (nanos < 0 || nanos > 999999999) throw BadValue();  // Timestamp.setNanos
    int64_t millis = localToMillis(cal, year, month, day, hour, minutes, seconds);
    if (nanos != 0) millis += nanos / 1000000;
    return makeTimestamp(millis, nanos);
}

}  // namespace

Timestamp* ResultSet::timestampFromCell(int32_t col0) {
    int64_t len;
    const char* p = cell(col0, &len);
    if (len < 0) return nullptr;
    ColumnInfo& f = meta_->cols[static_cast<size_t>(col0)];
    try {
        return parseTimestamp(p, len, f.mysqlType, false, cfg_, String(p, static_cast<size_t>(len)), col0 + 1,
                              calendar());
    } catch (BadValue&) {
        throw SQLException(str("Cannot convert value '", cellString(col0), "' from column ", col0 + 1, " to TIMESTAMP."),
                           String("S1009"));
    }
}

Timestamp* ResultSet::timestampFromString(int32_t columnIndex, String value) {
    if (value.isNull()) return nullptr;
    String v = value.trim();
    ColumnInfo& f = meta_->cols[static_cast<size_t>(columnIndex - 1)];
    try {
        return parseTimestamp(v.data(), v.length(), f.mysqlType, true, cfg_, v, columnIndex, calendar());
    } catch (BadValue&) {
        throw SQLException(str("Cannot convert value '", v, "' from column ", columnIndex, " to TIMESTAMP."),
                           String("S1009"));
    }
}

// ResultSetImpl.getDateFromString -> java.sql.Date.toString()
String ResultSet::dateStringFromString(int32_t columnIndex, const String& value) {
    if (value.isNull()) return String();
    String s = value.trim();
    std::string_view sv(s);
    if (sv == "0" || sv == "0000-00-00" || sv == "0000-00-00 00:00:00" || sv == "00000000000000") {
        switch (cfg_->zeroDateBehavior) {
            case ZERO_CONVERT_TO_NULL: return String();
            case ZERO_EXCEPTION:
                throw SQLException(str("Value '", s, "' can not be represented as java.sql.Date"), String("S1009"));
            default: return dateToString(calendar(), localToMillis(calendar(), 1, 1, 1, 0, 0, 0));
        }
    }
    ColumnInfo& f = meta_->cols[static_cast<size_t>(columnIndex - 1)];
    auto bad = [&]() -> SQLException {
        return SQLException(str("Bad format for DATE '", s, "' in column ", columnIndex, "."), String("S1009"));
    };
    int32_t year = 0, month = 0, day = 0;
    try {
        if (f.mysqlType == T_TIMESTAMP) {
            switch (sv.size()) {
                case 19:
                case 21:
                    year = strictInt(sv, 0, 4);
                    month = strictInt(sv, 5, 7);
                    day = strictInt(sv, 8, 10);
                    break;
                case 8:
                case 14:
                    year = strictInt(sv, 0, 4);
                    month = strictInt(sv, 4, 6);
                    day = strictInt(sv, 6, 8);
                    break;
                case 6:
                case 10:
                case 12:
                    year = strictInt(sv, 0, 2);
                    if (year <= 69) year += 100;
                    year += 1900;
                    month = strictInt(sv, 2, 4);
                    day = strictInt(sv, 4, 6);
                    break;
                case 4:
                    year = strictInt(sv, 0, 4);
                    if (year <= 69) year += 100;
                    year += 1900;
                    month = strictInt(sv, 2, 4);
                    day = 1;
                    break;
                case 2:
                    year = strictInt(sv, 0, 2);
                    if (year <= 69) year += 100;
                    year += 1900;
                    month = 1;
                    day = 1;
                    break;
                default: throw bad();
            }
        } else if (f.mysqlType == T_YEAR) {
            if (sv.size() == 2 || sv.size() == 1) {
                year = strictInt(sv, 0, sv.size());
                if (year <= 69) year += 100;
                year += 1900;
            } else {
                year = strictInt(sv, 0, 4);
            }
            month = 1;
            day = 1;
        } else if (f.mysqlType == T_TIME) {
            year = 1970;
            month = 1;
            day = 1;
        } else if (sv.size() < 10) {
            if (sv.size() != 8) throw bad();
            year = 1970;
            month = 1;
            day = 1;
        } else if (sv.size() != 18) {
            year = strictInt(sv, 0, 4);
            month = strictInt(sv, 5, 7);
            day = strictInt(sv, 8, 10);
        } else {
            // StringTokenizer(s, "- ")
            std::vector<std::string_view> tok;
            size_t i = 0;
            while (i < sv.size() && tok.size() < 3) {
                while (i < sv.size() && (sv[i] == '-' || sv[i] == ' ')) i++;
                size_t b = i;
                while (i < sv.size() && sv[i] != '-' && sv[i] != ' ') i++;
                if (i > b) tok.push_back(sv.substr(b, i - b));
            }
            if (tok.size() < 3) throw BadValue();
            year = strictInt(tok[0], 0, tok[0].size());
            month = strictInt(tok[1], 0, tok[1].size());
            day = strictInt(tok[2], 0, tok[2].size());
        }
    } catch (BadValue&) {
        throw bad();
    }
    return dateToString(calendar(), localToMillis(calendar(), year, month, day, 0, 0, 0));
}

// ResultSetImpl.getTimeFromString -> java.sql.Time.toString() (TIME columns)
String ResultSet::timeStringFromString(int32_t columnIndex, const String& value) {
    if (value.isNull()) return String();
    String s = value.trim();
    std::string_view sv(s);
    auto wrapped = [](const String& msg) {
        // Java wraps inner exceptions with createSQLException(ex.toString()).
        return SQLException(str("java.sql.SQLException: ", msg), String("S1009"));
    };
    if (sv == "0" || sv == "0000-00-00" || sv == "0000-00-00 00:00:00" || sv == "00000000000000") {
        switch (cfg_->zeroDateBehavior) {
            case ZERO_CONVERT_TO_NULL: return String();
            case ZERO_EXCEPTION: throw wrapped(str("Value '", s, "' can not be represented as java.sql.Time"));
            default: return timeToString(calendar(), localToMillis(calendar(), 1970, 1, 1, 0, 0, 0));
        }
    }
    if (sv.size() != 5 && sv.size() != 8) {
        throw wrapped(str("Bad format for Time '", s, "' in column ", columnIndex));
    }
    int32_t hr, min, sec;
    try {
        hr = strictInt(sv, 0, 2);
        min = strictInt(sv, 3, 5);
        sec = sv.size() == 5 ? 0 : strictInt(sv, 6, sv.size());
    } catch (BadValue&) {
        throw wrapped(str("Bad format for Time '", s, "' in column ", columnIndex));
    }
    auto formatted = [&]() {
        return String::format("%02d:%02d:%02d", hr, min, sec);
    };
    if (hr < 0 || hr > 24) {
        throw wrapped(str("Illegal hour value '", hr, "' for java.sql.Time type in value '", formatted(), "."));
    }
    if (min < 0 || min > 59) {
        throw wrapped(str("Illegal minute value '", min, "'' for java.sql.Time type in value '", formatted(), "."));
    }
    if (sec < 0 || sec > 59) {
        throw wrapped(str("Illegal minute value '", sec, "'' for java.sql.Time type in value '", formatted(), "."));
    }
    return timeToString(calendar(), localToMillis(calendar(), 1970, 1, 1, hr, min, sec));
}

Timestamp* ResultSet::getTimestamp(int32_t columnIndex) {
    checkClosed();
    checkRowPos();
    checkColumnBounds(columnIndex);
    Timestamp* ts = timestampFromCell(columnIndex - 1);
    wasNull_ = ts == nullptr;
    return ts;
}

// ---------------------------------------------------------------------------------------
// getObject (Connector/J ResultSetImpl.getObject, text protocol)
Object* ResultSet::getObject(int32_t columnIndex) {
    checkRowPos();
    checkColumnBounds(columnIndex);
    int32_t col0 = columnIndex - 1;
    if (cellIsNull(col0)) {
        wasNull_ = true;
        return nullptr;
    }
    wasNull_ = false;
    ColumnInfo& f = meta_->cols[static_cast<size_t>(col0)];
    switch (f.sqlType) {
        case Types::BIT:
        case Types::BOOLEAN:
            if (f.mysqlType == T_BIT && !f.singleBit) return getBytes(columnIndex);
            return Boolean::valueOf(getBoolean(columnIndex));
        case Types::TINYINT:
        case Types::SMALLINT: return Integer::valueOf(getInt(columnIndex));
        case Types::INTEGER:
            if (!f.isUnsigned() || f.mysqlType == T_INT24) return Integer::valueOf(getInt(columnIndex));
            return Long::valueOf(getLong(columnIndex));
        case Types::BIGINT:
            if (!f.isUnsigned()) return Long::valueOf(getLong(columnIndex));
            return box(getString(columnIndex));  // BigInteger in Java
        case Types::DECIMAL:
        case Types::NUMERIC: return box(getString(columnIndex));  // BigDecimal in Java
        case Types::REAL: return Float::valueOf(getFloat(columnIndex));
        case Types::FLOAT:
        case Types::DOUBLE: return Double::valueOf(getDouble(columnIndex));
        case Types::CHAR:
        case Types::VARCHAR:
        case Types::LONGVARCHAR: return box(getString(columnIndex));
        case Types::BINARY:
        case Types::VARBINARY:
        case Types::LONGVARBINARY: return getBytes(columnIndex);
        case Types::DATE:
        case Types::TIME:
        case Types::TIMESTAMP: return timestampObject(getTimestamp(columnIndex));
        default: return box(getString(columnIndex));
    }
}

}  // namespace jlang
