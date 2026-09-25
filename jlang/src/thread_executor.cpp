// FutureTask, AbstractExecutorService, ThreadPoolExecutor, ScheduledThreadPoolExecutor
// (ports of the JDK 8 algorithms) and Executors.
#include <jlang/Thread.h>

#include <sched.h>

#include <algorithm>

namespace jlang {

// =======================================================================================
// FutureTask

namespace {
constexpr int32_t FT_NEW = 0;
constexpr int32_t FT_COMPLETING = 1;
constexpr int32_t FT_NORMAL = 2;
constexpr int32_t FT_EXCEPTIONAL = 3;
constexpr int32_t FT_CANCELLED = 4;
constexpr int32_t FT_INTERRUPTING = 5;
constexpr int32_t FT_INTERRUPTED = 6;

Throwable* asThrowable(std::exception& e) {
    if (auto* t = dynamic_cast<Throwable*>(&e)) return t->copyThrowable();
    return new Error(str("C++ exception: ", e.what()));
}
}  // namespace

FutureTask::FutureTask(Callable<Object*>* callable, int) : callable_(callable) {
    if (callable == nullptr) detail::throwNullPointerException();
}

FutureTask::FutureTask(Runnable* runnable, Object* result) : callable_(Executors::callable(runnable, result)) {}

bool FutureTask::isCancelled() { return state_.load() >= FT_CANCELLED; }
bool FutureTask::isDone() { return state_.load() != FT_NEW; }

bool FutureTask::cancel(bool mayInterruptIfRunning) {
    int32_t expected = FT_NEW;
    if (!state_.compare_exchange_strong(expected, mayInterruptIfRunning ? FT_INTERRUPTING : FT_CANCELLED)) return false;
    {
        JFINALLY { finishCompletion(); };
        if (mayInterruptIfRunning) {
            JFINALLY { state_.store(FT_INTERRUPTED); };
            Thread* t = runner_.load();
            if (t != nullptr) t->interrupt();
        }
    }
    return true;
}

Object* FutureTask::report(int32_t s) {
    if (s == FT_NORMAL) return outcome_;
    if (s >= FT_CANCELLED) throw CancellationException();
    throw ExecutionException(dynamic_cast<Throwable*>(outcome_));
}

int32_t FutureTask::awaitDone(bool timed, int64_t nanos) {
    detail::throwIfInterrupted();
    std::unique_lock<std::mutex> lk(core_.mu);
    detail::awaitInterruptibly(lk, core_.notEmpty, &detail::SyncCore::wake, &core_, timed, nanos,
                               [&] { return state_.load() > FT_COMPLETING; });
    return state_.load();
}

Object* FutureTask::get() {
    int32_t s = state_.load();
    if (s <= FT_COMPLETING) s = awaitDone(false, 0);
    return report(s);
}

Object* FutureTask::get(int64_t timeout, TimeUnit unit) {
    if (unit == nullptr) detail::throwNullPointerException();
    int32_t s = state_.load();
    if (s <= FT_COMPLETING && (s = awaitDone(true, unit.toNanos(timeout))) <= FT_COMPLETING) throw TimeoutException();
    return report(s);
}

void FutureTask::set(Object* v) {
    int32_t expected = FT_NEW;
    if (state_.compare_exchange_strong(expected, FT_COMPLETING)) {
        outcome_ = v;
        state_.store(FT_NORMAL);
        finishCompletion();
    }
}

void FutureTask::setException(Throwable* t) {
    int32_t expected = FT_NEW;
    if (state_.compare_exchange_strong(expected, FT_COMPLETING)) {
        outcome_ = t;
        state_.store(FT_EXCEPTIONAL);
        finishCompletion();
    }
}

void FutureTask::finishCompletion() {
    {
        std::lock_guard<std::mutex> g(core_.mu);
        core_.notEmpty.notify_all();
    }
    done();
    callable_ = nullptr;
}

static void handlePossibleCancellationInterrupt(std::atomic<int32_t>& state, int32_t s) {
    if (s == FT_INTERRUPTING)
        while (state.load() == FT_INTERRUPTING) sched_yield();
}

void FutureTask::run() {
    Thread* me = Thread::currentThread();
    Thread* expected = nullptr;
    if (state_.load() != FT_NEW || !runner_.compare_exchange_strong(expected, me)) return;
    JFINALLY {
        runner_.store(nullptr);
        int32_t s = state_.load();
        if (s >= FT_INTERRUPTING) handlePossibleCancellationInterrupt(state_, s);
    };
    Callable<Object*>* c = callable_;
    if (c != nullptr && state_.load() == FT_NEW) {
        Object* result = nullptr;
        bool ran;
        try {
            result = c->call();
            ran = true;
        } catch (std::exception& ex) {
            ran = false;
            setException(asThrowable(ex));
        }
        if (ran) set(result);
    }
}

bool FutureTask::runAndReset() {
    Thread* me = Thread::currentThread();
    Thread* expected = nullptr;
    if (state_.load() != FT_NEW || !runner_.compare_exchange_strong(expected, me)) return false;
    bool ran = false;
    int32_t s = state_.load();
    {
        JFINALLY {
            runner_.store(nullptr);
            s = state_.load();
            if (s >= FT_INTERRUPTING) handlePossibleCancellationInterrupt(state_, s);
        };
        Callable<Object*>* c = callable_;
        if (c != nullptr && s == FT_NEW) {
            try {
                c->call();
                ran = true;
            } catch (std::exception& ex) {
                setException(asThrowable(ex));
            }
        }
    }
    return ran && s == FT_NEW;
}

// =======================================================================================
// AbstractExecutorService

FutureTask* AbstractExecutorService::newTaskFor(Runnable* runnable, Object* value) {
    return new FutureTask(runnable, value);
}

FutureTask* AbstractExecutorService::newTaskFor(Callable<Object*>* callable) { return new FutureTask(callable); }

Future* AbstractExecutorService::submit(Runnable* task) {
    if (task == nullptr) detail::throwNullPointerException();
    FutureTask* f = newTaskFor(task, nullptr);
    execute(f);
    return f;
}

Future* AbstractExecutorService::submit(Runnable* task, Object* result) {
    if (task == nullptr) detail::throwNullPointerException();
    FutureTask* f = newTaskFor(task, result);
    execute(f);
    return f;
}

Future* AbstractExecutorService::submit(Callable<Object*>* task) {
    if (task == nullptr) detail::throwNullPointerException();
    FutureTask* f = newTaskFor(task);
    execute(f);
    return f;
}

// =======================================================================================
// ThreadPoolExecutor

namespace {
constexpr int32_t COUNT_BITS = 29;
constexpr int32_t CAPACITY = (1 << COUNT_BITS) - 1;
constexpr int32_t RUNNING = static_cast<int32_t>(0xE0000000u);  // -1 << 29
constexpr int32_t SHUTDOWN = 0;
constexpr int32_t STOP = 1 << COUNT_BITS;
constexpr int32_t TIDYING = 2 << COUNT_BITS;
constexpr int32_t TERMINATED = 3 << COUNT_BITS;

inline int32_t runStateOf(int32_t c) { return c & ~CAPACITY; }
inline int32_t workerCountOf(int32_t c) { return c & CAPACITY; }
inline int32_t ctlOf(int32_t rs, int32_t wc) { return rs | wc; }
inline bool runStateLessThan(int32_t c, int32_t s) { return c < s; }
inline bool runStateAtLeast(int32_t c, int32_t s) { return c >= s; }
inline bool isRunning(int32_t c) { return c < SHUTDOWN; }

class DefaultThreadFactory final : public virtual ThreadFactory {
public:
    DefaultThreadFactory() {
        group_ = Thread::currentThread()->getThreadGroup();
        namePrefix_ = str("pool-", poolNumber().fetch_add(1), "-thread-");
    }
    Thread* newThread(Runnable* r) override {
        Thread* t = new Thread(group_, r, str(namePrefix_, threadNumber_.fetch_add(1)), 0);
        if (t->isDaemon()) t->setDaemon(false);
        if (t->getPriority() != Thread::NORM_PRIORITY) t->setPriority(Thread::NORM_PRIORITY);
        return t;
    }

private:
    static std::atomic<int32_t>& poolNumber() {
        static std::atomic<int32_t> n{1};
        return n;
    }
    ThreadGroup* group_;
    std::atomic<int32_t> threadNumber_{1};
    String namePrefix_;
};

RejectedExecutionHandler* defaultHandler() {
    static RejectedExecutionHandler* h = new ThreadPoolExecutor_AbortPolicy();
    return h;
}
}  // namespace

namespace detail {
// ThreadPoolExecutor.Worker: a non-reentrant lock (-1 until started, 0 free, 1 running a task).
class TpeWorker final : public virtual Runnable {
public:
    TpeWorker(ThreadPoolExecutor* pool, Runnable* firstTask) : pool_(pool), firstTask(firstTask) {
        state.store(-1);
        thread = pool->getThreadFactory()->newThread(this);
    }
    void run() override { pool_->runWorker(this); }
    void lock() {
        for (;;) {
            int32_t e = 0;
            if (state.compare_exchange_weak(e, 1)) return;
            sched_yield();
        }
    }
    bool tryLock() {
        int32_t e = 0;
        return state.compare_exchange_strong(e, 1);
    }
    void unlock() { state.store(0); }
    bool isLocked() { return state.load() != 0; }
    void interruptIfStarted() {
        Thread* t = thread;
        if (state.load() >= 0 && t != nullptr && !t->isInterrupted()) t->interrupt();
    }

    ThreadPoolExecutor* pool_;
    Thread* thread = nullptr;
    Runnable* firstTask;
    std::atomic<int64_t> completedTasks{0};
    std::atomic<int32_t> state{-1};
};
}  // namespace detail

void ThreadPoolExecutor_AbortPolicy::rejectedExecution(Runnable* r, ThreadPoolExecutor* e) {
    throw RejectedExecutionException(str("Task ", r, " rejected from ", e));
}

void ThreadPoolExecutor_CallerRunsPolicy::rejectedExecution(Runnable* r, ThreadPoolExecutor* e) {
    if (!e->isShutdown()) r->run();
}

void ThreadPoolExecutor_DiscardPolicy::rejectedExecution(Runnable* r, ThreadPoolExecutor* e) {
    (void)r;
    (void)e;
}

void ThreadPoolExecutor_DiscardOldestPolicy::rejectedExecution(Runnable* r, ThreadPoolExecutor* e) {
    if (!e->isShutdown()) {
        e->getQueue()->poll();
        e->execute(r);
    }
}

ThreadPoolExecutor::ThreadPoolExecutor(int32_t corePoolSize, int32_t maximumPoolSize, int64_t keepAliveTime,
                                       TimeUnit unit, BlockingQueue<Runnable*>* workQueue)
    : ThreadPoolExecutor(corePoolSize, maximumPoolSize, keepAliveTime, unit, workQueue,
                         Executors::defaultThreadFactory(), defaultHandler()) {}

ThreadPoolExecutor::ThreadPoolExecutor(int32_t corePoolSize, int32_t maximumPoolSize, int64_t keepAliveTime,
                                       TimeUnit unit, BlockingQueue<Runnable*>* workQueue, ThreadFactory* threadFactory)
    : ThreadPoolExecutor(corePoolSize, maximumPoolSize, keepAliveTime, unit, workQueue, threadFactory,
                         defaultHandler()) {}

ThreadPoolExecutor::ThreadPoolExecutor(int32_t corePoolSize, int32_t maximumPoolSize, int64_t keepAliveTime,
                                       TimeUnit unit, BlockingQueue<Runnable*>* workQueue,
                                       RejectedExecutionHandler* handler)
    : ThreadPoolExecutor(corePoolSize, maximumPoolSize, keepAliveTime, unit, workQueue,
                         Executors::defaultThreadFactory(), handler) {}

ThreadPoolExecutor::ThreadPoolExecutor(int32_t corePoolSize, int32_t maximumPoolSize, int64_t keepAliveTime,
                                       TimeUnit unit, BlockingQueue<Runnable*>* workQueue,
                                       ThreadFactory* threadFactory, RejectedExecutionHandler* handler)
    : ctl_(ctlOf(RUNNING, 0)) {
    if (corePoolSize < 0 || maximumPoolSize <= 0 || maximumPoolSize < corePoolSize || keepAliveTime < 0)
        throw IllegalArgumentException();
    if (workQueue == nullptr || threadFactory == nullptr || handler == nullptr || unit == nullptr)
        throw NullPointerException();
    corePoolSize_.store(corePoolSize);
    maximumPoolSize_.store(maximumPoolSize);
    workQueue_ = workQueue;
    keepAliveTime_.store(unit.toNanos(keepAliveTime));
    threadFactory_.store(threadFactory);
    handler_.store(handler);
    mainLock_ = new ReentrantLock();
    termination_ = mainLock_->newCondition();
    workers_ = new std::vector<detail::TpeWorker*>();
}

bool ThreadPoolExecutor::compareAndIncrementWorkerCount(int32_t expect) {
    return ctl_.compare_exchange_strong(expect, expect + 1);
}
bool ThreadPoolExecutor::compareAndDecrementWorkerCount(int32_t expect) {
    return ctl_.compare_exchange_strong(expect, expect - 1);
}
void ThreadPoolExecutor::decrementWorkerCount() {
    while (!compareAndDecrementWorkerCount(ctl_.load())) {
    }
}

void ThreadPoolExecutor::advanceRunState(int32_t targetState) {
    for (;;) {
        int32_t c = ctl_.load();
        if (runStateAtLeast(c, targetState) || ctl_.compare_exchange_strong(c, ctlOf(targetState, workerCountOf(c))))
            break;
    }
}

void ThreadPoolExecutor::tryTerminate() {
    for (;;) {
        int32_t c = ctl_.load();
        if (isRunning(c) || runStateAtLeast(c, TIDYING) || (runStateOf(c) == SHUTDOWN && !workQueue_->isEmpty())) return;
        if (workerCountOf(c) != 0) {
            interruptIdleWorkers(true);
            return;
        }
        mainLock_->lock();
        JFINALLY { mainLock_->unlock(); };
        if (ctl_.compare_exchange_strong(c, ctlOf(TIDYING, 0))) {
            JFINALLY {
                ctl_.store(ctlOf(TERMINATED, 0));
                termination_->signalAll();
            };
            terminated();
            return;
        }
    }
}

void ThreadPoolExecutor::interruptWorkers() {
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    for (detail::TpeWorker* w : *workers_) w->interruptIfStarted();
}

void ThreadPoolExecutor::interruptIdleWorkers(bool onlyOne) {
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    for (detail::TpeWorker* w : *workers_) {
        Thread* t = w->thread;
        if (t != nullptr && !t->isInterrupted() && w->tryLock()) {
            JFINALLY { w->unlock(); };
            t->interrupt();
        }
        if (onlyOne) break;
    }
}

void ThreadPoolExecutor::reject(Runnable* command) { handler_.load()->rejectedExecution(command, this); }

bool ThreadPoolExecutor::isRunningOrShutdown(bool shutdownOK) {
    int32_t rs = runStateOf(ctl_.load());
    return rs == RUNNING || (rs == SHUTDOWN && shutdownOK);
}

List<Runnable*>* ThreadPoolExecutor::drainQueue() {
    auto* taskList = new List<Runnable*>();
    workQueue_->drainTo(taskList);
    if (!workQueue_->isEmpty()) {
        for (Runnable* r : workQueue_->_snapshot()) {
            if (workQueue_->removeObject(r)) taskList->add(r);
        }
    }
    return taskList;
}

bool ThreadPoolExecutor::addWorker(Runnable* firstTask, bool core) {
    for (;;) {
        int32_t c = ctl_.load();
        int32_t rs = runStateOf(c);
        if (rs >= SHUTDOWN && !(rs == SHUTDOWN && firstTask == nullptr && !workQueue_->isEmpty())) return false;
        bool retryOuter = false;
        for (;;) {
            int32_t wc = workerCountOf(c);
            if (wc >= CAPACITY || wc >= (core ? corePoolSize_.load() : maximumPoolSize_.load())) return false;
            if (compareAndIncrementWorkerCount(c)) goto counted;
            c = ctl_.load();
            if (runStateOf(c) != rs) {
                retryOuter = true;
                break;
            }
        }
        if (retryOuter) continue;
    }
counted:
    bool workerStarted = false;
    bool workerAdded = false;
    detail::TpeWorker* w = nullptr;
    {
        JFINALLY {
            if (!workerStarted) addWorkerFailed(w);
        };
        w = new detail::TpeWorker(this, firstTask);
        Thread* t = w->thread;
        if (t != nullptr) {
            {
                mainLock_->lock();
                JFINALLY { mainLock_->unlock(); };
                int32_t rs = runStateOf(ctl_.load());
                if (rs < SHUTDOWN || (rs == SHUTDOWN && firstTask == nullptr)) {
                    if (t->isAlive()) throw IllegalThreadStateException();
                    workers_->push_back(w);
                    int32_t s = static_cast<int32_t>(workers_->size());
                    if (s > largestPoolSize_) largestPoolSize_ = s;
                    workerAdded = true;
                }
            }
            if (workerAdded) {
                t->start();
                workerStarted = true;
            }
        }
    }
    return workerStarted;
}

void ThreadPoolExecutor::addWorkerFailed(detail::TpeWorker* w) {
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    if (w != nullptr) {
        auto it = std::find(workers_->begin(), workers_->end(), w);
        if (it != workers_->end()) workers_->erase(it);
    }
    decrementWorkerCount();
    tryTerminate();
}

void ThreadPoolExecutor::processWorkerExit(detail::TpeWorker* w, bool completedAbruptly) {
    if (completedAbruptly) decrementWorkerCount();
    {
        mainLock_->lock();
        JFINALLY { mainLock_->unlock(); };
        completedTaskCount_ += w->completedTasks.load();
        auto it = std::find(workers_->begin(), workers_->end(), w);
        if (it != workers_->end()) workers_->erase(it);
    }
    tryTerminate();
    int32_t c = ctl_.load();
    if (runStateLessThan(c, STOP)) {
        if (!completedAbruptly) {
            int32_t min = allowCoreThreadTimeOut_.load() ? 0 : corePoolSize_.load();
            if (min == 0 && !workQueue_->isEmpty()) min = 1;
            if (workerCountOf(c) >= min) return;
        }
        addWorker(nullptr, false);
    }
}

Runnable* ThreadPoolExecutor::getTask() {
    bool timedOut = false;
    for (;;) {
        int32_t c = ctl_.load();
        int32_t rs = runStateOf(c);
        if (rs >= SHUTDOWN && (rs >= STOP || workQueue_->isEmpty())) {
            decrementWorkerCount();
            return nullptr;
        }
        int32_t wc = workerCountOf(c);
        bool timed = allowCoreThreadTimeOut_.load() || wc > corePoolSize_.load();
        if ((wc > maximumPoolSize_.load() || (timed && timedOut)) && (wc > 1 || workQueue_->isEmpty())) {
            if (compareAndDecrementWorkerCount(c)) return nullptr;
            continue;
        }
        try {
            Runnable* r = timed ? workQueue_->poll(keepAliveTime_.load(), TimeUnit::NANOSECONDS) : workQueue_->take();
            if (r != nullptr) return r;
            timedOut = true;
        } catch (InterruptedException&) {
            timedOut = false;
        }
    }
}

void ThreadPoolExecutor::runWorker(detail::TpeWorker* w) {
    Thread* wt = Thread::currentThread();
    Runnable* task = w->firstTask;
    w->firstTask = nullptr;
    w->unlock();  // allow interrupts
    bool completedAbruptly = true;
    {
        JFINALLY { processWorkerExit(w, completedAbruptly); };
        while (task != nullptr || (task = getTask()) != nullptr) {
            w->lock();
            // If the pool is stopping, the thread must be interrupted; otherwise it must not be.
            if ((runStateAtLeast(ctl_.load(), STOP) || (Thread::interrupted() && runStateAtLeast(ctl_.load(), STOP))) &&
                !wt->isInterrupted())
                wt->interrupt();
            {
                JFINALLY {
                    task = nullptr;
                    w->completedTasks++;
                    w->unlock();
                };
                beforeExecute(wt, task);
                try {
                    task->run();
                } catch (Throwable& x) {
                    afterExecute(task, x.copyThrowable());
                    throw;
                } catch (std::exception& x) {
                    afterExecute(task, asThrowable(x));
                    throw;
                }
                afterExecute(task, nullptr);
            }
        }
        completedAbruptly = false;
    }
}

void ThreadPoolExecutor::execute(Runnable* command) {
    if (command == nullptr) throw NullPointerException();
    int32_t c = ctl_.load();
    if (workerCountOf(c) < corePoolSize_.load()) {
        if (addWorker(command, true)) return;
        c = ctl_.load();
    }
    if (isRunning(c) && workQueue_->offer(command)) {
        int32_t recheck = ctl_.load();
        if (!isRunning(recheck) && remove(command)) reject(command);
        else if (workerCountOf(recheck) == 0) addWorker(nullptr, false);
    } else if (!addWorker(command, false)) {
        reject(command);
    }
}

void ThreadPoolExecutor::shutdown() {
    {
        mainLock_->lock();
        JFINALLY { mainLock_->unlock(); };
        advanceRunState(SHUTDOWN);
        interruptIdleWorkers(false);
        onShutdown();
    }
    tryTerminate();
}

List<Runnable*>* ThreadPoolExecutor::shutdownNow() {
    List<Runnable*>* tasks;
    {
        mainLock_->lock();
        JFINALLY { mainLock_->unlock(); };
        advanceRunState(STOP);
        interruptWorkers();
        tasks = drainQueue();
    }
    tryTerminate();
    return tasks;
}

bool ThreadPoolExecutor::isShutdown() { return !isRunning(ctl_.load()); }

bool ThreadPoolExecutor::isTerminating() {
    int32_t c = ctl_.load();
    return !isRunning(c) && runStateLessThan(c, TERMINATED);
}

bool ThreadPoolExecutor::isTerminated() { return runStateAtLeast(ctl_.load(), TERMINATED); }

bool ThreadPoolExecutor::awaitTermination(int64_t timeout, TimeUnit unit) {
    int64_t nanos = unit.toNanos(timeout);
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    for (;;) {
        if (runStateAtLeast(ctl_.load(), TERMINATED)) return true;
        if (nanos <= 0) return false;
        nanos = termination_->awaitNanos(nanos);
        if (nanos <= 0 && runStateAtLeast(ctl_.load(), TERMINATED)) return true;
    }
}

void ThreadPoolExecutor::setThreadFactory(ThreadFactory* threadFactory) {
    if (threadFactory == nullptr) throw NullPointerException();
    threadFactory_.store(threadFactory);
}

void ThreadPoolExecutor::setRejectedExecutionHandler(RejectedExecutionHandler* handler) {
    if (handler == nullptr) throw NullPointerException();
    handler_.store(handler);
}

void ThreadPoolExecutor::setCorePoolSize(int32_t corePoolSize) {
    if (corePoolSize < 0) throw IllegalArgumentException();
    int32_t delta = corePoolSize - corePoolSize_.load();
    corePoolSize_.store(corePoolSize);
    if (workerCountOf(ctl_.load()) > corePoolSize) {
        interruptIdleWorkers(false);
    } else if (delta > 0) {
        int32_t k = std::min(delta, workQueue_->size());
        while (k-- > 0 && addWorker(nullptr, true)) {
            if (workQueue_->isEmpty()) break;
        }
    }
}

bool ThreadPoolExecutor::prestartCoreThread() {
    return workerCountOf(ctl_.load()) < corePoolSize_.load() && addWorker(nullptr, true);
}

void ThreadPoolExecutor::ensurePrestart() {
    int32_t wc = workerCountOf(ctl_.load());
    if (wc < corePoolSize_.load()) addWorker(nullptr, true);
    else if (wc == 0) addWorker(nullptr, false);
}

int32_t ThreadPoolExecutor::prestartAllCoreThreads() {
    int32_t n = 0;
    while (addWorker(nullptr, true)) ++n;
    return n;
}

void ThreadPoolExecutor::allowCoreThreadTimeOut(bool value) {
    if (value && keepAliveTime_.load() <= 0)
        throw IllegalArgumentException(String("Core threads must have nonzero keep alive times"));
    if (value != allowCoreThreadTimeOut_.load()) {
        allowCoreThreadTimeOut_.store(value);
        if (value) interruptIdleWorkers(false);
    }
}

void ThreadPoolExecutor::setMaximumPoolSize(int32_t maximumPoolSize) {
    if (maximumPoolSize <= 0 || maximumPoolSize < corePoolSize_.load()) throw IllegalArgumentException();
    maximumPoolSize_.store(maximumPoolSize);
    if (workerCountOf(ctl_.load()) > maximumPoolSize) interruptIdleWorkers(false);
}

void ThreadPoolExecutor::setKeepAliveTime(int64_t time, TimeUnit unit) {
    if (time < 0) throw IllegalArgumentException();
    if (time == 0 && allowsCoreThreadTimeOut())
        throw IllegalArgumentException(String("Core threads must have nonzero keep alive times"));
    int64_t keepAliveTime = unit.toNanos(time);
    int64_t delta = keepAliveTime - keepAliveTime_.load();
    keepAliveTime_.store(keepAliveTime);
    if (delta < 0) interruptIdleWorkers(false);
}

int64_t ThreadPoolExecutor::getKeepAliveTime(TimeUnit unit) {
    return unit.convert(keepAliveTime_.load(), TimeUnit::NANOSECONDS);
}

bool ThreadPoolExecutor::remove(Runnable* task) {
    bool removed = workQueue_->removeObject(task);
    tryTerminate();
    return removed;
}

void ThreadPoolExecutor::purge() {
    for (Runnable* r : workQueue_->_snapshot()) {
        auto* f = dynamic_cast<Future*>(r);
        if (f != nullptr && f->isCancelled()) workQueue_->removeObject(r);
    }
    tryTerminate();
}

int32_t ThreadPoolExecutor::getPoolSize() {
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    return runStateAtLeast(ctl_.load(), TIDYING) ? 0 : static_cast<int32_t>(workers_->size());
}

int32_t ThreadPoolExecutor::getActiveCount() {
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    int32_t n = 0;
    for (detail::TpeWorker* w : *workers_)
        if (w->isLocked()) ++n;
    return n;
}

int32_t ThreadPoolExecutor::getLargestPoolSize() {
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    return largestPoolSize_;
}

int64_t ThreadPoolExecutor::getTaskCount() {
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    int64_t n = completedTaskCount_;
    for (detail::TpeWorker* w : *workers_) {
        n += w->completedTasks.load();
        if (w->isLocked()) ++n;
    }
    return n + workQueue_->size();
}

int64_t ThreadPoolExecutor::getCompletedTaskCount() {
    mainLock_->lock();
    JFINALLY { mainLock_->unlock(); };
    int64_t n = completedTaskCount_;
    for (detail::TpeWorker* w : *workers_) n += w->completedTasks.load();
    return n;
}

String ThreadPoolExecutor::toString() {
    int64_t ncompleted;
    int32_t nworkers, nactive;
    {
        mainLock_->lock();
        JFINALLY { mainLock_->unlock(); };
        ncompleted = completedTaskCount_;
        nactive = 0;
        nworkers = static_cast<int32_t>(workers_->size());
        for (detail::TpeWorker* w : *workers_) {
            ncompleted += w->completedTasks.load();
            if (w->isLocked()) ++nactive;
        }
    }
    int32_t c = ctl_.load();
    const char* rs = runStateLessThan(c, SHUTDOWN) ? "Running" : (runStateAtLeast(c, TERMINATED) ? "Terminated" : "Shutting down");
    return str(javaClassName(), "@", Integer::toHexString(hashCode()), "[", rs, ", pool size = ", nworkers,
               ", active threads = ", nactive, ", queued tasks = ", workQueue_->size(), ", completed tasks = ",
               ncompleted, "]");
}

// =======================================================================================
// ScheduledThreadPoolExecutor

namespace {

std::atomic<int64_t> g_sequencer{0};

class ScheduledFutureTask final : public FutureTask, public virtual RunnableScheduledFuture {
public:
    ScheduledFutureTask(ScheduledThreadPoolExecutor* ex, Runnable* r, Object* result, int64_t ns)
        : FutureTask(r, result), ex_(ex), time_(ns), period_(0), seq_(g_sequencer.fetch_add(1)) {}
    ScheduledFutureTask(ScheduledThreadPoolExecutor* ex, Runnable* r, Object* result, int64_t ns, int64_t period)
        : FutureTask(r, result), ex_(ex), time_(ns), period_(period), seq_(g_sequencer.fetch_add(1)) {}
    ScheduledFutureTask(ScheduledThreadPoolExecutor* ex, Callable<Object*>* callable, int64_t ns)
        : FutureTask(callable), ex_(ex), time_(ns), period_(0), seq_(g_sequencer.fetch_add(1)) {}

    int64_t getDelay(TimeUnit unit) override {
        return unit.convert(time_.load() - System::nanoTime(), TimeUnit::NANOSECONDS);
    }
    int32_t compareTo(Delayed* other) override {
        if (other == static_cast<Delayed*>(this)) return 0;
        if (auto* x = dynamic_cast<ScheduledFutureTask*>(other)) {
            int64_t diff = time_.load() - x->time_.load();
            if (diff < 0) return -1;
            if (diff > 0) return 1;
            if (seq_ < x->seq_) return -1;
            return 1;
        }
        if (other == nullptr) detail::throwNullPointerException();
        int64_t diff = getDelay(TimeUnit::NANOSECONDS) - other->getDelay(TimeUnit::NANOSECONDS);
        return (diff < 0) ? -1 : (diff > 0) ? 1 : 0;
    }
    bool isPeriodic() override { return period_ != 0; }
    bool cancel(bool mayInterruptIfRunning) override {
        bool cancelled = FutureTask::cancel(mayInterruptIfRunning);
        if (cancelled && ex_->getRemoveOnCancelPolicy() && heapIndex.load() >= 0) ex_->removeTask(this);
        return cancelled;
    }
    void run() override {
        bool periodic = isPeriodic();
        if (!ex_->canRunInCurrentRunState(periodic)) {
            cancel(false);
        } else if (!periodic) {
            FutureTask::run();
        } else if (FutureTask::runAndReset()) {
            setNextRunTime();
            ex_->reExecutePeriodic(outerTask);
        }
    }
    String toString() override {
        return str("java.util.concurrent.ScheduledThreadPoolExecutor$ScheduledFutureTask@", Integer::toHexString(hashCode()));
    }

    ScheduledThreadPoolExecutor* ex_;
    std::atomic<int64_t> time_;
    const int64_t period_;
    const int64_t seq_;
    RunnableScheduledFuture* outerTask = this;
    std::atomic<int32_t> heapIndex{-1};

private:
    void setNextRunTime() {
        int64_t p = period_;
        if (p > 0) time_.fetch_add(p);
        else time_.store(ex_->triggerTime(-p));
    }
};

// ScheduledThreadPoolExecutor.DelayedWorkQueue: a heap of RunnableScheduledFutures with the
// leader/follower waiting scheme.
class DelayedWorkQueue final : public virtual BlockingQueue<Runnable*> {
public:
    using BlockingQueue<Runnable*>::remove;
    using BlockingQueue<Runnable*>::drainTo;

    bool offer(Runnable* const& x) override {
        if (x == nullptr) detail::throwNullPointer();
        RunnableScheduledFuture* e = jlang::cast<RunnableScheduledFuture>(x);
        std::lock_guard<std::mutex> g(core_.mu);
        size_t i = q_.size();
        q_.push_back(e);
        if (i == 0) setIndex(e, 0);
        else siftUp(i, e);
        if (q_[0] == e) {
            leader_ = nullptr;
            core_.notEmpty.notify_all();
        }
        return true;
    }
    void put(Runnable* const& e) override { offer(e); }
    bool add(Runnable* const& e) override { return offer(e); }
    bool offer(Runnable* const& e, int64_t timeout, TimeUnit unit) override {
        (void)timeout;
        (void)unit;
        return offer(e);
    }
    Runnable* poll() override {
        std::lock_guard<std::mutex> g(core_.mu);
        if (q_.empty()) return nullptr;
        RunnableScheduledFuture* first = q_[0];
        if (first->getDelay(TimeUnit::NANOSECONDS) > 0) return nullptr;
        return finishPoll(first);
    }
    Runnable* take() override { return takeImpl(false, 0); }
    Runnable* poll(int64_t timeout, TimeUnit unit) override { return takeImpl(true, unit.toNanos(timeout)); }
    Runnable* peek() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return q_.empty() ? nullptr : static_cast<Runnable*>(q_[0]);
    }
    int32_t size() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return static_cast<int32_t>(q_.size());
    }
    bool isEmpty() override { return size() == 0; }
    int32_t remainingCapacity() override { return INT32_MAX; }
    bool contains(Runnable* const& x) override {
        std::lock_guard<std::mutex> g(core_.mu);
        return indexOf(x) != -1;
    }
    bool removeObject(Runnable* const& x) override {
        std::lock_guard<std::mutex> g(core_.mu);
        int32_t i = indexOf(x);
        if (i < 0) return false;
        setIndex(q_[static_cast<size_t>(i)], -1);
        size_t s = q_.size() - 1;
        RunnableScheduledFuture* replacement = q_[s];
        q_.pop_back();
        if (s != static_cast<size_t>(i)) {
            siftDown(static_cast<size_t>(i), replacement);
            if (q_[static_cast<size_t>(i)] == replacement) siftUp(static_cast<size_t>(i), replacement);
        }
        return true;
    }
    void clear() override {
        std::lock_guard<std::mutex> g(core_.mu);
        for (auto* t : q_) setIndex(t, -1);
        q_.clear();
    }
    int32_t drainTo(Collection<Runnable*>* c, int32_t maxElements) override {
        checkDrainTarget(c);
        if (maxElements <= 0) return 0;
        detail::Vec<Runnable*> taken;
        {
            std::lock_guard<std::mutex> g(core_.mu);
            while (static_cast<int32_t>(taken.size()) < maxElements && !q_.empty() &&
                   q_[0]->getDelay(TimeUnit::NANOSECONDS) <= 0)
                taken.push_back(finishPoll(q_[0]));
        }
        for (Runnable* r : taken) c->add(r);
        return static_cast<int32_t>(taken.size());
    }
    Iterator<Runnable*>* iterator() override {
        return new detail::SnapshotIterator<Runnable*>(this, this->_snapshot());
    }
    detail::Vec<Runnable*> _snapshot() override {
        std::lock_guard<std::mutex> g(core_.mu);
        return detail::Vec<Runnable*>(q_.begin(), q_.end());
    }
    void _removeIterated(Runnable* const& e, int32_t hint) override {
        (void)hint;
        removeObject(e);
    }

private:
    static void setIndex(RunnableScheduledFuture* f, int32_t idx) {
        if (auto* t = dynamic_cast<ScheduledFutureTask*>(f)) t->heapIndex.store(idx);
    }
    int32_t indexOf(Runnable* x) {
        if (x == nullptr) return -1;
        if (auto* t = dynamic_cast<ScheduledFutureTask*>(x)) {
            int32_t i = t->heapIndex.load();
            if (i >= 0 && static_cast<size_t>(i) < q_.size() && static_cast<Runnable*>(q_[static_cast<size_t>(i)]) == x)
                return i;
        } else {
            for (size_t i = 0; i < q_.size(); i++)
                if (detail::javaEquals<Runnable*>(x, q_[i])) return static_cast<int32_t>(i);
        }
        return -1;
    }
    RunnableScheduledFuture* finishPoll(RunnableScheduledFuture* f) {
        size_t s = q_.size() - 1;
        RunnableScheduledFuture* x = q_[s];
        q_.pop_back();
        if (s != 0) siftDown(0, x);
        setIndex(f, -1);
        return f;
    }
    void siftUp(size_t k, RunnableScheduledFuture* key) {
        while (k > 0) {
            size_t parent = (k - 1) >> 1;
            RunnableScheduledFuture* e = q_[parent];
            if (key->compareTo(e) >= 0) break;
            q_[k] = e;
            setIndex(e, static_cast<int32_t>(k));
            k = parent;
        }
        q_[k] = key;
        setIndex(key, static_cast<int32_t>(k));
    }
    void siftDown(size_t k, RunnableScheduledFuture* key) {
        size_t n = q_.size();
        size_t half = n >> 1;
        while (k < half) {
            size_t child = (k << 1) + 1;
            RunnableScheduledFuture* c = q_[child];
            size_t right = child + 1;
            if (right < n && c->compareTo(q_[right]) > 0) c = q_[child = right];
            if (key->compareTo(c) <= 0) break;
            q_[k] = c;
            setIndex(c, static_cast<int32_t>(k));
            k = child;
        }
        q_[k] = key;
        setIndex(key, static_cast<int32_t>(k));
    }

    // take() / poll(timeout) (timed): Java's leader/follower algorithm.
    Runnable* takeImpl(bool timed, int64_t nanos) {
        detail::throwIfInterrupted();
        Thread* thisThread = Thread::currentThread();
        sync::InterruptState* st = sync::current();
        std::unique_lock<std::mutex> lk(core_.mu);
        detail::BlockerScope blocker(&detail::SyncCore::wake, &core_);
        struct Finally {
            DelayedWorkQueue* q;
            ~Finally() {
                if (q->leader_ == nullptr && !q->q_.empty()) q->core_.notEmpty.notify_one();
            }
        } fin{this};
        const auto start = std::chrono::steady_clock::now();
        auto remaining = [&]() -> int64_t {
            if (!timed) return INT64_MAX;
            int64_t el = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
            return nanos - el;
        };
        auto waitFor = [&](int64_t ns) {
            if (sync::isInterrupted(st)) {
                sync::clearInterrupt(st);
                throw InterruptedException();
            }
            constexpr int64_t kMaxWait = INT64_C(86400000000000);  // re-check at least daily
            if (ns >= kMaxWait) ns = kMaxWait;
            if (ns <= 0) return;
            core_.notEmpty.wait_for(lk, std::chrono::nanoseconds(ns));
        };
        for (;;) {
            RunnableScheduledFuture* first = q_.empty() ? nullptr : q_[0];
            if (first == nullptr) {
                if (timed) {
                    int64_t r = remaining();
                    if (r <= 0) return nullptr;
                    waitFor(r);
                } else {
                    waitFor(INT64_MAX);
                }
            } else {
                int64_t delay = first->getDelay(TimeUnit::NANOSECONDS);
                if (delay <= 0) return finishPoll(first);
                first = nullptr;
                int64_t r = remaining();
                if (timed && r <= 0) return nullptr;
                if (leader_ != nullptr || (timed && r < delay)) {
                    waitFor(timed ? r : INT64_MAX);
                } else {
                    leader_ = thisThread;
                    try {
                        waitFor(delay);
                    } catch (...) {
                        if (leader_ == thisThread) leader_ = nullptr;
                        throw;
                    }
                    if (leader_ == thisThread) leader_ = nullptr;
                }
            }
        }
    }

    detail::SyncCore core_;
    std::vector<RunnableScheduledFuture*> q_;
    Thread* leader_ = nullptr;
};

}  // namespace

ScheduledThreadPoolExecutor::ScheduledThreadPoolExecutor(int32_t corePoolSize)
    : ThreadPoolExecutor(corePoolSize, INT32_MAX, 0, TimeUnit::NANOSECONDS, new DelayedWorkQueue()) {}

ScheduledThreadPoolExecutor::ScheduledThreadPoolExecutor(int32_t corePoolSize, ThreadFactory* threadFactory)
    : ThreadPoolExecutor(corePoolSize, INT32_MAX, 0, TimeUnit::NANOSECONDS, new DelayedWorkQueue(), threadFactory) {}

ScheduledThreadPoolExecutor::ScheduledThreadPoolExecutor(int32_t corePoolSize, RejectedExecutionHandler* handler)
    : ThreadPoolExecutor(corePoolSize, INT32_MAX, 0, TimeUnit::NANOSECONDS, new DelayedWorkQueue(), handler) {}

ScheduledThreadPoolExecutor::ScheduledThreadPoolExecutor(int32_t corePoolSize, ThreadFactory* threadFactory,
                                                         RejectedExecutionHandler* handler)
    : ThreadPoolExecutor(corePoolSize, INT32_MAX, 0, TimeUnit::NANOSECONDS, new DelayedWorkQueue(), threadFactory,
                         handler) {}

bool ScheduledThreadPoolExecutor::canRunInCurrentRunState(bool periodic) {
    return isRunningOrShutdown(periodic ? continueExistingPeriodic_.load() : executeExistingDelayed_.load());
}

void ScheduledThreadPoolExecutor::delayedExecute(RunnableScheduledFuture* task) {
    if (isShutdown()) {
        reject(task);
    } else {
        getQueue()->add(task);
        if (isShutdown() && !canRunInCurrentRunState(task->isPeriodic()) && remove(task)) task->cancel(false);
        else ensurePrestart();
    }
}

void ScheduledThreadPoolExecutor::reExecutePeriodic(RunnableScheduledFuture* task) {
    if (canRunInCurrentRunState(true)) {
        getQueue()->add(task);
        if (!canRunInCurrentRunState(true) && remove(task)) task->cancel(false);
        else ensurePrestart();
    }
}

void ScheduledThreadPoolExecutor::onShutdown() {
    BlockingQueue<Runnable*>* q = getQueue();
    bool keepDelayed = getExecuteExistingDelayedTasksAfterShutdownPolicy();
    bool keepPeriodic = getContinueExistingPeriodicTasksAfterShutdownPolicy();
    if (!keepDelayed && !keepPeriodic) {
        for (Runnable* e : q->_snapshot())
            if (auto* t = dynamic_cast<RunnableScheduledFuture*>(e)) t->cancel(false);
        q->clear();
    } else {
        for (Runnable* e : q->_snapshot()) {
            if (auto* t = dynamic_cast<RunnableScheduledFuture*>(e)) {
                if ((t->isPeriodic() ? !keepPeriodic : !keepDelayed) || t->isCancelled()) {
                    if (q->removeObject(e)) t->cancel(false);
                }
            }
        }
    }
    tryTerminate();
}

int64_t ScheduledThreadPoolExecutor::triggerTime(int64_t delay, TimeUnit unit) {
    return triggerTime(unit.toNanos((delay < 0) ? 0 : delay));
}

int64_t ScheduledThreadPoolExecutor::triggerTime(int64_t delay) {
    return System::nanoTime() + ((delay < (INT64_MAX >> 1)) ? delay : overflowFree(delay));
}

int64_t ScheduledThreadPoolExecutor::overflowFree(int64_t delay) {
    auto* head = dynamic_cast<Delayed*>(getQueue()->peek());
    if (head != nullptr) {
        int64_t headDelay = head->getDelay(TimeUnit::NANOSECONDS);
        if (headDelay < 0 && (delay - headDelay < 0)) delay = INT64_MAX + headDelay;
    }
    return delay;
}

ScheduledFuture* ScheduledThreadPoolExecutor::schedule(Runnable* command, int64_t delay, TimeUnit unit) {
    if (command == nullptr || unit == nullptr) throw NullPointerException();
    RunnableScheduledFuture* t =
        decorateTask(command, new ScheduledFutureTask(this, command, nullptr, triggerTime(delay, unit)));
    delayedExecute(t);
    return t;
}

ScheduledFuture* ScheduledThreadPoolExecutor::schedule(Callable<Object*>* callable, int64_t delay, TimeUnit unit) {
    if (callable == nullptr || unit == nullptr) throw NullPointerException();
    RunnableScheduledFuture* t =
        decorateTask(callable, new ScheduledFutureTask(this, callable, triggerTime(delay, unit)));
    delayedExecute(t);
    return t;
}

ScheduledFuture* ScheduledThreadPoolExecutor::scheduleAtFixedRate(Runnable* command, int64_t initialDelay,
                                                                  int64_t period, TimeUnit unit) {
    if (command == nullptr || unit == nullptr) throw NullPointerException();
    if (period <= 0) throw IllegalArgumentException();
    auto* sft = new ScheduledFutureTask(this, command, nullptr, triggerTime(initialDelay, unit), unit.toNanos(period));
    RunnableScheduledFuture* t = decorateTask(command, sft);
    sft->outerTask = t;
    delayedExecute(t);
    return t;
}

ScheduledFuture* ScheduledThreadPoolExecutor::scheduleWithFixedDelay(Runnable* command, int64_t initialDelay,
                                                                     int64_t delay, TimeUnit unit) {
    if (command == nullptr || unit == nullptr) throw NullPointerException();
    if (delay <= 0) throw IllegalArgumentException();
    auto* sft = new ScheduledFutureTask(this, command, nullptr, triggerTime(initialDelay, unit), unit.toNanos(-delay));
    RunnableScheduledFuture* t = decorateTask(command, sft);
    sft->outerTask = t;
    delayedExecute(t);
    return t;
}

void ScheduledThreadPoolExecutor::execute(Runnable* command) { schedule(command, 0, TimeUnit::NANOSECONDS); }

Future* ScheduledThreadPoolExecutor::submit(Runnable* task) { return schedule(task, 0, TimeUnit::NANOSECONDS); }

Future* ScheduledThreadPoolExecutor::submit(Runnable* task, Object* result) {
    return schedule(Executors::callable(task, result), 0, TimeUnit::NANOSECONDS);
}

Future* ScheduledThreadPoolExecutor::submit(Callable<Object*>* task) { return schedule(task, 0, TimeUnit::NANOSECONDS); }

void ScheduledThreadPoolExecutor::shutdown() { ThreadPoolExecutor::shutdown(); }

List<Runnable*>* ScheduledThreadPoolExecutor::shutdownNow() { return ThreadPoolExecutor::shutdownNow(); }

void ScheduledThreadPoolExecutor::setContinueExistingPeriodicTasksAfterShutdownPolicy(bool value) {
    continueExistingPeriodic_.store(value);
    if (!value && isShutdown()) onShutdown();
}

void ScheduledThreadPoolExecutor::setExecuteExistingDelayedTasksAfterShutdownPolicy(bool value) {
    executeExistingDelayed_.store(value);
    if (!value && isShutdown()) onShutdown();
}

// =======================================================================================
// Executors

ExecutorService* Executors::newFixedThreadPool(int32_t nThreads) {
    return new ThreadPoolExecutor(nThreads, nThreads, 0, TimeUnit::MILLISECONDS, new LinkedBlockingQueue<Runnable*>());
}

ExecutorService* Executors::newFixedThreadPool(int32_t nThreads, ThreadFactory* threadFactory) {
    return new ThreadPoolExecutor(nThreads, nThreads, 0, TimeUnit::MILLISECONDS, new LinkedBlockingQueue<Runnable*>(),
                                  threadFactory);
}

ExecutorService* Executors::newCachedThreadPool() {
    return new ThreadPoolExecutor(0, INT32_MAX, 60, TimeUnit::SECONDS, new SynchronousQueue<Runnable*>());
}

ExecutorService* Executors::newCachedThreadPool(ThreadFactory* threadFactory) {
    return new ThreadPoolExecutor(0, INT32_MAX, 60, TimeUnit::SECONDS, new SynchronousQueue<Runnable*>(), threadFactory);
}

ExecutorService* Executors::newSingleThreadExecutor() { return newFixedThreadPool(1); }

ExecutorService* Executors::newSingleThreadExecutor(ThreadFactory* threadFactory) {
    return newFixedThreadPool(1, threadFactory);
}

ScheduledExecutorService* Executors::newScheduledThreadPool(int32_t corePoolSize) {
    return new ScheduledThreadPoolExecutor(corePoolSize);
}

ScheduledExecutorService* Executors::newScheduledThreadPool(int32_t corePoolSize, ThreadFactory* threadFactory) {
    return new ScheduledThreadPoolExecutor(corePoolSize, threadFactory);
}

ScheduledExecutorService* Executors::newSingleThreadScheduledExecutor() { return new ScheduledThreadPoolExecutor(1); }

ScheduledExecutorService* Executors::newSingleThreadScheduledExecutor(ThreadFactory* threadFactory) {
    return new ScheduledThreadPoolExecutor(1, threadFactory);
}

ThreadFactory* Executors::defaultThreadFactory() { return new DefaultThreadFactory(); }

Callable<Object*>* Executors::callable(Runnable* task, Object* result) {
    if (task == nullptr) throw NullPointerException();
    return Callable<Object*>::of([task, result]() -> Object* {
        task->run();
        return result;
    });
}

Callable<Object*>* Executors::callable(Runnable* task) { return callable(task, nullptr); }

}  // namespace jlang
