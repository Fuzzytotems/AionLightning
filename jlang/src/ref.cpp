// jlang/Ref.cpp - java.lang.ref on Boehm GC disappearing links.
#include <jlang/Ref.h>

#include <jlang/Exceptions.h>
#include <jlang/Runtime.h>

#define GC_THREADS
#define GC_NO_THREAD_REDIRECTS
#include <gc/gc.h>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace jlang {

namespace {

inline bool inHeap(const void* p) { return p != nullptr && GC_base(const_cast<void*>(p)) != nullptr; }

void* revealFn(void* arg) {
    uintptr_t h = *static_cast<volatile uintptr_t*>(arg);
    return h == 0 ? nullptr : GC_REVEAL_POINTER(h);
}

// Weakly tracked GC objects: each slot is a separately allocated pointer-free cell holding a
// disguised pointer, registered as a disappearing link on the object.
uintptr_t* newWeakSlot(void* obj) {
    auto* slot = static_cast<uintptr_t*>(GC_MALLOC_ATOMIC(sizeof(uintptr_t)));
    if (slot == nullptr) throw OutOfMemoryError();
    *slot = GC_HIDE_POINTER(obj);
    void* base = GC_base(obj);
    if (base != nullptr) GC_general_register_disappearing_link(reinterpret_cast<void**>(slot), base);
    return slot;
}

void* revealSlot(uintptr_t* slot) { return GC_call_with_alloc_lock(revealFn, slot); }

// Registry of live soft references (weak slots) for clearSoftReferences().
std::mutex& softLock() {
    static std::mutex* m = new std::mutex();
    return *m;
}
std::vector<uintptr_t*>& softSlots() {
    static auto* v = new std::vector<uintptr_t*>();
    return *v;
}

int64_t gcCount() { return static_cast<int64_t>(GC_get_gc_no()); }

}  // namespace

int32_t clearSoftReferences() {
    std::vector<uintptr_t*> slots;
    {
        std::lock_guard<std::mutex> g(softLock());
        slots.swap(softSlots());
    }
    int32_t n = 0;
    for (uintptr_t* s : slots) {
        auto* r = static_cast<detail::RefBase*>(revealSlot(s));
        if (r != nullptr && r->isSoftStrong()) {
            r->makeWeak();
            n++;
        }
    }
    return n;
}

namespace detail {

// =======================================================================================
// RefBase

RefBase::RefBase(const void* referent, RefQueueBase* queue, bool soft) : queue_(queue) {
    if (referent != nullptr) {
        void* base = GC_base(const_cast<void*>(referent));
        if (base != nullptr && inHeap(&hidden_)) {
            hidden_ = GC_HIDE_POINTER(const_cast<void*>(referent));
            GC_general_register_disappearing_link(reinterpret_cast<void**>(&hidden_), base);
            registered_ = true;
            if (soft) {
                strong_ = const_cast<void*>(referent);
                auto* slot = newWeakSlot(static_cast<RefBase*>(this));
                std::lock_guard<std::mutex> g(softLock());
                auto& v = softSlots();
                // Drop slots of collected references now and then.
                if (v.size() >= 1024 && (v.size() & (v.size() - 1)) == 0)
                    v.erase(std::remove_if(v.begin(), v.end(), [](uintptr_t* s) { return *s == 0; }), v.end());
                v.push_back(slot);
            }
        } else {
            strong_ = const_cast<void*>(referent);  // not collectable (or reference not on the heap)
        }
    }
    if (queue_ != nullptr) queue_->track(this);
}

void* RefBase::getRaw() {
    void* s = strong_;
    if (s != nullptr) return s;
    if (!registered_) return nullptr;
    return GC_call_with_alloc_lock(revealFn, &hidden_);
}

void RefBase::clearRaw() {
    cleared_ = true;
    strong_ = nullptr;
    if (registered_) {
        GC_unregister_disappearing_link(reinterpret_cast<void**>(&hidden_));
        registered_ = false;
    }
    hidden_ = 0;
}

bool RefBase::linkClearedByCollector() { return registered_ && hidden_ == 0 && !cleared_; }

bool RefBase::isEnqueuedRaw() {
    if (queue_ == nullptr) return false;
    JSYNC(queue_) { return enqueued_; }
    return false;
}

bool RefBase::enqueueRaw() {
    clearRaw();
    if (queue_ == nullptr) return false;
    return queue_->enqueueExplicit(this);
}

bool RefBase::refersToRaw(const void* p) { return getRaw() == p; }

// =======================================================================================
// RefQueueBase

void RefQueueBase::track(RefBase* r) {
    uintptr_t* slot = newWeakSlot(static_cast<void*>(r));
    JSYNC(this) {
        // Prune slots of references that were collected themselves.
        if (tracked_.size() >= 64 && (tracked_.size() & (tracked_.size() - 1)) == 0)
            tracked_.erase(std::remove_if(tracked_.begin(), tracked_.end(), [](uintptr_t* s) { return *s == 0; }),
                           tracked_.end());
        tracked_.push_back(slot);
    }
}

bool RefQueueBase::enqueueExplicit(RefBase* r) {
    JSYNC(this) {
        if (r->wasEnqueued_) return false;
        r->wasEnqueued_ = true;
        r->enqueued_ = true;
        pending_.push_back(r);
        notifyAll();
        return true;
    }
    return false;
}

// Called with the monitor held: moves references whose referent was collected to pending_.
void RefQueueBase::scan() {
    int64_t gc = gcCount();
    if (gc == lastScanGc_) return;
    lastScanGc_ = gc;
    std::vector<uintptr_t*> keep;
    keep.reserve(tracked_.size());
    for (uintptr_t* slot : tracked_) {
        if (*slot == 0) continue;  // the reference object itself was collected
        auto* r = static_cast<RefBase*>(revealSlot(slot));
        if (r == nullptr) continue;
        if (r->wasEnqueued_ || r->cleared_) continue;  // enqueued explicitly or cleared: never enqueued by GC
        if (r->linkClearedByCollector()) {
            r->wasEnqueued_ = true;
            r->enqueued_ = true;
            pending_.push_back(r);
            continue;
        }
        keep.push_back(slot);
    }
    tracked_.swap(keep);
}

RefBase* RefQueueBase::pollRaw() {
    JSYNC(this) {
        if (pending_.empty()) scan();
        if (pending_.empty()) return nullptr;
        RefBase* r = pending_.front();
        pending_.erase(pending_.begin());
        r->enqueued_ = false;
        return r;
    }
    return nullptr;
}

RefBase* RefQueueBase::removeRaw(int64_t timeoutMillis) {
    if (timeoutMillis < 0) throw IllegalArgumentException(String("Negative timeout value"));
    auto start = std::chrono::steady_clock::now();
    JSYNC(this) {
        for (;;) {
            RefBase* r = pollRaw();
            if (r != nullptr) return r;
            int64_t waitMs = 50;  // collections are only noticed by polling
            if (timeoutMillis > 0) {
                int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                int64_t left = timeoutMillis - elapsed;
                if (left <= 0) return nullptr;
                if (left < waitMs) waitMs = left;
            }
            wait(waitMs);
        }
    }
    return nullptr;
}

}  // namespace detail

}  // namespace jlang
