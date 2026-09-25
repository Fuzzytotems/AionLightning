// jlang/src/xml_sax.cpp - org.xml.sax / javax.xml.parsers emulation (see <jlang/XmlEvents.h>).
#include "xml_internal.h"

#include <cstring>
#include <unistd.h>

namespace jlang::xml {

// =======================================================================================
// Exceptions

String SAXException::getMessage() {
    String m = Exception::getMessage();
    Throwable* cause = getCause();
    if (m.isNull() && cause != nullptr) return cause->getMessage();
    return m;
}

String SAXException::toString() {
    Throwable* cause = getCause();
    if (cause != nullptr) return Exception::toString() + "\n" + cause->toString();
    return Exception::toString();
}

static String locatorPublicId(Locator* l) { return l != nullptr ? l->getPublicId() : String(); }
static String locatorSystemId(Locator* l) { return l != nullptr ? l->getSystemId() : String(); }
static int32_t locatorLine(Locator* l) { return l != nullptr ? l->getLineNumber() : -1; }
static int32_t locatorColumn(Locator* l) { return l != nullptr ? l->getColumnNumber() : -1; }

SAXParseException::SAXParseException(const String& message, Locator* locator)
    : SAXException(message), publicId_(locatorPublicId(locator)), systemId_(locatorSystemId(locator)),
      line_(locatorLine(locator)), column_(locatorColumn(locator)) {}

SAXParseException::SAXParseException(const String& message, Locator* locator, Throwable* cause)
    : SAXException(message, cause), publicId_(locatorPublicId(locator)), systemId_(locatorSystemId(locator)),
      line_(locatorLine(locator)), column_(locatorColumn(locator)) {}

SAXParseException::SAXParseException(const String& message, const String& publicId, const String& systemId,
                                     int32_t lineNumber, int32_t columnNumber)
    : SAXException(message), publicId_(publicId), systemId_(systemId), line_(lineNumber), column_(columnNumber) {}

SAXParseException::SAXParseException(const String& message, const String& publicId, const String& systemId,
                                     int32_t lineNumber, int32_t columnNumber, Throwable* cause)
    : SAXException(message, cause), publicId_(publicId), systemId_(systemId), line_(lineNumber), column_(columnNumber) {}

String SAXParseException::toString() {
    // org.xml.sax.SAXParseException.toString (JDK 9+), including its missing "; " before publicId.
    std::string b(className());
    String message = getLocalizedMessage();
    if (!publicId_->isNull()) b += "publicId: " + std::string(*publicId_);
    if (!systemId_->isNull()) b += "; systemId: " + std::string(*systemId_);
    if (line_ != -1) b += "; lineNumber: " + std::to_string(line_);
    if (column_ != -1) b += "; columnNumber: " + std::to_string(column_);
    if (!message.isNull()) b += "; " + std::string(message);
    return String(b);
}

// =======================================================================================
// Attributes

String Attributes::getURI(int32_t i) { return i >= 0 && i < getLength() ? items_[static_cast<size_t>(i)].uri : String(); }
String Attributes::getLocalName(int32_t i) {
    return i >= 0 && i < getLength() ? items_[static_cast<size_t>(i)].localName : String();
}
String Attributes::getQName(int32_t i) { return i >= 0 && i < getLength() ? items_[static_cast<size_t>(i)].qName : String(); }
String Attributes::getType(int32_t i) { return i >= 0 && i < getLength() ? String("CDATA") : String(); }
String Attributes::getValue(int32_t i) { return i >= 0 && i < getLength() ? items_[static_cast<size_t>(i)].value : String(); }

int32_t Attributes::getIndex(const String& qName) {
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].qName.equals(qName)) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t Attributes::getIndex(const String& uri, const String& localName) {
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].uri.equals(uri) && items_[i].localName.equals(localName)) return static_cast<int32_t>(i);
    }
    return -1;
}

String Attributes::getType(const String& qName) { return getType(getIndex(qName)); }
String Attributes::getType(const String& uri, const String& localName) { return getType(getIndex(uri, localName)); }
String Attributes::getValue(const String& qName) { return getValue(getIndex(qName)); }
String Attributes::getValue(const String& uri, const String& localName) { return getValue(getIndex(uri, localName)); }

// =======================================================================================
// DefaultHandler

using StartElementRef = void (DefaultHandler::*)(const String&, const String&, const String&, Attributes*);
using StartElementVal = void (DefaultHandler::*)(String, String, String, Attributes*);
using EndElementRef = void (DefaultHandler::*)(const String&, const String&, const String&);
using EndElementVal = void (DefaultHandler::*)(String, String, String);

Object* DefaultHandler::resolveEntity(const String&, const String&) { return nullptr; }
void DefaultHandler::notationDecl(const String&, const String&, const String&) {}
void DefaultHandler::unparsedEntityDecl(const String&, const String&, const String&, const String&) {}
void DefaultHandler::setDocumentLocator(Locator*) {}
void DefaultHandler::startDocument() {}
void DefaultHandler::endDocument() {}
void DefaultHandler::startPrefixMapping(const String&, const String&) {}
void DefaultHandler::endPrefixMapping(const String&) {}
void DefaultHandler::startElement(const String& uri, const String& localName, const String& qName, Attributes* attributes) {
    (this->*static_cast<StartElementVal>(&DefaultHandler::startElement))(uri, localName, qName, attributes);
}
void DefaultHandler::startElement(String, String, String, Attributes*) {}
void DefaultHandler::endElement(const String& uri, const String& localName, const String& qName) {
    (this->*static_cast<EndElementVal>(&DefaultHandler::endElement))(uri, localName, qName);
}
void DefaultHandler::endElement(String, String, String) {}
void DefaultHandler::characters(Array<char16_t>*, int32_t, int32_t) {}
void DefaultHandler::ignorableWhitespace(Array<char16_t>*, int32_t, int32_t) {}
void DefaultHandler::processingInstruction(const String&, const String&) {}
void DefaultHandler::skippedEntity(const String&) {}
void DefaultHandler::warning(SAXParseException*) {}
void DefaultHandler::error(SAXParseException*) {}
void DefaultHandler::fatalError(SAXParseException* e) { throw *e; }

// =======================================================================================
// Parser

SAXParserFactory* SAXParserFactory::newInstance() { return new SAXParserFactory(); }

SAXParser* SAXParserFactory::newSAXParser() { return new SAXParser(namespaceAware_); }

namespace {

// The Locator handed to setDocumentLocator: positions are computed on demand from the source
// text (a GC object, so the locator stays usable after parse() returned).
class DocLocator final : public Locator {
public:
    DocLocator(detail::XmlSource* source, const String& systemId) : source_(source), systemId_(systemId) {}
    String getPublicId() override { return String(); }
    String getSystemId() override { return systemId_; }
    int32_t getLineNumber() override {
        compute();
        return pos_.line;
    }
    int32_t getColumnNumber() override {
        compute();
        return pos_.column;
    }
    void at(pugi::xml_node n) {
        ptrdiff_t off = n.offset_debug();
        nameOffset_ = off < 0 ? -1 : off;
        valid_ = false;
    }
    void atDocumentStart() {
        valid_ = true;
        pos_ = detail::Pos{1, 1, 0};
    }
    void atDocumentEnd() {
        valid_ = true;
        pos_ = detail::Pos{};
    }

private:
    void compute() {
        if (valid_) return;
        valid_ = true;
        pos_ = source_ != nullptr && nameOffset_ >= 0 ? source_->afterTag(static_cast<size_t>(nameOffset_)) : detail::Pos{};
    }
    detail::XmlSource* source_;
    String systemId_;
    ptrdiff_t nameOffset_ = -1;
    bool valid_ = false;
    detail::Pos pos_;
};

String fileUri(const std::string& path) {
    if (path.empty()) return String();
    if (path.rfind("file:", 0) == 0) return String(path);
    std::string abs = path;
    if (abs[0] != '/') {
        char buf[4096];
        if (::getcwd(buf, sizeof buf) != nullptr) abs = std::string(buf) + "/" + path;
    }
    return String("file:" + abs);
}

void callStart(DefaultHandler* h, const String& uri, const String& local, const String& q, Attributes* a) {
    (h->*static_cast<StartElementRef>(&DefaultHandler::startElement))(uri, local, q, a);
}

void callEnd(DefaultHandler* h, const String& uri, const String& local, const String& q) {
    (h->*static_cast<EndElementRef>(&DefaultHandler::endElement))(uri, local, q);
}

void characters(DefaultHandler* h, const char* text) {
    String s(text);
    Array<char16_t>* chars = s.toCharArray();
    h->characters(chars, 0, chars->length);
}

}  // namespace

void SAXParser::parseDoc(detail::XmlDoc* doc, DefaultHandler* handler) {
    if (handler == nullptr) handler = new DefaultHandler();
    String systemId = fileUri(doc->systemId);
    auto* locator = new DocLocator(doc->source(), systemId);
    handler->setDocumentLocator(locator);
    locator->atDocumentStart();
    handler->startDocument();

    struct NsDecl {
        std::string prefix, uri;
    };
    std::vector<NsDecl> decls;
    std::vector<size_t> marks;
    auto lookup = [&](std::string_view prefix) -> std::string {
        if (prefix == "xml") return std::string(XMLConstants::XML_NS_URI);
        for (size_t i = decls.size(); i-- > 0;) {
            if (decls[i].prefix == prefix) return decls[i].uri;
        }
        return std::string();
    };
    auto splitName = [&](std::string_view qn, bool isAttribute, String& uri, String& local) {
        size_t colon = qn.find(':');
        if (colon == std::string_view::npos) {
            uri = String(isAttribute ? std::string() : lookup(""));
            local = String(qn);
        } else {
            uri = String(lookup(qn.substr(0, colon)));
            local = String(qn.substr(colon + 1));
        }
    };

    // Iterative pre/post-order walk.
    pugi::xml_node n = doc->doc.first_child();
    bool leaving = false;
    const String empty("");
    while (n) {
        if (!leaving) {
            switch (n.type()) {
                case pugi::node_element: {
                    std::vector<Attributes::Item> items;
                    std::string_view qn(n.name());
                    if (namespaceAware_) {
                        marks.push_back(decls.size());
                        for (pugi::xml_attribute a = n.first_attribute(); a; a = a.next_attribute()) {
                            std::string_view an(a.name());
                            if (an == "xmlns") {
                                decls.push_back(NsDecl{std::string(), a.value()});
                            } else if (an.substr(0, 6) == "xmlns:") {
                                decls.push_back(NsDecl{std::string(an.substr(6)), a.value()});
                            }
                        }
                        for (size_t i = marks.back(); i < decls.size(); i++) {
                            handler->startPrefixMapping(String(decls[i].prefix), String(decls[i].uri));
                        }
                        for (pugi::xml_attribute a = n.first_attribute(); a; a = a.next_attribute()) {
                            std::string_view an(a.name());
                            if (an == "xmlns" || an.substr(0, 6) == "xmlns:") continue;
                            Attributes::Item it;
                            splitName(an, true, it.uri, it.localName);
                            it.qName = String(an);
                            it.value = String(a.value());
                            items.push_back(std::move(it));
                        }
                    } else {
                        for (pugi::xml_attribute a = n.first_attribute(); a; a = a.next_attribute()) {
                            Attributes::Item it;
                            it.uri = empty;
                            it.localName = String(a.name());
                            it.qName = it.localName;
                            it.value = String(a.value());
                            items.push_back(std::move(it));
                        }
                    }
                    auto* attrs = new Attributes(std::move(items));
                    locator->at(n);
                    if (namespaceAware_) {
                        String uri, local;
                        splitName(qn, false, uri, local);
                        callStart(handler, uri, local, String(qn), attrs);
                    } else {
                        callStart(handler, empty, empty, String(qn), attrs);
                    }
                    if (n.first_child()) {
                        n = n.first_child();
                        continue;
                    }
                    leaving = true;
                    continue;
                }
                case pugi::node_pcdata:
                case pugi::node_cdata:
                    if (n.parent().type() != pugi::node_document) characters(handler, n.value());
                    break;
                case pugi::node_pi:
                    handler->processingInstruction(String(n.name()), String(n.value()));
                    break;
                default:
                    break;
            }
        } else {
            // End of element n.
            locator->at(n);
            std::string_view qn(n.name());
            if (namespaceAware_) {
                String uri, local;
                splitName(qn, false, uri, local);
                callEnd(handler, uri, local, String(qn));
                for (size_t i = decls.size(); i-- > marks.back();) handler->endPrefixMapping(String(decls[i].prefix));
                decls.resize(marks.back());
                marks.pop_back();
            } else {
                callEnd(handler, empty, empty, String(qn));
            }
        }
        // advance
        if (pugi::xml_node next = n.next_sibling()) {
            n = next;
            leaving = false;
        } else {
            n = n.parent();
            if (!n || n.type() == pugi::node_document) break;
            leaving = true;
        }
    }
    locator->atDocumentEnd();
    handler->endDocument();
}

static void parseBytes(detail::XmlDoc* doc, detail::Bytes bytes, DefaultHandler* handler) {
    detail::XmlDoc::Error err;
    if (!doc->parseWithSource(bytes.data, bytes.size, detail::kSaxParse, &err)) {
        SAXParseException e(String(err.message), String(), fileUri(doc->systemId), err.line, err.column);
        if (handler != nullptr) handler->fatalError(&e);
        throw e;
    }
}

void SAXParser::parse(File* f, DefaultHandler* handler) {
    if (f == nullptr) throw ::jlang::IllegalArgumentException("File cannot be null");
    parse(String(detail::filePath(f)), handler);
}

void SAXParser::parse(const String& uri, DefaultHandler* handler) {
    if (uri.isNull()) throw ::jlang::IllegalArgumentException("uri cannot be null");
    std::string path(uri);
    if (path.rfind("file://", 0) == 0) {
        path = path.substr(7);
    } else if (path.rfind("file:", 0) == 0) {
        path = path.substr(5);
    }
    detail::XmlDoc doc;
    doc.systemId = path;
    parseBytes(&doc, detail::readFile(path), handler);
    parseDoc(&doc, handler);
}

void SAXParser::parse(InputStream* in, DefaultHandler* handler) {
    if (in == nullptr) throw ::jlang::IllegalArgumentException("InputStream cannot be null");
    detail::XmlDoc doc;
    parseBytes(&doc, detail::readStream(in), handler);
    parseDoc(&doc, handler);
}

void SAXParser::parseString(std::string_view xml, DefaultHandler* handler, const String& systemId) {
    detail::XmlDoc doc;
    if (!systemId.isNull()) doc.systemId = std::string(systemId);
    auto* data = static_cast<char*>(std::malloc(xml.size() + 1));
    if (data == nullptr) throw std::bad_alloc();
    std::memcpy(data, xml.data(), xml.size());
    data[xml.size()] = 0;
    parseBytes(&doc, detail::Bytes{data, xml.size()}, handler);
    parseDoc(&doc, handler);
}

}  // namespace jlang::xml
