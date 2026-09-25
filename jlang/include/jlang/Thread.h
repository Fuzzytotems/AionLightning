// jlang/Thread.h - java.lang.Thread / ThreadGroup / Thread.UncaughtExceptionHandler and
// java.util.concurrent (TimeUnit, atomics, locks and conditions, CountDownLatch, blocking
// queues, Callable/Future/FutureTask, executors and thread pools, Executors) plus
// java.util.Timer / TimerTask.
//
// Everything has Java's names and semantics (CONVENTIONS §2, §9, §12):
//
//   * Every jlang::Thread runs on a native thread registered with the garbage collector for
//     its whole life; executors and Timer run their work on jlang::Threads.
//   * Thread::interrupt() wakes Thread.sleep, Object.wait, Thread.join, Condition.await,
//     BlockingQueue.take/put/poll(timeout)/offer(timeout), CountDownLatch.await,
//     Future.get and lock acquisitions made with lockInterruptibly/tryLock(timeout), which
//     throw jlang::InterruptedException and clear the interrupt status, as in Java.
//   * An exception escaping Thread.run() goes to the thread's UncaughtExceptionHandler, else
//     its ThreadGroup, else Thread.getDefaultUncaughtExceptionHandler(), else it is printed
//     as `Exception in thread "name" ...` on System.err.
//   * The JVM stays alive until all non-daemon threads have finished: a C++ main() that
//     emulates this calls jlang::Thread::joinAllNonDaemon() (then jlang::System::exit(0) to
//     run the shutdown hooks).
//   * Pools (ThreadPoolExecutor, ScheduledThreadPoolExecutor) are ports of the JDK 8
//     algorithms: worker creation rules (core threads first, then the queue, then up to the
//     maximum), keep-alive, shutdown/shutdownNow, periodic task rules (fixed rate vs fixed
//     delay, suppression after an exception, cancel/removeOnCancel, shutdown policies).
//
// Generic Java interfaces of the JDK are templates (Callable<V>, BlockingQueue<E>, ...);
// Future, ScheduledFuture and FutureTask are erased (get() returns jlang::Object*).
#pragma once

#include <jlang/jlang.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace jlang {

class Thread;
class ThreadGroup;
class Date;

// java.lang.IllegalThreadStateException (not in the core exception set)
JLANG_DECLARE_EXCEPTION(IllegalThreadStateException, IllegalArgumentException);

// =======================================================================================
// java.util.concurrent.TimeUnit (value-class enum, CONVENTIONS §7)
class TimeUnit final {
public:
    enum class Value : int32_t { _NULL = -1, NANOSECONDS, MICROSECONDS, MILLISECONDS, SECONDS, MINUTES, HOURS, DAYS };
    static const TimeUnit NANOSECONDS, MICROSECONDS, MILLISECONDS, SECONDS, MINUTES, HOURS, DAYS;

    constexpr TimeUnit() noexcept : v_(Value::_NULL) {}
    constexpr TimeUnit(std::nullptr_t) noexcept : v_(Value::_NULL) {}
    constexpr explicit TimeUnit(Value v) noexcept : v_(v) {}
    constexpr operator Value() const noexcept { return v_; }
    constexpr bool operator==(const TimeUnit& o) const noexcept { return v_ == o.v_; }
    constexpr bool operator==(std::nullptr_t) const noexcept { return v_ == Value::_NULL; }

    constexpr int32_t ordinal() const noexcept { return static_cast<int32_t>(v_); }
    String name() const;
    String toString() const { return name(); }
    int32_t compareTo(TimeUnit o) const noexcept { return ordinal() - o.ordinal(); }
    bool equals(TimeUnit o) const noexcept { return v_ == o.v_; }
    int32_t hashCode() const noexcept { return ordinal(); }
    static Array<TimeUnit>* values();
    static TimeUnit valueOf(const String& name);

    // Conversions saturate at Long.MIN_VALUE / Long.MAX_VALUE like Java.
    int64_t convert(int64_t sourceDuration, TimeUnit sourceUnit) const;
    int64_t toNanos(int64_t d) const;
    int64_t toMicros(int64_t d) const;
    int64_t toMillis(int64_t d) const;
    int64_t toSeconds(int64_t d) const;
    int64_t toMinutes(int64_t d) const;
    int64_t toHours(int64_t d) const;
    int64_t toDays(int64_t d) const;
    // Thread.sleep / Thread.join / Object.wait with this unit (no-ops for timeout <= 0).
    void sleep(int64_t timeout) const;
    void timedJoin(Thread* thread, int64_t timeout) const;
    void timedWait(Object* obj, int64_t timeout) const;

private:
    Value v_;
};
inline constexpr TimeUnit TimeUnit::NANOSECONDS{TimeUnit::Value::NANOSECONDS};
inline constexpr TimeUnit TimeUnit::MICROSECONDS{TimeUnit::Value::MICROSECONDS};
inline constexpr TimeUnit TimeUnit::MILLISECONDS{TimeUnit::Value::MILLISECONDS};
inline constexpr TimeUnit TimeUnit::SECONDS{TimeUnit::Value::SECONDS};
inline constexpr TimeUnit TimeUnit::MINUTES{TimeUnit::Value::MINUTES};
inline constexpr TimeUnit TimeUnit::HOURS{TimeUnit::Value::HOURS};
inline constexpr TimeUnit TimeUnit::DAYS{TimeUnit::Value::DAYS};

// =======================================================================================
// Blocking support shared by the primitives below (jlang internal).
namespace detail {

// A mutex with two condition variables; wake() is the jlang::sync wake function that makes
// interrupt() reach a thread blocked on either condition.
class SyncCore {
public:
    std::mutex mu;
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    static void wake(void* self);
};

// Clears the current thread's interrupt status; throws InterruptedException if it was set.
void throwIfInterrupted();
[[noreturn]] void throwInterrupted();
// Calls sync::clearBlocker() on scope exit.
struct BlockerScope {
    BlockerScope(sync::WakeFn fn, void* arg) { sync::setBlocker(fn, arg); }
    ~BlockerScope() { sync::clearBlocker(); }
    BlockerScope(const BlockerScope&) = delete;
    BlockerScope& operator=(const BlockerScope&) = delete;
};

// Waits on `cv` (lk holds its mutex) until pred() is true (returns true), the timeout passes
// (timed only; returns false) or the thread is interrupted (clears the status and throws
// InterruptedException). nanos is the remaining time for a timed wait.
template<class Pred>
bool awaitInterruptibly(std::unique_lock<std::mutex>& lk, std::condition_variable& cv, sync::WakeFn wakeFn,
                        void* wakeArg, bool timed, int64_t nanos, Pred pred) {
    if (pred()) return true;
    if (timed && nanos <= 0) return false;
    sync::InterruptState* st = sync::current();
    BlockerScope blocker(wakeFn, wakeArg);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::nanoseconds(nanos > INT64_C(0x3FFFFFFFFFFFFFFF) ? INT64_C(0x3FFFFFFFFFFFFFFF) : nanos);
    while (!pred()) {
        if (sync::isInterrupted(st)) {
            sync::clearInterrupt(st);
            throwInterrupted();
        }
        if (timed) {
            if (cv.wait_until(lk, deadline) == std::cv_status::timeout) return pred();
        } else {
            cv.wait(lk);
        }
    }
    return true;
}

// Waits until pred() or the timeout, ignoring interrupts (the status is kept).
template<class Pred>
bool awaitUninterruptibly(std::unique_lock<std::mutex>& lk, std::condition_variable& cv, bool timed, int64_t nanos,
                          Pred pred) {
    if (!timed) {
        while (!pred()) cv.wait(lk);
        return true;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(nanos);
    while (!pred()) {
        if (cv.wait_until(lk, deadline) == std::cv_status::timeout) return pred();
    }
    return true;
}

// An identity of the calling native thread (for lock ownership).
uintptr_t currentThreadToken() noexcept;

}  // namespace detail

// =======================================================================================
// java.lang.Thread.UncaughtExceptionHandler
class Thread_UncaughtExceptionHandler : public virtual Object {
public:
    virtual void uncaughtException(Thread* t, Throwable* e) = 0;
    template<class F> static Thread_UncaughtExceptionHandler* of(F f);
};

namespace detail {
template<class F>
class UncaughtHandlerLambda final : public virtual Thread_UncaughtExceptionHandler {
public:
    explicit UncaughtHandlerLambda(F f) : f_(std::move(f)) {}
    void uncaughtException(Thread* t, Throwable* e) override { f_(t, e); }

private:
    F f_;
};
}  // namespace detail
template<class F>
Thread_UncaughtExceptionHandler* Thread_UncaughtExceptionHandler::of(F f) {
    return new detail::UncaughtHandlerLambda<F>(std::move(f));
}

// =======================================================================================
// java.lang.ThreadGroup (name, parent, max priority, daemon flag, member threads).
class ThreadGroup : public virtual Thread_UncaughtExceptionHandler {
public:
    explicit ThreadGroup(const String& name);  // child of the current thread's group
    ThreadGroup(ThreadGroup* parent, const String& name);

    String getName() { return name_; }
    ThreadGroup* getParent() { return parent_; }
    int32_t getMaxPriority();
    void setMaxPriority(int32_t pri);
    bool isDaemon();
    void setDaemon(bool daemon);
    bool isDestroyed() { return false; }
    // Live threads in this group and its subgroups.
    int32_t activeCount();
    int32_t activeGroupCount();
    int32_t enumerate(Array<Thread*>* list);
    bool parentOf(ThreadGroup* g);
    void interrupt();
    // Java's ThreadGroup.uncaughtException: parent, else the default handler, else print.
    void uncaughtException(Thread* t, Throwable* e) override;
    String toString() override;  // java.lang.ThreadGroup[name=main,maxpri=10]

    // jlang: the "system" and "main" groups
    static ThreadGroup* systemGroup();
    static ThreadGroup* mainGroup();

private:
    friend class Thread;
    friend struct ThreadInternals;
    struct RootTag {};
    ThreadGroup(RootTag, ThreadGroup* parent, const String& name);
    void add(Thread* t);
    void remove(Thread* t);
    void collect(std::vector<Thread*>& out, bool recurse);

    String name_;
    ThreadGroup* parent_ = nullptr;
    std::mutex mu_;
    int32_t maxPriority_ = 10;
    bool daemon_ = false;
    std::vector<Thread*> threads_;
    std::vector<ThreadGroup*> groups_;
};

// =======================================================================================
// java.lang.Thread
class Thread : public virtual Runnable {
public:
    using UncaughtExceptionHandler = Thread_UncaughtExceptionHandler;

    static constexpr int32_t MIN_PRIORITY = 1;
    static constexpr int32_t NORM_PRIORITY = 5;
    static constexpr int32_t MAX_PRIORITY = 10;

    // The new thread inherits the creating thread's group (unless given), daemon status and
    // priority, like Java. Unnamed threads are called "Thread-<n>".
    Thread();
    explicit Thread(Runnable* target);
    explicit Thread(const String& name);
    Thread(Runnable* target, const String& name);
    Thread(ThreadGroup* group, Runnable* target);
    Thread(ThreadGroup* group, const String& name);
    Thread(ThreadGroup* group, Runnable* target, const String& name);
    Thread(ThreadGroup* group, Runnable* target, const String& name, int64_t stackSize);

    // Calls the target's run() (override in subclasses).
    void run() override;
    // IllegalThreadStateException if already started.
    virtual void start();

    // join waits on this thread's monitor like Java (a terminating thread calls notifyAll()).
    void join();
    void join(int64_t millis);
    void join(int64_t millis, int32_t nanos);

    virtual void interrupt();
    virtual bool isInterrupted();
    static bool interrupted();  // tests and clears the current thread's status

    bool isAlive();
    void setDaemon(bool on);  // IllegalThreadStateException if alive
    bool isDaemon() { return daemon_; }
    void setName(const String& name);  // also sets the native thread name (first 15 bytes)
    String getName();
    // Stored only (the OS scheduler is not asked); capped by the group's max priority.
    void setPriority(int32_t newPriority);
    int32_t getPriority() { return priority_; }
    int64_t getId() { return tid_; }
    ThreadGroup* getThreadGroup() { return group_; }
    Array<StackTraceElement*>* getStackTrace();  // empty

    void setUncaughtExceptionHandler(UncaughtExceptionHandler* eh) { ueh_ = eh; }
    UncaughtExceptionHandler* getUncaughtExceptionHandler();
    static void setDefaultUncaughtExceptionHandler(UncaughtExceptionHandler* eh);
    static UncaughtExceptionHandler* getDefaultUncaughtExceptionHandler();

    // Works on every thread: jlang threads, the main thread ("main", id 1) and threads not
    // started by jlang (a wrapper named "Thread-<n>" is created on first use).
    static Thread* currentThread();
    static void sleep(int64_t millis);
    static void sleep(int64_t millis, int32_t nanos);
    static void yield();
    static bool holdsLock(Object* obj);
    static int32_t activeCount();
    static int32_t enumerate(Array<Thread*>* tarray);
    static void dumpStack();

    String toString() override;  // Thread[name,priority,group]

    // ---- jlang extensions
    // Blocks until every started non-daemon jlang::Thread (other than the caller) has
    // finished: what the JVM does after main() returns.
    static void joinAllNonDaemon();
    // Hands e to the uncaught exception handler (what the JVM does when run() throws).
    // A C++ main() can use it for exceptions escaping the Java main method.
    void dispatchUncaughtException(Throwable* e);
    // The thread's interrupt state (jlang::sync).
    sync::InterruptState* interruptState() { return interruptState_; }

private:
    friend class ThreadGroup;
    friend struct ThreadInternals;
    struct AttachTag {};
    Thread(AttachTag, bool isMainThread);
    void init(ThreadGroup* g, Runnable* target, const String& name, int64_t stackSize);
    void exitThread();
    static void entry(void* self);

    Runnable* target_ = nullptr;
    ThreadGroup* group_ = nullptr;
    std::mutex nameMu_;
    String name_;
    int32_t priority_ = NORM_PRIORITY;
    bool daemon_ = false;
    bool attached_ = false;
    int64_t tid_ = 0;
    int64_t stackSize_ = 0;
    UncaughtExceptionHandler* ueh_ = nullptr;
    sync::InterruptState* interruptState_ = nullptr;
    std::atomic<int32_t> state_{0};  // 0 new, 1 alive, 2 terminated
};

// =======================================================================================
// java.util.concurrent.atomic
class AtomicInteger : public Number {
public:
    AtomicInteger() : v_(0) {}
    explicit AtomicInteger(int32_t initialValue) : v_(initialValue) {}
    int32_t get() const { return v_.load(); }
    void set(int32_t newValue) { v_.store(newValue); }
    void lazySet(int32_t newValue) { v_.store(newValue, std::memory_order_release); }
    int32_t getAndSet(int32_t newValue) { return v_.exchange(newValue); }
    bool compareAndSet(int32_t expect, int32_t update) { return v_.compare_exchange_strong(expect, update); }
    bool weakCompareAndSet(int32_t expect, int32_t update) { return compareAndSet(expect, update); }
    int32_t getAndIncrement() { return getAndAdd(1); }
    int32_t getAndDecrement() { return getAndAdd(-1); }
    int32_t getAndAdd(int32_t delta) {
        return static_cast<int32_t>(static_cast<uint32_t>(v_.fetch_add(delta)));
    }
    int32_t incrementAndGet() { return addAndGet(1); }
    int32_t decrementAndGet() { return addAndGet(-1); }
    int32_t addAndGet(int32_t delta) { return static_cast<int32_t>(static_cast<uint32_t>(getAndAdd(delta)) + static_cast<uint32_t>(delta)); }
    template<class F> int32_t getAndUpdate(F f) {
        int32_t prev = get();
        while (!compareAndSet(prev, static_cast<int32_t>(f(prev)))) prev = get();
        return prev;
    }
    template<class F> int32_t updateAndGet(F f) {
        int32_t prev = get(), next;
        while (!compareAndSet(prev, next = static_cast<int32_t>(f(prev)))) prev = get();
        return next;
    }
    int32_t intValue() override { return get(); }
    int64_t longValue() override { return get(); }
    float floatValue() override { return static_cast<float>(get()); }
    double doubleValue() override { return get(); }
    String toString() override { return String::valueOf(get()); }

private:
    std::atomic<int32_t> v_;
};

class AtomicLong : public Number {
public:
    AtomicLong() : v_(0) {}
    explicit AtomicLong(int64_t initialValue) : v_(initialValue) {}
    int64_t get() const { return v_.load(); }
    void set(int64_t newValue) { v_.store(newValue); }
    void lazySet(int64_t newValue) { v_.store(newValue, std::memory_order_release); }
    int64_t getAndSet(int64_t newValue) { return v_.exchange(newValue); }
    bool compareAndSet(int64_t expect, int64_t update) { return v_.compare_exchange_strong(expect, update); }
    bool weakCompareAndSet(int64_t expect, int64_t update) { return compareAndSet(expect, update); }
    int64_t getAndIncrement() { return getAndAdd(1); }
    int64_t getAndDecrement() { return getAndAdd(-1); }
    int64_t getAndAdd(int64_t delta) { return v_.fetch_add(delta); }
    int64_t incrementAndGet() { return addAndGet(1); }
    int64_t decrementAndGet() { return addAndGet(-1); }
    int64_t addAndGet(int64_t delta) { return static_cast<int64_t>(static_cast<uint64_t>(v_.fetch_add(delta)) + static_cast<uint64_t>(delta)); }
    template<class F> int64_t getAndUpdate(F f) {
        int64_t prev = get();
        while (!compareAndSet(prev, static_cast<int64_t>(f(prev)))) prev = get();
        return prev;
    }
    template<class F> int64_t updateAndGet(F f) {
        int64_t prev = get(), next;
        while (!compareAndSet(prev, next = static_cast<int64_t>(f(prev)))) prev = get();
        return next;
    }
    int32_t intValue() override { return static_cast<int32_t>(get()); }
    int64_t longValue() override { return get(); }
    float floatValue() override { return static_cast<float>(get()); }
    double doubleValue() override { return static_cast<double>(get()); }
    String toString() override { return String::valueOf(get()); }

private:
    std::atomic<int64_t> v_;
};

class AtomicBoolean : public virtual Object {
public:
    AtomicBoolean() : v_(false) {}
    explicit AtomicBoolean(bool initialValue) : v_(initialValue) {}
    bool get() const { return v_.load(); }
    void set(bool newValue) { v_.store(newValue); }
    void lazySet(bool newValue) { v_.store(newValue, std::memory_order_release); }
    bool getAndSet(bool newValue) { return v_.exchange(newValue); }
    bool compareAndSet(bool expect, bool update) { return v_.compare_exchange_strong(expect, update); }
    bool weakCompareAndSet(bool expect, bool update) { return compareAndSet(expect, update); }
    String toString() override { return String(get() ? "true" : "false"); }

private:
    std::atomic<bool> v_;
};

// AtomicReference<T>: lock-free for pointers and arithmetic types; compareAndSet compares
// pointers by identity (Java ==) and value types (String, enums) by value.
template<class T>
class AtomicReference : public virtual Object {
    static constexpr bool kLockFree = std::is_pointer_v<T> || std::is_arithmetic_v<T>;

public:
    AtomicReference() : v_() {}
    explicit AtomicReference(T initialValue) : v_(initialValue) {}
    T get() {
        if constexpr (kLockFree) {
            return v_.load();
        } else {
            std::lock_guard<std::mutex> g(mu_);
            return v_;
        }
    }
    void set(T newValue) {
        if constexpr (kLockFree) {
            v_.store(newValue);
        } else {
            std::lock_guard<std::mutex> g(mu_);
            v_ = newValue;
        }
    }
    void lazySet(T newValue) { set(newValue); }
    T getAndSet(T newValue) {
        if constexpr (kLockFree) {
            return v_.exchange(newValue);
        } else {
            std::lock_guard<std::mutex> g(mu_);
            T old = v_;
            v_ = newValue;
            return old;
        }
    }
    bool compareAndSet(T expect, T update) {
        if constexpr (kLockFree) {
            return v_.compare_exchange_strong(expect, update);
        } else {
            std::lock_guard<std::mutex> g(mu_);
            if (!(v_ == expect)) return false;
            v_ = update;
            return true;
        }
    }
    bool weakCompareAndSet(T expect, T update) { return compareAndSet(expect, update); }
    template<class F> T getAndUpdate(F f) {
        for (;;) {
            T prev = get();
            if (compareAndSet(prev, f(prev))) return prev;
        }
    }
    template<class F> T updateAndGet(F f) {
        for (;;) {
            T prev = get();
            T next = f(prev);
            if (compareAndSet(prev, next)) return next;
        }
    }
    String toString() override { return detail::elemToString(get()); }

private:
    std::conditional_t<kLockFree, std::atomic<T>, T> v_;
    [[no_unique_address]] std::conditional_t<kLockFree, detail::IterEnd, std::mutex> mu_;
};

// =======================================================================================
// java.util.concurrent.locks
class Condition : public virtual Object {
public:
    virtual void await() = 0;
    virtual bool await(int64_t time, TimeUnit unit) = 0;
    virtual int64_t awaitNanos(int64_t nanosTimeout) = 0;
    virtual void awaitUninterruptibly() = 0;
    virtual bool awaitUntil(Date* deadline) = 0;
    virtual void signal() = 0;
    virtual void signalAll() = 0;
};

class Lock : public virtual Object {
public:
    virtual void lock() = 0;
    virtual void lockInterruptibly() = 0;
    virtual bool tryLock() = 0;
    virtual bool tryLock(int64_t time, TimeUnit unit) = 0;
    virtual void unlock() = 0;
    virtual Condition* newCondition() = 0;
};

namespace detail {
// A lock that Condition objects can release and reacquire (ReentrantLock, write lock).
class OwnedLock : public virtual Object {
public:
    std::mutex mu;  // guards the lock state; conditions wait on it
    // All called with `mu` held:
    virtual bool heldByCurrentLocked() = 0;
    virtual int32_t releaseAllLocked() = 0;  // returns the saved hold count
    virtual void reacquireLocked(std::unique_lock<std::mutex>& lk, int32_t holds) = 0;
    static void wake(void* self);  // interrupt wake-up: notifies every condition of the lock
    void registerCondition(std::condition_variable* cv);
    std::vector<std::condition_variable*> conditions_;
};
class ConditionObject;
}  // namespace detail

// java.util.concurrent.locks.ReentrantLock (the fairness flag is accepted and ignored).
class ReentrantLock : public virtual Lock, public detail::OwnedLock {
public:
    ReentrantLock() { conditions_.push_back(&cv_); }
    explicit ReentrantLock(bool fair) : fair_(fair) { conditions_.push_back(&cv_); }
    void lock() override;
    void lockInterruptibly() override;
    bool tryLock() override;
    bool tryLock(int64_t timeout, TimeUnit unit) override;
    void unlock() override;  // IllegalMonitorStateException if not held by the caller
    Condition* newCondition() override;
    int32_t getHoldCount();
    bool isHeldByCurrentThread();
    bool isLocked();
    bool isFair() { return fair_; }
    bool hasQueuedThreads();
    int32_t getQueueLength();
    bool hasWaiters(Condition* condition);
    int32_t getWaitQueueLength(Condition* condition);
    String toString() override;  // ...ReentrantLock@hash[Unlocked] / [Locked by thread name]

    bool heldByCurrentLocked() override;
    int32_t releaseAllLocked() override;
    void reacquireLocked(std::unique_lock<std::mutex>& lk, int32_t holds) override;

private:
    std::condition_variable cv_;
    uintptr_t owner_ = 0;
    Thread* ownerThread_ = nullptr;
    int32_t holds_ = 0;
    int32_t waiting_ = 0;
    bool fair_ = false;
};

namespace detail {
class RWSync;
}

// java.util.concurrent.locks.ReentrantReadWriteLock: reentrant read and write locks; a
// writer may also take the read lock (downgrading); waiting writers block new readers.
class ReentrantReadWriteLock : public virtual Object {
public:
    ReentrantReadWriteLock();
    explicit ReentrantReadWriteLock(bool fair);
    Lock* readLock() { return readLock_; }
    Lock* writeLock() { return writeLock_; }
    int32_t getReadLockCount();
    bool isWriteLocked();
    bool isWriteLockedByCurrentThread();
    int32_t getWriteHoldCount();
    int32_t getReadHoldCount();
    bool isFair() { return fair_; }
    bool hasQueuedThreads();
    int32_t getQueueLength();
    String toString() override;  // ...[Write locks = w, Read locks = r]

private:
    detail::RWSync* sync_;
    Lock* readLock_;
    Lock* writeLock_;
    bool fair_ = false;
};

// java.util.concurrent.CountDownLatch
class CountDownLatch : public virtual Object {
public:
    explicit CountDownLatch(int32_t count);  // IllegalArgumentException if count < 0
    void await();
    bool await(int64_t timeout, TimeUnit unit);
    void countDown();
    int64_t getCount();
    String toString() override;  // ...CountDownLatch@hash[Count = n]

private:
    detail::SyncCore core_;
    int64_t count_;
};

// =======================================================================================
// java.util.concurrent.BlockingQueue<E> and implementations. Null elements are rejected
// with NullPointerException (for pointer/value types with a null state). Iterators work on
// snapshots; their remove() removes the returned element from the queue.
template<class T>
class BlockingQueue : public virtual Collection<T> {
public:
    using Collection<T>::remove;
    // java.util.Queue
    virtual bool offer(const T& e) = 0;
    virtual T poll() = 0;
    virtual T peek() = 0;
    T element() {
        T x = peek();
        if (detail::isNullValue(x)) detail::throwNoSuchElement();
        return x;
    }
    // Queue.remove(): the head, NoSuchElementException when empty.
    T remove() {
        T x = poll();
        if (detail::isNullValue(x)) detail::throwNoSuchElement();
        return x;
    }
    // Queue.add: IllegalStateException("Queue full") when offer fails.
    bool add(const T& e) override {
        if (offer(e)) return true;
        throw IllegalStateException(String("Queue full"));
    }
    // java.util.concurrent.BlockingQueue
    virtual void put(const T& e) = 0;
    virtual bool offer(const T& e, int64_t timeout, TimeUnit unit) = 0;
    virtual T take() = 0;
    virtual T poll(int64_t timeout, TimeUnit unit) = 0;
    virtual int32_t remainingCapacity() = 0;
    int32_t drainTo(Collection<T>* c) { return drainTo(c, INT32_MAX); }
    virtual int32_t drainTo(Collection<T>* c, int32_t maxElements) = 0;

protected:
    static void checkNotNull(const T& e) {
        if (detail::isNullValue(e)) detail::throwNullPointer();
    }
    void checkDrainTarget(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        if (static_cast<Object*>(c) == static_cast<Object*>(this)) detail::throwIllegalArgument("");
    }
};

namespace detail {

// LinkedBlockingQueue / ArrayBlockingQueue: a bounded FIFO with notEmpty/notFull conditions.
template<class T>
class FifoBlockingQueue : public virtual BlockingQueue<T> {
public:
    using BlockingQueue<T>::remove;
    using BlockingQueue<T>::drainTo;
    using BlockingQueue<T>::offer;
    using BlockingQueue<T>::poll;

    int32_t size() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return static_cast<int32_t>(q_.size());
    }
    bool isEmpty() override { return size() == 0; }
    int32_t remainingCapacity() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return capacity_ - static_cast<int32_t>(q_.size());
    }
    bool offer(const T& e) override {
        this->checkNotNull(e);
        std::lock_guard<std::mutex> g(core_.mu);
        if (static_cast<int32_t>(q_.size()) >= capacity_) return false;
        q_.push_back(e);
        core_.notEmpty.notify_one();
        return true;
    }
    void put(const T& e) override {
        this->checkNotNull(e);
        throwIfInterrupted();
        std::unique_lock<std::mutex> lk(core_.mu);
        awaitInterruptibly(lk, core_.notFull, &SyncCore::wake, &core_, false, 0,
                           [&] { return static_cast<int32_t>(q_.size()) < capacity_; });
        q_.push_back(e);
        core_.notEmpty.notify_one();
    }
    bool offer(const T& e, int64_t timeout, TimeUnit unit) override {
        this->checkNotNull(e);
        throwIfInterrupted();
        std::unique_lock<std::mutex> lk(core_.mu);
        if (!awaitInterruptibly(lk, core_.notFull, &SyncCore::wake, &core_, true, unit.toNanos(timeout),
                                [&] { return static_cast<int32_t>(q_.size()) < capacity_; }))
            return false;
        q_.push_back(e);
        core_.notEmpty.notify_one();
        return true;
    }
    T poll() override {
        std::lock_guard<std::mutex> g(core_.mu);
        if (q_.empty()) return T{};
        return dequeueLocked();
    }
    T take() override {
        throwIfInterrupted();
        std::unique_lock<std::mutex> lk(core_.mu);
        awaitInterruptibly(lk, core_.notEmpty, &SyncCore::wake, &core_, false, 0, [&] { return !q_.empty(); });
        return dequeueLocked();
    }
    T poll(int64_t timeout, TimeUnit unit) override {
        throwIfInterrupted();
        std::unique_lock<std::mutex> lk(core_.mu);
        if (!awaitInterruptibly(lk, core_.notEmpty, &SyncCore::wake, &core_, true, unit.toNanos(timeout),
                                [&] { return !q_.empty(); }))
            return T{};
        return dequeueLocked();
    }
    T peek() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return q_.empty() ? T{} : T(q_.front());
    }
    bool contains(const T& o) override {
        if (isNullValue(o)) return false;
        std::lock_guard<std::mutex> g(core_.mu);
        for (const auto& e : q_)
            if (javaEquals(o, static_cast<const T&>(e))) return true;
        return false;
    }
    bool removeObject(const T& o) override {
        if (isNullValue(o)) return false;
        std::lock_guard<std::mutex> g(core_.mu);
        for (auto it = q_.begin(); it != q_.end(); ++it) {
            if (javaEquals(o, static_cast<const T&>(*it))) {
                q_.erase(it);
                core_.notFull.notify_all();
                return true;
            }
        }
        return false;
    }
    void clear() override {
        std::lock_guard<std::mutex> g(core_.mu);
        q_.clear();
        core_.notFull.notify_all();
    }
    int32_t drainTo(Collection<T>* c, int32_t maxElements) override {
        this->checkDrainTarget(c);
        if (maxElements <= 0) return 0;
        Vec<T> taken;
        {
            std::lock_guard<std::mutex> g(core_.mu);
            while (!q_.empty() && static_cast<int32_t>(taken.size()) < maxElements) {
                taken.push_back(q_.front());
                q_.pop_front();
            }
            core_.notFull.notify_all();
        }
        for (const auto& e : taken) c->add(e);
        return static_cast<int32_t>(taken.size());
    }
    Iterator<T>* iterator() override { return new SnapshotIterator<T>(this, this->_snapshot()); }
    Vec<T> _snapshot() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return Vec<T>(q_.begin(), q_.end());
    }
    void _removeIterated(const T& e, int32_t hint) override {
        (void)hint;
        std::lock_guard<std::mutex> g(core_.mu);
        for (auto it = q_.begin(); it != q_.end(); ++it) {
            if (sameElement(static_cast<const T&>(*it), e)) {
                q_.erase(it);
                core_.notFull.notify_all();
                return;
            }
        }
    }

protected:
    explicit FifoBlockingQueue(int32_t capacity) : capacity_(capacity) {
        if (capacity <= 0) throwIllegalArgument("");
    }
    T dequeueLocked() {
        T x = q_.front();
        q_.pop_front();
        core_.notFull.notify_one();
        return x;
    }
    SyncCore core_;
    std::deque<T, GcAllocator<T>> q_;
    int32_t capacity_;
};

}  // namespace detail

// java.util.concurrent.LinkedBlockingQueue (optionally bounded; default Integer.MAX_VALUE)
template<class T>
class LinkedBlockingQueue : public detail::FifoBlockingQueue<T> {
public:
    LinkedBlockingQueue() : detail::FifoBlockingQueue<T>(INT32_MAX) {}
    explicit LinkedBlockingQueue(int32_t capacity) : detail::FifoBlockingQueue<T>(capacity) {}
    explicit LinkedBlockingQueue(Collection<T>* c) : detail::FifoBlockingQueue<T>(INT32_MAX) {
        if (c == nullptr) detail::throwNullPointer();
        for (const auto& e : c->_snapshot()) this->put(e);
    }
};

// java.util.concurrent.ArrayBlockingQueue (bounded; the fairness flag is ignored)
template<class T>
class ArrayBlockingQueue : public detail::FifoBlockingQueue<T> {
public:
    explicit ArrayBlockingQueue(int32_t capacity) : detail::FifoBlockingQueue<T>(capacity) {}
    ArrayBlockingQueue(int32_t capacity, bool fair) : detail::FifoBlockingQueue<T>(capacity) { (void)fair; }
    ArrayBlockingQueue(int32_t capacity, bool fair, Collection<T>* c) : detail::FifoBlockingQueue<T>(capacity) {
        (void)fair;
        if (c == nullptr) detail::throwNullPointer();
        for (const auto& e : c->_snapshot()) {
            if (!this->offer(e)) detail::throwIllegalArgument("");
        }
    }
};

// java.util.concurrent.SynchronousQueue: a hand-off with no capacity. offer() succeeds only
// if a consumer is waiting in take()/poll(timeout); poll() only if a producer is waiting.
// Non-fair mode (default) serves the most recent waiter first, like Java's TransferStack.
template<class T>
class SynchronousQueue : public virtual BlockingQueue<T> {
public:
    using BlockingQueue<T>::remove;
    using BlockingQueue<T>::drainTo;
    using BlockingQueue<T>::offer;
    using BlockingQueue<T>::poll;

    SynchronousQueue() {}
    explicit SynchronousQueue(bool fair) : fair_(fair) {}

    int32_t size() override { return 0; }
    bool isEmpty() override { return true; }
    int32_t remainingCapacity() override { return 0; }
    bool contains(const T& o) override {
        (void)o;
        return false;
    }
    bool removeObject(const T& o) override {
        (void)o;
        return false;
    }
    void clear() override {}
    T peek() override { return T{}; }
    Iterator<T>* iterator() override { return new detail::SnapshotIterator<T>(this, detail::Vec<T>()); }
    detail::Vec<T> _snapshot() override { return detail::Vec<T>(); }

    bool offer(const T& e) override {
        this->checkNotNull(e);
        std::lock_guard<std::mutex> g(core_.mu);
        return handToConsumerLocked(e);
    }
    void put(const T& e) override {
        this->checkNotNull(e);
        transferIn(e, false, 0);
    }
    bool offer(const T& e, int64_t timeout, TimeUnit unit) override {
        this->checkNotNull(e);
        return transferIn(e, true, unit.toNanos(timeout));
    }
    T poll() override {
        std::lock_guard<std::mutex> g(core_.mu);
        T x{};
        takeFromProducerLocked(x);
        return x;
    }
    T take() override { return transferOut(false, 0); }
    T poll(int64_t timeout, TimeUnit unit) override { return transferOut(true, unit.toNanos(timeout)); }
    int32_t drainTo(Collection<T>* c, int32_t maxElements) override {
        this->checkDrainTarget(c);
        int32_t n = 0;
        while (n < maxElements) {
            T x = poll();
            if (detail::isNullValue(x)) break;
            c->add(x);
            n++;
        }
        return n;
    }
    String toString() override { return String("[]"); }

private:
    struct Node : public virtual Object {
        T item{};
        bool done = false;  // matched
    };
    Node* popWaiter(std::deque<Node*, detail::GcAllocator<Node*>>& q) {
        if (q.empty()) return nullptr;
        Node* n;
        if (fair_) {
            n = q.front();
            q.pop_front();
        } else {
            n = q.back();
            q.pop_back();
        }
        return n;
    }
    bool handToConsumerLocked(const T& e) {
        Node* c = popWaiter(consumers_);
        if (c == nullptr) return false;
        c->item = e;
        c->done = true;
        core_.notEmpty.notify_all();
        return true;
    }
    bool takeFromProducerLocked(T& out) {
        Node* p = popWaiter(producers_);
        if (p == nullptr) return false;
        out = p->item;
        p->item = T{};
        p->done = true;
        core_.notFull.notify_all();
        return true;
    }
    static void eraseNode(std::deque<Node*, detail::GcAllocator<Node*>>& q, Node* n) {
        for (auto it = q.begin(); it != q.end(); ++it)
            if (*it == n) {
                q.erase(it);
                return;
            }
    }
    bool transferIn(const T& e, bool timed, int64_t nanos) {
        detail::throwIfInterrupted();
        std::unique_lock<std::mutex> lk(core_.mu);
        if (handToConsumerLocked(e)) return true;
        if (timed && nanos <= 0) return false;
        Node* n = new Node();
        n->item = e;
        producers_.push_back(n);
        try {
            if (detail::awaitInterruptibly(lk, core_.notFull, &detail::SyncCore::wake, &core_, timed, nanos,
                                           [&] { return n->done; }))
                return true;
        } catch (InterruptedException&) {
            if (n->done) {  // matched meanwhile: the transfer happened; keep the interrupt
                sync::interrupt(sync::current());
                return true;
            }
            eraseNode(producers_, n);
            throw;
        }
        eraseNode(producers_, n);
        return false;
    }
    T transferOut(bool timed, int64_t nanos) {
        detail::throwIfInterrupted();
        std::unique_lock<std::mutex> lk(core_.mu);
        T x{};
        if (takeFromProducerLocked(x)) return x;
        if (timed && nanos <= 0) return T{};
        Node* n = new Node();
        consumers_.push_back(n);
        try {
            if (detail::awaitInterruptibly(lk, core_.notEmpty, &detail::SyncCore::wake, &core_, timed, nanos,
                                           [&] { return n->done; }))
                return n->item;
        } catch (InterruptedException&) {
            if (n->done) {
                sync::interrupt(sync::current());
                return n->item;
            }
            eraseNode(consumers_, n);
            throw;
        }
        eraseNode(consumers_, n);
        return T{};
    }

    detail::SyncCore core_;
    std::deque<Node*, detail::GcAllocator<Node*>> consumers_;
    std::deque<Node*, detail::GcAllocator<Node*>> producers_;
    bool fair_ = false;
};

// java.util.concurrent.PriorityBlockingQueue: an unbounded binary heap (Java's sift
// algorithms, so poll order and iteration order match Java) ordered by natural ordering
// (Comparable::compareTo) or a Comparator. May be subclassed (add/clear are virtual).
template<class T>
class PriorityBlockingQueue : public virtual BlockingQueue<T> {
public:
    using BlockingQueue<T>::remove;
    using BlockingQueue<T>::drainTo;
    using BlockingQueue<T>::offer;
    using BlockingQueue<T>::poll;

    PriorityBlockingQueue() {}
    explicit PriorityBlockingQueue(int32_t initialCapacity) {
        if (initialCapacity < 1) detail::throwIllegalArgument("");
    }
    PriorityBlockingQueue(int32_t initialCapacity, Comparator<T>* comparator) : cmp_(comparator) {
        if (initialCapacity < 1) detail::throwIllegalArgument("");
    }
    explicit PriorityBlockingQueue(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        if (auto* p = dynamic_cast<PriorityBlockingQueue<T>*>(c)) cmp_ = p->comparator();
        for (const auto& e : c->_snapshot()) {
            this->checkNotNull(e);
            q_.push_back(e);
        }
        for (int64_t i = (static_cast<int64_t>(q_.size()) >> 1) - 1; i >= 0; i--) siftDown(static_cast<size_t>(i), q_[i]);
    }

    Comparator<T>* comparator() { return cmp_; }

    bool add(const T& e) override { return offer(e); }
    bool offer(const T& e) override {
        this->checkNotNull(e);
        std::lock_guard<std::mutex> g(core_.mu);
        size_t n = q_.size();
        q_.push_back(e);
        siftUp(n, e);
        core_.notEmpty.notify_one();
        return true;
    }
    void put(const T& e) override { offer(e); }
    bool offer(const T& e, int64_t timeout, TimeUnit unit) override {
        (void)timeout;
        (void)unit;
        return offer(e);
    }
    T poll() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return dequeueLocked();
    }
    T take() override {
        detail::throwIfInterrupted();
        std::unique_lock<std::mutex> lk(core_.mu);
        detail::awaitInterruptibly(lk, core_.notEmpty, &detail::SyncCore::wake, &core_, false, 0,
                                   [&] { return !q_.empty(); });
        return dequeueLocked();
    }
    T poll(int64_t timeout, TimeUnit unit) override {
        detail::throwIfInterrupted();
        std::unique_lock<std::mutex> lk(core_.mu);
        if (!detail::awaitInterruptibly(lk, core_.notEmpty, &detail::SyncCore::wake, &core_, true,
                                        unit.toNanos(timeout), [&] { return !q_.empty(); }))
            return T{};
        return dequeueLocked();
    }
    T peek() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return q_.empty() ? T{} : T(q_[0]);
    }
    int32_t size() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return static_cast<int32_t>(q_.size());
    }
    bool isEmpty() override { return size() == 0; }
    int32_t remainingCapacity() override { return INT32_MAX; }
    bool contains(const T& o) override {
        std::lock_guard<std::mutex> g(core_.mu);
        return indexOfLocked(o) >= 0;
    }
    // remove(Object): the first element equal to o (equals()).
    bool removeObject(const T& o) override {
        std::lock_guard<std::mutex> g(core_.mu);
        int32_t i = indexOfLocked(o);
        if (i < 0) return false;
        removeAtLocked(static_cast<size_t>(i));
        return true;
    }
    void clear() override {
        std::lock_guard<std::mutex> g(core_.mu);
        q_.clear();
    }
    int32_t drainTo(Collection<T>* c, int32_t maxElements) override {
        this->checkDrainTarget(c);
        if (maxElements <= 0) return 0;
        detail::Vec<T> taken;
        {
            std::lock_guard<std::mutex> g(core_.mu);
            while (!q_.empty() && static_cast<int32_t>(taken.size()) < maxElements) taken.push_back(dequeueLocked());
        }
        for (const auto& e : taken) c->add(e);
        return static_cast<int32_t>(taken.size());
    }
    // A snapshot in heap-array order (like Java); remove() removes that element (identity).
    Iterator<T>* iterator() override { return new detail::SnapshotIterator<T>(this, this->_snapshot()); }
    detail::Vec<T> _snapshot() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return q_;
    }
    void _removeIterated(const T& e, int32_t hint) override {
        (void)hint;
        std::lock_guard<std::mutex> g(core_.mu);
        for (size_t i = 0; i < q_.size(); i++) {
            if (detail::sameElement(q_[i], e)) {
                removeAtLocked(i);
                return;
            }
        }
    }

protected:
    int32_t compare(const T& a, const T& b) {
        if (cmp_ != nullptr) return cmp_->compare(a, b);
        return detail::compareNatural(a, b);
    }
    T dequeueLocked() {
        if (q_.empty()) return T{};
        T result = q_[0];
        size_t n = q_.size() - 1;
        T x = q_[n];
        q_.pop_back();
        if (n > 0) siftDown(0, x);
        return result;
    }
    int32_t indexOfLocked(const T& o) {
        if (detail::isNullValue(o)) return -1;
        for (size_t i = 0; i < q_.size(); i++)
            if (detail::javaEquals(o, static_cast<const T&>(q_[i]))) return static_cast<int32_t>(i);
        return -1;
    }
    void removeAtLocked(size_t i) {
        size_t s = q_.size() - 1;
        if (s == i) {
            q_.pop_back();
        } else {
            T moved = q_[s];
            q_.pop_back();
            siftDown(i, moved);
            if (detail::sameElement(q_[i], moved)) siftUp(i, moved);
        }
    }
    void siftUp(size_t k, T x) {
        while (k > 0) {
            size_t parent = (k - 1) >> 1;
            T e = q_[parent];
            if (compare(x, e) >= 0) break;
            q_[k] = e;
            k = parent;
        }
        q_[k] = x;
    }
    void siftDown(size_t k, T x) {
        size_t n = q_.size();
        size_t half = n >> 1;
        while (k < half) {
            size_t child = (k << 1) + 1;
            T c = q_[child];
            size_t right = child + 1;
            if (right < n && compare(c, q_[right]) > 0) c = q_[child = right];
            if (compare(x, c) <= 0) break;
            q_[k] = c;
            k = child;
        }
        q_[k] = x;
    }

    detail::SyncCore core_;
    detail::Vec<T> q_;
    Comparator<T>* cmp_ = nullptr;
};

// =======================================================================================
// java.util.concurrent: Callable, Future, Delayed, ScheduledFuture, FutureTask
template<class V>
class Callable : public virtual Object {
public:
    virtual V call() = 0;
    template<class F> static Callable<V>* of(F f);
};

namespace detail {
template<class V, class F>
class CallableLambda final : public virtual Callable<V> {
public:
    explicit CallableLambda(F f) : f_(std::move(f)) {}
    V call() override { return f_(); }

private:
    F f_;
};

// A value returned by a Callable as a Java Object (boxed for primitives, Strings, enums).
template<class V>
Object* toObject(const V& v) {
    if constexpr (std::is_pointer_v<V>) {
        return v == nullptr ? nullptr : asObject(v);
    } else if constexpr (std::is_same_v<V, std::nullptr_t>) {
        return nullptr;
    } else {
        return box(v);
    }
}

// Callable<V>* -> Callable<Object*>* (identity for V = Object*).
template<class V>
Callable<Object*>* objectCallable(Callable<V>* c) {
    if (c == nullptr) throwNullPointer();
    if constexpr (std::is_same_v<V, Object*>) {
        return c;
    } else {
        return Callable<Object*>::of([c]() -> Object* { return toObject<V>(c->call()); });
    }
}
}  // namespace detail

template<class V>
template<class F>
Callable<V>* Callable<V>::of(F f) {
    return new detail::CallableLambda<V, F>(std::move(f));
}

// java.util.concurrent.Future (erased: get() returns the result as a jlang::Object*, boxed
// for primitive results).
class Future : public virtual Object {
public:
    virtual bool cancel(bool mayInterruptIfRunning) = 0;
    virtual bool isCancelled() = 0;
    virtual bool isDone() = 0;
    // CancellationException, ExecutionException (cause = the task's exception),
    // InterruptedException, TimeoutException (timed get) as in Java.
    virtual Object* get() = 0;
    virtual Object* get(int64_t timeout, TimeUnit unit) = 0;
};

class Delayed : public virtual Comparable<Delayed*> {
public:
    virtual int64_t getDelay(TimeUnit unit) = 0;
};

class ScheduledFuture : public virtual Delayed, public virtual Future {};

// java.util.concurrent.RunnableScheduledFuture (what ScheduledThreadPoolExecutor queues).
class RunnableScheduledFuture : public virtual ScheduledFuture, public virtual Runnable {
public:
    virtual bool isPeriodic() = 0;
};

// java.util.concurrent.FutureTask (also java.util.concurrent.RunnableFuture).
class FutureTask : public virtual Runnable, public virtual Future {
public:
    template<class V>
    explicit FutureTask(Callable<V>* callable) : FutureTask(detail::objectCallable(callable), 0) {}
    FutureTask(Runnable* runnable, Object* result);

    bool cancel(bool mayInterruptIfRunning) override;
    bool isCancelled() override;
    bool isDone() override;
    Object* get() override;
    Object* get(int64_t timeout, TimeUnit unit) override;
    void run() override;

protected:
    virtual void done() {}
    virtual void set(Object* v);
    virtual void setException(Throwable* t);
    // Runs the task without setting a result; false if it threw or was cancelled.
    bool runAndReset();

private:
    FutureTask(Callable<Object*>* callable, int);
    int32_t awaitDone(bool timed, int64_t nanos);
    Object* report(int32_t s);
    void finishCompletion();

    Callable<Object*>* callable_;
    std::atomic<int32_t> state_{0};
    Object* outcome_ = nullptr;
    std::atomic<Thread*> runner_{nullptr};
    detail::SyncCore core_;
};

// =======================================================================================
// Executors
class Executor : public virtual Object {
public:
    virtual void execute(Runnable* command) = 0;
};

class ExecutorService : public virtual Executor {
public:
    virtual void shutdown() = 0;
    virtual List<Runnable*>* shutdownNow() = 0;
    virtual bool isShutdown() = 0;
    virtual bool isTerminated() = 0;
    virtual bool awaitTermination(int64_t timeout, TimeUnit unit) = 0;
    virtual Future* submit(Runnable* task) = 0;
    virtual Future* submit(Runnable* task, Object* result) = 0;
    virtual Future* submit(Callable<Object*>* task) = 0;
    template<class V>
        requires(!std::is_same_v<V, Object*>)
    Future* submit(Callable<V>* task) {
        return submit(detail::objectCallable(task));
    }
};

class ScheduledExecutorService : public virtual ExecutorService {
public:
    virtual ScheduledFuture* schedule(Runnable* command, int64_t delay, TimeUnit unit) = 0;
    virtual ScheduledFuture* schedule(Callable<Object*>* callable, int64_t delay, TimeUnit unit) = 0;
    template<class V>
        requires(!std::is_same_v<V, Object*>)
    ScheduledFuture* schedule(Callable<V>* callable, int64_t delay, TimeUnit unit) {
        return schedule(detail::objectCallable(callable), delay, unit);
    }
    virtual ScheduledFuture* scheduleAtFixedRate(Runnable* command, int64_t initialDelay, int64_t period,
                                                 TimeUnit unit) = 0;
    virtual ScheduledFuture* scheduleWithFixedDelay(Runnable* command, int64_t initialDelay, int64_t delay,
                                                    TimeUnit unit) = 0;
};

class AbstractExecutorService : public virtual ExecutorService {
public:
    using ExecutorService::submit;
    Future* submit(Runnable* task) override;
    Future* submit(Runnable* task, Object* result) override;
    Future* submit(Callable<Object*>* task) override;

protected:
    virtual FutureTask* newTaskFor(Runnable* runnable, Object* value);
    virtual FutureTask* newTaskFor(Callable<Object*>* callable);
};

class ThreadFactory : public virtual Object {
public:
    virtual Thread* newThread(Runnable* r) = 0;
    template<class F> static ThreadFactory* of(F f);
};

namespace detail {
template<class F>
class ThreadFactoryLambda final : public virtual ThreadFactory {
public:
    explicit ThreadFactoryLambda(F f) : f_(std::move(f)) {}
    Thread* newThread(Runnable* r) override { return f_(r); }

private:
    F f_;
};
}  // namespace detail
template<class F>
ThreadFactory* ThreadFactory::of(F f) {
    return new detail::ThreadFactoryLambda<F>(std::move(f));
}

class ThreadPoolExecutor;

class RejectedExecutionHandler : public virtual Object {
public:
    virtual void rejectedExecution(Runnable* r, ThreadPoolExecutor* executor) = 0;
    template<class F> static RejectedExecutionHandler* of(F f);
};

namespace detail {
template<class F>
class RejectedHandlerLambda final : public virtual RejectedExecutionHandler {
public:
    explicit RejectedHandlerLambda(F f) : f_(std::move(f)) {}
    void rejectedExecution(Runnable* r, ThreadPoolExecutor* e) override { f_(r, e); }

private:
    F f_;
};
class TpeWorker;
}  // namespace detail
template<class F>
RejectedExecutionHandler* RejectedExecutionHandler::of(F f) {
    return new detail::RejectedHandlerLambda<F>(std::move(f));
}

// ThreadPoolExecutor's nested policies (hoisted: ThreadPoolExecutor_AbortPolicy, alias
// ThreadPoolExecutor::AbortPolicy, ...).
class ThreadPoolExecutor_AbortPolicy : public virtual RejectedExecutionHandler {
public:
    void rejectedExecution(Runnable* r, ThreadPoolExecutor* e) override;  // RejectedExecutionException
};
class ThreadPoolExecutor_CallerRunsPolicy : public virtual RejectedExecutionHandler {
public:
    void rejectedExecution(Runnable* r, ThreadPoolExecutor* e) override;  // r->run() unless shut down
};
class ThreadPoolExecutor_DiscardPolicy : public virtual RejectedExecutionHandler {
public:
    void rejectedExecution(Runnable* r, ThreadPoolExecutor* e) override;
};
class ThreadPoolExecutor_DiscardOldestPolicy : public virtual RejectedExecutionHandler {
public:
    void rejectedExecution(Runnable* r, ThreadPoolExecutor* e) override;
};

// java.util.concurrent.ThreadPoolExecutor (JDK 8 algorithm). Worker threads are created by
// the ThreadFactory (default: Executors.defaultThreadFactory(): non-daemon
// "pool-N-thread-M"). An exception thrown by a task given to execute() terminates its worker
// thread through the uncaught exception handler (and the worker is replaced), like Java;
// submit() captures it in the Future instead.
class ThreadPoolExecutor : public AbstractExecutorService {
public:
    using AbortPolicy = ThreadPoolExecutor_AbortPolicy;
    using CallerRunsPolicy = ThreadPoolExecutor_CallerRunsPolicy;
    using DiscardPolicy = ThreadPoolExecutor_DiscardPolicy;
    using DiscardOldestPolicy = ThreadPoolExecutor_DiscardOldestPolicy;

    ThreadPoolExecutor(int32_t corePoolSize, int32_t maximumPoolSize, int64_t keepAliveTime, TimeUnit unit,
                       BlockingQueue<Runnable*>* workQueue);
    ThreadPoolExecutor(int32_t corePoolSize, int32_t maximumPoolSize, int64_t keepAliveTime, TimeUnit unit,
                       BlockingQueue<Runnable*>* workQueue, ThreadFactory* threadFactory);
    ThreadPoolExecutor(int32_t corePoolSize, int32_t maximumPoolSize, int64_t keepAliveTime, TimeUnit unit,
                       BlockingQueue<Runnable*>* workQueue, RejectedExecutionHandler* handler);
    ThreadPoolExecutor(int32_t corePoolSize, int32_t maximumPoolSize, int64_t keepAliveTime, TimeUnit unit,
                       BlockingQueue<Runnable*>* workQueue, ThreadFactory* threadFactory,
                       RejectedExecutionHandler* handler);

    void execute(Runnable* command) override;
    void shutdown() override;
    List<Runnable*>* shutdownNow() override;
    bool isShutdown() override;
    virtual bool isTerminating();
    bool isTerminated() override;
    bool awaitTermination(int64_t timeout, TimeUnit unit) override;

    // (virtual like every non-final Java method: subclasses such as Netty's executors override them)
    virtual void setThreadFactory(ThreadFactory* threadFactory);
    virtual ThreadFactory* getThreadFactory() { return threadFactory_.load(); }
    virtual void setRejectedExecutionHandler(RejectedExecutionHandler* handler);
    virtual RejectedExecutionHandler* getRejectedExecutionHandler() { return handler_.load(); }
    virtual void setCorePoolSize(int32_t corePoolSize);
    virtual int32_t getCorePoolSize() { return corePoolSize_.load(); }
    virtual bool prestartCoreThread();
    virtual int32_t prestartAllCoreThreads();
    virtual bool allowsCoreThreadTimeOut() { return allowCoreThreadTimeOut_.load(); }
    virtual void allowCoreThreadTimeOut(bool value);
    virtual void setMaximumPoolSize(int32_t maximumPoolSize);
    virtual int32_t getMaximumPoolSize() { return maximumPoolSize_.load(); }
    virtual void setKeepAliveTime(int64_t time, TimeUnit unit);
    virtual int64_t getKeepAliveTime(TimeUnit unit);
    virtual BlockingQueue<Runnable*>* getQueue() { return workQueue_; }
    virtual bool remove(Runnable* task);
    virtual void purge();  // removes cancelled Futures from the queue
    virtual int32_t getPoolSize();
    virtual int32_t getActiveCount();
    virtual int32_t getLargestPoolSize();
    virtual int64_t getTaskCount();
    virtual int64_t getCompletedTaskCount();
    // java.util.concurrent.ThreadPoolExecutor@hash[Running, pool size = 0, active threads = 0,
    // queued tasks = 0, completed tasks = 0]
    String toString() override;

protected:
    virtual void beforeExecute(Thread* t, Runnable* r) {
        (void)t;
        (void)r;
    }
    virtual void afterExecute(Runnable* r, Throwable* t) {
        (void)r;
        (void)t;
    }
    virtual void terminated() {}
    // jlang: the Java class name used by toString()
    virtual const char* javaClassName() const { return "java.util.concurrent.ThreadPoolExecutor"; }

    // ---- package-private in Java (used by ScheduledThreadPoolExecutor)
    void reject(Runnable* command);
    virtual void onShutdown() {}
    bool isRunningOrShutdown(bool shutdownOK);
    void ensurePrestart();
    void tryTerminate();

private:
    friend class detail::TpeWorker;
    bool addWorker(Runnable* firstTask, bool core);
    void addWorkerFailed(detail::TpeWorker* w);
    void processWorkerExit(detail::TpeWorker* w, bool completedAbruptly);
    Runnable* getTask();
    void runWorker(detail::TpeWorker* w);
    void interruptIdleWorkers(bool onlyOne);
    void interruptWorkers();
    void advanceRunState(int32_t targetState);
    List<Runnable*>* drainQueue();
    bool compareAndIncrementWorkerCount(int32_t expect);
    bool compareAndDecrementWorkerCount(int32_t expect);
    void decrementWorkerCount();

    std::atomic<int32_t> ctl_;
    BlockingQueue<Runnable*>* workQueue_;
    ReentrantLock* mainLock_;
    Condition* termination_;
    std::vector<detail::TpeWorker*>* workers_;  // guarded by mainLock_
    int32_t largestPoolSize_ = 0;
    int64_t completedTaskCount_ = 0;
    std::atomic<ThreadFactory*> threadFactory_;
    std::atomic<RejectedExecutionHandler*> handler_;
    std::atomic<int64_t> keepAliveTime_;
    std::atomic<bool> allowCoreThreadTimeOut_{false};
    std::atomic<int32_t> corePoolSize_;
    std::atomic<int32_t> maximumPoolSize_;
};

// java.util.concurrent.ScheduledThreadPoolExecutor (JDK 8 algorithm; core threads only).
// Periodic tasks: fixed rate schedules from the previous planned start, fixed delay from the
// end of the previous run; if a run throws, the task stops and its Future holds the
// exception. cancel() removes the task from the queue when setRemoveOnCancelPolicy(true).
class ScheduledThreadPoolExecutor : public ThreadPoolExecutor, public virtual ScheduledExecutorService {
public:
    using ThreadPoolExecutor::submit;
    explicit ScheduledThreadPoolExecutor(int32_t corePoolSize);
    ScheduledThreadPoolExecutor(int32_t corePoolSize, ThreadFactory* threadFactory);
    ScheduledThreadPoolExecutor(int32_t corePoolSize, RejectedExecutionHandler* handler);
    ScheduledThreadPoolExecutor(int32_t corePoolSize, ThreadFactory* threadFactory, RejectedExecutionHandler* handler);

    ScheduledFuture* schedule(Runnable* command, int64_t delay, TimeUnit unit) override;
    ScheduledFuture* schedule(Callable<Object*>* callable, int64_t delay, TimeUnit unit) override;
    using ScheduledExecutorService::schedule;
    ScheduledFuture* scheduleAtFixedRate(Runnable* command, int64_t initialDelay, int64_t period,
                                         TimeUnit unit) override;
    ScheduledFuture* scheduleWithFixedDelay(Runnable* command, int64_t initialDelay, int64_t delay,
                                            TimeUnit unit) override;
    void execute(Runnable* command) override;  // schedule(command, 0, NANOSECONDS)
    Future* submit(Runnable* task) override;
    Future* submit(Runnable* task, Object* result) override;
    Future* submit(Callable<Object*>* task) override;
    void shutdown() override;
    List<Runnable*>* shutdownNow() override;
    bool isShutdown() override { return ThreadPoolExecutor::isShutdown(); }
    bool isTerminated() override { return ThreadPoolExecutor::isTerminated(); }
    bool awaitTermination(int64_t timeout, TimeUnit unit) override {
        return ThreadPoolExecutor::awaitTermination(timeout, unit);
    }

    void setContinueExistingPeriodicTasksAfterShutdownPolicy(bool value);
    bool getContinueExistingPeriodicTasksAfterShutdownPolicy() { return continueExistingPeriodic_.load(); }
    void setExecuteExistingDelayedTasksAfterShutdownPolicy(bool value);
    bool getExecuteExistingDelayedTasksAfterShutdownPolicy() { return executeExistingDelayed_.load(); }
    void setRemoveOnCancelPolicy(bool value) { removeOnCancel_.store(value); }
    bool getRemoveOnCancelPolicy() { return removeOnCancel_.load(); }

    // ---- jlang internals (used by the scheduled task class)
    bool canRunInCurrentRunState(bool periodic);
    void reExecutePeriodic(RunnableScheduledFuture* task);
    int64_t triggerTime(int64_t delayNanos);
    bool removeTask(Runnable* task) { return remove(task); }

protected:
    virtual RunnableScheduledFuture* decorateTask(Runnable* runnable, RunnableScheduledFuture* task) {
        (void)runnable;
        return task;
    }
    virtual RunnableScheduledFuture* decorateTask(Callable<Object*>* callable, RunnableScheduledFuture* task) {
        (void)callable;
        return task;
    }
    void onShutdown() override;
    const char* javaClassName() const override { return "java.util.concurrent.ScheduledThreadPoolExecutor"; }

private:
    void delayedExecute(RunnableScheduledFuture* task);
    int64_t triggerTime(int64_t delay, TimeUnit unit);
    int64_t overflowFree(int64_t delay);

    std::atomic<bool> continueExistingPeriodic_{false};
    std::atomic<bool> executeExistingDelayed_{true};
    std::atomic<bool> removeOnCancel_{false};
};

// java.util.concurrent.Executors
class Executors {
public:
    Executors() = delete;
    static ExecutorService* newFixedThreadPool(int32_t nThreads);
    static ExecutorService* newFixedThreadPool(int32_t nThreads, ThreadFactory* threadFactory);
    static ExecutorService* newCachedThreadPool();
    static ExecutorService* newCachedThreadPool(ThreadFactory* threadFactory);
    static ExecutorService* newSingleThreadExecutor();
    static ExecutorService* newSingleThreadExecutor(ThreadFactory* threadFactory);
    static ScheduledExecutorService* newScheduledThreadPool(int32_t corePoolSize);
    static ScheduledExecutorService* newScheduledThreadPool(int32_t corePoolSize, ThreadFactory* threadFactory);
    static ScheduledExecutorService* newSingleThreadScheduledExecutor();
    static ScheduledExecutorService* newSingleThreadScheduledExecutor(ThreadFactory* threadFactory);
    // Non-daemon, NORM_PRIORITY threads named "pool-<poolNumber>-thread-<n>".
    static ThreadFactory* defaultThreadFactory();
    static Callable<Object*>* callable(Runnable* task, Object* result);
    static Callable<Object*>* callable(Runnable* task);
};

// =======================================================================================
// java.util.Timer / TimerTask (one background thread per Timer; a task that throws kills
// the timer, later schedule calls throw IllegalStateException, as in Java).
class TimerTask : public virtual Runnable {
public:
    virtual bool cancel();
    int64_t scheduledExecutionTime();

protected:
    TimerTask() {}

private:
    friend class Timer;
    friend struct TimerInternals;
    std::mutex lock_;
    int32_t state_ = 0;  // 0 VIRGIN, 1 SCHEDULED, 2 EXECUTED, 3 CANCELLED
    int64_t nextExecutionTime_ = 0;
    int64_t period_ = 0;
};

namespace detail {
class TimerQueue;
}

class Timer : public virtual Object {
public:
    Timer();  // non-daemon thread "Timer-<n>"
    explicit Timer(bool isDaemon);
    explicit Timer(const String& name);
    Timer(const String& name, bool isDaemon);

    void schedule(TimerTask* task, int64_t delay);
    void schedule(TimerTask* task, Date* time);
    void schedule(TimerTask* task, int64_t delay, int64_t period);
    void schedule(TimerTask* task, Date* firstTime, int64_t period);
    void scheduleAtFixedRate(TimerTask* task, int64_t delay, int64_t period);
    void scheduleAtFixedRate(TimerTask* task, Date* firstTime, int64_t period);
    // int overloads: a literal 0 delay must not be taken for a null Date*
    void schedule(TimerTask* task, int32_t delay) { schedule(task, static_cast<int64_t>(delay)); }
    void schedule(TimerTask* task, int32_t delay, int64_t period) { schedule(task, static_cast<int64_t>(delay), period); }
    void scheduleAtFixedRate(TimerTask* task, int32_t delay, int64_t period) {
        scheduleAtFixedRate(task, static_cast<int64_t>(delay), period);
    }
    void cancel();
    int32_t purge();

private:
    void sched(TimerTask* task, int64_t time, int64_t period);
    void start(const String& name, bool isDaemon);
    detail::TimerQueue* queue_;
    Thread* thread_;
};

}  // namespace jlang

template<>
struct std::hash<jlang::TimeUnit> {
    size_t operator()(jlang::TimeUnit u) const noexcept { return static_cast<size_t>(u.ordinal()); }
};
