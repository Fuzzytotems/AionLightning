// jlang/Regex.cpp - java.util.regex on PCRE2 (8-bit, UTF mode): a Java -> PCRE2 pattern
// translator plus Matcher with Java's find/matches/lookingAt/replace/split semantics.
#include <jlang/Regex.h>

#include <jlang/Collections.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <cstring>
#include <string>
#include <vector>

namespace jlang {

namespace {

// ---------------------------------------------------------------------------------------
// PCRE2 memory on the GC heap (blocks are scanned: match data keeps its heap frames alive).
void* pcreMalloc(PCRE2_SIZE n, void*) { return detail::collMalloc(n == 0 ? 1 : n); }
void pcreFree(void*, void*) {}

pcre2_general_context* gctx() {
    static pcre2_general_context* g = pcre2_general_context_create(pcreMalloc, pcreFree, nullptr);
    return g;
}

pcre2_compile_context* cctx() {
    static pcre2_compile_context* c = [] {
        pcre2_compile_context* ctx = pcre2_compile_context_create(gctx());
        pcre2_set_newline(ctx, PCRE2_NEWLINE_LF);
        pcre2_set_bsr(ctx, PCRE2_BSR_UNICODE);
        return ctx;
    }();
    return c;
}

// ---------------------------------------------------------------------------------------
// UTF-8 helpers

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::vector<char32_t> toCodePoints(const String& s) {
    std::vector<char32_t> r;
    std::u16string u = s.toUtf16();
    for (size_t i = 0; i < u.size(); i++) {
        char32_t c = u[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (u[i + 1] - 0xDC00);
            i++;
        }
        r.push_back(c);
    }
    return r;
}

String hexCp(char32_t c) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "\\x{%X}", static_cast<unsigned>(c));
    return String(buf);
}

bool isAsciiAlnum(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
bool isAsciiAlpha(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAsciiDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool isJavaSpace(char32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == 0x0B || c == '\f' || c == '\r'; }

constexpr int32_t kUnicodeCaseBits = 0x40 | 0x100;  // UNICODE_CASE | UNICODE_CHARACTER_CLASS
// CASE_INSENSITIVE alone folds ASCII letters only (done by the translator); with UNICODE_CASE
// (or UNICODE_CHARACTER_CLASS) PCRE2's caseless mode is used.
bool pcreCaseless(int32_t f) { return (f & 0x02) != 0 && (f & kUnicodeCaseBits) != 0; }
bool asciiCaseless(int32_t f) { return (f & 0x02) != 0 && (f & kUnicodeCaseBits) == 0; }
char32_t swapAsciiCase(char32_t c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

// A literal code point outside a character class.
void emitLiteral(std::string& o, char32_t c) {
    if (isAsciiAlnum(c) || c >= 0x80) {
        if (c >= 0x80 && (c < 0xA0 || (c >= 0xD800 && c <= 0xDFFF))) {
            o += std::string(hexCp(c));
        } else {
            appendUtf8(o, c);
        }
    } else if (c < 0x20 || c == 0x7F) {
        o += std::string(hexCp(c));
    } else {
        o.push_back('\\');
        o.push_back(static_cast<char>(c));
    }
}

// A literal code point inside a character class.
void emitClassLiteral(std::string& o, char32_t c) { emitLiteral(o, c); }

// ---------------------------------------------------------------------------------------
// Java's RemoveQEQuoting (applied before parsing, like java.util.regex.Pattern does).
std::vector<char32_t> removeQEQuoting(const std::vector<char32_t>& t) {
    const size_t pLen = t.size();
    size_t i = 0;
    while (i + 1 < pLen) {
        if (t[i] != '\\') i += 1;
        else if (t[i + 1] != 'Q') i += 2;
        else break;
    }
    if (pLen < 2 || i >= pLen - 1) return t;
    std::vector<char32_t> n(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(i));
    i += 2;
    bool inQuote = true;
    bool beginQuote = true;
    while (i < pLen) {
        char32_t c = t[i++];
        if (c >= 0x80 || isAsciiAlpha(c)) {
            n.push_back(c);
        } else if (isAsciiDigit(c)) {
            if (beginQuote) {
                n.push_back('\\');
                n.push_back('x');
                n.push_back('3');
            }
            n.push_back(c);
        } else if (c != '\\') {
            if (inQuote) n.push_back('\\');
            n.push_back(c);
        } else if (inQuote) {
            if (i < pLen && t[i] == 'E') {
                i++;
                inQuote = false;
            } else {
                n.push_back('\\');
                n.push_back('\\');
            }
        } else {
            if (i < pLen && t[i] == 'Q') {
                i++;
                inQuote = true;
                beginQuote = true;
                continue;
            }
            n.push_back(c);
            if (i != pLen) n.push_back(t[i++]);
        }
        beginQuote = false;
    }
    return n;
}

// ---------------------------------------------------------------------------------------
// Character class expression tree
struct ClassExpr {
    enum Kind { SET, UNION, INTER, NEG } kind = SET;
    std::string items;  // SET: PCRE2 bracket content
    std::vector<ClassExpr> kids;
};

bool isPlainSet(const ClassExpr& e) { return e.kind == ClassExpr::SET; }

std::string emitClass(const ClassExpr& e) {
    switch (e.kind) {
        case ClassExpr::SET:
            return "[" + e.items + "]";
        case ClassExpr::UNION: {
            bool allSets = true;
            for (const auto& k : e.kids)
                if (!isPlainSet(k)) allSets = false;
            if (allSets) {
                std::string it;
                for (const auto& k : e.kids) it += k.items;
                return "[" + it + "]";
            }
            std::string s = "(?:";
            for (size_t i = 0; i < e.kids.size(); i++) {
                if (i) s += "|";
                s += emitClass(e.kids[i]);
            }
            return s + ")";
        }
        case ClassExpr::INTER:
            return "(?:(?=" + emitClass(e.kids[0]) + ")" + emitClass(e.kids[1]) + ")";
        case ClassExpr::NEG: {
            const ClassExpr& k = e.kids[0];
            if (isPlainSet(k)) return "[^" + k.items + "]";
            if (k.kind == ClassExpr::UNION) {
                bool allSets = true;
                for (const auto& kk : k.kids)
                    if (!isPlainSet(kk)) allSets = false;
                if (allSets) {
                    std::string it;
                    for (const auto& kk : k.kids) it += kk.items;
                    return "[^" + it + "]";
                }
            }
            return "(?:(?!" + emitClass(k) + ")[\\s\\S])";
        }
    }
    return "";
}

ClassExpr makeSet(const std::string& items) {
    ClassExpr e;
    e.kind = ClassExpr::SET;
    e.items = items;
    return e;
}

ClassExpr unionOf(ClassExpr a, ClassExpr b) {
    if (a.kind == ClassExpr::SET && b.kind == ClassExpr::SET) return makeSet(a.items + b.items);
    ClassExpr e;
    e.kind = ClassExpr::UNION;
    if (a.kind == ClassExpr::UNION) e.kids = a.kids;
    else e.kids.push_back(std::move(a));
    e.kids.push_back(std::move(b));
    return e;
}

ClassExpr interOf(ClassExpr a, ClassExpr b) {
    ClassExpr e;
    e.kind = ClassExpr::INTER;
    e.kids.push_back(std::move(a));
    e.kids.push_back(std::move(b));
    return e;
}

ClassExpr negOf(ClassExpr a) {
    ClassExpr e;
    e.kind = ClassExpr::NEG;
    e.kids.push_back(std::move(a));
    return e;
}

// ---------------------------------------------------------------------------------------
// The translator
struct SyntaxError {
    std::string desc;
    int32_t index;
};

class Translator {
public:
    Translator(const std::vector<char32_t>& p, int32_t flags) : t_(p), flags_(flags) {}

    bool usesUnicodeClasses() const { return usedU_; }

    std::string run() {
        countGroups();
        if (flags_ & Pattern::UNICODE_CHARACTER_CLASS) usedU_ = true;
        if (pcreCaseless(flags_)) out_ += "(?i)";
        parseSeq(false);
        if (pos_ < t_.size()) {
            // Only an unmatched ')' stops the top-level sequence.
            throw SyntaxError{"Unmatched closing ')'", static_cast<int32_t>(pos_ > 0 ? pos_ - 1 : 0)};
        }
        return out_;
    }

private:
    // ---- input
    bool comments() const { return (flags_ & Pattern::COMMENTS) != 0; }
    bool atEnd() const { return pos_ >= t_.size(); }
    char32_t raw(size_t i) const { return i < t_.size() ? t_[i] : 0; }

    bool isLineSep(char32_t c) const {
        if (flags_ & Pattern::UNIX_LINES) return c == '\n';
        return c == '\n' || c == '\r' || (c | 1) == 0x2029 || c == 0x85;
    }
    void skipComments() {
        if (!comments()) return;
        for (;;) {
            while (pos_ < t_.size() && isJavaSpace(t_[pos_])) pos_++;
            if (pos_ < t_.size() && t_[pos_] == '#') {
                while (pos_ < t_.size() && !isLineSep(t_[pos_])) pos_++;
                continue;
            }
            break;
        }
    }
    char32_t peek() {
        skipComments();
        return raw(pos_);
    }
    char32_t next() {
        skipComments();
        char32_t c = raw(pos_);
        if (pos_ < t_.size()) pos_++;
        return c;
    }
    [[noreturn]] void error(const std::string& d) {
        throw SyntaxError{d, static_cast<int32_t>(pos_ > 0 ? pos_ - 1 : 0)};
    }

    // Pre-count the capturing groups: '(' not followed by '?', and "(?<name>".
    void countGroups() {
        bool inClass = false;
        int depth = 0;
        for (size_t i = 0; i < t_.size(); i++) {
            char32_t c = t_[i];
            if (c == '\\') {
                i++;
                continue;
            }
            if (inClass) {
                if (c == '[') depth++;
                else if (c == ']') {
                    if (depth == 0) inClass = false;
                    else depth--;
                }
                continue;
            }
            if (c == '[') {
                inClass = true;
                depth = 0;
                if (raw(i + 1) == '^') i++;
                if (raw(i + 1) == ']') i++;
                continue;
            }
            if (c == '(') {
                if (raw(i + 1) != '?') totalGroups_++;
                else if (raw(i + 2) == '<' && raw(i + 3) != '=' && raw(i + 3) != '!') totalGroups_++;
            }
        }
    }

    // ---- sequence / alternation
    void parseSeq(bool inGroup) {
        haveAtom_ = false;
        lastQuant_ = false;
        for (;;) {
            char32_t c = peek();
            if (atEnd()) return;
            switch (c) {
                case '|':
                    next();
                    out_ += '|';
                    haveAtom_ = false;
                    lastQuant_ = false;
                    continue;
                case ')':
                    if (inGroup) return;
                    next();
                    error("Unmatched closing ')'");
                case '(':
                    parseGroup();
                    continue;
                case '[': {
                    next();
                    size_t at = out_.size();
                    ClassExpr e = parseClass(true);
                    out_ += emitClass(e);
                    atom(at);
                    continue;
                }
                case '.': {
                    next();
                    size_t at = out_.size();
                    if (flags_ & Pattern::DOTALL) out_ += "(?s:.)";
                    else if (flags_ & Pattern::UNIX_LINES) out_ += "[^\\n]";
                    else out_ += "[^\\n\\r\\x{85}\\x{2028}\\x{2029}]";
                    atom(at);
                    continue;
                }
                case '^': {
                    next();
                    size_t at = out_.size();
                    if (flags_ & Pattern::MULTILINE) {
                        if (flags_ & Pattern::UNIX_LINES) out_ += "(?:(?:\\A|(?<=\\n))(?=[\\s\\S]))";
                        else
                            out_ += "(?:(?:\\A|(?<=[\\n\\x{85}\\x{2028}\\x{2029}])|(?<=\\r)(?!\\n))(?=[\\s\\S]))";
                    } else {
                        out_ += "(?:\\A)";
                    }
                    atom(at);
                    continue;
                }
                case '$': {
                    next();
                    size_t at = out_.size();
                    emitDollar((flags_ & Pattern::MULTILINE) != 0);
                    atom(at);
                    continue;
                }
                case '*':
                case '+':
                case '?': {
                    next();
                    if (!haveAtom_ || lastQuant_) {
                        std::string d = "Dangling meta character '";
                        appendUtf8(d, c);
                        d += "'";
                        error(d);
                    }
                    appendUtf8(out_, c);
                    quantSuffix(true);
                    lastQuant_ = true;
                    continue;
                }
                case '{': {
                    next();
                    std::string q = parseCurly();
                    if (!haveAtom_ || lastQuant_) {
                        // Java applies a quantifier after a quantifier (or at the start) to an
                        // empty node: it matches the empty string.
                        quantSuffix(false);
                        continue;
                    }
                    out_ += q;
                    quantSuffix(true);
                    lastQuant_ = true;
                    continue;
                }
                case '\\': {
                    next();
                    size_t at = out_.size();
                    escapeOutside();
                    if (out_.size() > at) atom(at);
                    continue;
                }
                default: {
                    next();
                    size_t at = out_.size();
                    lit(c);
                    atom(at);
                    continue;
                }
            }
        }
    }

    void lit(char32_t c) {
        if (asciiCaseless(flags_) && swapAsciiCase(c) != c) {
            out_ += '[';
            emitClassLiteral(out_, c);
            emitClassLiteral(out_, swapAsciiCase(c));
            out_ += ']';
        } else {
            emitLiteral(out_, c);
        }
    }
    void classChar(std::string& bits, char32_t c) {
        emitClassLiteral(bits, c);
        if (asciiCaseless(flags_) && swapAsciiCase(c) != c) emitClassLiteral(bits, swapAsciiCase(c));
    }
    void classRange(std::string& bits, char32_t lo, char32_t hi) {
        emitClassLiteral(bits, lo);
        bits += '-';
        emitClassLiteral(bits, hi);
        if (!asciiCaseless(flags_)) return;
        auto addSwapped = [&](char32_t a, char32_t b, char32_t from, char32_t to) {
            char32_t l = lo > a ? lo : a, h = hi < b ? hi : b;
            if (l > h) return;
            emitClassLiteral(bits, l - from + to);
            bits += '-';
            emitClassLiteral(bits, h - from + to);
        };
        addSwapped('a', 'z', 'a', 'A');
        addSwapped('A', 'Z', 'A', 'a');
    }

    void atom(size_t) {
        haveAtom_ = true;
        lastQuant_ = false;
    }

    void quantSuffix(bool emit) {
        if (atEnd()) return;
        char32_t c = raw(pos_);
        if (c == '?' || c == '+') {
            pos_++;
            if (emit) out_ += static_cast<char>(c);
        }
    }

    // After '{': returns "{n}", "{n,}" or "{n,m}".
    std::string parseCurly() {
        char32_t c = peek();
        if (!isAsciiDigit(c)) error("Illegal repetition");
        auto readNum = [&](int64_t& v) {
            v = 0;
            bool any = false;
            while (isAsciiDigit(peek())) {
                any = true;
                v = v * 10 + (next() - '0');
                if (v > 0x7fffffff) error("Illegal repetition range");
            }
            return any;
        };
        int64_t cmin = 0, cmax = -1;
        readNum(cmin);
        c = peek();
        std::string q = "{" + std::to_string(cmin);
        if (c == ',') {
            next();
            q += ",";
            if (peek() != '}') {
                if (!isAsciiDigit(peek())) error("Unclosed counted closure");
                readNum(cmax);
                if (cmax < cmin) error("Illegal repetition range");
                q += std::to_string(cmax);
            }
        } else {
            cmax = cmin;
        }
        if (peek() != '}') {
            pos_ = t_.size();
            error("Unclosed counted closure");
        }
        next();
        // PCRE2 limits repeat counts to 65535; larger counts behave like "unbounded" here.
        if (cmin > 65535) error("Illegal repetition range");
        if (cmax > 65535) q = "{" + std::to_string(cmin) + ",";
        return q + "}";
    }

    void emitDollar(bool multiline) {
        const bool unix = (flags_ & Pattern::UNIX_LINES) != 0;
        if (unix) {
            out_ += multiline ? "(?=\\n|\\z)" : "(?=\\n?\\z)";
        } else if (multiline) {
            out_ += "(?:(?=[\\n\\r\\x{85}\\x{2028}\\x{2029}]|\\z)(?!(?<=\\r)\\n))";
        } else {
            out_ += "(?:(?=(?:\\r\\n|[\\n\\r\\x{85}\\x{2028}\\x{2029}])?\\z)(?!(?<=\\r)\\n))";
        }
    }

    // ---- groups
    void parseGroup() {
        next();  // '('
        int32_t saved = flags_;
        if (raw(pos_) == '?') {
            pos_++;
            char32_t c = raw(pos_);
            if (c == ':' || c == '=' || c == '!' || c == '>') {
                pos_++;
                out_ += "(?";
                out_ += static_cast<char>(c);
            } else if (c == '<') {
                pos_++;
                char32_t d = raw(pos_);
                if (d == '=' || d == '!') {
                    pos_++;
                    out_ += "(?<";
                    out_ += static_cast<char>(d);
                } else {
                    std::string name = groupName();
                    for (const auto& n : names_)
                        if (n == name) error("Named capturing group <" + name + "> is already defined");
                    names_.push_back(name);
                    groupsSoFar_++;
                    out_ += "(?<" + name + ">";
                }
            } else {
                // inline flags: (?idmsuxU-idmsuxU) or (?idmsuxU-idmsuxU:X)
                int32_t f = flags_;
                bool neg = false;
                for (;;) {
                    c = raw(pos_);
                    int32_t bit = flagBit(c);
                    if (c == '-') {
                        neg = true;
                        pos_++;
                        continue;
                    }
                    if (bit == 0) break;
                    pos_++;
                    if (neg) f &= ~bit;
                    else f |= bit;
                }
                if (c == ')') {
                    pos_++;
                    setFlagsInline(f);
                    return;  // not an atom; flags stay for the rest of the enclosing group
                }
                if (c != ':') {
                    pos_++;
                    error("Unknown inline modifier");
                }
                pos_++;
                if (f & Pattern::UNICODE_CHARACTER_CLASS) usedU_ = true;
                bool ci = pcreCaseless(f);
                bool wasCi = pcreCaseless(flags_);
                out_ += ci == wasCi ? "(?:" : (ci ? "(?i:" : "(?-i:");
                flags_ = f;
            }
        } else {
            groupsSoFar_++;
            out_ += "(";
        }
        size_t at = out_.size();
        parseSeq(true);
        if (atEnd() || raw(pos_) != ')') {
            pos_ = t_.size() + 1;
            error("Unclosed group");
        }
        pos_++;
        out_ += ")";
        flags_ = saved;
        atom(at);
    }

    static int32_t flagBit(char32_t c) {
        switch (c) {
            case 'i': return Pattern::CASE_INSENSITIVE;
            case 'm': return Pattern::MULTILINE;
            case 's': return Pattern::DOTALL;
            case 'd': return Pattern::UNIX_LINES;
            case 'u': return Pattern::UNICODE_CASE;
            case 'c': return Pattern::CANON_EQ;
            case 'x': return Pattern::COMMENTS;
            case 'U': return Pattern::UNICODE_CHARACTER_CLASS;
            default: return 0;
        }
    }

    void setFlagsInline(int32_t f) {
        if (f & Pattern::UNICODE_CHARACTER_CLASS) usedU_ = true;
        bool ci = pcreCaseless(f);
        bool wasCi = pcreCaseless(flags_);
        if (ci != wasCi) out_ += ci ? "(?i)" : "(?-i)";
        flags_ = f;
    }

    std::string groupName() {
        std::string name;
        char32_t c = raw(pos_);
        if (!isAsciiAlpha(c)) {
            pos_++;
            error("capturing group name does not start with a Latin letter");
        }
        while (pos_ < t_.size() && isAsciiAlnum(t_[pos_])) name.push_back(static_cast<char>(t_[pos_++]));
        if (raw(pos_) != '>') {
            pos_++;
            error("named capturing group is missing trailing '>'");
        }
        pos_++;
        return name;
    }

    // ---- escapes outside character classes
    void escapeOutside() {
        char32_t c = raw(pos_);
        if (atEnd()) error("Unexpected internal error");
        pos_++;
        switch (c) {
            case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': {
                int32_t ref = static_cast<int32_t>(c - '0');
                while (isAsciiDigit(raw(pos_))) {
                    int32_t nr = ref * 10 + static_cast<int32_t>(raw(pos_) - '0');
                    if (groupsSoFar_ < nr) break;
                    ref = nr;
                    pos_++;
                }
                if (ref <= totalGroups_)
                    out_ += (asciiCaseless(flags_) ? "(?i:\\g{" : "(?:\\g{") + std::to_string(ref) + "})";
                else out_ += "(?!)";
                return;
            }
            case 'k': {
                if (raw(pos_) != '<') error("\\k is not followed by '<' for named capturing group");
                pos_++;
                std::string name = groupName();
                bool found = false;
                for (const auto& n : names_)
                    if (n == name) found = true;
                if (!found) error("named capturing group <" + name + "> does not exist");
                out_ += (asciiCaseless(flags_) ? "(?i:\\k<" : "(?:\\k<") + name + ">)";
                return;
            }
            case 'b':
                if (raw(pos_) == '{') error("Unsupported \\b{g} grapheme boundary");
                out_ += "(?:\\b)";
                return;
            case 'B': out_ += "(?:\\B)"; return;
            case 'A': out_ += "(?:\\A)"; return;
            case 'G': out_ += "(?:\\G)"; return;
            case 'z': out_ += "(?:\\z)"; return;
            case 'Z': emitDollar(false); return;
            case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
            case 'h': case 'H': case 'v': case 'V': case 'R': case 'X':
                out_ += '\\';
                out_ += static_cast<char>(c);
                return;
            case 'p':
            case 'P': {
                std::string item = property(c == 'P');
                out_ += "[" + item + "]";
                return;
            }
            default: {
                char32_t v = escapedChar(c, false);
                lit(v);
                return;
            }
        }
    }

    // Escapes that denote a single character (shared by class and non-class parsing).
    char32_t escapedChar(char32_t c, bool inClass) {
        (void)inClass;
        switch (c) {
            case '0': return octal();
            case 't': return '\t';
            case 'n': return '\n';
            case 'r': return '\r';
            case 'f': return '\f';
            case 'a': return 0x07;
            case 'e': return 0x1B;
            case 'c': {
                if (atEnd()) error("Illegal control escape sequence");
                return raw(pos_++) ^ 64;
            }
            case 'x': return hexEscape();
            case 'u': return unicodeEscape();
            default:
                if (isAsciiAlnum(c)) error("Illegal/unsupported escape sequence");
                return c;  // escaped punctuation / non-ASCII
        }
    }

    char32_t octal() {
        auto isOct = [](char32_t d) { return d >= '0' && d <= '7'; };
        char32_t n = raw(pos_);
        if (!isOct(n) || atEnd()) {
            pos_++;
            error("Illegal octal escape sequence");
        }
        pos_++;
        char32_t m = raw(pos_);
        if (pos_ < t_.size() && isOct(m)) {
            pos_++;
            char32_t o = raw(pos_);
            if (pos_ < t_.size() && isOct(o) && n <= '3') {
                pos_++;
                return (n - '0') * 64 + (m - '0') * 8 + (o - '0');
            }
            return (n - '0') * 8 + (m - '0');
        }
        return n - '0';
    }

    static int hexVal(char32_t c) {
        if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A' + 10);
        return -1;
    }

    char32_t hexEscape() {
        char32_t c = raw(pos_);
        if (hexVal(c) >= 0 && pos_ < t_.size()) {
            char32_t d = raw(pos_ + 1);
            if (hexVal(d) >= 0 && pos_ + 1 < t_.size()) {
                pos_ += 2;
                return static_cast<char32_t>(hexVal(c) * 16 + hexVal(d));
            }
        } else if (c == '{') {
            pos_++;
            uint32_t v = 0;
            size_t start = pos_;
            while (pos_ < t_.size() && hexVal(t_[pos_]) >= 0) {
                v = v * 16 + static_cast<uint32_t>(hexVal(t_[pos_]));
                if (v > 0x10FFFF) error("Hexadecimal codepoint is too big");
                pos_++;
            }
            if (raw(pos_) != '}' || pos_ == start) error("Unclosed hexadecimal escape sequence");
            pos_++;
            return v;
        }
        error("Illegal hexadecimal escape sequence");
    }

    char32_t unicodeEscape() {
        auto read4 = [&]() -> int32_t {
            int32_t v = 0;
            for (int i = 0; i < 4; i++) {
                int h = hexVal(raw(pos_));
                if (h < 0 || pos_ >= t_.size()) error("Illegal Unicode escape sequence");
                v = v * 16 + h;
                pos_++;
            }
            return v;
        };
        int32_t v = read4();
        if (v >= 0xD800 && v <= 0xDBFF && raw(pos_) == '\\' && raw(pos_ + 1) == 'u') {
            size_t save = pos_;
            pos_ += 2;
            bool ok = true;
            for (int i = 0; i < 4; i++)
                if (hexVal(raw(pos_ + static_cast<size_t>(i))) < 0) ok = false;
            if (ok) {
                int32_t lo = read4();
                if (lo >= 0xDC00 && lo <= 0xDFFF) return static_cast<char32_t>(0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00));
            }
            pos_ = save;
        }
        return static_cast<char32_t>(v);
    }

    // \p{Name} / \pL / \P... : returns PCRE2 class item text.
    std::string property(bool comp) {
        std::string name;
        if (raw(pos_) == '{') {
            pos_++;
            while (pos_ < t_.size() && t_[pos_] != '}') appendUtf8(name, t_[pos_++]);
            if (atEnd()) error("Unclosed character family");
            pos_++;
        } else {
            if (atEnd()) error("Illegal character family");
            appendUtf8(name, t_[pos_++]);
        }
        if (name.empty()) error("Empty character family");
        return propertyItem(name, comp);
    }

    std::string propertyItem(std::string name, bool comp) {
        auto posix = [&](const char* pn) { return std::string(comp ? "[:^" : "[:") + pn + ":]"; };
        auto prop = [&](const std::string& pn) { return std::string(comp ? "\\P{" : "\\p{") + pn + "}"; };
        // Several PCRE2 properties OR-ed: only expressible un-negated inside a class; for
        // negation use a lookahead construct handled by the caller through NEG sets.
        auto multi = [&](const std::string& items) -> std::string {
            if (!comp) return items;
            negatedMulti_ = items;
            return "";
        };
        size_t eq = name.find('=');
        if (eq != std::string::npos) {
            std::string key = name.substr(0, eq), val = name.substr(eq + 1);
            if (key == "sc" || key == "script") return prop(val);
            if (key == "blk" || key == "block") return prop(val);
            if (key == "gc" || key == "general_category") return prop(val);
            error("Unknown Unicode property {name=<" + key + ">, value=<" + val + ">}");
        }
        static const char* const kPosix[][2] = {
            {"Lower", "lower"}, {"Upper", "upper"}, {"ASCII", "ascii"}, {"Alpha", "alpha"}, {"Digit", "digit"},
            {"Alnum", "alnum"}, {"Punct", "punct"}, {"Graph", "graph"}, {"Print", "print"}, {"Blank", "blank"},
            {"Cntrl", "cntrl"}, {"XDigit", "xdigit"}, {"Space", "space"},
        };
        for (const auto& p : kPosix) {
            if (name == p[0]) {
                if (asciiCaseless(flags_) && (name == "Lower" || name == "Upper")) return posix("alpha");
                return posix(p[1]);
            }
        }
        if (name.rfind("In", 0) == 0 && name.size() > 2) return prop(name.substr(2));
        std::string n = name;
        if (n.rfind("Is", 0) == 0 && n.size() > 2) {
            n = n.substr(2);
            if (n == "Alphabetic") return multi("\\p{L}\\p{Nl}");
            if (n == "Letter") return prop("L");
            if (n == "Ideographic") return prop("Han");
            if (n == "Lowercase") return prop("Ll");
            if (n == "Uppercase") return prop("Lu");
            if (n == "Titlecase") return prop("Lt");
            if (n == "Punctuation") return prop("P");
            if (n == "Control") return prop("Cc");
            if (n == "White_Space" || n == "WhiteSpace" || n == "White Space")
                return multi("\\x{9}-\\x{D}\\x{20}\\x{85}\\x{A0}\\x{1680}\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}");
            if (n == "Digit") return prop("Nd");
            if (n == "Hex_Digit" || n == "HexDigit") return multi("0-9A-Fa-f\\x{FF10}-\\x{FF19}\\x{FF21}-\\x{FF26}\\x{FF41}-\\x{FF46}");
            if (n == "Join_Control" || n == "JoinControl") return multi("\\x{200C}\\x{200D}");
            if (n == "Assigned") return comp ? std::string("\\p{Cn}") : std::string("\\P{Cn}");
            return prop(n);  // category (IsLu) or script (IsLatin)
        }
        if (n == "LC") return prop("L&");
        if (n == "LD") return multi("\\p{L}\\p{Nd}");
        if (n == "L1") return multi("\\x{0}-\\x{FF}");
        if (n == "all") return multi("\\x{0}-\\x{10FFFF}");
        if (n == "javaLowerCase") return prop("Ll");
        if (n == "javaUpperCase") return prop("Lu");
        if (n == "javaTitleCase") return prop("Lt");
        if (n == "javaDigit") return prop("Nd");
        if (n == "javaDefined") return comp ? std::string("\\p{Cn}") : std::string("\\P{Cn}");
        if (n == "javaLetter") return prop("L");
        if (n == "javaLetterOrDigit") return multi("\\p{L}\\p{Nd}");
        if (n == "javaAlphabetic") return multi("\\p{L}\\p{Nl}");
        if (n == "javaIdeographic") return prop("Han");
        if (n == "javaSpaceChar") return prop("Z");
        if (n == "javaWhitespace")
            return multi("\\x{9}-\\x{D}\\x{1C}-\\x{20}\\x{1680}\\x{2000}-\\x{2006}\\x{2008}-\\x{200A}\\x{2028}\\x{2029}\\x{205F}\\x{3000}");
        if (n == "javaISOControl") return multi("\\x{0}-\\x{1F}\\x{7F}-\\x{9F}");
        if (n == "javaIdentifierIgnorable") return multi("\\x{0}-\\x{8}\\x{E}-\\x{1B}\\x{7F}-\\x{9F}\\p{Cf}");
        if (n == "javaJavaIdentifierStart" || n == "javaUnicodeIdentifierStart") return multi("\\p{L}\\p{Nl}\\p{Sc}\\p{Pc}");
        if (n == "javaJavaIdentifierPart" || n == "javaUnicodeIdentifierPart")
            return multi("\\p{L}\\p{Nl}\\p{Sc}\\p{Pc}\\p{Nd}\\p{Mn}\\p{Mc}\\x{0}-\\x{8}\\x{E}-\\x{1B}\\x{7F}-\\x{9F}\\p{Cf}");
        if (n == "javaMirrored") return multi("\\p{Ps}\\p{Pe}\\p{Pi}\\p{Pf}");
        static const char* const kCats[] = {"L", "Lu", "Ll", "Lt", "Lm", "Lo", "M", "Mn", "Mc", "Me", "N", "Nd", "Nl",
                                            "No", "Z", "Zs", "Zl", "Zp", "C", "Cc", "Cf", "Co", "Cs", "Cn", "P", "Pd",
                                            "Ps", "Pe", "Pc", "Po", "Pi", "Pf", "S", "Sm", "Sc", "Sk", "So"};
        for (const char* c : kCats)
            if (n == c) return prop(n);
        error("Unknown character property name {" + name + "}");
    }

    // ---- character classes (after '['); Java 9+ semantics (negation of the whole class).
    ClassExpr parseClass(bool consume) {
        bool have = false;
        ClassExpr prev;
        std::string bits;  // accumulated simple items
        bool hasBits = false;
        bool isNeg = false;
        char32_t ch = peek();
        if (ch == '^' && pos_ > 0 && t_[pos_ - 1] == '[') {
            next();
            isNeg = true;
        }
        auto addPrev = [&](ClassExpr e) {
            if (!have) {
                prev = std::move(e);
                have = true;
            } else {
                prev = unionOf(std::move(prev), std::move(e));
            }
        };
        for (;;) {
            ch = peek();
            if (atEnd()) {
                pos_ = t_.size() + 1;
                error("Unclosed character class");
            }
            if (ch == '[') {
                next();
                addPrev(parseClass(true));
                continue;
            }
            if (ch == '&' && raw(pos_ + 1) == '&') {
                next();
                next();
                bool haveRight = false;
                ClassExpr right;
                for (;;) {
                    ch = peek();
                    if (atEnd()) {
                        pos_ = t_.size() + 1;
                        error("Unclosed character class");
                    }
                    if (ch == ']' || ch == '&') break;
                    ClassExpr r;
                    if (ch == '[') {
                        next();
                        r = parseClass(true);
                    } else {
                        r = parseClass(false);
                    }
                    if (!haveRight) {
                        right = std::move(r);
                        haveRight = true;
                    } else {
                        right = unionOf(std::move(right), std::move(r));
                    }
                }
                if (hasBits) {
                    addPrev(makeSet(bits));
                    bits.clear();
                    hasBits = false;
                }
                if (!have) {
                    if (!haveRight) error("Bad class syntax");
                    prev = std::move(right);
                    have = true;
                } else if (haveRight) {
                    prev = interOf(std::move(prev), std::move(right));
                }
                continue;
            }
            if (ch == ']' && (have || hasBits)) {
                if (consume) next();
                if (hasBits) addPrev(makeSet(bits));
                if (isNeg) return negOf(std::move(prev));
                return prev;
            }
            // a single item (possibly a range) into bits
            classItem(bits, have, prev, hasBits);
        }
    }

    void classItem(std::string& bits, bool& have, ClassExpr& prev, bool& hasBits) {
        char32_t ch = peek();
        char32_t lo;
        if (ch == '\\') {
            next();
            char32_t e = raw(pos_);
            if (atEnd()) error("Unexpected internal error");
            pos_++;
            switch (e) {
                case 'd': case 'D': case 's': case 'S': case 'w': case 'W': case 'h': case 'H': case 'v': case 'V':
                    bits += '\\';
                    bits += static_cast<char>(e);
                    hasBits = true;
                    return;
                case 'p':
                case 'P': {
                    negatedMulti_.clear();
                    std::string item = property(e == 'P');
                    if (!negatedMulti_.empty()) {
                        ClassExpr n = negOf(makeSet(negatedMulti_));
                        negatedMulti_.clear();
                        if (!have) {
                            prev = std::move(n);
                            have = true;
                        } else {
                            prev = unionOf(std::move(prev), std::move(n));
                        }
                        return;
                    }
                    bits += item;
                    hasBits = true;
                    return;
                }
                case 'R':
                case 'X':
                case 'b':
                case 'B':
                case 'A':
                case 'G':
                case 'Z':
                case 'z':
                case 'k':
                    error("Illegal/unsupported escape sequence");
                default:
                    if (e >= '1' && e <= '9') error("Illegal/unsupported escape sequence");
                    lo = escapedChar(e, true);
                    break;
            }
        } else {
            next();
            lo = ch;
        }
        // range?
        if (peek() == '-') {
            char32_t endRange = raw(pos_ + 1);
            if (endRange == '[') {
                classChar(bits, lo);
                hasBits = true;
                return;
            }
            if (endRange != ']' && pos_ + 1 < t_.size()) {
                next();  // '-'
                char32_t m = peek();
                if (m == '\\') {
                    next();
                    char32_t e = raw(pos_);
                    pos_++;
                    m = escapedChar(e, true);
                } else {
                    next();
                }
                if (m < lo) error("Illegal character range");
                classRange(bits, lo, m);
                hasBits = true;
                return;
            }
        }
        classChar(bits, lo);
        hasBits = true;
    }

    const std::vector<char32_t>& t_;
    size_t pos_ = 0;
    int32_t flags_;
    std::string out_;
    int32_t totalGroups_ = 0;
    int32_t groupsSoFar_ = 0;
    std::vector<std::string> names_;
    bool haveAtom_ = false;
    bool lastQuant_ = false;
    bool usedU_ = false;
    std::string negatedMulti_;
};

std::string literalPattern(const std::vector<char32_t>& t, int32_t flags) {
    std::string o;
    if (pcreCaseless(flags)) o += "(?i)";
    for (char32_t c : t) {
        if (asciiCaseless(flags) && swapAsciiCase(c) != c) {
            o += '[';
            emitLiteral(o, c);
            emitLiteral(o, swapAsciiCase(c));
            o += ']';
        } else {
            emitLiteral(o, c);
        }
    }
    return o;
}

}  // namespace

// =======================================================================================
// PatternSyntaxException

String PatternSyntaxException::getMessage() {
    std::string sb = std::string(*desc_);
    if (index_ >= 0) sb += " near index " + std::to_string(index_);
    sb += "\n";
    sb += std::string(*pattern_);
    if (index_ >= 0 && !pattern_->isNull() && index_ < pattern_->length()) {
        sb += "\n";
        sb.append(static_cast<size_t>(index_), ' ');
        sb += "^";
    }
    return String(sb);
}

// =======================================================================================
// Pattern

Pattern::Pattern(const String& regex, int32_t flags) : pattern_(regex), flags_(flags) {
    if (regex.isNull()) throw NullPointerException();
    if ((flags & ~0x1FF) != 0) throw IllegalArgumentException(String("Unknown flag 0x") + String::format("%x", flags));
    std::vector<char32_t> cps = toCodePoints(regex);
    std::string tr;
    bool ucp = (flags & UNICODE_CHARACTER_CLASS) != 0;
    if (flags & LITERAL) {
        tr = literalPattern(cps, flags);
    } else {
        std::vector<char32_t> q = removeQEQuoting(cps);
        try {
            Translator t(q, flags);
            tr = t.run();
            ucp = ucp || t.usesUnicodeClasses();
        } catch (SyntaxError& e) {
            throw PatternSyntaxException(String(e.desc), regex, e.index);
        }
    }
    translated_ = String(tr);
    int err = 0;
    PCRE2_SIZE erroff = 0;
    pcre2_code* code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(tr.data()), tr.size(),
                                     PCRE2_UTF | PCRE2_MATCH_INVALID_UTF | PCRE2_ALLOW_EMPTY_CLASS | (ucp ? PCRE2_UCP : 0u), &err, &erroff, cctx());
    if (code == nullptr) {
        PCRE2_UCHAR buf[256];
        pcre2_get_error_message(err, buf, sizeof buf);
        std::string msg(reinterpret_cast<char*>(buf));
        if (msg.find("unknown property") != std::string::npos) msg = "Unknown character property name {" + msg + "}";
        throw PatternSyntaxException(String(msg), regex, -1);
    }
    code_ = code;
    uint32_t cnt = 0;
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &cnt);
    groupCount_ = static_cast<int32_t>(cnt);
    uint32_t nameCount = 0, entrySize = 0;
    PCRE2_SPTR table = nullptr;
    pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &nameCount);
    if (nameCount > 0) {
        pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &entrySize);
        pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &table);
        for (uint32_t i = 0; i < nameCount; i++) {
            PCRE2_SPTR e = table + i * entrySize;
            int32_t num = (e[0] << 8) | e[1];
            names_.emplace_back(std::string(reinterpret_cast<const char*>(e + 2)), num);
        }
    }
}

Pattern* Pattern::compile(const String& regex) { return new Pattern(regex, 0); }
Pattern* Pattern::compile(const String& regex, int32_t flags) { return new Pattern(regex, flags); }

bool Pattern::matches(const String& regex, const String& input) { return compile(regex)->matcher(input)->matches(); }

String Pattern::quote(const String& s) {
    const std::string& str = s;
    size_t e = str.find("\\E");
    if (e == std::string::npos) return String("\\Q" + str + "\\E");
    std::string sb = "\\Q";
    size_t current = 0;
    do {
        sb.append(str, current, e - current);
        sb += "\\E\\\\E\\Q";
        current = e + 2;
    } while ((e = str.find("\\E", current)) != std::string::npos);
    sb.append(str, current, std::string::npos);
    sb += "\\E";
    return String(sb);
}

int32_t Pattern::groupIndex(const String& name) {
    for (const auto& n : names_)
        if (name == n.first) return n.second;
    return -1;
}

Matcher* Pattern::matcher(const String& input) { return new Matcher(this, input); }

Array<String>* Pattern::split(const String& input) { return split(input, 0); }

Array<String>* Pattern::split(const String& input, int32_t limit) {
    int32_t index = 0;
    const bool matchLimited = limit > 0;
    std::vector<String> list;
    Matcher* m = matcher(input);
    const std::string& in = input;
    while (m->find()) {
        if (!matchLimited || static_cast<int32_t>(list.size()) < limit - 1) {
            if (index == 0 && index == m->start() && m->start() == m->end()) continue;
            list.emplace_back(in.substr(static_cast<size_t>(index), static_cast<size_t>(m->start() - index)));
            index = m->end();
        } else if (static_cast<int32_t>(list.size()) == limit - 1) {
            list.emplace_back(in.substr(static_cast<size_t>(index)));
            index = m->end();
        }
    }
    if (index == 0) {
        auto* r = new Array<String>(1);
        (*r)[0] = input.isNull() ? String("") : input;
        return r;
    }
    if (!matchLimited || static_cast<int32_t>(list.size()) < limit) list.emplace_back(in.substr(static_cast<size_t>(index)));
    size_t size = list.size();
    if (limit == 0)
        while (size > 0 && list[size - 1].isEmpty()) size--;
    auto* r = new Array<String>(static_cast<int32_t>(size));
    for (size_t i = 0; i < size; i++) (*r)[static_cast<int32_t>(i)] = list[i];
    return r;
}

// =======================================================================================
// Matcher

Matcher::Matcher(Pattern* p, const String& input) : pattern_(p), text_(input.isNull() ? String("") : input) {
    to_ = text_.length();
    groups_.assign(static_cast<size_t>(2 * (p->groupCount() + 1)), -1);
    matchData_ = pcre2_match_data_create_from_pattern(static_cast<pcre2_code*>(p->code()), gctx());
    if (matchData_ == nullptr) throw OutOfMemoryError();
}

Matcher* Matcher::usePattern(Pattern* newPattern) {
    if (newPattern == nullptr) throw IllegalArgumentException(String("Pattern cannot be null"));
    pattern_ = newPattern;
    groups_.assign(static_cast<size_t>(2 * (newPattern->groupCount() + 1)), -1);
    matchData_ = pcre2_match_data_create_from_pattern(static_cast<pcre2_code*>(newPattern->code()), gctx());
    return this;
}

Matcher* Matcher::reset() {
    first_ = -1;
    last_ = 0;
    clearGroups();
    lastAppend_ = 0;
    from_ = 0;
    to_ = text_.length();
    return this;
}

Matcher* Matcher::reset(const String& input) {
    text_ = input.isNull() ? String("") : input;
    return reset();
}

Matcher* Matcher::region(int32_t start, int32_t end) {
    int32_t len = text_.length();
    if (start < 0 || start > len) throw IndexOutOfBoundsException(String("start"));
    if (end < 0 || end > len) throw IndexOutOfBoundsException(String("end"));
    if (start > end) throw IndexOutOfBoundsException(String("start > end"));
    reset();
    from_ = start;
    to_ = end;
    return this;
}

void Matcher::clearGroups() { std::fill(groups_.begin(), groups_.end(), -1); }

int32_t Matcher::nextCharBoundary(int32_t i) {
    const std::string& s = text_;
    int32_t n = static_cast<int32_t>(s.size());
    if (i >= n) return i + 1;
    i++;
    while (i < n && (static_cast<unsigned char>(s[static_cast<size_t>(i)]) & 0xC0) == 0x80) i++;
    return i;
}

bool Matcher::search(int32_t from, int mode) {
    hitEnd_ = false;
    const std::string& s = text_;
    PCRE2_SPTR subject = reinterpret_cast<PCRE2_SPTR>(s.data()) + from_;
    PCRE2_SIZE len = static_cast<PCRE2_SIZE>(to_ - from_);
    PCRE2_SIZE start = static_cast<PCRE2_SIZE>(from - from_);
    uint32_t opts = 0;
    if (mode == 1) opts |= PCRE2_ANCHORED;
    if (mode == 2) opts |= PCRE2_ANCHORED | PCRE2_ENDANCHORED;
    auto* md = static_cast<pcre2_match_data*>(matchData_);
    int rc = pcre2_match(static_cast<pcre2_code*>(pattern_->code()), subject, len, start, opts, md, nullptr);
    if (rc < 0) {
        clearGroups();
        first_ = -1;
        if (rc == PCRE2_ERROR_NOMATCH || rc == PCRE2_ERROR_PARTIAL) {
            hitEnd_ = true;
            return false;
        }
        PCRE2_UCHAR buf[256];
        pcre2_get_error_message(rc, buf, sizeof buf);
        throw RuntimeException(str("regex match failed: ", String(reinterpret_cast<char*>(buf))));
    }
    PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
    uint32_t pairs = pcre2_get_ovector_count(md);
    size_t ng = groups_.size() / 2;
    for (size_t g = 0; g < ng; g++) {
        if (g < pairs && ov[2 * g] != PCRE2_UNSET) {
            groups_[2 * g] = static_cast<int32_t>(ov[2 * g]) + from_;
            groups_[2 * g + 1] = static_cast<int32_t>(ov[2 * g + 1]) + from_;
        } else {
            groups_[2 * g] = -1;
            groups_[2 * g + 1] = -1;
        }
    }
    first_ = groups_[0];
    last_ = groups_[1];
    hitEnd_ = last_ == to_;
    return true;
}

bool Matcher::matches() { return search(from_, 2); }
bool Matcher::lookingAt() { return search(from_, 1); }

bool Matcher::find() {
    int32_t next = last_;
    if (next == first_) next = nextCharBoundary(next);
    if (next < from_) next = from_;
    if (next > to_) {
        clearGroups();
        first_ = -1;
        return false;
    }
    return search(next, 0);
}

bool Matcher::find(int32_t start) {
    int32_t limit = text_.length();
    if (start < 0 || start > limit) throw IndexOutOfBoundsException(String("Illegal start index"));
    reset();
    return search(start, 0);
}

void Matcher::ensureMatch() {
    if (first_ < 0) throw IllegalStateException(String("No match found"));
}

String Matcher::group() { return group(0); }

String Matcher::group(int32_t g) {
    ensureMatch();
    if (g < 0 || g > groupCount()) throw IndexOutOfBoundsException(str("No group ", g));
    int32_t s = groups_[static_cast<size_t>(2 * g)], e = groups_[static_cast<size_t>(2 * g + 1)];
    if (s == -1 || e == -1) return String();
    return String(std::string(text_).substr(static_cast<size_t>(s), static_cast<size_t>(e - s)));
}

String Matcher::group(const String& name) {
    ensureMatch();
    int32_t g = pattern_->groupIndex(name);
    if (g < 0) throw IllegalArgumentException(str("No group with name <", name, ">"));
    return group(g);
}

int32_t Matcher::start() {
    if (first_ < 0) throw IllegalStateException(String("No match available"));
    return first_;
}

int32_t Matcher::start(int32_t g) {
    if (first_ < 0) throw IllegalStateException(String("No match available"));
    if (g < 0 || g > groupCount()) throw IndexOutOfBoundsException(str("No group ", g));
    return groups_[static_cast<size_t>(2 * g)];
}

int32_t Matcher::start(const String& name) {
    int32_t g = pattern_->groupIndex(name);
    if (first_ < 0) throw IllegalStateException(String("No match available"));
    if (g < 0) throw IllegalArgumentException(str("No group with name <", name, ">"));
    return start(g);
}

int32_t Matcher::end() {
    if (first_ < 0) throw IllegalStateException(String("No match available"));
    return last_;
}

int32_t Matcher::end(int32_t g) {
    if (first_ < 0) throw IllegalStateException(String("No match available"));
    if (g < 0 || g > groupCount()) throw IndexOutOfBoundsException(str("No group ", g));
    return groups_[static_cast<size_t>(2 * g + 1)];
}

int32_t Matcher::end(const String& name) {
    int32_t g = pattern_->groupIndex(name);
    if (first_ < 0) throw IllegalStateException(String("No match available"));
    if (g < 0) throw IllegalArgumentException(str("No group with name <", name, ">"));
    return end(g);
}

String Matcher::expandReplacement(const String& replacementIn) {
    const std::string& r = replacementIn;
    std::string result;
    const std::string& text = text_;
    size_t cursor = 0;
    while (cursor < r.size()) {
        char c = r[cursor];
        if (c == '\\') {
            cursor++;
            if (cursor == r.size()) throw IllegalArgumentException(String("character to be escaped is missing"));
            // copy one (possibly multi-byte) character
            size_t n = 1;
            while (cursor + n < r.size() && (static_cast<unsigned char>(r[cursor + n]) & 0xC0) == 0x80) n++;
            result.append(r, cursor, n);
            cursor += n;
        } else if (c == '$') {
            cursor++;
            if (cursor == r.size()) throw IllegalArgumentException(String("Illegal group reference: group index is missing"));
            char nc = r[cursor];
            int32_t refNum = -1;
            if (nc == '{') {
                cursor++;
                std::string gname;
                while (cursor < r.size()) {
                    nc = r[cursor];
                    if ((nc >= 'a' && nc <= 'z') || (nc >= 'A' && nc <= 'Z') || (nc >= '0' && nc <= '9')) {
                        gname.push_back(nc);
                        cursor++;
                    } else {
                        break;
                    }
                }
                if (gname.empty()) throw IllegalArgumentException(String("named capturing group has 0 length name"));
                if (nc != '}') throw IllegalArgumentException(String("named capturing group is missing trailing '}'"));
                if (gname[0] >= '0' && gname[0] <= '9')
                    throw IllegalArgumentException(String("capturing group name {" + gname + "} starts with digit character"));
                refNum = pattern_->groupIndex(String(gname));
                if (refNum < 0) throw IllegalArgumentException(String("No group with name {" + gname + "}"));
                cursor++;
            } else {
                refNum = nc - '0';
                if (refNum < 0 || refNum > 9) throw IllegalArgumentException(String("Illegal group reference"));
                cursor++;
                for (;;) {
                    if (cursor >= r.size()) break;
                    int32_t nd = r[cursor] - '0';
                    if (nd < 0 || nd > 9) break;
                    int32_t nr = refNum * 10 + nd;
                    if (groupCount() < nr) break;
                    refNum = nr;
                    cursor++;
                }
            }
            int32_t s = start(refNum), e = end(refNum);
            if (s != -1 && e != -1) result.append(text, static_cast<size_t>(s), static_cast<size_t>(e - s));
        } else {
            result.push_back(c);
            cursor++;
        }
    }
    return String(result);
}

Matcher* Matcher::appendReplacement(StringBuilder* sb, const String& replacement) {
    if (first_ < 0) throw IllegalStateException(String("No match available"));
    String expanded = expandReplacement(replacement);
    sb->buffer().append(std::string(text_), static_cast<size_t>(lastAppend_), static_cast<size_t>(first_ - lastAppend_));
    sb->buffer().append(std::string(expanded));
    lastAppend_ = last_;
    return this;
}

StringBuilder* Matcher::appendTail(StringBuilder* sb) {
    const std::string& t = text_;
    sb->buffer().append(t, static_cast<size_t>(lastAppend_), std::string::npos);
    return sb;
}

String Matcher::replaceAll(const String& replacement) {
    reset();
    bool result = find();
    if (result) {
        auto* sb = new StringBuilder();
        do {
            appendReplacement(sb, replacement);
            result = find();
        } while (result);
        appendTail(sb);
        return sb->toString();
    }
    return text_;
}

String Matcher::replaceFirst(const String& replacement) {
    if (replacement.isNull()) throw NullPointerException(String("replacement"));
    reset();
    if (!find()) return text_;
    auto* sb = new StringBuilder();
    appendReplacement(sb, replacement);
    appendTail(sb);
    return sb->toString();
}

String Matcher::quoteReplacement(const String& s) {
    const std::string& str = s;
    if (str.find('\\') == std::string::npos && str.find('$') == std::string::npos) return s;
    std::string sb;
    for (char c : str) {
        if (c == '\\' || c == '$') sb.push_back('\\');
        sb.push_back(c);
    }
    return String(sb);
}

String Matcher::toString() {
    String last = (first_ >= 0) ? group() : String("");
    if (last.isNull()) last = String("");
    return str("java.util.regex.Matcher[pattern=", pattern_->pattern(), " region=", from_, ",", to_, " lastmatch=", last,
               "]");
}

}  // namespace jlang
