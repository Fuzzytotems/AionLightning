// jlang/String.h - java.lang.String (value type), string concatenation, String.format,
// java.lang.StringBuilder/StringBuffer, java.text.StringCharacterIterator and
// org.apache.commons.lang.StringUtils.
//
// Part of the jlang core: translated code includes <jlang/jlang.h>.
#pragma once

#include <jlang/Object.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jlang {

template<class T> class Array;
class StringBuilder;
class Locale;
class String;

namespace detail {

template<class T> struct IsOptional : std::false_type {};
template<class T> struct IsOptional<std::optional<T>> : std::true_type {};

template<class T>
concept HasToStringConst = requires(const T& t) {
    { t.toString() } -> std::convertible_to<std::string_view>;
};

// Types that can be appended to a String (Java string conversion, JLS 5.1.11).
template<class T, class D = std::remove_cvref_t<T>>
concept Concatenable =
    std::is_arithmetic_v<D> || std::is_enum_v<D> || std::is_same_v<D, std::nullptr_t> ||
    std::is_convertible_v<const D&, std::string_view> ||
    IsOptional<D>::value || (std::is_pointer_v<D> && std::is_class_v<std::remove_pointer_t<D>>) ||
    std::is_same_v<std::decay_t<D>, const char*> || std::is_same_v<std::decay_t<D>, char*> ||
    HasToStringConst<D>;

// Non-template appenders (String.cpp).
void appendInt(std::string& out, int64_t v);
void appendUInt(std::string& out, uint64_t v);
void appendFloat(std::string& out, float v);    // Java Float.toString
void appendDouble(std::string& out, double v);  // Java Double.toString
void appendChar16(std::string& out, char16_t c);
void appendCodePoint(std::string& out, char32_t c);
void appendObject(std::string& out, Object* o);  // o->toString() or "null"
void appendString(std::string& out, const String& s);  // "null" for a null String

template<class T>
void appendValue(std::string& out, const T& v);

}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.lang.String
//
// A value type deriving from std::string, holding UTF-8.
//
// * Nullable: a default-constructed String (and String(nullptr)) is Java null.
//   `s == nullptr`, `s != nullptr`, `s = nullptr` work. "" is a non-null empty string.
//   Calling a method on a null String behaves as if it were "" (no NPE).
//   Concatenating a null String appends "null", as Java does.
// * INDICES ARE UTF-8 CODE UNITS (bytes): length(), charAt(), substring(), indexOf() ... count
//   bytes. For ASCII text (all identifiers, keys, config values, commands, packet names) this
//   is identical to Java. For non-ASCII text lengths are larger than Java's UTF-16 length.
//   charAt(i) returns the character that starts at byte i as char16_t (U+FFFD when i is in the
//   middle of a multi-byte sequence).
//   Exact for any text: hashCode() (Java's UTF-16 hash), toCharArray() (UTF-16 units),
//   getBytes(charset), equals(), compareTo() sign, toUtf16()/fromUtf16().
// * Java exceptions: substring/charAt throw StringIndexOutOfBoundsException, regex methods
//   throw IllegalArgumentException (PatternSyntaxException) on bad patterns.
class String : public std::string {
public:
    // ---- construction
    String() noexcept {}
    String(std::nullptr_t) noexcept {}
    String(const char* s) : null_(s == nullptr) {
        if (s != nullptr) std::string::assign(s);
    }
    String(const char* s, size_t n) : std::string(s, n), null_(false) {}
    String(const std::string& s) : std::string(s), null_(false) {}
    String(std::string&& s) noexcept : std::string(std::move(s)), null_(false) {}
    String(std::string_view s) : std::string(s), null_(false) {}
    String(const String&) = default;
    String(String&&) noexcept = default;

    // Java constructors. Byte arrays are decoded with the charset (default UTF-8; also
    // "ISO-8859-1"/"latin1", "US-ASCII", "UTF-16LE", "UTF-16BE", "UTF-16"); char arrays hold
    // UTF-16 code units.
    explicit String(Array<int8_t>* bytes);
    String(Array<int8_t>* bytes, int32_t offset, int32_t length);
    String(Array<int8_t>* bytes, const String& charsetName);
    String(Array<int8_t>* bytes, int32_t offset, int32_t length, const String& charsetName);
    explicit String(Array<char16_t>* chars);
    String(Array<char16_t>* chars, int32_t offset, int32_t count);
    explicit String(StringBuilder* sb);

    // ---- assignment
    String& operator=(const String&) = default;
    String& operator=(String&&) noexcept = default;
    String& operator=(std::nullptr_t) noexcept {
        std::string::clear();
        null_ = true;
        return *this;
    }
    String& operator=(const char* s) {
        if (s == nullptr) return *this = nullptr;
        std::string::assign(s);
        null_ = false;
        return *this;
    }
    String& operator=(const std::string& s) {
        std::string::assign(s);
        null_ = false;
        return *this;
    }
    String& operator=(std::string&& s) noexcept {
        std::string::operator=(std::move(s));
        null_ = false;
        return *this;
    }
    String& operator=(std::string_view s) {
        std::string::assign(s);
        null_ = false;
        return *this;
    }

    // ---- null
    bool isNull() const noexcept { return null_; }
    friend bool operator==(const String& a, std::nullptr_t) noexcept { return a.null_; }

    // ---- equality (null-aware: null == null, null != "")
    friend bool operator==(const String& a, const String& b) noexcept {
        if (a.null_ || b.null_) return a.null_ == b.null_;
        return static_cast<const std::string&>(a) == static_cast<const std::string&>(b);
    }
    friend bool operator==(const String& a, const std::string& b) noexcept {
        return !a.null_ && static_cast<const std::string&>(a) == b;
    }
    friend bool operator==(const String& a, const char* b) noexcept {
        if (b == nullptr) return a.null_;
        return !a.null_ && static_cast<const std::string&>(a) == b;
    }
    friend bool operator==(const String& a, std::string_view b) noexcept {
        return !a.null_ && std::string_view(a) == b;
    }

    // ---- java.lang.String API
    int32_t length() const noexcept { return static_cast<int32_t>(std::string::size()); }
    bool isEmpty() const noexcept { return std::string::empty(); }
    char16_t charAt(int32_t index) const;
    int32_t codePointAt(int32_t index) const;

    bool equals(const String& o) const noexcept;
    bool equals(const char* o) const noexcept;
    bool equals(std::nullptr_t) const noexcept { return false; }
    bool equals(Object* o) const;  // true iff o is a boxed string (jlang::box) with equal text
    bool equalsIgnoreCase(const String& o) const noexcept;
    bool contentEquals(const String& o) const noexcept { return equals(o); }
    int32_t compareTo(const String& o) const noexcept;
    int32_t compareToIgnoreCase(const String& o) const noexcept;
    bool regionMatches(int32_t toffset, const String& other, int32_t ooffset, int32_t len) const noexcept;
    bool regionMatches(bool ignoreCase, int32_t toffset, const String& other, int32_t ooffset,
                       int32_t len) const noexcept;

    bool startsWith(const String& prefix) const noexcept;
    bool startsWith(const String& prefix, int32_t toffset) const noexcept;
    bool endsWith(const String& suffix) const noexcept;

    // indexOf(int ch): ch is a UTF-16 code unit / code point (char16_t, char and int all work).
    int32_t indexOf(int32_t ch) const noexcept;
    int32_t indexOf(int32_t ch, int32_t fromIndex) const noexcept;
    int32_t indexOf(const String& str) const noexcept;
    int32_t indexOf(const String& str, int32_t fromIndex) const noexcept;
    int32_t lastIndexOf(int32_t ch) const noexcept;
    int32_t lastIndexOf(int32_t ch, int32_t fromIndex) const noexcept;
    int32_t lastIndexOf(const String& str) const noexcept;
    int32_t lastIndexOf(const String& str, int32_t fromIndex) const noexcept;
    bool contains(const String& s) const noexcept;

    String substring(int32_t beginIndex) const;
    String substring(int32_t beginIndex, int32_t endIndex) const;
    String subSequence(int32_t beginIndex, int32_t endIndex) const;

    // Unicode-aware for Latin-1, Latin Extended, Greek and Cyrillic (Locale ignored).
    String toLowerCase() const;
    String toLowerCase(Locale* locale) const;
    String toUpperCase() const;
    String toUpperCase(Locale* locale) const;
    String trim() const;  // strips chars <= ' ' at both ends

    // Regex methods use Java regex syntax (translated to ECMAScript for common constructs;
    // single literal characters and escaped literals take a fast path like Java's).
    Array<String>* split(const String& regex) const;
    Array<String>* split(const String& regex, int32_t limit) const;
    String replace(char16_t oldChar, char16_t newChar) const;
    String replace(const String& target, const String& replacement) const;
    String replaceAll(const String& regex, const String& replacement) const;
    String replaceFirst(const String& regex, const String& replacement) const;
    bool matches(const String& regex) const;

    int32_t hashCode() const noexcept;  // Java's s[0]*31^(n-1) + ... over UTF-16 code units
    String concat(const String& str) const;
    Array<char16_t>* toCharArray() const;
    Array<int8_t>* getBytes() const;                           // UTF-8
    Array<int8_t>* getBytes(const String& charsetName) const;  // UnsupportedEncodingException
    template<class C>
        requires requires(C* c) { c->name(); }
    Array<int8_t>* getBytes(C* charset) const {  // java.nio.charset.Charset
        return getBytes(String(charset->name()));
    }
    String intern() const { return *this; }
    String toString() const { return *this; }

    // ---- conversions
    std::u16string toUtf16() const;
    static String fromUtf16(const char16_t* s, size_t n);
    static String fromUtf16(const std::u16string& s) { return fromUtf16(s.data(), s.size()); }
    // Number of UTF-16 code units (Java's length()) of this text.
    int32_t utf16Length() const noexcept;

    // ---- static factories
    // String.valueOf(x): Java string conversion of any concatenable value.
    template<class T>
        requires detail::Concatenable<T>
    static String valueOf(const T& v) {
        String r("");
        detail::appendValue(r, v);
        return r;
    }
    static String valueOf(Array<char16_t>* chars);
    static String valueOf(Array<char16_t>* chars, int32_t offset, int32_t count);
    static String copyValueOf(Array<char16_t>* chars) { return valueOf(chars); }
    static String copyValueOf(Array<char16_t>* chars, int32_t offset, int32_t count) {
        return valueOf(chars, offset, count);
    }

    // String.format(fmt, args...): Java Formatter syntax (%s %S %d %x %X %o %f %e %E %g %c %b
    // %h %n %% with argument index 'n$', flags '-#+ 0,(' , width and precision).
    // A single jlang::Array<T>* argument (Object[] / String[] ...) is expanded like Java varargs.
    template<class... A>
    static String format(const String& fmt, const A&... args);
    template<class... A>
    static String format(Locale* locale, const String& fmt, const A&... args) {
        (void)locale;
        return format(fmt, args...);
    }
    template<class... A>
    static String format(Locale* locale, const char* fmt, const A&... args) {
        (void)locale;
        return format(String(fmt), args...);
    }

    // String.join(delimiter, elements): any range of concatenable values (or a pointer to one).
    template<class R>
    static String join(const String& delimiter, const R& elements);

    // ---- concatenation (Java '+'): see detail::Concatenable for the accepted types.
    template<class T>
        requires detail::Concatenable<T>
    String& operator+=(T&& v) {
        makeNonNull();
        detail::appendValue(*this, v);
        return *this;
    }
    // String on the right-hand side: non-template overloads (exact matches beat templates).
    friend String operator+(const String& a, const String& b) {
        String r(a);
        r.makeNonNull();
        detail::appendString(r, b);
        return r;
    }
    friend String operator+(String&& a, const String& b) {
        a.makeNonNull();
        detail::appendString(a, b);
        return std::move(a);
    }
    template<class T>
        requires(detail::Concatenable<T> && !std::is_same_v<std::remove_cvref_t<T>, String>)
    friend String operator+(const String& a, T&& b) {
        String r(a);
        r.makeNonNull();
        detail::appendValue(r, b);
        return r;
    }
    template<class T>
        requires(detail::Concatenable<T> && !std::is_same_v<std::remove_cvref_t<T>, String>)
    friend String operator+(String&& a, T&& b) {
        a.makeNonNull();
        detail::appendValue(a, b);
        return std::move(a);
    }
    template<class T>
        requires(detail::Concatenable<T> && !std::is_same_v<std::remove_cvref_t<T>, String>)
    friend String operator+(T&& a, const String& b) {
        String r("");
        detail::appendValue(r, a);
        detail::appendString(r, b);
        return r;
    }

private:
    void makeNonNull() {
        if (null_) {
            std::string::assign("null", 4);
            null_ = false;
        }
    }
    bool null_ = true;
};

// jlang::str(a, b, c...) == Java "" + a + b + c...
template<class... A>
String str(const A&... args) {
    String r("");
    (detail::appendValue(r, args), ...);
    return r;
}

// Java Float.toString / Double.toString (shortest round-trip digits, Java's formatting rules).
String floatToString(float v);
String doubleToString(double v);

namespace detail {
// Translates Java regex syntax to std::regex ECMAScript syntax: leading inline flags
// ((?i) (?s) (?m) are reported through the out-parameters), \p{Alpha}-style classes,
// \Q..\E quoting, possessive quantifiers and atomic groups (made greedy / non-capturing),
// named groups (made unnamed), \A \Z \z \h \R. Used by String and jlang::Pattern.
std::string javaRegexToEcma(const std::string& javaRegex, bool* icase, bool* dotall, bool* multiline);
}  // namespace detail

// ---------------------------------------------------------------------------------------
namespace detail {

template<class T>
void appendValue(std::string& out, const T& v) {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<D, String>) {
        appendString(out, v);
    } else if constexpr (std::is_same_v<D, std::nullptr_t>) {
        out.append("null", 4);
    } else if constexpr (std::is_same_v<D, bool>) {
        if (v) out.append("true", 4); else out.append("false", 5);
    } else if constexpr (std::is_same_v<D, char>) {
        out.push_back(v);
    } else if constexpr (std::is_same_v<D, char16_t>) {
        appendChar16(out, v);
    } else if constexpr (std::is_same_v<D, char32_t> || std::is_same_v<D, wchar_t>) {
        appendCodePoint(out, static_cast<char32_t>(v));
    } else if constexpr (std::is_same_v<D, char8_t>) {
        out.push_back(static_cast<char>(v));
    } else if constexpr (std::is_same_v<D, float>) {
        appendFloat(out, v);
    } else if constexpr (std::is_floating_point_v<D>) {
        appendDouble(out, static_cast<double>(v));
    } else if constexpr (std::is_integral_v<D> && std::is_signed_v<D>) {
        appendInt(out, static_cast<int64_t>(v));
    } else if constexpr (std::is_integral_v<D>) {
        appendUInt(out, static_cast<uint64_t>(v));
    } else if constexpr (std::is_enum_v<D>) {
        appendInt(out, static_cast<int64_t>(v));
    } else if constexpr (std::is_same_v<std::decay_t<D>, const char*> ||
                         std::is_same_v<std::decay_t<D>, char*>) {
        const char* p = v;
        if (p != nullptr) out.append(p); else out.append("null", 4);
    } else if constexpr (std::is_convertible_v<const D&, std::string_view>) {
        out.append(std::string_view(v));
    } else if constexpr (IsOptional<D>::value) {
        if (v.has_value()) appendValue(out, *v); else out.append("null", 4);
    } else if constexpr (std::is_pointer_v<D>) {
        appendObject(out, v == nullptr ? nullptr : asObject(v));
    } else if constexpr (HasToStringConst<D>) {
        appendString(out, String(v.toString()));
    } else {
        static_assert(sizeof(D) == 0, "type cannot be converted to jlang::String");
    }
}

// ---- String.format arguments
struct FormatArg {
    enum Kind : uint8_t { NUL, BOOL, INT, UINT, FLOAT, DOUBLE, CHAR, STR, OBJ };
    Kind kind = NUL;
    uint8_t bits = 32;  // integer width (for %x/%o of negative values)
    union {
        bool b;
        int64_t i;
        uint64_t u;
        double d;
        char32_t c;
        Object* o;
    };
    String s;
    FormatArg() : i(0) {}
};

template<class T>
FormatArg makeFormatArg(const T& v) {
    using D = std::remove_cvref_t<T>;
    FormatArg a;
    if constexpr (std::is_same_v<D, std::nullptr_t>) {
        a.kind = FormatArg::NUL;
    } else if constexpr (std::is_same_v<D, bool>) {
        a.kind = FormatArg::BOOL;
        a.b = v;
    } else if constexpr (std::is_same_v<D, char> || std::is_same_v<D, char16_t> ||
                         std::is_same_v<D, char32_t> || std::is_same_v<D, wchar_t>) {
        a.kind = FormatArg::CHAR;
        a.c = static_cast<char32_t>(static_cast<std::make_unsigned_t<D>>(v));
    } else if constexpr (std::is_same_v<D, float>) {
        a.kind = FormatArg::FLOAT;
        a.d = v;
    } else if constexpr (std::is_floating_point_v<D>) {
        a.kind = FormatArg::DOUBLE;
        a.d = static_cast<double>(v);
    } else if constexpr (std::is_integral_v<D> && std::is_signed_v<D>) {
        a.kind = FormatArg::INT;
        a.i = v;
        a.bits = static_cast<uint8_t>(sizeof(D) * 8);
    } else if constexpr (std::is_integral_v<D>) {
        a.kind = FormatArg::UINT;
        a.u = v;
        a.bits = static_cast<uint8_t>(sizeof(D) * 8);
    } else if constexpr (std::is_enum_v<D>) {
        a.kind = FormatArg::INT;
        a.i = static_cast<int64_t>(v);
    } else if constexpr (std::is_same_v<D, String>) {
        if (v.isNull()) {
            a.kind = FormatArg::NUL;
        } else {
            a.kind = FormatArg::STR;
            a.s = v;
        }
    } else if constexpr (std::is_same_v<std::decay_t<D>, const char*> ||
                         std::is_same_v<std::decay_t<D>, char*>) {
        const char* p = v;
        if (p == nullptr) {
            a.kind = FormatArg::NUL;
        } else {
            a.kind = FormatArg::STR;
            a.s = p;
        }
    } else if constexpr (std::is_convertible_v<const D&, std::string_view>) {
        a.kind = FormatArg::STR;
        a.s = String(std::string_view(v));
    } else if constexpr (IsOptional<D>::value) {
        if (v.has_value()) return makeFormatArg(*v);
        a.kind = FormatArg::NUL;
    } else if constexpr (std::is_pointer_v<D>) {
        if (v == nullptr) {
            a.kind = FormatArg::NUL;
        } else {
            a.kind = FormatArg::OBJ;
            a.o = asObject(v);
        }
    } else if constexpr (HasToStringConst<D>) {
        a.kind = FormatArg::STR;
        a.s = String(v.toString());
    } else {
        static_assert(sizeof(D) == 0, "type cannot be a String.format argument");
    }
    return a;
}

String formatImpl(const String& fmt, const FormatArg* args, size_t n);

// A single jlang::Array<T>* argument to format() is expanded like Java's Object... varargs.
template<class T> struct IsObjectArrayPtr : std::false_type {};
template<class E>
struct IsObjectArrayPtr<Array<E>*> : std::bool_constant<std::is_pointer_v<E> || std::is_same_v<E, String>> {};

template<class Arr>
String formatArray(const String& fmt, Arr* arr) {
    if (arr == nullptr) return formatImpl(fmt, nullptr, 0);
    const int32_t n = arr->length;
    FormatArg* args = new FormatArg[n > 0 ? n : 1];
    for (int32_t i = 0; i < n; i++) args[i] = makeFormatArg((*arr)[i]);
    return formatImpl(fmt, args, static_cast<size_t>(n));
}

}  // namespace detail

template<class... A>
String String::format(const String& fmt, const A&... args) {
    if constexpr (sizeof...(A) == 0) {
        return detail::formatImpl(fmt, nullptr, 0);
    } else if constexpr (sizeof...(A) == 1 &&
                         (detail::IsObjectArrayPtr<std::remove_cvref_t<A>>::value && ...)) {
        return detail::formatArray(fmt, args...);
    } else {
        const detail::FormatArg arr[] = {detail::makeFormatArg(args)...};
        return detail::formatImpl(fmt, arr, sizeof...(A));
    }
}

template<class R>
String String::join(const String& delimiter, const R& elements) {
    String r("");
    bool first = true;
    auto add = [&](const auto& range) {
        for (const auto& e : range) {
            if (!first) detail::appendString(r, delimiter);
            first = false;
            detail::appendValue(r, e);
        }
    };
    if constexpr (std::is_pointer_v<R>) {
        if (elements != nullptr) add(*elements);
    } else {
        add(elements);
    }
    return r;
}

// ---------------------------------------------------------------------------------------
// java.lang.StringBuilder / StringBuffer / javolution TextBuilder (GC object).
// Indices are UTF-8 code units, like jlang::String. Not synchronized (StringBuffer sharing
// across threads must use JSYNC).
class StringBuilder : public virtual Object {
public:
    StringBuilder() {}
    explicit StringBuilder(int32_t capacity);
    explicit StringBuilder(const String& s) : buf_(s.isNull() ? std::string("null") : std::string(s)) {}
    explicit StringBuilder(const char* s) : StringBuilder(String(s)) {}

    // append(x): Java string conversion of x (see jlang::String concatenation).
    template<class T>
        requires detail::Concatenable<T>
    StringBuilder* append(const T& v) {
        detail::appendValue(buf_, v);
        return this;
    }
    StringBuilder* append(Array<char16_t>* chars);  // append(char[])
    StringBuilder* append(Array<char16_t>* chars, int32_t offset, int32_t len);
    StringBuilder* append(const String& s, int32_t start, int32_t end);  // append(CharSequence,int,int)
    StringBuilder* appendCodePoint(int32_t cp);

    template<class T>
        requires detail::Concatenable<T>
    StringBuilder* insert(int32_t offset, const T& v) {
        std::string tmp;
        detail::appendValue(tmp, v);
        return insertRaw(offset, tmp);
    }

    int32_t length() const noexcept { return static_cast<int32_t>(buf_.size()); }
    bool isEmpty() const noexcept { return buf_.empty(); }
    char16_t charAt(int32_t index);
    void setCharAt(int32_t index, char16_t ch);
    StringBuilder* deleteCharAt(int32_t index);
    StringBuilder* delete_(int32_t start, int32_t end);
    StringBuilder* replace(int32_t start, int32_t end, const String& str);
    StringBuilder* reverse();
    void setLength(int32_t newLength);
    int32_t indexOf(const String& str);
    int32_t indexOf(const String& str, int32_t fromIndex);
    int32_t lastIndexOf(const String& str);
    int32_t lastIndexOf(const String& str, int32_t fromIndex);
    String substring(int32_t start);
    String substring(int32_t start, int32_t end);
    String subSequence(int32_t start, int32_t end) { return substring(start, end); }
    int32_t capacity() const noexcept { return static_cast<int32_t>(buf_.capacity()); }
    void ensureCapacity(int32_t minimumCapacity);
    void trimToSize() {}
    String toString() override;
    int32_t hashCode() override { return Object::hashCode(); }  // identity, as in Java
    bool equals(Object* o) override { return Object::equals(o); }

    // javolution TextBuilder compatibility.
    static StringBuilder* newInstance() { return new StringBuilder(); }
    static void recycle(StringBuilder*) {}
    StringBuilder* clear() {
        buf_.clear();
        return this;
    }

    // Direct access to the UTF-8 buffer (jlang internal use).
    std::string& buffer() noexcept { return buf_; }

private:
    StringBuilder* insertRaw(int32_t offset, const std::string& s);
    std::string buf_;
};

// ---------------------------------------------------------------------------------------
// java.text.CharacterIterator / StringCharacterIterator (iterates UTF-16 code units).
class StringCharacterIterator : public virtual Object {
public:
    static constexpr char16_t DONE = u'￿';
    explicit StringCharacterIterator(const String& text);
    StringCharacterIterator(const String& text, int32_t pos);
    char16_t first();
    char16_t last();
    char16_t current();
    char16_t next();
    char16_t previous();
    char16_t setIndex(int32_t position);
    int32_t getBeginIndex() { return 0; }
    int32_t getEndIndex() { return static_cast<int32_t>(text_.size()); }
    int32_t getIndex() { return pos_; }
    void setText(const String& text);

private:
    std::u16string text_;
    int32_t pos_ = 0;
};

// ---------------------------------------------------------------------------------------
// org.apache.commons.lang.StringUtils (the parts the code base uses, plus common helpers).
class StringUtils {
public:
    static inline const String EMPTY = String("");
    static bool isEmpty(const String& s) { return s.isNull() || s.isEmpty(); }
    static bool isNotEmpty(const String& s) { return !isEmpty(s); }
    static bool isBlank(const String& s);
    static bool isNotBlank(const String& s) { return !isBlank(s); }
    static String trimToEmpty(const String& s) { return s.isNull() ? EMPTY : s.trim(); }
    static String defaultString(const String& s) { return s.isNull() ? EMPTY : s; }
    static bool equals(const String& a, const String& b) { return a == b; }
    static String capitalize(const String& s);
};

}  // namespace jlang

// std::hash so jlang::String works as an unordered_map/set key (hashes the UTF-8 content).
template<>
struct std::hash<jlang::String> {
    size_t operator()(const jlang::String& s) const noexcept {
        return std::hash<std::string_view>()(std::string_view(s));
    }
};
