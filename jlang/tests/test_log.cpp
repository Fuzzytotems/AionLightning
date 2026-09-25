// Tests for <jlang/Log.h> (log4j 1.2 emulation). Layout/date reference values come from
// log4j-1.2.16.jar and java.text.SimpleDateFormat (Java 21), see the javaref programs of the port.
#include "jtest.h"

#include <jlang/Log.h>
#include <jlang/Thread.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

using namespace jlang;
using namespace jlang::log4j;

namespace {

// {enhanced, pattern, message ("hello world" String, NULL, "42" Integer, "sb" StringBuilder), output, ignoresThrowable}
// from log4j-1.2.16.jar (javaref/LayoutRef.java), TZ=UTC, event time 1234567890123
struct LayoutRow { int enhanced; const char* pattern; const char* msg; const char* out; bool ignoresThrowable; };
#define NULL_MSG nullptr
const LayoutRow kLayoutRows[] = {
    {0, "[%p %d{yyyy-MM-dd HH-mm-ss}] %c - %m%n", "hello world", "[INFO 2009-02-13 23-31-30] org.openaion.gameserver.services.ItemService - hello world\n", true},
    {0, "[%p %d{yyyy-MM-dd HH-mm-ss}] %c - %m%n", NULL_MSG, "[INFO 2009-02-13 23-31-30] org.openaion.gameserver.services.ItemService - \n", true},
    {0, "[%p %d{yyyy-MM-dd HH-mm-ss}] %c - %m%n", "42", "[INFO 2009-02-13 23-31-30] org.openaion.gameserver.services.ItemService - 42\n", true},
    {0, "[%p %d{yyyy-MM-dd HH-mm-ss}] %c - %m%n", "sb", "[INFO 2009-02-13 23-31-30] org.openaion.gameserver.services.ItemService - sb\n", true},
    {0, "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n", "hello world", "[INFO] 2009-02-13 23:31:30 - hello world\n", true},
    {0, "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n", NULL_MSG, "[INFO] 2009-02-13 23:31:30 - \n", true},
    {0, "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n", "42", "[INFO] 2009-02-13 23:31:30 - 42\n", true},
    {0, "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n", "sb", "[INFO] 2009-02-13 23:31:30 - sb\n", true},
    {0, "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n", "hello world", "INFO [13 Feb 23:31:30,123] hello world\n", true},
    {0, "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n", NULL_MSG, "INFO [13 Feb 23:31:30,123] \n", true},
    {0, "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n", "42", "INFO [13 Feb 23:31:30,123] 42\n", true},
    {0, "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n", "sb", "INFO [13 Feb 23:31:30,123] sb\n", true},
    {0, "%p [%d{dd MMM yyyy HH:mm:ss,SSS}] %c %m%n", "hello world", "INFO [13 Feb 2009 23:31:30,123] org.openaion.gameserver.services.ItemService hello world\n", true},
    {0, "%p [%d{dd MMM yyyy HH:mm:ss,SSS}] %c %m%n", NULL_MSG, "INFO [13 Feb 2009 23:31:30,123] org.openaion.gameserver.services.ItemService \n", true},
    {0, "%p [%d{dd MMM yyyy HH:mm:ss,SSS}] %c %m%n", "42", "INFO [13 Feb 2009 23:31:30,123] org.openaion.gameserver.services.ItemService 42\n", true},
    {0, "%p [%d{dd MMM yyyy HH:mm:ss,SSS}] %c %m%n", "sb", "INFO [13 Feb 2009 23:31:30,123] org.openaion.gameserver.services.ItemService sb\n", true},
    {0, "[%d{yyyy-dd-MM HH:mm:ss}] %m%n", "hello world", "[2009-13-02 23:31:30] hello world\n", true},
    {0, "[%d{yyyy-dd-MM HH:mm:ss}] %m%n", NULL_MSG, "[2009-13-02 23:31:30] \n", true},
    {0, "[%d{yyyy-dd-MM HH:mm:ss}] %m%n", "42", "[2009-13-02 23:31:30] 42\n", true},
    {0, "[%d{yyyy-dd-MM HH:mm:ss}] %m%n", "sb", "[2009-13-02 23:31:30] sb\n", true},
    {0, "%-5p|%5p|%.2p|%-7.3p|%c{1}|%c{2}|%c{10}|%.10c|%20c|%-20c|", "hello world", "INFO | INFO|FO|NFO|ItemService|services.ItemService|org.openaion.gameserver.services.ItemService|temService|org.openaion.gameserver.services.ItemService|org.openaion.gameserver.services.ItemService|", true},
    {0, "%-5p|%5p|%.2p|%-7.3p|%c{1}|%c{2}|%c{10}|%.10c|%20c|%-20c|", NULL_MSG, "INFO | INFO|FO|NFO|ItemService|services.ItemService|org.openaion.gameserver.services.ItemService|temService|org.openaion.gameserver.services.ItemService|org.openaion.gameserver.services.ItemService|", true},
    {0, "%-5p|%5p|%.2p|%-7.3p|%c{1}|%c{2}|%c{10}|%.10c|%20c|%-20c|", "42", "INFO | INFO|FO|NFO|ItemService|services.ItemService|org.openaion.gameserver.services.ItemService|temService|org.openaion.gameserver.services.ItemService|org.openaion.gameserver.services.ItemService|", true},
    {0, "%-5p|%5p|%.2p|%-7.3p|%c{1}|%c{2}|%c{10}|%.10c|%20c|%-20c|", "sb", "INFO | INFO|FO|NFO|ItemService|services.ItemService|org.openaion.gameserver.services.ItemService|temService|org.openaion.gameserver.services.ItemService|org.openaion.gameserver.services.ItemService|", true},
    {0, "%d|%d{ISO8601}|%d{ABSOLUTE}|%d{DATE}|%d{HH:mm:ss.S}|%d{yy/M/d h:mm a}|%d{EEE EEEE MMM MMMM}|%d{D w W F u k K z Z}|%d{'quoted''s' yyyy}", "hello world", "2009-02-13 23:31:30,123|2009-02-13 23:31:30,123|23:31:30,123|13 Feb 2009 23:31:30,123|23:31:30.123|09/2/13 11:31 PM|Fri Friday Feb February|44 7 2 2 5 23 11 UTC +0000|quoted's 2009", true},
    {0, "%d|%d{ISO8601}|%d{ABSOLUTE}|%d{DATE}|%d{HH:mm:ss.S}|%d{yy/M/d h:mm a}|%d{EEE EEEE MMM MMMM}|%d{D w W F u k K z Z}|%d{'quoted''s' yyyy}", NULL_MSG, "2009-02-13 23:31:30,123|2009-02-13 23:31:30,123|23:31:30,123|13 Feb 2009 23:31:30,123|23:31:30.123|09/2/13 11:31 PM|Fri Friday Feb February|44 7 2 2 5 23 11 UTC +0000|quoted's 2009", true},
    {0, "%d|%d{ISO8601}|%d{ABSOLUTE}|%d{DATE}|%d{HH:mm:ss.S}|%d{yy/M/d h:mm a}|%d{EEE EEEE MMM MMMM}|%d{D w W F u k K z Z}|%d{'quoted''s' yyyy}", "42", "2009-02-13 23:31:30,123|2009-02-13 23:31:30,123|23:31:30,123|13 Feb 2009 23:31:30,123|23:31:30.123|09/2/13 11:31 PM|Fri Friday Feb February|44 7 2 2 5 23 11 UTC +0000|quoted's 2009", true},
    {0, "%d|%d{ISO8601}|%d{ABSOLUTE}|%d{DATE}|%d{HH:mm:ss.S}|%d{yy/M/d h:mm a}|%d{EEE EEEE MMM MMMM}|%d{D w W F u k K z Z}|%d{'quoted''s' yyyy}", "sb", "2009-02-13 23:31:30,123|2009-02-13 23:31:30,123|23:31:30,123|13 Feb 2009 23:31:30,123|23:31:30.123|09/2/13 11:31 PM|Fri Friday Feb February|44 7 2 2 5 23 11 UTC +0000|quoted's 2009", true},
    {0, "%m|%x|%%|%n", "hello world", "hello world||%|\n", true},
    {0, "%m|%x|%%|%n", NULL_MSG, "||%|\n", true},
    {0, "%m|%x|%%|%n", "42", "42||%|\n", true},
    {0, "%m|%x|%%|%n", "sb", "sb||%|\n", true},
    {0, "%c{1}abc %mxyz", "hello world", "ItemServiceabc hello worldxyz", true},
    {0, "%c{1}abc %mxyz", NULL_MSG, "ItemServiceabc xyz", true},
    {0, "%c{1}abc %mxyz", "42", "ItemServiceabc 42xyz", true},
    {0, "%c{1}abc %mxyz", "sb", "ItemServiceabc sbxyz", true},
    {0, "%-10m|%10m|%.3m", "hello world", "hello world|hello world|rld", true},
    {0, "%-10m|%10m|%.3m", NULL_MSG, "          |          |", true},
    {0, "%-10m|%10m|%.3m", "42", "42        |        42|42", true},
    {0, "%-10m|%10m|%.3m", "sb", "sb        |        sb|sb", true},
    {0, "%d{G yyy yyyyy y yy}", "hello world", "AD 2009 02009 2009 09", true},
    {0, "%d{G yyy yyyyy y yy}", NULL_MSG, "AD 2009 02009 2009 09", true},
    {0, "%d{G yyy yyyyy y yy}", "42", "AD 2009 02009 2009 09", true},
    {0, "%d{G yyy yyyyy y yy}", "sb", "AD 2009 02009 2009 09", true},
    {0, "%d{S SS SSS SSSS}", "hello world", "123 123 123 0123", true},
    {0, "%d{S SS SSS SSSS}", NULL_MSG, "123 123 123 0123", true},
    {0, "%d{S SS SSS SSSS}", "42", "123 123 123 0123", true},
    {0, "%d{S SS SSS SSSS}", "sb", "123 123 123 0123", true},
    {0, "%q unknown", "hello world", "%q unknown", true},
    {0, "%q unknown", NULL_MSG, "%q unknown", true},
    {0, "%q unknown", "42", "%q unknown", true},
    {0, "%q unknown", "sb", "%q unknown", true},
    {0, "%logger %level %message %date{yyyy} %c{-1}", "hello world", "ogger evel hello worldessage 2009-02-13 23:31:30,123ate{yyyy} org.openaion.gameserver.services.ItemService", true},
    {0, "%logger %level %message %date{yyyy} %c{-1}", NULL_MSG, "ogger evel essage 2009-02-13 23:31:30,123ate{yyyy} org.openaion.gameserver.services.ItemService", true},
    {0, "%logger %level %message %date{yyyy} %c{-1}", "42", "ogger evel 42essage 2009-02-13 23:31:30,123ate{yyyy} org.openaion.gameserver.services.ItemService", true},
    {0, "%logger %level %message %date{yyyy} %c{-1}", "sb", "ogger evel sbessage 2009-02-13 23:31:30,123ate{yyyy} org.openaion.gameserver.services.ItemService", true},
    {0, "%throwable", "hello world", "mainhrowable", true},
    {0, "%throwable", NULL_MSG, "mainhrowable", true},
    {0, "%throwable", "42", "mainhrowable", true},
    {0, "%throwable", "sb", "mainhrowable", true},
    {1, "[%p %d{yyyy-MM-dd HH-mm-ss}] %c - %m%n", "hello world", "[INFO 2009-02-13 23-31-30] org.openaion.gameserver.services.ItemService - hello world\n", true},
    {1, "[%p %d{yyyy-MM-dd HH-mm-ss}] %c - %m%n", NULL_MSG, "[INFO 2009-02-13 23-31-30] org.openaion.gameserver.services.ItemService - null\n", true},
    {1, "[%p %d{yyyy-MM-dd HH-mm-ss}] %c - %m%n", "42", "[INFO 2009-02-13 23-31-30] org.openaion.gameserver.services.ItemService - 42\n", true},
    {1, "[%p %d{yyyy-MM-dd HH-mm-ss}] %c - %m%n", "sb", "[INFO 2009-02-13 23-31-30] org.openaion.gameserver.services.ItemService - sb\n", true},
    {1, "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n", "hello world", "[INFO] 2009-02-13 23:31:30 - hello world\n", true},
    {1, "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n", NULL_MSG, "[INFO] 2009-02-13 23:31:30 - null\n", true},
    {1, "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n", "42", "[INFO] 2009-02-13 23:31:30 - 42\n", true},
    {1, "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n", "sb", "[INFO] 2009-02-13 23:31:30 - sb\n", true},
    {1, "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n", "hello world", "INFO [13 Feb 23:31:30,123] hello world\n", true},
    {1, "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n", NULL_MSG, "INFO [13 Feb 23:31:30,123] null\n", true},
    {1, "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n", "42", "INFO [13 Feb 23:31:30,123] 42\n", true},
    {1, "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n", "sb", "INFO [13 Feb 23:31:30,123] sb\n", true},
    {1, "%p [%d{dd MMM yyyy HH:mm:ss,SSS}] %c %m%n", "hello world", "INFO [13 Feb 2009 23:31:30,123] org.openaion.gameserver.services.ItemService hello world\n", true},
    {1, "%p [%d{dd MMM yyyy HH:mm:ss,SSS}] %c %m%n", NULL_MSG, "INFO [13 Feb 2009 23:31:30,123] org.openaion.gameserver.services.ItemService null\n", true},
    {1, "%p [%d{dd MMM yyyy HH:mm:ss,SSS}] %c %m%n", "42", "INFO [13 Feb 2009 23:31:30,123] org.openaion.gameserver.services.ItemService 42\n", true},
    {1, "%p [%d{dd MMM yyyy HH:mm:ss,SSS}] %c %m%n", "sb", "INFO [13 Feb 2009 23:31:30,123] org.openaion.gameserver.services.ItemService sb\n", true},
    {1, "[%d{yyyy-dd-MM HH:mm:ss}] %m%n", "hello world", "[2009-13-02 23:31:30] hello world\n", true},
    {1, "[%d{yyyy-dd-MM HH:mm:ss}] %m%n", NULL_MSG, "[2009-13-02 23:31:30] null\n", true},
    {1, "[%d{yyyy-dd-MM HH:mm:ss}] %m%n", "42", "[2009-13-02 23:31:30] 42\n", true},
    {1, "[%d{yyyy-dd-MM HH:mm:ss}] %m%n", "sb", "[2009-13-02 23:31:30] sb\n", true},
    {1, "%-5p|%5p|%.2p|%-7.3p|%c{1}|%c{2}|%c{10}|%.10c|%20c|%-20c|", "hello world", "INFO | INFO|FO|NFO|ItemService|services.ItemService|org.openaion.gameserver.services.ItemService|temService|org.openaion.gameserver.services.ItemService|org.openaion.gameserver.services.ItemService|", true},
    {1, "%-5p|%5p|%.2p|%-7.3p|%c{1}|%c{2}|%c{10}|%.10c|%20c|%-20c|", NULL_MSG, "INFO | INFO|FO|NFO|ItemService|services.ItemService|org.openaion.gameserver.services.ItemService|temService|org.openaion.gameserver.services.ItemService|org.openaion.gameserver.services.ItemService|", true},
    {1, "%-5p|%5p|%.2p|%-7.3p|%c{1}|%c{2}|%c{10}|%.10c|%20c|%-20c|", "42", "INFO | INFO|FO|NFO|ItemService|services.ItemService|org.openaion.gameserver.services.ItemService|temService|org.openaion.gameserver.services.ItemService|org.openaion.gameserver.services.ItemService|", true},
    {1, "%-5p|%5p|%.2p|%-7.3p|%c{1}|%c{2}|%c{10}|%.10c|%20c|%-20c|", "sb", "INFO | INFO|FO|NFO|ItemService|services.ItemService|org.openaion.gameserver.services.ItemService|temService|org.openaion.gameserver.services.ItemService|org.openaion.gameserver.services.ItemService|", true},
    {1, "%d|%d{ISO8601}|%d{ABSOLUTE}|%d{DATE}|%d{HH:mm:ss.S}|%d{yy/M/d h:mm a}|%d{EEE EEEE MMM MMMM}|%d{D w W F u k K z Z}|%d{'quoted''s' yyyy}", "hello world", "2009-02-13 23:31:30,123|2009-02-13 23:31:30,123|23:31:30,123|13 Feb 2009 23:31:30,123|23:31:30.123|09/2/13 11:31 PM|Fri Friday Feb February|44 7 2 2 5 23 11 UTC +0000|quoted's 2009", true},
    {1, "%d|%d{ISO8601}|%d{ABSOLUTE}|%d{DATE}|%d{HH:mm:ss.S}|%d{yy/M/d h:mm a}|%d{EEE EEEE MMM MMMM}|%d{D w W F u k K z Z}|%d{'quoted''s' yyyy}", NULL_MSG, "2009-02-13 23:31:30,123|2009-02-13 23:31:30,123|23:31:30,123|13 Feb 2009 23:31:30,123|23:31:30.123|09/2/13 11:31 PM|Fri Friday Feb February|44 7 2 2 5 23 11 UTC +0000|quoted's 2009", true},
    {1, "%d|%d{ISO8601}|%d{ABSOLUTE}|%d{DATE}|%d{HH:mm:ss.S}|%d{yy/M/d h:mm a}|%d{EEE EEEE MMM MMMM}|%d{D w W F u k K z Z}|%d{'quoted''s' yyyy}", "42", "2009-02-13 23:31:30,123|2009-02-13 23:31:30,123|23:31:30,123|13 Feb 2009 23:31:30,123|23:31:30.123|09/2/13 11:31 PM|Fri Friday Feb February|44 7 2 2 5 23 11 UTC +0000|quoted's 2009", true},
    {1, "%d|%d{ISO8601}|%d{ABSOLUTE}|%d{DATE}|%d{HH:mm:ss.S}|%d{yy/M/d h:mm a}|%d{EEE EEEE MMM MMMM}|%d{D w W F u k K z Z}|%d{'quoted''s' yyyy}", "sb", "2009-02-13 23:31:30,123|2009-02-13 23:31:30,123|23:31:30,123|13 Feb 2009 23:31:30,123|23:31:30.123|09/2/13 11:31 PM|Fri Friday Feb February|44 7 2 2 5 23 11 UTC +0000|quoted's 2009", true},
    {1, "%m|%x|%%|%n", "hello world", "hello world|null|%|\n", true},
    {1, "%m|%x|%%|%n", NULL_MSG, "null|null|%|\n", true},
    {1, "%m|%x|%%|%n", "42", "42|null|%|\n", true},
    {1, "%m|%x|%%|%n", "sb", "sb|null|%|\n", true},
    {1, "%c{1}abc %mxyz", "hello world", "ItemServiceabc hello worldxyz", true},
    {1, "%c{1}abc %mxyz", NULL_MSG, "ItemServiceabc nullxyz", true},
    {1, "%c{1}abc %mxyz", "42", "ItemServiceabc 42xyz", true},
    {1, "%c{1}abc %mxyz", "sb", "ItemServiceabc sbxyz", true},
    {1, "%-10m|%10m|%.3m", "hello world", "hello world|hello world|rld", true},
    {1, "%-10m|%10m|%.3m", NULL_MSG, "null      |      null|ull", true},
    {1, "%-10m|%10m|%.3m", "42", "42        |        42|42", true},
    {1, "%-10m|%10m|%.3m", "sb", "sb        |        sb|sb", true},
    {1, "%d{G yyy yyyyy y yy}", "hello world", "AD 2009 02009 2009 09", true},
    {1, "%d{G yyy yyyyy y yy}", NULL_MSG, "AD 2009 02009 2009 09", true},
    {1, "%d{G yyy yyyyy y yy}", "42", "AD 2009 02009 2009 09", true},
    {1, "%d{G yyy yyyyy y yy}", "sb", "AD 2009 02009 2009 09", true},
    {1, "%d{S SS SSS SSSS}", "hello world", "123 123 123 0123", true},
    {1, "%d{S SS SSS SSSS}", NULL_MSG, "123 123 123 0123", true},
    {1, "%d{S SS SSS SSSS}", "42", "123 123 123 0123", true},
    {1, "%d{S SS SSS SSSS}", "sb", "123 123 123 0123", true},
    {1, "%q unknown", "hello world", "%q unknown", true},
    {1, "%q unknown", NULL_MSG, "%q unknown", true},
    {1, "%q unknown", "42", "%q unknown", true},
    {1, "%q unknown", "sb", "%q unknown", true},
    {1, "%logger %level %message %date{yyyy} %c{-1}", "hello world", "org.openaion.gameserver.services.ItemService INFO hello world 2009 openaion.gameserver.services.ItemService", true},
    {1, "%logger %level %message %date{yyyy} %c{-1}", NULL_MSG, "org.openaion.gameserver.services.ItemService INFO null 2009 openaion.gameserver.services.ItemService", true},
    {1, "%logger %level %message %date{yyyy} %c{-1}", "42", "org.openaion.gameserver.services.ItemService INFO 42 2009 openaion.gameserver.services.ItemService", true},
    {1, "%logger %level %message %date{yyyy} %c{-1}", "sb", "org.openaion.gameserver.services.ItemService INFO sb 2009 openaion.gameserver.services.ItemService", true},
    {1, "%throwable", "hello world", "", false},
    {1, "%throwable", NULL_MSG, "", false},
    {1, "%throwable", "42", "", false},
    {1, "%throwable", "sb", "", false},
};
// java.text.SimpleDateFormat("yyyy-MM-dd EEE w W D F u Y h a k K"), Locale.US, UTC (javaref/DateRef.java)
struct DateRow { int64_t millis; const char* text; };
const DateRow kDateRows[] = {
    {INT64_C(-298492991), "1969-12-28 Sun 1 5 362 4 7 1970 1 PM 13 1"},
    {INT64_C(-39292991), "1969-12-31 Wed 1 5 365 5 3 1970 1 PM 13 1"},
    {INT64_C(219907009), "1970-01-03 Sat 1 1 3 1 6 1970 1 PM 13 1"},
    {INT64_C(479107009), "1970-01-06 Tue 2 2 6 1 2 1970 1 PM 13 1"},
    {INT64_C(738307009), "1970-01-09 Fri 2 2 9 2 5 1970 1 PM 13 1"},
    {INT64_C(946386307009), "1999-12-28 Tue 1 5 362 4 2 2000 1 PM 13 1"},
    {INT64_C(946645507009), "1999-12-31 Fri 1 5 365 5 5 2000 1 PM 13 1"},
    {INT64_C(946904707009), "2000-01-03 Mon 2 2 3 1 1 2000 1 PM 13 1"},
    {INT64_C(947163907009), "2000-01-06 Thu 2 2 6 1 4 2000 1 PM 13 1"},
    {INT64_C(947423107009), "2000-01-09 Sun 3 3 9 2 7 2000 1 PM 13 1"},
    {INT64_C(1230469507009), "2008-12-28 Sun 1 5 363 4 7 2009 1 PM 13 1"},
    {INT64_C(1230728707009), "2008-12-31 Wed 1 5 366 5 3 2009 1 PM 13 1"},
    {INT64_C(1230987907009), "2009-01-03 Sat 1 1 3 1 6 2009 1 PM 13 1"},
    {INT64_C(1231247107009), "2009-01-06 Tue 2 2 6 1 2 2009 1 PM 13 1"},
    {INT64_C(1231506307009), "2009-01-09 Fri 2 2 9 2 5 2009 1 PM 13 1"},
    {INT64_C(1293541507009), "2010-12-28 Tue 1 5 362 4 2 2011 1 PM 13 1"},
    {INT64_C(1293800707009), "2010-12-31 Fri 1 5 365 5 5 2011 1 PM 13 1"},
    {INT64_C(1294059907009), "2011-01-03 Mon 2 2 3 1 1 2011 1 PM 13 1"},
    {INT64_C(1294319107009), "2011-01-06 Thu 2 2 6 1 4 2011 1 PM 13 1"},
    {INT64_C(1294578307009), "2011-01-09 Sun 3 3 9 2 7 2011 1 PM 13 1"},
    {INT64_C(1356699907009), "2012-12-28 Fri 52 5 363 4 5 2012 1 PM 13 1"},
    {INT64_C(1356959107009), "2012-12-31 Mon 1 6 366 5 1 2013 1 PM 13 1"},
    {INT64_C(1357218307009), "2013-01-03 Thu 1 1 3 1 4 2013 1 PM 13 1"},
    {INT64_C(1357477507009), "2013-01-06 Sun 2 2 6 1 7 2013 1 PM 13 1"},
    {INT64_C(1357736707009), "2013-01-09 Wed 2 2 9 2 3 2013 1 PM 13 1"},
    {INT64_C(1419685507009), "2014-12-27 Sat 52 4 361 4 6 2014 1 PM 13 1"},
    {INT64_C(1419944707009), "2014-12-30 Tue 1 5 364 5 2 2015 1 PM 13 1"},
    {INT64_C(1420203907009), "2015-01-02 Fri 1 1 2 1 5 2015 1 PM 13 1"},
    {INT64_C(1420463107009), "2015-01-05 Mon 2 2 5 1 1 2015 1 PM 13 1"},
    {INT64_C(1420722307009), "2015-01-08 Thu 2 2 8 2 4 2015 1 PM 13 1"},
    {INT64_C(1451307907009), "2015-12-28 Mon 1 5 362 4 1 2016 1 PM 13 1"},
    {INT64_C(1451567107009), "2015-12-31 Thu 1 5 365 5 4 2016 1 PM 13 1"},
    {INT64_C(1451826307009), "2016-01-03 Sun 2 2 3 1 7 2016 1 PM 13 1"},
    {INT64_C(1452085507009), "2016-01-06 Wed 2 2 6 1 3 2016 1 PM 13 1"},
    {INT64_C(1452344707009), "2016-01-09 Sat 2 2 9 2 6 2016 1 PM 13 1"},
    {INT64_C(1609160707009), "2020-12-28 Mon 1 5 363 4 1 2021 1 PM 13 1"},
    {INT64_C(1609419907009), "2020-12-31 Thu 1 5 366 5 4 2021 1 PM 13 1"},
    {INT64_C(1609679107009), "2021-01-03 Sun 2 2 3 1 7 2021 1 PM 13 1"},
    {INT64_C(1609938307009), "2021-01-06 Wed 2 2 6 1 3 2021 1 PM 13 1"},
    {INT64_C(1610197507009), "2021-01-09 Sat 2 2 9 2 6 2021 1 PM 13 1"},
    {INT64_C(1703768707009), "2023-12-28 Thu 52 5 362 4 4 2023 1 PM 13 1"},
    {INT64_C(1704027907009), "2023-12-31 Sun 1 6 365 5 7 2024 1 PM 13 1"},
    {INT64_C(1704287107009), "2024-01-03 Wed 1 1 3 1 3 2024 1 PM 13 1"},
    {INT64_C(1704546307009), "2024-01-06 Sat 1 1 6 1 6 2024 1 PM 13 1"},
    {INT64_C(1704805507009), "2024-01-09 Tue 2 2 9 2 2 2024 1 PM 13 1"},
    {INT64_C(-34858492991), "1968-11-23 Sat 47 4 328 4 6 1968 1 PM 13 1"},
    {INT64_C(-34599292991), "1968-11-26 Tue 48 5 331 4 2 1968 1 PM 13 1"},
    {INT64_C(-34340092991), "1968-11-29 Fri 48 5 334 5 5 1968 1 PM 13 1"},
    {INT64_C(-34080892991), "1968-12-02 Mon 49 1 337 1 1 1968 1 PM 13 1"},
    {INT64_C(-33821692991), "1968-12-05 Thu 49 1 340 1 4 1968 1 PM 13 1"},
};


// Collects formatted events (appended under the appender's monitor).
class CaptureAppender : public AppenderSkeleton {
public:
    std::vector<String> lines;
    std::vector<LoggingEvent*> events;
    void append(LoggingEvent* e) override {
        events.push_back(e);
        lines.push_back(layout != nullptr ? layout->format(e) : e->getRenderedMessage());
    }
    void close() override { closed = true; }
    bool requiresLayout() override { return true; }
};

// Captures LogLog output while alive.
struct LogLogCapture {
    std::vector<String>* lines = new std::vector<String>();
    std::mutex* mu = new std::mutex();
    LogLogCapture() {
        auto* l = lines;
        auto* m = mu;
        LogLog::setSink([l, m](const String& s, bool) {
            std::lock_guard<std::mutex> g(*m);
            l->push_back(s);
        });
    }
    ~LogLogCapture() { LogLog::setSink(nullptr); }
    int count(const std::string& sub) const {
        int n = 0;
        for (const String& s : *lines)
            if (std::string(s).find(sub) != std::string::npos) n++;
        return n;
    }
    std::string all() const {
        std::string r;
        for (const String& s : *lines) r += std::string(s) + "\n";
        return r;
    }
};

// A PrintStream that keeps what is written (System.out replacement).
class CapturePrintStream : public PrintStream {
public:
    std::string text() {
        std::lock_guard<std::mutex> g(mu_);
        return buf_;
    }
    void flush() override {}

protected:
    void writeBytes(const char* data, size_t n) override {
        std::lock_guard<std::mutex> g(mu_);
        buf_.append(data, n);
    }

private:
    std::mutex mu_;
    std::string buf_;
};

struct TzScope {
    std::string old;
    bool had;
    explicit TzScope(const char* tz) {
        const char* o = std::getenv("TZ");
        had = o != nullptr;
        if (had) old = o;
        setenv("TZ", tz, 1);
        tzset();
    }
    ~TzScope() {
        if (had) setenv("TZ", old.c_str(), 1);
        else unsetenv("TZ");
        tzset();
    }
};

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool exists(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0;
}

// A fresh directory under /tmp, removed when the test binary exits.
std::string makeTempDir() {
    static std::vector<std::string>* dirs = [] {
        auto* v = new std::vector<std::string>();
        std::atexit([] {
            std::error_code ec;
            for (const std::string& d : *dirs) std::filesystem::remove_all(d, ec);
        });
        return v;
    }();
    char tmpl[] = "/tmp/jlang_logtest_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (d == nullptr) return std::string("/tmp");
    dirs->push_back(d);
    return std::string(d);
}

// A logger that only writes to `app` (no root appenders).
Logger* isolatedLogger(const char* name, Appender* app, Level* level = nullptr) {
    Logger* l = Logger::getLogger(String(name));
    l->removeAllAppenders();
    l->setAdditivity(false);
    l->setLevel(level != nullptr ? level : Level::DEBUG);
    l->addAppender(app);
    return l;
}

// Restores the root logger to the unconfigured jlang default (INFO to System.out).
void restoreRoot() {
    Logger* root = Logger::getRootLogger();
    root->removeAllAppenders();
    root->setLevel(Level::INFO);
    root->addAppender(new ConsoleAppender(new EnhancedPatternLayout(String("[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n"))));
    LogManager::setThreshold(Level::ALL);
}

// ---- static initialization: loggers are usable before main() and before any configuration
CaptureAppender* g_staticCapture = nullptr;
bool g_staticRootDefaults = false;
jlang::Logger* g_staticLog = [] {
    jlang::Logger* l = jlang::Logger::getLogger("jtest.log.StaticInit");
    g_staticRootDefaults = Logger::getRootLogger()->getLevel() == Level::INFO &&
                           Logger::getRootLogger()->getAllAppenders().size() == 1;
    g_staticCapture = new CaptureAppender();
    g_staticCapture->setLayout(new PatternLayout(String("%p %c %m")));
    l->addAppender(g_staticCapture);  // additive: also goes to the default console appender
    l->info("logged during static initialization");
    l->debug("not logged: the default root level is INFO");
    return l;
}();

}  // namespace

// =======================================================================================

JTEST(Log_StaticInitialization) {
    JCHECK(g_staticRootDefaults);
    JCHECK(g_staticLog == Logger::getLogger(String("jtest.log.StaticInit")));
    JCHECK_EQ(g_staticCapture->lines.size(), static_cast<size_t>(1));
    if (!g_staticCapture->lines.empty())
        JCHECK_EQ(g_staticCapture->lines[0], String("INFO jtest.log.StaticInit logged during static initialization"));
    JCHECK(!g_staticLog->isDebugEnabled());
    JCHECK(g_staticLog->isInfoEnabled());
}

JTEST(Log_Levels) {
    JCHECK_EQ(Level::ALL->toInt(), INT32_MIN);
    JCHECK_EQ(Level::TRACE->toInt(), 5000);
    JCHECK_EQ(Level::DEBUG->toInt(), 10000);
    JCHECK_EQ(Level::INFO->toInt(), 20000);
    JCHECK_EQ(Level::WARN->toInt(), 30000);
    JCHECK_EQ(Level::ERROR->toInt(), 40000);
    JCHECK_EQ(Level::FATAL->toInt(), 50000);
    JCHECK_EQ(Level::OFF->toInt(), INT32_MAX);
    JCHECK_EQ(Level::INFO->getSyslogEquivalent(), 6);
    JCHECK_EQ(Level::WARN->getSyslogEquivalent(), 4);
    JCHECK_EQ(Level::ERROR->getSyslogEquivalent(), 3);
    JCHECK_EQ(Level::FATAL->getSyslogEquivalent(), 0);
    JCHECK_EQ(Level::ALL->getSyslogEquivalent(), 7);
    JCHECK_EQ(Level::ERROR->toString(), String("ERROR"));
    JCHECK(Level::toLevel(String("info")) == Level::INFO);
    JCHECK(Level::toLevel(String("Warn")) == Level::WARN);
    JCHECK(Level::toLevel(String("off")) == Level::OFF);
    JCHECK(Level::toLevel(String("all")) == Level::ALL);
    JCHECK(Level::toLevel(String("trace")) == Level::TRACE);
    JCHECK(Level::toLevel(String("bogus")) == Level::DEBUG);
    JCHECK(Level::toLevel(String(" info")) == Level::DEBUG);  // not trimmed, like Java
    JCHECK(Level::toLevel(String("bogus"), Level::ERROR) == Level::ERROR);
    JCHECK(Level::toLevel(String(nullptr), Level::ERROR) == Level::ERROR);
    JCHECK(Level::toLevel(20000) == Level::INFO);
    JCHECK(Level::toLevel(12345) == Level::DEBUG);
    JCHECK(Level::toLevel(12345, Level::WARN) == Level::WARN);
    JCHECK(Level::WARN->isGreaterOrEqual(Level::INFO));
    JCHECK(!Level::DEBUG->isGreaterOrEqual(Level::INFO));
    JCHECK(Level::INFO->equals(new Level(20000, String("X"), 1)));
    JCHECK_EQ(Level::INFO->getClass()->getName(), String("org.apache.log4j.Level"));
}

JTEST(Log_PatternLayout_JavaParity) {
    TzScope tz("UTC");
    Logger* lg = Logger::getLogger(String("org.openaion.gameserver.services.ItemService"));
    for (const LayoutRow& r : kLayoutRows) {
        LogLogCapture cap;  // "%q" and "%c{-1}" rows report errors
        Layout* l = r.enhanced ? static_cast<Layout*>(new EnhancedPatternLayout(String(r.pattern)))
                               : static_cast<Layout*>(new PatternLayout(String(r.pattern)));
        Object* msg = nullptr;
        if (r.msg == nullptr) msg = nullptr;
        else if (std::strcmp(r.msg, "42") == 0) msg = box(42);
        else if (std::strcmp(r.msg, "sb") == 0) msg = new StringBuilder(String("sb"));
        else msg = box(String(r.msg));
        auto* ev = new LoggingEvent(String("x"), lg, INT64_C(1234567890123), Level::INFO, msg, nullptr);
        String out = l->format(ev);
        if (!(out == String(r.out))) {
            jtest::fail(__FILE__, __LINE__,
                        std::string("pattern ") + r.pattern + " (" + (r.enhanced ? "enhanced" : "classic") +
                            "): got [" + std::string(out) + "] expected [" + r.out + "]");
        }
        JCHECK_EQ(l->ignoresThrowable(), r.ignoresThrowable);
    }
}

JTEST(Log_PatternLayout_Errors) {
    {
        LogLogCapture cap;
        PatternLayout l(String("%q unknown"));
        JCHECK_EQ(cap.count("log4j:ERROR Unexpected char [q] at position 2 in conversion patterrn."), 1);
    }
    {
        LogLogCapture cap;
        EnhancedPatternLayout l(String("%q unknown"));
        JCHECK_EQ(cap.count("log4j:ERROR Unrecognized format specifier [q]"), 1);
        JCHECK_EQ(cap.count("log4j:ERROR Unrecognized conversion specifier [q] starting at position 2 in conversion pattern."), 1);
    }
    {
        LogLogCapture cap;
        PatternLayout l(String("%c{-1}"));
        JCHECK_EQ(cap.count("log4j:ERROR Precision option (-1) isn't a positive integer."), 1);
    }
    {
        LogLogCapture cap;
        EnhancedPatternLayout l(String("%d{yyyy qq}"));
        JCHECK_EQ(cap.count("log4j:WARN Could not instantiate SimpleDateFormat with pattern yyyy qq"), 1);
        JCHECK_EQ(cap.count("Illegal pattern character 'q'"), 1);
    }
}

JTEST(Log_SimpleDateFormat) {
    TzScope tz("UTC");
    Logger* lg = Logger::getLogger(String("d"));
    EnhancedPatternLayout l(String("%d{yyyy-MM-dd EEE w W D F u Y h a k K}"));
    for (const DateRow& r : kDateRows) {
        auto* ev = new LoggingEvent(String("x"), lg, r.millis, Level::INFO, nullptr, nullptr);
        JCHECK_EQ(l.format(ev), String(r.text));
    }
    auto* ev = new LoggingEvent(String("x"), lg, INT64_C(1234567890123), Level::INFO, nullptr, nullptr);
    JCHECK_EQ((new EnhancedPatternLayout(String("%d{HH:mm:ss.SSS X XX XXX Z zzzz}{GMT+05:30}")))->format(ev),
              String("05:01:30.123 +05 +0530 +05:30 +0530 GMT+05:30"));
    JCHECK_EQ((new EnhancedPatternLayout(String("%d{HH:mm:ss.SSS X XX XXX Z}{UTC}")))->format(ev),
              String("23:31:30.123 Z Z Z +0000"));
    {
        TzScope berlin("Europe/Berlin");
        JCHECK_EQ((new PatternLayout(String("%d{yyyy-MM-dd HH:mm:ss z Z}")))->format(ev),
                  String("2009-02-14 00:31:30 CET +0100"));
        auto* summer = new LoggingEvent(String("x"), lg, INT64_C(1245000000000), Level::INFO, nullptr, nullptr);
        JCHECK_EQ((new PatternLayout(String("%d{HH:mm z Z}")))->format(summer), String("19:20 CEST +0200"));
    }
}

JTEST(Log_Abbreviations) {
    Logger* lg = Logger::getLogger(String("org.openaion.gameserver.services.ItemService"));
    auto* ev = new LoggingEvent(String("x"), lg, 0, Level::INFO, nullptr, nullptr);
    auto fmt = [&](const char* p) { return (new EnhancedPatternLayout(String(p)))->format(ev); };
    JCHECK_EQ(fmt("%c{1}"), String("ItemService"));
    JCHECK_EQ(fmt("%c{3}"), String("gameserver.services.ItemService"));
    JCHECK_EQ(fmt("%c{-2}"), String("gameserver.services.ItemService"));
    JCHECK_EQ(fmt("%c{1.}"), String("o.o.g.s.ItemService"));
    JCHECK_EQ(fmt("%c{1~.}"), String("o~.o~.g~.s~.ItemService"));
    JCHECK_EQ(fmt("%c{2.*.}"), String("or.openaion.gameserver.services.ItemService"));
    JCHECK_EQ(fmt("%c{0}"), String(""));
    JCHECK_EQ((new PatternLayout(String("%c{2}")))->format(ev), String("services.ItemService"));
}

JTEST(Log_LoggerHierarchy) {
    // children created before their ancestors (log4j provision nodes)
    Logger* c = Logger::getLogger(String("jtest.h.a.b.C"));
    Logger* d = Logger::getLogger(String("jtest.h.a.b.D"));
    JCHECK(c->getParent() == Logger::getRootLogger());
    Logger* a = Logger::getLogger(String("jtest.h.a"));
    JCHECK(c->getParent() == a);
    JCHECK(d->getParent() == a);
    JCHECK(a->getParent() == Logger::getRootLogger());
    Logger* b = Logger::getLogger(String("jtest.h.a.b"));
    JCHECK(c->getParent() == b);
    JCHECK(b->getParent() == a);
    JCHECK(Logger::getLogger(String("jtest.h.a.b.C")) == c);
    JCHECK(Logger::exists(String("jtest.h.a.b")) == b);
    JCHECK(Logger::exists(String("jtest.h")) == nullptr);
    // level inheritance
    a->setLevel(Level::ERROR);
    JCHECK(c->getEffectiveLevel() == Level::ERROR);
    JCHECK(!c->isInfoEnabled());
    JCHECK(c->isEnabledFor(Level::FATAL));
    b->setLevel(Level::TRACE);
    JCHECK(c->isTraceEnabled());
    b->setLevel(nullptr);
    JCHECK(c->getEffectiveLevel() == Level::ERROR);
    a->setLevel(nullptr);
    // additivity
    auto* appA = new CaptureAppender();
    auto* appC = new CaptureAppender();
    a->addAppender(appA);
    a->addAppender(appA);  // no duplicates
    c->addAppender(appC);
    a->setLevel(Level::DEBUG);
    a->setAdditivity(false);
    c->info("to c and a");
    JCHECK_EQ(appA->events.size(), static_cast<size_t>(1));
    JCHECK_EQ(appC->events.size(), static_cast<size_t>(1));
    c->setAdditivity(false);
    c->info("only c");
    JCHECK_EQ(appA->events.size(), static_cast<size_t>(1));
    JCHECK_EQ(appC->events.size(), static_cast<size_t>(2));
    JCHECK(c->isAttached(appC));
    c->removeAppender(appC);
    JCHECK(!c->isAttached(appC));
    appC->setName(String("named"));
    c->addAppender(appC);
    JCHECK(c->getAppender(String("named")) == appC);
    c->removeAppender(String("named"));
    JCHECK(c->getAllAppenders().empty());
    a->removeAllAppenders();
    JCHECK(appA->closed);
    // the root logger refuses a null level
    {
        LogLogCapture cap;
        Level* before = Logger::getRootLogger()->getLevel();
        Logger::getRootLogger()->setLevel(nullptr);
        JCHECK(Logger::getRootLogger()->getLevel() == before);
        JCHECK_EQ(cap.count("log4j:ERROR You have tried to set a null level to root."), 1);
    }
    JCHECK_EQ(Logger::getRootLogger()->getName(), String("root"));
    // getLogger(Class*) uses the Java class name
    JCHECK_EQ(Logger::getLogger(Class::of<ConsoleAppender>())->getName(), String("org.apache.log4j.ConsoleAppender"));
}

JTEST(Log_MessagesAndThrowables) {
    auto* app = new CaptureAppender();
    app->setLayout(new EnhancedPatternLayout(String("%p %m|")));
    Logger* l = isolatedLogger("jtest.log.Messages", app);
    l->info("literal");
    l->info(String("string"));
    l->info(str("concat ", 42, ' ', 1.5));
    l->info(std::string("std"));
    l->warn(static_cast<Object*>(new StringBuilder(String("builder"))));
    l->error(String(nullptr));
    l->error(nullptr);
    l->debug("dbg");
    l->trace("not enabled at DEBUG");
    l->fatal("fatal");
    l->log(Level::WARN, "via log");
    l->log(Logger::FQCN, Level::ERROR, "via fqcn log", nullptr);
    l->assertLog(true, String("not logged"));
    l->assertLog(false, String("assertion"));
    std::vector<std::string> expect = {"INFO literal|",  "INFO string|", "INFO concat 42 1.5|", "INFO std|",
                                       "WARN builder|",  "ERROR null|",  "ERROR null|",          "DEBUG dbg|",
                                       "FATAL fatal|",   "WARN via log|", "ERROR via fqcn log|", "ERROR assertion|"};
    JCHECK_EQ(app->lines.size(), expect.size());
    for (size_t i = 0; i < expect.size() && i < app->lines.size(); i++) JCHECK_EQ(std::string(app->lines[i]), expect[i]);
    // String messages are boxed Strings (commons filters cast them back)
    auto* ev0 = app->events[0];
    JCHECK(instanceof<StringBox>(ev0->getMessage()));
    JCHECK_EQ(unbox<String>(ev0->getMessage()), String("literal"));
    JCHECK(app->events[5]->getMessage() == nullptr);
    JCHECK_EQ(ev0->getLoggerName(), String("jtest.log.Messages"));
    JCHECK(ev0->getLevel() == Level::INFO);
    JCHECK(ev0->getThrowableInformation() == nullptr);
    JCHECK_EQ(ev0->fqnOfCategoryClass, String("org.apache.log4j.Logger"));
    JCHECK(ev0->timeStamp > 0);
    JCHECK_EQ(ev0->getThreadName(), String("main"));

    // Throwables: (msg, t), (msg, t*), and a throwable as message (ThrowableAsMessageLogger)
    app->lines.clear();
    app->events.clear();
    try {
        throw IllegalStateException(String("boom"));
    } catch (Exception& e) {
        l->error("failed", e);
        l->error(e);  // == error(e.getLocalizedMessage(), e)
        l->warn("ptr", &e);
    }
    l->error("heap", new RuntimeException(String("heap one")));
    try {
        throw NullPointerException();
    } catch (Throwable& t) {
        l->error(t);  // null localized message -> null message, throwable kept
    }
    JCHECK_EQ(app->events.size(), static_cast<size_t>(5));
    if (app->events.size() == 5) {
        JCHECK_EQ(app->lines[0], String("ERROR failed|"));
        JCHECK_EQ(app->lines[1], String("ERROR boom|"));
        JCHECK_EQ(app->lines[2], String("WARN ptr|"));
        JCHECK_EQ(app->lines[4], String("ERROR null|"));
        for (auto* ev : app->events) {
            ThrowableInformation* ti = ev->getThrowableInformation();
            JCHECK(ti != nullptr && ti->getThrowable() != nullptr);
        }
        JCHECK(instanceof<StringBox>(app->events[1]->getMessage()));
        JCHECK(app->events[4]->getMessage() == nullptr);
        Array<String>* rep = app->events[0]->getThrowableStrRep();
        JCHECK(rep != nullptr && rep->length >= 1);
        if (rep != nullptr && rep->length >= 1) JCHECK_EQ((*rep)[0], String("java.lang.IllegalStateException: boom"));
        // the event keeps a GC copy of the caught exception (valid after the catch block)
        JCHECK_EQ(app->events[0]->getThrowableInformation()->getThrowable()->getMessage(), String("boom"));
        JCHECK(instanceof<IllegalStateException>(app->events[0]->getThrowableInformation()->getThrowable()));
        JCHECK_EQ(app->events[3]->getThrowableInformation()->getThrowable()->getMessage(), String("heap one"));
    }
}

JTEST(Log_ThrowableRendering) {
    auto* app = new CaptureAppender();
    Logger* l = isolatedLogger("jtest.log.Throwables", app);
    RuntimeException* outer = nullptr;
    try {
        try {
            throw IllegalStateException(String("inner"));
        } catch (IllegalStateException& inner) {
            throw RuntimeException(String("outer"), inner);
        }
    } catch (RuntimeException& e) {
        outer = static_cast<RuntimeException*>(e.copyThrowable());
    }
    app->setLayout(new EnhancedPatternLayout(String("%m %throwable{short}|")));
    l->error("msg", outer);
    app->setLayout(new EnhancedPatternLayout(String("%m %throwable{none}|")));
    l->error("msg", outer);
    app->setLayout(new EnhancedPatternLayout(String("%m %throwable{2}|")));
    l->error("msg", outer);
    app->setLayout(new EnhancedPatternLayout(String("%m %throwable|")));
    l->error("msg", outer);
    l->error("plain");
    JCHECK_EQ(app->lines[0], String("msg java.lang.RuntimeException: outer\n|"));
    JCHECK_EQ(app->lines[1], String("msg |"));
    std::string two = app->lines[2];
    JCHECK(two.rfind("msg java.lang.RuntimeException: outer\n\tat ", 0) == 0);
    std::string full = app->lines[3];
    JCHECK(full.find("\nCaused by: java.lang.IllegalStateException: inner\n") != std::string::npos);
    JCHECK(full.find("\n\t... ") != std::string::npos);  // frames in common with the enclosing trace
    JCHECK_EQ(app->lines[4], String("plain |"));
    Array<String>* rep = (new ThrowableInformation(outer))->getThrowableStrRep();
    JCHECK_EQ((*rep)[0], String("java.lang.RuntimeException: outer"));
    bool sawCause = false;
    for (const String& s : *rep) sawCause = sawCause || s.equals("Caused by: java.lang.IllegalStateException: inner");
    JCHECK(sawCause);
}

JTEST(Log_WriterAppenderPrintsStackTraces) {
    auto* ps = new CapturePrintStream();
    PrintStream* oldOut = System::out;
    System::setOut(ps);
    auto* console = new ConsoleAppender(new PatternLayout(String("%p %m%n")));
    System::setOut(oldOut);
    Logger* l = isolatedLogger("jtest.log.Writer", console);
    l->info("hello");
    l->error("bad", new IllegalArgumentException(String("arg")));
    std::string out = ps->text();
    JCHECK(out.rfind("INFO hello\nERROR bad\njava.lang.IllegalArgumentException: arg\n", 0) == 0);
    // EnhancedPatternLayout with %throwable handles the throwable itself: no second copy
    auto* ps2 = new CapturePrintStream();
    System::setOut(ps2);
    auto* console2 = new ConsoleAppender(new EnhancedPatternLayout(String("%p %m %throwable{short}")));
    System::setOut(oldOut);
    l->removeAllAppenders();
    l->addAppender(console2);
    l->error("bad", new IllegalArgumentException(String("arg")));
    JCHECK_EQ(ps2->text(), std::string("ERROR bad java.lang.IllegalArgumentException: arg\n"));
    // System.err target
    auto* ps3 = new CapturePrintStream();
    PrintStream* oldErr = System::err;
    System::setErr(ps3);
    auto* errApp = new ConsoleAppender(new SimpleLayout(), String("System.err"));
    System::setErr(oldErr);
    l->removeAllAppenders();
    l->addAppender(errApp);
    l->warn("to stderr");
    JCHECK_EQ(ps3->text(), std::string("WARN - to stderr\n"));
    {
        LogLogCapture cap;
        errApp->setTarget(String("System.bogus"));
        JCHECK_EQ(cap.count("log4j:WARN [System.bogus] should be System.out or System.err."), 1);
    }
    l->removeAllAppenders();
    {
        LogLogCapture cap;
        l->warn("closed appender");  // no appenders at all: log4j's one-time warning
        errApp->doAppend(new LoggingEvent(Logger::FQCN, l, Level::WARN, nullptr, nullptr));
        JCHECK_EQ(cap.count("log4j:ERROR Attempted to append to closed appender named [null]."), 1);
    }
}

namespace {
class PrefixFilter : public Filter {
public:
    String prefix;
    int32_t onMatch = ACCEPT;
    int32_t decide(LoggingEvent* e) override {
        String m = e->getRenderedMessage();
        return m.startsWith(prefix) ? onMatch : NEUTRAL;
    }
};
}  // namespace

JTEST(Log_ThresholdAndFilters) {
    auto* app = new CaptureAppender();
    Logger* l = isolatedLogger("jtest.log.Filters", app);
    app->setThreshold(Level::WARN);
    l->info("below threshold");
    l->warn("at threshold");
    JCHECK_EQ(app->events.size(), static_cast<size_t>(1));
    app->setThreshold(nullptr);
    app->events.clear();
    // chain: [AUDIT] accepted, [SKIP] denied, the rest denied by DenyAllFilter
    auto* f1 = new PrefixFilter();
    f1->prefix = "[AUDIT]";
    auto* f2 = new PrefixFilter();
    f2->prefix = "[SKIP]";
    f2->onMatch = Filter::DENY;
    app->addFilter(f1);
    app->addFilter(f2);
    app->addFilter(new DenyAllFilter());
    JCHECK(app->getFilter() == f1 && f1->getNext() == f2);
    l->info("[AUDIT] yes");
    l->info("[SKIP] no");
    l->info("other");
    JCHECK_EQ(app->events.size(), static_cast<size_t>(1));
    app->clearFilters();
    app->events.clear();
    auto* range = new LevelRangeFilter();
    JCHECK(range->setOption(String("LevelMin"), String("INFO")));
    JCHECK(range->setOption(String("LevelMax"), String("WARN")));
    JCHECK(range->setOption(String("AcceptOnMatch"), String("true")));
    app->addFilter(range);
    l->debug("d");
    l->info("i");
    l->warn("w");
    l->error("e");
    JCHECK_EQ(app->events.size(), static_cast<size_t>(2));
    app->clearFilters();
    app->events.clear();
    auto* lm = new LevelMatchFilter();
    lm->setLevelToMatch(String("ERROR"));
    lm->setAcceptOnMatch(false);
    app->addFilter(lm);
    auto* sm = new StringMatchFilter();
    sm->setStringToMatch(String("drop"));
    sm->setAcceptOnMatch(false);
    app->addFilter(sm);
    l->error("error dropped");
    l->info("please drop me");
    l->info("kept");
    JCHECK_EQ(app->events.size(), static_cast<size_t>(1));
    // repository threshold
    app->clearFilters();
    app->events.clear();
    LogManager::setThreshold(Level::ERROR);
    l->warn("disabled by threshold");
    JCHECK(!l->isDebugEnabled());
    LogManager::setThreshold(Level::ALL);
    l->warn("enabled again");
    JCHECK_EQ(app->events.size(), static_cast<size_t>(1));
}

JTEST(Log_LocationInfo) {
    auto* app = new CaptureAppender();
    app->setLayout(new EnhancedPatternLayout(String("%F:%L %C.%M|%l")));
    Logger* l = isolatedLogger("jtest.log.Location", app);
    int line = __LINE__ + 1;
    l->info("where");
    std::string out = app->lines.at(0);
    std::string expectPrefix = "test_log.cpp:" + std::to_string(line) + " ";
    JCHECK(out.rfind(expectPrefix, 0) == 0);
    JCHECK(out.find("|?.jtest_fn_Log_LocationInfo(test_log.cpp:" + std::to_string(line) + ")") != std::string::npos);
    auto* li = app->events[0]->getLocationInformation();
    JCHECK_EQ(li->getMethodName(), String("jtest_fn_Log_LocationInfo"));
    JCHECK_EQ(li->getLineNumber(), String::valueOf(line));
    // an event created without a call site has no location
    auto* ev = new LoggingEvent(String("x"), l, Level::INFO, nullptr, nullptr);
    JCHECK_EQ(ev->getLocationInformation()->getClassName(), String("?"));
    JCHECK_EQ((new EnhancedPatternLayout(String("%L %l")))->format(ev), String("? null"));
    JCHECK_EQ((new PatternLayout(String("%L|%l|")))->format(ev), String("?||"));
}

namespace jtestlog {
struct Worker : public virtual Object {
    Logger* log = Logger::getLogger(String("jtest.log.Worker"));
    void run() { log->info("from member"); }
    Worker() { log->info("from ctor"); }
    static void _clinit() { Logger::getLogger(String("jtest.log.Worker"))->info("from clinit"); }
    template<class T>
    void templ(T) {
        auto f = [this]() { log->info("from lambda"); };
        f();
    }
};
}  // namespace jtestlog

JTEST(Log_LocationClassAndMethod) {
    auto* app = new CaptureAppender();
    app->setLayout(new EnhancedPatternLayout(String("%C|%M")));
    isolatedLogger("jtest.log.Worker", app);
    auto* w = new jtestlog::Worker();
    w->run();
    jtestlog::Worker::_clinit();
    w->templ(1);
    JCHECK_EQ(app->lines.size(), static_cast<size_t>(4));
    if (app->lines.size() == 4) {
        JCHECK_EQ(app->lines[0], String("jtestlog.Worker|<init>"));
        JCHECK_EQ(app->lines[1], String("jtestlog.Worker|run"));
        JCHECK_EQ(app->lines[2], String("jtestlog.Worker|<clinit>"));
        JCHECK_EQ(app->lines[3], String("jtestlog.Worker|templ"));
    }
}

JTEST(Log_FileAppender) {
    std::string dir = makeTempDir();
    std::string path = dir + "/sub/dir/app.log";  // parent directories are created
    auto* fa = new FileAppender();
    fa->setName(String("F"));
    JCHECK(fa->setOption(String("File"), String("  " + path + "  ")));
    JCHECK(fa->setOption(String("append"), String("false")));
    fa->setLayout(new EnhancedPatternLayout(String("%p %m%n")));
    fa->activateOptions();
    JCHECK_EQ(fa->getFile(), String(path));
    Logger* l = isolatedLogger("jtest.log.File", fa);
    l->info("one");
    l->warn("two \xC3\xA4");
    JCHECK_EQ(readFile(path), std::string("INFO one\nWARN two \xC3\xA4\n"));  // immediate flush
    fa->close();
    // append mode keeps the content; BufferedIO flushes on close
    auto* fb = new FileAppender(new EnhancedPatternLayout(String("%m%n")), String(path), true, true, 4096);
    JCHECK(!fb->getImmediateFlush());
    l->removeAllAppenders();
    l->addAppender(fb);
    l->info("three");
    l->removeAllAppenders();  // closes fb
    JCHECK_EQ(readFile(path), std::string("INFO one\nWARN two \xC3\xA4\nthree\n"));
    // truncate mode
    auto* fc = new FileAppender(new SimpleLayout(), String(path), false);
    l->addAppender(fc);
    l->error("four");
    JCHECK_EQ(readFile(path), std::string("ERROR - four\n"));
    l->removeAllAppenders();
    // open failure goes to the error handler (reported once through LogLog)
    {
        LogLogCapture cap;
        auto* bad = new FileAppender();
        bad->setFile(String("/proc/definitely/not/writable.log"));
        bad->setLayout(new SimpleLayout());
        bad->activateOptions();
        JCHECK_EQ(cap.count("log4j:ERROR setFile(/proc/definitely/not/writable.log,true) call failed."), 1);
        JCHECK_EQ(cap.count("java.io.FileNotFoundException: /proc/definitely/not/writable.log ("), 1);
        auto* none = new FileAppender();
        none->setName(String("NOFILE"));
        none->activateOptions();
        JCHECK_EQ(cap.count("log4j:WARN File option not set for appender [NOFILE]."), 1);
    }
    JCHECK_THROWS(FileNotFoundException, new FileAppender(new SimpleLayout(), String("/proc/nope/x.log")));
}

JTEST(Log_RollingFileAppender) {
    std::string dir = makeTempDir();
    std::string path = dir + "/roll.log";
    auto* ra = new RollingFileAppender();
    JCHECK(ra->setOption(String("file"), String(path)));
    JCHECK(ra->setOption(String("MaxFileSize"), String("1KB")));
    JCHECK(ra->setOption(String("MaxBackupIndex"), String("2")));
    JCHECK_EQ(ra->getMaximumFileSize(), INT64_C(1024));
    ra->setLayout(new EnhancedPatternLayout(String("%m%n")));
    ra->activateOptions();
    Logger* l = isolatedLogger("jtest.log.Rolling", ra);
    std::string line(99, 'x');  // 100 bytes per event
    for (int i = 0; i < 45; i++) l->info(line);
    l->removeAllAppenders();
    JCHECK(exists(path));
    JCHECK(exists(path + ".1"));
    JCHECK(exists(path + ".2"));
    JCHECK(!exists(path + ".3"));
    JCHECK_EQ(readFile(path + ".1").size(), static_cast<size_t>(1100));
    JCHECK_EQ(readFile(path).size(), static_cast<size_t>(45 * 100 - 4 * 1100));
}

// ---------------------------------------------------------------------------------------
// DOMConfigurator

namespace {

std::string repoRoot() {
    std::string f = __FILE__;  // <repo>/jlang/tests/test_log.cpp
    for (int i = 0; i < 3; i++) f = f.substr(0, f.find_last_of('/'));
    return f;
}

struct CwdScope {
    std::string old;
    explicit CwdScope(const std::string& dir) {
        char buf[4096];
        old = getcwd(buf, sizeof buf) != nullptr ? buf : ".";
        if (chdir(dir.c_str()) != 0) jtest::fail(__FILE__, __LINE__, "chdir failed");
    }
    ~CwdScope() {
        if (chdir(old.c_str()) != 0) jtest::fail(__FILE__, __LINE__, "chdir back failed");
    }
};

struct OutScope {
    CapturePrintStream* ps = new CapturePrintStream();
    PrintStream* old = System::out;
    OutScope() { System::setOut(ps); }
    ~OutScope() { System::setOut(old); }
};

}  // namespace

JTEST(Log_DOMConfigurator_RepoConfigs) {
    const char* modules[] = {"commons", "gameserver", "loginserver", "chatserver"};
    for (const char* mod : modules) {
        std::string dir = makeTempDir();
        CwdScope cwd(dir);
        LogLogCapture cap;
        OutScope out;
        DOMConfigurator::configure(String(repoRoot() + "/" + mod + "/config/log4j.xml"));
        Logger* root = Logger::getRootLogger();
        JCHECK(root->getLevel() == Level::INFO);
        std::vector<Appender*> apps = root->getAllAppenders();
        // The commons classes (TruncateToZipFileAppender, the filters, ThrowableAsMessageAwareFactory)
        // are not ported yet: skipped with a warning each.
        JCHECK_EQ(cap.count("log4j:WARN Could not instantiate logger factory class"), 1);
        JCHECK_EQ(cap.count("log4j:ERROR"), 0);
        if (std::string(mod) == "chatserver") {
            JCHECK_EQ(apps.size(), static_cast<size_t>(3));
            JCHECK_EQ(cap.count("log4j:WARN Could not create an Appender"), 0);
            JCHECK_EQ(cap.count("[org.openaion.commons.log4j.filters.ConsoleFilter]") + cap.count("[com.aionengine.commons.log4j.filters.ConsoleFilter]"), 2);
            JCHECK_EQ(cap.count("ThrowablePresentFilter"), 1);
            auto* errors = dynamic_cast<RollingFileAppender*>(root->getAppender(String("ERROR_APPENDER")));
            JCHECK(errors != nullptr);
            if (errors != nullptr) {
                JCHECK_EQ(errors->getMaximumFileSize(), INT64_C(50000) * 1024);
                JCHECK_EQ(errors->getMaxBackupIndex(), 5);
                JCHECK(errors->getAppend());
            }
            auto* c2f = dynamic_cast<RollingFileAppender*>(root->getAppender(String("CONSOLE_TO_FILE")));
            JCHECK(c2f != nullptr && !c2f->getAppend());
        } else {
            JCHECK_EQ(apps.size(), static_cast<size_t>(1));
            int truncating = std::string(mod) == "gameserver" ? 7 : 2;
            JCHECK_EQ(cap.count("class [org.openaion.commons.log4j.appenders.TruncateToZipFileAppender]"), truncating);
        }
        auto* console = dynamic_cast<ConsoleAppender*>(root->getAppender(String("CONSOLE")));
        JCHECK(console != nullptr);
        auto* layout = console != nullptr ? dynamic_cast<EnhancedPatternLayout*>(console->getLayout()) : nullptr;
        JCHECK(layout != nullptr);
        Logger* l = Logger::getLogger(String("jtest.log.config.Probe"));
        int line = __LINE__ + 1;
        l->info("probe message");
        l->debug("not at INFO");
        std::string text = out.ps->text();
        JCHECK(text.find("probe message\n") != std::string::npos);
        JCHECK(text.find("not at INFO") == std::string::npos);
        if (std::string(mod) == "commons") {
            // "[%p %d{yyyy-MM-dd HH-mm-ss}] %c:%L - %m%n"
            JCHECK(text.rfind("[INFO ", 0) == 0);
            JCHECK(text.find("] jtest.log.config.Probe:" + std::to_string(line) + " - probe message\n") != std::string::npos);
        } else if (std::string(mod) == "chatserver") {
            JCHECK(text.rfind("INFO [", 0) == 0);  // "%p [%d{dd MMM HH:mm:ss,SSS}] %m%n"
            std::string file = readFile(dir + "/log/console.log");
            JCHECK(file.find("] jtest.log.config.Probe:" + std::to_string(line) + " probe message\n") != std::string::npos);
            JCHECK(exists(dir + "/log/errors.log"));
        } else {
            JCHECK(text.rfind("[INFO] ", 0) == 0);  // "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n"
        }
        LogManager::resetConfiguration();
        restoreRoot();
    }
}

JTEST(Log_DOMConfigurator_Features) {
    std::string dir = makeTempDir();
    System::setProperty(String("jtest.logdir"), String(dir));
    LogLogCapture cap;
    DOMConfigurator::configureFromXml(String(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE log4j:configuration SYSTEM "log4j.dtd">
<log4j:configuration threshold="debug">
  <appender name="FILE" class="org.apache.log4j.FileAppender">
    <param name="File" value="${jtest.logdir}/f.log"/>
    <param name="Append" value="false"/>
    <param name="Threshold" value="info"/>
    <param name="BogusProperty" value="x"/>
    <param name="BufferSize" value="notanumber"/>
    <layout class="org.apache.log4j.PatternLayout">
      <param name="ConversionPattern" value="%-5p %c{1}\t%m%n"/>
    </layout>
    <filter class="org.apache.log4j.varia.StringMatchFilter">
      <param name="StringToMatch" value="secret"/>
      <param name="AcceptOnMatch" value="false"/>
    </filter>
  </appender>
  <appender name="UNUSED" class="org.apache.log4j.FileAppender">
    <param name="File" value="${jtest.logdir}/unused.log"/>
  </appender>
  <appender name="MISSING_CLASS" class="com.example.NoSuchAppender"/>
  <logger name="jtest.cfg.a" additivity="false">
    <level value="debug"/>
    <appender-ref ref="FILE"/>
    <appender-ref ref="MISSING_CLASS"/>
    <appender-ref ref="NOT_DEFINED"/>
  </logger>
  <category name="jtest.cfg.b">
    <priority value="error"/>
    <appender-ref ref="FILE"/>
  </category>
  <logger name="jtest.cfg.c">
    <level value="inherited"/>
  </logger>
</log4j:configuration>)"));
    std::string log = cap.all();
    JCHECK_EQ(cap.count("log4j:WARN No such property [bogusProperty] in org.apache.log4j.FileAppender."), 1);
    JCHECK_EQ(cap.count("log4j:WARN Failed to set property [bufferSize] to value \"notanumber\". "), 1);
    JCHECK_EQ(cap.count("class [com.example.NoSuchAppender] of appender [MISSING_CLASS] is not registered"), 1);
    JCHECK_EQ(cap.count("log4j:ERROR No appender named [NOT_DEFINED] could be found."), 1);
    JCHECK(!exists(dir + "/unused.log"));  // appenders are only created when referenced
    Logger* a = Logger::getLogger(String("jtest.cfg.a.X"));
    Logger* b = Logger::getLogger(String("jtest.cfg.b"));
    JCHECK(!Logger::getLogger(String("jtest.cfg.a"))->getAdditivity());
    JCHECK(Logger::getLogger(String("jtest.cfg.c"))->getLevel() == nullptr);
    JCHECK(LogManager::getThreshold() == Level::DEBUG);
    a->debug("below the appender threshold");
    a->info("a info");
    a->warn("a secret dropped by the filter");
    b->warn("b warn below logger level");
    b->error("b error");
    JCHECK(b->getAllAppenders().size() == 1 && Logger::getLogger(String("jtest.cfg.a"))->getAllAppenders()[0] ==
                                                      b->getAllAppenders()[0]);  // one instance per name
    std::string content = readFile(dir + "/f.log");
    JCHECK_EQ(content, std::string("INFO  X\ta info\nERROR b\tb error\n"));
    Logger::getLogger(String("jtest.cfg.a"))->removeAllAppenders();
    b->removeAllAppenders();
    LogManager::setThreshold(Level::ALL);
    // malformed / missing files never throw
    LogLogCapture cap2;
    DOMConfigurator::configure(String(dir + "/does-not-exist.xml"));
    JCHECK_EQ(cap2.count("log4j:ERROR Could not parse file [" + dir + "/does-not-exist.xml]."), 1);
    DOMConfigurator::configureFromXml(String("<log4j:configuration><root>"));
    JCHECK_EQ(cap2.count("log4j:ERROR Could not parse XML text."), 1);
    DOMConfigurator::configureFromXml(String("<other/>"));
    JCHECK_EQ(cap2.count("log4j:ERROR DOM element is - not a <log4j:configuration> element."), 1);
}

// ---- extension points used by the ported commons classes
namespace {

// like commons' ConsoleFilter (casts the message to String)
class TestConsoleFilter : public Filter {
public:
    int32_t decide(LoggingEvent* e) override {
        Object* message = e->getMessage();
        if (message == nullptr) return DENY;
        if (unbox<String>(message).startsWith(String("[MESSAGE]"))) return DENY;
        return ACCEPT;
    }
};

// like TruncateToZipFileAppender (generated header: bean setters, setFile override, no setOption)
class TestTruncatingAppender : public FileAppender {
public:
    String backupDir = String("log/backup");
    String backupDateFormat = String("yyyy-MM-dd HH-mm-ss");
    int setFileCalls = 0;
    using FileAppender::setFile;
    void setFile(String fileName, bool append, bool bufferedIO, int32_t bufferSize) override {
        setFileCalls++;
        FileAppender::setFile(fileName, append, bufferedIO, bufferSize);
    }
    virtual void setBackupDir(String v) { backupDir = v; }
    virtual void setBackupDateFormat(String v) { backupDateFormat = v; }
};

// a subclass inherits the registered setters
class TestTruncatingAppender2 : public TestTruncatingAppender {};

// like ThrowableAsMessageLogger / ThrowableAsMessageAwareFactory
class TestLogger : public jlang::Logger {
public:
    explicit TestLogger(const String& name) : jlang::Logger(name) {}
    static inline int forced = 0;
    void forcedLog(String fqcn, Level* level, Object* message, Throwable* t) override {
        forced++;
        if (instanceof<Throwable>(message) && t == nullptr) {
            t = cast<Throwable>(message);
            message = box(t->getLocalizedMessage());
        }
        jlang::Logger::forcedLog(fqcn, level, message, t);
    }
};

class TestLoggerFactory : public LoggerFactory {
public:
    jlang::Logger* makeNewLoggerInstance(String name) override { return new TestLogger(name); }
};

}  // namespace

JTEST(Log_DOMConfigurator_CustomClasses) {
    std::string dir = makeTempDir();
    registerFilterFactory(String("org.openaion.commons.log4j.filters.ConsoleFilter"), [] { return new TestConsoleFilter(); });
    // what the generator's class registry and the ported TruncateToZipFileAppender.cpp provide
    const char* truncName = "org.openaion.commons.log4j.appenders.TruncateToZipFileAppender";
    Class::registerClass<TestTruncatingAppender>(String(truncName), Class::of<FileAppender>(), {}, Class::NONE,
                                                 []() -> Object* { return new TestTruncatingAppender(); });
    Class::registerClass<TestTruncatingAppender2>(String("jtest.TruncatingSubclass"), Class::of<TestTruncatingAppender>(),
                                                  {}, Class::NONE, []() -> Object* { return new TestTruncatingAppender2(); });
    registerPropertySetter(String(truncName), String("backupDir"), [](Object* o, const String& v) {
        cast<TestTruncatingAppender>(o)->setBackupDir(v);
    });
    registerPropertySetter(String(truncName), String("BackupDateFormat"), [](Object* o, const String& v) {
        cast<TestTruncatingAppender>(o)->setBackupDateFormat(v);
    });
    registerAppenderFactory(String("jtest.RegisteredAppender"), [] { return new FileAppender(); });
    registerLoggerFactoryFactory(String("org.openaion.commons.log4j.ThrowableAsMessageAwareFactory"),
                                 [] { return new TestLoggerFactory(); });
    LogLogCapture cap;
    DOMConfigurator::configureFromXml(String(R"(<log4j:configuration>
  <appender name="T" class="org.openaion.commons.log4j.appenders.TruncateToZipFileAppender">
    <param name="file" value=")" + dir + R"(/t.log"/>
    <param name="append" value="false"/>
    <param name="backupDir" value="log/other"/>
    <param name="backupDateFormat" value="yyyy"/>
    <layout class="org.apache.log4j.EnhancedPatternLayout"><param name="ConversionPattern" value="%m%n"/></layout>
    <filter class="com.aionengine.commons.log4j.filters.ConsoleFilter"/>
  </appender>
  <appender name="S" class="jtest.TruncatingSubclass">
    <param name="File" value=")" + dir + R"(/s.log"/>
    <param name="BackupDir" value="sub/dir"/>
    <layout class="org.apache.log4j.SimpleLayout"/>
  </appender>
  <appender name="R" class="jtest.RegisteredAppender">
    <param name="File" value=")" + dir + R"(/r.log"/>
    <layout class="org.apache.log4j.SimpleLayout"/>
  </appender>
  <logger name="jtest.custom" additivity="false"><level value="info"/><appender-ref ref="T"/></logger>
  <logger name="jtest.custom2" additivity="false"><appender-ref ref="S"/><appender-ref ref="R"/></logger>
  <categoryFactory class="org.openaion.commons.log4j.ThrowableAsMessageAwareFactory"/>
</log4j:configuration>)"));
    JCHECK_EQ(cap.count("log4j:WARN"), 0);
    JCHECK_EQ(cap.count("log4j:ERROR"), 0);
    Logger* l = Logger::getLogger(String("jtest.custom"));
    JCHECK(instanceof<TestLogger>(l));  // created through the <categoryFactory>
    auto* app = dynamic_cast<TestTruncatingAppender*>(l->getAppender(String("T")));
    JCHECK(app != nullptr);
    if (app == nullptr) return;
    JCHECK_EQ(app->backupDir, String("log/other"));
    JCHECK_EQ(app->backupDateFormat, String("yyyy"));
    JCHECK_EQ(app->setFileCalls, 1);  // activateOptions -> overridden setFile
    Logger* l2 = Logger::getLogger(String("jtest.custom2"));
    auto* sub = dynamic_cast<TestTruncatingAppender2*>(l2->getAppender(String("S")));
    JCHECK(sub != nullptr && sub->backupDir.equals("sub/dir"));
    JCHECK(dynamic_cast<FileAppender*>(l2->getAppender(String("R"))) != nullptr);
    l2->removeAllAppenders();
    l->info("[MESSAGE] chat");
    l->info("shown");
    try {
        throw IllegalStateException(String("as message"));
    } catch (Exception& e) {
        l->error(e);
    }
    JCHECK(TestLogger::forced >= 3);
    std::string content = readFile(dir + "/t.log");
    JCHECK(content.rfind("shown\nas message\njava.lang.IllegalStateException: as message\n", 0) == 0);
    l->removeAllAppenders();
    // the default factory override (LoggingService's Hierarchy hack)
    LogManager::setDefaultLoggerFactory(new TestLoggerFactory());
    JCHECK(instanceof<TestLogger>(Logger::getLogger(String("jtest.custom.factory.New"))));
    LogManager::setDefaultLoggerFactory(nullptr);
    JCHECK(!instanceof<TestLogger>(Logger::getLogger(String("jtest.custom.factory.Plain"))));
}

// ---------------------------------------------------------------------------------------
// Concurrency: several GC-registered threads logging through the same appenders.

namespace {
struct ThreadArg {
    Logger* log;
    int id;
    int count;
};
void logThread(void* p) {
    auto* a = static_cast<ThreadArg*>(p);
    for (int i = 0; i < a->count; i++) {
        a->log->info(str("thread ", a->id, " message ", i));
        if (i % 50 == 0) a->log->isDebugEnabled();
    }
}
}  // namespace

JTEST(Log_Concurrency) {
    std::string dir = makeTempDir();
    auto* cap = new CaptureAppender();
    cap->setLayout(new EnhancedPatternLayout(String("%t %m%n")));
    auto* file = new FileAppender(new EnhancedPatternLayout(String("%p %m%n")), String(dir + "/mt.log"), false);
    Logger* l = isolatedLogger("jtest.log.Concurrency", cap);
    l->addAppender(file);
    const int kThreads = 8, kCount = 500;
    std::vector<uint64_t> handles;
    for (int t = 0; t < kThreads; t++) {
        auto* arg = new ThreadArg{l, t, kCount};
        handles.push_back(gc::startNativeThread(&logThread, arg));
    }
    // meanwhile, create loggers and change levels concurrently
    auto* other = new CaptureAppender();
    isolatedLogger("jtest.log.ConcurrencyOther", other);
    for (int i = 0; i < 200; i++) {
        Logger::getLogger(String(str("jtest.log.ConcurrencyOther.child", i)))->info("child");
        l->setLevel(Level::INFO);
    }
    JCHECK_EQ(other->lines.size(), static_cast<size_t>(200));
    for (uint64_t h : handles) gc::joinNativeThread(h);
    JCHECK_EQ(cap->lines.size(), static_cast<size_t>(kThreads * kCount));
    l->removeAllAppenders();
    std::string content = readFile(dir + "/mt.log");
    size_t lines = 0;
    std::istringstream in(content);
    std::string line;
    bool wellFormed = true;
    while (std::getline(in, line)) {
        lines++;
        wellFormed = wellFormed && line.rfind("INFO thread ", 0) == 0 && line.find(" message ") != std::string::npos;
    }
    JCHECK_EQ(lines, static_cast<size_t>(kThreads * kCount));
    JCHECK(wellFormed);
    bool threadNames = true;
    for (const String& s : cap->lines) threadNames = threadNames && !s.startsWith(String("main "));
    JCHECK(threadNames);
}

// Events, messages and throwable copies stay valid across collections (the caught exception
// itself lives in C++ exception storage and is gone after the catch block).
JTEST(Log_SurvivesGarbageCollection) {
    auto* app = new CaptureAppender();
    app->setLayout(new EnhancedPatternLayout(String("%m")));
    Logger* l = isolatedLogger("jtest.log.Gc", app);
    for (int i = 0; i < 200; i++) {
        try {
            throw IllegalStateException(str("failure ", i));
        } catch (Exception& e) {
            l->error(str("message ", i), e);
        }
        if (i % 50 == 0) gc::collect();
    }
    for (int i = 0; i < 3; i++) gc::collect();
    JCHECK_EQ(app->events.size(), static_cast<size_t>(200));
    bool ok = true;
    for (size_t i = 0; i < app->events.size(); i++) {
        LoggingEvent* ev = app->events[i];
        ok = ok && ev->getRenderedMessage().equals(str("message ", static_cast<int64_t>(i)));
        ok = ok && ev->getThrowableInformation()->getThrowable()->getMessage().equals(str("failure ", static_cast<int64_t>(i)));
        Array<String>* rep = ev->getThrowableStrRep();
        ok = ok && (*rep)[0].equals(str("java.lang.IllegalStateException: failure ", static_cast<int64_t>(i)));
    }
    JCHECK(ok);
}

JTEST(Log_EnhancedPatternEscapes) {
    // log4j 1.2.16: the constructor keeps the pattern, setConversionPattern converts "\t" escapes
    Logger* lg = Logger::getLogger(String("x"));
    auto* ev = new LoggingEvent(String("x"), lg, 0, Level::INFO, box(String("m")), nullptr);
    JCHECK_EQ((new EnhancedPatternLayout(String("a\\tb|%m")))->format(ev), String("a\\tb|m"));
    JCHECK_EQ((new PatternLayout(String("a\\tb|%m")))->format(ev), String("a\\tb|m"));
    auto* e = new EnhancedPatternLayout();
    e->setConversionPattern(String("c\\td|%m"));
    JCHECK_EQ(e->getConversionPattern(), String("c\td|%m"));
    JCHECK_EQ(e->format(ev), String("c\td|m"));
}

JTEST(Log_ThreadNames) {
    auto* app = new CaptureAppender();
    app->setLayout(new EnhancedPatternLayout(String("%t|%m")));
    Logger* l = isolatedLogger("jtest.log.ThreadNames", app);
    l->info("on main");
    auto* t = new Thread(Runnable::of([l]() { l->info("on worker"); }), String("LoginServer-worker-7"));
    t->start();
    t->join();
    JCHECK_EQ(app->lines.size(), static_cast<size_t>(2));
    if (app->lines.size() == 2) {
        JCHECK_EQ(app->lines[0], String("main|on main"));
        JCHECK_EQ(app->lines[1], String("LoginServer-worker-7|on worker"));
    }
}
