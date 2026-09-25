// jlang/NioSelector.cpp - java.nio.channels.Selector / SelectionKey / SelectorProvider on epoll.
//
// Model (JDK 11+ EPollSelectorImpl semantics, level triggered):
//   * every key has a unique 64-bit id stored in epoll_event.data (never a pointer: the kernel
//     is invisible to the collector, and ids make events of deregistered keys harmless even if
//     the descriptor number is reused);
//   * interest changes and registrations are applied to epoll immediately (thread safe);
//     a key with interest 0 is removed from the epoll set (no spurious HUP wake-ups);
//   * cancel() removes the descriptor from epoll at once and queues the key; the queue is
//     processed (key removed from keys()/selectedKeys() and from its channel) at the start
//     and at the end of every selection operation;
//   * ready sets: keys not in the selected set get readyOps = translated events (added when
//     readyOps & interestOps != 0); keys already selected accumulate readyOps; select()
//     returns the number of keys updated.
#include <jlang/Nio.h>

#include <jlang/Collections.h>
#include <jlang/Runtime.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <unordered_map>

namespace jlang {

namespace {

using KeyMap = std::unordered_map<uint64_t, SelectionKey*, std::hash<uint64_t>, std::equal_to<uint64_t>,
                                  detail::GcAllocator<std::pair<const uint64_t, SelectionKey*>>>;

inline KeyMap& keyMap(void* p) { return *static_cast<KeyMap*>(p); }

uint32_t toEvents(SelectionKey* k, int32_t ops) {
    uint32_t ev = 0;
    if (dynamic_cast<ServerSocketChannel*>(k->channel()) != nullptr) {
        if (ops & SelectionKey::OP_ACCEPT) ev |= EPOLLIN;
        return ev;
    }
    if (ops & SelectionKey::OP_READ) ev |= EPOLLIN;
    if (ops & (SelectionKey::OP_WRITE | SelectionKey::OP_CONNECT)) ev |= EPOLLOUT;
    return ev;
}

// SocketChannelImpl/ServerSocketChannelImpl.translateReadyOps
int32_t translateReady(SelectionKey* k, uint32_t events, int32_t initialOps) {
    const int32_t intOps = k->rawInterest();
    int32_t newOps = initialOps;
    if (events & (EPOLLERR | EPOLLHUP)) return intOps;
    SelectableChannel* ch = k->channel();
    if (dynamic_cast<ServerSocketChannel*>(ch) != nullptr) {
        if ((events & EPOLLIN) && (intOps & SelectionKey::OP_ACCEPT)) newOps |= SelectionKey::OP_ACCEPT;
        return newOps;
    }
    if (auto* sc = dynamic_cast<SocketChannel*>(ch)) {
        const bool connected = sc->isConnected();
        if ((events & EPOLLIN) && (intOps & SelectionKey::OP_READ) && connected) newOps |= SelectionKey::OP_READ;
        if ((events & EPOLLOUT) && (intOps & SelectionKey::OP_CONNECT) && sc->isConnectionPending())
            newOps |= SelectionKey::OP_CONNECT;
        if ((events & EPOLLOUT) && (intOps & SelectionKey::OP_WRITE) && connected) newOps |= SelectionKey::OP_WRITE;
        return newOps;
    }
    if ((events & EPOLLIN) && (intOps & SelectionKey::OP_READ)) newOps |= SelectionKey::OP_READ;
    if ((events & EPOLLOUT) && (intOps & SelectionKey::OP_WRITE)) newOps |= SelectionKey::OP_WRITE;
    return newOps;
}

}  // namespace

// =======================================================================================
// SelectionKey

SelectionKey::SelectionKey(SelectableChannel* ch, Selector* sel, uint64_t id, int32_t ops, Object* att)
    : channel_(ch), selector_(sel), id_(id), interest_(ops), attachment_(att) {}

void SelectionKey::cancel() { selector_->_cancel(this); }

int32_t SelectionKey::interestOps() {
    if (!isValid()) throw CancelledKeyException();
    return interest_.load(std::memory_order_acquire);
}

SelectionKey* SelectionKey::interestOps(int32_t ops) {
    if (!isValid()) throw CancelledKeyException();
    if ((ops & ~channel_->validOps()) != 0) throw IllegalArgumentException();
    int32_t old = interest_.exchange(ops, std::memory_order_acq_rel);
    if (old != ops) selector_->_updateInterest(this);
    return this;
}

int32_t SelectionKey::interestOpsOr(int32_t ops) {
    if (!isValid()) throw CancelledKeyException();
    if ((ops & ~channel_->validOps()) != 0) throw IllegalArgumentException();
    int32_t old = interest_.fetch_or(ops, std::memory_order_acq_rel);
    if ((old | ops) != old) selector_->_updateInterest(this);
    return old;
}

int32_t SelectionKey::interestOpsAnd(int32_t ops) {
    if (!isValid()) throw CancelledKeyException();
    int32_t old = interest_.fetch_and(ops, std::memory_order_acq_rel);
    if ((old & ops) != old) selector_->_updateInterest(this);
    return old;
}

int32_t SelectionKey::readyOps() {
    if (!isValid()) throw CancelledKeyException();
    return ready_;
}

String SelectionKey::toString() {
    std::string s = "channel=" + std::string(channel_->toString()) + ", selector=" + std::string(selector_->toString());
    if (isValid()) {
        s += ", interestOps=" + std::to_string(interest_.load()) + ", readyOps=" + std::to_string(ready_);
    } else {
        s += ", invalid";
    }
    return String(s);
}

// =======================================================================================
// Selector

Selector::Selector() {
    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0) throw IOException(str("epoll_create1: ", String(std::strerror(errno))));
    wakeupFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0) {
        int err = errno;
        ::close(epfd_);
        throw IOException(str("eventfd: ", String(std::strerror(err))));
    }
    epoll_event ev;
    std::memset(&ev, 0, sizeof ev);
    ev.events = EPOLLIN;
    ev.data.u64 = 0;  // id 0 = wakeup
    ::epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeupFd_, &ev);
    keys_ = new Set<SelectionKey*>();
    selected_ = new Set<SelectionKey*>();
    keyMap_ = new KeyMap();
}

Selector* Selector::open() { return new Selector(); }

SelectorProvider* Selector::provider() { return SelectorProvider::provider(); }

Set<SelectionKey*>* Selector::keys() {
    if (!isOpen()) throw ClosedSelectorException();
    std::lock_guard<std::mutex> g(lock_);
    auto* s = new Set<SelectionKey*>();
    for (SelectionKey* k : *keys_) s->add(k);
    return s;
}

Set<SelectionKey*>* Selector::selectedKeys() {
    if (!isOpen()) throw ClosedSelectorException();
    return selected_;
}

SelectionKey* Selector::_register(SelectableChannel* ch, int32_t ops, Object* att) {
    std::lock_guard<std::mutex> g(lock_);
    if (!isOpen()) throw ClosedSelectorException();
    SelectionKey* k = new SelectionKey(ch, this, nextId_++, ops, att);
    keyMap(keyMap_)[k->id()] = k;
    keys_->add(k);
    uint32_t events = toEvents(k, ops);
    if (events != 0) {
        epoll_event ev;
        std::memset(&ev, 0, sizeof ev);
        ev.events = events;
        ev.data.u64 = k->id();
        if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, ch->fd(), &ev) < 0) {
            int err = errno;
            keyMap(keyMap_).erase(k->id());
            keys_->remove(k);
            k->invalidate();
            throw IOException(str("epoll_ctl: ", String(std::strerror(err))));
        }
        k->registeredEvents = events;
    }
    return k;
}

void Selector::_updateInterest(SelectionKey* k) {
    std::lock_guard<std::mutex> g(lock_);
    if (!k->isValid() || !isOpen()) return;
    int fd = k->channel()->fd();
    if (fd < 0) return;
    uint32_t events = toEvents(k, k->rawInterest());
    if (events == k->registeredEvents) return;
    epoll_event ev;
    std::memset(&ev, 0, sizeof ev);
    ev.events = events;
    ev.data.u64 = k->id();
    if (events == 0) {
        ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, &ev);
    } else if (k->registeredEvents == 0) {
        ::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);
    } else {
        ::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
    }
    k->registeredEvents = events;
}

void Selector::_cancel(SelectionKey* k) {
    std::lock_guard<std::mutex> g(lock_);
    if (!k->isValid()) return;
    k->invalidate();
    if (k->registeredEvents != 0 && isOpen()) {
        int fd = k->channel()->fd();
        if (fd >= 0) {
            epoll_event ev;
            std::memset(&ev, 0, sizeof ev);
            ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, &ev);
        }
    }
    k->registeredEvents = 0;
    cancelled_.push_back(k);
}

void Selector::processDeregisterQueue() {
    std::vector<SelectionKey*> done;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (cancelled_.empty()) return;
        done.swap(cancelled_);
        for (SelectionKey* k : done) {
            keyMap(keyMap_).erase(k->id());
            keys_->remove(k);
            selected_->remove(k);
        }
    }
    for (SelectionKey* k : done) k->channel()->_removeKey(k);
}

int32_t Selector::select() { return doSelect(-1); }

int32_t Selector::select(int64_t timeout) {
    if (timeout < 0) throw IllegalArgumentException(String("Negative timeout"));
    if (timeout == 0) return doSelect(-1);
    return doSelect(timeout > 0x7fffffff ? 0x7fffffff : static_cast<int32_t>(timeout));
}

int32_t Selector::selectNow() { return doSelect(0); }

namespace {
// Thread.interrupt() of a thread blocked in select() wakes the selector (Java's
// AbstractSelector.begin()/end() interruptor).
void wakeSelector(void* arg) { static_cast<Selector*>(arg)->wakeup(); }
}  // namespace

int32_t Selector::doSelect(int32_t timeoutMs) {
    std::lock_guard<std::mutex> sg(selectLock_);
    if (!isOpen()) throw ClosedSelectorException();
    processDeregisterQueue();
    // Register the interruptor first, then test the flag (interrupt() sets the flag before
    // calling the wake function, so no interrupt is lost). An interrupted thread does not
    // block; its interrupt status stays set, as in Java.
    struct BlockerScope {
        explicit BlockerScope(Selector* s) { sync::setBlocker(&wakeSelector, s); }
        ~BlockerScope() { sync::clearBlocker(); }
    } blocker(this);
    if (timeoutMs != 0 && sync::isInterrupted(sync::current())) timeoutMs = 0;

    constexpr int kMaxEvents = 1024;
    epoll_event events[kMaxEvents];
    int n;
    auto start = std::chrono::steady_clock::now();
    int32_t to = timeoutMs;
    for (;;) {
        n = ::epoll_wait(epfd_, events, kMaxEvents, to);
        if (n >= 0) break;
        if (errno != EINTR) throw IOException(str("epoll_wait: ", String(std::strerror(errno))));
        if (!isOpen()) {
            n = 0;
            break;
        }
        if (to > 0) {  // adjust the remaining time (GC signals interrupt epoll_wait)
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            to = timeoutMs - static_cast<int32_t>(elapsed.count());
            if (to <= 0) {
                n = 0;
                break;
            }
        }
    }
    if (!isOpen()) return 0;

    processDeregisterQueue();

    int32_t updated = 0;
    bool woken = false;
    {
        std::lock_guard<std::mutex> g(lock_);
        KeyMap& map = keyMap(keyMap_);
        for (int i = 0; i < n; i++) {
            uint64_t id = events[i].data.u64;
            if (id == 0) {
                woken = true;
                continue;
            }
            auto it = map.find(id);
            if (it == map.end()) continue;
            SelectionKey* k = it->second;
            if (!k->isValid()) continue;
            uint32_t ev = events[i].events;
            if (selected_->contains(k)) {
                int32_t old = k->rawReady();
                int32_t nw = translateReady(k, ev, old);
                k->setReady(nw);
                if ((nw & ~old) != 0) updated++;
            } else {
                int32_t nw = translateReady(k, ev, 0);
                k->setReady(nw);
                if ((nw & k->rawInterest()) != 0) {
                    selected_->add(k);
                    updated++;
                }
            }
        }
    }
    if (woken) {
        std::lock_guard<std::mutex> g(wakeLock_);
        uint64_t v;
        while (::read(wakeupFd_, &v, sizeof v) > 0) {
        }
        wakeupPending_ = false;
    }
    return updated;
}

Selector* Selector::wakeup() {
    std::lock_guard<std::mutex> g(wakeLock_);
    if (!wakeupPending_ && isOpen()) {
        uint64_t one = 1;
        ssize_t r;
        do {
            r = ::write(wakeupFd_, &one, sizeof one);
        } while (r < 0 && errno == EINTR);
        wakeupPending_ = true;
    }
    return this;
}

void Selector::close() {
    {
        std::lock_guard<std::mutex> g(wakeLock_);
        if (!open_.load()) return;
        uint64_t one = 1;
        ssize_t wr = ::write(wakeupFd_, &one, sizeof one);  // wake a blocked select
        (void)wr;
        open_.store(false, std::memory_order_release);
    }
    std::lock_guard<std::mutex> sg(selectLock_);  // wait for a select in progress
    std::vector<SelectionKey*> all;
    {
        std::lock_guard<std::mutex> g(lock_);
        for (SelectionKey* k : *keys_) {
            k->invalidate();
            k->registeredEvents = 0;
            all.push_back(k);
        }
        keys_->clear();
        selected_->clear();
        keyMap(keyMap_).clear();
        cancelled_.clear();
    }
    for (SelectionKey* k : all) k->channel()->_removeKey(k);
    ::close(epfd_);
    ::close(wakeupFd_);
    epfd_ = -1;
    wakeupFd_ = -1;
}

String Selector::toString() { return str("sun.nio.ch.EPollSelectorImpl@", String::format("%x", identityHashCode())); }

// =======================================================================================
// SelectorProvider

SelectorProvider* SelectorProvider::provider() {
    static SelectorProvider* p = new SelectorProvider();
    return p;
}

}  // namespace jlang
