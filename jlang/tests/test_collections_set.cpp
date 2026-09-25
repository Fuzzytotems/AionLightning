// Tests for jlang::Set, jlang::TreeSet, jlang::Deque, jlang::ConcurrentLinkedQueue,
// jlang::PriorityQueue, jlang::BitSet and jlang::TIntArrayList.
#include "jtest.h"

#include <set>

using namespace jlang;

namespace {

class Item : public virtual Object {
public:
    explicit Item(int32_t id, int32_t tag = 0) : id(id), tag(tag) {}
    bool equals(Object* o) override {
        auto* i = dynamic_cast<Item*>(o);
        return i != nullptr && i->id == id;
    }
    int32_t hashCode() override { return id; }
    String toString() override { return str("I", id); }
    int32_t id;
    int32_t tag;
};

// StatModifier-like: natural order by priority then id.
class Mod : public virtual Comparable<Mod*> {
public:
    Mod(int32_t prio, int32_t id) : prio(prio), id(id) {}
    int32_t compareTo(Mod* o) override {
        int32_t r = prio - o->prio;
        if (r == 0) r = id - o->id;
        return r;
    }
    String toString() override { return str(prio, ":", id); }
    int32_t prio;
    int32_t id;
};

class Shade final {
public:
    enum class Value : int32_t { _NULL = -1, LIGHT, MEDIUM, DARK };
    constexpr Shade() = default;
    constexpr Shade(std::nullptr_t) {}
    constexpr explicit Shade(Value v) : v_(v) {}
    constexpr operator Value() const { return v_; }
    int32_t ordinal() const { return static_cast<int32_t>(v_); }
    String name() const {
        static const char* const n[] = {"LIGHT", "MEDIUM", "DARK"};
        return v_ == Value::_NULL ? String() : String(n[ordinal()]);
    }
    String toString() const { return name(); }
    int32_t compareTo(Shade o) const { return ordinal() - o.ordinal(); }
    bool equals(Shade o) const { return v_ == o.v_; }
    int32_t hashCode() const { return ordinal(); }
    friend bool operator==(Shade a, Shade b) { return a.v_ == b.v_; }
    friend bool operator==(Shade a, std::nullptr_t) { return a.v_ == Value::_NULL; }
    static Array<Shade>* values() {
        return Array<Shade>::of({Shade(Value::DARK), Shade(Value::LIGHT), Shade(Value::MEDIUM)});
    }

private:
    Value v_ = Value::_NULL;
};

template<class C>
std::string s(C* c) {
    return std::string(c->toString());
}

}  // namespace

JTEST(SetBasics) {
    auto* set = new Set<Item*>();
    JCHECK(set->add(new Item(1)));
    JCHECK(set->add(new Item(2)));
    JCHECK(!set->add(new Item(1)));  // equals/hashCode
    JCHECK_EQ(set->size(), 2);
    JCHECK(set->contains(new Item(2)));
    JCHECK(set->remove(new Item(2)));
    JCHECK(!set->remove(new Item(2)));
    JCHECK(set->add(nullptr));
    JCHECK(set->contains(nullptr));
    JCHECK_EQ(s(set), std::string("[I1, null]"));
    set->clear();
    JCHECK(set->isEmpty());

    auto* ints = new Set<int32_t>({5, 1, 5, 3});
    JCHECK_EQ(s(ints), std::string("[5, 1, 3]"));  // insertion order
    JCHECK(ints->remove(5));                      // Set<int>::remove is remove(Object)
    JCHECK_EQ(s(ints), std::string("[1, 3]"));
    auto* strs = new Set<String>();
    strs->add("a");
    strs->add(String("a"));
    strs->add(String());  // null
    JCHECK_EQ(strs->size(), 2);
    JCHECK(strs->contains(nullptr));
    JCHECK(!strs->contains(""));
    // JDK reference: HashSet<String>{x,yy,zzz}.hashCode()
    auto* hs = new Set<String>({"x", "yy", "zzz"});
    JCHECK_EQ(hs->hashCode(), 125138);
    JCHECK(hs->equals(new Set<String>({"zzz", "x", "yy"})));
    JCHECK(!hs->equals(new Set<String>({"zzz", "x"})));
    JCHECK(!hs->equals(new List<String>({"x", "yy", "zzz"})));
    auto* cl = hs->clone();
    JCHECK(cl->equals(hs));
    auto* fromList = new Set<int32_t>(new List<int32_t>({3, 3, 2}));
    JCHECK_EQ(s(fromList), std::string("[3, 2]"));
    JCHECK(fromList->containsAll(new List<int32_t>({2})));
    JCHECK(fromList->removeAll(new List<int32_t>({2, 9})));
    JCHECK(fromList->addAll(new List<int32_t>({7, 8})));
    JCHECK(fromList->retainAll(new List<int32_t>({7})));
    JCHECK_EQ(s(fromList), std::string("[7]"));
    auto* arr = fromList->toArray();
    JCHECK_EQ(arr->length, 1);
    // enum elements
    JCHECK(Shade() == nullptr);
    JCHECK(Shade(Shade::Value::DARK) == Shade(Shade::Value::DARK));
    auto* shades = new Set<Shade>();
    shades->add(Shade(Shade::Value::DARK));
    JCHECK(shades->contains(Shade(Shade::Value::DARK)));
    JCHECK_EQ(s(shades), std::string("[DARK]"));
    // doubles: NaN equals NaN, -0.0 != 0.0 (Double.equals)
    auto* d = new Set<double>({std::nan(""), std::nan(""), 0.0, -0.0});
    JCHECK_EQ(d->size(), 3);
}

JTEST(SetIterationAndRemoval) {
    auto* set = new Set<int32_t>();
    for (int32_t i = 0; i < 100; i++) set->add(i);
    int32_t visited = 0;
    for (int32_t v : *set) {
        visited++;
        set->remove(v + 1);          // later elements removed: skipped
        if (v == 50) set->add(1000);  // added: visited at the end
    }
    JCHECK_EQ(visited, 51);
    JCHECK(set->contains(1000));
    Iterator<int32_t>* it = set->iterator();
    while (it->hasNext()) {
        if (it->next() % 4 == 0) it->remove();
    }
    for (int32_t v : *set) JCHECK(v % 4 != 0);
    JCHECK_THROWS(IllegalStateException, set->iterator()->remove());
    // churn: remove everything while iterating, then reuse
    for (int32_t v : *set) set->remove(v);
    JCHECK(set->isEmpty());
    for (int32_t i = 0; i < 10; i++) set->add(i);
    JCHECK_EQ(set->size(), 10);
    // shared set: snapshot iteration
    auto* sh = (new Set<int32_t>({1, 2}))->shared();
    for (int32_t v : *sh) sh->add(v + 10);
    JCHECK_EQ(sh->size(), 4);
    // a Set seen as a Collection
    Collection<int32_t>* c = sh;
    int32_t sum = 0;
    for (int32_t v : *c) sum += v;
    JCHECK_EQ(sum, 26);  // 1 + 2 + 11 + 12
}

JTEST(TreeSetOrdering) {
    auto* ts = new TreeSet<Mod*>();
    ts->add(new Mod(2, 1));
    ts->add(new Mod(1, 5));
    ts->add(new Mod(1, 2));
    JCHECK(!ts->add(new Mod(1, 2)));  // compareTo == 0: duplicate
    JCHECK_EQ(s(ts), std::string("[1:2, 1:5, 2:1]"));
    JCHECK_EQ(ts->first()->id, 2);
    JCHECK_EQ(ts->last()->prio, 2);
    JCHECK(ts->contains(new Mod(1, 5)));
    JCHECK(ts->remove(new Mod(1, 5)));
    JCHECK_EQ(ts->size(), 2);
    auto* ints = new TreeSet<int32_t>({5, 1, 9, 3, 7});
    JCHECK_EQ(s(ints), std::string("[1, 3, 5, 7, 9]"));
    JCHECK_EQ(ints->ceiling(4), 5);
    JCHECK_EQ(ints->floor(4), 3);
    JCHECK_EQ(ints->higher(5), 7);
    JCHECK_EQ(ints->lower(5), 3);
    JCHECK_EQ(ints->higher(9), 0);
    JCHECK_EQ(s(ints->headSet(5)), std::string("[1, 3]"));
    JCHECK_EQ(s(ints->headSet(5, true)), std::string("[1, 3, 5]"));
    JCHECK_EQ(s(ints->tailSet(5)), std::string("[5, 7, 9]"));
    JCHECK_EQ(s(ints->subSet(3, 9)), std::string("[3, 5, 7]"));
    JCHECK_EQ(s(ints->descendingSet()), std::string("[9, 7, 5, 3, 1]"));
    Iterator<int32_t>* di = ints->descendingIterator();
    JCHECK_EQ(di->next(), 9);
    JCHECK_EQ(ints->pollFirst(), 1);
    JCHECK_EQ(ints->pollLast(), 9);
    ints->headSet(6)->clear();  // copy with write-through
    JCHECK_EQ(s(ints), std::string("[7]"));
    auto* empty = new TreeSet<int32_t>();
    JCHECK_THROWS(NoSuchElementException, empty->first());
    JCHECK_EQ(empty->pollFirst(), 0);
    auto* strs = new TreeSet<String>({"b", "a", "C"});
    JCHECK_EQ(s(strs), std::string("[C, a, b]"));
    // EnumSet
    auto* all = TreeSet<Shade>::allOf(Shade::values());
    JCHECK_EQ(s(all), std::string("[LIGHT, MEDIUM, DARK]"));
    auto* none = new TreeSet<Shade>();  // EnumSet.noneOf
    none->add(Shade(Shade::Value::DARK));
    none->add(Shade(Shade::Value::LIGHT));
    JCHECK_EQ(s(none), std::string("[LIGHT, DARK]"));
    JCHECK(none->contains(Shade(Shade::Value::LIGHT)));
    // a TreeSet is a Set and a Collection
    Set<int32_t>* asSet = new TreeSet<int32_t>({3, 1, 2});
    std::string order;
    for (int32_t v : *asSet) order += std::to_string(v);
    JCHECK_EQ(order, std::string("123"));
    JCHECK(asSet->equals(new Set<int32_t>({2, 1, 3})));
    auto* addTo = new TreeSet<int32_t>();
    addTo->addAll(new List<int32_t>({4, 2}));
    JCHECK_EQ(s(addTo), std::string("[2, 4]"));
    JCHECK(addTo->clone()->equals(addTo));
}

JTEST(TreeSetInconsistentComparator) {
    // ZoneService/LegionHistory style: a comparator that never returns 0 keeps duplicates, and
    // contains/remove(Object) never find anything; iterator().remove() still works (JDK ref).
    auto* ts = new TreeSet<Item*>(Comparator<Item*>::of([](Item* a, Item* b) { return a->id > b->id ? 1 : -1; }));
    int32_t ids[][2] = {{3, 0}, {1, 1}, {3, 2}, {2, 3}, {1, 4}, {3, 5}};
    Item* first = nullptr;
    for (auto& e : ids) {
        auto* it = new Item(e[0], e[1]);
        if (first == nullptr) first = it;
        JCHECK(ts->add(it));
    }
    std::string order;
    for (Item* e : *ts) order += std::to_string(e->id) + ":" + std::to_string(e->tag) + " ";
    JCHECK_EQ(order, std::string("1:4 1:1 2:3 3:5 3:2 3:0 "));
    JCHECK_EQ(ts->size(), 6);
    JCHECK(!ts->contains(first));
    JCHECK(!ts->remove(first));
    Iterator<Item*>* it = ts->iterator();
    while (it->hasNext()) {
        Item* e = it->next();
        if (e->tag % 2 == 0) it->remove();
    }
    order.clear();
    for (Item* e : *ts) order += std::to_string(e->id) + ":" + std::to_string(e->tag) + " ";
    JCHECK_EQ(order, std::string("1:1 2:3 3:5 "));
    JCHECK_EQ(ts->size(), 3);
    // Mailbox style comparator on a TreeSet<Item*> with a Comparator<Object*>
    auto* byObj = new TreeSet<Item*>(Comparator<Object*>::of([](Object* a, Object* b) {
        return dynamic_cast<Item*>(b)->id - dynamic_cast<Item*>(a)->id;
    }));
    byObj->add(new Item(1));
    byObj->add(new Item(3));
    JCHECK_EQ(byObj->first()->id, 3);
}

JTEST(TreeSetStress) {
    // Random inserts/removals checked against std::set (consistent comparator).
    auto* ts = new TreeSet<int32_t>();
    std::set<int32_t> ref;
    uint32_t x = 7;
    for (int32_t i = 0; i < 20000; i++) {
        x = x * 1103515245u + 12345u;
        const int32_t v = static_cast<int32_t>((x >> 8) % 1000);
        if ((x >> 4) & 1) {
            JCHECK_EQ(ts->add(v), ref.insert(v).second);
        } else {
            JCHECK_EQ(ts->remove(v), ref.erase(v) == 1);
        }
    }
    JCHECK_EQ(ts->size(), static_cast<int32_t>(ref.size()));
    auto snap = ts->_snapshot();
    JCHECK(std::equal(snap.begin(), snap.end(), ref.begin(), ref.end()));
    Iterator<int32_t>* it = ts->iterator();
    while (it->hasNext()) {
        const int32_t v = it->next();
        if (v % 3 == 0) {
            it->remove();
            ref.erase(v);
        }
    }
    snap = ts->_snapshot();
    JCHECK(std::equal(snap.begin(), snap.end(), ref.begin(), ref.end()));
}

JTEST(DequeApi) {
    auto* d = new Deque<String>();
    d->addLast("b");
    d->addFirst("a");
    d->offer("c");
    d->push("z");
    JCHECK_EQ(s(d), std::string("[z, a, b, c]"));
    JCHECK_EQ(d->pollFirst(), String("z"));
    JCHECK_EQ(d->peekLast(), String("c"));
    JCHECK_EQ(d->getFirst(), String("a"));
    JCHECK(d->remove("b"));  // remove(Object)
    JCHECK_EQ(d->remove(), String("a"));
    JCHECK_EQ(d->pop(), String("c"));
    JCHECK(d->poll() == nullptr);
    JCHECK(d->peek() == nullptr);
    JCHECK_THROWS(NoSuchElementException, d->remove());
    JCHECK_THROWS(NoSuchElementException, d->element());
    JCHECK_THROWS(NoSuchElementException, d->removeLast());
    auto* q = new Deque<int32_t>({1, 2, 3, 2, 1});
    JCHECK(q->removeLastOccurrence(2));
    JCHECK_EQ(s(q), std::string("[1, 2, 3, 1]"));
    JCHECK(q->remove(1));  // Deque<int>::remove(Object): the value, not an index
    JCHECK_EQ(s(q), std::string("[2, 3, 1]"));
    JCHECK(q->contains(3));
    int32_t sum = 0;
    for (int32_t v : *q) {
        sum += v;
        q->addLast(100);  // appended elements are visited: stop after a few
        if (q->size() > 6) break;
    }
    JCHECK(sum > 0);
    Iterator<int32_t>* it = q->iterator();
    while (it->hasNext()) {
        if (it->next() == 100) it->remove();
    }
    JCHECK_EQ(s(q), std::string("[2, 3, 1]"));
    Iterator<int32_t>* desc = q->descendingIterator();
    JCHECK_EQ(desc->next(), 1);
    desc->remove();
    JCHECK_EQ(s(q), std::string("[2, 3]"));
    JCHECK_EQ(q->toArray()->length, 2);
    auto* fromList = new Deque<int32_t>(new List<int32_t>({7, 8}));
    JCHECK_EQ(fromList->pollLast(), 8);
    // FIFOSimpleExecutableQueue pattern
    auto* fifo = new Deque<Item*>();
    fifo->addLast(new Item(1));
    fifo->addLast(new Item(2));
    JCHECK_EQ(fifo->removeFirst()->id, 1);
}

JTEST(ConcurrentLinkedQueueApi) {
    Deque<Item*>* q = new ConcurrentLinkedQueue<Item*>();  // Queue<X> q = new ConcurrentLinkedQueue<X>()
    JCHECK(q->isShared());
    Item* a = new Item(1);
    q->add(a);
    q->add(new Item(2));
    JCHECK_THROWS(NullPointerException, q->add(nullptr));
    JCHECK_THROWS(NullPointerException, q->offer(nullptr));
    int32_t n = 0;
    for (Item* i : *q) {
        (void)i;
        q->add(new Item(9));  // snapshot iteration
        n++;
    }
    JCHECK_EQ(n, 2);
    JCHECK(q->remove(a));
    JCHECK_EQ(q->poll()->id, 2);
    auto* copy = new ConcurrentLinkedQueue<Item*>(q);
    JCHECK_EQ(copy->size(), q->size());
}

JTEST(PriorityQueueJavaOrder) {
    auto* pq = new PriorityQueue<int32_t>();
    for (int32_t x : {5, 3, 8, 1, 9, 2, 7, 4, 6, 0}) pq->add(x);
    JCHECK_EQ(s(pq), std::string("[0, 1, 2, 4, 3, 8, 7, 5, 6, 9]"));  // JDK array order
    JCHECK(pq->remove(3));
    JCHECK_EQ(s(pq), std::string("[0, 1, 2, 4, 9, 8, 7, 5, 6]"));
    std::string polled;
    while (!pq->isEmpty()) polled += std::to_string(pq->poll()) + " ";
    JCHECK_EQ(polled, std::string("0 1 2 4 5 6 7 8 9 "));
    JCHECK_EQ(pq->poll(), 0);
    JCHECK_THROWS(NoSuchElementException, pq->remove());
    JCHECK_THROWS(NoSuchElementException, pq->element());
    auto* byTag = new PriorityQueue<Item*>(11, Comparator<Item*>::of([](Item* a, Item* b) { return b->tag - a->tag; }));
    byTag->offer(new Item(1, 5));
    byTag->offer(new Item(2, 9));
    byTag->offer(new Item(3, 1));
    JCHECK_EQ(byTag->peek()->id, 2);
    JCHECK_THROWS(NullPointerException, byTag->add(nullptr));
    Iterator<Item*>* it = byTag->iterator();
    while (it->hasNext()) {
        if (it->next()->id == 2) it->remove();
    }
    JCHECK_EQ(byTag->poll()->id, 1);
    JCHECK_THROWS(IllegalArgumentException, new PriorityQueue<int32_t>(0));
    auto* mods = new PriorityQueue<Mod*>();
    mods->add(new Mod(3, 1));
    mods->add(new Mod(1, 1));
    JCHECK_EQ(mods->poll()->prio, 1);
    auto* heapified = new PriorityQueue<int32_t>(new List<int32_t>({9, 4, 7, 1}));
    JCHECK_EQ(heapified->poll(), 1);
    JCHECK_EQ(heapified->poll(), 4);
}

JTEST(BitSetJava) {
    auto* bs = new BitSet();
    for (int32_t i : {1, 3, 64, 65, 66, 200}) bs->set(i);
    // JDK reference values
    JCHECK_EQ(s(bs), std::string("{1, 3, 64, 65, 66, 200}"));
    JCHECK_EQ(bs->length(), 201);
    JCHECK_EQ(bs->size(), 256);
    JCHECK_EQ(bs->cardinality(), 6);
    JCHECK_EQ(bs->hashCode(), 214);
    JCHECK_EQ(bs->nextSetBit(4), 64);
    JCHECK_EQ(bs->nextClearBit(64), 67);
    bs->clear(200);
    JCHECK_EQ(s(bs), std::string("{1, 3, 64, 65, 66}"));
    JCHECK_EQ(bs->length(), 67);
    JCHECK_EQ(bs->size(), 256);
    JCHECK_EQ(bs->hashCode(), 1238);
    auto* b2 = new BitSet(10);
    b2->set(0, 70);
    JCHECK_EQ(b2->cardinality(), 70);
    JCHECK_EQ(b2->size(), 128);
    JCHECK_EQ(b2->get(5, 68)->cardinality(), 63);
    JCHECK_EQ(b2->hashCode(), 1196);
    b2->clear(3, 66);
    JCHECK_EQ(s(b2), std::string("{0, 1, 2, 66, 67, 68, 69}"));
    auto* b3 = bs->clone();
    b3->xor_(b2);
    JCHECK_EQ(s(b3), std::string("{0, 2, 3, 64, 65, 67, 68, 69}"));
    b3->and_(bs);
    JCHECK_EQ(s(b3), std::string("{3, 64, 65}"));
    b3->or_(b2);
    JCHECK_EQ(s(b3), std::string("{0, 1, 2, 3, 64, 65, 66, 67, 68, 69}"));
    b3->andNot(bs);
    JCHECK_EQ(s(b3), std::string("{0, 2, 67, 68, 69}"));
    b3->flip(0, 5);
    JCHECK_EQ(s(b3), std::string("{1, 3, 4, 67, 68, 69}"));
    JCHECK_EQ(b3->previousSetBit(100), 69);
    JCHECK_EQ(b3->previousClearBit(2), 2);
    // IdFactory pattern
    auto* ids = new BitSet();
    int32_t next = ids->nextClearBit(0);
    ids->set(next);
    JCHECK_EQ(ids->nextClearBit(0), 1);
    ids->set(5, true);
    ids->set(5, false);
    JCHECK(!ids->get(5));
    JCHECK(ids->get(0));
    JCHECK(!ids->get(100000));
    JCHECK(!ids->isEmpty());
    ids->clear();
    JCHECK(ids->isEmpty());
    JCHECK_EQ(ids->nextSetBit(0), -1);
    try {
        ids->set(-1);
        JCHECK(false);
    } catch (IndexOutOfBoundsException& e) {
        JCHECK_EQ(e.getMessage(), String("bitIndex < 0: -1"));
    }
    JCHECK_THROWS(IndexOutOfBoundsException, ids->nextSetBit(-1));
    JCHECK_THROWS(NegativeArraySizeException, new BitSet(-1));
    JCHECK(bs->equals(bs->clone()));
    JCHECK(!bs->equals(b2));
    JCHECK(bs->intersects(b2) == false || bs->intersects(b2) == true);
    auto* big = new BitSet();
    big->set(1000000);
    JCHECK_EQ(big->length(), 1000001);
    JCHECK_EQ(big->toLongArray()->length, 15626);
    JCHECK_EQ((new BitSet())->toByteArray()->length, 0);
}

JTEST(TroveIntArrayList) {
    auto* l = new TIntArrayList();
    JCHECK_EQ(s(l), std::string("{}"));
    l->add(3);
    l->add(1);
    l->add(2);
    JCHECK_EQ(s(l), std::string("{3, 1, 2}"));
    JCHECK_EQ(l->hashCode(), 86118);  // Trove 2.1 reference
    l->sort();
    JCHECK_EQ(s(l), std::string("{1, 2, 3}"));
    JCHECK_EQ(l->remove(0), 1);  // remove(offset) returns the value
    JCHECK_EQ(s(l), std::string("{2, 3}"));
    try {
        l->get(10);
        JCHECK(false);
    } catch (ArrayIndexOutOfBoundsException& e) {
        JCHECK_EQ(e.getMessage(), String("Array index out of range: 10"));
    }
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, l->remove(10));
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, l->set(10, 1));
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, l->insert(10, 1));
    JCHECK_EQ(l->getSet(0, 9), 2);
    JCHECK_EQ(s(l), std::string("{9, 3}"));
    JCHECK_EQ(l->max(), 9);
    JCHECK_EQ(l->min(), 3);
    try {
        (new TIntArrayList())->max();
        JCHECK(false);
    } catch (IllegalStateException& e) {
        JCHECK_EQ(e.getMessage(), String("cannot find maximum of an empty list"));
    }
    l->insert(1, 5);
    JCHECK_EQ(s(l), std::string("{9, 5, 3}"));
    JCHECK(l->contains(5));
    JCHECK_EQ(l->indexOf(3), 2);
    JCHECK(l->removeObject(5));  // remove(Object) of a List<Integer>
    auto* native = l->toNativeArray();
    JCHECK_EQ(Arrays::toString(native), String("[9, 3]"));
    l->add(Array<int32_t>::of({4, 4}));
    l->reverse();
    JCHECK_EQ(s(l), std::string("{4, 4, 3, 9}"));
    l->sort();
    JCHECK_EQ(l->binarySearch(9), 3);
    JCHECK_EQ(l->binarySearch(5), -4);
    JCHECK_EQ(l->indexOf(1, 4), 1);
    JCHECK_EQ(l->lastIndexOf(4), 2);
    JCHECK_EQ(s(l->subList(1, 3)), std::string("{4, 4}"));
    auto* g = l->grep(TIntProcedure::of([](int32_t v) { return v > 3; }));
    JCHECK_EQ(s(g), std::string("{4, 4, 9}"));
    l->transformValues(TIntFunction::of([](int32_t v) { return v + 1; }));
    JCHECK_EQ(s(l), std::string("{4, 5, 5, 10}"));
    l->remove(1, 2);
    JCHECK_EQ(s(l), std::string("{4, 10}"));
    JCHECK(l->equals(new TIntArrayList(Array<int32_t>::of({4, 10}))));
    int32_t sum = 0;
    for (int32_t index = 0; index < l->size(); index++) sum += l->get(index);  // QuestEngine pattern
    JCHECK_EQ(sum, 14);
    l->clear();
    JCHECK(l->isEmpty());
    auto* cl = (new TIntArrayList(Array<int32_t>::of({1})))->clone();
    JCHECK_EQ(s(cl), std::string("{1}"));
    List<int32_t>* asList = cl;
    JCHECK_EQ(asList->size(), 1);
}
