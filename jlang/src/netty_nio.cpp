// jlang/src/netty_nio.cpp - org.jboss.netty.channel.socket.nio server transport
// (NioServerSocketChannelFactory, NioServerSocketPipelineSink with its Boss, NioWorker,
// NioServerSocketChannel, NioAcceptedSocketChannel), ported from Netty 3.2.0.BETA1 onto the
// jlang NIO layer (<jlang/Nio.h>: Selector, SelectionKey, SocketChannel, ServerSocketChannel).
//
// One deliberate difference: Netty closes a socket in whatever thread calls close(). Here the
// descriptor itself is closed by the channel's I/O thread (immediately when close() runs there,
// otherwise through the worker's task queue, which is woken up), and a server socket is closed
// only after its boss thread stopped accepting. Everything observable (close future, the
// disconnected/unbound/closed events, failing pending writes) still happens synchronously in the
// calling thread; the only effect is that the kernel socket may outlive close() by one selector
// wake-up. This removes the descriptor-reuse races of closing a descriptor that another thread
// may be reading from.
#include "netty_internal.h"

#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace jlang::netty {

using ::jlang::InetSocketAddress;
using ::jlang::Object;
using ::jlang::Runnable;
using ::jlang::SelectionKey;
using ::jlang::Selector;
using ::jlang::String;
using ::jlang::Throwable;

namespace detail {

class NioWorker;
class NioServerSocketPipelineSink;

// =======================================================================================
// IoWorkerRunnable + ThreadRenamingRunnable
// =======================================================================================
class IoWorkerRunnable final : public virtual Runnable {
public:
    IoWorkerRunnable(Runnable* runnable, const String& threadName) : runnable_(runnable), threadName_(threadName) {}
    void run() override {
        ::jlang::Thread* t = ::jlang::Thread::currentThread();
        String oldName = t->getName();
        t->setName(threadName_);
        setInIoThread(true);
        try {
            runnable_->run();
        } catch (...) {
            setInIoThread(false);
            t->setName(oldName);
            throw;
        }
        setInIoThread(false);
        t->setName(oldName);
    }

private:
    Runnable* runnable_;
    String threadName_;
};

// =======================================================================================
// AdaptiveReceiveBufferSizePredictor (64 / 1024 / 65536)
// =======================================================================================
class AdaptiveReceiveBufferSizePredictor {
public:
    AdaptiveReceiveBufferSizePredictor() {
        const std::vector<int32_t>& t = table();
        int32_t minIndex = getSizeTableIndex(64);
        minIndex_ = t[minIndex] < 64 ? minIndex + 1 : minIndex;
        int32_t maxIndex = getSizeTableIndex(65536);
        maxIndex_ = t[maxIndex] > 65536 ? maxIndex - 1 : maxIndex;
        index_ = getSizeTableIndex(1024);
        nextReceiveBufferSize_ = t[index_];
    }
    int32_t nextReceiveBufferSize() const { return nextReceiveBufferSize_; }
    void previousReceiveBufferSize(int32_t previousReceiveBufferSize) {
        const std::vector<int32_t>& t = table();
        if (previousReceiveBufferSize <= t[std::max(0, index_ - 1 - 1)]) {
            if (decreaseNow_) {
                index_ = std::max(index_ - 1, minIndex_);
                nextReceiveBufferSize_ = t[index_];
                decreaseNow_ = false;
            } else {
                decreaseNow_ = true;
            }
        } else if (previousReceiveBufferSize >= nextReceiveBufferSize_) {
            index_ = std::min(index_ + 4, maxIndex_);
            nextReceiveBufferSize_ = t[index_];
            decreaseNow_ = false;
        }
    }

private:
    static const std::vector<int32_t>& table() {
        static const std::vector<int32_t>* sizeTable = [] {
            auto* v = new std::vector<int32_t>();
            for (int32_t i = 1; i <= 8; i++) v->push_back(i);
            for (int32_t i = 4; i < 32; i++) {
                int64_t val = INT64_C(1) << i;
                int64_t inc = val >> 4;
                val -= inc << 3;
                for (int32_t j = 0; j < 8; j++) {
                    val += inc;
                    v->push_back(val > INT32_MAX ? INT32_MAX : static_cast<int32_t>(val));
                }
            }
            return v;
        }();
        return *sizeTable;
    }
    static int32_t getSizeTableIndex(int32_t size) {
        if (size <= 16) return size - 1;
        int32_t bits = 0;
        uint32_t v = static_cast<uint32_t>(size);
        do {
            v >>= 1;
            bits++;
        } while (v != 0);
        const int32_t baseIdx = bits << 3;
        const int32_t startIdx = baseIdx - 18;
        const int32_t endIdx = baseIdx - 25;
        const std::vector<int32_t>& t = table();
        for (int32_t i = startIdx; i >= endIdx; i--) {
            if (size >= t[i]) return i;
        }
        throw ::jlang::Error("shouldn't reach here; please file a bug report.");
    }
    int32_t minIndex_ = 0;
    int32_t maxIndex_ = 0;
    int32_t index_ = 0;
    int32_t nextReceiveBufferSize_ = 1024;
    bool decreaseNow_ = false;
};

// =======================================================================================
// Channels
// =======================================================================================

// A queued outgoing message (SocketSendBufferPool.SendBuffer): the readable bytes of the
// ChannelBuffer, copied when the I/O thread starts writing it.
class SendBuffer final : public virtual Object {
public:
    explicit SendBuffer(ChannelBuffer* src) {
        int32_t n = src->readableBytes();
        auto* bytes = new ::jlang::Array<int8_t>(n);
        if (n > 0) std::memcpy(bytes->data(), src->rawBytes(src->readerIndex(), n), static_cast<size_t>(n));
        buffer_ = ::jlang::ByteBuffer::wrap(bytes);
        total_ = n;
    }
    bool finished() { return !buffer_->hasRemaining(); }
    int64_t writtenBytes() { return total_ - buffer_->remaining(); }
    int64_t totalBytes() { return total_; }
    int64_t transferTo(::jlang::SocketChannel* ch) { return ch->write(buffer_); }

private:
    ::jlang::ByteBuffer* buffer_;
    int64_t total_;
};

// org.jboss.netty.channel.socket.nio.NioServerSocketChannel
class NioServerSocketChannel final : public AbstractChannel, public virtual ServerChannel {
public:
    NioServerSocketChannel(ChannelFactory* factory, ChannelPipeline* pipeline, ChannelSink* sink)
        : AbstractChannel(nullptr, factory, pipeline, sink) {
        try {
            socket = ::jlang::ServerSocketChannel::open();
        } catch (::jlang::IOException& e) {
            throw ChannelException("Failed to open a server socket.", e);
        }
        try {
            socket->configureBlocking(false);
        } catch (::jlang::IOException& e) {
            try {
                socket->close();
            } catch (::jlang::IOException& e2) {
                logWarn("org.jboss.netty.channel.socket.nio.NioServerSocketChannel",
                        "Failed to close a partially initialized socket.", &e2);
            }
            throw ChannelException("Failed to enter non-blocking mode.", e);
        }
        config_ = new ServerSocketChannelConfig(socket);
        Channels::fireChannelOpen(this);
    }
    ChannelConfig* getConfig() override { return config_; }
    ServerSocketChannelConfig* serverConfig() { return config_; }
    InetSocketAddress* getLocalAddress() override {
        try {
            return socket->isOpen() ? socket->getLocalAddress() : nullptr;
        } catch (Throwable&) {
            return nullptr;
        }
    }
    InetSocketAddress* getRemoteAddress() override { return nullptr; }
    bool isBound() override { return isOpen() && bound.load(); }
    bool isConnected() override { return false; }
    // AbstractServerChannel: unsupported operations.
    ChannelFuture* connect(InetSocketAddress*) override { return unsupported(); }
    ChannelFuture* disconnect() override { return unsupported(); }
    int32_t getInterestOps() override { return OP_NONE; }
    ChannelFuture* setInterestOps(int32_t) override { return unsupported(); }
    ChannelFuture* write(Object*) override { return unsupported(); }
    ChannelFuture* write(Object*, InetSocketAddress*) override { return unsupported(); }

    ::jlang::ServerSocketChannel* socket = nullptr;
    std::recursive_mutex shutdownLock;       // held by the boss thread while it accepts
    std::atomic<Selector*> selector{nullptr};  // the boss selector (wakeup on close)
    std::atomic<bool> closeRequested{false};
    std::atomic<bool> bound{false};

private:
    ChannelFuture* unsupported() {
        return new FailedChannelFuture(this, new ::jlang::UnsupportedOperationException());
    }
    ServerSocketChannelConfig* config_ = nullptr;
};

// org.jboss.netty.channel.socket.nio.NioSocketChannel / NioAcceptedSocketChannel
class NioSocketChannel final : public AbstractChannel {
public:
    static constexpr int32_t ST_OPEN = 0;
    static constexpr int32_t ST_BOUND = 1;
    static constexpr int32_t ST_CONNECTED = 2;
    static constexpr int32_t ST_CLOSED = -1;

    NioSocketChannel(ChannelFactory* factory, ChannelPipeline* pipeline, Channel* parent, ChannelSink* sink,
                     ::jlang::SocketChannel* socket, NioWorker* worker, pthread_t bossThread);

    ChannelConfig* getConfig() override { return config_; }
    SocketChannelConfig* socketConfig() { return config_; }
    InetSocketAddress* getLocalAddress() override {
        InetSocketAddress* a = localAddress_.load();
        if (a == nullptr) {
            try {
                a = socket->getLocalAddress();
                localAddress_.store(a);
            } catch (Throwable&) {
                return nullptr;
            }
        }
        return a;
    }
    InetSocketAddress* getRemoteAddress() override {
        InetSocketAddress* a = remoteAddress_.load();
        if (a == nullptr) {
            try {
                a = socket->getRemoteAddress();
                remoteAddress_.store(a);
            } catch (Throwable&) {
                return nullptr;
            }
        }
        return a;
    }
    bool isOpen() override { return state.load() >= ST_OPEN; }
    bool isBound() override { return state.load() >= ST_BOUND; }
    bool isConnected() override { return state.load() == ST_CONNECTED; }
    void setConnected() {
        if (state.load() != ST_CLOSED) state.store(ST_CONNECTED);
    }
    bool setClosed() override {
        state.store(ST_CLOSED);
        return AbstractChannel::setClosed();
    }
    int32_t getInterestOps() override;
    int32_t getRawInterestOps() { return AbstractChannel::getInterestOps(); }
    void setRawInterestOpsNow(int32_t interestOps) { setInterestOpsNow(interestOps); }
    ChannelFuture* write(Object* message, InetSocketAddress* remoteAddress) override {
        if (remoteAddress == nullptr || remoteAddress->equals(getRemoteAddress())) {
            return AbstractChannel::write(message, nullptr);
        }
        return new FailedChannelFuture(this, new ::jlang::UnsupportedOperationException());
    }
    using AbstractChannel::write;

    // WriteRequestQueue
    void offerWrite(MessageEvent* e);
    MessageEvent* pollWrite();
    bool writeBufferEmpty() {
        std::lock_guard<std::mutex> g(writeQueueLock);
        return writeBuffer.empty();
    }

    ::jlang::SocketChannel* socket;
    NioWorker* worker;
    pthread_t bossThread;
    std::atomic<int32_t> state{ST_OPEN};
    std::mutex interestOpsLock;
    std::recursive_mutex writeLock;
    Runnable* writeTask = nullptr;
    std::atomic<bool> writeTaskInTaskQueue{false};
    std::mutex writeQueueLock;
    std::deque<MessageEvent*> writeBuffer;
    std::atomic<int32_t> writeBufferSize{0};
    std::atomic<int32_t> highWaterMarkCounter{0};
    bool inWriteNowLoop = false;
    bool writeSuspended = false;
    MessageEvent* currentWriteEvent = nullptr;
    SendBuffer* currentWriteBuffer = nullptr;
    AdaptiveReceiveBufferSizePredictor predictor;
    // Owned by the worker thread:
    SelectionKey* key = nullptr;
    bool registered = false;
    bool released = false;

private:
    SocketChannelConfig* config_;
    std::atomic<InetSocketAddress*> localAddress_{nullptr};
    std::atomic<InetSocketAddress*> remoteAddress_{nullptr};
};

namespace {
thread_local bool tlNotifying = false;  // WriteRequestQueue.notifying

int32_t messageSize(MessageEvent* e) {
    if (auto* b = dynamic_cast<ChannelBuffer*>(e->getMessage())) return b->readableBytes();
    return 0;
}
}  // namespace

void NioSocketChannel::offerWrite(MessageEvent* e) {
    {
        std::lock_guard<std::mutex> g(writeQueueLock);
        writeBuffer.push_back(e);
    }
    int32_t size = messageSize(e);
    int32_t newWriteBufferSize = writeBufferSize += size;
    int32_t highWaterMark = config_->getWriteBufferHighWaterMark();
    if (newWriteBufferSize >= highWaterMark && newWriteBufferSize - size < highWaterMark) {
        highWaterMarkCounter++;
        if (!tlNotifying) {
            tlNotifying = true;
            Channels::fireChannelInterestChanged(this);
            tlNotifying = false;
        }
    }
}

MessageEvent* NioSocketChannel::pollWrite() {
    MessageEvent* e = nullptr;
    {
        std::lock_guard<std::mutex> g(writeQueueLock);
        if (writeBuffer.empty()) return nullptr;
        e = writeBuffer.front();
        writeBuffer.pop_front();
    }
    int32_t size = messageSize(e);
    int32_t newWriteBufferSize = writeBufferSize -= size;
    int32_t lowWaterMark = config_->getWriteBufferLowWaterMark();
    if (newWriteBufferSize == 0 || newWriteBufferSize < lowWaterMark) {
        if (newWriteBufferSize + size >= lowWaterMark) {
            highWaterMarkCounter--;
            if (!tlNotifying) {
                tlNotifying = true;
                Channels::fireChannelInterestChanged(this);
                tlNotifying = false;
            }
        }
    }
    return e;
}

int32_t NioSocketChannel::getInterestOps() {
    if (!isOpen()) return OP_WRITE;
    int32_t interestOps = getRawInterestOps();
    int32_t size = writeBufferSize.load();
    if (size != 0) {
        if (highWaterMarkCounter.load() > 0) {
            if (size >= config_->getWriteBufferLowWaterMark()) {
                interestOps |= OP_WRITE;
            } else {
                interestOps &= ~OP_WRITE;
            }
        } else {
            if (size >= config_->getWriteBufferHighWaterMark()) {
                interestOps |= OP_WRITE;
            } else {
                interestOps &= ~OP_WRITE;
            }
        }
    } else {
        interestOps &= ~OP_WRITE;
    }
    return interestOps;
}

// =======================================================================================
// NioWorker
// =======================================================================================
class NioWorker final : public virtual Runnable {
public:
    NioWorker(int32_t bossId, int32_t id, ::jlang::Executor* executor) : bossId_(bossId), id_(id), executor_(executor) {}

    void register_(NioSocketChannel* channel, ChannelFuture* future);
    void run() override;
    void writeFromUserCode(NioSocketChannel* channel);
    void writeFromTaskLoop(NioSocketChannel* channel) {
        if (!channel->writeSuspended) write0(channel);
    }
    void close(NioSocketChannel* channel, ChannelFuture* future);
    void setInterestOps(NioSocketChannel* channel, ChannelFuture* future, int32_t interestOps);
    bool inWorkerThread() { return hasThread_.load() && pthread_equal(thread_.load(), pthread_self()); }

private:
    friend class RegisterTask;
    friend class ReleaseTask;
    void submit(Runnable* task, bool server);
    void wakeup();
    void processTaskQueue(std::deque<Runnable*>& queue);
    void processSelectedKeys(Selector* selector);
    bool read(NioSocketChannel* channel);
    void writeFromSelectorLoop(NioSocketChannel* channel) {
        channel->writeSuspended = false;
        write0(channel);
    }
    bool scheduleWriteIfNecessary(NioSocketChannel* channel);
    void write0(NioSocketChannel* channel);
    void setOpWrite(NioSocketChannel* channel);
    void clearOpWrite(NioSocketChannel* channel);
    void closeSocket(NioSocketChannel* channel);
    void releaseSocket(NioSocketChannel* channel);
    void cleanUpWriteBuffer(NioSocketChannel* channel);
    void doRegister(NioSocketChannel* channel, ChannelFuture* future);

    int32_t bossId_;
    int32_t id_;
    ::jlang::Executor* executor_;
    std::mutex startStopLock_;
    bool started_ = false;
    std::atomic<Selector*> selector_{nullptr};
    std::atomic<bool> wakenUp_{false};
    std::atomic<bool> hasThread_{false};
    std::atomic<pthread_t> thread_{};
    std::mutex queueLock_;
    std::deque<Runnable*> registerTaskQueue_;
    std::deque<Runnable*> writeTaskQueue_;
    int32_t registeredCount_ = 0;          // worker thread only
    ::jlang::ByteBuffer* recvBuffer_ = nullptr;  // worker thread only
};

// NioWorker.RegisterTask
class RegisterTask final : public virtual Runnable {
public:
    RegisterTask(NioWorker* worker, NioSocketChannel* channel, ChannelFuture* future)
        : worker_(worker), channel_(channel), future_(future) {}
    void run() override { worker_->doRegister(channel_, future_); }

private:
    NioWorker* worker_;
    NioSocketChannel* channel_;
    ChannelFuture* future_;
};

// Closes a channel's socket in its I/O thread (see the file comment).
class ReleaseTask final : public virtual Runnable {
public:
    ReleaseTask(NioWorker* worker, NioSocketChannel* channel) : worker_(worker), channel_(channel) {}
    void run() override { worker_->releaseSocket(channel_); }

private:
    NioWorker* worker_;
    NioSocketChannel* channel_;
};

// NioSocketChannel.WriteTask
class WriteTask final : public virtual Runnable {
public:
    explicit WriteTask(NioSocketChannel* channel) : channel_(channel) {}
    void run() override {
        channel_->writeTaskInTaskQueue.store(false);
        channel_->worker->writeFromTaskLoop(channel_);
    }

private:
    NioSocketChannel* channel_;
};

NioSocketChannel::NioSocketChannel(ChannelFactory* factory, ChannelPipeline* pipeline, Channel* parent,
                                   ChannelSink* sink, ::jlang::SocketChannel* socket, NioWorker* worker,
                                   pthread_t bossThread)
    : AbstractChannel(parent, factory, pipeline, sink), socket(socket), worker(worker), bossThread(bossThread) {
    config_ = new SocketChannelConfig(socket);
    writeTask = new WriteTask(this);
    // NioAcceptedSocketChannel
    setConnected();
    Channels::fireChannelOpen(this);
    Channels::fireChannelBound(this, getLocalAddress());
    Channels::fireChannelConnected(this, getRemoteAddress());
}

void NioWorker::submit(Runnable* task, bool server) {
    Selector* selector;
    {
        std::lock_guard<std::mutex> g(startStopLock_);
        if (!started_) {
            try {
                selector = Selector::open();
            } catch (Throwable& t) {
                throw ChannelException("Failed to create a selector.", t);
            }
            selector_.store(selector);
            String threadName = ::jlang::str(server ? "New I/O server worker #" : "New I/O client worker #", bossId_,
                                             "-", id_);
            bool success = false;
            try {
                executor_->execute(new IoWorkerRunnable(this, threadName));
                success = true;
            } catch (...) {
                if (!success) {
                    try {
                        selector->close();
                    } catch (Throwable& t) {
                        logWarn("org.jboss.netty.channel.socket.nio.NioWorker", "Failed to close a selector.", &t);
                    }
                    selector_.store(nullptr);
                }
                throw;
            }
        } else {
            selector = selector_.load();
        }
        started_ = true;
        {
            std::lock_guard<std::mutex> q(queueLock_);
            registerTaskQueue_.push_back(task);
        }
        // Under startStopLock_: the worker closes its selector only while holding it.
        if (!wakenUp_.exchange(true)) selector->wakeup();
    }
}

void NioWorker::register_(NioSocketChannel* channel, ChannelFuture* future) {
    submit(new RegisterTask(this, channel, future), true);
}

void NioWorker::wakeup() {
    if (wakenUp_.exchange(true)) return;
    std::lock_guard<std::mutex> g(startStopLock_);  // the selector is closed under this lock
    Selector* s = selector_.load();
    if (s != nullptr) s->wakeup();
}

void NioWorker::run() {
    thread_.store(pthread_self());
    hasThread_.store(true);
    bool shutdown = false;
    Selector* selector = selector_.load();
    for (;;) {
        wakenUp_.store(false);
        try {
            try {
                selector->select(500);  // SelectorUtil.select
            } catch (::jlang::CancelledKeyException&) {
                // JDK bug workaround in Netty; ignore.
            }
            if (wakenUp_.load()) selector->wakeup();
            processTaskQueue(registerTaskQueue_);
            processTaskQueue(writeTaskQueue_);
            processSelectedKeys(selector);

            if (registeredCount_ == 0) {
                if (shutdown || isShutdown(executor_)) {
                    std::lock_guard<std::mutex> g(startStopLock_);
                    bool queueEmpty;
                    {
                        std::lock_guard<std::mutex> q(queueLock_);
                        queueEmpty = registerTaskQueue_.empty();
                    }
                    if (queueEmpty && registeredCount_ == 0) {
                        started_ = false;
                        hasThread_.store(false);
                        try {
                            selector->close();
                        } catch (Throwable& e) {
                            logWarn("org.jboss.netty.channel.socket.nio.NioWorker", "Failed to close a selector.", &e);
                        }
                        selector_.store(nullptr);
                        break;
                    }
                    shutdown = false;
                } else {
                    // Give one more second (one more select) before shutting down.
                    shutdown = true;
                }
            } else {
                shutdown = false;
            }
        } catch (Throwable& t) {
            logWarn("org.jboss.netty.channel.socket.nio.NioWorker", "Unexpected exception in the selector loop.", &t);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void NioWorker::processTaskQueue(std::deque<Runnable*>& queue) {
    for (;;) {
        Runnable* task;
        {
            std::lock_guard<std::mutex> q(queueLock_);
            if (queue.empty()) break;
            task = queue.front();
            queue.pop_front();
        }
        task->run();
    }
}

void NioWorker::processSelectedKeys(Selector* selector) {
    auto* selected = selector->selectedKeys();
    std::vector<SelectionKey*> keys;
    for (SelectionKey* k : *selected) keys.push_back(k);
    selected->clear();
    for (SelectionKey* k : keys) {
        auto* channel = dynamic_cast<NioSocketChannel*>(k->attachment());
        if (channel == nullptr || channel->released) continue;
        try {
            int32_t readyOps = k->readyOps();
            if ((readyOps & SelectionKey::OP_READ) != 0 || readyOps == 0) {
                if (!read(channel)) continue;
            }
            if ((readyOps & SelectionKey::OP_WRITE) != 0) writeFromSelectorLoop(channel);
        } catch (::jlang::CancelledKeyException&) {
            close(channel, Channels::succeededFuture(channel));
        }
    }
}

void NioWorker::doRegister(NioSocketChannel* channel, ChannelFuture* future) {
    InetSocketAddress* localAddress = channel->getLocalAddress();
    InetSocketAddress* remoteAddress = channel->getRemoteAddress();
    if (localAddress == nullptr || remoteAddress == nullptr || !channel->isOpen()) {
        if (future != nullptr) future->setFailure(new ::jlang::ClosedChannelException());
        close(channel, Channels::succeededFuture(channel));
        releaseSocket(channel);
        return;
    }
    try {
        channel->socket->configureBlocking(false);
        {
            std::lock_guard<std::mutex> g(channel->interestOpsLock);
            int32_t ops = channel->getRawInterestOps();
            if (channel->writeSuspended) ops |= Channel::OP_WRITE;  // a write started before registration
            channel->key = channel->socket->register_(selector_.load(), ops, channel);
        }
        channel->registered = true;
        registeredCount_++;
        if (future != nullptr) {
            channel->setConnected();
            future->setSuccess();
        }
    } catch (::jlang::IOException& e) {
        if (future != nullptr) future->setFailure(e.copyThrowable());
        close(channel, Channels::succeededFuture(channel));
        releaseSocket(channel);
        if (dynamic_cast<::jlang::ClosedChannelException*>(&e) == nullptr) {
            throw ChannelException("Failed to register a socket to the selector.", e);
        }
    }
}

bool NioWorker::read(NioSocketChannel* channel) {
    int32_t predictedRecvBufSize = channel->predictor.nextReceiveBufferSize();
    int32_t ret = 0;
    int32_t readBytes = 0;
    bool failure = true;

    if (recvBuffer_ == nullptr || recvBuffer_->capacity() < predictedRecvBufSize) {
        recvBuffer_ = ::jlang::ByteBuffer::allocate(std::max(predictedRecvBufSize, 1024));
    }
    ::jlang::ByteBuffer* bb = recvBuffer_;
    bb->clear();
    bb->limit(predictedRecvBufSize);
    try {
        while ((ret = channel->socket->read(bb)) > 0) {
            readBytes += ret;
            if (!bb->hasRemaining()) break;
        }
        failure = false;
    } catch (::jlang::ClosedChannelException&) {
        // Can happen, and does not need a user attention.
    } catch (Throwable& t) {
        Channels::fireExceptionCaught(channel, t.copyThrowable());
    }

    if (readBytes > 0) {
        bb->flip();
        ChannelBufferFactory* bufferFactory = channel->getConfig()->getBufferFactory();
        ChannelBuffer* buffer = bufferFactory->getBuffer(bufferFactory->getDefaultOrder(), readBytes);
        std::memcpy(buffer->rawBytes(0, readBytes), bb->rawBase(), static_cast<size_t>(readBytes));
        buffer->writerIndex(readBytes);
        channel->predictor.previousReceiveBufferSize(readBytes);
        Channels::fireMessageReceived(channel, buffer);
    }

    if (ret < 0 || failure) {
        close(channel, Channels::succeededFuture(channel));
        return false;
    }
    return true;
}

void NioWorker::writeFromUserCode(NioSocketChannel* channel) {
    if (!channel->isConnected()) {
        cleanUpWriteBuffer(channel);
        return;
    }
    if (scheduleWriteIfNecessary(channel)) return;
    // From here, we are sure Thread.currentThread() == workerThread.
    if (channel->writeSuspended) return;
    if (channel->inWriteNowLoop) return;
    write0(channel);
}

bool NioWorker::scheduleWriteIfNecessary(NioSocketChannel* channel) {
    if (inWorkerThread()) return false;
    if (!channel->writeTaskInTaskQueue.exchange(true)) {
        std::lock_guard<std::mutex> q(queueLock_);
        writeTaskQueue_.push_back(channel->writeTask);
    }
    // Netty skips the wakeup when called from the boss thread (the registration wakes the
    // worker up anyway); waking up is always correct.
    wakeup();
    return true;
}

void NioWorker::write0(NioSocketChannel* channel) {
    bool open = true;
    bool addOpWrite = false;
    bool removeOpWrite = false;
    int64_t writtenBytes = 0;
    ::jlang::SocketChannel* ch = channel->socket;
    const int32_t writeSpinCount = channel->socketConfig()->getWriteSpinCount();
    {
        std::lock_guard<std::recursive_mutex> g(channel->writeLock);
        channel->inWriteNowLoop = true;
        for (;;) {
            MessageEvent* evt = channel->currentWriteEvent;
            SendBuffer* buf;
            if (evt == nullptr) {
                if ((channel->currentWriteEvent = evt = channel->pollWrite()) == nullptr) {
                    removeOpWrite = true;
                    channel->writeSuspended = false;
                    break;
                }
                auto* msg = dynamic_cast<ChannelBuffer*>(evt->getMessage());
                if (msg == nullptr) {
                    // SocketSendBufferPool.acquire: IllegalArgumentException("unsupported message type")
                    channel->currentWriteEvent = nullptr;
                    Throwable* t = new ::jlang::IllegalArgumentException(
                        ::jlang::str("unsupported message type: ", evt->getMessage()->getClass()));
                    evt->getFuture()->setFailure(t);
                    Channels::fireExceptionCaught(channel, t);
                    continue;
                }
                channel->currentWriteBuffer = buf = new SendBuffer(msg);
            } else {
                buf = channel->currentWriteBuffer;
            }

            ChannelFuture* future = evt->getFuture();
            try {
                int64_t localWrittenBytes = 0;
                for (int32_t i = writeSpinCount; i > 0; i--) {
                    localWrittenBytes = buf->transferTo(ch);
                    if (localWrittenBytes != 0) {
                        writtenBytes += localWrittenBytes;
                        break;
                    }
                }
                if (buf->finished()) {
                    // Successful write - proceed to the next message.
                    channel->currentWriteEvent = nullptr;
                    channel->currentWriteBuffer = nullptr;
                    future->setSuccess();
                } else {
                    // Not written fully - perhaps the kernel buffer is full.
                    addOpWrite = true;
                    channel->writeSuspended = true;
                    if (localWrittenBytes > 0) {
                        future->setProgress(localWrittenBytes, buf->writtenBytes(), buf->totalBytes());
                    }
                    break;
                }
            } catch (Throwable& t) {
                channel->currentWriteEvent = nullptr;
                channel->currentWriteBuffer = nullptr;
                Throwable* cause = t.copyThrowable();
                future->setFailure(cause);
                Channels::fireExceptionCaught(channel, cause);
                if (dynamic_cast<::jlang::IOException*>(&t) != nullptr) {
                    open = false;
                    close(channel, Channels::succeededFuture(channel));
                }
            }
        }
        channel->inWriteNowLoop = false;
    }

    Channels::fireWriteComplete(channel, writtenBytes);

    if (open) {
        if (addOpWrite) {
            setOpWrite(channel);
        } else if (removeOpWrite) {
            clearOpWrite(channel);
        }
    }
}

void NioWorker::setOpWrite(NioSocketChannel* channel) {
    SelectionKey* key = channel->key;
    if (key == nullptr || !inWorkerThread()) return;
    if (!key->isValid()) {
        close(channel, Channels::succeededFuture(channel));
        return;
    }
    int32_t interestOps;
    bool changed = false;
    {
        std::lock_guard<std::mutex> g(channel->interestOpsLock);
        interestOps = channel->getRawInterestOps();
        if ((interestOps & SelectionKey::OP_WRITE) == 0) {
            interestOps |= SelectionKey::OP_WRITE;
            key->interestOps(interestOps);
            changed = true;
        }
    }
    if (changed) channel->setRawInterestOpsNow(interestOps);
}

void NioWorker::clearOpWrite(NioSocketChannel* channel) {
    SelectionKey* key = channel->key;
    if (key == nullptr || !inWorkerThread()) return;
    if (!key->isValid()) {
        close(channel, Channels::succeededFuture(channel));
        return;
    }
    int32_t interestOps;
    bool changed = false;
    {
        std::lock_guard<std::mutex> g(channel->interestOpsLock);
        interestOps = channel->getRawInterestOps();
        if ((interestOps & SelectionKey::OP_WRITE) != 0) {
            interestOps &= ~SelectionKey::OP_WRITE;
            key->interestOps(interestOps);
            changed = true;
        }
    }
    if (changed) channel->setRawInterestOpsNow(interestOps);
}

void NioWorker::closeSocket(NioSocketChannel* channel) {
    if (inWorkerThread()) {
        releaseSocket(channel);
        return;
    }
    try {
        submit(new ReleaseTask(this, channel), true);
    } catch (Throwable& t) {
        // The I/O thread cannot be started (executor shut down): close the socket here.
        logWarn("org.jboss.netty.channel.socket.nio.NioWorker", "Failed to schedule a socket close.", &t);
        try {
            channel->socket->close();
        } catch (Throwable&) {
        }
    }
}

void NioWorker::releaseSocket(NioSocketChannel* channel) {
    if (channel->released) return;
    channel->released = true;
    {
        std::lock_guard<std::mutex> g(channel->interestOpsLock);
        if (channel->key != nullptr) channel->key->cancel();
    }
    try {
        channel->socket->close();
    } catch (Throwable& t) {
        logWarn("org.jboss.netty.channel.socket.nio.NioWorker", "Failed to close a socket.", &t);
    }
    if (channel->registered) {
        channel->registered = false;
        registeredCount_--;
    }
}

void NioWorker::close(NioSocketChannel* channel, ChannelFuture* future) {
    bool connected = channel->isConnected();
    bool bound = channel->isBound();
    try {
        if (channel->setClosed()) {
            closeSocket(channel);
            future->setSuccess();
            if (connected) Channels::fireChannelDisconnected(channel);
            if (bound) Channels::fireChannelUnbound(channel);
            cleanUpWriteBuffer(channel);
            Channels::fireChannelClosed(channel);
        } else {
            future->setSuccess();
        }
    } catch (Throwable& t) {
        Throwable* cause = t.copyThrowable();
        future->setFailure(cause);
        Channels::fireExceptionCaught(channel, cause);
    }
}

void NioWorker::cleanUpWriteBuffer(NioSocketChannel* channel) {
    Throwable* cause = nullptr;
    bool fireExceptionCaught = false;
    {
        // Create the exception only once to avoid the excessive overhead caused by
        // fillStackTrace.
        std::lock_guard<std::recursive_mutex> g(channel->writeLock);
        MessageEvent* evt = channel->currentWriteEvent;
        if (evt != nullptr) {
            if (channel->isOpen()) {
                cause = new NotYetConnectedException();
            } else {
                cause = new ::jlang::ClosedChannelException();
            }
            ChannelFuture* future = evt->getFuture();
            channel->currentWriteEvent = nullptr;
            channel->currentWriteBuffer = nullptr;
            future->setFailure(cause);
            fireExceptionCaught = true;
        }
        if (!channel->writeBufferEmpty()) {
            if (cause == nullptr) {
                if (channel->isOpen()) {
                    cause = new NotYetConnectedException();
                } else {
                    cause = new ::jlang::ClosedChannelException();
                }
            }
            for (;;) {
                evt = channel->pollWrite();
                if (evt == nullptr) break;
                evt->getFuture()->setFailure(cause);
                fireExceptionCaught = true;
            }
        }
    }
    if (fireExceptionCaught) Channels::fireExceptionCaught(channel, cause);
}

void NioWorker::setInterestOps(NioSocketChannel* channel, ChannelFuture* future, int32_t interestOps) {
    bool changed = false;
    try {
        {
            std::lock_guard<std::mutex> g(channel->interestOpsLock);
            SelectionKey* key = channel->key;
            if (key == nullptr || selector_.load() == nullptr) {
                // Not registered to the worker yet: set the value so that the RegisterTask
                // uses it. (Netty 3.2 leaves the future incomplete here.)
                channel->setRawInterestOpsNow(interestOps);
                return;
            }
            // Override OP_WRITE flag - a user cannot change this flag.
            interestOps &= ~Channel::OP_WRITE;
            interestOps |= channel->getRawInterestOps() & Channel::OP_WRITE;
            if (channel->getRawInterestOps() != interestOps) {
                key->interestOps(interestOps);  // thread-safe, effective immediately
                changed = true;
            }
        }
        future->setSuccess();
        if (changed) {
            channel->setRawInterestOpsNow(interestOps);
            Channels::fireChannelInterestChanged(channel);
        }
    } catch (::jlang::CancelledKeyException&) {
        // setInterestOps() was called on a closed channel.
        Throwable* cce = new ::jlang::ClosedChannelException();
        future->setFailure(cce);
        Channels::fireExceptionCaught(channel, cce);
    } catch (Throwable& t) {
        Throwable* cause = t.copyThrowable();
        future->setFailure(cause);
        Channels::fireExceptionCaught(channel, cause);
    }
}

// =======================================================================================
// NioServerSocketPipelineSink
// =======================================================================================
namespace {
std::atomic<int32_t> g_nextSinkId{0};
}

class NioServerSocketPipelineSink final : public virtual ChannelSink {
public:
    NioServerSocketPipelineSink(::jlang::Executor* workerExecutor, int32_t workerCount) : id_(++g_nextSinkId) {
        for (int32_t i = 0; i < workerCount; i++) workers_.push_back(new NioWorker(id_, i + 1, workerExecutor));
    }

    void eventSunk(ChannelPipeline*, ChannelEvent* e) override {
        Channel* channel = e->getChannel();
        if (auto* server = dynamic_cast<NioServerSocketChannel*>(channel)) {
            handleServerSocket(server, e);
        } else if (auto* accepted = dynamic_cast<NioSocketChannel*>(channel)) {
            handleAcceptedSocket(accepted, e);
        }
    }

    NioWorker* nextWorker() {
        int32_t n = static_cast<int32_t>(workers_.size());
        int32_t i = workerIndex_++ % n;
        return workers_[static_cast<size_t>(i < 0 ? -i : i)];
    }

    void registerAcceptedChannel(NioServerSocketChannel* channel, ::jlang::SocketChannel* acceptedSocket,
                                 pthread_t currentThread) {
        try {
            ChannelPipeline* pipeline = channel->getConfig()->getPipelineFactory()->getPipeline();
            NioWorker* worker = nextWorker();
            worker->register_(new NioSocketChannel(channel->getFactory(), pipeline, channel, this, acceptedSocket, worker,
                                                   currentThread),
                              nullptr);
        } catch (::jlang::Exception& e) {
            logWarn("org.jboss.netty.channel.socket.nio.NioServerSocketPipelineSink",
                    "Failed to initialize an accepted socket.", &e);
            try {
                acceptedSocket->close();
            } catch (::jlang::IOException& e2) {
                logWarn("org.jboss.netty.channel.socket.nio.NioServerSocketPipelineSink",
                        "Failed to close a partially accepted socket.", &e2);
            }
        }
    }

    int32_t id() { return id_; }

private:
    void handleServerSocket(NioServerSocketChannel* channel, ChannelEvent* e) {
        auto* event = dynamic_cast<ChannelStateEvent*>(e);
        if (event == nullptr) return;
        ChannelFuture* future = event->getFuture();
        Object* value = event->getValue();
        switch (event->getState()) {
        case ChannelState::OPEN:
            if (isFalse(value)) close(channel, future);
            break;
        case ChannelState::BOUND:
            if (value != nullptr) {
                bind(channel, future, ::jlang::cast<InetSocketAddress>(value));
            } else {
                close(channel, future);
            }
            break;
        default: break;
        }
    }

    void handleAcceptedSocket(NioSocketChannel* channel, ChannelEvent* e) {
        if (auto* event = dynamic_cast<ChannelStateEvent*>(e)) {
            ChannelFuture* future = event->getFuture();
            Object* value = event->getValue();
            switch (event->getState()) {
            case ChannelState::OPEN:
                if (isFalse(value)) channel->worker->close(channel, future);
                break;
            case ChannelState::BOUND:
            case ChannelState::CONNECTED:
                if (value == nullptr) channel->worker->close(channel, future);
                break;
            case ChannelState::INTEREST_OPS: channel->worker->setInterestOps(channel, future, toInt(value)); break;
            default: break;
            }
        } else if (auto* event = dynamic_cast<MessageEvent*>(e)) {
            channel->offerWrite(event);
            channel->worker->writeFromUserCode(channel);
        }
    }

    void bind(NioServerSocketChannel* channel, ChannelFuture* future, InetSocketAddress* localAddress);
    void close(NioServerSocketChannel* channel, ChannelFuture* future);

    int32_t id_;
    std::vector<NioWorker*> workers_;
    std::atomic<int32_t> workerIndex_{0};
};

// NioServerSocketPipelineSink.Boss
class Boss final : public virtual Runnable {
public:
    Boss(NioServerSocketPipelineSink* sink, NioServerSocketChannel* channel) : sink_(sink), channel_(channel) {
        selector_ = Selector::open();
        bool registered = false;
        try {
            channel->socket->register_(selector_, SelectionKey::OP_ACCEPT, nullptr);
            registered = true;
        } catch (...) {
            if (!registered) closeSelector();
            throw;
        }
        channel->selector.store(selector_);
    }

    void run() override {
        pthread_t currentThread = pthread_self();
        std::lock_guard<std::recursive_mutex> shutdownGuard(channel_->shutdownLock);
        for (;;) {
            if (channel_->closeRequested.load()) break;
            try {
                if (selector_->select(1000) > 0) selector_->selectedKeys()->clear();
                if (channel_->closeRequested.load()) break;
                ::jlang::SocketChannel* acceptedSocket = channel_->socket->accept();
                if (acceptedSocket != nullptr) sink_->registerAcceptedChannel(channel_, acceptedSocket, currentThread);
            } catch (::jlang::SocketTimeoutException&) {
                // Thrown every second to get ClosedChannelException raised.
            } catch (::jlang::ClosedChannelException&) {
                // Closed as requested.
                break;
            } catch (::jlang::IllegalStateException&) {
                // CancelledKeyException / ClosedSelectorException: raised when the server
                // socket was closed.
            } catch (::jlang::IOException& e) {
                logWarn("org.jboss.netty.channel.socket.nio.NioServerSocketPipelineSink",
                        "Failed to accept a connection.", &e);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        closeSelector();
    }

private:
    void closeSelector() {
        channel_->selector.store(nullptr);
        try {
            selector_->close();
        } catch (::jlang::Exception& e) {
            logWarn("org.jboss.netty.channel.socket.nio.NioServerSocketPipelineSink", "Failed to close a selector.", &e);
        }
    }

    NioServerSocketPipelineSink* sink_;
    NioServerSocketChannel* channel_;
    Selector* selector_ = nullptr;
};

void NioServerSocketPipelineSink::bind(NioServerSocketChannel* channel, ChannelFuture* future,
                                       InetSocketAddress* localAddress) {
    bool bound = false;
    bool bossStarted = false;
    try {
        channel->socket->socket()->bind(localAddress, channel->serverConfig()->getBacklog());
        bound = true;
        channel->bound.store(true);

        future->setSuccess();
        Channels::fireChannelBound(channel, channel->getLocalAddress());

        auto* factory = dynamic_cast<NioServerSocketChannelFactory*>(channel->getFactory());
        ::jlang::Executor* bossExecutor = factory->bossExecutor();
        bossExecutor->execute(new IoWorkerRunnable(
            new Boss(this, channel), ::jlang::str("New I/O server boss #", id_, " (channelId: ", channel->getId(), ", ",
                                                  channel->getLocalAddress(), ")")));
        bossStarted = true;
    } catch (Throwable& t) {
        Throwable* cause = t.copyThrowable();
        future->setFailure(cause);
        Channels::fireExceptionCaught(channel, cause);
    }
    if (!bossStarted && bound) close(channel, future);
}

void NioServerSocketPipelineSink::close(NioServerSocketChannel* channel, ChannelFuture* future) {
    bool bound = channel->isBound();
    try {
        // Ask the boss thread to stop (Netty closes the socket first; see the file comment).
        channel->closeRequested.store(true);
        if (Selector* selector = channel->selector.load()) selector->wakeup();
        std::lock_guard<std::recursive_mutex> shutdownGuard(channel->shutdownLock);
        if (channel->socket->isOpen()) channel->socket->close();
        if (channel->setClosed()) {
            future->setSuccess();
            if (bound) Channels::fireChannelUnbound(channel);
            Channels::fireChannelClosed(channel);
        } else {
            future->setSuccess();
        }
    } catch (Throwable& t) {
        Throwable* cause = t.copyThrowable();
        future->setFailure(cause);
        Channels::fireExceptionCaught(channel, cause);
    }
}

}  // namespace detail

// =======================================================================================
// NioServerSocketChannelFactory
// =======================================================================================

NioServerSocketChannelFactory::NioServerSocketChannelFactory(::jlang::Executor* bossExecutor,
                                                             ::jlang::Executor* workerExecutor)
    : NioServerSocketChannelFactory(bossExecutor, workerExecutor,
                                    ::jlang::Runtime::getRuntime()->availableProcessors() * 2) {}

NioServerSocketChannelFactory::NioServerSocketChannelFactory(::jlang::Executor* bossExecutor,
                                                             ::jlang::Executor* workerExecutor, int32_t workerCount) {
    if (bossExecutor == nullptr) throw ::jlang::NullPointerException("bossExecutor");
    if (workerExecutor == nullptr) throw ::jlang::NullPointerException("workerExecutor");
    if (workerCount <= 0) {
        throw ::jlang::IllegalArgumentException(
            ::jlang::str("workerCount (", workerCount, ") must be a positive integer."));
    }
    bossExecutor_ = bossExecutor;
    workerExecutor_ = workerExecutor;
    sink_ = new detail::NioServerSocketPipelineSink(workerExecutor, workerCount);
}

Channel* NioServerSocketChannelFactory::newChannel(ChannelPipeline* pipeline) {
    return new detail::NioServerSocketChannel(this, pipeline, sink_);
}

void NioServerSocketChannelFactory::releaseExternalResources() {
    detail::terminateExecutors({bossExecutor_, workerExecutor_});
}

}  // namespace jlang::netty
