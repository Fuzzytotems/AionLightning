// Concurrency and garbage-collection tests for the jlang collections: lock-enabled collections
// hammered by 4 GC-registered threads, JSYNC atomicity, and objects reachable only through
// collection storage surviving collections.
#include "jtest.h"

#include <atomic>

using namespace jlang;

namespace {

class Node : public virtual Object {
public:
    explicit Node(int32_t v) : v(v), payload(String("n") + v) {}
    bool equals(Object* o) override {
        auto* n = dynamic_cast<Node*>(o);
        return n != nullptr && n->v == v;
    }
    int32_t hashCode() override { return v; }
    int32_t v;
    String payload;
};

constexpr int32_t kThreads = 4;
constexpr int32_t kOps = 20000;

struct Shared {
    List<int32_t>* list = nullptr;
    Map<int32_t, Node*>* chm = nullptr;
    Set<int32_t>* set = nullptr;
    Deque<Node*>* queue = nullptr;
    TreeMap<int32_t, int32_t>* tree = nullptr;
    List<int32_t>* unique = nullptr;
    std::atomic<int32_t> consumed{0};
    std::atomic<int64_t> consumedSum{0};
    std::atomic<int32_t> errors{0};
};

struct Arg {
    Shared* s;
    int32_t id;
};

void worker(void* p) {
    Arg* a = static_cast<Arg*>(p);
    Shared* s = a->s;
    const int32_t base = a->id * kOps;
    try {
        for (int32_t i = 0; i < kOps; i++) {
            const int32_t k = base + i;
            // shared list: add, check, remove own elements; iterate a snapshot now and then
            s->list->add(k);
            if (!s->list->contains(k)) s->errors++;
            if (i % 2 == 0) s->list->removeObject(k);
            if (i % 1000 == 0) {
                int64_t n = 0;
                for (int32_t v : *s->list) n += v >= 0 ? 1 : 0;
                (void)n;
            }
            // ConcurrentHashMap
            s->chm->put(k, new Node(k));
            Node* got = s->chm->get(k);
            if (got == nullptr || got->v != k) s->errors++;
            if (i % 3 == 0) s->chm->remove(k);
            if (i % 2000 == 0) {
                for (auto& e : *s->chm) {
                    if (e.getValue() == nullptr || e.getValue()->v != e.getKey()) s->errors++;
                }
                auto* vals = s->chm->values();
                (void)vals->size();
            }
            // shared set
            s->set->add(k % 5000);
            if (i % 5 == 0) s->set->remove((k + 7) % 5000);
            // producer/consumer queue: ids 0,1 produce, 2,3 consume
            if (a->id < 2) {
                s->queue->offer(new Node(i));
            } else {
                Node* n = s->queue->poll();
                if (n != nullptr) {
                    s->consumed++;
                    s->consumedSum += n->v;
                }
            }
            // synchronized sorted map
            s->tree->put(k % 1000, k);
            if (i % 7 == 0) s->tree->remove((k + 3) % 1000);
            if (i % 3000 == 0) (void)s->tree->firstEntry();
            // JSYNC compound action: check-then-add must stay atomic
            const int32_t u = i % 500;
            JSYNC(s->unique) {
                if (!s->unique->contains(u)) s->unique->add(u);
            }
        }
    } catch (Throwable& t) {
        std::fprintf(stderr, "worker %d: %s\n", a->id, t.what());
        s->errors++;
    }
}

}  // namespace

JTEST(CollectionsFourThreads) {
    auto* s = new Shared();
    s->list = (new List<int32_t>())->shared();
    s->chm = new ConcurrentHashMap<int32_t, Node*>();
    s->set = Collections::synchronizedSet(new Set<int32_t>());
    s->queue = new ConcurrentLinkedQueue<Node*>();
    s->tree = Collections::synchronizedSortedMap(new TreeMap<int32_t, int32_t>());
    s->unique = Collections::synchronizedList(new List<int32_t>());
    uint64_t handles[kThreads];
    Arg* args[kThreads];
    for (int32_t t = 0; t < kThreads; t++) {
        args[t] = new Arg{s, t};
        handles[t] = gc::startNativeThread(&worker, args[t]);
    }
    for (int32_t t = 0; t < kThreads; t++) gc::joinNativeThread(handles[t]);

    JCHECK_EQ(s->errors.load(), 0);
    JCHECK_EQ(s->list->size(), kThreads * kOps / 2);
    // exactly the odd offsets remain
    auto* sorted = new List<int32_t>(s->list);
    Collections::sort(sorted);
    bool ok = true;
    for (int32_t i = 0; i < sorted->size(); i++) ok = ok && (sorted->get(i) % 2 == 1);
    JCHECK(ok);
    int32_t expectedMap = 0;
    for (int32_t i = 0; i < kOps; i++) expectedMap += (i % 3 == 0) ? 0 : 1;
    JCHECK_EQ(s->chm->size(), kThreads * expectedMap);
    for (auto& e : *s->chm) JCHECK_EQ(e.getValue()->v, e.getKey());
    JCHECK(s->set->size() <= 5000);
    // everything produced is consumed or still queued
    int64_t remainingSum = 0;
    int32_t remaining = 0;
    for (Node* n : *s->queue) {
        remaining++;
        remainingSum += n->v;
    }
    JCHECK_EQ(s->consumed.load() + remaining, 2 * kOps);
    const int64_t produced = 2 * (static_cast<int64_t>(kOps) * (kOps - 1) / 2);
    JCHECK_EQ(s->consumedSum.load() + remainingSum, produced);
    JCHECK(s->tree->size() <= 1000);
    std::vector<int32_t> tk;
    for (auto& e : *s->tree) tk.push_back(e.getKey());
    JCHECK(std::is_sorted(tk.begin(), tk.end()));
    JCHECK_EQ(s->unique->size(), 500);  // no duplicates: JSYNC and the list lock are one monitor
}

JTEST(CollectionsSurviveGarbageCollection) {
    // Objects referenced only from collection storage must survive collections.
    auto* map = new Map<int32_t, Node*>();
    auto* set = new Set<String>();
    auto* tree = new TreeMap<String, Node*>();
    auto* deque = new Deque<Node*>();
    auto* arr = new Array<Node*>(2000);
    auto* strs = new Array<String>(2000);
    auto* trove = new TIntObjectHashMap<List<Node*>*>();
    for (int32_t i = 0; i < 2000; i++) {
        map->put(i, new Node(i));
        set->add(str("s", i, "-some-long-string-to-avoid-sso"));
        tree->put(str("k", i), new Node(i));
        deque->addLast(new Node(i));
        (*arr)[i] = new Node(i);
        (*strs)[i] = str("string-number-", i, "-long-enough");
        auto* l = new List<Node*>();
        l->add(new Node(i));
        trove->put(i, l);
    }
    for (int32_t i = 0; i < 2000; i += 2) map->remove(i);  // leave dead slots behind
    for (int32_t round = 0; round < 3; round++) {
        for (int32_t j = 0; j < 20000; j++) (void)new Node(j);  // garbage
        gc::collect();
    }
    bool ok = true;
    for (int32_t i = 1; i < 2000; i += 2) ok = ok && map->get(i) != nullptr && map->get(i)->v == i && map->get(i)->payload.equals(str("n", i));
    JCHECK(ok);
    JCHECK_EQ(map->size(), 1000);
    ok = true;
    for (int32_t i = 0; i < 2000; i++) ok = ok && set->contains(str("s", i, "-some-long-string-to-avoid-sso"));
    JCHECK(ok);
    ok = true;
    for (int32_t i = 0; i < 2000; i++) ok = ok && tree->get(str("k", i))->v == i;
    JCHECK(ok);
    int32_t idx = 0;
    ok = true;
    for (Node* n : *deque) ok = ok && n->v == idx++;
    JCHECK(ok);
    ok = true;
    for (int32_t i = 0; i < 2000; i++) {
        ok = ok && (*arr)[i]->v == i && (*strs)[i].equals(str("string-number-", i, "-long-enough")) &&
             trove->get(i)->get(0)->v == i;
    }
    JCHECK(ok);
}
