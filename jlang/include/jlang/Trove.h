// jlang/Trove.h - the GNU Trove 2.1 classes used by the code base:
//   gnu.trove.TIntObjectHashMap<V> -> jlang::TIntObjectHashMap<V>   (a Map<int32_t, V>)
//   gnu.trove.TIntIntHashMap       -> jlang::TIntIntHashMap         (a Map<int32_t, int32_t>)
//   gnu.trove.TIntArrayList        -> jlang::TIntArrayList          (a List<int32_t>)
//   TIntObjectIterator/TIntIntIterator, TIntObjectProcedure/TIntIntProcedure/TIntProcedure/
//   TObjectProcedure, TObjectFunction/TIntFunction (all with lambda adapters ::of).
//
// Semantics follow Trove 2.1: get() of an absent key returns null/0, put()/remove() return the
// previous value or null/0, adjustValue() ADDS the amount (returns false when the key is
// absent), toString() is "{k=v,k=v}" for maps and "{1, 2, 3}" for TIntArrayList.
// Iteration order is the map's insertion order (Trove's is its hash order).
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/Array.h>
#include <jlang/CollectionsCore.h>
#include <jlang/List.h>
#include <jlang/Map.h>

namespace jlang {

// ---------------------------------------------------------------------------------------
// Procedures and functions (interfaces; anonymous classes -> ::of(lambda)).
template<class V>
class TIntObjectProcedure : public virtual Object {
public:
    virtual bool execute(int32_t a, V b) = 0;
    template<class F>
    static TIntObjectProcedure<V>* of(F f);
};
class TIntIntProcedure : public virtual Object {
public:
    virtual bool execute(int32_t a, int32_t b) = 0;
    template<class F>
    static TIntIntProcedure* of(F f);
};
class TIntProcedure : public virtual Object {
public:
    virtual bool execute(int32_t value) = 0;
    template<class F>
    static TIntProcedure* of(F f);
};
template<class V>
class TObjectProcedure : public virtual Object {
public:
    virtual bool execute(V object) = 0;
    template<class F>
    static TObjectProcedure<V>* of(F f);
};
template<class T, class R>
class TObjectFunction : public virtual Object {
public:
    virtual R execute(T value) = 0;
    template<class F>
    static TObjectFunction<T, R>* of(F f);
};
class TIntFunction : public virtual Object {
public:
    virtual int32_t execute(int32_t value) = 0;
    template<class F>
    static TIntFunction* of(F f);
};

namespace detail {
template<class V, class F>
struct TIntObjectProcedureL final : public virtual TIntObjectProcedure<V> {
    explicit TIntObjectProcedureL(F f) : f_(std::move(f)) {}
    bool execute(int32_t a, V b) override { return f_(a, b); }
    F f_;
};
template<class F>
struct TIntIntProcedureL final : public virtual TIntIntProcedure {
    explicit TIntIntProcedureL(F f) : f_(std::move(f)) {}
    bool execute(int32_t a, int32_t b) override { return f_(a, b); }
    F f_;
};
template<class F>
struct TIntProcedureL final : public virtual TIntProcedure {
    explicit TIntProcedureL(F f) : f_(std::move(f)) {}
    bool execute(int32_t v) override { return f_(v); }
    F f_;
};
template<class V, class F>
struct TObjectProcedureL final : public virtual TObjectProcedure<V> {
    explicit TObjectProcedureL(F f) : f_(std::move(f)) {}
    bool execute(V v) override { return f_(v); }
    F f_;
};
template<class T, class R, class F>
struct TObjectFunctionL final : public virtual TObjectFunction<T, R> {
    explicit TObjectFunctionL(F f) : f_(std::move(f)) {}
    R execute(T v) override { return f_(v); }
    F f_;
};
template<class F>
struct TIntFunctionL final : public virtual TIntFunction {
    explicit TIntFunctionL(F f) : f_(std::move(f)) {}
    int32_t execute(int32_t v) override { return f_(v); }
    F f_;
};

// Trove maps' toString(): "{k=v,k=v}" (no space after the comma).
template<class K, class V>
String troveMapToString(const Vec<Entry<K, V>>& entries) {
    std::string out;
    out.push_back('{');
    bool first = true;
    for (const auto& e : entries) {
        if (!first) out.push_back(',');
        first = false;
        appendElem(out, e.getKey());
        out.push_back('=');
        appendElem(out, e.getValue());
    }
    out.push_back('}');
    return String(std::move(out));
}

[[noreturn]] void throwTroveIndex(int32_t index);  // ArrayIndexOutOfBoundsException("Array index out of range: i")
}  // namespace detail

template<class V>
template<class F>
TIntObjectProcedure<V>* TIntObjectProcedure<V>::of(F f) {
    return new detail::TIntObjectProcedureL<V, F>(std::move(f));
}
template<class F>
TIntIntProcedure* TIntIntProcedure::of(F f) {
    return new detail::TIntIntProcedureL<F>(std::move(f));
}
template<class F>
TIntProcedure* TIntProcedure::of(F f) {
    return new detail::TIntProcedureL<F>(std::move(f));
}
template<class V>
template<class F>
TObjectProcedure<V>* TObjectProcedure<V>::of(F f) {
    return new detail::TObjectProcedureL<V, F>(std::move(f));
}
template<class T, class R>
template<class F>
TObjectFunction<T, R>* TObjectFunction<T, R>::of(F f) {
    return new detail::TObjectFunctionL<T, R, F>(std::move(f));
}
template<class F>
TIntFunction* TIntFunction::of(F f) {
    return new detail::TIntFunctionL<F>(std::move(f));
}

// ---------------------------------------------------------------------------------------
// Trove map iterators: `for (auto it = map->iterator(); it->hasNext();) { it->advance();
// it->key(); it->value(); }`. They iterate a snapshot; setValue()/remove() update the map.
template<class V>
class TIntObjectIterator : public virtual Object {
public:
    explicit TIntObjectIterator(Map<int32_t, V>* map) : map_(map) { map->_snapshot(&keys_, &values_); }
    bool hasNext() { return static_cast<std::size_t>(pos_ + 1) < keys_.size(); }
    void advance() {
        if (!hasNext()) detail::throwNoSuchElement();
        pos_++;
        removed_ = false;
    }
    int32_t key() {
        check();
        return keys_[static_cast<std::size_t>(pos_)];
    }
    V value() {
        check();
        return values_[static_cast<std::size_t>(pos_)];
    }
    V setValue(const V& v) {
        check();
        V old = values_[static_cast<std::size_t>(pos_)];
        values_[static_cast<std::size_t>(pos_)] = v;
        map_->put(keys_[static_cast<std::size_t>(pos_)], v);
        return old;
    }
    void remove() {
        check();
        map_->remove(keys_[static_cast<std::size_t>(pos_)]);
        removed_ = true;
    }

private:
    void check() {
        if (pos_ < 0 || removed_) detail::throwIllegalState();
    }
    Map<int32_t, V>* map_;
    detail::Vec<int32_t> keys_;
    detail::Vec<V> values_;
    int32_t pos_ = -1;
    bool removed_ = false;
};

class TIntIntIterator : public TIntObjectIterator<int32_t> {
public:
    using TIntObjectIterator<int32_t>::TIntObjectIterator;
};

// ---------------------------------------------------------------------------------------
// gnu.trove.TIntObjectHashMap<V>
template<class V>
class TIntObjectHashMap : public Map<int32_t, V> {
public:
    TIntObjectHashMap() {}
    explicit TIntObjectHashMap(int32_t initialCapacity) : Map<int32_t, V>(initialCapacity) {}
    TIntObjectHashMap(int32_t initialCapacity, float loadFactor) : Map<int32_t, V>(initialCapacity, loadFactor) {}

    // int[] keys() / keys(int[] a) (a new array when a is too small)
    Array<int32_t>* keys() { return keys(new Array<int32_t>(0)); }
    Array<int32_t>* keys(Array<int32_t>* a) {
        detail::Vec<int32_t> ks;
        this->_snapshot(&ks, nullptr);
        const int32_t n = static_cast<int32_t>(ks.size());
        if (a == nullptr || a->length < n) a = new Array<int32_t>(n);
        std::copy(ks.begin(), ks.end(), a->data());
        return a;
    }
    // Object[] getValues() / T[] getValues(T[] a)
    Array<V>* getValues() {
        detail::Vec<V> vs;
        this->_snapshot(nullptr, &vs);
        return Array<V>::fromRange(vs.begin(), vs.end());
    }
    template<class U>
        requires std::is_convertible_v<const V&, U>
    Array<U>* getValues(Array<U>* a) {
        detail::Vec<V> vs;
        this->_snapshot(nullptr, &vs);
        const int32_t n = static_cast<int32_t>(vs.size());
        if (a == nullptr || a->length < n) a = new Array<U>(n);
        for (int32_t i = 0; i < n; i++) a->data()[i] = vs[static_cast<std::size_t>(i)];
        if (a->length > n) a->data()[n] = U{};
        return a;
    }
    TIntObjectIterator<V>* iterator() { return new TIntObjectIterator<V>(this); }

    bool forEachKey(TIntProcedure* procedure) {
        for (const auto& e : this->_entries()) {
            if (!procedure->execute(e.getKey())) return false;
        }
        return true;
    }
    bool forEachValue(TObjectProcedure<V>* procedure) {
        for (const auto& e : this->_entries()) {
            if (!procedure->execute(e.getValue())) return false;
        }
        return true;
    }
    bool forEachEntry(TIntObjectProcedure<V>* procedure) {
        for (const auto& e : this->_entries()) {
            if (!procedure->execute(e.getKey(), e.getValue())) return false;
        }
        return true;
    }
    template<class F>
        requires std::is_invocable_r_v<bool, F&, int32_t, V>
    bool forEachEntry(F f) {
        for (const auto& e : this->_entries()) {
            if (!f(e.getKey(), e.getValue())) return false;
        }
        return true;
    }
    // Removes the entries for which the procedure returns false; true if any was removed.
    bool retainEntries(TIntObjectProcedure<V>* procedure) {
        bool modified = false;
        for (const auto& e : this->_entries()) {
            if (!procedure->execute(e.getKey(), e.getValue())) {
                this->remove(e.getKey());
                modified = true;
            }
        }
        return modified;
    }
    void transformValues(TObjectFunction<V, V>* function) {
        for (const auto& e : this->_entries()) this->put(e.getKey(), function->execute(e.getValue()));
    }

    TIntObjectHashMap<V>* clone() override {
        auto* m = new TIntObjectHashMap<V>();
        m->putAll(this);
        return m;
    }
    String toString() override { return detail::troveMapToString(this->_entries()); }
    TIntObjectHashMap<V>* shared() {
        this->shared_ = true;
        return this;
    }
    TIntObjectHashMap<V>* synchronized_() { return shared(); }
};

// ---------------------------------------------------------------------------------------
// gnu.trove.TIntIntHashMap
class TIntIntHashMap : public Map<int32_t, int32_t> {
public:
    TIntIntHashMap() {}
    explicit TIntIntHashMap(int32_t initialCapacity) : Map<int32_t, int32_t>(initialCapacity) {}
    TIntIntHashMap(int32_t initialCapacity, float loadFactor) : Map<int32_t, int32_t>(initialCapacity, loadFactor) {}

    Array<int32_t>* keys() { return keys(new Array<int32_t>(0)); }
    Array<int32_t>* keys(Array<int32_t>* a) {
        detail::Vec<int32_t> ks;
        _snapshot(&ks, nullptr);
        const int32_t n = static_cast<int32_t>(ks.size());
        if (a == nullptr || a->length < n) a = new Array<int32_t>(n);
        std::copy(ks.begin(), ks.end(), a->data());
        return a;
    }
    Array<int32_t>* getValues() {
        detail::Vec<int32_t> vs;
        _snapshot(nullptr, &vs);
        return Array<int32_t>::fromRange(vs.begin(), vs.end());
    }
    TIntIntIterator* iterator() { return new TIntIntIterator(this); }

    // Adds amount to the value of key; false (and no change) when key is absent.
    bool adjustValue(int32_t key, int32_t amount) {
        auto g = lock();
        const int32_t s = t_.find(key, t_.hashOf(key));
        if (s < 0) return false;
        t_.slot(static_cast<std::size_t>(s)).value += amount;
        return true;
    }
    // Adds adjustAmount when key is present, else puts putAmount; returns the new value.
    int32_t adjustOrPutValue(int32_t key, int32_t adjustAmount, int32_t putAmount) {
        auto g = lock();
        bool inserted = false;
        const int32_t s = t_.findOrInsert(key, t_.hashOf(key), inserted);
        auto& v = t_.slot(static_cast<std::size_t>(s)).value;
        if (inserted) v = putAmount;
        else v += adjustAmount;
        return v;
    }
    bool increment(int32_t key) { return adjustValue(key, 1); }

    bool forEachKey(TIntProcedure* procedure) {
        for (const auto& e : _entries()) {
            if (!procedure->execute(e.getKey())) return false;
        }
        return true;
    }
    bool forEachValue(TIntProcedure* procedure) {
        for (const auto& e : _entries()) {
            if (!procedure->execute(e.getValue())) return false;
        }
        return true;
    }
    bool forEachEntry(TIntIntProcedure* procedure) {
        for (const auto& e : _entries()) {
            if (!procedure->execute(e.getKey(), e.getValue())) return false;
        }
        return true;
    }
    bool retainEntries(TIntIntProcedure* procedure) {
        bool modified = false;
        for (const auto& e : _entries()) {
            if (!procedure->execute(e.getKey(), e.getValue())) {
                remove(e.getKey());
                modified = true;
            }
        }
        return modified;
    }
    void transformValues(TIntFunction* function) {
        for (const auto& e : _entries()) put(e.getKey(), function->execute(e.getValue()));
    }

    TIntIntHashMap* clone() override {
        auto* m = new TIntIntHashMap();
        m->putAll(this);
        return m;
    }
    String toString() override { return detail::troveMapToString(_entries()); }
    TIntIntHashMap* shared() {
        shared_ = true;
        return this;
    }
    TIntIntHashMap* synchronized_() { return shared(); }
};

// ---------------------------------------------------------------------------------------
// gnu.trove.TIntArrayList: a List<int32_t> with Trove's names. Index errors throw
// ArrayIndexOutOfBoundsException("Array index out of range: i") like Trove.
class TIntArrayList : public List<int32_t> {
public:
    using List<int32_t>::add;
    using List<int32_t>::remove;
    using List<int32_t>::indexOf;
    using List<int32_t>::lastIndexOf;
    using List<int32_t>::sort;
    using List<int32_t>::clear;
    using List<int32_t>::set;

    TIntArrayList() {}
    explicit TIntArrayList(int32_t capacity) : List<int32_t>(capacity) {}
    explicit TIntArrayList(Array<int32_t>* values) {
        if (values == nullptr) detail::throwNullPointer();
        v_.assign(values->begin(), values->end());
    }

    int32_t get(int32_t offset) override {
        auto g = lock();
        return v_[checkTrove(offset)];
    }
    int32_t getQuick(int32_t offset) { return get(offset); }
    int32_t set(int32_t offset, const int32_t& value) override {
        auto g = lock();
        const std::size_t i = checkTrove(offset);
        const int32_t old = v_[i];
        v_[i] = value;
        return old;
    }
    void setQuick(int32_t offset, int32_t value) { set(offset, value); }
    int32_t getSet(int32_t offset, int32_t value) { return set(offset, value); }
    int32_t removeAt(int32_t offset) override {
        auto g = lock();
        const std::size_t i = checkTrove(offset);
        const int32_t old = v_[i];
        v_.erase(v_.begin() + static_cast<std::ptrdiff_t>(i));
        return old;
    }
    // remove(offset, length)
    void remove(int32_t offset, int32_t length) {
        auto g = lock();
        if (offset < 0 || offset >= size()) detail::throwTroveIndex(offset);
        if (length < 0 || static_cast<int64_t>(offset) + length > size()) detail::throwArrayIndexOutOfBoundsNoMsg();
        v_.erase(v_.begin() + offset, v_.begin() + offset + length);
    }
    void add(Array<int32_t>* values) { add(values, 0, values->length); }
    void add(Array<int32_t>* values, int32_t offset, int32_t length) {
        auto g = lock();
        checkRange(values, offset, length);
        v_.insert(v_.end(), values->begin() + offset, values->begin() + offset + length);
    }
    void insert(int32_t offset, int32_t value) {
        auto g = lock();
        if (offset < 0 || offset > size()) detail::throwArrayIndexOutOfBoundsNoMsg();
        v_.insert(v_.begin() + offset, value);
    }
    void insert(int32_t offset, Array<int32_t>* values) { insert(offset, values, 0, values->length); }
    void insert(int32_t offset, Array<int32_t>* values, int32_t valOffset, int32_t len) {
        auto g = lock();
        if (offset < 0 || offset > size()) detail::throwArrayIndexOutOfBoundsNoMsg();
        checkRange(values, valOffset, len);
        v_.insert(v_.begin() + offset, values->begin() + valOffset, values->begin() + valOffset + len);
    }
    // set(offset, int[] values[, valOffset, length])
    void set(int32_t offset, Array<int32_t>* values) { set(offset, values, 0, values->length); }
    void set(int32_t offset, Array<int32_t>* values, int32_t valOffset, int32_t length) {
        auto g = lock();
        if (offset < 0 || static_cast<int64_t>(offset) + length > size()) detail::throwTroveIndex(offset);
        checkRange(values, valOffset, length);
        std::copy(values->begin() + valOffset, values->begin() + valOffset + length, v_.begin() + offset);
    }
    void clear(int32_t capacity) {
        auto g = lock();
        v_.clear();
        v_.reserve(static_cast<std::size_t>(capacity > 0 ? capacity : 0));
    }
    void reset() { clear(); }
    void resetQuick() { clear(); }

    Array<int32_t>* toNativeArray() {
        auto g = lock();
        return Array<int32_t>::fromRange(v_.begin(), v_.end());
    }
    Array<int32_t>* toNativeArray(int32_t offset, int32_t len) {
        auto g = lock();
        auto* a = new Array<int32_t>(len);
        toNativeArray(a, offset, len);
        return a;
    }
    void toNativeArray(Array<int32_t>* dest, int32_t offset, int32_t len) {
        auto g = lock();
        if (len == 0) return;
        if (offset < 0 || offset >= size()) detail::throwTroveIndex(offset);
        if (len < 0 || static_cast<int64_t>(offset) + len > size() || len > dest->length) {
            detail::throwArrayIndexOutOfBoundsNoMsg();
        }
        std::copy(v_.begin() + offset, v_.begin() + offset + len, dest->data());
    }

    void transformValues(TIntFunction* function) {
        auto g = lock();
        for (auto& v : v_) v = function->execute(v);
    }
    void reverse() {
        auto g = lock();
        std::reverse(v_.begin(), v_.end());
    }
    void reverse(int32_t from, int32_t to) {
        auto g = lock();
        if (from == to) return;
        if (from > to) detail::throwIllegalArgument("from cannot be greater than to");
        if (from < 0 || to > size()) detail::throwArrayIndexOutOfBoundsNoMsg();
        std::reverse(v_.begin() + from, v_.begin() + to);
    }
    void sort(int32_t fromIndex, int32_t toIndex) {
        auto g = lock();
        if (fromIndex < 0 || toIndex > size() || fromIndex > toIndex) detail::throwArrayIndexOutOfBoundsNoMsg();
        std::sort(v_.begin() + fromIndex, v_.begin() + toIndex);
    }
    void fill(int32_t value) {
        auto g = lock();
        std::fill(v_.begin(), v_.end(), value);
    }
    void fill(int32_t fromIndex, int32_t toIndex, int32_t value) {
        auto g = lock();
        if (fromIndex < 0) detail::throwTroveIndex(fromIndex);
        if (static_cast<std::size_t>(toIndex) > v_.size()) v_.resize(static_cast<std::size_t>(toIndex));
        std::fill(v_.begin() + fromIndex, v_.begin() + toIndex, value);
    }
    // Binary search of a sorted list: the index, or -(insertion point) - 1.
    int32_t binarySearch(int32_t value) { return binarySearch(value, 0, size()); }
    int32_t binarySearch(int32_t value, int32_t fromIndex, int32_t toIndex) {
        auto g = lock();
        if (fromIndex < 0) detail::throwTroveIndex(fromIndex);
        if (toIndex > size()) detail::throwTroveIndex(toIndex);
        int32_t low = fromIndex, high = toIndex - 1;
        while (low <= high) {
            const int32_t mid = static_cast<int32_t>(static_cast<uint32_t>(low + high) >> 1);
            const int32_t midVal = v_[static_cast<std::size_t>(mid)];
            if (midVal < value) low = mid + 1;
            else if (midVal > value) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }
    int32_t indexOf(int32_t offset, int32_t value) {
        auto g = lock();
        for (int32_t i = offset < 0 ? 0 : offset; i < size(); i++) {
            if (v_[static_cast<std::size_t>(i)] == value) return i;
        }
        return -1;
    }
    int32_t lastIndexOf(int32_t offset, int32_t value) {
        auto g = lock();
        for (int32_t i = offset; i-- > 0;) {
            if (i < size() && v_[static_cast<std::size_t>(i)] == value) return i;
        }
        return -1;
    }
    bool forEach(TIntProcedure* procedure) {
        for (int32_t v : _snapshot()) {
            if (!procedure->execute(v)) return false;
        }
        return true;
    }
    bool forEachDescending(TIntProcedure* procedure) {
        auto s = _snapshot();
        for (std::size_t i = s.size(); i-- > 0;) {
            if (!procedure->execute(s[i])) return false;
        }
        return true;
    }
    TIntArrayList* grep(TIntProcedure* condition) {
        auto* r = new TIntArrayList();
        for (int32_t v : _snapshot()) {
            if (condition->execute(v)) r->v_.push_back(v);
        }
        return r;
    }
    TIntArrayList* inverseGrep(TIntProcedure* condition) {
        auto* r = new TIntArrayList();
        for (int32_t v : _snapshot()) {
            if (!condition->execute(v)) r->v_.push_back(v);
        }
        return r;
    }
    int32_t max() {
        auto g = lock();
        if (v_.empty()) detail::throwIllegalStateMsg("cannot find maximum of an empty list");
        return *std::max_element(v_.begin(), v_.end());
    }
    int32_t min() {
        auto g = lock();
        if (v_.empty()) detail::throwIllegalStateMsg("cannot find minimum of an empty list");
        return *std::min_element(v_.begin(), v_.end());
    }
    TIntArrayList* subList(int32_t begin, int32_t end) override {
        auto g = lock();
        if (end < begin) {
            detail::throwIllegalArgument("end index " + std::to_string(end) + " greater than begin index " +
                                         std::to_string(begin));
        }
        if (begin < 0) detail::throwIndexOutOfBoundsMsg("begin index can not be < 0");
        if (end > size()) detail::throwIndexOutOfBoundsMsg("end index < " + std::to_string(size()));
        auto* r = new TIntArrayList();
        r->v_.assign(v_.begin() + begin, v_.begin() + end);
        return r;
    }

    // Trove's hashCode: h = 37 * h + value * 31, from the last element to the first.
    int32_t hashCode() override {
        auto g = lock();
        int32_t h = 0;
        for (std::size_t i = v_.size(); i-- > 0;) h = 37 * h + v_[i] * 31;
        return h;
    }
    // "{1, 2, 3}"
    String toString() override {
        std::string out;
        out.push_back('{');
        auto s = _snapshot();
        for (std::size_t i = 0; i < s.size(); i++) {
            if (i != 0) out.append(", ", 2);
            out.append(std::to_string(s[i]));
        }
        out.push_back('}');
        return String(std::move(out));
    }
    TIntArrayList* clone() override {
        auto* r = new TIntArrayList();
        r->v_ = _snapshot();
        return r;
    }
    TIntArrayList* shared() {
        shared_ = true;
        return this;
    }
    TIntArrayList* synchronized_() { return shared(); }

private:
    std::size_t checkTrove(int32_t offset) {
        if (offset >= static_cast<int32_t>(v_.size())) detail::throwTroveIndex(offset);
        if (offset < 0) detail::throwArrayIndexOutOfBounds(offset);
        return static_cast<std::size_t>(offset);
    }
    static void checkRange(Array<int32_t>* values, int32_t offset, int32_t length) {
        if (values == nullptr) detail::throwNullPointer();
        if (offset < 0 || length < 0 || static_cast<int64_t>(offset) + length > values->length) {
            detail::throwArrayIndexOutOfBoundsNoMsg();
        }
    }
};

}  // namespace jlang
