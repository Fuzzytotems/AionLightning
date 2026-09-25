// jlang/NioNet.cpp - java.net.InetAddress / InetSocketAddress / Socket / ServerSocket and
// java.nio.channels.SocketChannel / ServerSocketChannel over POSIX sockets.
#include <jlang/Nio.h>

#include <jlang/Collections.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace jlang {

namespace {

String errnoText(int err) {
    char buf[256];
    const char* s = strerror_r(err, buf, sizeof buf);  // GNU variant
    return String(s);
}

[[noreturn]] void throwIOErrno(int err) {
    switch (err) {
        case ECONNREFUSED:
            throw ConnectException(errnoText(err));
        case EHOSTUNREACH:
        case ENETUNREACH:
            throw NoRouteToHostException(errnoText(err));
        case EADDRINUSE:
        case EADDRNOTAVAIL:
        case EACCES:
            throw BindException(errnoText(err));
        case ETIMEDOUT:
            throw ConnectException(errnoText(err));
        case EBADF:
            throw ClosedChannelException();
        default:
            throw IOException(errnoText(err));
    }
}

[[noreturn]] void throwSocketErrno(int err) { throw SocketException(errnoText(err)); }

InetAddress* fromSockaddr(const sockaddr_storage& ss, int32_t* port) {
    if (ss.ss_family == AF_INET) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(&ss);
        if (port) *port = ntohs(sin->sin_port);
        return new InetAddress(reinterpret_cast<const uint8_t*>(&sin->sin_addr), 4, String());
    }
    if (ss.ss_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(&ss);
        if (port) *port = ntohs(sin6->sin6_port);
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&sin6->sin6_addr);
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (std::memcmp(b, mapped, 12) == 0) return new InetAddress(b + 12, 4, String());
        return new InetAddress(b, 16, String(), sin6->sin6_scope_id);
    }
    if (port) *port = 0;
    return nullptr;
}

InetSocketAddress* sockAddrOf(const sockaddr_storage& ss) {
    int32_t port = 0;
    InetAddress* a = fromSockaddr(ss, &port);
    if (a == nullptr) return nullptr;
    return new InetSocketAddress(a, port);
}

// Fills ss for addr:port for a socket of `family`. Throws UnresolvedAddressException for an
// unresolved address, UnsupportedAddressTypeException for an IPv6 address on an IPv4 socket.
socklen_t toSockaddr(InetSocketAddress* isa, int family, sockaddr_storage& ss) {
    if (isa == nullptr) throw IllegalArgumentException(String("Invalid address"));
    if (isa->isUnresolved()) throw UnresolvedAddressException();
    InetAddress* a = isa->getAddress();
    std::memset(&ss, 0, sizeof ss);
    if (family == AF_INET) {
        if (!a->isIPv4()) throw UnsupportedAddressTypeException();
        auto* sin = reinterpret_cast<sockaddr_in*>(&ss);
        sin->sin_family = AF_INET;
        sin->sin_port = htons(static_cast<uint16_t>(isa->getPort()));
        std::memcpy(&sin->sin_addr, a->rawBytes(), 4);
        return sizeof(sockaddr_in);
    }
    auto* sin6 = reinterpret_cast<sockaddr_in6*>(&ss);
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(static_cast<uint16_t>(isa->getPort()));
    if (a->isIPv4()) {
        uint8_t* b = reinterpret_cast<uint8_t*>(&sin6->sin6_addr);
        if (a->isAnyLocalAddress()) {
            std::memset(b, 0, 16);
        } else {
            b[10] = 0xff;
            b[11] = 0xff;
            std::memcpy(b + 12, a->rawBytes(), 4);
        }
    } else {
        std::memcpy(&sin6->sin6_addr, a->rawBytes(), 16);
        sin6->sin6_scope_id = a->scopeId();
    }
    return sizeof(sockaddr_in6);
}

int familyFor(InetSocketAddress* isa) {
    if (isa == nullptr || isa->isUnresolved()) return AF_INET;
    return isa->getAddress()->isIPv4() ? AF_INET : AF_INET6;
}

int newSocket(int family) {
    int fd = ::socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        int err = errno;
        if (err == EAFNOSUPPORT) throw SocketException(String("Protocol family unavailable"));
        throwSocketErrno(err);
    }
    if (family == AF_INET6) {
        int off = 0;
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    }
    return fd;
}

void setBlockingFd(int fd, bool block) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) throwIOErrno(errno);
    int nf = block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    if (nf != flags && ::fcntl(fd, F_SETFL, nf) < 0) throwIOErrno(errno);
}

bool getLocal(int fd, sockaddr_storage& ss) {
    socklen_t len = sizeof ss;
    std::memset(&ss, 0, sizeof ss);
    return fd >= 0 && ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len) == 0;
}

void setIntOpt(int fd, int level, int opt, int value) {
    if (fd < 0) throw SocketException(String("Socket is closed"));
    if (::setsockopt(fd, level, opt, &value, sizeof value) < 0) throwSocketErrno(errno);
}

int getIntOpt(int fd, int level, int opt) {
    if (fd < 0) throw SocketException(String("Socket is closed"));
    int value = 0;
    socklen_t len = sizeof value;
    if (::getsockopt(fd, level, opt, &value, &len) < 0) throwSocketErrno(errno);
    return value;
}

String hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%x", v);
    return String(buf);
}

// Parses a numeric IPv4 literal ("1.2.3.4") or IPv6 literal (optionally with %scope).
// Returns the byte length (4/16) or 0.
int32_t parseLiteral(const std::string& host, uint8_t out[16], uint32_t* scope) {
    *scope = 0;
    in_addr a4;
    if (::inet_pton(AF_INET, host.c_str(), &a4) == 1) {
        std::memcpy(out, &a4, 4);
        return 4;
    }
    if (host.find(':') != std::string::npos) {
        std::string h = host;
        std::string sc;
        size_t pct = h.find('%');
        if (pct != std::string::npos) {
            sc = h.substr(pct + 1);
            h = h.substr(0, pct);
        }
        in6_addr a6;
        if (::inet_pton(AF_INET6, h.c_str(), &a6) == 1) {
            std::memcpy(out, &a6, 16);
            if (!sc.empty()) {
                char* end = nullptr;
                unsigned long v = std::strtoul(sc.c_str(), &end, 10);
                if (end != nullptr && *end == '\0') *scope = static_cast<uint32_t>(v);
                else *scope = ::if_nametoindex(sc.c_str());
            }
            static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
            if (std::memcmp(out, mapped, 12) == 0) {
                std::memmove(out, out + 12, 4);
                return 4;
            }
            return 16;
        }
    }
    return 0;
}

}  // namespace

// =======================================================================================
// InetAddress

InetAddress::InetAddress(const uint8_t* bytes, int32_t len, const String& hostName, uint32_t scopeId)
    : len_(len), scope_(scopeId), hostName_(hostName) {
    std::memset(addr_, 0, sizeof addr_);
    std::memcpy(addr_, bytes, static_cast<size_t>(len));
}

InetAddress* InetAddress::getLoopbackAddress() {
    static const uint8_t lo[4] = {127, 0, 0, 1};
    return new InetAddress(lo, 4, String("localhost"));
}

InetAddress* InetAddress::anyLocalAddress() {
    static const uint8_t any[4] = {0, 0, 0, 0};
    return new InetAddress(any, 4, String("0.0.0.0"));
}

Array<InetAddress*>* InetAddress::getAllByName(const String& hostIn) {
    if (hostIn.isNull() || hostIn.isEmpty()) {
        auto* r = new Array<InetAddress*>(1);
        (*r)[0] = getLoopbackAddress();
        return r;
    }
    std::string host = hostIn;
    bool ipv6Expected = false;
    if (host[0] == '[') {
        if (host.size() > 2 && host.back() == ']') {
            host = host.substr(1, host.size() - 2);
            ipv6Expected = true;
        } else {
            throw UnknownHostException(str(hostIn, ": invalid IPv6 address"));
        }
    }
    uint8_t bytes[16];
    uint32_t scope = 0;
    int32_t n = parseLiteral(host, bytes, &scope);
    if (n != 0) {
        auto* r = new Array<InetAddress*>(1);
        (*r)[0] = new InetAddress(bytes, n, String(), scope);
        return r;
    }
    if (ipv6Expected) throw UnknownHostException(str("[", String(host), "]: invalid IPv6 address"));
    addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc == EAI_NONAME || rc == EAI_FAIL || rc == EAI_NODATA || rc == EAI_ADDRFAMILY) {
        // Retry without AI_ADDRCONFIG (containers without a configured non-loopback address).
        hints.ai_flags = 0;
        rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
    }
    if (rc != 0) throw UnknownHostException(str(hostIn, ": ", String(::gai_strerror(rc))));
    std::vector<InetAddress*> v4, v6;
    for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        sockaddr_storage ss;
        std::memset(&ss, 0, sizeof ss);
        std::memcpy(&ss, ai->ai_addr, ai->ai_addrlen);
        InetAddress* a = fromSockaddr(ss, nullptr);
        if (a == nullptr) continue;
        a->hostName_ = hostIn;
        bool dup = false;
        for (InetAddress* o : (a->isIPv4() ? v4 : v6))
            if (o->len_ == a->len_ && std::memcmp(o->addr_, a->addr_, static_cast<size_t>(a->len_)) == 0) dup = true;
        if (!dup) (a->isIPv4() ? v4 : v6).push_back(a);
    }
    ::freeaddrinfo(res);
    if (v4.empty() && v6.empty()) throw UnknownHostException(str(hostIn, ": Name or service not known"));
    auto* r = new Array<InetAddress*>(static_cast<int32_t>(v4.size() + v6.size()));
    int32_t i = 0;
    for (InetAddress* a : v4) (*r)[i++] = a;
    for (InetAddress* a : v6) (*r)[i++] = a;
    return r;
}

InetAddress* InetAddress::getByName(const String& host) { return (*getAllByName(host))[0]; }

InetAddress* InetAddress::getByAddress(Array<int8_t>* addr) { return getByAddress(String(), addr); }

InetAddress* InetAddress::getByAddress(const String& host, Array<int8_t>* addr) {
    if (addr == nullptr) throw NullPointerException();
    if (addr->length == 4 || addr->length == 16) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(addr->data());
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (addr->length == 16 && std::memcmp(b, mapped, 12) == 0) return new InetAddress(b + 12, 4, host);
        return new InetAddress(b, addr->length, host);
    }
    throw UnknownHostException(String("addr is of illegal length"));
}

InetAddress* InetAddress::getLocalHost() {
    char name[256];
    if (::gethostname(name, sizeof name) != 0) throw UnknownHostException(errnoText(errno));
    name[sizeof name - 1] = '\0';
    String local(name);
    if (local.equals("localhost")) return getLoopbackAddress();
    try {
        InetAddress* a = getByName(local);
        return new InetAddress(a->addr_, a->len_, local, a->scope_);
    } catch (UnknownHostException& e) {
        throw UnknownHostException(str(local, ": ", e.getMessage()));
    }
}

Array<int8_t>* InetAddress::getAddress() {
    auto* r = new Array<int8_t>(len_);
    std::memcpy(r->data(), addr_, static_cast<size_t>(len_));
    return r;
}

String InetAddress::getHostAddress() {
    if (len_ == 4) return str(addr_[0], ".", addr_[1], ".", addr_[2], ".", addr_[3]);
    std::string s;
    for (int i = 0; i < 8; i++) {
        uint32_t v = (static_cast<uint32_t>(addr_[2 * i]) << 8) | addr_[2 * i + 1];
        s += std::string(hex(v));
        if (i < 7) s.push_back(':');
    }
    if (scope_ != 0) s += "%" + std::to_string(scope_);
    return String(s);
}

String InetAddress::getHostName() {
    {
        std::lock_guard<std::mutex> g(nameLock_);
        if (!hostName_.isNull()) return hostName_;
    }
    String result;
    sockaddr_storage ss;
    std::memset(&ss, 0, sizeof ss);
    socklen_t len;
    if (len_ == 4) {
        auto* sin = reinterpret_cast<sockaddr_in*>(&ss);
        sin->sin_family = AF_INET;
        std::memcpy(&sin->sin_addr, addr_, 4);
        len = sizeof(sockaddr_in);
    } else {
        auto* sin6 = reinterpret_cast<sockaddr_in6*>(&ss);
        sin6->sin6_family = AF_INET6;
        std::memcpy(&sin6->sin6_addr, addr_, 16);
        sin6->sin6_scope_id = scope_;
        len = sizeof(sockaddr_in6);
    }
    char host[NI_MAXHOST];
    if (::getnameinfo(reinterpret_cast<sockaddr*>(&ss), len, host, sizeof host, nullptr, 0, NI_NAMEREQD) == 0)
        result = String(host);
    else
        result = getHostAddress();
    std::lock_guard<std::mutex> g(nameLock_);
    if (hostName_.isNull()) hostName_ = result;
    return hostName_;
}

String InetAddress::getCanonicalHostName() { return getHostName(); }

bool InetAddress::isAnyLocalAddress() {
    for (int32_t i = 0; i < len_; i++)
        if (addr_[i] != 0) return false;
    return true;
}

bool InetAddress::isLoopbackAddress() {
    if (len_ == 4) return addr_[0] == 127;
    for (int i = 0; i < 15; i++)
        if (addr_[i] != 0) return false;
    return addr_[15] == 1;
}

bool InetAddress::isSiteLocalAddress() {
    if (len_ == 4)
        return addr_[0] == 10 || (addr_[0] == 172 && (addr_[1] & 0xF0) == 16) || (addr_[0] == 192 && addr_[1] == 168);
    return addr_[0] == 0xfe && (addr_[1] & 0xc0) == 0xc0;
}

bool InetAddress::isLinkLocalAddress() {
    if (len_ == 4) return addr_[0] == 169 && addr_[1] == 254;
    return addr_[0] == 0xfe && (addr_[1] & 0xc0) == 0x80;
}

bool InetAddress::isMulticastAddress() {
    if (len_ == 4) return (addr_[0] & 0xf0) == 0xe0;
    return addr_[0] == 0xff;
}

bool InetAddress::equals(Object* o) {
    InetAddress* a = dynamic_cast<InetAddress*>(o);
    if (a == nullptr || a->len_ != len_) return false;
    return std::memcmp(a->addr_, addr_, static_cast<size_t>(len_)) == 0;
}

int32_t InetAddress::hashCode() {
    if (len_ == 4) {
        return static_cast<int32_t>((static_cast<uint32_t>(addr_[0]) << 24) | (static_cast<uint32_t>(addr_[1]) << 16) |
                                    (static_cast<uint32_t>(addr_[2]) << 8) | addr_[3]);
    }
    // Inet6Address: components built from *signed* bytes, like the JDK.
    int32_t hash = 0;
    int i = 0;
    while (i < 16) {
        int j = 0;
        int32_t component = 0;
        while (j < 4 && i < 16) {
            component = static_cast<int32_t>(static_cast<uint32_t>(component) << 8) + static_cast<int8_t>(addr_[i]);
            j++;
            i++;
        }
        hash = static_cast<int32_t>(static_cast<uint32_t>(hash) + static_cast<uint32_t>(component));
    }
    return hash;
}

String InetAddress::toString() {
    String name;
    {
        std::lock_guard<std::mutex> g(nameLock_);
        name = hostName_;
    }
    return str(name.isNull() ? String("") : name, "/", getHostAddress());
}

// =======================================================================================
// InetSocketAddress

namespace {
int32_t checkPort(int32_t port) {
    if (port < 0 || port > 0xFFFF) throw IllegalArgumentException(str("port out of range:", port));
    return port;
}
}  // namespace

InetSocketAddress::InetSocketAddress(int32_t port) : InetSocketAddress(nullptr, port) {}

InetSocketAddress::InetSocketAddress(InetAddress* addr, int32_t port)
    : addr_(addr == nullptr ? InetAddress::anyLocalAddress() : addr), port_(checkPort(port)) {}

InetSocketAddress::InetSocketAddress(const String& hostname, int32_t port) : port_(checkPort(port)) {
    if (hostname.isNull()) throw IllegalArgumentException(String("hostname can't be null"));
    try {
        addr_ = InetAddress::getByName(hostname);
    } catch (UnknownHostException&) {
        addr_ = nullptr;
    }
    hostname_ = hostname;
}

InetSocketAddress* InetSocketAddress::createUnresolved(const String& host, int32_t port) {
    if (host.isNull()) throw IllegalArgumentException(String("hostname can't be null"));
    auto* r = new InetSocketAddress();
    r->port_ = checkPort(port);
    r->hostname_ = host;
    return r;
}

String InetSocketAddress::getHostName() {
    if (!hostname_.isNull()) return hostname_;
    if (addr_ != nullptr) return addr_->getHostName();
    return String();
}

String InetSocketAddress::getHostString() {
    if (!hostname_.isNull()) return hostname_;
    if (addr_ != nullptr) {
        String s = addr_->toString();
        String name = s.substring(0, s.indexOf(String("/")));
        if (!name.isEmpty()) return name;
        return addr_->getHostAddress();
    }
    return String();
}

bool InetSocketAddress::equals(Object* o) {
    InetSocketAddress* that = dynamic_cast<InetSocketAddress*>(o);
    if (that == nullptr) return false;
    bool sameIP;
    if (addr_ != nullptr) sameIP = addr_->equals(that->addr_);
    else if (!hostname_.isNull()) sameIP = that->addr_ == nullptr && hostname_.equalsIgnoreCase(that->hostname_);
    else sameIP = that->addr_ == nullptr && that->hostname_.isNull();
    return sameIP && port_ == that->port_;
}

int32_t InetSocketAddress::hashCode() {
    if (addr_ != nullptr) return static_cast<int32_t>(static_cast<uint32_t>(addr_->hashCode()) + static_cast<uint32_t>(port_));
    if (!hostname_.isNull())
        return static_cast<int32_t>(static_cast<uint32_t>(hostname_.toLowerCase().hashCode()) + static_cast<uint32_t>(port_));
    return port_;
}

String InetSocketAddress::toString() {
    if (addr_ == nullptr) return str(hostname_, ":", port_);
    return str(addr_->toString(), ":", port_);
}

// =======================================================================================
// SelectableChannel

void SelectableChannel::ensureOpen() {
    if (!isOpen()) throw ClosedChannelException();
}

bool SelectableChannel::isBlocking() {
    std::lock_guard<std::mutex> g(keyLock_);
    return blocking_;
}

SelectableChannel* SelectableChannel::configureBlocking(bool block) {
    ensureOpen();
    std::lock_guard<std::mutex> g(keyLock_);
    if (blocking_ == block) return this;
    if (block) {
        for (SelectionKey* k : keys_)
            if (k->isValid()) throw IllegalBlockingModeException();
    }
    if (fd_ >= 0) setBlockingFd(fd_, block);
    blocking_ = block;
    return this;
}

SelectionKey* SelectableChannel::keyFor(Selector* sel) {
    std::lock_guard<std::mutex> g(keyLock_);
    for (SelectionKey* k : keys_)
        if (k->selector() == sel) return k;
    return nullptr;
}

bool SelectableChannel::isRegistered() {
    std::lock_guard<std::mutex> g(keyLock_);
    return !keys_.empty();
}

void SelectableChannel::_removeKey(SelectionKey* k) {
    std::lock_guard<std::mutex> g(keyLock_);
    for (auto it = keys_.begin(); it != keys_.end(); ++it) {
        if (*it == k) {
            keys_.erase(it);
            break;
        }
    }
}

SelectionKey* SelectableChannel::register_(Selector* sel, int32_t ops, Object* att) {
    if (sel == nullptr) throw NullPointerException();
    if ((ops & ~validOps()) != 0) throw IllegalArgumentException();
    ensureOpen();
    // An unconnected/unbound channel gets its (IPv4) socket now so it can be polled.
    if (fd_ < 0) _ensureFd();
    std::lock_guard<std::mutex> g(keyLock_);
    if (blocking_) throw IllegalBlockingModeException();
    if (!isOpen()) throw ClosedChannelException();
    for (SelectionKey* k : keys_) {
        if (k->selector() == sel) {
            k->attach(att);
            k->interestOps(ops);
            return k;
        }
    }
    if (!sel->isOpen()) throw ClosedSelectorException();
    SelectionKey* k = sel->_register(this, ops, att);
    keys_.push_back(k);
    return k;
}

void SelectableChannel::close() {
    if (!open_.exchange(false, std::memory_order_acq_rel)) return;
    std::vector<SelectionKey*> ks;
    {
        std::lock_guard<std::mutex> g(keyLock_);
        ks = keys_;
    }
    for (SelectionKey* k : ks) k->cancel();  // removes fd from every epoll set first
    int fd = fd_;
    fd_ = -1;
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);  // wakes threads blocked in read/accept
        while (::close(fd) < 0 && errno == EINTR) {
        }
    }
}

SelectorProvider* SelectableChannel::provider() { return SelectorProvider::provider(); }

// =======================================================================================
// SocketChannel

SocketChannel::SocketChannel(int fd, int family, bool connected) : family_(family), state_(connected ? 2 : 0) {
    fd_ = fd;
    sockaddr_storage ss;
    socklen_t len = sizeof ss;
    std::memset(&ss, 0, sizeof ss);
    if (connected && ::getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &len) == 0) remote_ = sockAddrOf(ss);
}

SocketChannel* SocketChannel::open() { return new SocketChannel(); }

SocketChannel* SocketChannel::open(InetSocketAddress* remote) {
    SocketChannel* sc = new SocketChannel();
    try {
        sc->connect(remote);
    } catch (...) {
        try {
            sc->close();
        } catch (...) {
        }
        throw;
    }
    return sc;
}

void SocketChannel::ensureSocket(int family) {
    if (fd_ >= 0) return;
    ensureOpen();
    int fd = newSocket(family);
    if (!blocking_) {
        try {
            setBlockingFd(fd, false);
        } catch (...) {
            ::close(fd);
            throw;
        }
    }
    fd_ = fd;
    family_ = family;
}

SocketChannel* SocketChannel::bind(InetSocketAddress* local) {
    ensureOpen();
    if (state_ == 2) throw AlreadyConnectedException();
    if (state_ == 1) throw ConnectionPendingException();
    if (local == nullptr) local = new InetSocketAddress(0);
    ensureSocket(familyFor(local));
    sockaddr_storage ss;
    socklen_t len = toSockaddr(local, family_, ss);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&ss), len) < 0) throwIOErrno(errno);
    return this;
}

bool SocketChannel::connect(InetSocketAddress* remote) {
    ensureOpen();
    std::lock_guard<std::mutex> g(ioLock_);
    if (state_ == 2) throw AlreadyConnectedException();
    if (state_ == 1) throw ConnectionPendingException();
    if (remote == nullptr) throw IllegalArgumentException(String("Invalid address"));
    if (remote->isUnresolved()) throw UnresolvedAddressException();
    ensureSocket(familyFor(remote));
    sockaddr_storage ss;
    socklen_t len = toSockaddr(remote, family_, ss);
    int rc;
    rc = ::connect(fd_, reinterpret_cast<sockaddr*>(&ss), len);
    if (rc == 0) {
        state_ = 2;
        remote_ = remote;
        return true;
    }
    int err = errno;
    if (err == EINPROGRESS || err == EINTR) {
        if (!blocking_) {
            state_ = 1;
            remote_ = remote;
            return false;
        }
        // Blocking connect interrupted by a signal (GC): wait for completion.
        pollfd p{fd_, POLLOUT, 0};
        while (::poll(&p, 1, -1) < 0 && errno == EINTR) {
        }
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        ::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) throwIOErrno(soerr);
        state_ = 2;
        remote_ = remote;
        return true;
    }
    throwIOErrno(err);
}

bool SocketChannel::finishConnect() {
    ensureOpen();
    std::lock_guard<std::mutex> g(ioLock_);
    if (state_ == 2) return true;
    if (state_ != 1) throw NoConnectionPendingException();
    pollfd p{fd_, POLLOUT, 0};
    int timeout = blocking_ ? -1 : 0;
    int rc;
    while ((rc = ::poll(&p, 1, timeout)) < 0 && errno == EINTR) {
    }
    if (rc < 0) throwIOErrno(errno);
    if (rc == 0) return false;
    int soerr = 0;
    socklen_t sl = sizeof soerr;
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soerr, &sl) < 0) soerr = errno;
    if (soerr != 0) {
        state_ = 0;
        try {
            close();
        } catch (...) {
        }
        throwIOErrno(soerr);
    }
    state_ = 2;
    return true;
}

bool SocketChannel::isConnected() { return state_ == 2; }
bool SocketChannel::isConnectionPending() { return state_ == 1; }

void SocketChannel::checkConnected() {
    ensureOpen();
    if (state_ != 2) throw NotYetConnectedException();
}

int32_t SocketChannel::read(ByteBuffer* dst) {
    if (dst == nullptr) throw NullPointerException();
    if (dst->isReadOnly()) throw IllegalArgumentException(String("Read-only buffer"));
    checkConnected();
    int32_t pos = dst->position();
    int32_t rem = dst->remaining();
    if (rem == 0) return 0;
    if (inputShutdown_) return -1;
    int fd = fd_;
    ssize_t n;
    do {
        n = ::recv(fd, dst->rawBase() + pos, static_cast<size_t>(rem), 0);
    } while (n < 0 && errno == EINTR);
    if (n > 0) {
        dst->position(pos + static_cast<int32_t>(n));
        return static_cast<int32_t>(n);
    }
    if (n == 0) return -1;
    int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) return 0;
    if (!isOpen()) throw AsynchronousCloseException();
    throwIOErrno(err);
}

int64_t SocketChannel::read(Array<ByteBuffer*>* dsts) {
    if (dsts == nullptr) throw NullPointerException();
    int64_t total = 0;
    for (ByteBuffer* b : *dsts) {
        if (b == nullptr) throw NullPointerException();
        if (!b->hasRemaining()) continue;
        int32_t n = read(b);
        if (n < 0) return total > 0 ? total : -1;
        total += n;
        if (b->hasRemaining()) break;
    }
    return total;
}

int32_t SocketChannel::write(ByteBuffer* src) {
    if (src == nullptr) throw NullPointerException();
    checkConnected();
    if (outputShutdown_) throw ClosedChannelException();
    int32_t pos = src->position();
    int32_t rem = src->remaining();
    if (rem == 0) return 0;
    int32_t written = 0;
    int fd = fd_;
    while (written < rem) {
        ssize_t n = ::send(fd, src->rawBase() + pos + written, static_cast<size_t>(rem - written), MSG_NOSIGNAL);
        if (n > 0) {
            written += static_cast<int32_t>(n);
            if (!blocking_) break;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        int err = n < 0 ? errno : EIO;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            if (!blocking_) break;
            pollfd p{fd, POLLOUT, 0};
            while (::poll(&p, 1, -1) < 0 && errno == EINTR) {
            }
            continue;
        }
        if (written > 0) break;
        if (!isOpen()) throw AsynchronousCloseException();
        throwIOErrno(err);
    }
    src->position(pos + written);
    return written;
}

int64_t SocketChannel::write(Array<ByteBuffer*>* srcs) {
    if (srcs == nullptr) throw NullPointerException();
    int64_t total = 0;
    for (ByteBuffer* b : *srcs) {
        if (b == nullptr) throw NullPointerException();
        if (!b->hasRemaining()) continue;
        int32_t n = write(b);
        total += n;
        if (b->hasRemaining()) break;
    }
    return total;
}

SocketChannel* SocketChannel::shutdownInput() {
    checkConnected();
    if (!inputShutdown_) {
        if (::shutdown(fd_, SHUT_RD) < 0 && errno != ENOTCONN) throwIOErrno(errno);
        inputShutdown_ = true;
    }
    return this;
}

SocketChannel* SocketChannel::shutdownOutput() {
    checkConnected();
    if (!outputShutdown_) {
        if (::shutdown(fd_, SHUT_WR) < 0 && errno != ENOTCONN) throwIOErrno(errno);
        outputShutdown_ = true;
    }
    return this;
}

void SocketChannel::_ensureFd() { ensureSocket(AF_INET); }

InetSocketAddress* SocketChannel::getRemoteAddress() {
    ensureOpen();
    return state_ == 2 ? remote_ : nullptr;
}

InetSocketAddress* SocketChannel::getLocalAddress() {
    ensureOpen();
    if (fd_ < 0) ensureSocket(AF_INET);
    sockaddr_storage ss;
    if (!getLocal(fd_, ss)) return nullptr;
    int32_t port = 0;
    InetAddress* a = fromSockaddr(ss, &port);
    if (a == nullptr) return nullptr;
    if (port == 0 && a->isAnyLocalAddress() && state_ == 0) return nullptr;  // unbound
    return new InetSocketAddress(a, port);
}

Socket* SocketChannel::socket() {
    std::lock_guard<std::mutex> g(keyLock_);
    if (socket_ == nullptr) socket_ = new Socket(this);
    return socket_;
}

String SocketChannel::toString() {
    std::string s = "java.nio.channels.SocketChannel[";
    if (!isOpen()) {
        s += "closed";
    } else {
        s += state_ == 2 ? "connected" : (state_ == 1 ? "connection-pending" : "unconnected");
        sockaddr_storage ss;
        if (fd_ >= 0 && getLocal(fd_, ss)) {
            InetSocketAddress* l = sockAddrOf(ss);
            if (l != nullptr) s += " local=" + std::string(l->toString());
        }
        if (remote_ != nullptr && state_ != 0) s += " remote=" + std::string(remote_->toString());
    }
    s += "]";
    return String(s);
}

// =======================================================================================
// ServerSocketChannel

ServerSocketChannel* ServerSocketChannel::open() { return new ServerSocketChannel(); }

void ServerSocketChannel::ensureSocket(int family) {
    if (fd_ >= 0) return;
    ensureOpen();
    int fd = newSocket(family);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);  // JDK default for servers
    if (!blocking_) {
        try {
            setBlockingFd(fd, false);
        } catch (...) {
            ::close(fd);
            throw;
        }
    }
    fd_ = fd;
    family_ = family;
}

ServerSocketChannel* ServerSocketChannel::bind(InetSocketAddress* local) { return bind(local, 0); }

ServerSocketChannel* ServerSocketChannel::bind(InetSocketAddress* local, int32_t backlog) {
    ensureOpen();
    if (bound_) throw AlreadyBoundException();
    if (local == nullptr) local = new InetSocketAddress(0);
    ensureSocket(familyFor(local));
    sockaddr_storage ss;
    socklen_t len = toSockaddr(local, family_, ss);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&ss), len) < 0) throwIOErrno(errno);
    if (::listen(fd_, backlog < 1 ? 50 : backlog) < 0) throwIOErrno(errno);
    bound_ = true;
    sockaddr_storage ls;
    if (getLocal(fd_, ls)) local_ = sockAddrOf(ls);
    return this;
}

SocketChannel* ServerSocketChannel::accept() {
    ensureOpen();
    if (!bound_) throw NotYetBoundException();
    for (;;) {
        sockaddr_storage ss;
        socklen_t len = sizeof ss;
        int fd = ::accept4(fd_, reinterpret_cast<sockaddr*>(&ss), &len, SOCK_CLOEXEC);
        if (fd >= 0) {
            auto* sc = new SocketChannel(fd, family_, true);
            if (sc->remote_ == nullptr) sc->remote_ = sockAddrOf(ss);
            return sc;
        }
        int err = errno;
        if (err == EINTR || err == ECONNABORTED) continue;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            if (!blocking_) return nullptr;
            continue;
        }
        if (!isOpen()) throw AsynchronousCloseException();
        throwIOErrno(err);
    }
}

void ServerSocketChannel::_ensureFd() { ensureSocket(AF_INET); }

InetSocketAddress* ServerSocketChannel::getLocalAddress() {
    ensureOpen();
    return local_;
}

ServerSocket* ServerSocketChannel::socket() {
    std::lock_guard<std::mutex> g(keyLock_);
    if (socket_ == nullptr) socket_ = new ServerSocket(this);
    return socket_;
}

String ServerSocketChannel::toString() {
    std::string s = "sun.nio.ch.ServerSocketChannelImpl[";
    if (!isOpen()) s += "closed";
    else if (local_ == nullptr) s += "unbound";
    else s += std::string(local_->toString());
    s += "]";
    return String(s);
}

// =======================================================================================
// Socket (adaptor)

InetAddress* Socket::getInetAddress() {
    InetSocketAddress* r = ch_->remote_;
    return (r == nullptr || ch_->state_ == 0) ? nullptr : r->getAddress();
}

int32_t Socket::getPort() {
    InetSocketAddress* r = ch_->remote_;
    return (r == nullptr || ch_->state_ == 0) ? 0 : r->getPort();
}

InetAddress* Socket::getLocalAddress() {
    sockaddr_storage ss;
    if (!ch_->isOpen() || !getLocal(ch_->fd_, ss)) return InetAddress::anyLocalAddress();
    InetAddress* a = fromSockaddr(ss, nullptr);
    return a != nullptr ? a : InetAddress::anyLocalAddress();
}

int32_t Socket::getLocalPort() {
    sockaddr_storage ss;
    if (!ch_->isOpen() || !getLocal(ch_->fd_, ss)) return -1;
    int32_t port = 0;
    (void)fromSockaddr(ss, &port);
    return port == 0 ? -1 : port;
}

InetSocketAddress* Socket::getRemoteSocketAddress() { return ch_->state_ == 2 ? ch_->remote_ : nullptr; }

InetSocketAddress* Socket::getLocalSocketAddress() {
    sockaddr_storage ss;
    if (!ch_->isOpen() || !getLocal(ch_->fd_, ss)) return nullptr;
    return sockAddrOf(ss);
}

void Socket::setTcpNoDelay(bool on) { setIntOpt(ch_->fd_, IPPROTO_TCP, TCP_NODELAY, on ? 1 : 0); }
bool Socket::getTcpNoDelay() { return getIntOpt(ch_->fd_, IPPROTO_TCP, TCP_NODELAY) != 0; }
void Socket::setKeepAlive(bool on) { setIntOpt(ch_->fd_, SOL_SOCKET, SO_KEEPALIVE, on ? 1 : 0); }
bool Socket::getKeepAlive() { return getIntOpt(ch_->fd_, SOL_SOCKET, SO_KEEPALIVE) != 0; }
void Socket::setReceiveBufferSize(int32_t size) {
    if (size <= 0) throw IllegalArgumentException(String("invalid receive size"));
    setIntOpt(ch_->fd_, SOL_SOCKET, SO_RCVBUF, size);
}
int32_t Socket::getReceiveBufferSize() { return getIntOpt(ch_->fd_, SOL_SOCKET, SO_RCVBUF); }
void Socket::setSendBufferSize(int32_t size) {
    if (size <= 0) throw IllegalArgumentException(String("negative send size"));
    setIntOpt(ch_->fd_, SOL_SOCKET, SO_SNDBUF, size);
}
int32_t Socket::getSendBufferSize() { return getIntOpt(ch_->fd_, SOL_SOCKET, SO_SNDBUF); }
void Socket::setReuseAddress(bool on) { setIntOpt(ch_->fd_, SOL_SOCKET, SO_REUSEADDR, on ? 1 : 0); }

void Socket::setSoLinger(bool on, int32_t linger) {
    if (ch_->fd_ < 0) throw SocketException(String("Socket is closed"));
    struct linger l;
    l.l_onoff = on ? 1 : 0;
    l.l_linger = linger > 65535 ? 65535 : (linger < 0 ? 0 : linger);
    if (::setsockopt(ch_->fd_, SOL_SOCKET, SO_LINGER, &l, sizeof l) < 0) throwSocketErrno(errno);
}

void Socket::shutdownInput() {
    try {
        ch_->shutdownInput();
    } catch (NotYetConnectedException&) {
        throw SocketException(String("Socket is not connected"));
    } catch (ClosedChannelException&) {
        throw SocketException(String("Socket is closed"));
    }
}

void Socket::shutdownOutput() {
    try {
        ch_->shutdownOutput();
    } catch (NotYetConnectedException&) {
        throw SocketException(String("Socket is not connected"));
    } catch (ClosedChannelException&) {
        throw SocketException(String("Socket is closed"));
    }
}

bool Socket::isConnected() { return ch_->state_ == 2; }
bool Socket::isBound() { return ch_->fd_ >= 0 && getLocalPort() > 0; }
bool Socket::isClosed() { return !ch_->isOpen(); }
bool Socket::isInputShutdown() { return ch_->inputShutdown_ || !ch_->isOpen(); }
bool Socket::isOutputShutdown() { return ch_->outputShutdown_ || !ch_->isOpen(); }
void Socket::close() { ch_->close(); }

String Socket::toString() {
    if (isConnected())
        return str("Socket[addr=", getInetAddress(), ",port=", getPort(), ",localport=", getLocalPort(), "]");
    return String("Socket[unconnected]");
}

// =======================================================================================
// ServerSocket (adaptor)

void ServerSocket::bind(InetSocketAddress* endpoint) { bind(endpoint, 50); }

void ServerSocket::bind(InetSocketAddress* endpoint, int32_t backlog) {
    if (ch_->bound_) throw SocketException(String("Already bound"));
    ch_->bind(endpoint, backlog);
}

InetAddress* ServerSocket::getInetAddress() {
    if (ch_->local_ == nullptr) return nullptr;
    return ch_->local_->getAddress();
}

int32_t ServerSocket::getLocalPort() { return ch_->local_ == nullptr ? -1 : ch_->local_->getPort(); }
InetSocketAddress* ServerSocket::getLocalSocketAddress() { return ch_->local_; }

void ServerSocket::setReuseAddress(bool on) {
    ch_->ensureSocket(AF_INET);
    setIntOpt(ch_->fd_, SOL_SOCKET, SO_REUSEADDR, on ? 1 : 0);
}

bool ServerSocket::getReuseAddress() {
    ch_->ensureSocket(AF_INET);
    return getIntOpt(ch_->fd_, SOL_SOCKET, SO_REUSEADDR) != 0;
}

void ServerSocket::setReceiveBufferSize(int32_t size) {
    if (size <= 0) throw IllegalArgumentException(String("negative receive size"));
    ch_->ensureSocket(AF_INET);
    setIntOpt(ch_->fd_, SOL_SOCKET, SO_RCVBUF, size);
}

int32_t ServerSocket::getReceiveBufferSize() {
    ch_->ensureSocket(AF_INET);
    return getIntOpt(ch_->fd_, SOL_SOCKET, SO_RCVBUF);
}

bool ServerSocket::isBound() { return ch_->bound_; }
bool ServerSocket::isClosed() { return !ch_->isOpen(); }
void ServerSocket::close() { ch_->close(); }

String ServerSocket::toString() {
    if (!isBound()) return String("ServerSocket[unbound]");
    return str("ServerSocket[addr=", getInetAddress(), ",localport=", getLocalPort(), "]");
}

}  // namespace jlang
