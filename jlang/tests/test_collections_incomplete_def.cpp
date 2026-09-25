// Definition of the element type used (incomplete) by test_collections_incomplete.cpp. This file
// must not instantiate any collection of inctest::Hidden*, so that the collections' code is
// only instantiated where Hidden is incomplete.
#include "jtest.h"

namespace inctest {

class Named : public virtual jlang::Object {
public:
    virtual ~Named() = default;
    int64_t pad1 = 1;
};
class Tagged : public virtual jlang::Object {
public:
    int32_t pad2 = 2;
};

// Several bases and virtual inheritance, so the Object subobject is not at offset 0.
class Hidden : public Named, public virtual Tagged, public virtual jlang::Comparable<Hidden*> {
public:
    explicit Hidden(int32_t id) : id(id) {}
    bool equals(jlang::Object* o) override {
        auto* h = dynamic_cast<Hidden*>(o);
        return h != nullptr && h->id == id;
    }
    int32_t hashCode() override { return id * 3; }
    jlang::String toString() override { return jlang::str("H", id); }
    int32_t compareTo(Hidden* o) override { return id - o->id; }
    int32_t id;
};

Hidden* makeHidden(int32_t id) { return new Hidden(id); }
int32_t hiddenId(Hidden* h) { return h->id; }
jlang::Object* hiddenAsObject(Hidden* h) { return h; }

}  // namespace inctest
