// Global operator new/delete on the Boehm collector (CONVENTIONS §2).
//
// Every `new` (and every std::allocator allocation, which goes through operator new) returns
// collectable, scanned, zero-filled memory from GC_MALLOC. operator delete is a no-op: memory
// is reclaimed when it becomes unreachable. The collector is initialized on the first
// allocation (static constructors run on the main thread, so GC_INIT happens there).
#define GC_THREADS
#define GC_NO_THREAD_REDIRECTS
#include <gc/gc.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>

namespace jlang::detail {

static std::atomic<bool> g_gcReady{false};

void gcInitSlow() noexcept {
    static std::once_flag once;
    std::call_once(once, [] {
        GC_INIT();
        GC_allow_register_threads();
        g_gcReady.store(true, std::memory_order_release);
    });
}

}  // namespace jlang::detail

namespace {

inline void ensureGc() noexcept {
    if (!jlang::detail::g_gcReady.load(std::memory_order_acquire)) [[unlikely]] {
        jlang::detail::gcInitSlow();
    }
}

inline void* gcAlloc(std::size_t n) {
    ensureGc();
    void* p = GC_MALLOC(n);
    if (p == nullptr) [[unlikely]] {
        throw std::bad_alloc();
    }
    return p;
}

inline void* gcAllocNoThrow(std::size_t n) noexcept {
    ensureGc();
    return GC_MALLOC(n);
}

// Over-aligned allocation: GC_MALLOC blocks are 16-byte aligned; for larger alignments the
// block is over-allocated and an interior pointer returned (the collector recognizes interior
// pointers, so the object stays alive).
inline void* gcAllocAligned(std::size_t n, std::size_t align) {
    if (align <= 16) return gcAlloc(n);
    void* p = gcAlloc(n + align);
    auto a = (reinterpret_cast<std::uintptr_t>(p) + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
    return reinterpret_cast<void*>(a);
}

inline void* gcAllocAlignedNoThrow(std::size_t n, std::size_t align) noexcept {
    if (align <= 16) return gcAllocNoThrow(n);
    void* p = gcAllocNoThrow(n + align);
    if (p == nullptr) return nullptr;
    auto a = (reinterpret_cast<std::uintptr_t>(p) + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
    return reinterpret_cast<void*>(a);
}

}  // namespace

void* operator new(std::size_t n) { return gcAlloc(n); }
void* operator new[](std::size_t n) { return gcAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return gcAllocNoThrow(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return gcAllocNoThrow(n); }
void* operator new(std::size_t n, std::align_val_t a) { return gcAllocAligned(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return gcAllocAligned(n, static_cast<std::size_t>(a)); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return gcAllocAlignedNoThrow(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return gcAllocAlignedNoThrow(n, static_cast<std::size_t>(a));
}

// delete: no-op (the collector reclaims unreachable memory).
void operator delete(void*) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete(void*, std::size_t) noexcept {}
void operator delete[](void*, std::size_t) noexcept {}
void operator delete(void*, const std::nothrow_t&) noexcept {}
void operator delete[](void*, const std::nothrow_t&) noexcept {}
void operator delete(void*, std::align_val_t) noexcept {}
void operator delete[](void*, std::align_val_t) noexcept {}
void operator delete(void*, std::size_t, std::align_val_t) noexcept {}
void operator delete[](void*, std::size_t, std::align_val_t) noexcept {}
void operator delete(void*, std::align_val_t, const std::nothrow_t&) noexcept {}
void operator delete[](void*, std::align_val_t, const std::nothrow_t&) noexcept {}
