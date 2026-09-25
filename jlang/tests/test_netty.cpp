// jlang/tests/test_netty.cpp - tests of <jlang/Netty.h>: ChannelBuffer semantics, the frame
// decoder on an in-memory pipeline, and loopback servers shaped like the chatserver pipeline
// (framedecoder -> packetdecoder -> packetencoder -> executor -> handler).
#include "jtest.h"

#include <jlang/Netty.h>
#include <jlang/Nio.h>
#include <jlang/Thread.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace jlang::netty;
using jlang::Array;
using jlang::ByteOrder;
using jlang::Object;
using jlang::String;

namespace {

Array<int8_t>* bytesOf(std::initializer_list<int> v) {
    auto* a = new Array<int8_t>(static_cast<int32_t>(v.size()));
    int32_t i = 0;
    for (int x : v) (*a)[i++] = static_cast<int8_t>(x);
    return a;
}

template<class F>
bool waitFor(F cond, int timeoutMs = 20000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!cond()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

std::vector<uint8_t> readable(ChannelBuffer* b) {
    std::vector<uint8_t> v;
    for (int32_t i = b->readerIndex(); i < b->writerIndex(); i++) v.push_back(static_cast<uint8_t>(b->getByte(i)));
    return v;
}

}  // namespace

// =======================================================================================
// ChannelBuffer
// =======================================================================================

JTEST(NettyBufferLittleEndian) {
    ChannelBuffer* b = ChannelBuffers::buffer(ByteOrder::LITTLE_ENDIAN, 64);
    JCHECK_EQ(b->capacity(), 64);
    JCHECK(b->order() == ByteOrder::LITTLE_ENDIAN);
    JCHECK(!b->readable());
    JCHECK_EQ(b->writableBytes(), 64);
    b->writeByte(0x81);
    b->writeShort(0x1234);
    b->writeInt(0x01020304);
    b->writeLong(INT64_C(0x0102030405060708));
    b->writeFloat(1.5f);
    b->writeDouble(-2.25);
    b->writeChar(u'A');
    b->writeMedium(0x800001);
    JCHECK_EQ(b->writerIndex(), 1 + 2 + 4 + 8 + 4 + 8 + 2 + 3);
    JCHECK_EQ(b->getByte(1), 0x34);  // little endian
    JCHECK_EQ(b->getByte(2), 0x12);
    JCHECK_EQ(b->getUnsignedShort(1), 0x1234);
    JCHECK_EQ(b->readByte(), static_cast<int8_t>(0x81));
    JCHECK_EQ(b->readShort(), 0x1234);
    JCHECK_EQ(b->readInt(), 0x01020304);
    JCHECK_EQ(b->readLong(), INT64_C(0x0102030405060708));
    JCHECK_EQ(b->readFloat(), 1.5f);
    JCHECK_EQ(b->readDouble(), -2.25);
    JCHECK_EQ(b->readChar(), u'A');
    JCHECK_EQ(b->readMedium(), static_cast<int32_t>(0xFF800001u));
    JCHECK(!b->readable());
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, b->readByte());
    JCHECK_EQ(b->readerIndex(), b->writerIndex());
}

JTEST(NettyBufferBigEndian) {
    ChannelBuffer* b = ChannelBuffers::buffer(16);
    JCHECK(b->order() == ByteOrder::BIG_ENDIAN);
    b->writeShort(0x1234);
    b->writeInt(-2);
    JCHECK_EQ(b->getByte(0), 0x12);
    JCHECK_EQ(b->getByte(1), 0x34);
    JCHECK_EQ(b->getUnsignedInt(2), INT64_C(0xFFFFFFFE));
    JCHECK_EQ(b->readUnsignedShort(), 0x1234);
    JCHECK_EQ(b->readInt(), -2);
    JCHECK_EQ(b->toString(), String("BigEndianHeapChannelBuffer(ridx=6, widx=6, cap=16)"));
}

JTEST(NettyBufferBounds) {
    ChannelBuffer* b = ChannelBuffers::buffer(ByteOrder::LITTLE_ENDIAN, 4);
    b->writeInt(7);
    // writeShort past the capacity: nothing written, index unchanged.
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, b->writeShort(1));
    JCHECK_EQ(b->writerIndex(), 4);
    // Netty's writeByte does setByte(writerIndex++, v): the index moves even when it throws.
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, b->writeByte(1));
    JCHECK_EQ(b->writerIndex(), 5);

    ChannelBuffer* r = ChannelBuffers::wrappedBuffer(ByteOrder::LITTLE_ENDIAN, bytesOf({1, 2, 3, 4}));
    auto* dst = new Array<int8_t>(10);
    // readBytes(byte[]) checks before copying.
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, r->readBytes(dst));
    JCHECK_EQ(r->readerIndex(), 0);
    JCHECK_EQ((*dst)[0], 0);
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, r->readLong());
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, r->skipBytes(5));
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, r->readerIndex(5));
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, r->getInt(1));
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, r->setShort(3, 0));

    // readBytes(0) is the shared big-endian EMPTY_BUFFER.
    ChannelBuffer* e = r->readBytes(0);
    JCHECK(e == ChannelBuffers::EMPTY_BUFFER);
    JCHECK(e->order() == ByteOrder::BIG_ENDIAN);
    JCHECK_EQ(e->capacity(), 0);
    ChannelBuffer* two = r->readBytes(2);
    JCHECK(two->order() == ByteOrder::LITTLE_ENDIAN);
    JCHECK_EQ(two->readShort(), 0x0201);
    JCHECK_EQ(r->readerIndex(), 2);
    auto* small = new Array<int8_t>(2);
    r->readBytes(small);
    JCHECK_EQ((*small)[1], 4);
    JCHECK(ChannelBuffers::buffer(ByteOrder::LITTLE_ENDIAN, 0) == ChannelBuffers::EMPTY_BUFFER);
}

JTEST(NettyBufferDynamicAndDiscard) {
    ChannelBuffer* d = ChannelBuffers::dynamicBuffer(HeapChannelBufferFactory::getInstance(ByteOrder::LITTLE_ENDIAN));
    JCHECK_EQ(d->capacity(), 256);
    JCHECK(d->order() == ByteOrder::LITTLE_ENDIAN);
    for (int i = 0; i < 1000; i++) d->writeByte(i);
    JCHECK_EQ(d->capacity(), 1024);
    JCHECK_EQ(d->readableBytes(), 1000);
    d->skipBytes(998);
    d->discardReadBytes();
    JCHECK_EQ(d->readerIndex(), 0);
    JCHECK_EQ(d->writerIndex(), 2);
    JCHECK_EQ(d->readUnsignedByte(), 998 & 0xFF);
    JCHECK_EQ(d->readUnsignedByte(), 999 & 0xFF);
    d->writeShort(0x0102);
    JCHECK_EQ(d->getByte(2), 0x02);
    ChannelBuffer* src = ChannelBuffers::wrappedBuffer(bytesOf({9, 8, 7}));
    d->writeBytes(src);
    JCHECK_EQ(src->readableBytes(), 0);
    JCHECK_EQ(d->readableBytes(), 3 + 2);
    ChannelBuffer* z = ChannelBuffers::dynamicBuffer(ByteOrder::BIG_ENDIAN, 0);
    z->writeZero(13);
    JCHECK_EQ(z->readableBytes(), 13);
    JCHECK(z->capacity() >= 13);
}

JTEST(NettyBufferViewsAndEquality) {
    ChannelBuffer* b = ChannelBuffers::buffer(ByteOrder::LITTLE_ENDIAN, 16);
    for (int i = 0; i < 8; i++) b->writeByte(i);
    ChannelBuffer* s = b->slice(2, 4);
    JCHECK_EQ(s->capacity(), 4);
    JCHECK_EQ(s->readableBytes(), 4);
    JCHECK_EQ(s->getByte(0), 2);
    s->setByte(0, 42);
    JCHECK_EQ(b->getByte(2), 42);  // shared content
    JCHECK_THROWS(jlang::IndexOutOfBoundsException, s->getByte(4));
    ChannelBuffer* c = b->copy();
    c->setByte(0, 99);
    JCHECK_EQ(b->getByte(0), 0);  // independent
    ChannelBuffer* d = b->duplicate();
    JCHECK_EQ(d->readableBytes(), 8);
    JCHECK(d->equals(b));
    JCHECK_EQ(d->hashCode(), b->hashCode());
    JCHECK(!c->equals(b));
    JCHECK(c->compareTo(b) > 0);
    ChannelBuffer* be = ChannelBuffers::copiedBuffer(ByteOrder::BIG_ENDIAN, b->array());
    be->writerIndex(8);
    JCHECK(be->equals(b));  // content equality ignores the byte order
    b->setShort(0, static_cast<int16_t>(0xFFFE));
    JCHECK_EQ(b->getUnsignedShort(0), 0xFFFE);
    JCHECK_EQ(b->getShort(0), -2);
    JCHECK_EQ(ChannelBuffers::hexDump(ChannelBuffers::wrappedBuffer(bytesOf({0, 0x7f, -1}))), String("007fff"));
    JCHECK_EQ(b->indexOf(0, 8, 5), 5);
    JCHECK_EQ(b->bytesBefore(static_cast<int8_t>(42)), 2);
}

// =======================================================================================
// In-memory pipeline: frame decoder semantics
// =======================================================================================
namespace {

class TestConfig final : public ChannelConfig {
public:
    TestConfig() { setBufferFactory(HeapChannelBufferFactory::getInstance(ByteOrder::LITTLE_ENDIAN)); }
};

class TestChannel final : public AbstractChannel {
public:
    TestChannel(ChannelPipeline* pipeline, ChannelSink* sink) : AbstractChannel(nullptr, nullptr, pipeline, sink) {}
    ChannelConfig* getConfig() override { return config_; }
    bool isBound() override { return isOpen(); }
    bool isConnected() override { return isOpen(); }
    jlang::InetSocketAddress* getLocalAddress() override { return nullptr; }
    jlang::InetSocketAddress* getRemoteAddress() override { return nullptr; }

private:
    ChannelConfig* config_ = new TestConfig();
};

// Collects downstream messages and executes close requests.
class TestSink final : public virtual ChannelSink {
public:
    void eventSunk(ChannelPipeline*, ChannelEvent* e) override {
        if (auto* me = dynamic_cast<MessageEvent*>(e)) {
            written.push_back(me->getMessage());
            me->getFuture()->setSuccess();
        } else if (auto* se = dynamic_cast<ChannelStateEvent*>(e)) {
            if (se->getState() == ChannelState::OPEN && !jlang::cast<jlang::Boolean>(se->getValue())->booleanValue()) {
                auto* ch = dynamic_cast<AbstractChannel*>(e->getChannel());
                if (ch->setClosed()) Channels::fireChannelClosed(ch);
            }
        }
    }
    std::vector<Object*> written;
};

class PacketFrameDecoder final : public LengthFieldBasedFrameDecoder {
public:
    PacketFrameDecoder() : LengthFieldBasedFrameDecoder(8192 * 2, 0, 2, -2, 2) {}
    Object* decode(ChannelHandlerContext* ctx, Channel* channel, ChannelBuffer* buffer) override {
        return LengthFieldBasedFrameDecoder::decode(ctx, channel, buffer);
    }
};

class PassThroughDecoder final : public OneToOneDecoder {
public:
    Object* decode(ChannelHandlerContext*, Channel*, Object* msg) override {
        ChannelBuffer* message = jlang::cast<ChannelBuffer>(msg);
        return message;
    }
};

class LengthEncoder final : public OneToOneEncoder {
public:
    Object* encode(ChannelHandlerContext*, Channel*, Object* msg) override {
        ChannelBuffer* message = jlang::cast<ChannelBuffer>(msg);
        message->setShort(0, static_cast<int16_t>(message->readableBytes()));
        return message;
    }
};

class RecordingHandler final : public SimpleChannelUpstreamHandler {
public:
    void messageReceived(ChannelHandlerContext* ctx, MessageEvent* e) override {
        SimpleChannelUpstreamHandler::messageReceived(ctx, e);
        auto* b = jlang::cast<ChannelBuffer>(e->getMessage());
        frames.push_back(readable(b));
        orders.push_back(b->order() == ByteOrder::LITTLE_ENDIAN);
    }
    void exceptionCaught(ChannelHandlerContext*, ExceptionEvent* e) override {
        exceptions.push_back(e->getCause()->getClass()->getSimpleName());
    }
    void channelClosed(ChannelHandlerContext* ctx, ChannelStateEvent* e) override {
        closed++;
        SimpleChannelUpstreamHandler::channelClosed(ctx, e);
    }
    std::vector<std::vector<uint8_t>> frames;
    std::vector<bool> orders;
    std::vector<String> exceptions;
    int closed = 0;
};

struct MemPipeline {
    ChannelPipeline* pipeline;
    TestSink* sink;
    TestChannel* channel;
    RecordingHandler* handler;
    MemPipeline() {
        pipeline = Channels::pipeline();
        pipeline->addLast("framedecoder", new PacketFrameDecoder());
        pipeline->addLast("packetdecoder", new PassThroughDecoder());
        pipeline->addLast("packetencoder", new LengthEncoder());
        handler = new RecordingHandler();
        pipeline->addLast("handler", handler);
        sink = new TestSink();
        channel = new TestChannel(pipeline, sink);
    }
    void feed(std::initializer_list<int> bytes) {
        Channels::fireMessageReceived(channel, ChannelBuffers::wrappedBuffer(ByteOrder::LITTLE_ENDIAN, bytesOf(bytes)));
    }
    void feed(const std::vector<uint8_t>& bytes) {
        auto* a = new Array<int8_t>(static_cast<int32_t>(bytes.size()));
        for (size_t i = 0; i < bytes.size(); i++) (*a)[static_cast<int32_t>(i)] = static_cast<int8_t>(bytes[i]);
        Channels::fireMessageReceived(channel, ChannelBuffers::wrappedBuffer(ByteOrder::LITTLE_ENDIAN, a));
    }
};

}  // namespace

JTEST(NettyFrameDecoderFragmentsAndCoalesced) {
    MemPipeline p;
    JCHECK_EQ(p.pipeline->getNames().size(), size_t{4});
    // Two complete frames and the first byte of a third in one read.
    p.feed({5, 0, 1, 2, 3, 4, 0, 10, 20, 6});
    JCHECK_EQ(p.handler->frames.size(), size_t{2});
    JCHECK(p.handler->frames[0] == (std::vector<uint8_t>{1, 2, 3}));
    JCHECK(p.handler->frames[1] == (std::vector<uint8_t>{10, 20}));
    // The third frame one byte at a time.
    p.feed({0});
    p.feed({7});
    p.feed({8});
    p.feed({9});
    JCHECK_EQ(p.handler->frames.size(), size_t{2});
    p.feed({11, 7, 0});  // completes the frame and starts a 7-byte frame
    JCHECK_EQ(p.handler->frames.size(), size_t{3});
    JCHECK(p.handler->frames[2] == (std::vector<uint8_t>{7, 8, 9, 11}));
    p.feed({1, 2, 3, 4, 5});
    JCHECK_EQ(p.handler->frames.size(), size_t{4});
    JCHECK(p.handler->frames[3] == (std::vector<uint8_t>{1, 2, 3, 4, 5}));
    for (bool le : p.handler->orders) JCHECK(le);  // frames come from the LE buffer factory
    // A frame whose length is only the length field: EMPTY_BUFFER (big endian).
    p.feed({2, 0});
    JCHECK_EQ(p.handler->frames.size(), size_t{5});
    JCHECK(p.handler->frames[4].empty());
    JCHECK(!p.handler->orders[4]);
    JCHECK(p.handler->exceptions.empty());
}

JTEST(NettyFrameDecoderCorruptAndTooLong) {
    MemPipeline p;
    // Length 1 < 2: CorruptedFrameException after skipping the length field.
    p.feed({1, 0, 3, 0, 9});
    JCHECK_EQ(p.handler->exceptions.size(), size_t{1});
    JCHECK_EQ(p.handler->exceptions[0], String("CorruptedFrameException"));
    // The rest of that read is lost (not cumulated), as in Netty.
    p.feed({3, 0, 42});
    JCHECK_EQ(p.handler->frames.size(), size_t{1});
    JCHECK(p.handler->frames[0] == (std::vector<uint8_t>{42}));

    // 20000 > 16384: the frame is discarded, then TooLongFrameException is raised.
    std::vector<uint8_t> big(20000, 0x55);
    big[0] = static_cast<uint8_t>(20000 & 0xFF);
    big[1] = static_cast<uint8_t>(20000 >> 8);
    std::vector<uint8_t> first(big.begin(), big.begin() + 5000);
    std::vector<uint8_t> rest(big.begin() + 5000, big.end());
    p.feed(first);
    JCHECK_EQ(p.handler->exceptions.size(), size_t{1});
    rest.push_back(4);  // next frame after the discarded one
    rest.push_back(0);
    rest.push_back(1);
    rest.push_back(2);
    p.feed(rest);
    JCHECK_EQ(p.handler->exceptions.size(), size_t{2});
    JCHECK_EQ(p.handler->exceptions[1], String("TooLongFrameException"));
    // Netty throws when the discard completes; the bytes after it in that read are lost.
    p.feed({4, 0, 5, 6});
    JCHECK(p.handler->frames.back() == (std::vector<uint8_t>{5, 6}));

    // Encoder: write() runs the encoder in the caller's thread and patches the length.
    ChannelBuffer* out = ChannelBuffers::buffer(ByteOrder::LITTLE_ENDIAN, 16384);
    out->writeShort(0);
    out->writeInt(77);
    ChannelFuture* f = p.channel->write(out);
    JCHECK(f->isSuccess());
    JCHECK_EQ(p.sink->written.size(), size_t{1});
    JCHECK(p.sink->written[0] == static_cast<Object*>(out));
    JCHECK_EQ(out->getUnsignedShort(0), 6);

    // close: the sink closes, channelClosed reaches the handler (after the decoder cleanup).
    p.channel->close();
    JCHECK(!p.channel->isOpen());
    JCHECK_EQ(p.handler->closed, 1);
    JCHECK(p.channel->getCloseFuture()->isDone());
}

JTEST(NettyPipelineHandlerExceptionAndListeners) {
    // A handler exception becomes an ExceptionEvent fired from the head of the pipeline.
    class Thrower final : public SimpleChannelUpstreamHandler {
    public:
        void messageReceived(ChannelHandlerContext*, MessageEvent*) override { throw jlang::IllegalStateException("boom"); }
    };
    ChannelPipeline* pipeline = Channels::pipeline();
    pipeline->addLast("thrower", new Thrower());
    auto* rec = new RecordingHandler();
    pipeline->addLast("rec", rec);
    auto* sink = new TestSink();
    auto* ch = new TestChannel(pipeline, sink);
    Channels::fireMessageReceived(ch, ChannelBuffers::buffer(4));
    JCHECK_EQ(rec->exceptions.size(), size_t{1});
    JCHECK_EQ(rec->exceptions[0], String("IllegalStateException"));
    JCHECK_THROWS(jlang::IllegalArgumentException, pipeline->addLast("rec", new RecordingHandler()));
    JCHECK(pipeline->get("rec") == static_cast<ChannelHandler*>(rec));
    JCHECK(pipeline->getLast() == static_cast<ChannelHandler*>(rec));

    // Futures: listeners run once, immediately when already done.
    auto* f = new DefaultChannelFuture(ch, false);
    int calls = 0;
    f->addListener(ChannelFutureListener::of([&calls](ChannelFuture*) { calls++; }));
    JCHECK_EQ(calls, 0);
    JCHECK(f->setSuccess());
    JCHECK(!f->setSuccess());
    JCHECK_EQ(calls, 1);
    f->addListener(ChannelFutureListener::of([&calls](ChannelFuture*) { calls++; }));
    JCHECK_EQ(calls, 2);
    JCHECK(f->awaitUninterruptibly(10));
    auto* failed = new DefaultChannelFuture(ch, false);
    failed->setFailure(new jlang::IOException("x"));
    JCHECK(!failed->isSuccess());
    JCHECK(failed->getCause() != nullptr);
    // CLOSE closes the channel when the operation completes.
    auto* g = new DefaultChannelFuture(ch, false);
    g->addListener(ChannelFutureListener::CLOSE);
    JCHECK(ch->isOpen());
    g->setSuccess();
    JCHECK(!ch->isOpen());
}

namespace {
// Counts per-channel sequence numbers and detects overlapping calls for one channel.
class OrderCheckingHandler final : public SimpleChannelUpstreamHandler {
public:
    void messageReceived(ChannelHandlerContext*, MessageEvent* e) override {
        if (inFlight.fetch_add(1) != 0) violations++;
        int32_t seq = jlang::cast<ChannelBuffer>(e->getMessage())->getInt(0);
        if (seq != next) violations++;
        next = seq + 1;
        if ((seq & 63) == 0) std::this_thread::yield();
        inFlight.fetch_sub(1);
        done++;
    }
    std::atomic<int> inFlight{0};
    std::atomic<int> violations{0};
    std::atomic<int> done{0};
    int32_t next = 0;
};
}  // namespace

JTEST(NettyOrderedExecutorPerChannelOrder) {
    auto* executor = new OrderedMemoryAwareThreadPoolExecutor(4, 0, 0, 50, jlang::TimeUnit::MILLISECONDS);
    const int kChannels = 16;
    const int kEvents = 500;
    std::vector<TestChannel*> channels;
    std::vector<OrderCheckingHandler*> handlers;
    for (int c = 0; c < kChannels; c++) {
        ChannelPipeline* p = Channels::pipeline();
        p->addLast("executor", new ExecutionHandler(executor));
        auto* h = new OrderCheckingHandler();
        p->addLast("handler", h);
        channels.push_back(new TestChannel(p, new TestSink()));
        handlers.push_back(h);
    }
    std::vector<jlang::Thread*> threads;
    for (int t = 0; t < 4; t++) {
        auto* th = new jlang::Thread(jlang::Runnable::of([t, &channels]() {
            for (int i = 0; i < kEvents; i++) {
                for (int c = t; c < kChannels; c += 4) {
                    ChannelBuffer* b = ChannelBuffers::buffer(4);
                    b->writeInt(i);
                    Channels::fireMessageReceived(channels[static_cast<size_t>(c)], b);
                }
            }
        }));
        threads.push_back(th);
        th->start();
    }
    for (auto* th : threads) th->join();
    JCHECK(waitFor([&] {
        for (auto* h : handlers) {
            if (h->done.load() != kEvents) return false;
        }
        return true;
    }));
    int violations = 0;
    for (auto* h : handlers) violations += h->violations.load();
    JCHECK_EQ(violations, 0);
    JCHECK(executor->getPoolSize() <= 4);
    JCHECK(waitFor([&] { return executor->getPoolSize() == 0; }));  // idle threads time out
    executor->shutdown();
    JCHECK(executor->awaitTermination(5, jlang::TimeUnit::SECONDS));
    JCHECK(executor->isTerminated());
}

// =======================================================================================
// Loopback: chatserver-shaped pipeline over the NIO transport
// =======================================================================================
namespace {

// Server-side bookkeeping shared by all connections of one test.
struct ServerStats {
    std::atomic<int> connected{0};
    std::atomic<int> disconnected{0};
    std::atomic<int> closed{0};
    std::atomic<int> messages{0};
    std::atomic<int> exceptions{0};
    std::atomic<int> violations{0};  // ordering / concurrency / lifecycle errors
    std::atomic<int> nextConnectionId{0};
    int sleepMicrosPerMessage = 0;   // slow handler (memory-limit test)
    std::mutex lock;
    std::vector<std::string> errors;
    std::vector<Channel*> channels;  // every connected channel (broadcast)
    void error(const std::string& s) {
        violations++;
        std::lock_guard<std::mutex> g(lock);
        if (errors.size() < 20) errors.push_back(s);
    }
};

ChannelBuffer* makeReply(int32_t seq, int payloadLen) {
    ChannelBuffer* out = ChannelBuffers::buffer(ByteOrder::LITTLE_ENDIAN, 2 * 8192);
    out->writeShort(0);  // patched by the encoder
    out->writeInt(seq);
    for (int i = 0; i < payloadLen; i++) out->writeByte(seq + i);
    return out;
}

// Frame body: int32 LE sequence, uint8 command, payload ((seq + i) & 0xFF). Commands:
//   0 echo, 1 echo then close with ChannelFutureListener.CLOSE, 2 close immediately,
//   3 throw from the handler (exceptionCaught closes), 4 burst of 40 large replies then CLOSE,
//   5 broadcast the reply to every connected channel (seq = connectionId << 16 | seq),
//   6 close, then write (the write must fail with ClosedChannelException).
class EchoHandler final : public SimpleChannelUpstreamHandler {
public:
    EchoHandler(ServerStats* stats, ChannelGroup* group) : stats_(stats), group_(group) {}

    void channelConnected(ChannelHandlerContext* ctx, ChannelStateEvent* e) override {
        Guard g(this);
        SimpleChannelUpstreamHandler::channelConnected(ctx, e);
        if (state_ != 0) stats_->error("connected twice");
        state_ = 1;
        channel_ = ctx->getChannel();
        id_ = stats_->nextConnectionId++;
        auto* remote = jlang::cast<jlang::InetSocketAddress>(e->getChannel()->getRemoteAddress());
        if (remote == nullptr || !remote->getAddress()->getHostAddress().equals("127.0.0.1")) {
            stats_->error("bad remote address");
        }
        if (group_ != nullptr) group_->add(channel_);
        {
            std::lock_guard<std::mutex> l(stats_->lock);
            stats_->channels.push_back(channel_);
        }
        stats_->connected++;
    }

    void messageReceived(ChannelHandlerContext* ctx, MessageEvent* e) override {
        Guard g(this);
        SimpleChannelUpstreamHandler::messageReceived(ctx, e);
        if (state_ != 1) stats_->error("message outside connected state");
        auto* buf = jlang::cast<ChannelBuffer>(e->getMessage());
        if (buf->order() != ByteOrder::LITTLE_ENDIAN) stats_->error("frame is not little endian");
        int32_t seq = buf->readInt();
        int32_t cmd = buf->readByte() & 0xFF;
        if (seq != expected_) stats_->error("out of order: got " + std::to_string(seq) + " expected " + std::to_string(expected_));
        expected_ = seq + 1;
        auto* payload = new Array<int8_t>(buf->readableBytes());
        buf->readBytes(payload);
        for (int32_t i = 0; i < payload->length; i++) {
            if ((*payload)[i] != static_cast<int8_t>(seq + i)) {
                stats_->error("payload mismatch");
                break;
            }
        }
        stats_->messages++;
        if (stats_->sleepMicrosPerMessage > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(stats_->sleepMicrosPerMessage));
        }
        switch (cmd) {
        case 2: e->getChannel()->close(); return;
        case 3: throw jlang::IllegalStateException("handler failure");
        case 4: {
            ChannelFuture* last = nullptr;
            for (int k = 0; k < 40; k++) last = channel_->write(makeReply(1000 + k, 16000));
            last->addListener(ChannelFutureListener::CLOSE);
            return;
        }
        case 5: {
            std::vector<Channel*> all;
            {
                std::lock_guard<std::mutex> l(stats_->lock);
                all = stats_->channels;
            }
            for (Channel* c : all) c->write(makeReply((id_ << 16) | seq, payload->length));
            return;
        }
        case 6: {
            channel_->close();
            ChannelFuture* f = channel_->write(makeReply(seq, 1));
            if (!f->isDone() || f->isSuccess() || jlang::cast<jlang::ClosedChannelException>(f->getCause()) == nullptr) {
                stats_->error("write after close did not fail with ClosedChannelException");
            }
            return;
        }
        default: break;
        }
        ChannelBuffer* out = ChannelBuffers::buffer(ByteOrder::LITTLE_ENDIAN, 2 * 8192);
        out->writeShort(0);
        out->writeInt(seq);
        out->writeBytes(payload);
        ChannelFuture* f = channel_->write(out);
        if (cmd == 1) f->addListener(ChannelFutureListener::CLOSE);
    }

    void channelDisconnected(ChannelHandlerContext* ctx, ChannelStateEvent* e) override {
        Guard g(this);
        if (state_ != 1) stats_->error("disconnected in state " + std::to_string(state_));
        state_ = 2;
        stats_->disconnected++;
        SimpleChannelUpstreamHandler::channelDisconnected(ctx, e);
    }

    void channelClosed(ChannelHandlerContext* ctx, ChannelStateEvent* e) override {
        Guard g(this);
        if (state_ != 2) stats_->error("closed before disconnected");
        state_ = 3;
        {
            std::lock_guard<std::mutex> l(stats_->lock);
            auto& v = stats_->channels;
            v.erase(std::remove(v.begin(), v.end(), channel_), v.end());
        }
        stats_->closed++;
        SimpleChannelUpstreamHandler::channelClosed(ctx, e);
    }

    void exceptionCaught(ChannelHandlerContext*, ExceptionEvent* e) override {
        Guard g(this);
        stats_->exceptions++;
        e->getChannel()->close();
    }

private:
    // Detects two handler methods of one channel running at the same time.
    struct Guard {
        explicit Guard(EchoHandler* h) : h_(h) {
            if (h_->inFlight_.fetch_add(1) != 0) h_->stats_->error("concurrent handler calls");
        }
        ~Guard() { h_->inFlight_.fetch_sub(1); }
        EchoHandler* h_;
    };
    ServerStats* stats_;
    ChannelGroup* group_;
    Channel* channel_ = nullptr;
    std::atomic<int> inFlight_{0};
    int state_ = 0;
    int32_t id_ = 0;
    int32_t expected_ = 0;
};

class TestPipelineFactory final : public virtual ChannelPipelineFactory {
public:
    TestPipelineFactory(ServerStats* stats, ChannelGroup* group, int64_t maxChannelMemory, int64_t maxTotalMemory)
        : stats_(stats), group_(group) {
        executor_ = new OrderedMemoryAwareThreadPoolExecutor(10, maxChannelMemory, maxTotalMemory, 100,
                                                             jlang::TimeUnit::MILLISECONDS,
                                                             jlang::Executors::defaultThreadFactory());
    }
    ChannelPipeline* getPipeline() override {
        ChannelPipeline* pipeline = Channels::pipeline();
        pipeline->addLast("framedecoder", new PacketFrameDecoder());
        pipeline->addLast("packetdecoder", new PassThroughDecoder());
        pipeline->addLast("packetencoder", new LengthEncoder());
        pipeline->addLast("executor", new ExecutionHandler(executor_));
        pipeline->addLast("handler", new EchoHandler(stats_, group_));
        return pipeline;
    }
    OrderedMemoryAwareThreadPoolExecutor* executor_;

private:
    ServerStats* stats_;
    ChannelGroup* group_;
};

// NettyServer.initChannel: the chatserver's bootstrap options.
struct TestServer {
    ServerStats* stats = new ServerStats();
    ChannelGroup* group = nullptr;
    ChannelFactory* factory = nullptr;
    TestPipelineFactory* pipelineFactory = nullptr;
    Channel* serverChannel = nullptr;
    int port = 0;

    explicit TestServer(ChannelGroup* g = nullptr, int64_t maxChannelMemory = 1048576,
                        int64_t maxTotalMemory = 134217728, int32_t sendBufferSize = 0)
        : group(g) {
        factory = new NioServerSocketChannelFactory(jlang::Executors::newCachedThreadPool(),
                                                    jlang::Executors::newCachedThreadPool(), 3);
        pipelineFactory = new TestPipelineFactory(stats, group, maxChannelMemory, maxTotalMemory);
        auto* bootstrap = new ServerBootstrap(factory);
        bootstrap->setPipelineFactory(pipelineFactory);
        bootstrap->setOption("child.bufferFactory", HeapChannelBufferFactory::getInstance(ByteOrder::LITTLE_ENDIAN));
        bootstrap->setOption("child.tcpNoDelay", jlang::box(true));
        bootstrap->setOption("child.keepAlive", jlang::box(true));
        bootstrap->setOption("child.reuseAddress", jlang::box(true));
        bootstrap->setOption("child.connectTimeoutMillis", jlang::box(100));
        bootstrap->setOption("readWriteFair", jlang::box(true));
        if (sendBufferSize > 0) bootstrap->setOption("child.sendBufferSize", sendBufferSize);
        serverChannel = bootstrap->bind(new jlang::InetSocketAddress(jlang::InetAddress::getByName("127.0.0.1"), 0));
        port = serverChannel->getLocalAddress()->getPort();
    }
    void shutdown() {
        serverChannel->close()->awaitUninterruptibly();
        factory->releaseExternalResources();
    }
};

// ---- raw POSIX test client
int connectTo(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) {
        ::close(fd);
        return -1;
    }
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

bool sendAll(int fd, const uint8_t* p, size_t n) {
    while (n > 0) {
        ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

// Reads exactly n bytes (false on EOF/timeout).
bool recvAll(int fd, uint8_t* p, size_t n, int timeoutMs = 20000) {
    while (n > 0) {
        pollfd pf{fd, POLLIN, 0};
        int r = ::poll(&pf, 1, timeoutMs);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        ssize_t got = ::recv(fd, p, n, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        p += got;
        n -= static_cast<size_t>(got);
    }
    return true;
}

// True when the peer closed the connection (EOF or reset) within the timeout.
bool waitEof(int fd, int timeoutMs = 20000) {
    uint8_t b;
    for (;;) {
        pollfd pf{fd, POLLIN, 0};
        int r = ::poll(&pf, 1, timeoutMs);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        ssize_t got = ::recv(fd, &b, 1, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return true;
    }
}

void appendFrame(std::vector<uint8_t>& out, int32_t seq, int cmd, int payloadLen) {
    int total = 2 + 4 + 1 + payloadLen;
    out.push_back(static_cast<uint8_t>(total & 0xFF));
    out.push_back(static_cast<uint8_t>(total >> 8));
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(seq >> (8 * i)));
    out.push_back(static_cast<uint8_t>(cmd));
    for (int i = 0; i < payloadLen; i++) out.push_back(static_cast<uint8_t>(seq + i));
}

// Reads one response frame; returns the sequence number (or -1) and checks the payload.
int32_t readResponse(int fd, int* payloadLenOut = nullptr, bool* payloadOk = nullptr) {
    uint8_t hdr[2];
    if (!recvAll(fd, hdr, 2)) return -1;
    int total = hdr[0] | (hdr[1] << 8);
    if (total < 6) return -1;
    std::vector<uint8_t> body(static_cast<size_t>(total - 2));
    if (!recvAll(fd, body.data(), body.size())) return -1;
    int32_t seq = static_cast<int32_t>(body[0] | (body[1] << 8) | (body[2] << 16) | (static_cast<uint32_t>(body[3]) << 24));
    bool ok = true;
    for (size_t i = 4; i < body.size(); i++) {
        if (body[i] != static_cast<uint8_t>(seq + static_cast<int32_t>(i - 4))) ok = false;
    }
    if (payloadLenOut) *payloadLenOut = static_cast<int>(body.size() - 4);
    if (payloadOk) *payloadOk = ok;
    return seq;
}

void reportErrors(ServerStats* stats) {
    std::lock_guard<std::mutex> g(stats->lock);
    for (auto& e : stats->errors) std::fprintf(stderr, "    server error: %s\n", e.c_str());
}

}  // namespace

namespace {

// Runs `clients` client threads: each connects, sends `frames` frames (random sizes, a few
// large ones) as a byte stream cut into random fragments (1..3 bytes up to several frames per
// send), then reads and checks every echoed frame in order.
void runEchoClients(TestServer& server, int clients, int frames, std::atomic<int>& failures,
                    std::atomic<int>& responses) {
    std::vector<jlang::Thread*> threads;
    for (int c = 0; c < clients; c++) {
        int port = server.port;
        auto* t = new jlang::Thread(jlang::Runnable::of([c, port, frames, &failures, &responses]() {
            std::mt19937 rng(static_cast<unsigned>(c * 7919 + 1));
            int fd = connectTo(port);
            if (fd < 0) {
                failures++;
                return;
            }
            std::vector<uint8_t> stream;
            std::vector<int> lengths;
            for (int i = 0; i < frames; i++) {
                int len = static_cast<int>(rng() % 300);
                if (i % 50 == 7) len = 5000 + static_cast<int>(rng() % 3000);
                lengths.push_back(len);
                appendFrame(stream, i, 0, len);
            }
            size_t pos = 0;
            while (pos < stream.size()) {
                size_t chunk = (rng() % 4 == 0) ? 1 + rng() % 3 : 1 + rng() % 1500;
                chunk = std::min(chunk, stream.size() - pos);
                if (!sendAll(fd, stream.data() + pos, chunk)) {
                    failures++;
                    ::close(fd);
                    return;
                }
                pos += chunk;
                if (rng() % 16 == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            for (int i = 0; i < frames; i++) {
                int plen = -1;
                bool ok = false;
                int32_t seq = readResponse(fd, &plen, &ok);
                if (seq != i || plen != lengths[static_cast<size_t>(i)] || !ok) {
                    failures++;
                    break;
                }
                responses++;
            }
            ::close(fd);
        }));
        threads.push_back(t);
        t->start();
    }
    for (auto* t : threads) t->join();
}

}  // namespace

JTEST(NettyLoopbackManyClientsOrdering) {
    TestServer server;
    JCHECK(server.port > 0);
    const int kClients = 40;
    const int kFrames = 250;
    std::atomic<int> clientFailures{0};
    std::atomic<int> responses{0};
    // Collect garbage continuously while the traffic runs (GC safety of the transport).
    std::atomic<bool> stopGc{false};
    auto* gcThread = new jlang::Thread(jlang::Runnable::of([&stopGc]() {
        while (!stopGc.load()) {
            jlang::gc::collect();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }));
    gcThread->start();
    runEchoClients(server, kClients, kFrames, clientFailures, responses);
    stopGc.store(true);
    gcThread->join();
    JCHECK_EQ(clientFailures.load(), 0);
    JCHECK_EQ(responses.load(), kClients * kFrames);
    JCHECK(waitFor([&] { return server.stats->closed.load() == kClients; }));
    JCHECK_EQ(server.stats->connected.load(), kClients);
    JCHECK_EQ(server.stats->messages.load(), kClients * kFrames);
    JCHECK_EQ(server.stats->disconnected.load(), kClients);
    JCHECK_EQ(server.stats->exceptions.load(), 0);
    JCHECK_EQ(server.stats->violations.load(), 0);
    reportErrors(server.stats);
    server.serverChannel->close()->awaitUninterruptibly();
    JCHECK(!server.serverChannel->isOpen());
    JCHECK(!server.serverChannel->isBound());
    server.factory->releaseExternalResources();
}

JTEST(NettyLoopbackMemoryLimits) {
    // Tiny per-channel limit (reads are suspended with setReadable(false) and resumed) and a
    // small total limit (the I/O thread blocks in execute()), with a slow handler.
    TestServer server(nullptr, 2000, 20000);
    server.stats->sleepMicrosPerMessage = 300;
    std::atomic<int> failures{0};
    std::atomic<int> responses{0};
    runEchoClients(server, 6, 200, failures, responses);
    JCHECK_EQ(failures.load(), 0);
    JCHECK_EQ(responses.load(), 6 * 200);
    JCHECK(waitFor([&] { return server.stats->closed.load() == 6; }));
    JCHECK_EQ(server.stats->violations.load(), 0);
    reportErrors(server.stats);
    server.shutdown();
}

JTEST(NettyLoopbackBroadcast) {
    // Handlers write to every connected channel from their executor threads (like the
    // chatserver's BroadcastService): each client must get every sender's messages in order.
    TestServer server;
    const int kClients = 8;
    const int kMessages = 60;
    std::atomic<int> failures{0};
    std::atomic<int> ready{0};
    std::vector<jlang::Thread*> threads;
    for (int c = 0; c < kClients; c++) {
        int port = server.port;
        ServerStats* stats = server.stats;
        auto* t = new jlang::Thread(jlang::Runnable::of([port, stats, &failures, &ready]() {
            int fd = connectTo(port);
            if (fd < 0) {
                failures++;
                return;
            }
            ready++;
            // Everyone is connected (and registered by channelConnected) before broadcasting.
            if (!waitFor([&] { return ready.load() == kClients && stats->connected.load() == kClients; })) failures++;
            std::vector<uint8_t> stream;
            for (int i = 0; i < kMessages; i++) appendFrame(stream, i, 5, 10 + i);
            if (!sendAll(fd, stream.data(), stream.size())) failures++;
            std::vector<int> next(kClients, 0);
            for (int n = 0; n < kClients * kMessages; n++) {
                int plen = 0;
                bool ok = false;
                int32_t seq = readResponse(fd, &plen, &ok);
                if (seq < 0 || !ok) {
                    failures++;
                    break;
                }
                int sender = seq >> 16, i = seq & 0xFFFF;
                if (sender < 0 || sender >= kClients || next[static_cast<size_t>(sender)] != i || plen != 10 + i) {
                    failures++;
                    break;
                }
                next[static_cast<size_t>(sender)]++;
            }
            ::close(fd);
        }));
        threads.push_back(t);
        t->start();
    }
    for (auto* t : threads) t->join();
    JCHECK_EQ(failures.load(), 0);
    JCHECK(waitFor([&] { return server.stats->closed.load() == kClients; }));
    JCHECK_EQ(server.stats->violations.load(), 0);
    reportErrors(server.stats);
    server.shutdown();
}

JTEST(NettyLoopbackCloseSemantics) {
    // A small kernel send buffer makes the server hit partial writes (OP_WRITE) in case 2.
    TestServer server(nullptr, 1048576, 134217728, 8192);
    ServerStats* stats = server.stats;

    // 1. write(...).addListener(CLOSE): the response is flushed, then the connection closes.
    {
        int fd = connectTo(server.port);
        JCHECK(fd >= 0);
        std::vector<uint8_t> s;
        appendFrame(s, 0, 0, 10);
        appendFrame(s, 1, 1, 12000);  // large reply, then CLOSE
        JCHECK(sendAll(fd, s.data(), s.size()));
        int len = 0;
        bool ok = false;
        JCHECK_EQ(readResponse(fd, &len, &ok), 0);
        JCHECK_EQ(readResponse(fd, &len, &ok), 1);
        JCHECK_EQ(len, 12000);
        JCHECK(ok);
        JCHECK(waitEof(fd));
        ::close(fd);
    }
    // 2. A burst larger than the socket buffers (partial writes, OP_WRITE) followed by CLOSE:
    //    every byte is delivered before the close, even though the client reads late.
    {
        int fd = connectTo(server.port);
        std::vector<uint8_t> s;
        appendFrame(s, 0, 4, 1);
        JCHECK(sendAll(fd, s.data(), s.size()));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        for (int k = 0; k < 40; k++) {
            int len = 0;
            bool ok = false;
            JCHECK_EQ(readResponse(fd, &len, &ok), 1000 + k);
            JCHECK_EQ(len, 16000);
            JCHECK(ok);
        }
        JCHECK(waitEof(fd));
        ::close(fd);
    }
    // 3. close() from the handler: the client sees EOF, no reply.
    {
        int fd = connectTo(server.port);
        std::vector<uint8_t> s;
        appendFrame(s, 0, 2, 3);
        JCHECK(sendAll(fd, s.data(), s.size()));
        JCHECK(waitEof(fd));
        ::close(fd);
    }
    // 4. A handler exception goes to exceptionCaught, which closes the channel.
    {
        int fd = connectTo(server.port);
        std::vector<uint8_t> s;
        appendFrame(s, 0, 3, 3);
        JCHECK(sendAll(fd, s.data(), s.size()));
        JCHECK(waitEof(fd));
        ::close(fd);
    }
    // 5. A corrupt length field (< 2): CorruptedFrameException -> exceptionCaught -> close.
    {
        int fd = connectTo(server.port);
        uint8_t bad[] = {1, 0, 0, 0};
        JCHECK(sendAll(fd, bad, sizeof bad));
        JCHECK(waitEof(fd));
        ::close(fd);
    }
    // 6. Writing to a closed channel fails (ClosedChannelException) and fires exceptionCaught.
    {
        int fd = connectTo(server.port);
        std::vector<uint8_t> s;
        appendFrame(s, 0, 6, 3);
        JCHECK(sendAll(fd, s.data(), s.size()));
        JCHECK(waitEof(fd));
        ::close(fd);
    }
    // 7. Remote close: channelDisconnected then channelClosed (checked by the handler).
    {
        int fd = connectTo(server.port);
        std::vector<uint8_t> s;
        appendFrame(s, 0, 0, 1);
        JCHECK(sendAll(fd, s.data(), s.size()));
        JCHECK_EQ(readResponse(fd), 0);
        ::close(fd);
    }
    JCHECK(waitFor([&] { return stats->closed.load() == 7; }));
    JCHECK_EQ(stats->connected.load(), 7);
    JCHECK_EQ(stats->disconnected.load(), 7);
    // handler failure + corrupted frame + the failed write after close
    JCHECK(waitFor([&] { return stats->exceptions.load() == 3; }));
    JCHECK_EQ(stats->violations.load(), 0);
    reportErrors(stats);
    server.shutdown();
}

JTEST(NettyLoopbackChannelGroupClose) {
    auto* group = new ChannelGroup("test-group");
    TestServer a(group);
    TestServer b(group);
    group->add(a.serverChannel);
    group->add(b.serverChannel);
    JCHECK_EQ(group->size(), 2);
    JCHECK(!group->add(a.serverChannel));  // already there

    int c1 = connectTo(a.port);
    int c2 = connectTo(b.port);
    JCHECK(c1 >= 0 && c2 >= 0);
    std::vector<uint8_t> s;
    appendFrame(s, 0, 0, 4);
    JCHECK(sendAll(c1, s.data(), s.size()));
    JCHECK(sendAll(c2, s.data(), s.size()));
    JCHECK_EQ(readResponse(c1), 0);
    JCHECK_EQ(readResponse(c2), 0);
    JCHECK(waitFor([&] { return group->size() == 4; }));  // the handlers added the children

    ChannelGroupFuture* f = group->close();
    f->awaitUninterruptibly();
    JCHECK(f->isDone());
    JCHECK(f->isCompleteSuccess());
    JCHECK(group->isEmpty());  // closed channels leave the group
    JCHECK(!a.serverChannel->isOpen());
    JCHECK(!b.serverChannel->isOpen());
    JCHECK(waitEof(c1));
    JCHECK(waitEof(c2));
    ::close(c1);
    ::close(c2);
    JCHECK(connectTo(a.port) < 0);  // no longer listening
    JCHECK(waitFor([&] { return a.stats->closed.load() == 1 && b.stats->closed.load() == 1; }));
    JCHECK_EQ(a.stats->violations.load() + b.stats->violations.load(), 0);
    a.factory->releaseExternalResources();
    b.factory->releaseExternalResources();
}

JTEST(NettyBindFailure) {
    TestServer a;
    auto* factory = new NioServerSocketChannelFactory(jlang::Executors::newCachedThreadPool(),
                                                      jlang::Executors::newCachedThreadPool(), 1);
    auto* bootstrap = new ServerBootstrap(factory);
    bootstrap->setPipelineFactory(a.pipelineFactory);
    auto* addr = new jlang::InetSocketAddress(jlang::InetAddress::getByName("127.0.0.1"), a.port);
    JCHECK_THROWS(ChannelException, bootstrap->bind(addr));
    a.shutdown();
    factory->releaseExternalResources();
}
