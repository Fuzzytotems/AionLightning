// Collections of pointers to an INCOMPLETE class (only forward-declared in this file), as in
// generated headers with `jlang::List<Foo*>* f = new jlang::List<Foo*>();` members. GCC
// instantiates the collections' virtual functions here; they must compile and behave exactly
// like with a complete type (Object subobject found through the ABI).
#include "jtest.h"

using namespace jlang;

namespace inctest {
class Hidden;
Hidden* makeHidden(int32_t id);
int32_t hiddenId(Hidden* h);
Object* hiddenAsObject(Hidden* h);

// A class like a generated one: member initializers create collections of Hidden*, the
// constructor is defined elsewhere (here: below).
class Holder : public virtual Object {
public:
    Holder();
    List<Hidden*>* list = new List<Hidden*>();
    Map<Hidden*, int32_t>* byKey = new Map<Hidden*, int32_t>();
    Set<Hidden*>* set = new Set<Hidden*>();
    TreeSet<Hidden*>* sorted = new TreeSet<Hidden*>();
    Map<int32_t, List<Hidden*>*>* nested = new Map<int32_t, List<Hidden*>*>();
    Deque<Hidden*>* queue = new ConcurrentLinkedQueue<Hidden*>();
};
Holder::Holder() {}
}  // namespace inctest

using inctest::Hidden;

JTEST(CollectionsOfIncompleteTypes) {
    auto* h = new inctest::Holder();
    Hidden* a = inctest::makeHidden(1);
    Hidden* b = inctest::makeHidden(2);
    Hidden* a2 = inctest::makeHidden(1);  // equals(a), not identical
    h->list->add(a);
    h->list->add(b);
    JCHECK(h->list->contains(a2));
    JCHECK_EQ(h->list->indexOf(a2), 0);
    JCHECK_EQ(h->list->toString(), String("[H1, H2]"));
    JCHECK_EQ(h->list->hashCode(), 31 * (31 * 1 + 3) + 6);
    JCHECK(h->list->remove(a2));
    JCHECK_EQ(h->list->size(), 1);

    h->byKey->put(a, 10);
    h->byKey->put(b, 20);
    JCHECK_EQ(h->byKey->get(a2), 10);  // hashCode()/equals() of the incomplete type
    JCHECK_EQ(h->byKey->toString(), String("{H1=10, H2=20}"));
    JCHECK(h->set->add(a));
    JCHECK(!h->set->add(a2));
    JCHECK(h->set->contains(a2));

    for (int32_t i : {5, 3, 4}) h->sorted->add(inctest::makeHidden(i));
    JCHECK_EQ(h->sorted->toString(), String("[H3, H4, H5]"));  // natural order via Comparable
    JCHECK_EQ(inctest::hiddenId(h->sorted->first()), 3);

    h->nested->put(1, h->list);
    JCHECK_EQ(h->nested->toString(), String("{1=[H2]}"));
    h->queue->add(b);
    JCHECK(h->queue->contains(inctest::makeHidden(2)));

    // the Object subobject found for an incomplete type is the real one
    JCHECK(detail::elemObject(a) == inctest::hiddenAsObject(a));
    JCHECK(detail::elemObject(b) == inctest::hiddenAsObject(b));
    JCHECK(detail::elemObject(static_cast<Hidden*>(nullptr)) == nullptr);
}
