// log4j emulation, part 4: DOMConfigurator (log4j.xml via pugixml), the class factory
// registry and the registration of the log4j classes' Java names. See <jlang/Log.h>.
#include <jlang/Log.h>

#include "log_internal.h"

#include <pugixml.hpp>

#include <cctype>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace jlang::log4j {

namespace {

// =======================================================================================
// Factory registry

using ErrorHandlerFactoryFn = std::function<ErrorHandler*()>;

struct FactoryRegistry {
    std::mutex mu;
    // class name -> bean property name -> setter
    std::unordered_map<std::string, std::unordered_map<std::string, PropertySetterFn>> setters;
    std::unordered_map<std::string, AppenderFactoryFn> appenders;
    std::unordered_map<std::string, FilterFactoryFn> filters;
    std::unordered_map<std::string, LayoutFactoryFn> layouts;
    std::unordered_map<std::string, LoggerFactoryFactoryFn> loggerFactories;
    std::unordered_map<std::string, ErrorHandlerFactoryFn> errorHandlers;
};

FactoryRegistry& factories() {
    static FactoryRegistry* r = [] {
        auto* x = new FactoryRegistry();
        x->appenders["org.apache.log4j.ConsoleAppender"] = [] { return static_cast<Appender*>(new ConsoleAppender()); };
        x->appenders["org.apache.log4j.FileAppender"] = [] { return static_cast<Appender*>(new FileAppender()); };
        x->appenders["org.apache.log4j.RollingFileAppender"] = [] {
            return static_cast<Appender*>(new RollingFileAppender());
        };
        x->layouts["org.apache.log4j.PatternLayout"] = [] { return static_cast<Layout*>(new PatternLayout()); };
        x->layouts["org.apache.log4j.EnhancedPatternLayout"] = [] {
            return static_cast<Layout*>(new EnhancedPatternLayout());
        };
        x->layouts["org.apache.log4j.SimpleLayout"] = [] { return static_cast<Layout*>(new SimpleLayout()); };
        x->filters["org.apache.log4j.varia.DenyAllFilter"] = [] { return static_cast<Filter*>(new DenyAllFilter()); };
        x->filters["org.apache.log4j.varia.LevelMatchFilter"] = [] {
            return static_cast<Filter*>(new LevelMatchFilter());
        };
        x->filters["org.apache.log4j.varia.LevelRangeFilter"] = [] {
            return static_cast<Filter*>(new LevelRangeFilter());
        };
        x->filters["org.apache.log4j.varia.StringMatchFilter"] = [] {
            return static_cast<Filter*>(new StringMatchFilter());
        };
        x->errorHandlers["org.apache.log4j.helpers.OnlyOnceErrorHandler"] = [] {
            return static_cast<ErrorHandler*>(new OnlyOnceErrorHandler());
        };
        return x;
    }();
    return *r;
}

// Java names for getClass()->getName() and Class.forName (also used in LogLog messages).
bool registerJavaNames() {
    using C = ::jlang::Class;
    C::registerClass<::jlang::Logger>(String("org.apache.log4j.Logger"), nullptr, {}, C::NONE, nullptr);
    C::registerClass<Level>(String("org.apache.log4j.Level"), nullptr, {}, C::NONE, nullptr);
    C::registerClass<LoggingEvent>(String("org.apache.log4j.spi.LoggingEvent"), nullptr, {}, C::NONE, nullptr);
    C::registerClass<ThrowableInformation>(String("org.apache.log4j.spi.ThrowableInformation"), nullptr, {},
                                           C::NONE, nullptr);
    C::registerClass<LocationInfo>(String("org.apache.log4j.spi.LocationInfo"), nullptr, {}, C::NONE, nullptr);
    C::registerClass<OptionHandler>(String("org.apache.log4j.spi.OptionHandler"), nullptr, {}, C::INTERFACE, nullptr);
    C::registerClass<Filter>(String("org.apache.log4j.spi.Filter"), nullptr, {C::of<OptionHandler>()}, C::ABSTRACT,
                             nullptr);
    C::registerClass<LoggerFactory>(String("org.apache.log4j.spi.LoggerFactory"), nullptr, {}, C::INTERFACE, nullptr);
    C::registerClass<ErrorHandler>(String("org.apache.log4j.spi.ErrorHandler"), nullptr, {C::of<OptionHandler>()},
                                   C::INTERFACE, nullptr);
    C::registerClass<Appender>(String("org.apache.log4j.Appender"), nullptr, {}, C::INTERFACE, nullptr);
    C::registerClass<Layout>(String("org.apache.log4j.Layout"), nullptr, {C::of<OptionHandler>()}, C::ABSTRACT,
                             nullptr);
    C::registerClass<AppenderSkeleton>(String("org.apache.log4j.AppenderSkeleton"), nullptr,
                                       {C::of<Appender>(), C::of<OptionHandler>()}, C::ABSTRACT, nullptr);
    C::registerClass<WriterAppender>(String("org.apache.log4j.WriterAppender"), C::of<AppenderSkeleton>(), {},
                                     C::NONE, []() -> Object* { return new WriterAppender(); });
    C::registerClass<ConsoleAppender>(String("org.apache.log4j.ConsoleAppender"), C::of<WriterAppender>(), {},
                                      C::NONE, []() -> Object* { return new ConsoleAppender(); });
    C::registerClass<FileAppender>(String("org.apache.log4j.FileAppender"), C::of<WriterAppender>(), {}, C::NONE,
                                   []() -> Object* { return new FileAppender(); });
    C::registerClass<RollingFileAppender>(String("org.apache.log4j.RollingFileAppender"), C::of<FileAppender>(),
                                          {}, C::NONE, []() -> Object* { return new RollingFileAppender(); });
    C::registerClass<SimpleLayout>(String("org.apache.log4j.SimpleLayout"), C::of<Layout>(), {}, C::NONE,
                                   []() -> Object* { return new SimpleLayout(); });
    C::registerClass<PatternLayout>(String("org.apache.log4j.PatternLayout"), C::of<Layout>(), {}, C::NONE,
                                    []() -> Object* { return new PatternLayout(); });
    C::registerClass<EnhancedPatternLayout>(String("org.apache.log4j.EnhancedPatternLayout"), C::of<PatternLayout>(),
                                            {}, C::NONE, []() -> Object* { return new EnhancedPatternLayout(); });
    C::registerClass<DenyAllFilter>(String("org.apache.log4j.varia.DenyAllFilter"), C::of<Filter>(), {}, C::NONE,
                                    []() -> Object* { return new DenyAllFilter(); });
    C::registerClass<LevelMatchFilter>(String("org.apache.log4j.varia.LevelMatchFilter"), C::of<Filter>(), {},
                                       C::NONE, []() -> Object* { return new LevelMatchFilter(); });
    C::registerClass<LevelRangeFilter>(String("org.apache.log4j.varia.LevelRangeFilter"), C::of<Filter>(), {},
                                       C::NONE, []() -> Object* { return new LevelRangeFilter(); });
    C::registerClass<StringMatchFilter>(String("org.apache.log4j.varia.StringMatchFilter"), C::of<Filter>(), {},
                                        C::NONE, []() -> Object* { return new StringMatchFilter(); });
    C::registerClass<OnlyOnceErrorHandler>(String("org.apache.log4j.helpers.OnlyOnceErrorHandler"), nullptr,
                                           {C::of<ErrorHandler>()}, C::NONE,
                                           []() -> Object* { return new OnlyOnceErrorHandler(); });
    return true;
}

[[maybe_unused]] const bool g_javaNamesRegistered = registerJavaNames();

// Class names to try: the name itself, and for chatserver's ae-commons jar
// (com.aionengine.commons.*) the ported commons class (org.openaion.commons.*).
std::vector<std::string> candidateNames(const std::string& className) {
    std::vector<std::string> names{className};
    static const std::string ae = "com.aionengine.commons.";
    if (className.rfind(ae, 0) == 0) names.push_back("org.openaion.commons." + className.substr(ae.size()));
    return names;
}

enum class Lookup { OK, NOT_FOUND, WRONG_TYPE, FAILED };

template<class T, class Fn>
T* instantiate(const String& className, std::unordered_map<std::string, Fn> FactoryRegistry::*map,
               const char* javaType, Lookup& result) {
    result = Lookup::NOT_FOUND;
    if (className == nullptr || className.isEmpty()) return nullptr;
    auto names = candidateNames(className);
    FactoryRegistry& reg = factories();
    for (const std::string& n : names) {
        Fn fn;
        {
            std::lock_guard<std::mutex> g(reg.mu);
            auto it = (reg.*map).find(n);
            if (it != (reg.*map).end()) fn = it->second;
        }
        if (fn) {
            T* obj = fn();
            result = obj != nullptr ? Lookup::OK : Lookup::FAILED;
            return obj;
        }
    }
    for (const std::string& n : names) {
        ::jlang::Class* cls = ::jlang::Class::findClass(String(n));
        if (cls == nullptr || !cls->hasFactory()) continue;
        Object* o;
        try {
            o = cls->newInstance();
        } catch (Exception& e) {
            LogLog::error(str("Could not instantiate class [", className, "]."), e);
            result = Lookup::FAILED;
            return nullptr;
        }
        T* t = dynamic_cast<T*>(o);
        if (t == nullptr) {
            LogLog::error(str("A \"", className, "\" object is not assignable to a \"", javaType, "\" variable."));
            result = Lookup::WRONG_TYPE;
            return nullptr;
        }
        result = Lookup::OK;
        return t;
    }
    return nullptr;
}

std::string decapitalize(std::string_view n) {
    std::string r(n);
    if (r.empty()) return r;
    if (r.size() > 1 && std::isupper(static_cast<unsigned char>(r[1])) && std::isupper(static_cast<unsigned char>(r[0])))
        return r;
    r[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[0])));
    return r;
}

String className(Object* o) { return o == nullptr ? String("null") : o->getClass()->getName(); }

// =======================================================================================
// DOMConfigurator

class Configurator {
public:
    void parse(pugi::xml_node element);

private:
    static String attr(pugi::xml_node e, const char* name) { return String(e.attribute(name).value()); }
    String subst(const String& value);
    void setParameter(pugi::xml_node elem, Object* target);
    void activate(Object* o) {
        if (auto* oh = dynamic_cast<OptionHandler*>(o)) oh->activateOptions();
    }
    void parseCategoryFactory(pugi::xml_node e);
    void parseCategory(pugi::xml_node e);
    void parseRoot(pugi::xml_node e);
    void parseChildrenOfLoggerElement(pugi::xml_node e, Logger* cat, bool isRoot);
    void parseLevel(pugi::xml_node e, Logger* logger, bool isRoot);
    Appender* findAppenderByName(const String& name);
    Appender* parseAppender(pugi::xml_node e);
    Layout* parseLayout(pugi::xml_node e);
    void parseFilters(pugi::xml_node e, Appender* appender);
    void parseErrorHandler(pugi::xml_node e, Appender* appender);

    pugi::xml_node root_;
    std::unordered_map<std::string, Appender*> appenderBag_;
    LoggerFactory* catFactory_ = nullptr;
};

String Configurator::subst(const String& value) {
    try {
        return impl::substVars(value);
    } catch (IllegalArgumentException& e) {
        LogLog::warn(String("Could not perform variable substitution."), e);
        return value;
    }
}

PropertySetterFn findPropertySetter(Object* target, const std::string& prop) {
    FactoryRegistry& reg = factories();
    std::lock_guard<std::mutex> g(reg.mu);
    if (reg.setters.empty()) return PropertySetterFn();
    int depth = 0;
    for (::jlang::Class* c = target->getClass(); c != nullptr && depth < 64; c = c->getSuperclass(), depth++) {
        auto it = reg.setters.find(c->getName());
        if (it == reg.setters.end()) continue;
        auto jt = it->second.find(prop);
        if (jt != it->second.end()) return jt->second;
    }
    return PropertySetterFn();
}

void Configurator::setParameter(pugi::xml_node elem, Object* target) {
    String name = subst(attr(elem, "name"));
    String value = subst(impl::convertSpecialChars(attr(elem, "value")));
    String prop(decapitalize(name));
    if (PropertySetterFn setter = findPropertySetter(target, prop)) {
        try {
            setter(target, value);
            LogLog::debug(str("Setting property [", prop, "] to [", value, "]."));
        } catch (Exception& e) {
            LogLog::warn(str("Failed to set property [", prop, "] to value \"", value, "\". "), e);
        }
        return;
    }
    auto* oh = dynamic_cast<OptionHandler*>(target);
    bool handled = false;
    try {
        handled = oh != nullptr && oh->setOption(name, value);
    } catch (Exception& e) {
        LogLog::warn(str("Failed to set property [", prop, "] to value \"", value, "\". "), e);
        return;
    }
    if (handled) LogLog::debug(str("Setting property [", prop, "] to [", value, "]."));
    else LogLog::warn(str("No such property [", prop, "] in ", className(target), "."));
}

void Configurator::parse(pugi::xml_node element) {
    root_ = element;
    std::string rootName = element.name();
    if (rootName != "log4j:configuration") {
        if (rootName == "configuration") {
            LogLog::warn(String("The <configuration> element has been deprecated."));
            LogLog::warn(String("Use the <log4j:configuration> element instead."));
        } else {
            LogLog::error(String("DOM element is - not a <log4j:configuration> element."));
            return;
        }
    }
    String debugAttrib = subst(attr(element, "debug"));
    LogLog::debug(str("debug attribute= \"", debugAttrib, "\"."));
    if (!debugAttrib.equals("") && !debugAttrib.equals("null")) {
        LogLog::setInternalDebugging(impl::toBoolean(debugAttrib, true));
    } else {
        LogLog::debug(String("Ignoring debug attribute."));
    }
    String resetAttrib = subst(attr(element, "reset"));
    LogLog::debug(str("reset attribute= \"", resetAttrib, "\"."));
    if (!resetAttrib.equals("")) {
        if (impl::toBoolean(resetAttrib, false)) LogManager::resetConfiguration();
    }
    String confDebug = subst(attr(element, "configDebug"));
    if (!confDebug.equals("") && !confDebug.equals("null")) {
        LogLog::warn(String("The \"configDebug\" attribute is deprecated."));
        LogLog::warn(String("Use the \"debug\" attribute instead."));
        LogLog::setInternalDebugging(impl::toBoolean(confDebug, true));
    }
    String thresholdStr = subst(attr(element, "threshold"));
    LogLog::debug(str("Threshold =\"", thresholdStr, "\"."));
    if (!thresholdStr.equals("") && !thresholdStr.equals("null")) {
        Level* l = Level::toLevel(thresholdStr, nullptr);
        if (l != nullptr) LogManager::setThreshold(l);
        else LogLog::warn(str("Could not convert [", thresholdStr, "] to Level."));
    }
    // First the logger factories, then everything else (appenders are parsed on demand).
    for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) continue;
        std::string tag = child.name();
        if (tag == "categoryFactory" || tag == "loggerFactory") parseCategoryFactory(child);
    }
    for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) continue;
        std::string tag = child.name();
        if (tag == "category" || tag == "logger") parseCategory(child);
        else if (tag == "root") parseRoot(child);
        else if (tag == "renderer" || tag == "throwableRenderer")
            LogLog::debug(str("Ignoring <", tag, "> element (not supported)."));
        // appender, categoryFactory, loggerFactory and unknown elements: nothing to do here
    }
}

void Configurator::parseCategoryFactory(pugi::xml_node e) {
    String className = subst(attr(e, "class"));
    if (className.equals("")) {
        LogLog::error(String("Category Factory tag class attribute not found."));
        LogLog::debug(String("No Category Factory configured."));
        return;
    }
    LogLog::debug(str("Desired category factory: [", className, "]"));
    Lookup res;
    LoggerFactory* factory =
        instantiate<LoggerFactory>(className, &FactoryRegistry::loggerFactories, "org.apache.log4j.spi.LoggerFactory", res);
    if (res == Lookup::NOT_FOUND) {
        LogLog::warn(str("Could not instantiate logger factory class [", className,
                         "]: class not registered; element skipped."));
        return;
    }
    if (factory == nullptr) {
        LogLog::error(str("Category Factory class ", className, " does not implement org.apache.log4j.LoggerFactory"));
        return;
    }
    catFactory_ = factory;
    for (pugi::xml_node child = e.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_element && std::strcmp(child.name(), "param") == 0) setParameter(child, factory);
    }
}

void Configurator::parseCategory(pugi::xml_node e) {
    String catName = subst(attr(e, "name"));
    String cls = subst(attr(e, "class"));
    if (!cls.equals("")) {
        LogLog::debug(str("Desired logger sub-class: [", cls, "]"));
        LogLog::warn(str("Logger sub-class [", cls, "] is not supported (no reflection); using org.apache.log4j.Logger."));
    } else {
        LogLog::debug(String("Retreiving an instance of org.apache.log4j.Logger."));
    }
    Logger* cat = catFactory_ == nullptr ? LogManager::getLogger(catName) : LogManager::getLogger(catName, catFactory_);
    JSYNC(cat) {
        bool additivity = impl::toBoolean(subst(attr(e, "additivity")), true);
        LogLog::debug(str("Setting [", cat->getName(), "] additivity to [", additivity, "]."));
        cat->setAdditivity(additivity);
        parseChildrenOfLoggerElement(e, cat, false);
    }
}

void Configurator::parseRoot(pugi::xml_node e) {
    Logger* root = LogManager::getRootLogger();
    JSYNC(root) { parseChildrenOfLoggerElement(e, root, true); }
}

void Configurator::parseChildrenOfLoggerElement(pugi::xml_node e, Logger* cat, bool isRoot) {
    // Remove all existing appenders: they are reconstructed if need be.
    cat->removeAllAppenders();
    for (pugi::xml_node child = e.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) continue;
        std::string tag = child.name();
        if (tag == "appender-ref") {
            String refName = subst(attr(child, "ref"));
            Appender* appender = findAppenderByName(refName);
            if (appender != nullptr)
                LogLog::debug(str("Adding appender named [", refName, "] to category [", cat->getName(), "]."));
            else LogLog::debug(str("Appender named [", refName, "] not found."));
            cat->addAppender(appender);
        } else if (tag == "level" || tag == "priority") {
            parseLevel(child, cat, isRoot);
        } else if (tag == "param") {
            String name = subst(attr(child, "name"));
            String value = subst(impl::convertSpecialChars(attr(child, "value")));
            if (OptionHandler::optionIs(name, "additivity")) cat->setAdditivity(impl::toBoolean(value, true));
            else if (OptionHandler::optionIs(name, "level") || OptionHandler::optionIs(name, "priority"))
                cat->setLevel(impl::toLevel(value, Level::builtin(5)));
            else LogLog::warn(str("No such property [", decapitalize(name), "] in org.apache.log4j.Logger."));
        }
    }
}

void Configurator::parseLevel(pugi::xml_node e, Logger* logger, bool isRoot) {
    String catName = isRoot ? String("root") : logger->getName();
    String priStr = subst(attr(e, "value"));
    LogLog::debug(str("Level value for ", catName, " is  [", priStr, "]."));
    if (priStr.equalsIgnoreCase("inherited") || priStr.equalsIgnoreCase("null")) {
        if (isRoot) LogLog::error(String("Root level cannot be inherited. Ignoring directive."));
        else logger->setLevel(nullptr);
    } else {
        String cls = subst(attr(e, "class"));
        if (cls.equals("")) {
            logger->setLevel(impl::toLevel(priStr, Level::builtin(5)));
        } else {
            LogLog::debug(str("Desired Level sub-class: [", cls, "]"));
            LogLog::error(str("Could not create level [", priStr, "]: custom level class [", cls, "] is not supported."));
        }
    }
    LogLog::debug(str(catName, " level set to ", logger->getLevel()));
}

Appender* Configurator::findAppenderByName(const String& name) {
    auto it = appenderBag_.find(name);
    if (it != appenderBag_.end()) return it->second;
    pugi::xml_node found;
    // document.getElementsByTagName("appender"): every <appender>, in document order
    struct Finder : pugi::xml_tree_walker {
        const std::string* want;
        pugi::xml_node result;
        bool for_each(pugi::xml_node& n) override {
            if (n.type() == pugi::node_element && std::strcmp(n.name(), "appender") == 0 &&
                *want == n.attribute("name").value()) {
                result = n;
                return false;
            }
            return true;
        }
    } finder;
    finder.want = &name;
    root_.root().traverse(finder);
    found = finder.result;
    if (!found) {
        LogLog::error(str("No appender named [", name, "] could be found."));
        return nullptr;
    }
    Appender* a = parseAppender(found);
    if (a != nullptr) appenderBag_[name] = a;
    return a;
}

Appender* Configurator::parseAppender(pugi::xml_node e) {
    String cls = subst(attr(e, "class"));
    String appenderName = subst(attr(e, "name"));
    LogLog::debug(str("Class name: [", cls, "]"));
    Lookup res;
    Appender* appender;
    try {
        appender = instantiate<Appender>(cls, &FactoryRegistry::appenders, "org.apache.log4j.Appender", res);
    } catch (Exception& oops) {
        LogLog::error(String("Could not create an Appender. Reported error follows."), oops);
        return nullptr;
    }
    if (res == Lookup::NOT_FOUND) {
        LogLog::warn(str("Could not create an Appender: class [", cls, "] of appender [", appenderName,
                         "] is not registered; appender skipped."));
        return nullptr;
    }
    if (appender == nullptr) {
        ClassCastException e(str(cls, " cannot be cast to org.apache.log4j.Appender"));
        LogLog::error(String("Could not create an Appender. Reported error follows."), e);
        return nullptr;
    }
    try {
        appender->setName(appenderName);
        for (pugi::xml_node child = e.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) continue;
            std::string tag = child.name();
            if (tag == "param") {
                setParameter(child, appender);
            } else if (tag == "layout") {
                appender->setLayout(parseLayout(child));
            } else if (tag == "filter") {
                parseFilters(child, appender);
            } else if (tag == "errorHandler") {
                parseErrorHandler(child, appender);
            } else if (tag == "appender-ref") {
                String refName = subst(attr(child, "ref"));
                LogLog::error(str("Requesting attachment of appender named [", refName, "] to appender named [",
                                  appender->getName(),
                                  "] which does not implement org.apache.log4j.spi.AppenderAttachable."));
            } else {
                LogLog::warn(str("Unrecognized element ", tag));
            }
        }
        activate(appender);
        return appender;
    } catch (Exception& oops) {
        LogLog::error(String("Could not create an Appender. Reported error follows."), oops);
        return nullptr;
    }
}

Layout* Configurator::parseLayout(pugi::xml_node e) {
    String cls = subst(attr(e, "class"));
    LogLog::debug(str("Parsing layout of class: \"", cls, "\""));
    Lookup res;
    Layout* layout;
    try {
        layout = instantiate<Layout>(cls, &FactoryRegistry::layouts, "org.apache.log4j.Layout", res);
    } catch (Exception& oops) {
        LogLog::error(String("Could not create the Layout. Reported error follows."), oops);
        return nullptr;
    }
    if (res == Lookup::NOT_FOUND) {
        LogLog::warn(str("Could not create the Layout: class [", cls, "] is not registered; layout skipped."));
        return nullptr;
    }
    if (layout == nullptr) return nullptr;
    try {
        for (pugi::xml_node child = e.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) continue;
            if (std::strcmp(child.name(), "param") == 0) setParameter(child, layout);
            else LogLog::warn(str("Unrecognized element ", child.name()));
        }
        activate(layout);
        return layout;
    } catch (Exception& oops) {
        LogLog::error(String("Could not create the Layout. Reported error follows."), oops);
        return nullptr;
    }
}

void Configurator::parseFilters(pugi::xml_node e, Appender* appender) {
    String cls = subst(attr(e, "class"));
    Lookup res;
    Filter* filter = instantiate<Filter>(cls, &FactoryRegistry::filters, "org.apache.log4j.spi.Filter", res);
    if (res == Lookup::NOT_FOUND) {
        LogLog::warn(str("Could not instantiate filter class [", cls, "] of appender [", appender->getName(),
                         "]: class not registered; filter skipped."));
        return;
    }
    if (filter == nullptr) return;
    for (pugi::xml_node child = e.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) continue;
        if (std::strcmp(child.name(), "param") == 0) setParameter(child, filter);
        else LogLog::warn(str("Unrecognized element ", child.name()));
    }
    activate(filter);
    LogLog::debug(str("Adding filter of type [", className(filter), "] to appender named [", appender->getName(), "]."));
    appender->addFilter(filter);
}

void Configurator::parseErrorHandler(pugi::xml_node e, Appender* appender) {
    String cls = subst(attr(e, "class"));
    Lookup res;
    ErrorHandler* eh =
        instantiate<ErrorHandler>(cls, &FactoryRegistry::errorHandlers, "org.apache.log4j.spi.ErrorHandler", res);
    if (res == Lookup::NOT_FOUND) {
        LogLog::warn(str("Could not instantiate error handler class [", cls, "]: class not registered; skipped."));
        return;
    }
    if (eh == nullptr) return;
    for (pugi::xml_node child = e.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) continue;
        if (std::strcmp(child.name(), "param") == 0) setParameter(child, eh);
        // root-ref / logger-ref / appender-ref (backup appenders) are not supported
    }
    activate(eh);
    appender->setErrorHandler(eh);
}

std::mutex& configureMutex() {
    static std::mutex* m = new std::mutex();
    return *m;
}

void configureDocument(pugi::xml_document& doc, const pugi::xml_parse_result& result, const String& what) {
    if (!result) {
        String msg = str(result.description(), " at offset ", static_cast<int64_t>(result.offset));
        if (result.status == pugi::status_file_not_found) {
            FileNotFoundException e(str(what, " (No such file or directory)"));
            LogLog::error(str("Could not parse ", what, "."), e);
        } else {
            IOException e(msg);
            LogLog::error(str("Could not parse ", what, "."), e);
        }
        return;
    }
    pugi::xml_node root = doc.document_element();
    if (!root) {
        IOException e(String("no document element"));
        LogLog::error(str("Could not parse ", what, "."), e);
        return;
    }
    Configurator c;
    c.parse(root);
}

}  // namespace

// =======================================================================================
// public API

void DOMConfigurator::configure(const String& filename) {
    std::lock_guard<std::mutex> g(configureMutex());
    LogLog::debug(str("DOMConfigurator configuring from file [", filename, "]."));
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filename.c_str(), pugi::parse_default, pugi::encoding_auto);
    configureDocument(doc, result, str("file [", filename, "]"));
}

void DOMConfigurator::configureFromXml(const String& xml) {
    std::lock_guard<std::mutex> g(configureMutex());
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_auto);
    configureDocument(doc, result, String("XML text"));
}

void DOMConfigurator::configureAndWatch(const String& filename) { configure(filename); }

void DOMConfigurator::configureAndWatch(const String& filename, int64_t delay) {
    (void)delay;
    configure(filename);
}

void registerAppenderFactory(const String& className, AppenderFactoryFn factory) {
    FactoryRegistry& r = factories();
    std::lock_guard<std::mutex> g(r.mu);
    r.appenders[className] = std::move(factory);
}

void registerFilterFactory(const String& className, FilterFactoryFn factory) {
    FactoryRegistry& r = factories();
    std::lock_guard<std::mutex> g(r.mu);
    r.filters[className] = std::move(factory);
}

void registerLayoutFactory(const String& className, LayoutFactoryFn factory) {
    FactoryRegistry& r = factories();
    std::lock_guard<std::mutex> g(r.mu);
    r.layouts[className] = std::move(factory);
}

void registerPropertySetter(const String& className, const String& property, PropertySetterFn setter) {
    FactoryRegistry& r = factories();
    std::lock_guard<std::mutex> g(r.mu);
    r.setters[className][decapitalize(property)] = std::move(setter);
}

void registerLoggerFactoryFactory(const String& className, LoggerFactoryFactoryFn factory) {
    FactoryRegistry& r = factories();
    std::lock_guard<std::mutex> g(r.mu);
    r.loggerFactories[className] = std::move(factory);
}

}  // namespace jlang::log4j
