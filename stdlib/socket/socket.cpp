#include "socket.hpp"

#include <cerrno>
#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cheatah::socket {
namespace {

// Resolve host:port to an IPv4 TCP address. Returns true and fills `out`/`len` on
// success. Used by bind/connect so "localhost", dotted IPs, and DNS names all work.
bool resolve(const std::string& host, long long port, sockaddr_storage& out, socklen_t& len) {
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

int as_fd(long long fd) { return static_cast<int>(fd); }

} // namespace

long long socket() {
    return ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

long long set_reuseaddr(long long fd) {
    int yes = 1;
    return ::setsockopt(as_fd(fd), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
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
    return ::accept(as_fd(fd), nullptr, nullptr);
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
    return ::send(as_fd(fd), data.data(), data.size(), MSG_NOSIGNAL);
}

long long sendall(long long fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(as_fd(fd), data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += static_cast<std::size_t>(n);
    }
    return 0;
}

std::string recv(long long fd, long long bufsize) {
    if (bufsize <= 0) return std::string();
    std::string buf(static_cast<std::size_t>(bufsize), '\0');
    ssize_t n = ::recv(as_fd(fd), buf.data(), buf.size(), 0);
    if (n <= 0) return std::string();
    buf.resize(static_cast<std::size_t>(n));
    return buf;
}

long long close(long long fd) {
    return ::close(as_fd(fd));
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

std::string last_error() {
    return std::strerror(errno);
}

} // namespace cheatah::socket
