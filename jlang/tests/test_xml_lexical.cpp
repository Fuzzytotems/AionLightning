// Tests for the JAXB lexical rules (jlang/Jaxb.h). Expected values were produced by the JAXB RI
// 2.3.9 (see jlang/tests/data/xml/java and the probe results quoted in docs/cpp-port/JAXB.md).
#include "jtest.h"

#include <jlang/IO.h>
#include <jlang/Xml.h>

#include <cmath>

using namespace jlang;
using namespace jlang::xml;

namespace {

// A minimal enum value class in the shape CONVENTIONS §7 describes (what the generator emits).
class Kind final {
public:
    enum class Value : int32_t { _NULL = -1, A, B, C };
    static const Kind A, B, C;
    constexpr Kind() = default;
    constexpr Kind(std::nullptr_t) {}
    constexpr explicit Kind(Value v) : v_(v) {}
    constexpr operator Value() const { return v_; }
    int32_t ordinal() const { return static_cast<int32_t>(v_); }
    String name() const {
        switch (v_) {
            case Value::A: return "A";
            case Value::B: return "B";
            case Value::C: return "C";
            default: return String();
        }
    }
    // @XmlEnumValue("C1") on C
    String toXml() const { return v_ == Value::C ? String("C1") : name(); }
    static Array<Kind>* values() { return Array<Kind>::of({A, B, C}); }
    friend bool operator==(Kind a, Kind b) { return a.v_ == b.v_; }
    friend bool operator==(Kind a, std::nullptr_t) { return a.v_ == Value::_NULL; }

private:
    Value v_ = Value::_NULL;
};
const Kind Kind::A{Kind::Value::A};
const Kind Kind::B{Kind::Value::B};
const Kind Kind::C{Kind::Value::C};

class Plain final {  // bound by name(), no toXml()
public:
    enum class Value : int32_t { _NULL = -1, DAY, NIGHT };
    static const Plain DAY, NIGHT;
    constexpr Plain() = default;
    constexpr Plain(std::nullptr_t) {}
    constexpr explicit Plain(Value v) : v_(v) {}
    String name() const { return v_ == Value::DAY ? String("DAY") : v_ == Value::NIGHT ? String("NIGHT") : String(); }
    static Array<Plain>* values() { return Array<Plain>::of({DAY, NIGHT}); }
    friend bool operator==(Plain a, Plain b) { return a.v_ == b.v_; }
    friend bool operator==(Plain a, std::nullptr_t) { return a.v_ == Value::_NULL; }

private:
    Value v_ = Value::_NULL;
};
const Plain Plain::DAY{Plain::Value::DAY};
const Plain Plain::NIGHT{Plain::Value::NIGHT};

}  // namespace

JTEST(XmlLexicalInt) {
    JCHECK_EQ(parseInt(""), 0);
    JCHECK_EQ(parseInt(" "), 0);
    JCHECK_EQ(parseInt(" 42 "), 42);
    JCHECK_EQ(parseInt("\t7\n"), 7);
    JCHECK_EQ(parseInt("1 2"), 12);
    JCHECK_EQ(parseInt(" 4 2 "), 42);
    JCHECK_EQ(parseInt("--5"), -5);
    JCHECK_EQ(parseInt("5-"), -5);
    JCHECK_EQ(parseInt("+3"), 3);
    JCHECK_EQ(parseInt("-2147483648"), INT32_MIN);
    JCHECK_EQ(parseInt("2147483648"), INT32_MIN);   // wraps
    JCHECK_EQ(parseInt("99999999999"), 1215752191);  // wraps
    JCHECK_THROWS(NumberFormatException, parseInt("1.5"));
    JCHECK_THROWS(NumberFormatException, parseInt("abc"));
    try {
        parseInt("x1");
        JCHECK(false);
    } catch (NumberFormatException& e) {
        JCHECK_EQ(e.getMessage(), String("Not a number: x1"));
    }
    JCHECK_EQ(parseShort("70000"), static_cast<int16_t>(4464));
    JCHECK_EQ(parseShort(" -3 "), static_cast<int16_t>(-3));
    JCHECK_EQ(parseByte("300"), static_cast<int8_t>(44));
    JCHECK_EQ(parseByte("200"), static_cast<int8_t>(-56));
    JCHECK_EQ(parseByte(""), static_cast<int8_t>(0));
}

JTEST(XmlLexicalLong) {
    JCHECK_EQ(parseLong(" +5 "), INT64_C(5));
    JCHECK_EQ(parseLong("-9223372036854775808"), INT64_MIN);
    JCHECK_EQ(parseLong("9223372036854775807"), INT64_MAX);
    JCHECK_EQ(parseLong("2164391835"), INT64_C(2164391835));
    JCHECK_THROWS(NumberFormatException, parseLong("9223372036854775808"));
    JCHECK_THROWS(NumberFormatException, parseLong("1 2"));
    JCHECK_THROWS(NumberFormatException, parseLong(""));
    JCHECK_THROWS(NumberFormatException, parseLong("++5"));
    JCHECK_THROWS(NumberFormatException, parseLong("-"));
    JCHECK_THROWS(NumberFormatException, parseLong("+"));
    try {
        parseLong("1 2");
    } catch (NumberFormatException& e) {
        JCHECK_EQ(e.getMessage(), String("For input string: \"1 2\""));
    }
    try {
        parseLong("++5");
    } catch (NumberFormatException& e) {
        JCHECK(e.getMessage().isNull());
    }
}

JTEST(XmlLexicalFloat) {
    JCHECK_EQ(parseFloat(" 1.5 "), 1.5f);
    JCHECK(std::isinf(parseFloat("INF")) && parseFloat("INF") > 0);
    JCHECK(std::isinf(parseFloat("-INF")) && parseFloat("-INF") < 0);
    JCHECK(std::isinf(parseFloat(" INF ")));
    JCHECK(std::isnan(parseFloat("NaN")));
    JCHECK_EQ(parseFloat(".5"), 0.5f);
    JCHECK_EQ(parseFloat("1e3"), 1000.0f);
    JCHECK_EQ(parseFloat("+1.5"), 1.5f);
    JCHECK_EQ(parseFloat("0x1p3"), 8.0f);
    JCHECK_EQ(parseFloat("1."), 1.0f);
    JCHECK(parseFloat("-0") == 0.0f && std::signbit(parseFloat("-0")));
    JCHECK(std::isinf(parseFloat("3.4028236e38")));
    JCHECK_EQ(parseFloat("1e-50"), 0.0f);
    JCHECK_EQ(parseFloat("0.1"), 0.1f);
    JCHECK_EQ(parseFloat("16777217"), 16777216.0f);
    JCHECK_EQ(parseFloat("291.3283 "), 291.3283f);  // npc_walker.xml
    JCHECK_THROWS(NumberFormatException, parseFloat("1.5f"));
    JCHECK_THROWS(NumberFormatException, parseFloat(""));
    JCHECK_THROWS(NumberFormatException, parseFloat("Infinity"));
    JCHECK_THROWS(NumberFormatException, parseFloat("."));
    JCHECK_THROWS(NumberFormatException, parseFloat("1e+"));
    JCHECK_THROWS(NumberFormatException, parseFloat("0x10"));
    JCHECK_THROWS(NumberFormatException, parseFloat("1,5"));
    JCHECK_EQ(parseDouble("0.1"), 0.1);
    JCHECK(std::isinf(parseDouble(" -INF")) && parseDouble("-INF") < 0);
    JCHECK(std::isinf(parseDouble("1e400")));
    JCHECK_THROWS(NumberFormatException, parseDouble("1d"));
    try {
        parseDouble("1d");
    } catch (NumberFormatException& e) {
        JCHECK_EQ(e.getMessage(), String("1d"));
    }
    JCHECK_EQ(printFloat(2789.0f), String("2789.0"));
    JCHECK_EQ(printFloat(1e-5f), String("1.0E-5"));
    JCHECK_EQ(printFloat(INFINITY), String("INF"));
    JCHECK_EQ(printDouble(1e10), String("1.0E10"));
    JCHECK_EQ(printDouble(-HUGE_VAL), String("-INF"));
}

JTEST(XmlLexicalBoolean) {
    auto b = [](const char* s) { return parseBooleanOrNull(s); };
    JCHECK(b("true") == std::optional<bool>(true));
    JCHECK(b("1") == std::optional<bool>(true));
    JCHECK(b(" true ") == std::optional<bool>(true));
    JCHECK(b("true\t") == std::optional<bool>(true));
    JCHECK(b("false") == std::optional<bool>(false));
    JCHECK(b("0") == std::optional<bool>(false));
    JCHECK(b("  ") == std::optional<bool>(false));
    JCHECK(b("y") == std::optional<bool>(false));
    JCHECK(!b("yes").has_value());
    JCHECK(!b("TRUE").has_value());
    JCHECK(!b("").has_value());
    JCHECK(!b("10").has_value());
    JCHECK(!b("truex").has_value());
    JCHECK(!b("true x").has_value());
    JCHECK(!b("tr").has_value());
    JCHECK(!b("fals").has_value());
    JCHECK(!b("falsey").has_value());
    JCHECK(!b("no").has_value());
    JCHECK_THROWS(StringIndexOutOfBoundsException, b("t"));
    JCHECK_THROWS(StringIndexOutOfBoundsException, b("f"));
    // primitive boolean: null -> false
    JCHECK_EQ(parseBoolean("yes"), false);
    JCHECK_EQ(parseBoolean("1"), true);
}

JTEST(XmlLexicalEnumAndLists) {
    JCHECK(parseEnum<Kind>("B") == Kind::B);
    JCHECK(parseEnum<Kind>(" B ") == nullptr);  // not trimmed
    JCHECK(parseEnum<Kind>("C") == nullptr);    // @XmlEnumValue("C1")
    JCHECK(parseEnum<Kind>("C1") == Kind::C);
    JCHECK(parseEnum<Kind>("") == nullptr);
    JCHECK(parseEnum<Plain>("NIGHT") == Plain::NIGHT);
    JCHECK(parseEnum<Plain>("night") == nullptr);
    auto lookup = [](const String& s) -> Plain {
        if (s.equals("DAY")) return Plain::DAY;
        throw IllegalArgumentException(String("No enum constant ") + s);
    };
    JCHECK(parseEnum<Plain>("DAY", lookup) == Plain::DAY);
    JCHECK(parseEnum<Plain>("X", lookup) == nullptr);

    List<int32_t>* li = parseList<int32_t>("1 2  3", parseInt);
    JCHECK_EQ(li->size(), 3);
    JCHECK_EQ(li->get(2), 3);
    JCHECK_EQ(parseList<int32_t>(" 4 ", parseInt)->size(), 1);
    JCHECK_EQ(parseList<int32_t>("", parseInt)->size(), 0);
    JCHECK_THROWS(NumberFormatException, parseList<int32_t>("1 x", parseInt));
    List<Kind>* lk = parseList<Kind>("A Z\tC1", [](std::string_view t) { return parseEnum<Kind>(t); });
    JCHECK_EQ(lk->size(), 3);
    JCHECK(lk->get(0) == Kind::A);
    JCHECK(lk->get(1) == nullptr);
    JCHECK(lk->get(2) == Kind::C);
    JCHECK_EQ(trimXml(" \t\r\nab c\n"), std::string_view("ab c"));
    JCHECK_EQ(parseFile(" scripts/x \n")->getPath(), String("scripts/x"));  // java.io.File leaf: trimmed
    static_assert(nameHash("npc_id") != nameHash("npcid"));
}
