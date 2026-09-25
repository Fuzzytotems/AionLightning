// jlang/Log.h - log4j 1.2 emulation: org.apache.log4j (Logger/Category, Level/Priority,
// LogManager, appenders, layouts), org.apache.log4j.spi (LoggingEvent, ThrowableInformation,
// LocationInfo, Filter, LoggerFactory, OptionHandler, ErrorHandler), org.apache.log4j.varia
// filters, org.apache.log4j.helpers.LogLog and org.apache.log4j.xml.DOMConfigurator.
//
// Java -> C++ (tools/cppgen/jdkmap.tsv):
//   org.apache.log4j.Logger / Category           jlang::Logger*  (== jlang::log4j::Logger*)
//   org.apache.log4j.Level / Priority            jlang::log4j::Level*
//   org.apache.log4j.LogManager                  jlang::log4j::LogManager (static)
//   org.apache.log4j.spi.LoggerFactory           jlang::log4j::LoggerFactory*
//   org.apache.log4j.spi.LoggingEvent            jlang::log4j::LoggingEvent*
//   org.apache.log4j.spi.ThrowableInformation    jlang::log4j::ThrowableInformation*
//   org.apache.log4j.spi.Filter                  jlang::log4j::Filter*
//   org.apache.log4j.FileAppender                jlang::log4j::FileAppender*
//   org.apache.log4j.AppenderSkeleton            jlang::log4j::AppenderSkeleton*
//   org.apache.log4j.xml.DOMConfigurator         jlang::log4j::DOMConfigurator (static)
//   org.apache.log4j.helpers.LogLog              jlang::log4j::LogLog (static)
//
// Usage (CONVENTIONS §14):
//   static inline jlang::Logger* log = jlang::Logger::getLogger("org.openaion.Foo");
//   log->info("text");  log->info(jlang::str("a", x));  log->error("failed", e);  log->error(e);
//   if (log->isDebugEnabled()) log->debug(...);
//
// * Usable at any time, including from static initializers before main() and before any
//   configuration: until the root logger is configured, INFO and above go to System.out with
//   the pattern "[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n".
// * Thread-safe. Appenders are serialized per appender (JSYNC(appender)), like log4j's
//   synchronized doAppend.
// * A message is a String, a C string, nullptr, any jlang::Object* (rendered with toString())
//   or a Throwable (as caught, by reference, or by pointer). A Throwable message without a
//   throwable argument is logged the way commons' ThrowableAsMessageLogger does it:
//   log->error(e) == log->error(e.getLocalizedMessage(), e).
// * Location info (%L %F %M %C %l) comes from a std::source_location captured as a defaulted
//   argument of every logging method: %F/%L are the C++ file name and line of the call; %C/%M
//   are parsed from the C++ function name ("org::openaion::X::run" -> "org.openaion.X", "run").
// * Extension points for the ported commons classes: subclass Filter, FileAppender (or any
//   appender), Layout, jlang::Logger, LoggerFactory. DOMConfigurator instantiates the classes
//   named in log4j.xml through the factories registered with registerAppenderFactory /
//   registerFilterFactory / registerLayoutFactory / registerLoggerFactoryFactory, falling back
//   to the jlang::Class registry (Class::forName + newInstance), so a generated, registered
//   codebase class needs no extra code. "com.aionengine.commons.X" falls back to
//   "org.openaion.commons.X" (chatserver's ae-commons jar is commons/src). Each
//   <param name="x" value="v"/> goes to a setter registered with registerPropertySetter for the
//   object's class (or a superclass), else to the virtual OptionHandler::setOption(name, value)
//   that the built-in classes implement (overrides call the base class for other names).
// * Overridable virtuals take jlang::String by value, like generated code declares them.
#pragma once

#include <jlang/jlang.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <source_location>
#include <string>
#include <vector>

namespace jlang {
class Logger;
}

namespace jlang::log4j {

class Level;
class LoggingEvent;
class ThrowableInformation;
class LocationInfo;
class Layout;
class Filter;
class Appender;
class ErrorHandler;
class LoggerFactory;
class LogManager;

// ---------------------------------------------------------------------------------------
// org.apache.log4j.Level (also org.apache.log4j.Priority). The constants are unique objects:
// compare with == or equals()/toInt().
class Level : public virtual Object {
public:
    static constexpr int32_t OFF_INT = INT32_MAX;
    static constexpr int32_t FATAL_INT = 50000;
    static constexpr int32_t ERROR_INT = 40000;
    static constexpr int32_t WARN_INT = 30000;
    static constexpr int32_t INFO_INT = 20000;
    static constexpr int32_t DEBUG_INT = 10000;
    static constexpr int32_t TRACE_INT = 5000;
    static constexpr int32_t ALL_INT = INT32_MIN;

    static Level* const OFF;
    static Level* const FATAL;
    static Level* const ERROR;
    static Level* const WARN;
    static Level* const INFO;
    static Level* const DEBUG;
    static Level* const TRACE;
    static Level* const ALL;

    // protected in Java (subclassed for custom levels).
    Level(int32_t level, const String& levelStr, int32_t syslogEquivalent);

    int32_t toInt() { return level_; }
    int32_t getSyslogEquivalent() { return syslogEquivalent_; }
    bool isGreaterOrEqual(Level* r) { return level_ >= r->level_; }
    String toString() override { return levelStr_; }
    bool equals(Object* o) override;  // same int level (Java Priority.equals)
    int32_t hashCode() override { return level_; }

    // Case-insensitive level name ("DEBUG", "info"...); DEBUG / defaultLevel otherwise.
    static Level* toLevel(const String& sArg);
    static Level* toLevel(const String& sArg, Level* defaultLevel);
    static Level* toLevel(int32_t val);
    static Level* toLevel(int32_t val, Level* defaultLevel);

    // Static-initialization-safe access to the constants (what the fields above hold):
    // 0 OFF, 1 FATAL, 2 ERROR, 3 WARN, 4 INFO, 5 DEBUG, 6 TRACE, 7 ALL.
    static Level* builtin(int idx);

private:
    int32_t level_;
    String levelStr_;
    int32_t syslogEquivalent_;
};

inline Level* const Level::OFF = Level::builtin(0);
inline Level* const Level::FATAL = Level::builtin(1);
inline Level* const Level::ERROR = Level::builtin(2);
inline Level* const Level::WARN = Level::builtin(3);
inline Level* const Level::INFO = Level::builtin(4);
inline Level* const Level::DEBUG = Level::builtin(5);
inline Level* const Level::TRACE = Level::builtin(6);
inline Level* const Level::ALL = Level::builtin(7);

// ---------------------------------------------------------------------------------------
// The message parameter of the logging methods (Java: Object message), implicitly built from
// a String, a C string, nullptr, any jlang::Object* or a Throwable. It only refers to the
// argument (no copy, no allocation while the level is disabled): never store one.
class LogMessage {
public:
    LogMessage(const String& s) : str_(&s), kind_(s.isNull() ? NUL : STR) {}
    LogMessage(const std::string& s) : std_(&s), kind_(STD) {}
    LogMessage(const char* s) : cstr_(s), kind_(s == nullptr ? NUL : CSTR) {}
    LogMessage(std::nullptr_t) : kind_(NUL) {}
    LogMessage(Object* o) : obj_(o), kind_(o == nullptr ? NUL : OBJ) {}
    LogMessage(const Throwable& t) : obj_(static_cast<Object*>(const_cast<Throwable*>(&t))), kind_(OBJ) {}
    // The Java message object: Strings are boxed (jlang::StringBox), null is nullptr.
    Object* toObject() const;

private:
    enum Kind : uint8_t { NUL, STR, STD, CSTR, OBJ };
    const String* str_ = nullptr;
    const std::string* std_ = nullptr;
    const char* cstr_ = nullptr;
    Object* obj_ = nullptr;
    Kind kind_;
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.spi.LoggerFactory
class LoggerFactory : public virtual Object {
public:
    virtual ::jlang::Logger* makeNewLoggerInstance(String name) = 0;
};

}  // namespace jlang::log4j

namespace jlang {

// ---------------------------------------------------------------------------------------
// org.apache.log4j.Logger (merged with org.apache.log4j.Category). jlang::log4j::Logger is
// the same class.
class Logger : public virtual Object {
public:
    // Loggers are obtained with getLogger; the constructor is for subclasses and factories
    // (protected in Java).
    explicit Logger(const String& name);

    static Logger* getLogger(const String& name);
    static Logger* getLogger(const char* name) { return getLogger(String(name)); }
    // clazz->getName() (the Java name for classes registered with the class registry).
    static Logger* getLogger(Class* clazz);
    static Logger* getLogger(const String& name, log4j::LoggerFactory* factory);
    static Logger* getRootLogger();
    static Logger* exists(const String& name);

#define JLANG_LOG4J_LEVEL_METHODS(method)                                                   \
    void method(const log4j::LogMessage& message,                                                  \
                const std::source_location& loc = std::source_location::current());         \
    void method(const log4j::LogMessage& message, const Throwable& t,                              \
                const std::source_location& loc = std::source_location::current());         \
    void method(const log4j::LogMessage& message, Throwable* t,                                    \
                const std::source_location& loc = std::source_location::current());
    JLANG_LOG4J_LEVEL_METHODS(trace)
    JLANG_LOG4J_LEVEL_METHODS(debug)
    JLANG_LOG4J_LEVEL_METHODS(info)
    JLANG_LOG4J_LEVEL_METHODS(warn)
    JLANG_LOG4J_LEVEL_METHODS(error)
    JLANG_LOG4J_LEVEL_METHODS(fatal)
#undef JLANG_LOG4J_LEVEL_METHODS

    void log(log4j::Level* priority, const log4j::LogMessage& message,
             const std::source_location& loc = std::source_location::current());
    void log(log4j::Level* priority, const log4j::LogMessage& message, const Throwable& t,
             const std::source_location& loc = std::source_location::current());
    void log(log4j::Level* priority, const log4j::LogMessage& message, Throwable* t,
             const std::source_location& loc = std::source_location::current());
    // Java log(String callerFQCN, Priority level, Object message, Throwable t).
    void log(const String& callerFQCN, log4j::Level* level, const log4j::LogMessage& message, Throwable* t,
             const std::source_location& loc = std::source_location::current());
    // Java assertLog: logs msg at ERROR when assertion is false.
    void assertLog(bool assertion, const String& msg,
                   const std::source_location& loc = std::source_location::current());

    bool isTraceEnabled();
    bool isDebugEnabled();
    bool isInfoEnabled();
    bool isEnabledFor(log4j::Level* level);

    String getName() { return name_; }
    Logger* getParent() { return parent_.load(std::memory_order_acquire); }
    log4j::Level* getLevel() { return level_.load(std::memory_order_acquire); }
    log4j::Level* getPriority() { return getLevel(); }
    // A null level means "inherit from the parent". The root logger rejects null.
    virtual void setLevel(log4j::Level* level);
    void setPriority(log4j::Level* priority) { setLevel(priority); }
    log4j::Level* getEffectiveLevel();
    log4j::Level* getChainedPriority() { return getEffectiveLevel(); }

    bool getAdditivity() { return additive_.load(std::memory_order_acquire); }
    void setAdditivity(bool additive) { additive_.store(additive, std::memory_order_release); }

    // Appender attachment: null is ignored, an already attached appender is not added twice.
    virtual void addAppender(log4j::Appender* newAppender);
    void removeAppender(log4j::Appender* appender);
    void removeAppender(const String& name);
    // Closes and removes every appender (Java AppenderAttachableImpl.removeAllAppenders).
    void removeAllAppenders();
    log4j::Appender* getAppender(const String& name);
    std::vector<log4j::Appender*> getAllAppenders();
    bool isAttached(log4j::Appender* appender);
    // Calls the appenders of this logger and its ancestors (up to the first non-additive one).
    void callAppenders(log4j::LoggingEvent* event);
    void closeNestedAppenders() {}

    // Creates the LoggingEvent and dispatches it (overridden by commons' ThrowableAsMessageLogger).
    // Already rewrites (Throwable message, null t) into (message.getLocalizedMessage(), message).
    // The call site location of the current logging call is attached to the event.
    virtual void forcedLog(String fqcn, log4j::Level* level, Object* message, Throwable* t);

    // LoggingEvent.fqnOfCategoryClass of events created by the logging methods.
    static inline const String FQCN = String("org.apache.log4j.Logger");

private:
    friend class log4j::LogManager;
    friend struct LoggerInternals;
    void logAt(const String& fqcn, log4j::Level* level, const log4j::LogMessage& message, Throwable* t,
               const std::source_location& loc);
    String name_;
    std::atomic<log4j::Level*> level_{nullptr};
    std::atomic<Logger*> parent_{nullptr};
    std::atomic<bool> additive_{true};
    std::mutex appenderMu_;
    // Copy-on-write list: replaced (never mutated) under appenderMu_, read without locking.
    std::atomic<std::vector<log4j::Appender*>*> appenders_{nullptr};
};

}  // namespace jlang

namespace jlang::log4j {

using Logger = ::jlang::Logger;
using Category = ::jlang::Logger;  // org.apache.log4j.Category
using Priority = Level;            // org.apache.log4j.Priority

// ---------------------------------------------------------------------------------------
// org.apache.log4j.spi.LocationInfo (built from the std::source_location of the call).
class LocationInfo : public virtual Object {
public:
    static inline const String NA = String("?");
    LocationInfo(const String& fileName, const String& className, const String& methodName,
                 const String& lineNumber);
    static LocationInfo* fromSourceLocation(const std::source_location& loc);
    String getClassName() { return className_; }
    String getFileName() { return fileName_; }
    String getLineNumber() { return lineNumber_; }
    String getMethodName() { return methodName_; }
    // "a.b.C.method(File.cpp:12)"
    String fullInfo;

private:
    String className_, fileName_, methodName_, lineNumber_;
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.spi.ThrowableInformation
class ThrowableInformation : public virtual Object {
public:
    explicit ThrowableInformation(Throwable* throwable);
    explicit ThrowableInformation(Array<String>* rep);
    Throwable* getThrowable() { return throwable_; }
    // The lines of Java's printStackTrace(): "java.lang.X: msg", "\tat <frame>",
    // "Caused by: ...", "\t... n more".
    Array<String>* getThrowableStrRep();

private:
    Throwable* throwable_ = nullptr;
    Array<String>* rep_ = nullptr;
    std::mutex mu_;
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.spi.LoggingEvent
class LoggingEvent : public virtual Object {
public:
    LoggingEvent(const String& fqnOfCategoryClass, Logger* logger, Level* level, Object* message,
                 Throwable* throwable);
    LoggingEvent(const String& fqnOfCategoryClass, Logger* logger, int64_t timeStamp, Level* level,
                 Object* message, Throwable* throwable);

    // Java public fields.
    String fqnOfCategoryClass;
    String categoryName;
    Level* level;
    int64_t timeStamp;

    Level* getLevel() { return level; }
    String getLoggerName() { return categoryName; }
    Logger* getLogger() { return logger_; }
    // The message object: a jlang::StringBox for String messages (unbox with
    // jlang::unbox<jlang::String>(m)), any other Object as given, nullptr for null.
    Object* getMessage() { return message_; }
    // The String, or toString() of the object (a throwing toString() yields the exception's
    // toString(), like log4j's DefaultRenderer); a null String for a null message.
    String getRenderedMessage();
    ThrowableInformation* getThrowableInformation() { return throwableInfo_; }
    Array<String>* getThrowableStrRep();
    String getThreadName();
    int64_t getTimeStamp() { return timeStamp; }
    LocationInfo* getLocationInformation();
    bool locationInformationExists() { return location_ != nullptr || hasSource_; }
    String getNDC() { return nullptr; }  // NDC/MDC are not used by the code base
    Object* getMDC(const String& key) {
        (void)key;
        return nullptr;
    }
    // Time at which the logging system started (%r is relative to it).
    static int64_t getStartTime();

    // jlang: the call site (set by Logger::forcedLog; location info is derived lazily).
    void setSourceLocation(const std::source_location& loc) {
        source_ = loc;
        hasSource_ = true;
    }

private:
    Logger* logger_;
    Object* message_;
    String rendered_;
    bool isRendered_ = false;
    ThrowableInformation* throwableInfo_ = nullptr;
    String threadName_;
    LocationInfo* location_ = nullptr;
    std::source_location source_;
    bool hasSource_ = false;
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.spi.OptionHandler + the jlang replacement for log4j's JavaBeans-based
// PropertySetter (used by DOMConfigurator for <param name="..." value="..."/>).
class OptionHandler : public virtual Object {
public:
    virtual void activateOptions() {}
    // Sets a configuration property. `name` is matched like log4j does (after
    // Introspector.decapitalize: "File" == "file", "ConversionPattern" == "conversionPattern",
    // but "FILE" matches nothing): use optionIs(name, "file"). Returns false for an unknown
    // property ("No such property" warning). Throw IllegalArgumentException for a value that
    // cannot be converted (the helpers below do). Overrides call the base class for the rest.
    virtual bool setOption(String name, String value);

    static bool optionIs(const String& name, const char* property);
    static int32_t toIntOption(const String& value);   // Integer.valueOf(value.trim())
    static int64_t toLongOption(const String& value);  // Long.valueOf(value.trim())
    static bool toBoolOption(const String& value);     // "true"/"false" (case-insensitive, trimmed)
    static Level* toLevelOption(const String& value);  // OptionConverter.toLevel(value, DEBUG)
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.spi.Filter. decide() returns DENY, NEUTRAL or ACCEPT.
class Filter : public virtual OptionHandler {
public:
    static constexpr int32_t DENY = -1;
    static constexpr int32_t NEUTRAL = 0;
    static constexpr int32_t ACCEPT = 1;

    virtual int32_t decide(LoggingEvent* event) = 0;
    void activateOptions() override {}
    virtual void setNext(Filter* next) { this->next = next; }
    virtual Filter* getNext() { return next; }

    Filter* next = nullptr;  // Java: public Filter next
};

// org.apache.log4j.varia.DenyAllFilter
class DenyAllFilter : public Filter {
public:
    int32_t decide(LoggingEvent* event) override {
        (void)event;
        return DENY;
    }
};

// org.apache.log4j.varia.LevelMatchFilter (LevelToMatch, AcceptOnMatch)
class LevelMatchFilter : public Filter {
public:
    int32_t decide(LoggingEvent* event) override;
    bool setOption(String name, String value) override;
    void setLevelToMatch(const String& level);
    String getLevelToMatch();
    void setAcceptOnMatch(bool v) { acceptOnMatch_ = v; }
    bool getAcceptOnMatch() { return acceptOnMatch_; }

private:
    Level* levelToMatch_ = nullptr;
    bool acceptOnMatch_ = true;
};

// org.apache.log4j.varia.LevelRangeFilter (LevelMin, LevelMax, AcceptOnMatch)
class LevelRangeFilter : public Filter {
public:
    int32_t decide(LoggingEvent* event) override;
    bool setOption(String name, String value) override;
    void setLevelMin(Level* l) { levelMin_ = l; }
    Level* getLevelMin() { return levelMin_; }
    void setLevelMax(Level* l) { levelMax_ = l; }
    Level* getLevelMax() { return levelMax_; }
    void setAcceptOnMatch(bool v) { acceptOnMatch_ = v; }
    bool getAcceptOnMatch() { return acceptOnMatch_; }

private:
    Level* levelMin_ = nullptr;
    Level* levelMax_ = nullptr;
    bool acceptOnMatch_ = false;
};

// org.apache.log4j.varia.StringMatchFilter (StringToMatch, AcceptOnMatch)
class StringMatchFilter : public Filter {
public:
    int32_t decide(LoggingEvent* event) override;
    bool setOption(String name, String value) override;
    void setStringToMatch(const String& s) { stringToMatch_ = s; }
    String getStringToMatch() { return stringToMatch_; }
    void setAcceptOnMatch(bool v) { acceptOnMatch_ = v; }
    bool getAcceptOnMatch() { return acceptOnMatch_; }

private:
    String stringToMatch_;
    bool acceptOnMatch_ = true;
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.Layout
class Layout : public virtual OptionHandler {
public:
    static inline const String LINE_SEP = String("\n");
    static constexpr int32_t LINE_SEP_LEN = 1;
    virtual String format(LoggingEvent* event) = 0;
    virtual String getContentType() { return String("text/plain"); }
    virtual String getHeader() { return nullptr; }
    virtual String getFooter() { return nullptr; }
    // true: the appender prints the throwable's stack trace lines after the formatted event.
    virtual bool ignoresThrowable() = 0;
};

// org.apache.log4j.SimpleLayout: "LEVEL - message\n"
class SimpleLayout : public Layout {
public:
    String format(LoggingEvent* event) override;
    bool ignoresThrowable() override { return true; }
};

namespace impl {
class PatternConverter;
}

// org.apache.log4j.PatternLayout (ConversionPattern): %c{n} %C{n} %d{fmt} %F %l %L %m %M %n %p
// %r %t %x %X{key} %% with format modifiers (%-5p %.10c %20.30c). %d takes a
// java.text.SimpleDateFormat pattern or ISO8601 / ABSOLUTE / DATE and formats in the local
// time zone. A null message prints as "".
class PatternLayout : public Layout {
public:
    static inline const String DEFAULT_CONVERSION_PATTERN = String("%m%n");
    static inline const String TTCC_CONVERSION_PATTERN = String("%r [%t] %p %c %x - %m%n");

    PatternLayout();
    explicit PatternLayout(const String& pattern);
    virtual void setConversionPattern(String conversionPattern);
    virtual String getConversionPattern();
    void activateOptions() override {}
    String format(LoggingEvent* event) override;
    bool ignoresThrowable() override { return true; }
    bool setOption(String name, String value) override;  // ConversionPattern

protected:
    PatternLayout(bool enhanced, const String& pattern);
    void compile(const String& pattern);
    bool enhanced_ = false;
    std::atomic<bool> handlesExceptions_{false};
    String pattern_;
    std::atomic<impl::PatternConverter*> head_{nullptr};
};

// org.apache.log4j.EnhancedPatternLayout (log4j 1.2.16): conversion words as well (%logger
// %level %message %date{fmt}{tz} %thread %class %method %file %line %relative %ndc
// %properties{key} %throwable{none|short|n}), %c{-n} and %c{1.} abbreviations; null
// message/NDC print as "null". ignoresThrowable() is false when the pattern uses %throwable.
class EnhancedPatternLayout : public PatternLayout {
public:
    EnhancedPatternLayout();
    explicit EnhancedPatternLayout(const String& pattern);
    // Like log4j 1.2.16, applies OptionConverter.convertSpecialChars ("\\t" -> tab) first
    // (the constructor does not).
    void setConversionPattern(String conversionPattern) override;
    bool ignoresThrowable() override { return !handlesExceptions_.load(); }
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.spi.ErrorHandler; the default is OnlyOnceErrorHandler (reports the first
// error with LogLog.error, ignores the rest).
class ErrorHandler : public virtual OptionHandler {
public:
    virtual void error(String message, Throwable* e, int32_t errorCode) = 0;
    virtual void error(String message) = 0;
};

class OnlyOnceErrorHandler : public ErrorHandler {
public:
    void error(String message, Throwable* e, int32_t errorCode) override;
    void error(String message) override;

private:
    std::atomic<bool> firstTime_{true};
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.Appender
class Appender : public virtual Object {
public:
    virtual void addFilter(Filter* newFilter) = 0;
    virtual Filter* getFilter() = 0;
    virtual void clearFilters() = 0;
    virtual void close() = 0;
    virtual void doAppend(LoggingEvent* event) = 0;
    virtual String getName() = 0;
    virtual void setName(String name) = 0;
    virtual void setErrorHandler(ErrorHandler* errorHandler) = 0;
    virtual ErrorHandler* getErrorHandler() = 0;
    virtual void setLayout(Layout* layout) = 0;
    virtual Layout* getLayout() = 0;
    virtual bool requiresLayout() = 0;
};

// org.apache.log4j.AppenderSkeleton: name, threshold, layout, filter chain, error handler.
// doAppend: closed check, threshold, filters (DENY stops, ACCEPT skips the remaining filters),
// then append(), all under JSYNC(this) like Java's synchronized doAppend.
class AppenderSkeleton : public virtual Appender, public virtual OptionHandler {
public:
    AppenderSkeleton();
    explicit AppenderSkeleton(bool isActive);

    void activateOptions() override {}
    void addFilter(Filter* newFilter) override;
    Filter* getFilter() override { return headFilter; }
    Filter* getFirstFilter() { return headFilter; }
    void clearFilters() override;
    void doAppend(LoggingEvent* event) override;
    String getName() override { return name; }
    void setName(String name) override { this->name = name; }
    ErrorHandler* getErrorHandler() override { return errorHandler; }
    void setErrorHandler(ErrorHandler* eh) override;
    Layout* getLayout() override { return layout; }
    void setLayout(Layout* layout) override { this->layout = layout; }
    Level* getThreshold() { return threshold; }
    void setThreshold(Level* threshold) { this->threshold = threshold; }
    bool isAsSevereAsThreshold(Level* priority);
    bool setOption(String name, String value) override;  // Threshold, Name

    // Subclass responsibility (protected abstract in Java).
    virtual void append(LoggingEvent* event) = 0;

    // Java protected fields.
    Layout* layout = nullptr;
    String name;
    Level* threshold = nullptr;
    ErrorHandler* errorHandler;
    Filter* headFilter = nullptr;
    Filter* tailFilter = nullptr;
    bool closed = false;
};

// org.apache.log4j.WriterAppender: writes layout->format(event) to its output (a file or a
// jlang::PrintStream), then the throwable's stack trace lines when the layout ignores
// throwables; flushes after each event while ImmediateFlush (default true).
class WriterAppender : public AppenderSkeleton {
public:
    WriterAppender();
    void setImmediateFlush(bool value) { immediateFlush = value; }
    bool getImmediateFlush() { return immediateFlush; }
    // The output is UTF-8; any other encoding is applied with String::getBytes(encoding).
    void setEncoding(const String& value) { encoding = value; }
    String getEncoding() { return encoding; }
    void activateOptions() override {}
    void append(LoggingEvent* event) override;
    void close() override;
    bool requiresLayout() override { return true; }
    bool setOption(String name, String value) override;  // ImmediateFlush, Encoding

    // Java protected API.
    virtual bool checkEntryConditions();
    virtual void subAppend(LoggingEvent* event);
    virtual void reset();
    virtual void closeWriter();
    virtual void writeHeader();
    virtual void writeFooter();
    bool shouldFlush(LoggingEvent* event) {
        (void)event;
        return immediateFlush;
    }

    // jlang: output targets (replace Java's setWriter/setQWForFiles).
    // A C stdio stream (fclose()d by closeWriter when `owned`), or a PrintStream (never closed).
    void setOutput(std::FILE* stream, bool owned);
    void setOutput(PrintStream* stream);
    bool hasOutput() { return file_ != nullptr || printStream_ != nullptr; }
    void write(const String& s);
    void flush();
    // Bytes written since the output was set, plus setCount (RollingFileAppender).
    int64_t getCount() { return count_; }
    void setCount(int64_t c) { count_ = c; }

    bool immediateFlush = true;
    String encoding;

private:
    std::FILE* file_ = nullptr;
    bool owned_ = false;
    PrintStream* printStream_ = nullptr;
    int64_t count_ = 0;
    bool writeErrorReported_ = false;
};

// org.apache.log4j.ConsoleAppender: Target "System.out" (default) or "System.err" (the
// jlang::System streams current at activateOptions(), or followed when Follow is true).
class ConsoleAppender : public WriterAppender {
public:
    static inline const String SYSTEM_OUT = String("System.out");
    static inline const String SYSTEM_ERR = String("System.err");
    ConsoleAppender();
    explicit ConsoleAppender(Layout* layout);
    ConsoleAppender(Layout* layout, const String& target);
    void setTarget(const String& value);
    String getTarget() { return target_; }
    void setFollow(bool newValue) { follow_ = newValue; }
    bool getFollow() { return follow_; }
    void activateOptions() override;
    void subAppend(LoggingEvent* event) override;
    bool setOption(String name, String value) override;  // Target, Follow

private:
    String target_ = SYSTEM_OUT;
    bool follow_ = false;
};

// org.apache.log4j.FileAppender (File, Append, BufferedIO, BufferSize + WriterAppender's
// options). Subclassed by commons' TruncateToZipFileAppender, which overrides the 4-argument
// setFile.
class FileAppender : public WriterAppender {
public:
    FileAppender();
    // IOException (FileNotFoundException) when the file cannot be opened.
    FileAppender(Layout* layout, const String& filename, bool append, bool bufferedIO, int32_t bufferSize);
    FileAppender(Layout* layout, const String& filename, bool append);
    FileAppender(Layout* layout, const String& filename);

    // Bean setter: records the trimmed file name; activateOptions() opens it.
    virtual void setFile(String file);
    // Opens the file (creating missing parent directories), after closing the current one.
    // Throws IOException/FileNotFoundException. (synchronized in Java)
    virtual void setFile(String fileName, bool append, bool bufferedIO, int32_t bufferSize);
    String getFile() { return fileName; }
    bool getAppend() { return fileAppend; }
    void setAppend(bool flag) { fileAppend = flag; }
    bool getBufferedIO() { return bufferedIO; }
    void setBufferedIO(bool bufferedIO);
    int32_t getBufferSize() { return bufferSize; }
    void setBufferSize(int32_t bufferSize) { this->bufferSize = bufferSize; }
    // Opens the file set with setFile(String) (errors go to the ErrorHandler, as in Java).
    void activateOptions() override;
    bool setOption(String name, String value) override;

    // Java protected API.
    virtual void closeFile();
    void reset() override;

    bool fileAppend = true;
    String fileName;
    bool bufferedIO = false;
    int32_t bufferSize = 8 * 1024;
};

// org.apache.log4j.RollingFileAppender (MaxFileSize "10MB"/"500KB", MaximumFileSize,
// MaxBackupIndex): file -> file.1 -> ... -> file.N when the file grows past MaxFileSize.
class RollingFileAppender : public FileAppender {
public:
    RollingFileAppender();
    RollingFileAppender(Layout* layout, const String& filename, bool append);
    RollingFileAppender(Layout* layout, const String& filename);
    int32_t getMaxBackupIndex() { return maxBackupIndex; }
    int64_t getMaximumFileSize() { return maxFileSize; }
    void setMaxBackupIndex(int32_t maxBackups) { maxBackupIndex = maxBackups; }
    void setMaximumFileSize(int64_t maxFileSize) { this->maxFileSize = maxFileSize; }
    void setMaxFileSize(const String& value);
    virtual void rollOver();
    using FileAppender::setFile;
    void setFile(String fileName, bool append, bool bufferedIO, int32_t bufferSize) override;
    bool setOption(String name, String value) override;
    void subAppend(LoggingEvent* event) override;

    int64_t maxFileSize = 10 * 1024 * 1024;
    int32_t maxBackupIndex = 1;

private:
    int64_t nextRollover_ = 0;
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.LogManager (with the parts of Hierarchy that the code base reaches).
class LogManager {
public:
    LogManager() = delete;
    static Logger* getRootLogger();
    static Logger* getLogger(const String& name);
    static Logger* getLogger(Class* clazz);
    static Logger* getLogger(const String& name, LoggerFactory* factory);
    static Logger* exists(const String& name);
    static std::vector<Logger*> getCurrentLoggers();
    // Closes and removes the appenders of every logger.
    static void shutdown();
    // Hierarchy.resetConfiguration(): root level DEBUG, all appenders closed and removed,
    // every other logger's level null and additivity true, threshold ALL.
    static void resetConfiguration();
    // Repository threshold (<log4j:configuration threshold="...">).
    static void setThreshold(Level* level);
    static Level* getThreshold();
    // jlang replacement for commons' LoggingService reflection hack on
    // Hierarchy.defaultFactory: the factory getLogger(name) uses for loggers created from now
    // on (nullptr = plain jlang::Logger).
    static void setDefaultLoggerFactory(LoggerFactory* factory);
    static LoggerFactory* getDefaultLoggerFactory();
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.helpers.LogLog: log4j's own diagnostics. debug -> System.out "log4j: "
// (only when internal debugging is on: setInternalDebugging, <log4j:configuration
// debug="true">, or the system property log4j.debug), warn -> System.err "log4j:WARN ",
// error -> System.err "log4j:ERROR "; a throwable is printed as a stack trace below.
class LogLog {
public:
    LogLog() = delete;
    static inline const String DEBUG_KEY = String("log4j.debug");
    static void setInternalDebugging(bool enabled);
    static bool isDebugEnabled();
    static void setQuietMode(bool quietMode);
    static void debug(const String& msg);
    static void debug(const String& msg, const Throwable& t);
    static void debug(const String& msg, Throwable* t);
    static void warn(const String& msg);
    static void warn(const String& msg, const Throwable& t);
    static void warn(const String& msg, Throwable* t);
    static void error(const String& msg);
    static void error(const String& msg, const Throwable& t);
    static void error(const String& msg, Throwable* t);

    // jlang (tests): redirects LogLog's output. The sink receives each line (prefix included,
    // no newline) and whether it goes to stderr. nullptr restores System.out/System.err.
    using Sink = std::function<void(const String& line, bool toStderr)>;
    static void setSink(Sink sink);
};

// ---------------------------------------------------------------------------------------
// org.apache.log4j.xml.DOMConfigurator: applies a log4j.xml (<log4j:configuration> with
// appender, layout, filter, param, errorHandler, logger/category, root, categoryFactory/
// loggerFactory elements, ${property} substitution, debug/threshold/reset attributes).
// Appenders are created when first referenced by an <appender-ref>. Like log4j it never throws
// for a missing or malformed file (LogLog.error "Could not parse file [...]."); classes that
// are not registered are skipped with a LogLog warning. Errors (jlang::Error subclasses, e.g.
// commons' AppenderInitializationError) thrown while an appender is set up propagate.
class DOMConfigurator {
public:
    DOMConfigurator() = delete;
    static void configure(const String& filename);
    // jlang: configure from XML text.
    static void configureFromXml(const String& xml);
    // Java configureAndWatch: configures once (no file watchdog).
    static void configureAndWatch(const String& filename);
    static void configureAndWatch(const String& filename, int64_t delay);
};

// ---------------------------------------------------------------------------------------
// Class factories used by DOMConfigurator, keyed by the Java class name written in log4j.xml.
// Pre-registered: org.apache.log4j.ConsoleAppender, FileAppender, RollingFileAppender,
// PatternLayout, EnhancedPatternLayout, SimpleLayout, varia.DenyAllFilter,
// varia.LevelMatchFilter, varia.LevelRangeFilter, varia.StringMatchFilter,
// helpers.OnlyOnceErrorHandler. Safe from static initializers; a later registration of the
// same name replaces the earlier one. Unregistered names fall back to jlang::Class::forName.
using AppenderFactoryFn = std::function<Appender*()>;
using FilterFactoryFn = std::function<Filter*()>;
using LayoutFactoryFn = std::function<Layout*()>;
using LoggerFactoryFactoryFn = std::function<LoggerFactory*()>;
void registerAppenderFactory(const String& className, AppenderFactoryFn factory);
void registerFilterFactory(const String& className, FilterFactoryFn factory);
void registerLayoutFactory(const String& className, LayoutFactoryFn factory);
void registerLoggerFactoryFactory(const String& className, LoggerFactoryFactoryFn factory);

// JavaBeans setters for <param name="property" value="..."/> of a (ported) class, used before
// OptionHandler::setOption. `property` is the bean property name ("backupDir" for
// setBackupDir); a setter registered for a class also applies to its subclasses (through the
// class registry's superclass links). Throw IllegalArgumentException for a bad value (the
// OptionHandler::to*Option helpers do). Example, in TruncateToZipFileAppender.cpp:
//   static const bool _log4jProps = (jlang::log4j::registerPropertySetter(
//       "org.openaion.commons.log4j.appenders.TruncateToZipFileAppender", "backupDir",
//       [](jlang::Object* o, const jlang::String& v) { jlang::cast<TruncateToZipFileAppender>(o)->setBackupDir(v); }),
//       true);
using PropertySetterFn = std::function<void(Object* target, const String& value)>;
void registerPropertySetter(const String& className, const String& property, PropertySetterFn setter);

}  // namespace jlang::log4j
