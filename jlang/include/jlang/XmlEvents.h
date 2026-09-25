// jlang/XmlEvents.h - event-based XML APIs: StAX (javax.xml.stream, javax.xml.namespace.QName)
// and SAX (javax.xml.parsers.SAXParser*, org.xml.sax.*), as used by XmlMerger, IPConfig and
// the admin scripts. Translated code includes <jlang/Xml.h>.
//
// Both are backed by pugixml: the whole document is parsed first (malformed input fails before
// any event is delivered), then events are produced from the tree, lazily for StAX.
//
// ---- StAX event sequence (matches the JDK's built-in SJSXP reader with default properties)
//   * StartDocument first, EndDocument last. Whitespace outside the root element produces no
//     events; comments and processing instructions do.
//   * Character data: one Characters event per text run, and every entity or character
//     reference (&amp; &lt; &#10; ...) is an event of its own ("a &amp; b" -> "a ", "&", " b";
//     "&lt; &gt;" -> "<", " ", ">"). CDATA sections are separate Characters events that report
//     isCData() == false (the JDK reports them as CHARACTERS). isWhiteSpace() is true when all
//     characters are ' ', '\t', '\n' or '\r'. isIgnorableWhiteSpace() is always false (no DTD).
//     Not emulated: the JDK also splits text at its internal 8K buffer boundaries and before a
//     lone '\r'.
//   * StartElement.getAttributes() iterates in java.util.HashMap order of the attribute QNames
//     (the JDK keeps them in a HashMap), NOT document order; namespace declarations are
//     separate (getNamespaces(), document order). This makes XmlMerger output byte-identical.
//   * A reader created from a Reader reports getCharacterEncodingScheme() == null (like the
//     JDK); from an InputStream it reports the declared encoding.
//   * getLocation() of a StartElement is the position just after its '>' (line, column
//     1-based; character offset 0-based, counted in bytes of UTF-8 here). Other reader events
//     report -1 positions.
//   * The parsed document is released when EndDocument is returned or on close(); a reader
//     that is abandoned before either keeps its (malloc'ed) tree: there are no finalizers.
//
// ---- XMLEventWriter output (JDK XMLStreamWriterImpl, non-repairing, byte for byte)
//   * StartDocument: `<?xml version="1.0"?>` (+ ` encoding="X"` when the event has an
//     encoding, + ` standalone="yes|no"` when it was declared); with neither version, encoding
//     nor standalone: `<?xml version="1.0" ?>`. No newline after it.
//   * Start tags stay open until the next event; an EndElement right after its StartElement
//     gives `<a></a>` (never `<a/>`). EndElement writes the name of the innermost open element.
//   * Text escapes & < > (&amp; &lt; &gt;); attribute values also " (&quot;); everything else
//     (', \t, \r, \n, non-ASCII) is written raw. Comments `<!--text-->`, PIs `<?t data?>`.
//   * close() flushes the underlying jlang::Writer but does not close it (JDK behavior).
//
// ---- SAX (JDK default SAXParserFactory: not namespace aware)
//   * setDocumentLocator, startDocument, startElement("", "", qName, attributes) with xmlns*
//     attributes included in document order, characters (one call per text node / CDATA
//     section, as UTF-16 chars), endElement, processingInstruction, endDocument. Comments are
//     not reported (no LexicalHandler).
//   * With setNamespaceAware(true): uri/localName are filled, xmlns* attributes are removed and
//     reported through startPrefixMapping/endPrefixMapping.
//   * Locator during startElement: line/column just after the start tag's '>' (as Xerces);
//     during endElement it still reports the start tag's position (Xerces: the end tag's).
//     Xerces also splits characters() at entity references; here one call per text node.
//   * A missing file throws jlang::FileNotFoundException; malformed XML calls
//     handler->fatalError(e) and then throws the SAXParseException.
//
// Helper and event classes derive from jlang::Object (they are Java objects). The jdkmap maps
// javax.xml.stream.events.{StartElement,EndElement,Characters,Comment} to XMLEvent*, so XMLEvent
// itself declares every accessor (they throw ClassCastException on events of another kind);
// the subclasses exist so `instanceof Comment` can be translated as
// jlang::instanceof<jlang::xml::Comment>(e).
#pragma once

#include <jlang/jlang.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jlang::xml {

namespace detail {
class XmlDoc;
struct XmlSource;
struct StaxCursor;
struct EventWriterState;
}  // namespace detail

class QName;
class Location;
class Locator;
class XMLEvent;
class XMLAttribute;
class Namespace;
class StartElement;
class EndElement;
class Characters;
class Comment;
class StartDocument;
class EndDocument;
class ProcessingInstruction;
class DTD;

// =======================================================================================
// Exceptions

// org.xml.sax.SAXException (getException() == getCause()).
class SAXException : public ::jlang::Exception {
public:
    SAXException() {}
    explicit SAXException(const String& message) : Exception(message) {}
    explicit SAXException(const char* message) : Exception(String(message)) {}
    SAXException(const String& message, const Throwable& cause) : Exception(message, cause) {}
    SAXException(const String& message, Throwable* cause) : Exception(message, cause) {}
    // Java SAXException(Exception e): message null (getMessage() then returns the cause's).
    explicit SAXException(Throwable* cause) : Exception(String(), cause) {}
    template<class E>
        requires(std::is_base_of_v<Throwable, E> && !std::is_base_of_v<SAXException, E>)
    explicit SAXException(const E& cause) : Exception(String(), static_cast<const Throwable&>(cause)) {}

    // Java: the own message, or the embedded exception's message when there is none.
    String getMessage() override;
    Throwable* getException() { return getCause(); }
    String toString() override;
    String className() const override { return "org.xml.sax.SAXException"; }
    JLANG_THROWABLE(SAXException)
};

// org.xml.sax.SAXParseException
class SAXParseException : public SAXException {
public:
    // Copies publicId, systemId, line and column from the locator (null locator: -1/-1).
    SAXParseException(const String& message, Locator* locator);
    SAXParseException(const String& message, Locator* locator, Throwable* cause);
    SAXParseException(const String& message, const String& publicId, const String& systemId,
                      int32_t lineNumber, int32_t columnNumber);
    SAXParseException(const String& message, const String& publicId, const String& systemId,
                      int32_t lineNumber, int32_t columnNumber, Throwable* cause);

    String getPublicId() { return *publicId_; }
    String getSystemId() { return *systemId_; }
    int32_t getLineNumber() { return line_; }
    int32_t getColumnNumber() { return column_; }
    // "org.xml.sax.SAXParseException; systemId: X; lineNumber: N; columnNumber: N; message"
    String toString() override;
    String className() const override { return "org.xml.sax.SAXParseException"; }
    JLANG_THROWABLE(SAXParseException)

private:
    ::jlang::detail::Pinned<String> publicId_;
    ::jlang::detail::Pinned<String> systemId_;
    int32_t line_ = -1;
    int32_t column_ = -1;
};

// javax.xml.stream.XMLStreamException
class XMLStreamException : public ::jlang::Exception {
public:
    XMLStreamException() {}
    explicit XMLStreamException(const String& message) : Exception(message) {}
    explicit XMLStreamException(const char* message) : Exception(String(message)) {}
    XMLStreamException(const String& message, const Throwable& cause) : Exception(message, cause) {}
    XMLStreamException(const String& message, Throwable* cause) : Exception(message, cause) {}
    explicit XMLStreamException(Throwable* cause) : Exception(String(), cause) {}
    template<class E>
        requires(std::is_base_of_v<Throwable, E> && !std::is_base_of_v<XMLStreamException, E>)
    explicit XMLStreamException(const E& cause) : Exception(String(), static_cast<const Throwable&>(cause)) {}
    // Java: message = "ParseError at [row,col]:[L,C]\nMessage: " + msg.
    XMLStreamException(const String& message, Location* location);
    XMLStreamException(const String& message, Location* location, Throwable* cause);

    Location* getLocation() { return *location_; }
    Throwable* getNestedException() { return getCause(); }
    String className() const override { return "javax.xml.stream.XMLStreamException"; }
    JLANG_THROWABLE(XMLStreamException)

private:
    ::jlang::detail::Pinned<Location*> location_{nullptr};
};

// =======================================================================================
// javax.xml.namespace.QName
class QName : public virtual Object {
public:
    // IllegalArgumentException for a null local part (Java); null namespace URI -> "".
    explicit QName(const String& localPart);
    QName(const String& namespaceURI, const String& localPart);
    QName(const String& namespaceURI, const String& localPart, const String& prefix);

    String getLocalPart() { return localPart_; }
    String getNamespaceURI() { return namespaceURI_; }
    String getPrefix() { return prefix_; }
    // Namespace URI and local part (not the prefix), as in Java.
    bool equals(Object* o) override;
    int32_t hashCode() override;  // namespaceURI.hashCode() ^ localPart.hashCode() (cached)
    String toString() override;   // "local" or "{uri}local"
    static QName* valueOf(const String& qNameAsString);

    // jlang: copy-free access (QName is immutable).
    const String& localPartRef() const noexcept { return localPart_; }
    const String& namespaceURIRef() const noexcept { return namespaceURI_; }
    const String& prefixRef() const noexcept { return prefix_; }

private:
    String namespaceURI_;
    String localPart_;
    String prefix_;
    int32_t hash_ = 0;
    bool hashed_ = false;
};

// =======================================================================================
// javax.xml.stream.Location (also used as the SAX Locator snapshot of events).
class Location : public virtual Object {
public:
    Location(int32_t line, int32_t column, int32_t offset, const String& publicId, const String& systemId)
        : line_(line), column_(column), offset_(offset), publicId_(publicId), systemId_(systemId) {}
    virtual int32_t getLineNumber() { return line_; }
    virtual int32_t getColumnNumber() { return column_; }
    virtual int32_t getCharacterOffset() { return offset_; }
    virtual String getPublicId() { return publicId_; }
    virtual String getSystemId() { return systemId_; }

private:
    int32_t line_, column_, offset_;
    String publicId_, systemId_;
};

// javax.xml.stream.XMLStreamConstants
class XMLStreamConstants {
public:
    static constexpr int32_t START_ELEMENT = 1;
    static constexpr int32_t END_ELEMENT = 2;
    static constexpr int32_t PROCESSING_INSTRUCTION = 3;
    static constexpr int32_t CHARACTERS = 4;
    static constexpr int32_t COMMENT = 5;
    static constexpr int32_t SPACE = 6;
    static constexpr int32_t START_DOCUMENT = 7;
    static constexpr int32_t END_DOCUMENT = 8;
    static constexpr int32_t ENTITY_REFERENCE = 9;
    static constexpr int32_t ATTRIBUTE = 10;
    static constexpr int32_t DTD = 11;
    static constexpr int32_t CDATA = 12;
    static constexpr int32_t NAMESPACE = 13;
    static constexpr int32_t NOTATION_DECLARATION = 14;
    static constexpr int32_t ENTITY_DECLARATION = 15;
};

// =======================================================================================
// javax.xml.stream.events.*
class XMLEvent : public virtual Object {
public:
    int32_t getEventType() { return type_; }
    // Reader events: a Location (-1 values where unknown); factory events: the location set
    // on the factory (null by default).
    Location* getLocation();
    void setLocation(Location* l) { location_ = l; }
    void setPosition(int32_t line, int32_t column, int32_t offset) {
        line_ = line;
        column_ = column;
        offset_ = offset;
        hasPosition_ = true;
    }
    // Position computed on demand from the source text: just after the '>' of the tag whose
    // name starts at nameOffset.
    void setTagPosition(detail::XmlSource* source, int32_t nameOffset) {
        source_ = source;
        offset_ = nameOffset;
        hasPosition_ = true;
    }

    bool isStartElement() { return type_ == XMLStreamConstants::START_ELEMENT; }
    bool isEndElement() { return type_ == XMLStreamConstants::END_ELEMENT; }
    bool isCharacters() { return type_ == XMLStreamConstants::CHARACTERS; }
    bool isStartDocument() { return type_ == XMLStreamConstants::START_DOCUMENT; }
    bool isEndDocument() { return type_ == XMLStreamConstants::END_DOCUMENT; }
    bool isAttribute() { return type_ == XMLStreamConstants::ATTRIBUTE; }
    bool isNamespace() { return type_ == XMLStreamConstants::NAMESPACE; }
    bool isProcessingInstruction() { return type_ == XMLStreamConstants::PROCESSING_INSTRUCTION; }
    bool isEntityReference() { return type_ == XMLStreamConstants::ENTITY_REFERENCE; }
    // ClassCastException when the event is of another kind (Java's cast semantics).
    StartElement* asStartElement();
    EndElement* asEndElement();
    Characters* asCharacters();
    QName* getSchemaType() { return nullptr; }

    // ---- accessors of the subinterfaces (see the header comment)
    // StartElement / EndElement / Attribute / Namespace
    virtual QName* getName();
    // StartElement: attributes (XMLAttribute*) / namespaces (Namespace*), as a raw Iterator.
    virtual Iterator<Object*>* getAttributes();
    virtual Iterator<Object*>* getNamespaces();
    virtual XMLAttribute* getAttributeByName(QName* name);
    virtual String getNamespaceURI(const String& prefix);
    // Characters
    virtual String getData();
    virtual bool isWhiteSpace();
    virtual bool isIgnorableWhiteSpace();
    virtual bool isCData();
    // Comment / DTD / ProcessingInstruction
    virtual String getText();
    virtual String getTarget();
    // Attribute
    virtual String getValue();
    // StartDocument
    virtual String getCharacterEncodingScheme();
    virtual bool encodingSet();
    virtual bool isStandalone();
    virtual bool standaloneSet();
    virtual String getVersion();
    virtual String getSystemId();

    // Java's event toString(): the event as XML text ("<a b='c'>", "</a>", the characters...).
    String toString() override;

protected:
    explicit XMLEvent(int32_t type) : type_(type) {}
    [[noreturn]] void wrongKind(const char* what);

private:
    int32_t type_;
    int32_t line_ = -1, column_ = -1, offset_ = -1;
    bool hasPosition_ = false;
    detail::XmlSource* source_ = nullptr;
    Location* location_ = nullptr;
};

class XMLAttribute : public XMLEvent {  // javax.xml.stream.events.Attribute
public:
    XMLAttribute(QName* name, const String& value) : XMLAttribute(XMLStreamConstants::ATTRIBUTE, name, value) {}
    QName* getName() override { return name_; }
    String getValue() override { return value_; }
    String getDTDType() { return "CDATA"; }
    bool isSpecified() { return true; }
    const String& valueRef() const noexcept { return value_; }

protected:
    XMLAttribute(int32_t type, QName* name, const String& value) : XMLEvent(type), name_(name), value_(value) {}

private:
    QName* name_;
    String value_;
};

class Namespace : public XMLAttribute {  // javax.xml.stream.events.Namespace
public:
    // prefix "" = default namespace declaration (xmlns="uri").
    Namespace(const String& prefix, const String& namespaceURI);
    String getPrefix() { return prefix_; }
    const String& prefixRef() const noexcept { return prefix_; }
    String getNamespaceURI() { return uri_; }
    String getNamespaceURI(const String&) override { return uri_; }
    bool isDefaultNamespaceDeclaration() { return prefix_.isEmpty(); }

private:
    String prefix_;
    String uri_;
};

class StartElement : public XMLEvent {
public:
    // Attributes are kept in the order the JDK's HashMap iterates them (see the header comment).
    StartElement(QName* name, std::vector<XMLAttribute*> attributes, std::vector<Namespace*> namespaces);
    QName* getName() override { return name_; }
    Iterator<Object*>* getAttributes() override;
    Iterator<Object*>* getNamespaces() override;
    XMLAttribute* getAttributeByName(QName* name) override;
    String getNamespaceURI(const String& prefix) override;
    const std::vector<XMLAttribute*>& attributeList() const noexcept { return attributes_; }
    const std::vector<Namespace*>& namespaceList() const noexcept { return namespaces_; }

    // Reorders attributes into java.util.HashMap<QName, Attribute> iteration order (insertion
    // order within a bucket). Called by the constructor.
    static void hashMapOrder(std::vector<XMLAttribute*>& attributes);

private:
    QName* name_;
    std::vector<XMLAttribute*> attributes_;
    std::vector<Namespace*> namespaces_;
};

class EndElement : public XMLEvent {
public:
    EndElement(QName* name, std::vector<Namespace*> namespaces)
        : XMLEvent(XMLStreamConstants::END_ELEMENT), name_(name), namespaces_(std::move(namespaces)) {}
    QName* getName() override { return name_; }
    Iterator<Object*>* getNamespaces() override;

private:
    QName* name_;
    std::vector<Namespace*> namespaces_;
};

class Characters : public XMLEvent {
public:
    Characters(const String& data, bool cdata, bool ignorable = false);
    String getData() override { return data_; }
    const String& dataRef() const noexcept { return data_; }
    bool isWhiteSpace() override { return whiteSpace_; }
    bool isIgnorableWhiteSpace() override { return ignorable_; }
    bool isCData() override { return cdata_; }

private:
    String data_;
    bool cdata_;
    bool ignorable_;
    bool whiteSpace_;
};

class Comment : public XMLEvent {
public:
    explicit Comment(const String& text) : XMLEvent(XMLStreamConstants::COMMENT), text_(text) {}
    String getText() override { return text_; }

private:
    String text_;
};

class ProcessingInstruction : public XMLEvent {
public:
    ProcessingInstruction(const String& target, const String& data)
        : XMLEvent(XMLStreamConstants::PROCESSING_INSTRUCTION), target_(target), data_(data) {}
    String getTarget() override { return target_; }
    String getData() override { return data_; }

private:
    String target_;
    String data_;
};

class DTD : public XMLEvent {
public:
    explicit DTD(const String& text) : XMLEvent(XMLStreamConstants::DTD), text_(text) {}
    String getDocumentTypeDeclaration() { return text_; }
    String getText() override { return text_; }

private:
    String text_;
};

class StartDocument : public XMLEvent {
public:
    StartDocument(const String& encoding, bool encodingSet, const String& version, bool standalone,
                  bool standaloneSet, const String& systemId = String())
        : XMLEvent(XMLStreamConstants::START_DOCUMENT), encoding_(encoding), version_(version),
          systemId_(systemId), encodingSet_(encodingSet), standalone_(standalone), standaloneSet_(standaloneSet) {}
    String getCharacterEncodingScheme() override { return encoding_; }
    bool encodingSet() override { return encodingSet_; }
    bool isStandalone() override { return standalone_; }
    bool standaloneSet() override { return standaloneSet_; }
    String getVersion() override { return version_; }
    String getSystemId() override { return systemId_; }

private:
    String encoding_, version_, systemId_;
    bool encodingSet_, standalone_, standaloneSet_;
};

class EndDocument : public XMLEvent {
public:
    EndDocument() : XMLEvent(XMLStreamConstants::END_DOCUMENT) {}
};

// =======================================================================================
// javax.xml.stream.XMLEventReader
class XMLEventReader : public virtual Object {
public:
    bool hasNext();
    XMLEvent* nextEvent();  // NoSuchElementException after EndDocument
    XMLEvent* peek();       // null at the end
    Object* next() { return nextEvent(); }
    // Concatenated text up to the matching end tag (current event must be a StartElement).
    String getElementText();
    // Skips whitespace, comments and PIs up to the next start/end tag.
    XMLEvent* nextTag();
    Object* getProperty(const String& name);
    // Frees the parsed document. Also happens automatically once EndDocument was returned.
    void close();

    // Implementation (use XMLInputFactory).
    XMLEventReader(detail::XmlDoc* doc, bool reportEncoding);

private:
    XMLEvent* produce();
    detail::StaxCursor* cursor_;
    XMLEvent* peeked_ = nullptr;
};

// javax.xml.stream.XMLEventWriter
class XMLEventWriter : public virtual Object {
public:
    explicit XMLEventWriter(::jlang::Writer* out);
    void add(XMLEvent* event);
    void add(XMLEventReader* reader);  // all remaining events
    void flush();
    void close();  // flushes the jlang::Writer, does not close it
    String getPrefix(const String& uri);
    void setPrefix(const String& prefix, const String& uri);
    void setDefaultNamespace(const String& uri);

    // Output not yet flushed to the jlang::Writer (all of it for createXMLEventWriterToString()).
    std::string& pending();

private:
    void writeStartTag(QName* name, const std::vector<Namespace*>& ns, const std::vector<XMLAttribute*>& attrs);
    detail::EventWriterState* st_;
    ::jlang::Writer* out_;
};

// javax.xml.stream.XMLInputFactory
class XMLInputFactory : public virtual Object {
public:
    static inline const String IS_NAMESPACE_AWARE = "javax.xml.stream.isNamespaceAware";
    static inline const String IS_VALIDATING = "javax.xml.stream.isValidating";
    static inline const String IS_COALESCING = "javax.xml.stream.isCoalescing";
    static inline const String IS_REPLACING_ENTITY_REFERENCES = "javax.xml.stream.isReplacingEntityReferences";
    static inline const String IS_SUPPORTING_EXTERNAL_ENTITIES = "javax.xml.stream.isSupportingExternalEntities";
    static inline const String SUPPORT_DTD = "javax.xml.stream.supportDTD";

    static XMLInputFactory* newInstance();
    static XMLInputFactory* newFactory() { return newInstance(); }
    // The whole input is read and parsed here; XMLStreamException on malformed XML.
    XMLEventReader* createXMLEventReader(Reader* reader);
    XMLEventReader* createXMLEventReader(InputStream* stream);
    // jlang extensions (tests, hand-written code): like a Reader over the file's UTF-8 text /
    // over the given text (getCharacterEncodingScheme() == null).
    XMLEventReader* createXMLEventReaderFromFile(const String& path);
    XMLEventReader* createXMLEventReaderFromString(std::string_view xml);
    // Accepted and ignored (only the defaults are implemented).
    void setProperty(const String& name, Object* value) { (void)name, (void)value; }
    bool isPropertySupported(const String& name) { (void)name; return false; }
};

// javax.xml.stream.XMLOutputFactory
class XMLOutputFactory : public virtual Object {
public:
    static inline const String IS_REPAIRING_NAMESPACES = "javax.xml.stream.isRepairingNamespaces";
    static XMLOutputFactory* newInstance();
    static XMLOutputFactory* newFactory() { return newInstance(); }
    XMLEventWriter* createXMLEventWriter(::jlang::Writer* writer);
    // jlang extension: collects the output in memory (XMLEventWriter::pending()).
    XMLEventWriter* createXMLEventWriterToString();
    void setProperty(const String& name, Object* value) { (void)name, (void)value; }
    bool isPropertySupported(const String& name) { (void)name; return false; }
};

// javax.xml.stream.XMLEventFactory
class XMLEventFactory : public virtual Object {
public:
    static XMLEventFactory* newInstance();
    static XMLEventFactory* newFactory() { return newInstance(); }
    void setLocation(Location* location) { location_ = location; }

    // attributes: Iterator of XMLAttribute* (null = none); namespaces: Iterator of Namespace*
    // (null = none).
    StartElement* createStartElement(QName* name, Iterator<Object*>* attributes, Iterator<Object*>* namespaces);
    StartElement* createStartElement(const String& prefix, const String& namespaceUri, const String& localName);
    EndElement* createEndElement(QName* name, Iterator<Object*>* namespaces);
    EndElement* createEndElement(const String& prefix, const String& namespaceUri, const String& localName);
    Characters* createCharacters(const String& content);
    Characters* createCData(const String& content);
    Characters* createSpace(const String& content);
    Characters* createIgnorableSpace(const String& content);
    Comment* createComment(const String& text);
    ProcessingInstruction* createProcessingInstruction(const String& target, const String& data);
    DTD* createDTD(const String& dtd);
    StartDocument* createStartDocument();
    StartDocument* createStartDocument(const String& encoding);
    StartDocument* createStartDocument(const String& encoding, const String& version);
    StartDocument* createStartDocument(const String& encoding, const String& version, bool standalone);
    EndDocument* createEndDocument();
    XMLAttribute* createAttribute(const String& localName, const String& value);
    XMLAttribute* createAttribute(QName* name, const String& value);
    XMLAttribute* createAttribute(const String& prefix, const String& namespaceURI, const String& localName,
                                  const String& value);
    Namespace* createNamespace(const String& namespaceURI);
    Namespace* createNamespace(const String& prefix, const String& namespaceUri);

private:
    template<class E>
    E* located(E* e) {
        e->setLocation(location_);
        return e;
    }
    Location* location_ = nullptr;
};

// =======================================================================================
// SAX

// org.xml.sax.Locator
class Locator : public virtual Object {
public:
    virtual String getPublicId() = 0;
    virtual String getSystemId() = 0;
    virtual int32_t getLineNumber() = 0;
    virtual int32_t getColumnNumber() = 0;
};

// org.xml.sax.Attributes (the parser's implementation; index-based and by qualified name).
class Attributes : public virtual Object {
public:
    struct Item {
        String uri, localName, qName, value;
    };
    Attributes() = default;
    explicit Attributes(std::vector<Item> items) : items_(std::move(items)) {}

    int32_t getLength() { return static_cast<int32_t>(items_.size()); }
    String getURI(int32_t index);        // null String when out of range (Java)
    String getLocalName(int32_t index);
    String getQName(int32_t index);
    String getType(int32_t index);       // "CDATA"
    String getValue(int32_t index);
    int32_t getIndex(const String& qName);
    int32_t getIndex(const String& uri, const String& localName);
    String getType(const String& qName);
    String getType(const String& uri, const String& localName);
    String getValue(const String& qName);  // null String when absent
    String getValue(const String& uri, const String& localName);
    String getValue(const char* qName) { return getValue(String(qName)); }

    std::vector<Item>& items() noexcept { return items_; }

private:
    std::vector<Item> items_;
};

// org.xml.sax.helpers.DefaultHandler: every callback is a no-op except fatalError, which
// throws the exception. startElement/endElement exist with `const String&` and by-value String
// parameters so that an override with either signature is called (the parser calls the
// const& version, whose default forwards to the by-value one).
class DefaultHandler : public virtual Object {
public:
    // EntityResolver / DTDHandler (InputSource is not modelled: always null)
    virtual Object* resolveEntity(const String& publicId, const String& systemId);
    virtual void notationDecl(const String& name, const String& publicId, const String& systemId);
    virtual void unparsedEntityDecl(const String& name, const String& publicId, const String& systemId,
                                    const String& notationName);
    // ContentHandler
    virtual void setDocumentLocator(Locator* locator);
    virtual void startDocument();
    virtual void endDocument();
    virtual void startPrefixMapping(const String& prefix, const String& uri);
    virtual void endPrefixMapping(const String& prefix);
    virtual void startElement(const String& uri, const String& localName, const String& qName, Attributes* attributes);
    virtual void startElement(String uri, String localName, String qName, Attributes* attributes);
    virtual void endElement(const String& uri, const String& localName, const String& qName);
    virtual void endElement(String uri, String localName, String qName);
    virtual void characters(Array<char16_t>* ch, int32_t start, int32_t length);
    virtual void ignorableWhitespace(Array<char16_t>* ch, int32_t start, int32_t length);
    virtual void processingInstruction(const String& target, const String& data);
    virtual void skippedEntity(const String& name);
    // ErrorHandler (exception parameters are pointers, as the jdkmap declares exception types)
    virtual void warning(SAXParseException* e);
    virtual void error(SAXParseException* e);
    virtual void fatalError(SAXParseException* e);  // throws *e
};

// javax.xml.parsers.SAXParserFactory
class SAXParserFactory : public virtual Object {
public:
    static SAXParserFactory* newInstance();
    SAXParser* newSAXParser();
    void setNamespaceAware(bool v) { namespaceAware_ = v; }
    bool isNamespaceAware() { return namespaceAware_; }
    void setValidating(bool v) { validating_ = v; }  // ignored (no DTD validation)
    bool isValidating() { return validating_; }
    void setFeature(const String& name, bool value) { (void)name, (void)value; }
    bool getFeature(const String& name) { (void)name; return false; }

private:
    bool namespaceAware_ = false;
    bool validating_ = false;
};

// javax.xml.parsers.SAXParser
class SAXParser : public virtual Object {
public:
    explicit SAXParser(bool namespaceAware) : namespaceAware_(namespaceAware) {}
    void parse(File* f, DefaultHandler* handler);
    void parse(InputStream* in, DefaultHandler* handler);
    void parse(const String& uri, DefaultHandler* handler);  // a file path or file: URI
    bool isNamespaceAware() { return namespaceAware_; }
    bool isValidating() { return false; }

    // Parses XML text (tests / hand-written code); systemId is used in messages.
    void parseString(std::string_view xml, DefaultHandler* handler, const String& systemId = String());

private:
    void parseDoc(detail::XmlDoc* doc, DefaultHandler* handler);
    bool namespaceAware_;
};

}  // namespace jlang::xml
