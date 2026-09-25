// jlang/Boxes.h - java.lang.Number and the boxed primitives (Integer, Long, Short, Byte,
// Float, Double, Boolean, Character) with their static utilities, plus jlang::StringBox (a
// String boxed as an Object).
//
// Part of the jlang core: translated code includes <jlang/jlang.h>.
//
// Boxed objects are only used where Java stores a primitive as an Object (CONVENTIONS §4);
// use jlang::box(x) to create them. Integer/Long/Short/Byte/Character/Boolean.valueOf cache
// the same ranges as Java, so identity comparisons behave like Java's.
#pragma once

#include <jlang/Exceptions.h>
#include <jlang/String.h>

#include <cstdint>
#include <limits>

namespace jlang {

// ---------------------------------------------------------------------------------------
class Number : public virtual Object {
public:
    virtual int32_t intValue() = 0;
    virtual int64_t longValue() = 0;
    virtual float floatValue() = 0;
    virtual double doubleValue() = 0;
    virtual int16_t shortValue() { return static_cast<int16_t>(intValue()); }
    virtual int8_t byteValue() { return static_cast<int8_t>(intValue()); }
};

// ---------------------------------------------------------------------------------------
class Integer final : public Number {
public:
    static constexpr int32_t MAX_VALUE = std::numeric_limits<int32_t>::max();
    static constexpr int32_t MIN_VALUE = std::numeric_limits<int32_t>::min();
    static constexpr int32_t SIZE = 32;
    static constexpr int32_t BYTES = 4;
    static Class* const TYPE;  // int.class

    explicit Integer(int32_t v) : value(v) {}
    explicit Integer(const String& s);

    int32_t intValue() override { return value; }
    int64_t longValue() override { return value; }
    float floatValue() override { return static_cast<float>(value); }
    double doubleValue() override { return value; }
    int32_t hashCode() override { return value; }
    bool equals(Object* o) override;
    String toString() override;
    int32_t compareTo(Integer* o) { return compare(value, o->value); }

    static Integer* valueOf(int32_t v);  // cached for -128..127
    static Integer* valueOf(const String& s);
    static Integer* valueOf(const String& s, int32_t radix);
    static int32_t parseInt(const String& s);
    static int32_t parseInt(const String& s, int32_t radix);
    static Integer* decode(const String& s);
    static String toString(int32_t v);
    static String toString(int32_t v, int32_t radix);
    static String toHexString(int32_t v);
    static String toOctalString(int32_t v);
    static String toBinaryString(int32_t v);
    static int32_t hashCode(int32_t v) { return v; }
    static int32_t compare(int32_t x, int32_t y) { return x < y ? -1 : (x == y ? 0 : 1); }
    static int32_t signum(int32_t v) { return (v >> 31) | static_cast<int32_t>(static_cast<uint32_t>(-v) >> 31); }
    static int32_t bitCount(int32_t v);
    static int32_t reverse(int32_t v);
    static int32_t reverseBytes(int32_t v);
    static int32_t highestOneBit(int32_t v);
    static int32_t lowestOneBit(int32_t v) { return v & -v; }
    static int32_t numberOfLeadingZeros(int32_t v);
    static int32_t numberOfTrailingZeros(int32_t v);
    static int32_t rotateLeft(int32_t v, int32_t d);
    static int32_t rotateRight(int32_t v, int32_t d);
    static int32_t max(int32_t a, int32_t b) { return a >= b ? a : b; }
    static int32_t min(int32_t a, int32_t b) { return a <= b ? a : b; }
    static int32_t sum(int32_t a, int32_t b) { return a + b; }

    int32_t value;
};

class Long final : public Number {
public:
    static constexpr int64_t MAX_VALUE = std::numeric_limits<int64_t>::max();
    static constexpr int64_t MIN_VALUE = std::numeric_limits<int64_t>::min();
    static constexpr int32_t SIZE = 64;
    static constexpr int32_t BYTES = 8;
    static Class* const TYPE;  // long.class

    explicit Long(int64_t v) : value(v) {}
    explicit Long(const String& s);

    int32_t intValue() override { return static_cast<int32_t>(value); }
    int64_t longValue() override { return value; }
    float floatValue() override { return static_cast<float>(value); }
    double doubleValue() override { return static_cast<double>(value); }
    int32_t hashCode() override { return hashCode(value); }
    bool equals(Object* o) override;
    String toString() override;
    int32_t compareTo(Long* o) { return compare(value, o->value); }

    static Long* valueOf(int64_t v);  // cached for -128..127
    static Long* valueOf(const String& s);
    static Long* valueOf(const String& s, int32_t radix);
    static int64_t parseLong(const String& s);
    static int64_t parseLong(const String& s, int32_t radix);
    static Long* decode(const String& s);
    static String toString(int64_t v);
    static String toString(int64_t v, int32_t radix);
    static String toHexString(int64_t v);
    static String toOctalString(int64_t v);
    static String toBinaryString(int64_t v);
    static int32_t hashCode(int64_t v) { return static_cast<int32_t>(v ^ static_cast<int64_t>(static_cast<uint64_t>(v) >> 32)); }
    static int32_t compare(int64_t x, int64_t y) { return x < y ? -1 : (x == y ? 0 : 1); }
    static int32_t signum(int64_t v) { return static_cast<int32_t>((v >> 63) | static_cast<int64_t>(static_cast<uint64_t>(-v) >> 63)); }
    static int32_t bitCount(int64_t v);
    static int64_t reverse(int64_t v);
    static int64_t reverseBytes(int64_t v);
    static int64_t highestOneBit(int64_t v);
    static int64_t lowestOneBit(int64_t v) { return v & -v; }
    static int32_t numberOfLeadingZeros(int64_t v);
    static int32_t numberOfTrailingZeros(int64_t v);
    static int64_t rotateLeft(int64_t v, int32_t d);
    static int64_t rotateRight(int64_t v, int32_t d);
    static int64_t max(int64_t a, int64_t b) { return a >= b ? a : b; }
    static int64_t min(int64_t a, int64_t b) { return a <= b ? a : b; }
    static int64_t sum(int64_t a, int64_t b) { return a + b; }

    int64_t value;
};

class Short final : public Number {
public:
    static constexpr int16_t MAX_VALUE = std::numeric_limits<int16_t>::max();
    static constexpr int16_t MIN_VALUE = std::numeric_limits<int16_t>::min();
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t BYTES = 2;
    static Class* const TYPE;

    explicit Short(int16_t v) : value(v) {}
    explicit Short(const String& s);

    int32_t intValue() override { return value; }
    int64_t longValue() override { return value; }
    float floatValue() override { return value; }
    double doubleValue() override { return value; }
    int16_t shortValue() override { return value; }
    int32_t hashCode() override { return value; }
    bool equals(Object* o) override;
    String toString() override;
    int32_t compareTo(Short* o) { return compare(value, o->value); }

    static Short* valueOf(int16_t v);
    static Short* valueOf(const String& s);
    static Short* valueOf(const String& s, int32_t radix);
    static int16_t parseShort(const String& s);
    static int16_t parseShort(const String& s, int32_t radix);
    static Short* decode(const String& s);
    static String toString(int16_t v);
    static int32_t hashCode(int16_t v) { return v; }
    static int32_t compare(int16_t x, int16_t y) { return x - y; }
    static int16_t reverseBytes(int16_t v);
    static int32_t toUnsignedInt(int16_t v) { return static_cast<uint16_t>(v); }

    int16_t value;
};

class Byte final : public Number {
public:
    static constexpr int8_t MAX_VALUE = std::numeric_limits<int8_t>::max();
    static constexpr int8_t MIN_VALUE = std::numeric_limits<int8_t>::min();
    static constexpr int32_t SIZE = 8;
    static constexpr int32_t BYTES = 1;
    static Class* const TYPE;

    explicit Byte(int8_t v) : value(v) {}
    explicit Byte(const String& s);

    int32_t intValue() override { return value; }
    int64_t longValue() override { return value; }
    float floatValue() override { return value; }
    double doubleValue() override { return value; }
    int8_t byteValue() override { return value; }
    int32_t hashCode() override { return value; }
    bool equals(Object* o) override;
    String toString() override;
    int32_t compareTo(Byte* o) { return compare(value, o->value); }

    static Byte* valueOf(int8_t v);  // all values cached
    static Byte* valueOf(const String& s);
    static Byte* valueOf(const String& s, int32_t radix);
    static int8_t parseByte(const String& s);
    static int8_t parseByte(const String& s, int32_t radix);
    static Byte* decode(const String& s);
    static String toString(int8_t v);
    static int32_t hashCode(int8_t v) { return v; }
    static int32_t compare(int8_t x, int8_t y) { return x - y; }
    static int32_t toUnsignedInt(int8_t v) { return static_cast<uint8_t>(v); }

    int8_t value;
};

class Float final : public Number {
public:
    static constexpr float MAX_VALUE = std::numeric_limits<float>::max();
    static constexpr float MIN_VALUE = std::numeric_limits<float>::denorm_min();
    static constexpr float MIN_NORMAL = std::numeric_limits<float>::min();
    static constexpr float POSITIVE_INFINITY = std::numeric_limits<float>::infinity();
    static constexpr float NEGATIVE_INFINITY = -std::numeric_limits<float>::infinity();
    static constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
    static constexpr int32_t MAX_EXPONENT = 127;
    static constexpr int32_t MIN_EXPONENT = -126;
    static constexpr int32_t SIZE = 32;
    static constexpr int32_t BYTES = 4;
    static Class* const TYPE;

    explicit Float(float v) : value(v) {}
    explicit Float(double v) : value(static_cast<float>(v)) {}
    explicit Float(const String& s);

    int32_t intValue() override;
    int64_t longValue() override;
    float floatValue() override { return value; }
    double doubleValue() override { return value; }
    int32_t hashCode() override { return floatToIntBits(value); }
    bool equals(Object* o) override;  // Java: floatToIntBits equality
    String toString() override;
    int32_t compareTo(Float* o) { return compare(value, o->value); }
    bool isNaN() { return value != value; }
    bool isInfinite() { return isInfinite(value); }

    static Float* valueOf(float v) { return new Float(v); }
    static Float* valueOf(const String& s);
    static float parseFloat(const String& s);
    static String toString(float v);
    static String toHexString(float v);
    static bool isNaN(float v) { return v != v; }
    static bool isInfinite(float v) { return v == POSITIVE_INFINITY || v == NEGATIVE_INFINITY; }
    static bool isFinite(float v) { return v - v == 0.0f; }
    static int32_t floatToIntBits(float v);     // canonical NaN
    static int32_t floatToRawIntBits(float v);
    static float intBitsToFloat(int32_t bits);
    static int32_t compare(float a, float b);   // -0.0 < 0.0, NaN greatest
    static int32_t hashCode(float v) { return floatToIntBits(v); }
    static float max(float a, float b);
    static float min(float a, float b);
    static float sum(float a, float b) { return a + b; }

    float value;
};

class Double final : public Number {
public:
    static constexpr double MAX_VALUE = std::numeric_limits<double>::max();
    static constexpr double MIN_VALUE = std::numeric_limits<double>::denorm_min();
    static constexpr double MIN_NORMAL = std::numeric_limits<double>::min();
    static constexpr double POSITIVE_INFINITY = std::numeric_limits<double>::infinity();
    static constexpr double NEGATIVE_INFINITY = -std::numeric_limits<double>::infinity();
    static constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
    static constexpr int32_t MAX_EXPONENT = 1023;
    static constexpr int32_t MIN_EXPONENT = -1022;
    static constexpr int32_t SIZE = 64;
    static constexpr int32_t BYTES = 8;
    static Class* const TYPE;

    explicit Double(double v) : value(v) {}
    explicit Double(const String& s);

    int32_t intValue() override;
    int64_t longValue() override;
    float floatValue() override { return static_cast<float>(value); }
    double doubleValue() override { return value; }
    int32_t hashCode() override { return hashCode(value); }
    bool equals(Object* o) override;  // Java: doubleToLongBits equality
    String toString() override;
    int32_t compareTo(Double* o) { return compare(value, o->value); }
    bool isNaN() { return value != value; }
    bool isInfinite() { return isInfinite(value); }

    static Double* valueOf(double v) { return new Double(v); }
    static Double* valueOf(const String& s);
    static double parseDouble(const String& s);
    static String toString(double v);
    static String toHexString(double v);
    static bool isNaN(double v) { return v != v; }
    static bool isInfinite(double v) { return v == POSITIVE_INFINITY || v == NEGATIVE_INFINITY; }
    static bool isFinite(double v) { return v - v == 0.0; }
    static int64_t doubleToLongBits(double v);  // canonical NaN
    static int64_t doubleToRawLongBits(double v);
    static double longBitsToDouble(int64_t bits);
    static int32_t compare(double a, double b);
    static int32_t hashCode(double v) {
        int64_t b = doubleToLongBits(v);
        return static_cast<int32_t>(b ^ static_cast<int64_t>(static_cast<uint64_t>(b) >> 32));
    }
    static double max(double a, double b);
    static double min(double a, double b);
    static double sum(double a, double b) { return a + b; }

    double value;
};

class Boolean final : public virtual Object {
public:
    static Boolean* const TRUE;
    static Boolean* const FALSE;
    static Class* const TYPE;

    explicit Boolean(bool v) : value(v) {}
    explicit Boolean(const String& s) : value(parseBoolean(s)) {}

    bool booleanValue() { return value; }
    int32_t hashCode() override { return value ? 1231 : 1237; }
    bool equals(Object* o) override;
    String toString() override { return toString(value); }
    int32_t compareTo(Boolean* o) { return compare(value, o->value); }

    static Boolean* valueOf(bool v) { return v ? TRUE : FALSE; }
    static Boolean* valueOf(const String& s) { return valueOf(parseBoolean(s)); }
    static bool parseBoolean(const String& s) { return s.equalsIgnoreCase("true"); }
    static String toString(bool v) { return v ? String("true") : String("false"); }
    static int32_t hashCode(bool v) { return v ? 1231 : 1237; }
    static int32_t compare(bool x, bool y) { return x == y ? 0 : (x ? 1 : -1); }
    static bool logicalAnd(bool a, bool b) { return a && b; }
    static bool logicalOr(bool a, bool b) { return a || b; }
    static bool logicalXor(bool a, bool b) { return a != b; }

    bool value;
};

class Character final : public virtual Object {
public:
    static constexpr char16_t MIN_VALUE = u'\u0000';
    static constexpr char16_t MAX_VALUE = u'￿';
    static constexpr int32_t MIN_RADIX = 2;
    static constexpr int32_t MAX_RADIX = 36;
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t BYTES = 2;
    static constexpr int32_t MIN_CODE_POINT = 0;
    static constexpr int32_t MAX_CODE_POINT = 0x10FFFF;
    static constexpr char16_t MIN_HIGH_SURROGATE = u'\xD800';
    static constexpr char16_t MAX_HIGH_SURROGATE = u'\xDBFF';
    static constexpr char16_t MIN_LOW_SURROGATE = u'\xDC00';
    static constexpr char16_t MAX_LOW_SURROGATE = u'\xDFFF';
    static Class* const TYPE;

    explicit Character(char16_t v) : value(v) {}

    char16_t charValue() { return value; }
    int32_t hashCode() override { return value; }
    bool equals(Object* o) override;
    String toString() override { return toString(value); }
    int32_t compareTo(Character* o) { return compare(value, o->value); }

    static Character* valueOf(char16_t c);  // cached for 0..127
    static String toString(char16_t c);
    static int32_t hashCode(char16_t c) { return c; }
    static int32_t compare(char16_t x, char16_t y) { return static_cast<int32_t>(x) - static_cast<int32_t>(y); }

    // Classification (Unicode; code point overloads accept int like Java's (int codePoint)).
    static bool isDigit(int32_t cp);
    static bool isLetter(int32_t cp);
    static bool isLetterOrDigit(int32_t cp);
    static bool isAlphabetic(int32_t cp);
    static bool isUpperCase(int32_t cp);
    static bool isLowerCase(int32_t cp);
    static bool isWhitespace(int32_t cp);  // Java: Unicode space separators except no-break, + \t\n\v\f\r\x1C-\x1F
    static bool isSpaceChar(int32_t cp);
    static bool isISOControl(int32_t cp) { return (cp >= 0 && cp <= 0x1F) || (cp >= 0x7F && cp <= 0x9F); }
    static bool isJavaIdentifierStart(int32_t cp);
    static bool isJavaIdentifierPart(int32_t cp);
    static bool isHighSurrogate(char16_t c) { return c >= MIN_HIGH_SURROGATE && c <= MAX_HIGH_SURROGATE; }
    static bool isLowSurrogate(char16_t c) { return c >= MIN_LOW_SURROGATE && c <= MAX_LOW_SURROGATE; }
    static bool isSurrogate(char16_t c) { return c >= MIN_HIGH_SURROGATE && c <= MAX_LOW_SURROGATE; }
    static bool isValidCodePoint(int32_t cp) { return cp >= 0 && cp <= MAX_CODE_POINT; }
    static bool isSupplementaryCodePoint(int32_t cp) { return cp >= 0x10000 && cp <= MAX_CODE_POINT; }
    static int32_t toCodePoint(char16_t high, char16_t low) {
        return ((high - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
    }
    static int32_t charCount(int32_t cp) { return cp >= 0x10000 ? 2 : 1; }

    // Case mapping: char16_t in -> char16_t out, int (code point) in -> int out.
    static char16_t toUpperCase(char16_t c) { return static_cast<char16_t>(toUpperCase(static_cast<int32_t>(c))); }
    static char16_t toLowerCase(char16_t c) { return static_cast<char16_t>(toLowerCase(static_cast<int32_t>(c))); }
    static int32_t toUpperCase(int32_t cp);
    static int32_t toLowerCase(int32_t cp);
    static char16_t toUpperCase(char c) { return toUpperCase(static_cast<char16_t>(static_cast<unsigned char>(c))); }
    static char16_t toLowerCase(char c) { return toLowerCase(static_cast<char16_t>(static_cast<unsigned char>(c))); }

    // digit(ch, radix): value of ch in radix or -1; getNumericValue: 0..35 for digits/letters,
    // -1 if none; forDigit(digit, radix): '0'-'9','a'-'z' or '\0'.
    static int32_t digit(int32_t cp, int32_t radix);
    static int32_t getNumericValue(int32_t cp);
    static char16_t forDigit(int32_t digit, int32_t radix);
    static char16_t reverseBytes(char16_t c) { return static_cast<char16_t>((c << 8) | (c >> 8)); }

    char16_t value;
};

// ---------------------------------------------------------------------------------------
// A String stored where Java holds a String as an Object (Object fields, Object... varargs,
// raw collections). Created by jlang::box(String). equals/hashCode/toString follow the text.
class StringBox final : public virtual Object {
public:
    explicit StringBox(const String& v) : value(v) {}
    int32_t hashCode() override { return value.hashCode(); }
    bool equals(Object* o) override;
    String toString() override { return value; }
    int32_t compareTo(StringBox* o) { return value.compareTo(o->value); }
    String value;
};

// Out-of-class inline definitions (the classes must be complete).
inline Class* const Integer::TYPE = Class::of<int32_t>();
inline Class* const Long::TYPE = Class::of<int64_t>();
inline Class* const Short::TYPE = Class::of<int16_t>();
inline Class* const Byte::TYPE = Class::of<int8_t>();
inline Class* const Float::TYPE = Class::of<float>();
inline Class* const Double::TYPE = Class::of<double>();
inline Class* const Boolean::TYPE = Class::of<bool>();
inline Class* const Character::TYPE = Class::of<char16_t>();
inline Boolean* const Boolean::TRUE = new Boolean(true);
inline Boolean* const Boolean::FALSE = new Boolean(false);

}  // namespace jlang
