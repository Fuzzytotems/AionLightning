// jlang::String, concatenation, format, StringBuilder, StringCharacterIterator, StringUtils.
#include "jtest.h"

#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using jlang::String;
using jlang::StringBuilder;

namespace {

// A codebase-style enum value class (CONVENTIONS §7).
class Color final {
public:
    enum class Value : int32_t { _NULL = -1, RED, GREEN };
    static const Color RED, GREEN;
    constexpr Color() = default;
    constexpr Color(std::nullptr_t) {}
    constexpr explicit Color(Value v) : v_(v) {}
    constexpr operator Value() const { return v_; }
    int32_t ordinal() const { return static_cast<int32_t>(v_); }
    String name() const { return v_ == Value::RED ? String("RED") : String("GREEN"); }
    String toString() const { return name(); }
    bool equals(Color o) const { return v_ == o.v_; }
    int32_t hashCode() const { return ordinal() * 7 + 1; }
    [[maybe_unused]] friend bool operator==(Color a, Color b) { return a.v_ == b.v_; }
    [[maybe_unused]] friend bool operator==(Color a, std::nullptr_t) { return a.v_ == Value::_NULL; }

private:
    Value v_ = Value::_NULL;
};
inline const Color Color::RED{Color::Value::RED};
inline const Color Color::GREEN{Color::Value::GREEN};

class Named : public virtual jlang::Object {
public:
    explicit Named(String n) : name(std::move(n)) {}
    String toString() override { return str("Named(", name, ")"); }
    String name;
};

}  // namespace

JTEST(String_NullSemantics) {
    String n;
    JCHECK(n.isNull());
    JCHECK(n == nullptr);
    JCHECK(nullptr == n);
    JCHECK(!(n != nullptr));
    String e("");
    JCHECK(!e.isNull());
    JCHECK(e != nullptr);
    JCHECK(e.isEmpty());
    JCHECK(!(n == e));  // null != ""
    JCHECK(n == String());
    String c = static_cast<const char*>(nullptr);
    JCHECK(c.isNull());
    String s = "abc";
    s = nullptr;
    JCHECK(s.isNull());
    // null behaves like "" for method calls
    JCHECK_EQ(n.length(), 0);
    JCHECK(!n.startsWith("a"));
    JCHECK_EQ(n.trim(), String(""));
    // but concatenation prints "null" like Java
    JCHECK_EQ(String("x") + n, String("xnull"));
    JCHECK_EQ(n + "x", String("nullx"));
    String acc;
    acc += "a";
    JCHECK_EQ(acc, String("nulla"));
    JCHECK(!String("a").equals(n));
    JCHECK(!String("a").equals(nullptr));
    JCHECK(!String("a").equalsIgnoreCase(n));
    // copies keep nullness; std::string conversion makes non-null
    String n2 = n;
    JCHECK(n2.isNull());
    String fromStd = std::string();
    JCHECK(!fromStd.isNull());
}

JTEST(String_Concatenation) {
    String s("v=");
    JCHECK_EQ(s + 1, String("v=1"));
    JCHECK_EQ(s + int64_t{-5}, String("v=-5"));
    JCHECK_EQ(s + 1.0f, String("v=1.0"));
    JCHECK_EQ(s + 1.0, String("v=1.0"));
    JCHECK_EQ(s + 1e10, String("v=1.0E10"));
    JCHECK_EQ(s + true, String("v=true"));
    JCHECK_EQ(s + false, String("v=false"));
    JCHECK_EQ(s + u'x', String("v=x"));
    JCHECK_EQ(s + u'Ж', String("v=Ж"));
    JCHECK_EQ(s + 'c', String("v=c"));
    JCHECK_EQ(s + static_cast<int8_t>(-3), String("v=-3"));   // byte is a number
    JCHECK_EQ(s + static_cast<uint8_t>(200), String("v=200"));
    JCHECK_EQ(s + static_cast<int16_t>(7), String("v=7"));
    JCHECK_EQ(s + nullptr, String("v=null"));
    JCHECK_EQ(s + static_cast<jlang::Object*>(nullptr), String("v=null"));
    JCHECK_EQ(s + std::string("std"), String("v=std"));
    JCHECK_EQ(s + std::string_view("sv"), String("v=sv"));
    JCHECK_EQ(s + std::optional<int32_t>(3), String("v=3"));
    JCHECK_EQ(s + std::optional<int32_t>(), String("v=null"));
    JCHECK_EQ(s + Color::GREEN, String("v=GREEN"));
    Named* nm = new Named("x");
    JCHECK_EQ(s + nm, String("v=Named(x)"));
    jlang::Runnable* asIface = jlang::Runnable::of([] {});
    JCHECK((s + asIface).startsWith("v="));
    // left operand not a String
    JCHECK_EQ("a" + String("b"), String("ab"));
    JCHECK_EQ(1 + String("b"), String("1b"));
    JCHECK_EQ(1.5 + String("b"), String("1.5b"));
    JCHECK_EQ(std::string("x") + String("y"), String("xy"));
    JCHECK_EQ(u'q' + String("y"), String("qy"));
    // chains / rvalues
    String chain = String("a") + 1 + "b" + 2.0 + u'c' + nm;
    JCHECK_EQ(chain, String("a1b2.0cNamed(x)"));
    String t = "x";
    t += 1;
    t += u'y';
    t += 2.5f;
    t += "z";
    t += String("w");
    JCHECK_EQ(t, String("x1y2.5zw"));
    // Java order: 1 + 2 + "a" == "3a"
    JCHECK_EQ(1 + 2 + String("a"), String("3a"));
    // str()
    JCHECK_EQ(jlang::str("a", 1, u'b', 2.0f, nullptr, true, String()), String("a1b2.0nulltruenull"));
    // surrogate pairs appended one unit at a time become one code point
    String emoji;
    emoji = String("") + u'\xD83D' + u'\xDE00';
    JCHECK_EQ(emoji, String("\xF0\x9F\x98\x80"));
    JCHECK_EQ(emoji.utf16Length(), 2);
}

JTEST(String_Equality) {
    String a("abc"), b("abc"), c("abd");
    JCHECK(a == b);
    JCHECK(a != c);
    JCHECK(a == "abc");
    JCHECK("abc" == a);
    JCHECK(a == std::string("abc"));
    JCHECK(std::string("abc") == a);
    JCHECK(a.equals(b));
    JCHECK(a.equals("abc"));
    JCHECK(!a.equals(c));
    JCHECK(String("ABC").equalsIgnoreCase("abc"));
    JCHECK(String("Привет").equalsIgnoreCase("пРИВЕТ"));
    JCHECK(a.equals(static_cast<jlang::Object*>(jlang::box(String("abc")))));
    JCHECK(!a.equals(static_cast<jlang::Object*>(jlang::box(5))));
    JCHECK(a < c);
    std::map<String, int> m{{"b", 2}, {"a", 1}};
    JCHECK_EQ(m.begin()->first, String("a"));
    std::unordered_map<String, int> um;
    um["k"] = 1;
    JCHECK_EQ(um.at(String("k")), 1);
    std::unordered_set<String, jlang::Hash<String>, jlang::Equal<String>> js;
    js.insert("x");
    JCHECK(js.count(String("x")) == 1);
}

JTEST(String_Queries) {
    String s("Hello, World");
    JCHECK_EQ(s.length(), 12);
    JCHECK_EQ(s.charAt(0), u'H');
    JCHECK_THROWS(jlang::StringIndexOutOfBoundsException, s.charAt(12));
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, s.charAt(-1));
    JCHECK_EQ(s.indexOf(u'o'), 4);
    JCHECK_EQ(s.indexOf('o', 5), 8);
    JCHECK_EQ(s.indexOf(static_cast<int32_t>('z')), -1);
    JCHECK_EQ(s.indexOf("World"), 7);
    JCHECK_EQ(s.indexOf("o", -5), 4);
    JCHECK_EQ(s.indexOf("", 100), 12);
    JCHECK_EQ(s.lastIndexOf(u'o'), 8);
    JCHECK_EQ(s.lastIndexOf('o', 7), 4);
    JCHECK_EQ(s.lastIndexOf("l"), 10);
    JCHECK_EQ(s.lastIndexOf("l", 9), 3);
    JCHECK_EQ(s.lastIndexOf("", 3), 3);
    JCHECK_EQ(s.lastIndexOf(u'x', -1), -1);
    JCHECK(s.contains("lo, W"));
    JCHECK(s.startsWith("Hell"));
    JCHECK(s.startsWith("World", 7));
    JCHECK(!s.startsWith("World", 8));
    JCHECK(!s.startsWith("H", -1));
    JCHECK(s.endsWith("rld"));
    JCHECK(s.endsWith(""));
    JCHECK_EQ(s.substring(7), String("World"));
    JCHECK_EQ(s.substring(0, 5), String("Hello"));
    JCHECK_EQ(s.substring(12), String(""));
    JCHECK_THROWS(jlang::StringIndexOutOfBoundsException, s.substring(13));
    JCHECK_THROWS(jlang::StringIndexOutOfBoundsException, s.substring(5, 4));
    JCHECK_THROWS(jlang::StringIndexOutOfBoundsException, s.substring(-1, 4));
    try {
        s.substring(3, 20);
    } catch (jlang::StringIndexOutOfBoundsException& e) {
        JCHECK_EQ(e.getMessage(), String("begin 3, end 20, length 12"));
    }
    JCHECK_EQ(String("  \t x y \n").trim(), String("x y"));
    JCHECK_EQ(String("\x01\x02").trim(), String(""));
    JCHECK_EQ(String("a.b.c").replace(u'.', u'/'), String("a/b/c"));
    JCHECK_EQ(String("aXbXc").replace("X", "--"), String("a--b--c"));
    JCHECK_EQ(String("abc").replace("", "-"), String("-a-b-c-"));
    JCHECK_EQ(String("жук").replace(u'ж', u'Ж'), String("Жук"));
    JCHECK_EQ(String("abc").concat("def"), String("abcdef"));
    JCHECK_EQ(String("a,b").intern(), String("a,b"));
    JCHECK(String("abc123").matches("[a-z]+\\d+"));
    JCHECK(!String("abc123x").matches("[a-z]+\\d+"));
    JCHECK(String("HeLLo").matches("(?i)hello"));
    JCHECK(String("abc").matches("\\p{Alpha}+"));
    JCHECK(String("a.b").matches("\\Qa.b\\E"));
    JCHECK(!String("axb").matches("\\Qa.b\\E"));
    JCHECK_THROWS(jlang::IllegalArgumentException, String("x").matches("("));
    // non-ASCII: byte indices, charAt decodes
    String ru("Жук");
    JCHECK_EQ(ru.length(), 6);
    JCHECK_EQ(ru.charAt(0), u'Ж');
    JCHECK_EQ(ru.charAt(1), u'�');
    JCHECK_EQ(ru.indexOf(u'у'), 2);
    JCHECK_EQ(ru.toUpperCase(), String("ЖУК"));
    JCHECK_EQ(String("Straße").toUpperCase(), String("STRASSE"));
    JCHECK_EQ(String("MiXeD").toLowerCase(), String("mixed"));
    JCHECK_EQ(String("MiXeD").toUpperCase(nullptr), String("MIXED"));
}

JTEST(String_ArraysAndCharsets) {
    String s("Aé€\xF0\x9F\x98\x80");  // A, e-acute, euro, emoji
    auto* chars = s.toCharArray();
    JCHECK_EQ(chars->length, 5);
    JCHECK_EQ((*chars)[0], u'A');
    JCHECK_EQ((*chars)[1], u'é');
    JCHECK_EQ((*chars)[2], u'€');
    JCHECK_EQ((*chars)[3], u'\xD83D');
    JCHECK_EQ((*chars)[4], u'\xDE00');
    JCHECK_EQ(String(chars), s);
    JCHECK_EQ(String(chars, 1, 2), String("é€"));
    JCHECK_EQ(String::valueOf(chars, 0, 1), String("A"));
    JCHECK_THROWS(jlang::StringIndexOutOfBoundsException, String(chars, 3, 5));
    auto* utf8 = s.getBytes();
    JCHECK_EQ(utf8->length, static_cast<int32_t>(s.size()));
    JCHECK_EQ(String(utf8), s);
    auto* utf8b = s.getBytes("UTF-8");
    JCHECK_EQ(utf8b->length, utf8->length);
    auto* le = s.getBytes("UTF-16LE");
    JCHECK_EQ(le->length, 10);
    JCHECK_EQ((*le)[0], 'A');
    JCHECK_EQ((*le)[1], 0);
    JCHECK_EQ(String(le, "UTF-16LE"), s);
    auto* latin = String("aé€").getBytes("ISO-8859-1");
    JCHECK_EQ(latin->length, 3);
    JCHECK_EQ(static_cast<uint8_t>((*latin)[1]), 0xE9);
    JCHECK_EQ((*latin)[2], '?');
    JCHECK_EQ(String(latin, "ISO-8859-1"), String("aé?"));
    auto* bytes = jlang::Array<int8_t>::of({'h', 'e', 'l', 'l', 'o'});
    JCHECK_EQ(String(bytes, 1, 3), String("ell"));
    JCHECK_EQ(String(bytes, 1, 3, "US-ASCII"), String("ell"));
    JCHECK_THROWS(jlang::UnsupportedEncodingException, s.getBytes("no-such-charset"));
    JCHECK_THROWS(jlang::IOException, String(bytes, "EBCDIC-XYZ"));
    // invalid UTF-8 decodes to U+FFFD
    auto* bad = jlang::Array<int8_t>::of({'a', static_cast<int8_t>(0xC3), 'b'});
    JCHECK_EQ(String(bad), String("a�b"));
    JCHECK_EQ(String::fromUtf16(u"xÿ", 2), String("xÿ"));
    JCHECK(String("xÿ").toUtf16() == std::u16string(u"xÿ"));
}

JTEST(String_ValueOfAndJoin) {
    JCHECK_EQ(String::valueOf(42), String("42"));
    JCHECK_EQ(String::valueOf(true), String("true"));
    JCHECK_EQ(String::valueOf(u'z'), String("z"));
    JCHECK_EQ(String::valueOf(2.5f), String("2.5"));
    JCHECK_EQ(String::valueOf(int64_t{1} << 40), String("1099511627776"));
    JCHECK_EQ(String::valueOf(static_cast<jlang::Object*>(nullptr)), String("null"));
    JCHECK_EQ(String::valueOf(String()), String("null"));
    std::vector<String> parts{"a", "b", "c"};
    JCHECK_EQ(String::join(",", parts), String("a,b,c"));
    auto* arr = jlang::Array<int32_t>::of({1, 2, 3});
    JCHECK_EQ(String::join("-", arr), String("1-2-3"));
}

JTEST(String_Format) {
    using jlang::Object;
    JCHECK_EQ(String::format("%s=%d", "x", 5), String("x=5"));
    JCHECK_EQ(String::format("%s", Color::RED), String("RED"));
    JCHECK_EQ(String::format("%s|%s", new Named("n"), static_cast<Object*>(nullptr)), String("Named(n)|null"));
    JCHECK_EQ(String::format("%d%%", jlang::box(int64_t{7})), String("7%"));
    JCHECK_EQ(String::format("%.2f", jlang::box(1.005)), String("1.01"));  // Java rounds HALF_UP on the shortest repr
    JCHECK_EQ(String::format("%5.1f|", 2.25f), String("  2.3|"));
    JCHECK_EQ(String::format("%c", jlang::box(u'Z')), String("Z"));
    JCHECK_EQ(String::format("%b", jlang::box(false)), String("false"));
    JCHECK_EQ(String::format("%s", std::optional<int32_t>()), String("null"));
    JCHECK_EQ(String::format("%s", std::optional<int32_t>(4)), String("4"));
    JCHECK_EQ(String::format("no args"), String("no args"));
    JCHECK_EQ(String::format(nullptr, "%s", "loc"), String("loc"));
    // Object[] varargs expansion
    auto* args = jlang::Array<Object*>::of({jlang::box(1), jlang::box(String("two"))});
    JCHECK_EQ(String::format("%s %s", args), String("1 two"));
    auto* sargs = jlang::Array<String>::of({"p", "q"});
    JCHECK_EQ(String::format("%2$s%1$s", sargs), String("qp"));
    JCHECK_THROWS(jlang::IllegalArgumentException, String::format("%d", "str"));
    JCHECK_THROWS(jlang::IllegalArgumentException, String::format("%s %s", "one"));
    JCHECK_THROWS(jlang::IllegalArgumentException, String::format("%q", 1));
    JCHECK_THROWS(jlang::IllegalArgumentException, String::format("%f", 1));
}

JTEST(String_Split_Extra) {
    auto* p = String("a:b:c").split(":");
    JCHECK_EQ(p->length, 3);
    JCHECK_EQ((*p)[2], String("c"));
    auto* q = String("Жук Жук").split(" ");
    JCHECK_EQ(q->length, 2);
    JCHECK_EQ((*q)[1], String("Жук"));
    auto* r = String("a1b2c3").split("(?i)[0-9]");
    JCHECK_EQ(r->length, 3);
    auto* n = String().split(",");
    JCHECK_EQ(n->length, 1);
    JCHECK_EQ((*n)[0], String(""));
}

JTEST(StringBuilder_Basics) {
    StringBuilder* sb = new StringBuilder();
    sb->append("a")->append(1)->append(u'b')->append(2.5)->append(true)->append(static_cast<jlang::Object*>(nullptr));
    JCHECK_EQ(sb->toString(), String("a1b2.5truenull"));
    JCHECK_EQ(sb->length(), 14);
    sb->setLength(3);
    JCHECK_EQ(sb->toString(), String("a1b"));
    sb->insert(0, "X")->insert(2, 99);
    JCHECK_EQ(sb->toString(), String("Xa991b"));
    sb->deleteCharAt(0);
    JCHECK_EQ(sb->toString(), String("a991b"));
    sb->delete_(1, 3);
    JCHECK_EQ(sb->toString(), String("a1b"));
    sb->delete_(1, 100);
    JCHECK_EQ(sb->toString(), String("a"));
    sb->append("bcdef")->replace(1, 3, "XYZ");
    JCHECK_EQ(sb->toString(), String("aXYZdef"));
    sb->setCharAt(0, u'A');
    JCHECK_EQ(sb->charAt(0), u'A');
    JCHECK_EQ(sb->indexOf("Z"), 3);
    JCHECK_EQ(sb->indexOf("d", 5), -1);
    JCHECK_EQ(sb->lastIndexOf("f"), 6);
    JCHECK_EQ(sb->substring(1, 4), String("XYZ"));
    sb->reverse();
    JCHECK_EQ(sb->toString(), String("fedZYXA"));
    JCHECK_THROWS(jlang::StringIndexOutOfBoundsException, sb->charAt(100));
    JCHECK_THROWS(jlang::StringIndexOutOfBoundsException, sb->delete_(5, 2));
    StringBuilder* u = new StringBuilder("жу");
    u->append(u'к')->reverse();
    JCHECK_EQ(u->toString(), String("куж"));
    u->setCharAt(0, u'x');
    JCHECK_EQ(u->toString(), String("xуж"));
    StringBuilder* c = new StringBuilder(10);
    JCHECK_EQ(c->length(), 0);
    auto* chars = String("hi!").toCharArray();
    c->append(chars)->append(chars, 0, 2)->append(String("12345"), 1, 3);
    JCHECK_EQ(c->toString(), String("hi!hi23"));
    JCHECK_EQ(String(c), String("hi!hi23"));
    JCHECK_EQ(String("x") + c, String("xhi!hi23"));
    JCHECK(!c->equals(new StringBuilder("hi!hi23")));  // identity like Java
    StringBuilder* tb = StringBuilder::newInstance();
    tb->append("t");
    StringBuilder::recycle(tb);
    JCHECK_THROWS(jlang::NegativeArraySizeException, new StringBuilder(-1));
}

JTEST(StringCharacterIterator_Basics) {
    jlang::StringCharacterIterator it("abé");
    String collected("");
    for (char16_t c = it.first(); c != jlang::StringCharacterIterator::DONE; c = it.next()) collected += c;
    JCHECK_EQ(collected, String("abé"));
    JCHECK_EQ(it.getEndIndex(), 3);
    JCHECK_EQ(it.last(), u'é');
    JCHECK_EQ(it.previous(), u'b');
    JCHECK_EQ(it.setIndex(0), u'a');
    JCHECK_EQ(it.previous(), jlang::StringCharacterIterator::DONE);
}

JTEST(StringUtils_Basics) {
    JCHECK_EQ(jlang::StringUtils::EMPTY, String(""));
    JCHECK(!jlang::StringUtils::EMPTY.isNull());
    JCHECK(jlang::StringUtils::isEmpty(String()));
    JCHECK(jlang::StringUtils::isBlank(" \t"));
    JCHECK(!jlang::StringUtils::isBlank(" x "));
    JCHECK_EQ(jlang::StringUtils::capitalize("abc"), String("Abc"));
    JCHECK_EQ(jlang::StringUtils::defaultString(String()), String(""));
}

JTEST(Hash_EqualFunctors) {
    using jlang::equalsOf;
    using jlang::hashCodeOf;
    JCHECK_EQ(hashCodeOf(true), 1231);
    JCHECK_EQ(hashCodeOf(int64_t{1} << 32), 1);
    JCHECK_EQ(hashCodeOf(String("abc")), 96354);
    JCHECK_EQ(hashCodeOf(1.0f), 1065353216);
    JCHECK_EQ(hashCodeOf(Color::GREEN), Color::GREEN.hashCode());
    JCHECK_EQ(hashCodeOf(std::optional<int32_t>()), 0);
    JCHECK(equalsOf(std::nan(""), std::nan("")));  // Double.equals semantics
    JCHECK(!equalsOf(0.0, -0.0));
    JCHECK(equalsOf(Color::RED, Color::RED));
    auto* a = jlang::box(String("k"));
    auto* b = jlang::box(String("k"));
    JCHECK(a != b);
    jlang::Object* ao = a;
    jlang::Object* bo = b;
    JCHECK(equalsOf(ao, bo));
    JCHECK_EQ(hashCodeOf(ao), hashCodeOf(bo));
    JCHECK(!equalsOf(ao, static_cast<jlang::Object*>(nullptr)));
    std::unordered_map<jlang::Object*, int, jlang::Hash<jlang::Object*>, jlang::Equal<jlang::Object*>> m;
    m[ao] = 1;
    JCHECK_EQ(m.count(bo), 1u);
}

JTEST(String_LiteralRegexFastPaths) {
    // multi-char literal patterns bypass std::regex; results follow Pattern.split / replaceAll
    auto* a = String("a--b----c--").split("--");
    JCHECK_EQ(a->length, 4);
    JCHECK_EQ((*a)[2], String(""));
    JCHECK_EQ((*a)[3], String("c"));
    auto* b = String("--a--b").split("--", -1);
    JCHECK_EQ(b->length, 3);
    JCHECK_EQ((*b)[0], String(""));
    auto* c = String("a, b, c").split(", ", 2);
    JCHECK_EQ(c->length, 2);
    JCHECK_EQ((*c)[1], String("b, c"));
    auto* d = String("abc").split("xyz");
    JCHECK_EQ(d->length, 1);
    JCHECK_EQ((*d)[0], String("abc"));
    auto* e = String("ab").split("ab");
    JCHECK_EQ(e->length, 0);
    JCHECK_EQ(String("foo bar foo").replaceAll("foo", "baz"), String("baz bar baz"));
    JCHECK_EQ(String("foo bar foo").replaceFirst("foo", "baz"), String("baz bar foo"));
    JCHECK_EQ(String("foo").replaceAll("xx", "y"), String("foo"));
    JCHECK_EQ(String("a-b").replaceAll("-", "\\$"), String("a$b"));
}
