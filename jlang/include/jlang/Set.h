// jlang/Set.h - the insertion-ordered hash table used by jlang::Set and jlang::Map, and
// java.util.Set (jlang::Set<T>: HashSet, LinkedHashSet, FastSet, THashSet).
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/CollectionsCore.h>
#include <jlang/List.h>
#include <jlang/Util.h>

namespace jlang {

namespace detail {

struct Unit {
    friend bool operator==(const Unit&, const Unit&) noexcept { return true; }
};

// Insertion-ordered open-addressing hash table (the layout of a "compact dict"):
//   slots_: entries in insertion order; removed entries stay as dead slots until compaction
//   index_: power-of-two array of slot numbers (kEmpty / kTomb / slot), linear probing,
//           Fibonacci hashing of the Java hash (jlang::Hash<K>), stored hashes avoid most
//           equals() calls.
// Iteration walks slots_ by position, so iterating while inserting/removing is safe; dead
// slots are only compacted away when no live range-for iterator is active (guards_).
template<class K, class V>
class LinkedHashTable {
public:
    struct Slot {
        K key;
        [[no_unique_address]] V value;
        int32_t hash;
        bool live;
    };
    static constexpr int32_t kEmpty = -1;
    static constexpr int32_t kTomb = -2;

    LinkedHashTable() = default;
    LinkedHashTable(const LinkedHashTable& o) : slots_(o.slots_), live_(o.live_) {
        if (o.cap_ != 0) rebuildIndex(o.cap_);
    }
    LinkedHashTable& operator=(const LinkedHashTable& o) {
        if (this != &o) {
            slots_ = o.slots_;
            live_ = o.live_;
            index_ = nullptr;
            cap_ = 0;
            shift_ = 32;
            tombs_ = 0;
            if (o.cap_ != 0) rebuildIndex(o.cap_);
        }
        return *this;
    }

    // jlang::Hash/Equal (Java hashCode/equals); pointer keys use the collections' own helpers,
    // which also work while the pointee type is incomplete (see detail::elemObject).
    static int32_t hashOf(const K& k) {
        if constexpr (std::is_pointer_v<K>) return javaHash(k);
        else return static_cast<int32_t>(static_cast<uint32_t>(Hash<K>()(k)));
    }
    static bool keyEq(const K& a, const K& b) {
        if constexpr (std::is_pointer_v<K>) return javaEquals(a, b);
        else return Equal<K>()(a, b);
    }

    int32_t size() const noexcept { return live_; }
    std::size_t slotCount() const noexcept { return slots_.size(); }
    Slot& slot(std::size_t i) noexcept { return slots_[i]; }

    void reserve(int32_t n) {
        if (n <= 0) return;
        uint32_t need = 8;
        while (need < static_cast<uint32_t>(n) * 2u && need < (1u << 30)) need <<= 1;
        if (need > cap_) rebuildIndex(need);
        slots_.reserve(static_cast<std::size_t>(n));
    }

    // Slot number of k, or -1.
    int32_t find(const K& k, int32_t h) const {
        if (live_ == 0) return -1;
        const uint32_t mask = cap_ - 1;
        for (uint32_t p = home(h);; p = (p + 1) & mask) {
            const int32_t s = index_[p];
            if (s == kEmpty) return -1;
            if (s >= 0) {
                const Slot& sl = slots_[static_cast<std::size_t>(s)];
                if (sl.hash == h && keyEq(k, sl.key)) return s;
            }
        }
    }

    // Slot number of k; appends (k, V{}) when absent (inserted = true).
    int32_t findOrInsert(const K& k, int32_t h, bool& inserted) {
        maybeCompact();
        if (cap_ == 0 || static_cast<uint64_t>(live_ + tombs_ + 1) * 4 > static_cast<uint64_t>(cap_) * 3) grow();
        const uint32_t mask = cap_ - 1;
        int64_t firstTomb = -1;
        uint32_t p = home(h);
        for (;; p = (p + 1) & mask) {
            const int32_t s = index_[p];
            if (s == kEmpty) break;
            if (s == kTomb) {
                if (firstTomb < 0) firstTomb = p;
                continue;
            }
            const Slot& sl = slots_[static_cast<std::size_t>(s)];
            if (sl.hash == h && keyEq(k, sl.key)) {
                inserted = false;
                return s;
            }
        }
        const int32_t s = static_cast<int32_t>(slots_.size());
        slots_.push_back(Slot{k, V{}, h, true});
        if (firstTomb >= 0) {
            index_[firstTomb] = s;
            tombs_--;
        } else {
            index_[p] = s;
        }
        live_++;
        inserted = true;
        return s;
    }

    // Removes k; the old value is moved to *old when given.
    bool erase(const K& k, int32_t h, V* old = nullptr) {
        if (live_ == 0) return false;
        const uint32_t mask = cap_ - 1;
        for (uint32_t p = home(h);; p = (p + 1) & mask) {
            const int32_t s = index_[p];
            if (s == kEmpty) return false;
            if (s >= 0) {
                Slot& sl = slots_[static_cast<std::size_t>(s)];
                if (sl.hash == h && keyEq(k, sl.key)) {
                    if (old != nullptr) *old = std::move(sl.value);
                    index_[p] = kTomb;
                    tombs_++;
                    sl.live = false;
                    sl.key = K{};      // drop references for the collector
                    sl.value = V{};
                    live_--;
                    if (live_ == 0 && guards_ == 0) clear();
                    else maybeCompact();
                    return true;
                }
            }
        }
    }

    void clear() {
        slots_.clear();
        if (cap_ != 0) std::fill(index_, index_ + cap_, kEmpty);
        live_ = 0;
        tombs_ = 0;
    }

    // Live range-for iterators register themselves so dead slots are not compacted away
    // (which would renumber the slots) while they run.
    void addGuard() noexcept { guards_++; }
    void removeGuard() noexcept { guards_--; }

    template<class F>
    void forEachLive(F&& f) {
        for (std::size_t i = 0; i < slots_.size(); i++) {
            if (slots_[i].live) f(slots_[i]);
        }
    }

private:
    uint32_t home(int32_t h) const noexcept { return (static_cast<uint32_t>(h) * 0x9E3779B9u) >> shift_; }

    void grow() {
        uint32_t need = 8;
        const uint64_t want = static_cast<uint64_t>(live_ + 1) * 2;
        while (need < want && need < (1u << 30)) need <<= 1;
        rebuildIndex(need > cap_ ? need : cap_);
    }

    void rebuildIndex(uint32_t newCap) {
        int32_t* idx = static_cast<int32_t*>(collMallocAtomic(static_cast<std::size_t>(newCap) * sizeof(int32_t)));
        if (idx == nullptr) throw std::bad_alloc();
        std::fill(idx, idx + newCap, kEmpty);
        index_ = idx;
        cap_ = newCap;
        shift_ = 32u - static_cast<uint32_t>(std::countr_zero(newCap));
        tombs_ = 0;
        const uint32_t mask = cap_ - 1;
        for (std::size_t i = 0; i < slots_.size(); i++) {
            if (!slots_[i].live) continue;
            uint32_t p = home(slots_[i].hash);
            while (index_[p] != kEmpty) p = (p + 1) & mask;
            index_[p] = static_cast<int32_t>(i);
        }
    }

    void maybeCompact() {
        if (guards_ != 0) return;
        const std::size_t dead = slots_.size() - static_cast<std::size_t>(live_);
        if (dead < 16 || dead <= static_cast<std::size_t>(live_)) return;
        Vec<Slot> kept;
        kept.reserve(static_cast<std::size_t>(live_) + static_cast<std::size_t>(live_) / 2 + 1);
        for (auto& sl : slots_) {
            if (sl.live) kept.push_back(std::move(sl));
        }
        slots_.swap(kept);
        rebuildIndex(cap_);
    }

    Vec<Slot> slots_;
    int32_t* index_ = nullptr;  // GC atomic memory, cap_ entries
    uint32_t cap_ = 0;
    uint32_t shift_ = 32;
    int32_t live_ = 0;
    int32_t tombs_ = 0;
    int32_t guards_ = 0;
};

// Range-for over a hash table: live (by slot position, guarded) or over a snapshot.
// Out is the dereferenced type; Proj builds it from a slot.
template<class K, class V, class Out, class Proj>
class TableRange {
public:
    using value_type = Out;
    using difference_type = std::ptrdiff_t;
    // Live iteration.
    TableRange(LinkedHashTable<K, V>* t, Proj proj) : t_(t), proj_(proj) { t_->addGuard(); }
    // Snapshot iteration.
    explicit TableRange(Vec<Out>* snap) : snap_(snap) {}
    TableRange(const TableRange& o) : t_(o.t_), snap_(o.snap_), proj_(o.proj_), i_(o.i_), cur_(o.cur_) {
        if (t_ != nullptr) t_->addGuard();
    }
    TableRange& operator=(const TableRange&) = delete;
    ~TableRange() {
        if (t_ != nullptr) t_->removeGuard();
    }
    bool operator!=(IterEnd) {
        if (snap_ != nullptr) return i_ < snap_->size();
        while (i_ < t_->slotCount() && !t_->slot(i_).live) ++i_;
        return i_ < t_->slotCount();
    }
    bool operator==(IterEnd e) { return !(*this != e); }
    Out& operator*() {
        if (snap_ != nullptr) return (*snap_)[i_];
        cur_ = proj_(t_->slot(i_));
        return cur_;
    }
    TableRange& operator++() {
        ++i_;
        return *this;
    }

private:
    LinkedHashTable<K, V>* t_ = nullptr;
    Vec<Out>* snap_ = nullptr;
    Proj proj_{};
    std::size_t i_ = 0;
    Out cur_{};
};

template<class K>
struct KeyProj {
    template<class S>
    K operator()(const S& s) const {
        return s.key;
    }
};

}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.util.Set<E> (HashSet/LinkedHashSet/FastSet/THashSet): an insertion-ordered hash set.
// Equality and hashing are Java's (jlang::Hash/Equal: equals()/hashCode() for objects, value
// equality for numbers, Strings and enums). O(1) add/remove/contains.
//
// Range-for is live (inserting or removing during the loop is safe: removed elements are
// skipped, added ones are visited) or over a snapshot when the set is shared. iterator()
// iterates a snapshot; its remove() removes the element from the set.
template<class T>
class Set : public virtual Collection<T> {
public:
    using Collection<T>::addAll;
    using Collection<T>::contains;
    using Collection<T>::removeObject;

    Set() {}
    explicit Set(int32_t initialCapacity) {
        if (initialCapacity < 0) {
            detail::throwIllegalArgument("Illegal initial capacity: " + std::to_string(initialCapacity));
        }
        t_.reserve(initialCapacity);
    }
    Set(int32_t initialCapacity, float loadFactor) : Set(initialCapacity) {
        if (!(loadFactor > 0)) detail::throwIllegalArgument(std::string("Illegal load factor: ") + String::valueOf(loadFactor));
    }
    Set(std::initializer_list<T> values) {
        for (const auto& v : values) addNoLock(v);
    }
    explicit Set(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        for (const auto& e : c->_snapshot()) addNoLock(e);
    }
    template<class U>
        requires(!std::is_same_v<U, T> && std::is_convertible_v<const U&, T>)
    explicit Set(Collection<U>* c) {
        if (c == nullptr) detail::throwNullPointer();
        for (const auto& e : c->_snapshot()) addNoLock(static_cast<T>(e));
    }

    int32_t size() override {
        auto g = this->lock();
        return t_.size();
    }
    bool isEmpty() override {
        auto g = this->lock();
        return t_.size() == 0;
    }
    bool contains(const T& o) override {
        auto g = this->lock();
        return t_.find(o, t_.hashOf(o)) >= 0;
    }
    bool add(const T& e) override {
        auto g = this->lock();
        return addNoLock(e);
    }
    bool removeObject(const T& o) override {
        auto g = this->lock();
        return t_.erase(o, t_.hashOf(o));
    }
    void clear() override {
        auto g = this->lock();
        t_.clear();
    }
    Iterator<T>* iterator() override { return new detail::SnapshotIterator<T>(this, this->_snapshot()); }

    // AbstractSet.removeAll
    bool removeAll(Collection<T>* c) override {
        if (c == nullptr) detail::throwNullPointer();
        auto g = this->lock();
        bool modified = false;
        if (size() > c->size()) {
            for (const auto& e : c->_snapshot()) {
                if (removeObject(e)) modified = true;
            }
        } else {
            for (const auto& e : this->_snapshot()) {
                if (c->contains(e)) {
                    removeObject(e);
                    modified = true;
                }
            }
        }
        return modified;
    }

    // AbstractSet.equals / hashCode
    bool equals(Object* o) override {
        if (o == static_cast<Object*>(this)) return true;
        auto* other = dynamic_cast<Set<T>*>(o);
        if (other == nullptr) return false;
        if (other->size() != size()) return false;
        for (const auto& e : other->_snapshot()) {
            if (!contains(e)) return false;
        }
        return true;
    }
    int32_t hashCode() override {
        int32_t h = 0;
        for (const auto& e : this->_snapshot()) h += detail::javaHash(static_cast<const T&>(e));
        return h;
    }
    // HashSet.clone(): a shallow, unlocked copy (a TreeSet clones itself as a TreeSet).
    Set<T>* clone() override {
        auto* s = new Set<T>();
        for (const auto& e : this->_snapshot()) s->addNoLock(e);
        return s;
    }

    Set<T>* shared() {
        this->shared_ = true;
        return this;
    }
    Set<T>* synchronized_() { return shared(); }

    using Range = detail::TableRange<T, detail::Unit, T, detail::KeyProj<T>>;
    Range begin() {
        if (this->shared_ || !_hashBacked()) return Range(detail::heapVec(this->_snapshot()));
        return Range(&t_, detail::KeyProj<T>{});
    }
    detail::IterEnd end() { return {}; }
    Range begin() const { return const_cast<Set*>(this)->begin(); }
    detail::IterEnd end() const { return {}; }

    detail::Vec<T> _snapshot() override {
        auto g = this->lock();
        detail::Vec<T> out;
        out.reserve(static_cast<std::size_t>(t_.size()));
        t_.forEachLive([&](auto& s) { out.push_back(s.key); });
        return out;
    }

protected:
    // false for subclasses with their own storage (TreeSet).
    virtual bool _hashBacked() { return true; }
    bool addNoLock(const T& e) {
        bool inserted = false;
        t_.findOrInsert(e, t_.hashOf(e), inserted);
        return inserted;
    }

    detail::LinkedHashTable<T, detail::Unit> t_;
};

}  // namespace jlang
