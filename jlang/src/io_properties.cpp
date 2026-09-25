// jlang/IOProperties.cpp - java.util.Properties (exact LineReader/loadConvert/saveConvert
// algorithms of the JDK) and java.util.ResourceBundle / PropertyResourceBundle.
#include <jlang/IO.h>

#include <cstring>
#include <ctime>
#include <string>

namespace jlang {

namespace {

// Character source for the LineReader: ISO-8859-1 bytes of an InputStream or chars of a Reader.
struct ByteSource {
    InputStream* in;
    Array<int8_t>* buf = new Array<int8_t>(8192);
    int32_t fill() { return in->read(buf, 0, buf->length); }
    char16_t at(int32_t i) const { return static_cast<char16_t>(static_cast<uint8_t>(buf->data()[i])); }
};

struct CharSource {
    Reader* reader;
    Array<char16_t>* buf = new Array<char16_t>(8192);
    int32_t fill() { return reader->read(buf, 0, buf->length); }
    char16_t at(int32_t i) const { return buf->data()[i]; }
};

// java.util.Properties.LineReader
template<class Src>
class LineReader {
public:
    explicit LineReader(Src& src) : src_(src) {}
    std::u16string lineBuf;

    int32_t readLine() {
        int32_t len = 0;
        int32_t off = inOff_;
        int32_t limit = inLimit_;
        bool skipWhiteSpace = true;
        bool appendedLineBegin = false;
        bool precedingBackslash = false;
        char16_t c;
        lineBuf.clear();

        for (;;) {
            if (off >= limit) {
                inLimit_ = limit = src_.fill();
                if (limit <= 0) {
                    if (len == 0) return -1;
                    return precedingBackslash ? len - 1 : len;
                }
                off = 0;
            }
            c = src_.at(off++);

            if (skipWhiteSpace) {
                if (c == ' ' || c == '\t' || c == '\f') continue;
                if (!appendedLineBegin && (c == '\r' || c == '\n')) continue;
                skipWhiteSpace = false;
                appendedLineBegin = false;
            }
            if (len == 0) {  // still on a new logical line
                if (c == '#' || c == '!') {
                    // Comment: consume the rest of the line.
                    for (;;) {
                        bool eol = false;
                        while (off < limit) {
                            char16_t b = src_.at(off++);
                            if (b == '\r' || b == '\n') {
                                eol = true;
                                break;
                            }
                        }
                        if (eol) break;
                        if (off == limit) {
                            inLimit_ = limit = src_.fill();
                            if (limit <= 0) return -1;  // EOF
                            off = 0;
                        }
                    }
                    skipWhiteSpace = true;
                    continue;
                }
            }

            if (c != '\n' && c != '\r') {
                if (static_cast<int32_t>(lineBuf.size()) <= len) lineBuf.resize(static_cast<size_t>(len) + 1);
                lineBuf[static_cast<size_t>(len++)] = c;
                // flip the preceding backslash flag
                precedingBackslash = (c == '\\') ? !precedingBackslash : false;
            } else {
                // reached EOL
                if (len == 0) {
                    skipWhiteSpace = true;
                    continue;
                }
                if (off >= limit) {
                    inLimit_ = limit = src_.fill();
                    off = 0;
                    if (limit <= 0) {  // EOF
                        return precedingBackslash ? len - 1 : len;
                    }
                }
                if (precedingBackslash) {
                    // backslash at EOL is not part of the line
                    len -= 1;
                    // skip leading whitespace characters in the following line
                    skipWhiteSpace = true;
                    appendedLineBegin = true;
                    precedingBackslash = false;
                    // take care not to include any subsequent \n
                    if (c == '\r') {
                        if (src_.at(off) == '\n') off++;
                    }
                } else {
                    inOff_ = off;
                    return len;
                }
            }
        }
    }

private:
    Src& src_;
    int32_t inOff_ = 0;
    int32_t inLimit_ = 0;
};

// Properties.loadConvert
std::u16string loadConvert(const std::u16string& in, int32_t off, int32_t len) {
    int32_t end = off + len;
    std::u16string out;
    out.reserve(static_cast<size_t>(len));
    while (off < end) {
        char16_t aChar = in[static_cast<size_t>(off++)];
        if (aChar == '\\') {
            aChar = in[static_cast<size_t>(off++)];
            if (aChar == 'u') {
                if (off > end - 4) throw IllegalArgumentException(String("Malformed \\uxxxx encoding."));
                int32_t value = 0;
                for (int i = 0; i < 4; i++) {
                    aChar = in[static_cast<size_t>(off++)];
                    if (aChar >= '0' && aChar <= '9') value = (value << 4) + aChar - '0';
                    else if (aChar >= 'a' && aChar <= 'f') value = (value << 4) + 10 + aChar - 'a';
                    else if (aChar >= 'A' && aChar <= 'F') value = (value << 4) + 10 + aChar - 'A';
                    else throw IllegalArgumentException(String("Malformed \\uxxxx encoding."));
                }
                out.push_back(static_cast<char16_t>(value));
            } else {
                if (aChar == 't') aChar = '\t';
                else if (aChar == 'r') aChar = '\r';
                else if (aChar == 'n') aChar = '\n';
                else if (aChar == 'f') aChar = '\f';
                out.push_back(aChar);
            }
        } else {
            out.push_back(aChar);
        }
    }
    return out;
}

// Properties.saveConvert
std::u16string saveConvert(const std::u16string& s, bool escapeSpace, bool escapeUnicode) {
    static const char* hex = "0123456789ABCDEF";
    std::u16string out;
    out.reserve(s.size() * 2);
    for (size_t x = 0; x < s.size(); x++) {
        char16_t aChar = s[x];
        if (aChar > 61 && aChar < 127) {
            if (aChar == '\\') {
                out.push_back('\\');
                out.push_back('\\');
                continue;
            }
            out.push_back(aChar);
            continue;
        }
        switch (aChar) {
            case ' ':
                if (x == 0 || escapeSpace) out.push_back('\\');
                out.push_back(' ');
                break;
            case '\t':
                out += u"\\t";
                break;
            case '\n':
                out += u"\\n";
                break;
            case '\r':
                out += u"\\r";
                break;
            case '\f':
                out += u"\\f";
                break;
            case '=':
            case ':':
            case '#':
            case '!':
                out.push_back('\\');
                out.push_back(aChar);
                break;
            default:
                if ((aChar < 0x0020 || aChar > 0x007e) && escapeUnicode) {
                    out += u"\\u";
                    out.push_back(static_cast<char16_t>(hex[(aChar >> 12) & 0xF]));
                    out.push_back(static_cast<char16_t>(hex[(aChar >> 8) & 0xF]));
                    out.push_back(static_cast<char16_t>(hex[(aChar >> 4) & 0xF]));
                    out.push_back(static_cast<char16_t>(hex[aChar & 0xF]));
                } else {
                    out.push_back(aChar);
                }
        }
    }
    return out;
}

// Properties.writeComments
void writeComments(BufferedWriter* bw, const std::u16string& comments) {
    static const char* hex = "0123456789ABCDEF";
    bw->write(String("#"));
    size_t len = comments.size();
    size_t current = 0;
    size_t last = 0;
    while (current < len) {
        char16_t c = comments[current];
        if (c > 0x00ff || c == '\n' || c == '\r') {
            if (last != current) bw->write(String::fromUtf16(comments.data() + last, current - last));
            if (c > 0x00ff) {
                std::u16string u = u"\\u";
                u.push_back(static_cast<char16_t>(hex[(c >> 12) & 0xF]));
                u.push_back(static_cast<char16_t>(hex[(c >> 8) & 0xF]));
                u.push_back(static_cast<char16_t>(hex[(c >> 4) & 0xF]));
                u.push_back(static_cast<char16_t>(hex[c & 0xF]));
                bw->write(String::fromUtf16(u));
            } else {
                bw->newLine();
                if (c == '\r' && current != len - 1 && comments[current + 1] == '\n') current++;
                if (current == len - 1 || (comments[current + 1] != '#' && comments[current + 1] != '!'))
                    bw->write(String("#"));
            }
            last = current + 1;
        }
        current++;
    }
    if (last != current) bw->write(String::fromUtf16(comments.data() + last, current - last));
    bw->newLine();
}

// new Date().toString(): "EEE MMM dd HH:mm:ss zzz yyyy"
String dateString() {
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, "%a %b %d %H:%M:%S %Z %Y", &tm);
    return String(buf);
}

}  // namespace

// =======================================================================================
// Properties

Properties::Properties(Properties* defs) : defaults(defs), map_(new Map<String, String>()) {}

String Properties::getProperty(const String& key) {
    JSYNC(this) {
        auto o = map_->getOptional(key);
        if (o.has_value()) return *o;
    }
    return defaults != nullptr ? defaults->getProperty(key) : String();
}

String Properties::getProperty(const String& key, const String& defaultValue) {
    String val = getProperty(key);
    return val.isNull() ? defaultValue : val;
}

String Properties::setProperty(const String& key, const String& value) { return put(key, value); }

String Properties::get(const String& key) {
    JSYNC(this) { return map_->get(key); }
    return String();
}

String Properties::put(const String& key, const String& value) {
    if (key.isNull() || value.isNull()) throw NullPointerException();
    JSYNC(this) { return map_->put(key, value); }
    return String();
}

String Properties::remove(const String& key) {
    JSYNC(this) { return map_->remove(key); }
    return String();
}

bool Properties::containsKey(const String& key) {
    JSYNC(this) { return map_->containsKey(key); }
    return false;
}

bool Properties::containsValue(const String& value) {
    JSYNC(this) { return map_->containsValue(value); }
    return false;
}

int32_t Properties::size() {
    JSYNC(this) { return map_->size(); }
    return 0;
}

void Properties::clear() {
    JSYNC(this) { map_->clear(); }
}

void Properties::putAll(Properties* t) {
    if (t == nullptr) throw NullPointerException();
    List<Entry<String, String>>* es = t->entrySet();
    JSYNC(this) {
        for (auto& e : *es) map_->put(e.getKey(), e.getValue());
    }
}

void Properties::putAll(Map<String, String>* t) {
    if (t == nullptr) throw NullPointerException();
    List<Entry<String, String>>* es = t->entrySet();
    JSYNC(this) {
        for (auto& e : *es) put(e.getKey(), e.getValue());
    }
}

Set<String>* Properties::keySet() {
    auto* s = new Set<String>();
    JSYNC(this) {
        for (auto& e : *map_) s->add(e.getKey());
    }
    return s;
}

List<String>* Properties::values() {
    JSYNC(this) { return map_->values(); }
    return nullptr;
}

List<Entry<String, String>>* Properties::entrySet() {
    JSYNC(this) { return map_->entrySet(); }
    return nullptr;
}

Iterator<String>* Properties::keys() { return keySet()->iterator(); }

void Properties::enumerate(Set<String>* out) {
    if (defaults != nullptr) defaults->enumerate(out);
    JSYNC(this) {
        for (auto& e : *map_) out->add(e.getKey());
    }
}

Set<String>* Properties::stringPropertyNames() {
    auto* s = new Set<String>();
    enumerate(s);
    return s;
}

Iterator<String>* Properties::propertyNames() { return stringPropertyNames()->iterator(); }

template<class Src>
void Properties::load0(Src& src) {
    LineReader<Src> lr(src);
    int32_t limit;
    while ((limit = lr.readLine()) >= 0) {
        int32_t keyLen = 0;
        int32_t valueStart = limit;
        bool hasSep = false;
        bool precedingBackslash = false;
        const std::u16string& lb = lr.lineBuf;
        while (keyLen < limit) {
            char16_t c = lb[static_cast<size_t>(keyLen)];
            if ((c == '=' || c == ':') && !precedingBackslash) {
                valueStart = keyLen + 1;
                hasSep = true;
                break;
            } else if ((c == ' ' || c == '\t' || c == '\f') && !precedingBackslash) {
                valueStart = keyLen + 1;
                break;
            }
            if (c == '\\') precedingBackslash = !precedingBackslash;
            else precedingBackslash = false;
            keyLen++;
        }
        while (valueStart < limit) {
            char16_t c = lb[static_cast<size_t>(valueStart)];
            if (c != ' ' && c != '\t' && c != '\f') {
                if (!hasSep && (c == '=' || c == ':')) hasSep = true;
                else break;
            }
            valueStart++;
        }
        std::u16string key = loadConvert(lb, 0, keyLen);
        std::u16string value = loadConvert(lb, valueStart, limit - valueStart);
        put(String::fromUtf16(key), String::fromUtf16(value));
    }
}

void Properties::load(InputStream* inStream) {
    if (inStream == nullptr) throw NullPointerException();
    ByteSource src{inStream};
    JSYNC(this) { load0(src); }
}

void Properties::load(Reader* reader) {
    if (reader == nullptr) throw NullPointerException();
    CharSource src{reader};
    JSYNC(this) { load0(src); }
}

void Properties::store0(Writer* w, const String& comments, bool escUnicode) {
    auto* bw = dynamic_cast<BufferedWriter*>(w);
    if (bw == nullptr) bw = new BufferedWriter(w);
    if (!comments.isNull()) writeComments(bw, comments.toUtf16());
    bw->write(str("#", dateString()));
    bw->newLine();
    List<Entry<String, String>>* es = entrySet();
    for (auto& e : *es) {
        std::u16string key = saveConvert(e.getKey().toUtf16(), true, escUnicode);
        std::u16string val = saveConvert(e.getValue().toUtf16(), false, escUnicode);
        bw->write(String::fromUtf16(key + u"=" + val));
        bw->newLine();
    }
    bw->flush();
}

void Properties::store(Writer* writer, const String& comments) {
    if (writer == nullptr) throw NullPointerException();
    store0(writer, comments, false);
}

void Properties::store(OutputStream* out, const String& comments) {
    if (out == nullptr) throw NullPointerException();
    store0(new BufferedWriter(new OutputStreamWriter(out, StandardCharsets::ISO_8859_1())), comments, true);
}

void Properties::list(PrintStream* out) {
    out->println(String("-- listing properties --"));
    for (const String& key : *stringPropertyNames()) {
        String val = getProperty(key);
        if (val.length() > 40) val = str(val.substring(0, 37), "...");
        out->println(str(key, "=", val));
    }
}

void Properties::list(PrintWriter* out) {
    out->println(String("-- listing properties --"));
    for (const String& key : *stringPropertyNames()) {
        String val = getProperty(key);
        if (val.length() > 40) val = str(val.substring(0, 37), "...");
        out->println(str(key, "=", val));
    }
}

bool Properties::equals(Object* o) {
    if (o == static_cast<Object*>(this)) return true;
    auto* p = dynamic_cast<Properties*>(o);
    if (p == nullptr) return false;
    List<Entry<String, String>>* mine = entrySet();
    if (mine->size() != p->size()) return false;
    for (auto& e : *mine) {
        if (!p->containsKey(e.getKey())) return false;
        if (!p->get(e.getKey()).equals(e.getValue())) return false;
    }
    return true;
}

int32_t Properties::hashCode() {
    int32_t h = 0;
    for (auto& e : *entrySet())
        h = static_cast<int32_t>(static_cast<uint32_t>(h) +
                                 static_cast<uint32_t>(e.getKey().hashCode() ^ e.getValue().hashCode()));
    return h;
}

String Properties::toString() {
    std::string s = "{";
    bool first = true;
    for (auto& e : *entrySet()) {
        if (!first) s += ", ";
        first = false;
        s += std::string(e.getKey()) + "=" + std::string(e.getValue());
    }
    s += "}";
    return String(s);
}

// =======================================================================================
// ResourceBundle / PropertyResourceBundle

ResourceBundle::ResourceBundle(InputStream* stream) : props_(new Properties()) {
    if (stream == nullptr) throw NullPointerException();
    // Java 9+: UTF-8, falling back to ISO-8859-1 when the input is not valid UTF-8.
    Array<int8_t>* bytes = stream->readAllBytes();
    const auto* p = reinterpret_cast<const uint8_t*>(bytes->data());
    const size_t n = static_cast<size_t>(bytes->length);
    std::u16string u = detail::decodeBytes(Charset::UTF_8_KIND, p, n);
    std::string back = detail::encodeChars(Charset::UTF_8_KIND, u.data(), u.size());
    if (back.size() != n || std::memcmp(back.data(), p, n) != 0) {
        props_->load(new ByteArrayInputStream(bytes));
    } else {
        props_->load(new StringReader(String::fromUtf16(u)));
    }
}

ResourceBundle::ResourceBundle(Reader* reader) : props_(new Properties()) {
    if (reader == nullptr) throw NullPointerException();
    props_->load(reader);
}

ResourceBundle* ResourceBundle::getBundle(const String& baseName) {
    if (baseName.isNull()) throw NullPointerException();
    std::string path = std::string(baseName);
    for (char& c : path)
        if (c == '.') c = '/';
    std::string candidates[] = {path + ".properties", std::string(baseName) + ".properties"};
    for (const std::string& c : candidates) {
        File* f = new File(String(c));
        if (f->isFile()) {
            FileInputStream* in = new FileInputStream(f);
            ResourceBundle* rb;
            {
                JFINALLY { IOUtils::closeQuietly(in); };
                rb = new ResourceBundle(static_cast<InputStream*>(in));
            }
            rb->baseName_ = baseName;
            return rb;
        }
    }
    throw MissingResourceException(str("Can't find bundle for base name ", baseName, ", locale "), str(baseName),
                                   String(""));
}

Object* ResourceBundle::handleGetObject(const String& key) {
    if (key.isNull()) throw NullPointerException();
    String v = props_->get(key);
    if (v.isNull()) return nullptr;
    return box(v);
}

Object* ResourceBundle::getObject(const String& key) {
    for (ResourceBundle* b = this; b != nullptr; b = b->parent_) {
        Object* o = b->handleGetObject(key);
        if (o != nullptr) return o;
    }
    throw MissingResourceException(str("Can't find resource for bundle java.util.PropertyResourceBundle, key ", key),
                                   String("java.util.PropertyResourceBundle"), key);
}

String ResourceBundle::getString(const String& key) {
    if (key.isNull()) throw NullPointerException();
    for (ResourceBundle* b = this; b != nullptr; b = b->parent_) {
        String v = b->props_->get(key);
        if (!v.isNull()) return v;
    }
    throw MissingResourceException(str("Can't find resource for bundle java.util.PropertyResourceBundle, key ", key),
                                   String("java.util.PropertyResourceBundle"), key);
}

Array<String>* ResourceBundle::getStringArray(const String& key) {
    (void)getString(key);
    throw ClassCastException(String("java.lang.String cannot be cast to [Ljava.lang.String;"));
}

bool ResourceBundle::containsKey(const String& key) {
    if (key.isNull()) throw NullPointerException();
    for (ResourceBundle* b = this; b != nullptr; b = b->parent_)
        if (b->props_->containsKey(key)) return true;
    return false;
}

Set<String>* ResourceBundle::keySet() {
    auto* s = new Set<String>();
    for (ResourceBundle* b = this; b != nullptr; b = b->parent_)
        for (const String& k : *b->props_->keySet()) s->add(k);
    return s;
}

Iterator<String>* ResourceBundle::getKeys() { return keySet()->iterator(); }

}  // namespace jlang
