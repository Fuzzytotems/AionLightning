// jlang/src/xml_dom.cpp - the pugixml-backed document (detail::XmlDoc) and the Element /
// Attribute views used by generated JAXB binders.
#include "xml_internal.h"

#include <algorithm>
#include <cstring>

namespace jlang::xml {

namespace detail {

// ---------------------------------------------------------------------------------------
// Node access (out of line so that <pugixml.hpp> stays out of the public headers).

pugi::xml_node_struct* firstChildElement(pugi::xml_node_struct* n) noexcept {
    if (n == nullptr) return nullptr;
    for (pugi::xml_node c = pugi::xml_node(n).first_child(); c; c = c.next_sibling()) {
        if (c.type() == pugi::node_element) return c.internal_object();
    }
    return nullptr;
}

pugi::xml_node_struct* nextSiblingElement(pugi::xml_node_struct* n) noexcept {
    if (n == nullptr) return nullptr;
    for (pugi::xml_node c = pugi::xml_node(n).next_sibling(); c; c = c.next_sibling()) {
        if (c.type() == pugi::node_element) return c.internal_object();
    }
    return nullptr;
}

std::string_view nodeName(pugi::xml_node_struct* n) noexcept {
    if (n == nullptr) return {};
    return std::string_view(pugi::xml_node(n).name());
}

pugi::xml_attribute_struct* firstAttribute(pugi::xml_node_struct* n) noexcept {
    if (n == nullptr) return nullptr;
    return pugi::xml_node(n).first_attribute().internal_object();
}

pugi::xml_attribute_struct* nextAttribute(pugi::xml_attribute_struct* a) noexcept {
    if (a == nullptr) return nullptr;
    return pugi::xml_attribute(a).next_attribute().internal_object();
}

std::string_view attributeName(pugi::xml_attribute_struct* a) noexcept {
    return std::string_view(pugi::xml_attribute(a).name());
}

std::string_view attributeValue(pugi::xml_attribute_struct* a) noexcept {
    return std::string_view(pugi::xml_attribute(a).value());
}

// ---------------------------------------------------------------------------------------
// XmlDoc

static void describeError(const pugi::xml_parse_result& r, XmlDoc* doc, XmlDoc::Error* err) {
    if (err == nullptr) return;
    err->message = r.description();
    if (r.offset >= 0) {
        Pos p = doc->positionOf(static_cast<size_t>(r.offset));
        err->line = p.line;
        err->column = p.column;
    }
}

static bool isUtf8Like(const char* data, size_t size) {
    if (size >= 2) {
        auto b0 = static_cast<unsigned char>(data[0]), b1 = static_cast<unsigned char>(data[1]);
        if ((b0 == 0xFE && b1 == 0xFF) || (b0 == 0xFF && b1 == 0xFE) || b0 == 0 || b1 == 0) return false;
    }
    return true;
}

XmlSource* XmlSource::copyOf(const char* data, size_t size) {
    auto* s = new XmlSource();
    auto* t = static_cast<char*>(::jlang::gc::allocAtomic(size + 1));
    std::memcpy(t, data, size);
    t[size] = 0;
    s->text = t;
    s->size = size;
    return s;
}

Pos XmlSource::positionOf(size_t offset) {
    Pos p;
    if (text == nullptr) return p;
    if (!linesBuilt) {
        linesBuilt = true;
        for (const char* q = text; (q = static_cast<const char*>(std::memchr(q, '\n', size - static_cast<size_t>(q - text)))) != nullptr; q++) {
            lines.push_back(static_cast<uint32_t>(q - text));
        }
    }
    auto it = std::lower_bound(lines.begin(), lines.end(), static_cast<uint32_t>(offset));
    size_t line = static_cast<size_t>(it - lines.begin());  // '\n' strictly before offset
    size_t lineStart = line == 0 ? 0 : lines[line - 1] + 1;
    p.line = static_cast<int32_t>(line + 1);
    p.column = static_cast<int32_t>(offset - lineStart + 1);
    p.offset = static_cast<int32_t>(offset);
    return p;
}

Pos XmlSource::afterTag(size_t nameOffset) {
    if (text == nullptr || nameOffset >= size) return Pos{};
    size_t i = nameOffset;
    while (i < size) {
        char c = text[i];
        if (c == '>') {
            i++;
            break;
        }
        if (c == '"' || c == '\'') {
            const void* q = std::memchr(text + i + 1, c, size - i - 1);
            if (q == nullptr) {
                i = size;
                break;
            }
            i = static_cast<size_t>(static_cast<const char*>(q) - text) + 1;
            continue;
        }
        i++;
    }
    return positionOf(i);
}

bool XmlDoc::parseInPlace(char* data, size_t size, unsigned options, Error* err) {
    // pugixml takes ownership of `data` whatever the outcome.
    if (isUtf8Like(data, size)) {
        inplace_ = data;
        inplaceSize_ = size;
    }
    pugi::xml_parse_result r = doc.load_buffer_inplace_own(data, size, options, pugi::encoding_auto);
    if (!r) {
        describeError(r, this, err);
        return false;
    }
    return true;
}

bool XmlDoc::parseWithSource(char* data, size_t size, unsigned options, Error* err) {
    source_ = XmlSource::copyOf(data, size);
    pugi::xml_parse_result r = doc.load_buffer_inplace_own(data, size, options, pugi::encoding_auto);
    if (!r) {
        describeError(r, this, err);
        return false;
    }
    return true;
}

Pos XmlDoc::positionOf(size_t offset) {
    if (source_ != nullptr) return source_->positionOf(offset);
    if (inplace_ == nullptr) return Pos{};
    if (inplaceLines_ == nullptr) {
        inplaceLines_ = new XmlSource();
        inplaceLines_->text = inplace_;
        inplaceLines_->size = inplaceSize_;
    }
    return inplaceLines_->positionOf(offset);
}

Pos XmlDoc::startTagEnd(pugi::xml_node_struct* n) {
    if (source_ == nullptr || n == nullptr) return Pos{};
    ptrdiff_t off = pugi::xml_node(n).offset_debug();
    if (off < 0) return Pos{};
    return source_->afterTag(static_cast<size_t>(off));
}

// ---------------------------------------------------------------------------------------
// References

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool decodeReference(std::string_view s, size_t& i, std::string& out) {
    size_t semi = s.find(';', i + 1);
    if (semi == std::string_view::npos || semi - i > 16) return false;
    std::string_view name = s.substr(i + 1, semi - i - 1);
    if (name.empty()) return false;
    if (name[0] == '#') {
        uint32_t cp = 0;
        bool hex = name.size() > 1 && (name[1] == 'x');
        size_t k = hex ? 2 : 1;
        if (k >= name.size()) return false;
        for (; k < name.size(); k++) {
            char c = name[k];
            uint32_t d;
            if (c >= '0' && c <= '9') {
                d = static_cast<uint32_t>(c - '0');
            } else if (hex && c >= 'a' && c <= 'f') {
                d = static_cast<uint32_t>(c - 'a' + 10);
            } else if (hex && c >= 'A' && c <= 'F') {
                d = static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
            cp = cp * (hex ? 16 : 10) + d;
            if (cp > 0x10FFFF) return false;
        }
        if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        appendUtf8(out, cp);
    } else if (name == "lt") {
        out.push_back('<');
    } else if (name == "gt") {
        out.push_back('>');
    } else if (name == "amp") {
        out.push_back('&');
    } else if (name == "quot") {
        out.push_back('"');
    } else if (name == "apos") {
        out.push_back('\'');
    } else {
        return false;
    }
    i = semi + 1;
    return true;
}

std::string decodeReferences(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        size_t amp = s.find('&', i);
        if (amp == std::string_view::npos) {
            out.append(s.substr(i));
            break;
        }
        out.append(s.substr(i, amp - i));
        i = amp;
        if (!decodeReference(s, i, out)) {
            out.push_back('&');
            i++;
        }
    }
    return out;
}

}  // namespace detail

// ---------------------------------------------------------------------------------------
// Element

std::optional<std::string_view> Element::attribute(std::string_view name) const noexcept {
    for (pugi::xml_attribute_struct* a = detail::firstAttribute(node_); a != nullptr; a = detail::nextAttribute(a)) {
        if (detail::attributeName(a) == name) return detail::attributeValue(a);
    }
    return std::nullopt;
}

std::string_view Element::text() const {
    if (node_ == nullptr) return {};
    pugi::xml_node n(node_);
    pugi::xml_node first;
    int count = 0;
    for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) {
        if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) {
            if (count == 0) first = c;
            count++;
            if (count > 1) break;
        }
    }
    if (count == 0) return {};
    if (count == 1) return std::string_view(first.value());
    std::string& buf = doc_->scratch;
    buf.clear();
    for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) {
        if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) buf.append(c.value());
    }
    return buf;
}

Element Element::child(std::string_view name) const noexcept {
    for (pugi::xml_node_struct* c = detail::firstChildElement(node_); c != nullptr; c = detail::nextSiblingElement(c)) {
        if (detail::nodeName(c) == name) return Element(c, doc_);
    }
    return Element();
}

int32_t Element::lineNumber() const {
    if (node_ == nullptr || doc_ == nullptr) return -1;
    ptrdiff_t off = pugi::xml_node(node_).offset_debug();
    if (off < 0) return -1;
    return doc_->lineOf(static_cast<size_t>(off));
}

}  // namespace jlang::xml
