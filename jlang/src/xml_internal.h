// jlang/src/xml_internal.h - private helpers shared by jlang/src/xml_*.cpp (pugixml lives here).
#pragma once

#include <jlang/Xml.h>

#include <pugixml.hpp>

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace jlang::xml::detail {

struct Pos {
    int32_t line = -1, column = -1, offset = -1;
};

// The unmodified input text of a SAX/StAX document, kept for positions (Locator, Location).
// GC-managed (the text is a pointer-free GC block), so events can compute their location
// lazily after the document itself was released.
struct XmlSource {
    const char* text = nullptr;
    size_t size = 0;
    std::vector<uint32_t> lines;  // offsets of '\n', built on first use
    bool linesBuilt = false;

    static XmlSource* copyOf(const char* data, size_t size);
    Pos positionOf(size_t offset);
    // Position just after the '>' of the tag whose name starts at nameOffset.
    Pos afterTag(size_t nameOffset);
};

// A parsed document. Allocated with `new` (GC heap) or on the stack; the pugixml tree and the
// buffers are malloc'ed and released by the destructor, so owners `delete` it explicitly when
// done (operator delete itself is a no-op, the destructor is what frees the memory).
class XmlDoc {
public:
    XmlDoc() = default;
    XmlDoc(const XmlDoc&) = delete;
    XmlDoc& operator=(const XmlDoc&) = delete;
    ~XmlDoc() = default;

    pugi::xml_document doc;
    std::string systemId;  // file path / URI for messages ("" when unknown)
    std::string scratch;   // Element::text() concatenation buffer

    using Pos = detail::Pos;
    // The unmodified input (SAX/StAX documents), or null (JAXB documents).
    XmlSource* source() const noexcept { return source_; }

    // Line (1-based) / column (1-based) / offset (0-based) just after the '>' of the start tag
    // of element `n` (needs source()); {-1,-1,-1} when unknown.
    Pos startTagEnd(pugi::xml_node_struct* n);
    // Position of a buffer offset (source text, else the in-place buffer: approximate).
    Pos positionOf(size_t offset);
    int32_t lineOf(size_t offset) { return positionOf(offset).line; }

    // ---- loading. `data` is malloc'ed and owned by the document afterwards.
    // Returns false on a parse error (message/line/column in err*).
    struct Error {
        std::string message;
        int32_t line = -1, column = -1;
    };
    bool parseInPlace(char* data, size_t size, unsigned options, Error* err);
    // Keeps a copy of the input as source() for positions, then parses `data` in place.
    bool parseWithSource(char* data, size_t size, unsigned options, Error* err);

private:
    XmlSource* source_ = nullptr;
    XmlSource* inplaceLines_ = nullptr;  // line index over the in-place buffer (JAXB diagnostics)
    const char* inplace_ = nullptr;
    size_t inplaceSize_ = 0;
};

// pugixml option sets.
constexpr unsigned kJaxbParse = pugi::parse_default | pugi::parse_ws_pcdata_single;
constexpr unsigned kSaxParse = pugi::parse_default | pugi::parse_ws_pcdata | pugi::parse_pi;
// StAX: escapes are decoded by the reader itself (each reference is its own event).
constexpr unsigned kStaxParse = (pugi::parse_default & ~pugi::parse_escapes) | pugi::parse_ws_pcdata |
                                pugi::parse_comments | pugi::parse_pi | pugi::parse_declaration |
                                pugi::parse_doctype;

// ---- IO (jlang/src/xml_io.cpp). Buffers returned by the read functions are malloc'ed.
struct Bytes {
    char* data = nullptr;
    size_t size = 0;
};
// Reads a whole file; throws jlang::FileNotFoundException("<path> (No such file or directory)")
// or jlang::IOException like java.io.FileInputStream.
Bytes readFile(const std::string& path);
Bytes readStream(InputStream* in);
Bytes readReader(Reader* r);  // chars re-encoded as UTF-8
std::string filePath(File* f);
void writeFile(const std::string& path, std::string_view data);
void writeStream(OutputStream* os, std::string_view data);
void writeWriter(::jlang::Writer* w, std::string_view data);
void flushWriter(::jlang::Writer* w);

// XML text helpers.
// Decodes one reference starting at s[i] == '&'. On success appends the replacement to out,
// sets i past the ';' and returns true. Unknown/malformed references return false.
bool decodeReference(std::string_view s, size_t& i, std::string& out);
// Decodes all references of an attribute value (unknown ones are kept as written).
std::string decodeReferences(std::string_view s);
void appendUtf8(std::string& out, uint32_t cp);

}  // namespace jlang::xml::detail
