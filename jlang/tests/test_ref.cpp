// Tests for <jlang/Ref.h>: weak references cleared by the collector and enqueued, soft
// references kept until clearSoftReferences(), clear()/enqueue(), subclassing with extra fields
// (the gameserver SoftCacheMap/WeakCacheMap pattern).
#include "jtest.h"

#include <jlang/Ref.h>

#include <cstring>
#include <vector>

using namespace jlang;

namespace {

class Payload : public virtual Object {
public:
    explicit Payload(int v) : v(v) {}
    int v;
    char pad[48] = {};
};

// SoftCacheMap.SoftEntry / WeakCacheMap.WeakEntry
class SoftEntry : public SoftReference<Object*> {
public:
    SoftEntry(int32_t key, Object* referent, ReferenceQueue<Object*>* q) : SoftReference<Object*>(referent, q), key(key) {}
    int32_t key;
};

class WeakEntry : public WeakReference<Object*> {
public:
    WeakEntry(int32_t key, Object* referent, ReferenceQueue<Object*>* q) : WeakReference<Object*>(referent, q), key(key) {}
    int32_t key;
};

template<class E>
__attribute__((noinline)) void makeEntries(std::vector<E*>& out, std::vector<Object*>& strong, ReferenceQueue<Object*>* q, int n) {
    for (int i = 0; i < n; i++) {
        auto* p = new Payload(i);
        if (i % 10 == 0) strong.push_back(p);
        out.push_back(new E(i, p, q));
    }
}

__attribute__((noinline)) void scrubStack() {
    volatile char junk[64 * 1024];
    std::memset(const_cast<char*>(junk), 0, sizeof junk);
}

void collect() {
    for (int i = 0; i < 3; i++) {
        scrubStack();
        gc::collect();
    }
}

}  // namespace

JTEST(RefWeakCollectedAndEnqueued) {
    auto* q = new ReferenceQueue<Object*>();
    std::vector<WeakEntry*> refs;
    std::vector<Object*> strong;
    makeEntries(refs, strong, q, 1000);
    collect();
    int cleared = 0, kept = 0;
    for (WeakEntry* r : refs) {
        Object* o = r->get();
        if (o == nullptr) {
            cleared++;
        } else {
            kept++;
            JCHECK_EQ(cast<Payload>(o)->v, r->key);
        }
    }
    for (int i = 0; i < 1000; i += 10) JCHECK(refs[static_cast<size_t>(i)]->get() == strong[static_cast<size_t>(i / 10)]);
    JCHECK(cleared >= 800);  // conservative GC: a few may be retained by stale words
    int polled = 0;
    for (Reference<Object*>* r = q->poll(); r != nullptr; r = q->poll()) {
        auto* e = cast<WeakEntry>(r);
        JCHECK(e->get() == nullptr);
        JCHECK(e->key % 10 != 0);
        JCHECK(!e->isEnqueued());
        polled++;
    }
    JCHECK_EQ(polled, cleared);
    JCHECK(q->poll() == nullptr);
    JCHECK(strong.size() == 100);
}

JTEST(RefSoftKeptUntilPressure) {
    auto* q = new ReferenceQueue<Object*>();
    std::vector<SoftEntry*> refs;
    std::vector<Object*> strong;
    makeEntries(refs, strong, q, 500);
    collect();
    for (SoftEntry* r : refs) JCHECK(r->get() != nullptr);
    JCHECK(q->poll() == nullptr);
    JCHECK(clearSoftReferences() >= 500);
    collect();
    int cleared = 0;
    for (SoftEntry* r : refs)
        if (r->get() == nullptr) cleared++;
    JCHECK(cleared >= 350);
    int polled = 0;
    while (q->poll() != nullptr) polled++;
    JCHECK_EQ(polled, cleared);
    for (int i = 0; i < 500; i += 10) JCHECK(refs[static_cast<size_t>(i)]->get() != nullptr);
}

JTEST(RefClearEnqueueRemove) {
    auto* q = new ReferenceQueue<Object*>();
    auto* keep = new Payload(1);
    auto* w = new WeakReference<Object*>(keep, q);
    JCHECK(w->get() == keep);
    JCHECK(w->refersTo(keep));
    w->clear();
    JCHECK(w->get() == nullptr);
    collect();
    JCHECK(q->poll() == nullptr);  // clear() does not enqueue
    auto* s = new SoftReference<Object*>(keep, q);
    JCHECK(s->enqueue());
    JCHECK(s->get() == nullptr);
    JCHECK(s->isEnqueued());
    JCHECK(!s->enqueue());
    JCHECK(q->remove(1000) == s);
    JCHECK(!s->isEnqueued());
    JCHECK(q->remove(50) == nullptr);
    JCHECK_THROWS(IllegalArgumentException, q->remove(-1));
    auto* noq = new WeakReference<Object*>(keep);
    JCHECK(!noq->enqueue());
    auto* nul = new WeakReference<Object*>(nullptr);
    JCHECK(nul->get() == nullptr);
    JCHECK(keep->v == 1);
}
