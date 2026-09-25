// Tests for <jlang/Thread.h>: Thread, interruption, locks/conditions, atomics, blocking
// queues, futures, thread pools, scheduling, Timer and GC safety.
#include "jtest.h"

#include <jlang/Thread.h>

#include <atomic>
#include <string>
#include <vector>

using namespace jlang;

namespace {

int64_t nowMs() { return System::nanoTime() / 1000000; }  // monotonic, like the executors

// Waits (polling) until cond() or timeout; returns cond().
template<class F>
bool waitUntil(F cond, int64_t timeoutMs = 30000) {
    int64_t end = nowMs() + timeoutMs;
    while (!cond()) {
        if (nowMs() > end) return false;
        Thread::sleep(2);
    }
    return true;
}

class CountingThread : public Thread {
public:
    explicit CountingThread(const String& name) : Thread(name) {}
    void run() override {
        ran = true;
        seenName = Thread::currentThread()->getName();
        self = Thread::currentThread();
    }
    std::atomic<bool> ran{false};
    String seenName;
    Thread* self = nullptr;
};

struct Payload : public virtual Object {
    explicit Payload(int32_t v) : value(v), text(str("payload-", v)) {}
    int32_t value;
    String text;
    List<Integer*>* list = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------------------
JTEST(ThreadBasics) {
    Thread* main = Thread::currentThread();
    JCHECK_EQ(main->getName(), String("main"));
    JCHECK_EQ(main->getId(), INT64_C(1));
    JCHECK(main->isAlive());
    JCHECK(!main->isDaemon());
    JCHECK_EQ(main->getThreadGroup()->getName(), String("main"));
    JCHECK(Thread::currentThread() == main);

    auto* t = new CountingThread(String("worker-1"));
    JCHECK(!t->isAlive());
    t->start();
    t->join();
    JCHECK(t->ran.load());
    JCHECK(!t->isAlive());
    JCHECK_EQ(t->seenName, String("worker-1"));
    JCHECK(t->self == t);
    JCHECK_THROWS(IllegalThreadStateException, t->start());

    std::atomic<int32_t> count{0};
    Thread* anon = new Thread(Runnable::of([&count] { count++; }));
    JCHECK(anon->getName().startsWith(String("Thread-")));
    JCHECK(anon->getId() > 1);
    anon->start();
    anon->join();
    JCHECK_EQ(count.load(), 1);
    JCHECK_EQ(anon->toString(), str("Thread[", anon->getName(), ",5,]"));

    // daemon and priority are inherited from the creating thread
    std::atomic<bool> childDaemon{false};
    std::atomic<int32_t> childPriority{0};
    Thread* parent = new Thread(Runnable::of([&] {
        Thread* c = new Thread(Runnable::of([] {}));
        childDaemon = c->isDaemon();
        childPriority = c->getPriority();
    }));
    parent->setDaemon(true);
    parent->setPriority(7);
    JCHECK_EQ(parent->getPriority(), 7);
    parent->start();
    parent->join();
    JCHECK(childDaemon.load());
    JCHECK_EQ(childPriority.load(), 7);
    JCHECK_THROWS(IllegalArgumentException, parent->setPriority(11));

    auto* g = new ThreadGroup(String("grp"));
    g->setMaxPriority(4);
    Thread* inGroup = new Thread(g, Runnable::of([] { Thread::sleep(50); }), String("g-1"));
    inGroup->setPriority(9);
    JCHECK_EQ(inGroup->getPriority(), 4);
    inGroup->start();
    JCHECK_EQ(g->activeCount(), 1);
    JCHECK_EQ(inGroup->toString(), String("Thread[g-1,4,grp]"));
    inGroup->join();
    JCHECK_EQ(g->activeCount(), 0);
    JCHECK(g->getParent() == ThreadGroup::mainGroup());
    JCHECK_EQ(g->toString(), String("java.lang.ThreadGroup[name=grp,maxpri=4]"));
    JCHECK_THROWS(IllegalThreadStateException, main->setDaemon(true));
    JCHECK(Thread::holdsLock(main) == false);
    JSYNC(main) { JCHECK(Thread::holdsLock(main)); }
}

JTEST(ThreadJoinTimeoutAndSleep) {
    auto* release = new CountDownLatch(1);
    Thread* t = new Thread(Runnable::of([release] { release->await(); }));
    t->start();
    int64_t start = nowMs();
    t->join(50);
    int64_t el = nowMs() - start;
    JCHECK(el >= 45);
    JCHECK(t->isAlive());
    release->countDown();
    t->join();
    JCHECK(!t->isAlive());
    start = nowMs();
    Thread::sleep(30);
    JCHECK(nowMs() - start >= 29);
    JCHECK_THROWS(IllegalArgumentException, Thread::sleep(-1));
    JCHECK_THROWS(IllegalArgumentException, t->join(-1));
}

JTEST(ThreadInterruptSleepWaitJoin) {
    // sleep
    std::atomic<int32_t> result{0};
    Thread* sleeper = new Thread(Runnable::of([&] {
        try {
            Thread::sleep(10000);
            result = 1;
        } catch (InterruptedException&) {
            result = Thread::currentThread()->isInterrupted() ? 3 : 2;  // flag cleared -> 2
        }
    }));
    sleeper->start();
    Thread::sleep(50);
    int64_t start = nowMs();
    sleeper->interrupt();
    sleeper->join();
    JCHECK_EQ(result.load(), 2);
    JCHECK(nowMs() - start < 9000);

    // Object.wait
    Object* mon = new Object();
    result = 0;
    Thread* waiter = new Thread(Runnable::of([&] {
        JSYNC(mon) {
            try {
                mon->wait();
                result = 1;
            } catch (InterruptedException&) {
                result = Thread::holdsLock(mon) ? 2 : 3;
            }
        }
    }));
    waiter->start();
    Thread::sleep(50);
    waiter->interrupt();
    waiter->join();
    JCHECK_EQ(result.load(), 2);

    // join
    Thread* forever = new Thread(Runnable::of([] {
        try {
            Thread::sleep(100000);
        } catch (InterruptedException&) {
        }
    }));
    forever->start();
    result = 0;
    Thread* joiner = new Thread(Runnable::of([&] {
        try {
            forever->join();
            result = 1;
        } catch (InterruptedException&) {
            result = 2;
        }
    }));
    joiner->start();
    Thread::sleep(50);
    joiner->interrupt();
    joiner->join();
    JCHECK_EQ(result.load(), 2);
    forever->interrupt();
    forever->join();

    // interrupted() clears the flag; an interrupt before sleep makes sleep throw at once
    Thread::currentThread()->interrupt();
    JCHECK(Thread::currentThread()->isInterrupted());
    JCHECK(Thread::interrupted());
    JCHECK(!Thread::interrupted());
    Thread::currentThread()->interrupt();
    JCHECK_THROWS(InterruptedException, Thread::sleep(1000));
    JCHECK(!Thread::currentThread()->isInterrupted());
}

JTEST(ThreadUncaughtExceptionHandlers) {
    std::atomic<int32_t> calls{0};
    String message;
    Thread* seen = nullptr;
    Thread* t = new Thread(Runnable::of([] { throw IllegalStateException(String("boom")); }), String("thrower"));
    t->setUncaughtExceptionHandler(Thread_UncaughtExceptionHandler::of([&](Thread* th, Throwable* e) {
        seen = th;
        message = e->toString();
        calls++;
    }));
    t->start();
    t->join();
    JCHECK_EQ(calls.load(), 1);
    JCHECK(seen == t);
    JCHECK_EQ(message, String("java.lang.IllegalStateException: boom"));

    // default handler (through the thread group)
    Thread::UncaughtExceptionHandler* old = Thread::getDefaultUncaughtExceptionHandler();
    std::atomic<int32_t> defCalls{0};
    Thread::setDefaultUncaughtExceptionHandler(
        Thread_UncaughtExceptionHandler::of([&](Thread*, Throwable* e) {
            if (instanceof<NullPointerException>(e)) defCalls++;
        }));
    Thread* npe = new Thread(Runnable::of([] {
        Object* volatile o = nullptr;
        (void)o->hashCode();
    }));
    npe->start();
    npe->join();
    JCHECK_EQ(defCalls.load(), 1);
    Thread::setDefaultUncaughtExceptionHandler(old);
}

JTEST(ThreadJoinAllNonDaemon) {
    std::atomic<int32_t> finished{0};
    for (int i = 0; i < 3; i++) {
        (new Thread(Runnable::of([&finished, i] {
            Thread::sleep(50 + 50 * i);
            finished++;
        })))->start();
    }
    Thread* daemon = new Thread(Runnable::of([] {
        try {
            Thread::sleep(100000);
        } catch (InterruptedException&) {
        }
    }));
    daemon->setDaemon(true);
    daemon->start();
    Thread::joinAllNonDaemon();
    JCHECK_EQ(finished.load(), 3);
    JCHECK(daemon->isAlive());
    daemon->interrupt();
    daemon->join();
}

JTEST(ThreadForeignThreadWrapper) {
    // a native thread started without jlang::Thread gets a lazily created wrapper
    struct Arg {
        String name;
        int64_t id = 0;
        bool alive = false;
    };
    Arg* a = new Arg();
    uint64_t h = gc::startNativeThread(
        [](void* p) {
            auto* arg = static_cast<Arg*>(p);
            Thread* t = Thread::currentThread();
            arg->name = t->getName();
            arg->id = t->getId();
            arg->alive = t->isAlive() && Thread::currentThread() == t;
        },
        a);
    gc::joinNativeThread(h);
    JCHECK(a->name.startsWith(String("Thread-")));
    JCHECK(a->id > 1);
    JCHECK(a->alive);
}

// ---------------------------------------------------------------------------------------
JTEST(TimeUnitConversions) {
    JCHECK_EQ(TimeUnit::SECONDS.toMillis(3), INT64_C(3000));
    JCHECK_EQ(TimeUnit::MILLISECONDS.toSeconds(3999), INT64_C(3));
    JCHECK_EQ(TimeUnit::NANOSECONDS.toMillis(-1999999), INT64_C(-1));
    JCHECK_EQ(TimeUnit::DAYS.toHours(2), INT64_C(48));
    JCHECK_EQ(TimeUnit::MILLISECONDS.convert(5, TimeUnit::MINUTES), INT64_C(300000));
    JCHECK_EQ(TimeUnit::DAYS.toNanos(INT64_MAX / 1000), INT64_MAX);
    JCHECK_EQ(TimeUnit::HOURS.toMicros(INT64_MIN / 10), INT64_MIN);
    JCHECK_EQ(TimeUnit::NANOSECONDS.toMillis(INT64_MAX - 1), INT64_C(9223372036854));
    JCHECK_EQ(TimeUnit::SECONDS.name(), String("SECONDS"));
    JCHECK_EQ(TimeUnit::valueOf(String("HOURS")), TimeUnit::HOURS);
    JCHECK_THROWS(IllegalArgumentException, TimeUnit::valueOf(String("WEEKS")));
    JCHECK_EQ(TimeUnit::values()->length, 7);
    TimeUnit u = TimeUnit::MINUTES;
    int32_t k = 0;
    switch (u) {
        case TimeUnit::SECONDS:
            k = 1;
            break;
        case TimeUnit::MINUTES:
            k = 2;
            break;
        default:
            k = 3;
    }
    JCHECK_EQ(k, 2);
    TimeUnit n = nullptr;
    JCHECK(n == nullptr && u != nullptr);
    JCHECK_EQ(str(u), String("MINUTES"));
    Object* boxed = box(TimeUnit::DAYS);
    JCHECK(boxed == box(TimeUnit::DAYS));
    JCHECK_EQ(boxed->toString(), String("DAYS"));
    int64_t start = nowMs();
    TimeUnit::MILLISECONDS.sleep(20);
    JCHECK(nowMs() - start >= 19);
}

// ---------------------------------------------------------------------------------------
JTEST(AtomicsBasicsAndContention) {
    auto* ai = new AtomicInteger(5);
    JCHECK_EQ(ai->incrementAndGet(), 6);
    JCHECK_EQ(ai->getAndIncrement(), 6);
    JCHECK_EQ(ai->get(), 7);
    JCHECK_EQ(ai->addAndGet(-10), -3);
    JCHECK_EQ(ai->getAndAdd(3), -3);
    JCHECK(ai->compareAndSet(0, 42));
    JCHECK(!ai->compareAndSet(0, 43));
    JCHECK_EQ(ai->getAndSet(1), 42);
    JCHECK_EQ(ai->decrementAndGet(), 0);
    JCHECK_EQ(ai->toString(), String("0"));
    ai->set(INT32_MAX);
    JCHECK_EQ(ai->incrementAndGet(), INT32_MIN);  // wraps like Java
    JCHECK_EQ(ai->intValue(), INT32_MIN);
    JCHECK_EQ(ai->updateAndGet([](int32_t x) { return x / 2; }), INT32_MIN / 2);

    auto* al = new AtomicLong();
    JCHECK_EQ(al->addAndGet(INT64_C(1) << 40), INT64_C(1) << 40);
    JCHECK_EQ(al->longValue(), INT64_C(1) << 40);
    auto* ab = new AtomicBoolean();
    JCHECK(ab->compareAndSet(false, true));
    JCHECK(!ab->compareAndSet(false, true));
    JCHECK_EQ(ab->toString(), String("true"));
    Object* o1 = new Object();
    Object* o2 = new Object();
    auto* ar = new AtomicReference<Object*>(o1);
    JCHECK(!ar->compareAndSet(o2, o2));
    JCHECK(ar->compareAndSet(o1, o2));
    JCHECK(ar->get() == o2);
    auto* as = new AtomicReference<String>(String("a"));
    JCHECK(as->compareAndSet(String("a"), String("b")));
    JCHECK_EQ(as->get(), String("b"));
    JCHECK_EQ(as->toString(), String("b"));
    JCHECK_EQ((new AtomicReference<Object*>())->toString(), String("null"));

    auto* counter = new AtomicInteger();
    auto* lcounter = new AtomicLong();
    std::vector<Thread*> ts;
    for (int i = 0; i < 4; i++) {
        Thread* t = new Thread(Runnable::of([=] {
            for (int k = 0; k < 100000; k++) {
                counter->incrementAndGet();
                lcounter->getAndAdd(2);
            }
        }));
        ts.push_back(t);
        t->start();
    }
    for (Thread* t : ts) t->join();
    JCHECK_EQ(counter->get(), 400000);
    JCHECK_EQ(lcounter->get(), INT64_C(800000));
}

// ---------------------------------------------------------------------------------------
JTEST(ReentrantLockBasics) {
    auto* lock = new ReentrantLock();
    JCHECK(!lock->isLocked());
    lock->lock();
    lock->lock();
    JCHECK_EQ(lock->getHoldCount(), 2);
    JCHECK(lock->isHeldByCurrentThread());
    JCHECK(lock->toString().endsWith(String("[Locked by thread main]")));
    std::atomic<int32_t> r{0};
    Thread* other = new Thread(Runnable::of([&] {
        r = lock->tryLock() ? 1 : 2;
        JCHECK(!lock->isHeldByCurrentThread());
        int64_t s = nowMs();
        bool got = lock->tryLock(50, TimeUnit::MILLISECONDS);
        if (!got && nowMs() - s >= 45) r = r + 10;
        try {
            lock->unlock();
        } catch (IllegalMonitorStateException&) {
            r = r + 100;
        }
    }));
    other->start();
    other->join();
    JCHECK_EQ(r.load(), 112);
    lock->unlock();
    lock->unlock();
    JCHECK(!lock->isLocked());
    JCHECK(lock->toString().endsWith(String("[Unlocked]")));
    JCHECK_THROWS(IllegalMonitorStateException, lock->unlock());

    // mutual exclusion under contention
    int64_t plain = 0;
    std::vector<Thread*> ts;
    for (int i = 0; i < 4; i++) {
        Thread* t = new Thread(Runnable::of([&, lock] {
            for (int k = 0; k < 20000; k++) {
                lock->lock();
                plain++;
                lock->unlock();
            }
        }));
        ts.push_back(t);
        t->start();
    }
    for (Thread* t : ts) t->join();
    JCHECK_EQ(plain, INT64_C(80000));

    // lockInterruptibly is interrupted while waiting
    lock->lock();
    r = 0;
    Thread* w = new Thread(Runnable::of([&] {
        try {
            lock->lockInterruptibly();
            r = 1;
        } catch (InterruptedException&) {
            r = 2;
        }
    }));
    w->start();
    Thread::sleep(50);
    JCHECK(lock->hasQueuedThreads());
    w->interrupt();
    w->join();
    JCHECK_EQ(r.load(), 2);
    lock->unlock();
}

JTEST(ConditionSignalAwait) {
    auto* lock = new ReentrantLock();
    Condition* notEmpty = lock->newCondition();
    auto* items = new List<int32_t>();
    std::atomic<int64_t> sum{0};
    const int N = 2000;
    std::vector<Thread*> consumers;
    for (int c = 0; c < 3; c++) {
        Thread* t = new Thread(Runnable::of([&, lock, notEmpty, items] {
            for (;;) {
                lock->lock();
                JFINALLY { lock->unlock(); };
                while (items->isEmpty()) notEmpty->await();
                int32_t v = items->removeAt(0);
                if (v < 0) return;
                sum += v;
            }
        }));
        consumers.push_back(t);
        t->start();
    }
    for (int i = 1; i <= N; i++) {
        lock->lock();
        items->add(i);
        notEmpty->signal();
        lock->unlock();
    }
    lock->lock();
    for (int c = 0; c < 3; c++) items->add(-1);
    notEmpty->signalAll();
    lock->unlock();
    for (Thread* t : consumers) t->join();
    JCHECK_EQ(sum.load(), static_cast<int64_t>(N) * (N + 1) / 2);

    // timed wait and errors
    lock->lock();
    int64_t start = nowMs();
    int64_t rem = notEmpty->awaitNanos(INT64_C(30000000));
    JCHECK(rem <= 0);
    JCHECK(nowMs() - start >= 25);
    JCHECK(!notEmpty->await(10, TimeUnit::MILLISECONDS));
    JCHECK_EQ(lock->getHoldCount(), 1);
    lock->unlock();
    JCHECK_THROWS(IllegalMonitorStateException, notEmpty->signal());
    JCHECK_THROWS(IllegalMonitorStateException, notEmpty->await());

    // await interrupted: InterruptedException after reacquiring the lock
    std::atomic<int32_t> r{0};
    Thread* w = new Thread(Runnable::of([&] {
        lock->lock();
        try {
            notEmpty->await();
            r = 1;
        } catch (InterruptedException&) {
            r = lock->isHeldByCurrentThread() ? 2 : 3;
        }
        lock->unlock();
    }));
    w->start();
    Thread::sleep(50);
    w->interrupt();
    w->join();
    JCHECK_EQ(r.load(), 2);

    // awaitUninterruptibly ignores interrupts but keeps the status
    r = 0;
    Thread* u = new Thread(Runnable::of([&] {
        lock->lock();
        notEmpty->awaitUninterruptibly();
        r = Thread::currentThread()->isInterrupted() ? 2 : 1;
        lock->unlock();
    }));
    u->start();
    Thread::sleep(50);
    u->interrupt();
    Thread::sleep(50);
    JCHECK_EQ(r.load(), 0);
    lock->lock();
    notEmpty->signal();
    lock->unlock();
    u->join();
    JCHECK_EQ(r.load(), 2);
}

JTEST(ReadWriteLock) {
    auto* rw = new ReentrantReadWriteLock();
    Lock* rl = rw->readLock();
    Lock* wl = rw->writeLock();
    // readers hold the lock at the same time: each waits (with the read lock held) for all
    auto* allIn = new CountDownLatch(3);
    std::atomic<int32_t> together{0};
    std::vector<Thread*> readers;
    for (int i = 0; i < 3; i++) {
        Thread* t = new Thread(Runnable::of([&, rl, allIn] {
            rl->lock();
            allIn->countDown();
            if (allIn->await(20, TimeUnit::SECONDS)) together++;
            rl->unlock();
        }));
        readers.push_back(t);
        t->start();
    }
    for (Thread* t : readers) t->join();
    JCHECK_EQ(together.load(), 3);

    // writer exclusive, reentrant, downgrade
    wl->lock();
    wl->lock();
    JCHECK_EQ(rw->getWriteHoldCount(), 2);
    JCHECK(rw->isWriteLockedByCurrentThread());
    rl->lock();  // downgrade
    wl->unlock();
    wl->unlock();
    JCHECK(!rw->isWriteLocked());
    JCHECK_EQ(rw->getReadLockCount(), 1);
    JCHECK_EQ(rw->getReadHoldCount(), 1);
    std::atomic<int32_t> r{0};
    Thread* w = new Thread(Runnable::of([&] {
        r = wl->tryLock() ? 1 : 2;
        bool got = wl->tryLock(30, TimeUnit::MILLISECONDS);
        if (!got) r = r + 10;
    }));
    w->start();
    w->join();
    JCHECK_EQ(r.load(), 12);
    rl->unlock();
    JCHECK_THROWS(IllegalMonitorStateException, rl->unlock());
    JCHECK_THROWS(IllegalMonitorStateException, wl->unlock());
    JCHECK_THROWS(UnsupportedOperationException, rl->newCondition());

    // counter protected by the write lock, readers check consistency
    int64_t a = 0, b = 0;
    std::atomic<bool> bad{false};
    std::vector<Thread*> ts;
    for (int i = 0; i < 3; i++) {
        Thread* t = new Thread(Runnable::of([&, wl] {
            for (int k = 0; k < 5000; k++) {
                wl->lock();
                a++;
                b++;
                wl->unlock();
            }
        }));
        ts.push_back(t);
        t->start();
    }
    for (int i = 0; i < 3; i++) {
        Thread* t = new Thread(Runnable::of([&, rl] {
            for (int k = 0; k < 5000; k++) {
                rl->lock();
                rl->lock();  // reentrant read
                if (a != b) bad = true;
                rl->unlock();
                rl->unlock();
            }
        }));
        ts.push_back(t);
        t->start();
    }
    for (Thread* t : ts) t->join();
    JCHECK(!bad.load());
    JCHECK_EQ(a, INT64_C(15000));
    JCHECK(rw->toString().endsWith(String("[Write locks = 0, Read locks = 0]")));
}

JTEST(CountDownLatchBasics) {
    auto* latch = new CountDownLatch(3);
    std::atomic<int32_t> passed{0};
    std::vector<Thread*> ts;
    for (int i = 0; i < 4; i++) {
        Thread* t = new Thread(Runnable::of([&, latch] {
            latch->await();
            passed++;
        }));
        ts.push_back(t);
        t->start();
    }
    Thread::sleep(30);
    JCHECK_EQ(passed.load(), 0);
    latch->countDown();
    latch->countDown();
    JCHECK_EQ(latch->getCount(), INT64_C(1));
    JCHECK(!latch->await(20, TimeUnit::MILLISECONDS));
    latch->countDown();
    latch->countDown();
    for (Thread* t : ts) t->join();
    JCHECK_EQ(passed.load(), 4);
    JCHECK(latch->await(0, TimeUnit::SECONDS));
    JCHECK(latch->toString().endsWith(String("[Count = 0]")));
    JCHECK_THROWS(IllegalArgumentException, new CountDownLatch(-1));
}

// ---------------------------------------------------------------------------------------
JTEST(BlockingQueuesContention) {
    auto* q = new LinkedBlockingQueue<Integer*>();
    const int P = 4, PER = 5000;
    std::atomic<int64_t> sum{0};
    std::atomic<int32_t> got{0};
    std::vector<Thread*> ts;
    for (int p = 0; p < P; p++) {
        Thread* t = new Thread(Runnable::of([=] {
            for (int k = 1; k <= PER; k++) q->put(Integer::valueOf(k));
        }));
        ts.push_back(t);
        t->start();
    }
    for (int c = 0; c < 3; c++) {
        Thread* t = new Thread(Runnable::of([&, q] {
            for (;;) {
                Integer* v = q->take();
                if (v->intValue() < 0) return;
                sum += v->intValue();
                got++;
            }
        }));
        ts.push_back(t);
        t->start();
    }
    for (int i = 0; i < P; i++) ts[static_cast<size_t>(i)]->join();
    for (int c = 0; c < 3; c++) q->put(Integer::valueOf(-1));
    for (Thread* t : ts) t->join();
    JCHECK_EQ(got.load(), P * PER);
    JCHECK_EQ(sum.load(), static_cast<int64_t>(P) * PER * (PER + 1) / 2);
    JCHECK(q->isEmpty());

    // bounded queue: offer fails when full, put blocks, poll(timeout)
    auto* aq = new ArrayBlockingQueue<Integer*>(2);
    JCHECK(aq->offer(Integer::valueOf(1)));
    JCHECK(aq->offer(Integer::valueOf(2)));
    JCHECK(!aq->offer(Integer::valueOf(3)));
    JCHECK_THROWS(IllegalStateException, aq->add(Integer::valueOf(3)));
    JCHECK_EQ(aq->remainingCapacity(), 0);
    JCHECK(!aq->offer(Integer::valueOf(3), 20, TimeUnit::MILLISECONDS));
    std::atomic<bool> putDone{false};
    Thread* putter = new Thread(Runnable::of([&, aq] {
        aq->put(Integer::valueOf(3));
        putDone = true;
    }));
    putter->start();
    Thread::sleep(50);
    JCHECK(!putDone.load());
    JCHECK_EQ(aq->poll()->intValue(), 1);
    putter->join();
    JCHECK(putDone.load());
    JCHECK_EQ(aq->toString(), String("[2, 3]"));
    JCHECK_EQ(aq->take()->intValue(), 2);
    JCHECK_EQ(aq->remove()->intValue(), 3);
    JCHECK_THROWS(NoSuchElementException, aq->remove());
    JCHECK(aq->poll() == nullptr);
    int64_t start = nowMs();
    JCHECK(aq->poll(30, TimeUnit::MILLISECONDS) == nullptr);
    JCHECK(nowMs() - start >= 25);
    JCHECK_THROWS(NullPointerException, aq->offer(nullptr));

    // take interrupted
    std::atomic<int32_t> r{0};
    Thread* taker = new Thread(Runnable::of([&, aq] {
        try {
            aq->take();
            r = 1;
        } catch (InterruptedException&) {
            r = 2;
        }
    }));
    taker->start();
    Thread::sleep(30);
    taker->interrupt();
    taker->join();
    JCHECK_EQ(r.load(), 2);

    // drainTo and iterator remove
    auto* lq = new LinkedBlockingQueue<String>();
    for (int i = 0; i < 5; i++) lq->add(str("s", i));
    Iterator<String>* it = lq->iterator();
    while (it->hasNext())
        if (it->next().equals(String("s2"))) it->remove();
    auto* out = new List<String>();
    JCHECK_EQ(lq->drainTo(out, 2), 2);
    JCHECK_EQ(out->toString(), String("[s0, s1]"));
    JCHECK_EQ(lq->toString(), String("[s3, s4]"));
    JCHECK(lq->contains(String("s4")));
    JCHECK(lq->remove(String("s4")));
    JCHECK_EQ(lq->size(), 1);
}

JTEST(SynchronousQueueHandoff) {
    auto* q = new SynchronousQueue<Integer*>();
    JCHECK(!q->offer(Integer::valueOf(1)));  // nobody waiting
    JCHECK(q->poll() == nullptr);
    JCHECK_EQ(q->size(), 0);
    std::atomic<int32_t> received{0};
    Thread* consumer = new Thread(Runnable::of([&, q] {
        for (int i = 0; i < 100; i++) received += q->take()->intValue();
    }));
    consumer->start();
    for (int i = 1; i <= 100; i++) q->put(Integer::valueOf(i));
    consumer->join();
    JCHECK_EQ(received.load(), 5050);
    // offer succeeds once a consumer waits
    Integer* got = nullptr;
    Thread* c2 = new Thread(Runnable::of([&, q] { got = q->poll(2, TimeUnit::SECONDS); }));
    c2->start();
    JCHECK(waitUntil([&] { return q->offer(Integer::valueOf(7)); }));
    c2->join();
    JCHECK(got != nullptr && got->intValue() == 7);
    JCHECK(!q->offer(Integer::valueOf(8), 20, TimeUnit::MILLISECONDS));
}

namespace {
struct Prio : public virtual Object, public virtual Comparable<Prio*> {
    explicit Prio(int32_t p, int32_t id = 0) : prio(p), id(id) {}
    int32_t compareTo(Prio* o) override { return prio - o->prio; }
    bool equals(Object* o) override {
        auto* p = dynamic_cast<Prio*>(o);
        return p != nullptr && p->id == id;
    }
    int32_t hashCode() override { return id; }
    String toString() override { return str(prio); }
    int32_t prio, id;
};

// Like the gameserver's DesireQueue: overrides add() and clear(), uses super.remove/poll.
class MergingQueue : public PriorityBlockingQueue<Prio*> {
public:
    bool add(Prio* const& p) override {
        Iterator<Prio*>* it = PriorityBlockingQueue<Prio*>::iterator();
        while (it->hasNext()) {
            Prio* x = it->next();
            if (p->equals(x)) {
                PriorityBlockingQueue<Prio*>::remove(p);
                if (p != x) p->prio += x->prio;
                break;
            }
        }
        return PriorityBlockingQueue<Prio*>::add(p);
    }
    void clear() override {
        Prio* p;
        while ((p = PriorityBlockingQueue<Prio*>::poll()) != nullptr) cleared++;
    }
    int32_t cleared = 0;
};
}  // namespace

JTEST(PriorityBlockingQueueOrderAndSubclass) {
    auto* q = new PriorityBlockingQueue<Prio*>();
    int32_t vals[] = {5, 1, 9, 3, 7, 2, 8};
    for (int32_t v : vals) q->add(new Prio(v));
    JCHECK_EQ(q->peek()->prio, 1);
    std::string order;
    while (!q->isEmpty()) order += std::to_string(q->poll()->prio);
    JCHECK_EQ(order, std::string("1235789"));
    // comparator (reverse)
    auto* rq = new PriorityBlockingQueue<int32_t>(11, Comparator<int32_t>::of([](int32_t a, int32_t b) { return b - a; }));
    for (int32_t v : vals) rq->offer(v);
    JCHECK_EQ(rq->take(), 9);
    JCHECK_EQ(rq->poll(), 8);
    // natural order of Strings, Java iteration (heap array) order
    auto* sq = new PriorityBlockingQueue<String>();
    for (const char* s : {"d", "b", "e", "a", "c"}) sq->add(String(s));
    JCHECK_EQ(sq->toString(), String("[a, b, e, d, c]"));  // same heap layout as Java
    // blocking take is woken by an offer
    Prio* taken = nullptr;
    Thread* t = new Thread(Runnable::of([&, q] { taken = q->take(); }));
    t->start();
    Thread::sleep(30);
    q->add(new Prio(42));
    t->join();
    JCHECK(taken != nullptr && taken->prio == 42);

    auto* mq = new MergingQueue();
    mq->add(new Prio(10, 1));
    mq->add(new Prio(20, 2));
    mq->add(new Prio(5, 1));  // merges with id 1 -> prio 15
    JCHECK_EQ(mq->size(), 2);
    JCHECK_EQ(mq->peek()->prio, 15);
    Iterator<Prio*>* it = mq->iterator();
    while (it->hasNext())
        if (it->next()->id == 2) it->remove();
    JCHECK_EQ(mq->size(), 1);
    mq->add(new Prio(1, 3));
    mq->clear();
    JCHECK_EQ(mq->cleared, 2);
    JCHECK(mq->isEmpty());
}

// ---------------------------------------------------------------------------------------
JTEST(FutureTaskSemantics) {
    auto* f = new FutureTask(Callable<int32_t>::of([] { return 42; }));
    JCHECK(!f->isDone());
    (new Thread(f))->start();
    Object* v = f->get();
    JCHECK(f->isDone());
    JCHECK_EQ(unbox<int32_t>(v), 42);

    auto* g = new FutureTask(Callable<Object*>::of([]() -> Object* { throw IllegalArgumentException(String("bad")); }));
    g->run();
    try {
        g->get();
        JCHECK(false);
    } catch (ExecutionException& e) {
        JCHECK(e.getCause() != nullptr && instanceof<IllegalArgumentException>(e.getCause()));
        JCHECK_EQ(e.getCause()->getMessage(), String("bad"));
    }

    // cancel(true) interrupts the running task
    std::atomic<int32_t> r{0};
    auto* h = new FutureTask(Runnable::of([&] {
        try {
            Thread::sleep(10000);
            r = 1;
        } catch (InterruptedException&) {
            r = 2;
        }
    }), nullptr);
    Thread* runner = new Thread(h);
    runner->start();
    Thread::sleep(30);
    JCHECK(h->cancel(true));
    runner->join();
    JCHECK_EQ(r.load(), 2);
    JCHECK(h->isCancelled() && h->isDone());
    JCHECK(!h->cancel(true));
    JCHECK_THROWS(CancellationException, h->get());

    auto* gate = new CountDownLatch(1);
    auto* slow = new FutureTask(Runnable::of([gate] { gate->await(); }), box(String("done")));
    (new Thread(slow))->start();
    JCHECK_THROWS(TimeoutException, slow->get(20, TimeUnit::MILLISECONDS));
    gate->countDown();
    JCHECK_EQ(slow->get()->toString(), String("done"));
}

// ---------------------------------------------------------------------------------------
JTEST(ThreadPoolExecutorSizing) {
    // unbounded LinkedBlockingQueue: exactly corePoolSize threads
    auto* tpe = new ThreadPoolExecutor(2, 10, 1, TimeUnit::SECONDS, new LinkedBlockingQueue<Runnable*>());
    std::atomic<int32_t> done{0};
    auto* names = (new Map<String, int32_t>())->shared();
    for (int i = 0; i < 50; i++) {
        tpe->execute(Runnable::of([&, names] {
            names->put(Thread::currentThread()->getName(), 1);
            Thread::sleep(1);
            done++;
        }));
    }
    JCHECK(waitUntil([&] { return done.load() == 50; }));
    JCHECK_EQ(tpe->getPoolSize(), 2);
    JCHECK_EQ(tpe->getLargestPoolSize(), 2);
    JCHECK_EQ(names->size(), 2);
    JCHECK(names->containsKey(String("pool-1-thread-1")) || names->keySet()->get(0).startsWith(String("pool-")));
    JCHECK(waitUntil([&] { return tpe->getCompletedTaskCount() == 50 && tpe->getActiveCount() == 0; }));
    JCHECK_EQ(tpe->getTaskCount(), INT64_C(50));
    JCHECK(tpe->toString().contains(String("[Running, pool size = 2, active threads = 0, queued tasks = 0, completed tasks = 50]")));
    tpe->shutdown();
    JCHECK(tpe->isShutdown());
    JCHECK(tpe->awaitTermination(5, TimeUnit::SECONDS));
    JCHECK(tpe->isTerminated());
    JCHECK_EQ(tpe->getPoolSize(), 0);
    JCHECK_THROWS(RejectedExecutionException, tpe->execute(Runnable::of([] {})));

    // bounded queue: extra threads only when the queue is full; then rejection
    auto* gate = new CountDownLatch(1);
    auto* b = new ThreadPoolExecutor(1, 2, 1, TimeUnit::SECONDS, new ArrayBlockingQueue<Runnable*>(1));
    auto blocker = Runnable::of([gate] { gate->await(); });
    b->execute(blocker);  // thread 1
    b->execute(blocker);  // queued
    JCHECK_EQ(b->getPoolSize(), 1);
    b->execute(blocker);  // queue full -> thread 2
    JCHECK_EQ(b->getPoolSize(), 2);
    JCHECK_THROWS(RejectedExecutionException, b->execute(blocker));
    std::atomic<bool> callerRan{false};
    b->setRejectedExecutionHandler(new ThreadPoolExecutor::CallerRunsPolicy());
    b->execute(Runnable::of([&] { callerRan = Thread::currentThread()->getName() == String("main"); }));
    JCHECK(callerRan.load());
    gate->countDown();
    b->shutdown();
    JCHECK(b->awaitTermination(5, TimeUnit::SECONDS));

    // SynchronousQueue (cached pool): grows, then shrinks after keepAlive
    auto* cached = new ThreadPoolExecutor(0, 100, 100, TimeUnit::MILLISECONDS, new SynchronousQueue<Runnable*>());
    auto* gate2 = new CountDownLatch(1);
    for (int i = 0; i < 5; i++) cached->execute(Runnable::of([gate2] { gate2->await(); }));
    JCHECK_EQ(cached->getPoolSize(), 5);
    gate2->countDown();
    JCHECK(waitUntil([&] { return cached->getPoolSize() == 0; }));
    cached->execute(Runnable::of([] {}));
    JCHECK(waitUntil([&] { return cached->getCompletedTaskCount() == 6; }));
    cached->shutdown();
    JCHECK(cached->awaitTermination(5, TimeUnit::SECONDS));

    // prestart
    auto* pre = new ThreadPoolExecutor(3, 3, 0, TimeUnit::SECONDS, new LinkedBlockingQueue<Runnable*>());
    JCHECK_EQ(pre->prestartAllCoreThreads(), 3);
    JCHECK_EQ(pre->getPoolSize(), 3);
    JCHECK_EQ(pre->prestartAllCoreThreads(), 0);
    pre->shutdown();
    JCHECK(pre->awaitTermination(5, TimeUnit::SECONDS));
}

JTEST(ThreadPoolExecutorExceptionsAndShutdownNow) {
    // an exception in an execute()d task kills its worker (uncaught handler) and a new one
    // replaces it; submit() captures the exception in the Future.
    std::atomic<int32_t> handled{0};
    ThreadFactory* tf = ThreadFactory::of([&](Runnable* r) {
        Thread* t = new Thread(r);
        t->setUncaughtExceptionHandler(Thread_UncaughtExceptionHandler::of([&](Thread*, Throwable* e) {
            if (e->getMessage() == String("task failed")) handled++;
        }));
        return t;
    });
    auto* tpe = new ThreadPoolExecutor(1, 1, 0, TimeUnit::SECONDS, new LinkedBlockingQueue<Runnable*>(), tf);
    tpe->execute(Runnable::of([] { throw RuntimeException(String("task failed")); }));
    std::atomic<bool> after{false};
    tpe->execute(Runnable::of([&] { after = true; }));
    JCHECK(waitUntil([&] { return after.load(); }));
    JCHECK_EQ(handled.load(), 1);
    Future* f = tpe->submit(Runnable::of([] { throw IllegalStateException(String("in future")); }));
    JCHECK_THROWS(ExecutionException, f->get());
    Future* ok = tpe->submit(Callable<String>::of([] { return String("result"); }));
    JCHECK_EQ(ok->get()->toString(), String("result"));
    JCHECK_EQ(handled.load(), 1);

    // shutdownNow: interrupts the running task and returns the queued ones
    std::atomic<int32_t> interrupted{0};
    auto* started = new CountDownLatch(1);
    tpe->execute(Runnable::of([&, started] {
        started->countDown();
        try {
            Thread::sleep(100000);
        } catch (InterruptedException&) {
            interrupted++;
        }
    }));
    for (int i = 0; i < 3; i++) tpe->execute(Runnable::of([] {}));
    JCHECK(started->await(20, TimeUnit::SECONDS));
    List<Runnable*>* left = tpe->shutdownNow();
    JCHECK_EQ(left->size(), 3);
    JCHECK(tpe->awaitTermination(5, TimeUnit::SECONDS));
    JCHECK_EQ(interrupted.load(), 1);

    // purge removes cancelled futures from the queue
    auto* one = new ThreadPoolExecutor(1, 1, 0, TimeUnit::SECONDS, new LinkedBlockingQueue<Runnable*>());
    auto* gate = new CountDownLatch(1);
    one->execute(Runnable::of([gate] { gate->await(); }));
    Future* c1 = one->submit(Runnable::of([] {}));
    Future* c2 = one->submit(Runnable::of([] {}));
    JCHECK_EQ(one->getQueue()->size(), 2);
    c1->cancel(false);
    one->purge();
    JCHECK_EQ(one->getQueue()->size(), 1);
    gate->countDown();
    c2->get();
    one->shutdown();
    JCHECK(one->awaitTermination(5, TimeUnit::SECONDS));
}

JTEST(ExecutorsFactories) {
    ExecutorService* fixed = Executors::newFixedThreadPool(3);
    auto* tpe = cast<ThreadPoolExecutor>(fixed);
    JCHECK_EQ(tpe->getCorePoolSize(), 3);
    JCHECK_EQ(tpe->getMaximumPoolSize(), 3);
    std::vector<Future*> fs;
    for (int i = 0; i < 20; i++) fs.push_back(fixed->submit(Callable<int32_t>::of([i] { return i * i; })));
    int64_t sum = 0;
    for (Future* f : fs) sum += unbox<int32_t>(f->get());
    JCHECK_EQ(sum, INT64_C(2470));
    fixed->shutdown();
    JCHECK(fixed->awaitTermination(5, TimeUnit::SECONDS));

    ExecutorService* cached = Executors::newCachedThreadPool();
    auto* latch = new CountDownLatch(10);
    for (int i = 0; i < 10; i++) cached->execute(Runnable::of([latch] { latch->countDown(); }));
    JCHECK(latch->await(5, TimeUnit::SECONDS));
    cached->shutdown();
    JCHECK(cached->awaitTermination(5, TimeUnit::SECONDS));

    ThreadFactory* tf = Executors::defaultThreadFactory();
    Thread* t = tf->newThread(Runnable::of([] {}));
    JCHECK(t->getName().startsWith(String("pool-")));
    JCHECK(t->getName().endsWith(String("-thread-1")));
    JCHECK(!t->isDaemon());
    JCHECK_EQ(t->getPriority(), Thread::NORM_PRIORITY);
    Callable<Object*>* c = Executors::callable(Runnable::of([] {}), box(5));
    JCHECK_EQ(unbox<int32_t>(c->call()), 5);
}

// ---------------------------------------------------------------------------------------
JTEST(ScheduledExecutorOrderingAndCancel) {
    // one worker, blocked by a gate while the tasks are queued: due tasks run in time order
    auto* stpe = new ScheduledThreadPoolExecutor(1);
    auto* order = (new List<int32_t>())->synchronized_();
    auto* gate = new CountDownLatch(1);
    auto* gateTaken = new CountDownLatch(1);
    stpe->execute(Runnable::of([gate, gateTaken] {
        gateTaken->countDown();
        gate->await();
    }));
    JCHECK(gateTaken->await(20, TimeUnit::SECONDS));
    stpe->schedule(Runnable::of([order] { order->add(3); }), 150, TimeUnit::MILLISECONDS);
    stpe->schedule(Runnable::of([order] { order->add(1); }), 50, TimeUnit::MILLISECONDS);
    stpe->schedule(Runnable::of([order] { order->add(2); }), 100, TimeUnit::MILLISECONDS);
    ScheduledFuture* cancelled = stpe->schedule(Runnable::of([order] { order->add(99); }), 120, TimeUnit::MILLISECONDS);
    JCHECK_EQ(stpe->getQueue()->size(), 4);
    int64_t d = cancelled->getDelay(TimeUnit::MILLISECONDS);
    JCHECK(d <= 120);
    JCHECK(cancelled->cancel(false));
    JCHECK_EQ(stpe->getQueue()->size(), 4);  // removeOnCancel is off by default
    stpe->purge();
    JCHECK_EQ(stpe->getQueue()->size(), 3);
    gate->countDown();
    JCHECK(waitUntil([&] { return order->size() == 3; }));
    Thread::sleep(50);
    JCHECK_EQ(order->toString(), String("[1, 2, 3]"));

    stpe->setRemoveOnCancelPolicy(true);
    ScheduledFuture* f = stpe->schedule(Runnable::of([] {}), 10, TimeUnit::SECONDS);
    JCHECK_EQ(stpe->getQueue()->size(), 1);
    f->cancel(false);
    JCHECK_EQ(stpe->getQueue()->size(), 0);
    JCHECK(f->isCancelled());
    JCHECK_THROWS(CancellationException, f->get());

    // Callable scheduling, compareTo, execute == schedule(0)
    ScheduledFuture* c = stpe->schedule(Callable<String>::of([] { return String("x"); }), 20, TimeUnit::MILLISECONDS);
    ScheduledFuture* later = stpe->schedule(Runnable::of([] {}), 1, TimeUnit::SECONDS);
    JCHECK(c->compareTo(later) < 0 && later->compareTo(c) > 0 && c->compareTo(c) == 0);
    JCHECK_EQ(c->get()->toString(), String("x"));
    later->cancel(false);
    std::atomic<bool> ran{false};
    stpe->execute(Runnable::of([&] { ran = true; }));
    JCHECK(waitUntil([&] { return ran.load(); }));
    stpe->shutdown();
    JCHECK(stpe->awaitTermination(5, TimeUnit::SECONDS));
}

JTEST(ScheduledExecutorFixedRateVsFixedDelay) {
    auto* stpe = new ScheduledThreadPoolExecutor(3);
    // Fixed rate: run k never starts before start0 + k*period, and after a slow run the late
    // runs start back to back (catch-up). Fixed delay: each run starts >= delay after the
    // previous one ended. Runs of one task never overlap.
    const int64_t P = 100;
    auto* rateStarts = (new List<int64_t>())->synchronized_();
    auto* delayStarts = (new List<int64_t>())->synchronized_();
    auto* delayEnds = (new List<int64_t>())->synchronized_();
    std::atomic<int32_t> inRate{0}, inDelay{0};
    std::atomic<bool> overlap{false};
    const int64_t t0 = nowMs();  // <= the first trigger time
    ScheduledFuture* rate = stpe->scheduleAtFixedRate(Runnable::of([&, rateStarts] {
        if (++inRate > 1) overlap = true;
        rateStarts->add(nowMs());
        if (rateStarts->size() == 1) Thread::sleep(5 * P);  // the first run is slow
        --inRate;
    }), 0, P, TimeUnit::MILLISECONDS);
    ScheduledFuture* delay = stpe->scheduleWithFixedDelay(Runnable::of([&, delayStarts, delayEnds] {
        if (++inDelay > 1) overlap = true;
        delayStarts->add(nowMs());
        Thread::sleep(20);
        delayEnds->add(nowMs());
        --inDelay;
    }), 0, P / 2, TimeUnit::MILLISECONDS);
    JCHECK(waitUntil([&] { return rateStarts->size() >= 8 && delayEnds->size() >= 5; }));
    rate->cancel(false);
    delay->cancel(false);
    JCHECK(!overlap.load());
    for (int32_t k = 0; k < rateStarts->size(); k++) JCHECK(rateStarts->get(k) >= t0 + k * P - 2);
    // catch-up: runs 1..4 were already due when the slow first run ended, so they run back to
    // back instead of P apart
    JCHECK(rateStarts->get(4) - rateStarts->get(1) < 2 * P);
    for (int32_t k = 1; k < delayStarts->size() && k < delayEnds->size(); k++)
        JCHECK(delayStarts->get(k) >= delayEnds->get(k - 1) + P / 2 - 2);
    JCHECK(rate->isCancelled() && rate->isDone());

    // a periodic task that throws is not run again; its future holds the exception
    std::atomic<int32_t> runs{0};
    ScheduledFuture* failing = stpe->scheduleAtFixedRate(Runnable::of([&] {
        if (++runs == 3) throw IllegalStateException(String("third run"));
    }), 0, 10, TimeUnit::MILLISECONDS);
    try {
        failing->get();
        JCHECK(false);
    } catch (ExecutionException& e) {
        JCHECK_EQ(e.getCause()->getMessage(), String("third run"));
    }
    Thread::sleep(60);
    JCHECK_EQ(runs.load(), 3);
    JCHECK(failing->isDone() && !failing->isCancelled());
    JCHECK_THROWS(IllegalArgumentException, stpe->scheduleAtFixedRate(Runnable::of([] {}), 0, 0, TimeUnit::SECONDS));
    JCHECK_THROWS(NullPointerException, stpe->schedule(static_cast<Runnable*>(nullptr), 0, TimeUnit::SECONDS));
    stpe->shutdown();
    JCHECK(stpe->awaitTermination(5, TimeUnit::SECONDS));
}

JTEST(ScheduledExecutorShutdownPolicies) {
    // default: delayed tasks still run after shutdown, periodic ones are cancelled
    auto* stpe = new ScheduledThreadPoolExecutor(1);
    std::atomic<bool> delayedRan{false};
    std::atomic<int32_t> periodicRuns{0};
    stpe->schedule(Runnable::of([&] { delayedRan = true; }), 400, TimeUnit::MILLISECONDS);
    ScheduledFuture* p = stpe->scheduleAtFixedRate(Runnable::of([&] { periodicRuns++; }), 0, 10, TimeUnit::MILLISECONDS);
    Thread::sleep(35);
    stpe->shutdown();
    JCHECK(p->isCancelled());
    int32_t runsAtShutdown = periodicRuns.load();
    if (!delayedRan.load()) JCHECK(!stpe->isTerminated());
    JCHECK(stpe->awaitTermination(5, TimeUnit::SECONDS));
    JCHECK(delayedRan.load());
    JCHECK(periodicRuns.load() <= runsAtShutdown + 1);
    JCHECK_THROWS(RejectedExecutionException, stpe->schedule(Runnable::of([] {}), 0, TimeUnit::SECONDS));

    // ThreadPoolManager's shutdown: drop the delayed tasks after shutdown
    auto* s2 = new ScheduledThreadPoolExecutor(1);
    std::atomic<bool> ran2{false};
    ScheduledFuture* longDelay = s2->schedule(Runnable::of([&] { ran2 = true; }), 10, TimeUnit::SECONDS);
    s2->shutdown();
    JCHECK(!s2->awaitTermination(50, TimeUnit::MILLISECONDS));
    s2->setExecuteExistingDelayedTasksAfterShutdownPolicy(false);
    s2->setContinueExistingPeriodicTasksAfterShutdownPolicy(false);
    JCHECK(s2->awaitTermination(5, TimeUnit::SECONDS));
    JCHECK(!ran2.load());
    JCHECK(longDelay->isCancelled());
    JCHECK(s2->toString().contains(String("ScheduledThreadPoolExecutor@")));
    JCHECK(s2->toString().contains(String("[Terminated, pool size = ")));  // workers may still be exiting (as in Java)
    JCHECK_EQ(s2->getPoolSize(), 0);
}

// ---------------------------------------------------------------------------------------
JTEST(TimerAndTimerTask) {
    class Task : public TimerTask {
    public:
        explicit Task(std::atomic<int32_t>* c, bool fail = false) : c_(c), fail_(fail) {}
        void run() override {
            ++*c_;
            if (fail_) throw RuntimeException(String("timer task failed"));
        }

    private:
        std::atomic<int32_t>* c_;
        bool fail_;
    };
    Timer* timer = new Timer(String("test-timer"), true);
    std::atomic<int32_t> once{0}, rate{0};
    timer->schedule(new Task(&once), 20);
    Task* periodic = new Task(&rate);
    timer->scheduleAtFixedRate(periodic, 0, 20);
    JCHECK(waitUntil([&] { return once.load() == 1 && rate.load() >= 5; }));
    JCHECK(periodic->cancel());
    int32_t n = rate.load();
    Thread::sleep(60);
    JCHECK(rate.load() <= n + 1);
    JCHECK_EQ(once.load(), 1);
    JCHECK_THROWS(IllegalStateException, timer->schedule(periodic, 10));  // already cancelled
    JCHECK_EQ(timer->purge(), 0);
    timer->cancel();
    JCHECK_THROWS(IllegalStateException, timer->schedule(new Task(&once), 10));

    // a task that throws kills the timer
    Timer* t2 = new Timer(true);
    std::atomic<int32_t> c{0};
    Thread::UncaughtExceptionHandler* old = Thread::getDefaultUncaughtExceptionHandler();
    Thread::setDefaultUncaughtExceptionHandler(Thread_UncaughtExceptionHandler::of([](Thread*, Throwable*) {}));
    t2->schedule(new Task(&c, true), 0);
    JCHECK(waitUntil([&] {
        try {
            t2->schedule(new Task(&c), 100000);
            return false;
        } catch (IllegalStateException&) {
            return true;  // the timer thread died
        }
    }));
    JCHECK_EQ(c.load(), 1);
    Thread::setDefaultUncaughtExceptionHandler(old);
}

// ---------------------------------------------------------------------------------------
// GC safety: objects referenced only from queued tasks, thread stacks and blocking queues
// survive collections; threads allocating heavily while collections run.
JTEST(ThreadGcSafety) {
    auto* stpe = new ScheduledThreadPoolExecutor(3);
    const int N = 2000;
    auto* bad = new AtomicInteger();
    auto* ok = new AtomicInteger();
    for (int i = 0; i < N; i++) {
        auto* p = new Payload(i);
        p->list = new List<Integer*>();
        for (int k = 0; k < 5; k++) p->list->add(Integer::valueOf(1000 + i * 7 + k));
        // p is only reachable from the scheduled task
        stpe->schedule(Runnable::of([p, i, bad, ok] {
            bool good = p->value == i && p->text.equals(str("payload-", i)) && p->list->size() == 5 &&
                        p->list->get(4)->intValue() == 1004 + i * 7;
            if (good) ok->incrementAndGet();
            else bad->incrementAndGet();
        }), 150 + (i % 50), TimeUnit::MILLISECONDS);
    }
    // churn the heap and force collections while the tasks wait
    for (int round = 0; round < 5; round++) {
        auto* garbage = new List<String>();
        for (int k = 0; k < 20000; k++) garbage->add(str("garbage-", k));
        System::gc();
    }
    JCHECK(waitUntil([&] { return ok->get() + bad->get() == N; }, 60000));
    JCHECK_EQ(bad->get(), 0);
    JCHECK_EQ(ok->get(), N);

    // worker threads allocate heavily while other threads force collections; data held only
    // on the workers' stacks and in a blocking queue must survive
    auto* q = new LinkedBlockingQueue<Payload*>();
    std::atomic<bool> stop{false};
    Thread* collector = new Thread(Runnable::of([&] {
        while (!stop.load()) {
            System::gc();
            Thread::sleep(5);
        }
    }));
    collector->start();
    std::vector<Thread*> ts;
    auto* errors = new AtomicInteger();
    for (int w = 0; w < 4; w++) {
        Thread* t = new Thread(Runnable::of([=] {
            for (int k = 0; k < 3000; k++) {
                auto* local = new Payload(k);
                local->list = new List<Integer*>();
                for (int j = 0; j < 20; j++) local->list->add(Integer::valueOf(j * 100000 + k));
                String s = str("stack-", w, "-", k);
                if (k % 3 == 0) q->put(new Payload(w * 100000 + k));
                for (int j = 0; j < 20; j++) (void)new Payload(j);  // garbage
                if (local->value != k || local->list->get(19)->intValue() != 1900000 + k ||
                    !s.equals(str("stack-", w, "-", k)))
                    errors->incrementAndGet();
            }
        }));
        ts.push_back(t);
        t->start();
    }
    for (Thread* t : ts) t->join();
    stop = true;
    collector->join();
    JCHECK_EQ(errors->get(), 0);
    JCHECK_EQ(q->size(), 4 * 1000);
    int64_t sum = 0;
    while (!q->isEmpty()) {
        Payload* p = q->take();
        if (!p->text.equals(str("payload-", p->value))) errors->incrementAndGet();
        sum += p->value;
    }
    JCHECK_EQ(errors->get(), 0);
    int64_t expected = 0;
    for (int w = 0; w < 4; w++)
        for (int k = 0; k < 3000; k += 3) expected += w * 100000 + k;
    JCHECK_EQ(sum, expected);
    stpe->shutdown();
    JCHECK(stpe->awaitTermination(5, TimeUnit::SECONDS));
}
