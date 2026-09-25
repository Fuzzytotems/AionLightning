// Tests for jlang::Array, jlang::arraycopy, jlang::Arrays and jlang::ArrayUtils.
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
    int32_t hashCode() override { return id * 7; }
    String toString() override { return str("Item", id); }
    int32_t id;
};

class Rank : public virtual Comparable<Rank*> {
public:
    Rank(int32_t k, int32_t tag) : key(k), tag(tag) {}
    int32_t compareTo(Rank* o) override { return key - o->key; }
    int32_t key;
    int32_t tag;
};

template<class T>
String joined(Array<T>* a) {
    return Arrays::toString(a);
}

}  // namespace

JTEST(ArrayBasics) {
    auto* a = new Array<int32_t>(5);
    JCHECK_EQ(a->length, 5);
    for (int32_t v : *a) JCHECK_EQ(v, 0);
    (*a)[2] = 7;
    a->set(3, 9);
    JCHECK_EQ(a->get(2), 7);
    JCHECK_EQ((*a)[3], 9);
    JCHECK_EQ(a->data()[2], 7);
    JCHECK_EQ(a->end() - a->begin(), 5);

    auto* e = new Array<int8_t>(0);
    JCHECK_EQ(e->length, 0);
    JCHECK(e->begin() == e->end());

    auto* s = new Array<String>(3);
    JCHECK(s->get(0) == nullptr);
    (*s)[1] = "x";
    JCHECK_EQ((*s)[1], String("x"));

    auto* p = new Array<Item*>(4);
    for (Item* i : *p) JCHECK(i == nullptr);

    auto* b = new Array<bool>(3);
    JCHECK(!(*b)[0]);
    (*b)[1] = true;
    JCHECK((*b)[1]);

    auto* d = new Array<double>(2);
    JCHECK_EQ((*d)[1], 0.0);
    auto* o = new Array<std::optional<int32_t>>(2);
    JCHECK(!(*o)[0].has_value());
}

JTEST(ArrayBoundsAndNegative) {
    auto* a = new Array<int32_t>(3);
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, (*a)[3]);
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, (*a)[-1]);
    JCHECK_THROWS(IndexOutOfBoundsException, a->get(100));
    try {
        (*a)[5] = 1;
        JCHECK(false);
    } catch (ArrayIndexOutOfBoundsException& ex) {
        JCHECK_EQ(ex.getMessage(), String("5"));  // JDK 6 message
    }
    JCHECK_THROWS(NegativeArraySizeException, new Array<int32_t>(-1));
    JCHECK_THROWS(NegativeArraySizeException, Array<int32_t>::newMatrix(2, -1));
}

JTEST(ArrayOfMatrixClone) {
    auto* a = Array<int32_t>::of({1, 2, 3});
    JCHECK_EQ(a->length, 3);
    JCHECK_EQ(joined(a), String("[1, 2, 3]"));
    auto* strs = Array<String>::of({"a", "b"});
    JCHECK_EQ(Arrays::toString(strs), String("[a, b]"));

    auto* m = Array<int32_t>::newMatrix(3, 4);
    JCHECK_EQ(m->length, 3);
    JCHECK_EQ((*m)[2]->length, 4);
    (*(*m)[1])[3] = 5;
    JCHECK_EQ((*(*m)[1])[3], 5);
    JCHECK((*m)[0] != (*m)[1]);

    auto* m3 = Array<int8_t>::newMatrix(2, 3, 4);
    JCHECK_EQ((*(*m3)[1])[2]->length, 4);

    auto* jag = new Array<Array<int8_t>*>(2);
    JCHECK((*jag)[0] == nullptr);

    auto* c = a->clone();
    JCHECK(c != a);
    JCHECK(Arrays::equals(a, c));
    (*c)[0] = 100;
    JCHECK_EQ((*a)[0], 1);
    // identity equals/hashCode like Java arrays
    JCHECK(!a->equals(c));
    JCHECK(a->equals(a));
    JCHECK_EQ(a->hashCode(), a->hashCode());
    JCHECK(a->toString().startsWith("[I@"));
    JCHECK(strs->toString().startsWith("[Ljava.lang.String;@"));
    Object* ao = a;
    JCHECK(ao != nullptr);
}

JTEST(ArrayCopy) {
    auto* a = Array<int32_t>::of({1, 2, 3, 4, 5, 6});
    auto* b = new Array<int32_t>(6);
    arraycopy(a, 1, b, 2, 3);
    JCHECK_EQ(joined(b), String("[0, 0, 2, 3, 4, 0]"));
    // overlapping, forward and backward
    arraycopy(a, 0, a, 2, 4);
    JCHECK_EQ(joined(a), String("[1, 2, 1, 2, 3, 4]"));
    auto* c = Array<int32_t>::of({1, 2, 3, 4, 5, 6});
    arraycopy(c, 2, c, 0, 4);
    JCHECK_EQ(joined(c), String("[3, 4, 5, 6, 5, 6]"));
    // non-trivial elements, overlapping
    auto* s = Array<String>::of({"a", "b", "c", "d"});
    arraycopy(s, 0, s, 1, 3);
    JCHECK_EQ(Arrays::toString(s), String("[a, a, b, c]"));
    auto* s2 = Array<String>::of({"a", "b", "c", "d"});
    arraycopy(s2, 1, s2, 0, 3);
    JCHECK_EQ(Arrays::toString(s2), String("[b, c, d, d]"));
    // to a wider element type
    auto* items = Array<Item*>::of({new Item(1), new Item(2)});
    auto* objs = new Array<Object*>(2);
    arraycopy(items, 0, objs, 0, 2);
    JCHECK((*objs)[1] == (*items)[1]);
    // System::arraycopy forwards
    auto* z = new Array<int32_t>(6);
    System::arraycopy(c, 0, z, 0, 6);
    JCHECK(Arrays::equals(c, z));
    // exceptions, nothing copied
    auto* d = Array<int32_t>::of({1, 2, 3});
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, arraycopy(a, 4, d, 0, 3));
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, arraycopy(a, 0, d, 1, 3));
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, arraycopy(a, -1, d, 0, 1));
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, arraycopy(a, 0, d, 0, -1));
    JCHECK_EQ(joined(d), String("[1, 2, 3]"));
    JCHECK_THROWS(NullPointerException, arraycopy(static_cast<Array<int32_t>*>(nullptr), 0, d, 0, 0));
    arraycopy(a, 6, d, 3, 0);  // empty copy at the end is legal
}

JTEST(ArraysFillCopy) {
    auto* a = new Array<int32_t>(5);
    Arrays::fill(a, -1);
    JCHECK_EQ(joined(a), String("[-1, -1, -1, -1, -1]"));
    Arrays::fill(a, 1, 3, 7);
    JCHECK_EQ(joined(a), String("[-1, 7, 7, -1, -1]"));
    JCHECK_THROWS(IllegalArgumentException, Arrays::fill(a, 3, 1, 0));
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, Arrays::fill(a, 0, 6, 0));
    auto* items = new Array<Item*>(2);
    Item* it = new Item(3);
    Arrays::fill(items, it);
    JCHECK((*items)[1] == it);

    auto* c = Arrays::copyOf(a, 7);
    JCHECK_EQ(joined(c), String("[-1, 7, 7, -1, -1, 0, 0]"));
    JCHECK_EQ(joined(Arrays::copyOf(a, 2)), String("[-1, 7]"));
    JCHECK_THROWS(NegativeArraySizeException, Arrays::copyOf(a, -1));
    JCHECK_EQ(joined(Arrays::copyOfRange(a, 1, 3)), String("[7, 7]"));
    JCHECK_EQ(joined(Arrays::copyOfRange(a, 4, 7)), String("[-1, 0, 0]"));
    JCHECK_THROWS(IllegalArgumentException, Arrays::copyOfRange(a, 3, 1));
    JCHECK_THROWS(ArrayIndexOutOfBoundsException, Arrays::copyOfRange(a, 6, 7));
    auto* s = Arrays::copyOf(Array<String>::of({"x"}), 2);
    JCHECK(s->get(1) == nullptr);
}

JTEST(ArraysEqualsHashToString) {
    JCHECK(Arrays::equals(Array<int8_t>::of({1, 2}), Array<int8_t>::of({1, 2})));
    JCHECK(!Arrays::equals(Array<int8_t>::of({1, 2}), Array<int8_t>::of({1, 3})));
    JCHECK(!Arrays::equals(Array<int8_t>::of({1, 2}), Array<int8_t>::of({1})));
    JCHECK(Arrays::equals<int8_t>(nullptr, nullptr));
    JCHECK(!Arrays::equals(Array<int8_t>::of({1}), static_cast<Array<int8_t>*>(nullptr)));
    auto* nan = Array<double>::of({std::nan("")});
    JCHECK(Arrays::equals(nan, Array<double>::of({std::nan("")})));  // Double.equals semantics
    JCHECK(!Arrays::equals(Array<double>::of({0.0}), Array<double>::of({-0.0})));
    JCHECK(Arrays::equals(Array<Item*>::of({new Item(1), nullptr}), Array<Item*>::of({new Item(1), nullptr})));
    JCHECK(Arrays::equals(Array<String>::of({"a", nullptr}), Array<String>::of({"a", nullptr})));
    JCHECK(!Arrays::equals(Array<String>::of({""}), Array<String>::of({nullptr})));

    // reference values from the JDK
    JCHECK_EQ(Arrays::hashCode(Array<int8_t>::of({1, -2, 3, 127, -128})), 29499782);
    JCHECK_EQ(Arrays::hashCode(new Array<int32_t>(0)), 1);
    JCHECK_EQ(Arrays::hashCode(Array<double>::of({1.5, -0.0, std::nan("")})), 569406559);
    JCHECK_EQ(Arrays::hashCode(Array<int64_t>::of({INT64_C(1) << 40, -1})), 8897);
    JCHECK_EQ(Arrays::hashCode(Array<bool>::of({true, false})), 40359);
    JCHECK_EQ(Arrays::hashCode<int32_t>(nullptr), 0);

    JCHECK_EQ(Arrays::toString(Array<float>::of({1.0f, 0.1f, 1e10f, -0.0f})), String("[1.0, 0.1, 1.0E10, -0.0]"));
    JCHECK_EQ(Arrays::toString(Array<int8_t>::of({-1, 2})), String("[-1, 2]"));
    JCHECK_EQ(Arrays::toString(Array<char16_t>::of({u'a', u'b'})), String("[a, b]"));
    JCHECK_EQ(Arrays::toString(Array<double>::of({1.0, 1e-5, 123456789.0})), String("[1.0, 1.0E-5, 1.23456789E8]"));
    JCHECK_EQ(Arrays::toString<int32_t>(nullptr), String("null"));
    JCHECK_EQ(Arrays::toString(Array<String>::of({"a", nullptr})), String("[a, null]"));
    JCHECK_EQ(Arrays::toString(Array<Item*>::of({new Item(4), nullptr})), String("[Item4, null]"));
    JCHECK_EQ(Arrays::toString(new Array<int32_t>(0)), String("[]"));
    JCHECK_EQ(Arrays::toString(Array<bool>::of({true, false})), String("[true, false]"));
    auto* m = Array<int32_t>::newMatrix(2, 2);
    JCHECK_EQ(Arrays::deepToString(m), String("[[0, 0], [0, 0]]"));
    JCHECK(Arrays::deepEquals(m, Array<int32_t>::newMatrix(2, 2)));
}

JTEST(ArraysSort) {
    auto* a = Array<int32_t>::of({5, -1, 3, 3, 0, 100, -50});
    Arrays::sort(a);
    JCHECK_EQ(joined(a), String("[-50, -1, 0, 3, 3, 5, 100]"));
    auto* d = Array<double>::of({3.0, std::nan(""), -0.0, 0.0, -1.0});
    Arrays::sort(d);
    JCHECK_EQ(Arrays::toString(d), String("[-1.0, -0.0, 0.0, 3.0, NaN]"));
    auto* part = Array<int32_t>::of({9, 8, 7, 6, 5});
    Arrays::sort(part, 1, 4);
    JCHECK_EQ(joined(part), String("[9, 6, 7, 8, 5]"));
    JCHECK_THROWS(IllegalArgumentException, Arrays::sort(part, 3, 1));

    auto* s = Array<String>::of({"pear", "Apple", "apple", "banana"});
    Arrays::sort(s);
    JCHECK_EQ(Arrays::toString(s), String("[Apple, apple, banana, pear]"));

    // natural order through Comparable, stable
    auto* r = Array<Rank*>::of({new Rank(2, 0), new Rank(1, 1), new Rank(2, 2), new Rank(1, 3), new Rank(0, 4)});
    Arrays::sort(r);
    std::string tags;
    for (Rank* x : *r) tags += std::to_string(x->tag);
    JCHECK_EQ(tags, std::string("41302"));

    // comparator and lambda
    auto* items = Array<Item*>::of({new Item(3), new Item(1), new Item(2)});
    Arrays::sort(items, Comparator<Item*>::of([](Item* x, Item* y) { return y->id - x->id; }));
    JCHECK_EQ(Arrays::toString(items), String("[Item3, Item2, Item1]"));
    Arrays::sort(items, [](Item* x, Item* y) { return x->id - y->id; });
    JCHECK_EQ(Arrays::toString(items), String("[Item1, Item2, Item3]"));
    // a Comparator<Object*> sorts an Item array (Comparator<? super T>)
    Arrays::sort(items, Comparator<Object*>::of([](Object* x, Object* y) {
        return dynamic_cast<Item*>(y)->id - dynamic_cast<Item*>(x)->id;
    }));
    JCHECK_EQ((*items)[0]->id, 3);

    // Java 6 legacy merge sort result with an inconsistent comparator (JDK reference)
    auto* bad = new Array<int32_t>(30);
    for (int32_t i = 0; i < 30; i++) (*bad)[i] = (i * 7919) % 31;
    auto* boxed = new Array<Item*>(30);
    for (int32_t i = 0; i < 30; i++) (*boxed)[i] = new Item((*bad)[i]);
    Arrays::sort(boxed, [](Item* x, Item* y) { return x->id % 5 > y->id % 5 ? 1 : -1; });
    std::string got;
    for (int32_t i = 0; i < 30; i++) got += (i ? "," : "") + std::to_string((*boxed)[i]->id);
    JCHECK_EQ(got, std::string("0,25,5,30,10,15,20,11,16,21,1,26,6,22,2,27,7,12,28,8,13,18,23,3,14,19,24,4,29,9"));
}

JTEST(ArraysBinarySearchAsList) {
    auto* a = Array<int32_t>::of({1, 3, 5, 7, 9});
    JCHECK_EQ(Arrays::binarySearch(a, 7), 3);
    JCHECK_EQ(Arrays::binarySearch(a, 4), -3);
    JCHECK_EQ(Arrays::binarySearch(a, 100), -6);
    JCHECK_EQ(Arrays::binarySearch(a, 0), -1);
    JCHECK_EQ(Arrays::binarySearch(a, 1, 3, 5), 2);

    List<int32_t>* l = Arrays::asList(Array<int32_t>::of({1, 2, 3}));
    JCHECK_EQ(l->size(), 3);
    JCHECK(l->contains(2));
    auto* l2 = Arrays::asList(0, 3, 6, 9);
    JCHECK(l2->contains(6));
    JCHECK(!l2->contains(7));
    auto* l3 = Arrays::asList("-encoding", "UTF-8", "-g");
    static_assert(std::is_same_v<decltype(l3), List<String>*>);
    JCHECK_EQ(l3->get(1), String("UTF-8"));
    JCHECK_EQ(l3->toString(), String("[-encoding, UTF-8, -g]"));
    auto* l4 = Arrays::asList(String("a,b,c").split(","));
    JCHECK_EQ(l4->size(), 3);
    JCHECK_EQ(l4->get(2), String("c"));
}

JTEST(ArrayUtilsApi) {
    Array<Item*>* obs = nullptr;
    Item* i1 = new Item(1);
    Item* i2 = new Item(2);
    obs = ArrayUtils::add(obs, i1);
    JCHECK_EQ(obs->length, 1);
    obs = ArrayUtils::add(obs, i2);
    obs = ArrayUtils::add(obs, i1);
    JCHECK_EQ(Arrays::toString(obs), String("[Item1, Item2, Item1]"));
    JCHECK(ArrayUtils::contains(obs, new Item(2)));  // equals()
    JCHECK(!ArrayUtils::contains(obs, new Item(9)));
    JCHECK_EQ(ArrayUtils::indexOf(obs, i1), 0);
    JCHECK_EQ(ArrayUtils::indexOf(obs, i1, 1), 2);
    JCHECK_EQ(ArrayUtils::lastIndexOf(obs, i1), 2);
    JCHECK_EQ(ArrayUtils::indexOf(obs, static_cast<Item*>(nullptr)), -1);
    auto* r = ArrayUtils::removeElement(obs, i1);
    JCHECK_EQ(Arrays::toString(r), String("[Item2, Item1]"));
    JCHECK_EQ(obs->length, 3);  // the input is not modified
    auto* same = ArrayUtils::removeElement(obs, new Item(42));
    JCHECK(same != obs && Arrays::equals(same, obs));
    auto* r2 = ArrayUtils::remove(obs, 1);
    JCHECK_EQ(Arrays::toString(r2), String("[Item1, Item1]"));
    try {
        ArrayUtils::remove(obs, 3);
        JCHECK(false);
    } catch (IndexOutOfBoundsException& e) {
        JCHECK_EQ(e.getMessage(), String("Index: 3, Length: 3"));
    }
    JCHECK_THROWS(IndexOutOfBoundsException, ArrayUtils::remove<Item*>(nullptr, 0));
    JCHECK(ArrayUtils::removeElement<Item*>(nullptr, i1) == nullptr);

    auto* ints = Array<int32_t>::of({3, 5, 7});
    JCHECK(ArrayUtils::contains(ints, 5));
    JCHECK(!ArrayUtils::contains(ints, 4));
    JCHECK(!ArrayUtils::contains<int32_t>(nullptr, 4));
    JCHECK_EQ(joined(ArrayUtils::add(ints, 1, 4)), String("[3, 4, 5, 7]"));
    JCHECK_THROWS(IndexOutOfBoundsException, ArrayUtils::add(ints, 5, 4));
    JCHECK_EQ(joined(ArrayUtils::addAll(ints, Array<int32_t>::of({1}))), String("[3, 5, 7, 1]"));
    JCHECK_EQ(joined(ArrayUtils::subarray(ints, -5, 2)), String("[3, 5]"));
    JCHECK_EQ(ArrayUtils::subarray(ints, 2, 1)->length, 0);
    JCHECK(ArrayUtils::isEmpty<int32_t>(nullptr));
    JCHECK(ArrayUtils::isEmpty(new Array<int32_t>(0)));
    JCHECK(ArrayUtils::isNotEmpty(ints));
    JCHECK_EQ(ArrayUtils::getLength(ints), 3);
    ArrayUtils::reverse(ints);
    JCHECK_EQ(joined(ints), String("[7, 5, 3]"));
}
