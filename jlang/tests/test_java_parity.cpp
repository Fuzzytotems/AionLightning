// Parity tests against reference outputs produced by the JDK (java_ref_data.inc, generated
// by jlang/tests/java/JavaRef.java).
#include "jtest.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

struct DoubleCase { double v; const char* s; };
struct FloatCase { float v; const char* s; };
struct FormatDoubleCase { const char* fmt; double v; const char* out; };
struct FormatFloatCase { const char* fmt; float v; const char* out; };
struct FormatIntCase { const char* fmt; int32_t v; const char* out; };
struct FormatLongCase { const char* fmt; int64_t v; const char* out; };
struct FormatByteCase { const char* fmt; int8_t v; const char* outByte; const char* outShort; };
struct FormatMiscCase { const char* fmt; const char* out; };
struct RandomCase {
    int64_t seed;
    int32_t ints[5];
    int32_t bounded100[5];
    int32_t pow2[5];
    int32_t big[5];
    int64_t longs[3];
    bool bools[5];
    float floats[3];
    double doubles[3];
    double gauss[3];
    int8_t bytes[7];
};
struct StringCase { const char* s; int32_t hash; const char* upper; const char* lower; int32_t len16; };
struct CompareCase { const char* a; const char* b; int32_t cmp; int32_t cmpIc; bool eqIc; };
struct SplitCase { const char* s; const char* regex; int32_t limit; int32_t n; const char* parts[16]; };
struct ReplaceCase { const char* s; const char* regex; const char* repl; const char* all; const char* first; };
struct RoundFloatCase { float v; int32_t r; };
struct RoundDoubleCase { double v; int64_t r; };
struct ParseIntCase { const char* s; int32_t radix; bool ok; const char* result; };
struct ParseDoubleCase { const char* s; bool dok; double d; bool fok; float f; };
struct IntStringCase {
    int32_t v;
    const char* dec;
    const char* hex;
    const char* oct;
    const char* bin;
    const char* b36;
    int32_t bitCount, reverse, reverseBytes, highestOneBit, nlz, ntz;
};
struct LongStringCase { int64_t v; const char* dec; const char* hex; const char* b7; int32_t hash; };
struct HexFloatCase { double v; const char* hex; int32_t hash; };
struct HexFloatFCase { float v; const char* hex; int32_t hash; };

#include "java_ref_data.inc"

using jlang::String;

bool sameDouble(double a, double b) {
    return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b) || (a != a && b != b);
}
bool sameFloat(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b) || (a != a && b != b);
}

}  // namespace

JTEST(Parity_DoubleToString) {
    for (const auto& c : kDoubleToString) {
        JCHECK_EQ(jlang::Double::toString(c.v), String(c.s));
        JCHECK_EQ(String("") + c.v, String(c.s));
        JCHECK_EQ(jlang::str(c.v), String(c.s));
    }
}

JTEST(Parity_FloatToString) {
    for (const auto& c : kFloatToString) {
        JCHECK_EQ(jlang::Float::toString(c.v), String(c.s));
        JCHECK_EQ(String("") + c.v, String(c.s));
        JCHECK_EQ(String::valueOf(c.v), String(c.s));
    }
}

JTEST(Parity_FormatDouble) {
    for (const auto& c : kFormatDouble) JCHECK_EQ(String::format(c.fmt, c.v), String(c.out));
}

JTEST(Parity_FormatFloat) {
    for (const auto& c : kFormatFloat) JCHECK_EQ(String::format(c.fmt, c.v), String(c.out));
}

JTEST(Parity_FormatIntegers) {
    for (const auto& c : kFormatInt) {
        JCHECK_EQ(String::format(c.fmt, c.v), String(c.out));
        // boxed Integer goes through the same path
        JCHECK_EQ(String::format(c.fmt, static_cast<jlang::Object*>(jlang::box(c.v))), String(c.out));
    }
    for (const auto& c : kFormatLong) JCHECK_EQ(String::format(c.fmt, c.v), String(c.out));
    for (const auto& c : kFormatByte) {
        JCHECK_EQ(String::format(c.fmt, c.v), String(c.outByte));
        JCHECK_EQ(String::format(c.fmt, static_cast<int16_t>(c.v)), String(c.outShort));
    }
}

JTEST(Parity_FormatMisc) {
    using jlang::Object;
    const String results[] = {
        String::format("%s %s", "a", "b"),
        String::format("%2$s %1$s", "a", "b"),
        String::format("%s %<s %s", "a", "b"),
        String::format("%.2s", "abcdef"),
        String::format("%S", "abc"),
        String::format("%-6s|%6s", "ab", "cd"),
        String::format("%b %b %b", true, nullptr, "x"),
        String::format("%B", false),
        String::format("%%d %n", 1),
        String::format("%c%c", u'x', u'y'),
        String::format("%s", nullptr),
        String::format("[%5s]", String()),
        String::format("%10.3s|", "abcdef"),
        String::format("x=%d y=%s", 5, "q"),
    };
    size_t i = 0;
    for (const auto& c : kFormatMisc) {
        JCHECK_EQ(results[i], String(c.out));
        i++;
    }
    JCHECK_EQ(i, sizeof(results) / sizeof(results[0]));
}

JTEST(Parity_Random) {
    for (const auto& c : kRandom) {
        jlang::Random r(c.seed);
        for (int i = 0; i < 5; i++) JCHECK_EQ(r.nextInt(), c.ints[i]);
        for (int i = 0; i < 5; i++) JCHECK_EQ(r.nextInt(100), c.bounded100[i]);
        for (int i = 0; i < 5; i++) JCHECK_EQ(r.nextInt(1 << 20), c.pow2[i]);
        for (int i = 0; i < 5; i++) JCHECK_EQ(r.nextInt(1000000007), c.big[i]);
        for (int i = 0; i < 3; i++) JCHECK_EQ(r.nextLong(), c.longs[i]);
        for (int i = 0; i < 5; i++) JCHECK_EQ(r.nextBoolean(), c.bools[i]);
        for (int i = 0; i < 3; i++) JCHECK(sameFloat(r.nextFloat(), c.floats[i]));
        for (int i = 0; i < 3; i++) JCHECK(sameDouble(r.nextDouble(), c.doubles[i]));
        for (int i = 0; i < 3; i++) {
            double g = r.nextGaussian();
            // StrictMath.log/sqrt vs libm may differ in the last ulp
            JCHECK(std::fabs(g - c.gauss[i]) <= 4 * std::numeric_limits<double>::epsilon() * std::fabs(c.gauss[i]));
        }
        auto* bytes = new jlang::Array<int8_t>(7);
        r.nextBytes(bytes);
        for (int i = 0; i < 7; i++) JCHECK_EQ((*bytes)[i], c.bytes[i]);
    }
}

JTEST(Parity_StringHashAndCase) {
    for (const auto& c : kStrings) {
        String s(c.s);
        JCHECK_EQ(s.hashCode(), c.hash);
        JCHECK_EQ(s.toUpperCase(), String(c.upper));
        JCHECK_EQ(s.toLowerCase(), String(c.lower));
        JCHECK_EQ(s.utf16Length(), c.len16);
        JCHECK_EQ(s.toCharArray()->length, c.len16);
        JCHECK_EQ(String(s.toCharArray()), s);
        JCHECK_EQ(jlang::StringBox(s).hashCode(), c.hash);
    }
}

JTEST(Parity_Compare) {
    auto sign = [](int32_t v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); };
    for (const auto& c : kCompare) {
        String a(c.a), b(c.b);
        JCHECK_EQ(a.compareTo(b), c.cmp);
        JCHECK_EQ(sign(a.compareToIgnoreCase(b)), sign(c.cmpIc));
        JCHECK_EQ(a.equalsIgnoreCase(b), c.eqIc);
    }
}

JTEST(Parity_Split) {
    for (const auto& c : kSplit) {
        auto* parts = String(c.s).split(c.regex, c.limit);
        JCHECK_EQ(parts->length, c.n);
        if (parts->length != c.n) {
            std::fprintf(stderr, "    split(\"%s\", \"%s\", %d)\n", c.s, c.regex, c.limit);
            continue;
        }
        for (int32_t i = 0; i < c.n; i++) JCHECK_EQ((*parts)[i], String(c.parts[i]));
    }
}

JTEST(Parity_Replace) {
    for (const auto& c : kReplace) {
        JCHECK_EQ(String(c.s).replaceAll(c.regex, c.repl), String(c.all));
        JCHECK_EQ(String(c.s).replaceFirst(c.regex, c.repl), String(c.first));
    }
}

JTEST(Parity_MathRound) {
    for (const auto& c : kRoundFloat) JCHECK_EQ(jlang::Math::round(c.v), c.r);
    for (const auto& c : kRoundDouble) JCHECK_EQ(jlang::Math::round(c.v), c.r);
}

JTEST(Parity_ParseInt) {
    for (const auto& c : kParseInt) {
        try {
            int32_t v = jlang::Integer::parseInt(c.s, c.radix);
            JCHECK(c.ok);
            JCHECK_EQ(String::valueOf(v), String(c.result));
        } catch (jlang::NumberFormatException& e) {
            JCHECK(!c.ok);
            JCHECK_EQ(e.getMessage(), String(c.result));
        }
    }
    for (const auto& c : kDecode) {
        try {
            int32_t v = jlang::Integer::decode(c.s)->intValue();
            JCHECK(c.ok);
            JCHECK_EQ(String::valueOf(v), String(c.result));
        } catch (jlang::NumberFormatException& e) {
            JCHECK(!c.ok);
            JCHECK_EQ(e.getMessage(), String(c.result));
        }
    }
    for (const auto& c : kParseLong) {
        try {
            int64_t v = jlang::Long::parseLong(c.s);
            JCHECK(c.ok);
            JCHECK_EQ(String::valueOf(v), String(c.result));
        } catch (jlang::NumberFormatException& e) {
            JCHECK(!c.ok);
            JCHECK_EQ(e.getMessage(), String(c.result));
        }
    }
    for (const auto& c : kParseByte) {
        try {
            int8_t v = jlang::Byte::parseByte(c.s);
            JCHECK(c.ok);
            JCHECK_EQ(String::valueOf(v), String(c.result));
        } catch (jlang::NumberFormatException& e) {
            JCHECK(!c.ok);
            JCHECK_EQ(e.getMessage(), String(c.result));
        }
    }
}

JTEST(Parity_ParseDouble) {
    for (const auto& c : kParseDouble) {
        try {
            double v = jlang::Double::parseDouble(c.s);
            JCHECK(c.dok);
            JCHECK(sameDouble(v, c.d));
        } catch (jlang::NumberFormatException&) {
            JCHECK(!c.dok);
        }
        try {
            float v = jlang::Float::parseFloat(c.s);
            JCHECK(c.fok);
            JCHECK(sameFloat(v, c.f));
        } catch (jlang::NumberFormatException&) {
            JCHECK(!c.fok);
        }
    }
}

JTEST(Parity_IntegerStrings) {
    using jlang::Integer;
    for (const auto& c : kIntStrings) {
        JCHECK_EQ(Integer::toString(c.v), String(c.dec));
        JCHECK_EQ(Integer::toHexString(c.v), String(c.hex));
        JCHECK_EQ(Integer::toOctalString(c.v), String(c.oct));
        JCHECK_EQ(Integer::toBinaryString(c.v), String(c.bin));
        JCHECK_EQ(Integer::toString(c.v, 36), String(c.b36));
        JCHECK_EQ(Integer::bitCount(c.v), c.bitCount);
        JCHECK_EQ(Integer::reverse(c.v), c.reverse);
        JCHECK_EQ(Integer::reverseBytes(c.v), c.reverseBytes);
        JCHECK_EQ(Integer::highestOneBit(c.v), c.highestOneBit);
        JCHECK_EQ(Integer::numberOfLeadingZeros(c.v), c.nlz);
        JCHECK_EQ(Integer::numberOfTrailingZeros(c.v), c.ntz);
        JCHECK_EQ(Integer::parseInt(Integer::toString(c.v, 36), 36), c.v);
    }
    for (const auto& c : kLongStrings) {
        JCHECK_EQ(jlang::Long::toString(c.v), String(c.dec));
        JCHECK_EQ(jlang::Long::toHexString(c.v), String(c.hex));
        JCHECK_EQ(jlang::Long::toString(c.v, 7), String(c.b7));
        JCHECK_EQ(jlang::Long::hashCode(c.v), c.hash);
        JCHECK_EQ(jlang::hashCodeOf(c.v), c.hash);
    }
    for (const auto& c : kHexDouble) {
        JCHECK_EQ(jlang::Double::toHexString(c.v), String(c.hex));
        JCHECK_EQ(jlang::Double::hashCode(c.v), c.hash);
    }
    for (const auto& c : kHexFloat) {
        JCHECK_EQ(jlang::Float::toHexString(c.v), String(c.hex));
        JCHECK_EQ(jlang::Float::hashCode(c.v), c.hash);
    }
}
