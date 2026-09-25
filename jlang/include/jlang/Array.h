// jlang/Array.h - Java arrays: jlang::Array<T> and System.arraycopy (jlang::arraycopy).
//
//   new int[n]            -> new jlang::Array<int32_t>(n)          (zero/null initialized)
//   new int[]{1, 2}       -> jlang::Array<int32_t>::of({1, 2})
//   new int[a][b]         -> jlang::Array<int32_t>::newMatrix(a, b)  (Array<Array<int32_t>*>*)
//   new int[a][]          -> new jlang::Array<jlang::Array<int32_t>*>(a)
//   arr.length            -> arr->length
//   arr[i]                -> (*arr)[i]   (bounds-checked: ArrayIndexOutOfBoundsException)
//   for (int v : arr)     -> for (int32_t v : *arr)
//   arr.clone()           -> arr->clone()
//   System.arraycopy(...) -> jlang::arraycopy(...)   (jlang::System::arraycopy forwards here)
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/CollectionsCore.h>

namespace jlang {

namespace detail {
// Java type descriptor used by Array::toString ("[I", "[Ljava.lang.Object;"...).
template<class T>
const char* arrayTypeCode() {
    if constexpr (std::is_same_v<T, bool>) return "[Z";
    else if constexpr (std::is_same_v<T, int8_t>) return "[B";
    else if constexpr (std::is_same_v<T, char16_t>) return "[C";
    else if constexpr (std::is_same_v<T, int16_t>) return "[S";
    else if constexpr (std::is_same_v<T, int32_t>) return "[I";
    else if constexpr (std::is_same_v<T, int64_t>) return "[J";
    else if constexpr (std::is_same_v<T, float>) return "[F";
    else if constexpr (std::is_same_v<T, double>) return "[D";
    else if constexpr (std::is_same_v<T, String>) return "[Ljava.lang.String;";
    else return "[Ljava.lang.Object;";
}
String arrayToString(const char* typeCode, int32_t identityHash);  // "[I@1b6d3586"
}  // namespace detail

// ---------------------------------------------------------------------------------------
// A fixed-length, garbage-collected Java array. Elements are value-initialized (0, false,
// nullptr, null String, null enum). Primitive element storage is not scanned by the
// collector. equals()/hashCode() are identity based, as for Java arrays (use
// jlang::Arrays::equals/hashCode for content).
template<class T>
class Array final : public virtual Object {
public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;
    using reference = T&;
    using size_type = std::size_t;

    const int32_t length;

    explicit Array(int32_t n) : length(checkLength(n)) { data_ = allocate(n); }

    // new T[]{a, b, c}
    static Array<T>* of(std::initializer_list<T> values) {
        auto* a = new Array<T>(static_cast<int32_t>(values.size()));
        std::copy(values.begin(), values.end(), a->data_);
        return a;
    }
    // Array from any iterator range.
    template<class It>
    static Array<T>* fromRange(It first, It last) {
        auto* a = new Array<T>(static_cast<int32_t>(std::distance(first, last)));
        std::copy(first, last, a->data_);
        return a;
    }

    // new T[a][b], new T[a][b][c]
    static Array<Array<T>*>* newMatrix(int32_t a, int32_t b) {
        auto* m = new Array<Array<T>*>(a);
        checkLength(b);
        for (int32_t i = 0; i < a; i++) m->data()[i] = new Array<T>(b);
        return m;
    }
    static Array<Array<Array<T>*>*>* newMatrix(int32_t a, int32_t b, int32_t c) {
        auto* m = new Array<Array<Array<T>*>*>(a);
        checkLength(b);
        checkLength(c);
        for (int32_t i = 0; i < a; i++) m->data()[i] = newMatrix(b, c);
        return m;
    }

    // Bounds-checked element access (ArrayIndexOutOfBoundsException, message = the index).
    T& operator[](int32_t i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(length)) detail::throwArrayIndexOutOfBounds(i);
        return data_[i];
    }
    const T& operator[](int32_t i) const {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(length)) detail::throwArrayIndexOutOfBounds(i);
        return data_[i];
    }
    T get(int32_t i) const { return (*this)[i]; }
    void set(int32_t i, const T& v) { (*this)[i] = v; }

    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }
    T* begin() noexcept { return data_; }
    T* end() noexcept { return data_ + length; }
    const T* begin() const noexcept { return data_; }
    const T* end() const noexcept { return data_ + length; }
    std::size_t size() const noexcept { return static_cast<std::size_t>(length); }  // STL use only

    // arr.clone(): shallow copy.
    Array<T>* clone() override {
        auto* a = new Array<T>(length);
        std::copy(data_, data_ + length, a->data_);
        return a;
    }

    // Java's Object.toString of an array: "[I@1b6d3586".
    String toString() override { return detail::arrayToString(detail::arrayTypeCode<T>(), identityHashCode()); }

private:
    static int32_t checkLength(int32_t n) {
        if (n < 0) detail::throwNegativeArraySize(n);
        return n;
    }
    static T* allocate(int32_t n) {
        const std::size_t count = static_cast<std::size_t>(n > 0 ? n : 1);
        T* p = detail::GcAllocator<T>().allocate(count);
        if constexpr (detail::kPointerFree<T>) {
            std::memset(static_cast<void*>(p), 0, count * sizeof(T));
        } else if constexpr (std::is_pointer_v<T>) {
            // GC_MALLOC memory is already zero-filled: all elements are nullptr.
        } else {
            std::uninitialized_value_construct_n(p, count);
        }
        return p;
    }

    T* data_;
};

// ---------------------------------------------------------------------------------------
// System.arraycopy(src, srcPos, dest, destPos, length) with Java's semantics: copies as if
// through a temporary (overlapping ranges in the same array work), NullPointerException for
// a null array, ArrayIndexOutOfBoundsException for bad positions or length (nothing copied).
// dest may have a different element type if src elements convert to it (Object[] <- String[]).
template<class S, class D>
    requires std::is_convertible_v<const S&, D>
void arraycopy(Array<S>* src, int32_t srcPos, Array<D>* dest, int32_t destPos, int32_t length) {
    if (src == nullptr || dest == nullptr) detail::throwNullPointer();
    if (length < 0 || srcPos < 0 || destPos < 0 || static_cast<int64_t>(srcPos) + length > src->length ||
        static_cast<int64_t>(destPos) + length > dest->length) {
        detail::throwArrayIndexOutOfBoundsNoMsg();
    }
    if (length == 0) return;
    S* s = src->data() + srcPos;
    D* d = dest->data() + destPos;
    if constexpr (std::is_same_v<S, D>) {
        if constexpr (std::is_trivially_copyable_v<S>) {
            std::memmove(static_cast<void*>(d), static_cast<const void*>(s), static_cast<std::size_t>(length) * sizeof(S));
        } else if (static_cast<const void*>(src) == static_cast<const void*>(dest) && srcPos < destPos) {
            std::copy_backward(s, s + length, d + length);
        } else {
            std::copy(s, s + length, d);
        }
    } else {
        for (int32_t i = 0; i < length; i++) d[i] = s[i];
    }
}

}  // namespace jlang
