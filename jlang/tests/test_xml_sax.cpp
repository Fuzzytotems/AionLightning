// Tests for the SAX emulation (jlang/XmlEvents.h). Expected event sequences and locator positions
// were observed with the JDK's default SAXParserFactory (Xerces, not namespace aware).
#include "jtest.h"

#include <jlang/IO.h>
#include <jlang/Xml.h>

using namespace jlang;
using namespace jlang::xml;

namespace {

std::string testDir() {
    std::string f = __FILE__;
    return f.substr(0, f.rfind('/'));
}
std::string dataFile(const std::string& rel) { return testDir() + "/data/xml/" + rel; }

// Records events like the Java probe did (overrides with const String& parameters).
class Recorder : public DefaultHandler {
public:
    std::string log;
    Locator* locator = nullptr;
    void setDocumentLocator(Locator* l) override {
        locator = l;
        log += "setLocator ";
    }
    void startDocument() override { log += "startDoc@" + pos() + " "; }
    void endDocument() override { log += "endDoc@" + pos() + " "; }
    void startElement(const String& uri, const String& localName, const String& qName, Attributes* at) override {
        log += "S[" + std::string(uri) + "|" + std::string(localName) + "|" + std::string(qName) + "]@" + pos();
        for (int32_t i = 0; i < at->getLength(); i++) {
            log += " " + std::string(at->getQName(i)) + "(" + std::string(at->getLocalName(i)) + "," + std::string(at->getURI(i)) +
                   "," + std::string(at->getType(i)) + ")=" + std::string(at->getValue(i));
        }
        String x = at->getValue("x");
        log += " getValue(x)=" + (x.isNull() ? std::string("null") : std::string(x)) + " ";
    }
    void endElement(const String&, const String&, const String& qName) override { log += "E[" + std::string(qName) + "] "; }
    void characters(Array<char16_t>* ch, int32_t start, int32_t length) override {
        std::string s(String(ch, start, length));
        for (char& c : s) c = c == '\n' ? '|' : c;
        log += "C(" + s + ") ";
    }
    void processingInstruction(const String& target, const String& data) override {
        log += "PI(" + std::string(target) + "," + std::string(data) + ") ";
    }

private:
    std::string pos() { return std::to_string(locator->getLineNumber()) + ":" + std::to_string(locator->getColumnNumber()); }
};

// IPConfig-style anonymous handler whose override takes Strings by value.
class ByValue : public DefaultHandler {
public:
    std::vector<std::string> seen;
    void startElement(String uri, String localName, String qName, Attributes* attributes) override {
        (void)uri, (void)localName;
        if (qName.equals("iprange")) seen.push_back(std::string(attributes->getValue("min")) + "-" + std::string(attributes->getValue("max")));
    }
};

// gameserver XmlMerger.TimeCheckerHandler, translated.
class TimeCheckerHandler : public DefaultHandler {
public:
    std::vector<std::string> imports;
    void setDocumentLocator(Locator* l) override { locator = l; }
    void startElement(const String& uri, const String& localName, const String& qName, Attributes* attributes) override {
        (void)uri, (void)localName;
        if (!String("import").equals(qName)) return;
        String value = attributes->getValue(String("file"));
        if (value == nullptr) throw SAXParseException(String("Attribute 'file' is missing"), locator);
        String rec = attributes->getValue(String("recursiveImport"));
        imports.push_back(std::string(value) + (rec == nullptr ? "" : "," + std::string(rec)) + "@" +
                          std::to_string(locator->getLineNumber()) + ":" + std::to_string(locator->getColumnNumber()));
    }

private:
    Locator* locator = nullptr;
};

}  // namespace

JTEST(XmlSaxEventsLikeXerces) {
    const char* doc =
        "<?xml version=\"1.0\"?>\n<!-- c -->\n<?pi x y?>\n<root xmlns:xsi=\"urn:x\" xsi:a=\"1\" x=\"2\">\n"
        "  <import file=\"a&amp;b\" x=\"3\"/>\n  t&lt;u<![CDATA[v]]>\n</root>\n";
    auto* h = new Recorder();
    SAXParserFactory::newInstance()->newSAXParser()->parseString(doc, h);
    // Xerces: identical except that it also splits characters at references ("t", "<", "u").
    JCHECK_EQ(h->log, std::string("setLocator startDoc@1:1 PI(pi,x y) S[||root]@4:41 xmlns:xsi(xmlns:xsi,,CDATA)=urn:x "
                                  "xsi:a(xsi:a,,CDATA)=1 x(x,,CDATA)=2 getValue(x)=2 C(|  ) S[||import]@5:33 "
                                  "file(file,,CDATA)=a&b x(x,,CDATA)=3 getValue(x)=3 E[import] C(|  t<u) C(v) C(|) "
                                  "E[root] endDoc@-1:-1 "));
}

JTEST(XmlSaxTimeCheckerHandler) {
    auto* h = new TimeCheckerHandler();
    SAXParser* p = SAXParserFactory::newInstance()->newSAXParser();
    p->parse(String(dataFile("merge/static_data.xml")), h);
    std::vector<std::string> want = {"a.xml@6:24", "bdir@8:40", "cdir,false@11:64", "ndir@13:47", "empty_root.xml@15:33"};
    JCHECK(h->imports == want);
    auto* h2 = new TimeCheckerHandler();
    p->parse(new File(String(dataFile("merge/static_data.xml"))), h2);  // XmlMerger: parser.parse(sourceFile, handler)
    JCHECK(h2->imports == want);
    for (auto& s : h->imports) std::fprintf(stderr, "  %s\n", s.c_str());

    // Missing attribute: the handler's SAXParseException carries the locator position.
    try {
        p->parseString("<root>\n  <import skipRoot='true'/>\n</root>", new TimeCheckerHandler(), String("x.xml"));
        JCHECK(false);
    } catch (SAXParseException& e) {
        JCHECK_EQ(e.getLineNumber(), 2);
        JCHECK_EQ(e.getColumnNumber(), 28);
        JCHECK_EQ(e.getMessage(), String("Attribute 'file' is missing"));
    }
}

JTEST(XmlSaxErrorsAndByValueOverride) {
    auto* h = new ByValue();
    SAXParserFactory::newInstance()->newSAXParser()->parseString(
        "<ipconfig default='127.0.0.1'><iprange min='1' max='2' address='x'/><iprange min='3' max='4' address='y'/></ipconfig>", h);
    JCHECK((h->seen == std::vector<std::string>{"1-2", "3-4"}));

    // Missing file: java.io.FileNotFoundException, not a SAXException.
    JCHECK_THROWS(FileNotFoundException, SAXParserFactory::newInstance()->newSAXParser()->parse(String(dataFile("nope.xml")), h));
    // Malformed: fatalError (default: rethrow) and a SAXParseException with a position.
    try {
        SAXParserFactory::newInstance()->newSAXParser()->parseString("<root>\n<a>\n</root>\n", new DefaultHandler(), String("sax2.xml"));
        JCHECK(false);
    } catch (SAXParseException& e) {
        JCHECK(e.getLineNumber() > 0);
        JCHECK(e.toString().startsWith("org.xml.sax.SAXParseException; systemId: file:"));
    }
    // Namespace-aware mode.
    struct Ns : DefaultHandler {
        std::string log;
        void startPrefixMapping(const String& p, const String& u) override { log += "+" + std::string(p) + "=" + std::string(u) + " "; }
        void endPrefixMapping(const String& p) override { log += "-" + std::string(p) + " "; }
        void startElement(const String& uri, const String& local, const String& q, Attributes* a) override {
            log += "S[" + std::string(uri) + "|" + std::string(local) + "|" + std::string(q) + "|" + std::to_string(a->getLength()) + "] ";
        }
    };
    auto* f = SAXParserFactory::newInstance();
    f->setNamespaceAware(true);
    auto* ns = new Ns();
    f->newSAXParser()->parseString("<p:r xmlns:p='urn:p' p:a='1'><e/></p:r>", ns);
    JCHECK_EQ(ns->log, std::string("+p=urn:p S[urn:p|r|p:r|1] S[|e|e|0] -p "));
}
