// java.lang.Object (monitors, wait/notify), java.lang.Class and cast helpers.
#define GC_THREADS
#define GC_NO_THREAD_REDIRECTS
#include <gc/gc.h>

#include <jlang/Boxes.h>
#include <jlang/Exceptions.h>
#include <jlang/Object.h>
#include <jlang/Runtime.h>
#include <jlang/String.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jlang {

// ---------------------------------------------------------------------------------------
// detail throw helpers

namespace detail {

void throwNullPointerException() { throw NullPointerException(); }
void throwNullPointerException(const char* msg) { throw NullPointerException(String(msg)); }

void throwClassCastException(Object* from, const std::type_info& to) {
    String fromName = from != nullptr ? from->getClass()->getName() : String("null");
    String toName = Class::forType(to)->getName();
    throw ClassCastException(str("class ", fromName, " cannot be cast to class ", toName));
}

// ---------------------------------------------------------------------------------------
// Monitor: reentrant lock + wait set. Uncontended enter/exit are a CAS / a store.

namespace {
thread_local char tl_threadToken;
inline uintptr_t selfToken() noexcept { return reinterpret_cast<uintptr_t>(&tl_threadToken); }
}  // namespace

class Monitor {
public:
    void enter() {
        const uintptr_t me = selfToken();
        if (owner_.load(std::memory_order_relaxed) == me) {
            recursion_++;
            return;
        }
        uintptr_t expected = 0;
        if (owner_.compare_exchange_strong(expected, me, std::memory_order_acquire)) {
            recursion_ = 1;
            return;
        }
        std::unique_lock<std::mutex> lock(mu_);
        acquireSlow(lock, me);
        recursion_ = 1;
    }

    void exit() {
        const uintptr_t me = selfToken();
        if (owner_.load(std::memory_order_relaxed) != me) {
            throw IllegalMonitorStateException(String("current thread is not owner"));
        }
        if (--recursion_ > 0) return;
        release();
    }

    bool heldByCurrentThread() const noexcept { return owner_.load(std::memory_order_relaxed) == selfToken(); }

    // Object.wait(timeout): millis == 0 && nanos == 0 waits forever.
    void wait(int64_t millis, int32_t nanos) {
        const uintptr_t me = selfToken();
        if (owner_.load(std::memory_order_relaxed) != me) {
            throw IllegalMonitorStateException(String("current thread is not owner"));
        }
        sync::InterruptState* st = sync::current();
        const bool timed = millis > 0 || nanos > 0;
        if (millis > INT64_C(3153600000000)) millis = INT64_C(3153600000000);  // no deadline overflow
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis) +
                              std::chrono::nanoseconds(nanos);
        std::unique_lock<std::mutex> lock(mu_);
        sync::setBlocker(&Monitor::wakeForInterrupt, this);
        bool interrupted = sync::isInterrupted(st);
        const int32_t savedRecursion = recursion_;
        if (!interrupted) {
            // Release ownership completely while holding mu_, so a notify (which requires
            // ownership, then mu_) cannot run before we are waiting.
            recursion_ = 0;
            owner_.store(0, std::memory_order_release);
            if (contenders_.load() > 0) enterCv_.notify_one();
            waiters_++;
            if (timed) {
                waitCv_.wait_until(lock, deadline);
            } else {
                waitCv_.wait(lock);
            }
            waiters_--;
            acquireSlow(lock, me);
            recursion_ = savedRecursion;
        }
        lock.unlock();
        sync::clearBlocker();
        if (sync::isInterrupted(st)) {
            sync::clearInterrupt(st);
            throw InterruptedException();
        }
    }

    void notify(bool all) {
        if (owner_.load(std::memory_order_relaxed) != selfToken()) {
            throw IllegalMonitorStateException(String("current thread is not owner"));
        }
        std::lock_guard<std::mutex> lock(mu_);
        if (waiters_ == 0) return;
        if (all) {
            waitCv_.notify_all();
        } else {
            waitCv_.notify_one();
        }
    }

private:
    static void wakeForInterrupt(void* arg) {
        auto* m = static_cast<Monitor*>(arg);
        std::lock_guard<std::mutex> lock(m->mu_);
        m->waitCv_.notify_all();
    }

    // Called with mu_ held: wait until ownership can be taken.
    void acquireSlow(std::unique_lock<std::mutex>& lock, uintptr_t me) {
        contenders_.fetch_add(1);
        for (;;) {
            uintptr_t expected = 0;
            if (owner_.compare_exchange_strong(expected, me, std::memory_order_acquire)) break;
            enterCv_.wait(lock);
        }
        contenders_.fetch_sub(1);
    }

    void release() {
        owner_.store(0, std::memory_order_seq_cst);
        if (contenders_.load(std::memory_order_seq_cst) > 0) {
            // Taking mu_ orders this wake-up after a contender that is about to wait.
            std::lock_guard<std::mutex> lock(mu_);
            enterCv_.notify_one();
        }
    }

    std::atomic<uintptr_t> owner_{0};
    int32_t recursion_ = 0;  // only touched by the owner
    std::atomic<int32_t> contenders_{0};
    int32_t waiters_ = 0;  // guarded by mu_
    std::mutex mu_;
    std::condition_variable enterCv_;
    std::condition_variable waitCv_;
};

}  // namespace detail

// ---------------------------------------------------------------------------------------
// Object

Object::~Object() {
    uintptr_t v = monitor_.load(std::memory_order_acquire);
    if ((v & 1) != 0) {
        auto* m = reinterpret_cast<detail::Monitor*>(v & ~static_cast<uintptr_t>(1));
        m->~Monitor();
        GC_FREE(m);
    }
}

detail::Monitor* Object::monitor() {
    uintptr_t v = monitor_.load(std::memory_order_acquire);
    if (v != 0) return reinterpret_cast<detail::Monitor*>(v & ~static_cast<uintptr_t>(1));
    // Objects outside the GC heap (stack, static storage, thrown exceptions in malloc'ed
    // memory) get an uncollectable monitor that ~Object frees.
    const bool uncollectable = !detail::inGcHeap(this);
    void* mem = detail::gcAllocCell(sizeof(detail::Monitor), uncollectable);
    auto* m = new (mem) detail::Monitor();
    const uintptr_t nv = reinterpret_cast<uintptr_t>(m) | (uncollectable ? 1u : 0u);
    if (monitor_.compare_exchange_strong(v, nv, std::memory_order_acq_rel)) return m;
    if (uncollectable) {
        m->~Monitor();
        GC_FREE(mem);
    }
    return reinterpret_cast<detail::Monitor*>(v & ~static_cast<uintptr_t>(1));
}

int32_t Object::hashCode() { return identityHashCode(); }

bool Object::equals(Object* o) { return this == o; }

String Object::toString() { return str(getClass()->getName(), "@", Integer::toHexString(hashCode())); }

Object* Object::clone() { throw CloneNotSupportedException(getClass()->getName()); }

Class* Object::getClass() { return Class::forType(typeid(*this)); }

void Object::wait() { monitor()->wait(0, 0); }

void Object::wait(int64_t timeoutMillis) {
    if (timeoutMillis < 0) throw IllegalArgumentException(String("timeout value is negative"));
    monitor()->wait(timeoutMillis, 0);
}

void Object::wait(int64_t timeoutMillis, int32_t nanos) {
    if (timeoutMillis < 0) throw IllegalArgumentException(String("timeout value is negative"));
    if (nanos < 0 || nanos > 999999) throw IllegalArgumentException(String("nanosecond timeout value out of range"));
    monitor()->wait(timeoutMillis, nanos);
}

void Object::notify() { monitor()->notify(false); }
void Object::notifyAll() { monitor()->notify(true); }
void Object::monitorEnter() { monitor()->enter(); }
void Object::monitorExit() { monitor()->exit(); }
bool Object::monitorHeldByCurrentThread() {
    uintptr_t v = monitor_.load(std::memory_order_acquire);
    if (v == 0) return false;
    return reinterpret_cast<detail::Monitor*>(v & ~static_cast<uintptr_t>(1))->heldByCurrentThread();
}

// ---------------------------------------------------------------------------------------
// Class registry

namespace {

// Java names of the jlang classes (tools/cppgen/jdkmap.tsv) and primitive types.
const std::unordered_map<std::string, std::string>& knownNames() {
    static const auto* names = new std::unordered_map<std::string, std::string>{
        {"int", "int"}, {"long", "long"}, {"short", "short"}, {"signed char", "byte"},
        {"char16_t", "char"}, {"bool", "boolean"}, {"float", "float"}, {"double", "double"},
        {"void", "void"}, {"char", "char"}, {"long long", "long"},
        {"jlang.Object", "java.lang.Object"}, {"jlang.String", "java.lang.String"},
        {"jlang.StringBuilder", "java.lang.StringBuilder"}, {"jlang.Number", "java.lang.Number"},
        {"jlang.Integer", "java.lang.Integer"}, {"jlang.Long", "java.lang.Long"},
        {"jlang.Short", "java.lang.Short"}, {"jlang.Byte", "java.lang.Byte"},
        {"jlang.Float", "java.lang.Float"}, {"jlang.Double", "java.lang.Double"},
        {"jlang.Boolean", "java.lang.Boolean"}, {"jlang.Character", "java.lang.Character"},
        {"jlang.StringBox", "java.lang.String"},
        {"jlang.Class", "java.lang.Class"}, {"jlang.Math", "java.lang.Math"},
        {"jlang.System", "java.lang.System"}, {"jlang.Runtime", "java.lang.Runtime"},
        {"jlang.Thread", "java.lang.Thread"}, {"jlang.ThreadGroup", "java.lang.ThreadGroup"},
        {"jlang.Runnable", "java.lang.Runnable"}, {"jlang.Iterable", "java.lang.Iterable"},
        {"jlang.Comparable", "java.lang.Comparable"},
        {"jlang.StackTraceElement", "java.lang.StackTraceElement"},
        {"jlang.Throwable", "java.lang.Throwable"}, {"jlang.Exception", "java.lang.Exception"},
        {"jlang.Error", "java.lang.Error"}, {"jlang.RuntimeException", "java.lang.RuntimeException"},
        {"jlang.IllegalArgumentException", "java.lang.IllegalArgumentException"},
        {"jlang.IllegalStateException", "java.lang.IllegalStateException"},
        {"jlang.NullPointerException", "java.lang.NullPointerException"},
        {"jlang.ClassCastException", "java.lang.ClassCastException"},
        {"jlang.ArithmeticException", "java.lang.ArithmeticException"},
        {"jlang.IndexOutOfBoundsException", "java.lang.IndexOutOfBoundsException"},
        {"jlang.ArrayIndexOutOfBoundsException", "java.lang.ArrayIndexOutOfBoundsException"},
        {"jlang.StringIndexOutOfBoundsException", "java.lang.StringIndexOutOfBoundsException"},
        {"jlang.NegativeArraySizeException", "java.lang.NegativeArraySizeException"},
        {"jlang.UnsupportedOperationException", "java.lang.UnsupportedOperationException"},
        {"jlang.NumberFormatException", "java.lang.NumberFormatException"},
        {"jlang.SecurityException", "java.lang.SecurityException"},
        {"jlang.IllegalMonitorStateException", "java.lang.IllegalMonitorStateException"},
        {"jlang.ArrayStoreException", "java.lang.ArrayStoreException"},
        {"jlang.InterruptedException", "java.lang.InterruptedException"},
        {"jlang.CloneNotSupportedException", "java.lang.CloneNotSupportedException"},
        {"jlang.ReflectiveOperationException", "java.lang.ReflectiveOperationException"},
        {"jlang.ClassNotFoundException", "java.lang.ClassNotFoundException"},
        {"jlang.InstantiationException", "java.lang.InstantiationException"},
        {"jlang.IllegalAccessException", "java.lang.IllegalAccessException"},
        {"jlang.NoSuchFieldException", "java.lang.NoSuchFieldException"},
        {"jlang.NoSuchMethodException", "java.lang.NoSuchMethodException"},
        {"jlang.VirtualMachineError", "java.lang.VirtualMachineError"},
        {"jlang.InternalError", "java.lang.InternalError"},
        {"jlang.OutOfMemoryError", "java.lang.OutOfMemoryError"},
        {"jlang.StackOverflowError", "java.lang.StackOverflowError"},
        {"jlang.LinkageError", "java.lang.LinkageError"},
        {"jlang.IncompatibleClassChangeError", "java.lang.IncompatibleClassChangeError"},
        {"jlang.AbstractMethodError", "java.lang.AbstractMethodError"},
        {"jlang.ExceptionInInitializerError", "java.lang.ExceptionInInitializerError"},
        {"jlang.AssertionError", "java.lang.AssertionError"},
        {"jlang.NoSuchElementException", "java.util.NoSuchElementException"},
        {"jlang.ConcurrentModificationException", "java.util.ConcurrentModificationException"},
        {"jlang.ExecutionException", "java.util.concurrent.ExecutionException"},
        {"jlang.TimeoutException", "java.util.concurrent.TimeoutException"},
        {"jlang.RejectedExecutionException", "java.util.concurrent.RejectedExecutionException"},
        {"jlang.CancellationException", "java.util.concurrent.CancellationException"},
        {"jlang.BrokenBarrierException", "java.util.concurrent.BrokenBarrierException"},
        {"jlang.IOException", "java.io.IOException"},
        {"jlang.FileNotFoundException", "java.io.FileNotFoundException"},
        {"jlang.UnsupportedEncodingException", "java.io.UnsupportedEncodingException"},
        {"jlang.EOFException", "java.io.EOFException"},
        {"jlang.UncheckedIOException", "java.io.UncheckedIOException"},
        {"jlang.BufferUnderflowException", "java.nio.BufferUnderflowException"},
        {"jlang.BufferOverflowException", "java.nio.BufferOverflowException"},
        {"jlang.ClosedChannelException", "java.nio.channels.ClosedChannelException"},
        {"jlang.CancelledKeyException", "java.nio.channels.CancelledKeyException"},
        {"jlang.UnknownHostException", "java.net.UnknownHostException"},
        {"jlang.SocketException", "java.net.SocketException"},
        {"jlang.SocketTimeoutException", "java.net.SocketTimeoutException"},
        {"jlang.GeneralSecurityException", "java.security.GeneralSecurityException"},
        {"jlang.NoSuchAlgorithmException", "java.security.NoSuchAlgorithmException"},
        {"jlang.SQLException", "java.sql.SQLException"},
        {"jlang.ParseException", "java.text.ParseException"},
        {"jlang.PrintStream", "java.io.PrintStream"},
        {"jlang.Random", "java.util.Random"},
        {"jlang.StringCharacterIterator", "java.text.StringCharacterIterator"},
        {"jlang.List", "java.util.ArrayList"}, {"jlang.Set", "java.util.LinkedHashSet"},
        {"jlang.Map", "java.util.LinkedHashMap"}, {"jlang.TreeMap", "java.util.TreeMap"},
        {"jlang.TreeSet", "java.util.TreeSet"}, {"jlang.Deque", "java.util.ArrayDeque"},
        {"jlang.PriorityQueue", "java.util.PriorityQueue"},
        {"jlang.ConcurrentHashMap", "java.util.concurrent.ConcurrentHashMap"},
        {"jlang.ConcurrentLinkedQueue", "java.util.concurrent.ConcurrentLinkedQueue"},
        {"jlang.Iterator", "java.util.Iterator"}, {"jlang.Comparator", "java.util.Comparator"},
        {"jlang.BitSet", "java.util.BitSet"},
        // every other jlang class (earlier entries win)
#include "JavaNames.inc"
    };
    return *names;
}

// Removes template argument lists ("a::B<x<y>>::C" -> "a::B::C").
std::string stripTemplateArgs(const std::string& s) {
    std::string out;
    int depth = 0;
    for (char c : s) {
        if (c == '<') {
            depth++;
        } else if (c == '>') {
            if (depth > 0) depth--;
        } else if (depth == 0) {
            out.push_back(c);
        }
    }
    return out;
}

std::string javaNameFromTypeInfo(const std::type_info& ti, bool& primitive) {
    int status = 0;
    char* dem = abi::__cxa_demangle(ti.name(), nullptr, nullptr, &status);
    std::string cpp = (status == 0 && dem != nullptr) ? std::string(dem) : std::string(ti.name());
    std::free(dem);
    const auto& known = knownNames();
    primitive = false;
    if (auto it = known.find(cpp); it != known.end()) {
        primitive = true;
        return it->second;
    }
    std::string s = stripTemplateArgs(cpp);
    // drop "(anonymous namespace)::"
    const std::string anon = "(anonymous namespace)::";
    for (size_t p; (p = s.find(anon)) != std::string::npos;) s.erase(p, anon.size());
    std::string dotted;
    dotted.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == ':' && i + 1 < s.size() && s[i + 1] == ':') {
            dotted.push_back('.');
            i++;
        } else {
            dotted.push_back(s[i]);
        }
    }
    if (auto it = known.find(dotted); it != known.end()) return it->second;
    return dotted;
}

std::string simpleNameOf(const std::string& javaName) {
    size_t p = javaName.find_last_of(".$");
    return p == std::string::npos ? javaName : javaName.substr(p + 1);
}

struct Registry {
    std::shared_mutex mu;
    std::unordered_map<std::type_index, Class*> byType;
    std::unordered_map<std::string, Class*> byName;
    std::vector<Class*> registered;
};

Registry& registry() {
    static Registry* r = new Registry();
    return *r;
}

}  // namespace

class ClassRegistryAccess {
public:
    static void setNames(Class* c, const std::string& name) {
        c->name_ = name;
        c->simpleName_ = simpleNameOf(name);
    }
    static const std::string& name(Class* c) { return c->name_; }
    static void setPrimitive(Class* c, bool p) { c->primitive_ = p; }
    static Class* super(Class* c) { return c->superclass_; }
    static const std::vector<Class*>& ifaces(Class* c) { return c->interfaces_; }
};

Class::Class(const std::type_info& ti, InstanceCheck check) : type_(&ti), check_(check) {}

Class* Class::forType(const std::type_info& ti, InstanceCheck check) {
    Registry& r = registry();
    const std::type_index key(ti);
    {
        std::shared_lock<std::shared_mutex> lock(r.mu);
        auto it = r.byType.find(key);
        if (it != r.byType.end()) {
            Class* c = it->second;
            if (check != nullptr && c->check_.load(std::memory_order_relaxed) == nullptr) {
                c->check_.store(check, std::memory_order_relaxed);
            }
            return c;
        }
    }
    bool primitive = false;
    std::string name = javaNameFromTypeInfo(ti, primitive);
    std::unique_lock<std::shared_mutex> lock(r.mu);
    auto it = r.byType.find(key);
    if (it != r.byType.end()) return it->second;
    auto* c = new Class(ti, check);
    ClassRegistryAccess::setNames(c, name);
    ClassRegistryAccess::setPrimitive(c, primitive);
    r.byType.emplace(key, c);
    r.byName.emplace(name, c);  // keeps an earlier registration of the same name
    return c;
}

Class* Class::findClass(const String& javaName) {
    Registry& r = registry();
    std::shared_lock<std::shared_mutex> lock(r.mu);
    auto it = r.byName.find(std::string(javaName));
    return it == r.byName.end() ? nullptr : it->second;
}

Class* Class::forName(const String& javaName) {
    Class* c = findClass(javaName);
    if (c == nullptr) throw ClassNotFoundException(javaName);
    return c;
}

std::vector<Class*> Class::getRegisteredClasses() {
    Registry& r = registry();
    std::shared_lock<std::shared_mutex> lock(r.mu);
    return r.registered;
}

Class* Class::registerClass(Class* cls, const String& javaName, Class* superclass,
                            std::vector<Class*> interfaces, uint32_t flags, Factory factory,
                            std::vector<AnnotationInit> annotations, const String* scriptRoot) {
    Registry& r = registry();
    std::unique_lock<std::shared_mutex> lock(r.mu);
    if (!javaName.isNull() && !javaName.isEmpty() && std::string(javaName) != cls->name_) {
        auto it = r.byName.find(cls->name_);
        if (it != r.byName.end() && it->second == cls) r.byName.erase(it);
        ClassRegistryAccess::setNames(cls, std::string(javaName));
    }
    r.byName[cls->name_] = cls;
    cls->superclass_ = superclass;
    cls->interfaces_ = std::move(interfaces);
    cls->flags_ = flags;
    cls->factory_ = factory;
    if (!annotations.empty()) {
        if (cls->annotations_ == nullptr) cls->annotations_ = new Annotations();
        for (auto& a : annotations) {
            AnnotationValues values;
            for (auto& kv : a.values) values[String(kv.first)] = String(kv.second);
            (*cls->annotations_)[String(a.name)] = std::move(values);
        }
    }
    if (scriptRoot != nullptr) cls->scriptRoot_ = std::string(*scriptRoot);
    if (!cls->registered_) {
        cls->registered_ = true;
        r.registered.push_back(cls);
    }
    return cls;
}

namespace {
template<class F>
Class* locked(Class* c, F f) {
    std::unique_lock<std::shared_mutex> lock(registry().mu);
    f();
    return c;
}
}  // namespace

Class* Class::setName(const String& javaName) {
    Registry& r = registry();
    std::unique_lock<std::shared_mutex> lock(r.mu);
    auto it = r.byName.find(name_);
    if (it != r.byName.end() && it->second == this) r.byName.erase(it);
    ClassRegistryAccess::setNames(this, std::string(javaName));
    r.byName[name_] = this;
    return this;
}
Class* Class::setSuperclass(Class* c) { return locked(this, [&] { superclass_ = c; }); }
Class* Class::addInterface(Class* c) { return locked(this, [&] { interfaces_.push_back(c); }); }
Class* Class::setAbstract(bool v) {
    return locked(this, [&] { flags_ = v ? (flags_ | ABSTRACT) : (flags_ & ~uint32_t(ABSTRACT)); });
}
Class* Class::setInterface(bool v) {
    return locked(this, [&] { flags_ = v ? (flags_ | INTERFACE) : (flags_ & ~uint32_t(INTERFACE)); });
}
Class* Class::setEnum(bool v) {
    return locked(this, [&] { flags_ = v ? (flags_ | ENUM) : (flags_ & ~uint32_t(ENUM)); });
}
Class* Class::setFactory(Factory f) { return locked(this, [&] { factory_ = f; }); }
Class* Class::addAnnotation(const String& name, const AnnotationValues& values) {
    return locked(this, [&] {
        if (annotations_ == nullptr) annotations_ = new Annotations();
        (*annotations_)[name] = values;
    });
}
Class* Class::setScriptRoot(const String& dir) { return locked(this, [&] { scriptRoot_ = std::string(dir); }); }

String Class::getName() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return String(name_);
}
String Class::getSimpleName() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return String(simpleName_);
}
String Class::getCanonicalName() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    std::string n = name_;
    for (char& c : n) {
        if (c == '$') c = '.';
    }
    return String(n);
}
String Class::getScriptRoot() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return scriptRoot_.empty() ? String() : String(scriptRoot_);
}
Class* Class::getSuperclass() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return superclass_;
}
std::vector<Class*> Class::getInterfaces() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return interfaces_;
}
bool Class::isAbstract() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return (flags_ & (ABSTRACT | INTERFACE)) != 0;
}
bool Class::isInterface() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return (flags_ & INTERFACE) != 0;
}
bool Class::isEnum() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return (flags_ & ENUM) != 0;
}
bool Class::isPrimitive() { return primitive_; }
bool Class::hasFactory() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return factory_ != nullptr;
}
bool Class::isRegistered() {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    return registered_;
}

bool Class::isInstance(Object* obj) {
    if (obj == nullptr) return false;
    if (InstanceCheck check = check_.load(std::memory_order_relaxed); check != nullptr) return check(obj);
    return isAssignableFrom(obj->getClass());
}

namespace {
bool assignableLocked(Class* target, Class* c, std::unordered_set<Class*>& seen, Class* (*super)(Class*),
                      const std::vector<Class*>& (*ifaces)(Class*)) {
    if (c == nullptr || !seen.insert(c).second) return false;
    if (c == target) return true;
    if (assignableLocked(target, super(c), seen, super, ifaces)) return true;
    for (Class* i : ifaces(c)) {
        if (assignableLocked(target, i, seen, super, ifaces)) return true;
    }
    return false;
}
}  // namespace

bool Class::isAssignableFrom(Class* other) {
    if (other == nullptr) throw NullPointerException();
    if (other == this) return true;
    if (this == Class::of<Object>()) return !other->primitive_;
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    std::unordered_set<Class*> seen;
    return assignableLocked(this, other, seen, &ClassRegistryAccess::super, &ClassRegistryAccess::ifaces);
}

Object* Class::newInstance() {
    Factory f;
    {
        std::shared_lock<std::shared_mutex> lock(registry().mu);
        f = factory_;
        if (f == nullptr || (flags_ & (ABSTRACT | INTERFACE)) != 0) f = nullptr;
    }
    if (f == nullptr) throw InstantiationException(getName());
    return f();
}

const Class::AnnotationValues* Class::getAnnotation(const String& name) {
    std::shared_lock<std::shared_mutex> lock(registry().mu);
    if (annotations_ == nullptr) return nullptr;
    auto it = annotations_->find(name);
    if (it != annotations_->end()) return &it->second;
    // Match "QuestHandler" against "a.b.QuestHandler" and vice versa.
    const std::string wanted = simpleNameOf(std::string(name));
    for (auto& [k, v] : *annotations_) {
        if (simpleNameOf(std::string(k)) == wanted) return &v;
    }
    return nullptr;
}

bool Class::isAnnotationPresent(const String& name) { return getAnnotation(name) != nullptr; }

String Class::toString() {
    if (primitive_) return getName();
    return str(isInterface() ? "interface " : "class ", getName());
}

}  // namespace jlang
