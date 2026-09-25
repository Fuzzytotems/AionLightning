// Tests for the JAXB runtime (jlang/Jaxb.h) with hand-written binders written exactly the way the
// code generator will emit them (docs/cpp-port/JAXB.md). Expected outputs in
// jlang/tests/data/xml/*.txt|*.xml come from the JAXB RI 2.3.9 (data/xml/java/JaxbFixtures.java),
// run on the same inputs with Java classes mirroring the gameserver ones.
#include "jtest.h"

#include <jlang/IO.h>
#include <jlang/Xml.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

using namespace jlang;

namespace {

std::string testDir() {
    std::string f = __FILE__;
    return f.substr(0, f.rfind('/'));
}
std::string dataFile(const std::string& rel) { return testDir() + "/data/xml/" + rel; }
std::string repoFile(const std::string& rel) { return testDir() + "/../../" + rel; }
std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
std::string fstr(float f) { return std::string(String::valueOf(f)); }

// Line-by-line comparison with a readable failure message.
void checkSameText(const std::string& actual, const std::string& expected, const char* what) {
    if (actual == expected) return;
    std::istringstream a(actual), e(expected);
    std::string la, le;
    int line = 1;
    while (true) {
        bool ga = static_cast<bool>(std::getline(a, la)), ge = static_cast<bool>(std::getline(e, le));
        if (!ga && !ge) break;
        if (!ga || !ge || la != le) {
            jtest::fail(__FILE__, __LINE__,
                        std::string(what) + " differs at line " + std::to_string(line) + "\n    actual:   " +
                            (ga ? la : "<eof>") + "\n    expected: " + (ge ? le : "<eof>"));
            return;
        }
        line++;
    }
    jtest::fail(__FILE__, __LINE__, std::string(what) + " differs (line endings / trailing data)");
}

// Enum value class in the CONVENTIONS §7 shape, as the generator emits it.
#define TEST_ENUM2(Name, A_, B_)                                                              \
    class Name final {                                                                        \
    public:                                                                                   \
        enum class Value : int32_t { _NULL = -1, A_, B_ };                                    \
        static const Name A_, B_;                                                             \
        constexpr Name() = default;                                                           \
        constexpr Name(std::nullptr_t) {}                                                     \
        constexpr explicit Name(Value v) : v_(v) {}                                           \
        constexpr operator Value() const { return v_; }                                       \
        int32_t ordinal() const { return static_cast<int32_t>(v_); }                          \
        String name() const {                                                                 \
            return v_ == Value::A_ ? String(#A_) : v_ == Value::B_ ? String(#B_) : String();  \
        }                                                                                     \
        String toString() const { return name(); }                                            \
        static Array<Name>* values() { return Array<Name>::of({A_, B_}); }                    \
        [[maybe_unused]] friend bool operator==(Name a, Name b) { return a.v_ == b.v_; }      \
        [[maybe_unused]] friend bool operator==(Name a, std::nullptr_t) { return a.v_ == Value::_NULL; } \
                                                                                              \
    private:                                                                                  \
        Value v_ = Value::_NULL;                                                              \
    };                                                                                        \
    inline const Name Name::A_{Name::Value::A_};                                              \
    inline const Name Name::B_{Name::Value::B_};

std::string enumStr(const auto& e) { return e == nullptr ? std::string("null") : std::string(e.name()); }

}  // namespace

// =======================================================================================
// Spawns: mirrors gameserver SpawnsData / SpawnGroup / SpawnTemplate.
namespace {  // test-local
namespace t_spawn {

TEST_ENUM2(SpawnTime, DAY, NIGHT)
TEST_ENUM2(SpawnHandlerType, RIFT, STATIC)

class SpawnGroup;
class SpawnTemplate;

class SpawnsData : public virtual Object {
public:
    List<SpawnGroup*>* spawnGroups = nullptr;

    void afterUnmarshal(xml::Unmarshaller* u, Object* parent) {
        (void)u, (void)parent;
        counter = spawnGroups == nullptr ? 0 : spawnGroups->size();
    }
    std::string dump();

    // ---- generated JAXB binder
    static constexpr const char* _jaxbRootName = "spawns";
    void _jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx);
    bool _jaxbAttribute(std::string_view name, std::string_view value, xml::JaxbContext* ctx);
    bool _jaxbElement(xml::Element* c, xml::JaxbContext* ctx);
    void _jaxbMarshal(xml::Writer* w);

private:
    int32_t counter = 0;
};

class SpawnGroup : public virtual Object {
public:
    // ---- generated JAXB binder
    void _jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx);
    bool _jaxbAttribute(std::string_view name, std::string_view value, xml::JaxbContext* ctx);
    bool _jaxbElement(xml::Element* c, xml::JaxbContext* ctx);
    void _jaxbMarshal(xml::Writer* w);

private:
    friend class SpawnsData;
    SpawnTime spawnTime;
    String anchor;
    SpawnHandlerType handler;
    int32_t interval = 0;
    int32_t pool = 0;
    int32_t npcid = 0;
    int32_t mapid = 0;
    int32_t randomWalk = 0;
    int32_t siegeNpcId_balaur = 0;
    int32_t siegeNpcId_asmodians = 0;
    int32_t siegeNpcId_elyos = 0;
    bool boss = false;
    List<SpawnTemplate*>* objects = nullptr;
};

class SpawnTemplate : public virtual Object {
public:
    void _jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx);
    bool _jaxbAttribute(std::string_view name, std::string_view value, xml::JaxbContext* ctx);
    bool _jaxbElement(xml::Element* c, xml::JaxbContext* ctx);
    void _jaxbMarshal(xml::Writer* w);

private:
    friend class SpawnsData;
    int32_t randomWalk = 0;
    int32_t walkerId = 0;
    int8_t heading = 0;
    float z = 0, y = 0, x = 0;
    int32_t staticid = 0;
    int32_t npcfly = 0;
    int32_t spawnId = 0;
};

// ---- SpawnsData
void SpawnsData::_jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx) {
    xml::JaxbScope scope(ctx, this);
    for (const xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx);
    for (xml::Element* c : e->children()) _jaxbElement(c, ctx);
    scope.finish();
    afterUnmarshal(ctx->getUnmarshaller(), scope.parent());
}
bool SpawnsData::_jaxbAttribute(std::string_view, std::string_view, xml::JaxbContext*) { return false; }
bool SpawnsData::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    if (c->name() == "spawn") {
        ctx->collection(spawnGroups)->add(ctx->unmarshal<SpawnGroup>(c));
        return true;
    }
    return false;
}
void SpawnsData::_jaxbMarshal(xml::Writer* w) {
    if (spawnGroups != nullptr) {
        for (SpawnGroup* v : *spawnGroups) {
            if (v == nullptr) continue;
            w->startElement("spawn");
            v->_jaxbMarshal(w);
            w->endElement();
        }
    }
}

// ---- SpawnGroup
void SpawnGroup::_jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx) {
    xml::JaxbScope scope(ctx, this);
    for (const xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx);
    for (xml::Element* c : e->children()) _jaxbElement(c, ctx);
    scope.finish();
}
bool SpawnGroup::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext*) {
    switch (xml::nameHash(n)) {
        case xml::nameHash("time"): if (n == "time") { spawnTime = xml::parseEnum<SpawnTime>(v); return true; } break;
        case xml::nameHash("anchor"): if (n == "anchor") { anchor = xml::parseString(v); return true; } break;
        case xml::nameHash("handler"): if (n == "handler") { handler = xml::parseEnum<SpawnHandlerType>(v); return true; } break;
        case xml::nameHash("interval"): if (n == "interval") { interval = xml::parseInt(v); return true; } break;
        case xml::nameHash("pool"): if (n == "pool") { pool = xml::parseInt(v); return true; } break;
        case xml::nameHash("npcid"): if (n == "npcid") { npcid = xml::parseInt(v); return true; } break;
        case xml::nameHash("map"): if (n == "map") { mapid = xml::parseInt(v); return true; } break;
        case xml::nameHash("rw"): if (n == "rw") { randomWalk = xml::parseInt(v); return true; } break;
        case xml::nameHash("npcid_dr"): if (n == "npcid_dr") { siegeNpcId_balaur = xml::parseInt(v); return true; } break;
        case xml::nameHash("npcid_da"): if (n == "npcid_da") { siegeNpcId_asmodians = xml::parseInt(v); return true; } break;
        case xml::nameHash("npcid_li"): if (n == "npcid_li") { siegeNpcId_elyos = xml::parseInt(v); return true; } break;
        case xml::nameHash("boss"): if (n == "boss") { boss = xml::parseBoolean(v); return true; } break;
        default: break;
    }
    return false;
}
bool SpawnGroup::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    if (c->name() == "object") {
        ctx->collection(objects)->add(ctx->unmarshal<SpawnTemplate>(c));
        return true;
    }
    return false;
}
void SpawnGroup::_jaxbMarshal(xml::Writer* w) {
    w->attribute("time", spawnTime);
    w->attribute("anchor", anchor);
    w->attribute("handler", handler);
    w->attribute("interval", interval);
    w->attribute("pool", pool);
    w->attribute("npcid", npcid);
    w->attribute("map", mapid);
    w->attribute("rw", randomWalk);
    w->attribute("npcid_dr", siegeNpcId_balaur);
    w->attribute("npcid_da", siegeNpcId_asmodians);
    w->attribute("npcid_li", siegeNpcId_elyos);
    w->attribute("boss", boss);
    if (objects != nullptr) {
        for (SpawnTemplate* v : *objects) {
            if (v == nullptr) continue;
            w->startElement("object");
            v->_jaxbMarshal(w);
            w->endElement();
        }
    }
}

// ---- SpawnTemplate
void SpawnTemplate::_jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx) {
    xml::JaxbScope scope(ctx, this);
    for (const xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx);
    for (xml::Element* c : e->children()) _jaxbElement(c, ctx);
    scope.finish();
}
bool SpawnTemplate::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext*) {
    if (n == "rw") { randomWalk = xml::parseInt(v); return true; }
    if (n == "w") { walkerId = xml::parseInt(v); return true; }
    if (n == "h") { heading = xml::parseByte(v); return true; }
    if (n == "z") { z = xml::parseFloat(v); return true; }
    if (n == "y") { y = xml::parseFloat(v); return true; }
    if (n == "x") { x = xml::parseFloat(v); return true; }
    if (n == "staticid") { staticid = xml::parseInt(v); return true; }
    if (n == "fly") { npcfly = xml::parseInt(v); return true; }
    return false;
}
bool SpawnTemplate::_jaxbElement(xml::Element* c, xml::JaxbContext*) {
    if (c->name() == "spawnId") {  // implicitly bound field (FIELD access)
        spawnId = xml::parseInt(c->text());
        return true;
    }
    return false;
}
void SpawnTemplate::_jaxbMarshal(xml::Writer* w) {
    w->attribute("rw", randomWalk);
    w->attribute("w", walkerId);
    w->attribute("h", heading);
    w->attribute("z", z);
    w->attribute("y", y);
    w->attribute("x", x);
    w->attribute("staticid", staticid);
    w->attribute("fly", npcfly);
    w->element("spawnId", spawnId);
}

std::string SpawnsData::dump() {
    std::string s = "counter=" + std::to_string(counter) + "\n";
    for (SpawnGroup* g : *spawnGroups) {
        s += "spawn time=" + enumStr(g->spawnTime) + " anchor=" + (g->anchor.isNull() ? std::string("null") : std::string(g->anchor)) +
             " handler=" + enumStr(g->handler) + " interval=" + std::to_string(g->interval) + " pool=" + std::to_string(g->pool) +
             " npcid=" + std::to_string(g->npcid) + " map=" + std::to_string(g->mapid) + " rw=" + std::to_string(g->randomWalk) +
             " dr=" + std::to_string(g->siegeNpcId_balaur) + " da=" + std::to_string(g->siegeNpcId_asmodians) +
             " li=" + std::to_string(g->siegeNpcId_elyos) + " boss=" + (g->boss ? "true" : "false") +
             " objects=" + (g->objects == nullptr ? std::string("null") : std::to_string(g->objects->size())) + "\n";
        if (g->objects == nullptr) continue;
        for (SpawnTemplate* t : *g->objects) {
            s += "  object rw=" + std::to_string(t->randomWalk) + " w=" + std::to_string(t->walkerId) +
                 " h=" + std::to_string(static_cast<int>(t->heading)) + " z=" + fstr(t->z) + " y=" + fstr(t->y) +
                 " x=" + fstr(t->x) + " staticid=" + std::to_string(t->staticid) + " fly=" + std::to_string(t->npcfly) +
                 " spawnId=" + std::to_string(t->spawnId) + "\n";
        }
    }
    return s;
}

}  // namespace t_spawn
}  // namespace

// =======================================================================================
// PlayerExperienceTable: long[] from repeated <exp> elements (array property).
namespace {  // test-local
namespace t_exp {

class PlayerExperienceTable : public virtual Object {
public:
    static constexpr const char* _jaxbRootName = "player_experience_table";
    void _jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx) {
        xml::JaxbScope scope(ctx, this);
        for (const xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx);
        for (xml::Element* c : e->children()) _jaxbElement(c, ctx);
        scope.finish();
    }
    bool _jaxbAttribute(std::string_view, std::string_view, xml::JaxbContext*) { return false; }
    bool _jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
        if (c->name() == "exp") {
            ctx->arrayAdd(experience, xml::parseLong(c->text()));
            return true;
        }
        return false;
    }
    Array<int64_t>* experience = nullptr;
};

}  // namespace t_exp
}  // namespace

// =======================================================================================
// Behavior probe (probe.xml): ordering, IDs/IDREFs, adapter, polymorphism, lists...
namespace {  // test-local
namespace t_probe {

std::string LOG;
void log(const std::string& s) { LOG += s + "\n"; }

class Kind final {  // enum Kind { A, B, @XmlEnumValue("C1") C }
public:
    enum class Value : int32_t { _NULL = -1, A, B, C };
    static const Kind A, B, C;
    constexpr Kind() = default;
    constexpr Kind(std::nullptr_t) {}
    constexpr explicit Kind(Value v) : v_(v) {}
    String name() const { return v_ == Value::A ? String("A") : v_ == Value::B ? String("B") : v_ == Value::C ? String("C") : String(); }
    String toXml() const { return v_ == Value::C ? String("C1") : name(); }
    static Array<Kind>* values() { return Array<Kind>::of({A, B, C}); }
    friend bool operator==(Kind a, Kind b) { return a.v_ == b.v_; }
    friend bool operator==(Kind a, std::nullptr_t) { return a.v_ == Value::_NULL; }

private:
    Value v_ = Value::_NULL;
};
inline const Kind Kind::A{Kind::Value::A};
inline const Kind Kind::B{Kind::Value::B};
inline const Kind Kind::C{Kind::Value::C};

std::string cls(Object* o);

#define BINDER_DECL                                                                       \
    void _jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx);                          \
    bool _jaxbAttribute(std::string_view name, std::string_view value, xml::JaxbContext* ctx); \
    bool _jaxbElement(xml::Element* c, xml::JaxbContext* ctx);

class Item : public virtual Object {
public:
    static inline int32_t N = 0;
    int32_t serial = ++N;
    String id;
    String name;
    void afterUnmarshal(xml::Unmarshaller*, Object*) {
        log("after Item [" + std::string(id) + "] " + std::string(name) + " #" + std::to_string(serial));
    }
    BINDER_DECL
};

class Title : public virtual Object {
public:
    String id;
    BINDER_DECL
};

class GearList : public virtual Object {
public:
    Array<Item*>* items = nullptr;
    void afterUnmarshal(xml::Unmarshaller*, Object* parent) {
        log("after GearList parent=" + cls(parent) + " items=" + (items == nullptr ? "null" : "set"));
    }
    BINDER_DECL
};

class Gear : public virtual Object {
public:
    GearList* list = nullptr;
};

// As the generator declares an XmlAdapter<GearList, Gear> subclass: typed overloads.
class GearAdapter : public xml::XmlAdapter {
public:
    virtual GearList* marshal(Gear*) { return nullptr; }
    virtual Gear* unmarshal(GearList* l) {
        log(std::string("adapter.unmarshal items=") + (l->items == nullptr ? "null" : "set"));
        auto* g = new Gear();
        g->list = l;
        return g;
    }
    using xml::XmlAdapter::unmarshal;
    using xml::XmlAdapter::marshal;
};

class Npc : public virtual Object {
public:
    static inline int32_t N = 0;
    int32_t serial = ++N;
    int32_t aggro = 0, arange = 0, rate = 0;
    float height = 1;
    bool flag = true;
    Kind kind;
    std::optional<int32_t> n;
    List<int32_t>* ids = nullptr;
    Gear* gear = nullptr;
    List<int32_t>* skills = nullptr;
    List<Kind>* classes = nullptr;
    String id;
    void afterUnmarshal(xml::Unmarshaller*, Object* parent) {
        log("after Npc " + std::string(id) + " #" + std::to_string(serial) + " parent=" + cls(parent) + " gear.items=" +
            (gear == nullptr ? "nogear" : gear->list->items == nullptr ? "null" : "set"));
    }
    BINDER_DECL

private:
    void setUid(const String& s) {
        id = s;
        log("setUid [" + std::string(s) + "] npc#" + std::to_string(serial));
    }
};

class Ref : public virtual Object {
public:
    Item* first = nullptr;
    Item* second = nullptr;
    Object* third = nullptr;
    List<Object*>* any = nullptr;
    void afterUnmarshal(xml::Unmarshaller*, Object*) {
        log("after Ref first=" + (first == nullptr ? std::string("null") : std::string(first->name)) +
            " second=" + (second == nullptr ? std::string("null") : std::string(second->name)) + " third=" + cls(third) +
            " any=" + (any == nullptr ? "null" : "set"));
    }
    BINDER_DECL
};

class M : public virtual Object, public virtual Comparable<M*> {
public:
    static inline int32_t ID = 0;
    int32_t myid;
    String name;
    int32_t priority = 0;
    M() { myid = ++ID; }
    int32_t compareTo(M* o) override {
        int32_t r = priority - o->priority;
        return r != 0 ? r : myid - o->myid;
    }
    virtual std::string kind() = 0;
    virtual void afterUnmarshal(xml::Unmarshaller*, Object* parent) {
        log("after " + kind() + " " + std::string(name) + " #" + std::to_string(myid) + " parent=" + cls(parent));
    }
    bool _jaxbAttribute(std::string_view name, std::string_view value, xml::JaxbContext* ctx);
    bool _jaxbElement(xml::Element* c, xml::JaxbContext* ctx);
};
class Add : public M {
public:
    std::string kind() override { return "Add"; }
    BINDER_DECL
};
class Set_ : public M {
public:
    std::string kind() override { return "Set_"; }
    BINDER_DECL
};

class Mods : public virtual Object {
public:
    TreeSet<M*>* m = nullptr;
    void afterUnmarshal(xml::Unmarshaller*, Object*) { log("after Mods size=" + std::to_string(m == nullptr ? -1 : m->size())); }
    BINDER_DECL
};

class Group : public virtual Object {
public:
    String value;
    Kind type;
    BINDER_DECL
};

class Root : public virtual Object {
public:
    List<Npc*>* npcs = nullptr;
    List<Item*>* items = nullptr;
    List<Title*>* titles = nullptr;
    Ref* ref = nullptr;
    Mods* mods = nullptr;
    List<Group*>* groups = nullptr;
    List<String>* pre = new List<String>({String("x"), String("y")});
    Array<int32_t>* arr = Array<int32_t>::of({7, 7});
    List<String>* texts = nullptr;
    void afterUnmarshal(xml::Unmarshaller*, Object* parent) {
        log("after Root parent=" + cls(parent) + " ref.first=" + std::string(ref->first->name) +
            " ref.any=" + (ref->any == nullptr ? "null" : "set") +
            " gear.items=" + (npcs->get(0)->gear->list->items == nullptr ? "null" : "set"));
    }
    static constexpr const char* _jaxbRootName = "root";
    BINDER_DECL
};

std::string cls(Object* o) {
    if (o == nullptr) return "null";
    if (instanceof<Root>(o)) return "Root";
    if (instanceof<Npc>(o)) return "Npc";
    if (instanceof<Mods>(o)) return "Mods";
    if (instanceof<Title>(o)) return "Title";
    if (instanceof<Item>(o)) return "Item";
    return "?";
}

#define STD_UNMARSHAL(C)                                                              \
    void C::_jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx) {                  \
        xml::JaxbScope scope(ctx, this);                                              \
        for (const xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx); \
        for (xml::Element* c : e->children()) _jaxbElement(c, ctx);                   \
        scope.finish();                                                               \
        afterUnmarshal(ctx->getUnmarshaller(), scope.parent());                       \
    }
#define STD_UNMARSHAL_NO_CALLBACK(C)                                                  \
    void C::_jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx) {                  \
        xml::JaxbScope scope(ctx, this);                                              \
        for (const xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx); \
        for (xml::Element* c : e->children()) _jaxbElement(c, ctx);                   \
        scope.finish();                                                               \
    }

// ---- Root
STD_UNMARSHAL(Root)
bool Root::_jaxbAttribute(std::string_view, std::string_view, xml::JaxbContext*) { return false; }
bool Root::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    std::string_view n = c->name();
    if (n == "npc") { ctx->collection(npcs)->add(ctx->unmarshal<Npc>(c)); return true; }
    if (n == "item") { ctx->collection(items)->add(ctx->unmarshal<Item>(c)); return true; }
    if (n == "title") { ctx->collection(titles)->add(ctx->unmarshal<Title>(c)); return true; }
    if (n == "ref") { ref = ctx->unmarshal<Ref>(c); return true; }
    if (n == "mods") { mods = ctx->unmarshal<Mods>(c); return true; }
    if (n == "group") { ctx->collection(groups)->add(ctx->unmarshal<Group>(c)); return true; }
    if (n == "pre") { ctx->collection(pre)->add(xml::parseString(c->text())); return true; }
    if (n == "arr") { ctx->arrayAdd(arr, xml::parseInt(c->text())); return true; }
    if (n == "text") { ctx->collection(texts)->add(xml::parseString(c->text())); return true; }
    return false;
}

// ---- Npc
STD_UNMARSHAL(Npc)
bool Npc::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext* ctx) {
    if (n == "srange") { aggro = xml::parseInt(v); return true; }  // first @XmlAttribute("srange") wins
    if (n == "arange") { arange = xml::parseInt(v); return true; }
    if (n == "height") { height = xml::parseFloat(v); return true; }
    if (n == "flag") { flag = xml::parseBoolean(v); return true; }
    if (n == "kind") { kind = xml::parseEnum<Kind>(v); return true; }
    if (n == "n") { this->n = xml::parseInt(v); return true; }
    if (n == "ids") { ids = xml::parseList<int32_t>(v, xml::parseInt); return true; }
    if (n == "npc_id") { setUid(ctx->id(v, this)); return true; }  // @XmlID on a private setter
    return false;
}
bool Npc::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    std::string_view n = c->name();
    if (n == "equipment") {  // @XmlJavaTypeAdapter(GearAdapter.class) on class Gear
        gear = ctx->adapter<GearAdapter>()->unmarshal(ctx->unmarshal<GearList>(c));
        return true;
    }
    if (n == "skill") { ctx->collection(skills)->add(xml::parseInt(c->text())); return true; }
    if (n == "classes") {  // @XmlList
        classes = xml::parseList<Kind>(c->text(), [](std::string_view t) { return xml::parseEnum<Kind>(t); });
        return true;
    }
    return false;
}

// ---- GearList: @XmlElement(name="item") @XmlIDREF Item[] items
STD_UNMARSHAL(GearList)
bool GearList::_jaxbAttribute(std::string_view, std::string_view, xml::JaxbContext*) { return false; }
bool GearList::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    if (c->name() == "item") {
        ctx->idrefItem(items, c->text(), [this](std::vector<Object*>& v) { items = xml::toArray<Item*>(v); });
        return true;
    }
    return false;
}

// ---- Item / Title
STD_UNMARSHAL(Item)
bool Item::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext* ctx) {
    if (n == "id") { id = ctx->id(v, this); return true; }
    if (n == "name") { name = xml::parseString(v); return true; }
    return false;
}
bool Item::_jaxbElement(xml::Element*, xml::JaxbContext*) { return false; }

STD_UNMARSHAL_NO_CALLBACK(Title)
bool Title::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext* ctx) {
    if (n == "id") { id = ctx->id(v, this); return true; }
    return false;
}
bool Title::_jaxbElement(xml::Element*, xml::JaxbContext*) { return false; }

// ---- Ref
STD_UNMARSHAL(Ref)
bool Ref::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext* ctx) {
    if (n == "first") { ctx->idref(v, [this](Object* o) { first = cast<Item>(o); }); return true; }
    if (n == "second") { ctx->idref(v, [this](Object* o) { second = cast<Item>(o); }); return true; }
    if (n == "third") { ctx->idref(v, [this](Object* o) { third = o; }); return true; }
    return false;
}
bool Ref::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    if (c->name() == "r") {
        ctx->idrefItem(any, c->text(), [this](std::vector<Object*>& v) { xml::toCollection(any, v); });
        return true;
    }
    return false;
}

// ---- Mods: @XmlElements({add -> Add, set -> Set_}) TreeSet<M> m
STD_UNMARSHAL(Mods)
bool Mods::_jaxbAttribute(std::string_view, std::string_view, xml::JaxbContext*) { return false; }
bool Mods::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    static const xml::ElementFactories<M> choices{
        {"add", xml::makeBean<M, Add>},
        {"set", xml::makeBean<M, Set_>},
    };
    if (auto make = choices.find(c->name())) {
        ctx->collection(m)->add(make(c, ctx));
        return true;
    }
    return false;
}

bool M::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext*) {
    if (n == "name") { name = xml::parseString(v); return true; }
    if (n == "priority") { priority = xml::parseInt(v); return true; }
    return false;
}
bool M::_jaxbElement(xml::Element*, xml::JaxbContext*) { return false; }
STD_UNMARSHAL(Add)
bool Add::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext* ctx) { return M::_jaxbAttribute(n, v, ctx); }
bool Add::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) { return M::_jaxbElement(c, ctx); }
STD_UNMARSHAL(Set_)
bool Set_::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext* ctx) { return M::_jaxbAttribute(n, v, ctx); }
bool Set_::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) { return M::_jaxbElement(c, ctx); }

// ---- Group: @XmlValue String value + @XmlAttribute Kind type
void Group::_jaxbUnmarshal(xml::Element* e, xml::JaxbContext* ctx) {
    xml::JaxbScope scope(ctx, this);
    for (const xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx);
    value = xml::parseString(e->text());
    for (xml::Element* c : e->children()) _jaxbElement(c, ctx);
    scope.finish();
}
bool Group::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext*) {
    if (n == "type") { type = xml::parseEnum<Kind>(v); return true; }
    return false;
}
bool Group::_jaxbElement(xml::Element*, xml::JaxbContext*) { return false; }

std::string listStr(List<int32_t>* l) {
    if (l == nullptr) return "null";
    std::string s = "[";
    for (int32_t i = 0; i < l->size(); i++) s += (i ? ", " : "") + std::to_string(l->get(i));
    return s + "]";
}
std::string listStr(List<Kind>* l) {
    if (l == nullptr) return "null";
    std::string s = "[";
    for (int32_t i = 0; i < l->size(); i++) s += (i ? ", " : "") + enumStr(l->get(i));
    return s + "]";
}
std::string listStr(List<String>* l) {
    if (l == nullptr) return "null";
    std::string s = "[";
    for (int32_t i = 0; i < l->size(); i++) s += (i ? ", " : "") + std::string(l->get(i));
    return s + "]";
}

std::string dump(Root* r) {
    std::string s;
    for (Npc* n : *r->npcs) {
        s += "npc #" + std::to_string(n->serial) + " id=[" + std::string(n->id) + "] aggro=" + std::to_string(n->aggro) +
             " arange=" + std::to_string(n->arange) + " rate=" + std::to_string(n->rate) + " height=" + fstr(n->height) +
             " flag=" + (n->flag ? "true" : "false") + " kind=" + enumStr(n->kind) +
             " n=" + (n->n ? std::to_string(*n->n) : std::string("null")) + " ids=" + listStr(n->ids) +
             " skills=" + listStr(n->skills) + " classes=" + listStr(n->classes);
        if (n->gear != nullptr) {
            s += " gear=[";
            for (Item* i : *n->gear->list->items) s += std::string(i->name) + "#" + std::to_string(i->serial) + " ";
            s += "]";
        }
        s += "\n";
    }
    for (Item* i : *r->items) s += "item [" + std::string(i->id) + "] " + std::string(i->name) + " #" + std::to_string(i->serial) + "\n";
    s += "titles=" + std::to_string(r->titles->size()) + "\n";
    s += "ref first=" + std::string(r->ref->first->name) + "#" + std::to_string(r->ref->first->serial) +
         " second=" + std::string(r->ref->second->name) + " third=" + cls(r->ref->third) + " any=";
    for (Object* o : *r->ref->any) s += (instanceof<Item>(o) ? std::string(cast<Item>(o)->name) : cls(o)) + " ";
    s += "\nmods=";
    for (M* m : *r->mods->m) s += m->kind() + ":" + std::string(m->name) + "#" + std::to_string(m->myid) + " ";
    s += "TreeSet\n";
    for (Group* g : *r->groups) s += "group type=" + enumStr(g->type) + " value=[" + std::string(g->value) + "]\n";
    s += "pre=" + listStr(r->pre) + "\n";
    s += "arr=[";
    for (int32_t i = 0; i < r->arr->length; i++) s += (i ? ", " : "") + std::to_string((*r->arr)[i]);
    s += "]\n";
    for (int32_t i = 0; i < r->texts->size(); i++) s += "text=[" + std::string(r->texts->get(i)) + "]\n";
    return s;
}

}  // namespace t_probe
}  // namespace

// =======================================================================================
// commons ScriptList / ScriptInfo (ScriptManager: JAXBContext.newInstance(ScriptInfo.class,
// ScriptList.class), java.io.File attribute and elements, Set collection, recursion).
namespace {  // test-local
namespace t_script {

class ScriptInfo : public virtual Object {
public:
    File* root = nullptr;
    List<File*>* libraries = nullptr;
    List<ScriptInfo*>* scriptInfos = nullptr;
    String compilerClass = "org.openaion.commons.scripting.impl.javacompiler.ScriptCompilerImpl";
    static constexpr const char* _jaxbRootName = "scriptinfo";
    BINDER_DECL
};

class ScriptList : public virtual Object {
public:
    Set<ScriptInfo*>* scriptInfos = nullptr;
    static constexpr const char* _jaxbRootName = "scriptlist";
    BINDER_DECL
};

STD_UNMARSHAL_NO_CALLBACK(ScriptInfo)
bool ScriptInfo::_jaxbAttribute(std::string_view n, std::string_view v, xml::JaxbContext*) {
    if (n == "root") { root = xml::parseFile(v); return true; }
    return false;
}
bool ScriptInfo::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    std::string_view n = c->name();
    if (n == "library") { ctx->collection(libraries)->add(xml::parseFile(c->text())); return true; }
    if (n == "scriptinfo") { ctx->collection(scriptInfos)->add(ctx->unmarshal<ScriptInfo>(c)); return true; }
    if (n == "compiler") { compilerClass = xml::parseString(c->text()); return true; }
    return false;
}

STD_UNMARSHAL_NO_CALLBACK(ScriptList)
bool ScriptList::_jaxbAttribute(std::string_view, std::string_view, xml::JaxbContext*) { return false; }
bool ScriptList::_jaxbElement(xml::Element* c, xml::JaxbContext* ctx) {
    if (c->name() == "scriptinfo") { ctx->collection(scriptInfos)->add(ctx->unmarshal<ScriptInfo>(c)); return true; }
    return false;
}

}  // namespace t_script
}  // namespace

JTEST(XmlJaxbScriptDescriptors) {
    auto* u = xml::JAXBContext::newInstance<t_script::ScriptInfo, t_script::ScriptList>()->createUnmarshaller();
    auto* list = cast<t_script::ScriptList>(u->unmarshal(new File(String(repoFile("gameserver/data/scripts/system/handlers.xml")))));
    JCHECK_EQ(list->scriptInfos->size(), 2);
    std::vector<std::string> roots;
    for (t_script::ScriptInfo* si : *list->scriptInfos) roots.push_back(std::string(si->root->getPath()));
    JCHECK((roots == std::vector<std::string>{"./data/scripts/system/handlers/admincommands",
                                              "./data/scripts/system/handlers/usercommands"}));
    JCHECK(list->scriptInfos->iterator()->next()->libraries == nullptr);
    // The root element picks the class; nested infos, libraries and the compiler element.
    auto* si = cast<t_script::ScriptInfo>(u->unmarshalString(
        "<scriptinfo root=' r '><library> lib/a.jar </library><library>b.jar</library>"
        "<scriptinfo root='child'><compiler>X</compiler></scriptinfo></scriptinfo>"));
    JCHECK_EQ(si->root->getPath(), String("r"));
    JCHECK_EQ(si->libraries->size(), 2);
    JCHECK_EQ(si->libraries->get(0)->getPath(), String("lib/a.jar"));
    JCHECK_EQ(si->scriptInfos->get(0)->compilerClass, String("X"));
    JCHECK(si->compilerClass.startsWith("org.openaion"));
}

JTEST(XmlJaxbSpawnsUnmarshalAndMarshal) {
    const char* files[] = {"spawns/Npcs/110010000.xml", "spawns/Rifts/rifts.xml"};
    xml::JAXBContext* jc = xml::JAXBContext::newInstance<t_spawn::SpawnsData>();
    for (const char* f : files) {
        std::string base = f;
        for (char& ch : base) ch = ch == '/' ? '_' : ch;
        base = base.substr(0, base.size() - 4);
        xml::Unmarshaller* un = jc->createUnmarshaller();
        un->setSchema(xml::SchemaFactory::newInstance(xml::XMLConstants::W3C_XML_SCHEMA_NS_URI)->newSchema());
        Object* o = un->unmarshal(String(repoFile("gameserver/data/static_data/" + std::string(f))));
        auto* data = cast<t_spawn::SpawnsData>(o);
        checkSameText(data->dump(), slurp(dataFile(base + ".dump.txt")), f);

        xml::Marshaller* m = jc->createMarshaller();
        m->setProperty(xml::Marshaller::JAXB_FORMATTED_OUTPUT, box(true));
        checkSameText(m->marshalToString(data), slurp(dataFile(base + ".formatted.xml")), "formatted marshal");
        JCHECK(m->marshalToString(data) == slurp(dataFile(base + ".formatted.xml")));
        xml::Marshaller* plain = jc->createMarshaller();
        JCHECK(plain->marshalToString(data) == slurp(dataFile(base + ".plain.xml")));

        // Round trip: the marshalled document loads to the same values.
        auto* again = un->unmarshalString<t_spawn::SpawnsData>(m->marshalToString(data));
        checkSameText(again->dump(), data->dump(), "round trip");
    }
}

JTEST(XmlJaxbPlayerExperienceTable) {
    auto* un = xml::JAXBContext::newInstance<t_exp::PlayerExperienceTable>()->createUnmarshaller();
    auto* t = cast<t_exp::PlayerExperienceTable>(
        un->unmarshal(String(repoFile("gameserver/data/static_data/player_experience_table.xml"))));
    JCHECK(t->experience != nullptr);
    std::string s = "[";
    for (int32_t i = 0; i < t->experience->length; i++) s += (i ? ", " : "") + std::to_string((*t->experience)[i]);
    s += "]\n";
    JCHECK_EQ(s, slurp(dataFile("player_experience_table.expected.txt")));
    JCHECK_EQ(t->experience->length, 56);
}

JTEST(XmlJaxbProbeSemantics) {
    t_probe::LOG.clear();
    t_probe::Npc::N = 0;
    t_probe::Item::N = 0;
    t_probe::M::ID = 0;
    auto* jc = xml::JAXBContext::newInstance<t_probe::Root>();
    auto* r = cast<t_probe::Root>(jc->createUnmarshaller()->unmarshal(String(dataFile("probe.xml"))));
    checkSameText(t_probe::LOG + "----\n" + t_probe::dump(r), slurp(dataFile("probe.expected.txt")), "probe");
}

JTEST(XmlJaxbFacadeErrors) {
    // Root dispatch among several classes (JAXBContext.newInstance(A.class, B.class)).
    auto* jc = xml::JAXBContext::newInstance<t_probe::Root, t_spawn::SpawnsData>();
    auto* un = jc->createUnmarshaller();
    JCHECK(instanceof<t_spawn::SpawnsData>(un->unmarshalString("<spawns><spawn npcid='1'/></spawns>")));
    t_probe::LOG.clear();
    JCHECK(instanceof<t_probe::Root>(un->unmarshal(String(dataFile("probe.xml")))));

    // Unknown root element: UnmarshalException, as JAXB ("unexpected element").
    auto* only = xml::JAXBContext::newInstance<t_exp::PlayerExperienceTable>()->createUnmarshaller();
    try {
        only->unmarshalString("<spawns/>");
        JCHECK(false);
    } catch (xml::UnmarshalException& e) {
        JCHECK(e.getMessage().contains("unexpected element (uri:\"\", local:\"spawns\")"));
        JCHECK(e.getMessage().contains("<{}player_experience_table>"));
    }
    // Typed unmarshal binds the root whatever its name (Reload.java: holder files).
    auto* d = only->unmarshalString<t_spawn::SpawnsData>("<whatever><spawn npcid='7'/></whatever>");
    JCHECK_EQ(d->spawnGroups->size(), 1);

    // Missing file: UnmarshalException linking FileNotFoundException.
    try {
        only->unmarshal(String(dataFile("does_not_exist.xml")));
        JCHECK(false);
    } catch (xml::UnmarshalException& e) {
        JCHECK(e.getLinkedException() != nullptr);
        JCHECK(instanceof<FileNotFoundException>(e.getLinkedException()));
        JCHECK(e.toString().contains("with linked exception"));
    }
    // Malformed XML: UnmarshalException linking a SAXParseException with a position.
    try {
        only->unmarshalString("<player_experience_table>\n<exp>1</exp>\n<exp>2</oops></player_experience_table>");
        JCHECK(false);
    } catch (xml::UnmarshalException& e) {
        auto* spe = dynamic_cast<xml::SAXParseException*>(e.getLinkedException());
        JCHECK(spe != nullptr);
        if (spe != nullptr) JCHECK_EQ(spe->getLineNumber(), 3);
    }
    // Lexical errors propagate as NumberFormatException (JAXB RI does not wrap them).
    JCHECK_THROWS(NumberFormatException, un->unmarshalString("<spawns><spawn interval='abc'/></spawns>"));
    JCHECK_THROWS(NumberFormatException, only->unmarshalString("<player_experience_table><exp>1 2</exp></player_experience_table>"));

    // Marshaller properties.
    auto* m = xml::JAXBContext::newInstance<t_spawn::SpawnsData>()->createMarshaller();
    JCHECK_THROWS(xml::PropertyException, m->setProperty(String("jaxb.bogus"), box(true)));
    m->setProperty(xml::Marshaller::JAXB_FRAGMENT, true);
    JCHECK_EQ(m->marshalToString(d), std::string("<spawns><spawn interval=\"0\" pool=\"0\" npcid=\"7\" map=\"0\" rw=\"0\" "
                                                 "npcid_dr=\"0\" npcid_da=\"0\" npcid_li=\"0\" boss=\"false\"/></spawns>"));
    // Objects of classes outside the context cannot be marshalled.
    JCHECK_THROWS(xml::MarshalException, m->marshalToString(new t_probe::Item()));
}

JTEST(XmlJaxbFileIO) {
    // unmarshal(File*) / marshal(Object*, File*) as XmlDataLoader and SaveSpawnData use them.
    auto* jc = xml::JAXBContext::newInstance<t_spawn::SpawnsData>();
    auto* data = cast<t_spawn::SpawnsData>(
        jc->createUnmarshaller()->unmarshal(new File(String(repoFile("gameserver/data/static_data/spawns/Rifts/rifts.xml")))));
    std::string out = (std::filesystem::temp_directory_path() / ("jlang_xml_marshal_" + std::to_string(::getpid()) + ".xml")).string();
    xml::Marshaller* m = jc->createMarshaller();
    m->setSchema(xml::SchemaFactory::newInstance(xml::XMLConstants::W3C_XML_SCHEMA_NS_URI)->newSchema(new File(String("spawns.xsd"))));
    m->setProperty(xml::Marshaller::JAXB_FORMATTED_OUTPUT, box(true));
    m->marshal(data, new File(String(out)));
    JCHECK(slurp(out) == slurp(dataFile("spawns_Rifts_rifts.formatted.xml")));
    std::filesystem::remove(out);
    // A directory that does not exist: MarshalException linking the FileNotFoundException.
    try {
        m->marshal(data, new File(String("/nonexistent-dir/x.xml")));
        JCHECK(false);
    } catch (xml::MarshalException& e) {
        JCHECK(instanceof<FileNotFoundException>(e.getCause()));
    }
    // InputStream variant.
    auto* in = new FileInputStream(new File(String(dataFile("probe.xml"))));
    t_probe::LOG.clear();
    JCHECK(instanceof<t_probe::Root>(xml::JAXBContext::newInstance<t_probe::Root>()->createUnmarshaller()->unmarshal(in)));
    in->close();
}

JTEST(XmlJaxbWriterFormatting) {
    xml::Writer w(true);
    w.startDocument();
    w.startElement("a");
    w.attribute("q", "x\"<&>\n\r\t'");
    w.element("t", String("a<b>&\r\n\t\"'"));
    w.element("empty", String(""));
    w.element("absent", String());
    w.element("n", std::optional<int32_t>());
    w.element("f", 1.0f);
    for (int i = 0; i < 9; i++) w.startElement("d");
    for (int i = 0; i < 9; i++) w.endElement();
    w.endElement();
    w.endDocument();
    std::string expected =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<a q=\"x&quot;&lt;&amp;&gt;&#10;&#13;\t'\">\n"
        "    <t>a&lt;b&gt;&amp;&#13;\n\t\"'</t>\n"
        "    <empty></empty>\n"
        "    <f>1.0</f>\n"
        "    <d>\n"
        "        <d>\n"
        "            <d>\n"
        "                <d>\n"
        "                    <d>\n"
        "                        <d>\n"
        "                            <d>\n"
        "<d>\n"  // depth 8: JAXB RI's printIndent wraps around (depth % 8)
        "    <d/>\n"
        "</d>\n"
        "                            </d>\n"
        "                        </d>\n"
        "                    </d>\n"
        "                </d>\n"
        "            </d>\n"
        "        </d>\n"
        "    </d>\n"
        "</a>\n";
    JCHECK_EQ(w.output(), expected);
}

// Throughput of the Element API on the real spawn data (all 145 files), as a smoke benchmark.
JTEST(XmlJaxbAllSpawnFiles) {
    auto* un = xml::JAXBContext::newInstance<t_spawn::SpawnsData>()->createUnmarshaller();
    std::vector<std::string> files;
    std::string dir = repoFile("gameserver/data/static_data/spawns");
    for (const char* sub : {"Gather", "Instances", "Monsters", "Npcs", "Rifts", "StaticObjects"}) {
        std::string d = dir + "/" + sub;
        for (auto& e : std::filesystem::directory_iterator(d)) {
            if (e.is_regular_file() && e.path().extension() == ".xml") files.push_back(e.path().string());
        }
    }
    auto t0 = std::chrono::steady_clock::now();
    int64_t groups = 0;
    for (auto& f : files) {
        auto* d = cast<t_spawn::SpawnsData>(un->unmarshal(String(f)));
        if (d->spawnGroups != nullptr) groups += d->spawnGroups->size();  // some files are empty
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "  %zu spawn files, %lld spawn groups in %lld ms\n", files.size(), static_cast<long long>(groups),
                 static_cast<long long>(ms));
    JCHECK(files.size() >= 140);
    JCHECK_EQ(groups, INT64_C(10125));  // <spawn> elements outside comments (checked with Python ElementTree)
}
