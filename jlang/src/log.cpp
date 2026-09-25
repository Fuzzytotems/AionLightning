// log4j emulation, part 1: Level, jlang::Logger and the logger repository (log4j's Hierarchy),
// LogManager, LoggingEvent, ThrowableInformation, LocationInfo, LogLog and the OptionHandler
// helpers. See <jlang/Log.h>.
#include <jlang/Log.h>
#include <jlang/Thread.h>

#include "log_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>
#include <unordered_map>

#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace jlang {

struct LoggerInternals {
    static void setParent(Logger* l, Logger* p) { l->parent_.store(p, std::memory_order_release); }
    static void setLevel(Logger* l, log4j::Level* v) { l->level_.store(v, std::memory_order_release); }
    static const String& name(Logger* l) { return l->name_; }
};

namespace {

// ---------------------------------------------------------------------------------------
// Call-site location of the logging call in progress on this thread (set by the logging
// methods around forcedLog, so that an overriding forcedLog keeps Java's signature). Holds
// only pointers to static strings: nothing for the collector to see.
thread_local std::source_location tl_loc;
thread_local bool tl_hasLoc = false;

struct CallSiteScope {
    std::source_location prev;
    bool prevHas;
    explicit CallSiteScope(const std::source_location& loc) : prev(tl_loc), prevHas(tl_hasLoc) {
        tl_loc = loc;
        tl_hasLoc = true;
    }
    ~CallSiteScope() {
        tl_loc = prev;
        tl_hasLoc = prevHas;
    }
    CallSiteScope(const CallSiteScope&) = delete;
    CallSiteScope& operator=(const CallSiteScope&) = delete;
};

// ---------------------------------------------------------------------------------------
// The logger repository (log4j Hierarchy): name -> Logger, provision nodes for loggers created
// before their ancestors, the root logger, the threshold and the default logger factory.
struct Repository {
    std::recursive_mutex mu;
    std::unordered_map<std::string, Logger*> loggers;
    std::unordered_map<std::string, std::vector<Logger*>> provision;
    std::vector<Logger*> order;  // creation order (getCurrentLoggers)
    Logger* root = nullptr;
    std::atomic<int32_t> thresholdInt{log4j::Level::ALL_INT};
    std::atomic<log4j::Level*> threshold{nullptr};
    std::atomic<log4j::LoggerFactory*> defaultFactory{nullptr};
    std::atomic<bool> emittedNoAppenderWarning{false};
};

Repository& repo() {
    static Repository* r = [] {
        auto* x = new Repository();
        x->threshold.store(log4j::Level::builtin(7));
        Logger* root = new Logger(String("root"));
        // jlang: an unconfigured log4j logs nothing ("No appenders could be found"); here the
        // root logger starts at INFO with a console appender, replaced by the configuration.
        LoggerInternals::setLevel(root, log4j::Level::builtin(4));
        auto* layout = new log4j::EnhancedPatternLayout(String("[%p] %d{yyyy-MM-dd HH:mm:ss} - %m%n"));
        auto* console = new log4j::ConsoleAppender(layout);
        console->setName(String("jlang-default-console"));
        root->addAppender(console);
        x->root = root;
        return x;
    }();
    return *r;
}

// %r is relative to the start of the program: fix the start time during static initialization.
[[maybe_unused]] const int64_t g_startTimeInit = log4j::LoggingEvent::getStartTime();

}  // namespace

namespace {

void updateParents(Repository& r, Logger* cat) {
    const std::string& name = LoggerInternals::name(cat);
    bool parentFound = false;
    if (!name.empty()) {
        size_t i = name.rfind('.', name.size() - 1);
        while (i != std::string::npos) {
            std::string sub = name.substr(0, i);
            auto it = r.loggers.find(sub);
            if (it != r.loggers.end()) {
                parentFound = true;
                LoggerInternals::setParent(cat, it->second);
                break;
            }
            r.provision[sub].push_back(cat);
            if (i == 0) break;
            i = name.rfind('.', i - 1);
        }
    }
    if (!parentFound) LoggerInternals::setParent(cat, r.root);
}

void updateChildren(std::vector<Logger*>& children, Logger* logger) {
    const std::string& lname = LoggerInternals::name(logger);
    for (Logger* l : children) {
        Logger* p = l->getParent();
        // Unless this child already points to a correct (lower) parent, insert the new logger.
        if (p != nullptr && std::string_view(LoggerInternals::name(p)).substr(0, lname.size()) != lname) {
            LoggerInternals::setParent(logger, p);
            LoggerInternals::setParent(l, logger);
        }
    }
}

Logger* getLoggerImpl(const String& name, log4j::LoggerFactory* factory) {
    Repository& r = repo();
    std::lock_guard<std::recursive_mutex> g(r.mu);
    auto it = r.loggers.find(name);
    if (it != r.loggers.end()) return it->second;
    Logger* logger = factory != nullptr ? factory->makeNewLoggerInstance(name) : new Logger(name);
    if (logger == nullptr) logger = new Logger(name);
    it = r.loggers.find(name);  // the factory may have created it recursively
    if (it != r.loggers.end()) return it->second;
    r.loggers.emplace(name, logger);
    r.order.push_back(logger);
    auto pit = r.provision.find(name);
    if (pit != r.provision.end()) {
        std::vector<Logger*> children = std::move(pit->second);
        r.provision.erase(pit);
        updateChildren(children, logger);
    }
    updateParents(r, logger);
    return logger;
}

inline bool repoDisabled(int32_t level) {
    return repo().thresholdInt.load(std::memory_order_relaxed) > level;
}

void emitNoAppenderWarning(Logger* cat) {
    Repository& r = repo();
    if (!r.emittedNoAppenderWarning.exchange(true)) {
        log4j::LogLog::warn(str("No appenders could be found for logger (", cat->getName(), ")."));
        log4j::LogLog::warn(String("Please initialize the log4j system properly."));
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Logger

Logger::Logger(const String& name) : name_(name) {}

Logger* Logger::getLogger(const String& name) {
    return getLoggerImpl(name, repo().defaultFactory.load(std::memory_order_acquire));
}

Logger* Logger::getLogger(Class* clazz) {
    if (clazz == nullptr) throw NullPointerException();
    return getLogger(clazz->getName());
}

Logger* Logger::getLogger(const String& name, log4j::LoggerFactory* factory) {
    return getLoggerImpl(name, factory);
}

Logger* Logger::getRootLogger() { return repo().root; }

Logger* Logger::exists(const String& name) {
    Repository& r = repo();
    std::lock_guard<std::recursive_mutex> g(r.mu);
    auto it = r.loggers.find(name);
    return it == r.loggers.end() ? nullptr : it->second;
}

void Logger::logAt(const String& fqcn, log4j::Level* level, const log4j::LogMessage& message, Throwable* t,
                   const std::source_location& loc) {
    if (level == nullptr) throw NullPointerException();
    if (repoDisabled(level->toInt())) return;
    log4j::Level* eff = getEffectiveLevel();
    if (eff == nullptr || !level->isGreaterOrEqual(eff)) return;
    CallSiteScope scope(loc);
    forcedLog(fqcn, level, message.toObject(), t);
}

#define JLANG_LOG4J_LEVEL_IMPL(method, idx)                                                               \
    void Logger::method(const log4j::LogMessage& message, const std::source_location& loc) {              \
        logAt(FQCN, log4j::Level::builtin(idx), message, nullptr, loc);                                   \
    }                                                                                                     \
    void Logger::method(const log4j::LogMessage& message, const Throwable& t, const std::source_location& loc) { \
        logAt(FQCN, log4j::Level::builtin(idx), message, const_cast<Throwable*>(&t), loc);                \
    }                                                                                                     \
    void Logger::method(const log4j::LogMessage& message, Throwable* t, const std::source_location& loc) { \
        logAt(FQCN, log4j::Level::builtin(idx), message, t, loc);                                         \
    }
JLANG_LOG4J_LEVEL_IMPL(trace, 6)
JLANG_LOG4J_LEVEL_IMPL(debug, 5)
JLANG_LOG4J_LEVEL_IMPL(info, 4)
JLANG_LOG4J_LEVEL_IMPL(warn, 3)
JLANG_LOG4J_LEVEL_IMPL(error, 2)
JLANG_LOG4J_LEVEL_IMPL(fatal, 1)
#undef JLANG_LOG4J_LEVEL_IMPL

void Logger::log(log4j::Level* priority, const log4j::LogMessage& message, const std::source_location& loc) {
    logAt(FQCN, priority, message, nullptr, loc);
}

void Logger::log(log4j::Level* priority, const log4j::LogMessage& message, const Throwable& t,
                 const std::source_location& loc) {
    logAt(FQCN, priority, message, const_cast<Throwable*>(&t), loc);
}

void Logger::log(log4j::Level* priority, const log4j::LogMessage& message, Throwable* t,
                 const std::source_location& loc) {
    logAt(FQCN, priority, message, t, loc);
}

void Logger::log(const String& callerFQCN, log4j::Level* level, const log4j::LogMessage& message, Throwable* t,
                 const std::source_location& loc) {
    logAt(callerFQCN, level, message, t, loc);
}

void Logger::assertLog(bool assertion, const String& msg, const std::source_location& loc) {
    if (!assertion) logAt(FQCN, log4j::Level::builtin(2), msg, nullptr, loc);
}

bool Logger::isTraceEnabled() { return isEnabledFor(log4j::Level::builtin(6)); }
bool Logger::isDebugEnabled() { return isEnabledFor(log4j::Level::builtin(5)); }
bool Logger::isInfoEnabled() { return isEnabledFor(log4j::Level::builtin(4)); }

bool Logger::isEnabledFor(log4j::Level* level) {
    if (level == nullptr) throw NullPointerException();
    if (repoDisabled(level->toInt())) return false;
    log4j::Level* eff = getEffectiveLevel();
    return eff != nullptr && level->isGreaterOrEqual(eff);
}

void Logger::setLevel(log4j::Level* level) {
    if (level == nullptr && this == repo().root) {
        Throwable where;
        log4j::LogLog::error(String("You have tried to set a null level to root."), where);
        return;
    }
    level_.store(level, std::memory_order_release);
}

log4j::Level* Logger::getEffectiveLevel() {
    for (Logger* c = this; c != nullptr; c = c->getParent()) {
        log4j::Level* l = c->getLevel();
        if (l != nullptr) return l;
    }
    return nullptr;
}

void Logger::addAppender(log4j::Appender* newAppender) {
    if (newAppender == nullptr) return;
    std::lock_guard<std::mutex> g(appenderMu_);
    auto* cur = appenders_.load(std::memory_order_acquire);
    if (cur != nullptr && std::find(cur->begin(), cur->end(), newAppender) != cur->end()) return;
    auto* next = cur != nullptr ? new std::vector<log4j::Appender*>(*cur) : new std::vector<log4j::Appender*>();
    next->push_back(newAppender);
    appenders_.store(next, std::memory_order_release);
}

void Logger::removeAppender(log4j::Appender* appender) {
    if (appender == nullptr) return;
    std::lock_guard<std::mutex> g(appenderMu_);
    auto* cur = appenders_.load(std::memory_order_acquire);
    if (cur == nullptr) return;
    auto* next = new std::vector<log4j::Appender*>();
    for (auto* a : *cur)
        if (a != appender) next->push_back(a);
    appenders_.store(next, std::memory_order_release);
}

void Logger::removeAppender(const String& name) {
    if (name == nullptr) return;
    std::lock_guard<std::mutex> g(appenderMu_);
    auto* cur = appenders_.load(std::memory_order_acquire);
    if (cur == nullptr) return;
    auto* next = new std::vector<log4j::Appender*>();
    bool removed = false;
    for (auto* a : *cur) {
        if (!removed && name.equals(a->getName())) {
            removed = true;
            continue;
        }
        next->push_back(a);
    }
    appenders_.store(next, std::memory_order_release);
}

void Logger::removeAllAppenders() {
    std::vector<log4j::Appender*>* cur;
    {
        std::lock_guard<std::mutex> g(appenderMu_);
        cur = appenders_.exchange(nullptr, std::memory_order_acq_rel);
    }
    if (cur != nullptr)
        for (auto* a : *cur) a->close();
}

log4j::Appender* Logger::getAppender(const String& name) {
    auto* cur = appenders_.load(std::memory_order_acquire);
    if (cur == nullptr || name == nullptr) return nullptr;
    for (auto* a : *cur)
        if (name.equals(a->getName())) return a;
    return nullptr;
}

std::vector<log4j::Appender*> Logger::getAllAppenders() {
    auto* cur = appenders_.load(std::memory_order_acquire);
    return cur != nullptr ? *cur : std::vector<log4j::Appender*>();
}

bool Logger::isAttached(log4j::Appender* appender) {
    auto* cur = appenders_.load(std::memory_order_acquire);
    return appender != nullptr && cur != nullptr && std::find(cur->begin(), cur->end(), appender) != cur->end();
}

void Logger::callAppenders(log4j::LoggingEvent* event) {
    int writes = 0;
    for (Logger* c = this; c != nullptr; c = c->getParent()) {
        auto* list = c->appenders_.load(std::memory_order_acquire);
        if (list != nullptr) {
            for (log4j::Appender* a : *list) {
                a->doAppend(event);
                writes++;
            }
        }
        if (!c->getAdditivity()) break;
    }
    if (writes == 0) emitNoAppenderWarning(this);
}

void Logger::forcedLog(String fqcn, log4j::Level* level, Object* message, Throwable* t) {
    // commons ThrowableAsMessageLogger: (Throwable message, null) -> (localized message, throwable)
    if (t == nullptr && message != nullptr) {
        if (auto* mt = dynamic_cast<Throwable*>(message)) {
            t = mt;
            message = box(mt->getLocalizedMessage());
        }
    }
    auto* event = new log4j::LoggingEvent(fqcn, this, level, message, t);
    if (tl_hasLoc) event->setSourceLocation(tl_loc);
    callAppenders(event);
}

}  // namespace jlang

namespace jlang::log4j {

// ---------------------------------------------------------------------------------------
// Level

Level::Level(int32_t level, const String& levelStr, int32_t syslogEquivalent)
    : level_(level), levelStr_(levelStr), syslogEquivalent_(syslogEquivalent) {}

Level* Level::builtin(int idx) {
    static Level* const levels[8] = {
        new Level(OFF_INT, String("OFF"), 0),     new Level(FATAL_INT, String("FATAL"), 0),
        new Level(ERROR_INT, String("ERROR"), 3), new Level(WARN_INT, String("WARN"), 4),
        new Level(INFO_INT, String("INFO"), 6),   new Level(DEBUG_INT, String("DEBUG"), 7),
        new Level(TRACE_INT, String("TRACE"), 7), new Level(ALL_INT, String("ALL"), 7),
    };
    return levels[idx];
}

bool Level::equals(Object* o) {
    auto* l = dynamic_cast<Level*>(o);
    return l != nullptr && l->level_ == level_;
}

Level* Level::toLevel(const String& sArg) { return toLevel(sArg, builtin(5)); }

Level* Level::toLevel(const String& sArg, Level* defaultLevel) {
    if (sArg == nullptr) return defaultLevel;
    String s = sArg.toUpperCase();
    if (s.equals("ALL")) return builtin(7);
    if (s.equals("DEBUG")) return builtin(5);
    if (s.equals("INFO")) return builtin(4);
    if (s.equals("WARN")) return builtin(3);
    if (s.equals("ERROR")) return builtin(2);
    if (s.equals("FATAL")) return builtin(1);
    if (s.equals("OFF")) return builtin(0);
    if (s.equals("TRACE")) return builtin(6);
    if (s.equals("\xC4\xB0NFO")) return builtin(4);  // Turkish dotted capital I (log4j bug 40937)
    return defaultLevel;
}

Level* Level::toLevel(int32_t val) { return toLevel(val, builtin(5)); }

Level* Level::toLevel(int32_t val, Level* defaultLevel) {
    switch (val) {
        case ALL_INT: return builtin(7);
        case DEBUG_INT: return builtin(5);
        case INFO_INT: return builtin(4);
        case WARN_INT: return builtin(3);
        case ERROR_INT: return builtin(2);
        case FATAL_INT: return builtin(1);
        case OFF_INT: return builtin(0);
        case TRACE_INT: return builtin(6);
        default: return defaultLevel;
    }
}

// ---------------------------------------------------------------------------------------
// LogMessage

Object* LogMessage::toObject() const {
    switch (kind_) {
        case STR: return new StringBox(*str_);
        case STD: return new StringBox(String(*std_));
        case CSTR: return new StringBox(String(cstr_));
        case OBJ: return obj_;
        case NUL: break;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------
// LocationInfo

namespace {

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
}

std::string stripTemplates(const std::string& s) {
    std::string out;
    int depth = 0;
    for (char c : s) {
        if (c == '<') depth++;
        else if (c == '>') {
            if (depth > 0) depth--;
        } else if (depth == 0) out.push_back(c);
    }
    return out;
}

}  // namespace

namespace impl {

// "void org::openaion::X::run()" -> {"org.openaion.X", "run"} (see Log.h).
void parseFunctionName(const char* fn, String& cls, String& method) {
    std::string s = fn != nullptr ? fn : "";
    // GCC " [with T = int]", Clang " [T = int]"
    if (!s.empty() && s.back() == ']') {
        size_t b = s.rfind(" [");
        if (b != std::string::npos) s.erase(b);
    }
    replaceAll(s, "(anonymous namespace)::", "");
    replaceAll(s, "{anonymous}::", "");
    replaceAll(s, "(anonymous class)", "{lambda}");
    // First '(' at template depth 0 that is not part of "operator()".
    int depth = 0;
    size_t paren = std::string::npos;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '<') {
            if (i >= 8 && s.compare(i - 8, 8, "operator") == 0) continue;
            depth++;
        } else if (c == '>') {
            if (depth > 0) depth--;
        } else if (c == '(' && depth == 0) {
            if (i >= 8 && s.compare(i - 8, 8, "operator") == 0 && i + 1 < s.size() && s[i + 1] == ')') {
                i++;
                continue;
            }
            paren = i;
            break;
        }
    }
    std::string prefix = paren == std::string::npos ? s : s.substr(0, paren);
    // Start of the qualified name: after the last ' ', '*' or '&' at template depth 0.
    size_t start = 0;
    depth = 0;
    for (size_t i = prefix.size(); i-- > 0;) {
        char c = prefix[i];
        if (c == '>') depth++;
        else if (c == '<') {
            if (depth > 0) depth--;
        } else if (depth == 0 && (c == ' ' || c == '*' || c == '&')) {
            start = i + 1;
            break;
        }
    }
    std::string qual = stripTemplates(prefix.substr(start));
    std::vector<std::string> parts;
    for (size_t p = 0;;) {
        size_t q = qual.find("::", p);
        std::string part = qual.substr(p, q == std::string::npos ? std::string::npos : q - p);
        if (!part.empty()) parts.push_back(part);
        if (q == std::string::npos) break;
        p = q + 2;
    }
    if (parts.empty()) {
        cls = LocationInfo::NA;
        method = LocationInfo::NA;
        return;
    }
    std::string m = parts.back();
    parts.pop_back();
    std::string c;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) c.push_back('.');
        c += parts[i];
    }
    if (!parts.empty() && m == parts.back()) m = "<init>";
    else if (m == "_clinit") m = "<clinit>";
    cls = c.empty() ? LocationInfo::NA : String(c);
    method = String(m);
}

}  // namespace impl

LocationInfo::LocationInfo(const String& fileName, const String& className, const String& methodName,
                           const String& lineNumber)
    : className_(className), fileName_(fileName), methodName_(methodName), lineNumber_(lineNumber) {
    fullInfo = str(className, ".", methodName, "(", fileName, ":", lineNumber, ")");
}

LocationInfo* LocationInfo::fromSourceLocation(const std::source_location& loc) {
    String cls, method;
    impl::parseFunctionName(loc.function_name(), cls, method);
    const char* f = loc.file_name();
    String file = NA;
    if (f != nullptr && *f != '\0') {
        const char* slash = std::strrchr(f, '/');
        file = String(slash != nullptr ? slash + 1 : f);
    }
    String line = loc.line() > 0 ? String::valueOf(static_cast<int64_t>(loc.line())) : NA;
    return new LocationInfo(file, cls, method, line);
}

// ---------------------------------------------------------------------------------------
// ThrowableInformation

namespace impl {

Throwable* retainThrowable(Throwable* t) {
    if (t == nullptr) return nullptr;
    if (::jlang::detail::inGcHeap(static_cast<Object*>(t))) return t;
    return t->copyThrowable();
}

void renderThrowable(Throwable* t, std::vector<String>& out) {
    if (t == nullptr) return;
    std::vector<String> trace = t->getStackTraceText();
    out.push_back(t->toString());
    for (const String& f : trace) out.push_back(str("\tat ", f));
    std::vector<Throwable*> seen{t};
    Throwable* cause = t->getCause();
    for (int depth = 0; cause != nullptr && depth < 100; depth++) {
        if (std::find(seen.begin(), seen.end(), cause) != seen.end()) {
            out.push_back(str("\t[CIRCULAR REFERENCE:", cause->toString(), "]"));
            break;
        }
        seen.push_back(cause);
        std::vector<String> ct = cause->getStackTraceText();
        int m = static_cast<int>(ct.size()) - 1;
        int n = static_cast<int>(trace.size()) - 1;
        while (m >= 0 && n >= 0 && ct[static_cast<size_t>(m)] == trace[static_cast<size_t>(n)]) {
            m--;
            n--;
        }
        int framesInCommon = static_cast<int>(ct.size()) - 1 - m;
        out.push_back(str("Caused by: ", cause->toString()));
        for (int i = 0; i <= m; i++) out.push_back(str("\tat ", ct[static_cast<size_t>(i)]));
        if (framesInCommon != 0) out.push_back(str("\t... ", framesInCommon, " more"));
        trace = std::move(ct);
        cause = cause->getCause();
    }
}

}  // namespace impl

ThrowableInformation::ThrowableInformation(Throwable* throwable) : throwable_(impl::retainThrowable(throwable)) {}

ThrowableInformation::ThrowableInformation(Array<String>* rep) : rep_(rep) {}

Array<String>* ThrowableInformation::getThrowableStrRep() {
    std::lock_guard<std::mutex> g(mu_);
    if (rep_ == nullptr) {
        std::vector<String> lines;
        impl::renderThrowable(throwable_, lines);
        rep_ = Array<String>::fromRange(lines.begin(), lines.end());
    }
    return rep_->clone();
}

// ---------------------------------------------------------------------------------------
// LoggingEvent

LoggingEvent::LoggingEvent(const String& fqn, Logger* logger, Level* level, Object* message, Throwable* throwable)
    : LoggingEvent(fqn, logger, System::currentTimeMillis(), level, message, throwable) {}

LoggingEvent::LoggingEvent(const String& fqn, Logger* logger, int64_t ts, Level* level, Object* message,
                           Throwable* throwable)
    : fqnOfCategoryClass(fqn), level(level), timeStamp(ts), logger_(logger), message_(message) {
    if (logger != nullptr) categoryName = logger->getName();
    if (auto* mt = dynamic_cast<Throwable*>(message)) message_ = impl::retainThrowable(mt);
    if (throwable != nullptr) throwableInfo_ = new ThrowableInformation(throwable);
}

String LoggingEvent::getRenderedMessage() {
    if (!isRendered_ && message_ != nullptr) {
        if (auto* sb = dynamic_cast<StringBox*>(message_)) {
            rendered_ = sb->value;
        } else {
            try {
                rendered_ = message_->toString();
            } catch (Exception& ex) {
                rendered_ = ex.toString();
            }
        }
        isRendered_ = true;
    }
    return rendered_;
}

Array<String>* LoggingEvent::getThrowableStrRep() {
    return throwableInfo_ == nullptr ? nullptr : throwableInfo_->getThrowableStrRep();
}

String LoggingEvent::getThreadName() {
    if (threadName_ == nullptr) threadName_ = impl::currentThreadName();
    return threadName_;
}

LocationInfo* LoggingEvent::getLocationInformation() {
    if (location_ == nullptr) {
        if (hasSource_) {
            location_ = LocationInfo::fromSourceLocation(source_);
        } else {
            location_ = new LocationInfo(LocationInfo::NA, LocationInfo::NA, LocationInfo::NA, LocationInfo::NA);
            location_->fullInfo = nullptr;
        }
    }
    return location_;
}

int64_t LoggingEvent::getStartTime() {
    static const int64_t start = System::currentTimeMillis();
    return start;
}

// ---------------------------------------------------------------------------------------
// LogManager

Logger* LogManager::getRootLogger() { return repo().root; }
Logger* LogManager::getLogger(const String& name) { return Logger::getLogger(name); }
Logger* LogManager::getLogger(Class* clazz) { return Logger::getLogger(clazz); }
Logger* LogManager::getLogger(const String& name, LoggerFactory* factory) { return getLoggerImpl(name, factory); }
Logger* LogManager::exists(const String& name) { return Logger::exists(name); }

std::vector<Logger*> LogManager::getCurrentLoggers() {
    Repository& r = repo();
    std::lock_guard<std::recursive_mutex> g(r.mu);
    return r.order;
}

void LogManager::shutdown() {
    Repository& r = repo();
    std::lock_guard<std::recursive_mutex> g(r.mu);
    r.root->removeAllAppenders();
    for (Logger* l : r.order) l->removeAllAppenders();
}

void LogManager::resetConfiguration() {
    Repository& r = repo();
    r.root->setLevel(Level::builtin(5));
    setThreshold(Level::builtin(7));
    std::lock_guard<std::recursive_mutex> g(r.mu);
    shutdown();
    for (Logger* l : r.order) {
        l->setLevel(nullptr);
        l->setAdditivity(true);
    }
}

void LogManager::setThreshold(Level* level) {
    if (level == nullptr) return;
    Repository& r = repo();
    r.threshold.store(level);
    r.thresholdInt.store(level->toInt());
}

Level* LogManager::getThreshold() { return repo().threshold.load(); }

void LogManager::setDefaultLoggerFactory(LoggerFactory* factory) { repo().defaultFactory.store(factory); }

LoggerFactory* LogManager::getDefaultLoggerFactory() { return repo().defaultFactory.load(); }

// ---------------------------------------------------------------------------------------
// LogLog

namespace {

struct LogLogState {
    std::atomic<int> debug{-1};  // -1: not set explicitly (use the log4j.debug property)
    std::atomic<bool> quiet{false};
    std::mutex sinkMu;
    LogLog::Sink sink;
    std::atomic<bool> hasSink{false};
};

LogLogState& logLogState() {
    static LogLogState* s = new LogLogState();
    return *s;
}

void emitLine(const String& line, bool toStderr) {
    LogLogState& s = logLogState();
    if (s.hasSink.load(std::memory_order_acquire)) {
        LogLog::Sink copy;
        {
            std::lock_guard<std::mutex> g(s.sinkMu);
            copy = s.sink;
        }
        if (copy) {
            copy(line, toStderr);
            return;
        }
    }
    PrintStream* ps = toStderr ? System::err : System::out;
    if (ps == nullptr) ps = toStderr ? ::jlang::detail::stderrStream() : ::jlang::detail::stdoutStream();
    ps->println(line);
}

void emit(const char* prefix, const String& msg, Throwable* t, bool toStderr) {
    emitLine(str(prefix, msg), toStderr);
    if (t != nullptr) {
        std::vector<String> lines;
        impl::renderThrowable(t, lines);
        for (const String& l : lines) emitLine(l, toStderr);
    }
}

}  // namespace

void LogLog::setInternalDebugging(bool enabled) { logLogState().debug.store(enabled ? 1 : 0); }

bool LogLog::isDebugEnabled() {
    int d = logLogState().debug.load();
    if (d >= 0) return d == 1;
    String v = System::getProperty(DEBUG_KEY);
    if (v == nullptr) v = System::getProperty(String("log4j.configDebug"));
    if (v == nullptr) return false;
    return !v.trim().equalsIgnoreCase("false");  // OptionConverter.toBoolean(v, true)
}

void LogLog::setQuietMode(bool quietMode) { logLogState().quiet.store(quietMode); }

void LogLog::setSink(Sink sink) {
    LogLogState& s = logLogState();
    std::lock_guard<std::mutex> g(s.sinkMu);
    s.sink = std::move(sink);
    s.hasSink.store(static_cast<bool>(s.sink), std::memory_order_release);
}

void LogLog::debug(const String& msg) { debug(msg, nullptr); }
void LogLog::debug(const String& msg, const Throwable& t) { debug(msg, const_cast<Throwable*>(&t)); }
void LogLog::debug(const String& msg, Throwable* t) {
    if (isDebugEnabled() && !logLogState().quiet.load()) emit("log4j: ", msg, t, false);
}

void LogLog::warn(const String& msg) { warn(msg, nullptr); }
void LogLog::warn(const String& msg, const Throwable& t) { warn(msg, const_cast<Throwable*>(&t)); }
void LogLog::warn(const String& msg, Throwable* t) {
    if (!logLogState().quiet.load()) emit("log4j:WARN ", msg, t, true);
}

void LogLog::error(const String& msg) { error(msg, nullptr); }
void LogLog::error(const String& msg, const Throwable& t) { error(msg, const_cast<Throwable*>(&t)); }
void LogLog::error(const String& msg, Throwable* t) {
    if (!logLogState().quiet.load()) emit("log4j:ERROR ", msg, t, true);
}

// ---------------------------------------------------------------------------------------
// OptionHandler

namespace {
std::string decapitalize(std::string_view n) {
    std::string r(n);
    if (r.empty()) return r;
    if (r.size() > 1 && std::isupper(static_cast<unsigned char>(r[1])) && std::isupper(static_cast<unsigned char>(r[0])))
        return r;
    r[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[0])));
    return r;
}
}  // namespace

bool OptionHandler::setOption(String name, String value) {
    (void)name;
    (void)value;
    return false;
}

bool OptionHandler::optionIs(const String& name, const char* property) {
    return decapitalize(name) == decapitalize(property);
}

int32_t OptionHandler::toIntOption(const String& value) { return Integer::parseInt(value.trim()); }

int64_t OptionHandler::toLongOption(const String& value) { return Long::parseLong(value.trim()); }

bool OptionHandler::toBoolOption(const String& value) {
    String v = value.trim();
    if (v.equalsIgnoreCase("true")) return true;
    if (v.equalsIgnoreCase("false")) return false;
    throw IllegalArgumentException(String("Conversion to type [boolean] failed."));
}

Level* OptionHandler::toLevelOption(const String& value) { return impl::toLevel(value, Level::builtin(5)); }

namespace impl {

// OptionConverter.toLevel(value, defaultValue)
Level* toLevel(const String& value, Level* defaultValue) {
    if (value == nullptr) return defaultValue;
    String v = value.trim();
    int32_t hash = v.indexOf('#');
    if (hash == -1) {
        if (v.equalsIgnoreCase("NULL")) return nullptr;
        return Level::toLevel(v, defaultValue);
    }
    String clazz = v.substring(hash + 1);
    String levelName = v.substring(0, hash);
    if (levelName.equalsIgnoreCase("NULL")) return nullptr;
    LogLog::warn(str("custom level class [", clazz, "] not found."));
    return defaultValue;
}

bool toBoolean(const String& value, bool defaultValue) {
    if (value == nullptr) return defaultValue;
    String v = value.trim();
    if (v.equalsIgnoreCase("true")) return true;
    if (v.equalsIgnoreCase("false")) return false;
    return defaultValue;
}

int64_t toFileSize(const String& value, int64_t defaultValue) {
    if (value == nullptr) return defaultValue;
    String s = value.trim().toUpperCase();
    int64_t multiplier = 1;
    int32_t index;
    if ((index = s.indexOf(String("KB"))) != -1) {
        multiplier = 1024;
        s = s.substring(0, index);
    } else if ((index = s.indexOf(String("MB"))) != -1) {
        multiplier = 1024 * 1024;
        s = s.substring(0, index);
    } else if ((index = s.indexOf(String("GB"))) != -1) {
        multiplier = 1024 * 1024 * 1024;
        s = s.substring(0, index);
    }
    try {
        return Long::parseLong(s) * multiplier;
    } catch (NumberFormatException& e) {
        LogLog::error(str("[", s, "] is not in proper int form."));
        LogLog::error(str("[", value, "] not in expected format."), e);
    }
    return defaultValue;
}

String substVars(const String& val) {
    std::string out;
    size_t i = 0;
    const std::string& v = val;
    for (;;) {
        size_t j = v.find("${", i);
        if (j == std::string::npos) {
            if (i == 0) return val;
            out.append(v, i, std::string::npos);
            return String(out);
        }
        out.append(v, i, j - i);
        size_t k = v.find('}', j);
        if (k == std::string::npos) {
            throw IllegalArgumentException(str("\"", val, "\" has no closing brace. Opening brace at position ",
                                               static_cast<int64_t>(j), "."));
        }
        String key(v.substr(j + 2, k - j - 2));
        String replacement;
        try {
            replacement = System::getProperty(key);  // OptionConverter.getSystemProperty: failures -> null
        } catch (Exception&) {
        }
        if (replacement != nullptr) out += substVars(replacement);
        i = k + 1;
    }
}

String convertSpecialChars(const String& s) {
    std::string out;
    const std::string& in = s;
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '\\' && i + 1 < in.size()) {
            c = in[++i];
            switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'f': c = '\f'; break;
                case 'b': c = '\b'; break;
                default: break;  // \" \' \\ and anything else: the character itself
            }
        }
        out.push_back(c);
    }
    return String(out);
}

String currentThreadName() {
    // Thread.currentThread().getName(); before jlang::init() (static initialization) the thread
    // library may not be set up yet: name the main thread "main", others by their OS name.
    if (::jlang::isInitialized()) {
        try {
            if (Thread* t = Thread::currentThread()) return t->getName();
        } catch (Throwable&) {
        }
    }
    if (static_cast<long>(::getpid()) == ::syscall(SYS_gettid)) return String("main");
    char buf[64] = {};
    if (::pthread_getname_np(::pthread_self(), buf, sizeof buf) == 0 && buf[0] != '\0') return String(buf);
    return str("Thread-", static_cast<int64_t>(::syscall(SYS_gettid)));
}

}  // namespace impl

}  // namespace jlang::log4j
