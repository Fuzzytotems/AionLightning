// Tests for jlang::List, jlang::Collection, jlang::Iterator/Iterable/Comparator and
// jlang::Collections.
#include "jtest.h"

using namespace jlang;

namespace {

class Item : public virtual Object {
public:
    explicit Item(int32_t id) : id(id) {}
    bool equals(Object* o) override {
        auto* i = dynamic_cast<Item*>(o);
        return i != nullptr && i->id == id;
    }
    int32_t hashCode() override { return id; }
    String toString() override { return str("I", id); }
    int32_t id;
};

class Ranked : public virtual Comparable<Ranked*> {
public:
    Ranked(int32_t k, int32_t t) : key(k), tag(t) {}
    int32_t compareTo(Ranked* o) override { return key < o->key ? -1 : (key == o->key ? 0 : 1); }
    String toString() override { return str(key, "/", tag); }
    int32_t key;
    int32_t tag;
};

// A codebase class implementing Iterable (like Account implements Iterable<PlayerAccountData>).
class Bag : public virtual Iterable<Item*> {
public:
    Bag() : items(new List<Item*>()) {}
    Iterator<Item*>* iterator() override { return items->iterator(); }
    List<Item*>* items;
};

// A codebase class implementing Iterator (like IteratorIterator).
class CountDown : public virtual Iterator<Object*> {
public:
    explicit CountDown(int32_t n) : n(n) {}
    bool hasNext() override { return n > 0; }
    Object* next() override {
        if (n <= 0) throw NoSuchElementException();
        n--;
        return box(n);
    }
    int32_t n;
};

template<class T>
std::string join(List<T>* l) {
    return std::string(l->toString());
}

}  // namespace

JTEST(ListBasics) {
    auto* l = new List<Item*>();
    JCHECK(l->isEmpty());
    Item* a = new Item(1);
    Item* b = new Item(2);
    Item* c = new Item(3);
    JCHECK(l->add(a));
    l->add(b);
    l->add(c);
    JCHECK_EQ(l->size(), 3);
    JCHECK(l->get(1) == b);
    JCHECK(l->contains(new Item(2)));  // equals()
    JCHECK_EQ(l->indexOf(new Item(3)), 2);
    JCHECK_EQ(l->indexOf(new Item(9)), -1);
    JCHECK(l->set(0, c) == a);
    JCHECK_EQ(l->lastIndexOf(c), 2);
    l->add(1, a);
    JCHECK_EQ(join(l), std::string("[I3, I1, I2, I3]"));
    JCHECK(l->remove(1) == a);  // remove(int)
    JCHECK(l->remove(b));        // remove(Object)
    JCHECK(!l->remove(b));
    JCHECK_EQ(join(l), std::string("[I3, I3]"));
    JCHECK(l->removeObject(new Item(3)));
    JCHECK_EQ(l->size(), 1);
    l->clear();
    JCHECK(l->isEmpty());
    l->add(nullptr);
    JCHECK(l->contains(nullptr));
    JCHECK_EQ(join(l), std::string("[null]"));

    auto* init = new List<int32_t>({1, 2, 3});
    JCHECK_EQ(init->size(), 3);
    auto* cap = new List<String>(100);
    JCHECK(cap->isEmpty());
    JCHECK_THROWS(IllegalArgumentException, new List<int32_t>(-1));
}

JTEST(ListIntRemoveOverloads) {
    auto* l = new List<int32_t>({10, 20, 30, 20});
    // Java: list.remove(1) removes index 1; list.remove(Integer.valueOf(20)) removes the value.
    JCHECK_EQ(l->remove(1), 20);
    JCHECK_EQ(join(l), std::string("[10, 30, 20]"));
    JCHECK(l->removeObject(20));
    JCHECK_EQ(join(l), std::string("[10, 30]"));
    JCHECK_EQ(l->removeAt(0), 10);
    static_assert(std::is_same_v<decltype(std::declval<List<int32_t>&>().remove(1)), int32_t>);
    static_assert(std::is_same_v<decltype(std::declval<List<Item*>&>().remove(static_cast<Item*>(nullptr))), bool>);
    static_assert(std::is_same_v<decltype(std::declval<List<Item*>&>().remove(0)), Item*>);
    // List<int64_t>: remove(int) is the index, remove(long) the value (Java overloads)
    auto* ll = new List<int64_t>({5, 6, 7});
    JCHECK_EQ(ll->remove(0), INT64_C(5));
    JCHECK(ll->remove(INT64_C(7)));
    JCHECK_EQ(join(ll), std::string("[6]"));
    auto* ls = new List<String>({"a", "b"});
    JCHECK(ls->remove("a"));
    JCHECK_EQ(join(ls), std::string("[b]"));
}

JTEST(ListExceptions) {
    auto* l = new List<int32_t>({1, 2, 3});
    try {
        l->get(5);
        JCHECK(false);
    } catch (IndexOutOfBoundsException& e) {
        JCHECK_EQ(e.getMessage(), String("Index: 5, Size: 3"));
    }
    try {
        l->get(-1);
        JCHECK(false);
    } catch (ArrayIndexOutOfBoundsException& e) {
        JCHECK_EQ(e.getMessage(), String("-1"));
    }
    JCHECK_THROWS(IndexOutOfBoundsException, l->set(3, 0));
    JCHECK_THROWS(IndexOutOfBoundsException, l->remove(3));
    JCHECK_THROWS(IndexOutOfBoundsException, l->add(4, 0));
    JCHECK_THROWS(IndexOutOfBoundsException, l->add(-1, 0));
    l->add(3, 4);  // add at size is fine
    JCHECK_EQ(l->size(), 4);
    auto* e = new List<Item*>();
    JCHECK_THROWS(NoSuchElementException, e->getFirst());
    JCHECK_THROWS(NoSuchElementException, e->removeLast());
    JCHECK_THROWS(NoSuchElementException, e->remove());
    JCHECK_THROWS(NoSuchElementException, e->element());
    JCHECK(e->poll() == nullptr);
    JCHECK(e->peekLast() == nullptr);
    JCHECK_THROWS(NullPointerException, e->addAll(static_cast<Collection<Item*>*>(nullptr)));
    JCHECK_THROWS(IndexOutOfBoundsException, l->subList(-1, 2));
    JCHECK_THROWS(IndexOutOfBoundsException, l->subList(0, 9));
    JCHECK_THROWS(IllegalArgumentException, l->subList(2, 1));
}

JTEST(ListDequeStyle) {
    auto* l = new List<String>();
    l->addLast("b");
    l->addFirst("a");
    l->offer("c");
    l->push("z");
    JCHECK_EQ(join(l), std::string("[z, a, b, c]"));
    JCHECK_EQ(l->pop(), String("z"));
    JCHECK_EQ(l->peek(), String("a"));
    JCHECK_EQ(l->peekLast(), String("c"));
    JCHECK_EQ(l->getLast(), String("c"));
    JCHECK_EQ(l->poll(), String("a"));
    JCHECK_EQ(l->pollLast(), String("c"));
    JCHECK_EQ(l->removeFirst(), String("b"));
    JCHECK(l->poll() == nullptr);  // null String
    JCHECK(l->isEmpty());
}

JTEST(ListBulkOps) {
    auto* l = new List<int32_t>({1, 2, 3, 4, 5, 2});
    auto* rm = new List<int32_t>({2, 4});
    JCHECK(l->removeAll(rm));
    JCHECK_EQ(join(l), std::string("[1, 3, 5]"));
    JCHECK(!l->removeAll(rm));
    JCHECK(l->containsAll(new List<int32_t>({5, 1})));
    JCHECK(!l->containsAll(new List<int32_t>({5, 2})));
    JCHECK(l->retainAll(new List<int32_t>({3, 5, 9})));
    JCHECK_EQ(join(l), std::string("[3, 5]"));
    JCHECK(l->addAll(new List<int32_t>({7, 8})));
    JCHECK(l->addAll(1, new List<int32_t>({0})));
    JCHECK_EQ(join(l), std::string("[3, 0, 5, 7, 8]"));
    JCHECK(!l->addAll(new List<int32_t>()));
    JCHECK(l->addAll(l));  // doubles, like ArrayList
    JCHECK_EQ(l->size(), 10);
    JCHECK(l->removeAll(l));
    JCHECK(l->isEmpty());

    // addAll with a subtype collection (Collection<? extends E>)
    auto* objs = new List<Object*>();
    auto* items = new List<Item*>({new Item(1), new Item(2)});
    JCHECK(objs->addAll(items));
    JCHECK_EQ(objs->size(), 2);
    auto* copy = new List<Object*>(items);
    JCHECK_EQ(copy->size(), 2);
    // from a Set (any Collection)
    auto* s = new Set<int32_t>({3, 1, 2});
    auto* fromSet = new List<int32_t>(s);
    JCHECK_EQ(join(fromSet), std::string("[3, 1, 2]"));
    auto* sub = new List<int32_t>({1, 2, 3, 4});
    auto* sl = sub->subList(1, 3);
    JCHECK_EQ(join(sl), std::string("[2, 3]"));
    sl->add(9);  // a copy: the original is unchanged
    JCHECK_EQ(sub->size(), 4);
}

JTEST(ListToArray) {
    auto* l = new List<String>({"a", "b"});
    Array<String>* a = l->toArray();
    JCHECK_EQ(a->length, 2);
    JCHECK_EQ((*a)[1], String("b"));
    auto* big = new Array<String>(4);
    (*big)[3] = "keep";
    (*big)[2] = "x";
    JCHECK(l->toArray(big) == big);
    JCHECK((*big)[2] == nullptr);  // a[size] = null
    JCHECK_EQ((*big)[3], String("keep"));
    auto* small = new Array<String>(1);
    auto* r = l->toArray(small);
    JCHECK(r != small);
    JCHECK_EQ(r->length, 2);
    auto* items = new List<Item*>({new Item(5)});
    Array<Object*>* objs = items->toArray(new Array<Object*>(0));
    JCHECK_EQ(objs->length, 1);
    auto* ints = new List<int32_t>({1, 2});
    auto* ia = ints->toArray(new Array<int32_t>(3));
    JCHECK_EQ(Arrays::toString(ia), String("[1, 2, 0]"));
}

JTEST(ListEqualsHashToString) {
    // JDK reference values
    JCHECK_EQ((new List<int32_t>({1, 2, 3, -7}))->hashCode(), 955320);
    JCHECK_EQ((new List<String>({"a", "bc", "h\xc3\xa9llo"}))->hashCode(), 103314989);
    JCHECK((new List<int32_t>({1, 2}))->equals(new List<int32_t>({1, 2})));
    JCHECK(!(new List<int32_t>({1, 2}))->equals(new List<int32_t>({2, 1})));
    JCHECK(!(new List<int32_t>({1, 2}))->equals(new List<int32_t>({1})));
    JCHECK((new List<Item*>({new Item(1), nullptr}))->equals(new List<Item*>({new Item(1), nullptr})));
    JCHECK(!(new List<int32_t>())->equals(new Set<int32_t>()));
    JCHECK_EQ((new List<int32_t>())->hashCode(), 1);
    JCHECK_EQ((new List<float>({1.0f, 2.5f}))->toString(), String("[1.0, 2.5]"));
    JCHECK_EQ((new List<bool>({true, false}))->toString(), String("[true, false]"));
    JCHECK_EQ((new List<char16_t>({u'x'}))->toString(), String("[x]"));
    JCHECK_EQ((new List<int8_t>({-3}))->toString(), String("[-3]"));
    auto* self = new List<Object*>();
    self->add(self);
    JCHECK_EQ(self->toString(), String("[(this Collection)]"));
    auto* nested = new List<List<int32_t>*>({new List<int32_t>({1}), nullptr});
    JCHECK_EQ(nested->toString(), String("[[1], null]"));
    // concatenation with a collection pointer uses toString()
    JCHECK_EQ(str("x", new List<int32_t>({1, 2})), String("x[1, 2]"));
    auto* cl = (new List<int32_t>({4, 5}))->clone();
    JCHECK_EQ(cl->toString(), String("[4, 5]"));
}

JTEST(ListIterationModifySafe) {
    // Range-for never crashes when the list changes during the loop.
    auto* l = new List<int32_t>();
    for (int32_t i = 0; i < 10; i++) l->add(i);
    int32_t visited = 0;
    for (int32_t v : *l) {
        visited++;
        if (v % 2 == 0) l->removeObject(v);
        if (v == 2) l->add(100);  // appended elements are visited too
    }
    JCHECK_EQ(visited, 6);  // 0, 2, 4, 6, 8, 100 (each removal skips the next element)
    JCHECK(!l->contains(0));
    JCHECK_EQ(join(l), std::string("[1, 3, 5, 7, 9]"));
    // clearing inside the loop ends it
    auto* m = new List<Item*>({new Item(1), new Item(2), new Item(3)});
    int32_t n = 0;
    for (Item* it : *m) {
        (void)it;
        n++;
        m->clear();
    }
    JCHECK_EQ(n, 1);
    // loop variable stays valid even if the storage reallocates
    auto* s = new List<String>({"first"});
    for (auto& str0 : *s) {
        for (int32_t i = 0; i < 1000; i++) s->add(String("x"));
        JCHECK_EQ(str0, String("first"));
        break;
    }
    // entries iterated by reference (Java for-each over entrySet)
    auto* e = new List<Entry<int32_t, String>>();
    e->add(Entry<int32_t, String>(1, "a"));
    for (auto& en : *e) JCHECK_EQ(en.getValue(), String("a"));
    // std algorithms on the backing vector
    auto* v = new List<int32_t>({3, 1, 2});
    std::sort(v->vec().begin(), v->vec().end());
    JCHECK_EQ(join(v), std::string("[1, 2, 3]"));
}

JTEST(ListIterators) {
    auto* l = new List<int32_t>({1, 2, 3, 4, 5, 6});
    Iterator<int32_t>* it = l->iterator();
    JCHECK_THROWS(IllegalStateException, it->remove());
    while (it->hasNext()) {
        if (it->next() % 2 == 0) it->remove();
    }
    JCHECK_EQ(join(l), std::string("[1, 3, 5]"));
    JCHECK_THROWS(NoSuchElementException, it->next());

    Iterator<int32_t>* li = l->listIterator();
    JCHECK_EQ(li->next(), 1);
    li->set(10);
    li->add(11);
    JCHECK_EQ(li->nextIndex(), 2);
    JCHECK_EQ(li->next(), 3);
    JCHECK(li->hasPrevious());
    JCHECK_EQ(li->previous(), 3);
    JCHECK_EQ(li->previous(), 11);
    li->remove();
    JCHECK_EQ(join(l), std::string("[10, 3, 5]"));
    JCHECK_THROWS(IndexOutOfBoundsException, l->listIterator(4));
    Iterator<int32_t>* tail = l->listIterator(2);
    JCHECK_EQ(tail->next(), 5);
    JCHECK(!tail->hasNext());

    // Enumeration aliases
    Iterator<int32_t>* en = Collections::enumeration(l);
    int32_t sum = 0;
    while (en->hasMoreElements()) sum += en->nextElement();
    JCHECK_EQ(sum, 18);

    // modification between next() calls never crashes
    auto* m = new List<int32_t>({1, 2, 3});
    Iterator<int32_t>* mi = m->iterator();
    mi->next();
    m->clear();
    JCHECK(!mi->hasNext());
}

JTEST(IterableAdapters) {
    auto* bag = new Bag();
    bag->items->add(new Item(1));
    bag->items->add(new Item(2));
    int32_t sum = 0;
    for (Item* it : *bag) sum += it->id;
    JCHECK_EQ(sum, 3);
    Iterable<Item*>* asIterable = bag;
    int32_t count = 0;
    for (auto* it : *asIterable) {
        (void)it;
        count++;
    }
    JCHECK_EQ(count, 2);
    // A List seen as an Iterable/Collection
    Iterable<int32_t>* li = new List<int32_t>({4, 5});
    sum = 0;
    for (int32_t v : *li) sum += v;
    JCHECK_EQ(sum, 9);
    Collection<int32_t>* coll = new List<int32_t>({7, 8});
    sum = 0;
    for (int32_t v : *coll) sum += v;
    JCHECK_EQ(sum, 15);
    JCHECK(coll->remove(7));  // Collection.remove(Object)
    JCHECK_EQ(coll->size(), 1);
    // a codebase Iterator
    Iterator<Object*>* cd = new CountDown(3);
    std::string s;
    while (cd->hasNext()) s += std::string(cd->next()->toString());
    JCHECK_EQ(s, std::string("210"));
    JCHECK_THROWS(UnsupportedOperationException, cd->remove());
}

JTEST(ListSorting) {
    auto* l = new List<String>({"bb", "a", "ccc", "dd", "e", "fff", "g", "hh", "iii", "j"});
    Collections::sort(l, Comparator<String>::of([](const String& x, const String& y) { return x.length() - y.length(); }));
    JCHECK_EQ(join(l), std::string("[a, e, g, j, bb, dd, hh, ccc, fff, iii]"));  // JDK (stable)
    Collections::sort(l);
    JCHECK_EQ(join(l), std::string("[a, bb, ccc, dd, e, fff, g, hh, iii, j]"));
    Collections::sort(l, [](const String& x, const String& y) { return y.compareTo(x); });
    JCHECK_EQ(l->get(0), String("j"));
    Collections::sort(l, static_cast<Comparator<String>*>(nullptr));  // null comparator = natural
    JCHECK_EQ(l->get(0), String("a"));

    auto* r = new List<Ranked*>();
    for (int32_t i = 0; i < 20; i++) r->add(new Ranked(i % 3, i));
    Collections::sort(r);
    std::string order;
    for (Ranked* x : *r) order += std::to_string(x->tag) + ",";
    JCHECK_EQ(order, std::string("0,3,6,9,12,15,18,1,4,7,10,13,16,19,2,5,8,11,14,17,"));
    // Comparator<? super T>
    Collections::sort(r, Comparator<Object*>::of([](Object* a, Object* b) {
        return dynamic_cast<Ranked*>(b)->tag - dynamic_cast<Ranked*>(a)->tag;
    }));
    JCHECK_EQ(r->get(0)->tag, 19);
    // list->sort(...) directly and on List<int32_t>
    auto* ints = new List<int32_t>({5, 3, 9, -1});
    ints->sort();
    JCHECK_EQ(join(ints), std::string("[-1, 3, 5, 9]"));
    auto* bools = new List<bool>({true, false, true});
    bools->sort();
    JCHECK_EQ(bools->toString(), String("[false, true, true]"));
    auto* dbl = new List<double>({2.0, std::nan(""), -0.0, 0.0});
    dbl->sort();
    JCHECK_EQ(dbl->toString(), String("[-0.0, 0.0, 2.0, NaN]"));
    // elements without an ordering: ClassCastException, like Java
    auto* items = new List<Item*>({new Item(1), new Item(2)});
    JCHECK_THROWS(ClassCastException, Collections::sort(items));
    auto* withNull = new List<Ranked*>({new Ranked(1, 0), nullptr});
    JCHECK_THROWS(NullPointerException, Collections::sort(withNull));
}

JTEST(CollectionsUtilities) {
    auto* l = new List<int32_t>();
    for (int32_t i = 0; i < 20; i++) l->add(i);
    Collections::shuffle(l, new Random(42));
    JCHECK_EQ(join(l), std::string("[15, 16, 14, 13, 5, 11, 17, 18, 8, 2, 6, 7, 3, 1, 19, 4, 12, 0, 9, 10]"));  // JDK
    auto* ls = new List<String>({"a", "b", "c", "d", "e", "f", "g"});
    Collections::shuffle(ls, new Random(-5));
    JCHECK_EQ(join(ls), std::string("[a, c, e, b, d, g, f]"));  // JDK
    Collections::shuffle(ls);
    JCHECK_EQ(ls->size(), 7);

    auto* li = new List<int32_t>({4, 1, 9, 1, 7});
    Collections::reverse(li);
    JCHECK_EQ(join(li), std::string("[7, 1, 9, 1, 4]"));
    JCHECK_EQ(Collections::max(li), 9);
    JCHECK_EQ(Collections::min(li), 1);
    JCHECK_EQ(Collections::frequency(li, 1), 2);
    JCHECK_EQ(Collections::max(li, Comparator<int32_t>::of([](int32_t a, int32_t b) { return b - a; })), 1);
    JCHECK_EQ(Collections::binarySearch(new List<int32_t>({1, 3, 5, 7}), 4), -3);
    JCHECK_EQ(Collections::binarySearch(new List<int32_t>({1, 3, 5, 7}), 5), 2);
    JCHECK_THROWS(NoSuchElementException, Collections::max(new List<int32_t>()));
    Collections::swap(li, 0, 4);
    JCHECK_EQ(join(li), std::string("[4, 1, 9, 1, 7]"));
    JCHECK_THROWS(IndexOutOfBoundsException, Collections::swap(li, 0, 5));
    Collections::fill(li, 0);
    JCHECK_EQ(join(li), std::string("[0, 0, 0, 0, 0]"));
    auto* nc = Collections::nCopies(3, String("x"));
    JCHECK_EQ(join(nc), std::string("[x, x, x]"));
    JCHECK_THROWS(IllegalArgumentException, Collections::nCopies(-1, 0));
    auto* dest = new List<int32_t>({0, 0, 0});
    Collections::copy(dest, new List<int32_t>({1, 2}));
    JCHECK_EQ(join(dest), std::string("[1, 2, 0]"));
    JCHECK_THROWS(IndexOutOfBoundsException, Collections::copy(new List<int32_t>(), new List<int32_t>({1})));

    Item* it = new Item(5);
    List<Item*>* single = Collections::singletonList(it);
    JCHECK_EQ(single->size(), 1);
    JCHECK(single->get(0) == it);
    auto* nullList = Collections::singletonList<Item*>(nullptr);
    JCHECK(nullList->get(0) == nullptr);
    auto* sl = Collections::singletonList("s");
    static_assert(std::is_same_v<decltype(sl), List<String>*>);
    auto* st = Collections::singleton(3);
    JCHECK(st->contains(3));
    auto* sm = Collections::singletonMap(String("k"), 1);
    JCHECK_EQ(sm->get("k"), 1);
    auto* el = Collections::emptyList<Item*>();
    JCHECK(el->isEmpty());
    el->add(it);  // fresh, mutable
    JCHECK(Collections::emptyList<Item*>()->isEmpty());
    JCHECK(Collections::emptySet<int32_t>()->isEmpty());
    JCHECK((Collections::emptyMap<int32_t, Item*>())->isEmpty());
    JCHECK(Collections::unmodifiableList(single) == single);
    auto* m = new Map<int32_t, int32_t>();
    JCHECK(Collections::unmodifiableMap(m) == m);
    JCHECK(Collections::synchronizedMap(m) == m);
    JCHECK(m->isShared());
    auto* sy = Collections::synchronizedList(new List<int32_t>());
    JCHECK(sy->isShared());
    auto* coll = new List<int32_t>();
    JCHECK(Collections::addAll(coll, 1, 2, 3));
    JCHECK(Collections::addAll(coll, Array<int32_t>::of({4})));
    JCHECK_EQ(join(coll), std::string("[1, 2, 3, 4]"));
    auto* rs = new List<int32_t>({1, 3, 2});
    Collections::sort(rs, Collections::reverseOrder<int32_t>());
    JCHECK_EQ(join(rs), std::string("[3, 2, 1]"));
    JCHECK(Collections::disjoint(new List<int32_t>({1}), new List<int32_t>({2})));
    JCHECK(!Collections::disjoint(new List<int32_t>({1}), new List<int32_t>({1})));
    auto* fromMap = Collections::newSetFromMap(Collections::synchronizedMap(new Map<int32_t, bool>()));
    JCHECK(fromMap->isShared());
    fromMap->add(4);
    JCHECK(fromMap->contains(4));
}

JTEST(ListLockedSnapshotIteration) {
    auto* l = (new List<int32_t>({1, 2, 3, 4}))->shared();
    JCHECK(l->isShared());
    int32_t visited = 0;
    for (int32_t v : *l) {
        visited++;
        l->add(v * 10);  // not visited: iteration is over a snapshot
    }
    JCHECK_EQ(visited, 4);
    JCHECK_EQ(l->size(), 8);
    Iterator<int32_t>* it = l->iterator();
    while (it->hasNext()) {
        if (it->next() >= 10) it->remove();  // removes from the list
    }
    JCHECK_EQ(join(l), std::string("[1, 2, 3, 4]"));
    JSYNC(l) {  // the collection lock is the object monitor: reentrant
        l->add(5);
    }
    JCHECK_EQ(l->size(), 5);
}

JTEST(NaturalOrderingHelpers) {
    // jlang::Less: Java natural order as a C++ comparator
    std::vector<String> v{"b", "B", "a", ""};
    std::sort(v.begin(), v.end(), Less<String>());
    JCHECK_EQ(v[1], String("B"));
    std::vector<Ranked*> r{new Ranked(3, 0), new Ranked(1, 1)};
    std::sort(r.begin(), r.end(), Less<Ranked*>());
    JCHECK_EQ(r[0]->key, 1);
    std::vector<double> d{1.0, std::nan(""), -0.0, 0.0};
    std::sort(d.begin(), d.end(), Less<double>());
    JCHECK(std::signbit(d[0]) && d[0] == 0.0);
    JCHECK(std::isnan(d[3]));
    // raw Comparable through jlang::Object* (erased generics): Comparable<T> implements it
    auto* objs = new TreeSet<Object*>();
    objs->add(new Ranked(5, 0));
    objs->add(new Ranked(2, 1));
    JCHECK_EQ(dynamic_cast<Ranked*>(objs->first())->key, 2);
    JCHECK_THROWS(ClassCastException, objs->add(new Item(1)));
    // boxed numbers sort by value (Integer::compareTo)
    auto* boxes = new List<Integer*>({Integer::valueOf(3), Integer::valueOf(-1)});
    Collections::sort(boxes);
    JCHECK_EQ(boxes->get(0)->intValue(), -1);
    // Collections::list over an Enumeration
    auto* back = Collections::list(Collections::enumeration(new List<int32_t>({1, 2})));
    JCHECK_EQ(back->toString(), String("[1, 2]"));
}

JTEST(ConstIterationAndJoin) {
    auto* l = new List<String>({"a", "b"});
    JCHECK_EQ(String::join(", ", l), String("a, b"));
    const List<String>& cl = *l;
    int32_t n = 0;
    for (const auto& s : cl) n += s.length();
    JCHECK_EQ(n, 2);
    auto* set = new Set<int32_t>({1, 2});
    JCHECK_EQ(String::join("-", set), String("1-2"));
    auto* ts = new TreeSet<int32_t>({2, 1});
    JCHECK_EQ(String::join("-", ts), String("1-2"));
    auto* dq = new Deque<int32_t>({3, 4});
    JCHECK_EQ(String::join("", dq), String("34"));
}

namespace {
class Base : public virtual Object {};
class DerivedA : public Base {};
class DerivedB : public Base {};
}  // namespace

JTEST(SupertypeArguments) {
    // Java: list.contains(Object), map.get(Object)... accept any object; not-an-element = absent.
    auto* a1 = new DerivedA();
    auto* a2 = new DerivedA();
    Base* asBase = a1;
    Base* other = new DerivedB();
    Object* asObj = a2;
    auto* l = new List<DerivedA*>({a1, a2});
    JCHECK(l->contains(asBase));
    JCHECK(!l->contains(other));
    JCHECK_EQ(l->indexOf(asObj), 1);
    JCHECK_EQ(l->lastIndexOf(other), -1);
    JCHECK(!l->remove(other));
    JCHECK(l->remove(asBase));
    JCHECK_EQ(l->size(), 1);
    auto* s = new Set<DerivedA*>({a2});
    JCHECK(s->contains(asObj));
    JCHECK(!s->contains(other));
    JCHECK(s->remove(asObj));
    auto* ts = new TreeSet<DerivedA*>(Comparator<DerivedA*>::of([](DerivedA* x, DerivedA* y) {
        return x == y ? 0 : (x < y ? -1 : 1);
    }));
    ts->add(a1);
    JCHECK(ts->contains(asBase));
    auto* d = new Deque<DerivedA*>({a1});
    JCHECK(d->contains(asBase) && !d->contains(other));
    auto* m = new Map<DerivedA*, int32_t>();
    m->put(a1, 5);
    JCHECK_EQ(m->get(asBase), 5);
    JCHECK_EQ(m->get(other), 0);
    JCHECK(m->containsKey(asBase));
    JCHECK(!m->getOptional(other).has_value());
    auto* vm = new Map<int32_t, DerivedA*>();
    vm->put(1, a1);
    JCHECK(vm->containsValue(asBase));
    JCHECK(!vm->containsValue(other));
    JCHECK_EQ(m->remove(asBase), 5);
    auto* tm = new TreeMap<int32_t, DerivedA*>();
    tm->put(1, a2);
    JCHECK(tm->containsValue(asObj));
    auto* chm = new ConcurrentHashMap<DerivedA*, int32_t>();
    chm->put(a2, 1);
    JCHECK(chm->containsKey(asObj));
    JCHECK(!chm->containsKey(other));
}
