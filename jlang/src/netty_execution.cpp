// jlang/src/netty_execution.cpp - org.jboss.netty.handler.execution (ExecutionHandler,
// ChannelEventRunnable, MemoryAwareThreadPoolExecutor, OrderedMemoryAwareThreadPoolExecutor)
// and org.jboss.netty.util.DefaultObjectSizeEstimator, ported from Netty 3.2.0.BETA1.
#include "netty_internal.h"

#include <algorithm>
#include <chrono>

namespace jlang::netty {

using ::jlang::Object;
using ::jlang::Runnable;
using ::jlang::String;
using ::jlang::Throwable;

// =======================================================================================
// ChannelEventRunnable / ExecutionHandler
// =======================================================================================

void ChannelEventRunnable::run() { ctx_->sendUpstream(e_); }

ExecutionHandler::ExecutionHandler(::jlang::Executor* executor) : executor_(executor) {
    if (executor == nullptr) throw ::jlang::NullPointerException("executor");
}

void ExecutionHandler::releaseExternalResources() { detail::terminateExecutors({executor_}); }

void ExecutionHandler::handleUpstream(ChannelHandlerContext* context, ChannelEvent* e) {
    executor_->execute(new ChannelEventRunnable(context, e));
}

void ExecutionHandler::handleDownstream(ChannelHandlerContext* ctx, ChannelEvent* e) {
    if (auto* cse = dynamic_cast<ChannelStateEvent*>(e)) {
        if (cse->getState() == ChannelState::INTEREST_OPS && (detail::toInt(cse->getValue()) & Channel::OP_READ) != 0) {
            // setReadable(true) while reads are suspended by the memory limit: ignore it.
            bool readSuspended = ctx->getAttachment() != nullptr;
            if (readSuspended) {
                e->getFuture()->setSuccess();
                return;
            }
        }
    }
    ctx->sendDownstream(e);
}

// =======================================================================================
// DefaultObjectSizeEstimator (approximation of the reflective Java estimator)
// =======================================================================================
namespace {
int32_t align8(int32_t size) {
    int32_t r = size % 8;
    return r != 0 ? size + 8 - r : size;
}
}  // namespace

int32_t DefaultObjectSizeEstimator::estimateSize(Object* o) {
    if (o == nullptr) return 8;
    int32_t answer = 8;
    if (auto* r = dynamic_cast<ChannelEventRunnable*>(o)) {
        answer += 24;  // ctx, e, estimatedSize
        answer += estimateSize(r->getEvent());
    } else if (auto* me = dynamic_cast<MessageEvent*>(o)) {
        answer += 32;  // channel, message, remoteAddress (+ future)
        answer += estimateSize(me->getMessage());
    } else if (auto* b = dynamic_cast<ChannelBuffer*>(o)) {
        answer += 40;  // indexes + array reference
        answer += b->capacity();
    } else if (auto* a = dynamic_cast<::jlang::Array<int8_t>*>(o)) {
        answer += 8 + a->length;
    } else if (dynamic_cast<ChannelEvent*>(o) != nullptr) {
        answer += 24;
    } else {
        answer += 16;
    }
    return align8(answer);
}

// =======================================================================================
// MemoryAwareThreadPoolExecutor
// =======================================================================================
namespace detail {

// A pool thread (ThreadPoolExecutor.Worker).
class PoolWorker final : public virtual Runnable {
public:
    PoolWorker(MemoryAwareThreadPoolExecutor* pool, Runnable* firstTask) : firstTask(firstTask), pool_(pool) {}
    void run() override { pool_->runWorker(this); }
    Runnable* firstTask;
    ::jlang::Thread* thread = nullptr;

private:
    MemoryAwareThreadPoolExecutor* pool_;
};

// MemoryAwareThreadPoolExecutor.MemoryAwareRunnable
class MemoryAwareRunnable final : public virtual Runnable {
public:
    explicit MemoryAwareRunnable(Runnable* task) : task(task) {}
    void run() override { task->run(); }
    Runnable* task;
    int32_t estimatedSize = 0;
};

// OrderedMemoryAwareThreadPoolExecutor.ChildExecutor: runs the tasks of one channel one at a
// time, in submission order, on the pool's threads. The task being run stays at the head of
// the queue, so a new task only schedules the executor when the queue was empty.
class ChildExecutor final : public virtual ::jlang::Executor, public virtual Runnable {
public:
    ChildExecutor(OrderedMemoryAwareThreadPoolExecutor* parent, Object* key, Channel* channel)
        : parent_(parent), key_(key), channel_(channel) {}

    // Appends a task; true if the executor must be scheduled. Called with the parent's
    // childLock_ held (so that an idle executor cannot be dropped concurrently).
    bool enqueue(Runnable* command) {
        std::lock_guard<std::mutex> g(lock_);
        bool needsExecution = tasks_.empty();
        tasks_.push_back(command);
        return needsExecution;
    }

    bool idle() {
        std::lock_guard<std::mutex> g(lock_);
        return tasks_.empty();
    }

    void execute(Runnable* command) override {
        bool needsExecution;
        {
            std::lock_guard<std::mutex> g(parent_->childLock_);
            needsExecution = enqueue(command);
        }
        if (needsExecution) parent_->doUnorderedExecute(this);
    }

    void run() override {
        ::jlang::Thread* thread = ::jlang::Thread::currentThread();
        for (;;) {
            Runnable* task;
            {
                std::lock_guard<std::mutex> g(lock_);
                task = tasks_.front();
            }
            parent_->beforeExecute(thread, task);
            try {
                task->run();
                parent_->afterExecute(task, nullptr);
            } catch (Throwable& t) {
                // Java rethrows (the pool thread dies and this channel's queue stalls); here the
                // failure is reported and the channel keeps going.
                parent_->afterExecute(task, &t);
                detail::logWarn("org.jboss.netty.handler.execution.OrderedMemoryAwareThreadPoolExecutor",
                                "Unexpected exception from a channel event task.", &t);
            }
            std::lock_guard<std::mutex> g(lock_);
            tasks_.pop_front();
            if (tasks_.empty()) break;
        }
        // The channel is closed and everything queued for it ran: drop this executor.
        if (channel_ != nullptr && !channel_->isOpen()) parent_->removeIdleChildExecutor(key_, this);
    }

private:
    OrderedMemoryAwareThreadPoolExecutor* parent_;
    Object* key_;
    Channel* channel_;
    std::mutex lock_;
    std::deque<Runnable*> tasks_;
};

}  // namespace detail

namespace {
thread_local MemoryAwareThreadPoolExecutor* tlCurrentPool = nullptr;  // set in pool threads
constexpr int32_t RUNNING = 0;
constexpr int32_t SHUTDOWN = 1;
constexpr int32_t STOP = 2;
constexpr int32_t TERMINATED = 3;
std::atomic<int32_t> g_activeInstances{0};
}  // namespace

MemoryAwareThreadPoolExecutor::MemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize,
                                                             int64_t maxTotalMemorySize)
    : MemoryAwareThreadPoolExecutor(corePoolSize, maxChannelMemorySize, maxTotalMemorySize, 30,
                                    ::jlang::TimeUnit::SECONDS) {}

MemoryAwareThreadPoolExecutor::MemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize,
                                                             int64_t maxTotalMemorySize, int64_t keepAliveTime,
                                                             ::jlang::TimeUnit unit)
    : MemoryAwareThreadPoolExecutor(corePoolSize, maxChannelMemorySize, maxTotalMemorySize, keepAliveTime, unit,
                                    ::jlang::Executors::defaultThreadFactory()) {}

MemoryAwareThreadPoolExecutor::MemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize,
                                                             int64_t maxTotalMemorySize, int64_t keepAliveTime,
                                                             ::jlang::TimeUnit unit,
                                                             ::jlang::ThreadFactory* threadFactory)
    : MemoryAwareThreadPoolExecutor(corePoolSize, maxChannelMemorySize, maxTotalMemorySize, keepAliveTime, unit,
                                    new DefaultObjectSizeEstimator(), threadFactory) {}

MemoryAwareThreadPoolExecutor::MemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize,
                                                             int64_t maxTotalMemorySize, int64_t keepAliveTime,
                                                             ::jlang::TimeUnit unit,
                                                             ObjectSizeEstimator* objectSizeEstimator,
                                                             ::jlang::ThreadFactory* threadFactory) {
    // ThreadPoolExecutor(corePoolSize, corePoolSize, keepAliveTime, unit, queue, threadFactory, ...)
    if (corePoolSize <= 0 || keepAliveTime < 0) throw ::jlang::IllegalArgumentException();
    if (threadFactory == nullptr) throw ::jlang::NullPointerException();
    corePoolSize_ = corePoolSize;
    keepAliveNanos_ = unit.toNanos(keepAliveTime);
    threadFactory_ = threadFactory;
    if (objectSizeEstimator == nullptr) throw ::jlang::NullPointerException("objectSizeEstimator");
    if (maxChannelMemorySize < 0) {
        throw ::jlang::IllegalArgumentException(::jlang::str("maxChannelMemorySize: ", maxChannelMemorySize));
    }
    if (maxTotalMemorySize < 0) {
        throw ::jlang::IllegalArgumentException(::jlang::str("maxTotalMemorySize: ", maxTotalMemorySize));
    }
    // Netty enables allowCoreThreadTimeOut(true) reflectively (Java 6+).
    allowCoreThreadTimeOut_ = keepAliveTime > 0;
    settings_.store(new Settings{objectSizeEstimator, maxChannelMemorySize, maxTotalMemorySize});
    int32_t active = ++g_activeInstances;
    if (active >= 64) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true)) {
            detail::logDebug("org.jboss.netty.handler.execution.MemoryAwareThreadPoolExecutor",
                             ::jlang::str("There are too many active MemoryAwareThreadPoolExecutor instances (", active,
                                          ") - you should share the small number of instances to avoid excessive "
                                          "resource consumption."));
        }
    }
}

ObjectSizeEstimator* MemoryAwareThreadPoolExecutor::getObjectSizeEstimator() {
    return settings_.load()->objectSizeEstimator;
}

void MemoryAwareThreadPoolExecutor::setObjectSizeEstimator(ObjectSizeEstimator* objectSizeEstimator) {
    if (objectSizeEstimator == nullptr) throw ::jlang::NullPointerException("objectSizeEstimator");
    Settings* s = settings_.load();
    settings_.store(new Settings{objectSizeEstimator, s->maxChannelMemorySize, s->maxTotalMemorySize});
}

int64_t MemoryAwareThreadPoolExecutor::getMaxChannelMemorySize() { return settings_.load()->maxChannelMemorySize; }

void MemoryAwareThreadPoolExecutor::setMaxChannelMemorySize(int64_t maxChannelMemorySize) {
    if (maxChannelMemorySize < 0) {
        throw ::jlang::IllegalArgumentException(::jlang::str("maxChannelMemorySize: ", maxChannelMemorySize));
    }
    if (getTaskCount() > 0) throw ::jlang::IllegalStateException("can't be changed after a task is executed");
    Settings* s = settings_.load();
    settings_.store(new Settings{s->objectSizeEstimator, maxChannelMemorySize, s->maxTotalMemorySize});
}

int64_t MemoryAwareThreadPoolExecutor::getMaxTotalMemorySize() { return settings_.load()->maxTotalMemorySize; }

void MemoryAwareThreadPoolExecutor::setMaxTotalMemorySize(int64_t maxTotalMemorySize) {
    if (maxTotalMemorySize < 0) {
        throw ::jlang::IllegalArgumentException(::jlang::str("maxTotalMemorySize: ", maxTotalMemorySize));
    }
    if (getTaskCount() > 0) throw ::jlang::IllegalStateException("can't be changed after a task is executed");
    Settings* s = settings_.load();
    settings_.store(new Settings{s->objectSizeEstimator, s->maxChannelMemorySize, maxTotalMemorySize});
}

void MemoryAwareThreadPoolExecutor::execute(Runnable* command) {
    if (command == nullptr) throw ::jlang::NullPointerException();
    if (dynamic_cast<ChannelEventRunnable*>(command) == nullptr) command = new detail::MemoryAwareRunnable(command);
    increaseCounter(command);
    doExecute(command);
}

void MemoryAwareThreadPoolExecutor::doExecute(Runnable* task) { doUnorderedExecute(task); }

void MemoryAwareThreadPoolExecutor::doUnorderedExecute(Runnable* task) {
    // ThreadPoolExecutor.execute: a new thread while below corePoolSize, else the queue.
    detail::PoolWorker* w = nullptr;
    {
        std::lock_guard<std::mutex> g(poolLock_);
        if (runState_ != RUNNING) {
            w = nullptr;
        } else if (static_cast<int32_t>(workers_.size()) < corePoolSize_) {
            w = new detail::PoolWorker(this, task);
            workers_.push_back(w);  // counted now, so concurrent calls respect corePoolSize
        } else {
            queue_.push_back(task);
            poolCond_.notify_one();
            return;
        }
    }
    if (w == nullptr) {
        rejected(task);
        return;
    }
    startWorker(w);
}

void MemoryAwareThreadPoolExecutor::startWorker(detail::PoolWorker* w) {
    ::jlang::Thread* t = nullptr;
    try {
        t = threadFactory_->newThread(w);
        if (t != nullptr) {
            w->thread = t;
            t->start();
            return;
        }
    } catch (Throwable& e) {
        detail::logWarn("org.jboss.netty.handler.execution.MemoryAwareThreadPoolExecutor",
                        "Failed to start a pool thread.", &e);
    }
    // No thread: hand the task to the existing threads (or a temporary one).
    Runnable* task = w->firstTask;
    bool none;
    {
        std::lock_guard<std::mutex> g(poolLock_);
        workers_.erase(std::find(workers_.begin(), workers_.end(), w));
        none = workers_.empty();
        if (!none) {
            queue_.push_back(task);
            poolCond_.notify_one();
        }
    }
    if (none) rejected(task);
}

void MemoryAwareThreadPoolExecutor::rejected(Runnable* task) {
    // MemoryAwareThreadPoolExecutor.NewThreadRunsPolicy
    try {
        auto* t = new ::jlang::Thread(task, String("Temporary task executor"));
        t->start();
    } catch (Throwable& e) {
        throw ::jlang::RejectedExecutionException("Failed to start a new thread", e);
    }
}

Runnable* MemoryAwareThreadPoolExecutor::getTask(detail::PoolWorker* w) {
    std::unique_lock<std::mutex> g(poolLock_);
    for (;;) {
        if (runState_ >= STOP || (runState_ == SHUTDOWN && queue_.empty())) break;
        if (!queue_.empty()) {
            Runnable* r = queue_.front();
            queue_.pop_front();
            return r;
        }
        bool timed = allowCoreThreadTimeOut_ || static_cast<int32_t>(workers_.size()) > corePoolSize_;
        if (timed) {
            if (poolCond_.wait_for(g, std::chrono::nanoseconds(keepAliveNanos_)) == std::cv_status::timeout &&
                queue_.empty() && runState_ == RUNNING) {
                break;  // idle for keepAliveTime: this thread exits
            }
        } else {
            poolCond_.wait(g);
        }
    }
    // Worker exit (under poolLock_).
    workers_.erase(std::find(workers_.begin(), workers_.end(), w));
    if (runState_ >= SHUTDOWN && workers_.empty() && (queue_.empty() || runState_ >= STOP)) {
        if (runState_ != TERMINATED) {
            runState_ = TERMINATED;
            --g_activeInstances;
        }
        terminationCond_.notify_all();
    }
    return nullptr;
}

void MemoryAwareThreadPoolExecutor::runWorker(detail::PoolWorker* w) {
    tlCurrentPool = this;
    Runnable* task = w->firstTask;
    w->firstTask = nullptr;
    ::jlang::Thread* thread = ::jlang::Thread::currentThread();
    while (task != nullptr || (task = getTask(w)) != nullptr) {
        {
            std::lock_guard<std::mutex> g(poolLock_);
            activeCount_++;
        }
        try {
            beforeExecute(thread, task);
            task->run();
            afterExecute(task, nullptr);
        } catch (Throwable& t) {
            afterExecute(task, &t);
            detail::logWarn("org.jboss.netty.handler.execution.MemoryAwareThreadPoolExecutor",
                            "Exception in a pool task.", &t);
        }
        {
            std::lock_guard<std::mutex> g(poolLock_);
            activeCount_--;
            completedTaskCount_++;
        }
        task = nullptr;
        ::jlang::Thread::interrupted();  // clear a stale interrupt, as ThreadPoolExecutor does
    }
}

void MemoryAwareThreadPoolExecutor::beforeExecute(::jlang::Thread*, Runnable* r) { decreaseCounter(r); }

bool MemoryAwareThreadPoolExecutor::remove(Runnable* task) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> g(poolLock_);
        auto it = std::find(queue_.begin(), queue_.end(), task);
        if (it != queue_.end()) {
            queue_.erase(it);
            removed = true;
        }
    }
    if (removed) decreaseCounter(task);
    return removed;
}

bool MemoryAwareThreadPoolExecutor::shouldCount(Runnable* task) {
    if (auto* r = dynamic_cast<ChannelEventRunnable*>(task)) {
        ChannelEvent* e = r->getEvent();
        if (dynamic_cast<WriteCompletionEvent*>(e) != nullptr) return false;
        if (auto* se = dynamic_cast<ChannelStateEvent*>(e)) {
            if (se->getState() == ChannelState::INTEREST_OPS) return false;
        }
    }
    return true;
}

MemoryAwareThreadPoolExecutor::ChannelCounter* MemoryAwareThreadPoolExecutor::getChannelCounter(Channel* channel) {
    std::lock_guard<std::mutex> g(countersLock_);
    ChannelCounter*& counter = channelCounters_[channel];
    ChannelCounter* c = counter;
    if (c == nullptr) {
        c = new ChannelCounter();
        counter = c;
    }
    if (!channel->isOpen()) channelCounters_.erase(channel);
    return c;
}

void MemoryAwareThreadPoolExecutor::increaseCounter(Runnable* task) {
    if (!shouldCount(task)) return;
    Settings* settings = settings_.load();
    int64_t maxTotalMemorySize = settings->maxTotalMemorySize;
    int64_t maxChannelMemorySize = settings->maxChannelMemorySize;
    int32_t increment = settings->objectSizeEstimator->estimateSize(task);

    if (auto* eventTask = dynamic_cast<ChannelEventRunnable*>(task)) {
        eventTask->estimatedSize = increment;
        Channel* channel = eventTask->getEvent()->getChannel();
        ChannelCounter* counter = getChannelCounter(channel);
        std::lock_guard<std::mutex> g(counter->lock);
        counter->value += increment;
        if (maxChannelMemorySize != 0 && counter->value >= maxChannelMemorySize && channel->isOpen()) {
            if (channel->isReadable()) {
                ChannelHandlerContext* ctx = eventTask->getContext();
                if (dynamic_cast<ExecutionHandler*>(ctx->getHandler()) != nullptr) ctx->setAttachment(detail::boxBool(true));
                channel->setReadable(false);
            }
        }
    } else {
        dynamic_cast<detail::MemoryAwareRunnable*>(task)->estimatedSize = increment;
    }

    std::unique_lock<std::mutex> g(totalLock_);
    if (maxTotalMemorySize != 0 && tlCurrentPool != this) {
        while (totalCounter_ >= maxTotalMemorySize) {
            totalWaiters_++;
            totalCond_.wait(g);
            totalWaiters_--;
        }
    }
    totalCounter_ += increment;
}

void MemoryAwareThreadPoolExecutor::decreaseCounter(Runnable* task) {
    if (!shouldCount(task)) return;
    Settings* settings = settings_.load();
    int64_t maxTotalMemorySize = settings->maxTotalMemorySize;
    int64_t maxChannelMemorySize = settings->maxChannelMemorySize;
    int32_t increment;
    auto* eventTask = dynamic_cast<ChannelEventRunnable*>(task);
    if (eventTask != nullptr) {
        increment = eventTask->estimatedSize;
    } else if (auto* mar = dynamic_cast<detail::MemoryAwareRunnable*>(task)) {
        increment = mar->estimatedSize;
    } else {
        return;  // not submitted through execute() (e.g. a ChildExecutor)
    }
    {
        std::lock_guard<std::mutex> g(totalLock_);
        totalCounter_ -= increment;
        if (totalWaiters_ > 0 && (maxTotalMemorySize == 0 || totalCounter_ < maxTotalMemorySize)) {
            totalCond_.notify_all();
        }
    }
    if (eventTask != nullptr) {
        Channel* channel = eventTask->getEvent()->getChannel();
        ChannelCounter* counter = getChannelCounter(channel);
        std::lock_guard<std::mutex> g(counter->lock);
        counter->value -= increment;
        if (maxChannelMemorySize != 0 && counter->value < maxChannelMemorySize && channel->isOpen()) {
            if (!channel->isReadable()) {
                ChannelHandlerContext* ctx = eventTask->getContext();
                if (dynamic_cast<ExecutionHandler*>(ctx->getHandler()) != nullptr) ctx->setAttachment(nullptr);
                channel->setReadable(true);
            }
        }
    }
}

void MemoryAwareThreadPoolExecutor::shutdown() {
    {
        std::lock_guard<std::mutex> g(poolLock_);
        if (runState_ < SHUTDOWN) runState_ = SHUTDOWN;
        if (workers_.empty() && queue_.empty() && runState_ != TERMINATED) {
            runState_ = TERMINATED;
            --g_activeInstances;
            terminationCond_.notify_all();
        }
    }
    poolCond_.notify_all();
}

std::vector<Runnable*> MemoryAwareThreadPoolExecutor::shutdownNow() {
    std::vector<Runnable*> drained;
    std::vector<detail::PoolWorker*> ws;
    {
        std::lock_guard<std::mutex> g(poolLock_);
        if (runState_ < STOP) runState_ = STOP;
        drained.assign(queue_.begin(), queue_.end());
        queue_.clear();
        ws = workers_;
        if (workers_.empty() && runState_ != TERMINATED) {
            runState_ = TERMINATED;
            --g_activeInstances;
            terminationCond_.notify_all();
        }
    }
    poolCond_.notify_all();
    for (auto* w : ws) {
        if (w->thread != nullptr) w->thread->interrupt();
    }
    return drained;
}

bool MemoryAwareThreadPoolExecutor::isShutdown() {
    std::lock_guard<std::mutex> g(poolLock_);
    return runState_ != RUNNING;
}

bool MemoryAwareThreadPoolExecutor::isTerminated() {
    std::lock_guard<std::mutex> g(poolLock_);
    return runState_ == TERMINATED;
}

bool MemoryAwareThreadPoolExecutor::awaitTermination(int64_t timeout, ::jlang::TimeUnit unit) {
    std::unique_lock<std::mutex> g(poolLock_);
    return terminationCond_.wait_for(g, std::chrono::nanoseconds(unit.toNanos(timeout)),
                                     [this] { return runState_ == TERMINATED; });
}

int32_t MemoryAwareThreadPoolExecutor::getCorePoolSize() { return corePoolSize_; }
int32_t MemoryAwareThreadPoolExecutor::getMaximumPoolSize() { return corePoolSize_; }

int32_t MemoryAwareThreadPoolExecutor::getPoolSize() {
    std::lock_guard<std::mutex> g(poolLock_);
    return static_cast<int32_t>(workers_.size());
}

int32_t MemoryAwareThreadPoolExecutor::getActiveCount() {
    std::lock_guard<std::mutex> g(poolLock_);
    return activeCount_;
}

int64_t MemoryAwareThreadPoolExecutor::getTaskCount() {
    std::lock_guard<std::mutex> g(poolLock_);
    return completedTaskCount_ + activeCount_ + static_cast<int64_t>(queue_.size());
}

int64_t MemoryAwareThreadPoolExecutor::getCompletedTaskCount() {
    std::lock_guard<std::mutex> g(poolLock_);
    return completedTaskCount_;
}

void MemoryAwareThreadPoolExecutor::allowCoreThreadTimeOut(bool value) {
    if (value && keepAliveNanos_ <= 0) {
        throw ::jlang::IllegalArgumentException("Core threads must have nonzero keep alive times");
    }
    {
        std::lock_guard<std::mutex> g(poolLock_);
        allowCoreThreadTimeOut_ = value;
    }
    poolCond_.notify_all();
}

// =======================================================================================
// OrderedMemoryAwareThreadPoolExecutor
// =======================================================================================

OrderedMemoryAwareThreadPoolExecutor::OrderedMemoryAwareThreadPoolExecutor(int32_t corePoolSize,
                                                                           int64_t maxChannelMemorySize,
                                                                           int64_t maxTotalMemorySize)
    : MemoryAwareThreadPoolExecutor(corePoolSize, maxChannelMemorySize, maxTotalMemorySize) {}

OrderedMemoryAwareThreadPoolExecutor::OrderedMemoryAwareThreadPoolExecutor(int32_t corePoolSize,
                                                                           int64_t maxChannelMemorySize,
                                                                           int64_t maxTotalMemorySize,
                                                                           int64_t keepAliveTime,
                                                                           ::jlang::TimeUnit unit)
    : MemoryAwareThreadPoolExecutor(corePoolSize, maxChannelMemorySize, maxTotalMemorySize, keepAliveTime, unit) {}

OrderedMemoryAwareThreadPoolExecutor::OrderedMemoryAwareThreadPoolExecutor(
    int32_t corePoolSize, int64_t maxChannelMemorySize, int64_t maxTotalMemorySize, int64_t keepAliveTime,
    ::jlang::TimeUnit unit, ::jlang::ThreadFactory* threadFactory)
    : MemoryAwareThreadPoolExecutor(corePoolSize, maxChannelMemorySize, maxTotalMemorySize, keepAliveTime, unit,
                                    threadFactory) {}

OrderedMemoryAwareThreadPoolExecutor::OrderedMemoryAwareThreadPoolExecutor(
    int32_t corePoolSize, int64_t maxChannelMemorySize, int64_t maxTotalMemorySize, int64_t keepAliveTime,
    ::jlang::TimeUnit unit, ObjectSizeEstimator* objectSizeEstimator, ::jlang::ThreadFactory* threadFactory)
    : MemoryAwareThreadPoolExecutor(corePoolSize, maxChannelMemorySize, maxTotalMemorySize, keepAliveTime, unit,
                                    objectSizeEstimator, threadFactory) {}

Object* OrderedMemoryAwareThreadPoolExecutor::getChildExecutorKey(ChannelEvent* e) { return e->getChannel(); }

bool OrderedMemoryAwareThreadPoolExecutor::removeChildExecutor(Object* key) {
    std::lock_guard<std::mutex> g(childLock_);
    return childExecutors_.erase(key) != 0;
}

void OrderedMemoryAwareThreadPoolExecutor::doExecute(Runnable* task) {
    auto* r = dynamic_cast<ChannelEventRunnable*>(task);
    if (r == nullptr) {
        doUnorderedExecute(task);
        return;
    }
    ChannelEvent* e = r->getEvent();
    detail::ChildExecutor* executor;
    bool needsExecution;
    {
        std::lock_guard<std::mutex> g(childLock_);
        Object* key = getChildExecutorKey(e);
        detail::ChildExecutor*& slot = childExecutors_[key];
        if (slot == nullptr) slot = new detail::ChildExecutor(this, key, e->getChannel());
        executor = slot;
        needsExecution = executor->enqueue(task);
    }
    if (needsExecution) doUnorderedExecute(executor);
}

void OrderedMemoryAwareThreadPoolExecutor::removeIdleChildExecutor(Object* key, detail::ChildExecutor* executor) {
    std::lock_guard<std::mutex> g(childLock_);
    auto it = childExecutors_.find(key);
    if (it != childExecutors_.end() && it->second == executor && executor->idle()) childExecutors_.erase(it);
}

bool OrderedMemoryAwareThreadPoolExecutor::shouldCount(Runnable* task) {
    if (dynamic_cast<detail::ChildExecutor*>(task) != nullptr) return false;
    return MemoryAwareThreadPoolExecutor::shouldCount(task);
}

// =======================================================================================
// ExecutorUtil.terminate
// =======================================================================================
namespace detail {

void terminateExecutors(std::initializer_list<::jlang::Executor*> executors) {
    for (::jlang::Executor* e : executors) {
        if (e == nullptr) throw ::jlang::NullPointerException("executors");
    }
    bool interrupted = false;
    for (::jlang::Executor* e : executors) {
        if (auto* mat = dynamic_cast<MemoryAwareThreadPoolExecutor*>(e)) {
            for (;;) {
                mat->shutdownNow();
                if (mat->awaitTermination(100, ::jlang::TimeUnit::MILLISECONDS)) break;
            }
            continue;
        }
        auto* es = dynamic_cast<::jlang::ExecutorService*>(e);
        if (es == nullptr) continue;
        for (;;) {
            es->shutdownNow();
            try {
                if (es->awaitTermination(100, ::jlang::TimeUnit::MILLISECONDS)) break;
            } catch (::jlang::InterruptedException&) {
                interrupted = true;
            }
        }
    }
    if (interrupted) ::jlang::Thread::currentThread()->interrupt();
}

bool isShutdown(::jlang::Executor* executor) {
    if (auto* mat = dynamic_cast<MemoryAwareThreadPoolExecutor*>(executor)) return mat->isShutdown();
    if (auto* es = dynamic_cast<::jlang::ExecutorService*>(executor)) return es->isShutdown();
    return false;
}

}  // namespace detail
}  // namespace jlang::netty
