// jlang/src/sql_session.cpp - URL/property parsing, the MariaDB session (connect, execute,
// error mapping, reconnect), result copying and the text/time helpers shared by the JDBC
// classes. See jlang/Sql.h.
#include "sql_internal.h"

#include <mysql.h>
#include <errmsg.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>

namespace jlang::sql_detail {

// ---------------------------------------------------------------------------------------
// Library / thread initialization
//
// mysql_library_init must run before threads use the library; it is not thread-safe, so it is
// done once from a static initializer (single-threaded) and guarded by call_once for safety.
// mysql_thread_init/mysql_thread_end are per-thread hooks (no-ops in Connector/C 3.x, required
// by libmysqlclient): called lazily on the first use of the library in a thread, ended by a
// thread_local guard when the thread exits. The guard holds no GC pointers.
namespace {
std::once_flag g_libOnce;
void libraryInit() noexcept {
    std::call_once(g_libOnce, [] { mysql_library_init(0, nullptr, nullptr); });
}
const bool g_libInitAtStartup = (libraryInit(), true);

struct ThreadGuard {
    bool inited = false;
    ~ThreadGuard() {
        if (inited) mysql_thread_end();
    }
};
thread_local ThreadGuard t_threadGuard;
}  // namespace

void ensureThreadInit() noexcept {
    (void)g_libInitAtStartup;
    libraryInit();
    if (!t_threadGuard.inited) {
        mysql_thread_init();
        t_threadGuard.inited = true;
    }
}

[[noreturn]] void throwSql(const String& message, const String& sqlState, int32_t code) {
    throw SQLException(message, sqlState, code);
}

int64_t nowMillis() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ---------------------------------------------------------------------------------------
// Config
namespace {

std::string lowerAscii(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// java.net.URLDecoder.decode(s, "UTF-8"): '+' -> ' ', %XX -> byte.
std::string urlDecode(std::string_view s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '+') {
            r += ' ';
        } else if (c == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            auto hex = [](char h) { return std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (std::tolower(h) - 'a' + 10); };
            r += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            r += c;
        }
    }
    return r;
}

bool parseBoolProperty(const std::string& name, const std::string& value) {
    std::string v = lowerAscii(value);
    if (v == "true" || v == "yes") return true;
    if (v == "false" || v == "no") return false;
    throwSql(str("The connection property '", name, "' only accepts values of the form: 'true', 'false', 'yes' or 'no'. The value '",
                 value, "' is not in this set."),
             "S1009");
}

uint32_t parseIntProperty(const std::string& name, const std::string& value) {
    try {
        int32_t v = Integer::parseInt(String(value));
        return v < 0 ? 0u : static_cast<uint32_t>(v);
    } catch (NumberFormatException&) {
        throwSql(str("The connection property '", name, "' only accepts integer values. The value '", value,
                     "' can not be converted to an integer."),
                 "S1009");
    }
}

}  // namespace

bool Config::acceptsURL(const String& url) {
    return !url.isNull() && std::string_view(url).substr(0, 13) == "jdbc:mysql://";
}

Config* Config::parse(const String& url, const String& user, const String& password) {
    if (!acceptsURL(url)) {
        throwSql(str("No suitable driver found for ", url), "08001");
    }
    auto* c = new Config();
    c->url = url;
    if (!user.isNull()) c->user = user;
    if (!password.isNull()) c->password = password;
    std::string_view rest = std::string_view(url).substr(13);
    std::string_view params;
    size_t q = rest.find('?');
    if (q != std::string_view::npos) {
        params = rest.substr(q + 1);
        rest = rest.substr(0, q);
    }
    std::string_view hostPart = rest;
    size_t slash = rest.find('/');
    if (slash != std::string_view::npos) {
        hostPart = rest.substr(0, slash);
        c->database = urlDecode(rest.substr(slash + 1));
    }
    // Failover lists ("h1,h2"): the first host is used.
    size_t comma = hostPart.find(',');
    if (comma != std::string_view::npos) hostPart = hostPart.substr(0, comma);
    if (!hostPart.empty()) {
        std::string_view h = hostPart;
        std::string_view portText;
        if (h.front() == '[') {  // [ipv6]:port
            size_t close = h.find(']');
            if (close == std::string_view::npos) throwSql(str("Malformed URL '", url, "'"), "S1009");
            portText = h.substr(close + 1);
            h = h.substr(1, close - 1);
            if (!portText.empty()) {
                if (portText.front() != ':') throwSql(str("Malformed URL '", url, "'"), "S1009");
                portText = portText.substr(1);
            }
        } else {
            size_t colon = h.rfind(':');
            if (colon != std::string_view::npos) {
                portText = h.substr(colon + 1);
                h = h.substr(0, colon);
            }
        }
        if (!h.empty()) c->host = std::string(h);
        if (!portText.empty()) {
            try {
                int32_t p = Integer::parseInt(String(portText));
                if (p <= 0 || p > 65535) throw NumberFormatException(String(portText));
                c->port = static_cast<unsigned>(p);
            } catch (NumberFormatException&) {
                throwSql(str("Illegal connection port value '", std::string(portText), "'"), "01S00");
            }
        }
    }
    // Properties.
    while (!params.empty()) {
        size_t amp = params.find('&');
        std::string_view kv = params.substr(0, amp);
        params = amp == std::string_view::npos ? std::string_view() : params.substr(amp + 1);
        if (kv.empty()) continue;
        size_t eq = kv.find('=');
        std::string key(kv.substr(0, eq));
        std::string value = eq == std::string_view::npos ? std::string() : urlDecode(kv.substr(eq + 1));
        if (key == "user") c->user = value;
        else if (key == "password") c->password = value;
        else if (key == "autoReconnect") c->autoReconnect = parseBoolProperty(key, value);
        else if (key == "zeroDateTimeBehavior") {
            if (value == "exception") c->zeroDateBehavior = ZERO_EXCEPTION;
            else if (value == "convertToNull") c->zeroDateBehavior = ZERO_CONVERT_TO_NULL;
            else if (value == "round") c->zeroDateBehavior = ZERO_ROUND;
            else
                throwSql(str("The connection property 'zeroDateTimeBehavior' only accepts values of the form: 'exception', 'round' or 'convertToNull'. The value '",
                             value, "' is not in this set."),
                         "S1009");
        } else if (key == "jdbcCompliantTruncation") c->jdbcCompliantTruncation = parseBoolProperty(key, value);
        else if (key == "tinyInt1isBit") c->tinyInt1isBit = parseBoolProperty(key, value);
        else if (key == "emptyStringsConvertToZero") c->emptyStringsConvertToZero = parseBoolProperty(key, value);
        else if (key == "useAffectedRows") c->useAffectedRows = parseBoolProperty(key, value);
        else if (key == "allowMultiQueries") c->allowMultiQueries = parseBoolProperty(key, value);
        else if (key == "continueBatchOnError") c->continueBatchOnError = parseBoolProperty(key, value);
        else if (key == "allowNanAndInf") c->allowNanAndInf = parseBoolProperty(key, value);
        else if (key == "yearIsDateType") c->yearIsDateType = parseBoolProperty(key, value);
        else if (key == "jlangLegacyTimestamps") c->legacyTimestamps = parseBoolProperty(key, value);
        else if (key == "connectTimeout") c->connectTimeoutMs = parseIntProperty(key, value);
        else if (key == "socketTimeout") c->socketTimeoutMs = parseIntProperty(key, value);
        else if (key == "sessionVariables") c->sessionVariables = value;
        else if (key == "jlangSocket") c->socket = value;
        else if (key == "useUnicode" || key == "autoReconnectForPools" || key == "useSSL" ||
                 key == "requireSSL" || key == "useServerPrepStmts" || key == "cachePrepStmts" ||
                 key == "rewriteBatchedStatements" || key == "useLocalSessionState") {
            // Accepted; booleans are validated like Connector/J does.
            (void)parseBoolProperty(key, value);
        }
        // characterEncoding and unknown keys: ignored (the connection is always utf8mb4).
    }
    return c;
}

// ---------------------------------------------------------------------------------------
// ResultMeta
ResultMeta* ResultMeta::generatedKeys() {
    auto* m = new ResultMeta();
    ColumnInfo ci;
    ci.label = "GENERATED_KEY";
    ci.name = "GENERATED_KEY";
    ci.table = "";
    ci.orgTable = "";
    ci.db = "";
    ci.mysqlType = T_LONGLONG;
    ci.sqlType = Types::BIGINT;
    ci.length = 17;
    ci.charsetnr = 63;
    m->cols.push_back(ci);
    return m;
}

namespace {
std::string foldKey(const String& s) {
    return std::string(s.toLowerCase());
}
}  // namespace

void ResultMeta::buildMaps() {
    // Java iterates from the last field to the first with put(): the first occurrence wins.
    for (int32_t i = static_cast<int32_t>(cols.size()) - 1; i >= 0; i--) {
        labels_[foldKey(cols[i].label)] = i;
        fullNames_[foldKey(str(cols[i].table, ".", cols[i].label))] = i;
    }
    mapsBuilt_ = true;
}

int32_t ResultMeta::find(std::string_view nameView) {
    auto c = cache_.find(nameView);
    if (c != cache_.end()) return c->second;
    if (!mapsBuilt_) buildMaps();
    String name(nameView);
    std::string key = foldKey(name);
    int32_t idx = -1;
    auto it = labels_.find(key);
    if (it != labels_.end()) {
        idx = it->second;
    } else {
        auto f = fullNames_.find(key);
        if (f != fullNames_.end()) idx = f->second;
    }
    if (idx >= 0) {
        cache_.emplace(std::string(nameView), idx);
        return idx;
    }
    for (size_t i = 0; i < cols.size(); i++) {
        if (cols[i].label.equalsIgnoreCase(name) || str(cols[i].table, ".", cols[i].label).equalsIgnoreCase(name))
            return static_cast<int32_t>(i);
    }
    return -1;
}

// ---------------------------------------------------------------------------------------
// RowData
namespace {
RowData* allocRows(int32_t nrows, int32_t ncols, size_t totalBytes) {
    auto* r = new RowData();
    r->nrows = nrows;
    r->ncols = ncols;
    size_t cells = static_cast<size_t>(nrows) * static_cast<size_t>(ncols);
    r->data = static_cast<char*>(gc::allocAtomic(totalBytes == 0 ? 1 : totalBytes));
    auto* idx = static_cast<int64_t*>(gc::allocAtomic(sizeof(int64_t) * (2 * cells + 1)));
    r->offs = idx;
    r->lens = idx + cells;
    return r;
}
}  // namespace

RowData* RowData::copy(st_mysql_res* res, int32_t maxRows) {
    uint64_t n = mysql_num_rows(res);
    if (maxRows > 0 && n > static_cast<uint64_t>(maxRows)) n = static_cast<uint64_t>(maxRows);
    if (n > static_cast<uint64_t>(INT32_MAX)) n = INT32_MAX;
    unsigned nf = mysql_num_fields(res);
    size_t total = 0;
    mysql_data_seek(res, 0);
    for (uint64_t i = 0; i < n; i++) {
        MYSQL_ROW row = mysql_fetch_row(res);
        unsigned long* lens = mysql_fetch_lengths(res);
        if (row == nullptr || lens == nullptr) break;
        for (unsigned c = 0; c < nf; c++)
            if (row[c] != nullptr) total += lens[c];
    }
    RowData* r = allocRows(static_cast<int32_t>(n), static_cast<int32_t>(nf), total);
    mysql_data_seek(res, 0);
    size_t pos = 0;
    for (uint64_t i = 0; i < n; i++) {
        MYSQL_ROW row = mysql_fetch_row(res);
        unsigned long* lens = mysql_fetch_lengths(res);
        for (unsigned c = 0; c < nf; c++) {
            size_t cell = static_cast<size_t>(i) * nf + c;
            if (row == nullptr || row[c] == nullptr) {
                r->offs[cell] = 0;
                r->lens[cell] = -1;
            } else {
                std::memcpy(r->data + pos, row[c], lens[c]);
                r->offs[cell] = static_cast<int64_t>(pos);
                r->lens[cell] = static_cast<int64_t>(lens[c]);
                pos += lens[c];
            }
        }
    }
    return r;
}

RowData* RowData::ofStrings(const std::vector<std::string>& cells) {
    size_t total = 0;
    for (auto& s : cells) total += s.size();
    RowData* r = allocRows(static_cast<int32_t>(cells.size()), 1, total);
    size_t pos = 0;
    for (size_t i = 0; i < cells.size(); i++) {
        std::memcpy(r->data + pos, cells[i].data(), cells[i].size());
        r->offs[i] = static_cast<int64_t>(pos);
        r->lens[i] = static_cast<int64_t>(cells[i].size());
        pos += cells[i].size();
    }
    return r;
}

namespace {

int32_t mysqlToJavaType(int32_t t) {
    switch (t) {
        case T_DECIMAL:
        case T_NEWDECIMAL: return Types::DECIMAL;
        case T_TINY: return Types::TINYINT;
        case T_SHORT: return Types::SMALLINT;
        case T_LONG: return Types::INTEGER;
        case T_FLOAT: return Types::REAL;
        case T_DOUBLE: return Types::DOUBLE;
        case T_NULL: return Types::NULL_;
        case T_TIMESTAMP: return Types::TIMESTAMP;
        case T_LONGLONG: return Types::BIGINT;
        case T_INT24: return Types::INTEGER;
        case T_DATE: return Types::DATE;
        case T_TIME: return Types::TIME;
        case T_DATETIME: return Types::TIMESTAMP;
        case T_YEAR: return Types::DATE;
        case T_NEWDATE: return Types::DATE;
        case T_ENUM: return Types::CHAR;
        case T_SET: return Types::CHAR;
        case T_TINY_BLOB: return Types::VARBINARY;
        case T_MEDIUM_BLOB: return Types::LONGVARBINARY;
        case T_LONG_BLOB: return Types::LONGVARBINARY;
        case T_BLOB: return Types::LONGVARBINARY;
        case T_VARCHAR:
        case T_VAR_STRING: return Types::VARCHAR;
        case T_STRING: return Types::CHAR;
        case T_GEOMETRY: return Types::BINARY;
        case T_BIT: return Types::BIT;
        default: return Types::VARCHAR;
    }
}

// Connector/J 5.1 Field constructor: java.sql.Types of a result column.
void computeSqlType(ColumnInfo& f, Config* cfg) {
    f.sqlType = mysqlToJavaType(f.mysqlType);
    bool isImplicitTempTable = f.table.length() > 5 && std::string_view(f.table).substr(0, 5) == "#sql_";
    if (f.mysqlType == T_BLOB) {
        if (f.charsetnr == 63) {
            if (f.length == 255) f.mysqlType = T_TINY_BLOB;
            else if (f.length == 65535) f.mysqlType = T_BLOB;
            else if (f.length == 0xFFFFFF) f.mysqlType = T_MEDIUM_BLOB;
            else if (f.length == 0xFFFFFFFFull) f.mysqlType = T_LONG_BLOB;
            f.sqlType = mysqlToJavaType(f.mysqlType);
        } else {
            f.mysqlType = T_VAR_STRING;
            f.sqlType = Types::LONGVARCHAR;
        }
    }
    if (f.sqlType == Types::TINYINT && f.length == 1 && cfg->tinyInt1isBit) f.sqlType = Types::BIT;
    bool nativeNumeric = (f.mysqlType >= 1 && f.mysqlType <= 5) || f.mysqlType == T_LONGLONG || f.mysqlType == T_YEAR;
    bool nativeDateTime = f.mysqlType == T_DATE || f.mysqlType == T_NEWDATE || f.mysqlType == T_DATETIME ||
                          f.mysqlType == T_TIME || f.mysqlType == T_TIMESTAMP;
    if (!nativeNumeric && !nativeDateTime) {
        bool isBinary = f.isBinaryFlag();
        bool opaque = f.charsetnr == 63 && isBinary && (f.mysqlType == T_STRING || f.mysqlType == T_VAR_STRING)
                          ? !isImplicitTempTable
                          : f.charsetnr == 63;
        if (f.mysqlType == T_VAR_STRING && isBinary && f.charsetnr == 63 && opaque) f.sqlType = Types::VARBINARY;
        if (f.mysqlType == T_STRING && isBinary && f.charsetnr == 63 && opaque) f.sqlType = Types::BINARY;
        if (f.mysqlType == T_BIT) {
            f.singleBit = f.length == 0 || f.length == 1;
            if (f.singleBit) {
                f.sqlType = Types::BIT;
            } else {
                f.sqlType = Types::VARBINARY;
                f.flags |= 128 | 16;
                isBinary = true;
            }
        }
        if (f.sqlType == Types::LONGVARBINARY && !isBinary) f.sqlType = Types::LONGVARCHAR;
        else if (f.sqlType == Types::VARBINARY && !isBinary) f.sqlType = Types::VARCHAR;
    }
}

ResultMeta* buildMeta(MYSQL_RES* res, Config* cfg) {
    auto* m = new ResultMeta();
    unsigned nf = mysql_num_fields(res);
    MYSQL_FIELD* fs = mysql_fetch_fields(res);
    m->cols.resize(nf);
    for (unsigned i = 0; i < nf; i++) {
        MYSQL_FIELD& f = fs[i];
        ColumnInfo& ci = m->cols[i];
        ci.label = String(f.name ? f.name : "", f.name ? f.name_length : 0);
        ci.name = String(f.org_name ? f.org_name : "", f.org_name ? f.org_name_length : 0);
        ci.table = String(f.table ? f.table : "", f.table ? f.table_length : 0);
        ci.orgTable = String(f.org_table ? f.org_table : "", f.org_table ? f.org_table_length : 0);
        ci.db = String(f.db ? f.db : "", f.db ? f.db_length : 0);
        ci.mysqlType = static_cast<int32_t>(f.type);
        ci.flags = f.flags;
        ci.charsetnr = f.charsetnr;
        ci.decimals = f.decimals;
        ci.length = f.length;
        computeSqlType(ci, cfg);
    }
    return m;
}

bool isCommunicationError(unsigned e) {
    switch (e) {
        case CR_CONNECTION_ERROR:
        case CR_CONN_HOST_ERROR:
        case CR_SERVER_GONE_ERROR:
        case CR_SERVER_LOST:
        case CR_SERVER_LOST_EXTENDED:
        case 4031:  // ER_CLIENT_INTERACTION_TIMEOUT (server closed an idle connection)
            return true;
        default:
            return false;
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Session
Session::Session(Config* c) : cfg(c) {}

bool Session::versionMeetsMinimum(int32_t ma, int32_t mi, int32_t sub) const noexcept {
    if (major != ma) return major > ma;
    if (minor != mi) return minor > mi;
    return subminor >= sub;
}

void Session::connect() {
    std::lock_guard<std::recursive_mutex> lk(mu);
    ensureThreadInit();
    openHandle();
    initSession();
    catalog = cfg->database;
    lastUsedMillis = nowMillis();
}

void Session::openHandle() {
    MYSQL* m = mysql_init(nullptr);
    if (m == nullptr) throwSql("Could not create connection to database server.", "08001");
    mysql_optionsv(m, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    unsigned int proto = cfg->socket.empty() ? MYSQL_PROTOCOL_TCP : MYSQL_PROTOCOL_SOCKET;
    mysql_optionsv(m, MYSQL_OPT_PROTOCOL, &proto);
    if (cfg->connectTimeoutMs > 0) {
        unsigned int t = (cfg->connectTimeoutMs + 999) / 1000;
        mysql_optionsv(m, MYSQL_OPT_CONNECT_TIMEOUT, &t);
    }
    if (cfg->socketTimeoutMs > 0) {
        unsigned int t = (cfg->socketTimeoutMs + 999) / 1000;
        mysql_optionsv(m, MYSQL_OPT_READ_TIMEOUT, &t);
        mysql_optionsv(m, MYSQL_OPT_WRITE_TIMEOUT, &t);
    }
    my_bool reconnect = 0;
    mysql_optionsv(m, MYSQL_OPT_RECONNECT, &reconnect);
    unsigned long flags = CLIENT_MULTI_RESULTS;
    if (!cfg->useAffectedRows) flags |= CLIENT_FOUND_ROWS;
    if (cfg->allowMultiQueries) flags |= CLIENT_MULTI_STATEMENTS;
    const char* db = cfg->database.empty() ? nullptr : cfg->database.c_str();
    const char* sock = cfg->socket.empty() ? nullptr : cfg->socket.c_str();
    const char* host = cfg->socket.empty() ? cfg->host.c_str() : "localhost";
    if (mysql_real_connect(m, host, cfg->user.c_str(), cfg->password.c_str(), db, cfg->port, sock, flags) == nullptr) {
        unsigned e = mysql_errno(m);
        String msg(mysql_error(m));
        String state(mysql_sqlstate(m));
        mysql_close(m);
        if (e >= 2000 && e < 3000) {
            throwSql(str("Communications link failure: ", msg), "08S01", static_cast<int32_t>(e));
        }
        throwSql(msg, state, static_cast<int32_t>(e));
    }
    mysql_ = m;
    broken = false;
    // Server version as Connector/J sees it: MariaDB >= 10 sends "5.5.5-10.x.y-MariaDB" in the
    // handshake (Connector/C strips the "5.5.5-" prefix).
    const char* info = mysql_get_server_info(m);
    std::string v = info ? info : "";
    unsigned long num = mysql_get_server_version(m);
    int32_t realMajor = static_cast<int32_t>(num / 10000);
    mariadb = v.find("MariaDB") != std::string::npos;
    if (mariadb && realMajor >= 10 && v.rfind("5.5.5-", 0) != 0) v = "5.5.5-" + v;
    serverVersion = String(v);
    // Parse like Connector/J (leading numbers of "a.b.c").
    int32_t parts[3] = {0, 0, 0};
    size_t p = 0;
    for (int k = 0; k < 3 && p < v.size(); k++) {
        int32_t val = 0;
        bool any = false;
        while (p < v.size() && std::isdigit(static_cast<unsigned char>(v[p]))) {
            val = val * 10 + (v[p] - '0');
            p++;
            any = true;
        }
        if (!any) break;
        parts[k] = val;
        if (p < v.size() && v[p] == '.') p++;
        else break;
    }
    major = parts[0];
    minor = parts[1];
    subminor = parts[2];
}

void Session::initSession() {
    if (initSql_.empty()) {
        if (cfg->jdbcCompliantTruncation) {
            // Connector/J setupServerForTruncationChecks: append STRICT_TRANS_TABLES.
            initSql_.push_back(
                "SET sql_mode = IF(FIND_IN_SET('STRICT_TRANS_TABLES', @@session.sql_mode) > 0, @@session.sql_mode, "
                "CONCAT_WS(',', NULLIF(@@session.sql_mode, ''), 'STRICT_TRANS_TABLES'))");
        }
        if (!cfg->sessionVariables.empty()) {
            // Split on commas outside quotes.
            std::string cur;
            char quote = 0;
            auto flush = [&] {
                size_t b = cur.find_first_not_of(" \t");
                if (b != std::string::npos) {
                    std::string item = cur.substr(b);
                    initSql_.push_back(item[0] == '@' ? "SET " + item : "SET SESSION " + item);
                }
                cur.clear();
            };
            for (char ch : cfg->sessionVariables) {
                if (quote != 0) {
                    if (ch == quote) quote = 0;
                    cur += ch;
                } else if (ch == '\'' || ch == '"') {
                    quote = ch;
                    cur += ch;
                } else if (ch == ',') {
                    flush();
                } else {
                    cur += ch;
                }
            }
            flush();
        }
    }
    for (auto& s : initSql_) {
        if (mysql_real_query(mysql_, s.data(), s.size()) != 0) {
            throwError();
        }
        // Discard any result.
        do {
            MYSQL_RES* r = mysql_store_result(mysql_);
            if (r) mysql_free_result(r);
        } while (mysql_next_result(mysql_) == 0);
    }
    if (cfg->legacyTimestamps) {
        unsigned long num = mysql_get_server_version(mysql_);
        bool has = mariadb ? num >= 100108 : num >= 50606;
        if (has) {
            static const char kSql[] = "SET SESSION explicit_defaults_for_timestamp = 0";
            if (mysql_real_query(mysql_, kSql, sizeof(kSql) - 1) == 0) {
                MYSQL_RES* r = mysql_store_result(mysql_);
                if (r) mysql_free_result(r);
            } else if (isCommunicationError(mysql_errno(mysql_))) {
                throwError();
            }
            // Other errors (read-only variable, missing privilege): ignored.
        }
    }
    // JDBC: new connections are in auto-commit mode.
    if ((mysql_->server_status & SERVER_STATUS_AUTOCOMMIT) == 0) {
        if (mysql_autocommit(mysql_, 1) != 0) throwError();
    }
    autoCommit = true;
}

void Session::close() noexcept {
    std::lock_guard<std::recursive_mutex> lk(mu);
    closed = true;
    if (mysql_ != nullptr) {
        ensureThreadInit();
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}

bool Session::isClosed() noexcept {
    std::lock_guard<std::recursive_mutex> lk(mu);
    return closed || (mysql_ == nullptr && !cfg->autoReconnect);
}

void Session::ensureOpen() {
    if (closed) throwSql("No operations allowed after connection closed.", "08003");
    if (mysql_ != nullptr) return;
    if (!cfg->autoReconnect) throwSql("No operations allowed after connection closed.", "08003");
    // Connector/J autoReconnect: re-establish and restore autocommit and the catalog.
    ensureThreadInit();
    bool oldAutoCommit = autoCommit;
    std::string oldCatalog = catalog;
    openHandle();
    initSession();
    if (!oldAutoCommit) {
        if (mysql_autocommit(mysql_, 0) != 0) throwError();
        autoCommit = false;
    }
    if (!oldCatalog.empty() && oldCatalog != cfg->database) {
        if (mysql_select_db(mysql_, oldCatalog.c_str()) != 0) throwError();
    }
    catalog = oldCatalog;
}

void Session::throwError(const char* context) {
    unsigned e = mysql_ ? mysql_errno(mysql_) : CR_SERVER_GONE_ERROR;
    String msg(mysql_ ? mysql_error(mysql_) : "MySQL server has gone away");
    String state(mysql_ ? mysql_sqlstate(mysql_) : "HY000");
    if (isCommunicationError(e)) {
        broken = true;
        if (mysql_ != nullptr) {
            mysql_close(mysql_);
            mysql_ = nullptr;
        }
        throwSql(str("Communications link failure: ", msg), "08S01", static_cast<int32_t>(e));
    }
    if (context != nullptr) msg = str(context, msg);
    throwSql(msg, state, static_cast<int32_t>(e));
}

ExecResult* Session::execute(std::string_view sql, int32_t maxRows) {
    std::lock_guard<std::recursive_mutex> lk(mu);
    ensureThreadInit();
    ensureOpen();
    lastUsedMillis = nowMillis();
    if (mysql_real_query(mysql_, sql.data(), static_cast<unsigned long>(sql.size())) != 0) throwError();
    ExecResult* first = nullptr;
    ExecResult* tail = nullptr;
    for (;;) {
        auto* r = new ExecResult();
        MYSQL_RES* res = mysql_store_result(mysql_);
        if (res != nullptr) {
            try {
                r->meta = buildMeta(res, cfg);
                r->rows = RowData::copy(res, maxRows);
            } catch (...) {
                mysql_free_result(res);
                throw;
            }
            mysql_free_result(res);
            r->updateCount = r->rows->nrows;
        } else if (mysql_field_count(mysql_) == 0) {
            r->updateCount = static_cast<int64_t>(mysql_affected_rows(mysql_));
            r->insertId = static_cast<int64_t>(mysql_insert_id(mysql_));
            const char* inf = mysql_info(mysql_);
            if (inf != nullptr) r->info = String(inf);
        } else {
            throwError();
        }
        if (first == nullptr) first = r;
        else tail->next = r;
        tail = r;
        int st = mysql_next_result(mysql_);
        if (st == -1) break;
        if (st > 0) throwError();
    }
    return first;
}

void Session::executeSimple(std::string_view sql) {
    (void)execute(sql, 0);
}

bool Session::ping(int32_t timeoutSeconds) noexcept {
    (void)timeoutSeconds;  // Connector/C cannot change the read timeout of an open handle
    std::lock_guard<std::recursive_mutex> lk(mu);
    if (closed || mysql_ == nullptr) return false;
    ensureThreadInit();
    if (mysql_ping(mysql_) != 0) return false;
    lastUsedMillis = nowMillis();
    return true;
}

int64_t Session::getAutoIncrementIncrement() {
    if (autoIncrementIncrement < 0) {
        ExecResult* r = execute("SELECT @@session.auto_increment_increment");
        int64_t v = 1;
        if (r->rows != nullptr && r->rows->nrows > 0 && r->rows->lens[0] > 0) {
            std::string s(r->rows->data + r->rows->offs[0], static_cast<size_t>(r->rows->lens[0]));
            v = std::strtoll(s.c_str(), nullptr, 10);
            if (v <= 0) v = 1;
        }
        autoIncrementIncrement = v;
    }
    return autoIncrementIncrement;
}

bool Session::noBackslashEscapes() noexcept {
    std::lock_guard<std::recursive_mutex> lk(mu);
    return mysql_ != nullptr && (mysql_->server_status & SERVER_STATUS_NO_BACKSLASH_ESCAPES) != 0;
}

std::string Session::quote(std::string_view s) {
    std::lock_guard<std::recursive_mutex> lk(mu);
    std::string out;
    if (mysql_ != nullptr) {
        out.resize(s.size() * 2 + 3);
        out[0] = '\'';
        unsigned long n = mysql_real_escape_string(mysql_, &out[1], s.data(), static_cast<unsigned long>(s.size()));
        if (n != static_cast<unsigned long>(-1)) {
            out.resize(n + 1);
            out += '\'';
            return out;
        }
        out.clear();
    }
    // No handle (waiting for autoReconnect): Connector/J's own escaping.
    out.reserve(s.size() + 2);
    out += '\'';
    for (char c : s) {
        switch (c) {
            case '\0': out += "\\0"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '"': out += "\\\""; break;
            case '\032': out += "\\Z"; break;
            default: out += c;
        }
    }
    out += '\'';
    return out;
}

// ---------------------------------------------------------------------------------------
// Connector/J StringUtils numeric parsers
namespace {
inline bool javaWhitespace(unsigned char c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F);
}
}  // namespace

bool cjGetLong(const char* buf, int64_t len, int64_t offset, int64_t endPos, int64_t& out) noexcept {
    int64_t s = offset;
    while (s < endPos && s < len && javaWhitespace(static_cast<unsigned char>(buf[s]))) s++;
    if (s >= endPos || s >= len) return false;
    bool negative = false;
    if (buf[s] == '-') {
        negative = true;
        s++;
    } else if (buf[s] == '+') {
        s++;
    }
    int64_t save = s;
    const int64_t cutoff = INT64_MAX / 10;
    int64_t cutlim = INT64_MAX % 10;
    if (negative) cutlim++;
    bool overflow = false;
    int64_t i = 0;
    for (; s < endPos && s < len; s++) {
        unsigned char c = static_cast<unsigned char>(buf[s]);
        if (c < '0' || c > '9') break;
        int d = c - '0';
        if (i > cutoff || (i == cutoff && d > cutlim)) {
            overflow = true;
        } else {
            i = i * 10 + d;
        }
    }
    if (s == save || overflow) return false;
    out = negative ? -i : i;  // -fwrapv: LONG_MIN wraps like Java
    return true;
}

bool cjGetInt(const char* buf, int64_t len, int64_t offset, int64_t endPos, int32_t& out) noexcept {
    int64_t s = offset;
    while (s < endPos && s < len && javaWhitespace(static_cast<unsigned char>(buf[s]))) s++;
    if (s >= endPos || s >= len) return false;
    bool negative = false;
    if (buf[s] == '-') {
        negative = true;
        s++;
    } else if (buf[s] == '+') {
        s++;
    }
    int64_t save = s;
    const int32_t cutoff = INT32_MAX / 10;
    int32_t cutlim = INT32_MAX % 10;
    if (negative) cutlim++;
    bool overflow = false;
    int32_t i = 0;
    for (; s < endPos && s < len; s++) {
        unsigned char c = static_cast<unsigned char>(buf[s]);
        if (c < '0' || c > '9') break;
        int d = c - '0';
        if (i > cutoff || (i == cutoff && d > cutlim)) {
            overflow = true;
        } else {
            i = i * 10 + d;
        }
    }
    if (s == save || overflow) return false;
    out = negative ? -i : i;
    return true;
}

bool cjGetShort(const char* buf, int64_t len, int16_t& out) noexcept {
    int64_t s = 0;
    while (s < len && javaWhitespace(static_cast<unsigned char>(buf[s]))) s++;
    if (s >= len) return false;
    bool negative = false;
    if (buf[s] == '-') {
        negative = true;
        s++;
    } else if (buf[s] == '+') {
        s++;
    }
    int64_t save = s;
    const int32_t cutoff = INT16_MAX / 10;
    int32_t cutlim = INT16_MAX % 10;
    if (negative) cutlim++;
    bool overflow = false;
    int32_t i = 0;
    for (; s < len; s++) {
        unsigned char c = static_cast<unsigned char>(buf[s]);
        if (c < '0' || c > '9') break;
        int d = c - '0';
        if (i > cutoff || (i == cutoff && d > cutlim)) {
            overflow = true;
        } else {
            i = static_cast<int16_t>(i * 10 + d);
        }
    }
    if (s == save || overflow) return false;
    out = static_cast<int16_t>(negative ? -i : i);
    return true;
}

// ---------------------------------------------------------------------------------------
// SQL text helpers
namespace {
inline bool isWs(char c) {
    return javaWhitespace(static_cast<unsigned char>(c));
}
bool startsWithIgnoreCaseAt(std::string_view s, size_t pos, std::string_view kw) {
    if (s.size() < pos + kw.size()) return false;
    for (size_t i = 0; i < kw.size(); i++) {
        if (std::toupper(static_cast<unsigned char>(s[pos + i])) != std::toupper(static_cast<unsigned char>(kw[i])))
            return false;
    }
    return true;
}
}  // namespace

bool startsWithIgnoreCaseAndWs(std::string_view sql, std::string_view keyword) noexcept {
    size_t p = 0;
    while (p < sql.size() && isWs(sql[p])) p++;
    return startsWithIgnoreCaseAt(sql, p, keyword);
}

int32_t findStartOfStatement(std::string_view sql) noexcept {
    int32_t pos = 0;
    if (startsWithIgnoreCaseAndWs(sql, "/*")) {
        size_t e = sql.find("*/");
        pos = e == std::string_view::npos ? 0 : static_cast<int32_t>(e + 2);
    } else if (startsWithIgnoreCaseAndWs(sql, "--") || startsWithIgnoreCaseAndWs(sql, "#")) {
        size_t e = sql.find('\n');
        if (e == std::string_view::npos) e = sql.find('\r');
        pos = e == std::string_view::npos ? 0 : static_cast<int32_t>(e);
    }
    return pos;
}

char firstAlphaCharUc(std::string_view sql, int32_t start) noexcept {
    for (size_t i = static_cast<size_t>(start); i < sql.size(); i++) {
        unsigned char c = static_cast<unsigned char>(sql[i]);
        if (std::isalpha(c)) return static_cast<char>(std::toupper(c));
    }
    return 0;
}

char firstNonWsCharUc(std::string_view sql, int32_t start) noexcept {
    for (size_t i = static_cast<size_t>(start); i < sql.size(); i++) {
        if (!isWs(sql[i])) return static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i])));
    }
    return 0;
}

bool startsWithKeywordIgnoringComments(std::string_view sql, std::string_view keyword) noexcept {
    // StringUtils.stripComments(sql, "'\"", "'\"", true, false, true, true) followed by
    // startsWithIgnoreCaseAndWs: only the leading part matters.
    size_t p = 0;
    for (;;) {
        while (p < sql.size() && isWs(sql[p])) p++;
        if (p + 1 < sql.size() && sql[p] == '/' && sql[p + 1] == '*') {
            size_t e = sql.find("*/", p + 2);
            if (e == std::string_view::npos) return false;
            p = e + 2;
        } else if (p < sql.size() && sql[p] == '#') {
            size_t e = sql.find_first_of("\r\n", p);
            if (e == std::string_view::npos) return false;
            p = e;
        } else if (p + 1 < sql.size() && sql[p] == '-' && sql[p + 1] == '-') {
            size_t e = sql.find_first_of("\r\n", p);
            if (e == std::string_view::npos) return false;
            p = e;
        } else {
            break;
        }
    }
    return startsWithIgnoreCaseAt(sql, p, keyword);
}

int32_t onDuplicateKeyLocation(std::string_view sql) noexcept {
    // StringUtils.indexOfIgnoreCaseRespectMarker(0, sql, " ON DUPLICATE KEY UPDATE ", "\"'`",
    // "\"'`", allowBackslashEscapes)
    static constexpr std::string_view kw = " ON DUPLICATE KEY UPDATE ";
    char marker = 0;
    for (size_t i = 0; i < sql.size(); i++) {
        char c = sql[i];
        if (c == '\\' && i + 1 < sql.size()) {
            i++;
            continue;
        }
        if (marker != 0) {
            if (c == marker) marker = 0;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            marker = c;
            continue;
        }
        if (startsWithIgnoreCaseAt(sql, i, kw)) return static_cast<int32_t>(i);
    }
    return -1;
}

}  // namespace jlang::sql_detail
