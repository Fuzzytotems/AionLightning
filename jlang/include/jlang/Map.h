// jlang/Map.h - java.util.Map.Entry (jlang::Entry<K,V>), java.util.Map (jlang::Map<K,V>:
// HashMap, LinkedHashMap, Hashtable, FastMap, THashMap, ...) and ConcurrentHashMap.
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/CollectionsCore.h>
#include <jlang/List.h>
#include <jlang/Set.h>

namespace jlang {

// ---------------------------------------------------------------------------------------
// java.util.Map.Entry<K,V> as a nullable value type:
//   for (auto& e : *map->entrySet()) { e.getKey(); e.getValue(); e.setValue(v); }
//   Entry<K,V> e = map->firstEntry(); if (e == nullptr) ...;   (TreeMap: null when empty)
// A default-constructed Entry is Java null; getKey()/getValue()/setValue() on it throw
// NullPointerException. Entries handed out by a map remember it: setValue() also updates the
// mapping in the map (if the key is still mapped), like Java's live entries.
template<class K, class V>
class Entry {
public:
    Entry() {}
    Entry(std::nullptr_t) {}
    Entry(const K& key, const V& value, Map<K, V>* map = nullptr)
        : key_(key), value_(value), map_(map), nonNull_(true) {}

    // By reference on lvalues; by value on temporaries (map->firstEntry().getKey()).
    K& getKey() & {
        check();
        return key_;
    }
    const K& getKey() const& {
        check();
        return key_;
    }
    K getKey() && {
        check();
        return std::move(key_);
    }
    V& getValue() & {
        check();
        return value_;
    }
    const V& getValue() const& {
        check();
        return value_;
    }
    V getValue() && {
        check();
        return std::move(value_);
    }
    // Returns the old value; writes through to the map the entry came from.
    V setValue(const V& value);

    bool isNull() const noexcept { return !nonNull_; }
    friend bool operator==(const Entry& e, std::nullptr_t) noexcept { return !e.nonNull_; }
    friend bool operator==(const Entry& a, const Entry& b) {
        if (!a.nonNull_ || !b.nonNull_) return a.nonNull_ == b.nonNull_;
        return a.equals(b);
    }
    // Map.Entry.equals: equal keys and equal values.
    bool equals(const Entry& o) const {
        if (!nonNull_ || !o.nonNull_) return nonNull_ == o.nonNull_ && nonNull_;
        return detail::javaEquals(key_, o.key_) && detail::javaEquals(value_, o.value_);
    }
    // Map.Entry.hashCode: key hash ^ value hash.
    int32_t hashCode() const { return detail::javaHash(key_) ^ detail::javaHash(value_); }
    // "key=value"
    String toString() const {
        if (!nonNull_) return String("null");
        std::string out;
        detail::appendElem(out, key_);
        out.push_back('=');
        detail::appendElem(out, value_);
        return String(std::move(out));
    }

private:
    void check() const {
        if (!nonNull_) detail::throwNullPointer();
    }
    K key_{};
    V value_{};
    Map<K, V>* map_ = nullptr;
    bool nonNull_ = false;
};

namespace detail {
template<class K, class V>
struct EntryProj {
    Map<K, V>* map = nullptr;
    template<class S>
    Entry<K, V> operator()(const S& s) const {
        return Entry<K, V>(s.key, s.value, map);
    }
};
template<class K, class V> class MapKeyView;
template<class K, class V> class MapValuesView;
template<class K, class V> class MapEntryView;
}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.util.Map<K,V> (HashMap/LinkedHashMap/Hashtable/FastMap/THashMap/IdentityHashMap...):
// an insertion-ordered hash map (LinkedHashMap order: re-putting a key keeps its position).
//
//   * get(k) returns the value or V{} (nullptr / 0 / null String) when absent; use
//     containsKey(k) or getOptional(k) when the Java code distinguishes a null Integer.
//   * put/remove return the previous value or V{}.
//   * keySet()/values()/entrySet() return SNAPSHOT lists (jlang::List) taken at the call.
//     Removing through them (remove(x), removeAll, retainAll, clear, and remove() of their
//     iterators) removes the mappings from the map, as Java's views do; adding throws
//     UnsupportedOperationException. keySet()->contains(k) asks the map.
//   * Range-for: `for (auto& e : *map)` visits Entry<K,V> (live; safe against modification
//     during the loop), or a snapshot when the map is shared.
//   * shared()/synchronized_() enable the map lock (the map's monitor, so JSYNC(map) blocks
//     exclude map operations like Java's synchronizedMap/Hashtable).
template<class K, class V>
class Map : public virtual Object {
public:
    using key_type = K;
    using mapped_type = V;
    using entry_type = Entry<K, V>;

    Map() {}
    explicit Map(int32_t initialCapacity) {
        if (initialCapacity < 0) {
            detail::throwIllegalArgument("Illegal initial capacity: " + std::to_string(initialCapacity));
        }
        t_.reserve(initialCapacity);
    }
    Map(int32_t initialCapacity, float loadFactor) : Map(initialCapacity) {
        if (!(loadFactor > 0)) detail::throwIllegalArgument(std::string("Illegal load factor: ") + String::valueOf(loadFactor));
    }
    Map(std::initializer_list<std::pair<K, V>> values) {
        for (const auto& kv : values) putNoLock(kv.first, kv.second);
    }
    // new HashMap(m)
    explicit Map(Map<K, V>* m) {
        if (m == nullptr) detail::throwNullPointer();
        for (const auto& e : m->_entries()) putNoLock(e.getKey(), e.getValue());
    }

    virtual int32_t size() {
        auto g = lock();
        return t_.size();
    }
    virtual bool isEmpty() { return size() == 0; }
    virtual V get(const K& key) {
        auto g = lock();
        const int32_t s = t_.find(key, t_.hashOf(key));
        return s < 0 ? V{} : V(t_.slot(static_cast<std::size_t>(s)).value);
    }
    // Java's `Integer v = map.get(k); if (v == null)`: nullopt when k is not mapped.
    virtual std::optional<V> getOptional(const K& key) {
        auto g = lock();
        const int32_t s = t_.find(key, t_.hashOf(key));
        if (s < 0) return std::nullopt;
        return t_.slot(static_cast<std::size_t>(s)).value;
    }
    V getOrDefault(const K& key, const V& defaultValue) {
        auto g = lock();
        std::optional<V> v = getOptional(key);
        return v.has_value() ? *v : defaultValue;
    }
    virtual bool containsKey(const K& key) {
        auto g = lock();
        return t_.find(key, t_.hashOf(key)) >= 0;
    }
    virtual bool containsValue(const V& value) {
        auto g = lock();
        bool found = false;
        t_.forEachLive([&](auto& s) {
            if (!found && detail::javaEquals(value, static_cast<const V&>(s.value))) found = true;
        });
        return found;
    }
    virtual V put(const K& key, const V& value) {
        auto g = lock();
        return putNoLock(key, value);
    }
    // ConcurrentMap/FastMap putIfAbsent: returns the existing value (V{} when it inserted).
    virtual V putIfAbsent(const K& key, const V& value) {
        auto g = lock();
        bool inserted = false;
        const int32_t s = t_.findOrInsert(key, t_.hashOf(key), inserted);
        auto& sl = t_.slot(static_cast<std::size_t>(s));
        if (inserted) {
            sl.value = value;
            return V{};
        }
        return sl.value;
    }
    virtual V remove(const K& key) {
        auto g = lock();
        V old{};
        t_.erase(key, t_.hashOf(key), &old);
        return old;
    }
    // ConcurrentMap.remove(key, value)
    virtual bool remove(const K& key, const V& value) {
        auto g = lock();
        if (!containsKey(key) || !detail::javaEquals(value, get(key))) return false;
        remove(key);
        return true;
    }
    // ConcurrentMap.replace(key, value): only if mapped; returns the previous value or V{}.
    virtual V replace(const K& key, const V& value) {
        auto g = lock();
        if (!containsKey(key)) return V{};
        return put(key, value);
    }
    virtual bool replace(const K& key, const V& oldValue, const V& newValue) {
        auto g = lock();
        if (!containsKey(key) || !detail::javaEquals(oldValue, get(key))) return false;
        put(key, newValue);
        return true;
    }
    // Java `V old = map.remove(k); if (old != null)` for primitive values: nullopt if unmapped.
    std::optional<V> removeOptional(const K& key) {
        auto g = lock();
        if (!containsKey(key)) return std::nullopt;
        return remove(key);
    }
    virtual void putAll(Map<K, V>* m) {
        if (m == nullptr) detail::throwNullPointer();
        auto entries = m->_entries();
        auto g = lock();
        for (const auto& e : entries) put(e.getKey(), e.getValue());
    }
    template<class K2, class V2>
        requires(!(std::is_same_v<K2, K> && std::is_same_v<V2, V>) && std::is_convertible_v<const K2&, K> &&
                 std::is_convertible_v<const V2&, V>)
    void putAll(Map<K2, V2>* m) {
        if (m == nullptr) detail::throwNullPointer();
        auto entries = m->_entries();
        auto g = lock();
        for (const auto& e : entries) put(static_cast<K>(e.getKey()), static_cast<V>(e.getValue()));
    }
    virtual void clear() {
        auto g = lock();
        t_.clear();
    }

    // get/containsKey/remove(Object) with a supertype pointer key, containsValue(Object) with a
    // supertype pointer value (see detail::SuperPointerArg): not found when not a K / V.
    template<class U>
        requires detail::SuperPointerArg<K, U>
    V get(U* key) {
        auto k = detail::downcastArg<K>(key);
        return k.has_value() ? get(*k) : V{};
    }
    template<class U>
        requires detail::SuperPointerArg<K, U>
    std::optional<V> getOptional(U* key) {
        auto k = detail::downcastArg<K>(key);
        return k.has_value() ? getOptional(*k) : std::nullopt;
    }
    template<class U>
        requires detail::SuperPointerArg<K, U>
    bool containsKey(U* key) {
        auto k = detail::downcastArg<K>(key);
        return k.has_value() && containsKey(*k);
    }
    template<class U>
        requires detail::SuperPointerArg<K, U>
    V remove(U* key) {
        auto k = detail::downcastArg<K>(key);
        return k.has_value() ? remove(*k) : V{};
    }
    template<class U>
        requires detail::SuperPointerArg<V, U>
    bool containsValue(U* value) {
        auto v = detail::downcastArg<V>(value);
        return v.has_value() && containsValue(*v);
    }

    // Snapshot views (see the class comment).
    virtual List<K>* keySet();
    virtual List<V>* values();
    virtual List<Entry<K, V>>* entrySet();
    // keySet()->iterator() etc.: iterators whose remove() removes the mapping.
    Iterator<K>* keyIterator() { return keySet()->iterator(); }
    Iterator<V>* valueIterator() { return values()->iterator(); }
    Iterator<Entry<K, V>>* entryIterator() { return entrySet()->iterator(); }

    bool isShared() { return shared_; }
    Map<K, V>* shared() {
        shared_ = true;
        return this;
    }
    Map<K, V>* synchronized_() { return shared(); }

    // AbstractMap.equals / hashCode / toString ("{k1=v1, k2=v2}")
    bool equals(Object* o) override {
        if (o == static_cast<Object*>(this)) return true;
        auto* m = dynamic_cast<Map<K, V>*>(o);
        if (m == nullptr) return false;
        if (m->size() != size()) return false;
        for (const auto& e : _entries()) {
            std::optional<V> ov = m->getOptional(e.getKey());
            if (!ov.has_value() || !detail::javaEquals(e.getValue(), *ov)) return false;
        }
        return true;
    }
    int32_t hashCode() override {
        int32_t h = 0;
        for (const auto& e : _entries()) h += e.hashCode();
        return h;
    }
    String toString() override {
        std::string out;
        out.push_back('{');
        bool first = true;
        const void* self = static_cast<const void*>(static_cast<Object*>(this));
        for (const auto& e : _entries()) {
            if (!first) out.append(", ", 2);
            first = false;
            appendSelfAware(out, e.getKey(), self);
            out.push_back('=');
            appendSelfAware(out, e.getValue(), self);
        }
        out.push_back('}');
        return String(std::move(out));
    }
    // HashMap.clone(): a shallow, unlocked copy.
    Map<K, V>* clone() override {
        auto* m = new Map<K, V>();
        for (const auto& e : _entries()) m->putNoLock(e.getKey(), e.getValue());
        return m;
    }

    using Range = detail::TableRange<K, V, Entry<K, V>, detail::EntryProj<K, V>>;
    Range begin() {
        if (shared_ || !_hashBacked()) return Range(detail::heapVec(_entries()));
        return Range(&t_, detail::EntryProj<K, V>{this});
    }
    detail::IterEnd end() { return {}; }
    Range begin() const { return const_cast<Map*>(this)->begin(); }
    detail::IterEnd end() const { return {}; }

    // jlang internals: snapshots in iteration order (under the lock).
    virtual detail::Vec<Entry<K, V>> _entries() {
        auto g = lock();
        detail::Vec<Entry<K, V>> out;
        out.reserve(static_cast<std::size_t>(t_.size()));
        t_.forEachLive([&](auto& s) { out.emplace_back(s.key, s.value, this); });
        return out;
    }
    virtual void _snapshot(detail::Vec<K>* keys, detail::Vec<V>* vals) {
        auto g = lock();
        if (keys != nullptr) keys->reserve(static_cast<std::size_t>(t_.size()));
        if (vals != nullptr) vals->reserve(static_cast<std::size_t>(t_.size()));
        t_.forEachLive([&](auto& s) {
            if (keys != nullptr) keys->push_back(s.key);
            if (vals != nullptr) vals->push_back(s.value);
        });
    }

protected:
    detail::CollLock lock() { return detail::CollLock(this, shared_); }
    // false for subclasses with their own storage (TreeMap).
    virtual bool _hashBacked() { return true; }
    V putNoLock(const K& key, const V& value) {
        bool inserted = false;
        const int32_t s = t_.findOrInsert(key, t_.hashOf(key), inserted);
        auto& sl = t_.slot(static_cast<std::size_t>(s));
        V old = inserted ? V{} : V(sl.value);
        sl.value = value;
        return old;
    }
    template<class X>
    static void appendSelfAware(std::string& out, const X& x, const void* self) {
        if constexpr (std::is_pointer_v<X>) {
            if (x != nullptr && static_cast<const void*>(detail::elemObject(x)) == self) {
                out.append("(this Map)");
                return;
            }
        }
        detail::appendElem(out, x);
    }

    detail::LinkedHashTable<K, V> t_;
    bool shared_ = false;
};

template<class K, class V>
V Entry<K, V>::setValue(const V& value) {
    check();
    V old = value_;
    value_ = value;
    if (map_ != nullptr) map_->replace(key_, value);
    return old;
}

namespace detail {

// Java's keySet()/values()/entrySet() are Sets/Collections without index-based removal; a
// remove(int) on a view (List<int32_t> of keys!) is a translation error: use removeObject(x).
[[noreturn]] void throwViewIndexRemoval();

// keySet() snapshot: removals reach the map.
template<class K, class V>
class MapKeyView final : public List<K> {
public:
    MapKeyView(Map<K, V>* m, Vec<K>&& keys) : map_(m) { this->v_ = std::move(keys); }
    bool contains(const K& k) override { return map_->containsKey(k); }
    bool add(const K& e) override {
        (void)e;
        throwUnsupportedOperation();
    }
    void add(int32_t index, const K& e) override {
        (void)index;
        (void)e;
        throwUnsupportedOperation();
    }
    bool addAll(Collection<K>* c) override {
        (void)c;
        throwUnsupportedOperation();
    }
    bool addAll(int32_t index, Collection<K>* c) override {
        (void)index;
        (void)c;
        throwUnsupportedOperation();
    }
    K set(int32_t index, const K& e) override {
        (void)index;
        (void)e;
        throwUnsupportedOperation();
    }
    K removeAt(int32_t index) override {
        (void)index;
        throwViewIndexRemoval();
    }
    K _removeIndex(int32_t index) override {
        K k = List<K>::removeAt(index);
        map_->remove(k);
        return k;
    }
    bool removeObject(const K& k) override {
        const bool had = map_->containsKey(k);
        const int32_t i = this->indexOf(k);
        if (i >= 0) List<K>::removeAt(i);
        if (had) map_->remove(k);
        return had;
    }
    void clear() override {
        List<K>::clear();
        map_->clear();
    }

protected:
    bool _isView() override { return true; }

private:
    Map<K, V>* map_;
};

// values() snapshot: removing a value removes its mapping (the key it had in the snapshot).
template<class K, class V>
class MapValuesView final : public List<V> {
public:
    MapValuesView(Map<K, V>* m, Vec<K>&& keys, Vec<V>&& vals) : map_(m), keys_(std::move(keys)) {
        this->v_ = std::move(vals);
    }
    bool add(const V& e) override {
        (void)e;
        throwUnsupportedOperation();
    }
    void add(int32_t index, const V& e) override {
        (void)index;
        (void)e;
        throwUnsupportedOperation();
    }
    bool addAll(Collection<V>* c) override {
        (void)c;
        throwUnsupportedOperation();
    }
    bool addAll(int32_t index, Collection<V>* c) override {
        (void)index;
        (void)c;
        throwUnsupportedOperation();
    }
    V set(int32_t index, const V& e) override {
        (void)index;
        (void)e;
        throwUnsupportedOperation();
    }
    V removeAt(int32_t index) override {
        (void)index;
        throwViewIndexRemoval();
    }
    V _removeIndex(int32_t index) override {
        V v = List<V>::removeAt(index);
        K k = keys_[static_cast<std::size_t>(index)];
        keys_.erase(keys_.begin() + index);
        map_->remove(k);
        return v;
    }
    void clear() override {
        List<V>::clear();
        keys_.clear();
        map_->clear();
    }

protected:
    bool _isView() override { return true; }

private:
    Map<K, V>* map_;
    Vec<K> keys_;
};

// entrySet() snapshot: removals reach the map; contains() asks the map.
template<class K, class V>
class MapEntryView final : public List<Entry<K, V>> {
public:
    using E = Entry<K, V>;
    MapEntryView(Map<K, V>* m, Vec<E>&& entries) : map_(m) { this->v_ = std::move(entries); }
    bool contains(const E& e) override {
        if (e == nullptr) return false;
        std::optional<V> v = map_->getOptional(e.getKey());
        return v.has_value() && javaEquals(*v, e.getValue());
    }
    bool add(const E& e) override {
        (void)e;
        throwUnsupportedOperation();
    }
    void add(int32_t index, const E& e) override {
        (void)index;
        (void)e;
        throwUnsupportedOperation();
    }
    bool addAll(Collection<E>* c) override {
        (void)c;
        throwUnsupportedOperation();
    }
    bool addAll(int32_t index, Collection<E>* c) override {
        (void)index;
        (void)c;
        throwUnsupportedOperation();
    }
    E set(int32_t index, const E& e) override {
        (void)index;
        (void)e;
        throwUnsupportedOperation();
    }
    E removeAt(int32_t index) override {
        (void)index;
        throwViewIndexRemoval();
    }
    E _removeIndex(int32_t index) override {
        E e = List<E>::removeAt(index);
        map_->remove(e.getKey());
        return e;
    }
    void clear() override {
        List<E>::clear();
        map_->clear();
    }

protected:
    bool _isView() override { return true; }

private:
    Map<K, V>* map_;
};

}  // namespace detail

template<class K, class V>
List<K>* Map<K, V>::keySet() {
    detail::Vec<K> keys;
    _snapshot(&keys, nullptr);
    return new detail::MapKeyView<K, V>(this, std::move(keys));
}

template<class K, class V>
List<V>* Map<K, V>::values() {
    detail::Vec<K> keys;
    detail::Vec<V> vals;
    _snapshot(&keys, &vals);
    return new detail::MapValuesView<K, V>(this, std::move(keys), std::move(vals));
}

template<class K, class V>
List<Entry<K, V>>* Map<K, V>::entrySet() {
    return new detail::MapEntryView<K, V>(this, _entries());
}

// ---------------------------------------------------------------------------------------
// java.util.concurrent.ConcurrentHashMap: a jlang::Map with its lock enabled. Like Java it
// rejects null keys and values (NullPointerException). Iteration is over snapshots.
template<class K, class V>
class ConcurrentHashMap : public Map<K, V> {
public:
    ConcurrentHashMap() { this->shared_ = true; }
    explicit ConcurrentHashMap(int32_t initialCapacity) : Map<K, V>(initialCapacity) { this->shared_ = true; }
    ConcurrentHashMap(int32_t initialCapacity, float loadFactor) : Map<K, V>(initialCapacity, loadFactor) {
        this->shared_ = true;
    }
    ConcurrentHashMap(int32_t initialCapacity, float loadFactor, int32_t concurrencyLevel)
        : Map<K, V>(initialCapacity, loadFactor) {
        if (concurrencyLevel <= 0) detail::throwIllegalArgumentNoMsg();
        this->shared_ = true;
    }
    explicit ConcurrentHashMap(Map<K, V>* m) {
        this->shared_ = true;
        putAll(m);
    }

    V get(const K& key) override {
        nullCheck(key);
        return Map<K, V>::get(key);
    }
    std::optional<V> getOptional(const K& key) override {
        nullCheck(key);
        return Map<K, V>::getOptional(key);
    }
    bool containsKey(const K& key) override {
        nullCheck(key);
        return Map<K, V>::containsKey(key);
    }
    bool containsValue(const V& value) override {
        nullCheck(value);
        return Map<K, V>::containsValue(value);
    }
    V put(const K& key, const V& value) override {
        nullCheck(key);
        nullCheck(value);
        return Map<K, V>::put(key, value);
    }
    V putIfAbsent(const K& key, const V& value) override {
        nullCheck(key);
        nullCheck(value);
        return Map<K, V>::putIfAbsent(key, value);
    }
    V remove(const K& key) override {
        nullCheck(key);
        return Map<K, V>::remove(key);
    }
    bool remove(const K& key, const V& value) override {
        nullCheck(key);
        return Map<K, V>::remove(key, value);
    }
    V replace(const K& key, const V& value) override {
        nullCheck(key);
        nullCheck(value);
        return Map<K, V>::replace(key, value);
    }
    bool replace(const K& key, const V& oldValue, const V& newValue) override {
        nullCheck(key);
        nullCheck(oldValue);
        nullCheck(newValue);
        return Map<K, V>::replace(key, oldValue, newValue);
    }
    ConcurrentHashMap<K, V>* clone() override {
        auto* m = new ConcurrentHashMap<K, V>();
        for (const auto& e : this->_entries()) m->putNoLock(e.getKey(), e.getValue());
        return m;
    }
    using Map<K, V>::putAll;
    using Map<K, V>::get;
    using Map<K, V>::getOptional;
    using Map<K, V>::containsKey;
    using Map<K, V>::containsValue;
    using Map<K, V>::remove;

private:
    template<class X>
    static void nullCheck(const X& x) {
        if (detail::isNullValue(x)) detail::throwNullPointer();
    }
};

}  // namespace jlang
