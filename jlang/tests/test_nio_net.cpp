// Tests for the java.net / java.nio.channels part of <jlang/Nio.h> over real sockets on
// 127.0.0.1: an echo server written with the commons Dispatcher idiom (select, iterate
// selectedKeys with iterator()->remove(), switch on readyOps(), interestOps toggling), many
// concurrent blocking clients, partial writes of large buffers, remote close (-1), wakeup
// from other threads (also before select), registration from another thread while select()
// blocks, cancel/keys() bookkeeping and address formatting.
#include "jtest.h"

#include <jlang/Collections.h>
#include <jlang/Nio.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace jlang;

namespace {

// A GC-registered native thread running a std::function (jlang::gc helper).
class TestThread : public virtual Object {
public:
    explicit TestThread(std::function<void()> f) : f_(std::move(f)) {}
    void start() { handle_ = gc::startNativeThread(&TestThread::trampoline, this); }
    void join() { gc::joinNativeThread(handle_); }

private:
    static void trampoline(void* arg) {
        auto* t = static_cast<TestThread*>(arg);
        try {
            t->f_();
        } catch (Throwable& e) {
            std::fprintf(stderr, "thread failed: %s\n", e.what());
            failures().fetch_add(1);
        }
    }

public:
    static std::atomic<int>& failures() {
        static std::atomic<int> f{0};
        return f;
    }

private:
    std::function<void()> f_;
    uint64_t handle_ = 0;
};

// Per-connection state of the echo server (the "AConnection").
class EchoConn : public virtual Object {
public:
    ByteBuffer* readBuf = ByteBuffer::allocate(8192);
    std::string pending;  // bytes to echo back
    ByteBuffer* writeBuf = ByteBuffer::allocate(16384)->flip();  // empty, like AConnection.writeBuffer
    int64_t echoed = 0;
};

class EchoServer : public virtual Object {
public:
    ServerSocketChannel* ssc;
    Selector* sel;
    std::atomic<bool> running{true};
    std::atomic<int> accepted{0};
    std::atomic<int> closedByPeer{0};
    std::atomic<int64_t> partialWrites{0};
    Object* gate = new Object();

    EchoServer() {
        ssc = ServerSocketChannel::open();
        ssc->configureBlocking(false);
        ssc->socket()->bind(new InetSocketAddress(String("127.0.0.1"), 0));
        sel = SelectorProvider::provider()->openSelector();
        ssc->register_(sel, SelectionKey::OP_ACCEPT);
    }
    int32_t port() { return ssc->socket()->getLocalPort(); }

    void loop() {
        while (running.load()) {
            int32_t selected = sel->select();
            JSYNC(gate) {}
            if (selected == 0) continue;
            Iterator<SelectionKey*>* it = sel->selectedKeys()->iterator();
            while (it->hasNext()) {
                SelectionKey* key = it->next();
                it->remove();
                if (!key->isValid()) continue;
                switch (key->readyOps()) {
                    case SelectionKey::OP_ACCEPT:
                        accept(key);
                        break;
                    case SelectionKey::OP_READ:
                        read(key);
                        break;
                    case SelectionKey::OP_WRITE:
                        write(key);
                        break;
                    case SelectionKey::OP_READ | SelectionKey::OP_WRITE:
                        read(key);
                        if (key->isValid()) write(key);
                        break;
                }
            }
        }
    }

    void accept(SelectionKey* key) {
        auto* server = cast<ServerSocketChannel>(key->channel());
        SocketChannel* sc = server->accept();
        if (sc == nullptr) return;
        sc->configureBlocking(false);
        sc->socket()->setTcpNoDelay(true);
        sc->register_(sel, SelectionKey::OP_READ, new EchoConn());
        accepted++;
    }

    void closeConn(SelectionKey* key) {
        auto* sc = cast<SocketChannel>(key->channel());
        sc->close();
        key->attach(nullptr);
        key->cancel();
    }

    void read(SelectionKey* key) {
        auto* sc = cast<SocketChannel>(key->channel());
        auto* con = cast<EchoConn>(key->attachment());
        ByteBuffer* rb = con->readBuf;
        int32_t n;
        try {
            n = sc->read(rb);
        } catch (IOException&) {
            closeConn(key);
            return;
        }
        if (n == -1) {
            closedByPeer++;
            closeConn(key);
            return;
        }
        if (n == 0) return;
        rb->flip();
        con->pending.append(reinterpret_cast<const char*>(rb->rawBase() + rb->position()), static_cast<size_t>(rb->remaining()));
        rb->clear();
        key->interestOps(key->interestOps() | SelectionKey::OP_WRITE);
    }

    void write(SelectionKey* key) {
        auto* sc = cast<SocketChannel>(key->channel());
        auto* con = cast<EchoConn>(key->attachment());
        ByteBuffer* wb = con->writeBuf;
        for (;;) {
            if (!wb->hasRemaining()) {
                if (con->pending.empty()) break;
                wb->clear();
                size_t n = con->pending.size() < static_cast<size_t>(wb->capacity()) ? con->pending.size() : static_cast<size_t>(wb->capacity());
                auto* chunk = new Array<int8_t>(static_cast<int32_t>(n));
                std::memcpy(chunk->data(), con->pending.data(), n);
                con->pending.erase(0, n);
                wb->put(chunk);
                wb->flip();
            }
            int32_t w;
            try {
                w = sc->write(wb);
            } catch (IOException&) {
                closeConn(key);
                return;
            }
            con->echoed += w;
            if (wb->hasRemaining()) {
                partialWrites++;
                return;  // socket buffer full: keep OP_WRITE
            }
        }
        key->interestOps(key->interestOps() & ~SelectionKey::OP_WRITE);
    }

    void stop() {
        running.store(false);
        sel->wakeup();
    }
};

std::string pattern(int client, int len) {
    std::string s(static_cast<size_t>(len), '\0');
    for (int i = 0; i < len; i++) s[static_cast<size_t>(i)] = static_cast<char>((client * 131 + i * 7) & 0xff);
    return s;
}

void clientRoundTrip(int32_t port, int client, int len) {
    SocketChannel* sc = SocketChannel::open(new InetSocketAddress(String("127.0.0.1"), port));
    if (!sc->isConnected() || !sc->isBlocking()) throw IllegalStateException(String("client not connected/blocking"));
    std::string data = pattern(client, len);
    auto* arr = new Array<int8_t>(len);
    std::memcpy(arr->data(), data.data(), static_cast<size_t>(len));
    ByteBuffer* out = ByteBuffer::wrap(arr);
    std::string got;
    ByteBuffer* in = ByteBuffer::allocate(4096);
    // interleave writes and reads so large payloads do not deadlock on full buffers
    sc->configureBlocking(false);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (static_cast<int>(got.size()) < len) {
        if (out->hasRemaining()) sc->write(out);
        int32_t n = sc->read(in);
        if (n < 0) throw IOException(String("unexpected EOF"));
        if (n > 0) {
            in->flip();
            got.append(reinterpret_cast<const char*>(in->rawBase()), static_cast<size_t>(in->remaining()));
            in->clear();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        if (std::chrono::steady_clock::now() > deadline) throw IOException(String("timeout"));
    }
    if (got != data) throw IllegalStateException(str("echo mismatch for client ", client));
    sc->close();
    if (sc->isOpen()) throw IllegalStateException(String("still open"));
}

}  // namespace

JTEST(NetAddresses) {
    InetAddress* a = InetAddress::getByName(String("127.0.0.1"));
    JCHECK_EQ(a->getHostAddress(), String("127.0.0.1"));
    JCHECK_EQ(a->toString(), String("/127.0.0.1"));
    JCHECK_EQ(a->hashCode(), 0x7f000001);
    Array<int8_t>* raw = a->getAddress();
    JCHECK_EQ(raw->length, 4);
    JCHECK_EQ((*raw)[0], 127);
    JCHECK(a->isLoopbackAddress());
    JCHECK(a->equals(InetAddress::getByAddress(Array<int8_t>::of({127, 0, 0, 1}))));
    InetAddress* lh = InetAddress::getByName(String("localhost"));
    JCHECK(lh->toString().startsWith(String("localhost/")));
    InetAddress* v6 = InetAddress::getByName(String("::1"));
    JCHECK_EQ(v6->getHostAddress(), String("0:0:0:0:0:0:0:1"));
    JCHECK_EQ(v6->getAddress()->length, 16);
    JCHECK_EQ(InetAddress::getByName(String("[::ffff:10.1.2.3]"))->getHostAddress(), String("10.1.2.3"));
    JCHECK(InetAddress::getByName(String("10.1.2.3"))->isSiteLocalAddress());
    JCHECK_EQ(InetAddress::getByName(String())->getHostAddress(), String("127.0.0.1"));
    JCHECK_THROWS(UnknownHostException, InetAddress::getByName(String("[::1")));

    auto* any = new InetSocketAddress(7777);
    JCHECK_EQ(any->toString(), String("0.0.0.0/0.0.0.0:7777"));
    JCHECK(any->getAddress()->isAnyLocalAddress());
    auto* isa = new InetSocketAddress(String("127.0.0.1"), 2106);
    JCHECK_EQ(isa->toString(), String("/127.0.0.1:2106"));
    JCHECK_EQ(isa->getPort(), 2106);
    JCHECK(!isa->isUnresolved());
    JCHECK_EQ(isa->getHostName(), String("127.0.0.1"));
    JCHECK(isa->equals(new InetSocketAddress(InetAddress::getByName(String("127.0.0.1")), 2106)));
    JCHECK_EQ(isa->hashCode(), 0x7f000001 + 2106);
    JCHECK_THROWS(IllegalArgumentException, new InetSocketAddress(70000));
    auto* un = InetSocketAddress::createUnresolved(String("somehost"), 1);
    JCHECK(un->isUnresolved());
    JCHECK_EQ(un->toString(), String("somehost:1"));
    JCHECK_THROWS(UnresolvedAddressException, SocketChannel::open(un));
}

JTEST(NetEchoServerManyClients) {
    auto* server = new EchoServer();
    int32_t port = server->port();
    JCHECK(port > 0);
    JCHECK(server->ssc->socket()->toString().startsWith(String("ServerSocket[addr=/127.0.0.1,localport=")));
    auto* st = new TestThread([server]() { server->loop(); });
    st->start();
    TestThread::failures().store(0);
    std::vector<TestThread*> clients;
    const int kClients = 40;
    for (int c = 0; c < kClients; c++) {
        int len = (c % 4 == 0) ? 3 * 1024 * 1024 + c : 1000 + c * 37;  // some large payloads force partial writes
        auto* t = new TestThread([port, c, len]() { clientRoundTrip(port, c, len); });
        clients.push_back(t);
        t->start();
    }
    for (TestThread* t : clients) t->join();
    JCHECK_EQ(TestThread::failures().load(), 0);
    JCHECK_EQ(server->accepted.load(), kClients);
    // remote close is observed as read() == -1 and the keys get deregistered
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (server->closedByPeer.load() < kClients && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    JCHECK_EQ(server->closedByPeer.load(), kClients);
    // cancelled keys are deregistered by the next selection operation
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (server->sel->keys()->size() != 1 && std::chrono::steady_clock::now() < deadline) {
        server->sel->wakeup();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    JCHECK_EQ(server->sel->keys()->size(), 1);  // only the server channel
    server->stop();
    st->join();
    server->sel->close();
    server->ssc->close();
    JCHECK(!server->sel->isOpen());
    JCHECK_THROWS(ClosedSelectorException, server->sel->select());
    JCHECK_THROWS(ClosedChannelException, server->ssc->accept());
}

JTEST(NetSelectorWakeupAndRegistration) {
    Selector* sel = Selector::open();
    // wakeup before select: the next select returns immediately
    sel->wakeup();
    auto t0 = std::chrono::steady_clock::now();
    JCHECK_EQ(sel->select(5000), 0);
    JCHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    // selectNow clears a pending wakeup
    sel->wakeup();
    JCHECK_EQ(sel->selectNow(), 0);
    t0 = std::chrono::steady_clock::now();
    JCHECK_EQ(sel->select(100), 0);
    JCHECK(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(90));
    JCHECK_THROWS(IllegalArgumentException, sel->select(-1));
    // wakeup from another thread while select() blocks
    auto* waker = new TestThread([sel]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        sel->wakeup();
    });
    t0 = std::chrono::steady_clock::now();
    waker->start();
    JCHECK_EQ(sel->select(), 0);
    JCHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5));
    waker->join();

    // registration from another thread while select() blocks (Dispatcher.register idiom)
    ServerSocketChannel* ssc = ServerSocketChannel::open();
    ssc->bind(new InetSocketAddress(String("127.0.0.1"), 0));
    int32_t port = ssc->socket()->getLocalPort();
    ssc->configureBlocking(false);
    Object* gate = new Object();
    std::atomic<int> accepts{0};
    std::atomic<bool> stop{false};
    auto* loop = new TestThread([&]() {
        while (!stop.load()) {
            sel->select();
            JSYNC(gate) {}
            Iterator<SelectionKey*>* it = sel->selectedKeys()->iterator();
            while (it->hasNext()) {
                SelectionKey* k = it->next();
                it->remove();
                if (k->isValid() && k->isAcceptable()) {
                    SocketChannel* c = cast<ServerSocketChannel>(k->channel())->accept();
                    if (c != nullptr) {
                        accepts++;
                        c->close();
                    }
                }
            }
        }
    });
    loop->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SelectionKey* key;
    JSYNC(gate) {
        sel->wakeup();
        key = ssc->register_(sel, SelectionKey::OP_ACCEPT, gate);
    }
    JCHECK(key->attachment() == gate);
    JCHECK(ssc->keyFor(sel) == key);
    JCHECK(ssc->isRegistered());
    JCHECK_THROWS(IllegalBlockingModeException, ssc->configureBlocking(true));
    JCHECK_THROWS(IllegalArgumentException, key->interestOps(SelectionKey::OP_READ));
    SocketChannel* c1 = SocketChannel::open(new InetSocketAddress(String("127.0.0.1"), port));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (accepts.load() < 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    JCHECK_EQ(accepts.load(), 1);
    // the accepted side was closed: the client sees EOF
    ByteBuffer* bb = ByteBuffer::allocate(16);
    JCHECK_EQ(c1->read(bb), -1);
    c1->close();
    // interestOps(0) from another thread: no more accept events
    key->interestOps(0);
    SocketChannel* c2 = SocketChannel::open(new InetSocketAddress(String("127.0.0.1"), port));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    JCHECK_EQ(accepts.load(), 1);
    key->interestOps(SelectionKey::OP_ACCEPT);
    sel->wakeup();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (accepts.load() < 2 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    JCHECK_EQ(accepts.load(), 2);
    c2->close();
    // cancel: invalid at once, deregistered at the next select
    key->cancel();
    JCHECK(!key->isValid());
    JCHECK_THROWS(CancelledKeyException, key->readyOps());
    JCHECK_THROWS(CancelledKeyException, ssc->register_(sel, SelectionKey::OP_ACCEPT));
    stop.store(true);
    sel->wakeup();
    loop->join();
    sel->selectNow();
    JCHECK(ssc->keyFor(sel) == nullptr);
    JCHECK_EQ(sel->keys()->size(), 0);
    SelectionKey* again = ssc->register_(sel, SelectionKey::OP_ACCEPT);
    JCHECK(again != key);
    ssc->close();
    JCHECK(!again->isValid());
    sel->close();
}

JTEST(NetPartialWrites) {
    ServerSocketChannel* ssc = ServerSocketChannel::open();
    ssc->bind(new InetSocketAddress(String("127.0.0.1"), 0));
    int32_t port = ssc->socket()->getLocalPort();
    SocketChannel* reader = SocketChannel::open(new InetSocketAddress(String("127.0.0.1"), port));
    SocketChannel* writer = ssc->accept();
    writer->configureBlocking(false);
    Selector* sel = Selector::open();
    SelectionKey* key = writer->register_(sel, SelectionKey::OP_WRITE);
    const int32_t total = 8 * 1024 * 1024;
    auto* data = new Array<int8_t>(total);
    for (int32_t i = 0; i < total; i++) (*data)[i] = static_cast<int8_t>(i * 31 + (i >> 11));
    ByteBuffer* out = ByteBuffer::wrap(data);
    // nobody reads yet: the first non-blocking write is partial, the next ones return 0
    int32_t first = writer->write(out);
    JCHECK(first > 0 && first < total);
    JCHECK_EQ(writer->write(out), 0);
    JCHECK_EQ(sel->selectNow(), 0);  // not writable while the send buffer is full
    // drain on another thread; the selector reports OP_WRITE again and we finish writing
    std::atomic<int64_t> got{0};
    std::atomic<bool> same{true};
    auto* t = new TestThread([&]() {
        ByteBuffer* in = ByteBuffer::allocate(65536);
        int64_t off = 0;
        for (;;) {
            int32_t n = reader->read(in);
            if (n < 0) break;
            in->flip();
            for (int32_t i = 0; i < in->remaining(); i++)
                if (in->get(i) != (*data)[static_cast<int32_t>(off + i)]) same.store(false);
            off += in->remaining();
            in->clear();
        }
        got.store(off);
    });
    t->start();
    int partials = 0;
    while (out->hasRemaining()) {
        JCHECK(sel->select(5000) >= 0);
        Iterator<SelectionKey*>* it = sel->selectedKeys()->iterator();
        while (it->hasNext()) {
            SelectionKey* k = it->next();
            it->remove();
            if (k->isWritable()) {
                writer->write(out);
                if (out->hasRemaining()) partials++;
            }
        }
    }
    JCHECK(partials > 0);
    key->interestOps(0);
    writer->close();  // reader sees EOF
    t->join();
    JCHECK_EQ(got.load(), static_cast<int64_t>(total));
    JCHECK(same.load());
    reader->close();
    ssc->close();
    sel->close();
}

JTEST(NetSocketChannelBasics) {
    ServerSocketChannel* ssc = ServerSocketChannel::open();
    ssc->socket()->bind(new InetSocketAddress(String("127.0.0.1"), 0), 10);
    int32_t port = ssc->socket()->getLocalPort();
    SocketChannel* c = SocketChannel::open();
    JCHECK(!c->isConnected());
    JCHECK_THROWS(NotYetConnectedException, c->read(ByteBuffer::allocate(1)));
    c->configureBlocking(false);
    bool now = c->connect(new InetSocketAddress(String("127.0.0.1"), port));
    SocketChannel* s = ssc->accept();  // blocking accept
    JCHECK(s != nullptr);
    if (!now) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!c->finishConnect() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    JCHECK(c->isConnected());
    JCHECK_EQ(s->socket()->getInetAddress()->getHostAddress(), String("127.0.0.1"));
    JCHECK_EQ(s->socket()->getPort(), c->socket()->getLocalPort());
    JCHECK_EQ(c->socket()->getPort(), port);
    JCHECK(s->socket()->toString().startsWith(String("Socket[addr=/127.0.0.1,port=")));
    JCHECK_EQ(c->getRemoteAddress()->getPort(), port);
    // non-blocking read with nothing available
    JCHECK_EQ(c->read(ByteBuffer::allocate(4)), 0);
    // blocking write/read
    ByteBuffer* msg = ByteBuffer::allocate(5)->put(Array<int8_t>::of({1, 2, 3, 4, 5}));
    msg->flip();
    JCHECK_EQ(s->write(msg), 5);
    ByteBuffer* in = ByteBuffer::allocate(5);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (in->hasRemaining() && std::chrono::steady_clock::now() < deadline) c->read(in);
    JCHECK_EQ(in->get(4), 5);
    JCHECK_EQ(c->read(ByteBuffer::allocate(0)), 0);
    // shutdownOutput: peer reads -1, channel stays open
    s->socket()->shutdownOutput();
    ByteBuffer* rest = ByteBuffer::allocate(4);
    int32_t r = 0;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((r = c->read(rest)) == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    JCHECK_EQ(r, -1);
    JCHECK(s->isOpen());
    s->close();
    s->close();
    JCHECK_THROWS(ClosedChannelException, s->write(ByteBuffer::allocate(1)));
    // writing to a peer that closed eventually fails with IOException (no SIGPIPE)
    bool failed = false;
    try {
        for (int i = 0; i < 100 && !failed; i++) {
            ByteBuffer* big = ByteBuffer::allocate(65536);
            c->write(big);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } catch (IOException&) {
        failed = true;
    }
    JCHECK(failed);
    c->close();
    // connection refused
    ServerSocketChannel* tmp = ServerSocketChannel::open();
    tmp->bind(new InetSocketAddress(String("127.0.0.1"), 0));
    int32_t deadPort = tmp->socket()->getLocalPort();
    tmp->close();
    JCHECK_THROWS(ConnectException, SocketChannel::open(new InetSocketAddress(String("127.0.0.1"), deadPort)));
    // bind conflict
    ServerSocketChannel* b2 = ServerSocketChannel::open();
    JCHECK_THROWS(BindException, b2->bind(new InetSocketAddress(String("127.0.0.1"), port)));
    b2->close();
    JCHECK_THROWS(NotYetBoundException, ServerSocketChannel::open()->accept());
    ssc->configureBlocking(false);
    JCHECK(ssc->accept() == nullptr);
    ssc->close();
}

JTEST(NetSelectInterrupt) {
    Selector* sel = Selector::open();
    std::atomic<sync::InterruptState*> state{nullptr};
    std::atomic<int> result{-1};
    std::atomic<bool> stillInterrupted{false};
    auto* t = new TestThread([&]() {
        state.store(sync::current());
        result.store(sel->select());  // blocks until interrupted
        stillInterrupted.store(sync::isInterrupted(sync::current()));
        sync::clearInterrupt(sync::current());
    });
    t->start();
    while (state.load() == nullptr) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sync::interrupt(state.load());
    t->join();
    JCHECK_EQ(result.load(), 0);
    JCHECK(stillInterrupted.load());  // Java: select returns, the interrupt status stays set
    sel->close();
}
