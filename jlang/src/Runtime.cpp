// jlang runtime: init(), fault handlers (NPE / ArithmeticException), collector services and
// thread interruption support.
#define GC_THREADS
#define GC_NO_THREAD_REDIRECTS
#include <gc/gc.h>

#include <jlang/Exceptions.h>
#include <jlang/Runtime.h>
#include <jlang/String.h>
#include <jlang/System.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <mutex>
#include <pthread.h>
#include <ucontext.h>
#include <unistd.h>

namespace jlang {

namespace detail {
void gcInitSlow() noexcept;
void setFaultPc(void* pc) noexcept;
void initSystemProperties(int argc, char** argv);

bool inGcHeap(const void* p) noexcept {
    gc::ensureInitialized();
    return GC_base(const_cast<void*>(p)) != nullptr;
}

void* gcAllocCell(size_t n, bool uncollectable) {
    gc::ensureInitialized();
    void* p = uncollectable ? GC_MALLOC_UNCOLLECTABLE(n) : GC_MALLOC(n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

void gcFreeCell(void* p) noexcept {
    if (p != nullptr) GC_FREE(p);
}
}  // namespace detail

// ---------------------------------------------------------------------------------------
// fault handlers

namespace {

std::atomic<bool> g_initialized{false};

}  // namespace
namespace detail {
[[noreturn]] void defaultAction(int sig) {
    // Restore the default action and re-raise: the process dies with a core dump like any
    // unhandled crash.
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
    raise(sig);
    _exit(128 + sig);
}
}  // namespace detail
}  // namespace jlang

// Exported with a fixed name so Throwable can drop the handler frames from stack traces.
extern "C" __attribute__((visibility("default"), noinline)) void jlang_fault_signal_handler(int sig, siginfo_t* si,
                                                                                          void* ctx) {
    using namespace jlang;
    // Tell Throwable::captureStack where the Java-visible stack starts (the faulting pc).
#if defined(__x86_64__)
    detail::setFaultPc(reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.gregs[REG_RIP]));
#elif defined(__aarch64__)
    detail::setFaultPc(reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.pc));
#else
    (void)ctx;
#endif
    if (sig == SIGFPE) {
        if (si->si_code == FPE_INTDIV || si->si_code == FPE_INTOVF) {
            throw ArithmeticException(String("/ by zero"));
        }
        detail::defaultAction(sig);
    }
    // SIGSEGV / SIGBUS: an access to the first 64 KiB is a null dereference (a field or
    // vtable read through a null pointer). SI_KERNEL faults (general protection, e.g.
    // non-canonical addresses) report si_addr 0 but are real crashes.
    const auto addr = reinterpret_cast<uintptr_t>(si->si_addr);
    if (si->si_code != SI_KERNEL && si->si_code > 0 && addr < 65536) {
        throw NullPointerException();
    }
    const char msg[] = "jlang: fatal memory fault (not a null dereference)\n";
    ssize_t w = write(2, msg, sizeof msg - 1);
    (void)w;
    detail::defaultAction(sig);
}

namespace jlang {
namespace {

void installHandler(int sig) {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = &jlang_fault_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}

}  // namespace

void installFaultHandlers() {
    // Preload the unwinder (backtrace() dlopens libgcc_s on first use) so the first fault
    // does not have to.
    void* frames[4];
    (void)backtrace(frames, 4);
    installHandler(SIGSEGV);
    installHandler(SIGBUS);
    installHandler(SIGFPE);
}

void init(int argc, char** argv) {
    gc::ensureInitialized();
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true)) return;
    installFaultHandlers();
    detail::initSystemProperties(argc, argv);
}

bool isInitialized() noexcept { return g_initialized.load(); }

// ---------------------------------------------------------------------------------------
// collector services

namespace gc {

void ensureInitialized() noexcept { detail::gcInitSlow(); }

bool registerCurrentThread() {
    ensureInitialized();
    if (GC_thread_is_registered()) return false;
    GC_stack_base sb;
    if (GC_get_stack_base(&sb) != GC_SUCCESS) {
        throw InternalError(String("GC_get_stack_base failed"));
    }
    return GC_register_my_thread(&sb) == GC_SUCCESS;
}

void unregisterCurrentThread() {
    if (GC_thread_is_registered()) GC_unregister_my_thread();
}

bool isCurrentThreadRegistered() noexcept { return GC_thread_is_registered() != 0; }

namespace {
struct NativeStart {
    NativeThreadFn fn;
    void* arg;
};

void* nativeTrampoline(void* p) {
    GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
    auto* holder = static_cast<NativeStart*>(p);
    volatile NativeStart start = *holder;  // now on this (scanned) stack
    GC_FREE(holder);
    try {
        start.fn(start.arg);
    } catch (Throwable& t) {
        std::fprintf(stderr, "Exception in native thread: ");
        t.printStackTrace();
    } catch (std::exception& e) {
        std::fprintf(stderr, "Exception in native thread: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "Unknown exception in native thread\n");
    }
    GC_unregister_my_thread();
    return nullptr;
}
}  // namespace

uint64_t startNativeThread(NativeThreadFn fn, void* arg, size_t stackSize, bool detached) {
    ensureInitialized();
    auto* holder = static_cast<NativeStart*>(GC_MALLOC_UNCOLLECTABLE(sizeof(NativeStart)));
    if (holder == nullptr) throw OutOfMemoryError(String("unable to create native thread"));
    holder->fn = fn;
    holder->arg = arg;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stackSize > 0) pthread_attr_setstacksize(&attr, stackSize < 65536 ? 65536 : stackSize);
    if (detached) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t tid;
    int rc = pthread_create(&tid, &attr, &nativeTrampoline, holder);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        GC_FREE(holder);
        throw OutOfMemoryError(str("unable to create native thread: ", std::strerror(rc)));
    }
    return static_cast<uint64_t>(tid);
}

void joinNativeThread(uint64_t handle) {
    // The joining thread may block for long: allow collections meanwhile.
    pthread_join(static_cast<pthread_t>(handle), nullptr);
}

void collect() {
    ensureInitialized();
    GC_gcollect();
}

int64_t heapSize() {
    ensureInitialized();
    return static_cast<int64_t>(GC_get_heap_size());
}

int64_t freeBytes() {
    ensureInitialized();
    return static_cast<int64_t>(GC_get_free_bytes());
}

int64_t maxHeapSize() {
    const char* env = std::getenv("GC_MAXIMUM_HEAP_SIZE");
    if (env != nullptr) {
        char* end = nullptr;
        long long v = std::strtoll(env, &end, 10);
        if (v > 0) {
            if (end != nullptr && (*end == 'k' || *end == 'K')) v <<= 10;
            if (end != nullptr && (*end == 'm' || *end == 'M')) v <<= 20;
            if (end != nullptr && (*end == 'g' || *end == 'G')) v <<= 30;
            return v;
        }
    }
    // Like the JVM's default: a quarter of the physical memory.
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) return static_cast<int64_t>(pages) * pageSize / 4;
    return heapSize();
}

int64_t totalAllocated() {
    ensureInitialized();
    return static_cast<int64_t>(GC_get_total_bytes());
}

int64_t collections() {
    ensureInitialized();
    return static_cast<int64_t>(GC_get_gc_no());
}

void* allocUncollectable(size_t n) { return detail::gcAllocCell(n, true); }
void freeUncollectable(void* p) noexcept { detail::gcFreeCell(p); }
void* allocAtomic(size_t n) {
    ensureInitialized();
    void* p = GC_MALLOC_ATOMIC(n);
    if (p == nullptr) throw std::bad_alloc();
    std::memset(p, 0, n);
    return p;
}

}  // namespace gc

// ---------------------------------------------------------------------------------------
// interruption

namespace sync {

class InterruptState {
public:
    std::atomic<bool> interrupted{false};
    std::mutex blockerMu;
    WakeFn wakeFn = nullptr;
    void* wakeArg = nullptr;
    // for sleep()
    std::mutex sleepMu;
    std::condition_variable sleepCv;
};

namespace {
thread_local InterruptState* tl_bound = nullptr;
thread_local InterruptState* tl_default = nullptr;

// The default state lives in malloc memory: it holds no GC pointers (wakeArg is only set
// while the waiter itself keeps the object alive), and it is freed by a thread_local
// destructor that may run after the thread left the collector.
struct DefaultStateOwner {
    ~DefaultStateOwner() {
        if (tl_default != nullptr) {
            tl_default->~InterruptState();
            std::free(tl_default);
            tl_default = nullptr;
        }
    }
};
thread_local DefaultStateOwner tl_defaultOwner;

void sleepWake(void* arg) {
    auto* s = static_cast<InterruptState*>(arg);
    std::lock_guard<std::mutex> lock(s->sleepMu);
    s->sleepCv.notify_all();
}
}  // namespace

InterruptState* newInterruptState() { return new InterruptState(); }

void bindCurrentThread(InterruptState* s) { tl_bound = s; }

InterruptState* current() {
    if (tl_bound != nullptr) return tl_bound;
    if (tl_default == nullptr) {
        void* mem = std::malloc(sizeof(InterruptState));
        if (mem == nullptr) throw std::bad_alloc();
        tl_default = new (mem) InterruptState();
        (void)&tl_defaultOwner;  // make sure the owner (and its destructor) exists
    }
    return tl_default;
}

void interrupt(InterruptState* s) {
    if (s == nullptr) return;
    s->interrupted.store(true);
    WakeFn fn;
    void* arg;
    {
        std::lock_guard<std::mutex> lock(s->blockerMu);
        fn = s->wakeFn;
        arg = s->wakeArg;
    }
    if (fn != nullptr) fn(arg);
}

bool isInterrupted(InterruptState* s) noexcept { return s != nullptr && s->interrupted.load(); }

bool interrupted() { return current()->interrupted.exchange(false); }

void clearInterrupt(InterruptState* s) noexcept {
    if (s != nullptr) s->interrupted.store(false);
}

void setBlocker(WakeFn fn, void* arg) noexcept {
    InterruptState* s = current();
    std::lock_guard<std::mutex> lock(s->blockerMu);
    s->wakeFn = fn;
    s->wakeArg = arg;
}

void clearBlocker() noexcept { setBlocker(nullptr, nullptr); }

void sleep(int64_t millis, int32_t nanos) {
    if (millis < 0) throw IllegalArgumentException(String("timeout value is negative"));
    if (nanos < 0 || nanos > 999999) throw IllegalArgumentException(String("nanosecond timeout value out of range"));
    InterruptState* s = current();
    // Clamp huge timeouts (Long.MAX_VALUE) so the deadline does not overflow: ~100 years.
    if (millis > INT64_C(3153600000000)) millis = INT64_C(3153600000000);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis) +
                    std::chrono::nanoseconds(nanos);
    std::unique_lock<std::mutex> lock(s->sleepMu);
    setBlocker(&sleepWake, s);
    for (;;) {
        if (s->interrupted.exchange(false)) {
            lock.unlock();
            clearBlocker();
            throw InterruptedException(String("sleep interrupted"));
        }
        if (s->sleepCv.wait_until(lock, deadline) == std::cv_status::timeout) {
            if (std::chrono::steady_clock::now() >= deadline) break;
        }
    }
    lock.unlock();
    clearBlocker();
}

}  // namespace sync

}  // namespace jlang
