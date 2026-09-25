// Tests for jlang::Map, jlang::Entry, the map views, ConcurrentHashMap, TreeMap and the Trove
// maps.
#include "jtest.h"

#include <random>
#include <unordered_map>

using namespace jlang;

namespace {

class MKey : public virtual Object {
public:
    explicit MKey(int32_t v) : v(v) {}
    bool equals(Object* o) override {
        auto* k = dynamic_cast<MKey*>(o);
        return k != nullptr && k->v == v;
    }
    int32_t hashCode() override { return v % 3; }  // many collisions on purpose
    String toString() override { return str("K", v); }
    int32_t v;
};

// A value class shaped like a generated Java enum (CONVENTIONS §7).
class Color final {
public:
    enum class Value : int32_t { _NULL = -1, RED, GREEN, BLUE };
    constexpr Color() = default;
    constexpr Color(std::nullptr_t) {}
    constexpr explicit Color(Value v) : v_(v) {}
    constexpr operator Value() const { return v_; }
    int32_t ordinal() const { return static_cast<int32_t>(v_); }
    String name() const {
        static const char* const names[] = {"RED", "GREEN", "BLUE"};
        return v_ == Value::_NULL ? String() : String(names[ordinal()]);
    }
    String toString() const { return name(); }
    int32_t compareTo(Color o) const { return ordinal() - o.ordinal(); }
    bool equals(Color o) const { return v_ == o.v_; }
    int32_t hashCode() const { return ordinal() * 1000; }
    friend bool operator==(Color a, Color b) { return a.v_ == b.v_; }
    friend bool operator==(Color a, std::nullptr_t) { return a.v_ == Value::_NULL; }
    static Array<Color>* values() {
        return Array<Color>::of({Color(Value::RED), Color(Value::GREEN), Color(Value::BLUE)});
    }
    static const Color RED, GREEN, BLUE;

private:
    Value v_ = Value::_NULL;
};
const Color Color::RED{Color::Value::RED};
const Color Color::GREEN{Color::Value::GREEN};
const Color Color::BLUE{Color::Value::BLUE};

template<class K, class V>
std::string keysOf(Map<K, V>* m) {
    return std::string(m->keySet()->toString());
}

}  // namespace

JTEST(MapBasics) {
    auto* m = new Map<String, int32_t>();
    JCHECK(m->isEmpty());
    JCHECK_EQ(m->put("a", 1), 0);
    JCHECK_EQ(m->put("b", 2), 0);
    JCHECK_EQ(m->put("a", 10), 1);  // previous value
    JCHECK_EQ(m->size(), 2);
    JCHECK_EQ(m->get("a"), 10);
    JCHECK_EQ(m->get("zz"), 0);  // absent: V{}
    JCHECK(!m->getOptional("zz").has_value());
    JCHECK_EQ(*m->getOptional("b"), 2);
    JCHECK_EQ(m->getOrDefault("zz", -1), -1);
    JCHECK(m->containsKey("b"));
    JCHECK(!m->containsKey("c"));
    JCHECK(m->containsValue(2));
    JCHECK(!m->containsValue(3));
    JCHECK_EQ(m->remove("b"), 2);
    JCHECK_EQ(m->remove("b"), 0);
    JCHECK(!m->removeOptional("b").has_value());
    JCHECK_EQ(*m->removeOptional("a"), 10);
    JCHECK(m->isEmpty());

    auto* p = new Map<int32_t, MKey*>();
    MKey* k1 = new MKey(1);
    JCHECK(p->put(1, k1) == nullptr);
    JCHECK(p->get(2) == nullptr);
    JCHECK(p->putIfAbsent(1, new MKey(9)) == k1);
    JCHECK(p->putIfAbsent(2, new MKey(2)) == nullptr);
    JCHECK_EQ(p->get(2)->v, 2);
    JCHECK(!p->remove(2, new MKey(3)));
    JCHECK(p->remove(2, new MKey(2)));  // equals()
    JCHECK(p->replace(1, new MKey(5))->v == 1);
    JCHECK(p->replace(7, new MKey(5)) == nullptr);
    JCHECK(!p->containsKey(7));
    JCHECK(p->replace(1, new MKey(5), new MKey(6)));
    JCHECK_EQ(p->get(1)->v, 6);

    // null key and values, like HashMap
    auto* n = new Map<MKey*, String>();
    n->put(nullptr, "nullkey");
    n->put(new MKey(1), nullptr);
    JCHECK_EQ(n->get(nullptr), String("nullkey"));
    JCHECK(n->containsKey(new MKey(1)));
    JCHECK(n->get(new MKey(1)) == nullptr);
    JCHECK(n->containsValue(nullptr));
    JCHECK_EQ(n->toString(), String("{null=nullkey, K1=null}"));

    auto* init = new Map<int32_t, String>({{1, "x"}, {2, "y"}});
    JCHECK_EQ(init->toString(), String("{1=x, 2=y}"));
    auto* copy = new Map<int32_t, String>(init);
    JCHECK(copy->equals(init));
    JCHECK_THROWS(IllegalArgumentException, new Map<int32_t, int32_t>(-1));
}

JTEST(MapInsertionOrderAndCollisions) {
    auto* m = new Map<MKey*, int32_t>();
    for (int32_t i = 0; i < 50; i++) m->put(new MKey(i), i);
    JCHECK_EQ(m->size(), 50);
    for (int32_t i = 0; i < 50; i++) JCHECK_EQ(m->get(new MKey(i)), i);
    for (int32_t i = 0; i < 50; i += 2) m->remove(new MKey(i));
    m->put(new MKey(1), 100);   // re-put keeps its position
    m->put(new MKey(0), 200);   // re-added: at the end
    std::string order;
    for (auto& e : *m) order += std::to_string(e.getKey()->v) + ",";
    JCHECK(order.rfind("1,3,5,", 0) == 0);
    JCHECK(order.size() > 3 && order.substr(order.size() - 3) == ",0,");
    JCHECK_EQ(m->get(new MKey(1)), 100);
}

JTEST(MapRandomizedAgainstStd) {
    std::mt19937 rng(12345);
    auto* m = new Map<int32_t, int32_t>();
    std::unordered_map<int32_t, int32_t> ref;
    for (int32_t step = 0; step < 200000; step++) {
        const int32_t k = static_cast<int32_t>(rng() % 3000) - 1500;
        const uint32_t op = rng() % 10;
        if (op < 5) {
            const int32_t v = static_cast<int32_t>(rng());
            auto it = ref.find(k);
            const int32_t expectOld = it == ref.end() ? 0 : it->second;
            JCHECK_EQ(m->put(k, v), expectOld);
            ref[k] = v;
        } else if (op < 8) {
            auto it = ref.find(k);
            const int32_t expectOld = it == ref.end() ? 0 : it->second;
            JCHECK_EQ(m->remove(k), expectOld);
            ref.erase(k);
        } else {
            JCHECK_EQ(m->containsKey(k), ref.count(k) == 1);
        }
        if (step % 50000 == 0) JCHECK_EQ(m->size(), static_cast<int32_t>(ref.size()));
    }
    JCHECK_EQ(m->size(), static_cast<int32_t>(ref.size()));
    int32_t n = 0;
    for (auto& e : *m) {
        JCHECK_EQ(ref.at(e.getKey()), e.getValue());
        n++;
    }
    JCHECK_EQ(n, static_cast<int32_t>(ref.size()));
    // everything removed, then reused
    for (auto& kv : ref) m->remove(kv.first);
    JCHECK(m->isEmpty());
    m->put(1, 1);
    JCHECK_EQ(m->size(), 1);
}

JTEST(MapViewsAreWriteThroughSnapshots) {
    auto* m = new Map<int32_t, String>();
    for (int32_t i = 0; i < 6; i++) m->put(i, String::valueOf(i * 10));
    List<int32_t>* keys = m->keySet();
    List<String>* vals = m->values();
    List<Entry<int32_t, String>>* entries = m->entrySet();
    JCHECK_EQ(keys->toString(), String("[0, 1, 2, 3, 4, 5]"));
    JCHECK_EQ(vals->toString(), String("[0, 10, 20, 30, 40, 50]"));
    JCHECK_EQ(entries->toString(), String("[0=0, 1=10, 2=20, 3=30, 4=40, 5=50]"));
    m->put(6, "60");  // snapshots: not visible
    JCHECK_EQ(keys->size(), 6);
    JCHECK(keys->contains(6));  // keySet().contains asks the map

    // values().iterator().remove() removes the mapping (KnownList.clear pattern)
    Iterator<String>* vi = m->values()->iterator();
    while (vi->hasNext()) {
        String v = vi->next();
        if (v.equals("20") || v.equals("40")) vi->remove();
    }
    JCHECK_EQ(keysOf(m), std::string("[0, 1, 3, 5, 6]"));
    // entrySet iterator remove
    Iterator<Entry<int32_t, String>>* ei = m->entryIterator();
    while (ei->hasNext()) {
        if (ei->next().getKey() == 0) ei->remove();
    }
    JCHECK(!m->containsKey(0));
    // keySet removal APIs
    JCHECK(m->keySet()->removeObject(1));  // Java keySet().remove(Object): int keys need removeObject
    JCHECK(!m->keySet()->removeObject(1));
    JCHECK_THROWS(UnsupportedOperationException, m->keySet()->remove(0));  // index removal on a view
    JCHECK(m->keySet()->removeAll(new List<int32_t>({3})));
    JCHECK(!m->containsKey(3));
    JCHECK(m->keySet()->retainAll(new List<int32_t>({5})));
    JCHECK_EQ(keysOf(m), std::string("[5]"));
    Iterator<int32_t>* ki = m->keyIterator();
    ki->next();
    ki->remove();
    JCHECK(m->isEmpty());
    // values().remove(v) removes one mapping with that value
    m->put(1, "a");
    m->put(2, "b");
    m->put(3, "a");
    JCHECK(m->values()->remove("a"));
    JCHECK_EQ(keysOf(m), std::string("[2, 3]"));
    // entrySet().remove(entry) needs key and value
    JCHECK(!m->entrySet()->remove(Entry<int32_t, String>(2, "zz")));
    JCHECK(m->entrySet()->remove(Entry<int32_t, String>(2, "b")));
    JCHECK(m->entrySet()->contains(Entry<int32_t, String>(3, "a")));
    // views reject insertion
    JCHECK_THROWS(UnsupportedOperationException, m->keySet()->add(9));
    JCHECK_THROWS(UnsupportedOperationException, m->values()->add("x"));
    JCHECK_THROWS(UnsupportedOperationException, m->entrySet()->add(Entry<int32_t, String>(1, "x")));
    JCHECK_THROWS(UnsupportedOperationException, m->values()->sort());
    // clear through a view clears the map
    m->values()->clear();
    JCHECK(m->isEmpty());
}

JTEST(MapEntries) {
    auto* m = new Map<String, String>();
    m->put("k1", "a");
    m->put("k2", "b");
    for (auto& e : *m->entrySet()) {
        if (e.getKey().equals("k1")) JCHECK_EQ(e.setValue("A"), String("a"));  // HTMLCache pattern
    }
    JCHECK_EQ(m->get("k1"), String("A"));
    for (auto& e : *m) e.setValue(e.getValue() + "!");  // range-for over the map itself
    JCHECK_EQ(m->toString(), String("{k1=A!, k2=b!}"));
    Entry<String, String> nul;
    JCHECK(nul == nullptr);
    JCHECK(nul.isNull());
    JCHECK_THROWS(NullPointerException, nul.getKey());
    Entry<String, String> e("x", "y");
    JCHECK(e != nullptr);
    JCHECK_EQ(e.toString(), String("x=y"));
    JCHECK_EQ(e.hashCode(), String("x").hashCode() ^ String("y").hashCode());
    JCHECK((e == Entry<String, String>("x", "y")));
    JCHECK(e.equals(Entry<String, String>("x", "y")));
    JCHECK(!e.equals(Entry<String, String>("x", "z")));
    e.setValue("w");  // detached entry: only the copy changes
    JCHECK_EQ(e.getValue(), String("w"));
    JCHECK_EQ(str("e: ", e), String("e: x=w"));
    // Entry in a Set/Map (hash + equals)
    auto* es = new Set<Entry<int32_t, int32_t>>();
    es->add(Entry<int32_t, int32_t>(1, 2));
    JCHECK(es->contains(Entry<int32_t, int32_t>(1, 2)));
    JCHECK(!es->contains(Entry<int32_t, int32_t>(1, 3)));
}

JTEST(MapEqualsHashToString) {
    auto* m = new Map<String, int32_t>();
    m->put("x", 1);
    m->put("yy", 2);
    m->put("zzz", 3);
    JCHECK_EQ(m->hashCode(), 125140);  // JDK
    JCHECK_EQ(m->toString(), String("{x=1, yy=2, zzz=3}"));
    auto* o = new Map<String, int32_t>();
    o->put("zzz", 3);
    o->put("x", 1);
    o->put("yy", 2);
    JCHECK(m->equals(o));
    JCHECK(o->equals(m));
    o->put("yy", 5);
    JCHECK(!m->equals(o));
    auto* t = new TreeMap<String, int32_t>(m);
    JCHECK(t->equals(m));  // AbstractMap.equals across implementations
    JCHECK(m->equals(t));
    JCHECK_EQ(t->hashCode(), m->hashCode());
    auto* self = new Map<int32_t, Object*>();
    self->put(1, self);
    JCHECK_EQ(self->toString(), String("{1=(this Map)}"));
    auto* mn = new Map<String, String>();
    mn->put("k", nullptr);
    mn->put(nullptr, "v");
    JCHECK_EQ(mn->toString(), String("{k=null, null=v}"));  // JDK
    JCHECK_EQ(mn->entrySet()->toString(), String("[k=null, null=v]"));
    auto* cl = m->clone();
    JCHECK(cl != m && cl->equals(m));
    JCHECK_EQ(str(m), String("{x=1, yy=2, zzz=3}"));
}

JTEST(MapIterationModifySafe) {
    auto* m = new Map<int32_t, int32_t>();
    for (int32_t i = 0; i < 100; i++) m->put(i, i);
    int32_t visited = 0;
    for (auto& e : *m) {
        visited++;
        if (e.getKey() % 2 == 0) m->remove(e.getKey() + 1);  // removals are skipped
        if (e.getKey() == 10) {
            for (int32_t j = 1000; j < 1500; j++) m->put(j, j);  // growth + rehash mid-loop
        }
        if (e.getKey() == 1200) m->clear();
    }
    JCHECK(visited > 0);
    JCHECK(m->isEmpty());
    // heavy remove/add churn while iterating: dead slots are compacted later
    for (int32_t i = 0; i < 1000; i++) m->put(i, i);
    int32_t seen = 0;
    for (auto& e : *m) {
        seen++;
        m->remove(e.getKey());
        if (seen < 500) m->put(10000 + seen, 0);
    }
    JCHECK(m->isEmpty());
    for (int32_t i = 0; i < 100; i++) m->put(i, i);
    JCHECK_EQ(m->size(), 100);
    JCHECK_EQ(m->get(99), 99);
}

JTEST(ConcurrentHashMapNulls) {
    auto* c = new ConcurrentHashMap<int32_t, MKey*>();
    JCHECK(c->isShared());
    JCHECK_THROWS(NullPointerException, c->put(1, nullptr));
    JCHECK_THROWS(NullPointerException, c->putIfAbsent(1, nullptr));
    auto* s = new ConcurrentHashMap<String, int32_t>();
    JCHECK_THROWS(NullPointerException, s->get(String()));
    JCHECK_THROWS(NullPointerException, s->put(String(), 1));
    s->put("a", 1);
    JCHECK_EQ(s->putIfAbsent("a", 2), 1);
    JCHECK_EQ(s->get("a"), 1);
    auto* copy = new ConcurrentHashMap<String, int32_t>(s);
    JCHECK_EQ(copy->get("a"), 1);
    // snapshot iteration while modifying
    for (auto& e : *copy) copy->put(e.getKey() + "x", 0);
    JCHECK_EQ(copy->size(), 2);
    Map<String, int32_t>* asMap = copy;
    JCHECK(asMap->isShared());
}

JTEST(MapEnumAndSharedKeys) {
    JCHECK(Color() == nullptr);
    JCHECK(Color::RED == Color::RED);
    auto* m = new Map<Color, int32_t>();
    m->put(Color::GREEN, 1);
    m->put(Color::RED, 2);
    JCHECK_EQ(m->get(Color::GREEN), 1);
    JCHECK_EQ(m->toString(), String("{GREEN=1, RED=2}"));
    auto* em = new TreeMap<Color, String>();  // EnumMap
    em->put(Color::BLUE, "b");
    em->put(Color::RED, "r");
    em->put(Color::GREEN, "g");
    JCHECK_EQ(em->toString(), String("{RED=r, GREEN=g, BLUE=b}"));  // ordinal order
    auto* copy = new TreeMap<Color, String>(em);
    JCHECK(copy->containsKey(Color::GREEN));
    JCHECK_EQ(copy->get(Color::BLUE), String("b"));
    auto* sh = (new Map<int32_t, int32_t>())->shared();
    JCHECK(sh->isShared());
}

JTEST(TreeMapNavigation) {
    auto* tm = new TreeMap<int32_t, String>();
    for (int32_t i = 0; i < 50; i++) tm->put((i * 37) % 101, str("v", i));
    for (int32_t i = 0; i < 101; i += 3) tm->remove(i);
    // JDK reference
    JCHECK_EQ(tm->keySet()->toString(), String("[2, 10, 13, 16, 19, 20, 22, 23, 26, 29, 37, 40, 43, 46, 47, 49, 50, 53, 56, 59, 67, 70, 73, 74, 76, 77, 80, 83, 86, 94, 97, 100]"));
    JCHECK_EQ(tm->ceilingKey(50), 50);
    JCHECK_EQ(tm->floorKey(50), 50);
    JCHECK_EQ(tm->higherKey(52), 53);
    JCHECK_EQ(tm->lowerKey(52), 50);
    JCHECK_EQ(tm->firstKey(), 2);
    JCHECK_EQ(tm->lastKey(), 100);
    JCHECK_EQ(tm->headMap(20)->toString(), String("{2=v41, 10=v3, 13=v14, 16=v25, 19=v36}"));
    JCHECK_EQ(tm->tailMap(90)->toString(), String("{94=v8, 97=v19, 100=v30}"));
    JCHECK_EQ(tm->subMap(40, 50)->toString(), String("{40=v12, 43=v23, 46=v34, 47=v4, 49=v45}"));
    JCHECK_EQ(tm->descendingMap()->keySet()->toString(), String("[100, 97, 94, 86, 83, 80, 77, 76, 74, 73, 70, 67, 59, 56, 53, 50, 49, 47, 46, 43, 40, 37, 29, 26, 23, 22, 20, 19, 16, 13, 10, 2]"));
    JCHECK_EQ(tm->headMap(20, true)->size(), 6);
    JCHECK_EQ(tm->tailMap(94, false)->size(), 2);
    JCHECK(tm->ceilingEntry(101) == nullptr);
    JCHECK_EQ(tm->ceilingEntry(95).getKey(), 97);
    JCHECK_EQ(tm->floorEntry(1).isNull(), true);
    JCHECK_EQ(tm->higherKey(100), 0);  // K{} when absent
    JCHECK_EQ(tm->firstEntry().getValue(), String("v41"));
    JCHECK_EQ(tm->lastEntry().getKey(), 100);
    Entry<int32_t, String> polled = tm->pollFirstEntry();
    JCHECK_EQ(polled.getKey(), 2);
    JCHECK(!tm->containsKey(2));
    JCHECK_EQ(tm->pollLastEntry().getKey(), 100);
    JCHECK_EQ(tm->descendingKeySet()->get(0), 97);

    // headMap copy writes through (TemporaryObjectsService pattern)
    auto* head = tm->headMap(20);
    head->remove(10);
    JCHECK(!tm->containsKey(10));
    head->clear();
    JCHECK_EQ(tm->firstKey(), 20);
    head->put(5, "five");
    JCHECK_EQ(tm->get(5), String("five"));
    JCHECK_THROWS(IllegalArgumentException, tm->subMap(50, 40));

    auto* empty = new TreeMap<int32_t, int32_t>();
    JCHECK_THROWS(NoSuchElementException, empty->firstKey());
    JCHECK_THROWS(NoSuchElementException, empty->lastKey());
    JCHECK(empty->firstEntry() == nullptr);
    JCHECK(empty->pollFirstEntry() == nullptr);
    JCHECK_THROWS(NullPointerException, empty->firstEntry().getKey());

    // Legion: remove(firstEntry().getKey())
    auto* ann = new TreeMap<int64_t, String>();
    ann->put(300, "c");
    ann->put(100, "a");
    ann->put(200, "b");
    ann->remove(ann->firstEntry().getKey());
    JCHECK_EQ(ann->toString(), String("{200=b, 300=c}"));
    // views of a TreeMap remove through
    Iterator<String>* vi = ann->values()->iterator();
    vi->next();
    vi->remove();
    JCHECK_EQ(ann->toString(), String("{300=c}"));
    // entry setValue writes through
    for (auto& e : *ann->entrySet()) e.setValue("z");
    JCHECK_EQ(ann->get(300), String("z"));
}

JTEST(TreeMapComparatorsAndStrings) {
    auto* byLen = new TreeMap<String, int32_t>(Comparator<String>::of([](const String& a, const String& b) {
        return a.length() - b.length();
    }));
    byLen->put("ccc", 3);
    byLen->put("a", 1);
    byLen->put("bb", 2);
    byLen->put("zz", 22);  // same length as "bb": replaces the value, keeps the key
    JCHECK_EQ(byLen->toString(), String("{a=1, bb=22, ccc=3}"));
    JCHECK(byLen->comparator() != nullptr);
    auto* strs = new TreeMap<String, int32_t>();
    strs->put("b", 1);
    strs->put("B", 2);
    strs->put("a", 3);
    strs->put("", 4);
    JCHECK_EQ(strs->keySet()->toString(), String("[, B, a, b]"));
    auto* desc = new TreeMap<int32_t, int32_t>(Collections::reverseOrder<int32_t>());
    for (int32_t i = 0; i < 5; i++) desc->put(i, i);
    JCHECK_EQ(desc->keySet()->toString(), String("[4, 3, 2, 1, 0]"));
    // natural order with a null pointer key: NullPointerException
    auto* objs = new TreeMap<Integer*, int32_t>();
    objs->put(Integer::valueOf(3), 3);
    JCHECK_THROWS(NullPointerException, objs->put(nullptr, 1));
    objs->put(Integer::valueOf(1), 1);
    JCHECK_EQ(objs->firstKey()->intValue(), 1);
    // keys without an ordering: ClassCastException
    auto* bad = new TreeMap<MKey*, int32_t>();
    bad->put(new MKey(1), 1);
    JCHECK_THROWS(ClassCastException, bad->put(new MKey(2), 2));
    // range-for over a TreeMap and a TreeMap seen as a Map (snapshot, ordered)
    Map<int32_t, int32_t>* asMap = desc;
    std::string s;
    for (auto& e : *asMap) {
        s += std::to_string(e.getKey());
        asMap->remove(e.getKey());
    }
    JCHECK_EQ(s, std::string("43210"));
    JCHECK(asMap->isEmpty());
    auto* sorted = Collections::synchronizedSortedMap(new TreeMap<int32_t, String>());
    JCHECK(sorted->isShared());
}

JTEST(TroveIntObjectHashMap) {
    auto* m = new TIntObjectHashMap<String>();
    m->put(1, "a");
    m->put(2, "b");
    JCHECK_EQ(m->toString(), String("{1=a,2=b}"));  // Trove format (insertion order)
    JCHECK(m->get(3) == nullptr);
    JCHECK_EQ(m->putIfAbsent(1, "zz"), String("a"));
    JCHECK(m->putIfAbsent(5, "q") == nullptr);
    Array<int32_t>* ks = m->keys();
    JCHECK_EQ(Arrays::toString(ks), String("[1, 2, 5]"));
    int32_t sum = 0;
    for (int32_t k : *m->keys()) sum += k;  // `for (int k : map.keys())`
    JCHECK_EQ(sum, 8);
    auto* big = new Array<int32_t>(5);
    JCHECK(m->keys(big) == big);
    Array<String>* vs = m->getValues();
    JCHECK_EQ(Arrays::toString(vs), String("[a, b, q]"));
    auto* va = m->getValues(new Array<String>(4));
    JCHECK((*va)[3] == nullptr);

    TIntObjectIterator<String>* it = m->iterator();
    std::string seen;
    while (it->hasNext()) {
        it->advance();
        seen += std::to_string(it->key()) + std::string(it->value());
        if (it->key() == 2) it->remove();
        else if (it->key() == 5) it->setValue("Q");
    }
    JCHECK_EQ(seen, std::string("1a2b5q"));
    JCHECK_EQ(m->toString(), String("{1=a,5=Q}"));
    JCHECK_THROWS(NoSuchElementException, it->advance());

    std::string visited;
    bool all = m->forEachEntry(TIntObjectProcedure<String>::of([&](int32_t k, String v) {
        visited += std::to_string(k) + std::string(v);
        return true;
    }));
    JCHECK(all);
    JCHECK_EQ(visited, std::string("1a5Q"));
    bool stopped = m->forEachEntry([](int32_t k, const String&) { return k != 1; });
    JCHECK(!stopped);
    JCHECK(m->forEachKey(TIntProcedure::of([](int32_t k) { return k > 0; })));
    JCHECK(m->forEachValue(TObjectProcedure<String>::of([](String v) { return !v.isEmpty(); })));
    JCHECK(m->retainEntries(TIntObjectProcedure<String>::of([](int32_t k, String) { return k == 5; })));
    JCHECK_EQ(m->size(), 1);
    m->transformValues(TObjectFunction<String, String>::of([](String v) { return v + v; }));
    JCHECK_EQ(m->get(5), String("QQ"));
    auto* c = m->clone();
    JCHECK(c->equals(m));
    // nested Trove types (QuestEngine)
    auto* nested = new TIntObjectHashMap<TIntArrayList*>();
    nested->put(7, new TIntArrayList());
    nested->get(7)->add(70);
    JCHECK(nested->get(7)->contains(70));
    auto* sh = (new TIntObjectHashMap<int32_t>())->shared();
    JCHECK(sh->isShared());
}

JTEST(TroveIntIntHashMap) {
    auto* ii = new TIntIntHashMap();
    ii->put(1, 10);
    ii->put(2, 20);
    JCHECK_EQ(ii->toString(), String("{1=10,2=20}"));
    // JDK/Trove reference: adjustValue adds, adjustOrPutValue, increment
    JCHECK(ii->adjustValue(1, 5));
    JCHECK_EQ(ii->get(1), 15);
    JCHECK(!ii->adjustValue(7, 5));
    JCHECK_EQ(ii->get(7), 0);
    JCHECK(!ii->containsKey(7));
    JCHECK_EQ(ii->adjustOrPutValue(1, 3, 100), 18);
    JCHECK_EQ(ii->adjustOrPutValue(9, 3, 100), 100);
    JCHECK(ii->increment(2));
    JCHECK_EQ(ii->get(2), 21);
    JCHECK(!ii->increment(55));
    JCHECK_EQ(ii->put(1, 5), 18);
    JCHECK_EQ(ii->put(77, 5), 0);
    JCHECK_EQ(ii->remove(77), 5);
    JCHECK_EQ(ii->remove(78), 0);
    // WrappedItem pattern: adjustValue(itemId, oldCount + 1) adds the whole expression
    ii->put(500, 3);
    int32_t oldCount = ii->get(500);
    ii->adjustValue(500, oldCount + 1);
    JCHECK_EQ(ii->get(500), 7);
    int32_t total = 0;
    for (int32_t k : *ii->keys()) total += ii->get(k);
    JCHECK_EQ(total, 5 + 21 + 100 + 7);
    JCHECK_EQ(Arrays::toString(ii->getValues()), String("[5, 21, 100, 7]"));
    TIntIntIterator* it = ii->iterator();
    int32_t n = 0;
    while (it->hasNext()) {
        it->advance();
        n += it->value();
    }
    JCHECK_EQ(n, total);
    JCHECK((ii->forEachEntry(TIntIntProcedure::of([](int32_t, int32_t v) { return v > 0; }))));
    ii->transformValues(TIntFunction::of([](int32_t v) { return v * 2; }));
    JCHECK_EQ(ii->get(9), 200);
    JCHECK(ii->retainEntries(TIntIntProcedure::of([](int32_t k, int32_t) { return k != 9; })));
    JCHECK(!ii->containsKey(9));
    auto* nested = new TIntObjectHashMap<TIntIntHashMap*>();  // PetSkillData
    nested->put(1, new TIntIntHashMap());
    nested->get(1)->put(2, 3);
    JCHECK_EQ(nested->get(1)->get(2), 3);
}
