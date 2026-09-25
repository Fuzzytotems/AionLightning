// jlang/Ref.h - java.lang.ref: Reference<T>, WeakReference<T>, SoftReference<T>,
// ReferenceQueue<T> on Boehm GC disappearing links.
//
//   java.lang.ref.WeakReference<V>   -> jlang::WeakReference<V'>*   (V' = mapped type, a pointer)
//   java.lang.ref.SoftReference<V>   -> jlang::SoftReference<V'>*
//   java.lang.ref.Reference<V>       -> jlang::Reference<V'>*
//   java.lang.ref.ReferenceQueue<V>  -> jlang::ReferenceQueue<V'>*
//
// All four are ordinary GC classes and may be subclassed (SoftCacheMap's SoftEntry adds a key):
//   class SoftCacheMap_SoftEntry : public jlang::SoftReference<jlang::Object*> {
//       SoftCacheMap_SoftEntry(Object* key, Object* referent, jlang::ReferenceQueue<Object*>* q)
//           : jlang::SoftReference<jlang::Object*>(referent, q), key(key) {}
//   ...
//   auto* en = jlang::cast<SoftCacheMap_SoftEntry>(refQueue->poll());
//
// Semantics:
//   * WeakReference: get() returns null once the referent became unreachable and was
//     collected (a disappearing link, cleared by the collector before finalization).
//   * SoftReference: holds the referent strongly (as a JVM with enough heap does) until
//     clear()/enqueue(), or until jlang::clearSoftReferences() is called - the memory-pressure
//     hook for the runtime - which turns every live soft reference into a weak one.
//   * ReferenceQueue: a reference constructed with a queue is appended to it once the
//     collector cleared its referent (detected lazily: poll() checks the references
//     registered with the queue after each completed collection). Like Java, a reference that
//     is itself unreachable is never enqueued, clear() does not enqueue, and enqueue() does.
//   * References must be allocated with `new` (a reference object on the stack or in static
//     storage keeps its referent strongly). Referents not allocated on the GC heap are held
//     strongly.
#pragma once

#include <jlang/Object.h>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace jlang {

template<class T> class Reference;
template<class T> class ReferenceQueue;

// Memory-pressure hook: converts all live SoftReferences into weak ones (their referents can
// then be collected and the references enqueued). Returns the number of references affected.
int32_t clearSoftReferences();

namespace detail {

class RefQueueBase;

class RefBase : public virtual Object {
public:
    RefBase(const RefBase&) = delete;
    RefBase& operator=(const RefBase&) = delete;

    void* getRaw();
    void clearRaw();
    bool isEnqueuedRaw();
    bool enqueueRaw();
    bool refersToRaw(const void* p);
    // jlang internal (queue / soft reference bookkeeping)
    bool linkClearedByCollector();
    void makeWeak() noexcept { strong_ = nullptr; }
    bool isSoftStrong() const noexcept { return strong_ != nullptr; }

protected:
    RefBase(const void* referent, RefQueueBase* queue, bool soft);

private:
    friend class RefQueueBase;
    uintptr_t hidden_ = 0;       // disguised referent pointer (disappearing link)
    void* strong_ = nullptr;     // soft references (until cleared), or unregisterable referents
    RefQueueBase* queue_ = nullptr;
    bool registered_ = false;    // hidden_ is a registered disappearing link
    bool cleared_ = false;       // clear()/enqueue() was called
    bool enqueued_ = false;      // currently in its queue
    bool wasEnqueued_ = false;   // ever enqueued (never enqueued twice)
};

class RefQueueBase : public virtual Object {
public:
    RefBase* pollRaw();
    RefBase* removeRaw(int64_t timeoutMillis);  // 0 = wait forever; nullptr on timeout
    // jlang internal
    void track(RefBase* r);
    bool enqueueExplicit(RefBase* r);

protected:
    RefQueueBase() = default;

private:
    void scan();
    std::vector<uintptr_t*> tracked_;  // slots holding disguised RefBase pointers (weak)
    std::vector<RefBase*> pending_;    // enqueued, not yet polled
    int64_t lastScanGc_ = -1;
};

}  // namespace detail

// java.lang.ref.Reference<T>
template<class T>
class Reference : public detail::RefBase {
    static_assert(std::is_pointer_v<T>, "jlang::Reference<T>: T must be a pointer type (e.g. jlang::Object*)");

public:
    // The referent, or nullptr once cleared.
    T get() { return static_cast<T>(this->getRaw()); }
    void clear() { this->clearRaw(); }
    bool isEnqueued() { return this->isEnqueuedRaw(); }
    // Clears the referent and adds this reference to its queue; false if there is no queue or
    // it was already enqueued.
    bool enqueue() { return this->enqueueRaw(); }
    bool refersTo(T obj) { return this->refersToRaw(static_cast<const void*>(obj)); }

protected:
    Reference(T referent, ReferenceQueue<T>* q, bool soft)
        : detail::RefBase(static_cast<const void*>(referent), static_cast<detail::RefQueueBase*>(q), soft) {}
};

// java.lang.ref.WeakReference<T>
template<class T>
class WeakReference : public Reference<T> {
public:
    explicit WeakReference(T referent) : Reference<T>(referent, nullptr, false) {}
    WeakReference(T referent, ReferenceQueue<T>* q) : Reference<T>(referent, q, false) {}
};

// java.lang.ref.SoftReference<T> (strong until cleared or until clearSoftReferences())
template<class T>
class SoftReference : public Reference<T> {
public:
    explicit SoftReference(T referent) : Reference<T>(referent, nullptr, true) {}
    SoftReference(T referent, ReferenceQueue<T>* q) : Reference<T>(referent, q, true) {}
};

// java.lang.ref.ReferenceQueue<T>
template<class T>
class ReferenceQueue : public detail::RefQueueBase {
public:
    ReferenceQueue() = default;
    // The next enqueued reference, or nullptr (never blocks).
    Reference<T>* poll() { return dynamic_cast<Reference<T>*>(this->pollRaw()); }
    // Blocks until a reference is enqueued (InterruptedException if interrupted).
    Reference<T>* remove() { return dynamic_cast<Reference<T>*>(this->removeRaw(0)); }
    // Waits at most timeout ms (0 = forever); nullptr on timeout. IllegalArgumentException if < 0.
    Reference<T>* remove(int64_t timeout) { return dynamic_cast<Reference<T>*>(this->removeRaw(timeout)); }
};

}  // namespace jlang
