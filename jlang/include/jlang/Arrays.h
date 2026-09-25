// jlang/Arrays.h - java.util.Arrays (jlang::Arrays) and org.apache.commons.lang.ArrayUtils
// (jlang::ArrayUtils), as static templates over jlang::Array<T>*.
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/Array.h>
#include <jlang/CollectionsCore.h>
#include <jlang/List.h>

namespace jlang {

namespace detail {
// Arrays.rangeCheck
inline void arraysRangeCheck(int32_t length, int32_t fromIndex, int32_t toIndex) {
    if (fromIndex > toIndex) {
        throwIllegalArgument("fromIndex(" + std::to_string(fromIndex) + ") > toIndex(" + std::to_string(toIndex) + ")");
    }
    if (fromIndex < 0) throwArrayIndexOutOfBounds(fromIndex);
    if (toIndex > length) throwArrayIndexOutOfBounds(toIndex);
}

// Element order for Arrays.sort of primitives (Java's total order for floating point).
template<class T>
struct PrimitiveLess {
    bool operator()(const T& a, const T& b) const {
        if constexpr (std::is_floating_point_v<T>) return javaCompareFloating(a, b) < 0;
        else return a < b;
    }
};

template<class T>
inline constexpr bool kPrimitive = std::is_arithmetic_v<T>;

// Java's String.valueOf for Arrays.toString elements.
template<class T>
void appendArrayElem(std::string& out, const T& v) {
    appendElem(out, v);
}
}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.util.Arrays
class Arrays final {
public:
    // Arrays.asList(array) / Arrays.asList(a, b, c): a new jlang::List (Java's is a fixed-size
    // view of the array; this is a copy). String literals become jlang::String elements.
    template<class T>
    static List<T>* asList(Array<T>* a) {
        if (a == nullptr) detail::throwNullPointer();
        return new List<T>(a->begin(), a->end());
    }
    template<class First, class... Rest>
        requires(!(sizeof...(Rest) == 0 && requires(First f) { f->length; }))
    static auto asList(const First& first, const Rest&... rest) {
        using E = std::conditional_t<std::is_convertible_v<const First&, const char*>, String, First>;
        auto* l = new List<E>();
        l->vec().reserve(1 + sizeof...(Rest));
        l->vec().push_back(E(first));
        (l->vec().push_back(E(rest)), ...);
        return l;
    }

    // ---- fill
    template<class T>
    static void fill(Array<T>* a, const std::type_identity_t<T>& value) {
        if (a == nullptr) detail::throwNullPointer();
        std::fill(a->begin(), a->end(), value);
    }
    template<class T>
    static void fill(Array<T>* a, int32_t fromIndex, int32_t toIndex, const std::type_identity_t<T>& value) {
        if (a == nullptr) detail::throwNullPointer();
        detail::arraysRangeCheck(a->length, fromIndex, toIndex);
        std::fill(a->begin() + fromIndex, a->begin() + toIndex, value);
    }

    // ---- copyOf / copyOfRange (padding with 0/null)
    template<class T>
    static Array<T>* copyOf(Array<T>* original, int32_t newLength) {
        if (original == nullptr) detail::throwNullPointer();
        auto* a = new Array<T>(newLength);
        const int32_t n = std::min(original->length, newLength);
        std::copy(original->begin(), original->begin() + n, a->begin());
        return a;
    }
    template<class T>
    static Array<T>* copyOfRange(Array<T>* original, int32_t from, int32_t to) {
        if (original == nullptr) detail::throwNullPointer();
        const int32_t newLength = to - from;
        if (newLength < 0) detail::throwIllegalArgument(std::to_string(from) + " > " + std::to_string(to));
        if (from < 0 || from > original->length) detail::throwArrayIndexOutOfBoundsNoMsg();
        auto* a = new Array<T>(newLength);
        const int32_t n = std::min(original->length - from, newLength);
        std::copy(original->begin() + from, original->begin() + from + n, a->begin());
        return a;
    }

    // ---- sort: primitives ascending (floating point: -0.0 < 0.0, NaN last); objects by
    // natural order or Comparator with Java 6's stable merge sort.
    template<class T>
    static void sort(Array<T>* a) {
        if (a == nullptr) detail::throwNullPointer();
        sortRange(a, 0, a->length);
    }
    template<class T>
    static void sort(Array<T>* a, int32_t fromIndex, int32_t toIndex) {
        if (a == nullptr) detail::throwNullPointer();
        detail::arraysRangeCheck(a->length, fromIndex, toIndex);
        sortRange(a, fromIndex, toIndex);
    }
    template<class T, class U>
        requires std::is_convertible_v<const T&, U>
    static void sort(Array<T>* a, Comparator<U>* c) {
        if (a == nullptr) detail::throwNullPointer();
        detail::stableSortRange(a->data(), 0, a->length, detail::PtrCmp<T, U>{c});
    }
    template<class T, class U>
        requires std::is_convertible_v<const T&, U>
    static void sort(Array<T>* a, int32_t fromIndex, int32_t toIndex, Comparator<U>* c) {
        if (a == nullptr) detail::throwNullPointer();
        detail::arraysRangeCheck(a->length, fromIndex, toIndex);
        detail::stableSortRange(a->data(), fromIndex, toIndex, detail::PtrCmp<T, U>{c});
    }
    template<class T, class F>
        requires detail::IsJavaCmpFn<F, T>
    static void sort(Array<T>* a, F f) {
        if (a == nullptr) detail::throwNullPointer();
        detail::stableSortRange(a->data(), 0, a->length, detail::FnCmp<F>{&f});
    }

    // ---- binarySearch (sorted array): index, or -(insertion point) - 1
    template<class T>
    static int32_t binarySearch(Array<T>* a, const std::type_identity_t<T>& key) {
        if (a == nullptr) detail::throwNullPointer();
        return binarySearch0(a, 0, a->length, key, detail::NaturalCmp<T>{});
    }
    template<class T>
    static int32_t binarySearch(Array<T>* a, int32_t fromIndex, int32_t toIndex, const std::type_identity_t<T>& key) {
        if (a == nullptr) detail::throwNullPointer();
        detail::arraysRangeCheck(a->length, fromIndex, toIndex);
        return binarySearch0(a, fromIndex, toIndex, key, detail::NaturalCmp<T>{});
    }
    template<class T, class U>
        requires std::is_convertible_v<const T&, U>
    static int32_t binarySearch(Array<T>* a, const std::type_identity_t<T>& key, Comparator<U>* c) {
        if (a == nullptr) detail::throwNullPointer();
        return binarySearch0(a, 0, a->length, key, detail::PtrCmp<T, U>{c});
    }

    // ---- equals / hashCode / toString (content based; null arrays allowed)
    template<class T>
    static bool equals(Array<T>* a, Array<T>* b) {
        if (a == b) return true;
        if (a == nullptr || b == nullptr) return false;
        if (a->length != b->length) return false;
        for (int32_t i = 0; i < a->length; i++) {
            if (!detail::javaEquals(a->data()[i], b->data()[i])) return false;
        }
        return true;
    }
    template<class T>
    static int32_t hashCode(Array<T>* a) {
        if (a == nullptr) return 0;
        int32_t h = 1;
        for (const auto& e : *a) h = 31 * h + detail::javaHash(e);
        return h;
    }
    // "[1, 2, 3]", "null" for a null array.
    template<class T>
    static String toString(Array<T>* a) {
        if (a == nullptr) return String("null");
        std::string out;
        out.push_back('[');
        for (int32_t i = 0; i < a->length; i++) {
            if (i != 0) out.append(", ", 2);
            detail::appendArrayElem(out, a->data()[i]);
        }
        out.push_back(']');
        return String(std::move(out));
    }
    // Arrays.deepToString: nested arrays are printed recursively.
    template<class T>
    static String deepToString(Array<T>* a) {
        if (a == nullptr) return String("null");
        std::string out;
        out.push_back('[');
        for (int32_t i = 0; i < a->length; i++) {
            if (i != 0) out.append(", ", 2);
            if constexpr (IsArrayPtr<T>::value) {
                if (static_cast<const void*>(a->data()[i]) == static_cast<const void*>(a)) out.append("[...]");
                else out.append(deepToString(a->data()[i]));
            } else {
                detail::appendArrayElem(out, a->data()[i]);
            }
        }
        out.push_back(']');
        return String(std::move(out));
    }
    template<class T>
    static bool deepEquals(Array<T>* a, Array<T>* b) {
        if constexpr (IsArrayPtr<T>::value) {
            if (a == b) return true;
            if (a == nullptr || b == nullptr || a->length != b->length) return false;
            for (int32_t i = 0; i < a->length; i++) {
                if (!deepEquals(a->data()[i], b->data()[i])) return false;
            }
            return true;
        } else {
            return equals(a, b);
        }
    }

private:
    template<class X> struct IsArrayPtr : std::false_type {};
    template<class E> struct IsArrayPtr<Array<E>*> : std::true_type {};

    template<class T>
    static void sortRange(Array<T>* a, int32_t fromIndex, int32_t toIndex) {
        if constexpr (detail::kPrimitive<T>) {
            std::sort(a->data() + fromIndex, a->data() + toIndex, detail::PrimitiveLess<T>{});
        } else {
            detail::stableSortRange(a->data(), fromIndex, toIndex, detail::NaturalCmp<T>{});
        }
    }
    template<class T, class Cmp>
    static int32_t binarySearch0(Array<T>* a, int32_t fromIndex, int32_t toIndex, const T& key, Cmp c) {
        int32_t low = fromIndex;
        int32_t high = toIndex - 1;
        while (low <= high) {
            const int32_t mid = static_cast<int32_t>(static_cast<uint32_t>(low + high) >> 1);
            const int32_t cmp = c(a->data()[mid], key);
            if (cmp < 0) low = mid + 1;
            else if (cmp > 0) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }
};

// ---------------------------------------------------------------------------------------
// org.apache.commons.lang.ArrayUtils (commons-lang 2.x). Arrays are never modified: add/remove
// return new arrays. A null array counts as empty (add(null, x) -> {x}).
class ArrayUtils final {
public:
    static constexpr int32_t INDEX_NOT_FOUND = -1;

    template<class T>
    static int32_t getLength(Array<T>* array) {
        return array == nullptr ? 0 : array->length;
    }
    template<class T>
    static bool isEmpty(Array<T>* array) {
        return array == nullptr || array->length == 0;
    }
    template<class T>
    static bool isNotEmpty(Array<T>* array) {
        return !isEmpty(array);
    }
    template<class T>
    static Array<T>* clone(Array<T>* array) {
        return array == nullptr ? nullptr : array->clone();
    }

    // add(array, element): a copy with element appended.
    template<class T>
    static Array<T>* add(Array<T>* array, const std::type_identity_t<T>& element) {
        const int32_t n = getLength(array);
        auto* r = new Array<T>(n + 1);
        if (n > 0) std::copy(array->begin(), array->end(), r->begin());
        r->data()[n] = element;
        return r;
    }
    // add(array, index, element): IndexOutOfBoundsException("Index: i, Length: n").
    template<class T>
    static Array<T>* add(Array<T>* array, int32_t index, const std::type_identity_t<T>& element) {
        const int32_t n = getLength(array);
        if (index > n || index < 0) {
            detail::throwIndexOutOfBoundsMsg("Index: " + std::to_string(index) + ", Length: " + std::to_string(n));
        }
        auto* r = new Array<T>(n + 1);
        if (n > 0) {
            std::copy(array->begin(), array->begin() + index, r->begin());
            std::copy(array->begin() + index, array->end(), r->begin() + index + 1);
        }
        r->data()[index] = element;
        return r;
    }
    // addAll(a, b): concatenation (clone of the other when one is null).
    template<class T>
    static Array<T>* addAll(Array<T>* a, Array<T>* b) {
        if (a == nullptr) return clone(b);
        if (b == nullptr) return clone(a);
        auto* r = new Array<T>(a->length + b->length);
        std::copy(a->begin(), a->end(), r->begin());
        std::copy(b->begin(), b->end(), r->begin() + a->length);
        return r;
    }
    // remove(array, index): IndexOutOfBoundsException("Index: i, Length: n").
    template<class T>
    static Array<T>* remove(Array<T>* array, int32_t index) {
        const int32_t n = getLength(array);
        if (index < 0 || index >= n) {
            detail::throwIndexOutOfBoundsMsg("Index: " + std::to_string(index) + ", Length: " + std::to_string(n));
        }
        auto* r = new Array<T>(n - 1);
        std::copy(array->begin(), array->begin() + index, r->begin());
        std::copy(array->begin() + index + 1, array->end(), r->begin() + index);
        return r;
    }
    // removeElement(array, element): without the first occurrence (a clone if absent).
    template<class T>
    static Array<T>* removeElement(Array<T>* array, const std::type_identity_t<T>& element) {
        const int32_t index = indexOf(array, element);
        if (index == INDEX_NOT_FOUND) return clone(array);
        return remove(array, index);
    }

    template<class T>
    static int32_t indexOf(Array<T>* array, const std::type_identity_t<T>& value) {
        return indexOf(array, value, 0);
    }
    template<class T>
    static int32_t indexOf(Array<T>* array, const std::type_identity_t<T>& value, int32_t startIndex) {
        if (array == nullptr) return INDEX_NOT_FOUND;
        if (startIndex < 0) startIndex = 0;
        for (int32_t i = startIndex; i < array->length; i++) {
            if (matches(array->data()[i], value)) return i;
        }
        return INDEX_NOT_FOUND;
    }
    template<class T>
    static int32_t lastIndexOf(Array<T>* array, const std::type_identity_t<T>& value) {
        return lastIndexOf(array, value, INT32_MAX);
    }
    template<class T>
    static int32_t lastIndexOf(Array<T>* array, const std::type_identity_t<T>& value, int32_t startIndex) {
        if (array == nullptr || startIndex < 0) return INDEX_NOT_FOUND;
        if (startIndex >= array->length) startIndex = array->length - 1;
        for (int32_t i = startIndex; i >= 0; i--) {
            if (matches(array->data()[i], value)) return i;
        }
        return INDEX_NOT_FOUND;
    }
    template<class T>
    static bool contains(Array<T>* array, const std::type_identity_t<T>& value) {
        return indexOf(array, value) != INDEX_NOT_FOUND;
    }
    // subarray(array, start, endExclusive): clamped; null for a null array.
    template<class T>
    static Array<T>* subarray(Array<T>* array, int32_t startIndexInclusive, int32_t endIndexExclusive) {
        if (array == nullptr) return nullptr;
        if (startIndexInclusive < 0) startIndexInclusive = 0;
        if (endIndexExclusive > array->length) endIndexExclusive = array->length;
        const int32_t newSize = endIndexExclusive - startIndexInclusive;
        if (newSize <= 0) return new Array<T>(0);
        auto* r = new Array<T>(newSize);
        std::copy(array->begin() + startIndexInclusive, array->begin() + endIndexExclusive, r->begin());
        return r;
    }
    template<class T>
    static void reverse(Array<T>* array) {
        if (array == nullptr) return;
        std::reverse(array->begin(), array->end());
    }
    template<class T>
    static bool isSameLength(Array<T>* a, Array<T>* b) {
        return getLength(a) == getLength(b);
    }

private:
    // Primitives compare by value (floating point: exact ==, as commons-lang does); objects
    // with equals() (null matches null).
    template<class T>
    static bool matches(const T& element, const T& value) {
        if constexpr (std::is_arithmetic_v<T>) return element == value;
        else return detail::javaEquals(value, element);
    }
};

}  // namespace jlang
