// jlang/Jaxb.h - JAXB (javax.xml.bind) runtime support for GENERATED binders, plus the
// javax.xml.validation / javax.xml.XMLConstants facades.
//
// Translated code includes <jlang/Xml.h> (which includes this header). The full contract for
// the code generator is docs/cpp-port/JAXB.md; this comment is the short version.
//
// ======================================================================================
// 1. Java facade (translated 1:1)
//
//   JAXBContext jc = JAXBContext.newInstance(StaticData.class);
//   Unmarshaller un = jc.createUnmarshaller();
//   un.setSchema(schema);                                   // no-op (no validation)
//   StaticData sd = (StaticData) un.unmarshal(file);
// ->
//   jlang::xml::JAXBContext* jc = jlang::xml::JAXBContext::newInstance<StaticData>();
//   jlang::xml::Unmarshaller* un = jc->createUnmarshaller();
//   un->setSchema(schema);
//   StaticData* sd = jlang::cast<StaticData>(un->unmarshal(file));
//
// unmarshal(File*) picks the class whose @XmlRootElement name (generated _jaxbRootName) equals
// the document's root element among the classes given to newInstance<...>(). The JVM context
// also knows every class reachable from them; this one does not, so a document whose root is
// a nested holder (Reload.java: `(NpcData) un.unmarshal(npcXml)` with a StaticData context)
// is unmarshalled with the typed overload, which binds the root as T whatever its name:
//   NpcData* data = un->unmarshal<NpcData>(npcXml);
//
// ======================================================================================
// 2. Generated binder members (per JAXB-bound class C; all public):
//
//   void _jaxbUnmarshal(jlang::xml::Element* e, jlang::xml::JaxbContext* ctx);
//   bool _jaxbAttribute(std::string_view name, std::string_view value, jlang::xml::JaxbContext* ctx);
//   bool _jaxbElement(jlang::xml::Element* child, jlang::xml::JaxbContext* ctx);
//   static constexpr const char* _jaxbRootName = "npc_templates";   // @XmlRootElement only
//   void _jaxbMarshal(jlang::xml::Writer* w);                          // marshalled classes only
//
// with the standard body
//
//   void C::_jaxbUnmarshal(jlang::xml::Element* e, jlang::xml::JaxbContext* ctx) {
//       jlang::xml::JaxbScope scope(ctx, this);
//       for (const jlang::xml::Attribute& a : e->attributes()) _jaxbAttribute(a.name(), a.value(), ctx);
//       for (jlang::xml::Element* c : e->children()) _jaxbElement(c, ctx);
//       scope.finish();
//       afterUnmarshal(ctx->getUnmarshaller(), scope.parent());   // if C or a base declares it
//   }
//
// _jaxbAttribute/_jaxbElement handle C's own properties and delegate the rest to the
// superclass (`return Base::_jaxbElement(c, ctx);`), or return false: unknown attributes and
// elements are ignored, as JAXB does with its default event handler. Children are processed
// in document order, so objects are constructed in document (pre-)order and afterUnmarshal
// runs post-order (children first, at the end of each element), exactly like the JAXB RI.
//
// ======================================================================================
// 3. Non-Object helper types
//
// Element, Attribute, JaxbContext, JaxbScope, Writer and ElementFactories are transient
// helpers that live on the C++ stack (or in static storage) during one unmarshal/marshal call.
// They are not Java objects, so they do not derive from jlang::Object. Element and Attribute
// are views into the parsed document: never keep them (or the string_views they return)
// beyond the _jaxbUnmarshal call that received them.
#pragma once

#include <jlang/jlang.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

// pugixml node types (opaque here; only jlang/src/xml*.cpp include <pugixml.hpp>).
namespace pugi {
struct xml_node_struct;
struct xml_attribute_struct;
}  // namespace pugi

namespace jlang::xml {

class Element;
class ElementIterator;
struct ElementRange;
class JaxbContext;
class Writer;
class Unmarshaller;
class Marshaller;
class JAXBContext;
class Schema;

namespace detail {
class XmlDoc;  // a parsed document (pugixml), defined in xml_dom.cpp
[[noreturn]] void throwNumberFormat(std::string_view prefix, std::string_view lexical);
[[noreturn]] void throwNumberFormatNull();
pugi::xml_node_struct* firstChildElement(pugi::xml_node_struct* n) noexcept;
pugi::xml_node_struct* nextSiblingElement(pugi::xml_node_struct* n) noexcept;
std::string_view nodeName(pugi::xml_node_struct* n) noexcept;
pugi::xml_attribute_struct* firstAttribute(pugi::xml_node_struct* n) noexcept;
pugi::xml_attribute_struct* nextAttribute(pugi::xml_attribute_struct* a) noexcept;
std::string_view attributeName(pugi::xml_attribute_struct* a) noexcept;
std::string_view attributeValue(pugi::xml_attribute_struct* a) noexcept;
}  // namespace detail

// =======================================================================================
// JAXB exceptions (javax.xml.bind.JAXBException and subclasses).
//
// The Java "linked exception" is the cause: getLinkedException() == getCause().
class JAXBException : public ::jlang::Exception {
public:
    JAXBException() {}
    explicit JAXBException(const String& message) : Exception(message) {}
    explicit JAXBException(const char* message) : Exception(String(message)) {}
    JAXBException(const String& message, const String& errorCode) : Exception(message), errorCode_(errorCode) {}
    JAXBException(const String& message, const Throwable& cause) : Exception(message, cause) {}
    JAXBException(const String& message, Throwable* cause) : Exception(message, cause) {}
    JAXBException(const String& message, const String& errorCode, Throwable* cause)
        : Exception(message, cause), errorCode_(errorCode) {}
    // Java `new JAXBException(Throwable)`: message stays null, the cause is linked.
    explicit JAXBException(Throwable* cause) : Exception(String(), cause) {}
    template<class E>
        requires(std::is_base_of_v<Throwable, E> && !std::is_base_of_v<JAXBException, E>)
    explicit JAXBException(const E& cause) : Exception(String(), static_cast<const Throwable&>(cause)) {}

    String getErrorCode() { return *errorCode_; }
    Throwable* getLinkedException() { return getCause(); }
    void setLinkedException(Throwable* e) { initCause(e); }
    // "javax.xml.bind.X: msg" plus "\n - with linked exception:\n[cause]" like the JAXB API.
    String toString() override;
    String className() const override { return "javax.xml.bind.JAXBException"; }
    JLANG_THROWABLE(JAXBException)

private:
    ::jlang::detail::Pinned<String> errorCode_;
};

#define JLANG_XML_JAXB_EXCEPTION(Name, JavaName)                                              \
    class Name : public JAXBException {                                                       \
    public:                                                                                   \
        Name() {}                                                                             \
        explicit Name(const String& message) : JAXBException(message) {}                      \
        explicit Name(const char* message) : JAXBException(String(message)) {}                \
        Name(const String& message, const String& errorCode) : JAXBException(message, errorCode) {} \
        Name(const String& message, const Throwable& cause) : JAXBException(message, cause) {} \
        Name(const String& message, Throwable* cause) : JAXBException(message, cause) {}      \
        explicit Name(Throwable* cause) : JAXBException(cause) {}                             \
        template<class E>                                                                     \
            requires(std::is_base_of_v<Throwable, E> && !std::is_base_of_v<JAXBException, E>) \
        explicit Name(const E& cause) : JAXBException(cause) {}                               \
        String className() const override { return JavaName; }                                \
        JLANG_THROWABLE(Name)                                                                 \
    }

JLANG_XML_JAXB_EXCEPTION(UnmarshalException, "javax.xml.bind.UnmarshalException");
JLANG_XML_JAXB_EXCEPTION(MarshalException, "javax.xml.bind.MarshalException");
JLANG_XML_JAXB_EXCEPTION(PropertyException, "javax.xml.bind.PropertyException");
#undef JLANG_XML_JAXB_EXCEPTION

// =======================================================================================
// Lexical conversions with the exact JAXB RI 2.3 rules (com.sun.xml.bind.DatatypeConverterImpl),
// verified against jaxb-impl 2.3.9. Inputs are the raw attribute value / element text.
//
// XML whitespace is ' ', '\t', '\n', '\r' (WhiteSpaceProcessor).
constexpr bool isXmlWhitespace(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
// WhiteSpaceProcessor.trim.
constexpr std::string_view trimXml(std::string_view s) noexcept {
    size_t b = 0, e = s.size();
    while (b < e && isXmlWhitespace(s[b])) b++;
    while (e > b && isXmlWhitespace(s[e - 1])) e--;
    return s.substr(b, e - b);
}

// int/Integer (_parseInt): every whitespace character anywhere is skipped, '+' is ignored and
// '-' anywhere negates, digits accumulate with 32-bit wrap-around; "" and "  " give 0; any
// other character throws NumberFormatException("Not a number: <lexical>").
int32_t parseInt(std::string_view lexical);
// short/byte (_parseShort/_parseByte): (short)/(byte) of parseInt (wraps: "300" -> 44).
int16_t parseShort(std::string_view lexical);
int8_t parseByte(std::string_view lexical);
// long (_parseLong): trimmed, one optional leading '+', then Long.parseLong (strict: no inner
// whitespace, overflow and "" throw NumberFormatException).
int64_t parseLong(std::string_view lexical);
// float/double (_parseFloat/_parseDouble): trimmed; "NaN", "INF", "-INF"; otherwise the first
// and last characters must be a digit, '+', '-' or '.', then Float/Double.parseFloat (decimal
// or hex "0x1p3" forms, correctly rounded; "1.5f", "Infinity", "", "1e" throw).
float parseFloat(std::string_view lexical);
double parseDouble(std::string_view lexical);
// Boolean (_parseBoolean): leading/trailing whitespace skipped; "true"/"1" -> true,
// "false"/"0" -> false; anything else -> null (std::nullopt), including "TRUE", "yes", "10";
// a lone "t" or "f" throws StringIndexOutOfBoundsException, as the RI does. "" -> null,
// whitespace only -> false.
std::optional<bool> parseBooleanOrNull(std::string_view lexical);
// Primitive boolean property: a null parse result stores false (verified: bp="yes" with a
// field initializer `= true` loads false).
inline bool parseBoolean(std::string_view lexical) { return parseBooleanOrNull(lexical).value_or(false); }
// String: the lexical value as is (never trimmed).
inline String parseString(std::string_view lexical) { return String(lexical); }
// java.io.File (ScriptInfo.root, libraries): new File(trimmed lexical).
File* parseFile(std::string_view lexical);

// Enums bound by name (@XmlEnum) or by @XmlEnumValue (enums that have toXml()): exact match,
// NOT trimmed; unknown lexicals give the enum's null value, silently.
// E must be a jlang enum value class (values(), name() [, toXml()]).
template<class E>
E parseEnum(std::string_view lexical);
// Same with an explicit lookup function (returns the null E for unknown input, or throws
// IllegalArgumentException like E::valueOf; both are mapped to the null E).
template<class E, class F>
E parseEnum(std::string_view lexical, F lookup);

// Whitespace-separated lists (@XmlList, and List<X> @XmlAttribute): splits on runs of XML
// whitespace, skips empty tokens, parses each token with parseItem and returns a new list
// ("" -> empty list). Unknown enum tokens become null elements, as in JAXB.
template<class T, class F>
List<T>* parseList(std::string_view lexical, F parseItem);

// Printing for marshalling (DatatypeConverterImpl._printX).
String printFloat(float v);    // "NaN", "INF", "-INF", else Float.toString
String printDouble(double v);  // "NaN", "INF", "-INF", else Double.toString

// Compile-time name hash (FNV-1a) for generated `switch` dispatch on element/attribute names:
//   switch (jlang::xml::nameHash(name)) { case jlang::xml::nameHash("npc_id"): if (name == "npc_id") ... }
constexpr uint32_t nameHash(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

// =======================================================================================
// Attribute: one attribute of an Element (name and value views into the parsed document).
class Attribute {
public:
    Attribute() noexcept = default;
    std::string_view name() const noexcept { return name_; }
    std::string_view value() const noexcept { return value_; }

private:
    friend class Element;
    friend class AttributeIterator;
    explicit Attribute(pugi::xml_attribute_struct* a) noexcept : a_(a) { load(); }
    void load() noexcept {
        if (a_ != nullptr) {
            name_ = detail::attributeName(a_);
            value_ = detail::attributeValue(a_);
        }
    }
    pugi::xml_attribute_struct* a_ = nullptr;
    std::string_view name_;
    std::string_view value_;
};

class AttributeIterator {
public:
    using value_type = Attribute;
    using difference_type = std::ptrdiff_t;
    AttributeIterator() noexcept = default;
    explicit AttributeIterator(pugi::xml_attribute_struct* a) noexcept : cur_(a) {}
    const Attribute& operator*() const noexcept { return cur_; }
    const Attribute* operator->() const noexcept { return &cur_; }
    AttributeIterator& operator++() noexcept {
        cur_ = Attribute(detail::nextAttribute(cur_.a_));
        return *this;
    }
    bool operator==(const AttributeIterator& o) const noexcept { return cur_.a_ == o.cur_.a_; }
    bool operator!=(const AttributeIterator& o) const noexcept { return cur_.a_ != o.cur_.a_; }

private:
    Attribute cur_;
};

struct AttributeRange {
    AttributeIterator first;
    AttributeIterator begin() const noexcept { return first; }
    AttributeIterator end() const noexcept { return AttributeIterator(); }
};

// =======================================================================================
// Element: an element of a parsed document.
//
//   e->name()                         element name (as written, "prefix:local" if prefixed)
//   e->attribute("id")                std::optional<std::string_view>: nullopt if absent
//   for (const Attribute& a : e->attributes())       document order
//   for (Element* c : e->children())                 child elements, document order
//   for (Element* c : e->children("item"))           only those named "item"
//   e->text()                         character data directly inside e (text and CDATA
//                                     sections concatenated, comments skipped), untrimmed; for
//                                     simple-typed elements and @XmlValue
//
// Generated binders only use attributes(), children() (iterated once, in order) and text(),
// which a streaming parser can provide as well; attribute(name)/children(name) are for
// hand-written code.
class Element {
public:
    Element() noexcept = default;
    Element(pugi::xml_node_struct* n, detail::XmlDoc* doc) noexcept : node_(n), doc_(doc) {
        if (n != nullptr) name_ = detail::nodeName(n);
    }

    std::string_view name() const noexcept { return name_; }
    std::optional<std::string_view> attribute(std::string_view name) const noexcept;
    AttributeRange attributes() const noexcept {
        return AttributeRange{AttributeIterator(detail::firstAttribute(node_))};
    }
    // Text content. The view is valid until the next text() call on an element of the same
    // document (it points into the document when there is one text node, which is the rule).
    std::string_view text() const;
    String textString() const { return String(text()); }
    bool hasChildElements() const noexcept { return detail::firstChildElement(node_) != nullptr; }
    // 1-based line of the element's start tag (for diagnostics).
    int32_t lineNumber() const;

    ElementRange children() const noexcept;
    ElementRange children(std::string_view name) const noexcept;
    // First child element with that name, or an Element for which isNull() is true.
    Element child(std::string_view name) const noexcept;
    bool isNull() const noexcept { return node_ == nullptr; }

    pugi::xml_node_struct* node() const noexcept { return node_; }
    detail::XmlDoc* document() const noexcept { return doc_; }

private:
    friend class ElementIterator;
    pugi::xml_node_struct* node_ = nullptr;
    detail::XmlDoc* doc_ = nullptr;
    std::string_view name_;
};

// Iterates child elements; dereferencing yields an Element* valid until the next increment.
class ElementIterator {
public:
    using value_type = Element*;
    using difference_type = std::ptrdiff_t;
    ElementIterator() noexcept = default;
    ElementIterator(pugi::xml_node_struct* n, detail::XmlDoc* doc, std::string_view filter, bool filtered) noexcept
        : cur_(n, doc), filter_(filter), filtered_(filtered) {
        skip();
    }
    Element* operator*() noexcept { return &cur_; }
    ElementIterator& operator++() noexcept {
        cur_ = Element(detail::nextSiblingElement(cur_.node_), cur_.doc_);
        skip();
        return *this;
    }
    bool operator==(const ElementIterator& o) const noexcept { return cur_.node_ == o.cur_.node_; }
    bool operator!=(const ElementIterator& o) const noexcept { return cur_.node_ != o.cur_.node_; }

private:
    void skip() noexcept {
        if (!filtered_) return;
        while (cur_.node_ != nullptr && cur_.name_ != filter_)
            cur_ = Element(detail::nextSiblingElement(cur_.node_), cur_.doc_);
    }
    Element cur_;
    std::string_view filter_;
    bool filtered_ = false;
};

struct ElementRange {
    pugi::xml_node_struct* first;
    detail::XmlDoc* doc;
    std::string_view filter;
    bool filtered;
    ElementIterator begin() const noexcept { return ElementIterator(first, doc, filter, filtered); }
    ElementIterator end() const noexcept { return ElementIterator(); }
};

inline ElementRange Element::children() const noexcept {
    return ElementRange{detail::firstChildElement(node_), doc_, {}, false};
}
inline ElementRange Element::children(std::string_view name) const noexcept {
    return ElementRange{detail::firstChildElement(node_), doc_, name, true};
}

// =======================================================================================
// JaxbAccess: how the runtime (and generated code) constructs beans and calls binders.
// JAXB reaches private no-arg constructors by reflection; a generated class whose no-arg
// constructor is private declares `friend struct jlang::xml::JaxbAccess;`.
struct JaxbAccess {
    template<class T>
    static T* create() {
        return new T();
    }
};

// =======================================================================================
// JaxbContext: the state of one unmarshal() call (JAXB RI's UnmarshallingContext). The
// generated binders receive it; the Unmarshaller creates it.
class JaxbContext final {
public:
    explicit JaxbContext(Unmarshaller* u);
    JaxbContext(const JaxbContext&) = delete;
    JaxbContext& operator=(const JaxbContext&) = delete;
    ~JaxbContext();

    // The Unmarshaller passed to afterUnmarshal(Unmarshaller*, Object* parent).
    Unmarshaller* getUnmarshaller() const noexcept { return unmarshaller_; }

    // ---- beans
    // `new T()` then T::_jaxbUnmarshal(e, this). Use for every child bean (single-valued,
    // collection item, polymorphic alternative): `holder = ctx->unmarshal<NpcTemplate>(c);`
    template<class T>
    T* unmarshal(Element* e) {
        T* o = JaxbAccess::create<T>();
        o->_jaxbUnmarshal(e, this);
        return o;
    }

    // ---- collection and array properties (scope = the bean currently being unmarshalled)
    // Returns the collection for a List/Set/TreeSet... field, creating it (`new C()`) if the
    // field is null. The first call for a field within one element clears a pre-filled
    // collection (JAXB startPacking). Items are added in document order, also when elements
    // of other properties are interleaved:
    //   ctx->collection(spawnGroups)->add(ctx->unmarshal<SpawnGroup>(c));
    template<class C>
    C* collection(C*& field) {
        if (markStarted(&field)) {
            if (field == nullptr) {
                field = new C();
            } else {
                field->clear();
            }
        }
        return field;
    }
    // Appends to an array field (`long[] exp`, `Foo[] foos`). The array is built when the
    // scope finishes (JaxbScope::finish, before afterUnmarshal) and replaces any previous
    // array; the field is untouched if the element has no such children.
    template<class T>
    void arrayAdd(Array<T>*& field, const T& value) {
        auto* pack = static_cast<ArrayPack<T>*>(findPack(&field));
        if (pack == nullptr) {
            pack = new ArrayPack<T>(&field);
            addPack(pack);
        }
        pack->items.push_back(value);
    }

    // ---- @XmlID / @XmlIDREF (one global ID space per document; later IDs win)
    // Registers `bean` under the trimmed lexical and returns the trimmed String to store in
    // the @XmlID field (JAXB stores the trimmed value):  id = ctx->id(value, this);
    String id(std::string_view lexical, Object* bean);
    void registerId(std::string_view id, Object* bean);
    Object* findId(std::string_view id) const;
    // Single-valued IDREF (attribute or element). The lexical is trimmed. If the ID is already
    // known the setter runs now (backward reference); otherwise at the end of the document,
    // after every afterUnmarshal, against the final ID table. Unresolved: the setter never runs.
    //   ctx->idref(value, [this](jlang::Object* o) { template_ = jlang::cast<ItemTemplate>(o); });
    template<class F>
    void idref(std::string_view lexical, F setter) {
        std::string_view id = trimXml(lexical);
        if (Object* o = findId(id)) {
            setter(o);
            return;
        }
        std::string key(id);
        addPatcher([this, key, setter]() mutable {
            if (Object* o = findId(key)) setter(o);
        });
    }
    // One item of a collection/array IDREF property (repeated <item>id</item>). `field` is
    // the property's field (it identifies the property of this bean). The lexical is NOT
    // trimmed (JAXB RI). All items of the property are resolved at the end of the document, in
    // document order; unresolved ids are dropped; then `store` runs once with the resolved
    // objects (use toArray/toList below):
    //   ctx->idrefItem(items, c->text(), [this](std::vector<jlang::Object*>& v) {
    //       items = jlang::xml::toArray<ItemTemplate*>(v); });
    template<class Field, class F>
    void idrefItem(Field& field, std::string_view lexical, F store) {
        IdrefPack* p = findIdrefPack(&field);
        if (p == nullptr) {
            p = newIdrefPack(&field, std::function<void(std::vector<Object*>&)>(std::move(store)));
        }
        p->ids.emplace_back(lexical);
    }

    // ---- @XmlJavaTypeAdapter: one adapter instance of each class per unmarshal call.
    template<class A>
    A* adapter() {
        auto it = adapters_.find(&typeid(A));
        if (it != adapters_.end()) return static_cast<A*>(it->second);
        A* a = JaxbAccess::create<A>();
        adapters_.emplace(&typeid(A), static_cast<void*>(a));
        return a;
    }

    // ---- driver (called by Unmarshaller)
    // Runs the deferred IDREF patchers in registration order.
    void finishDocument();
    // Registers an arbitrary end-of-document action (after all afterUnmarshal callbacks).
    void addPatcher(std::function<void()> fn) { patchers_.push_back(std::move(fn)); }

private:
    friend class JaxbScope;

    struct PackBase {
        explicit PackBase(void* f) : field(f) {}
        virtual ~PackBase() = default;
        virtual void finish() = 0;
        void* field;
    };
    template<class T>
    struct ArrayPack final : PackBase {
        explicit ArrayPack(Array<T>** f) : PackBase(f) {}
        void finish() override {
            *static_cast<Array<T>**>(field) = Array<T>::fromRange(items.begin(), items.end());
        }
        std::vector<T> items;
    };
    struct IdrefPack {
        std::vector<std::string> ids;
        std::function<void(std::vector<Object*>&)> store;
    };
    struct Frame {
        Object* bean = nullptr;
        std::vector<const void*> started;
        std::vector<PackBase*> packs;
    };

    // Frame stack (JaxbScope).
    size_t pushFrame(Object* bean);
    void finishFrame(size_t index);
    void popFrame(size_t index) noexcept;
    Object* frameParent(size_t index) const noexcept {
        return index > 0 ? frames_[index - 1].bean : nullptr;
    }
    bool markStarted(const void* field);
    PackBase* findPack(const void* field);
    void addPack(PackBase* p);
    IdrefPack* findIdrefPack(const void* field);
    IdrefPack* newIdrefPack(const void* field, std::function<void(std::vector<Object*>&)> store);

    struct StrHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>()(s); }
    };

    Unmarshaller* unmarshaller_;
    std::vector<Frame> frames_;
    size_t depth_ = 0;
    std::unordered_map<std::string, Object*, StrHash, std::equal_to<>> ids_;
    std::unordered_map<const void*, IdrefPack*> idrefPacks_;
    std::vector<std::function<void()>> patchers_;
    std::unordered_map<const std::type_info*, void*> adapters_;
};

// RAII frame for one bean (see the standard _jaxbUnmarshal body above).
class JaxbScope {
public:
    template<class T>
    JaxbScope(JaxbContext* ctx, T* bean) : ctx_(ctx), index_(ctx->pushFrame(static_cast<Object*>(bean))) {}
    JaxbScope(const JaxbScope&) = delete;
    JaxbScope& operator=(const JaxbScope&) = delete;
    ~JaxbScope() { ctx_->popFrame(index_); }

    // The enclosing bean (afterUnmarshal's `parent` argument); nullptr for the root.
    Object* parent() const noexcept { return ctx_->frameParent(index_); }
    // Builds the array properties collected with arrayAdd. Call once, after the children
    // loop and before afterUnmarshal.
    void finish() { ctx_->finishFrame(index_); }

private:
    JaxbContext* ctx_;
    size_t index_;
};

// Helpers for idrefItem's store callback: cast every object to T (ClassCastException where
// JAXB would throw ArrayStoreException for a typed array).
template<class T>
Array<T>* toArray(const std::vector<Object*>& objs) {
    auto* a = new Array<T>(static_cast<int32_t>(objs.size()));
    for (size_t i = 0; i < objs.size(); i++) (*a)[static_cast<int32_t>(i)] = cast<std::remove_pointer_t<T>>(objs[i]);
    return a;
}
// Fills a collection field: created if null, cleared, then the objects added in order.
template<class C>
void toCollection(C*& field, const std::vector<Object*>& objs) {
    if (field == nullptr) {
        field = new C();
    } else {
        field->clear();
    }
    using T = typename C::value_type;
    for (Object* o : objs) field->add(cast<std::remove_pointer_t<T>>(o));
}

// =======================================================================================
// @XmlElements: element name -> factory, for polymorphic properties. Declare it as a
// function-local static in the generated _jaxbElement:
//
//   static const jlang::xml::ElementFactories<EffectTemplate> choices{
//       {"root", jlang::xml::makeBean<EffectTemplate, RootEffect>},
//       {"spellatk", jlang::xml::makeBean<EffectTemplate, SpellAttackEffect>}, ...};
//   if (auto make = choices.find(c->name())) { ctx->collection(effects)->add(make(c, ctx)); return true; }
template<class B>
struct ElementChoice {
    const char* name;
    B* (*make)(Element*, JaxbContext*);
};
template<class B, class T>
B* makeBean(Element* e, JaxbContext* ctx) {
    return ctx->unmarshal<T>(e);
}
template<class B>
class ElementFactories {
public:
    using Factory = B* (*)(Element*, JaxbContext*);
    ElementFactories(std::initializer_list<ElementChoice<B>> choices) {
        for (const auto& c : choices) map_.emplace(std::string_view(c.name), c.make);  // first wins
    }
    Factory find(std::string_view name) const noexcept {
        auto it = map_.find(name);
        return it == map_.end() ? nullptr : it->second;
    }

private:
    std::unordered_map<std::string_view, Factory> map_;
};

// =======================================================================================
// javax.xml.bind.annotation.adapters.XmlAdapter<ValueType, BoundType>, erased. The generator
// declares the Java methods of a subclass as typed overloads next to the erased ones:
//   class NpcEquippedGearAdapter : public jlang::xml::XmlAdapter {
//       virtual NpcEquipmentList* marshal(NpcEquippedGear* v);
//       virtual NpcEquippedGear* unmarshal(NpcEquipmentList* v);
//       using jlang::xml::XmlAdapter::unmarshal;
//       using jlang::xml::XmlAdapter::marshal;
//   };
// Generated binders know both types and call the typed overload:
//   gear = ctx->adapter<NpcEquippedGearAdapter>()->unmarshal(ctx->unmarshal<NpcEquipmentList>(c));
// after the value object's own unmarshal (and afterUnmarshal) completed; its IDREF collections
// are still unresolved at that point, exactly as in JAXB. The erased virtuals are for code that
// only holds an XmlAdapter*; a subclass may override them, the defaults throw
// UnsupportedOperationException.
class XmlAdapter : public virtual Object {
public:
    virtual Object* unmarshal(Object* v);
    virtual Object* marshal(Object* v);
};

// =======================================================================================
// Marshalling output: JAXB RI's UTF8XmlOutput / IndentingUTF8XmlOutput with the
// MinimumEscapeHandler, byte for byte:
//   * header `<?xml version="1.0" encoding="UTF-8" standalone="yes"?>` (not for fragments);
//   * formatted: every start tag on a new line indented 4 spaces per depth (depth % 8, as the
//     RI does), end tags indented unless the element has text or is empty; empty elements
//     are written `<x .../>`; a final newline;
//   * escaping: & < > \r always (&amp; &lt; &gt; &#13;), \n and " in attribute values
//     (&#10; &quot;); everything else (', \t, non-ASCII) raw UTF-8.
//
// Generated `_jaxbMarshal(w)` writes the bean's attributes and then its child elements; the
// caller writes the element's own start/end tag:
//   void SpawnGroup::_jaxbMarshal(jlang::xml::Writer* w) {
//       w->attribute("time", spawnTime);          // null enum/String/optional: omitted
//       w->attribute("interval", interval);
//       if (objects != nullptr) for (SpawnTemplate* o : *objects) {
//           if (o == nullptr) continue;
//           w->startElement("object"); o->_jaxbMarshal(w); w->endElement();
//       }
//   }
// With inheritance, emit `_jaxbMarshalAttributes(w)` and `_jaxbMarshalElements(w)` per class
// (each calling the base first) and `_jaxbMarshal(w)` calling both, since all attributes must
// precede the first child element.
template<class E>
concept JavaEnum = std::is_class_v<E> && requires(const E& e) {
    { e.name() } -> std::convertible_to<String>;
    E::values();
    e == nullptr;
};

class Writer final {
public:
    explicit Writer(bool formatted = false) : formatted_(formatted) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    void startDocument(bool fragment = false);
    void endDocument();
    void startElement(std::string_view name);
    void endElement();

    // Attributes (only directly after startElement).
    void attribute(std::string_view name, std::string_view value);
    void attribute(std::string_view name, const char* value) {
        if (value != nullptr) attribute(name, std::string_view(value));
    }
    void attribute(std::string_view name, const String& value) {  // null: omitted
        if (!value.isNull()) attribute(name, std::string_view(value));
    }
    void attribute(std::string_view name, const std::string& value) { attribute(name, std::string_view(value)); }
    void attribute(std::string_view name, bool v) { attribute(name, std::string_view(v ? "true" : "false")); }
    void attribute(std::string_view name, int8_t v) { attribute(name, static_cast<int64_t>(v)); }
    void attribute(std::string_view name, int16_t v) { attribute(name, static_cast<int64_t>(v)); }
    void attribute(std::string_view name, int32_t v) { attribute(name, static_cast<int64_t>(v)); }
    void attribute(std::string_view name, int64_t v);
    void attribute(std::string_view name, float v) { attribute(name, std::string_view(printFloat(v))); }
    void attribute(std::string_view name, double v) { attribute(name, std::string_view(printDouble(v))); }
    template<class T>
    void attribute(std::string_view name, const std::optional<T>& v) {  // null: omitted
        if (v.has_value()) attribute(name, *v);
    }
    template<JavaEnum E>
    void attribute(std::string_view name, const E& v) {  // null: omitted
        if (!(v == nullptr)) attribute(name, std::string_view(enumLexical(v)));
    }

    // Character content of the current element.
    void text(std::string_view value);
    void text(const char* value) { text(std::string_view(value != nullptr ? value : "")); }
    void text(const String& value) { text(std::string_view(value)); }
    void text(const std::string& value) { text(std::string_view(value)); }
    void text(bool v) { text(std::string_view(v ? "true" : "false")); }
    void text(int8_t v) { text(static_cast<int64_t>(v)); }
    void text(int16_t v) { text(static_cast<int64_t>(v)); }
    void text(int32_t v) { text(static_cast<int64_t>(v)); }
    void text(int64_t v);
    void text(float v) { text(std::string_view(printFloat(v))); }
    void text(double v) { text(std::string_view(printDouble(v))); }
    template<JavaEnum E>
    void text(const E& v) {
        text(std::string_view(enumLexical(v)));
    }

    // Simple-typed child element <name>value</name>; a null String/optional/enum writes nothing.
    template<class T>
    void element(std::string_view name, const T& v) {
        if constexpr (requires { v.has_value(); }) {
            if (v.has_value()) element(name, *v);
        } else {
            if constexpr (std::is_same_v<T, String>) {
                if (v.isNull()) return;
            } else if constexpr (JavaEnum<T>) {
                if (v == nullptr) return;
            }
            startElement(name);
            text(v);
            endElement();
        }
    }

    const std::string& output() const noexcept { return out_; }
    std::string& output() noexcept { return out_; }

    template<class E>
    static String enumLexical(const E& v) {
        if constexpr (requires { v.toXml(); }) {
            return v.toXml();
        } else {
            return v.name();
        }
    }

private:
    void closeStartTag();
    void printIndent();
    void escape(std::string_view s, bool attribute);

    std::string out_;
    std::vector<std::string> names_;
    bool formatted_;
    int32_t depth_ = 0;
    bool seenText_ = false;
    bool closeStartTagPending_ = false;
};

// =======================================================================================
// Type table used by JAXBContext (one entry per class given to newInstance<...>()).
struct JaxbType {
    const std::type_info* type;
    const char* rootName;                            // @XmlRootElement name, or nullptr
    Object* (*unmarshal)(Element*, JaxbContext*);    // new T + _jaxbUnmarshal, or nullptr
    void (*marshal)(Object*, Writer*);               // T::_jaxbMarshal, or nullptr
    bool (*isInstance)(Object*);
};

namespace detail {
template<class T>
Object* unmarshalAs(Element* e, JaxbContext* ctx) {
    return static_cast<Object*>(ctx->unmarshal<T>(e));
}
template<class T>
void marshalAs(Object* o, Writer* w) {
    cast<T>(o)->_jaxbMarshal(w);
}
template<class T>
bool isInstanceOf(Object* o) {
    return o != nullptr && dynamic_cast<T*>(o) != nullptr;
}
}  // namespace detail

template<class T>
const JaxbType* jaxbType() {
    static const JaxbType t = [] {
        JaxbType r{&typeid(T), nullptr, nullptr, nullptr, &detail::isInstanceOf<T>};
        if constexpr (requires { T::_jaxbRootName; }) r.rootName = T::_jaxbRootName;
        if constexpr (!std::is_abstract_v<T> &&
                      requires(T* t, Element* e, JaxbContext* c) { t->_jaxbUnmarshal(e, c); }) {
            r.unmarshal = &detail::unmarshalAs<T>;
        }
        if constexpr (requires(T* t, Writer* w) { t->_jaxbMarshal(w); }) r.marshal = &detail::marshalAs<T>;
        return r;
    }();
    return &t;
}

// =======================================================================================
// javax.xml.validation.Schema / SchemaFactory: validation is a no-op (the data validates
// cleanly against the XSDs; validate in CI with xmllint instead). XSD default= values were
// never applied by JAXB either.
class Schema : public virtual Object {
public:
    Schema() = default;
    // Java Schema.newValidator() etc. are not used.
};

class SchemaFactory : public virtual Object {
public:
    // Any schema language is accepted (Java: W3C XML Schema).
    static SchemaFactory* newInstance(const String& schemaLanguage);
    Schema* newSchema(File* schema);
    Schema* newSchema();
};

// javax.xml.XMLConstants
class XMLConstants final {
public:
    static inline const String NULL_NS_URI = "";
    static inline const String DEFAULT_NS_PREFIX = "";
    static inline const String XML_NS_URI = "http://www.w3.org/XML/1998/namespace";
    static inline const String XML_NS_PREFIX = "xml";
    static inline const String XMLNS_ATTRIBUTE_NS_URI = "http://www.w3.org/2000/xmlns/";
    static inline const String XMLNS_ATTRIBUTE = "xmlns";
    static inline const String W3C_XML_SCHEMA_NS_URI = "http://www.w3.org/2001/XMLSchema";
    static inline const String W3C_XML_SCHEMA_INSTANCE_NS_URI = "http://www.w3.org/2001/XMLSchema-instance";
    static inline const String W3C_XPATH_DATATYPE_NS_URI = "http://www.w3.org/2003/11/xpath-datatypes";
    static inline const String XML_DTD_NS_URI = "http://www.w3.org/TR/REC-xml";
    static inline const String RELAXNG_NS_URI = "http://relaxng.org/ns/structure/1.0";
    static inline const String FEATURE_SECURE_PROCESSING = "http://javax.xml.XMLConstants/feature/secure-processing";
};

// =======================================================================================
// javax.xml.bind.JAXBContext
class JAXBContext : public virtual Object {
public:
    // JAXBContext.newInstance(A.class, B.class, ...) -> JAXBContext::newInstance<A, B>().
    template<class... T>
    static JAXBContext* newInstance() {
        static_assert(sizeof...(T) > 0, "JAXBContext::newInstance needs at least one class");
        return new JAXBContext(std::vector<const JaxbType*>{jaxbType<T>()...});
    }
    explicit JAXBContext(std::vector<const JaxbType*> types) : types_(std::move(types)) {}

    Unmarshaller* createUnmarshaller();
    Marshaller* createMarshaller();
    // SchemaGen (dev tool) only: throws UnsupportedOperationException.
    void generateSchema(Object* outputResolver);

    const std::vector<const JaxbType*>& types() const noexcept { return types_; }
    // The class bound to a root element name, or nullptr.
    const JaxbType* findRoot(std::string_view elementName) const noexcept;
    // The class used to marshal `o` (exact dynamic type first, then the first isInstance).
    const JaxbType* findType(Object* o) const noexcept;

private:
    std::vector<const JaxbType*> types_;
};

// =======================================================================================
// javax.xml.bind.Unmarshaller
//
// Errors: a missing/unreadable file or malformed XML throws UnmarshalException whose linked
// exception (getCause()) is the IOException / SAXParseException, as in Java. Exceptions thrown
// by lexical conversion (NumberFormatException...) and by afterUnmarshal propagate unchanged,
// as they do from the JAXB RI.
class Unmarshaller : public virtual Object {
public:
    explicit Unmarshaller(JAXBContext* context) : context_(context) {}

    void setSchema(Schema* schema) { schema_ = schema; }  // kept, not used (no validation)
    Schema* getSchema() { return schema_; }

    // Root element chosen by name among the context's classes (UnmarshalException otherwise).
    Object* unmarshal(File* file);
    Object* unmarshal(InputStream* in);
    Object* unmarshal(Reader* reader);
    Object* unmarshal(const String& path);  // convenience (a file path)
    Object* unmarshalString(std::string_view xml);

    // Typed variants: bind the root element as T, whatever its name (declared-type
    // unmarshal). T needs a generated _jaxbUnmarshal; it need not be in the context.
    template<class T>
    T* unmarshal(File* file) {
        return cast<T>(unmarshalFileAs(file, jaxbType<T>()));
    }
    template<class T>
    T* unmarshal(const String& path) {
        return cast<T>(unmarshalPathAs(path, jaxbType<T>()));
    }
    template<class T>
    T* unmarshal(InputStream* in) {
        return cast<T>(unmarshalStreamAs(in, jaxbType<T>()));
    }
    template<class T>
    T* unmarshalString(std::string_view xml) {
        return cast<T>(unmarshalStringAs(xml, jaxbType<T>()));
    }

    // Unmarshals an already parsed element as the root of a document (tests, hand-written
    // code): runs the IDREF patchers at the end.
    Object* unmarshalElement(Element* root, const JaxbType* type);

    JAXBContext* getContext() { return context_; }

private:
    Object* unmarshalFileAs(File* file, const JaxbType* type);
    Object* unmarshalPathAs(const String& path, const JaxbType* type);
    Object* unmarshalStreamAs(InputStream* in, const JaxbType* type);
    Object* unmarshalStringAs(std::string_view xml, const JaxbType* type);
    Object* unmarshalDoc(detail::XmlDoc* doc, const JaxbType* type);

    JAXBContext* context_;
    Schema* schema_ = nullptr;
};

// =======================================================================================
// javax.xml.bind.Marshaller (UTF-8 only).
class Marshaller : public virtual Object {
public:
    static inline const String JAXB_ENCODING = "jaxb.encoding";
    static inline const String JAXB_FORMATTED_OUTPUT = "jaxb.formatted.output";
    static inline const String JAXB_SCHEMA_LOCATION = "jaxb.schemaLocation";
    static inline const String JAXB_NO_NAMESPACE_SCHEMA_LOCATION = "jaxb.noNamespaceSchemaLocation";
    static inline const String JAXB_FRAGMENT = "jaxb.fragment";

    explicit Marshaller(JAXBContext* context) : context_(context) {}

    // Known properties: the five above (booleans as jlang::Boolean*, strings as boxed
    // Strings); anything else throws PropertyException, as the RI does.
    void setProperty(const String& name, Object* value);
    void setProperty(const String& name, bool value);
    void setProperty(const String& name, const String& value);
    void setProperty(const String& name, const char* value) { setProperty(name, String(value)); }
    Object* getProperty(const String& name);
    void setSchema(Schema* schema) { schema_ = schema; }  // kept, not used
    Schema* getSchema() { return schema_; }

    // The object's class must be in the context (or derive from one) with _jaxbRootName and
    // _jaxbMarshal; otherwise MarshalException. File errors: MarshalException linking the
    // IOException.
    void marshal(Object* obj, File* output);
    void marshal(Object* obj, OutputStream* os);
    void marshal(Object* obj, ::jlang::Writer* writer);
    std::string marshalToString(Object* obj);

private:
    void marshalTo(Object* obj, Writer& w);

    JAXBContext* context_;
    Schema* schema_ = nullptr;
    bool formatted_ = false;
    bool fragment_ = false;
    String encoding_ = "UTF-8";
    String schemaLocation_;
    String noNsSchemaLocation_;
};

// =======================================================================================
// Template definitions.

namespace detail {
template<class E>
struct EnumTable {
    struct Hash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>()(s); }
    };
    std::unordered_map<std::string, E, Hash, std::equal_to<>> byName;
    EnumTable() {
        auto* vals = E::values();
        for (int32_t i = 0; i < vals->length; i++) {
            E v = (*vals)[i];
            String lex = Writer::enumLexical(v);
            byName.emplace(std::string(lex), v);  // first constant wins
        }
    }
};
}  // namespace detail

template<class E>
E parseEnum(std::string_view lexical) {
    static const detail::EnumTable<E>* table = new detail::EnumTable<E>();
    auto it = table->byName.find(lexical);
    return it == table->byName.end() ? E() : it->second;
}

template<class E, class F>
E parseEnum(std::string_view lexical, F lookup) {
    try {
        return lookup(String(lexical));
    } catch (::jlang::IllegalArgumentException&) {
        return E();
    }
}

template<class T, class F>
List<T>* parseList(std::string_view lexical, F parseItem) {
    auto* list = new List<T>();
    size_t i = 0, n = lexical.size();
    while (i < n) {
        while (i < n && isXmlWhitespace(lexical[i])) i++;
        size_t b = i;
        while (i < n && !isXmlWhitespace(lexical[i])) i++;
        if (i > b) list->add(parseItem(lexical.substr(b, i - b)));
    }
    return list;
}

}  // namespace jlang::xml
