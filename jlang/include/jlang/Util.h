// jlang/Util.h - language helpers used by translated code:
//   JFINALLY { ... };      finally blocks (scope guard)
//   JASSERT(cond)          Java assert (active with -DJLANG_ASSERTS)
//   jlang::ushr(x, n)      Java >>> for int and long
//   jlang::d2i/d2l/f2i/f2l Java's saturating float->integer casts
//   jlang::Hash<T>, jlang::Equal<T>, jlang::hashCodeOf(x), jlang::equalsOf(a, b)
//                          Java hashCode()/equals() for any mapped type (collections use them)
//   jlang::box(x) / jlang::unbox<T>(o)   primitives, Strings and enums as Objects
//
// Part of the jlang core: translated code includes <jlang/jlang.h>.
#pragma once

#include <jlang/Boxes.h>
#include <jlang/Exceptions.h>
#include <jlang/Object.h>
#include <jlang/String.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace jlang {

// ---------------------------------------------------------------------------------------
// finally
//
//   { JFINALLY { DatabaseFactory::close(con); };
//     ...try body...
//   }
//
// The block runs when the enclosing scope exits (normally, by return/break/continue or by an
// exception). It captures by reference, so it sees the current values of locals, like Java.
// If the block throws while the scope is left normally the exception propagates (as in Java);
// while unwinding because of another exception it is swallowed (C++ cannot replace an
// in-flight exception).
namespace detail {
template<class F>
class ScopeExit {
public:
    explicit ScopeExit(F&& f) : f_(std::forward<F>(f)), uncaught_(std::uncaught_exceptions()) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit() noexcept(false) {
        if (std::uncaught_exceptions() > uncaught_) {
            try {
                f_();
            } catch (...) {
            }
        } else {
            f_();
        }
    }

private:
    F f_;
    int uncaught_;
};
struct ScopeExitMaker {
    template<class F>
    ScopeExit<F> operator+(F&& f) const {
        return ScopeExit<F>(std::forward<F>(f));
    }
};
[[noreturn]] void assertionFailed(const char* expr, const char* file, int line);
[[noreturn]] void assertionFailed(const char* expr, const char* file, int line, const String& message);
}  // namespace detail

#define JFINALLY \
    auto JLANG_CONCAT(_jfinally_, __COUNTER__) = ::jlang::detail::ScopeExitMaker{} + [&]()

// Java `assert cond;` -> JASSERT(cond);  `assert cond : msg;` -> JASSERT_MSG(cond, msg);
// Disabled (condition not evaluated) unless compiled with -DJLANG_ASSERTS.
#ifdef JLANG_ASSERTS
#define JASSERT(...) \
    do { \
        if (!(__VA_ARGS__)) ::jlang::detail::assertionFailed(#__VA_ARGS__, __FILE__, __LINE__); \
    } while (0)
#define JASSERT_MSG(cond, msg) \
    do { \
        if (!(cond)) ::jlang::detail::assertionFailed(#cond, __FILE__, __LINE__, ::jlang::str(msg)); \
    } while (0)
#else
#define JASSERT(...) \
    do { \
        (void)sizeof(!(__VA_ARGS__)); \
    } while (0)
#define JASSERT_MSG(cond, msg) \
    do { \
        (void)sizeof(!(cond)); \
    } while (0)
#endif

// ---------------------------------------------------------------------------------------
// Java >>> (shift count masked like Java: & 31 for int, & 63 for long). byte/short/char
// operands are promoted to int first, as in Java.
inline int32_t ushr(int32_t x, int32_t n) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(x) >> (n & 31));
}
inline int64_t ushr(int64_t x, int32_t n) noexcept {
    return static_cast<int64_t>(static_cast<uint64_t>(x) >> (n & 63));
}
template<class T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
inline auto ushr(T x, int32_t n) noexcept {
    if constexpr (sizeof(T) <= 4 && (std::is_signed_v<T> || sizeof(T) < 4)) {
        return ushr(static_cast<int32_t>(x), n);
    } else {
        return ushr(static_cast<int64_t>(x), n);
    }
}
// Java << and >> with the shift count masked (use where the count can be >= the width).
inline int32_t shl(int32_t x, int32_t n) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(x) << (n & 31));
}
inline int64_t shl(int64_t x, int32_t n) noexcept {
    return static_cast<int64_t>(static_cast<uint64_t>(x) << (n & 63));
}
inline int32_t shr(int32_t x, int32_t n) noexcept { return x >> (n & 31); }
inline int64_t shr(int64_t x, int32_t n) noexcept { return x >> (n & 63); }

// ---------------------------------------------------------------------------------------
// Java floating-point to integer conversions: NaN -> 0, saturate at the type's range.
inline int32_t d2i(double v) noexcept {
    if (v != v) return 0;
    if (v >= 2147483647.0) return std::numeric_limits<int32_t>::max();
    if (v <= -2147483648.0) return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(v);
}
inline int64_t d2l(double v) noexcept {
    if (v != v) return 0;
    if (v >= 9223372036854775807.0) return std::numeric_limits<int64_t>::max();
    if (v <= -9223372036854775808.0) return std::numeric_limits<int64_t>::min();
    return static_cast<int64_t>(v);
}
inline int32_t f2i(float v) noexcept { return d2i(static_cast<double>(v)); }
inline int64_t f2l(float v) noexcept { return d2l(static_cast<double>(v)); }
// Java (int)/(long) of a double/float with a narrowing to short/byte/char going through int.
inline int16_t d2s(double v) noexcept { return static_cast<int16_t>(d2i(v)); }
inline int8_t d2b(double v) noexcept { return static_cast<int8_t>(d2i(v)); }
inline char16_t d2c(double v) noexcept { return static_cast<char16_t>(d2i(v)); }

// Java integer division/remainder that throw ArithmeticException("/ by zero") instead of
// trapping, and give Java's result for MIN_VALUE / -1.
namespace detail {
[[noreturn]] void throwDivideByZero();
}
template<class T>
    requires std::is_integral_v<T>
inline T idiv(T a, T b) {
    if (b == 0) detail::throwDivideByZero();
    if constexpr (std::is_signed_v<T>) {
        if (b == -1) return static_cast<T>(-a);  // -fwrapv: MIN_VALUE / -1 == MIN_VALUE
    }
    return static_cast<T>(a / b);
}
template<class T>
    requires std::is_integral_v<T>
inline T irem(T a, T b) {
    if (b == 0) detail::throwDivideByZero();
    if constexpr (std::is_signed_v<T>) {
        if (b == -1) return 0;
    }
    return static_cast<T>(a % b);
}

// ---------------------------------------------------------------------------------------
// hashCode / equals for any mapped type
//
//   arithmetic   : Java's Integer/Long/Float/Double/Boolean/Character.hashCode and equals
//                  (float/double compare by bits: NaN equals NaN, 0.0 != -0.0)
//   enums (C++)  : underlying value
//   jlang::String: String.hashCode()/equals()
//   T* (Object)  : virtual hashCode()/equals() (identity by default), null-safe
//   value classes: `hashCode() const` / `equals(const T&) const` (codebase enums) or operator==
//   std::optional: value or 0 / both empty
namespace detail {
template<class T>
concept HasHashCodeMember = requires(T& t) {
    { t.hashCode() } -> std::convertible_to<int32_t>;
};
template<class T>
concept HasEqualsMember = requires(T& a, T& b) {
    { a.equals(b) } -> std::convertible_to<bool>;
};
template<class T>
concept HasStdHash = requires(const T& t) {
    { std::hash<T>()(t) } -> std::convertible_to<size_t>;
};
template<class T>
concept HasEqOp = requires(const T& a, const T& b) {
    { a == b } -> std::convertible_to<bool>;
};
}  // namespace detail

template<class T>
int32_t hashCodeOf(const T& v) {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        return v ? 1231 : 1237;
    } else if constexpr (std::is_same_v<D, float>) {
        return Float::floatToIntBits(v);
    } else if constexpr (std::is_same_v<D, double>) {
        return Double::hashCode(v);
    } else if constexpr (std::is_integral_v<D> && sizeof(D) <= 4) {
        return static_cast<int32_t>(v);
    } else if constexpr (std::is_integral_v<D>) {
        return Long::hashCode(static_cast<int64_t>(v));
    } else if constexpr (std::is_enum_v<D>) {
        return hashCodeOf(static_cast<std::underlying_type_t<D>>(v));
    } else if constexpr (std::is_same_v<D, String>) {
        return v.hashCode();
    } else if constexpr (std::is_same_v<D, std::nullptr_t>) {
        return 0;
    } else if constexpr (std::is_pointer_v<D> && std::is_class_v<std::remove_pointer_t<D>>) {
        return v == nullptr ? 0 : detail::asObject(v)->hashCode();
    } else if constexpr (detail::IsOptional<D>::value) {
        return v.has_value() ? hashCodeOf(*v) : 0;
    } else if constexpr (detail::HasHashCodeMember<D>) {
        return static_cast<int32_t>(const_cast<D&>(v).hashCode());
    } else if constexpr (std::is_pointer_v<D>) {
        return static_cast<int32_t>(std::hash<D>()(v));
    } else if constexpr (detail::HasStdHash<D>) {
        size_t h = std::hash<D>()(v);
        return static_cast<int32_t>(h ^ (h >> 32));
    } else {
        static_assert(sizeof(D) == 0, "jlang::hashCodeOf: no hash for this type");
    }
}

template<class T>
bool equalsOf(const T& a, const T& b) {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<D, float>) {
        return Float::floatToIntBits(a) == Float::floatToIntBits(b);
    } else if constexpr (std::is_same_v<D, double>) {
        return Double::doubleToLongBits(a) == Double::doubleToLongBits(b);
    } else if constexpr (std::is_arithmetic_v<D> || std::is_enum_v<D>) {
        return a == b;
    } else if constexpr (std::is_same_v<D, String>) {
        return a == b;  // null-aware content equality
    } else if constexpr (std::is_pointer_v<D> && std::is_class_v<std::remove_pointer_t<D>>) {
        if (a == b) return true;
        if (a == nullptr || b == nullptr) return false;
        return detail::asObject(a)->equals(detail::asObject(b));
    } else if constexpr (detail::IsOptional<D>::value) {
        if (a.has_value() != b.has_value()) return false;
        return !a.has_value() || equalsOf(*a, *b);
    } else if constexpr (detail::HasEqualsMember<D>) {
        return static_cast<bool>(const_cast<D&>(a).equals(const_cast<D&>(b)));
    } else if constexpr (detail::HasEqOp<D>) {
        return static_cast<bool>(a == b);
    } else {
        static_assert(sizeof(D) == 0, "jlang::equalsOf: no equality for this type");
    }
}

template<class T>
struct Hash {
    size_t operator()(const T& v) const { return static_cast<size_t>(static_cast<uint32_t>(hashCodeOf(v))); }
};
template<class T>
struct Equal {
    bool operator()(const T& a, const T& b) const { return equalsOf(a, b); }
};

// ---------------------------------------------------------------------------------------
// Boxing: Java primitives/Strings/enums stored as Object (CONVENTIONS §4, §7).
//
// A Java enum value class E (codebase enum: has ordinal()/name()) boxes to EnumBox<E>, one
// shared box per constant (so identity comparisons work as in Java). EnumBox forwards
// toString/hashCode/equals to the constant. An enum that implements an interface gets a
// generated specialization `template<> class jlang::EnumBox<E> : ..., public virtual I`
// (primary template's second parameter is for that purpose).
namespace detail {
template<class E>
concept JavaEnum = requires(const E& e) {
    { e.ordinal() } -> std::convertible_to<int32_t>;
    e.name();
    e.toString();
};
}  // namespace detail

template<class E, class = void>
class EnumBox : public virtual Object {
public:
    explicit EnumBox(E v) : value(v) {}
    String toString() override { return String(value.toString()); }
    int32_t hashCode() override {
        if constexpr (requires(const E& e) { { e.hashCode() } -> std::convertible_to<int32_t>; }) {
            return static_cast<int32_t>(value.hashCode());
        } else {
            return Object::hashCode();  // one box per constant: identity, like a Java enum
        }
    }
    bool equals(Object* o) override {
        if (o == this) return true;
        auto* b = dynamic_cast<EnumBox*>(o);
        return b != nullptr && b->value == value;
    }
    E get() const { return value; }
    int32_t ordinal() const { return value.ordinal(); }
    String name() const { return String(value.name()); }
    E value;
};

namespace detail {
template<class E>
EnumBox<E>* enumBoxFor(E e) {
    static std::mutex mu;
    static std::unordered_map<int32_t, EnumBox<E>*>* boxes = new std::unordered_map<int32_t, EnumBox<E>*>();
    std::lock_guard<std::mutex> lock(mu);
    auto& slot = (*boxes)[e.ordinal()];
    if (slot == nullptr) slot = new EnumBox<E>(e);
    return slot;
}
}  // namespace detail

inline Integer* box(int32_t v) { return Integer::valueOf(v); }
inline Long* box(int64_t v) { return Long::valueOf(v); }
inline Short* box(int16_t v) { return Short::valueOf(v); }
inline Byte* box(int8_t v) { return Byte::valueOf(v); }
inline Float* box(float v) { return Float::valueOf(v); }
inline Double* box(double v) { return Double::valueOf(v); }
inline Boolean* box(bool v) { return Boolean::valueOf(v); }
inline Character* box(char16_t v) { return Character::valueOf(v); }
inline Character* box(char v) { return Character::valueOf(static_cast<char16_t>(static_cast<unsigned char>(v))); }
inline Object* box(std::nullptr_t) { return nullptr; }
inline StringBox* box(const String& s) { return s.isNull() ? nullptr : new StringBox(s); }
inline StringBox* box(const char* s) { return s == nullptr ? nullptr : new StringBox(String(s)); }
inline StringBox* box(const std::string& s) { return new StringBox(String(s)); }
template<class T>
    requires std::is_class_v<T>
inline Object* box(T* p) {
    return p == nullptr ? nullptr : detail::asObject(p);
}
template<class T>
inline Object* box(const std::optional<T>& v) {
    if (!v.has_value()) return nullptr;
    return box(*v);
}
template<class E>
    requires detail::JavaEnum<E>
inline Object* box(const E& e) {
    if constexpr (requires { e == nullptr; }) {
        if (e == nullptr) return nullptr;
    }
    return detail::enumBoxFor<E>(e);
}

// Unboxing (Java's implicit (Integer) o -> int etc.): NullPointerException for null,
// ClassCastException for a box of another type. unbox<String> accepts a StringBox;
// unbox<E> (codebase enum) accepts an EnumBox<E>. unbox<T*> is jlang::cast<T>.
template<class T>
T unbox(Object* o) {
    if constexpr (std::is_pointer_v<T>) {
        return cast<std::remove_pointer_t<T>>(o);
    } else {
        if (o == nullptr) detail::throwNullPointerException();
        if constexpr (std::is_same_v<T, int32_t>) {
            return cast<Integer>(o)->value;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return cast<Long>(o)->value;
        } else if constexpr (std::is_same_v<T, int16_t>) {
            return cast<Short>(o)->value;
        } else if constexpr (std::is_same_v<T, int8_t>) {
            return cast<Byte>(o)->value;
        } else if constexpr (std::is_same_v<T, float>) {
            return cast<Float>(o)->value;
        } else if constexpr (std::is_same_v<T, double>) {
            return cast<Double>(o)->value;
        } else if constexpr (std::is_same_v<T, bool>) {
            return cast<Boolean>(o)->value;
        } else if constexpr (std::is_same_v<T, char16_t>) {
            return cast<Character>(o)->value;
        } else if constexpr (std::is_same_v<T, String>) {
            return cast<StringBox>(o)->value;
        } else if constexpr (detail::JavaEnum<T>) {
            return cast<EnumBox<T>>(o)->value;
        } else {
            static_assert(sizeof(T) == 0, "jlang::unbox: unsupported type");
        }
    }
}
// Unboxing to std::optional (Java Integer fields): null -> nullopt.
template<class T>
std::optional<T> unboxOptional(Object* o) {
    if (o == nullptr) return std::nullopt;
    return unbox<T>(o);
}

}  // namespace jlang
