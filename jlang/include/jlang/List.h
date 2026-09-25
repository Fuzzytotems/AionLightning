// jlang/List.h - java.util.Collection (jlang::Collection<T>) and java.util.List
// (jlang::List<T>: ArrayList, LinkedList, Vector, FastList, CopyOnWriteArrayList, ...).
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/Array.h>
#include <jlang/CollectionsCore.h>

namespace jlang {

namespace detail {

// Element types for which Java's list.remove(x) means remove(int index) (byte, short, char and
// int widen to int before boxing is considered). For these, List<T> has no remove(const T&):
// use removeObject(x) for Java's remove(Object) and remove(i)/removeAt(i) for the index form.
template<class T>
inline constexpr bool kIndexLike = std::is_same_v<T, int32_t> || std::is_same_v<T, int16_t> ||
                                   std::is_same_v<T, int8_t> || std::is_same_v<T, char16_t>;

// Range-for over a vector that may change during the loop (index based, re-checks the size
// every step, dereferences into a private copy so the loop variable stays valid).
template<class T>
class VecRange {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    explicit VecRange(Vec<T>* v) : v_(v) {}
    bool operator!=(IterEnd) const { return i_ < v_->size(); }
    bool operator==(IterEnd e) const { return !(*this != e); }
    T& operator*() {
        cur_ = (*v_)[i_];
        return cur_;
    }
    VecRange& operator++() {
        ++i_;
        return *this;
    }

private:
    Vec<T>* v_;
    std::size_t i_ = 0;
    T cur_{};
};

// Range-for over a private snapshot (never modified by anyone else).
template<class T>
class SnapshotRange {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    explicit SnapshotRange(Vec<T>* v) : v_(v) {}
    bool operator!=(IterEnd) const { return i_ < v_->size(); }
    bool operator==(IterEnd e) const { return !(*this != e); }
    decltype(auto) operator*() {
        if constexpr (std::is_same_v<T, bool>) {
            cur_ = (*v_)[i_];
            return (cur_);
        } else {
            return ((*v_)[i_]);
        }
    }
    SnapshotRange& operator++() {
        ++i_;
        return *this;
    }

private:
    Vec<T>* v_;
    std::size_t i_ = 0;
    [[no_unique_address]] std::conditional_t<std::is_same_v<T, bool>, bool, IterEnd> cur_{};
};

template<class T>
Vec<T>* heapVec(Vec<T>&& v) {
    return new Vec<T>(std::move(v));
}

// Iterator over a snapshot of a collection; remove() removes the last returned element from
// the collection (Collection::_removeIterated with the element's expected position).
template<class T>
class SnapshotIterator final : public virtual Iterator<T> {
public:
    SnapshotIterator(Collection<T>* owner, Vec<T>&& snap, int32_t start = 0)
        : owner_(owner), snap_(std::move(snap)), cursor_(static_cast<std::size_t>(start)) {}
    bool hasNext() override { return cursor_ < snap_.size(); }
    T next() override {
        if (cursor_ >= snap_.size()) throwNoSuchElement();
        lastRet_ = static_cast<int32_t>(cursor_++);
        return snap_[static_cast<std::size_t>(lastRet_)];
    }
    void remove() override {
        if (lastRet_ < 0) throwIllegalState();
        owner_->_removeIterated(snap_[static_cast<std::size_t>(lastRet_)], lastRet_ - removed_);
        removed_++;
        lastRet_ = -1;
    }
    bool hasPrevious() override { return cursor_ > 0; }
    T previous() override {
        if (cursor_ == 0) throwNoSuchElement();
        cursor_--;
        lastRet_ = static_cast<int32_t>(cursor_);
        return snap_[cursor_];
    }
    int32_t nextIndex() override { return static_cast<int32_t>(cursor_); }
    int32_t previousIndex() override { return static_cast<int32_t>(cursor_) - 1; }

private:
    Collection<T>* owner_;
    Vec<T> snap_;
    std::size_t cursor_;
    int32_t lastRet_ = -1;
    int32_t removed_ = 0;
};

template<class T>
void appendCollection(std::string& out, const Vec<T>& v, const void* self) {
    out.push_back('[');
    bool first = true;
    for (const auto& e : v) {
        if (!first) out.append(", ", 2);
        first = false;
        if constexpr (std::is_pointer_v<T>) {
            if (e != nullptr && static_cast<const void*>(elemObject(e)) == self) {
                out.append("(this Collection)");
                continue;
            }
        }
        appendElem(out, static_cast<const T&>(e));
    }
    out.push_back(']');
}

}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.util.Collection<E>: the common base of List, Set, TreeSet, Deque and PriorityQueue.
// Every method is virtual (codebase classes may implement Collection). remove(Object) is
// removeObject(o); remove(o) forwards to it (List<int32_t> hides it, see List).
//
// Thread safety: shared()/synchronized_() enable the collection lock (the object's monitor):
// every method then runs under it, and iteration (range-for, iterator()) uses snapshots.
template<class T>
class Collection : public virtual Iterable<T> {
public:
    using value_type = T;

    virtual int32_t size() = 0;
    virtual bool isEmpty() { return size() == 0; }
    virtual bool contains(const T& o) = 0;
    virtual bool add(const T& e) = 0;
    virtual bool removeObject(const T& o) = 0;
    bool remove(const T& o) { return removeObject(o); }
    virtual void clear() = 0;
    Iterator<T>* iterator() override = 0;

    // contains/remove(Object) with a supertype pointer (see detail::SuperPointerArg).
    template<class U>
        requires detail::SuperPointerArg<T, U>
    bool contains(U* o) {
        auto t = detail::downcastArg<T>(o);
        return t.has_value() && contains(*t);
    }
    template<class U>
        requires detail::SuperPointerArg<T, U>
    bool removeObject(U* o) {
        auto t = detail::downcastArg<T>(o);
        return t.has_value() && removeObject(*t);
    }
    template<class U>
        requires detail::SuperPointerArg<T, U>
    bool remove(U* o) {
        return removeObject(o);
    }

    virtual bool containsAll(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        for (const auto& e : c->_snapshot()) {
            if (!contains(e)) return false;
        }
        return true;
    }
    virtual bool addAll(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        bool modified = false;
        for (const auto& e : c->_snapshot()) {
            if (add(e)) modified = true;
        }
        return modified;
    }
    // addAll(Collection<? extends E>)
    template<class U>
        requires(!std::is_same_v<U, T> && std::is_convertible_v<const U&, T>)
    bool addAll(Collection<U>* c) {
        if (c == nullptr) detail::throwNullPointer();
        bool modified = false;
        for (const auto& e : c->_snapshot()) {
            if (add(static_cast<T>(e))) modified = true;
        }
        return modified;
    }
    virtual bool removeAll(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        bool modified = false;
        for (const auto& e : _snapshot()) {
            if (c->contains(e)) {
                removeObject(e);
                modified = true;
            }
        }
        return modified;
    }
    virtual bool retainAll(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        bool modified = false;
        for (const auto& e : _snapshot()) {
            if (!c->contains(e)) {
                removeObject(e);
                modified = true;
            }
        }
        return modified;
    }

    // toArray() / toArray(T[] a) with Java's semantics (a too small: a new array; a larger than
    // the collection: a[size] = null/0).
    virtual Array<T>* toArray() {
        detail::Vec<T> s = _snapshot();
        return Array<T>::fromRange(s.begin(), s.end());
    }
    virtual Array<T>* toArray(Array<T>* a) { return toArrayInto(a); }
    template<class U>
        requires(!std::is_same_v<U, T> && std::is_convertible_v<const T&, U>)
    Array<U>* toArray(Array<U>* a) {
        return toArrayInto(a);
    }

    bool isShared() { return shared_; }
    Collection<T>* shared() {
        shared_ = true;
        return this;
    }
    Collection<T>* synchronized_() { return shared(); }

    // Range-for through a Collection pointer: live and index based for an unshared List,
    // otherwise over a snapshot (List/Set/Deque also have their own begin()/end()).
    detail::VecRange<T> begin();
    detail::IterEnd end() { return {}; }
    detail::VecRange<T> begin() const { return const_cast<Collection*>(this)->begin(); }
    detail::IterEnd end() const { return {}; }

    // "[a, b, c]"
    String toString() override {
        std::string out;
        detail::appendCollection(out, _snapshot(), static_cast<const void*>(static_cast<Object*>(this)));
        return String(std::move(out));
    }

    // jlang internals: the elements in iteration order (taken under the lock), and the removal
    // of an element returned by a snapshot iterator (hint = its expected current position).
    virtual detail::Vec<T> _snapshot() = 0;
    virtual void _removeIterated(const T& e, int32_t hint) {
        (void)hint;
        removeObject(e);
    }

protected:
    template<class U>
    Array<U>* toArrayInto(Array<U>* a) {
        if (a == nullptr) detail::throwNullPointer();
        detail::Vec<T> s = _snapshot();
        const int32_t n = static_cast<int32_t>(s.size());
        if (a->length < n) a = new Array<U>(n);
        U* d = a->data();
        for (int32_t i = 0; i < n; i++) d[i] = static_cast<U>(s[static_cast<std::size_t>(i)]);
        if (a->length > n) d[n] = U{};
        return a;
    }
    detail::CollLock lock() { return detail::CollLock(this, shared_); }

    bool shared_ = false;
};

// ---------------------------------------------------------------------------------------
// java.util.List<E> (ArrayList semantics; also the LinkedList/Deque-style methods).
//
//   * remove(int index) returns the element; remove(const T&) is Java's remove(Object) and
//     exists only when T is not int/short/byte/char (List<int32_t>: removeObject(x) vs
//     remove(i)/removeAt(i), exactly the Java overload rules).
//   * get/set/add/remove with a bad index throw IndexOutOfBoundsException("Index: 5, Size: 3")
//     (a negative index: ArrayIndexOutOfBoundsException("-1"), as ArrayList does).
//   * Iteration (range-for, iterator(), listIterator()) is index based and never fails when
//     the list is modified during the loop; a shared (locked) list iterates a snapshot.
//   * subList(a, b) returns a copy (not a view).
template<class T>
class List : public virtual Collection<T> {
public:
    using Storage = detail::Vec<T>;
    using Collection<T>::addAll;
    using Collection<T>::toArray;
    using Collection<T>::contains;
    using Collection<T>::removeObject;

    List() {}
    explicit List(int32_t initialCapacity) {
        if (initialCapacity < 0) detail::throwIllegalArgument("Illegal Capacity: " + std::to_string(initialCapacity));
        v_.reserve(static_cast<std::size_t>(initialCapacity));
    }
    List(std::initializer_list<T> values) : v_(values.begin(), values.end()) {}
    explicit List(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        v_ = c->_snapshot();
    }
    template<class U>
        requires(!std::is_same_v<U, T> && std::is_convertible_v<const U&, T>)
    explicit List(Collection<U>* c) {
        if (c == nullptr) detail::throwNullPointer();
        for (const auto& e : c->_snapshot()) v_.push_back(static_cast<T>(e));
    }
    template<class It>
        requires(!std::is_integral_v<It>)
    List(It first, It last) : v_(first, last) {}

    // ---- Collection
    int32_t size() override {
        auto g = this->lock();
        return static_cast<int32_t>(v_.size());
    }
    bool isEmpty() override {
        auto g = this->lock();
        return v_.empty();
    }
    bool contains(const T& o) override { return indexOf(o) >= 0; }
    bool add(const T& e) override {
        auto g = this->lock();
        v_.push_back(e);
        return true;
    }
    bool removeObject(const T& o) override {
        auto g = this->lock();
        const int32_t i = indexOf(o);
        if (i < 0) return false;
        _removeIndex(i);
        return true;
    }
    void clear() override {
        auto g = this->lock();
        v_.clear();
    }
    bool addAll(Collection<T>* c) override {
        if (c == nullptr) detail::throwNullPointer();
        Storage s = c->_snapshot();
        auto g = this->lock();
        const int32_t n = static_cast<int32_t>(v_.size());
        return addAllAt(n, s);
    }
    bool removeAll(Collection<T>* c) override { return batchRemove(c, false); }
    bool retainAll(Collection<T>* c) override { return batchRemove(c, true); }
    Iterator<T>* iterator() override;
    Array<T>* toArray() override {
        auto g = this->lock();
        return Array<T>::fromRange(v_.begin(), v_.end());
    }

    // ---- List
    virtual T get(int32_t index) {
        auto g = this->lock();
        checkIndex(index);
        return v_[static_cast<std::size_t>(index)];
    }
    virtual T set(int32_t index, const T& e) {
        auto g = this->lock();
        checkIndex(index);
        T old = v_[static_cast<std::size_t>(index)];
        v_[static_cast<std::size_t>(index)] = e;
        return old;
    }
    virtual void add(int32_t index, const T& e) {
        auto g = this->lock();
        const int32_t n = static_cast<int32_t>(v_.size());
        if (index > n || index < 0) detail::throwIndexOutOfBounds(index, n);
        v_.insert(v_.begin() + index, e);
    }
    virtual T removeAt(int32_t index) {
        auto g = this->lock();
        checkIndex(index);
        T old = v_[static_cast<std::size_t>(index)];
        v_.erase(v_.begin() + index);
        return old;
    }
    T remove(int32_t index) { return removeAt(index); }
    bool remove(const T& o)
        requires(!detail::kIndexLike<T>)
    {
        return removeObject(o);
    }
    template<class U>
        requires detail::SuperPointerArg<T, U>
    bool remove(U* o) {
        return removeObject(o);
    }
    template<class U>
        requires detail::SuperPointerArg<T, U>
    int32_t indexOf(U* o) {
        auto t = detail::downcastArg<T>(o);
        return t.has_value() ? indexOf(*t) : -1;
    }
    template<class U>
        requires detail::SuperPointerArg<T, U>
    int32_t lastIndexOf(U* o) {
        auto t = detail::downcastArg<T>(o);
        return t.has_value() ? lastIndexOf(*t) : -1;
    }
    // Queue/Deque remove(): removes the first element (NoSuchElementException when empty).
    virtual T remove() { return removeFirst(); }
    virtual bool addAll(int32_t index, Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        Storage s = c->_snapshot();
        auto g = this->lock();
        const int32_t n = static_cast<int32_t>(v_.size());
        if (index > n || index < 0) detail::throwIndexOutOfBounds(index, n);
        return addAllAt(index, s);
    }
    virtual int32_t indexOf(const T& o) {
        auto g = this->lock();
        const std::size_t n = v_.size();
        for (std::size_t i = 0; i < n; i++) {
            if (detail::javaEquals(o, static_cast<const T&>(v_[i]))) return static_cast<int32_t>(i);
        }
        return -1;
    }
    virtual int32_t lastIndexOf(const T& o) {
        auto g = this->lock();
        for (std::size_t i = v_.size(); i-- > 0;) {
            if (detail::javaEquals(o, static_cast<const T&>(v_[i]))) return static_cast<int32_t>(i);
        }
        return -1;
    }
    // Copy of the range [fromIndex, toIndex) (Java returns a view).
    virtual List<T>* subList(int32_t fromIndex, int32_t toIndex) {
        auto g = this->lock();
        const int32_t n = static_cast<int32_t>(v_.size());
        if (fromIndex < 0) detail::throwIndexOutOfBoundsMsg("fromIndex = " + std::to_string(fromIndex));
        if (toIndex > n) detail::throwIndexOutOfBoundsMsg("toIndex = " + std::to_string(toIndex));
        if (fromIndex > toIndex) {
            detail::throwIllegalArgument("fromIndex(" + std::to_string(fromIndex) + ") > toIndex(" +
                                         std::to_string(toIndex) + ")");
        }
        return new List<T>(v_.begin() + fromIndex, v_.begin() + toIndex);
    }
    Iterator<T>* listIterator() { return listIterator(0); }
    virtual Iterator<T>* listIterator(int32_t index);

    // Sorting (stable, Java 6 merge sort): natural order, a Comparator (null = natural order)
    // or a lambda returning a Java compare() int.
    virtual void sort(Comparator<T>* c) { sortWith(detail::PtrCmp<T, T>{c}); }
    void sort() { sortWith(detail::NaturalCmp<T>{}); }
    template<class U>
        requires(!std::is_same_v<U, T> && std::is_convertible_v<const T&, U>)
    void sort(Comparator<U>* c) {
        sortWith(detail::PtrCmp<T, U>{c});
    }
    template<class F>
        requires detail::IsJavaCmpFn<F, T>
    void sort(F f) {
        sortWith(detail::FnCmp<F>{&f});
    }

    // ---- LinkedList / Deque / Queue style
    virtual void addFirst(const T& e) { add(0, e); }
    virtual void addLast(const T& e) { add(e); }
    virtual bool offer(const T& e) { return add(e); }
    virtual bool offerFirst(const T& e) {
        addFirst(e);
        return true;
    }
    virtual bool offerLast(const T& e) { return add(e); }
    virtual void push(const T& e) { addFirst(e); }
    virtual T pop() { return removeFirst(); }
    virtual T element() { return getFirst(); }
    virtual T getFirst() {
        auto g = this->lock();
        if (v_.empty()) detail::throwNoSuchElement();
        return v_.front();
    }
    virtual T getLast() {
        auto g = this->lock();
        if (v_.empty()) detail::throwNoSuchElement();
        return v_.back();
    }
    virtual T removeFirst() {
        auto g = this->lock();
        if (v_.empty()) detail::throwNoSuchElement();
        return removeAt(0);
    }
    virtual T removeLast() {
        auto g = this->lock();
        if (v_.empty()) detail::throwNoSuchElement();
        return removeAt(static_cast<int32_t>(v_.size()) - 1);
    }
    virtual T poll() { return pollFirst(); }
    virtual T pollFirst() {
        auto g = this->lock();
        if (v_.empty()) return T{};
        return removeAt(0);
    }
    virtual T pollLast() {
        auto g = this->lock();
        if (v_.empty()) return T{};
        return removeAt(static_cast<int32_t>(v_.size()) - 1);
    }
    virtual T peek() { return peekFirst(); }
    virtual T peekFirst() {
        auto g = this->lock();
        return v_.empty() ? T{} : T(v_.front());
    }
    virtual T peekLast() {
        auto g = this->lock();
        return v_.empty() ? T{} : T(v_.back());
    }
    virtual bool removeFirstOccurrence(const T& o) { return removeObject(o); }
    virtual bool removeLastOccurrence(const T& o) {
        auto g = this->lock();
        const int32_t i = lastIndexOf(o);
        if (i < 0) return false;
        _removeIndex(i);
        return true;
    }

    // ---- ArrayList
    virtual void ensureCapacity(int32_t minCapacity) {
        auto g = this->lock();
        if (minCapacity > 0) v_.reserve(static_cast<std::size_t>(minCapacity));
    }
    virtual void trimToSize() {}

    // ---- Object
    bool equals(Object* o) override {
        if (o == static_cast<Object*>(this)) return true;
        auto* other = dynamic_cast<List<T>*>(o);
        if (other == nullptr) return false;
        Storage a = _snapshot();
        Storage b = other->_snapshot();
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); i++) {
            if (!detail::javaEquals(static_cast<const T&>(a[i]), static_cast<const T&>(b[i]))) return false;
        }
        return true;
    }
    int32_t hashCode() override {
        auto g = this->lock();
        int32_t h = 1;
        for (const auto& e : v_) h = 31 * h + detail::javaHash(static_cast<const T&>(e));
        return h;
    }
    String toString() override {
        std::string out;
        {
            auto g = this->lock();
            detail::appendCollection(out, v_, static_cast<const void*>(static_cast<Object*>(this)));
        }
        return String(std::move(out));
    }
    // ArrayList.clone(): a shallow copy (unlocked).
    List<T>* clone() override {
        Storage s = _snapshot();
        return new List<T>(s.begin(), s.end());
    }

    List<T>* shared() {
        this->shared_ = true;
        return this;
    }
    List<T>* synchronized_() { return shared(); }

    // ---- C++ access
    // Range-for: live and index based, or over a snapshot when the list is shared.
    detail::VecRange<T> begin() {
        if (this->shared_) {
            auto g = this->lock();
            return detail::VecRange<T>(new Storage(v_));
        }
        return detail::VecRange<T>(&v_);
    }
    detail::IterEnd end() { return {}; }
    detail::VecRange<T> begin() const { return const_cast<List*>(this)->begin(); }
    detail::IterEnd end() const { return {}; }
    // The backing std::vector (for STL algorithms; no locking).
    Storage& vec() noexcept { return v_; }

    Storage _snapshot() override {
        auto g = this->lock();
        return v_;
    }
    // Removal by position on behalf of remove(Object), removeAll/retainAll and iterators (map
    // views route it to the map; their public removeAt() is not Java API and throws).
    virtual T _removeIndex(int32_t index) { return removeAt(index); }
    void _removeIterated(const T& e, int32_t hint) override {
        auto g = this->lock();
        if (hint >= 0 && hint < static_cast<int32_t>(v_.size()) &&
            detail::javaEquals(static_cast<const T&>(v_[static_cast<std::size_t>(hint)]), e)) {
            _removeIndex(hint);
        } else {
            removeObject(e);
        }
    }

protected:
    void checkIndex(int32_t index) {
        if (index >= static_cast<int32_t>(v_.size())) detail::throwIndexOutOfBounds(index, static_cast<int32_t>(v_.size()));
        if (index < 0) detail::throwArrayIndexOutOfBounds(index);
    }
    // Inserts s at index (lock held, index valid). Routed through add(i, e) only when a
    // subclass may intercept insertion (views); plain lists insert in one step.
    bool addAllAt(int32_t index, const Storage& s) {
        if (s.empty()) return false;
        v_.insert(v_.begin() + index, s.begin(), s.end());
        return true;
    }
    bool batchRemove(Collection<T>* c, bool complement) {
        if (c == nullptr) detail::throwNullPointer();
        auto g = this->lock();
        if (static_cast<Collection<T>*>(this) == c) {
            if (complement) return false;
            const bool had = !v_.empty();
            clear();
            return had;
        }
        if (_isView()) {
            bool modified = false;
            for (int32_t i = static_cast<int32_t>(v_.size()) - 1; i >= 0; i--) {
                if (c->contains(static_cast<const T&>(v_[static_cast<std::size_t>(i)])) != complement) {
                    _removeIndex(i);
                    modified = true;
                }
            }
            return modified;
        }
        // ArrayList.batchRemove: one compaction pass (c->contains may not modify this list).
        Storage keep;
        keep.reserve(v_.size());
        for (const auto& e : v_) {
            if (c->contains(static_cast<const T&>(e)) == complement) keep.push_back(e);
        }
        if (keep.size() == v_.size()) return false;
        v_.swap(keep);
        return true;
    }
    template<class Cmp>
    void sortWith(Cmp cmp) {
        auto g = this->lock();
        if (_isView()) detail::throwUnsupportedOperation();
        const int32_t n = static_cast<int32_t>(v_.size());
        if (n < 2) return;
        T* a = detail::GcAllocator<T>().allocate(static_cast<std::size_t>(n));
        std::uninitialized_copy(v_.begin(), v_.end(), a);
        T* aux = detail::tempCopy(a, n);
        detail::legacyMergeSort(aux, a, 0, n, 0, cmp);
        std::copy(a, a + n, v_.begin());
    }
    // Map views (keySet()/values()/entrySet() snapshots) route every removal through
    // _removeIndex() so that it reaches the map, and reject insertion and reordering.
    virtual bool _isView() { return false; }

    Storage v_;
};

namespace detail {

// java.util.ArrayList.ListItr without the ConcurrentModificationException checks: works on the
// live list by index through the virtual List API.
template<class T>
class ListItr final : public virtual Iterator<T> {
public:
    ListItr(List<T>* l, int32_t index) : l_(l), cursor_(index) {}
    bool hasNext() override { return cursor_ < l_->size(); }
    T next() override {
        const int32_t i = cursor_;
        if (i < 0 || i >= l_->size()) throwNoSuchElement();
        T e = l_->get(i);
        cursor_ = i + 1;
        lastRet_ = i;
        return e;
    }
    void remove() override {
        if (lastRet_ < 0) throwIllegalState();
        l_->_removeIndex(lastRet_);
        cursor_ = lastRet_;
        lastRet_ = -1;
    }
    bool hasPrevious() override { return cursor_ > 0; }
    T previous() override {
        int32_t i = cursor_ - 1;
        const int32_t n = l_->size();
        if (i >= n) i = n - 1;
        if (i < 0) throwNoSuchElement();
        T e = l_->get(i);
        cursor_ = i;
        lastRet_ = i;
        return e;
    }
    int32_t nextIndex() override { return cursor_; }
    int32_t previousIndex() override { return cursor_ - 1; }
    void set(const T& e) override {
        if (lastRet_ < 0) throwIllegalState();
        l_->set(lastRet_, e);
    }
    void add(const T& e) override {
        l_->add(cursor_, e);
        cursor_++;
        lastRet_ = -1;
    }

private:
    List<T>* l_;
    int32_t cursor_;
    int32_t lastRet_ = -1;
};

}  // namespace detail

template<class T>
detail::VecRange<T> Collection<T>::begin() {
    if (!shared_) {
        if (auto* l = dynamic_cast<List<T>*>(this)) return detail::VecRange<T>(&l->vec());
    }
    return detail::VecRange<T>(detail::heapVec(_snapshot()));
}

template<class T>
Iterator<T>* List<T>::iterator() {
    if (this->shared_) return new detail::SnapshotIterator<T>(this, _snapshot());
    return new detail::ListItr<T>(this, 0);
}

template<class T>
Iterator<T>* List<T>::listIterator(int32_t index) {
    const int32_t n = size();
    if (index < 0 || index > n) detail::throwIndexOutOfBoundsMsg("Index: " + std::to_string(index));
    if (this->shared_) return new detail::SnapshotIterator<T>(this, _snapshot(), index);
    return new detail::ListItr<T>(this, index);
}

}  // namespace jlang
