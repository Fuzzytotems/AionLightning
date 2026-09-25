# JAXB in the C++ port: runtime API and generator contract

The gameserver loads about 82 MB of static data (317 XML files) through JAXB 2 annotations,
the admin commands `SaveSpawnData` and `Ring` marshal data back to XML, and `XmlMerger` /
`IPConfig` use StAX and SAX directly. In the port all of this goes through `jlang::xml`
(`<jlang/Xml.h>`), implemented over pugixml in `jlang/src/xml_*.cpp`:

| Header | Content |
|---|---|
| `jlang/Jaxb.h` | JAXB runtime for generated binders (`Element`, `JaxbContext`, `JaxbScope`, `Writer`, lexical rules), the `JAXBContext` / `Unmarshaller` / `Marshaller` facades, `XmlAdapter`, `Schema` / `SchemaFactory` (validation is a no-op), `XMLConstants` |
| `jlang/XmlEvents.h` | StAX (`XMLInputFactory`, `XMLEventReader`, `XMLEventWriter`, events, `QName`) and SAX (`SAXParserFactory`, `SAXParser`, `DefaultHandler`, `Attributes`, `Locator`, exceptions) |
| `jlang/Xml.h` | includes both; this is what translated code includes (tools/cppgen/jdkmap.tsv) |

Nothing reflects over annotations at run time. For every JAXB-bound class, `tools/cppgen`
emits a small binder (members of the class) that the runtime drives. This document is the
contract between the generator and the runtime. Everything in it is exercised by
`jlang/tests/test_xml_jaxb.cpp`, whose hand-written binders are written exactly as the
generator must emit them, and checked against the output of the real JAXB RI 2.3.9 on the
same inputs (`jlang/tests/data/xml/java/JaxbFixtures.java`).

## 1. Behaviors that must be preserved (verified with JAXB RI 2.3.9)

* **Document order.** Beans are constructed at their start tag, in document (pre-)order. This
  matters: `StatModifier` numbers itself from a static counter in its constructor and a
  `TreeSet` orders modifiers by that number.
* **afterUnmarshal is post-order.** It runs once per bean, at the end of the bean's element,
  after all its children (so children before parents; the root's last). Collections and
  arrays of the bean are complete at that point. The `parent` argument is the enclosing bean
  (null for the root). JAXB finds the method by reflection on the class and its superclasses
  and calls it virtually: the generated code calls `afterUnmarshal` once, in the most-derived
  `_jaxbUnmarshal`, if the class or a superclass declares it.
* **Unknown attributes and elements are ignored,** including their whole subtree
  (the data has 28,615 `<bound_radius>` and more with no mapping).
* **Absent attributes/elements leave the Java field initializer value** (XSD `default=` values
  are never applied). Absent collections stay null.
* **Collections:** on the first child of a collection property within an element, a null field
  gets a new collection and a pre-filled one is cleared (`startPacking`); children of the
  property are appended in document order even when elements of other properties are
  interleaved. A repeated single-valued element: last one wins (each occurrence is
  constructed).
* **Arrays** (`long[] exp`, `Item[] items`) are built at the end of the element and replace any
  initializer value; untouched when there are no such children.
* **`@XmlList` element / `List<X>` attribute:** whitespace-separated tokens, each occurrence
  replaces the list (`<classes>A B</classes><classes>C</classes>` gives `[C]`); `""` gives an
  empty list; unknown enum tokens become null elements.
* **`@XmlID`:** the value is trimmed (XML whitespace) before it is stored in the field and
  registered. One ID table per document; a later ID replaces an earlier one.
* **`@XmlIDREF`, single value:** trimmed; resolved immediately if the ID is already known
  (backward reference, not affected by a later duplicate), otherwise at the end of the
  document against the final table (forward reference). Unresolved: stays null.
* **`@XmlIDREF`, collection/array:** item texts are NOT trimmed; all items of the property are
  resolved at the end of the document, after every `afterUnmarshal` (including the root's),
  against the final table; unresolved items are dropped. So an `XmlAdapter` that receives such
  an object (`NpcEquipmentList`) sees `items == null`.
* **`XmlAdapter`:** the value type (`NpcEquipmentList`) is unmarshalled completely (its own
  `afterUnmarshal` included; its `parent` is the bean that holds the adapted property) and then
  passed to `adapter.unmarshal`; the result is stored in the field.
* **`@XmlElements` polymorphism:** the element name selects the class. No `xsi:type`.
* **Property precedence:** when two fields map the same attribute name (`NpcTemplate`:
  `aggrorange` and `attackRate` both `srange`), the first declared wins; a subclass field that
  shadows a superclass property (`FpAttackEffect.percent`) does not receive the value, the
  superclass field does. The generator must reproduce what the JAXB runtime model says (the
  survey dump `jaxb_model.txt`), not "fix" it.
* **Lexical rules** (`DatatypeConverterImpl`), see §4.
* **Exceptions:** a `NumberFormatException` from a lexical conversion (or anything thrown by
  `afterUnmarshal`) propagates out of `unmarshal` unchanged; IO and XML syntax errors become
  `UnmarshalException` with the cause linked.

## 2. The generated members

For every class bound by JAXB (reachable from a context root; the generator can use the JAXB
runtime model), emit these **public** members (`xml` = `jlang::xml`):

```cpp
// C.h
class C : public Base {                           // or public virtual jlang::Object
public:
    ...
    // ---- JAXB (generated)
    static constexpr const char* _jaxbRootName = "npc_templates";   // only for @XmlRootElement
    void _jaxbUnmarshal(jlang::xml::Element* e, jlang::xml::JaxbContext* ctx);
    bool _jaxbAttribute(std::string_view name, std::string_view value, jlang::xml::JaxbContext* ctx);
    bool _jaxbElement(jlang::xml::Element* child, jlang::xml::JaxbContext* ctx);
    void _jaxbMarshal(jlang::xml::Writer* w);                           // only for marshalled classes
};
```

Abstract classes need `_jaxbAttribute` / `_jaxbElement` (subclasses delegate to them) but no
`_jaxbUnmarshal`. `@XmlRootElement` is not inherited in Java: a subclass of a root class that
is not itself annotated gets `static constexpr const char* _jaxbRootName = nullptr;` (the C++
member would otherwise be inherited). If the Java no-arg constructor is private, add
`friend struct jlang::xml::JaxbAccess;` (the runtime creates beans with
`JaxbAccess::create<T>()`, i.e. `new T()`).

### 2.1 `_jaxbUnmarshal`: always the same body

```cpp
void C::_jaxbUnmarshal(jlang::xml::Element* e, jlang::xml::JaxbContext* ctx) {
    jlang::xml::JaxbScope scope(ctx, this);
    for (const jlang::xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx);
    // only for a class with an @XmlValue property:  value = jlang::xml::parseString(e->text());
    for (jlang::xml::Element* c : e->children()) _jaxbElement(c, ctx);
    scope.finish();                                               // builds array properties
    afterUnmarshal(ctx->getUnmarshaller(), scope.parent());       // only if C or a base declares it
}
```

`afterUnmarshal(Unmarshaller u, Object parent)` translates to
`void afterUnmarshal(jlang::xml::Unmarshaller* u, jlang::Object* parent)` (virtual unless
private, per CONVENTIONS). Hand-written reload code keeps calling `afterUnmarshal(nullptr, nullptr)`.

### 2.2 `_jaxbAttribute` / `_jaxbElement`: dispatch by name, then the superclass

```cpp
bool SpawnGroup::_jaxbAttribute(std::string_view n, std::string_view v, jlang::xml::JaxbContext* ctx) {
    if (n == "time") { spawnTime = jlang::xml::parseEnum<SpawnTime>(v); return true; }
    if (n == "anchor") { anchor = jlang::xml::parseString(v); return true; }
    if (n == "interval") { interval = jlang::xml::parseInt(v); return true; }
    if (n == "boss") { boss = jlang::xml::parseBoolean(v); return true; }
    ...
    return Base::_jaxbAttribute(n, v, ctx);      // or `return false;` without a bound superclass
}
bool SpawnGroup::_jaxbElement(jlang::xml::Element* c, jlang::xml::JaxbContext* ctx) {
    std::string_view n = c->name();
    if (n == "object") { ctx->collection(objects)->add(ctx->unmarshal<SpawnTemplate>(c)); return true; }
    return Base::_jaxbElement(c, ctx);           // or `return false;`
}
```

For classes with many names, a `switch` on `jlang::xml::nameHash(n)` (constexpr FNV-1a) with
`case jlang::xml::nameHash("npc_id"): if (n == "npc_id") {...} break;` is faster; a hash
collision between two names of one class is a compile error (duplicate case), in which case
fall back to `if` chains for that class.

Unknown names return false and are ignored. Never add an `else throw`.

### 2.3 Property kinds → generated statement

Values: `v` is the attribute value, `c->text()` the text of a simple-typed child element.
`X` is the lexical parse from §4 for the Java type (`parseInt`, `parseLong`, `parseShort`,
`parseByte`, `parseFloat`, `parseDouble`, `parseBoolean`, `parseString`, `parseFile`,
`parseEnum<E>`); boxed types (`Integer` → `std::optional<int32_t>`) use the same function
(`field = jlang::xml::parseInt(v);`), except `Boolean`, which uses `parseBooleanOrNull`
(a malformed boolean makes a `Boolean` null and a `boolean` false).

| Java property | Generated code |
|---|---|
| `@XmlAttribute T f` | `f = X(v);` |
| `@XmlAttribute List<T> f` (attribute list) | `f = jlang::xml::parseList<T'>(v, X);` |
| `@XmlID @XmlAttribute String id` | `id = ctx->id(v, this);` |
| `@XmlID` on a setter `setXmlUid(String)` | `setXmlUid(ctx->id(v, this));` |
| `@XmlIDREF @XmlAttribute T f` | `ctx->idref(v, [this](jlang::Object* o) { f = jlang::cast<T>(o); });` |
| `@XmlElement T f` (simple type) | `f = X(c->text());` |
| `@XmlElement T f` (bean) | `f = ctx->unmarshal<T>(c);` |
| `@XmlElement List<T> f` (simple) | `ctx->collection(f)->add(X(c->text()));` |
| `@XmlElement List<T> f` (bean), also `Set`, `TreeSet` | `ctx->collection(f)->add(ctx->unmarshal<T>(c));` |
| `@XmlElement t[] f` (`long[] exp`, `Foo[]`) | `ctx->arrayAdd(f, X(c->text()));` / `ctx->arrayAdd(f, ctx->unmarshal<Foo>(c));` |
| `@XmlElement @XmlList List<T> f` | `f = jlang::xml::parseList<T'>(c->text(), X);` |
| `@XmlElement @XmlIDREF List<T>` / `T[]` | `ctx->idrefItem(f, c->text(), [this](std::vector<jlang::Object*>& v) { jlang::xml::toCollection(f, v); });` / `... { f = jlang::xml::toArray<T*>(v); }` |
| `@XmlElements({@XmlElement(name, type)...}) Collection<B> f` | see §2.4 |
| `@XmlValue String value` | in `_jaxbUnmarshal`: `value = jlang::xml::parseString(e->text());` |
| `@XmlJavaTypeAdapter(A.class)` on the bound class `B` (value type `V`) | `f = ctx->adapter<A>()->unmarshal(ctx->unmarshal<V>(c));` (the typed overload `B* unmarshal(V*)` the generator declares in `A`) |
| implicit field of a FIELD-access class | as `@XmlElement` named after the field (skip the Trove/Map caches) |

For `parseList` / `parseEnum` in lambdas: `[](std::string_view t) { return jlang::xml::parseEnum<Kind>(t); }`.
`T'` is the mapped element type (`int32_t`, `PlayerClass`, ...).

The generated `ctx->collection(f)->add(ctx->unmarshal<T>(c))` relies on C++17 sequencing
(the object expression is evaluated before the argument): `collection()` runs first. JAXB
creates/clears the collection when the first item is received; the difference is not
observable (the child's `afterUnmarshal` never looks at the parent's list).

### 2.4 `@XmlElements` (polymorphic properties)

```cpp
bool Effects::_jaxbElement(jlang::xml::Element* c, jlang::xml::JaxbContext* ctx) {
    static const jlang::xml::ElementFactories<EffectTemplate> choices{
        {"root", jlang::xml::makeBean<EffectTemplate, RootEffect>},
        {"spellatk", jlang::xml::makeBean<EffectTemplate, SpellAttackEffect>},
        // ... one line per @XmlElement(name, type)
    };
    if (auto make = choices.find(c->name())) { ctx->collection(effects)->add(make(c, ctx)); return true; }
    return false;
}
```

The table is a function-local static (built on first use, thread-safe). A single-valued
polymorphic property stores `make(c, ctx)` instead. A `TreeSet` property works the same way
(`ModifiersTemplate`): the item is added after it is fully unmarshalled.

### 2.5 Marshalling (`SaveSpawnData`, `Ring`)

Emit `_jaxbMarshal` for the classes reachable from `SpawnsData` and `FlyRingData` (or for all
bound classes). It writes the bean's attributes and then its child elements, in the order of
the JAXB runtime model (declaration order, superclass first; `propOrder` for elements when
given). The caller writes the element's own tags:

```cpp
void SpawnGroup::_jaxbMarshal(jlang::xml::Writer* w) {
    w->attribute("time", spawnTime);        // null enum / String / std::optional: omitted
    w->attribute("interval", interval);     // primitives always written
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
void SpawnTemplate::_jaxbMarshal(jlang::xml::Writer* w) {
    w->attribute("h", heading);
    w->attribute("x", x);                   // float: Java Float.toString ("2789.0", "1.0E-5")
    w->element("spawnId", spawnId);         // simple-typed element (implicit FIELD property)
}
```

With a bound superclass, split it: `_jaxbMarshalAttributes(w)` and `_jaxbMarshalElements(w)`,
each calling the superclass's first, and `_jaxbMarshal(w)` calling both (all attributes must
precede the first child). Polymorphic properties pick the element name from the dynamic class
(`dynamic_cast` chain in `@XmlElements` order). Enums are written with `toXml()` when they have
it (`@XmlEnumValue`), else `name()`. Adapted properties call `adapter->marshal(v)`.

## 3. Runtime API summary

* `JaxbContext` (per `unmarshal` call): `unmarshal<T>(Element*)`, `collection(field)`,
  `arrayAdd(field, value)`, `id(lexical, bean)`, `registerId`, `findId`, `idref(lexical, setter)`,
  `idrefItem(field, lexical, store)`, `adapter<A>()`, `getUnmarshaller()`, `addPatcher(fn)`.
* `JaxbScope(ctx, this)`: `parent()`, `finish()`.
* `Element`: `name()`, `attributes()`, `children()`, `text()` (character data of the element:
  text and CDATA concatenated, comments skipped, never trimmed), plus `attribute(name)`
  (`std::optional<std::string_view>`), `children(name)`, `child(name)` for hand-written code.
  Generated code only iterates attributes and children once, in order, and reads `text()`:
  a streaming parser could replace the DOM without regenerating anything.
  Views are valid only during the `_jaxbUnmarshal` call.
* `Writer`: `startElement`, `attribute(name, value)` (overloads for all mapped types; null
  `String`/`std::optional`/enum omitted), `text`, `element(name, value)`, `endElement`.
* Helpers: `toArray<T*>(objs)`, `toCollection(field, objs)`, `ElementFactories<B>`,
  `makeBean<B, T>`, `jaxbType<T>()`.

Transient helpers (`Element`, `Attribute`, `JaxbContext`, `JaxbScope`, `Writer`,
`ElementFactories`) are not Java objects and do not derive from `jlang::Object`.

## 4. Lexical rules (JAXB RI 2.3.9, `DatatypeConverterImpl`)

| Type | Rule | Examples (verified) |
|---|---|---|
| `int`/`Integer` | every XML whitespace char anywhere is skipped, `+` ignored, `-` anywhere negates, 32-bit wrap; any other char: `NumberFormatException("Not a number: <s>")` | `""`→0, `" 4 2 "`→42, `"5-"`→-5, `"2147483648"`→MIN_VALUE, `"1.5"`→NFE |
| `short`, `byte` | `(short)`/`(byte)` of the int rule | `"300"`→44 |
| `long` | trimmed, one optional leading `+`, then strict `Long.parseLong` | `" +5 "`→5, `"1 2"`, `""`, `"++5"`→NFE |
| `float`, `double` | trimmed; `NaN`, `INF`, `-INF`; else first and last char must be digit/`+`/`-`/`.`, then `Float.parseFloat` (decimal or `0x1p3`, correctly rounded) | `".5"`, `"1."` ok; `"1.5f"`, `"Infinity"`, `""` → NFE; `"291.3283 "`→291.3283 |
| `boolean` | `true`/`1` → true, `false`/`0` → false, surrounding whitespace allowed; anything else null → `Boolean` null, `boolean` **false**; lone `"t"`/`"f"` throw `StringIndexOutOfBoundsException` | `"yes"`, `"TRUE"`, `"10"`, `"truex"` → null; `"  "` → false |
| `String` | as is, never trimmed | |
| `java.io.File` (ScriptInfo) | `new File(trimmed)`: `jlang::xml::parseFile` | |
| enum | exact match on `name()` (or `@XmlEnumValue`), **not trimmed**; unknown → null, silently | `" B "` → null |
| `@XmlID` / single `@XmlIDREF` | trimmed | |
| collection `@XmlIDREF` items | not trimmed | `<item> 101</item>` does not resolve |

Real data relies on these: `loc_y="291.3283 "` (npc_walker.xml), 1,537 unknown NPC `race`
values that load as null, `TRANSFORM_RESISTANCE` → null `StatEnum`.

## 5. Facades used by hand-translated code

```cpp
// XmlDataLoader
jlang::xml::JAXBContext* jc = jlang::xml::JAXBContext::newInstance<StaticData>();
jlang::xml::Unmarshaller* un = jc->createUnmarshaller();
un->setSchema(getSchema());                           // kept, no validation
return jlang::cast<StaticData>(un->unmarshal(new jlang::File(CACHE_XML_FILE)));

// getSchema()
jlang::xml::SchemaFactory* sf = jlang::xml::SchemaFactory::newInstance(jlang::xml::XMLConstants::W3C_XML_SCHEMA_NS_URI);
schema = sf->newSchema(new jlang::File(XML_SCHEMA_FILE));   // catch (jlang::xml::SAXException& e) stays

// Reload.java: the context knows StaticData only; bind the holder file by its type:
NpcData* data = un->unmarshal<NpcData>(npcXml);       // Java: (NpcData) un.unmarshal(npcXml)

// ScriptManager: several classes, the root element name picks one
jlang::xml::JAXBContext* c = jlang::xml::JAXBContext::newInstance<ScriptInfo, ScriptList>();
ScriptList* list = jlang::cast<ScriptList>(c->createUnmarshaller()->unmarshal(scriptDescriptor));

// SaveSpawnData / Ring
jlang::xml::Marshaller* marshaller = jc->createMarshaller();
marshaller->setSchema(schema);
marshaller->setProperty(jlang::xml::Marshaller::JAXB_FORMATTED_OUTPUT, jlang::box(true));
marshaller->marshal(data, xml);                        // catch (jlang::xml::JAXBException& e)
```

* `unmarshal(File*)` picks the class whose `_jaxbRootName` equals the document's root element
  among the classes given to `newInstance`; no match → `UnmarshalException("unexpected
  element ...")`. The JVM context also knows every class reachable from its roots; this one
  does not, hence the typed `unmarshal<T>` for `Reload`. There is no global root registry, and
  none is needed: `item_templates` and `bind_points` are not unique root names anyway.
* Also `unmarshal(InputStream*)`, `unmarshal(Reader*)`, `unmarshal(const String& path)`,
  `unmarshalString(xml)` (tests), and typed variants of each.
* `Marshaller`: UTF-8 only. Output is byte-identical to the JAXB RI (checked on two real spawn
  files, formatted and not): `<?xml version="1.0" encoding="UTF-8" standalone="yes"?>`,
  4-space indentation (`depth % 8`, as the RI), `<x/>` for empty elements, escaping by the RI's
  `MinimumEscapeHandler`. Properties: `JAXB_FORMATTED_OUTPUT`, `JAXB_FRAGMENT`,
  `JAXB_ENCODING` (stored, UTF-8 is always written), `JAXB_SCHEMA_LOCATION`,
  `JAXB_NO_NAMESPACE_SCHEMA_LOCATION`; others throw `PropertyException`.
* `JAXBContext::generateSchema` (SchemaGen dev tool) throws `UnsupportedOperationException`.

## 6. XmlMerger (StAX) and SAX

The StAX emulation reproduces the JDK's event sequence and `XMLEventWriter` output well
enough that a line-by-line translation of `XmlMerger` produces **byte-identical** output to the
Java merger on the real data (85,018,961 bytes, 314 imported files) and on the synthetic tree
in `jlang/tests/data/xml/merge`. The non-obvious parts: every entity/character reference is a
separate `Characters` event (so `&lt; &gt;` loses its space in the merged file, as in Java),
CDATA sections are reported as `CHARACTERS`, `StartElement.getAttributes()` iterates in
`java.util.HashMap` order (attribute order changes in the merged file), imported roots lose
their `xmlns:` declarations, empty elements are written `<a></a>`, and the declaration is
written as `<?xml version="1.0"?>` (a Reader-based reader reports no encoding). See the header
comment of `<jlang/XmlEvents.h>` for the full list and the two JDK behaviors that are not
emulated (text split at 8K buffer boundaries and before a lone `\r`; the data has neither
case).

Directory imports (`FileUtils.listFiles`) use `readdir` order in Java, which differs between
file systems; the C++ port of `listFiles` should do the same or sort, and document it.

SAX (`TimeCheckerHandler`, `IPConfig`) follows the JDK's non-namespace-aware parser:
`startElement("", "", qName, attributes)` with `xmlns*` attributes included, locator positions
just after each start tag's `>`. `DefaultHandler::startElement/endElement` exist with both
`const jlang::String&` and by-value `jlang::String` parameters, so a generated override with
either convention is called.

## 7. Performance

Measured on this machine (CPU time; the JVM figures are from the survey, JDK 21 + JAXB RI 2.3.9):

| Workload | C++ (jlang::xml) | JVM |
|---|---|---|
| Unmarshal the merged 85 MB `static_data.xml` (1.05M beans, 4.28M attributes, generic test binder) | 0.8 s user + 0.55 s sys, peak RSS 591 MB (object graph 250 MB + transient pugixml tree ~320 MB) | 3.3-5.4 s, ~120 MB retained |
| 145 spawn files, 6.1 MB, 10,125 groups (test binders) | ~0.1 s | |
| Full `XmlMerger` run (StAX, 314 files, 85 MB out) through FileReader/BufferedWriter | 2.9 s user + 0.5 s sys, peak RSS 160 MB, byte-identical output | 3.0 s wall, 4.6 s CPU |

pugixml parses the whole document first (JAXB: in place, no copy), so the peak memory of the
static-data load is the tree of the merged file, freed when `unmarshal` returns. The generated
binders only need the streaming-compatible subset of `Element`, so a streaming tokenizer can
replace the DOM later without regenerating anything if that peak matters.
