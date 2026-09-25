// jlang/src/xml_jaxb.cpp - JAXB runtime: JaxbContext (unmarshalling state), Writer (JAXB RI
// output format), JAXBContext / Unmarshaller / Marshaller facades, SchemaFactory.
#include "xml_internal.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>

namespace jlang::xml {

// =======================================================================================
// JAXBException

String JAXBException::toString() {
    String s = Exception::toString();
    Throwable* linked = getCause();
    if (linked == nullptr) return s;
    return s + "\n - with linked exception:\n[" + linked->toString() + "]";
}

// =======================================================================================
// JaxbContext

JaxbContext::JaxbContext(Unmarshaller* u) : unmarshaller_(u) { frames_.reserve(32); }

JaxbContext::~JaxbContext() = default;

size_t JaxbContext::pushFrame(Object* bean) {
    if (depth_ == frames_.size()) frames_.emplace_back();
    Frame& f = frames_[depth_];
    f.bean = bean;
    f.started.clear();
    f.packs.clear();
    return depth_++;
}

void JaxbContext::finishFrame(size_t index) {
    Frame& f = frames_[index];
    // Packs finish in the order their properties were first seen.
    for (size_t i = 0; i < f.packs.size(); i++) f.packs[i]->finish();
    f.packs.clear();
}

void JaxbContext::popFrame(size_t index) noexcept {
    if (index < frames_.size()) {
        frames_[index].bean = nullptr;
        frames_[index].packs.clear();
    }
    depth_ = index;
}

bool JaxbContext::markStarted(const void* field) {
    if (depth_ == 0) throw ::jlang::IllegalStateException("jlang::xml::JaxbContext: collection() outside a JaxbScope");
    Frame& f = frames_[depth_ - 1];
    for (const void* p : f.started) {
        if (p == field) return false;
    }
    f.started.push_back(field);
    return true;
}

JaxbContext::PackBase* JaxbContext::findPack(const void* field) {
    if (depth_ == 0) throw ::jlang::IllegalStateException("jlang::xml::JaxbContext: arrayAdd() outside a JaxbScope");
    for (PackBase* p : frames_[depth_ - 1].packs) {
        if (p->field == field) return p;
    }
    return nullptr;
}

void JaxbContext::addPack(PackBase* p) { frames_[depth_ - 1].packs.push_back(p); }

String JaxbContext::id(std::string_view lexical, Object* bean) {
    std::string_view t = trimXml(lexical);
    registerId(t, bean);
    return String(t);
}

void JaxbContext::registerId(std::string_view id, Object* bean) {
    auto it = ids_.find(id);
    if (it != ids_.end()) {
        it->second = bean;  // later ID wins (DefaultIDResolver's HashMap.put)
    } else {
        ids_.emplace(std::string(id), bean);
    }
}

Object* JaxbContext::findId(std::string_view id) const {
    auto it = ids_.find(id);
    return it == ids_.end() ? nullptr : it->second;
}

JaxbContext::IdrefPack* JaxbContext::findIdrefPack(const void* field) {
    auto it = idrefPacks_.find(field);
    return it == idrefPacks_.end() ? nullptr : it->second;
}

JaxbContext::IdrefPack* JaxbContext::newIdrefPack(const void* field, std::function<void(std::vector<Object*>&)> store) {
    auto* p = new IdrefPack();
    p->store = std::move(store);
    idrefPacks_.emplace(field, p);
    addPatcher([this, p]() {
        std::vector<Object*> objs;
        objs.reserve(p->ids.size());
        for (const std::string& id : p->ids) {
            if (Object* o = findId(id)) objs.push_back(o);  // unresolved ids are dropped
        }
        p->store(objs);
    });
    return p;
}

void JaxbContext::finishDocument() {
    for (size_t i = 0; i < patchers_.size(); i++) patchers_[i]();
    patchers_.clear();
    idrefPacks_.clear();
}

// =======================================================================================
// Writer

static constexpr std::string_view kXmlDecl = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";

void Writer::startDocument(bool fragment) {
    if (!fragment) out_.append(kXmlDecl);
}

void Writer::endDocument() {
    closeStartTag();
    if (formatted_) out_.push_back('\n');
}

void Writer::closeStartTag() {
    if (closeStartTagPending_) {
        out_.push_back('>');
        closeStartTagPending_ = false;
    }
}

void Writer::printIndent() {
    out_.push_back('\n');
    // IndentingUTF8XmlOutput.printIndent writes (depth % 8) units and then loops over
    // (depth % 8) >> 3 == 0 more blocks of 8: deeper levels wrap around. Kept as is.
    out_.append(static_cast<size_t>(depth_ % 8) * 4, ' ');
}

void Writer::startElement(std::string_view name) {
    if (formatted_) {
        closeStartTag();
        if (!seenText_) printIndent();
        depth_++;
        seenText_ = false;
    }
    closeStartTag();
    out_.push_back('<');
    out_.append(name);
    names_.emplace_back(name);
    closeStartTagPending_ = true;
}

void Writer::endElement() {
    if (names_.empty()) throw ::jlang::IllegalStateException("jlang::xml::Writer: endElement without startElement");
    if (formatted_) {
        depth_--;
        if (!closeStartTagPending_ && !seenText_) printIndent();
        seenText_ = false;
    }
    if (closeStartTagPending_) {
        out_.append("/>");
        closeStartTagPending_ = false;
    } else {
        out_.append("</");
        out_.append(names_.back());
        out_.push_back('>');
    }
    names_.pop_back();
}

void Writer::attribute(std::string_view name, std::string_view value) {
    if (!closeStartTagPending_) {
        throw ::jlang::IllegalStateException("jlang::xml::Writer: attribute() must directly follow startElement()");
    }
    out_.push_back(' ');
    out_.append(name);
    out_.append("=\"");
    escape(value, true);
    out_.push_back('"');
}

void Writer::attribute(std::string_view name, int64_t v) {
    char buf[32];
    int n = std::snprintf(buf, sizeof buf, "%" PRId64, v);
    attribute(name, std::string_view(buf, static_cast<size_t>(n)));
}

void Writer::text(std::string_view value) {
    if (formatted_) seenText_ = true;
    closeStartTag();
    escape(value, false);
}

void Writer::text(int64_t v) {
    char buf[32];
    int n = std::snprintf(buf, sizeof buf, "%" PRId64, v);
    text(std::string_view(buf, static_cast<size_t>(n)));
}

void Writer::escape(std::string_view s, bool attr) {
    // com.sun.xml.bind.marshaller.MinimumEscapeHandler
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const char* rep = nullptr;
        switch (s[i]) {
            case '&': rep = "&amp;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '\r': rep = "&#13;"; break;
            case '\n': rep = attr ? "&#10;" : nullptr; break;
            case '"': rep = attr ? "&quot;" : nullptr; break;
            default: break;
        }
        if (rep != nullptr) {
            out_.append(s.substr(start, i - start));
            out_.append(rep);
            start = i + 1;
        }
    }
    out_.append(s.substr(start));
}

// =======================================================================================
// XmlAdapter

Object* XmlAdapter::unmarshal(Object* v) {
    (void)v;
    throw ::jlang::UnsupportedOperationException(
        "jlang::xml::XmlAdapter::unmarshal(Object*): call the adapter's typed unmarshal overload");
}

Object* XmlAdapter::marshal(Object* v) {
    (void)v;
    throw ::jlang::UnsupportedOperationException(
        "jlang::xml::XmlAdapter::marshal(Object*): call the adapter's typed marshal overload");
}

// =======================================================================================
// Schema / SchemaFactory

SchemaFactory* SchemaFactory::newInstance(const String& schemaLanguage) {
    (void)schemaLanguage;
    return new SchemaFactory();
}

Schema* SchemaFactory::newSchema(File* schema) {
    (void)schema;
    return new Schema();
}

Schema* SchemaFactory::newSchema() { return new Schema(); }

// =======================================================================================
// JAXBContext

Unmarshaller* JAXBContext::createUnmarshaller() { return new Unmarshaller(this); }

Marshaller* JAXBContext::createMarshaller() { return new Marshaller(this); }

void JAXBContext::generateSchema(Object* outputResolver) {
    (void)outputResolver;
    throw ::jlang::UnsupportedOperationException("JAXBContext.generateSchema is not supported in the C++ port");
}

const JaxbType* JAXBContext::findRoot(std::string_view elementName) const noexcept {
    for (const JaxbType* t : types_) {
        if (t->rootName != nullptr && elementName == t->rootName) return t;
    }
    return nullptr;
}

const JaxbType* JAXBContext::findType(Object* o) const noexcept {
    if (o == nullptr) return nullptr;
    const std::type_info& dyn = typeid(*o);
    for (const JaxbType* t : types_) {
        if (*t->type == dyn) return t;
    }
    for (const JaxbType* t : types_) {
        if (t->isInstance(o)) return t;
    }
    return nullptr;
}

// =======================================================================================
// Unmarshaller

namespace {

std::string typeName(const JaxbType* t) {
    Class* c = Class::forType(*t->type);
    return std::string(c->getName());
}

[[noreturn]] void throwParseError(detail::XmlDoc* doc, const detail::XmlDoc::Error& err) {
    std::string sys = doc->systemId.empty() ? std::string() : doc->systemId;
    SAXParseException e(String(err.message), String(), sys.empty() ? String() : String(sys), err.line, err.column);
    throw UnmarshalException(e);
}

// Parses bytes (ownership taken) in place for JAXB.
void parseForJaxb(detail::XmlDoc* doc, detail::Bytes bytes) {
    detail::XmlDoc::Error err;
    if (!doc->parseInPlace(bytes.data, bytes.size, detail::kJaxbParse, &err)) throwParseError(doc, err);
}

}  // namespace

Object* Unmarshaller::unmarshalDoc(detail::XmlDoc* doc, const JaxbType* type) {
    pugi::xml_node rootNode = doc->doc.document_element();
    if (!rootNode) {
        SAXParseException e(String("Premature end of file."), String(),
                            doc->systemId.empty() ? String() : String(doc->systemId), 1, 1);
        throw UnmarshalException(e);
    }
    Element root(rootNode.internal_object(), doc);
    if (type == nullptr) {
        type = context_->findRoot(root.name());
        if (type == nullptr) {
            std::string expected;
            for (const JaxbType* t : context_->types()) {
                if (t->rootName == nullptr) continue;
                if (!expected.empty()) expected += ",";
                expected += "<{}" + std::string(t->rootName) + ">";
            }
            throw UnmarshalException(String("unexpected element (uri:\"\", local:\"" + std::string(root.name()) +
                                            "\"). Expected elements are " + expected));
        }
    }
    return unmarshalElement(&root, type);
}

Object* Unmarshaller::unmarshalElement(Element* root, const JaxbType* type) {
    if (type == nullptr || type->unmarshal == nullptr) {
        throw UnmarshalException(String("jlang::xml: no generated _jaxbUnmarshal for ") +
                                 (type == nullptr ? std::string("<null>") : typeName(type)));
    }
    JaxbContext ctx(this);
    Object* o = type->unmarshal(root, &ctx);
    ctx.finishDocument();
    return o;
}

Object* Unmarshaller::unmarshalPathAs(const String& path, const JaxbType* type) {
    detail::XmlDoc doc;
    doc.systemId = std::string(path);
    detail::Bytes bytes;
    try {
        bytes = detail::readFile(std::string(path));
    } catch (::jlang::IOException& e) {
        throw UnmarshalException(e);
    }
    parseForJaxb(&doc, bytes);
    return unmarshalDoc(&doc, type);
}

Object* Unmarshaller::unmarshalFileAs(File* file, const JaxbType* type) {
    if (file == nullptr) throw ::jlang::IllegalArgumentException("file parameter must not be null");
    return unmarshalPathAs(String(detail::filePath(file)), type);
}

Object* Unmarshaller::unmarshalStreamAs(InputStream* in, const JaxbType* type) {
    if (in == nullptr) throw ::jlang::IllegalArgumentException("InputStream parameter must not be null");
    detail::XmlDoc doc;
    detail::Bytes bytes;
    try {
        bytes = detail::readStream(in);
    } catch (::jlang::IOException& e) {
        throw UnmarshalException(e);
    }
    parseForJaxb(&doc, bytes);
    return unmarshalDoc(&doc, type);
}

Object* Unmarshaller::unmarshalStringAs(std::string_view xml, const JaxbType* type) {
    detail::XmlDoc doc;
    auto* data = static_cast<char*>(std::malloc(xml.size() + 1));
    if (data == nullptr) throw std::bad_alloc();
    std::memcpy(data, xml.data(), xml.size());
    data[xml.size()] = 0;
    parseForJaxb(&doc, detail::Bytes{data, xml.size()});
    return unmarshalDoc(&doc, type);
}

Object* Unmarshaller::unmarshal(File* file) { return unmarshalFileAs(file, nullptr); }
Object* Unmarshaller::unmarshal(const String& path) { return unmarshalPathAs(path, nullptr); }
Object* Unmarshaller::unmarshal(InputStream* in) { return unmarshalStreamAs(in, nullptr); }
Object* Unmarshaller::unmarshalString(std::string_view xml) { return unmarshalStringAs(xml, nullptr); }

Object* Unmarshaller::unmarshal(Reader* reader) {
    if (reader == nullptr) throw ::jlang::IllegalArgumentException("Reader parameter must not be null");
    detail::XmlDoc doc;
    detail::Bytes bytes;
    try {
        bytes = detail::readReader(reader);
    } catch (::jlang::IOException& e) {
        throw UnmarshalException(e);
    }
    parseForJaxb(&doc, bytes);
    return unmarshalDoc(&doc, nullptr);
}

// =======================================================================================
// Marshaller

void Marshaller::setProperty(const String& name, Object* value) {
    auto asBool = [&](bool& target) {
        auto* b = dynamic_cast<::jlang::Boolean*>(value);
        if (b == nullptr) throw PropertyException(name + ": value must be a java.lang.Boolean");
        target = b->booleanValue();
    };
    auto asString = [&](String& target) {
        if (value == nullptr) {
            target = nullptr;
            return;
        }
        auto* s = dynamic_cast<::jlang::StringBox*>(value);
        if (s == nullptr) throw PropertyException(name + ": value must be a java.lang.String");
        target = s->value;
    };
    if (name.equals(JAXB_FORMATTED_OUTPUT)) {
        asBool(formatted_);
    } else if (name.equals(JAXB_FRAGMENT)) {
        asBool(fragment_);
    } else if (name.equals(JAXB_ENCODING)) {
        asString(encoding_);
    } else if (name.equals(JAXB_SCHEMA_LOCATION)) {
        asString(schemaLocation_);
    } else if (name.equals(JAXB_NO_NAMESPACE_SCHEMA_LOCATION)) {
        asString(noNsSchemaLocation_);
    } else {
        throw PropertyException(name + ": unknown property");
    }
}

void Marshaller::setProperty(const String& name, bool value) { setProperty(name, ::jlang::box(value)); }

void Marshaller::setProperty(const String& name, const String& value) {
    setProperty(name, static_cast<Object*>(::jlang::box(value)));
}

Object* Marshaller::getProperty(const String& name) {
    if (name.equals(JAXB_FORMATTED_OUTPUT)) return ::jlang::box(formatted_);
    if (name.equals(JAXB_FRAGMENT)) return ::jlang::box(fragment_);
    if (name.equals(JAXB_ENCODING)) return ::jlang::box(encoding_);
    if (name.equals(JAXB_SCHEMA_LOCATION)) return ::jlang::box(schemaLocation_);
    if (name.equals(JAXB_NO_NAMESPACE_SCHEMA_LOCATION)) return ::jlang::box(noNsSchemaLocation_);
    throw PropertyException(name + ": unknown property");
}

void Marshaller::marshalTo(Object* obj, Writer& w) {
    if (obj == nullptr) throw ::jlang::IllegalArgumentException("obj parameter must not be null");
    const JaxbType* t = context_->findType(obj);
    if (t == nullptr || t->marshal == nullptr) {
        throw MarshalException(String("jlang::xml: class ") + obj->getClass()->getName() +
                               " is not known to this context or has no generated _jaxbMarshal");
    }
    if (t->rootName == nullptr) {
        throw MarshalException(String("unable to marshal type \"") + typeName(t) +
                               "\" as an element because it is missing an @XmlRootElement annotation");
    }
    w.startDocument(fragment_);
    w.startElement(t->rootName);
    if (!schemaLocation_.isNull() || !noNsSchemaLocation_.isNull()) {
        w.attribute("xmlns:xsi", std::string_view(XMLConstants::W3C_XML_SCHEMA_INSTANCE_NS_URI));
        if (!schemaLocation_.isNull()) w.attribute("xsi:schemaLocation", schemaLocation_);
        if (!noNsSchemaLocation_.isNull()) w.attribute("xsi:noNamespaceSchemaLocation", noNsSchemaLocation_);
    }
    t->marshal(obj, &w);
    w.endElement();
    w.endDocument();
}

std::string Marshaller::marshalToString(Object* obj) {
    Writer w(formatted_);
    marshalTo(obj, w);
    return std::move(w.output());
}

void Marshaller::marshal(Object* obj, File* output) {
    if (output == nullptr) throw ::jlang::IllegalArgumentException("output parameter must not be null");
    std::string data = marshalToString(obj);
    try {
        detail::writeFile(detail::filePath(output), data);
    } catch (::jlang::IOException& e) {
        throw MarshalException(e);
    }
}

void Marshaller::marshal(Object* obj, OutputStream* os) {
    if (os == nullptr) throw ::jlang::IllegalArgumentException("os parameter must not be null");
    std::string data = marshalToString(obj);
    try {
        detail::writeStream(os, data);
    } catch (::jlang::IOException& e) {
        throw MarshalException(e);
    }
}

void Marshaller::marshal(Object* obj, ::jlang::Writer* writer) {
    if (writer == nullptr) throw ::jlang::IllegalArgumentException("writer parameter must not be null");
    std::string data = marshalToString(obj);
    try {
        detail::writeWriter(writer, data);
        detail::flushWriter(writer);
    } catch (::jlang::IOException& e) {
        throw MarshalException(e);
    }
}

}  // namespace jlang::xml
