// Tests for the StAX emulation (jlang/XmlEvents.h). The main test runs a line-by-line C++
// translation of gameserver XmlMerger.doUpdate/processImportElement/importFile on the synthetic
// tree jlang/tests/data/xml/merge and compares the result byte for byte with the output of the
// real Java XmlMerger (data/xml/merge_expected.xml, JDK 21 built-in StAX).
#include "jtest.h"

#include <jlang/IO.h>
#include <jlang/Xml.h>

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

using namespace jlang;
using namespace jlang::xml;

namespace {

std::string testDir() {
    std::string f = __FILE__;
    return f.substr(0, f.rfind('/'));
}
std::string dataFile(const std::string& rel) { return testDir() + "/data/xml/" + rel; }
std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

// commons-io FileUtils.listFiles(root, and(and(not(prefix "new"), suffix ".xml"), VISIBLE),
// recursive ? makeSVNAware(VISIBLE) : null): readdir order, depth first.
void listFiles(const std::string& dir, bool recursive, std::vector<std::string>& out) {
    DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) return;
    std::vector<std::string> names;
    while (dirent* e = ::readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        names.push_back(n);
    }
    ::closedir(d);
    for (const std::string& n : names) {
        std::string p = dir + "/" + n;
        bool hidden = n[0] == '.';
        if (isDir(p)) {
            if (recursive && !hidden && n != ".svn") listFiles(p, recursive, out);
        } else if (!hidden && n.rfind("new", 0) != 0 && n.size() > 4 && n.compare(n.size() - 4, 4, ".xml") == 0) {
            out.push_back(p);
        }
    }
}

// Translation of org.openaion.gameserver.dataholders.loadingutils.XmlMerger (update part), in
// the style of the port: same structure, jlang::xml API. File IO goes through the in-memory
// StAX extensions instead of FileReader/BufferedWriter.
class Merger {
public:
    explicit Merger(const std::string& baseDir) : baseDir(baseDir) {}

    std::string doUpdate(const std::string& sourceFile) {
        XMLEventReader* reader = nullptr;
        XMLEventWriter* writer = nullptr;
        {
            JFINALLY {
                if (writer != nullptr) try { writer->close(); } catch (Exception&) {}
                if (reader != nullptr) try { reader->close(); } catch (Exception&) {}
            };
            writer = outputFactory->createXMLEventWriterToString();
            reader = inputFactory->createXMLEventReaderFromFile(String(sourceFile));

            while (reader->hasNext()) {
                XMLEvent* xmlEvent = reader->nextEvent();

                if (xmlEvent->isStartElement() && isImportQName(xmlEvent->asStartElement()->getName())) {
                    processImportElement(xmlEvent->asStartElement(), writer);
                    continue;
                }

                if (xmlEvent->isEndElement() && isImportQName(xmlEvent->asEndElement()->getName())) continue;

                if (instanceof<Comment>(xmlEvent)) continue;  // skip comments.

                if (xmlEvent->isCharacters())  // skip whitespaces.
                    if (xmlEvent->asCharacters()->isWhiteSpace() || xmlEvent->asCharacters()->isIgnorableWhiteSpace())
                        continue;

                writer->add(xmlEvent);

                if (xmlEvent->isStartDocument()) {
                    writer->add(eventFactory->createComment("\nThis file is machine-generated. DO NOT MODIFY IT!\n"));
                }
            }
        }
        return writer->pending();
    }

    // The same with the Java IO classes, exactly as XmlMerger.doUpdate does it.
    void doUpdateIO(File* sourceFile, File* destFile) {
        XMLEventReader* reader = nullptr;
        XMLEventWriter* writer = nullptr;
        JFINALLY {
            if (writer != nullptr) try { writer->close(); } catch (Exception&) {}
            if (reader != nullptr) try { reader->close(); } catch (Exception&) {}
        };
        writer = outputFactory->createXMLEventWriter(new BufferedWriter(new FileWriter(destFile, false)));
        reader = inputFactory->createXMLEventReader(new FileReader(sourceFile));
        while (reader->hasNext()) {
            XMLEvent* xmlEvent = reader->nextEvent();
            if (xmlEvent->isStartElement() && isImportQName(xmlEvent->asStartElement()->getName())) {
                processImportElement(xmlEvent->asStartElement(), writer);
                continue;
            }
            if (xmlEvent->isEndElement() && isImportQName(xmlEvent->asEndElement()->getName())) continue;
            if (instanceof<Comment>(xmlEvent)) continue;
            if (xmlEvent->isCharacters())
                if (xmlEvent->asCharacters()->isWhiteSpace() || xmlEvent->asCharacters()->isIgnorableWhiteSpace()) continue;
            writer->add(xmlEvent);
            if (xmlEvent->isStartDocument()) {
                writer->add(eventFactory->createComment("\nThis file is machine-generated. DO NOT MODIFY IT!\n"));
            }
        }
    }
    bool useFileReader = false;

    std::vector<std::string> imported;

private:
    bool isImportQName(QName* name) { return String("import").equals(name->getLocalPart()); }

    void processImportElement(StartElement* element, XMLEventWriter* writer) {
        std::string file = baseDir + "/" + std::string(getAttributeValue(element, qNameFile, String(), "Attribute 'file' is missing or empty."));
        if (!exists(file)) throw FileNotFoundException(String("Missing file to import:") + file);

        bool skipRoot = Boolean::parseBoolean(getAttributeValue(element, qNameSkipRoot, "false", String()));
        bool recImport = Boolean::parseBoolean(getAttributeValue(element, qNameRecursiveImport, "true", String()));

        if (!isDir(file)) {
            importFile(file, skipRoot, writer);
        } else {
            std::vector<std::string> files;
            listFiles(file, recImport, files);
            for (const std::string& childFile : files) importFile(childFile, skipRoot, writer);
        }
    }

    String getAttributeValue(StartElement* element, QName* name, const String& def, const String& onErrorMessage) {
        XMLAttribute* attribute = element->getAttributeByName(name);
        if (attribute == nullptr) {
            if (def == nullptr) throw XMLStreamException(onErrorMessage, element->getLocation());
            return def;
        }
        return attribute->getValue();
    }

    void importFile(const std::string& file, bool skipRoot, XMLEventWriter* writer) {
        imported.push_back(file.substr(baseDir.size() + 1));
        XMLEventReader* reader = nullptr;
        JFINALLY {
            if (reader != nullptr) try { reader->close(); } catch (Exception&) {}
        };
        reader = useFileReader ? inputFactory->createXMLEventReader(new FileReader(new File(String(file))))
                               : inputFactory->createXMLEventReaderFromFile(String(file));
        QName* firstTagQName = nullptr;
        while (reader->hasNext()) {
            XMLEvent* event = reader->nextEvent();
            // skip start and end of document.
            if (event->isStartDocument() || event->isEndDocument()) continue;
            // skip all comments.
            if (instanceof<Comment>(event)) continue;
            // skip white-spaces and all ignoreable white-spaces.
            if (event->isCharacters()) {
                if (event->asCharacters()->isWhiteSpace() || event->asCharacters()->isIgnorableWhiteSpace()) continue;
            }
            // modify root-tag of imported file.
            if (firstTagQName == nullptr && event->isStartElement()) {
                firstTagQName = event->asStartElement()->getName();
                if (skipRoot) {
                    continue;
                } else {
                    StartElement* old = event->asStartElement();
                    event = eventFactory->createStartElement(old->getName(), old->getAttributes(), nullptr);
                }
            }
            // if root was skipped - skip root end too.
            if (event->isEndElement() && skipRoot && event->asEndElement()->getName()->equals(firstTagQName)) continue;
            // finally - write tag
            writer->add(event);
        }
    }

    std::string baseDir;
    XMLInputFactory* inputFactory = XMLInputFactory::newInstance();
    XMLOutputFactory* outputFactory = XMLOutputFactory::newInstance();
    XMLEventFactory* eventFactory = XMLEventFactory::newInstance();
    QName* qNameFile = new QName("file");
    QName* qNameSkipRoot = new QName("skipRoot");
    QName* qNameRecursiveImport = new QName("recursiveImport");
};

std::string eventsOf(const std::string& xml) {
    XMLEventReader* r = XMLInputFactory::newInstance()->createXMLEventReaderFromString(xml);
    std::string s;
    while (r->hasNext()) {
        XMLEvent* e = r->nextEvent();
        if (e->isCharacters()) {
            s += std::string("C") + (e->asCharacters()->isWhiteSpace() ? "w" : "") + "(" + std::string(e->asCharacters()->getData()) + ") ";
        } else if (e->isStartElement()) {
            s += "S(" + std::string(e->asStartElement()->getName()->toString());
            Iterator<Object*>* it = e->asStartElement()->getAttributes();
            while (it->hasNext()) {
                auto* a = cast<XMLAttribute>(it->next());
                s += " " + std::string(a->getName()->toString()) + "=[" + std::string(a->getValue()) + "]";
            }
            s += ") ";
        } else {
            s += std::to_string(e->getEventType()) + " ";
        }
    }
    return s;
}

}  // namespace

JTEST(XmlStaxMergerMatchesJava) {
    std::string base = dataFile("merge");
    Merger m(base);
    std::string out = m.doUpdate(base + "/static_data.xml");
    std::string expected = slurp(dataFile("merge_expected.xml"));
    if (out != expected) {
        size_t i = 0;
        while (i < out.size() && i < expected.size() && out[i] == expected[i]) i++;
        jtest::fail(__FILE__, __LINE__,
                    "merge output differs at byte " + std::to_string(i) + "\n    actual:   " + out.substr(i > 40 ? i - 40 : 0, 120) +
                        "\n    expected: " + expected.substr(i > 40 ? i - 40 : 0, 120));
    }
    std::sort(m.imported.begin(), m.imported.end());
    std::vector<std::string> want = {"a.xml", "bdir/sub/b1.xml", "cdir/c1.xml", "empty_root.xml", "ndir/new/n1.xml"};
    JCHECK(m.imported == want);
}

JTEST(XmlStaxMergerWithJavaIO) {
    std::string base = dataFile("merge");
    std::string out = (std::filesystem::temp_directory_path() / ("jlang_xml_merge_" + std::to_string(::getpid()) + ".xml")).string();
    Merger m(base);
    m.useFileReader = true;
    m.doUpdateIO(new File(String(base + "/static_data.xml")), new File(String(out)));
    // XMLEventWriter.close() flushes but does not close the FileWriter (JDK behavior): the
    // content must be complete anyway.
    std::string got = slurp(out);
    std::filesystem::remove(out);
    JCHECK(got == slurp(dataFile("merge_expected.xml")));
}

JTEST(XmlStaxMissingFileAttribute) {
    XMLEventReader* r = XMLInputFactory::newInstance()->createXMLEventReaderFromString(
        "<?xml version=\"1.0\"?>\n<root>\n  <x/>\n    <import skipRoot=\"true\"/>\n</root>\n");
    StartElement* imp = nullptr;
    while (r->hasNext()) {
        XMLEvent* e = r->nextEvent();
        if (e->isStartElement() && e->asStartElement()->getName()->getLocalPart().equals("import")) imp = e->asStartElement();
    }
    JCHECK(imp != nullptr);
    JCHECK(imp->getAttributeByName(new QName("file")) == nullptr);
    XMLStreamException ex(String("Attribute 'file' is missing or empty."), imp->getLocation());
    // Java: "ParseError at [row,col]:[4,30]\nMessage: Attribute 'file' is missing or empty."
    JCHECK_EQ(ex.getMessage(), String("ParseError at [row,col]:[4,30]\nMessage: Attribute 'file' is missing or empty."));
    JCHECK_EQ(imp->getLocation()->getCharacterOffset(), 65);
}

JTEST(XmlStaxEventSequence) {
    // Expected sequences observed with the JDK's StAX implementation.
    JCHECK_EQ(eventsOf("<a>&lt;&gt;&#65;&#x42;</a>"), std::string("7 S(a) C(<) C(>) C(A) C(B) 2 8 "));
    JCHECK_EQ(eventsOf("<a>&amp;x&amp;</a>"), std::string("7 S(a) C(&) C(x) C(&) 2 8 "));
    JCHECK_EQ(eventsOf("<f>&lt; &gt;</f>"), std::string("7 S(f) C(<) Cw( ) C(>) 2 8 "));
    JCHECK_EQ(eventsOf("<h>  <![CDATA[x<y]]>  </h>"), std::string("7 S(h) Cw(  ) C(x<y) Cw(  ) 2 8 "));
    JCHECK_EQ(eventsOf("<a b='x&#10;y\r\nz\tw'/>"), std::string("7 S(a b=[x\ny z w]) 2 8 "));
    JCHECK_EQ(eventsOf("<a>\n<!-- c -->\n<b/></a>"), std::string("7 S(a) Cw(\n) 5 Cw(\n) S(b) 2 2 8 "));
    JCHECK_EQ(eventsOf("<?xml version='1.0'?>\n<!-- c1 -->\n<?pi x?>\n<r/>\n<!-- after -->\n"), std::string("7 5 3 S(r) 2 5 8 "));
    // Prefixed names resolve their namespace; declarations are not attributes.
    JCHECK_EQ(eventsOf("<p:r xmlns:p='urn:p' p:a='1' b='2'/>"), std::string("7 S({urn:p}r {urn:p}a=[1] b=[2]) 2 8 "));

    XMLEventReader* r = XMLInputFactory::newInstance()->createXMLEventReaderFromString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><r a='1'>t<e>x</e></r>");
    XMLEvent* sd = r->nextEvent();
    JCHECK(sd->isStartDocument());
    JCHECK(sd->getCharacterEncodingScheme().isNull());  // Reader input: no encoding reported
    JCHECK(sd->encodingSet());
    JCHECK(sd->standaloneSet() && sd->isStandalone());
    JCHECK_EQ(sd->getVersion(), String("1.0"));
    XMLEvent* peeked = r->peek();
    JCHECK(r->nextEvent() == peeked);
    JCHECK(peeked->isStartElement());
    JCHECK_THROWS(ClassCastException, peeked->asCharacters());
    JCHECK_THROWS(ClassCastException, peeked->getData());
    JCHECK(instanceof<Characters>(r->nextEvent()));
    XMLEvent* e = r->nextTag();
    JCHECK(e->isStartElement());
    JCHECK_EQ(r->getElementText(), String("x"));
    JCHECK(r->nextEvent()->isEndElement());
    JCHECK(r->nextEvent()->isEndDocument());
    JCHECK(!r->hasNext());
    JCHECK_THROWS(NoSuchElementException, r->nextEvent());
    r->close();

    JCHECK_THROWS(XMLStreamException, XMLInputFactory::newInstance()->createXMLEventReaderFromString("<a><b></a>"));
}

JTEST(XmlStaxWriterFormat) {
    XMLEventFactory* f = XMLEventFactory::newInstance();
    XMLEventWriter* w = XMLOutputFactory::newInstance()->createXMLEventWriterToString();
    auto* attrs = new List<Object*>();
    attrs->add(f->createAttribute("q", "a\"<&>'\t"));
    auto* ns = new List<Object*>();
    ns->add(f->createNamespace("x", "urn:x"));
    ns->add(f->createNamespace("urn:default"));
    w->add(f->createStartDocument());
    w->add(f->createStartElement(new QName("root"), attrs->iterator(), ns->iterator()));
    w->add(f->createStartElement(String("x"), String("urn:x"), String("e")));
    w->add(f->createEndElement(String("x"), String("urn:x"), String("e")));
    w->add(f->createCharacters("<&>\"'"));
    w->add(f->createCData("raw <x>"));
    w->add(f->createComment(" c "));
    w->add(f->createProcessingInstruction("pi", "d"));
    w->add(f->createStartElement(String(""), String(""), String("open")));
    w->add(f->createEndDocument());
    w->close();
    JCHECK_EQ(w->pending(), std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root xmlns:x=\"urn:x\" xmlns=\"urn:default\" "
                                        "q=\"a&quot;&lt;&amp;&gt;'\t\"><x:e></x:e>&lt;&amp;&gt;\"'<![CDATA[raw <x>]]><!-- c -->"
                                        "<?pi d?><open></open></root>"));

    XMLEventWriter* w2 = XMLOutputFactory::newInstance()->createXMLEventWriterToString();
    w2->add(new StartDocument(String(), false, String(), false, false));
    JCHECK_EQ(w2->pending(), std::string("<?xml version=\"1.0\" ?>"));
    JCHECK_THROWS(XMLStreamException, w2->add(f->createEndElement(String(""), String(""), String("x"))));

    // QName semantics
    JCHECK((new QName("a"))->equals(new QName(String(""), String("a"), String("p"))));
    JCHECK(!(new QName("a"))->equals(new QName(String("urn"), String("a"))));
    JCHECK_EQ((new QName(String("urn"), String("a")))->toString(), String("{urn}a"));
    JCHECK_EQ(QName::valueOf("{urn}a")->getNamespaceURI(), String("urn"));
    JCHECK_EQ((new QName("level"))->hashCode(), String("level").hashCode());
}

JTEST(XmlStaxInputStreamEncoding) {
    // From an InputStream the declared encoding is reported (and written back), unlike a Reader.
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><a>\xC3\xA9</a>";
    auto* bytes = new Array<int8_t>(static_cast<int32_t>(xml.size()));
    std::memcpy(bytes->data(), xml.data(), xml.size());
    XMLEventReader* r = XMLInputFactory::newInstance()->createXMLEventReader(new ByteArrayInputStream(bytes));
    XMLEventWriter* w = XMLOutputFactory::newInstance()->createXMLEventWriterToString();
    XMLEvent* sd = r->nextEvent();
    JCHECK_EQ(sd->getCharacterEncodingScheme(), String("UTF-8"));
    w->add(sd);
    w->add(r);
    JCHECK_EQ(w->pending(), std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?><a>\xC3\xA9</a>"));
}

JTEST(XmlStaxAttributeHashOrder) {
    // java.util.HashMap iteration order of attribute QNames, as observed with the JDK
    // (<survey level="3" race="ELYOS"> comes out as race, level).
    JCHECK_EQ(eventsOf("<survey level='3' race='ELYOS'/>"), std::string("7 S(survey race=[ELYOS] level=[3]) 2 8 "));
    JCHECK_EQ(eventsOf("<item id='162000002' count='10'/>"), std::string("7 S(item count=[10] id=[162000002]) 2 8 "));
}
