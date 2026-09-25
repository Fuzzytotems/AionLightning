// jlang/Object.h - java.lang.Object, java.lang.Class, monitors (synchronized/wait/notify),
// checked casts (jlang::cast / jlang::instanceof) and java.lang.Runnable.
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace jlang {

class String;
class Class;
class Object;

namespace detail {
class Monitor;
// Out-of-line throw helpers (keep <jlang/Exceptions.h> out of this header).
[[noreturn]] void throwNullPointerException();
[[noreturn]] void throwNullPointerException(const char* msg);
[[noreturn]] void throwClassCastException(Object* from, const std::type_info& to);
// Returns true when p points into the collectable GC heap (GC_base(p) != nullptr).
bool inGcHeap(const void* p) noexcept;
}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.lang.Object
//
// Root of every translated class. Inherit it with `public virtual jlang::Object` (see
// CONVENTIONS §5.1): there is exactly one Object subobject per object, and its address is
// the object's identity.
//
// * hashCode()/equals()/toString() are virtual with Java defaults (identity hash, identity
//   equality, "java.class.Name@hexhash").
// * Every object has a lazily created reentrant monitor for JSYNC(obj) { ... },
//   wait()/notify()/notifyAll() with Java semantics (wait releases all recursion levels and
//   reacquires them; IllegalMonitorStateException when the monitor is not held;
//   InterruptedException when the waiting jlang::Thread is interrupted).
// * Copying an Object (copy constructor of a subclass, e.g. a translated clone()) never
//   copies the monitor: the copy gets its own.
class Object {
public:
    Object() noexcept = default;
    Object(const Object&) noexcept {}
    Object& operator=(const Object&) noexcept { return *this; }
    virtual ~Object();

    virtual int32_t hashCode();
    virtual bool equals(Object* o);
    virtual String toString();
    // Java Object.clone(): shallow copies cannot be made generically in C++. A translated
    // `super.clone()` becomes `new Foo(*this)` in the subclass. The default throws
    // CloneNotSupportedException, as Java does for classes that are not Cloneable.
    virtual Object* clone();
    // Java Object.finalize(): kept so `super.finalize()` compiles. The collector never calls it.
    virtual void finalize() {}

    // Runtime class (from the dynamic C++ type; metadata filled by the class registry).
    Class* getClass();

    void wait();
    void wait(int64_t timeoutMillis);
    void wait(int64_t timeoutMillis, int32_t nanos);
    void notify();
    void notifyAll();

    // Monitor primitives (used by MonitorGuard / JSYNC and by jlang::Thread::holdsLock).
    void monitorEnter();
    void monitorExit();
    bool monitorHeldByCurrentThread();

    // The identity hash (what System.identityHashCode returns): stable 32-bit value per object.
    int32_t identityHashCode() const noexcept {
        uint64_t a = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
        a ^= a >> 33;
        a *= 0xff51afd7ed558ccdULL;
        a ^= a >> 33;
        a *= 0xc4ceb9fe1a85ec53ULL;
        a ^= a >> 33;
        return static_cast<int32_t>(a);
    }

private:
    detail::Monitor* monitor();
    // Monitor pointer; bit 0 set = allocated uncollectable (object lives outside the GC heap,
    // e.g. a thrown exception) and freed by ~Object.
    std::atomic<uintptr_t> monitor_{0};
};

// System.identityHashCode(o): 0 for null.
inline int32_t identityHashCode(Object* o) noexcept { return o ? o->identityHashCode() : 0; }

// ---------------------------------------------------------------------------------------
// java.lang.Class
//
// One interned Class per C++ dynamic type (std::type_info). Names are derived from the
// demangled C++ name ('::' -> '.', template arguments dropped, jlang types mapped to their
// Java names, primitives to "int", "long", ...). The generator-emitted class registry
// (CONVENTIONS §13) refines the metadata through registerClass(): the exact Java name,
// superclass, interfaces, abstract/interface flags, annotations, a factory for the public
// no-arg constructor and the script root directory for classes compiled from scripts.
class Class final : public virtual Object {
public:
    using Factory = Object* (*)();
    using InstanceCheck = bool (*)(Object*);
    using AnnotationValues = std::map<String, String>;
    using Annotations = std::map<String, AnnotationValues>;

    // X.class
    template<class T> static Class* of();
    // Class for a runtime type.
    static Class* forType(const std::type_info& ti, InstanceCheck check = nullptr);

    // Class.forName(name): looks the Java FQN up in the registry (registered classes and every
    // class whose Class object was created so far). Throws ClassNotFoundException.
    static Class* forName(const String& javaName);
    // Like forName but returns nullptr instead of throwing.
    static Class* findClass(const String& javaName);
    // Every Class registered through registerClass (for the script loader).
    static std::vector<Class*> getRegisteredClasses();

    enum Flags : uint32_t { NONE = 0, ABSTRACT = 1, INTERFACE = 2, ENUM = 4, FINAL = 8 };
    struct AnnotationInit {
        const char* name;
        std::vector<std::pair<const char*, const char*>> values;
    };
    // Registry hook used by generated code:
    //   static const bool _reg = (jlang::Class::registerClass<Foo>("a.b.Foo",
    //       jlang::Class::of<Base>(), {jlang::Class::of<I1>()}, jlang::Class::ABSTRACT,
    //       nullptr, {{"QuestHandler", {{"value", "x"}}}}), true);
    // A null factory means "no public no-arg constructor" (newInstance throws).
    static Class* registerClass(Class* cls, const String& javaName, Class* superclass,
                                std::vector<Class*> interfaces, uint32_t flags, Factory factory,
                                std::vector<AnnotationInit> annotations = {},
                                const String* scriptRoot = nullptr);
    template<class T>
    static Class* registerClass(const String& javaName, Class* superclass,
                                std::vector<Class*> interfaces, uint32_t flags, Factory factory,
                                std::vector<AnnotationInit> annotations = {},
                                const String* scriptRoot = nullptr) {
        return registerClass(of<T>(), javaName, superclass, std::move(interfaces), flags, factory,
                             std::move(annotations), scriptRoot);
    }

    // Individual setters (also usable by hand-written registration code). Return this.
    Class* setName(const String& javaName);
    Class* setSuperclass(Class* c);
    Class* addInterface(Class* c);
    Class* setAbstract(bool v);
    Class* setInterface(bool v);
    Class* setEnum(bool v);
    Class* setFactory(Factory f);
    Class* addAnnotation(const String& name, const AnnotationValues& values);
    Class* setScriptRoot(const String& dir);

    String getName();
    String getSimpleName();
    String getCanonicalName();
    String getScriptRoot();
    Class* getSuperclass();
    std::vector<Class*> getInterfaces();
    bool isAbstract();
    bool isInterface();
    bool isEnum();
    bool isPrimitive();
    bool hasFactory();
    // Registry metadata was provided (registerClass was called for this class).
    bool isRegistered();

    // obj != null && obj is an instance of this class. Exact for classes obtained with
    // Class::of<T>() (dynamic_cast); otherwise falls back to isAssignableFrom(obj->getClass()).
    bool isInstance(Object* obj);
    // Walks registry metadata (superclass chain and interfaces). Without metadata only
    // identity (and java.lang.Object) are known.
    bool isAssignableFrom(Class* other);
    // Class.newInstance(): calls the registered factory; InstantiationException if none.
    Object* newInstance();
    // Annotation lookup by simple name ("QuestHandler") or fully qualified name.
    const AnnotationValues* getAnnotation(const String& name);
    bool isAnnotationPresent(const String& name);

    const std::type_info& typeInfo() const noexcept { return *type_; }

    // "class a.b.C" / "interface a.b.I" (Java Class.toString()).
    String toString() override;

    Class(const std::type_info& ti, InstanceCheck check);  // use of<T>()/forType()

private:
    friend class ClassRegistryAccess;
    const std::type_info* type_;
    std::atomic<InstanceCheck> check_;
    // Metadata; guarded by the registry lock. Strings are kept as std::string to keep this
    // header independent of <jlang/String.h>.
    std::string name_;
    std::string simpleName_;
    std::string scriptRoot_;
    Class* superclass_ = nullptr;
    std::vector<Class*> interfaces_;
    uint32_t flags_ = 0;
    bool primitive_ = false;
    bool registered_ = false;
    Factory factory_ = nullptr;
    Annotations* annotations_ = nullptr;
};

namespace detail {
template<class T>
bool instanceCheck(Object* o) {
    if constexpr (std::is_class_v<T> && std::is_polymorphic_v<T> && std::is_base_of_v<Object, T>) {
        return o != nullptr && dynamic_cast<T*>(o) != nullptr;
    } else {
        (void)o;
        return false;
    }
}
}  // namespace detail

template<class T>
Class* Class::of() {
    using U = std::remove_cv_t<T>;
    static Class* const cls = Class::forType(typeid(U), &detail::instanceCheck<U>);
    return cls;
}

// ---------------------------------------------------------------------------------------
// Casts. Java `(T) x` -> jlang::cast<T>(x): dynamic_cast; null stays null; throws
// ClassCastException ("a.b.X cannot be cast to a.b.T") when x is not a T.
// Java `x instanceof T` -> jlang::instanceof<T>(x).
// Both accept any pointer to a polymorphic class (classes and interfaces alike).
namespace detail {
template<class U>
inline Object* asObject(U* p) {
    if constexpr (std::is_convertible_v<U*, Object*>) {
        return static_cast<Object*>(p);
    } else {
        return dynamic_cast<Object*>(p);
    }
}
}  // namespace detail

template<class T, class U>
inline T* cast(U* p) {
    static_assert(std::is_polymorphic_v<U>, "jlang::cast needs a pointer to a polymorphic class");
    if (p == nullptr) return nullptr;
    if constexpr (std::is_base_of_v<T, U>) {
        return static_cast<T*>(p);
    } else {
        T* r = dynamic_cast<T*>(p);
        if (r == nullptr) detail::throwClassCastException(detail::asObject(p), typeid(T));
        return r;
    }
}
template<class T>
inline T* cast(std::nullptr_t) { return nullptr; }

template<class T, class U>
inline bool instanceof(U* p) {
    static_assert(std::is_polymorphic_v<U>, "jlang::instanceof needs a pointer to a polymorphic class");
    if (p == nullptr) return false;
    if constexpr (std::is_base_of_v<T, U>) {
        return true;
    } else {
        return dynamic_cast<T*>(p) != nullptr;
    }
}
template<class T>
inline bool instanceof(std::nullptr_t) { return false; }

// ---------------------------------------------------------------------------------------
// synchronized
//
//   JSYNC(obj) { ... }        // Java: synchronized (obj) { ... }
//
// Reentrant; released on every exit path (return, break, continue, exception). `obj` may be a
// pointer to any class or interface derived from jlang::Object; null throws
// NullPointerException (as Java does).
class MonitorGuard {
public:
    template<class U>
    explicit MonitorGuard(U* p) : obj_(detail::asObject(p)) { enter(); }
    explicit MonitorGuard(Object* p) : obj_(p) { enter(); }
    MonitorGuard(const MonitorGuard&) = delete;
    MonitorGuard& operator=(const MonitorGuard&) = delete;
    ~MonitorGuard() { obj_->monitorExit(); }
    Object* object() const noexcept { return obj_; }

private:
    void enter() {
        if (obj_ == nullptr) detail::throwNullPointerException();
        obj_->monitorEnter();
    }
    Object* obj_;
};

#define JLANG_CONCAT_(a, b) a##b
#define JLANG_CONCAT(a, b) JLANG_CONCAT_(a, b)
// if-with-initializer + empty then-branch: the user's block is the else-branch, so a
// following `else` can never bind to it; break/continue reach the enclosing loop.
#define JSYNC(obj) \
    if (::jlang::MonitorGuard JLANG_CONCAT(_jsync_guard_, __COUNTER__){(obj)}; false) { \
    } else

// ---------------------------------------------------------------------------------------
// java.lang.Runnable
class Runnable : public virtual Object {
public:
    virtual void run() = 0;
    // Lambda adapter for anonymous classes: jlang::Runnable::of([=, this]() { ... }).
    template<class F> static Runnable* of(F f);
};

namespace detail {
template<class F>
class RunnableLambda final : public virtual Runnable {
public:
    explicit RunnableLambda(F f) : f_(std::move(f)) {}
    void run() override { f_(); }

private:
    F f_;
};
}  // namespace detail

template<class F>
Runnable* Runnable::of(F f) {
    return new detail::RunnableLambda<F>(std::move(f));
}

}  // namespace jlang
