// Behavior oracle for jlang/tests/test_sql_oracle.cpp: runs a fixed JDBC scenario through
// MySQL Connector/J 5.1.13 (commons/lib/mysql-connector-java-5.1.13-bin.jar, the driver of the
// Java servers) against the local MariaDB server and prints one line per observation. The C++
// test runs the same scenario through jlang/Sql.h and must print the same lines.
//
// Regenerate jlang/tests/sql_oracle_expected.inc with jlang/tests/java/gen_sql_oracle.sh.
import java.sql.*;
import java.util.*;

public class SqlOracle {
    static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (byte x : b) sb.append(String.format("%02x", x & 0xff));
        return sb.toString();
    }
    static String ex(SQLException e) {
        return "EX[" + e.getSQLState() + "|" + e.getErrorCode() + "|" + e.getMessage() + "]";
    }
    interface G { Object get() throws SQLException; }
    static void p(String label, G g) {
        String s;
        try {
            Object o = g.get();
            if (o == null) s = "null";
            else if (o instanceof byte[]) s = "bytes:" + hex((byte[]) o);
            else if (o instanceof Timestamp) s = "ts:" + o + "@" + ((Timestamp) o).getTime();
            else s = o.toString();
        } catch (SQLException e) {
            s = ex(e);
        }
        System.out.println(label + " = " + s.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\0", "\\0"));
    }

    static final String[] COLS = {"ti", "ti1", "si", "i", "bi", "f", "d", "dc", "vc", "tx", "bl", "vb", "dt", "dt6", "ts", "dte", "tm", "yr", "en", "b1", "b8"};

    static void dumpRow(final ResultSet rs, String tag) throws SQLException {
        for (final String c : COLS) {
            String l = tag + "." + c;
            p(l + ".getString", () -> rs.getString(c));
            p(l + ".wasNull", () -> rs.wasNull());
            p(l + ".getInt", () -> rs.getInt(c));
            p(l + ".getLong", () -> rs.getLong(c));
            p(l + ".getShort", () -> rs.getShort(c));
            p(l + ".getByte", () -> rs.getByte(c));
            p(l + ".getBoolean", () -> rs.getBoolean(c));
            p(l + ".getFloat", () -> rs.getFloat(c));
            p(l + ".getDouble", () -> rs.getDouble(c));
            p(l + ".getBytes", () -> rs.getBytes(c));
            p(l + ".getTimestamp", () -> rs.getTimestamp(c));
        }
    }

    public static void main(String[] a) throws Exception {
        System.setOut(new java.io.PrintStream(new java.io.FileOutputStream(java.io.FileDescriptor.out), true, "UTF-8"));
        String extra = a.length > 0 ? a[0] : "";
        Class.forName("com.mysql.jdbc.Driver");
        String url = "jdbc:mysql://127.0.0.1:3306/jlang_test_oracle?useUnicode=true&characterEncoding=UTF-8" + extra;
        Connection c = DriverManager.getConnection(url, "aion", "aion");
        System.out.println("TZ = " + TimeZone.getDefault().getID());
        Statement st = c.createStatement();
        st.executeUpdate("DROP TABLE IF EXISTS t_types");
        st.executeUpdate("CREATE TABLE t_types (id INT AUTO_INCREMENT PRIMARY KEY, ti TINYINT, ti1 TINYINT(1), si SMALLINT, i INT, bi BIGINT,"
            + " f FLOAT, d DOUBLE, dc DECIMAL(10,3), vc VARCHAR(100), tx TEXT, bl BLOB, vb VARBINARY(20), dt DATETIME, dt6 DATETIME(6), ts TIMESTAMP NULL,"
            + " dte DATE, tm TIME, yr YEAR, en ENUM('MALE','FEMALE'), b1 BIT(1), b8 BIT(8)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
        // row 1: via setters
        PreparedStatement ps = c.prepareStatement("INSERT INTO t_types (ti,ti1,si,i,bi,f,d,dc,vc,tx,bl,vb,dt,dt6,ts,dte,tm,yr,en,b1,b8) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", Statement.RETURN_GENERATED_KEYS);
        ps.setByte(1, (byte) -5);
        ps.setBoolean(2, true);
        ps.setShort(3, (short) 1234);
        ps.setInt(4, -2000000000);
        ps.setLong(5, 9000000000000000000L);
        ps.setFloat(6, 0.1f);
        ps.setDouble(7, 1.0E-5);
        ps.setString(8, "12.5");
        ps.setString(9, "it's \"q\" \\ back\nnl 日本");
        ps.setString(10, "text");
        ps.setBytes(11, new byte[] {0, 1, (byte) 0xff, 39, 92, 10});
        ps.setBytes(12, new byte[] {});
        ps.setTimestamp(13, new Timestamp(1308650400123L)); // 2011-06-21 10:00:00.123 UTC
        ps.setTimestamp(14, Timestamp.valueOf("2011-06-21 10:00:00.5"));
        ps.setTimestamp(15, Timestamp.valueOf("2011-01-02 03:04:05"));
        ps.setString(16, "2011-02-03");
        ps.setString(17, "04:05:06");
        ps.setInt(18, 2011);
        ps.setString(19, "FEMALE");
        ps.setBoolean(20, true);
        ps.setInt(21, 200);
        p("insert1.executeUpdate", () -> ps.executeUpdate());
        final ResultSet gk = ps.getGeneratedKeys();
        p("insert1.gk.next", () -> gk.next());
        p("insert1.gk.getInt1", () -> gk.getInt(1));
        p("insert1.gk.label", () -> gk.getMetaData().getColumnLabel(1));
        p("insert1.gk.getLongLabel", () -> gk.getLong("GENERATED_KEY"));
        p("insert1.gk.next2", () -> gk.next());
        // row 2: all nulls
        for (int i = 1; i <= 21; i++) ps.setNull(i, Types.NULL);
        p("insert2.executeUpdate", () -> ps.executeUpdate());
        // row 3: edge values via plain SQL
        p("insert3", () -> st.executeUpdate("INSERT INTO t_types (ti,ti1,si,i,bi,f,d,dc,vc,tx,bl,vb,dt,dt6,ts,dte,tm,yr,en,b1,b8) VALUES "
            + "(127,2,-32768,2147483647,-9223372036854775808,-1.5e30,1e300,-0.5,'true','yes','1','-1','1999-12-31 23:59:59','1999-12-31 23:59:59.999999','2038-01-19 03:14:07','1970-01-01','-838:59:59',1901,'MALE',b'0',b'11111111')"));
        p("insert4", () -> st.executeUpdate("INSERT INTO t_types (ti,ti1,si,i,bi,f,d,dc,vc,tx,bl,vb,dt,dt6,ts,dte,tm,yr,en,b1,b8) VALUES "
            + "(-1,-1,0,0,0,0,0,0,'','  42  ','abc','0','0000-00-00 00:00:00','0000-00-00 00:00:00',NULL,'0000-00-00','00:00:00',0,NULL,NULL,b'1')"));
        p("insert5", () -> st.executeUpdate("INSERT INTO t_types (ti1,vc,tx,bl,vb) VALUES (-2,'no','1.9','9.9','x')"));
        p("insert6", () -> st.executeUpdate("INSERT INTO t_types (vc,tx,bl,vb) VALUES ('2147483648','-2147483649','1e3','T')"));
        p("insert7", () -> st.executeUpdate("INSERT INTO t_types (vc,tx,bl,vb) VALUES ('128','-129','32768','abc')"));
        ResultSet rs = st.executeQuery("SELECT * FROM t_types ORDER BY id");
        int row = 0;
        while (rs.next()) {
            row++;
            dumpRow(rs, "row" + row);
        }
        rs.close();

        // navigation
        final PreparedStatement nav = c.prepareStatement("SELECT id, vc AS label, t.i FROM t_types t ORDER BY id", ResultSet.TYPE_SCROLL_INSENSITIVE, ResultSet.CONCUR_READ_ONLY);
        final ResultSet r = nav.executeQuery();
        p("nav.getRow0", () -> r.getRow());
        p("nav.isBeforeFirst", () -> r.isBeforeFirst());
        p("nav.getIntBeforeFirst", () -> r.getInt(1));
        p("nav.last", () -> r.last());
        p("nav.getRowLast", () -> r.getRow());
        p("nav.isLast", () -> r.isLast());
        p("nav.next", () -> r.next());
        p("nav.isAfterLast", () -> r.isAfterLast());
        p("nav.getRowAfter", () -> r.getRow());
        p("nav.getIntAfter", () -> r.getInt(1));
        p("nav.first", () -> r.first());
        p("nav.getRowFirst", () -> r.getRow());
        p("nav.isFirst", () -> r.isFirst());
        p("nav.absolute3", () -> r.absolute(3));
        p("nav.absolute3.id", () -> r.getInt("ID"));
        p("nav.absolute-1", () -> r.absolute(-1));
        p("nav.absolute-1.row", () -> r.getRow());
        p("nav.absolute0", () -> r.absolute(0));
        p("nav.absolute99", () -> r.absolute(99));
        p("nav.previous", () -> r.previous());
        p("nav.previous.row", () -> r.getRow());
        p("nav.relative-2", () -> r.relative(-2));
        p("nav.relative-2.row", () -> r.getRow());
        r.beforeFirst();
        p("nav.beforeFirst.next", () -> r.next());
        p("nav.label", () -> r.getString("label"));
        p("nav.LABEL", () -> r.getString("LABEL"));
        p("nav.vc", () -> r.getString("vc"));
        p("nav.t.i", () -> r.getString("t.i"));
        p("nav.t_types.i", () -> r.getString("t_types.i"));
        p("nav.i", () -> r.getString("i"));
        p("nav.findColumn.label", () -> r.findColumn("Label"));
        p("nav.findColumn.nope", () -> r.findColumn("nope"));
        p("nav.getInt0", () -> r.getInt(0));
        p("nav.getInt4", () -> r.getInt(4));
        p("nav.md.count", () -> r.getMetaData().getColumnCount());
        p("nav.md.name2", () -> r.getMetaData().getColumnName(2));
        p("nav.md.label2", () -> r.getMetaData().getColumnLabel(2));
        p("nav.md.table3", () -> r.getMetaData().getTableName(3));
        p("nav.md.type1", () -> r.getMetaData().getColumnType(1));
        p("nav.md.typename2", () -> r.getMetaData().getColumnTypeName(2));
        r.close();
        p("nav.closed.next", () -> r.next());
        p("nav.closed.getInt", () -> r.getInt(1));

        // empty result
        final ResultSet e = st.executeQuery("SELECT * FROM t_types WHERE id < 0");
        p("empty.isBeforeFirst", () -> e.isBeforeFirst());
        p("empty.next", () -> e.next());
        p("empty.getRow", () -> e.getRow());
        p("empty.isAfterLast", () -> e.isAfterLast());
        p("empty.getInt", () -> e.getInt(1));

        // errors
        p("err.syntax", () -> st.executeQuery("SELEC 1"));
        p("err.unknownTable", () -> st.executeQuery("SELECT * FROM nope"));
        p("err.unknownColumn", () -> st.executeQuery("SELECT nope FROM t_types"));
        p("err.dup", () -> st.executeUpdate("INSERT INTO t_types (id) VALUES (1)"));
        p("err.executeQueryInsert", () -> c.prepareStatement("  insert into t_types (id) values (100)").executeQuery());
        p("err.executeQueryComment", () -> c.prepareStatement("/* x */ DELETE FROM t_types WHERE id=100").executeQuery());
        p("err.executeUpdateSelect", () -> c.prepareStatement("SELECT 1").executeUpdate());
        p("err.executeUpdateSelect2", () -> st.executeUpdate("select 1"));
        p("err.unsetParam", () -> { PreparedStatement x = c.prepareStatement("SELECT ?, ?"); x.setInt(1, 1); return x.executeQuery(); });
        p("err.paramIndexHigh", () -> { PreparedStatement x = c.prepareStatement("SELECT ?"); x.setInt(2, 1); return null; });
        p("err.paramIndexLow", () -> { PreparedStatement x = c.prepareStatement("SELECT ?"); x.setInt(0, 1); return null; });
        p("err.commitAuto", () -> { c.commit(); return null; });
        p("err.rollbackAuto", () -> { c.rollback(); return null; });
        p("err.gkNotRequested", () -> { PreparedStatement x = c.prepareStatement("INSERT INTO t_types (i) VALUES (1)"); x.executeUpdate(); return x.getGeneratedKeys(); });
        p("err.savepointNull", () -> c.setSavepoint(null));
        p("err.doubleNaN", () -> { PreparedStatement x = c.prepareStatement("SELECT ?"); x.setDouble(1, Double.NaN); return null; });
        p("err.floatNaN", () -> { PreparedStatement x = c.prepareStatement("SELECT ?"); x.setFloat(1, Float.NaN); return x.executeQuery(); });
        p("err.closedStmt", () -> { PreparedStatement x = c.prepareStatement("SELECT 1"); x.close(); x.close(); return x.executeQuery(); });
        p("err.closedStmtSet", () -> { PreparedStatement x = c.prepareStatement("SELECT ?"); x.close(); x.setInt(1, 1); return null; });
        p("err.rsAfterStmtClose", () -> { PreparedStatement x = c.prepareStatement("SELECT 1"); ResultSet y = x.executeQuery(); x.close(); return y.next(); });
        p("err.rsReexec", () -> { PreparedStatement x = c.prepareStatement("SELECT 1"); ResultSet y = x.executeQuery(); x.executeQuery(); return y.next(); });

        // parameter substitution / parser
        p("param.quotes", () -> { PreparedStatement x = c.prepareStatement("SELECT '?', \"?\", `id`, ? /* ? */, ? -- ?\n , ? # ?\n , 'a\\'?' FROM t_types WHERE id = 1"); x.setInt(1, 1); x.setString(2, "two"); x.setNull(3, Types.VARCHAR); ResultSet y = x.executeQuery(); y.next(); return y.getString(1) + "|" + y.getString(2) + "|" + y.getString(3) + "|" + y.getString(4) + "|" + y.getString(5) + "|" + y.getString(6) + "|" + y.getString(7); });
        p("param.floats", () -> { PreparedStatement x = c.prepareStatement("SELECT ?, ?, ?, ?, ?"); x.setFloat(1, 1.0e10f); x.setFloat(2, 1.0e-10f); x.setDouble(3, 123456789.0); x.setDouble(4, 1.0e-300); x.setFloat(5, 3.4028235e38f); ResultSet y = x.executeQuery(); y.next(); return y.getString(1) + "|" + y.getString(2) + "|" + y.getString(3) + "|" + y.getString(4) + "|" + y.getString(5) + "|" + y.getFloat(5) + "|" + y.getDouble(3); });
        p("param.unicode", () -> { PreparedStatement x = c.prepareStatement("SELECT ?, CHAR_LENGTH(?), ?"); x.setString(1, "éè 中文 Ж"); x.setString(2, "éè 中文 Ж"); x.setString(3, "\0\032\r"); ResultSet y = x.executeQuery(); y.next(); return y.getString(1) + "|" + y.getInt(2) + "|" + hex(y.getBytes(3)); });
        p("param.bytes", () -> { PreparedStatement x = c.prepareStatement("SELECT ?, HEX(?)"); byte[] b = new byte[256]; for (int i = 0; i < 256; i++) b[i] = (byte) i; x.setBytes(1, b); x.setBytes(2, b); ResultSet y = x.executeQuery(); y.next(); return hex(y.getBytes(1)).equals(hex(b)) + "|" + y.getString(2).length(); });
        p("param.nullString", () -> { PreparedStatement x = c.prepareStatement("SELECT ? IS NULL, ? IS NULL"); x.setString(1, null); x.setTimestamp(2, null); ResultSet y = x.executeQuery(); y.next(); return y.getInt(1) + "|" + y.getInt(2); });
        p("param.clear", () -> { PreparedStatement x = c.prepareStatement("SELECT ?"); x.setInt(1, 5); x.clearParameters(); return x.executeQuery(); });
        p("param.ts", () -> { PreparedStatement x = c.prepareStatement("SELECT CAST(? AS CHAR)"); x.setTimestamp(1, new Timestamp(1308650400999L)); ResultSet y = x.executeQuery(); y.next(); return y.getString(1); });
        p("param.expr", () -> { PreparedStatement x = c.prepareStatement("SELECT ? + ?, ? - ?"); x.setInt(1, 5); x.setInt(2, -3); x.setLong(3, 5); x.setLong(4, -3); ResultSet y = x.executeQuery(); y.next(); return y.getString(1) + "|" + y.getString(2); });

        // expressions / computed column types
        p("expr.types", () -> { ResultSet y = st.executeQuery("SELECT 1, 1.5, 'x', NOW() IS NOT NULL, NULL, COUNT(*), 1=1, CAST(1 AS UNSIGNED), 18446744073709551615"); y.next();
            ResultSetMetaData m = y.getMetaData(); StringBuilder sb = new StringBuilder(); for (int i = 1; i <= m.getColumnCount(); i++) sb.append(m.getColumnLabel(i)).append(":").append(m.getColumnType(i)).append(":").append(y.getString(i)).append(" "); return sb.toString(); });
        p("expr.bigUnsignedLong", () -> { ResultSet y = st.executeQuery("SELECT 18446744073709551615"); y.next(); return y.getLong(1); });
        p("expr.bigUnsignedDouble", () -> { ResultSet y = st.executeQuery("SELECT 18446744073709551615"); y.next(); return y.getDouble(1); });
        p("expr.bool", () -> { ResultSet y = st.executeQuery("SELECT 1=1, 1=0, 2, -1, 0.5, 1.5, 'Y', 'N', NULL"); y.next(); String s = ""; for (int i = 1; i <= 9; i++) s += y.getBoolean(i) + ","; return s; });

        // generated keys: multi row
        p("gk.multi", () -> { PreparedStatement x = c.prepareStatement("INSERT INTO t_types (i) VALUES (1),(2),(3)", Statement.RETURN_GENERATED_KEYS); int n = x.executeUpdate(); ResultSet y = x.getGeneratedKeys(); String s = n + ":"; while (y.next()) s += y.getLong(1) + ","; return s; });
        p("gk.none", () -> { PreparedStatement x = c.prepareStatement("UPDATE t_types SET i = 7 WHERE id = -1", Statement.RETURN_GENERATED_KEYS); int n = x.executeUpdate(); ResultSet y = x.getGeneratedKeys(); String s = n + ":"; while (y.next()) s += y.getLong(1) + ","; return s; });
        p("gk.execute", () -> { PreparedStatement x = c.prepareStatement("INSERT INTO t_types (i) VALUES (9)", Statement.RETURN_GENERATED_KEYS); boolean b = x.execute(); ResultSet y = x.getGeneratedKeys(); String s = b + ":" + x.getUpdateCount() + ":"; while (y.next()) s += y.getLong(1) + ","; return s; });

        // execute() results
        p("exec.select", () -> { PreparedStatement x = c.prepareStatement("SELECT 1"); boolean b = x.execute(); return b + ":" + x.getUpdateCount() + ":" + (x.getResultSet() != null); });
        p("exec.update", () -> { PreparedStatement x = c.prepareStatement("UPDATE t_types SET i = i WHERE id > 0"); boolean b = x.execute(); return b + ":" + x.getUpdateCount() + ":" + (x.getResultSet() != null); });
        p("exec.updateChanged", () -> { PreparedStatement x = c.prepareStatement("UPDATE t_types SET i = 77 WHERE id = 1"); return x.executeUpdate(); });
        p("exec.updateUnchanged", () -> { PreparedStatement x = c.prepareStatement("UPDATE t_types SET i = 77 WHERE id = 1"); return x.executeUpdate(); });
        p("exec.dupKeyUpdate", () -> { PreparedStatement x = c.prepareStatement("INSERT INTO t_types (id, i) VALUES (1, 78) ON DUPLICATE KEY UPDATE i = 78"); return x.executeUpdate(); });
        p("exec.set", () -> { PreparedStatement x = c.prepareStatement("SET @a = 1"); return x.executeQuery().next(); });

        // batch
        p("batch", () -> { PreparedStatement x = c.prepareStatement("INSERT INTO t_types (id, i) VALUES (?, ?)"); x.setInt(1, 1000); x.setInt(2, 1); x.addBatch(); x.setInt(1, 1001); x.setInt(2, 2); x.addBatch(); return Arrays.toString(x.executeBatch()); });
        p("batch.err", () -> { PreparedStatement x = c.prepareStatement("INSERT INTO t_types (id, i) VALUES (?, ?)"); x.setInt(1, 1002); x.setInt(2, 1); x.addBatch(); x.setInt(1, 1000); x.setInt(2, 2); x.addBatch(); x.setInt(1, 1003); x.setInt(2, 2); x.addBatch();
            try { return Arrays.toString(x.executeBatch()); } catch (BatchUpdateException be) { return ex(be) + Arrays.toString(be.getUpdateCounts()); } });
        p("batch.after", () -> { ResultSet y = st.executeQuery("SELECT COUNT(*) FROM t_types WHERE id IN (1002, 1003)"); y.next(); return y.getInt(1); });
        p("batch.empty", () -> Arrays.toString(c.prepareStatement("SELECT 1").executeBatch()));
        p("batch.stmt", () -> { Statement x = c.createStatement(); x.addBatch("UPDATE t_types SET i = 1 WHERE id = 1000"); x.addBatch("DELETE FROM t_types WHERE id = 1001"); return Arrays.toString(x.executeBatch()); });

        // transactions + savepoints
        c.setAutoCommit(false);
        p("tx.autocommit", () -> c.getAutoCommit());
        st.executeUpdate("INSERT INTO t_types (id, i) VALUES (2000, 1)");
        final Savepoint sp = c.setSavepoint("sp1");
        p("tx.sp.name", () -> sp.getSavepointName());
        p("tx.sp.id", () -> sp.getSavepointId());
        st.executeUpdate("INSERT INTO t_types (id, i) VALUES (2001, 1)");
        c.rollback(sp);
        p("tx.afterRollbackSp", () -> { ResultSet y = st.executeQuery("SELECT COUNT(*) FROM t_types WHERE id IN (2000, 2001)"); y.next(); return y.getInt(1); });
        c.releaseSavepoint(sp);
        p("tx.rollbackReleased", () -> { c.rollback(sp); return "ok"; });
        c.commit();
        final Savepoint sp2 = c.setSavepoint();
        p("tx.unnamed.id", () -> sp2.getSavepointId());
        st.executeUpdate("INSERT INTO t_types (id, i) VALUES (2002, 1)");
        c.rollback();
        c.setAutoCommit(true);
        p("tx.final", () -> { ResultSet y = st.executeQuery("SELECT COUNT(*) FROM t_types WHERE id IN (2000, 2001, 2002)"); y.next(); return y.getInt(1); });
        p("tx.rollbackBadSp", () -> { c.setAutoCommit(false); try { c.rollback(new Savepoint() { public int getSavepointId() { return 0; } public String getSavepointName() { return "nosuch"; } }); return "ok"; } finally { c.setAutoCommit(true); } });

        // read only
        c.setReadOnly(true);
        p("ro.isReadOnly", () -> c.isReadOnly());
        p("ro.select", () -> { ResultSet y = c.prepareStatement("SELECT 1").executeQuery(); y.next(); return y.getInt(1); });
        p("ro.executeUpdate", () -> c.prepareStatement("UPDATE t_types SET i = 1 WHERE id = -1").executeUpdate());
        p("ro.execute", () -> c.prepareStatement("UPDATE t_types SET i = 1 WHERE id = -1").execute());
        p("ro.executeSelect", () -> c.prepareStatement("SELECT 1").execute());
        c.setReadOnly(false);

        // zero date behaviour
        p("zero.string", () -> { ResultSet y = st.executeQuery("SELECT dt, dte FROM t_types WHERE id = 4"); y.next(); return y.getString(1) + "|" + y.wasNull() + "|" + y.getString(2); });
        p("zero.ts", () -> { ResultSet y = st.executeQuery("SELECT dt FROM t_types WHERE id = 4"); y.next(); return y.getTimestamp(1); });
        p("zero.obj", () -> { ResultSet y = st.executeQuery("SELECT dt FROM t_types WHERE id = 4"); y.next(); return y.getObject(1); });

        // DATETIME parsing across zones / DST
        p("dst.gap", () -> { ResultSet y = st.executeQuery("SELECT CAST('2011-03-27 02:30:00' AS DATETIME)"); y.next(); return y.getTimestamp(1); });
        p("dst.overlap", () -> { ResultSet y = st.executeQuery("SELECT CAST('2011-10-30 02:30:00' AS DATETIME)"); y.next(); return y.getTimestamp(1); });
        p("dst.set", () -> { PreparedStatement x = c.prepareStatement("SELECT CAST(? AS CHAR)"); x.setTimestamp(1, new Timestamp(1301189400000L)); ResultSet y = x.executeQuery(); y.next(); return y.getString(1); });

        // stored procedures (commons DB.call)
        st.executeUpdate("DROP PROCEDURE IF EXISTS p_twice");
        st.executeUpdate("CREATE PROCEDURE p_twice(IN x INT) BEGIN SELECT x * 2 AS doubled, 'a' AS s; SELECT x + 1 AS inc; END");
        p("call.escape", () -> { CallableStatement x = c.prepareCall("{call p_twice(?)}"); x.setInt(1, 21); ResultSet y = x.executeQuery(); y.next(); String s = y.getInt("doubled") + "|" + y.getString("s") + "|" + x.getMoreResults(); ResultSet z = x.getResultSet(); z.next(); s += "|" + z.getInt(1) + "|" + x.getMoreResults() + "|" + x.getUpdateCount() + "|" + x.getMoreResults() + "|" + x.getUpdateCount(); return s; });
        p("call.plain", () -> { CallableStatement x = c.prepareCall("CALL p_twice(?)"); x.setInt(1, 5); boolean b = x.execute(); ResultSet y = x.getResultSet(); y.next(); return b + "|" + y.getInt(1); });
        p("call.afterwards", () -> { ResultSet y = st.executeQuery("SELECT 7"); y.next(); return y.getInt(1); });
        // DAO-like
        p("dao.union", () -> { PreparedStatement x = c.prepareStatement("SELECT `id` AS `id` FROM t_types WHERE id < 3 UNION SELECT i FROM t_types WHERE id = 3", ResultSet.TYPE_SCROLL_INSENSITIVE, ResultSet.CONCUR_READ_ONLY); ResultSet y = x.executeQuery(); y.last(); int n = y.getRow(); y.beforeFirst(); String s = n + ":"; for (int i = 0; i < n; i++) { y.next(); s += y.getInt("id") + ","; } return s; });
        p("dao.count", () -> { PreparedStatement x = c.prepareStatement("SELECT count(id) as bkcount FROM t_types WHERE ? = vc AND id > ?"); x.setString(1, "no"); x.setInt(2, 0); ResultSet y = x.executeQuery(); y.next(); return y.getInt("bkcount"); });
        p("dao.stmtGetConnection", () -> { PreparedStatement x = c.prepareStatement("SELECT 1"); boolean same = x.getConnection() == c; x.close(); return same + "|" + x.isClosed(); });
        // isValid / isClosed / close idempotent
        p("conn.isValid", () -> c.isValid(1));
        p("conn.isClosed", () -> c.isClosed());
        st.executeUpdate("DROP TABLE t_types");
        c.close();
        p("conn.isClosedAfter", () -> c.isClosed());
        p("conn.isValidAfter", () -> c.isValid(1));
        p("conn.closeTwice", () -> { c.close(); return "ok"; });
        p("conn.useAfterClose", () -> c.prepareStatement("SELECT 1"));
        p("stmt.useAfterConnClose", () -> st.executeQuery("SELECT 1"));
    }
}
