import java.util.*;
import java.text.*;
import java.sql.Timestamp;

// Reference outputs for jlang Time.h parity tests.
// Run: java -Djava.locale.providers=COMPAT -Duser.language=en -Duser.country=US TimeRef > time_ref.txt
public class TimeRef {
    static void p(String k, Object v) { System.out.println(k + "=" + v); }

    static String fields(Calendar c) {
        StringBuilder sb = new StringBuilder();
        for (int f = 0; f < Calendar.FIELD_COUNT; f++) { if (f > 0) sb.append(','); sb.append(c.get(f)); }
        return sb.toString();
    }

    static final long[] TIMES = {
        0L, 1L, -1L, 999L, 1000L, 1308650400000L, 1308650400123L, 1300000000000L, -1300000000000L,
        253402300799999L, 1711846800000L, 1711846799999L, 1729990800000L, 1729990799999L,
        951782400000L, 978307200000L, 1104537600000L, 1230681600000L, 1293753600000L, 1609459199999L,
        -62135596800000L + 86400000L * 400, 4102444800000L, 1167609600000L, 1199145600000L
    };

    static final String[] FORMATS = {
        "H:mm:ss", "yyyy-MM-dd HH-mm-ss", "yyyy-MM-dd HH:mm:ss", "dd MMM HH:mm:ss,SSS",
        "dd MMM yyyy HH:mm:ss,SSS", "yyyy-dd-MM HH:mm:ss", "EEE MMM dd HH:mm:ss zzz yyyy",
        "EEEE, MMMM d, yyyy h:mm a", "yy-M-d k K h a", "Z", "z", "D w W F", "'''quoted''' 'text' G 'o''clock'",
        "yyyyy.MMMMM.dd GGG hh:mm aaa", "S SS SSS SSSS", "E EE EEE EEEE", "M MM MMM MMMM",
        "y yy yyy yyyy", "h hh H HH k kk K KK", "yyyyMMddHHmmss", "'T'HH'h'mm", "zzzz", "ZZZZ"
    };

    public static void main(String[] a) throws Exception {
        Locale.setDefault(Locale.US);
        for (String zone : new String[] { "UTC", "Europe/Berlin", "America/New_York", "Asia/Seoul" }) {
            TimeZone.setDefault(TimeZone.getTimeZone(zone));
            String z = zone + ":";
            for (long t : TIMES) {
                String k = z + t + ":";
                Date d = new Date(t);
                p(k + "date", d);
                p(k + "datehash", d.hashCode());
                p(k + "dateget", d.getYear() + "," + d.getMonth() + "," + d.getDate() + "," + d.getDay() + ","
                    + d.getHours() + "," + d.getMinutes() + "," + d.getSeconds() + "," + d.getTimezoneOffset());
                Timestamp ts = new Timestamp(t);
                p(k + "ts", ts + " " + ts.getTime() + " " + ts.getNanos() + " " + ts.hashCode());
                Calendar c = Calendar.getInstance();
                c.setTimeInMillis(t);
                p(k + "cal", fields(c));
                p(k + "calmax", c.getActualMaximum(Calendar.DAY_OF_MONTH) + "," + c.getActualMaximum(Calendar.DAY_OF_YEAR)
                    + "," + c.getActualMaximum(Calendar.WEEK_OF_YEAR) + "," + c.getActualMaximum(Calendar.WEEK_OF_MONTH)
                    + "," + c.getActualMinimum(Calendar.DAY_OF_MONTH) + "," + c.getActualMaximum(Calendar.DAY_OF_WEEK_IN_MONTH));
                for (int i = 0; i < FORMATS.length; i++) {
                    p(k + "fmt" + i, new SimpleDateFormat(FORMATS[i]).format(d));
                }
            }

            // ---- Calendar manipulation
            Calendar c = Calendar.getInstance();
            c.setTimeInMillis(1308650400123L);
            c.set(Calendar.DAY_OF_MONTH, 35);
            p(z + "set1", c.getTimeInMillis() + " " + fields(c));
            c.set(Calendar.MONTH, 13);
            c.set(Calendar.HOUR_OF_DAY, 25);
            c.set(Calendar.MINUTE, -5);
            p(z + "set2", c.getTimeInMillis() + " " + fields(c));
            c.set(Calendar.YEAR, 2011); c.set(Calendar.MONTH, Calendar.JANUARY); c.set(Calendar.DATE, 31);
            c.add(Calendar.MONTH, 1);
            p(z + "add1", c.getTimeInMillis() + " " + fields(c));
            c.set(2012, Calendar.FEBRUARY, 29);
            c.add(Calendar.YEAR, 1);
            p(z + "add2", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.DAY_OF_MONTH, 400);
            p(z + "add3", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.HOUR, 30);
            p(z + "add4", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.MINUTE, -100000);
            p(z + "add5", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.WEEK_OF_YEAR, 3);
            p(z + "add6", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.MILLISECOND, 12345678);
            p(z + "add7", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.SECOND, 86400 * 180);
            p(z + "add8", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.DAY_OF_YEAR, -1000);
            p(z + "add9", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.HOUR_OF_DAY, 24 * 100 + 5);
            p(z + "add10", c.getTimeInMillis() + " " + fields(c));
            c.add(Calendar.DAY_OF_WEEK, 10);
            p(z + "add11", c.getTimeInMillis() + " " + fields(c));
            c.roll(Calendar.MONTH, 5);
            p(z + "roll1", c.getTimeInMillis() + " " + fields(c));
            c.roll(Calendar.DAY_OF_MONTH, 20);
            p(z + "roll2", c.getTimeInMillis() + " " + fields(c));
            c.roll(Calendar.HOUR_OF_DAY, 5);
            p(z + "roll3", c.getTimeInMillis() + " " + fields(c));
            c.roll(Calendar.MONTH, true);
            p(z + "roll4", c.getTimeInMillis() + " " + fields(c));
            c.roll(Calendar.YEAR, -3);
            p(z + "roll5", c.getTimeInMillis() + " " + fields(c));
            c.roll(Calendar.MINUTE, 75);
            p(z + "roll6", c.getTimeInMillis() + " " + fields(c));
            c.roll(Calendar.DAY_OF_YEAR, 200);
            p(z + "roll7", c.getTimeInMillis() + " " + fields(c));
            c.roll(Calendar.HOUR, -7);
            p(z + "roll8", c.getTimeInMillis() + " " + fields(c));

            Calendar g1 = new GregorianCalendar(2011, 11, 15);
            Calendar g2 = new GregorianCalendar(2012, 0, 5);
            p(z + "greg", g1.getTimeInMillis() + " " + g2.getTimeInMillis() + " " + g1.before(g2) + " " + g1.after(g2)
                + " " + g2.after(g1) + " " + g1.equals(g2) + " " + g1.equals(g1.clone()) + " " + fields(g1));
            Calendar g3 = new GregorianCalendar(2011, 5, 21, 10, 30);
            Calendar g4 = new GregorianCalendar(2011, 5, 21, 10, 30, 45);
            p(z + "greg2", g3.getTimeInMillis() + " " + g4.getTimeInMillis() + " " + g4.getTime());

            Calendar dst = Calendar.getInstance();
            dst.clear();
            dst.set(2024, Calendar.MARCH, 31, 2, 30, 0);
            p(z + "gap", dst.getTimeInMillis() + " " + fields(dst));
            dst.clear();
            dst.set(2024, Calendar.OCTOBER, 27, 2, 30, 0);
            p(z + "overlap", dst.getTimeInMillis() + " " + fields(dst));
            dst.clear();
            dst.set(2024, Calendar.NOVEMBER, 3, 1, 30, 0);
            p(z + "overlapNY", dst.getTimeInMillis() + " " + fields(dst));
            dst.clear();
            dst.set(2024, Calendar.MARCH, 10, 2, 30, 0);
            p(z + "gapNY", dst.getTimeInMillis() + " " + fields(dst));
            dst.clear();
            dst.set(Calendar.YEAR, 2010);
            p(z + "clear", dst.getTimeInMillis() + " " + fields(dst));
            dst.clear();
            p(z + "clear2", dst.getTimeInMillis());

            Calendar w = Calendar.getInstance();
            w.setTimeInMillis(1308650400123L);
            w.set(Calendar.DAY_OF_WEEK, Calendar.MONDAY);
            p(z + "dow", w.getTimeInMillis() + " " + fields(w));
            w.set(Calendar.HOUR, 3);
            w.set(Calendar.AM_PM, Calendar.PM);
            p(z + "ampm", w.getTimeInMillis() + " " + fields(w));
            w.set(Calendar.WEEK_OF_YEAR, 1);
            p(z + "woy", w.getTimeInMillis() + " " + fields(w));
            w.set(Calendar.DAY_OF_YEAR, 300);
            p(z + "doy", w.getTimeInMillis() + " " + fields(w));
            w.set(Calendar.DAY_OF_WEEK_IN_MONTH, -1);
            w.set(Calendar.DAY_OF_WEEK, Calendar.FRIDAY);
            p(z + "dowim", w.getTimeInMillis() + " " + fields(w));
            w.set(Calendar.WEEK_OF_MONTH, 2);
            p(z + "wom", w.getTimeInMillis() + " " + fields(w));
            w.setTime(new Date(1300000000000L));
            p(z + "settime", w.getTime().getTime() + " " + fields(w));
            Calendar gs = Calendar.getInstance();
            gs.setTimeInMillis(1308650400123L);
            gs.set(Calendar.YEAR, gs.get(Calendar.YEAR));
            gs.set(Calendar.MONTH, 1);
            gs.set(Calendar.DATE, 30);
            gs.set(Calendar.HOUR_OF_DAY, 7);
            gs.set(Calendar.MINUTE, 8);
            gs.set(Calendar.SECOND, 9);
            p(z + "guild", gs.getTimeInMillis() + " " + fields(gs) + " " + gs.after(c) + " " + gs.before(c));

            // ---- parse
            String[][] parses = {
                { "yyyy-MM-dd HH:mm:ss", "2011-06-21 10:00:00" },
                { "yyyy-MM-dd HH:mm:ss", "2011-13-45 25:61:61" },
                { "dd MMM yyyy", "05 Feb 2011" },
                { "dd MMM yyyy", "05 february 2011" },
                { "H:mm:ss", "7:05:09" },
                { "yyyy-MM-dd", "2011-06-21extra" },
                { "yy-MM-dd", "11-06-21" },
                { "yy-MM-dd", "95-06-21" },
                { "EEE, d MMM yyyy HH:mm:ss Z", "Tue, 21 Jun 2011 10:00:00 +0200" },
                { "EEE, d MMM yyyy HH:mm:ss z", "Tue, 21 Jun 2011 10:00:00 GMT-03:30" },
                { "EEE, d MMM yyyy HH:mm:ss z", "Tue, 21 Jun 2011 10:00:00 UTC" },
                { "yyyyMMddHHmmss", "20110621100000" },
                { "yyyy-MM-dd HH:mm:ss", "xyz" },
                { "yyyy-MM-dd HH:mm:ss", "2011-06-21" },
                { "yyyy-MM-dd h:mm a", "2011-06-21 12:15 AM" },
                { "yyyy-MM-dd h:mm a", "2011-06-21 12:15 pm" },
                { "yyyy-MM-dd HH:mm:ss.SSS", "2011-06-21 10:00:00.5" },
                { "yyyy-MM-dd'T'HH:mm", "2011-06-21T10:07" },
                { "MMM d, yyyy", "Jun 1, 2011" },
                { "d/M/y", "1/2/11" },
                { "d/M/y", "1/2/2011" },
                { "yyyy-MM-dd", " 2011-06-21" },
                { "yyyy-MM-dd", "-2011-06-21" },
            };
            for (int i = 0; i < parses.length; i++) {
                SimpleDateFormat f = new SimpleDateFormat(parses[i][0]);
                try {
                    Date d = f.parse(parses[i][1]);
                    p(z + "parse" + i, d.getTime());
                } catch (ParseException e) {
                    p(z + "parse" + i, "ParseException " + e.getMessage() + " @" + e.getErrorOffset());
                }
            }
            SimpleDateFormat strict = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
            strict.setLenient(false);
            try { p(z + "strict", strict.parse("2011-13-45 25:61:61").getTime()); }
            catch (ParseException e) { p(z + "strict", "ParseException " + e.getMessage() + " @" + e.getErrorOffset()); }
            p(z + "strict2", strict.parse("2011-12-31 23:59:59").getTime());
            SimpleDateFormat utcFmt = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss zzz");
            utcFmt.setTimeZone(TimeZone.getTimeZone("GMT+05:30"));
            p(z + "tzfmt", utcFmt.format(new Date(1308650400123L)));
            p(z + "tzpat", utcFmt.toPattern());

            // ---- Timestamp
            String[] tsv = { "2011-06-21 10:00:00", "2011-06-21 10:00:00.0", "2011-06-21 10:00:00.123456789",
                "2011-6-1 1:2:3.5", "1969-12-31 23:59:59.999", "2011-06-21 10:00:00.000001", "2011-06-21", "bad",
                "2011-06-21 10:00:00.1234567890", "2011-02-30 10:00:00" };
            for (int i = 0; i < tsv.length; i++) {
                try {
                    Timestamp ts = Timestamp.valueOf(tsv[i]);
                    p(z + "tsv" + i, ts + " " + ts.getTime() + " " + ts.getNanos());
                } catch (IllegalArgumentException e) {
                    p(z + "tsv" + i, "IAE " + e.getMessage());
                }
            }
            Timestamp t1 = new Timestamp(1308650400123L);
            Timestamp t2 = new Timestamp(1308650400123L);
            t2.setNanos(123000001);
            Date dd = new Date(1308650400123L);
            p(z + "tscmp", t1.compareTo(t2) + " " + t2.compareTo(t1) + " " + t1.compareTo(dd) + " " + t1.equals(t2) + " "
                + t1.equals(new Timestamp(1308650400123L)) + " " + t1.equals(dd) + " " + dd.equals(t1) + " "
                + t1.before(t2) + " " + t2.after(t1) + " " + dd.compareTo(t1) + " " + t2 + " " + t2.getTime()
                + " " + dd.before(t2) + " " + dd.after(t2));
            Timestamp t3 = new Timestamp(-1500L);
            p(z + "tsneg", t3 + " " + t3.getTime() + " " + t3.getNanos());
            t3.setTime(-999L);
            p(z + "tsneg2", t3 + " " + t3.getTime() + " " + t3.getNanos());
            try { t3.setNanos(1000000000); } catch (IllegalArgumentException e) { p(z + "tsnanos", "IAE " + e.getMessage()); }
            p(z + "datecmp", new Date(5).compareTo(new Date(6)) + " " + new Date(6).compareTo(new Date(5)) + " "
                + new Date(5).compareTo(new Date(5)) + " " + new Date(5).before(new Date(6)) + " " + new Date(5).after(new Date(6))
                + " " + new Date(5).equals(new Date(5)));
            TimeZone tz = TimeZone.getDefault();
            p(z + "tz", tz.getID() + " " + tz.getRawOffset() + " " + tz.getOffset(1308650400123L) + " " + tz.getOffset(1293753600000L)
                + " " + tz.inDaylightTime(new Date(1308650400123L)) + " " + tz.useDaylightTime() + " " + tz.getDSTSavings()
                + " " + tz.getDisplayName(false, TimeZone.SHORT) + " " + tz.getDisplayName(true, TimeZone.SHORT));
        }

        TimeZone.setDefault(TimeZone.getTimeZone("UTC"));
        // custom zones
        String[] ids = { "GMT", "UTC", "GMT+2", "GMT+02:00", "GMT-0530", "GMT+14", "Nonexistent/Zone", "Europe/Berlin", "CET", "EST" };
        for (String id : ids) {
            TimeZone tz = TimeZone.getTimeZone(id);
            p("zone:" + id, tz.getID() + " " + tz.getRawOffset() + " " + tz.getOffset(1308650400123L));
        }

        // ---- DecimalFormat / NumberFormat
        double[] ds = { 0, 1, -1, 0.5, 1.5, 2.5, -2.5, 0.125, 0.135, 12345.6789, 1234567.891, 99.995, 0.0001, 1e-10, 123456789012.0,
            1e15, 3.14159, 0.8055, 1024, 524288.75, -0.0, Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY, 1e20, 7.0E-5 };
        String[] pats = { " (0.0000'%')", " # 'KB'", "#,##0", "0.00", "#,##0.###", "0.#", "#.##", "000", "#,###", "#", "0",
            "00.00", "#,##0.00;(#,##0.00)", "0.00%", "'#'#", "#,##,###", "0.###E0", "00.###E0", "##0.#####E0", "#0.0#",
            "¤#,##0.00", "#,##0.0#;-#", ".00", "#.", "0000.0000" };
        for (int i = 0; i < pats.length; i++) {
            DecimalFormat df = new DecimalFormat(pats[i]);
            StringBuilder sb = new StringBuilder();
            for (double d : ds) sb.append('[').append(df.format(d)).append(']');
            p("df" + i, sb);
            p("dfl" + i, "[" + df.format(0L) + "][" + df.format(1234567L) + "][" + df.format(-98765L) + "][" + df.format(Long.MAX_VALUE) + "][" + df.format(Long.MIN_VALUE) + "]");
            p("dfp" + i, df.toPattern());
        }
        NumberFormat nf = NumberFormat.getInstance(Locale.ENGLISH);
        p("nf", nf.format(1234567L) + " " + nf.format(-1234567L) + " " + nf.format(0L) + " " + nf.format(1234.5678) + " "
            + nf.format(0.0005) + " " + nf.format(0.0004) + " " + nf.format(-0.0004) + " " + nf.format(Long.MIN_VALUE) + " " + nf.format(1e18));
        NumberFormat nf2 = NumberFormat.getInstance(Locale.ENGLISH);
        nf2.setMaximumFractionDigits(1);
        nf2.setMinimumFractionDigits(1);
        p("nf2", nf2.format(2.25) + " " + nf2.format(2.35) + " " + nf2.format(1234567) + " " + nf2.format(0.05));
        nf2.setGroupingUsed(false);
        nf2.setMinimumIntegerDigits(3);
        p("nf3", nf2.format(2.25) + " " + nf2.format(1234567) + " " + nf2.format(-5));
        NumberFormat nf4 = NumberFormat.getIntegerInstance(Locale.US);
        p("nf4", nf4.format(2.5) + " " + nf4.format(3.5) + " " + nf4.format(-2.5) + " " + nf4.format(1234567.89));
        NumberFormat nf5 = NumberFormat.getPercentInstance(Locale.US);
        p("nf5", nf5.format(0.256) + " " + nf5.format(1.5) + " " + nf5.format(-0.004));
        DecimalFormat df = new DecimalFormat("#,##0.00");
        p("dfparse", df.parse("1,234.56") + " " + df.parse("-7") + " " + df.parse("12abc") + " " + df.parse("1,234"));
        try { df.parse("abc"); } catch (ParseException e) { p("dfparseerr", e.getMessage() + " @" + e.getErrorOffset()); }
        DecimalFormat dfr = new DecimalFormat("0.00");
        dfr.setRoundingMode(java.math.RoundingMode.HALF_UP);
        p("dfhalfup", dfr.format(2.345) + " " + dfr.format(2.355) + " " + dfr.format(-2.345) + " " + dfr.format(0.125));
    }
}
