// jlang/Deque.h - java.util.Queue/Deque/ArrayDeque/LinkedList-as-queue (jlang::Deque<T>),
// java.util.concurrent.ConcurrentLinkedQueue and java.util.PriorityQueue.
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/CollectionsCore.h>
#include <jlang/List.h>

#include <deque>

namespace jlang {

namespace detail {

template<class T>
using DequeStorage = std::deque<T, GcAllocator<T>>;

// Range-for over a std::deque by index (re-checks the size every step).
template<class T>
class DequeRange {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    explicit DequeRange(DequeStorage<T>* d) : d_(d) {}
    bool operator!=(IterEnd) const { return i_ < d_->size(); }
    bool operator==(IterEnd e) const { return !(*this != e); }
    T& operator*() {
        cur_ = (*d_)[i_];
        return cur_;
    }
    DequeRange& operator++() {
        ++i_;
        return *this;
    }

private:
    DequeStorage<T>* d_;
    std::size_t i_ = 0;
    T cur_{};
};

}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.util.Deque / Queue / ArrayDeque (and LinkedList used as a queue): a double-ended queue
// on std::deque. poll/peek return T{} (nullptr/0) when empty; remove()/element()/getFirst()/
// removeFirst()... throw NoSuchElementException. remove(x) removes the first occurrence.
// Iteration is index based (safe against modification during the loop), or over a snapshot
// when the deque is shared.
template<class T>
class Deque : public virtual Collection<T> {
public:
    using Storage = detail::DequeStorage<T>;
    using Collection<T>::addAll;
    using Collection<T>::contains;
    using Collection<T>::removeObject;

    Deque() {}
    explicit Deque(int32_t numElements) { (void)numElements; }
    explicit Deque(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        for (const auto& e : c->_snapshot()) d_.push_back(e);
    }
    template<class U>
        requires(!std::is_same_v<U, T> && std::is_convertible_v<const U&, T>)
    explicit Deque(Collection<U>* c) {
        if (c == nullptr) detail::throwNullPointer();
        for (const auto& e : c->_snapshot()) d_.push_back(static_cast<T>(e));
    }
    Deque(std::initializer_list<T> values) {
        for (const auto& v : values) d_.push_back(v);  // (the range constructor trips a GCC 13 -Wmaybe-uninitialized)
    }

    // ---- Collection
    int32_t size() override {
        auto g = this->lock();
        return static_cast<int32_t>(d_.size());
    }
    bool isEmpty() override {
        auto g = this->lock();
        return d_.empty();
    }
    bool contains(const T& o) override {
        auto g = this->lock();
        for (const auto& e : d_) {
            if (detail::javaEquals(o, static_cast<const T&>(e))) return true;
        }
        return false;
    }
    bool add(const T& e) override {
        addLast(e);
        return true;
    }
    bool removeObject(const T& o) override { return removeFirstOccurrence(o); }
    void clear() override {
        auto g = this->lock();
        d_.clear();
    }
    Iterator<T>* iterator() override;
    Iterator<T>* descendingIterator() {
        detail::Vec<T> s = this->_snapshot();
        std::reverse(s.begin(), s.end());
        return new detail::SnapshotIterator<T>(this, std::move(s));
    }

    // ---- Deque / Queue
    virtual void addFirst(const T& e) {
        auto g = this->lock();
        d_.push_front(e);
    }
    virtual void addLast(const T& e) {
        auto g = this->lock();
        d_.push_back(e);
    }
    virtual bool offer(const T& e) { return offerLast(e); }
    virtual bool offerFirst(const T& e) {
        addFirst(e);
        return true;
    }
    virtual bool offerLast(const T& e) {
        addLast(e);
        return true;
    }
    virtual void push(const T& e) { addFirst(e); }
    virtual T pop() { return removeFirst(); }
    virtual T poll() { return pollFirst(); }
    virtual T pollFirst() {
        auto g = this->lock();
        if (d_.empty()) return T{};
        T e = d_.front();
        d_.pop_front();
        return e;
    }
    virtual T pollLast() {
        auto g = this->lock();
        if (d_.empty()) return T{};
        T e = d_.back();
        d_.pop_back();
        return e;
    }
    virtual T peek() { return peekFirst(); }
    virtual T peekFirst() {
        auto g = this->lock();
        return d_.empty() ? T{} : T(d_.front());
    }
    virtual T peekLast() {
        auto g = this->lock();
        return d_.empty() ? T{} : T(d_.back());
    }
    virtual T element() { return getFirst(); }
    virtual T getFirst() {
        auto g = this->lock();
        if (d_.empty()) detail::throwNoSuchElement();
        return d_.front();
    }
    virtual T getLast() {
        auto g = this->lock();
        if (d_.empty()) detail::throwNoSuchElement();
        return d_.back();
    }
    // Queue.remove(): removes the head (NoSuchElementException when empty).
    virtual T remove() { return removeFirst(); }
    using Collection<T>::remove;
    virtual T removeFirst() {
        auto g = this->lock();
        if (d_.empty()) detail::throwNoSuchElement();
        return pollFirst();
    }
    virtual T removeLast() {
        auto g = this->lock();
        if (d_.empty()) detail::throwNoSuchElement();
        return pollLast();
    }
    virtual bool removeFirstOccurrence(const T& o) {
        auto g = this->lock();
        for (std::size_t i = 0; i < d_.size(); i++) {
            if (detail::javaEquals(o, static_cast<const T&>(d_[i]))) {
                d_.erase(d_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }
    virtual bool removeLastOccurrence(const T& o) {
        auto g = this->lock();
        for (std::size_t i = d_.size(); i-- > 0;) {
            if (detail::javaEquals(o, static_cast<const T&>(d_[i]))) {
                d_.erase(d_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    Deque<T>* clone() override {
        auto* d = new Deque<T>();
        for (const auto& e : this->_snapshot()) d->d_.push_back(e);
        return d;
    }
    Deque<T>* shared() {
        this->shared_ = true;
        return this;
    }
    Deque<T>* synchronized_() { return shared(); }

    // Range-for (index based; snapshot when shared).
    detail::DequeRange<T> begin() {
        if (this->shared_) {
            auto g = this->lock();
            return detail::DequeRange<T>(new Storage(d_));
        }
        return detail::DequeRange<T>(&d_);
    }
    detail::IterEnd end() { return {}; }
    detail::DequeRange<T> begin() const { return const_cast<Deque*>(this)->begin(); }
    detail::IterEnd end() const { return {}; }
    // The backing std::deque (no locking).
    Storage& deque() noexcept { return d_; }

    detail::Vec<T> _snapshot() override {
        auto g = this->lock();
        return detail::Vec<T>(d_.begin(), d_.end());
    }
    void _removeIterated(const T& e, int32_t hint) override {
        auto g = this->lock();
        if (hint >= 0 && static_cast<std::size_t>(hint) < d_.size() &&
            detail::javaEquals(static_cast<const T&>(d_[static_cast<std::size_t>(hint)]), e)) {
            d_.erase(d_.begin() + hint);
        } else {
            removeFirstOccurrence(e);
        }
    }
    // jlang internal (live iterators).
    T _at(int32_t i) {
        auto g = this->lock();
        if (i < 0 || static_cast<std::size_t>(i) >= d_.size()) detail::throwNoSuchElement();
        return d_[static_cast<std::size_t>(i)];
    }
    void _eraseAt(int32_t i) {
        auto g = this->lock();
        if (i >= 0 && static_cast<std::size_t>(i) < d_.size()) d_.erase(d_.begin() + i);
    }

protected:
    Storage d_;
};

namespace detail {
// Live, index-based iterator of an unshared Deque.
template<class T>
class DequeItr final : public virtual Iterator<T> {
public:
    explicit DequeItr(Deque<T>* d) : d_(d) {}
    bool hasNext() override { return cursor_ < d_->size(); }
    T next() override {
        T e = d_->_at(cursor_);
        lastRet_ = cursor_++;
        return e;
    }
    void remove() override {
        if (lastRet_ < 0) throwIllegalState();
        d_->_eraseAt(lastRet_);
        cursor_ = lastRet_;
        lastRet_ = -1;
    }

private:
    Deque<T>* d_;
    int32_t cursor_ = 0;
    int32_t lastRet_ = -1;
};
}  // namespace detail

template<class T>
Iterator<T>* Deque<T>::iterator() {
    if (this->shared_) return new detail::SnapshotIterator<T>(this, this->_snapshot());
    return new detail::DequeItr<T>(this);
}

// ---------------------------------------------------------------------------------------
// java.util.concurrent.ConcurrentLinkedQueue: a jlang::Deque with its lock enabled; null
// elements are rejected (NullPointerException), iteration is over snapshots.
template<class T>
class ConcurrentLinkedQueue : public Deque<T> {
public:
    ConcurrentLinkedQueue() { this->shared_ = true; }
    explicit ConcurrentLinkedQueue(Collection<T>* c) : Deque<T>(c) {
        for (const auto& e : this->d_) {
            if (detail::isNullValue(e)) detail::throwNullPointer();
        }
        this->shared_ = true;
    }
    void addFirst(const T& e) override {
        if (detail::isNullValue(e)) detail::throwNullPointer();
        Deque<T>::addFirst(e);
    }
    void addLast(const T& e) override {
        if (detail::isNullValue(e)) detail::throwNullPointer();
        Deque<T>::addLast(e);
    }
    ConcurrentLinkedQueue<T>* clone() override {
        auto* q = new ConcurrentLinkedQueue<T>();
        for (const auto& e : this->_snapshot()) q->d_.push_back(e);
        return q;
    }
};

// ---------------------------------------------------------------------------------------
// java.util.PriorityQueue<E>: Java's binary heap (same sift algorithms, so poll order and
// iteration/toArray order match Java). Ordered by natural ordering or a Comparator; null
// elements throw NullPointerException. Iteration is over a snapshot in array order.
template<class T>
class PriorityQueue : public virtual Collection<T> {
public:
    using Collection<T>::addAll;
    using Collection<T>::contains;
    using Collection<T>::removeObject;

    PriorityQueue() {}
    explicit PriorityQueue(int32_t initialCapacity) {
        if (initialCapacity < 1) detail::throwIllegalArgumentNoMsg();
        q_.reserve(static_cast<std::size_t>(initialCapacity));
    }
    PriorityQueue(int32_t initialCapacity, Comparator<T>* comparator) : PriorityQueue(initialCapacity) {
        cmp_ = comparator;
    }
    explicit PriorityQueue(Comparator<T>* comparator) : cmp_(comparator) {}
    // new PriorityQueue(collection): a PriorityQueue/TreeSet source keeps its comparator.
    explicit PriorityQueue(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        if (auto* pq = dynamic_cast<PriorityQueue<T>*>(c)) cmp_ = pq->comparator();
        for (const auto& e : c->_snapshot()) {
            if (detail::isNullValue(e)) detail::throwNullPointer();
            q_.push_back(e);
        }
        heapify();
    }

    Comparator<T>* comparator() { return cmp_; }

    int32_t size() override {
        auto g = this->lock();
        return static_cast<int32_t>(q_.size());
    }
    bool isEmpty() override { return size() == 0; }
    bool contains(const T& o) override {
        auto g = this->lock();
        return indexOf(o) >= 0;
    }
    bool add(const T& e) override { return offer(e); }
    virtual bool offer(const T& e) {
        if (detail::isNullValue(e)) detail::throwNullPointer();
        auto g = this->lock();
        const std::size_t i = q_.size();
        q_.push_back(e);
        if (i != 0) siftUp(i, e);
        return true;
    }
    virtual T peek() {
        auto g = this->lock();
        return q_.empty() ? T{} : T(q_[0]);
    }
    virtual T element() {
        auto g = this->lock();
        if (q_.empty()) detail::throwNoSuchElement();
        return q_[0];
    }
    virtual T poll() {
        auto g = this->lock();
        if (q_.empty()) return T{};
        T result = q_[0];
        const std::size_t s = q_.size() - 1;
        T x = q_[s];
        q_.pop_back();
        if (s != 0) siftDown(0, x);
        return result;
    }
    // Queue.remove(): the head (NoSuchElementException when empty).
    virtual T remove() {
        auto g = this->lock();
        if (q_.empty()) detail::throwNoSuchElement();
        return poll();
    }
    using Collection<T>::remove;
    bool removeObject(const T& o) override {
        auto g = this->lock();
        const int32_t i = indexOf(o);
        if (i < 0) return false;
        removeAt(static_cast<std::size_t>(i));
        return true;
    }
    void clear() override {
        auto g = this->lock();
        q_.clear();
    }
    Iterator<T>* iterator() override { return new detail::SnapshotIterator<T>(this, this->_snapshot()); }

    PriorityQueue<T>* clone() override {
        auto* p = new PriorityQueue<T>(cmp_);
        p->q_ = this->_snapshot();
        return p;
    }
    PriorityQueue<T>* shared() {
        this->shared_ = true;
        return this;
    }
    PriorityQueue<T>* synchronized_() { return shared(); }

    detail::Vec<T> _snapshot() override {
        auto g = this->lock();
        return q_;
    }
    void _removeIterated(const T& e, int32_t hint) override {
        (void)hint;
        auto g = this->lock();
        for (std::size_t i = 0; i < q_.size(); i++) {  // removeEq: identity first
            if (detail::sameElement(q_[i], e)) {
                removeAt(i);
                return;
            }
        }
        removeObject(e);
    }

protected:
    int32_t compare(const T& a, const T& b) {
        if (cmp_ != nullptr) return cmp_->compare(a, b);
        return detail::compareNatural(a, b);
    }
    int32_t indexOf(const T& o) {
        if (detail::isNullValue(o)) return -1;
        for (std::size_t i = 0; i < q_.size(); i++) {
            if (detail::javaEquals(o, static_cast<const T&>(q_[i]))) return static_cast<int32_t>(i);
        }
        return -1;
    }
    void removeAt(std::size_t i) {
        const std::size_t s = q_.size() - 1;
        if (s == i) {
            q_.pop_back();
        } else {
            T moved = q_[s];
            q_.pop_back();
            siftDown(i, moved);
            if (detail::sameElement(q_[i], moved)) siftUp(i, moved);
        }
    }
    void siftUp(std::size_t k, T x) {
        while (k > 0) {
            const std::size_t parent = (k - 1) >> 1;
            T e = q_[parent];
            if (compare(x, e) >= 0) break;
            q_[k] = e;
            k = parent;
        }
        q_[k] = x;
    }
    void siftDown(std::size_t k, T x) {
        const std::size_t size = q_.size();
        const std::size_t half = size >> 1;
        while (k < half) {
            std::size_t child = (k << 1) + 1;
            T c = q_[child];
            const std::size_t right = child + 1;
            if (right < size && compare(c, q_[right]) > 0) c = q_[child = right];
            if (compare(x, c) <= 0) break;
            q_[k] = c;
            k = child;
        }
        q_[k] = x;
    }
    void heapify() {
        for (std::size_t i = q_.size() >> 1; i-- > 0;) {
            T x = q_[i];
            siftDown(i, x);
        }
    }

    detail::Vec<T> q_;
    Comparator<T>* cmp_ = nullptr;
};

}  // namespace jlang
