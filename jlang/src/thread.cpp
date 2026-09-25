// java.lang.Thread / ThreadGroup, TimeUnit, locks, conditions and CountDownLatch.
#include <jlang/Thread.h>
#include <jlang/Time.h>

#include <cxxabi.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace jlang {

// =======================================================================================
// detail helpers

namespace detail {

void SyncCore::wake(void* self) {
    auto* c = static_cast<SyncCore*>(self);
    std::lock_guard<std::mutex> g(c->mu);
    c->notEmpty.notify_all();
    c->notFull.notify_all();
}

void throwIfInterrupted() {
    if (sync::interrupted()) throw InterruptedException();
}

void throwInterrupted() { throw InterruptedException(); }

uintptr_t currentThreadToken() noexcept { return static_cast<uintptr_t>(pthread_self()); }

}  // namespace detail

// =======================================================================================
// TimeUnit

namespace {
const int64_t UNIT_NANOS[] = {INT64_C(1),
                              INT64_C(1000),
                              INT64_C(1000000),
                              INT64_C(1000000000),
                              INT64_C(60000000000),
                              INT64_C(3600000000000),
                              INT64_C(86400000000000)};
const char* const UNIT_NAMES[] = {"NANOSECONDS", "MICROSECONDS", "MILLISECONDS", "SECONDS", "MINUTES", "HOURS", "DAYS"};

// Converts d from unit `from` to unit `to` with Java's saturation.
int64_t convertUnits(int64_t d, int32_t from, int32_t to) {
    if (from == to || from < 0 || from > 6 || to < 0 || to > 6) return d;
    if (from > to) {
        int64_t m = UNIT_NANOS[from] / UNIT_NANOS[to];
        int64_t over = INT64_MAX / m;
        if (d > over) return INT64_MAX;
        if (d < -over) return INT64_MIN;
        return d * m;
    }
    return d / (UNIT_NANOS[to] / UNIT_NANOS[from]);
}

int32_t unitIndex(TimeUnit u) {
    if (u == nullptr) detail::throwNullPointerException();
    return u.ordinal();
}
}  // namespace

String TimeUnit::name() const {
    if (v_ == Value::_NULL) return String("null");
    return String(UNIT_NAMES[ordinal()]);
}

Array<TimeUnit>* TimeUnit::values() {
    auto* a = new Array<TimeUnit>(7);
    for (int32_t i = 0; i < 7; i++) (*a)[i] = TimeUnit(static_cast<Value>(i));
    return a;
}

TimeUnit TimeUnit::valueOf(const String& name) {
    for (int32_t i = 0; i < 7; i++)
        if (name == UNIT_NAMES[i]) return TimeUnit(static_cast<Value>(i));
    if (name == nullptr) throw NullPointerException(String("Name is null"));
    throw IllegalArgumentException(str("No enum constant java.util.concurrent.TimeUnit.", name));
}

int64_t TimeUnit::convert(int64_t sourceDuration, TimeUnit sourceUnit) const {
    return convertUnits(sourceDuration, unitIndex(sourceUnit), unitIndex(*this));
}
int64_t TimeUnit::toNanos(int64_t d) const { return convertUnits(d, unitIndex(*this), 0); }
int64_t TimeUnit::toMicros(int64_t d) const { return convertUnits(d, unitIndex(*this), 1); }
int64_t TimeUnit::toMillis(int64_t d) const { return convertUnits(d, unitIndex(*this), 2); }
int64_t TimeUnit::toSeconds(int64_t d) const { return convertUnits(d, unitIndex(*this), 3); }
int64_t TimeUnit::toMinutes(int64_t d) const { return convertUnits(d, unitIndex(*this), 4); }
int64_t TimeUnit::toHours(int64_t d) const { return convertUnits(d, unitIndex(*this), 5); }
int64_t TimeUnit::toDays(int64_t d) const { return convertUnits(d, unitIndex(*this), 6); }

void TimeUnit::sleep(int64_t timeout) const {
    if (timeout > 0) {
        int64_t ms = toMillis(timeout);
        int64_t ns = toNanos(timeout) - ms * 1000000;
        Thread::sleep(ms, static_cast<int32_t>(ns < 0 ? 0 : (ns > 999999 ? 999999 : ns)));
    }
}

void TimeUnit::timedJoin(Thread* thread, int64_t timeout) const {
    if (thread == nullptr) detail::throwNullPointerException();
    if (timeout > 0) {
        int64_t ms = toMillis(timeout);
        int64_t ns = toNanos(timeout) - ms * 1000000;
        thread->join(ms, static_cast<int32_t>(ns < 0 ? 0 : (ns > 999999 ? 999999 : ns)));
    }
}

void TimeUnit::timedWait(Object* obj, int64_t timeout) const {
    if (obj == nullptr) detail::throwNullPointerException();
    if (timeout > 0) {
        int64_t ms = toMillis(timeout);
        int64_t ns = toNanos(timeout) - ms * 1000000;
        obj->wait(ms, static_cast<int32_t>(ns < 0 ? 0 : (ns > 999999 ? 999999 : ns)));
    }
}

// =======================================================================================
// Thread registry

namespace {

struct Registry {
    std::mutex mu;
    std::condition_variable cv;
    std::unordered_set<Thread*> live;  // started (and attached) threads: keeps them reachable
    int32_t nonDaemonRunning = 0;
};

Registry& registry() {
    static Registry* r = new Registry();
    return *r;
}

std::atomic<int64_t> g_threadSeq{1};  // 1 is the main thread
std::atomic<int32_t> g_threadInitNumber{0};
std::atomic<Thread_UncaughtExceptionHandler*> g_defaultHandler{nullptr};
Thread_UncaughtExceptionHandler* g_defaultHandlerRoot = nullptr;  // collector root

thread_local Thread* tl_current = nullptr;

bool isMainThread() { return static_cast<long>(::getpid()) == ::syscall(SYS_gettid); }

void setNativeName(const String& name) {
    std::string n(name);
    if (n.size() > 15) n.resize(15);
    pthread_setname_np(pthread_self(), n.c_str());
}

// Detaches the wrapper of a thread not started by jlang when that thread exits.
struct AttachedThreadCleanup {
    Thread* t = nullptr;
    ~AttachedThreadCleanup();
};
thread_local AttachedThreadCleanup tl_attached;

}  // namespace

struct ThreadInternals {
    static Thread* attach() {
        bool main = isMainThread();
        Thread* t = new Thread(Thread::AttachTag{}, main);
        tl_current = t;
        sync::bindCurrentThread(t->interruptState_);
        {
            Registry& r = registry();
            std::lock_guard<std::mutex> g(r.mu);
            r.live.insert(t);
        }
        if (!main) tl_attached.t = t;
        return t;
    }
    static void detach(Thread* t) {
        JSYNC(t) {
            t->state_.store(2);
            t->notifyAll();
        }
        if (t->group_ != nullptr) t->group_->remove(t);
        Registry& r = registry();
        std::lock_guard<std::mutex> g(r.mu);
        r.live.erase(t);
    }
};

namespace {
AttachedThreadCleanup::~AttachedThreadCleanup() {
    if (t == nullptr) return;
    // The thread may already be unregistered from the collector (TLS destructors run after
    // the thread function returned): register again while touching GC memory.
    gc::ThreadRegistration reg;
    Thread* th = t;
    t = nullptr;
    try {
        ThreadInternals::detach(th);
    } catch (...) {
    }
    sync::bindCurrentThread(nullptr);
    tl_current = nullptr;
}
}  // namespace

// =======================================================================================
// ThreadGroup

ThreadGroup* ThreadGroup::systemGroup() {
    static ThreadGroup* g = new ThreadGroup(RootTag{}, nullptr, String("system"));
    return g;
}

ThreadGroup* ThreadGroup::mainGroup() {
    static ThreadGroup* g = new ThreadGroup(RootTag{}, systemGroup(), String("main"));
    return g;
}

ThreadGroup::ThreadGroup(RootTag, ThreadGroup* parent, const String& name) : name_(name), parent_(parent) {
    if (parent != nullptr) {
        maxPriority_ = parent->getMaxPriority();
        std::lock_guard<std::mutex> g(parent->mu_);
        parent->groups_.push_back(this);
    }
}

ThreadGroup::ThreadGroup(const String& name) : ThreadGroup(Thread::currentThread()->getThreadGroup(), name) {}

ThreadGroup::ThreadGroup(ThreadGroup* parent, const String& name) : name_(name), parent_(parent) {
    if (parent == nullptr) throw NullPointerException();
    maxPriority_ = parent->getMaxPriority();
    daemon_ = parent->isDaemon();
    std::lock_guard<std::mutex> g(parent->mu_);
    parent->groups_.push_back(this);
}

int32_t ThreadGroup::getMaxPriority() {
    std::lock_guard<std::mutex> g(mu_);
    return maxPriority_;
}

void ThreadGroup::setMaxPriority(int32_t pri) {
    if (pri < Thread::MIN_PRIORITY || pri > Thread::MAX_PRIORITY) return;
    std::vector<ThreadGroup*> subs;
    {
        std::lock_guard<std::mutex> g(mu_);
        maxPriority_ = (parent_ != nullptr) ? std::min(pri, parent_->getMaxPriority()) : pri;
        subs = groups_;
    }
    for (ThreadGroup* s : subs) s->setMaxPriority(pri);
}

bool ThreadGroup::isDaemon() {
    std::lock_guard<std::mutex> g(mu_);
    return daemon_;
}

void ThreadGroup::setDaemon(bool daemon) {
    std::lock_guard<std::mutex> g(mu_);
    daemon_ = daemon;
}

void ThreadGroup::add(Thread* t) {
    std::lock_guard<std::mutex> g(mu_);
    threads_.push_back(t);
}

void ThreadGroup::remove(Thread* t) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = std::find(threads_.begin(), threads_.end(), t);
    if (it != threads_.end()) threads_.erase(it);
}

void ThreadGroup::collect(std::vector<Thread*>& out, bool recurse) {
    std::vector<ThreadGroup*> subs;
    {
        std::lock_guard<std::mutex> g(mu_);
        for (Thread* t : threads_)
            if (t->isAlive()) out.push_back(t);
        subs = groups_;
    }
    if (recurse)
        for (ThreadGroup* s : subs) s->collect(out, true);
}

int32_t ThreadGroup::activeCount() {
    std::vector<Thread*> v;
    collect(v, true);
    return static_cast<int32_t>(v.size());
}

int32_t ThreadGroup::activeGroupCount() {
    std::vector<ThreadGroup*> subs;
    {
        std::lock_guard<std::mutex> g(mu_);
        subs = groups_;
    }
    int32_t n = static_cast<int32_t>(subs.size());
    for (ThreadGroup* s : subs) n += s->activeGroupCount();
    return n;
}

int32_t ThreadGroup::enumerate(Array<Thread*>* list) {
    if (list == nullptr) detail::throwNullPointerException();
    std::vector<Thread*> v;
    collect(v, true);
    int32_t n = std::min(list->length, static_cast<int32_t>(v.size()));
    for (int32_t i = 0; i < n; i++) (*list)[i] = v[static_cast<size_t>(i)];
    return n;
}

bool ThreadGroup::parentOf(ThreadGroup* g) {
    for (; g != nullptr; g = g->parent_)
        if (g == this) return true;
    return false;
}

void ThreadGroup::interrupt() {
    std::vector<Thread*> v;
    collect(v, true);
    for (Thread* t : v) t->interrupt();
}

void ThreadGroup::uncaughtException(Thread* t, Throwable* e) {
    if (parent_ != nullptr) {
        parent_->uncaughtException(t, e);
        return;
    }
    Thread_UncaughtExceptionHandler* ueh = Thread::getDefaultUncaughtExceptionHandler();
    if (ueh != nullptr) {
        ueh->uncaughtException(t, e);
    } else {
        System::err->print(str("Exception in thread \"", t->getName(), "\" "));
        if (e != nullptr) e->printStackTrace(System::err);
        else System::err->println(String("null"));
    }
}

String ThreadGroup::toString() {
    return str("java.lang.ThreadGroup[name=", name_, ",maxpri=", getMaxPriority(), "]");
}

// =======================================================================================
// Thread

Thread::Thread(AttachTag, bool isMainThread) {
    attached_ = true;
    group_ = ThreadGroup::mainGroup();
    priority_ = NORM_PRIORITY;
    daemon_ = false;
    if (isMainThread) {
        tid_ = 1;
        name_ = String("main");
    } else {
        tid_ = ++g_threadSeq;
        name_ = str("Thread-", g_threadInitNumber.fetch_add(1));
    }
    interruptState_ = sync::newInterruptState();
    state_.store(1);
    group_->add(this);
}

void Thread::init(ThreadGroup* g, Runnable* target, const String& name, int64_t stackSize) {
    if (name == nullptr) throw NullPointerException(String("name cannot be null"));
    Thread* parent = currentThread();
    if (g == nullptr) g = parent->getThreadGroup();
    if (g == nullptr) g = ThreadGroup::mainGroup();
    group_ = g;
    daemon_ = parent->isDaemon();
    priority_ = parent->getPriority();
    name_ = name;
    target_ = target;
    stackSize_ = stackSize;
    tid_ = ++g_threadSeq;
    interruptState_ = sync::newInterruptState();
    int32_t maxp = g->getMaxPriority();
    if (priority_ > maxp) priority_ = maxp;
}

static String nextThreadName() { return str("Thread-", g_threadInitNumber.fetch_add(1)); }

Thread::Thread() { init(nullptr, nullptr, nextThreadName(), 0); }
Thread::Thread(Runnable* target) { init(nullptr, target, nextThreadName(), 0); }
Thread::Thread(const String& name) { init(nullptr, nullptr, name, 0); }
Thread::Thread(Runnable* target, const String& name) { init(nullptr, target, name, 0); }
Thread::Thread(ThreadGroup* group, Runnable* target) { init(group, target, nextThreadName(), 0); }
Thread::Thread(ThreadGroup* group, const String& name) { init(group, nullptr, name, 0); }
Thread::Thread(ThreadGroup* group, Runnable* target, const String& name) { init(group, target, name, 0); }
Thread::Thread(ThreadGroup* group, Runnable* target, const String& name, int64_t stackSize) {
    init(group, target, name, stackSize);
}

void Thread::run() {
    if (target_ != nullptr) target_->run();
}

void Thread::entry(void* self) {
    auto* t = static_cast<Thread*>(self);
    tl_current = t;
    sync::bindCurrentThread(t->interruptState_);
    setNativeName(t->getName());
    try {
        t->run();
    } catch (abi::__forced_unwind&) {
        t->exitThread();
        throw;
    } catch (Throwable& e) {
        t->dispatchUncaughtException(e.copyThrowable());
    } catch (std::exception& e) {
        t->dispatchUncaughtException(new Error(str("C++ exception: ", e.what())));
    } catch (...) {
        t->dispatchUncaughtException(new Error(String("unknown C++ exception")));
    }
    t->exitThread();
    sync::bindCurrentThread(nullptr);
    tl_current = nullptr;
}

void Thread::start() {
    int32_t expected = 0;
    if (attached_ || !state_.compare_exchange_strong(expected, 1)) throw IllegalThreadStateException();
    Registry& r = registry();
    {
        std::lock_guard<std::mutex> g(r.mu);
        r.live.insert(this);
        if (!daemon_) r.nonDaemonRunning++;
    }
    group_->add(this);
    try {
        size_t stack = stackSize_ > 0 ? static_cast<size_t>(stackSize_) : 0;
        gc::startNativeThread(&Thread::entry, this, stack, true);
    } catch (...) {
        group_->remove(this);
        {
            std::lock_guard<std::mutex> g(r.mu);
            r.live.erase(this);
            if (!daemon_) r.nonDaemonRunning--;
            r.cv.notify_all();
        }
        state_.store(0);
        throw;
    }
}

void Thread::exitThread() {
    // Same order as HotSpot: Thread.exit() (leave the group, drop references) runs before the
    // thread is marked terminated and joiners are notified, so a joiner sees the final state.
    ThreadGroup* g = group_;
    if (g != nullptr) g->remove(this);
    group_ = nullptr;
    target_ = nullptr;
    ueh_ = nullptr;
    JSYNC(this) {
        state_.store(2);
        this->notifyAll();
    }
    Registry& r = registry();
    {
        std::lock_guard<std::mutex> lk(r.mu);
        r.live.erase(this);
        if (!daemon_) r.nonDaemonRunning--;
        r.cv.notify_all();
    }
}

void Thread::dispatchUncaughtException(Throwable* e) {
    try {
        UncaughtExceptionHandler* h = getUncaughtExceptionHandler();
        if (h == nullptr) h = ThreadGroup::mainGroup();
        h->uncaughtException(this, e);
    } catch (...) {
        // Java ignores exceptions thrown by the handler
    }
}

void Thread::join() { join(0); }

void Thread::join(int64_t millis) {
    if (millis < 0) throw IllegalArgumentException(String("timeout value is negative"));
    JSYNC(this) {
        int64_t base = System::currentTimeMillis();
        int64_t now = 0;
        if (millis == 0) {
            while (isAlive()) this->wait(0);
        } else {
            while (isAlive()) {
                int64_t delay = millis - now;
                if (delay <= 0) break;
                this->wait(delay);
                now = System::currentTimeMillis() - base;
            }
        }
    }
}

void Thread::join(int64_t millis, int32_t nanos) {
    if (millis < 0) throw IllegalArgumentException(String("timeout value is negative"));
    if (nanos < 0 || nanos > 999999) throw IllegalArgumentException(String("nanosecond timeout value out of range"));
    if (nanos >= 500000 || (nanos != 0 && millis == 0)) millis++;
    join(millis);
}

void Thread::interrupt() {
    if (state_.load() == 1) sync::interrupt(interruptState_);
}

bool Thread::isInterrupted() { return state_.load() == 1 && sync::isInterrupted(interruptState_); }

bool Thread::interrupted() {
    currentThread();  // binds the interrupt state of attached threads
    return sync::interrupted();
}

bool Thread::isAlive() { return state_.load() == 1; }

void Thread::setDaemon(bool on) {
    if (isAlive()) throw IllegalThreadStateException();
    daemon_ = on;
}

void Thread::setName(const String& name) {
    if (name == nullptr) throw NullPointerException(String("name cannot be null"));
    {
        std::lock_guard<std::mutex> g(nameMu_);
        name_ = name;
    }
    if (tl_current == this) setNativeName(name);
}

String Thread::getName() {
    std::lock_guard<std::mutex> g(nameMu_);
    return name_;
}

void Thread::setPriority(int32_t newPriority) {
    if (newPriority > MAX_PRIORITY || newPriority < MIN_PRIORITY) throw IllegalArgumentException();
    ThreadGroup* g = group_;
    if (g != nullptr) {
        if (newPriority > g->getMaxPriority()) newPriority = g->getMaxPriority();
        priority_ = newPriority;
    }
}

Array<StackTraceElement*>* Thread::getStackTrace() { return new Array<StackTraceElement*>(0); }

Thread::UncaughtExceptionHandler* Thread::getUncaughtExceptionHandler() {
    return ueh_ != nullptr ? ueh_ : group_;
}

void Thread::setDefaultUncaughtExceptionHandler(UncaughtExceptionHandler* eh) {
    g_defaultHandlerRoot = eh;
    g_defaultHandler.store(eh);
}

Thread::UncaughtExceptionHandler* Thread::getDefaultUncaughtExceptionHandler() { return g_defaultHandler.load(); }

Thread* Thread::currentThread() {
    Thread* t = tl_current;
    if (t != nullptr) return t;
    return ThreadInternals::attach();
}

void Thread::sleep(int64_t millis) {
    currentThread();
    sync::sleep(millis, 0);
}

void Thread::sleep(int64_t millis, int32_t nanos) {
    currentThread();
    if (millis < 0) throw IllegalArgumentException(String("timeout value is negative"));
    if (nanos < 0 || nanos > 999999) throw IllegalArgumentException(String("nanosecond timeout value out of range"));
    sync::sleep(millis, nanos);
}

void Thread::yield() { sched_yield(); }

bool Thread::holdsLock(Object* obj) {
    if (obj == nullptr) detail::throwNullPointerException();
    return obj->monitorHeldByCurrentThread();
}

int32_t Thread::activeCount() {
    ThreadGroup* g = currentThread()->getThreadGroup();
    return g != nullptr ? g->activeCount() : 0;
}

int32_t Thread::enumerate(Array<Thread*>* tarray) {
    ThreadGroup* g = currentThread()->getThreadGroup();
    return g != nullptr ? g->enumerate(tarray) : 0;
}

void Thread::dumpStack() { Exception(String("Stack trace")).printStackTrace(); }

String Thread::toString() {
    ThreadGroup* g = group_;
    return str("Thread[", getName(), ",", getPriority(), ",", g != nullptr ? g->getName() : String(""), "]");
}

void Thread::joinAllNonDaemon() {
    Thread* self = tl_current;
    int32_t allowed = (self != nullptr && !self->attached_ && !self->daemon_ && self->isAlive()) ? 1 : 0;
    Registry& r = registry();
    std::unique_lock<std::mutex> lk(r.mu);
    r.cv.wait(lk, [&] { return r.nonDaemonRunning <= allowed; });
}

// =======================================================================================
// Locks

namespace detail {

void OwnedLock::wake(void* self) {
    auto* l = static_cast<OwnedLock*>(self);
    std::lock_guard<std::mutex> g(l->mu);
    for (auto* cv : l->conditions_) cv->notify_all();
}

void OwnedLock::registerCondition(std::condition_variable* cv) {
    std::lock_guard<std::mutex> g(mu);
    conditions_.push_back(cv);
}

// java.util.concurrent.locks.AbstractQueuedSynchronizer.ConditionObject semantics: FIFO
// wait queue, signal() transfers exactly one waiter, interrupt before a signal throws
// InterruptedException (after reacquiring the lock), after it re-asserts the status.
class ConditionObject final : public virtual Condition {
public:
    explicit ConditionObject(OwnedLock* owner) : owner_(owner) { owner->registerCondition(&cv_); }

    void await() override { (void)awaitImpl(false, 0, true); }
    bool await(int64_t time, TimeUnit unit) override { return awaitImpl(true, unit.toNanos(time), true) > 0; }
    int64_t awaitNanos(int64_t nanosTimeout) override { return awaitImpl(true, nanosTimeout, true); }
    void awaitUninterruptibly() override { (void)awaitImpl(false, 0, false); }
    bool awaitUntil(Date* deadline) override {
        if (deadline == nullptr) throwNullPointerException();
        int64_t ms = deadline->getTime() - System::currentTimeMillis();
        return awaitImpl(true, ms > INT64_MAX / 1000000 ? INT64_MAX : ms * 1000000, true) > 0;
    }
    void signal() override {
        std::lock_guard<std::mutex> g(owner_->mu);
        if (!owner_->heldByCurrentLocked()) throw IllegalMonitorStateException();
        while (!waiters_.empty()) {
            Node* n = waiters_.front();
            waiters_.pop_front();
            if (!n->cancelled) {
                n->signalled = true;
                cv_.notify_all();
                return;
            }
        }
    }
    void signalAll() override {
        std::lock_guard<std::mutex> g(owner_->mu);
        if (!owner_->heldByCurrentLocked()) throw IllegalMonitorStateException();
        for (Node* n : waiters_) n->signalled = true;
        waiters_.clear();
        cv_.notify_all();
    }
    bool hasWaiters() {
        std::lock_guard<std::mutex> g(owner_->mu);
        if (!owner_->heldByCurrentLocked()) throw IllegalMonitorStateException();
        return !waiters_.empty();
    }
    int32_t waitQueueLength() {
        std::lock_guard<std::mutex> g(owner_->mu);
        if (!owner_->heldByCurrentLocked()) throw IllegalMonitorStateException();
        return static_cast<int32_t>(waiters_.size());
    }
    OwnedLock* owner() { return owner_; }

private:
    struct Node {
        bool signalled = false;
        bool cancelled = false;
    };

    // Returns the remaining nanos (timed) or 1 (untimed); <= 0 on timeout.
    int64_t awaitImpl(bool timed, int64_t nanos, bool interruptible) {
        sync::InterruptState* st = sync::current();
        std::unique_lock<std::mutex> lk(owner_->mu);
        if (!owner_->heldByCurrentLocked()) throw IllegalMonitorStateException();
        if (interruptible && sync::isInterrupted(st)) {
            sync::clearInterrupt(st);
            throw InterruptedException();
        }
        Node* node = new Node();
        waiters_.push_back(node);
        int32_t saved = owner_->releaseAllLocked();
        const auto start = std::chrono::steady_clock::now();
        const auto deadline = start + std::chrono::nanoseconds(timed ? std::max<int64_t>(nanos, 0) : 0);
        bool interruptedBeforeSignal = false;
        bool timedOut = false;
        {
            BlockerScope blocker(&OwnedLock::wake, owner_);
            while (!node->signalled) {
                if (interruptible && sync::isInterrupted(st)) {
                    interruptedBeforeSignal = true;
                    break;
                }
                if (timed) {
                    if (nanos <= 0 || cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
                        if (!node->signalled) timedOut = true;
                        break;
                    }
                } else {
                    cv_.wait(lk);
                }
            }
        }
        if (!node->signalled) {
            node->cancelled = true;
            auto it = std::find(waiters_.begin(), waiters_.end(), node);
            if (it != waiters_.end()) waiters_.erase(it);
        }
        owner_->reacquireLocked(lk, saved);
        if (interruptedBeforeSignal) {
            sync::clearInterrupt(st);
            throw InterruptedException();
        }
        if (!timed) return 1;
        if (timedOut) return 0;
        int64_t elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        int64_t remaining = nanos - elapsed;
        return remaining > 0 ? remaining : 1;  // signalled: report positive
    }

    OwnedLock* owner_;
    std::condition_variable cv_;
    std::deque<Node*, GcAllocator<Node*>> waiters_;
};

// ReentrantReadWriteLock state
class RWSync : public OwnedLock {
public:
    std::condition_variable cv;  // lock waiters (readers and writers)
    uintptr_t writer = 0;
    Thread* writerThread = nullptr;
    int32_t writeHolds = 0;
    int32_t totalReads = 0;
    int32_t waitingWriters = 0;
    int32_t waitingReaders = 0;
    std::unordered_map<uintptr_t, int32_t> readHolds;

    RWSync() { conditions_.push_back(&cv); }

    int32_t readHoldsOf(uintptr_t me) {
        auto it = readHolds.find(me);
        return it == readHolds.end() ? 0 : it->second;
    }
    bool canRead(uintptr_t me) {
        if (writer != 0 && writer != me) return false;
        if (writer == me) return true;
        if (waitingWriters > 0 && readHoldsOf(me) == 0) return false;  // writer preference
        return true;
    }
    void takeRead(uintptr_t me) {
        readHolds[me]++;
        totalReads++;
    }
    bool canWrite(uintptr_t me) { return (writer == 0 || writer == me) && totalReads == 0; }
    void takeWrite(uintptr_t me) {
        writer = me;
        writerThread = Thread::currentThread();
        writeHolds++;
    }

    bool heldByCurrentLocked() override { return writer == currentThreadToken(); }
    int32_t releaseAllLocked() override {
        int32_t saved = writeHolds;
        writeHolds = 0;
        writer = 0;
        writerThread = nullptr;
        cv.notify_all();
        return saved;
    }
    void reacquireLocked(std::unique_lock<std::mutex>& lk, int32_t holds) override {
        uintptr_t me = currentThreadToken();
        waitingWriters++;
        while (!canWrite(me)) cv.wait(lk);
        waitingWriters--;
        writer = me;
        writerThread = Thread::currentThread();
        writeHolds = holds;
    }
};

class ReadLockImpl final : public virtual Lock {
public:
    explicit ReadLockImpl(RWSync* s) : s_(s) {}
    void lock() override {
        uintptr_t me = currentThreadToken();
        std::unique_lock<std::mutex> lk(s_->mu);
        s_->waitingReaders++;
        while (!s_->canRead(me)) s_->cv.wait(lk);
        s_->waitingReaders--;
        s_->takeRead(me);
    }
    void lockInterruptibly() override {
        throwIfInterrupted();
        uintptr_t me = currentThreadToken();
        std::unique_lock<std::mutex> lk(s_->mu);
        s_->waitingReaders++;
        try {
            awaitInterruptibly(lk, s_->cv, &OwnedLock::wake, s_, false, 0, [&] { return s_->canRead(me); });
        } catch (...) {
            s_->waitingReaders--;
            throw;
        }
        s_->waitingReaders--;
        s_->takeRead(me);
    }
    bool tryLock() override {
        uintptr_t me = currentThreadToken();
        std::lock_guard<std::mutex> g(s_->mu);
        if (s_->writer != 0 && s_->writer != me) return false;
        s_->takeRead(me);
        return true;
    }
    bool tryLock(int64_t time, TimeUnit unit) override {
        throwIfInterrupted();
        uintptr_t me = currentThreadToken();
        std::unique_lock<std::mutex> lk(s_->mu);
        s_->waitingReaders++;
        bool ok;
        try {
            ok = awaitInterruptibly(lk, s_->cv, &OwnedLock::wake, s_, true, unit.toNanos(time),
                                    [&] { return s_->canRead(me); });
        } catch (...) {
            s_->waitingReaders--;
            throw;
        }
        s_->waitingReaders--;
        if (ok) s_->takeRead(me);
        return ok;
    }
    void unlock() override {
        uintptr_t me = currentThreadToken();
        std::lock_guard<std::mutex> g(s_->mu);
        auto it = s_->readHolds.find(me);
        if (it == s_->readHolds.end() || it->second <= 0)
            throw IllegalMonitorStateException(String("attempt to unlock read lock, not locked by current thread"));
        if (--it->second == 0) s_->readHolds.erase(it);
        if (--s_->totalReads == 0) s_->cv.notify_all();
    }
    Condition* newCondition() override { throw UnsupportedOperationException(); }
    String toString() override {
        std::lock_guard<std::mutex> g(s_->mu);
        return str("java.util.concurrent.locks.ReentrantReadWriteLock$ReadLock@", Integer::toHexString(hashCode()),
                   "[Read locks = ", s_->totalReads, "]");
    }

private:
    RWSync* s_;
};

class WriteLockImpl final : public virtual Lock {
public:
    explicit WriteLockImpl(RWSync* s) : s_(s) {}
    void lock() override {
        uintptr_t me = currentThreadToken();
        std::unique_lock<std::mutex> lk(s_->mu);
        if (s_->writer == me) {
            s_->writeHolds++;
            return;
        }
        s_->waitingWriters++;
        while (!s_->canWrite(me)) s_->cv.wait(lk);
        s_->waitingWriters--;
        s_->takeWrite(me);
    }
    void lockInterruptibly() override {
        throwIfInterrupted();
        uintptr_t me = currentThreadToken();
        std::unique_lock<std::mutex> lk(s_->mu);
        if (s_->writer == me) {
            s_->writeHolds++;
            return;
        }
        s_->waitingWriters++;
        try {
            awaitInterruptibly(lk, s_->cv, &OwnedLock::wake, s_, false, 0, [&] { return s_->canWrite(me); });
        } catch (...) {
            s_->waitingWriters--;
            s_->cv.notify_all();
            throw;
        }
        s_->waitingWriters--;
        s_->takeWrite(me);
    }
    bool tryLock() override {
        uintptr_t me = currentThreadToken();
        std::lock_guard<std::mutex> g(s_->mu);
        if (!s_->canWrite(me)) return false;
        s_->takeWrite(me);
        return true;
    }
    bool tryLock(int64_t time, TimeUnit unit) override {
        throwIfInterrupted();
        uintptr_t me = currentThreadToken();
        std::unique_lock<std::mutex> lk(s_->mu);
        if (s_->writer == me) {
            s_->writeHolds++;
            return true;
        }
        s_->waitingWriters++;
        bool ok;
        try {
            ok = awaitInterruptibly(lk, s_->cv, &OwnedLock::wake, s_, true, unit.toNanos(time),
                                    [&] { return s_->canWrite(me); });
        } catch (...) {
            s_->waitingWriters--;
            s_->cv.notify_all();
            throw;
        }
        s_->waitingWriters--;
        if (ok) s_->takeWrite(me);
        else s_->cv.notify_all();
        return ok;
    }
    void unlock() override {
        uintptr_t me = currentThreadToken();
        std::lock_guard<std::mutex> g(s_->mu);
        if (s_->writer != me) throw IllegalMonitorStateException();
        if (--s_->writeHolds == 0) {
            s_->writer = 0;
            s_->writerThread = nullptr;
            s_->cv.notify_all();
        }
    }
    Condition* newCondition() override { return new ConditionObject(s_); }
    String toString() override {
        std::lock_guard<std::mutex> g(s_->mu);
        Thread* o = s_->writerThread;
        return str("java.util.concurrent.locks.ReentrantReadWriteLock$WriteLock@", Integer::toHexString(hashCode()),
                   o == nullptr ? String("[Unlocked]") : str("[Locked by thread ", o->getName(), "]"));
    }

private:
    RWSync* s_;
};

}  // namespace detail

// ---- ReentrantLock

void ReentrantLock::lock() {
    uintptr_t me = detail::currentThreadToken();
    Thread* t = Thread::currentThread();
    std::unique_lock<std::mutex> lk(mu);
    if (owner_ == me) {
        holds_++;
        return;
    }
    waiting_++;
    while (owner_ != 0) cv_.wait(lk);
    waiting_--;
    owner_ = me;
    holds_ = 1;
    ownerThread_ = t;
}

void ReentrantLock::lockInterruptibly() {
    detail::throwIfInterrupted();
    uintptr_t me = detail::currentThreadToken();
    Thread* t = Thread::currentThread();
    std::unique_lock<std::mutex> lk(mu);
    if (owner_ == me) {
        holds_++;
        return;
    }
    waiting_++;
    try {
        detail::awaitInterruptibly(lk, cv_, &OwnedLock::wake, static_cast<OwnedLock*>(this), false, 0,
                                   [&] { return owner_ == 0; });
    } catch (...) {
        waiting_--;
        throw;
    }
    waiting_--;
    owner_ = me;
    holds_ = 1;
    ownerThread_ = t;
}

bool ReentrantLock::tryLock() {
    uintptr_t me = detail::currentThreadToken();
    Thread* t = Thread::currentThread();
    std::lock_guard<std::mutex> g(mu);
    if (owner_ == me) {
        holds_++;
        return true;
    }
    if (owner_ != 0) return false;
    owner_ = me;
    holds_ = 1;
    ownerThread_ = t;
    return true;
}

bool ReentrantLock::tryLock(int64_t timeout, TimeUnit unit) {
    detail::throwIfInterrupted();
    uintptr_t me = detail::currentThreadToken();
    Thread* t = Thread::currentThread();
    std::unique_lock<std::mutex> lk(mu);
    if (owner_ == me) {
        holds_++;
        return true;
    }
    waiting_++;
    bool ok;
    try {
        ok = detail::awaitInterruptibly(lk, cv_, &OwnedLock::wake, static_cast<OwnedLock*>(this), true,
                                        unit.toNanos(timeout), [&] { return owner_ == 0; });
    } catch (...) {
        waiting_--;
        throw;
    }
    waiting_--;
    if (!ok) return false;
    owner_ = me;
    holds_ = 1;
    ownerThread_ = t;
    return true;
}

void ReentrantLock::unlock() {
    std::lock_guard<std::mutex> g(mu);
    if (owner_ != detail::currentThreadToken()) throw IllegalMonitorStateException();
    if (--holds_ == 0) {
        owner_ = 0;
        ownerThread_ = nullptr;
        if (waiting_ > 0) cv_.notify_one();
    }
}

Condition* ReentrantLock::newCondition() { return new detail::ConditionObject(this); }

int32_t ReentrantLock::getHoldCount() {
    std::lock_guard<std::mutex> g(mu);
    return owner_ == detail::currentThreadToken() ? holds_ : 0;
}

bool ReentrantLock::isHeldByCurrentThread() {
    std::lock_guard<std::mutex> g(mu);
    return owner_ == detail::currentThreadToken();
}

bool ReentrantLock::isLocked() {
    std::lock_guard<std::mutex> g(mu);
    return owner_ != 0;
}

bool ReentrantLock::hasQueuedThreads() {
    std::lock_guard<std::mutex> g(mu);
    return waiting_ > 0;
}

int32_t ReentrantLock::getQueueLength() {
    std::lock_guard<std::mutex> g(mu);
    return waiting_;
}

bool ReentrantLock::hasWaiters(Condition* condition) {
    auto* c = dynamic_cast<detail::ConditionObject*>(condition);
    if (condition == nullptr) throw NullPointerException();
    if (c == nullptr || c->owner() != this) throw IllegalArgumentException(String("not owner"));
    return c->hasWaiters();
}

int32_t ReentrantLock::getWaitQueueLength(Condition* condition) {
    auto* c = dynamic_cast<detail::ConditionObject*>(condition);
    if (condition == nullptr) throw NullPointerException();
    if (c == nullptr || c->owner() != this) throw IllegalArgumentException(String("not owner"));
    return c->waitQueueLength();
}

String ReentrantLock::toString() {
    Thread* o;
    bool locked;
    {
        std::lock_guard<std::mutex> g(mu);
        o = ownerThread_;
        locked = owner_ != 0;
    }
    String state = !locked ? String("[Unlocked]")
                           : (o != nullptr ? str("[Locked by thread ", o->getName(), "]") : String("[Locked]"));
    return str("java.util.concurrent.locks.ReentrantLock@", Integer::toHexString(hashCode()), state);
}

bool ReentrantLock::heldByCurrentLocked() { return owner_ == detail::currentThreadToken(); }

int32_t ReentrantLock::releaseAllLocked() {
    int32_t saved = holds_;
    holds_ = 0;
    owner_ = 0;
    ownerThread_ = nullptr;
    if (waiting_ > 0) cv_.notify_one();
    return saved;
}

void ReentrantLock::reacquireLocked(std::unique_lock<std::mutex>& lk, int32_t holds) {
    uintptr_t me = detail::currentThreadToken();
    waiting_++;
    while (owner_ != 0) cv_.wait(lk);
    waiting_--;
    owner_ = me;
    holds_ = holds;
    ownerThread_ = tl_current;
}

// ---- ReentrantReadWriteLock

ReentrantReadWriteLock::ReentrantReadWriteLock() : ReentrantReadWriteLock(false) {}

ReentrantReadWriteLock::ReentrantReadWriteLock(bool fair) : fair_(fair) {
    sync_ = new detail::RWSync();
    readLock_ = new detail::ReadLockImpl(sync_);
    writeLock_ = new detail::WriteLockImpl(sync_);
}

int32_t ReentrantReadWriteLock::getReadLockCount() {
    std::lock_guard<std::mutex> g(sync_->mu);
    return sync_->totalReads;
}

bool ReentrantReadWriteLock::isWriteLocked() {
    std::lock_guard<std::mutex> g(sync_->mu);
    return sync_->writer != 0;
}

bool ReentrantReadWriteLock::isWriteLockedByCurrentThread() {
    std::lock_guard<std::mutex> g(sync_->mu);
    return sync_->writer == detail::currentThreadToken();
}

int32_t ReentrantReadWriteLock::getWriteHoldCount() {
    std::lock_guard<std::mutex> g(sync_->mu);
    return sync_->writer == detail::currentThreadToken() ? sync_->writeHolds : 0;
}

int32_t ReentrantReadWriteLock::getReadHoldCount() {
    std::lock_guard<std::mutex> g(sync_->mu);
    return sync_->readHoldsOf(detail::currentThreadToken());
}

bool ReentrantReadWriteLock::hasQueuedThreads() { return getQueueLength() > 0; }

int32_t ReentrantReadWriteLock::getQueueLength() {
    std::lock_guard<std::mutex> g(sync_->mu);
    return sync_->waitingReaders + sync_->waitingWriters;
}

String ReentrantReadWriteLock::toString() {
    std::lock_guard<std::mutex> g(sync_->mu);
    return str("java.util.concurrent.locks.ReentrantReadWriteLock@", Integer::toHexString(hashCode()),
               "[Write locks = ", sync_->writeHolds, ", Read locks = ", sync_->totalReads, "]");
}

// ---- CountDownLatch

CountDownLatch::CountDownLatch(int32_t count) : count_(count) {
    if (count < 0) throw IllegalArgumentException(String("count < 0"));
}

void CountDownLatch::await() {
    detail::throwIfInterrupted();
    std::unique_lock<std::mutex> lk(core_.mu);
    detail::awaitInterruptibly(lk, core_.notEmpty, &detail::SyncCore::wake, &core_, false, 0,
                               [&] { return count_ == 0; });
}

bool CountDownLatch::await(int64_t timeout, TimeUnit unit) {
    detail::throwIfInterrupted();
    std::unique_lock<std::mutex> lk(core_.mu);
    return detail::awaitInterruptibly(lk, core_.notEmpty, &detail::SyncCore::wake, &core_, true, unit.toNanos(timeout),
                                      [&] { return count_ == 0; });
}

void CountDownLatch::countDown() {
    std::lock_guard<std::mutex> g(core_.mu);
    if (count_ == 0) return;
    if (--count_ == 0) core_.notEmpty.notify_all();
}

int64_t CountDownLatch::getCount() {
    std::lock_guard<std::mutex> g(core_.mu);
    return count_;
}

String CountDownLatch::toString() {
    return str("java.util.concurrent.CountDownLatch@", Integer::toHexString(hashCode()), "[Count = ", getCount(), "]");
}

}  // namespace jlang
