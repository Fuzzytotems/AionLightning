// jlang/src/xml_stax.cpp - javax.xml.stream emulation (see <jlang/XmlEvents.h>) and QName.
#include "xml_internal.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>

namespace jlang::xml {

// =======================================================================================
// QName

QName::QName(const String& localPart) : QName(String(""), localPart, String("")) {}

QName::QName(const String& namespaceURI, const String& localPart) : QName(namespaceURI, localPart, String("")) {}

QName::QName(const String& namespaceURI, const String& localPart, const String& prefix)
    : namespaceURI_(namespaceURI.isNull() ? String("") : namespaceURI), localPart_(localPart), prefix_(prefix) {
    if (localPart.isNull()) {
        throw ::jlang::IllegalArgumentException("local part cannot be \"null\" when creating a QName");
    }
    if (prefix.isNull()) {
        throw ::jlang::IllegalArgumentException("prefix cannot be \"null\" when creating a QName");
    }
}

bool QName::equals(Object* o) {
    if (o == this) return true;
    auto* q = dynamic_cast<QName*>(o);
    if (q == nullptr) return false;
    return namespaceURI_.equals(q->namespaceURI_) && localPart_.equals(q->localPart_);
}

int32_t QName::hashCode() {
    if (!hashed_) {
        hash_ = namespaceURI_.hashCode() ^ localPart_.hashCode();
        hashed_ = true;
    }
    return hash_;
}

String QName::toString() {
    if (namespaceURI_.isEmpty()) return localPart_;
    return String("{") + namespaceURI_ + "}" + localPart_;
}

QName* QName::valueOf(const String& s) {
    if (s.isNull()) throw ::jlang::IllegalArgumentException("cannot create QName from \"null\" or \"\" String");
    if (s.isEmpty() || s.charAt(0) != u'{') return new QName(String(""), s, String(""));
    int32_t end = s.indexOf(u'}');
    if (end == -1) throw ::jlang::IllegalArgumentException(String("cannot create QName from \"") + s + "\", missing closing \"}\"");
    return new QName(s.substring(1, end), s.substring(end + 1), String(""));
}

// =======================================================================================
// XMLStreamException

static String streamMessage(const String& msg, Location* location) {
    if (location == nullptr) return msg;
    return String("ParseError at [row,col]:[") + location->getLineNumber() + "," + location->getColumnNumber() +
           "]\nMessage: " + msg;
}

XMLStreamException::XMLStreamException(const String& message, Location* location)
    : Exception(streamMessage(message, location)), location_(location) {}

XMLStreamException::XMLStreamException(const String& message, Location* location, Throwable* cause)
    : Exception(streamMessage(message, location), cause), location_(location) {}

// =======================================================================================
// Events

Location* XMLEvent::getLocation() {
    if (location_ == nullptr && hasPosition_) {
        if (source_ != nullptr) {
            detail::Pos p = source_->afterTag(static_cast<size_t>(offset_));
            location_ = new Location(p.line, p.column, p.offset, String(), String());
        } else {
            location_ = new Location(line_, column_, offset_, String(), String());
        }
    }
    return location_;
}

void XMLEvent::wrongKind(const char* what) {
    throw ::jlang::ClassCastException(String("class ") + getClass()->getName() + " cannot be cast to class " + what);
}

// Event types identify the class (only this file creates events), so the casts are static.
StartElement* XMLEvent::asStartElement() {
    if (type_ != XMLStreamConstants::START_ELEMENT) wrongKind("javax.xml.stream.events.StartElement");
    return static_cast<StartElement*>(this);
}

EndElement* XMLEvent::asEndElement() {
    if (type_ != XMLStreamConstants::END_ELEMENT) wrongKind("javax.xml.stream.events.EndElement");
    return static_cast<EndElement*>(this);
}

Characters* XMLEvent::asCharacters() {
    if (type_ != XMLStreamConstants::CHARACTERS && type_ != XMLStreamConstants::SPACE && type_ != XMLStreamConstants::CDATA)
        wrongKind("javax.xml.stream.events.Characters");
    return static_cast<Characters*>(this);
}

QName* XMLEvent::getName() { wrongKind("javax.xml.stream.events.StartElement"); }
Iterator<Object*>* XMLEvent::getAttributes() { wrongKind("javax.xml.stream.events.StartElement"); }
Iterator<Object*>* XMLEvent::getNamespaces() { wrongKind("javax.xml.stream.events.StartElement"); }
XMLAttribute* XMLEvent::getAttributeByName(QName*) { wrongKind("javax.xml.stream.events.StartElement"); }
String XMLEvent::getNamespaceURI(const String&) { wrongKind("javax.xml.stream.events.StartElement"); }
String XMLEvent::getData() { wrongKind("javax.xml.stream.events.Characters"); }
bool XMLEvent::isWhiteSpace() { wrongKind("javax.xml.stream.events.Characters"); }
bool XMLEvent::isIgnorableWhiteSpace() { wrongKind("javax.xml.stream.events.Characters"); }
bool XMLEvent::isCData() { wrongKind("javax.xml.stream.events.Characters"); }
String XMLEvent::getText() { wrongKind("javax.xml.stream.events.Comment"); }
String XMLEvent::getTarget() { wrongKind("javax.xml.stream.events.ProcessingInstruction"); }
String XMLEvent::getValue() { wrongKind("javax.xml.stream.events.Attribute"); }
String XMLEvent::getCharacterEncodingScheme() { wrongKind("javax.xml.stream.events.StartDocument"); }
bool XMLEvent::encodingSet() { wrongKind("javax.xml.stream.events.StartDocument"); }
bool XMLEvent::isStandalone() { wrongKind("javax.xml.stream.events.StartDocument"); }
bool XMLEvent::standaloneSet() { wrongKind("javax.xml.stream.events.StartDocument"); }
String XMLEvent::getVersion() { wrongKind("javax.xml.stream.events.StartDocument"); }
String XMLEvent::getSystemId() { wrongKind("javax.xml.stream.events.StartDocument"); }

static String qualified(QName* n) {
    String p = n->getPrefix();
    if (p.isEmpty()) return n->getLocalPart();
    return p + ":" + n->getLocalPart();
}

String XMLEvent::toString() {
    switch (type_) {
        case XMLStreamConstants::START_ELEMENT: {
            auto* se = static_cast<StartElement*>(this);
            String s = String("<") + qualified(se->getName());
            for (Namespace* ns : se->namespaceList()) {
                s = s + " " + (ns->getPrefix().isEmpty() ? String("xmlns") : String("xmlns:") + ns->getPrefix()) + "='" +
                    ns->getNamespaceURI() + "'";
            }
            for (XMLAttribute* a : se->attributeList()) s = s + " " + qualified(a->getName()) + "='" + a->getValue() + "'";
            return s + ">";
        }
        case XMLStreamConstants::END_ELEMENT:
            return String("</") + qualified(static_cast<EndElement*>(this)->getName()) + ">";
        case XMLStreamConstants::CHARACTERS:
        case XMLStreamConstants::SPACE:
        case XMLStreamConstants::CDATA:
            return static_cast<Characters*>(this)->getData();
        case XMLStreamConstants::COMMENT:
            return String("<!--") + getText() + "-->";
        case XMLStreamConstants::PROCESSING_INSTRUCTION:
            return String("<?") + getTarget() + " " + getData() + "?>";
        case XMLStreamConstants::START_DOCUMENT:
            return String("<?xml version=\"") + getVersion() + "\"?>";
        case XMLStreamConstants::END_DOCUMENT:
            return String("ENDDOCUMENT");
        case XMLStreamConstants::DTD:
            return getText();
        case XMLStreamConstants::ATTRIBUTE:
        case XMLStreamConstants::NAMESPACE:
            return qualified(getName()) + "='" + getValue() + "'";
        default:
            return Object::toString();
    }
}

Namespace::Namespace(const String& prefix, const String& namespaceURI)
    : XMLAttribute(XMLStreamConstants::NAMESPACE,
                   prefix.isEmpty() ? new QName(XMLConstants::XMLNS_ATTRIBUTE_NS_URI, String("xmlns"), String(""))
                                    : new QName(XMLConstants::XMLNS_ATTRIBUTE_NS_URI, prefix, String("xmlns")),
                   namespaceURI),
      prefix_(prefix.isNull() ? String("") : prefix),
      uri_(namespaceURI) {}

StartElement::StartElement(QName* name, std::vector<XMLAttribute*> attributes, std::vector<Namespace*> namespaces)
    : XMLEvent(XMLStreamConstants::START_ELEMENT), name_(name), attributes_(std::move(attributes)),
      namespaces_(std::move(namespaces)) {
    hashMapOrder(attributes_);
}

void StartElement::hashMapOrder(std::vector<XMLAttribute*>& attrs) {
    const size_t n = attrs.size();
    if (n <= 1) return;
    // java.util.HashMap: table 16, doubled whenever size exceeds 3/4 of it; buckets
    // (h ^ h >>> 16) & (n - 1); iteration by bucket, insertion order inside a bucket (resizes
    // split buckets preserving order). Treeification needs 8 keys in one bucket: ignored.
    size_t cap = 16;
    while (n > cap * 3 / 4) cap *= 2;
    std::vector<std::pair<uint32_t, XMLAttribute*>> keyed;
    keyed.reserve(n);
    for (XMLAttribute* a : attrs) {
        auto h = static_cast<uint32_t>(a->getName()->hashCode());
        h ^= h >> 16;
        keyed.emplace_back(h & static_cast<uint32_t>(cap - 1), a);
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    for (size_t i = 0; i < n; i++) attrs[i] = keyed[i].second;
}

static Iterator<Object*>* iterate(const std::vector<Object*>& v) {
    auto* list = new List<Object*>();
    for (Object* o : v) list->add(o);
    return list->iterator();
}

Iterator<Object*>* StartElement::getAttributes() {
    return iterate(std::vector<Object*>(attributes_.begin(), attributes_.end()));
}

Iterator<Object*>* StartElement::getNamespaces() {
    return iterate(std::vector<Object*>(namespaces_.begin(), namespaces_.end()));
}

XMLAttribute* StartElement::getAttributeByName(QName* name) {
    if (name == nullptr) return nullptr;
    for (XMLAttribute* a : attributes_) {
        if (a->getName()->equals(name)) return a;
    }
    return nullptr;
}

String StartElement::getNamespaceURI(const String& prefix) {
    for (Namespace* ns : namespaces_) {
        if (ns->getPrefix().equals(prefix)) return ns->getNamespaceURI();
    }
    if (prefix.equals(name_->getPrefix())) return name_->getNamespaceURI();
    return String();
}

Iterator<Object*>* EndElement::getNamespaces() {
    return iterate(std::vector<Object*>(namespaces_.begin(), namespaces_.end()));
}

static bool allXmlWhitespace(std::string_view s) {
    if (s.empty()) return false;  // CharacterEvent.checkWhiteSpace leaves fIsSpace false for ""
    for (char c : s) {
        if (!isXmlWhitespace(c)) return false;
    }
    return true;
}

Characters::Characters(const String& data, bool cdata, bool ignorable)
    : XMLEvent(cdata ? XMLStreamConstants::CDATA : (ignorable ? XMLStreamConstants::SPACE : XMLStreamConstants::CHARACTERS)),
      data_(data), cdata_(cdata), ignorable_(ignorable), whiteSpace_(allXmlWhitespace(data)) {}

// =======================================================================================
// XMLEventReader

namespace detail {

struct StaxCursor {
    XmlDoc* doc = nullptr;
    bool reportEncoding = false;
    enum State { BEFORE, CONTENT, FINISHED } state = BEFORE;
    pugi::xml_node_struct* node = nullptr;
    bool leaving = false;
    std::deque<XMLEvent*> queue;
    XMLEvent* last = nullptr;
    struct NsDecl {
        std::string prefix, uri;
    };
    std::vector<NsDecl> decls;
    std::vector<size_t> marks;
    std::vector<QName*> openNames;
    std::vector<std::vector<Namespace*>> openNs;
    // QNames are immutable: one object per (uri, prefix, local) and document.
    std::unordered_map<std::string, QName*> qnames;
    std::string key;

    QName* qname(std::string_view qualified, std::string_view uri) {
        key.assign(uri);
        key.push_back('\x01');
        key.append(qualified);
        auto it = qnames.find(key);
        if (it != qnames.end()) return it->second;
        size_t colon = qualified.find(':');
        QName* q = colon == std::string_view::npos
                       ? new QName(String(uri), String(qualified), String(""))
                       : new QName(String(uri), String(qualified.substr(colon + 1)), String(qualified.substr(0, colon)));
        qnames.emplace(key, q);
        return q;
    }

    static String attrValue(const char* v) {
        if (std::strchr(v, '&') == nullptr) return String(v);
        return String(decodeReferences(v));
    }

    void release() {
        if (doc != nullptr) {
            delete doc;  // runs ~XmlDoc: frees the pugixml tree and buffers
            doc = nullptr;
        }
    }

    std::string_view lookup(std::string_view prefix) const {
        if (prefix == "xml") return std::string_view(XMLConstants::XML_NS_URI);
        for (size_t i = decls.size(); i-- > 0;) {
            if (decls[i].prefix == prefix) return decls[i].uri;
        }
        return std::string_view();
    }

    void advance() {
        pugi::xml_node n(node);
        pugi::xml_node next = n.next_sibling();
        if (next) {
            node = next.internal_object();
            leaving = false;
            return;
        }
        pugi::xml_node p = n.parent();
        if (!p || p.type() == pugi::node_document) {
            node = nullptr;
        } else {
            node = p.internal_object();
            leaving = true;
        }
    }

    XMLEvent* positioned(XMLEvent* e) {
        e->setPosition(-1, -1, -1);
        return e;
    }

    XMLEvent* startDocument() {
        String version, encoding;
        bool encSet = false, standalone = false, saSet = false;
        for (pugi::xml_node c = doc->doc.first_child(); c; c = c.next_sibling()) {
            if (c.type() != pugi::node_declaration) continue;
            if (pugi::xml_attribute a = c.attribute("version")) version = String(a.value());
            if (pugi::xml_attribute a = c.attribute("encoding")) {
                encSet = true;
                if (reportEncoding) encoding = String(a.value());
            }
            if (pugi::xml_attribute a = c.attribute("standalone")) {
                saSet = true;
                standalone = std::string_view(a.value()) == "yes";
            }
            break;
        }
        auto* e = new StartDocument(encoding, encSet, version, standalone, saSet);
        e->setPosition(1, 1, 0);
        return e;
    }

    XMLEvent* startElement(pugi::xml_node n) {
        marks.push_back(decls.size());
        std::vector<Namespace*> nsList;
        for (pugi::xml_attribute a = n.first_attribute(); a; a = a.next_attribute()) {
            std::string_view an(a.name());
            if (an == "xmlns") {
                String uri = attrValue(a.value());
                decls.push_back(NsDecl{std::string(), std::string(uri)});
                nsList.push_back(new Namespace(String(""), uri));
            } else if (an.substr(0, 6) == "xmlns:") {
                String uri = attrValue(a.value());
                decls.push_back(NsDecl{std::string(an.substr(6)), std::string(uri)});
                nsList.push_back(new Namespace(String(an.substr(6)), uri));
            }
        }
        std::vector<XMLAttribute*> attrs;
        for (pugi::xml_attribute a = n.first_attribute(); a; a = a.next_attribute()) {
            std::string_view an(a.name());
            if (an == "xmlns" || an.substr(0, 6) == "xmlns:") continue;
            size_t colon = an.find(':');
            QName* q = colon == std::string_view::npos ? qname(an, std::string_view()) : qname(an, lookup(an.substr(0, colon)));
            attrs.push_back(new XMLAttribute(q, attrValue(a.value())));
        }
        std::string_view name(n.name());
        size_t colon = name.find(':');
        QName* q = qname(name, lookup(colon == std::string_view::npos ? std::string_view() : name.substr(0, colon)));
        openNames.push_back(q);
        openNs.push_back(nsList);
        auto* e = new StartElement(q, std::move(attrs), std::move(nsList));
        ptrdiff_t off = n.offset_debug();
        if (doc->source() != nullptr && off >= 0) {
            e->setTagPosition(doc->source(), static_cast<int32_t>(off));
        } else {
            e->setPosition(-1, -1, -1);
        }
        return e;
    }

    XMLEvent* endElement() {
        QName* q = openNames.back();
        std::vector<Namespace*> ns = std::move(openNs.back());
        openNames.pop_back();
        openNs.pop_back();
        decls.resize(marks.back());
        marks.pop_back();
        return positioned(new EndElement(q, std::move(ns)));
    }

    // Character data: every entity/character reference is an event of its own (SJSXP).
    void text(std::string_view raw) {
        size_t start = 0, i = 0;
        while ((i = raw.find('&', i)) != std::string_view::npos) {
            std::string rep;
            size_t j = i;
            if (!decodeReference(raw, j, rep)) {
                i++;  // not a reference we know: literal text
                continue;
            }
            if (i > start) queue.push_back(positioned(new Characters(String(raw.substr(start, i - start)), false)));
            queue.push_back(positioned(new Characters(String(rep), false)));
            start = i = j;
        }
        if (start < raw.size()) queue.push_back(positioned(new Characters(String(raw.substr(start)), false)));
    }

    XMLEvent* produce() {
        if (!queue.empty()) {
            XMLEvent* e = queue.front();
            queue.pop_front();
            return e;
        }
        if (state == BEFORE) {
            state = CONTENT;
            node = doc->doc.first_child().internal_object();
            leaving = false;
            return startDocument();
        }
        if (state == FINISHED) throw ::jlang::NoSuchElementException();
        for (;;) {
            if (node == nullptr) {
                state = FINISHED;
                release();
                return positioned(new EndDocument());
            }
            pugi::xml_node n(node);
            if (leaving) {
                XMLEvent* e = endElement();
                advance();
                return e;
            }
            bool topLevel = n.parent().type() == pugi::node_document;
            switch (n.type()) {
                case pugi::node_element: {
                    XMLEvent* e = startElement(n);
                    if (n.first_child()) {
                        node = n.first_child().internal_object();
                        leaving = false;
                    } else {
                        leaving = true;
                    }
                    return e;
                }
                case pugi::node_pcdata:
                    if (!topLevel) text(n.value());
                    advance();
                    if (!queue.empty()) {
                        XMLEvent* e = queue.front();
                        queue.pop_front();
                        return e;
                    }
                    continue;
                case pugi::node_cdata: {
                    advance();
                    if (topLevel) continue;
                    // The JDK reports CDATA sections as CHARACTERS events.
                    return positioned(new Characters(String(n.value()), false));
                }
                case pugi::node_comment: {
                    advance();
                    return positioned(new Comment(String(n.value())));
                }
                case pugi::node_pi: {
                    advance();
                    return positioned(new ProcessingInstruction(String(n.name()), String(n.value())));
                }
                case pugi::node_doctype: {
                    advance();
                    return positioned(new DTD(String("<!DOCTYPE ") + n.value() + ">"));
                }
                default:
                    advance();
                    continue;
            }
        }
    }

    bool hasNext() const { return !queue.empty() || state != FINISHED; }
};

}  // namespace detail

XMLEventReader::XMLEventReader(detail::XmlDoc* doc, bool reportEncoding) : cursor_(new detail::StaxCursor()) {
    cursor_->doc = doc;
    cursor_->reportEncoding = reportEncoding;
}

bool XMLEventReader::hasNext() { return peeked_ != nullptr || cursor_->hasNext(); }

XMLEvent* XMLEventReader::produce() {
    XMLEvent* e = cursor_->produce();
    return e;
}

XMLEvent* XMLEventReader::nextEvent() {
    XMLEvent* e;
    if (peeked_ != nullptr) {
        e = peeked_;
        peeked_ = nullptr;
    } else {
        if (!cursor_->hasNext()) throw ::jlang::NoSuchElementException();
        e = produce();
    }
    cursor_->last = e;
    return e;
}

XMLEvent* XMLEventReader::peek() {
    if (peeked_ == nullptr) {
        if (!cursor_->hasNext()) return nullptr;
        peeked_ = produce();
    }
    return peeked_;
}

String XMLEventReader::getElementText() {
    if (cursor_->last == nullptr || !cursor_->last->isStartElement()) {
        throw XMLStreamException(String("parser must be on START_ELEMENT to read next text"), cursor_->last == nullptr ? nullptr : cursor_->last->getLocation());
    }
    std::string buf;
    for (;;) {
        XMLEvent* e = nextEvent();
        int32_t t = e->getEventType();
        if (t == XMLStreamConstants::CHARACTERS || t == XMLStreamConstants::CDATA || t == XMLStreamConstants::SPACE) {
            buf.append(e->getData());
        } else if (t == XMLStreamConstants::COMMENT || t == XMLStreamConstants::PROCESSING_INSTRUCTION) {
            continue;
        } else if (t == XMLStreamConstants::END_ELEMENT) {
            break;
        } else if (t == XMLStreamConstants::END_DOCUMENT) {
            throw XMLStreamException(String("unexpected end of document when reading element text content"));
        } else if (t == XMLStreamConstants::START_ELEMENT) {
            throw XMLStreamException(String("elementGetText() function expects text only elment but START_ELEMENT was encountered."),
                                     e->getLocation());
        } else {
            throw XMLStreamException(String("Unexpected event type ") + t, e->getLocation());
        }
    }
    return String(buf);
}

XMLEvent* XMLEventReader::nextTag() {
    for (;;) {
        XMLEvent* e = nextEvent();
        int32_t t = e->getEventType();
        if ((t == XMLStreamConstants::CHARACTERS || t == XMLStreamConstants::SPACE || t == XMLStreamConstants::CDATA) &&
            e->isWhiteSpace()) {
            continue;
        }
        if (t == XMLStreamConstants::COMMENT || t == XMLStreamConstants::PROCESSING_INSTRUCTION) continue;
        if (t == XMLStreamConstants::START_ELEMENT || t == XMLStreamConstants::END_ELEMENT) return e;
        throw XMLStreamException(String("expected start or end tag"), e->getLocation());
    }
}

Object* XMLEventReader::getProperty(const String& name) {
    (void)name;
    return nullptr;
}

void XMLEventReader::close() {
    cursor_->release();
    cursor_->queue.clear();
    cursor_->state = detail::StaxCursor::FINISHED;
    peeked_ = nullptr;
}

// =======================================================================================
// XMLEventWriter

namespace detail {

struct EventWriterState {
    std::string buf;
    bool open = false;
    struct El {
        std::string prefix, local;
    };
    std::vector<El> stack;
};

static void escapeInto(std::string& out, std::string_view s, bool attr) {
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const char* rep = nullptr;
        switch (s[i]) {
            case '<': rep = "&lt;"; break;
            case '&': rep = "&amp;"; break;
            case '>': rep = "&gt;"; break;
            case '"': rep = attr ? "&quot;" : nullptr; break;
            default: break;
        }
        if (rep != nullptr) {
            out.append(s.substr(start, i - start));
            out.append(rep);
            start = i + 1;
        }
    }
    out.append(s.substr(start));
}

}  // namespace detail

static constexpr size_t kFlushThreshold = 1 << 16;

XMLEventWriter::XMLEventWriter(::jlang::Writer* out) : st_(new detail::EventWriterState()), out_(out) {}

std::string& XMLEventWriter::pending() { return st_->buf; }

void XMLEventWriter::writeStartTag(QName* name, const std::vector<Namespace*>& ns, const std::vector<XMLAttribute*>& attrs) {
    std::string& b = st_->buf;
    if (st_->open) b.push_back('>');
    b.push_back('<');
    const std::string& prefix = name->prefixRef();
    const std::string& local = name->localPartRef();
    if (!prefix.empty()) {
        b.append(prefix);
        b.push_back(':');
    }
    b.append(local);
    st_->stack.push_back(detail::EventWriterState::El{prefix, local});
    for (Namespace* n : ns) {
        const String& p = n->prefixRef();
        if (p.isEmpty() || p.equals("xmlns")) {
            b.append(" xmlns=\"");
        } else {
            b.append(" xmlns:");
            b.append(p);
            b.append("=\"");
        }
        detail::escapeInto(b, n->valueRef(), true);
        b.push_back('"');
    }
    for (XMLAttribute* a : attrs) {
        QName* q = a->getName();
        b.push_back(' ');
        const String& p = q->prefixRef();
        if (!p.isEmpty()) {
            b.append(p);
            b.push_back(':');
        }
        b.append(q->localPartRef());
        b.append("=\"");
        detail::escapeInto(b, a->valueRef(), true);
        b.push_back('"');
    }
    st_->open = true;
}

void XMLEventWriter::add(XMLEvent* event) {
    if (event == nullptr) throw ::jlang::NullPointerException("event");
    std::string& b = st_->buf;
    auto closeTag = [&] {
        if (st_->open) {
            b.push_back('>');
            st_->open = false;
        }
    };
    switch (event->getEventType()) {
        case XMLStreamConstants::START_DOCUMENT: {
            String enc = event->getCharacterEncodingScheme();
            String ver = event->getVersion();
            bool saSet = event->standaloneSet();
            if (enc.isEmpty() && ver.isEmpty() && !saSet) {
                b.append("<?xml version=\"1.0\" ?>");
            } else {
                b.append("<?xml version=\"");
                b.append(ver.isEmpty() ? std::string_view("1.0") : std::string_view(ver));
                if (!enc.isEmpty()) {
                    b.append("\" encoding=\"");
                    b.append(enc);
                }
                if (saSet) {
                    b.append("\" standalone=\"");
                    b.append(event->isStandalone() ? "yes" : "no");
                }
                b.append("\"?>");
            }
            break;
        }
        case XMLStreamConstants::START_ELEMENT: {
            StartElement* se = event->asStartElement();
            writeStartTag(se->getName(), se->namespaceList(), se->attributeList());
            break;
        }
        case XMLStreamConstants::END_ELEMENT: {
            closeTag();
            if (st_->stack.empty()) throw XMLStreamException(String("No element was found to write"));
            detail::EventWriterState::El el = std::move(st_->stack.back());
            st_->stack.pop_back();
            b.append("</");
            if (!el.prefix.empty()) {
                b.append(el.prefix);
                b.push_back(':');
            }
            b.append(el.local);
            b.push_back('>');
            break;
        }
        case XMLStreamConstants::CHARACTERS:
        case XMLStreamConstants::SPACE: {
            closeTag();
            Characters* c = event->asCharacters();
            if (c->isCData()) {
                b.append("<![CDATA[");
                b.append(c->dataRef());
                b.append("]]>");
            } else {
                detail::escapeInto(b, c->dataRef(), false);
            }
            break;
        }
        case XMLStreamConstants::CDATA: {
            closeTag();
            if (event->isCData()) {
                b.append("<![CDATA[");
                b.append(event->getData());
                b.append("]]>");
            }
            break;
        }
        case XMLStreamConstants::COMMENT:
            closeTag();
            b.append("<!--");
            b.append(event->getText());
            b.append("-->");
            break;
        case XMLStreamConstants::PROCESSING_INSTRUCTION:
            closeTag();
            b.append("<?");
            b.append(event->getTarget());
            b.push_back(' ');
            b.append(event->getData());
            b.append("?>");
            break;
        case XMLStreamConstants::DTD:
            closeTag();
            b.append(event->getText());
            break;
        case XMLStreamConstants::ATTRIBUTE: {
            if (!st_->open) throw XMLStreamException(String("Attribute not associated with any element"));
            QName* q = event->getName();
            b.push_back(' ');
            String p = q->getPrefix();
            if (!p.isEmpty()) {
                b.append(p);
                b.push_back(':');
            }
            b.append(q->getLocalPart());
            b.append("=\"");
            detail::escapeInto(b, event->getValue(), true);
            b.push_back('"');
            break;
        }
        case XMLStreamConstants::NAMESPACE: {
            if (!st_->open) throw XMLStreamException(String("Namespace Attribute not associated with any element"));
            auto* ns = dynamic_cast<Namespace*>(event);
            String p = ns != nullptr ? ns->getPrefix() : String("");
            if (p.isEmpty()) {
                b.append(" xmlns=\"");
            } else {
                b.append(" xmlns:");
                b.append(p);
                b.append("=\"");
            }
            detail::escapeInto(b, event->getValue(), true);
            b.push_back('"');
            break;
        }
        case XMLStreamConstants::END_DOCUMENT:
            closeTag();
            while (!st_->stack.empty()) {
                detail::EventWriterState::El el = std::move(st_->stack.back());
                st_->stack.pop_back();
                b.append("</");
                if (!el.prefix.empty()) {
                    b.append(el.prefix);
                    b.push_back(':');
                }
                b.append(el.local);
                b.push_back('>');
            }
            break;
        default:
            break;
    }
    if (out_ != nullptr && b.size() >= kFlushThreshold) {
        detail::writeWriter(out_, b);
        b.clear();
    }
}

void XMLEventWriter::add(XMLEventReader* reader) {
    while (reader->hasNext()) add(reader->nextEvent());
}

void XMLEventWriter::flush() {
    if (out_ == nullptr) return;  // in-memory writer
    if (!st_->buf.empty()) {
        detail::writeWriter(out_, st_->buf);
        st_->buf.clear();
    }
    detail::flushWriter(out_);
}

void XMLEventWriter::close() { flush(); }

String XMLEventWriter::getPrefix(const String& uri) {
    (void)uri;
    return String();
}

void XMLEventWriter::setPrefix(const String& prefix, const String& uri) { (void)prefix, (void)uri; }

void XMLEventWriter::setDefaultNamespace(const String& uri) { (void)uri; }

// =======================================================================================
// Factories

XMLInputFactory* XMLInputFactory::newInstance() { return new XMLInputFactory(); }

static XMLEventReader* newReader(detail::Bytes bytes, bool reportEncoding) {
    auto* doc = new detail::XmlDoc();
    detail::XmlDoc::Error err;
    if (!doc->parseWithSource(bytes.data, bytes.size, detail::kStaxParse, &err)) {
        std::string msg = err.message;
        auto* loc = new Location(err.line, err.column, -1, String(), String());
        delete doc;
        throw XMLStreamException(String(msg), loc);
    }
    return new XMLEventReader(doc, reportEncoding);
}

XMLEventReader* XMLInputFactory::createXMLEventReader(Reader* reader) {
    if (reader == nullptr) throw ::jlang::NullPointerException("reader");
    detail::Bytes bytes;
    try {
        bytes = detail::readReader(reader);
    } catch (::jlang::IOException& e) {
        throw XMLStreamException(e);
    }
    return newReader(bytes, false);
}

XMLEventReader* XMLInputFactory::createXMLEventReader(InputStream* stream) {
    if (stream == nullptr) throw ::jlang::NullPointerException("stream");
    detail::Bytes bytes;
    try {
        bytes = detail::readStream(stream);
    } catch (::jlang::IOException& e) {
        throw XMLStreamException(e);
    }
    return newReader(bytes, true);
}

XMLEventReader* XMLInputFactory::createXMLEventReaderFromFile(const String& path) {
    detail::Bytes bytes;
    try {
        bytes = detail::readFile(std::string(path));
    } catch (::jlang::IOException& e) {
        throw XMLStreamException(e);
    }
    return newReader(bytes, false);
}

XMLEventReader* XMLInputFactory::createXMLEventReaderFromString(std::string_view xml) {
    auto* data = static_cast<char*>(std::malloc(xml.size() + 1));
    if (data == nullptr) throw std::bad_alloc();
    std::memcpy(data, xml.data(), xml.size());
    data[xml.size()] = 0;
    return newReader(detail::Bytes{data, xml.size()}, false);
}

XMLOutputFactory* XMLOutputFactory::newInstance() { return new XMLOutputFactory(); }

XMLEventWriter* XMLOutputFactory::createXMLEventWriter(::jlang::Writer* writer) {
    if (writer == nullptr) throw ::jlang::NullPointerException("writer");
    return new XMLEventWriter(writer);
}

XMLEventWriter* XMLOutputFactory::createXMLEventWriterToString() { return new XMLEventWriter(nullptr); }

XMLEventFactory* XMLEventFactory::newInstance() { return new XMLEventFactory(); }

static std::vector<XMLAttribute*> collectAttributes(Iterator<Object*>* it) {
    std::vector<XMLAttribute*> v;
    if (it != nullptr) {
        while (it->hasNext()) v.push_back(cast<XMLAttribute>(it->next()));
    }
    return v;
}

static std::vector<Namespace*> collectNamespaces(Iterator<Object*>* it) {
    std::vector<Namespace*> v;
    if (it != nullptr) {
        while (it->hasNext()) v.push_back(cast<Namespace>(it->next()));
    }
    return v;
}

StartElement* XMLEventFactory::createStartElement(QName* name, Iterator<Object*>* attributes, Iterator<Object*>* namespaces) {
    return located(new StartElement(name, collectAttributes(attributes), collectNamespaces(namespaces)));
}

StartElement* XMLEventFactory::createStartElement(const String& prefix, const String& namespaceUri, const String& localName) {
    return located(new StartElement(new QName(namespaceUri, localName, prefix), {}, {}));
}

EndElement* XMLEventFactory::createEndElement(QName* name, Iterator<Object*>* namespaces) {
    return located(new EndElement(name, collectNamespaces(namespaces)));
}

EndElement* XMLEventFactory::createEndElement(const String& prefix, const String& namespaceUri, const String& localName) {
    return located(new EndElement(new QName(namespaceUri, localName, prefix), {}));
}

Characters* XMLEventFactory::createCharacters(const String& content) { return located(new Characters(content, false)); }
Characters* XMLEventFactory::createCData(const String& content) { return located(new Characters(content, true)); }
Characters* XMLEventFactory::createSpace(const String& content) { return located(new Characters(content, false)); }
Characters* XMLEventFactory::createIgnorableSpace(const String& content) {
    return located(new Characters(content, false, true));
}
Comment* XMLEventFactory::createComment(const String& text) { return located(new Comment(text)); }
ProcessingInstruction* XMLEventFactory::createProcessingInstruction(const String& target, const String& data) {
    return located(new ProcessingInstruction(target, data));
}
DTD* XMLEventFactory::createDTD(const String& dtd) { return located(new DTD(dtd)); }
// JDK StartDocumentEvent: an empty encoding becomes "UTF-8" with encodingSet() false.
static StartDocument* newStartDocument(const String& encoding, const String& version, bool standalone, bool standaloneSet) {
    bool set = !encoding.isEmpty();
    return new StartDocument(set ? encoding : String("UTF-8"), set, version, standalone, standaloneSet);
}
StartDocument* XMLEventFactory::createStartDocument() {
    return located(newStartDocument(String("UTF-8"), String("1.0"), true, false));
}
StartDocument* XMLEventFactory::createStartDocument(const String& encoding) {
    return located(newStartDocument(encoding, String("1.0"), true, false));
}
StartDocument* XMLEventFactory::createStartDocument(const String& encoding, const String& version) {
    return located(newStartDocument(encoding, version, true, false));
}
StartDocument* XMLEventFactory::createStartDocument(const String& encoding, const String& version, bool standalone) {
    return located(newStartDocument(encoding, version, standalone, true));
}
EndDocument* XMLEventFactory::createEndDocument() { return located(new EndDocument()); }
XMLAttribute* XMLEventFactory::createAttribute(const String& localName, const String& value) {
    return located(new XMLAttribute(new QName(String(""), localName, String("")), value));
}
XMLAttribute* XMLEventFactory::createAttribute(QName* name, const String& value) {
    return located(new XMLAttribute(name, value));
}
XMLAttribute* XMLEventFactory::createAttribute(const String& prefix, const String& namespaceURI, const String& localName,
                                               const String& value) {
    return located(new XMLAttribute(new QName(namespaceURI, localName, prefix), value));
}
Namespace* XMLEventFactory::createNamespace(const String& namespaceURI) {
    return located(new Namespace(String(""), namespaceURI));
}
Namespace* XMLEventFactory::createNamespace(const String& prefix, const String& namespaceUri) {
    return located(new Namespace(prefix, namespaceUri));
}

}  // namespace jlang::xml
