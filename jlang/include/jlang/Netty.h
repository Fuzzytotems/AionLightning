// jlang/Netty.h - a faithful subset of Netty 3.2.0.BETA1 (org.jboss.netty.*), namespace
// jlang::netty, sufficient for the chatserver (and anything shaped like it).
//
// Java -> C++ (see tools/cppgen/jdkmap.tsv):
//   org.jboss.netty.buffer.ChannelBuffer            -> jlang::netty::ChannelBuffer*
//   org.jboss.netty.buffer.ChannelBuffers           -> jlang::netty::ChannelBuffers (static)
//   org.jboss.netty.buffer.HeapChannelBufferFactory -> jlang::netty::HeapChannelBufferFactory*
//   org.jboss.netty.channel.*                       -> jlang::netty::* (same simple names)
//   org.jboss.netty.channel.group.DefaultChannelGroup -> jlang::netty::ChannelGroup* (concrete;
//                                                      `DefaultChannelGroup` is an alias)
//   org.jboss.netty.bootstrap.ServerBootstrap       -> jlang::netty::ServerBootstrap*
//   org.jboss.netty.channel.socket.nio.NioServerSocketChannelFactory
//                                                   -> jlang::netty::NioServerSocketChannelFactory*
//   org.jboss.netty.handler.codec.frame.LengthFieldBasedFrameDecoder / FrameDecoder,
//   org.jboss.netty.handler.codec.oneone.OneToOneDecoder / OneToOneEncoder,
//   org.jboss.netty.handler.execution.ExecutionHandler / OrderedMemoryAwareThreadPoolExecutor
//                                                   -> jlang::netty::<same name>*
//
// Java semantics kept (Netty 3.2.0.BETA1 source was ported line by line where it matters):
//   * ChannelBuffer: readerIndex/writerIndex model, byte order per buffer, fixed-capacity heap
//     buffers (IndexOutOfBoundsException on overflow/underflow, checked before copying),
//     dynamic buffers that double their capacity, slices/duplicates that share content,
//     ChannelBuffers::EMPTY_BUFFER (big-endian, capacity 0), content equals/hashCode/compareTo.
//   * Pipeline: upstream events travel head->tail through ChannelUpstreamHandlers, downstream
//     events tail->head through ChannelDownstreamHandlers and then into the transport sink. An
//     exception thrown by a handler is turned into an ExceptionEvent fired from the pipeline head
//     (exceptionCaught). write() runs the downstream handlers (encoders) in the caller's thread.
//   * Transport (on the jlang NIO layer, <jlang/Nio.h>): one boss thread per bound server
//     channel and N I/O worker threads per factory (accepted channels are assigned round
//     robin), all run as Runnables on the user supplied Executors (Netty's NioWorker /
//     NioServerSocketPipelineSink loops). Accepted channels fire channelOpen, channelBound,
//     channelConnected; a close (local or remote) fires channelDisconnected, channelUnbound,
//     channelClosed, in that order. write() is thread-safe; the writes of one channel are sent
//     in call order; a write future completes when its bytes were handed to the kernel.
//     close() closes at once (pending writes fail with ClosedChannelException, as in Netty);
//     write(x)->addListener(ChannelFutureListener::CLOSE) closes after x was flushed.
//   * ExecutionHandler + OrderedMemoryAwareThreadPoolExecutor: upstream events are handed to the
//     pool; the events of one channel run one at a time and in order (per-channel child
//     executor), different channels run in parallel. Per-channel/total memory limits suspend
//     reads (setReadable(false)) or make the I/O thread wait.
//
// Deliberate differences from 3.2.0.BETA1 (each fixes a race that can hang or corrupt a
// connection; see the class comments): the kernel socket of a channel is closed by its I/O
// thread; a channel's child executor lives until the closed channel's queue drained; the
// memory limits use a lost-wake-up-free limiter. Unsupported: client bootstrap/connect, UDP,
// the local/http/ssl/... handlers, ChannelLocal, FileRegion.
//
// Memory: everything is GC allocated with new, never deleted. OS resources (sockets, epoll)
// are released by close()/releaseExternalResources().
#pragma once

#include <jlang/jlang.h>
#include <jlang/Nio.h>
#include <jlang/Thread.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jlang::netty {

class Channel;
class ChannelBuffer;
class ChannelBufferFactory;
class ChannelConfig;
class ChannelEvent;
class ChannelFactory;
class ChannelFuture;
class ChannelFutureListener;
class ChannelGroup;
class ChannelGroupFuture;
class ChannelHandler;
class ChannelHandlerContext;
class ChannelPipeline;
class ChannelPipelineFactory;
class ChannelSink;
class ExecutionHandler;

// =======================================================================================
// Exceptions (Java names via className()).
#define JLANG_NETTY_EXCEPTION(Name, Base, JavaName)                                             \
    class Name : public Base {                                                                   \
    public:                                                                                      \
        Name() : Base() {}                                                                       \
        explicit Name(const ::jlang::String& message) : Base(message) {}                        \
        explicit Name(const char* message) : Base(::jlang::String(message)) {}                  \
        Name(const ::jlang::String& message, const ::jlang::Throwable& cause) : Base(message, cause) {} \
        Name(const ::jlang::String& message, ::jlang::Throwable* cause) : Base(message, cause) {} \
        explicit Name(::jlang::Throwable* cause) : Base(cause) {}                                \
        Name(const ::jlang::Throwable& cause, ::jlang::Throwable::WrapTag t) : Base(cause, t) {} \
        template<class E>                                                                        \
            requires(std::is_base_of_v<::jlang::Throwable, E> && !std::is_same_v<E, Name>)      \
        explicit Name(const E& cause)                                                            \
            : Base(static_cast<const ::jlang::Throwable&>(cause), ::jlang::Throwable::WrapTag{}) {} \
        ::jlang::String className() const override { return ::jlang::String(JavaName); }        \
        JLANG_THROWABLE(Name)                                                                    \
    }

JLANG_NETTY_EXCEPTION(ChannelException, ::jlang::RuntimeException, "org.jboss.netty.channel.ChannelException");
JLANG_NETTY_EXCEPTION(ChannelPipelineException, ChannelException,
                      "org.jboss.netty.channel.ChannelPipelineException");
JLANG_NETTY_EXCEPTION(ChannelHandlerLifeCycleException, ::jlang::RuntimeException,
                      "org.jboss.netty.channel.ChannelHandlerLifeCycleException");
JLANG_NETTY_EXCEPTION(CorruptedFrameException, ::jlang::Exception,
                      "org.jboss.netty.handler.codec.frame.CorruptedFrameException");
JLANG_NETTY_EXCEPTION(TooLongFrameException, ::jlang::Exception,
                      "org.jboss.netty.handler.codec.frame.TooLongFrameException");
// java.nio.channels.NotYetConnectedException (not part of the jlang core).
JLANG_NETTY_EXCEPTION(NotYetConnectedException, ::jlang::IllegalStateException,
                      "java.nio.channels.NotYetConnectedException");

// =======================================================================================
// Buffers
// =======================================================================================

// org.jboss.netty.buffer.ChannelBufferFactory
class ChannelBufferFactory : public virtual ::jlang::Object {
public:
    virtual ChannelBuffer* getBuffer(int32_t capacity) = 0;
    virtual ChannelBuffer* getBuffer(::jlang::ByteOrder endianness, int32_t capacity) = 0;
    virtual ChannelBuffer* getBuffer(::jlang::Array<int8_t>* array, int32_t offset, int32_t length) = 0;
    virtual ChannelBuffer* getBuffer(::jlang::ByteOrder endianness, ::jlang::Array<int8_t>* array,
                                     int32_t offset, int32_t length) = 0;
    virtual ::jlang::ByteOrder getDefaultOrder() = 0;
};

// org.jboss.netty.buffer.HeapChannelBufferFactory
class HeapChannelBufferFactory : public virtual ChannelBufferFactory {
public:
    HeapChannelBufferFactory();                                   // big endian
    explicit HeapChannelBufferFactory(::jlang::ByteOrder defaultOrder);
    // Shared instances (big endian / the given order).
    static ChannelBufferFactory* getInstance();
    static ChannelBufferFactory* getInstance(::jlang::ByteOrder endianness);

    ChannelBuffer* getBuffer(int32_t capacity) override;
    ChannelBuffer* getBuffer(::jlang::ByteOrder order, int32_t capacity) override;
    ChannelBuffer* getBuffer(::jlang::Array<int8_t>* array, int32_t offset, int32_t length) override;
    ChannelBuffer* getBuffer(::jlang::ByteOrder order, ::jlang::Array<int8_t>* array, int32_t offset,
                             int32_t length) override;
    ::jlang::ByteOrder getDefaultOrder() override;

private:
    bool littleEndian_ = false;
};

// org.jboss.netty.buffer.ChannelBuffer. One concrete class implements Netty's heap
// (BigEndian/LittleEndianHeapChannelBuffer), DynamicChannelBuffer and the Sliced/Truncated/
// Duplicated views; create buffers with ChannelBuffers::* or a ChannelBufferFactory.
// Not thread-safe (like Netty).
class ChannelBuffer : public virtual ::jlang::Object {
public:
    // ---- capacity / order / factory
    int32_t capacity();
    ChannelBufferFactory* factory();
    ::jlang::ByteOrder order();
    bool isDirect() { return false; }

    // ---- indexes
    int32_t readerIndex() { return readerIndex_; }
    void readerIndex(int32_t readerIndex);
    int32_t writerIndex() { return writerIndex_; }
    void writerIndex(int32_t writerIndex);
    void setIndex(int32_t readerIndex, int32_t writerIndex);
    int32_t readableBytes() { return writerIndex_ - readerIndex_; }
    int32_t writableBytes() { return capacity() - writerIndex_; }
    bool readable() { return readableBytes() > 0; }
    bool writable() { return writableBytes() > 0; }
    void clear() { readerIndex_ = writerIndex_ = 0; }
    void markReaderIndex() { markedReaderIndex_ = readerIndex_; }
    void resetReaderIndex() { readerIndex(markedReaderIndex_); }
    void markWriterIndex() { markedWriterIndex_ = writerIndex_; }
    void resetWriterIndex() { writerIndex_ = markedWriterIndex_; }
    void discardReadBytes();
    // Fixed buffers: IndexOutOfBoundsException if fewer bytes are writable; dynamic buffers grow.
    void ensureWritableBytes(int32_t writableBytes);

    // ---- absolute get (indexes are not modified)
    int8_t getByte(int32_t index);
    int16_t getUnsignedByte(int32_t index) { return static_cast<int16_t>(getByte(index) & 0xFF); }
    int16_t getShort(int32_t index);
    int32_t getUnsignedShort(int32_t index) { return getShort(index) & 0xFFFF; }
    int32_t getMedium(int32_t index);
    int32_t getUnsignedMedium(int32_t index);
    int32_t getInt(int32_t index);
    int64_t getUnsignedInt(int32_t index) { return getInt(index) & INT64_C(0xFFFFFFFF); }
    int64_t getLong(int32_t index);
    char16_t getChar(int32_t index) { return static_cast<char16_t>(getShort(index)); }
    float getFloat(int32_t index);
    double getDouble(int32_t index);
    void getBytes(int32_t index, ChannelBuffer* dst);
    void getBytes(int32_t index, ChannelBuffer* dst, int32_t length);
    void getBytes(int32_t index, ChannelBuffer* dst, int32_t dstIndex, int32_t length);
    void getBytes(int32_t index, ::jlang::Array<int8_t>* dst);
    void getBytes(int32_t index, ::jlang::Array<int8_t>* dst, int32_t dstIndex, int32_t length);

    // ---- absolute set
    void setByte(int32_t index, int32_t value);
    void setShort(int32_t index, int32_t value);
    void setMedium(int32_t index, int32_t value);
    void setInt(int32_t index, int32_t value);
    void setLong(int32_t index, int64_t value);
    void setChar(int32_t index, int32_t value) { setShort(index, value); }
    void setFloat(int32_t index, float value);
    void setDouble(int32_t index, double value);
    void setBytes(int32_t index, ChannelBuffer* src);
    void setBytes(int32_t index, ChannelBuffer* src, int32_t length);
    void setBytes(int32_t index, ChannelBuffer* src, int32_t srcIndex, int32_t length);
    void setBytes(int32_t index, ::jlang::Array<int8_t>* src);
    void setBytes(int32_t index, ::jlang::Array<int8_t>* src, int32_t srcIndex, int32_t length);
    void setZero(int32_t index, int32_t length);

    // ---- relative read (IndexOutOfBoundsException, index unchanged, if not enough bytes)
    int8_t readByte();
    int16_t readUnsignedByte() { return static_cast<int16_t>(readByte() & 0xFF); }
    int16_t readShort();
    int32_t readUnsignedShort() { return readShort() & 0xFFFF; }
    int32_t readMedium();
    int32_t readUnsignedMedium();
    int32_t readInt();
    int64_t readUnsignedInt() { return readInt() & INT64_C(0xFFFFFFFF); }
    int64_t readLong();
    char16_t readChar() { return static_cast<char16_t>(readShort()); }
    float readFloat();
    double readDouble();
    // A new buffer (same order, from factory()) with the next `length` bytes;
    // ChannelBuffers::EMPTY_BUFFER for 0.
    ChannelBuffer* readBytes(int32_t length);
    ChannelBuffer* readSlice(int32_t length);
    void readBytes(::jlang::Array<int8_t>* dst);
    void readBytes(::jlang::Array<int8_t>* dst, int32_t dstIndex, int32_t length);
    void readBytes(ChannelBuffer* dst);
    void readBytes(ChannelBuffer* dst, int32_t length);
    void readBytes(ChannelBuffer* dst, int32_t dstIndex, int32_t length);
    void skipBytes(int32_t length);

    // ---- relative write
    void writeByte(int32_t value);
    void writeShort(int32_t value);
    void writeMedium(int32_t value);
    void writeInt(int32_t value);
    void writeLong(int64_t value);
    void writeChar(int32_t value) { writeShort(value); }
    void writeFloat(float value);
    void writeDouble(double value);
    void writeBytes(::jlang::Array<int8_t>* src);
    void writeBytes(::jlang::Array<int8_t>* src, int32_t srcIndex, int32_t length);
    void writeBytes(ChannelBuffer* src);
    void writeBytes(ChannelBuffer* src, int32_t length);
    void writeBytes(ChannelBuffer* src, int32_t srcIndex, int32_t length);
    void writeZero(int32_t length);

    // ---- search
    int32_t indexOf(int32_t fromIndex, int32_t toIndex, int8_t value);
    int32_t bytesBefore(int8_t value);
    int32_t bytesBefore(int32_t length, int8_t value);
    int32_t bytesBefore(int32_t index, int32_t length, int8_t value);

    // ---- copies and views
    ChannelBuffer* copy();
    ChannelBuffer* copy(int32_t index, int32_t length);
    ChannelBuffer* slice();
    ChannelBuffer* slice(int32_t index, int32_t length);
    ChannelBuffer* duplicate();

    // ---- backing array (always present: heap buffers only)
    bool hasArray() { return true; }
    ::jlang::Array<int8_t>* array();
    int32_t arrayOffset();

    // ---- Object
    // Content equality / hash / comparison over the readable bytes (ChannelBuffers::equals...).
    bool equals(::jlang::Object* o) override;
    int32_t hashCode() override;
    int32_t compareTo(ChannelBuffer* that);
    // "LittleEndianHeapChannelBuffer(ridx=0, widx=12, cap=16384)"
    ::jlang::String toString() override;

    // Internal: pointer to `length` bytes at `index` (bounds checked), for the transport.
    int8_t* rawBytes(int32_t index, int32_t length);

private:
    friend class ChannelBuffers;
    friend class HeapChannelBufferFactory;
    enum class Kind : uint8_t { HEAP, DYNAMIC, SLICED, TRUNCATED, DUPLICATED };
    ChannelBuffer(Kind kind) : kind_(kind) {}
    static ChannelBuffer* newHeap(bool littleEndian, ::jlang::Array<int8_t>* array, int32_t readerIndex,
                                  int32_t writerIndex);
    static ChannelBuffer* newDynamic(::jlang::ByteOrder order, int32_t estimatedLength,
                                     ChannelBufferFactory* factory);
    static ChannelBuffer* newSliced(ChannelBuffer* buffer, int32_t index, int32_t length);
    static ChannelBuffer* newTruncated(ChannelBuffer* buffer, int32_t length);
    static ChannelBuffer* newDuplicated(ChannelBuffer* buffer);
    bool isLittleEndian();
    void checkReadableBytes(int32_t minimumReadableBytes);
    void checkIndex(int32_t index, int32_t length);  // views: 0 <= index, index+length <= capacity
    uint64_t getN(int32_t index, int n);             // n-byte unsigned value in this buffer's order
    void setN(int32_t index, int n, uint64_t v);

    Kind kind_;
    bool littleEndian_ = false;            // HEAP / DYNAMIC
    int32_t readerIndex_ = 0;
    int32_t writerIndex_ = 0;
    int32_t markedReaderIndex_ = 0;
    int32_t markedWriterIndex_ = 0;
    ::jlang::Array<int8_t>* array_ = nullptr;  // HEAP
    ChannelBuffer* buffer_ = nullptr;          // DYNAMIC: current storage; views: the viewed buffer
    ChannelBufferFactory* factory_ = nullptr;  // DYNAMIC
    int32_t adjustment_ = 0;                   // SLICED: offset into buffer_
    int32_t length_ = 0;                       // SLICED / TRUNCATED: capacity
};

// org.jboss.netty.buffer.ChannelBuffers
class ChannelBuffers final {
public:
    ChannelBuffers() = delete;
    // A big-endian buffer whose capacity is 0.
    static ChannelBuffer* const EMPTY_BUFFER;

    static ChannelBuffer* buffer(int32_t capacity);
    static ChannelBuffer* buffer(::jlang::ByteOrder endianness, int32_t capacity);
    static ChannelBuffer* directBuffer(int32_t capacity) { return buffer(capacity); }
    static ChannelBuffer* directBuffer(::jlang::ByteOrder endianness, int32_t capacity) {
        return buffer(endianness, capacity);
    }
    static ChannelBuffer* dynamicBuffer();
    static ChannelBuffer* dynamicBuffer(int32_t estimatedLength);
    static ChannelBuffer* dynamicBuffer(::jlang::ByteOrder endianness, int32_t estimatedLength);
    static ChannelBuffer* dynamicBuffer(ChannelBufferFactory* factory);
    static ChannelBuffer* dynamicBuffer(int32_t estimatedLength, ChannelBufferFactory* factory);
    static ChannelBuffer* dynamicBuffer(::jlang::ByteOrder endianness, int32_t estimatedLength,
                                        ChannelBufferFactory* factory);
    static ChannelBuffer* wrappedBuffer(::jlang::Array<int8_t>* array);
    static ChannelBuffer* wrappedBuffer(::jlang::ByteOrder endianness, ::jlang::Array<int8_t>* array);
    static ChannelBuffer* wrappedBuffer(::jlang::Array<int8_t>* array, int32_t offset, int32_t length);
    static ChannelBuffer* wrappedBuffer(::jlang::ByteOrder endianness, ::jlang::Array<int8_t>* array,
                                        int32_t offset, int32_t length);
    static ChannelBuffer* wrappedBuffer(ChannelBuffer* buffer);
    static ChannelBuffer* copiedBuffer(::jlang::Array<int8_t>* array);
    static ChannelBuffer* copiedBuffer(::jlang::ByteOrder endianness, ::jlang::Array<int8_t>* array);
    static ChannelBuffer* copiedBuffer(::jlang::Array<int8_t>* array, int32_t offset, int32_t length);
    static ChannelBuffer* copiedBuffer(::jlang::ByteOrder endianness, ::jlang::Array<int8_t>* array,
                                       int32_t offset, int32_t length);
    static ChannelBuffer* copiedBuffer(ChannelBuffer* buffer);

    static ::jlang::String hexDump(ChannelBuffer* buffer);
    static ::jlang::String hexDump(ChannelBuffer* buffer, int32_t fromIndex, int32_t length);
    static int32_t hashCode(ChannelBuffer* buffer);
    static bool equals(ChannelBuffer* bufferA, ChannelBuffer* bufferB);
    static int32_t compare(ChannelBuffer* bufferA, ChannelBuffer* bufferB);
    static int32_t indexOf(ChannelBuffer* buffer, int32_t fromIndex, int32_t toIndex, int8_t value);
    static int16_t swapShort(int16_t value);
    static int32_t swapMedium(int32_t value);
    static int32_t swapInt(int32_t value);
    static int64_t swapLong(int64_t value);

private:
    friend class ChannelBuffer;
    static ChannelBuffer* emptyBuffer();  // EMPTY_BUFFER, safe during static initialization
};

// =======================================================================================
// Channel state, events, handlers
// =======================================================================================

// org.jboss.netty.channel.ChannelState (enum value class, CONVENTIONS §7)
class ChannelState final {
public:
    enum class Value : int32_t { _NULL = -1, OPEN, BOUND, CONNECTED, INTEREST_OPS };
    static const ChannelState OPEN, BOUND, CONNECTED, INTEREST_OPS;
    constexpr ChannelState() = default;
    constexpr ChannelState(std::nullptr_t) {}
    constexpr explicit ChannelState(Value v) : v_(v) {}
    constexpr operator Value() const { return v_; }
    int32_t ordinal() const { return static_cast<int32_t>(v_); }
    ::jlang::String name() const;
    ::jlang::String toString() const { return name(); }
    bool equals(ChannelState o) const { return v_ == o.v_; }
    int32_t hashCode() const { return ordinal(); }
    int32_t compareTo(ChannelState o) const { return ordinal() - o.ordinal(); }
    friend constexpr bool operator==(ChannelState a, std::nullptr_t) { return a.v_ == Value::_NULL; }

private:
    Value v_ = Value::_NULL;
};
inline constexpr ChannelState ChannelState::OPEN{ChannelState::Value::OPEN};
inline constexpr ChannelState ChannelState::BOUND{ChannelState::Value::BOUND};
inline constexpr ChannelState ChannelState::CONNECTED{ChannelState::Value::CONNECTED};
inline constexpr ChannelState ChannelState::INTEREST_OPS{ChannelState::Value::INTEREST_OPS};

// org.jboss.netty.channel.ChannelEvent and its sub-interfaces
class ChannelEvent : public virtual ::jlang::Object {
public:
    virtual Channel* getChannel() = 0;
    virtual ChannelFuture* getFuture() = 0;
};
class ChannelStateEvent : public virtual ChannelEvent {
public:
    virtual ChannelState getState() = 0;
    // OPEN: Boolean (TRUE = open), BOUND/CONNECTED: InetSocketAddress or null, INTEREST_OPS: Integer.
    virtual ::jlang::Object* getValue() = 0;
};
class MessageEvent : public virtual ChannelEvent {
public:
    virtual ::jlang::Object* getMessage() = 0;
    virtual ::jlang::InetSocketAddress* getRemoteAddress() = 0;
};
class ExceptionEvent : public virtual ChannelEvent {
public:
    virtual ::jlang::Throwable* getCause() = 0;
};
class WriteCompletionEvent : public virtual ChannelEvent {
public:
    virtual int64_t getWrittenAmount() = 0;
};
class ChildChannelStateEvent : public virtual ChannelEvent {
public:
    virtual Channel* getChildChannel() = 0;
};

// Event implementations (org.jboss.netty.channel.*Event).
class UpstreamChannelStateEvent : public virtual ChannelStateEvent {
public:
    UpstreamChannelStateEvent(Channel* channel, ChannelState state, ::jlang::Object* value);
    Channel* getChannel() override { return channel_; }
    ChannelFuture* getFuture() override;
    ChannelState getState() override { return state_; }
    ::jlang::Object* getValue() override { return value_; }
    ::jlang::String toString() override;

private:
    Channel* channel_;
    ChannelState state_;
    ::jlang::Object* value_;
};
class DownstreamChannelStateEvent : public virtual ChannelStateEvent {
public:
    DownstreamChannelStateEvent(Channel* channel, ChannelFuture* future, ChannelState state,
                                ::jlang::Object* value);
    Channel* getChannel() override { return channel_; }
    ChannelFuture* getFuture() override { return future_; }
    ChannelState getState() override { return state_; }
    ::jlang::Object* getValue() override { return value_; }
    ::jlang::String toString() override;

private:
    Channel* channel_;
    ChannelFuture* future_;
    ChannelState state_;
    ::jlang::Object* value_;
};
class UpstreamMessageEvent : public virtual MessageEvent {
public:
    UpstreamMessageEvent(Channel* channel, ::jlang::Object* message, ::jlang::InetSocketAddress* remoteAddress);
    Channel* getChannel() override { return channel_; }
    ChannelFuture* getFuture() override;
    ::jlang::Object* getMessage() override { return message_; }
    ::jlang::InetSocketAddress* getRemoteAddress() override;
    ::jlang::String toString() override;

private:
    Channel* channel_;
    ::jlang::Object* message_;
    ::jlang::InetSocketAddress* remoteAddress_;
};
class DownstreamMessageEvent : public virtual MessageEvent {
public:
    DownstreamMessageEvent(Channel* channel, ChannelFuture* future, ::jlang::Object* message,
                           ::jlang::InetSocketAddress* remoteAddress);
    Channel* getChannel() override { return channel_; }
    ChannelFuture* getFuture() override { return future_; }
    ::jlang::Object* getMessage() override { return message_; }
    ::jlang::InetSocketAddress* getRemoteAddress() override;
    ::jlang::String toString() override;

private:
    Channel* channel_;
    ChannelFuture* future_;
    ::jlang::Object* message_;
    ::jlang::InetSocketAddress* remoteAddress_;
};
class DefaultExceptionEvent : public virtual ExceptionEvent {
public:
    DefaultExceptionEvent(Channel* channel, ::jlang::Throwable* cause);
    Channel* getChannel() override { return channel_; }
    ChannelFuture* getFuture() override;
    ::jlang::Throwable* getCause() override { return cause_; }
    ::jlang::String toString() override;

private:
    Channel* channel_;
    ::jlang::Throwable* cause_;
};
class DefaultWriteCompletionEvent : public virtual WriteCompletionEvent {
public:
    DefaultWriteCompletionEvent(Channel* channel, int64_t writtenAmount);
    Channel* getChannel() override { return channel_; }
    ChannelFuture* getFuture() override;
    int64_t getWrittenAmount() override { return writtenAmount_; }
    ::jlang::String toString() override;

private:
    Channel* channel_;
    int64_t writtenAmount_;
};
class DefaultChildChannelStateEvent : public virtual ChildChannelStateEvent {
public:
    DefaultChildChannelStateEvent(Channel* parentChannel, Channel* childChannel);
    Channel* getChannel() override { return parentChannel_; }
    ChannelFuture* getFuture() override;
    Channel* getChildChannel() override { return childChannel_; }
    ::jlang::String toString() override;

private:
    Channel* parentChannel_;
    Channel* childChannel_;
};

// org.jboss.netty.channel.ChannelHandler (marker) and the up/downstream handler interfaces.
class ChannelHandler : public virtual ::jlang::Object {};
class ChannelUpstreamHandler : public virtual ChannelHandler {
public:
    virtual void handleUpstream(ChannelHandlerContext* ctx, ChannelEvent* e) = 0;
};
class ChannelDownstreamHandler : public virtual ChannelHandler {
public:
    virtual void handleDownstream(ChannelHandlerContext* ctx, ChannelEvent* e) = 0;
};
class LifeCycleAwareChannelHandler : public virtual ChannelHandler {
public:
    virtual void beforeAdd(ChannelHandlerContext* ctx) = 0;
    virtual void afterAdd(ChannelHandlerContext* ctx) = 0;
    virtual void beforeRemove(ChannelHandlerContext* ctx) = 0;
    virtual void afterRemove(ChannelHandlerContext* ctx) = 0;
};

// org.jboss.netty.channel.ChannelHandlerContext
class ChannelHandlerContext : public virtual ::jlang::Object {
public:
    virtual Channel* getChannel() = 0;
    virtual ChannelPipeline* getPipeline() = 0;
    virtual ::jlang::String getName() = 0;
    virtual ChannelHandler* getHandler() = 0;
    virtual bool canHandleUpstream() = 0;
    virtual bool canHandleDownstream() = 0;
    // Forwards the event to the next upstream handler (no-op at the tail).
    virtual void sendUpstream(ChannelEvent* e) = 0;
    // Forwards the event to the previous downstream handler, or to the transport sink.
    virtual void sendDownstream(ChannelEvent* e) = 0;
    virtual ::jlang::Object* getAttachment() = 0;
    virtual void setAttachment(::jlang::Object* attachment) = 0;
};

// org.jboss.netty.channel.ChannelSink (implemented by the transport)
class ChannelSink : public virtual ::jlang::Object {
public:
    virtual void eventSunk(ChannelPipeline* pipeline, ChannelEvent* e) = 0;
    // Default (AbstractChannelSink): fires an ExceptionEvent with the cause from the pipeline head.
    virtual void exceptionCaught(ChannelPipeline* pipeline, ChannelEvent* e, ChannelPipelineException& cause);
};

// org.jboss.netty.channel.ChannelPipeline
class ChannelPipeline : public virtual ::jlang::Object {
public:
    virtual void addFirst(const ::jlang::String& name, ChannelHandler* handler) = 0;
    virtual void addLast(const ::jlang::String& name, ChannelHandler* handler) = 0;
    virtual void addBefore(const ::jlang::String& baseName, const ::jlang::String& name,
                           ChannelHandler* handler) = 0;
    virtual void addAfter(const ::jlang::String& baseName, const ::jlang::String& name,
                          ChannelHandler* handler) = 0;
    virtual void remove(ChannelHandler* handler) = 0;
    virtual ChannelHandler* remove(const ::jlang::String& name) = 0;
    virtual ChannelHandler* removeFirst() = 0;
    virtual ChannelHandler* removeLast() = 0;
    virtual void replace(ChannelHandler* oldHandler, const ::jlang::String& newName,
                         ChannelHandler* newHandler) = 0;
    virtual ChannelHandler* replace(const ::jlang::String& oldName, const ::jlang::String& newName,
                                    ChannelHandler* newHandler) = 0;
    virtual ChannelHandler* getFirst() = 0;
    virtual ChannelHandler* getLast() = 0;
    virtual ChannelHandler* get(const ::jlang::String& name) = 0;
    virtual ChannelHandlerContext* getContext(const ::jlang::String& name) = 0;
    virtual ChannelHandlerContext* getContext(ChannelHandler* handler) = 0;
    // Handler names in pipeline order (Java toMap().keySet()).
    virtual std::vector<::jlang::String> getNames() = 0;
    virtual void sendUpstream(ChannelEvent* e) = 0;
    virtual void sendDownstream(ChannelEvent* e) = 0;
    virtual Channel* getChannel() = 0;
    virtual ChannelSink* getSink() = 0;
    virtual void attach(Channel* channel, ChannelSink* sink) = 0;
    virtual bool isAttached() = 0;
};

namespace detail {
class PipelineContext;
}

// org.jboss.netty.channel.DefaultChannelPipeline
class DefaultChannelPipeline : public virtual ChannelPipeline {
public:
    DefaultChannelPipeline() {}
    void addFirst(const ::jlang::String& name, ChannelHandler* handler) override;
    void addLast(const ::jlang::String& name, ChannelHandler* handler) override;
    void addBefore(const ::jlang::String& baseName, const ::jlang::String& name, ChannelHandler* handler) override;
    void addAfter(const ::jlang::String& baseName, const ::jlang::String& name, ChannelHandler* handler) override;
    void remove(ChannelHandler* handler) override;
    ChannelHandler* remove(const ::jlang::String& name) override;
    ChannelHandler* removeFirst() override;
    ChannelHandler* removeLast() override;
    void replace(ChannelHandler* oldHandler, const ::jlang::String& newName, ChannelHandler* newHandler) override;
    ChannelHandler* replace(const ::jlang::String& oldName, const ::jlang::String& newName,
                            ChannelHandler* newHandler) override;
    ChannelHandler* getFirst() override;
    ChannelHandler* getLast() override;
    ChannelHandler* get(const ::jlang::String& name) override;
    ChannelHandlerContext* getContext(const ::jlang::String& name) override;
    ChannelHandlerContext* getContext(ChannelHandler* handler) override;
    std::vector<::jlang::String> getNames() override;
    void sendUpstream(ChannelEvent* e) override;
    void sendDownstream(ChannelEvent* e) override;
    Channel* getChannel() override { return channel_.load(); }
    ChannelSink* getSink() override;
    void attach(Channel* channel, ChannelSink* sink) override;
    bool isAttached() override { return sink_.load() != nullptr; }
    // "DefaultChannelPipeline{(framedecoder = a.b.PacketFrameDecoder), ...}"
    ::jlang::String toString() override;

    // Internal (used by the handler contexts).
    void sendUpstream(detail::PipelineContext* ctx, ChannelEvent* e);
    void sendDownstream(detail::PipelineContext* ctx, ChannelEvent* e);
    detail::PipelineContext* getActualUpstreamContext(detail::PipelineContext* ctx);
    detail::PipelineContext* getActualDownstreamContext(detail::PipelineContext* ctx);
    void notifyHandlerException(ChannelEvent* e, ::jlang::Throwable* t);

private:
    friend class detail::PipelineContext;
    void init(const ::jlang::String& name, ChannelHandler* handler);
    void checkDuplicateName(const ::jlang::String& name);
    detail::PipelineContext* contextOrDie(const ::jlang::String& name);
    detail::PipelineContext* contextOrDie(ChannelHandler* handler);
    detail::PipelineContext* removeContext(detail::PipelineContext* ctx);
    ChannelHandler* replaceContext(detail::PipelineContext* ctx, const ::jlang::String& newName,
                                   ChannelHandler* newHandler);
    void callBeforeAdd(detail::PipelineContext* ctx);
    void callAfterAdd(detail::PipelineContext* ctx);
    void callBeforeRemove(detail::PipelineContext* ctx);
    void callAfterRemove(detail::PipelineContext* ctx);

    std::atomic<Channel*> channel_{nullptr};
    std::atomic<ChannelSink*> sink_{nullptr};
    std::atomic<detail::PipelineContext*> head_{nullptr};
    std::atomic<detail::PipelineContext*> tail_{nullptr};
    std::map<::jlang::String, detail::PipelineContext*> name2ctx_;
    std::recursive_mutex lock_;
};

// org.jboss.netty.channel.ChannelPipelineFactory
class ChannelPipelineFactory : public virtual ::jlang::Object {
public:
    virtual ChannelPipeline* getPipeline() = 0;
    template<class F> static ChannelPipelineFactory* of(F f);
};

// org.jboss.netty.channel.SimpleChannelUpstreamHandler: dispatches upstream events to the
// callbacks below; every default forwards the event upstream (ctx->sendUpstream(e)).
class SimpleChannelUpstreamHandler : public virtual ChannelUpstreamHandler {
public:
    SimpleChannelUpstreamHandler() {}
    void handleUpstream(ChannelHandlerContext* ctx, ChannelEvent* e) override;
    virtual void messageReceived(ChannelHandlerContext* ctx, MessageEvent* e);
    // Logs a warning when this handler is the last one in the pipeline, then forwards.
    virtual void exceptionCaught(ChannelHandlerContext* ctx, ExceptionEvent* e);
    virtual void channelOpen(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void channelBound(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void channelConnected(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void channelInterestChanged(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void channelDisconnected(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void channelUnbound(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void channelClosed(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void writeComplete(ChannelHandlerContext* ctx, WriteCompletionEvent* e);
    virtual void childChannelOpen(ChannelHandlerContext* ctx, ChildChannelStateEvent* e);
    virtual void childChannelClosed(ChannelHandlerContext* ctx, ChildChannelStateEvent* e);
};

// org.jboss.netty.channel.SimpleChannelDownstreamHandler
class SimpleChannelDownstreamHandler : public virtual ChannelDownstreamHandler {
public:
    SimpleChannelDownstreamHandler() {}
    void handleDownstream(ChannelHandlerContext* ctx, ChannelEvent* e) override;
    virtual void writeRequested(ChannelHandlerContext* ctx, MessageEvent* e);
    virtual void bindRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void connectRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void setInterestOpsRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void disconnectRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void unbindRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    virtual void closeRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e);
};

// =======================================================================================
// Futures
// =======================================================================================

// org.jboss.netty.channel.ChannelFuture
class ChannelFuture : public virtual ::jlang::Object {
public:
    virtual Channel* getChannel() = 0;
    virtual bool isDone() = 0;
    virtual bool isCancelled() = 0;
    virtual bool isSuccess() = 0;
    virtual ::jlang::Throwable* getCause() = 0;
    virtual bool cancel() = 0;
    virtual bool setSuccess() = 0;
    virtual bool setFailure(::jlang::Throwable* cause) = 0;
    virtual bool setProgress(int64_t amount, int64_t current, int64_t total) = 0;
    // Notified once when done (immediately, in the calling thread, if already done).
    virtual void addListener(ChannelFutureListener* listener) = 0;
    virtual void removeListener(ChannelFutureListener* listener) = 0;
    virtual ChannelFuture* await() = 0;                   // InterruptedException
    virtual ChannelFuture* awaitUninterruptibly() = 0;
    virtual bool await(int64_t timeout, ::jlang::TimeUnit unit) = 0;
    virtual bool await(int64_t timeoutMillis) = 0;
    virtual bool awaitUninterruptibly(int64_t timeout, ::jlang::TimeUnit unit) = 0;
    virtual bool awaitUninterruptibly(int64_t timeoutMillis) = 0;
};

// org.jboss.netty.channel.ChannelFutureListener
class ChannelFutureListener : public virtual ::jlang::Object {
public:
    virtual void operationComplete(ChannelFuture* future) = 0;
    // Closes the future's channel when the operation completes (success or failure).
    static ChannelFutureListener* const CLOSE;
    // Closes the future's channel when the operation failed.
    static ChannelFutureListener* const CLOSE_ON_FAILURE;
    template<class F> static ChannelFutureListener* of(F f);
};

// org.jboss.netty.channel.ChannelFutureProgressListener
class ChannelFutureProgressListener : public virtual ChannelFutureListener {
public:
    virtual void operationProgressed(ChannelFuture* future, int64_t amount, int64_t current, int64_t total) = 0;
};

// org.jboss.netty.channel.DefaultChannelFuture
class DefaultChannelFuture : public virtual ChannelFuture {
public:
    DefaultChannelFuture(Channel* channel, bool cancellable);
    Channel* getChannel() override { return channel_; }
    bool isDone() override;
    bool isCancelled() override;
    bool isSuccess() override;
    ::jlang::Throwable* getCause() override;
    bool cancel() override;
    bool setSuccess() override;
    bool setFailure(::jlang::Throwable* cause) override;
    bool setProgress(int64_t amount, int64_t current, int64_t total) override;
    void addListener(ChannelFutureListener* listener) override;
    void removeListener(ChannelFutureListener* listener) override;
    ChannelFuture* await() override;
    ChannelFuture* awaitUninterruptibly() override;
    bool await(int64_t timeout, ::jlang::TimeUnit unit) override;
    bool await(int64_t timeoutMillis) override;
    bool awaitUninterruptibly(int64_t timeout, ::jlang::TimeUnit unit) override;
    bool awaitUninterruptibly(int64_t timeoutMillis) override;
    // await*() in an I/O thread throws IllegalStateException (dead lock checker), as in Netty.
    static bool isUseDeadLockChecker();
    static void setUseDeadLockChecker(bool useDeadLockChecker);

private:
    bool await0(int64_t timeoutNanos, bool interruptable);
    void notifyListeners();
    void notifyListener(ChannelFutureListener* l);
    void checkDeadLock();

    Channel* channel_;
    bool cancellable_;
    ChannelFutureListener* firstListener_ = nullptr;
    std::vector<ChannelFutureListener*> otherListeners_;
    std::vector<ChannelFutureProgressListener*> progressListeners_;
    bool done_ = false;
    bool cancelled_ = false;
    ::jlang::Throwable* cause_ = nullptr;
    int32_t waiters_ = 0;
};

// org.jboss.netty.channel.CompleteChannelFuture / SucceededChannelFuture / FailedChannelFuture
class CompleteChannelFuture : public virtual ChannelFuture {
public:
    explicit CompleteChannelFuture(Channel* channel);
    Channel* getChannel() override { return channel_; }
    bool isDone() override { return true; }
    bool isCancelled() override { return false; }
    bool cancel() override { return false; }
    bool setSuccess() override { return false; }
    bool setFailure(::jlang::Throwable*) override { return false; }
    bool setProgress(int64_t, int64_t, int64_t) override { return false; }
    void addListener(ChannelFutureListener* listener) override;
    void removeListener(ChannelFutureListener*) override {}
    ChannelFuture* await() override;
    ChannelFuture* awaitUninterruptibly() override { return this; }
    bool await(int64_t timeout, ::jlang::TimeUnit unit) override;
    bool await(int64_t timeoutMillis) override;
    bool awaitUninterruptibly(int64_t, ::jlang::TimeUnit) override { return true; }
    bool awaitUninterruptibly(int64_t) override { return true; }

private:
    Channel* channel_;
};
class SucceededChannelFuture : public CompleteChannelFuture {
public:
    explicit SucceededChannelFuture(Channel* channel) : CompleteChannelFuture(channel) {}
    bool isSuccess() override { return true; }
    ::jlang::Throwable* getCause() override { return nullptr; }
};
class FailedChannelFuture : public CompleteChannelFuture {
public:
    FailedChannelFuture(Channel* channel, ::jlang::Throwable* cause);
    bool isSuccess() override { return false; }
    ::jlang::Throwable* getCause() override { return cause_; }

private:
    ::jlang::Throwable* cause_;
};

// =======================================================================================
// Channels, configuration, factories
// =======================================================================================

// org.jboss.netty.channel.ChannelConfig (+ the socket and NIO options of
// DefaultSocketChannelConfig / DefaultNioSocketChannelConfig / DefaultServerSocketChannelConfig).
// setOption() understands the keys Netty 3.2 does for the channel's kind and ignores (returns
// false for) unknown keys.
class ChannelConfig : public virtual ::jlang::Object {
public:
    // Applies every option (unknown keys are ignored).
    void setOptions(const std::map<::jlang::String, ::jlang::Object*>& options);
    // Values: jlang::Boolean* / jlang::Number* / ChannelBufferFactory* / ChannelPipelineFactory*.
    virtual bool setOption(const ::jlang::String& key, ::jlang::Object* value);
    ChannelBufferFactory* getBufferFactory() { return bufferFactory_.load(); }
    void setBufferFactory(ChannelBufferFactory* bufferFactory);
    ChannelPipelineFactory* getPipelineFactory() { return pipelineFactory_.load(); }
    void setPipelineFactory(ChannelPipelineFactory* pipelineFactory);
    int32_t getConnectTimeoutMillis() { return connectTimeoutMillis_; }
    void setConnectTimeoutMillis(int32_t connectTimeoutMillis);

protected:
    ChannelConfig();

private:
    std::atomic<ChannelBufferFactory*> bufferFactory_{nullptr};
    std::atomic<ChannelPipelineFactory*> pipelineFactory_{nullptr};
    std::atomic<int32_t> connectTimeoutMillis_{10000};
};

// Accepted socket channel configuration (org.jboss.netty.channel.socket.nio.NioSocketChannelConfig).
class SocketChannelConfig : public ChannelConfig {
public:
    explicit SocketChannelConfig(::jlang::SocketChannel* socket) : socket_(socket) {}
    bool setOption(const ::jlang::String& key, ::jlang::Object* value) override;
    bool isTcpNoDelay();
    void setTcpNoDelay(bool tcpNoDelay);
    bool isKeepAlive();
    void setKeepAlive(bool keepAlive);
    bool isReuseAddress();
    void setReuseAddress(bool reuseAddress);
    int32_t getSoLinger();
    void setSoLinger(int32_t soLinger);
    int32_t getTrafficClass();
    void setTrafficClass(int32_t trafficClass);
    int32_t getReceiveBufferSize();
    void setReceiveBufferSize(int32_t receiveBufferSize);
    int32_t getSendBufferSize();
    void setSendBufferSize(int32_t sendBufferSize);
    int32_t getWriteBufferHighWaterMark() { return writeBufferHighWaterMark_; }
    void setWriteBufferHighWaterMark(int32_t writeBufferHighWaterMark);
    int32_t getWriteBufferLowWaterMark() { return writeBufferLowWaterMark_; }
    void setWriteBufferLowWaterMark(int32_t writeBufferLowWaterMark);
    int32_t getWriteSpinCount() { return writeSpinCount_; }
    void setWriteSpinCount(int32_t writeSpinCount);

private:
    ::jlang::SocketChannel* socket_;
    std::atomic<int32_t> writeBufferHighWaterMark_{64 * 1024};
    std::atomic<int32_t> writeBufferLowWaterMark_{32 * 1024};
    std::atomic<int32_t> writeSpinCount_{16};
};

// Server socket channel configuration (org.jboss.netty.channel.socket.ServerSocketChannelConfig).
class ServerSocketChannelConfig : public ChannelConfig {
public:
    explicit ServerSocketChannelConfig(::jlang::ServerSocketChannel* socket) : socket_(socket) {}
    bool setOption(const ::jlang::String& key, ::jlang::Object* value) override;
    int32_t getBacklog() { return backlog_; }
    void setBacklog(int32_t backlog);
    bool isReuseAddress();
    void setReuseAddress(bool reuseAddress);
    int32_t getReceiveBufferSize();
    void setReceiveBufferSize(int32_t receiveBufferSize);

private:
    ::jlang::ServerSocketChannel* socket_;
    std::atomic<int32_t> backlog_{0};
};

// org.jboss.netty.channel.Channel. Identity equality/hash (like AbstractChannel).
class Channel : public virtual ::jlang::Object {
public:
    static constexpr int32_t OP_NONE = 0;
    static constexpr int32_t OP_READ = 1;
    static constexpr int32_t OP_WRITE = 4;
    static constexpr int32_t OP_READ_WRITE = OP_READ | OP_WRITE;

    // Unique id (identity hash based, like Netty).
    virtual int32_t getId() = 0;
    virtual ChannelFactory* getFactory() = 0;
    virtual Channel* getParent() = 0;
    virtual ChannelConfig* getConfig() = 0;
    virtual ChannelPipeline* getPipeline() = 0;
    virtual bool isOpen() = 0;
    virtual bool isBound() = 0;
    virtual bool isConnected() = 0;
    virtual ::jlang::InetSocketAddress* getLocalAddress() = 0;
    virtual ::jlang::InetSocketAddress* getRemoteAddress() = 0;
    // Thread-safe; the message goes through the downstream handlers in the calling thread.
    virtual ChannelFuture* write(::jlang::Object* message) = 0;
    virtual ChannelFuture* write(::jlang::Object* message, ::jlang::InetSocketAddress* remoteAddress) = 0;
    virtual ChannelFuture* bind(::jlang::InetSocketAddress* localAddress) = 0;
    virtual ChannelFuture* connect(::jlang::InetSocketAddress* remoteAddress) = 0;
    virtual ChannelFuture* disconnect() = 0;
    virtual ChannelFuture* unbind() = 0;
    // Closes the channel; returns getCloseFuture().
    virtual ChannelFuture* close() = 0;
    virtual ChannelFuture* getCloseFuture() = 0;
    virtual int32_t getInterestOps() = 0;
    virtual bool isReadable() = 0;
    virtual bool isWritable() = 0;
    virtual ChannelFuture* setInterestOps(int32_t interestOps) = 0;
    virtual ChannelFuture* setReadable(bool readable) = 0;
    virtual int32_t compareTo(Channel* o) = 0;
};

// Marker: org.jboss.netty.channel.ServerChannel
class ServerChannel : public virtual Channel {};

// org.jboss.netty.channel.AbstractChannel
class AbstractChannel : public virtual Channel {
public:
    int32_t getId() override { return id_; }
    ChannelFactory* getFactory() override { return factory_; }
    Channel* getParent() override { return parent_; }
    ChannelPipeline* getPipeline() override { return pipeline_; }
    bool isOpen() override;
    ChannelFuture* write(::jlang::Object* message) override;
    ChannelFuture* write(::jlang::Object* message, ::jlang::InetSocketAddress* remoteAddress) override;
    ChannelFuture* bind(::jlang::InetSocketAddress* localAddress) override;
    ChannelFuture* connect(::jlang::InetSocketAddress* remoteAddress) override;
    ChannelFuture* disconnect() override;
    ChannelFuture* unbind() override;
    ChannelFuture* close() override;
    ChannelFuture* getCloseFuture() override;
    int32_t getInterestOps() override { return interestOps_.load(); }
    bool isReadable() override { return (getInterestOps() & OP_READ) != 0; }
    bool isWritable() override { return (getInterestOps() & OP_WRITE) == 0; }
    ChannelFuture* setInterestOps(int32_t interestOps) override;
    ChannelFuture* setReadable(bool readable) override;
    int32_t compareTo(Channel* o) override;
    int32_t hashCode() final { return identityHashCode(); }
    bool equals(::jlang::Object* o) final { return o == static_cast<::jlang::Object*>(this); }
    // "[id: 0x0123abcd, /127.0.0.1:50000 => /127.0.0.1:10241]"
    ::jlang::String toString() override;

    // Internal
    ChannelFuture* getSucceededFuture();
    // Marks the channel closed (completes the close future); false if it already was.
    virtual bool setClosed();
    void setInterestOpsNow(int32_t interestOps) { interestOps_.store(interestOps); }

protected:
    AbstractChannel(Channel* parent, ChannelFactory* factory, ChannelPipeline* pipeline, ChannelSink* sink);

private:
    int32_t id_;
    Channel* parent_;
    ChannelFactory* factory_;
    ChannelPipeline* pipeline_;
    ChannelFuture* succeededFuture_;
    DefaultChannelFuture* closeFuture_;
    std::atomic<int32_t> interestOps_{OP_READ};
    ::jlang::String strVal_;
    std::mutex strLock_;
};

// org.jboss.netty.channel.ChannelFactory / ServerChannelFactory
class ChannelFactory : public virtual ::jlang::Object {
public:
    virtual Channel* newChannel(ChannelPipeline* pipeline) = 0;
    // Stops the I/O threads and shuts the executors down (ExecutorUtil.terminate): close all
    // channels first; like Netty it waits until the I/O threads have no channel left.
    virtual void releaseExternalResources() = 0;
};
class ServerChannelFactory : public virtual ChannelFactory {};

namespace detail {
class NioServerSocketPipelineSink;
}

// org.jboss.netty.channel.socket.nio.NioServerSocketChannelFactory
class NioServerSocketChannelFactory : public virtual ServerChannelFactory {
public:
    // workerCount defaults to 2 * availableProcessors (SelectorUtil.DEFAULT_IO_THREADS).
    NioServerSocketChannelFactory(::jlang::Executor* bossExecutor, ::jlang::Executor* workerExecutor);
    NioServerSocketChannelFactory(::jlang::Executor* bossExecutor, ::jlang::Executor* workerExecutor,
                                  int32_t workerCount);
    Channel* newChannel(ChannelPipeline* pipeline) override;
    void releaseExternalResources() override;

    ::jlang::Executor* bossExecutor() { return bossExecutor_; }

private:
    ::jlang::Executor* bossExecutor_;
    ::jlang::Executor* workerExecutor_;
    detail::NioServerSocketPipelineSink* sink_;
};

// org.jboss.netty.channel.Channels
class Channels final {
public:
    Channels() = delete;
    static ChannelPipeline* pipeline();
    static ChannelPipeline* pipeline(::jlang::Array<ChannelHandler*>* handlers);  // names "0", "1", ...
    template<class... H>
        requires(sizeof...(H) > 0 && (std::is_convertible_v<H, ChannelHandler*> && ...))
    static ChannelPipeline* pipeline(H... handlers) {
        return pipelineOf({static_cast<ChannelHandler*>(handlers)...});
    }
    static ChannelPipeline* pipeline(ChannelPipeline* pipeline);  // copy of the handler list
    static ChannelPipelineFactory* pipelineFactory(ChannelPipeline* pipeline);

    static ChannelFuture* future(Channel* channel);
    static ChannelFuture* future(Channel* channel, bool cancellable);
    static ChannelFuture* succeededFuture(Channel* channel);
    static ChannelFuture* failedFuture(Channel* channel, ::jlang::Throwable* cause);

    static void fireChannelOpen(Channel* channel);
    static void fireChannelOpen(ChannelHandlerContext* ctx);
    static void fireChannelBound(Channel* channel, ::jlang::InetSocketAddress* localAddress);
    static void fireChannelBound(ChannelHandlerContext* ctx, ::jlang::InetSocketAddress* localAddress);
    static void fireChannelConnected(Channel* channel, ::jlang::InetSocketAddress* remoteAddress);
    static void fireChannelConnected(ChannelHandlerContext* ctx, ::jlang::InetSocketAddress* remoteAddress);
    static void fireMessageReceived(Channel* channel, ::jlang::Object* message);
    static void fireMessageReceived(Channel* channel, ::jlang::Object* message,
                                    ::jlang::InetSocketAddress* remoteAddress);
    static void fireMessageReceived(ChannelHandlerContext* ctx, ::jlang::Object* message);
    static void fireMessageReceived(ChannelHandlerContext* ctx, ::jlang::Object* message,
                                    ::jlang::InetSocketAddress* remoteAddress);
    static void fireWriteComplete(Channel* channel, int64_t amount);
    static void fireWriteComplete(ChannelHandlerContext* ctx, int64_t amount);
    static void fireChannelInterestChanged(Channel* channel);
    static void fireChannelInterestChanged(ChannelHandlerContext* ctx);
    static void fireChannelDisconnected(Channel* channel);
    static void fireChannelDisconnected(ChannelHandlerContext* ctx);
    static void fireChannelUnbound(Channel* channel);
    static void fireChannelUnbound(ChannelHandlerContext* ctx);
    static void fireChannelClosed(Channel* channel);
    static void fireChannelClosed(ChannelHandlerContext* ctx);
    static void fireExceptionCaught(Channel* channel, ::jlang::Throwable* cause);
    static void fireExceptionCaught(ChannelHandlerContext* ctx, ::jlang::Throwable* cause);

    static ChannelFuture* bind(Channel* channel, ::jlang::InetSocketAddress* localAddress);
    static void bind(ChannelHandlerContext* ctx, ChannelFuture* future, ::jlang::InetSocketAddress* localAddress);
    static ChannelFuture* unbind(Channel* channel);
    static void unbind(ChannelHandlerContext* ctx, ChannelFuture* future);
    static ChannelFuture* connect(Channel* channel, ::jlang::InetSocketAddress* remoteAddress);
    static void connect(ChannelHandlerContext* ctx, ChannelFuture* future, ::jlang::InetSocketAddress* remoteAddress);
    static ChannelFuture* write(Channel* channel, ::jlang::Object* message);
    static ChannelFuture* write(Channel* channel, ::jlang::Object* message, ::jlang::InetSocketAddress* remoteAddress);
    static void write(ChannelHandlerContext* ctx, ChannelFuture* future, ::jlang::Object* message);
    static void write(ChannelHandlerContext* ctx, ChannelFuture* future, ::jlang::Object* message,
                      ::jlang::InetSocketAddress* remoteAddress);
    static ChannelFuture* setInterestOps(Channel* channel, int32_t interestOps);
    static void setInterestOps(ChannelHandlerContext* ctx, ChannelFuture* future, int32_t interestOps);
    static ChannelFuture* disconnect(Channel* channel);
    static void disconnect(ChannelHandlerContext* ctx, ChannelFuture* future);
    static ChannelFuture* close(Channel* channel);
    static void close(ChannelHandlerContext* ctx, ChannelFuture* future);

private:
    static ChannelPipeline* pipelineOf(std::initializer_list<ChannelHandler*> handlers);
};

// =======================================================================================
// Groups
// =======================================================================================

class ChannelGroupFutureListener : public virtual ::jlang::Object {
public:
    virtual void operationComplete(ChannelGroupFuture* future) = 0;
    template<class F> static ChannelGroupFutureListener* of(F f);
};

// org.jboss.netty.channel.group.ChannelGroupFuture (DefaultChannelGroupFuture): done when every
// channel future is done.
class ChannelGroupFuture : public virtual ::jlang::Object {
public:
    ChannelGroupFuture(ChannelGroup* group, const std::vector<ChannelFuture*>& futures);
    ChannelGroup* getGroup() { return group_; }
    ChannelFuture* find(int32_t channelId);
    ChannelFuture* find(Channel* channel);
    // The futures in order (Java iterator()).
    std::vector<ChannelFuture*> getFutures() { return futures_; }
    bool isDone();
    bool isCompleteSuccess();
    bool isPartialSuccess();
    bool isPartialFailure();
    bool isCompleteFailure();
    void addListener(ChannelGroupFutureListener* listener);
    void removeListener(ChannelGroupFutureListener* listener);
    ChannelGroupFuture* await();
    ChannelGroupFuture* awaitUninterruptibly();
    bool await(int64_t timeout, ::jlang::TimeUnit unit);
    bool await(int64_t timeoutMillis);
    bool awaitUninterruptibly(int64_t timeout, ::jlang::TimeUnit unit);
    bool awaitUninterruptibly(int64_t timeoutMillis);

    void childDone(bool success);  // internal

private:
    bool await0(int64_t timeoutNanos, bool interruptable);
    void setDone();
    void notifyListener(ChannelGroupFutureListener* l);

    ChannelGroup* group_;
    std::vector<ChannelFuture*> futures_;
    ChannelGroupFutureListener* firstListener_ = nullptr;
    std::vector<ChannelGroupFutureListener*> otherListeners_;
    bool done_ = false;
    int32_t successCount_ = 0;
    int32_t failureCount_ = 0;
    int32_t waiters_ = 0;
};

// org.jboss.netty.channel.group.ChannelGroup / DefaultChannelGroup: a thread-safe set of open
// channels; a channel is removed automatically when it is closed.
class ChannelGroup : public virtual ::jlang::Object {
public:
    ChannelGroup();                                   // name "group-0x<n>"
    explicit ChannelGroup(const ::jlang::String& name);
    ::jlang::String getName() { return name_; }
    bool isEmpty();
    int32_t size();
    Channel* find(int32_t id);
    bool contains(Channel* channel);
    bool add(Channel* channel);
    bool remove(Channel* channel);
    void clear();
    // Server channels first, then the others.
    std::vector<Channel*> toVector();
    // Closes every channel; server channels synchronously (awaitUninterruptibly), then the others.
    ChannelGroupFuture* close();
    ChannelGroupFuture* disconnect();
    ChannelGroupFuture* unbind();
    ChannelGroupFuture* setInterestOps(int32_t interestOps);
    ChannelGroupFuture* setReadable(bool readable);
    // Writes the message to every channel (a ChannelBuffer is duplicated per channel).
    ChannelGroupFuture* write(::jlang::Object* message);
    int32_t compareTo(ChannelGroup* o);
    int32_t hashCode() override { return identityHashCode(); }
    bool equals(::jlang::Object* o) override { return o == static_cast<::jlang::Object*>(this); }
    // "DefaultChannelGroup(name: x, size: 2)"
    ::jlang::String toString() override;

private:
    template<class Op> ChannelGroupFuture* forAll(Op op);
    ::jlang::String name_;
    std::mutex lock_;
    std::map<int32_t, Channel*> serverChannels_;
    std::map<int32_t, Channel*> nonServerChannels_;
    ChannelFutureListener* remover_;
};
using DefaultChannelGroup = ChannelGroup;

// =======================================================================================
// Bootstrap
// =======================================================================================

// org.jboss.netty.bootstrap.Bootstrap
class Bootstrap : public virtual ::jlang::Object {
public:
    ChannelFactory* getFactory();
    virtual void setFactory(ChannelFactory* factory);
    ChannelPipeline* getPipeline();
    void setPipeline(ChannelPipeline* pipeline);
    ChannelPipelineFactory* getPipelineFactory();
    void setPipelineFactory(ChannelPipelineFactory* pipelineFactory);
    // Sorted copy of the options (Java getOptions() returns a TreeMap).
    std::map<::jlang::String, ::jlang::Object*> getOptions();
    void setOptions(const std::map<::jlang::String, ::jlang::Object*>& options);
    ::jlang::Object* getOption(const ::jlang::String& key);
    // "child.xxx" options go to accepted channels, the others to the server channel. Values:
    // jlang::box(...)ed Boolean/Integer/..., ChannelBufferFactory*, ... A null value removes the key.
    void setOption(const ::jlang::String& key, ::jlang::Object* value);
    void setOption(const ::jlang::String& key, bool value);
    void setOption(const ::jlang::String& key, int32_t value);
    void setOption(const ::jlang::String& key, int64_t value);
    void releaseExternalResources();

protected:
    Bootstrap();
    explicit Bootstrap(ChannelFactory* channelFactory);

private:
    std::mutex lock_;
    ChannelFactory* factory_ = nullptr;
    ChannelPipeline* pipeline_ = nullptr;
    ChannelPipelineFactory* pipelineFactory_ = nullptr;
    std::map<::jlang::String, ::jlang::Object*> options_;
};

// org.jboss.netty.bootstrap.ServerBootstrap
class ServerBootstrap : public Bootstrap {
public:
    ServerBootstrap() {}
    explicit ServerBootstrap(ChannelFactory* channelFactory);
    void setFactory(ChannelFactory* factory) override;
    ChannelHandler* getParentHandler() { return parentHandler_.load(); }
    void setParentHandler(ChannelHandler* parentHandler) { parentHandler_.store(parentHandler); }
    // Binds using the "localAddress" option.
    Channel* bind();
    // Creates a server channel bound to localAddress; ChannelException("Failed to bind to: ...")
    // on failure.
    Channel* bind(::jlang::InetSocketAddress* localAddress);

private:
    std::atomic<ChannelHandler*> parentHandler_{nullptr};
};

// =======================================================================================
// Codecs
// =======================================================================================

// org.jboss.netty.handler.codec.frame.FrameDecoder: cumulates received ChannelBuffers in a
// dynamic buffer (from the channel's bufferFactory) and calls decode() until it returns null.
// On channelDisconnected/channelClosed the remaining complete frames are decoded, decodeLast()
// is called, and the event is forwarded.
class FrameDecoder : public SimpleChannelUpstreamHandler {
public:
    void messageReceived(ChannelHandlerContext* ctx, MessageEvent* e) override;
    void channelDisconnected(ChannelHandlerContext* ctx, ChannelStateEvent* e) override;
    void channelClosed(ChannelHandlerContext* ctx, ChannelStateEvent* e) override;
    void exceptionCaught(ChannelHandlerContext* ctx, ExceptionEvent* e) override;
    // Returns a frame, or nullptr if more data is needed.
    virtual ::jlang::Object* decode(ChannelHandlerContext* ctx, Channel* channel, ChannelBuffer* buffer) = 0;
    virtual ::jlang::Object* decodeLast(ChannelHandlerContext* ctx, Channel* channel, ChannelBuffer* buffer);

protected:
    FrameDecoder() : FrameDecoder(false) {}
    explicit FrameDecoder(bool unfold) : unfold_(unfold) {}

private:
    void callDecode(ChannelHandlerContext* context, Channel* channel, ChannelBuffer* cumulation,
                    ::jlang::InetSocketAddress* remoteAddress);
    void unfoldAndFireMessageReceived(ChannelHandlerContext* context, ::jlang::InetSocketAddress* remoteAddress,
                                      ::jlang::Object* result);
    void cleanup(ChannelHandlerContext* ctx, ChannelStateEvent* e);
    ChannelBuffer* cumulation(ChannelHandlerContext* ctx);

    bool unfold_;
    ChannelBuffer* cumulation_ = nullptr;
    // messageReceived (I/O thread) and cleanup (whichever thread closes the channel) never
    // decode concurrently; a cleanup that finds a decode in progress leaves the cumulation to it.
    std::recursive_mutex decodeLock_;
};

// org.jboss.netty.handler.codec.frame.LengthFieldBasedFrameDecoder (3.2.0.BETA1 semantics:
// CorruptedFrameException for a negative/too small length after skipping the length field,
// too long frames are discarded and TooLongFrameException is thrown once they are skipped).
class LengthFieldBasedFrameDecoder : public FrameDecoder {
public:
    LengthFieldBasedFrameDecoder(int32_t maxFrameLength, int32_t lengthFieldOffset, int32_t lengthFieldLength);
    LengthFieldBasedFrameDecoder(int32_t maxFrameLength, int32_t lengthFieldOffset, int32_t lengthFieldLength,
                                 int32_t lengthAdjustment, int32_t initialBytesToStrip);
    ::jlang::Object* decode(ChannelHandlerContext* ctx, Channel* channel, ChannelBuffer* buffer) override;

private:
    int32_t maxFrameLength_;
    int32_t lengthFieldOffset_;
    int32_t lengthFieldLength_;
    int32_t lengthFieldEndOffset_;
    int32_t lengthAdjustment_;
    int32_t initialBytesToStrip_;
    bool discardingTooLongFrame_ = false;
    int64_t tooLongFrameLength_ = 0;
    int64_t bytesToDiscard_ = 0;
};

// org.jboss.netty.handler.codec.oneone.OneToOneDecoder: transforms MessageEvents; returning the
// same object forwards the original event, nullptr drops it, anything else fires a new event.
class OneToOneDecoder : public virtual ChannelUpstreamHandler {
public:
    void handleUpstream(ChannelHandlerContext* ctx, ChannelEvent* evt) override;
    virtual ::jlang::Object* decode(ChannelHandlerContext* ctx, Channel* channel, ::jlang::Object* msg) = 0;

protected:
    OneToOneDecoder() {}
};

// org.jboss.netty.handler.codec.oneone.OneToOneEncoder (downstream counterpart).
class OneToOneEncoder : public virtual ChannelDownstreamHandler {
public:
    void handleDownstream(ChannelHandlerContext* ctx, ChannelEvent* evt) override;
    virtual ::jlang::Object* encode(ChannelHandlerContext* ctx, Channel* channel, ::jlang::Object* msg) = 0;

protected:
    OneToOneEncoder() {}
};

// =======================================================================================
// Execution
// =======================================================================================

// org.jboss.netty.handler.execution.ChannelEventRunnable
class ChannelEventRunnable : public virtual ::jlang::Runnable {
public:
    ChannelEventRunnable(ChannelHandlerContext* ctx, ChannelEvent* e) : ctx_(ctx), e_(e) {}
    ChannelHandlerContext* getContext() { return ctx_; }
    ChannelEvent* getEvent() { return e_; }
    void run() override;
    int32_t estimatedSize = 0;

private:
    ChannelHandlerContext* ctx_;
    ChannelEvent* e_;
};

// org.jboss.netty.handler.execution.ExecutionHandler: hands every upstream event to the
// executor (ctx->sendUpstream runs in a pool thread); downstream events pass through.
class ExecutionHandler : public virtual ChannelUpstreamHandler, public virtual ChannelDownstreamHandler {
public:
    explicit ExecutionHandler(::jlang::Executor* executor);
    ::jlang::Executor* getExecutor() { return executor_; }
    // Shuts the executor down (ExecutorUtil.terminate).
    void releaseExternalResources();
    void handleUpstream(ChannelHandlerContext* context, ChannelEvent* e) override;
    void handleDownstream(ChannelHandlerContext* ctx, ChannelEvent* e) override;

private:
    ::jlang::Executor* executor_;
};

// org.jboss.netty.util.ObjectSizeEstimator / DefaultObjectSizeEstimator (approximation of the
// reflective Java estimator: 8 + shallow size + message payload, 8-byte aligned).
class ObjectSizeEstimator : public virtual ::jlang::Object {
public:
    virtual int32_t estimateSize(::jlang::Object* o) = 0;
};
class DefaultObjectSizeEstimator : public virtual ObjectSizeEstimator {
public:
    int32_t estimateSize(::jlang::Object* o) override;
};

namespace detail {
class PoolWorker;
}

// org.jboss.netty.handler.execution.MemoryAwareThreadPoolExecutor: a fixed-size
// ThreadPoolExecutor (core = max = corePoolSize, unbounded FIFO queue, idle threads, core
// included, exit after keepAliveTime) with memory accounting of queued events:
//   * per channel: when the estimated size of a channel's queued events reaches
//     maxChannelMemorySize, reads of the channel are suspended (setReadable(false)) until it
//     drops below again;
//   * total: a thread submitting an event (the I/O thread) waits while the total reaches
//     maxTotalMemorySize (0 = no limit).
// Differences from 3.2.0.BETA1 (whose accounting can lose wake-ups and stall a channel or an I/O
// thread for good): the total limit is a condition-based limiter (like later Netty 3.x) and
// never blocks a pool thread of this executor; the per-channel decision is made atomically with
// the counter update.
class MemoryAwareThreadPoolExecutor : public virtual ::jlang::Executor {
public:
    MemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize, int64_t maxTotalMemorySize);
    MemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize, int64_t maxTotalMemorySize,
                                  int64_t keepAliveTime, ::jlang::TimeUnit unit);
    MemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize, int64_t maxTotalMemorySize,
                                  int64_t keepAliveTime, ::jlang::TimeUnit unit, ::jlang::ThreadFactory* threadFactory);
    MemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize, int64_t maxTotalMemorySize,
                                  int64_t keepAliveTime, ::jlang::TimeUnit unit,
                                  ObjectSizeEstimator* objectSizeEstimator, ::jlang::ThreadFactory* threadFactory);

    void execute(::jlang::Runnable* command) override;
    ObjectSizeEstimator* getObjectSizeEstimator();
    void setObjectSizeEstimator(ObjectSizeEstimator* objectSizeEstimator);
    int64_t getMaxChannelMemorySize();
    void setMaxChannelMemorySize(int64_t maxChannelMemorySize);
    int64_t getMaxTotalMemorySize();
    void setMaxTotalMemorySize(int64_t maxTotalMemorySize);

    // ---- java.util.concurrent.ThreadPoolExecutor subset
    void shutdown();
    std::vector<::jlang::Runnable*> shutdownNow();
    bool isShutdown();
    bool isTerminated();
    bool awaitTermination(int64_t timeout, ::jlang::TimeUnit unit);
    int32_t getCorePoolSize();
    int32_t getMaximumPoolSize();
    int32_t getPoolSize();
    int32_t getActiveCount();
    int64_t getTaskCount();
    int64_t getCompletedTaskCount();
    void allowCoreThreadTimeOut(bool value);
    bool remove(::jlang::Runnable* task);

protected:
    virtual void doExecute(::jlang::Runnable* task);
    void doUnorderedExecute(::jlang::Runnable* task);
    virtual void beforeExecute(::jlang::Thread* t, ::jlang::Runnable* r);
    virtual void afterExecute(::jlang::Runnable*, ::jlang::Throwable*) {}
    virtual bool shouldCount(::jlang::Runnable* task);
    void increaseCounter(::jlang::Runnable* task);
    void decreaseCounter(::jlang::Runnable* task);

private:
    friend class detail::PoolWorker;
    struct Settings {
        ObjectSizeEstimator* objectSizeEstimator;
        int64_t maxChannelMemorySize;
        int64_t maxTotalMemorySize;
    };
    struct ChannelCounter {
        std::mutex lock;  // serializes the counter update with the suspend/resume decision
        int64_t value = 0;
    };
    ChannelCounter* getChannelCounter(Channel* channel);
    ::jlang::Runnable* getTask(detail::PoolWorker* w);   // nullptr: the worker must exit
    void runWorker(detail::PoolWorker* w);
    void startWorker(detail::PoolWorker* w);
    void rejected(::jlang::Runnable* task);

    std::atomic<Settings*> settings_{nullptr};
    std::mutex countersLock_;
    std::unordered_map<Channel*, ChannelCounter*> channelCounters_;
    // Total limiter: a submitting thread (not a pool thread) waits while the total is at or
    // above maxTotalMemorySize.
    std::mutex totalLock_;
    std::condition_variable totalCond_;
    int64_t totalCounter_ = 0;
    int32_t totalWaiters_ = 0;

    // Pool state (guarded by poolLock_).
    std::mutex poolLock_;
    std::condition_variable poolCond_;        // tasks / shutdown for idle workers
    std::condition_variable terminationCond_;
    std::deque<::jlang::Runnable*> queue_;
    std::vector<detail::PoolWorker*> workers_;
    int32_t corePoolSize_;
    int64_t keepAliveNanos_;
    bool allowCoreThreadTimeOut_ = false;
    ::jlang::ThreadFactory* threadFactory_;
    int32_t runState_ = 0;                    // 0 RUNNING, 1 SHUTDOWN, 2 STOP, 3 TERMINATED
    int32_t activeCount_ = 0;
    int64_t completedTaskCount_ = 0;
};

namespace detail {
class ChildExecutor;
}

// org.jboss.netty.handler.execution.OrderedMemoryAwareThreadPoolExecutor: events of the same
// channel run one at a time, in submission order, on the pool threads; different channels run
// in parallel. A channel's child executor is dropped once the channel is closed and its queue
// has drained (Netty 3.2 drops it when the close event is submitted, so an event fired after
// the close, e.g. exceptionCaught for a write to the closed channel, could run concurrently
// with the handler; here it is queued behind the running events).
class OrderedMemoryAwareThreadPoolExecutor : public MemoryAwareThreadPoolExecutor {
public:
    OrderedMemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize,
                                         int64_t maxTotalMemorySize);
    OrderedMemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize,
                                         int64_t maxTotalMemorySize, int64_t keepAliveTime, ::jlang::TimeUnit unit);
    OrderedMemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize,
                                         int64_t maxTotalMemorySize, int64_t keepAliveTime, ::jlang::TimeUnit unit,
                                         ::jlang::ThreadFactory* threadFactory);
    OrderedMemoryAwareThreadPoolExecutor(int32_t corePoolSize, int64_t maxChannelMemorySize,
                                         int64_t maxTotalMemorySize, int64_t keepAliveTime, ::jlang::TimeUnit unit,
                                         ObjectSizeEstimator* objectSizeEstimator,
                                         ::jlang::ThreadFactory* threadFactory);

protected:
    void doExecute(::jlang::Runnable* task) override;
    bool shouldCount(::jlang::Runnable* task) override;
    // The key of the child executor for an event (the channel).
    virtual ::jlang::Object* getChildExecutorKey(ChannelEvent* e);
    bool removeChildExecutor(::jlang::Object* key);

private:
    friend class detail::ChildExecutor;
    void removeIdleChildExecutor(::jlang::Object* key, detail::ChildExecutor* executor);
    std::mutex childLock_;
    std::unordered_map<::jlang::Object*, detail::ChildExecutor*> childExecutors_;
};

// =======================================================================================
// Lambda adapters (CONVENTIONS §3.3)
// =======================================================================================
namespace detail {
template<class F>
class ChannelFutureListenerLambda final : public virtual ChannelFutureListener {
public:
    explicit ChannelFutureListenerLambda(F f) : f_(std::move(f)) {}
    void operationComplete(ChannelFuture* future) override { f_(future); }

private:
    F f_;
};
template<class F>
class ChannelGroupFutureListenerLambda final : public virtual ChannelGroupFutureListener {
public:
    explicit ChannelGroupFutureListenerLambda(F f) : f_(std::move(f)) {}
    void operationComplete(ChannelGroupFuture* future) override { f_(future); }

private:
    F f_;
};
template<class F>
class ChannelPipelineFactoryLambda final : public virtual ChannelPipelineFactory {
public:
    explicit ChannelPipelineFactoryLambda(F f) : f_(std::move(f)) {}
    ChannelPipeline* getPipeline() override { return f_(); }

private:
    F f_;
};
}  // namespace detail

template<class F>
ChannelFutureListener* ChannelFutureListener::of(F f) {
    return new detail::ChannelFutureListenerLambda<F>(std::move(f));
}
template<class F>
ChannelGroupFutureListener* ChannelGroupFutureListener::of(F f) {
    return new detail::ChannelGroupFutureListenerLambda<F>(std::move(f));
}
template<class F>
ChannelPipelineFactory* ChannelPipelineFactory::of(F f) {
    return new detail::ChannelPipelineFactoryLambda<F>(std::move(f));
}

}  // namespace jlang::netty
