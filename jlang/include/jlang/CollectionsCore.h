// jlang/CollectionsCore.h - foundations shared by jlang::Array and the jlang collections:
//
//   * java.util.Iterator (also ListIterator and Enumeration), java.lang.Iterable (with a
//     range-for adapter), java.lang.Comparable, java.util.Comparator (+ lambda adapter),
//     jlang::Less (Java natural ordering as a C++ "less" functor);
//   * jlang::detail helpers: GC allocators for STL containers, Java element semantics
//     (javaEquals / javaHash / compareNatural / string conversion), the optional collection
//     lock, Java 6's stable legacy merge sort, out-of-line exception helpers.
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/Object.h>
#include <jlang/String.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace jlang {

template<class T> class Array;
template<class T> class Iterator;
template<class T> class Iterable;
template<class T> class Comparable;
template<class T> class Comparator;
template<class T> class Collection;
template<class T> class List;
template<class T> class Set;
template<class T> class TreeSet;
template<class T> class Deque;
template<class T> class PriorityQueue;
template<class K, class V> class Map;
template<class K, class V> class TreeMap;
template<class K, class V> class Entry;

namespace detail {

// ---------------------------------------------------------------------------------------
// Garbage-collected memory for container storage (defined in collections.cpp).
void* collMalloc(std::size_t n);        // GC_MALLOC: scanned by the collector, zero-filled
void* collMallocAtomic(std::size_t n);  // GC_MALLOC_ATOMIC: never scanned, NOT zero-filled

// Element types that can never hold a GC pointer: stored in unscanned ("atomic") memory so
// that large int/byte buffers neither cost scanning time nor cause false retention.
template<class T>
inline constexpr bool kPointerFree = std::is_arithmetic_v<T> || std::is_enum_v<T>;

// STL allocator on the GC heap. deallocate() is a no-op: the collector reclaims buffers that
// are no longer referenced (a reference into an old vector buffer keeps it valid).
template<class T>
struct GcAllocator {
    using value_type = T;
    using is_always_equal = std::true_type;
    GcAllocator() noexcept = default;
    template<class U>
    GcAllocator(const GcAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        if (n > static_cast<std::size_t>(-1) / sizeof(T)) throw std::bad_array_new_length();
        std::size_t bytes = n * sizeof(T);
        if (bytes == 0) bytes = 1;
        void* p = kPointerFree<T> ? collMallocAtomic(bytes) : collMalloc(bytes);
        if (p == nullptr) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T*, std::size_t) noexcept {}
    template<class U>
    bool operator==(const GcAllocator<U>&) const noexcept { return true; }
};

template<class T>
using Vec = std::vector<T, GcAllocator<T>>;

// ---------------------------------------------------------------------------------------
// Out-of-line exception helpers (collections.cpp), with Java's messages.
[[noreturn]] void throwIndexOutOfBounds(int32_t index, int32_t size);  // "Index: 5, Size: 3"
[[noreturn]] void throwIndexOutOfBoundsMsg(const std::string& msg);
[[noreturn]] void throwArrayIndexOutOfBounds(int32_t index);          // message "5" (JDK 6)
[[noreturn]] void throwArrayIndexOutOfBoundsMsg(const std::string& msg);
[[noreturn]] void throwArrayIndexOutOfBoundsNoMsg();
[[noreturn]] void throwNegativeArraySize(int32_t n);                   // message "-1"
[[noreturn]] void throwNoSuchElement();
[[noreturn]] void throwNoSuchElementMsg(const std::string& msg);
[[noreturn]] void throwIllegalState();
[[noreturn]] void throwIllegalStateMsg(const std::string& msg);
[[noreturn]] void throwIllegalArgument(const std::string& msg);
[[noreturn]] void throwIllegalArgumentNoMsg();
[[noreturn]] void throwUnsupportedOperation();
[[noreturn]] void throwUnsupportedOperationMsg(const std::string& msg);
[[noreturn]] void throwNullPointer();
[[noreturn]] void throwClassCastNotComparable(Object* o);  // "X cannot be cast to java.lang.Comparable"
[[noreturn]] void throwClassCastMsg(const std::string& msg);

// ---------------------------------------------------------------------------------------
// Pointer elements: the jlang::Object subobject of an element, for equals()/hashCode()/
// toString()/compareTo().
//
// The pointee may be INCOMPLETE where a collection's virtual functions get instantiated: GCC
// instantiates them (with the vtable) wherever the collection's constructor is used, e.g. by
// a generated default member initializer `jlang::List<Foo*>* l = new jlang::List<Foo*>();` in
// a header that only forward-declares Foo. A complete pointee is converted with static_cast;
// an incomplete one through the Itanium C++ ABI's RTTI (vptr -> dynamic type ->
// __dynamic_cast to jlang::Object, cached per vtable), which gives the same Object pointer.
// Pointees that are complete but not jlang::Object subclasses get identity semantics.
Object* objectOfIncomplete(const void* p);  // collections.cpp; p != nullptr, polymorphic pointee

template<class P>
constexpr bool pointeeIsObject() {
    using U = std::remove_cv_t<P>;
    if constexpr (std::is_same_v<U, Object>) return true;
    else if constexpr (requires { sizeof(U); }) return std::is_base_of_v<Object, U>;
    else return true;  // incomplete: a translated class (all derive from jlang::Object)
}

template<class P>
inline Object* elemObject(P* p) {
    using U = std::remove_cv_t<P>;
    if constexpr (std::is_same_v<U, Object>) {
        return const_cast<Object*>(p);
    } else if constexpr (requires { sizeof(U); }) {
        if constexpr (std::is_base_of_v<Object, U>) return static_cast<Object*>(const_cast<U*>(p));
        else return nullptr;
    } else {
        return p == nullptr ? nullptr : objectOfIncomplete(static_cast<const void*>(p));
    }
}

template<class T> struct IsStdOptional : std::false_type {};
template<class T> struct IsStdOptional<std::optional<T>> : std::true_type {};

// Value types with a Java null state (jlang::String, enum value classes, jlang::Entry...).
template<class T>
concept NullComparable = !std::is_pointer_v<T> && !std::is_arithmetic_v<T> && requires(const T& t) {
    { t == nullptr } -> std::convertible_to<bool>;
};

template<class T>
inline bool isNullValue(const T& v) {
    if constexpr (std::is_pointer_v<T>) {
        return v == nullptr;
    } else if constexpr (IsStdOptional<T>::value) {
        return !v.has_value();
    } else if constexpr (NullComparable<T>) {
        return static_cast<bool>(v == nullptr);
    } else {
        (void)v;
        return false;
    }
}

inline int32_t floatBits(float f) noexcept {  // Float.floatToIntBits (canonical NaN)
    if (f != f) return 0x7fc00000;
    return std::bit_cast<int32_t>(f);
}
inline int64_t doubleBits(double d) noexcept {  // Double.doubleToLongBits (canonical NaN)
    if (d != d) return INT64_C(0x7ff8000000000000);
    return std::bit_cast<int64_t>(d);
}

// Java equality of two elements: Object.equals for pointers (null-safe), Float/Double.equals
// (bit equality) for floating point, == for other primitives, equals()/== for value classes.
template<class T>
bool javaEquals(const T& a, const T& b) {
    if constexpr (std::is_pointer_v<T>) {
        if (a == b) return true;
        if (a == nullptr || b == nullptr) return false;
        if constexpr (pointeeIsObject<std::remove_pointer_t<T>>()) return elemObject(a)->equals(elemObject(b));
        else return false;
    } else if constexpr (std::is_same_v<T, float>) {
        return floatBits(a) == floatBits(b);
    } else if constexpr (std::is_same_v<T, double>) {
        return doubleBits(a) == doubleBits(b);
    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        return a == b;
    } else if constexpr (IsStdOptional<T>::value) {
        if (!a.has_value() || !b.has_value()) return a.has_value() == b.has_value();
        return javaEquals(*a, *b);
    } else {
        if constexpr (NullComparable<T>) {
            const bool an = static_cast<bool>(a == nullptr), bn = static_cast<bool>(b == nullptr);
            if (an || bn) return an == bn;
        }
        if constexpr (requires(T& x, const T& y) { { x.equals(y) } -> std::convertible_to<bool>; }) {
            return static_cast<bool>(const_cast<T&>(a).equals(b));
        } else if constexpr (requires { { a == b } -> std::convertible_to<bool>; }) {
            return static_cast<bool>(a == b);
        } else {
            return &a == &b;
        }
    }
}

// Java hashCode() of an element (0 for null), as the boxed Java value would compute it.
template<class T>
int32_t javaHash(const T& v) {
    if constexpr (std::is_pointer_v<T>) {
        if (v == nullptr) return 0;
        if constexpr (pointeeIsObject<std::remove_pointer_t<T>>()) {
            return elemObject(v)->hashCode();
        } else {
            const auto a = reinterpret_cast<uintptr_t>(v);
            return static_cast<int32_t>(a ^ (a >> 32));
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? 1231 : 1237;
    } else if constexpr (std::is_same_v<T, float>) {
        return floatBits(v);
    } else if constexpr (std::is_same_v<T, double>) {
        const int64_t b = doubleBits(v);
        return static_cast<int32_t>(b ^ static_cast<int64_t>(static_cast<uint64_t>(b) >> 32));
    } else if constexpr (std::is_integral_v<T> && sizeof(T) == 8) {
        const uint64_t u = static_cast<uint64_t>(v);
        return static_cast<int32_t>(u ^ (u >> 32));
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        return static_cast<int32_t>(v);
    } else if constexpr (IsStdOptional<T>::value) {
        return v.has_value() ? javaHash(*v) : 0;
    } else {
        if constexpr (NullComparable<T>) {
            if (static_cast<bool>(v == nullptr)) return 0;
        }
        if constexpr (requires(T& x) { { x.hashCode() } -> std::convertible_to<int32_t>; }) {
            return static_cast<int32_t>(const_cast<T&>(v).hashCode());
        } else if constexpr (requires { std::hash<T>{}(v); }) {
            return static_cast<int32_t>(std::hash<T>{}(v));
        } else {
            return 0;
        }
    }
}

// Java's contains(Object)/remove(Object)/indexOf(Object)/get(Object)... accept any object:
// for pointer elements the collections also take a pointer to a supertype (or an unrelated
// polymorphic class). An argument that is not an element type (dynamic_cast fails) is simply
// not found, as Java's equals() would say.
template<class T, class U>
concept SuperPointerArg = std::is_pointer_v<T> && !std::is_convertible_v<U*, T> && std::is_class_v<U> &&
                          requires(U* u) { dynamic_cast<std::remove_cv_t<std::remove_pointer_t<T>>*>(u); };

// The argument as a T, nullopt when it is not one.
template<class T, class U>
std::optional<T> downcastArg(U* o) {
    if (o == nullptr) return T(nullptr);
    auto* p = dynamic_cast<std::remove_cv_t<std::remove_pointer_t<T>>*>(o);
    if (p == nullptr) return std::nullopt;
    return T(p);
}

// Identity for removing the exact element an iterator returned (pointers: same object).
template<class T>
bool sameElement(const T& a, const T& b) {
    if constexpr (std::is_pointer_v<T>) return a == b;
    else return javaEquals(a, b);
}

// Appends String.valueOf(v) (Java string conversion) to out.
template<class T>
void appendElem(std::string& out, const T& v) {
    if constexpr (std::is_pointer_v<T>) {
        if (v == nullptr) {
            out.append("null", 4);
        } else if constexpr (pointeeIsObject<std::remove_pointer_t<T>>()) {
            appendString(out, elemObject(v)->toString());
        } else {
            char buf[2 + 2 * sizeof(void*) + 1];
            std::snprintf(buf, sizeof buf, "%p", static_cast<const void*>(v));
            out.append(buf);
        }
    } else if constexpr (Concatenable<T>) {
        appendValue(out, v);
    } else if constexpr (requires(T& x) { x.toString(); }) {
        if constexpr (NullComparable<T>) {
            if (static_cast<bool>(v == nullptr)) {
                out.append("null", 4);
                return;
            }
        }
        appendString(out, String(const_cast<T&>(v).toString()));
    } else {
        out.append("?", 1);
    }
}

template<class T>
String elemToString(const T& v) {
    std::string s;
    appendElem(s, v);
    return String(std::move(s));
}

// Double.compare / Float.compare: -0.0 < 0.0, NaN greater than everything, NaN == NaN.
template<class F>
inline int32_t javaCompareFloating(F a, F b) noexcept {
    if (a < b) return -1;
    if (a > b) return 1;
    if constexpr (std::is_same_v<F, float>) {
        const int32_t x = floatBits(a), y = floatBits(b);
        return x == y ? 0 : (x < y ? -1 : 1);
    } else {
        const int64_t x = doubleBits(static_cast<double>(a)), y = doubleBits(static_cast<double>(b));
        return x == y ? 0 : (x < y ? -1 : 1);
    }
}

// Natural-order support for pointers to objects whose static type does not expose
// compareTo (raw Comparable, jlang::Object*): every jlang::Comparable<T> implements it.
class ComparableAny : public virtual Object {
public:
    virtual int32_t _compareToAny(Object* other) = 0;
};

// Java natural ordering (Comparable.compareTo): arithmetic types by value (Boolean false <
// true, floating point like Double.compare), jlang::String by Java's compareTo (UTF-16 code
// unit order), enum value classes by ordinal, pointers through compareTo(). Null elements
// throw NullPointerException, elements without an ordering ClassCastException (as Java).
template<class T>
int32_t compareNatural(const T& a, const T& b) {
    if constexpr (std::is_same_v<T, bool>) {
        return a == b ? 0 : (a ? 1 : -1);
    } else if constexpr (std::is_floating_point_v<T>) {
        return javaCompareFloating(a, b);
    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        return a < b ? -1 : (b < a ? 1 : 0);
    } else if constexpr (std::is_pointer_v<T>) {
        if (a == nullptr || b == nullptr) throwNullPointer();
        if constexpr (requires { { a->compareTo(b) } -> std::convertible_to<int32_t>; }) {
            return static_cast<int32_t>(a->compareTo(b));
        } else if constexpr (pointeeIsObject<std::remove_pointer_t<T>>()) {
            Object* ao = elemObject(a);
            ComparableAny* c = dynamic_cast<ComparableAny*>(ao);
            if (c == nullptr) throwClassCastNotComparable(ao);
            return c->_compareToAny(elemObject(b));
        } else {
            throwClassCastMsg(std::string(typeid(T).name()) + " cannot be cast to java.lang.Comparable");
        }
    } else if constexpr (IsStdOptional<T>::value) {
        if (!a.has_value() || !b.has_value()) throwNullPointer();
        return compareNatural(*a, *b);
    } else if constexpr (requires(T& x, const T& y) { { x.compareTo(y) } -> std::convertible_to<int32_t>; }) {
        return static_cast<int32_t>(const_cast<T&>(a).compareTo(b));
    } else {
        throwClassCastMsg(std::string(typeid(T).name()) + " cannot be cast to java.lang.Comparable");
    }
}

// ---------------------------------------------------------------------------------------
// The optional collection lock: the collection's own jlang::Object monitor, so that Java's
// `synchronized (list) { ... }` (JSYNC(list)) and the synchronized collection methods use
// the same reentrant lock, exactly like Collections.synchronizedList/Vector/Hashtable.
class CollLock {
public:
    CollLock(Object* self, bool enabled) : o_(enabled ? self : nullptr) {
        if (o_ != nullptr) o_->monitorEnter();
    }
    CollLock(const CollLock&) = delete;
    CollLock& operator=(const CollLock&) = delete;
    ~CollLock() {
        if (o_ != nullptr) o_->monitorExit();
    }

private:
    Object* o_;
};

// ---------------------------------------------------------------------------------------
// Java 6 java.util.Arrays legacy merge sort (stable; the exact algorithm, so results match
// Java even for inconsistent comparators, and it never reads out of bounds).
// cmp(a, b) returns a Java compare() result.
inline constexpr int32_t kInsertionSortThreshold = 7;

template<class T, class Cmp>
void legacyMergeSort(T* src, T* dest, int32_t low, int32_t high, int32_t off, Cmp& c) {
    const int32_t length = high - low;
    if (length < kInsertionSortThreshold) {
        for (int32_t i = low; i < high; i++) {
            for (int32_t j = i; j > low && c(dest[j - 1], dest[j]) > 0; j--) {
                using std::swap;
                swap(dest[j], dest[j - 1]);
            }
        }
        return;
    }
    const int32_t destLow = low;
    const int32_t destHigh = high;
    low += off;
    high += off;
    const int32_t mid = static_cast<int32_t>(static_cast<uint32_t>(low + high) >> 1);
    legacyMergeSort(dest, src, low, mid, -off, c);
    legacyMergeSort(dest, src, mid, high, -off, c);
    if (c(src[mid - 1], src[mid]) <= 0) {
        for (int32_t i = 0; i < length; i++) dest[destLow + i] = src[low + i];
        return;
    }
    for (int32_t i = destLow, p = low, q = mid; i < destHigh; i++) {
        if (q >= high || (p < mid && c(src[p], src[q]) <= 0)) dest[i] = src[p++];
        else dest[i] = src[q++];
    }
}

// A GC-heap array of n copies used as sort scratch space (works for T = bool too).
template<class T>
T* tempCopy(const T* first, int32_t n) {
    T* p = GcAllocator<T>().allocate(static_cast<std::size_t>(n > 0 ? n : 1));
    std::uninitialized_copy(first, first + n, p);
    return p;
}

// Arrays.sort(a, fromIndex, toIndex, c) of Java 6 (stable legacy merge sort).
template<class T, class Cmp>
void stableSortRange(T* a, int32_t fromIndex, int32_t toIndex, Cmp c) {
    const int32_t n = toIndex - fromIndex;
    if (n < 2) return;
    T* aux = tempCopy(a + fromIndex, n);
    legacyMergeSort(aux, a, fromIndex, toIndex, -fromIndex, c);
}

template<class T>
struct NaturalCmp {
    int32_t operator()(const T& a, const T& b) const { return compareNatural(a, b); }
};

}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.util.Iterator<E> / ListIterator<E> / Enumeration<E>.
//
// Codebase classes implementing Iterator override hasNext()/next()/remove(). The ListIterator
// operations are supported by List iterators; the defaults throw UnsupportedOperationException.
template<class T>
class Iterator : public virtual Object {
public:
    virtual bool hasNext() = 0;
    virtual T next() = 0;
    virtual void remove() { detail::throwUnsupportedOperationMsg("remove"); }

    // java.util.Enumeration
    bool hasMoreElements() { return hasNext(); }
    T nextElement() { return next(); }

    // java.util.ListIterator
    virtual bool hasPrevious() { detail::throwUnsupportedOperation(); }
    virtual T previous() { detail::throwUnsupportedOperation(); }
    virtual int32_t nextIndex() { detail::throwUnsupportedOperation(); }
    virtual int32_t previousIndex() { detail::throwUnsupportedOperation(); }
    virtual void set(const T& e) {
        (void)e;
        detail::throwUnsupportedOperation();
    }
    virtual void add(const T& e) {
        (void)e;
        detail::throwUnsupportedOperation();
    }
};

namespace detail {

struct IterEnd {};

// Range-for adapter over a jlang::Iterator: `for (T x : *iterable)` performs exactly Java's
// `for (Iterator i = c.iterator(); i.hasNext();) { T x = i.next(); ... }`.
template<class T>
class IteratorRange {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    explicit IteratorRange(Iterator<T>* it) : it_(it) {}
    bool operator!=(IterEnd) const { return fetched_ || it_->hasNext(); }
    bool operator==(IterEnd e) const { return !(*this != e); }
    T& operator*() {
        if (!fetched_) {
            cur_ = it_->next();
            fetched_ = true;
        }
        return cur_;
    }
    IteratorRange& operator++() {
        if (!fetched_) (void)it_->next();
        fetched_ = false;
        return *this;
    }

private:
    Iterator<T>* it_;
    T cur_{};
    bool fetched_ = false;
};

}  // namespace detail

// java.lang.Iterable<T>. Range-for works on any Iterable (`for (auto x : *account)`).
template<class T>
class Iterable : public virtual Object {
public:
    virtual Iterator<T>* iterator() = 0;
    detail::IteratorRange<T> begin() { return detail::IteratorRange<T>(iterator()); }
    detail::IterEnd end() { return {}; }
    detail::IteratorRange<T> begin() const { return const_cast<Iterable*>(this)->begin(); }
    detail::IterEnd end() const { return {}; }
};

// java.lang.Comparable<T>: `class Foo : public virtual jlang::Comparable<Foo*>` with
// `int32_t compareTo(Foo* o) override`.
template<class T>
class Comparable : public virtual detail::ComparableAny {
public:
    virtual int32_t compareTo(T o) = 0;
    int32_t _compareToAny(Object* other) override {
        if constexpr (std::is_pointer_v<T> && std::is_class_v<std::remove_pointer_t<T>>) {
            using U = std::remove_cv_t<std::remove_pointer_t<T>>;
            U* u = nullptr;
            if (other != nullptr) {
                u = dynamic_cast<U*>(other);
                if (u == nullptr) detail::throwClassCastMsg(std::string(typeid(*other).name()) + " is not comparable here");
            }
            return compareTo(u);
        } else {
            (void)other;
            detail::throwClassCastMsg("not comparable to an object");
        }
    }
};

// java.util.Comparator<T>. Anonymous comparators: jlang::Comparator<T>::of([](T a, T b) { ... })
// (the lambda returns a Java compare() int, not a bool).
template<class T>
class Comparator : public virtual Object {
public:
    virtual int32_t compare(T o1, T o2) = 0;
    template<class F>
    static Comparator<T>* of(F f);
};

namespace detail {
template<class T, class F>
class ComparatorLambda final : public virtual Comparator<T> {
public:
    explicit ComparatorLambda(F f) : f_(std::move(f)) {}
    int32_t compare(T a, T b) override { return static_cast<int32_t>(f_(a, b)); }

private:
    F f_;
};

// Adapts a Comparator<U>* (Comparator<? super T>) or natural order (nullptr) to a functor.
template<class T, class U>
struct PtrCmp {
    Comparator<U>* c;
    int32_t operator()(const T& a, const T& b) const {
        if (c == nullptr) return compareNatural(a, b);
        return c->compare(a, b);
    }
};

// Adapts a lambda returning a Java compare() int.
template<class F>
struct FnCmp {
    F* f;
    template<class A, class B>
    int32_t operator()(const A& a, const B& b) const {
        return static_cast<int32_t>((*f)(a, b));
    }
};

// A callable usable as a Java comparator (returns an int, not a bool). Constraint
// conjunctions short-circuit, so non-callables are rejected without hard errors.
template<class F, class T>
concept JavaCmpFn =
    !std::is_pointer_v<std::decay_t<F>> && requires(F& f, const T& a, const T& b) {
        { f(a, b) } -> std::convertible_to<int32_t>;
    } && !std::is_same_v<std::remove_cvref_t<decltype(std::declval<F&>()(std::declval<const T&>(), std::declval<const T&>()))>, bool>;
template<class F, class T>
inline constexpr bool IsJavaCmpFn = JavaCmpFn<F, T>;
}  // namespace detail

template<class T>
template<class F>
Comparator<T>* Comparator<T>::of(F f) {
    static_assert(!std::is_same_v<std::invoke_result_t<F&, T, T>, bool>,
                  "Comparator::of expects a Java compare() function returning int, not a bool");
    return new detail::ComparatorLambda<T, F>(std::move(f));
}

// Java natural ordering as a C++ strict-weak-order functor (see detail::compareNatural).
template<class T>
struct Less {
    bool operator()(const T& a, const T& b) const { return detail::compareNatural(a, b) < 0; }
};

}  // namespace jlang
