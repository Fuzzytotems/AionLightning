// jlang/IOBase.h - pieces shared by <jlang/IO.h>, <jlang/Nio.h> and <jlang/Regex.h>:
//
//   * java.io.Closeable / java.io.Flushable / java.lang.AutoCloseable (jlang::Closeable,
//     jlang::Flushable) - every stream, reader, writer and channel implements Closeable, so
//     IOUtils::closeQuietly(x) accepts all of them;
//   * the JDK exceptions of java.io / java.nio / java.net / java.util.regex / java.util.zip
//     that the core <jlang/Exceptions.h> does not declare;
//   * java.nio.charset.Charset (UTF-8, ISO-8859-1, US-ASCII, UTF-16LE, UTF-16BE, UTF-16) with
//     Java's decoding (malformed input -> U+FFFD, same granularity as the JDK decoders) and
//     encoding (unmappable -> '?', or U+FFFD for the UTF-16 family) rules.
//
// Translated code normally includes <jlang/IO.h> or <jlang/Nio.h>, which include this header.
#pragma once

#include <jlang/Array.h>
#include <jlang/Exceptions.h>
#include <jlang/Object.h>
#include <jlang/String.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace jlang {

class ByteBuffer;
class CharBuffer;

// ---------------------------------------------------------------------------------------
// java.io.Closeable (also java.lang.AutoCloseable) and java.io.Flushable.
class Closeable : public virtual Object {
public:
    virtual void close() = 0;
};
using AutoCloseable = Closeable;

class Flushable : public virtual Object {
public:
    virtual void flush() = 0;
};

// ---------------------------------------------------------------------------------------
// Extra JDK exceptions (constructors as in <jlang/Exceptions.h>; toString() uses the Java
// class name given here).
#define JLANG_JDK_EXCEPTION(Name, Base, JavaName)                                            \
    class Name : public Base {                                                               \
    public:                                                                                  \
        Name() : Base() {}                                                                   \
        explicit Name(const ::jlang::String& message) : Base(message) {}                     \
        explicit Name(const char* message) : Base(::jlang::String(message)) {}               \
        Name(const ::jlang::String& message, const ::jlang::Throwable& cause)                \
            : Base(message, cause) {}                                                        \
        Name(const ::jlang::String& message, ::jlang::Throwable* cause) : Base(message, cause) {} \
        explicit Name(::jlang::Throwable* cause) : Base(cause) {}                            \
        Name(const ::jlang::Throwable& cause, ::jlang::Throwable::WrapTag t) : Base(cause, t) {} \
        template<class E>                                                                    \
            requires(std::is_base_of_v<::jlang::Throwable, E> && !std::is_same_v<E, Name>)  \
        explicit Name(const E& cause)                                                        \
            : Base(static_cast<const ::jlang::Throwable&>(cause), ::jlang::Throwable::WrapTag{}) {} \
        ::jlang::String className() const override { return ::jlang::String(JavaName); }     \
        JLANG_THROWABLE(Name)                                                                \
    }

// java.io
JLANG_JDK_EXCEPTION(UTFDataFormatException, IOException, "java.io.UTFDataFormatException");
JLANG_JDK_EXCEPTION(InterruptedIOException, IOException, "java.io.InterruptedIOException");
JLANG_JDK_EXCEPTION(CharConversionException, IOException, "java.io.CharConversionException");
// java.nio
JLANG_JDK_EXCEPTION(ReadOnlyBufferException, UnsupportedOperationException, "java.nio.ReadOnlyBufferException");
JLANG_JDK_EXCEPTION(InvalidMarkException, IllegalStateException, "java.nio.InvalidMarkException");
// java.nio.channels
JLANG_JDK_EXCEPTION(ClosedSelectorException, IllegalStateException, "java.nio.channels.ClosedSelectorException");
JLANG_JDK_EXCEPTION(IllegalBlockingModeException, IllegalStateException, "java.nio.channels.IllegalBlockingModeException");
JLANG_JDK_EXCEPTION(NotYetConnectedException, IllegalStateException, "java.nio.channels.NotYetConnectedException");
JLANG_JDK_EXCEPTION(AlreadyConnectedException, IllegalStateException, "java.nio.channels.AlreadyConnectedException");
JLANG_JDK_EXCEPTION(ConnectionPendingException, IllegalStateException, "java.nio.channels.ConnectionPendingException");
JLANG_JDK_EXCEPTION(NoConnectionPendingException, IllegalStateException, "java.nio.channels.NoConnectionPendingException");
JLANG_JDK_EXCEPTION(NotYetBoundException, IllegalStateException, "java.nio.channels.NotYetBoundException");
JLANG_JDK_EXCEPTION(AlreadyBoundException, IllegalStateException, "java.nio.channels.AlreadyBoundException");
JLANG_JDK_EXCEPTION(UnresolvedAddressException, IllegalArgumentException, "java.nio.channels.UnresolvedAddressException");
JLANG_JDK_EXCEPTION(UnsupportedAddressTypeException, IllegalArgumentException, "java.nio.channels.UnsupportedAddressTypeException");
JLANG_JDK_EXCEPTION(IllegalSelectorException, IllegalArgumentException, "java.nio.channels.IllegalSelectorException");
JLANG_JDK_EXCEPTION(AsynchronousCloseException, ClosedChannelException, "java.nio.channels.AsynchronousCloseException");
// java.nio.charset
JLANG_JDK_EXCEPTION(UnsupportedCharsetException, IllegalArgumentException, "java.nio.charset.UnsupportedCharsetException");
JLANG_JDK_EXCEPTION(IllegalCharsetNameException, IllegalArgumentException, "java.nio.charset.IllegalCharsetNameException");
JLANG_JDK_EXCEPTION(CharacterCodingException, IOException, "java.nio.charset.CharacterCodingException");
// java.net
JLANG_JDK_EXCEPTION(ConnectException, SocketException, "java.net.ConnectException");
JLANG_JDK_EXCEPTION(BindException, SocketException, "java.net.BindException");
JLANG_JDK_EXCEPTION(NoRouteToHostException, SocketException, "java.net.NoRouteToHostException");
// java.util.zip
JLANG_JDK_EXCEPTION(ZipException, IOException, "java.util.zip.ZipException");
// java.util
JLANG_JDK_EXCEPTION(InputMismatchException, NoSuchElementException, "java.util.InputMismatchException");

// java.util.MissingResourceException(message, className, key)
class MissingResourceException : public RuntimeException {
public:
    MissingResourceException(const String& message, const String& className, const String& key)
        : RuntimeException(message), className_(className), key_(key) {}
    String getClassName() { return *className_; }
    String getKey() { return *key_; }
    String className() const override { return String("java.util.MissingResourceException"); }
    JLANG_THROWABLE(MissingResourceException)

private:
    detail::Pinned<String> className_;
    detail::Pinned<String> key_;
};

// ---------------------------------------------------------------------------------------
// java.nio.charset.Charset
//
// Supported: UTF-8, ISO-8859-1, US-ASCII, UTF-16BE, UTF-16LE, UTF-16 (BOM-detecting decoder,
// BOM-writing big-endian encoder), under their canonical names and the usual aliases
// ("utf8", "UTF-16le", "latin1", "ISO8859_1", "ASCII", ...; case-insensitive).
// Charset objects are interned: forName("utf8") == forName("UTF-8").
class Charset : public virtual Object {
public:
    enum Kind : int32_t { UTF_8_KIND, ISO_8859_1_KIND, US_ASCII_KIND, UTF_16BE_KIND, UTF_16LE_KIND, UTF_16_KIND };

    // UnsupportedCharsetException / IllegalCharsetNameException, like Java.
    static Charset* forName(const String& charsetName);
    // nullptr when unknown (IllegalCharsetNameException for syntactically illegal names).
    static Charset* lookup(const String& charsetName);
    static bool isSupported(const String& charsetName);
    static Charset* defaultCharset();  // UTF-8 (file.encoding of the ported server)
    // For java.io APIs that take a charset *name*: UnsupportedEncodingException when unknown.
    static Charset* forNameIO(const String& charsetName);

    String name() { return name_; }
    String displayName() { return name_; }
    // Historical java.io name, as InputStreamReader.getEncoding() returns ("UTF8", "ISO8859_1", ...)
    String historicalName();
    Kind kind() const noexcept { return kind_; }
    bool canEncode() { return true; }
    bool isRegistered() { return true; }

    // Charset.encode(String) -> buffer positioned at 0 with limit = encoded length.
    ByteBuffer* encode(const String& s);
    // Charset.decode(ByteBuffer): decodes bb's remaining bytes (advancing its position).
    CharBuffer* decode(ByteBuffer* bb);

    // Helpers for jlang (not Java API).
    Array<int8_t>* encodeToArray(const String& s);
    String decodeToString(const int8_t* bytes, int32_t length);
    String decodeToString(Array<int8_t>* bytes) { return decodeToString(bytes->data(), bytes->length); }
    String decodeToString(Array<int8_t>* bytes, int32_t offset, int32_t length);

    int32_t compareTo(Charset* that);
    bool equals(Object* o) override;
    int32_t hashCode() override { return name_.hashCode(); }
    String toString() override { return name_; }

    Charset(Kind kind, const char* name);  // use forName()

private:
    Kind kind_;
    String name_;
};

// java.nio.charset.StandardCharsets
struct StandardCharsets {
    static Charset* UTF_8();
    static Charset* ISO_8859_1();
    static Charset* US_ASCII();
    static Charset* UTF_16BE();
    static Charset* UTF_16LE();
    static Charset* UTF_16();
};

namespace detail {

// Incremental bytes -> UTF-16 decoder with the JDK's replacement behaviour. Bytes of an
// incomplete trailing sequence are kept until more input arrives (or endOfInput).
struct CharDecoderState {
    int32_t kind = Charset::UTF_8_KIND;
    uint8_t pend[4] = {0, 0, 0, 0};
    int32_t npend = 0;
    int32_t utf16Order = 0;  // UTF-16: 0 = not yet detected, 1 = big endian, 2 = little endian
    void decode(const uint8_t* p, std::size_t n, std::u16string& out, bool endOfInput);
    void reset() {
        npend = 0;
        utf16Order = 0;
    }
};

// Incremental UTF-16 -> bytes encoder (a high surrogate at the end of one call is kept until
// the next call). Unmappable characters become '?' (U+FFFD for the UTF-16 family), like the
// JDK encoders with REPLACE.
struct CharEncoderState {
    int32_t kind = Charset::UTF_8_KIND;
    char16_t pendingHigh = 0;
    bool bomWritten = false;
    void encode(const char16_t* p, std::size_t n, std::string& out, bool endOfInput);
    void reset() {
        pendingHigh = 0;
        bomWritten = false;
    }
};

// One-shot conversions.
std::u16string decodeBytes(int32_t kind, const uint8_t* p, std::size_t n);
std::string encodeChars(int32_t kind, const char16_t* p, std::size_t n);
// jlang::String (UTF-8) -> bytes in the charset / bytes -> jlang::String.
std::string encodeString(int32_t kind, const String& s);
String decodeToString(int32_t kind, const uint8_t* p, std::size_t n);

}  // namespace detail

}  // namespace jlang
