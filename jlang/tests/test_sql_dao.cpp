// jlang/tests/test_sql_dao.cpp - the real schemas (gameserver/sql/au_server_gs.sql,
// loginserver/sql/au_server_ls.sql) and hand-translated snippets of the MySQL5 DAO scripts
// (gameserver/data/scripts/system/database/mysql5, loginserver/data/scripts/system/database/
// mysql5) and of commons DatabaseFactory/DB/Transaction, run through jlang/Sql.h.
//
// Needs the local MariaDB server (see test_sql.cpp) and the repository's .sql files (found from
// this file's path or JLANG_REPO_ROOT). The gameserver schema is loaded with the mysql client
// when available (as an administrator would), the loginserver schema through jlang itself.
#include "jtest.h"

#include <jlang/Sql.h>

#if __has_include(<jlang/Time.h>)  // the tests need java.sql.Timestamp
#include <jlang/Time.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace jlang;

namespace {

std::string env(const char* name, const char* def) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' ? std::string(v) : std::string(def);
}
String baseUrl() { return String(env("JLANG_SQL_URL", "jdbc:mysql://127.0.0.1:3306/")); }
String user() { return String(env("JLANG_SQL_USER", "aion")); }
String password() { return String(env("JLANG_SQL_PASSWORD", "aion")); }

std::string repoRoot() {
    std::string r = env("JLANG_REPO_ROOT", "");
    if (!r.empty()) return r;
    std::string f = __FILE__;
    size_t p = f.rfind("/jlang/tests/");
    if (p != std::string::npos) return f.substr(0, p);
    return "/home/user/AionLightning";
}

bool fileExists(const std::string& p) {
    std::ifstream in(p);
    return in.good();
}

std::string readFile(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool available() {
    static int state = -1;
    if (state < 0) {
        state = 0;
        try {
            DriverManager::getConnection(baseUrl(), user(), password())->close();
            if (fileExists(repoRoot() + "/gameserver/sql/au_server_gs.sql")) state = 1;
            else std::fprintf(stderr, "  (schema files not found under %s, skipped)\n", repoRoot().c_str());
        } catch (SQLException& e) {
            std::fprintf(stderr, "  (MariaDB not reachable, skipped: %s)\n", std::string(e.getMessage()).c_str());
        }
    }
    return state == 1;
}

void adminExec(const String& sql) {
    Connection* c = DriverManager::getConnection(baseUrl(), user(), password());
    JFINALLY { c->close(); };
    c->createStatement()->executeUpdate(sql);
}

// Splits a dump into statements (';' at the end of a line) and runs them through jlang.
int32_t loadSchemaWithJlang(Connection* c, const std::string& file) {
    std::string text = readFile(file);
    std::vector<std::string> stmts;
    std::string cur;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        cur += line;
        cur += '\n';
        size_t e = line.find_last_not_of(" \t");
        if (e != std::string::npos && line[e] == ';') {
            stmts.push_back(cur);
            cur.clear();
        }
    }
    int32_t n = 0;
    Statement* st = c->createStatement();
    for (auto& s : stmts) {
        // skip chunks that are only comments
        std::string code;
        std::istringstream ls(s);
        bool inBlock = false;
        while (std::getline(ls, line)) {
            if (inBlock) {
                if (line.find("*/") != std::string::npos) inBlock = false;
                continue;
            }
            if (line.rfind("/*", 0) == 0 && line.find("*/") == std::string::npos) {
                inBlock = true;
                continue;
            }
            if (line.rfind("--", 0) == 0) continue;
            code += line + "\n";
        }
        if (code.find_first_not_of(" \t\r\n;") == std::string::npos) continue;
        st->execute(code);
        n++;
    }
    return n;
}

int32_t tableCount(Connection* c, const String& db) {
    PreparedStatement* ps = c->prepareStatement("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = ?");
    ps->setString(1, db);
    ResultSet* rs = ps->executeQuery();
    rs->next();
    return rs->getInt(1);
}

// ---------------------------------------------------------------------------------------
// commons: DatabaseFactory / DB / Transaction over jlang::DataSource (the shape the translated
// commons code will have).
struct DatabaseFactory {
    static inline DataSource* dataSource = nullptr;
    static inline String databaseName;
    static inline int32_t databaseMajorVersion = 0;
    static inline int32_t databaseMinorVersion = 0;

    static void init(const String& url, int32_t min, int32_t max) {
        dataSource = new DataSource(url, user(), password());
        dataSource->setMaxIdle(min);
        dataSource->setMaxActive(max);
        Connection* c = getConnection();
        DatabaseMetaData* dmd = c->getMetaData();
        databaseName = dmd->getDatabaseProductName();
        databaseMajorVersion = dmd->getDatabaseMajorVersion();
        databaseMinorVersion = dmd->getDatabaseMinorVersion();
        c->close();
    }
    static Connection* getConnection() { return dataSource->getConnection(); }
    static void close(Connection* con) {
        if (con == nullptr) return;
        con->close();
    }
    static void shutdown() {
        dataSource->close();
        dataSource = nullptr;
    }
};

// MySQL5DAOUtils.supports
bool supports(const String& db, int32_t majorVersion, int32_t minorVersion) {
    (void)minorVersion;
    return db.equals("MySQL") && majorVersion == 5;
}

struct DB {
    // DB.select(query, ReadStH / ParamReadStH)
    static bool select(const String& query, std::function<void(ResultSet*)> handleRead,
                       std::function<void(PreparedStatement*)> setParams = nullptr) {
        Connection* con = nullptr;
        PreparedStatement* stmt = nullptr;
        {
            JFINALLY {
                if (con != nullptr) con->close();
                if (stmt != nullptr) stmt->close();
            };
            try {
                con = DatabaseFactory::getConnection();
                stmt = con->prepareStatement(query);
                if (setParams) setParams(stmt);
                ResultSet* rset = stmt->executeQuery();
                handleRead(rset);
            } catch (Exception& e) {
                std::fprintf(stderr, "  DB.select failed: %s\n", e.what());
                return false;
            }
        }
        return true;
    }
    // DB.insertUpdate(query, IUStH)
    static bool insertUpdate(const String& query, std::function<void(PreparedStatement*)> batch = nullptr) {
        Connection* con = nullptr;
        PreparedStatement* stmt = nullptr;
        {
            JFINALLY {
                if (con != nullptr) con->close();
                if (stmt != nullptr) stmt->close();
            };
            try {
                con = DatabaseFactory::getConnection();
                stmt = con->prepareStatement(query);
                if (batch) batch(stmt);
                else stmt->executeUpdate();
            } catch (Exception& e) {
                std::fprintf(stderr, "  DB.insertUpdate failed: %s\n", e.what());
                return false;
            }
        }
        return true;
    }
    static PreparedStatement* prepareStatement(const String& sql) {
        return prepareStatement(sql, ResultSet::TYPE_FORWARD_ONLY, ResultSet::CONCUR_READ_ONLY);
    }
    static PreparedStatement* prepareStatement(const String& sql, int32_t type, int32_t conc) {
        Connection* c = nullptr;
        PreparedStatement* ps = nullptr;
        try {
            c = DatabaseFactory::getConnection();
            ps = c->prepareStatement(sql, type, conc);
        } catch (Exception&) {
            if (c != nullptr) c->close();
        }
        return ps;
    }
    static ResultSet* executeQuerry(PreparedStatement* statement) {
        try {
            return statement->executeQuery();
        } catch (Exception&) {
            return nullptr;
        }
    }
    static void close(PreparedStatement* statement) {
        try {
            if (statement->isClosed()) return;
            Connection* c = statement->getConnection();
            statement->close();
            c->close();
        } catch (Exception&) {
        }
    }
};

// commons Transaction
struct Transaction {
    Connection* connection;
    explicit Transaction(Connection* con) : connection(con) { connection->setAutoCommit(false); }
    void insertUpdate(const String& sql, std::function<void(PreparedStatement*)> iusth = nullptr) {
        PreparedStatement* statement = connection->prepareStatement(sql);
        if (iusth) iusth(statement);
        else statement->executeUpdate();
    }
    Savepoint* setSavepoint(const String& name) { return connection->setSavepoint(name); }
    void commit(Savepoint* rollBackToOnError = nullptr) {
        try {
            connection->commit();
        } catch (SQLException&) {
            try {
                if (rollBackToOnError != nullptr) connection->rollback(rollBackToOnError);
                else connection->rollback();
            } catch (SQLException&) {
            }
        }
        connection->setAutoCommit(true);
        connection->close();
    }
};

int64_t now() {
    return System::currentTimeMillis();
}

}  // namespace

// ---------------------------------------------------------------------------------------
JTEST(SqlRealSchemasLoad) {
    if (!available()) return;
    adminExec("DROP DATABASE IF EXISTS jlang_test_gs");
    adminExec("DROP DATABASE IF EXISTS jlang_test_ls");
    adminExec("CREATE DATABASE jlang_test_gs");
    adminExec("CREATE DATABASE jlang_test_ls");
    JFINALLY {
        try {
            adminExec("DROP DATABASE IF EXISTS jlang_test_gs");
            adminExec("DROP DATABASE IF EXISTS jlang_test_ls");
        } catch (...) {
        }
    };
    std::string gs = repoRoot() + "/gameserver/sql/au_server_gs.sql";
    std::string ls = repoRoot() + "/loginserver/sql/au_server_ls.sql";
    Connection* admin = DriverManager::getConnection(baseUrl(), user(), password());
    JFINALLY { admin->close(); };
    // gameserver schema: mysql client (falls back to jlang when the client is missing)
    std::string cmd = "mysql -h127.0.0.1 -u" + std::string(user()) + " -p" + std::string(password()) +
                      " jlang_test_gs < '" + gs + "' 2>&1";
    int rc = std::system(("command -v mysql >/dev/null 2>&1 && " + cmd).c_str());
    if (rc != 0) {
        Connection* g = DriverManager::getConnection(str(baseUrl(), "jlang_test_gs"), user(), password());
        JFINALLY { g->close(); };
        loadSchemaWithJlang(g, gs);
    }
    JCHECK_EQ(tableCount(admin, "jlang_test_gs"), 42);
    // loginserver schema: through jlang (DDL + INSERTs as plain statements)
    Connection* l = DriverManager::getConnection(str(baseUrl(), "jlang_test_ls"), user(), password());
    JFINALLY { l->close(); };
    int32_t n = loadSchemaWithJlang(l, ls);
    JCHECK(n > 10);
    JCHECK_EQ(tableCount(admin, "jlang_test_ls"), 7);
    // The InnoDB reference variants load too.
    for (const char* f : {"/gameserver/sql/InnoDB/Reference_InnoDB_au_server_gs.sql",
                          "/loginserver/sql/InnoDB/Reference_InnoDB_au_server_ls.sql"}) {
        adminExec("DROP DATABASE IF EXISTS jlang_test_ref");
        adminExec("CREATE DATABASE jlang_test_ref");
        Connection* r = DriverManager::getConnection(str(baseUrl(), "jlang_test_ref"), user(), password());
        loadSchemaWithJlang(r, repoRoot() + f);
        JCHECK(tableCount(admin, "jlang_test_ref") >= 7);
        r->close();
    }
    adminExec("DROP DATABASE IF EXISTS jlang_test_ref");
}

// ---------------------------------------------------------------------------------------
JTEST(SqlGameServerDaos) {
    if (!available()) return;
    adminExec("DROP DATABASE IF EXISTS jlang_test_gsdao");
    adminExec("CREATE DATABASE jlang_test_gsdao");
    JFINALLY {
        try {
            if (DatabaseFactory::dataSource != nullptr) DatabaseFactory::shutdown();
            adminExec("DROP DATABASE IF EXISTS jlang_test_gsdao");
        } catch (...) {
        }
    };
    {
        Connection* g = DriverManager::getConnection(str(baseUrl(), "jlang_test_gsdao"), user(), password());
        JFINALLY { g->close(); };
        loadSchemaWithJlang(g, repoRoot() + "/gameserver/sql/au_server_gs.sql");
    }
    // commons/config/network/database.properties style URL, min 5 / max 10
    DatabaseFactory::init(str(baseUrl(), "jlang_test_gsdao?useUnicode=true&characterEncoding=UTF-8"), 5, 10);
    JCHECK_EQ(DatabaseFactory::databaseName, String("MySQL"));
    JCHECK(supports(DatabaseFactory::databaseName, DatabaseFactory::databaseMajorVersion, DatabaseFactory::databaseMinorVersion));

    // ---- MySQL5PlayerDAO.saveNewPlayer
    const int32_t playerId = 100001;
    int64_t created = (now() / 1000) * 1000;
    {
        Connection* con = nullptr;
        bool ok = true;
        {
            JFINALLY { DatabaseFactory::close(con); };
            try {
                con = DatabaseFactory::getConnection();
                PreparedStatement* preparedStatement = con->prepareStatement(
                    str("INSERT INTO players(id, `name`, account_id, account_name, x, y, z, heading, world_id, gender, race, player_class , cube_size, warehouse_size, online, last_online, creation_date) ",
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?)"));
                preparedStatement->setInt(1, playerId);
                preparedStatement->setString(2, "JörgTheBrave");
                preparedStatement->setInt(3, 7);
                preparedStatement->setString(4, "account'7");
                preparedStatement->setFloat(5, 1234.5f);
                preparedStatement->setFloat(6, -98.25f);
                preparedStatement->setFloat(7, 0.1f);
                preparedStatement->setInt(8, 100);
                preparedStatement->setInt(9, 210010000);
                preparedStatement->setString(10, "FEMALE");
                preparedStatement->setString(11, "ELYOS");
                preparedStatement->setString(12, "SORCERER");
                preparedStatement->setInt(13, 0);
                preparedStatement->setInt(14, 0);
                preparedStatement->setTimestamp(15, new Timestamp(created));
                preparedStatement->setTimestamp(16, new Timestamp(created));
                preparedStatement->execute();
                preparedStatement->close();
            } catch (Exception& e) {
                std::fprintf(stderr, "  saveNewPlayer: %s\n", e.what());
                ok = false;
            }
        }
        JCHECK(ok);
    }
    // ---- MySQL5PlayerDAO.loadPlayerCommonData
    {
        Connection* con = nullptr;
        bool success = false;
        {
            JFINALLY { DatabaseFactory::close(con); };
            con = DatabaseFactory::getConnection();
            PreparedStatement* stmt = con->prepareStatement("SELECT * FROM players WHERE id = ?");
            stmt->setInt(1, playerId);
            ResultSet* resultSet = stmt->executeQuery();
            if (resultSet->next()) {
                success = true;
                JCHECK_EQ(resultSet->getString("name"), String("JörgTheBrave"));
                JCHECK_EQ(resultSet->getString("player_class"), String("SORCERER"));
                JCHECK_EQ(resultSet->getLong("exp"), INT64_C(0));
                JCHECK_EQ(resultSet->getString("race"), String("ELYOS"));
                JCHECK_EQ(resultSet->getString("gender"), String("FEMALE"));
                JCHECK(resultSet->getString("note").isNull());
                JCHECK_EQ(resultSet->getInt("cube_size"), 0);
                JCHECK_EQ(resultSet->getInt("title_id"), -1);
                JCHECK_EQ(resultSet->getBoolean("online"), false);
                JCHECK_EQ(resultSet->getFloat("x"), 1234.5f);
                JCHECK_EQ(resultSet->getFloat("y"), -98.25f);
                JCHECK_EQ(resultSet->getFloat("z"), 0.1f);
                JCHECK_EQ(resultSet->getByte("heading"), static_cast<int8_t>(100));
                JCHECK_EQ(resultSet->getInt("world_id"), 210010000);
                Timestamp* lastOnline = resultSet->getTimestamp("last_online");
                JCHECK(lastOnline != nullptr && lastOnline->getTime() == created);
            }
            resultSet->close();
            stmt->close();
        }
        JCHECK(success);
    }
    // ---- MySQL5PlayerDAO.storePlayer / storeLastOnlineTime (null -> current time, legacy rules)
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        PreparedStatement* stmt = con->prepareStatement(
            "UPDATE players SET name=?, exp=?, recoverexp=?, x=?, y=?, z=?, heading=?, world_id=?, gender=?, player_class=?, last_online=?, cube_size=?, advenced_stigma_slot_size=?, warehouse_size=?, note=?, bind_point=?, title_id=?, mailboxLetters=?, repletionstate=? WHERE id=?");
        stmt->setString(1, "JörgTheBrave");
        stmt->setLong(2, INT64_C(123456789012));
        stmt->setLong(3, 5);
        stmt->setFloat(4, 1.5f);
        stmt->setFloat(5, 2.5f);
        stmt->setFloat(6, -1001.0f);
        stmt->setInt(7, -20);
        stmt->setInt(8, 110010000);
        stmt->setString(9, "MALE");
        stmt->setString(10, "MAGE");
        stmt->setTimestamp(11, nullptr);
        stmt->setInt(12, 2);
        stmt->setInt(13, 1);
        stmt->setInt(14, 3);
        stmt->setString(15, "a note with 'quotes' and \\ backslash");
        stmt->setInt(16, 0);
        stmt->setInt(17, 45);
        stmt->setInt(18, 3);
        stmt->setLong(19, 99);
        stmt->setInt(20, playerId);
        JCHECK(!stmt->execute());
        JCHECK_EQ(stmt->getUpdateCount(), 1);
        stmt->close();
        ResultSet* rs = con->createStatement()->executeQuery(str("SELECT exp, heading, note, last_online FROM players WHERE id = ", playerId));
        JCHECK(rs->next());
        JCHECK_EQ(rs->getLong("exp"), INT64_C(123456789012));
        JCHECK_EQ(rs->getByte("heading"), static_cast<int8_t>(-20));
        JCHECK_EQ(rs->getString("note"), String("a note with 'quotes' and \\ backslash"));
        JCHECK(rs->getTimestamp("last_online") != nullptr);
    }
    // ---- MySQL5PlayerDAO.loadPlayerAccountData: deletion_date NULL, creation_date set
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        PreparedStatement* stmt = con->prepareStatement("SELECT creation_date, deletion_date FROM players WHERE id = ?");
        stmt->setInt(1, playerId);
        ResultSet* rset = stmt->executeQuery();
        JCHECK(rset->next());
        JCHECK(rset->getTimestamp("deletion_date") == nullptr);
        JCHECK_EQ(rset->getTimestamp("creation_date")->getTime(), created);
    }
    // ---- MySQL5PlayerSettingsDAO.saveSettings / loadSettings (REPLACE + blobs + int in blob)
    auto* ui = new Array<int8_t>(300);
    for (int32_t i = 0; i < ui->length; i++) (*ui)[i] = static_cast<int8_t>(i * 7);
    for (int32_t type = 0; type < 3; type++) {
        for (int round = 0; round < 2; round++) {  // REPLACE twice
            Connection* con = nullptr;
            JFINALLY { DatabaseFactory::close(con); };
            con = DatabaseFactory::getConnection();
            PreparedStatement* stmt = con->prepareStatement("REPLACE INTO player_settings values (?, ?, ?)");
            stmt->setInt(1, playerId);
            stmt->setInt(2, type);
            if (type == 2) stmt->setInt(3, 12345);
            else stmt->setBytes(3, ui);
            stmt->execute();
        }
    }
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        PreparedStatement* statement = con->prepareStatement("SELECT * FROM player_settings WHERE player_id = ?");
        statement->setInt(1, playerId);
        ResultSet* resultSet = statement->executeQuery();
        int found = 0;
        while (resultSet->next()) {
            int32_t type = resultSet->getInt("settings_type");
            switch (type) {
                case 0:
                case 1: {
                    Array<int8_t>* b = resultSet->getBytes("settings");
                    JCHECK(b != nullptr && b->length == 300 && (*b)[299] == static_cast<int8_t>(299 * 7));
                    found++;
                    break;
                }
                case 2:
                    JCHECK_EQ(resultSet->getInt("settings"), 12345);
                    found++;
                    break;
            }
        }
        JCHECK_EQ(found, 3);
    }
    // ---- MySQL5FriendListDAO.addFriends (batch)
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        PreparedStatement* stmt = con->prepareStatement("INSERT INTO friends (player, friend) VALUES (?, ?)");
        stmt->setInt(1, playerId);
        stmt->setInt(2, 200);
        stmt->addBatch();
        stmt->setInt(1, 200);
        stmt->setInt(2, playerId);
        stmt->addBatch();
        stmt->executeBatch();
        stmt->close();
        JCHECK(DB::select("SELECT COUNT(*) AS c FROM friends", [](ResultSet* rs) {
            rs->next();
            JCHECK_EQ(rs->getInt("c"), 2);
        }));
    }
    // ---- MySQL5SpawnDAO: group + spawn with generated keys, load templates
    int32_t spawnId = 0;
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        PreparedStatement* stmt = con->prepareStatement("INSERT INTO `spawn_groups`(`admin_id`, `group_name`, `spawned`) VALUES (?,?,?)");
        stmt->setInt(1, 1);
        stmt->setString(2, "group-é");
        stmt->setBoolean(3, false);
        stmt->execute();
        stmt->close();
        stmt = con->prepareStatement("SELECT `spawned` FROM `spawn_groups` WHERE `admin_id` = ? AND `group_name` = ?");
        stmt->setInt(1, 1);
        stmt->setString(2, "group-é");
        ResultSet* rs = stmt->executeQuery();
        JCHECK(rs->next());
        JCHECK(!rs->getBoolean("spawned"));
        rs->close();
        stmt->close();
        stmt = con->prepareStatement(
            "INSERT INTO `spawns`(`admin_id`, `group_name`, `npc_id`, `respawn`, `map_id`, `x`, `y`, `z`, `h`, `object_id`, `spawned`,`staticid` ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
            Statement::RETURN_GENERATED_KEYS);
        stmt->setInt(1, 1);
        stmt->setString(2, "group-é");
        stmt->setInt(3, 203000);
        stmt->setBoolean(4, true);
        stmt->setInt(5, 210010000);
        stmt->setFloat(6, 100.5f);
        stmt->setFloat(7, 200.25f);
        stmt->setFloat(8, 300.125f);
        stmt->setByte(9, static_cast<int8_t>(119));
        stmt->setInt(10, 777);
        stmt->setBoolean(11, true);
        stmt->setInt(12, 0);
        stmt->execute();
        rs = stmt->getGeneratedKeys();
        if (rs->next()) spawnId = rs->getInt(1);
        rs->close();
        stmt->close();
        JCHECK(spawnId > 0);
        stmt = con->prepareStatement("SELECT * FROM `spawns` WHERE `spawned` = ?");
        stmt->setBoolean(1, true);
        rs = stmt->executeQuery();
        JCHECK(rs->next());
        JCHECK_EQ(rs->getFloat("x"), 100.5f);
        JCHECK_EQ(rs->getByte("h"), static_cast<int8_t>(119));
        JCHECK_EQ(rs->getInt("spawn_id"), spawnId);
        JCHECK(rs->getBoolean("respawn"));
        JCHECK(rs->getBoolean("spawned"));
        JCHECK(!rs->next());
    }
    // ---- MySQL5InventoryDAO.insertItem / loadStorage (timestamps)
    int64_t itemCreationTime = INT64_C(1300000000000);
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        PreparedStatement* stmt = con->prepareStatement(
            "INSERT INTO `inventory` (`itemUniqueId`, `itemId`, `itemCount`, `itemColor`, `itemOwner`, `isEquiped`, isSoulBound, `slot`, `itemLocation`, `enchant`, `itemSkin`, `fusionedItem`, `optionalSocket`, `optionalFusionSocket`, `itemCreator`, `itemCreationTime`, `itemExistTime`, `itemTradeTime`) VALUES(?,?,?,?,?,?,?,?,?,?, ?,?,?,?,?,?,?,?)");
        stmt->setInt(1, 500001);
        stmt->setInt(2, 182400001);
        stmt->setLong(3, INT64_C(9000000000));
        stmt->setInt(4, 0);
        stmt->setInt(5, playerId);
        stmt->setBoolean(6, false);
        stmt->setInt(7, 1);
        stmt->setInt(8, 0);
        stmt->setInt(9, 0);
        stmt->setInt(10, 15);
        stmt->setInt(11, 182400001);
        stmt->setInt(12, 0);
        stmt->setInt(13, 0);
        stmt->setInt(14, 0);
        stmt->setString(15, "");
        stmt->setTimestamp(16, new Timestamp(itemCreationTime));
        stmt->setLong(17, 0);
        stmt->setInt(18, 0);
        stmt->execute();
        stmt->close();
        stmt = con->prepareStatement(
            "SELECT `itemUniqueId`, `itemId`, `itemCount`, `itemColor`, `isEquiped`, `isSoulBound`, `slot`, `enchant`, `itemSkin`, `fusionedItem`, `optionalSocket`, `optionalFusionSocket`, `itemCreator`, `itemCreationTime`, `itemExistTime`, `itemTradeTime` FROM `inventory` WHERE `itemOwner`=? AND `itemLocation`=? AND `isEquiped`=?");
        stmt->setInt(1, playerId);
        stmt->setInt(2, 0);
        stmt->setInt(3, 0);
        ResultSet* rset = stmt->executeQuery();
        JCHECK(rset->next());
        JCHECK_EQ(rset->getLong("itemCount"), INT64_C(9000000000));
        JCHECK_EQ(rset->getInt("isSoulBound"), 1);
        JCHECK_EQ(rset->getInt("enchant"), 15);
        JCHECK_EQ(rset->getString("itemCreator"), String(""));
        JCHECK_EQ(rset->getTimestamp("itemCreationTime")->getTime(), itemCreationTime);
        rset->close();
        stmt->close();
    }
    // ---- MySQL5LegionDAO: legion, emblem (boolean + blob), ranking query with trailing ';'
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        PreparedStatement* ps = con->prepareStatement("INSERT INTO legions(id, `name`) VALUES (?, ?)");
        for (int i = 0; i < 3; i++) {
            ps->setInt(1, 900 + i);
            ps->setString(2, str("Legion", i));
            ps->execute();
        }
        con->createStatement()->executeUpdate("UPDATE legions SET contribution_points = id - 900");
        PreparedStatement* stmt = con->prepareStatement("SELECT count(id) as cnt FROM legions WHERE ? = legions.name");
        stmt->setString(1, "Legion1");
        ResultSet* rs = stmt->executeQuery();
        rs->next();
        JCHECK(rs->getInt("cnt") > 0);
        PreparedStatement* preparedStatement = con->prepareStatement(
            "INSERT INTO legion_emblems(legion_id, emblem_ver, color_r, color_g, color_b, custom, emblem_data) VALUES (?, ?, ?, ?, ?, ?, ?)");
        auto* emblem = new Array<int8_t>(1024);
        for (int32_t i = 0; i < emblem->length; i++) (*emblem)[i] = static_cast<int8_t>(255 - (i % 256));
        preparedStatement->setInt(1, 900);
        preparedStatement->setInt(2, 1);
        preparedStatement->setInt(3, 255);
        preparedStatement->setInt(4, 128);
        preparedStatement->setInt(5, 0);
        preparedStatement->setBoolean(6, true);
        preparedStatement->setBytes(7, emblem);
        preparedStatement->execute();
        preparedStatement->setInt(1, 901);
        preparedStatement->setBoolean(6, false);
        preparedStatement->setBytes(7, nullptr);
        preparedStatement->execute();
        stmt = con->prepareStatement("SELECT * FROM legion_emblems WHERE legion_id=?");
        stmt->setInt(1, 900);
        ResultSet* resultSet = stmt->executeQuery();
        JCHECK(resultSet->next());
        JCHECK_EQ(resultSet->getInt("color_r"), 255);
        JCHECK(resultSet->getBoolean("custom"));
        Array<int8_t>* data = resultSet->getBytes("emblem_data");
        JCHECK(data != nullptr && data->length == 1024 && (*data)[1] == static_cast<int8_t>(254));
        stmt->setInt(1, 901);
        resultSet = stmt->executeQuery();
        JCHECK(resultSet->next());
        JCHECK(!resultSet->getBoolean("custom"));
        JCHECK(resultSet->getBytes("emblem_data") == nullptr);
        stmt = con->prepareStatement("SELECT id, contribution_points FROM legions ORDER BY contribution_points DESC;");
        resultSet = stmt->executeQuery();
        std::map<int32_t, int32_t> legionRanking;
        int32_t i = 1;
        while (resultSet->next()) {
            if (resultSet->getInt("contribution_points") > 0) {
                legionRanking[resultSet->getInt("id")] = i;
                i++;
            } else {
                legionRanking[resultSet->getInt("id")] = 0;
            }
        }
        JCHECK_EQ(legionRanking[902], 1);
        JCHECK_EQ(legionRanking[901], 2);
        JCHECK_EQ(legionRanking[900], 0);
        // legion history with timestamps and ENUM
        PreparedStatement* h = con->prepareStatement("INSERT INTO legion_history(`legion_id`, `date`, `history_type`, `name`) VALUES (?, ?, ?, ?)");
        h->setInt(1, 900);
        h->setTimestamp(2, new Timestamp(INT64_C(1308650400000)));
        h->setString(3, "CREATE");
        h->setString(4, "Jörg");
        h->execute();
        PreparedStatement* hs = con->prepareStatement("SELECT * FROM `legion_history` WHERE legion_id=? ORDER BY date ASC;");
        hs->setInt(1, 900);
        ResultSet* hr = hs->executeQuery();
        JCHECK(hr->next());
        JCHECK_EQ(hr->getString("history_type"), String("CREATE"));
        JCHECK_EQ(hr->getTimestamp("date")->getTime(), INT64_C(1308650400000));
    }
    // ---- MySQL5IdViewDAO.getUsedIDs (UNION, scrollable, read-only connection returned to the pool)
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        con->setReadOnly(true);
        PreparedStatement* statement = con->prepareStatement(
            "SELECT `players`.`id` AS `id` from `players` UNION SELECT `itemUniqueId` FROM `inventory` UNION SELECT `id` FROM `legions` UNION SELECT `mailUniqueId` FROM `mail`",
            ResultSet::TYPE_SCROLL_INSENSITIVE, ResultSet::CONCUR_READ_ONLY);
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
        JCHECK_EQ(count, 5);  // 1 player + 1 item + 3 legions
    }
    // The pool handed the read-only connection back read-write.
    {
        Connection* con = DatabaseFactory::getConnection();
        JFINALLY { DatabaseFactory::close(con); };
        JCHECK(!con->isReadOnly());
    }
    // ---- commons Transaction
    {
        Transaction* tx = new Transaction(DatabaseFactory::getConnection());
        tx->insertUpdate("INSERT INTO blocks (player, blocked_player, reason) VALUES (1, 2, 'spam')");
        Savepoint* sp = tx->setSavepoint("sp");
        tx->insertUpdate("INSERT INTO blocks (player, blocked_player, reason) VALUES (?, ?, ?)", [](PreparedStatement* ps) {
            ps->setInt(1, 1);
            ps->setInt(2, 3);
            ps->setString(3, "flood");
            ps->executeUpdate();
        });
        tx->commit(sp);
        JCHECK(DB::select("SELECT COUNT(*) FROM blocks", [](ResultSet* rs) {
            rs->next();
            JCHECK_EQ(rs->getInt(1), 2);
        }));
    }
    JCHECK_EQ(DatabaseFactory::dataSource->getNumActive(), 0);
    JCHECK(DatabaseFactory::dataSource->getNumIdle() <= 5);
    DatabaseFactory::shutdown();
}

// ---------------------------------------------------------------------------------------
JTEST(SqlLoginServerDaos) {
    if (!available()) return;
    adminExec("DROP DATABASE IF EXISTS jlang_test_lsdao");
    adminExec("CREATE DATABASE jlang_test_lsdao");
    JFINALLY {
        try {
            if (DatabaseFactory::dataSource != nullptr) DatabaseFactory::shutdown();
            adminExec("DROP DATABASE IF EXISTS jlang_test_lsdao");
        } catch (...) {
        }
    };
    {
        Connection* l = DriverManager::getConnection(str(baseUrl(), "jlang_test_lsdao"), user(), password());
        JFINALLY { l->close(); };
        loadSchemaWithJlang(l, repoRoot() + "/loginserver/sql/au_server_ls.sql");
    }
    // loginserver/config/network/database.properties style URL (no parameters), min 5 / max 10
    DatabaseFactory::init(str(baseUrl(), "jlang_test_lsdao"), 5, 10);
    // ---- MySQL5AccountDAO.insertAccount (DB.prepareStatement + DB.close)
    int32_t result = 0;
    {
        PreparedStatement* st = DB::prepareStatement(
            "INSERT INTO account_data(`name`, `password`, access_level, membership, activated, last_server, last_ip, ip_force) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        JFINALLY { DB::close(st); };
        st->setString(1, "admin");
        st->setString(2, "hash+/=");
        st->setByte(3, 3);
        st->setByte(4, 0);
        st->setByte(5, 1);
        st->setByte(6, -1);
        st->setString(7, "127.0.0.1");
        st->setString(8, String());
        result = st->executeUpdate();
    }
    JCHECK_EQ(result, 1);
    JCHECK_EQ(DatabaseFactory::dataSource->getNumActive(), 0);
    // ---- MySQL5AccountDAO.getAccount
    {
        PreparedStatement* st = DB::prepareStatement("SELECT * FROM account_data WHERE `name` = ?");
        JFINALLY { DB::close(st); };
        st->setString(1, "admin");
        ResultSet* rs = st->executeQuery();
        JCHECK(rs->next());
        JCHECK(rs->getInt("id") > 0);
        JCHECK_EQ(rs->getString("password"), String("hash+/="));
        JCHECK_EQ(rs->getByte("access_level"), static_cast<int8_t>(3));
        JCHECK_EQ(rs->getByte("membership"), static_cast<int8_t>(0));
        JCHECK_EQ(rs->getByte("activated"), static_cast<int8_t>(1));
        JCHECK_EQ(rs->getByte("last_server"), static_cast<int8_t>(-1));
        JCHECK_EQ(rs->getString("last_ip"), String("127.0.0.1"));
        JCHECK(rs->getString("ip_force").isNull());
    }
    // ---- MySQL5AccountDAO.getAccountCount (DB.executeQuerry)
    {
        PreparedStatement* st = DB::prepareStatement("SELECT count(*) AS c FROM account_data");
        ResultSet* rs = DB::executeQuerry(st);
        JFINALLY { DB::close(st); };
        rs->next();
        JCHECK_EQ(rs->getInt("c"), 1);
    }
    // ---- MySQL5GameServersDAO.getAllGameServers: getByte on int(11) id 11
    {
        std::map<int8_t, String> servers;
        JCHECK(DB::select("SELECT * FROM gameservers", [&](ResultSet* resultSet) {
            while (resultSet->next()) {
                int8_t id = resultSet->getByte("id");
                String ipMask = resultSet->getString("mask");
                String pw = resultSet->getString("password");
                servers[id] = ipMask;
                JCHECK_EQ(pw, String("aion"));
            }
        }));
        JCHECK_EQ(servers.size(), static_cast<size_t>(1));
        JCHECK_EQ(servers[11], String("*"));
    }
    // ---- MySQL5BannedIpDAO.insert (setNull(Types.TIMESTAMP)) + DB.select(ParamReadStH)
    for (int i = 0; i < 2; i++) {
        String mask = str("10.0.0.", i);
        Timestamp* timeEnd = i == 0 ? nullptr : new Timestamp(INT64_C(1893456000000));
        bool insert = DB::insertUpdate("INSERT INTO banned_ip(mask, time_end) VALUES (?, ?)", [&](PreparedStatement* preparedStatement) {
            preparedStatement->setString(1, mask);
            if (timeEnd == nullptr) preparedStatement->setNull(2, Types::TIMESTAMP);
            else preparedStatement->setTimestamp(2, timeEnd);
            preparedStatement->execute();
        });
        JCHECK(insert);
        int32_t id = 0;
        Timestamp* readBack = nullptr;
        JCHECK(DB::select(
            "SELECT * FROM banned_ip WHERE mask = ?",
            [&](ResultSet* resultSet) {
                resultSet->next();
                id = resultSet->getInt("id");
                readBack = resultSet->getTimestamp("time_end");
            },
            [&](PreparedStatement* ps) { ps->setString(1, mask); }));
        JCHECK(id > 0);
        if (timeEnd == nullptr) JCHECK(readBack == nullptr);
        else JCHECK(readBack != nullptr && readBack->getTime() == timeEnd->getTime());
    }
    // ---- MySQL5AccountTimeDAO.updateAccountTime / getAccountTime (REPLACE, nullable timestamps)
    int64_t lastActive = INT64_C(1308650400000);
    JCHECK(DB::insertUpdate(
        "REPLACE INTO account_time (account_id, last_active, expiration_time, session_duration, accumulated_online, accumulated_rest, penalty_end) values (?,?,?,?,?,?,?)",
        [&](PreparedStatement* preparedStatement) {
            preparedStatement->setLong(1, 1);
            preparedStatement->setTimestamp(2, new Timestamp(lastActive));
            preparedStatement->setTimestamp(3, nullptr);
            preparedStatement->setLong(4, 3600000);
            preparedStatement->setLong(5, 7200000);
            preparedStatement->setLong(6, 0);
            preparedStatement->setTimestamp(7, nullptr);
            preparedStatement->execute();
        }));
    {
        PreparedStatement* st = DB::prepareStatement("SELECT * FROM account_time WHERE account_id = ?");
        JFINALLY { DB::close(st); };
        st->setLong(1, 1);
        ResultSet* rs = st->executeQuery();
        JCHECK(rs->next());
        JCHECK_EQ(rs->getTimestamp("last_active")->getTime(), lastActive);
        JCHECK_EQ(rs->getLong("session_duration"), INT64_C(3600000));
        JCHECK(rs->getTimestamp("penalty_end") == nullptr);
        JCHECK(rs->getTimestamp("expiration_time") == nullptr);
    }
    JCHECK_EQ(DatabaseFactory::dataSource->getNumActive(), 0);
    DatabaseFactory::shutdown();
}

#endif  // __has_include(<jlang/Time.h>)
