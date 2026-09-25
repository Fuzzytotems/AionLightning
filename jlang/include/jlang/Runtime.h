// jlang/Runtime.h - process start-up and low-level runtime services:
//   * jlang::init(argc, argv): must be the first statement of every main()
//   * jlang::gc: collector thread registration and statistics (for jlang::Thread and executors)
//   * jlang::sync: per-thread interruption state (for jlang::Thread, Object::wait, sleep, locks)
//
// Translated code only calls jlang::init (from main); the rest is for runtime implementers.
#pragma once

#include <cstddef>
#include <cstdint>

namespace jlang {

// Initializes the runtime: the garbage collector (GC_INIT, thread registration support), the
// SIGSEGV/SIGBUS handler that turns null dereferences (fault address < 64 KiB) into
// jlang::NullPointerException, the SIGFPE handler that turns integer division by zero into
// jlang::ArithmeticException("/ by zero"), and System properties from `-Dkey=value`
// arguments. Idempotent. The collector itself is already usable before init() (the global
// operator new initializes it on first use from static constructors).
//
// Notes on the fault handlers (they rely on -fnon-call-exceptions):
//   * only faults in code compiled with -fnon-call-exceptions (translated code, jlang) can be
//     converted; a fault inside a library (libc, libstdc++) terminates the process.
//   * a fault at an address >= 64 KiB is a real crash: the default action runs (core dump).
//   * x86 raises the same trap for INT_MIN / -1 as for division by zero, so that case throws
//     ArithmeticException too (Java yields INT_MIN); use jlang::idiv where it matters.
//   * stack overflow is not converted (no StackOverflowError).
void init(int argc, char** argv);
bool isInitialized() noexcept;
// Installs only the fault handlers (init() calls it). Handlers are process-wide.
void installFaultHandlers();

namespace gc {

// Initializes the collector if needed (safe to call from any thread after main() started).
void ensureInitialized() noexcept;

// Registers the calling thread with the collector (its stack is scanned, it is stopped during
// collections). Returns false if it was already registered. Every thread that touches GC
// pointers must be registered: jlang::Thread and the executors do it for their threads.
bool registerCurrentThread();
// Unregisters the calling thread (call before the thread exits; it must not hold GC pointers
// afterwards).
void unregisterCurrentThread();
bool isCurrentThreadRegistered() noexcept;

// RAII: registers in the constructor (if not yet registered) and unregisters in the
// destructor (only if it registered).
class ThreadRegistration {
public:
    ThreadRegistration() : registered_(registerCurrentThread()) {}
    ~ThreadRegistration() {
        if (registered_) unregisterCurrentThread();
    }
    ThreadRegistration(const ThreadRegistration&) = delete;
    ThreadRegistration& operator=(const ThreadRegistration&) = delete;

private:
    bool registered_;
};

// Starts a native thread that is registered with the collector for its whole life and runs
// fn(arg). `arg` may be a GC pointer: it is kept reachable until the thread picked it up.
// stackSize 0 = default. Returns an opaque handle for joinNativeThread (unless detached).
// Exceptions escaping fn are reported to stderr and swallowed.
using NativeThreadFn = void (*)(void* arg);
uint64_t startNativeThread(NativeThreadFn fn, void* arg, size_t stackSize = 0, bool detached = false);
void joinNativeThread(uint64_t handle);

void collect();            // full collection (System.gc())
int64_t heapSize();        // bytes the collector obtained from the OS (Runtime.totalMemory)
int64_t freeBytes();       // free bytes in the heap (Runtime.freeMemory)
int64_t maxHeapSize();     // limit (GC_MAXIMUM_HEAP_SIZE / JLANG_MAX_HEAP env, else heap size * 4)
int64_t totalAllocated();  // bytes allocated since start
int64_t collections();     // number of completed collections

void* allocUncollectable(size_t n);  // scanned, never collected: free with freeUncollectable
void freeUncollectable(void* p) noexcept;
void* allocAtomic(size_t n);  // collectable, not scanned (pointer-free data)

}  // namespace gc

namespace sync {

// Interruption support shared by Object::wait, Thread.sleep/join/interrupt and the blocking
// primitives in <jlang/Thread.h>.
//
// Each jlang::Thread owns an InterruptState (newInterruptState(); keep it in a field so it stays
// reachable) and binds it at the start of the thread body (bindCurrentThread). Threads without
// a bound state (main, foreign threads) get a private per-thread one on first use.
//
// A blocking primitive cooperates with interrupt() as follows (mu = its internal mutex):
//     lock(mu); setBlocker(&wakeFn, obj);
//     while (!condition) { if (isInterrupted(current())) {...throw/return...}; cv.wait(mu); }
//     clearBlocker();
// and wakeFn(obj) must lock mu and notify_all the condition variable. interrupt() sets the
// flag first and then calls the registered wake function, so no wake-up is lost.
class InterruptState;
using WakeFn = void (*)(void* arg);

InterruptState* newInterruptState();  // GC object
void bindCurrentThread(InterruptState* s);  // nullptr unbinds
InterruptState* current();
void interrupt(InterruptState* s);   // Thread.interrupt()
bool isInterrupted(InterruptState* s) noexcept;  // Thread.isInterrupted()
bool interrupted();                  // Thread.interrupted(): test and clear (current thread)
void clearInterrupt(InterruptState* s) noexcept;
void setBlocker(WakeFn fn, void* arg) noexcept;  // current thread
void clearBlocker() noexcept;
// Thread.sleep(millis, nanos): interruptible; throws InterruptedException (clearing the flag).
// IllegalArgumentException for negative values.
void sleep(int64_t millis, int32_t nanos = 0);

}  // namespace sync

}  // namespace jlang
