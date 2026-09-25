// java.util.Timer / TimerTask (a port of the JDK's TimerThread main loop).
#include <jlang/Thread.h>
#include <jlang/Time.h>

#include <algorithm>

namespace jlang {

namespace {
constexpr int32_t VIRGIN = 0;
constexpr int32_t SCHEDULED = 1;
constexpr int32_t EXECUTED = 2;
constexpr int32_t CANCELLED = 3;

std::atomic<int32_t> g_timerSerial{0};
}  // namespace

struct TimerInternals {
    static std::mutex& lock(TimerTask* t) { return t->lock_; }
    static int32_t& state(TimerTask* t) { return t->state_; }
    static int64_t& next(TimerTask* t) { return t->nextExecutionTime_; }
    static int64_t& period(TimerTask* t) { return t->period_; }
};

namespace detail {

// java.util.TaskQueue + TimerThread state.
class TimerQueue : public virtual Runnable {
public:
    std::mutex mu;
    std::condition_variable cv;
    std::vector<TimerTask*> heap;  // min-heap on nextExecutionTime
    bool newTasksMayBeScheduled = true;

    void add(TimerTask* t) {
        heap.push_back(t);
        std::push_heap(heap.begin(), heap.end(), Later());
    }
    void removeMin() {
        std::pop_heap(heap.begin(), heap.end(), Later());
        heap.pop_back();
    }
    void rescheduleMin(int64_t newTime) {
        TimerTask* t = heap.front();
        removeMin();
        TimerInternals::next(t) = newTime;  // caller holds the task lock
        heap.push_back(t);
        std::push_heap(heap.begin(), heap.end(), Later());
    }
    struct Later {
        bool operator()(TimerTask* a, TimerTask* b) const {
            return TimerInternals::next(a) > TimerInternals::next(b);
        }
    };

    void run() override {
        JFINALLY {
            std::lock_guard<std::mutex> g(mu);
            newTasksMayBeScheduled = false;
            heap.clear();
        };
        mainLoop();
    }

private:
    void mainLoop() {
        for (;;) {
            TimerTask* task;
            bool taskFired;
            {
                std::unique_lock<std::mutex> lk(mu);
                while (heap.empty() && newTasksMayBeScheduled) cv.wait(lk);
                if (heap.empty()) break;
                int64_t currentTime, executionTime;
                task = heap.front();
                {
                    std::lock_guard<std::mutex> tg(TimerInternals::lock(task));
                    if (TimerInternals::state(task) == CANCELLED) {
                        removeMin();
                        continue;
                    }
                    currentTime = System::currentTimeMillis();
                    executionTime = TimerInternals::next(task);
                    if ((taskFired = (executionTime <= currentTime))) {
                        int64_t p = TimerInternals::period(task);
                        if (p == 0) {
                            removeMin();
                            TimerInternals::state(task) = EXECUTED;
                        } else {
                            rescheduleMin(p < 0 ? currentTime - p : executionTime + p);
                        }
                    }
                }
                if (!taskFired) {
                    int64_t ms = executionTime - currentTime;
                    if (ms > 86400000) ms = 86400000;  // re-check at least daily
                    cv.wait_for(lk, std::chrono::milliseconds(ms));
                }
            }
            if (taskFired) task->run();
        }
    }
};

}  // namespace detail

// ---------------------------------------------------------------------------------------
// TimerTask

bool TimerTask::cancel() {
    std::lock_guard<std::mutex> g(lock_);
    bool result = (state_ == SCHEDULED);
    state_ = CANCELLED;
    return result;
}

int64_t TimerTask::scheduledExecutionTime() {
    std::lock_guard<std::mutex> g(lock_);
    return period_ < 0 ? nextExecutionTime_ + period_ : nextExecutionTime_ - period_;
}

// ---------------------------------------------------------------------------------------
// Timer

Timer::Timer() { start(str("Timer-", g_timerSerial.fetch_add(1)), false); }
Timer::Timer(bool isDaemon) { start(str("Timer-", g_timerSerial.fetch_add(1)), isDaemon); }
Timer::Timer(const String& name) { start(name, false); }
Timer::Timer(const String& name, bool isDaemon) { start(name, isDaemon); }

void Timer::start(const String& name, bool isDaemon) {
    if (name == nullptr) throw NullPointerException();
    queue_ = new detail::TimerQueue();
    thread_ = new Thread(queue_, name);
    thread_->setDaemon(isDaemon);
    thread_->start();
}

void Timer::sched(TimerTask* task, int64_t time, int64_t period) {
    if (task == nullptr) throw NullPointerException();
    if (time < 0) throw IllegalArgumentException(String("Illegal execution time."));
    if ((period < 0 ? -period : period) > (INT64_MAX >> 1)) period >>= 1;
    std::lock_guard<std::mutex> g(queue_->mu);
    if (!queue_->newTasksMayBeScheduled) throw IllegalStateException(String("Timer already cancelled."));
    {
        std::lock_guard<std::mutex> tg(TimerInternals::lock(task));
        if (TimerInternals::state(task) != VIRGIN)
            throw IllegalStateException(String("Task already scheduled or cancelled"));
        TimerInternals::next(task) = time;
        TimerInternals::period(task) = period;
        TimerInternals::state(task) = SCHEDULED;
    }
    queue_->add(task);
    if (queue_->heap.front() == task) queue_->cv.notify_all();
}

void Timer::schedule(TimerTask* task, int64_t delay) {
    if (delay < 0) throw IllegalArgumentException(String("Negative delay."));
    sched(task, System::currentTimeMillis() + delay, 0);
}

void Timer::schedule(TimerTask* task, Date* time) {
    if (time == nullptr) throw NullPointerException();
    sched(task, time->getTime(), 0);
}

void Timer::schedule(TimerTask* task, int64_t delay, int64_t period) {
    if (delay < 0) throw IllegalArgumentException(String("Negative delay."));
    if (period <= 0) throw IllegalArgumentException(String("Non-positive period."));
    sched(task, System::currentTimeMillis() + delay, -period);
}

void Timer::schedule(TimerTask* task, Date* firstTime, int64_t period) {
    if (period <= 0) throw IllegalArgumentException(String("Non-positive period."));
    if (firstTime == nullptr) throw NullPointerException();
    sched(task, firstTime->getTime(), -period);
}

void Timer::scheduleAtFixedRate(TimerTask* task, int64_t delay, int64_t period) {
    if (delay < 0) throw IllegalArgumentException(String("Negative delay."));
    if (period <= 0) throw IllegalArgumentException(String("Non-positive period."));
    sched(task, System::currentTimeMillis() + delay, period);
}

void Timer::scheduleAtFixedRate(TimerTask* task, Date* firstTime, int64_t period) {
    if (period <= 0) throw IllegalArgumentException(String("Non-positive period."));
    if (firstTime == nullptr) throw NullPointerException();
    sched(task, firstTime->getTime(), period);
}

void Timer::cancel() {
    std::lock_guard<std::mutex> g(queue_->mu);
    queue_->newTasksMayBeScheduled = false;
    queue_->heap.clear();
    queue_->cv.notify_all();
}

int32_t Timer::purge() {
    int32_t result = 0;
    std::lock_guard<std::mutex> g(queue_->mu);
    auto& h = queue_->heap;
    auto it = std::remove_if(h.begin(), h.end(), [&](TimerTask* t) {
        std::lock_guard<std::mutex> tg(TimerInternals::lock(t));
        if (TimerInternals::state(t) == CANCELLED) {
            result++;
            return true;
        }
        return false;
    });
    h.erase(it, h.end());
    // nextExecutionTime is only written with the queue lock held, so the heap order can be
    // read under it.
    if (result != 0) std::make_heap(h.begin(), h.end(), detail::TimerQueue::Later());
    return result;
}

}  // namespace jlang
