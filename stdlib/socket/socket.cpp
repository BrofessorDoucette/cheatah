// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "socket.hpp"

#include <cerrno>
#include <cstring>
#include <string>

#if defined(_WIN32)
// Windows: the BSD socket API lives in Winsock2 (closesocket, WSAStartup, SOCKET type).
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// Suppress SIGPIPE on send() to a closed peer. Linux passes MSG_NOSIGNAL per-call; macOS/
// BSD have no such flag and instead use the SO_NOSIGPIPE socket option (set in socket()).
// Define MSG_NOSIGNAL to 0 where it's absent (macOS, Windows) so the send() calls stay portable.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace cheatah::socket {
namespace {

#if defined(_WIN32)
// Winsock must be initialized once per process before any socket call. A function-local
// static does it lazily and tears it down at exit.
void ensure_winsock() {
    struct WinsockInit {
        WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
        ~WinsockInit() { WSACleanup(); }
    };
    static WinsockInit init;
}
SOCKET as_fd(long long fd) { return static_cast<SOCKET>(fd); }
#else
void ensure_winsock() {}
int as_fd(long long fd) { return static_cast<int>(fd); }
#endif

// Resolve host:port to an IPv4 TCP address. Returns true and fills `out`/`len` on
// success. Used by bind/connect so "localhost", dotted IPs, and DNS names all work.
bool resolve(const std::string& host, long long port, sockaddr_storage& out, socklen_t& len) {
    ensure_winsock();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    const std::string service = std::to_string(port);
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || res == nullptr) {
        errno = EADDRNOTAVAIL;
        return false;
    }
    std::memcpy(&out, res->ai_addr, res->ai_addrlen);
    len = static_cast<socklen_t>(res->ai_addrlen);
    ::freeaddrinfo(res);
    return true;
}

} // namespace

long long socket() {
    ensure_winsock();
    const auto fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if defined(_WIN32)
    if (fd == INVALID_SOCKET) return -1;  // Winsock signals failure with INVALID_SOCKET
#else
#ifdef SO_NOSIGPIPE
    // macOS/BSD: ask the kernel not to raise SIGPIPE on this socket (Linux uses
    // MSG_NOSIGNAL per send() instead — SO_NOSIGPIPE isn't defined there).
    if (fd >= 0) {
        int on = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    }
#endif
#endif
    return static_cast<long long>(fd);
}

long long set_reuseaddr(long long fd) {
    int yes = 1;
    // optval is const void* on POSIX, const char* on Winsock — char* converts to both.
    return ::setsockopt(as_fd(fd), SOL_SOCKET, SO_REUSEADDR,
                        reinterpret_cast<const char*>(&yes), sizeof(yes));
}

long long bind(long long fd, const std::string& host, long long port) {
    sockaddr_storage addr{};
    socklen_t len = 0;
    if (!resolve(host, port, addr, len)) return -1;
    return ::bind(as_fd(fd), reinterpret_cast<sockaddr*>(&addr), len);
}

long long listen(long long fd, long long backlog) {
    return ::listen(as_fd(fd), static_cast<int>(backlog));
}

long long connect(long long fd, const std::string& host, long long port) {
    sockaddr_storage addr{};
    socklen_t len = 0;
    if (!resolve(host, port, addr, len)) return -1;
    return ::connect(as_fd(fd), reinterpret_cast<sockaddr*>(&addr), len);
}

long long accept(long long fd) {
    const auto c = ::accept(as_fd(fd), nullptr, nullptr);
#if defined(_WIN32)
    if (c == INVALID_SOCKET) return -1;
#endif
    return static_cast<long long>(c);
}

long long local_port(long long fd) {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(as_fd(fd), reinterpret_cast<sockaddr*>(&addr), &len) != 0) return -1;
    // sin_port sits at the same offset for IPv4/IPv6, and we only ever make
    // AF_INET sockets, so reading it back is safe.
    return ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);
}

long long send(long long fd, const std::string& data) {
    // send() returns ssize_t on POSIX, int on Winsock — long long holds both.
    const long long n = ::send(as_fd(fd), data.data(),
                               static_cast<int>(data.size()), MSG_NOSIGNAL);
    return n;
}

long long sendall(long long fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const long long n = ::send(as_fd(fd), data.data() + sent,
                                   static_cast<int>(data.size() - sent), MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += static_cast<std::size_t>(n);
    }
    return 0;
}

std::string recv(long long fd, long long bufsize) {
    if (bufsize <= 0) return std::string();
    std::string buf(static_cast<std::size_t>(bufsize), '\0');
    const long long n = ::recv(as_fd(fd), buf.data(), static_cast<int>(buf.size()), 0);
    if (n <= 0) return std::string();
    buf.resize(static_cast<std::size_t>(n));
    return buf;
}

long long close(long long fd) {
#if defined(_WIN32)
    return ::closesocket(as_fd(fd));
#else
    return ::close(as_fd(fd));
#endif
}

long long shutdown(long long fd) {
    // Half-close both directions WITHOUT releasing the fd: a blocking recv() on
    // ANOTHER thread returns immediately (EOF). Used to wake a reader for a clean
    // shutdown; the fd is still owned by the caller and must be close()d afterwards.
#if defined(_WIN32)
    return ::shutdown(as_fd(fd), SD_BOTH);
#else
    return ::shutdown(as_fd(fd), SHUT_RDWR);
#endif
}

long long tcp_listen(const std::string& host, long long port, long long backlog) {
    // If socket() fails (-1), the bind below fails too and we fall through to the
    // error path — no separate early return needed.
    long long fd = socket();
    set_reuseaddr(fd);
    if (bind(fd, host, port) != 0 || listen(fd, backlog) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

long long tcp_connect(const std::string& host, long long port) {
    long long fd = socket();
    if (connect(fd, host, port) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

long long set_timeout(long long fd, long long timeout_ms) {
    ensure_winsock();
#if defined(_WIN32)
    const DWORD ms = timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : 0;
    if (::setsockopt(as_fd(fd), SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&ms), sizeof ms) != 0) return -1;
    if (::setsockopt(as_fd(fd), SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char*>(&ms), sizeof ms) != 0) return -1;
#else
    timeval tv{};
    if (timeout_ms > 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    }
    if (::setsockopt(as_fd(fd), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) != 0) return -1;
    if (::setsockopt(as_fd(fd), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) != 0) return -1;
#endif
    return 0;
}

std::string last_error() {
#if defined(_WIN32)
    return "Winsock error " + std::to_string(::WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

// ---- owning RAII connections ----
// Each method forwards to the fd-based free function above; the value the guards add is
// deterministic close() on scope exit, so a `with` block cannot leak the fd.

Conn& Conn::operator=(Conn&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) cheatah::socket::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}
Conn::~Conn() {
    if (fd_ >= 0) cheatah::socket::close(fd_);
}
long long Conn::send(const std::string& data) { return cheatah::socket::send(fd_, data); }
long long Conn::sendall(const std::string& data) { return cheatah::socket::sendall(fd_, data); }
std::string Conn::recv(long long bufsize) { return cheatah::socket::recv(fd_, bufsize); }
long long Conn::set_timeout(long long timeout_ms) {
    return cheatah::socket::set_timeout(fd_, timeout_ms);
}
long long Conn::local_port() const { return cheatah::socket::local_port(fd_); }
long long Conn::shutdown() { return cheatah::socket::shutdown(fd_); }
long long Conn::close() {
    if (fd_ < 0) return -1;
    const long long rc = cheatah::socket::close(fd_);
    fd_ = -1;
    return rc;
}

Listener& Listener::operator=(Listener&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) cheatah::socket::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}
Listener::~Listener() {
    if (fd_ >= 0) cheatah::socket::close(fd_);
}
Conn Listener::accept() { return Conn(cheatah::socket::accept(fd_)); }
long long Listener::local_port() const { return cheatah::socket::local_port(fd_); }
long long Listener::close() {
    if (fd_ < 0) return -1;
    const long long rc = cheatah::socket::close(fd_);
    fd_ = -1;
    return rc;
}

Conn open(const std::string& host, long long port) { return Conn(tcp_connect(host, port)); }
Listener serve(const std::string& host, long long port, long long backlog) {
    return Listener(tcp_listen(host, port, backlog));
}

} // namespace cheatah::socket
