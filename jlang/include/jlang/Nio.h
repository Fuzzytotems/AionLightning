// jlang/Nio.h - java.nio buffers, byte order and charsets; java.nio.channels (selectors and
// TCP socket channels over epoll) and java.net addresses/sockets.
//
//   java.nio.Buffer / ByteBuffer         -> jlang::ByteBuffer*
//   java.nio.CharBuffer                  -> jlang::CharBuffer*
//   java.nio.ByteOrder                   -> jlang::ByteOrder (value: ByteOrder::LITTLE_ENDIAN)
//   java.nio.charset.Charset             -> jlang::Charset*          (declared in IOBase.h)
//   java.nio.channels.Selector           -> jlang::Selector*
//   java.nio.channels.SelectionKey       -> jlang::SelectionKey*
//   java.nio.channels.SelectableChannel  -> jlang::SelectableChannel* (also Channel,
//        AbstractSelectableChannel, AbstractInterruptibleChannel)
//   java.nio.channels.ServerSocketChannel / SocketChannel -> jlang::ServerSocketChannel* / SocketChannel*
//   java.nio.channels.spi.SelectorProvider -> jlang::SelectorProvider*
//   java.net.InetAddress / InetSocketAddress (and SocketAddress) / Socket / ServerSocket
//
// Name changes (C++ keywords, CONVENTIONS §3.4): channel.register(sel, ops[, att]) ->
// channel->register_(sel, ops[, att]).
//
// ByteBuffer has the exact java.nio semantics: position/limit/mark/capacity rules and the
// exceptions (BufferUnderflowException, BufferOverflowException, IndexOutOfBoundsException,
// IllegalArgumentException from position(n)/limit(n), InvalidMarkException,
// ReadOnlyBufferException); big-endian by default; slice()/duplicate()/asReadOnlyBuffer()
// share the backing array and reset the order to BIG_ENDIAN; array() returns the shared
// backing jlang::Array<int8_t>*. allocateDirect() returns a heap buffer that reports
// isDirect() == true (and still has an accessible array()).
//
// Channels and selectors follow Java NIO (level triggered): select() returns the number of
// keys whose ready set was updated; selectedKeys() is the live jlang::Set the caller iterates
// and removes from (iterator()->remove() or clear()); readyOps() only contains interest ops;
// cancel() deregisters at the next select; wakeup() works from any thread (and before a
// select). Deviations: interestOps(int) and register_() take effect immediately (epoll is
// thread safe) instead of at the next select, and register_() never blocks while another
// thread is in select(). See the notes at each class.
#pragma once

#include <jlang/IOBase.h>

// <endian.h> defines LITTLE_ENDIAN and BIG_ENDIAN as macros (it is pulled in by <cstdlib>,
// <string>, ...), which would break jlang::ByteOrder::LITTLE_ENDIAN. Include it once here and
// remove the two macros (the __LITTLE_ENDIAN/__BYTE_ORDER forms stay available).
#if __has_include(<endian.h>)
#include <endian.h>
#endif
#undef LITTLE_ENDIAN
#undef BIG_ENDIAN

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace jlang {

template<class T> class Set;
class SelectionKey;
class Selector;
class SelectorProvider;
class SocketChannel;
class ServerSocketChannel;
class Socket;
class ServerSocket;

// =======================================================================================
// java.nio.ByteOrder
class ByteOrder final {
public:
    enum class Value : int32_t { _NULL = -1, BIG = 0, LITTLE = 1 };
    static const ByteOrder BIG_ENDIAN;
    static const ByteOrder LITTLE_ENDIAN;

    constexpr ByteOrder() = default;
    constexpr ByteOrder(std::nullptr_t) {}
    constexpr operator Value() const noexcept { return v_; }

    static ByteOrder nativeOrder() noexcept;
    String toString() const;  // "BIG_ENDIAN" / "LITTLE_ENDIAN"
    String name() const { return toString(); }
    bool equals(ByteOrder o) const noexcept { return v_ == o.v_; }
    int32_t hashCode() const noexcept { return static_cast<int32_t>(v_); }
    friend constexpr bool operator==(ByteOrder a, ByteOrder b) noexcept { return a.v_ == b.v_; }
    friend constexpr bool operator==(ByteOrder a, std::nullptr_t) noexcept { return a.v_ == Value::_NULL; }
    constexpr bool isBigEndian() const noexcept { return v_ == Value::BIG; }

private:
    constexpr explicit ByteOrder(Value v) : v_(v) {}
    Value v_ = Value::_NULL;
};
inline constexpr ByteOrder ByteOrder::BIG_ENDIAN = ByteOrder(ByteOrder::Value::BIG);
inline constexpr ByteOrder ByteOrder::LITTLE_ENDIAN = ByteOrder(ByteOrder::Value::LITTLE);

// =======================================================================================
namespace detail {
[[noreturn]] void throwBufferUnderflow();
[[noreturn]] void throwBufferOverflow();
[[noreturn]] void throwReadOnlyBuffer();
[[noreturn]] void throwBufferIndex(int32_t index, int32_t limit);  // single-byte checkIndex
[[noreturn]] void throwBufferIndexNoMsg();
[[noreturn]] void throwFromIndexSize(int32_t from, int32_t size, int32_t length);

template<class T>
inline T loadBE(const int8_t* p) noexcept {
    using U = std::make_unsigned_t<T>;
    U v = 0;
    for (size_t i = 0; i < sizeof(T); i++) v = static_cast<U>((v << 8) | static_cast<uint8_t>(p[i]));
    return static_cast<T>(v);
}
template<class T>
inline T loadLE(const int8_t* p) noexcept {
    using U = std::make_unsigned_t<T>;
    U v = 0;
    for (size_t i = sizeof(T); i-- > 0;) v = static_cast<U>((v << 8) | static_cast<uint8_t>(p[i]));
    return static_cast<T>(v);
}
template<class T>
inline void storeBE(int8_t* p, T x) noexcept {
    using U = std::make_unsigned_t<T>;
    U v = static_cast<U>(x);
    for (size_t i = sizeof(T); i-- > 0;) {
        p[i] = static_cast<int8_t>(v & 0xff);
        v = static_cast<U>(v >> 8);
    }
}
template<class T>
inline void storeLE(int8_t* p, T x) noexcept {
    using U = std::make_unsigned_t<T>;
    U v = static_cast<U>(x);
    for (size_t i = 0; i < sizeof(T); i++) {
        p[i] = static_cast<int8_t>(v & 0xff);
        v = static_cast<U>(v >> 8);
    }
}
}  // namespace detail

// =======================================================================================
// java.nio.ByteBuffer (heap buffer; also used for java.nio.Buffer)
//
// All methods are non-virtual and inline where they are on the packet encoding/decoding hot
// path. Java methods returning Buffer return ByteBuffer* here (chaining works:
// buf->slice()->limit(n)).
class ByteBuffer final : public virtual Object {
public:
    // ---- creation
    static ByteBuffer* allocate(int32_t capacity);
    static ByteBuffer* allocateDirect(int32_t capacity);
    static ByteBuffer* wrap(Array<int8_t>* array);
    static ByteBuffer* wrap(Array<int8_t>* array, int32_t offset, int32_t length);

    // ---- java.nio.Buffer
    int32_t capacity() const noexcept { return capacity_; }
    int32_t position() const noexcept { return position_; }
    ByteBuffer* position(int32_t newPosition) {
        if (newPosition > limit_ || newPosition < 0) badPosition(newPosition);
        if (mark_ > newPosition) mark_ = -1;
        position_ = newPosition;
        return this;
    }
    int32_t limit() const noexcept { return limit_; }
    ByteBuffer* limit(int32_t newLimit) {
        if (newLimit > capacity_ || newLimit < 0) badLimit(newLimit);
        limit_ = newLimit;
        if (position_ > newLimit) position_ = newLimit;
        if (mark_ > newLimit) mark_ = -1;
        return this;
    }
    ByteBuffer* mark() noexcept {
        mark_ = position_;
        return this;
    }
    ByteBuffer* reset();
    ByteBuffer* clear() noexcept {
        position_ = 0;
        limit_ = capacity_;
        mark_ = -1;
        return this;
    }
    ByteBuffer* flip() noexcept {
        limit_ = position_;
        position_ = 0;
        mark_ = -1;
        return this;
    }
    ByteBuffer* rewind() noexcept {
        position_ = 0;
        mark_ = -1;
        return this;
    }
    int32_t remaining() const noexcept {
        int32_t rem = limit_ - position_;
        return rem > 0 ? rem : 0;
    }
    bool hasRemaining() const noexcept { return position_ < limit_; }
    bool isReadOnly() const noexcept { return readOnly_; }
    bool isDirect() const noexcept { return direct_; }
    bool hasArray() const noexcept { return hb_ != nullptr && !readOnly_; }
    // The shared backing array (ReadOnlyBufferException for read-only buffers).
    Array<int8_t>* array();
    int32_t arrayOffset();

    // ---- derived buffers (share the content; order resets to BIG_ENDIAN like Java)
    ByteBuffer* slice();
    ByteBuffer* slice(int32_t index, int32_t length);
    ByteBuffer* duplicate();
    ByteBuffer* asReadOnlyBuffer();
    ByteBuffer* compact();
    CharBuffer* asCharBuffer();

    // ---- byte order
    ByteOrder order() const noexcept { return bigEndian_ ? ByteOrder::BIG_ENDIAN : ByteOrder::LITTLE_ENDIAN; }
    ByteBuffer* order(ByteOrder bo) noexcept {
        bigEndian_ = (bo == ByteOrder::BIG_ENDIAN);
        return this;
    }

    // ---- relative / absolute byte access
    int8_t get() { return base()[nextGetIndex()]; }
    int8_t get(int32_t index) { return base()[checkIndex(index)]; }
    ByteBuffer* put(int8_t b) {
        checkWritable();
        base()[nextPutIndex()] = b;
        return this;
    }
    ByteBuffer* put(int32_t index, int8_t b) {
        checkWritable();
        base()[checkIndex(index)] = b;
        return this;
    }

    // ---- bulk
    ByteBuffer* get(Array<int8_t>* dst);
    ByteBuffer* get(Array<int8_t>* dst, int32_t offset, int32_t length);
    ByteBuffer* get(int32_t index, Array<int8_t>* dst);
    ByteBuffer* get(int32_t index, Array<int8_t>* dst, int32_t offset, int32_t length);
    ByteBuffer* put(Array<int8_t>* src);
    ByteBuffer* put(Array<int8_t>* src, int32_t offset, int32_t length);
    ByteBuffer* put(int32_t index, Array<int8_t>* src);
    ByteBuffer* put(int32_t index, Array<int8_t>* src, int32_t offset, int32_t length);
    ByteBuffer* put(ByteBuffer* src);
    ByteBuffer* put(int32_t index, ByteBuffer* src, int32_t offset, int32_t length);

    // ---- typed access (respecting order())
    char16_t getChar() { return static_cast<char16_t>(getT<uint16_t>()); }
    char16_t getChar(int32_t index) { return static_cast<char16_t>(getT<uint16_t>(index)); }
    ByteBuffer* putChar(char16_t v) { return putT<uint16_t>(static_cast<uint16_t>(v)); }
    ByteBuffer* putChar(int32_t index, char16_t v) { return putT<uint16_t>(index, static_cast<uint16_t>(v)); }
    int16_t getShort() { return getT<int16_t>(); }
    int16_t getShort(int32_t index) { return getT<int16_t>(index); }
    ByteBuffer* putShort(int16_t v) { return putT<int16_t>(v); }
    ByteBuffer* putShort(int32_t index, int16_t v) { return putT<int16_t>(index, v); }
    int32_t getInt() { return getT<int32_t>(); }
    int32_t getInt(int32_t index) { return getT<int32_t>(index); }
    ByteBuffer* putInt(int32_t v) { return putT<int32_t>(v); }
    ByteBuffer* putInt(int32_t index, int32_t v) { return putT<int32_t>(index, v); }
    int64_t getLong() { return getT<int64_t>(); }
    int64_t getLong(int32_t index) { return getT<int64_t>(index); }
    ByteBuffer* putLong(int64_t v) { return putT<int64_t>(v); }
    ByteBuffer* putLong(int32_t index, int64_t v) { return putT<int64_t>(index, v); }
    float getFloat() { return bitsToFloat(getT<int32_t>()); }
    float getFloat(int32_t index) { return bitsToFloat(getT<int32_t>(index)); }
    ByteBuffer* putFloat(float v) { return putT<int32_t>(floatToBits(v)); }
    ByteBuffer* putFloat(int32_t index, float v) { return putT<int32_t>(index, floatToBits(v)); }
    double getDouble() { return bitsToDouble(getT<int64_t>()); }
    double getDouble(int32_t index) { return bitsToDouble(getT<int64_t>(index)); }
    ByteBuffer* putDouble(double v) { return putT<int64_t>(doubleToBits(v)); }
    ByteBuffer* putDouble(int32_t index, double v) { return putT<int64_t>(index, doubleToBits(v)); }

    // ---- Object / Comparable
    bool equals(Object* o) override;
    int32_t hashCode() override;
    int32_t compareTo(ByteBuffer* that);
    int32_t mismatch(ByteBuffer* that);
    // "java.nio.HeapByteBuffer[pos=0 lim=16 cap=16]" (HeapByteBufferR / DirectByteBuffer[R])
    String toString() override;

    // jlang helpers: raw pointer to the byte at absolute index 0 of this buffer.
    int8_t* rawBase() noexcept { return base(); }

    // Internal constructor (use allocate/wrap/slice/duplicate).
    ByteBuffer(Array<int8_t>* hb, int32_t mark, int32_t pos, int32_t lim, int32_t cap, int32_t offset,
               bool readOnly, bool direct);

private:
    int8_t* base() noexcept { return hb_->data() + offset_; }
    int32_t nextGetIndex() {
        int32_t p = position_;
        if (p >= limit_) detail::throwBufferUnderflow();
        position_ = p + 1;
        return p;
    }
    int32_t nextGetIndex(int32_t nb) {
        int32_t p = position_;
        if (limit_ - p < nb) detail::throwBufferUnderflow();
        position_ = p + nb;
        return p;
    }
    int32_t nextPutIndex() {
        int32_t p = position_;
        if (p >= limit_) detail::throwBufferOverflow();
        position_ = p + 1;
        return p;
    }
    int32_t nextPutIndex(int32_t nb) {
        int32_t p = position_;
        if (limit_ - p < nb) detail::throwBufferOverflow();
        position_ = p + nb;
        return p;
    }
    int32_t checkIndex(int32_t i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(limit_)) detail::throwBufferIndex(i, limit_);
        return i;
    }
    int32_t checkIndex(int32_t i, int32_t nb) {
        if (i < 0 || nb > limit_ - i) detail::throwBufferIndexNoMsg();
        return i;
    }
    void checkWritable() {
        if (readOnly_) detail::throwReadOnlyBuffer();
    }
    template<class T>
    T load(int32_t i) noexcept {
        return bigEndian_ ? detail::loadBE<T>(base() + i) : detail::loadLE<T>(base() + i);
    }
    template<class T>
    void store(int32_t i, T v) noexcept {
        if (bigEndian_) detail::storeBE<T>(base() + i, v);
        else detail::storeLE<T>(base() + i, v);
    }
    template<class T>
    T getT() {
        return load<T>(nextGetIndex(static_cast<int32_t>(sizeof(T))));
    }
    template<class T>
    T getT(int32_t index) {
        return load<T>(checkIndex(index, static_cast<int32_t>(sizeof(T))));
    }
    template<class T>
    ByteBuffer* putT(T v) {
        checkWritable();
        store<T>(nextPutIndex(static_cast<int32_t>(sizeof(T))), v);
        return this;
    }
    template<class T>
    ByteBuffer* putT(int32_t index, T v) {
        checkWritable();
        store<T>(checkIndex(index, static_cast<int32_t>(sizeof(T))), v);
        return this;
    }
    static float bitsToFloat(int32_t b) noexcept {
        float f;
        std::memcpy(&f, &b, 4);
        return f;
    }
    static int32_t floatToBits(float f) noexcept {  // floatToRawIntBits
        int32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    }
    static double bitsToDouble(int64_t b) noexcept {
        double d;
        std::memcpy(&d, &b, 8);
        return d;
    }
    static int64_t doubleToBits(double d) noexcept {  // doubleToRawLongBits
        int64_t b;
        std::memcpy(&b, &d, 8);
        return b;
    }
    [[noreturn]] void badPosition(int32_t newPosition);
    [[noreturn]] void badLimit(int32_t newLimit);

    Array<int8_t>* hb_;
    int32_t offset_;
    int32_t mark_ = -1;
    int32_t position_ = 0;
    int32_t limit_ = 0;
    int32_t capacity_ = 0;
    bool bigEndian_ = true;
    bool readOnly_ = false;
    bool direct_ = false;
};

// =======================================================================================
// java.nio.CharBuffer: a heap char buffer (allocate/wrap) or a view of a ByteBuffer
// (ByteBuffer::asCharBuffer, which keeps the byte buffer's order at creation, like Java).
class CharBuffer final : public virtual Object {
public:
    static CharBuffer* allocate(int32_t capacity);
    static CharBuffer* wrap(Array<char16_t>* array);
    static CharBuffer* wrap(Array<char16_t>* array, int32_t offset, int32_t length);
    static CharBuffer* wrap(const String& s);  // read-only, like CharBuffer.wrap(CharSequence)

    int32_t capacity() const noexcept { return capacity_; }
    int32_t position() const noexcept { return position_; }
    CharBuffer* position(int32_t newPosition);
    int32_t limit() const noexcept { return limit_; }
    CharBuffer* limit(int32_t newLimit);
    CharBuffer* mark() noexcept {
        mark_ = position_;
        return this;
    }
    CharBuffer* reset();
    CharBuffer* clear() noexcept {
        position_ = 0;
        limit_ = capacity_;
        mark_ = -1;
        return this;
    }
    CharBuffer* flip() noexcept {
        limit_ = position_;
        position_ = 0;
        mark_ = -1;
        return this;
    }
    CharBuffer* rewind() noexcept {
        position_ = 0;
        mark_ = -1;
        return this;
    }
    int32_t remaining() const noexcept {
        int32_t rem = limit_ - position_;
        return rem > 0 ? rem : 0;
    }
    bool hasRemaining() const noexcept { return position_ < limit_; }
    bool isReadOnly() const noexcept { return readOnly_; }
    bool hasArray() const noexcept { return hb_ != nullptr && !readOnly_; }
    Array<char16_t>* array();
    int32_t arrayOffset();
    ByteOrder order() const noexcept;

    char16_t get();
    char16_t get(int32_t index);
    CharBuffer* get(Array<char16_t>* dst);
    CharBuffer* get(Array<char16_t>* dst, int32_t offset, int32_t length);
    CharBuffer* put(char16_t c);
    CharBuffer* put(int32_t index, char16_t c);
    CharBuffer* put(Array<char16_t>* src);
    CharBuffer* put(Array<char16_t>* src, int32_t offset, int32_t length);
    CharBuffer* put(const String& src);  // UTF-16 code units of src
    CharBuffer* put(const String& src, int32_t start, int32_t end);  // UTF-16 indices
    CharBuffer* put(CharBuffer* src);
    CharBuffer* slice();
    CharBuffer* duplicate();
    CharBuffer* compact();

    // CharSequence: length() == remaining(), charAt(i) relative to position.
    int32_t length() const noexcept { return remaining(); }
    char16_t charAt(int32_t index);
    // The remaining characters as a String.
    String toString() override;
    bool equals(Object* o) override;
    int32_t hashCode() override;

    CharBuffer(Array<char16_t>* hb, ByteBuffer* bb, int32_t mark, int32_t pos, int32_t lim, int32_t cap,
               int32_t offset, bool readOnly, bool bigEndian);

private:
    char16_t load(int32_t i);
    void store(int32_t i, char16_t c);
    int32_t nextGetIndex();
    int32_t nextPutIndex();
    int32_t checkIndex(int32_t i);

    Array<char16_t>* hb_;  // heap storage, or
    ByteBuffer* bb_;       // byte buffer view storage (offset_ in bytes)
    int32_t mark_ = -1;
    int32_t position_ = 0;
    int32_t limit_ = 0;
    int32_t capacity_ = 0;
    int32_t offset_ = 0;
    bool readOnly_ = false;
    bool bigEndian_ = true;
};

// =======================================================================================
// java.net

// java.net.InetAddress (IPv4 = Inet4Address, IPv6 = Inet6Address).
class InetAddress : public virtual Object {
public:
    // Numeric literals are parsed without DNS; names are resolved with getaddrinfo (IPv4
    // addresses preferred, like java.net.preferIPv4Stack/preferIPv6Addresses=false).
    // null or "" -> the loopback address. UnknownHostException on failure.
    static InetAddress* getByName(const String& host);
    static Array<InetAddress*>* getAllByName(const String& host);
    static InetAddress* getByAddress(Array<int8_t>* addr);                     // 4 or 16 bytes
    static InetAddress* getByAddress(const String& host, Array<int8_t>* addr);
    static InetAddress* getLocalHost();
    static InetAddress* getLoopbackAddress();  // 127.0.0.1
    static InetAddress* anyLocalAddress();     // 0.0.0.0 (jlang helper)

    // Raw address in network byte order (a copy): 4 bytes for IPv4, 16 for IPv6.
    Array<int8_t>* getAddress();
    // "1.2.3.4" / Java's IPv6 text form "0:0:0:0:0:0:0:1" (no :: compression).
    String getHostAddress();
    // The host name given at creation, else a reverse lookup (cached), else the literal.
    String getHostName();
    String getCanonicalHostName();
    bool isAnyLocalAddress();
    bool isLoopbackAddress();
    bool isSiteLocalAddress();
    bool isLinkLocalAddress();
    bool isMulticastAddress();
    bool isIPv4() const noexcept { return len_ == 4; }

    bool equals(Object* o) override;
    int32_t hashCode() override;
    // "hostname/1.2.3.4" (empty host name part when unknown: "/1.2.3.4")
    String toString() override;

    // jlang internal: raw bytes and scope.
    InetAddress(const uint8_t* bytes, int32_t len, const String& hostName, uint32_t scopeId = 0);
    const uint8_t* rawBytes() const noexcept { return addr_; }
    int32_t rawLength() const noexcept { return len_; }
    uint32_t scopeId() const noexcept { return scope_; }

private:
    uint8_t addr_[16];
    int32_t len_;
    uint32_t scope_;
    String hostName_;
    std::mutex nameLock_;
};

// java.net.InetSocketAddress (java.net.SocketAddress maps here too).
class InetSocketAddress : public virtual Object {
public:
    explicit InetSocketAddress(int32_t port);                     // wildcard address
    InetSocketAddress(InetAddress* addr, int32_t port);           // null addr = wildcard
    InetSocketAddress(const String& hostname, int32_t port);      // resolves; unresolved on failure
    static InetSocketAddress* createUnresolved(const String& host, int32_t port);

    int32_t getPort() { return port_; }
    InetAddress* getAddress() { return addr_; }
    String getHostName();
    String getHostString();
    bool isUnresolved() { return addr_ == nullptr; }

    bool equals(Object* o) override;
    int32_t hashCode() override;
    // Java 6/8 format: addr.toString() + ":" + port, or "host:port" when unresolved.
    String toString() override;

private:
    InetSocketAddress() {}
    InetAddress* addr_ = nullptr;
    String hostname_;
    int32_t port_ = 0;
};

// =======================================================================================
// java.nio.channels

// SelectableChannel (also Channel / AbstractSelectableChannel / AbstractInterruptibleChannel).
// Channels start in blocking mode, like Java.
class SelectableChannel : public virtual Object, public virtual Closeable {
public:
    virtual int32_t validOps() = 0;
    // IllegalBlockingModeException if keys are registered and block == true.
    SelectableChannel* configureBlocking(bool block);
    bool isBlocking();
    Object* blockingLock() { return regLock_; }
    // Java register(): ClosedChannelException, IllegalBlockingModeException (channel in blocking
    // mode), IllegalArgumentException (ops not in validOps), ClosedSelectorException,
    // CancelledKeyException (a cancelled key of this channel is still registered with sel).
    // If already registered with sel: sets interest ops and the attachment of that key.
    SelectionKey* register_(Selector* sel, int32_t ops, Object* att = nullptr);
    SelectionKey* keyFor(Selector* sel);
    bool isRegistered();
    bool isOpen() { return open_.load(std::memory_order_acquire); }
    // Closes the socket; every key of the channel is cancelled first.
    void close() override;
    SelectorProvider* provider();

    // jlang internal
    int fd() const noexcept { return fd_; }
    void _removeKey(SelectionKey* k);

protected:
    SelectableChannel() {}
    void ensureOpen();
    // Creates the (IPv4) socket of a channel that has none yet, so it can be registered.
    virtual void _ensureFd() {}
    int fd_ = -1;
    std::atomic<bool> open_{true};
    bool blocking_ = true;
    Object* regLock_ = new Object();
    std::mutex keyLock_;
    std::vector<SelectionKey*> keys_;  // GC heap storage (global operator new)
};

// java.nio.channels.SocketChannel (TCP; IPv4 and IPv6).
class SocketChannel : public SelectableChannel {
public:
    static SocketChannel* open();
    // Blocking connect; the channel is closed and the exception rethrown on failure.
    static SocketChannel* open(InetSocketAddress* remote);
    int32_t validOps() override { return 1 | 4 | 8; }  // OP_READ | OP_WRITE | OP_CONNECT

    SocketChannel* bind(InetSocketAddress* local);
    // Blocking mode: connects and returns true. Non-blocking: returns true if connected at
    // once, otherwise false and finishConnect() completes it. ConnectException etc. on error.
    bool connect(InetSocketAddress* remote);
    bool finishConnect();
    bool isConnected();
    bool isConnectionPending();

    // Reads into dst's remaining space; returns bytes read, 0 (non-blocking, nothing
    // available, or no space) or -1 at end of stream. IOException("Connection reset by peer")
    // etc. on errors, ClosedChannelException when closed, NotYetConnectedException.
    int32_t read(ByteBuffer* dst);
    int64_t read(Array<ByteBuffer*>* dsts);
    // Writes from src's remaining bytes. Blocking mode writes everything; non-blocking mode
    // writes what the socket accepts (possibly 0).
    int32_t write(ByteBuffer* src);
    int64_t write(Array<ByteBuffer*>* srcs);

    SocketChannel* shutdownInput();
    SocketChannel* shutdownOutput();
    InetSocketAddress* getRemoteAddress();
    InetSocketAddress* getLocalAddress();
    Socket* socket();
    String toString() override;

    // jlang internal: wraps an accepted descriptor.
    SocketChannel(int fd, int family, bool connected);

private:
    SocketChannel() {}
    void ensureSocket(int family);
    void checkConnected();
    int family_ = 0;
    int32_t state_ = 0;  // 0 unconnected, 1 pending, 2 connected
    bool inputShutdown_ = false;
    bool outputShutdown_ = false;
    Socket* socket_ = nullptr;
    InetSocketAddress* remote_ = nullptr;
    std::mutex ioLock_;

    friend class Socket;
    friend class ServerSocketChannel;

protected:
    void _ensureFd() override;
};

// java.nio.channels.ServerSocketChannel
class ServerSocketChannel : public SelectableChannel {
public:
    static ServerSocketChannel* open();
    int32_t validOps() override { return 16; }  // OP_ACCEPT
    ServerSocketChannel* bind(InetSocketAddress* local);
    ServerSocketChannel* bind(InetSocketAddress* local, int32_t backlog);
    // Blocking: waits for a connection. Non-blocking: nullptr when none is pending.
    // The accepted channel is in blocking mode, like Java.
    SocketChannel* accept();
    InetSocketAddress* getLocalAddress();
    ServerSocket* socket();
    String toString() override;

private:
    ServerSocketChannel() {}
    void ensureSocket(int family);
    int family_ = 0;
    bool bound_ = false;
    ServerSocket* socket_ = nullptr;
    InetSocketAddress* local_ = nullptr;

    friend class ServerSocket;

protected:
    void _ensureFd() override;
};

// java.net.Socket, as returned by SocketChannel::socket() (an adaptor over the channel).
class Socket : public virtual Object, public virtual Closeable {
public:
    InetAddress* getInetAddress();   // remote address, null if not connected
    int32_t getPort();               // remote port, 0 if not connected
    InetAddress* getLocalAddress();
    int32_t getLocalPort();          // -1 if unbound
    InetSocketAddress* getRemoteSocketAddress();
    InetSocketAddress* getLocalSocketAddress();
    void setTcpNoDelay(bool on);
    bool getTcpNoDelay();
    void setSoTimeout(int32_t timeout) { soTimeout_ = timeout; }  // stored only (no effect on channels, as in Java)
    int32_t getSoTimeout() { return soTimeout_; }
    void setKeepAlive(bool on);
    bool getKeepAlive();
    void setReceiveBufferSize(int32_t size);
    int32_t getReceiveBufferSize();
    void setSendBufferSize(int32_t size);
    int32_t getSendBufferSize();
    void setSoLinger(bool on, int32_t linger);
    void setReuseAddress(bool on);
    void shutdownInput();
    void shutdownOutput();
    bool isConnected();
    bool isBound();
    bool isClosed();
    bool isInputShutdown();
    bool isOutputShutdown();
    void close() override;
    SocketChannel* getChannel() { return ch_; }
    // "Socket[addr=/127.0.0.1,port=1234,localport=5678]" / "Socket[unconnected]"
    String toString() override;

    explicit Socket(SocketChannel* ch) : ch_(ch) {}

private:
    SocketChannel* ch_;
    int32_t soTimeout_ = 0;
};

// java.net.ServerSocket, as returned by ServerSocketChannel::socket().
class ServerSocket : public virtual Object, public virtual Closeable {
public:
    void bind(InetSocketAddress* endpoint);
    void bind(InetSocketAddress* endpoint, int32_t backlog);
    InetAddress* getInetAddress();
    int32_t getLocalPort();  // -1 if unbound
    InetSocketAddress* getLocalSocketAddress();
    void setReuseAddress(bool on);
    bool getReuseAddress();
    void setReceiveBufferSize(int32_t size);
    int32_t getReceiveBufferSize();
    void setSoTimeout(int32_t timeout) { soTimeout_ = timeout; }
    int32_t getSoTimeout() { return soTimeout_; }
    bool isBound();
    bool isClosed();
    void close() override;
    ServerSocketChannel* getChannel() { return ch_; }
    // "ServerSocket[addr=0.0.0.0/0.0.0.0,localport=7777]" / "ServerSocket[unbound]"
    String toString() override;

    explicit ServerSocket(ServerSocketChannel* ch) : ch_(ch) {}

private:
    ServerSocketChannel* ch_;
    int32_t soTimeout_ = 0;
};

// java.nio.channels.SelectionKey
class SelectionKey final : public virtual Object {
public:
    static constexpr int32_t OP_READ = 1 << 0;
    static constexpr int32_t OP_WRITE = 1 << 2;
    static constexpr int32_t OP_CONNECT = 1 << 3;
    static constexpr int32_t OP_ACCEPT = 1 << 4;

    SelectableChannel* channel() { return channel_; }
    Selector* selector() { return selector_; }
    bool isValid() { return valid_.load(std::memory_order_acquire); }
    // Requests deregistration (effective at the next selection operation); idempotent.
    void cancel();
    // CancelledKeyException when the key is cancelled (Java).
    int32_t interestOps();
    // IllegalArgumentException for ops outside validOps(); CancelledKeyException. Thread safe;
    // takes effect immediately (see the header comment).
    SelectionKey* interestOps(int32_t ops);
    int32_t interestOpsOr(int32_t ops);
    int32_t interestOpsAnd(int32_t ops);
    int32_t readyOps();
    bool isReadable() { return (readyOps() & OP_READ) != 0; }
    bool isWritable() { return (readyOps() & OP_WRITE) != 0; }
    bool isConnectable() { return (readyOps() & OP_CONNECT) != 0; }
    bool isAcceptable() { return (readyOps() & OP_ACCEPT) != 0; }
    // Returns the previous attachment.
    Object* attach(Object* ob) { return attachment_.exchange(ob, std::memory_order_acq_rel); }
    Object* attachment() { return attachment_.load(std::memory_order_acquire); }
    String toString() override;

    // jlang internal
    SelectionKey(SelectableChannel* ch, Selector* sel, uint64_t id, int32_t ops, Object* att);
    uint64_t id() const noexcept { return id_; }
    int32_t rawInterest() const noexcept { return interest_.load(std::memory_order_acquire); }
    int32_t rawReady() const noexcept { return ready_; }
    void setReady(int32_t r) noexcept { ready_ = r; }
    void invalidate() noexcept { valid_.store(false, std::memory_order_release); }
    uint32_t registeredEvents = 0;  // epoll events currently registered (selector lock)

private:
    SelectableChannel* channel_;
    Selector* selector_;
    uint64_t id_;
    std::atomic<int32_t> interest_;
    int32_t ready_ = 0;
    std::atomic<bool> valid_{true};
    std::atomic<Object*> attachment_;
};

// java.nio.channels.Selector (epoll based, level triggered).
class Selector : public virtual Object, public virtual Closeable {
public:
    static Selector* open();
    bool isOpen() { return open_.load(std::memory_order_acquire); }
    SelectorProvider* provider();
    // A snapshot of the registered keys (includes cancelled keys not yet deregistered).
    Set<SelectionKey*>* keys();
    // The selected-key set itself: iterate and remove processed keys (or clear()).
    Set<SelectionKey*>* selectedKeys();
    int32_t select();                 // blocks until >= 1 key updated, wakeup() or interrupt
    int32_t select(int64_t timeout);  // timeout in ms (0 = forever); IllegalArgumentException if < 0
    int32_t selectNow();
    Selector* wakeup();
    void close() override;
    String toString() override;

    // jlang internal (used by channels and keys)
    SelectionKey* _register(SelectableChannel* ch, int32_t ops, Object* att);
    void _updateInterest(SelectionKey* k);  // re-arm epoll for k's interest set
    void _cancel(SelectionKey* k);          // key cancelled: remove from epoll, queue deregistration

    Selector();

private:
    int32_t doSelect(int32_t timeoutMs);
    void processDeregisterQueue();
    void applyEvents(SelectionKey* k);

    int epfd_ = -1;
    int wakeupFd_ = -1;
    std::atomic<bool> open_{true};
    std::mutex lock_;         // keys, cancelled set, epoll registration state
    std::mutex selectLock_;   // one selecting thread at a time; held by close()
    std::mutex wakeLock_;     // wakeup state
    bool wakeupPending_ = false;
    uint64_t nextId_ = 1;
    Set<SelectionKey*>* keys_;
    Set<SelectionKey*>* selected_;
    std::vector<SelectionKey*> cancelled_;
    void* keyMap_ = nullptr;  // id -> key (defined in NioSelector.cpp)
};

// java.nio.channels.spi.SelectorProvider
class SelectorProvider : public virtual Object {
public:
    static SelectorProvider* provider();
    Selector* openSelector() { return Selector::open(); }
    ServerSocketChannel* openServerSocketChannel() { return ServerSocketChannel::open(); }
    SocketChannel* openSocketChannel() { return SocketChannel::open(); }
};

}  // namespace jlang
