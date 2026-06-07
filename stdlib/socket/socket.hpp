#pragma once

/**
 * @file socket.hpp
 * @brief cheatah `socket` — a small wrapper around BSD/POSIX TCP sockets,
 *        in the spirit of Python's `socket`. `import socket` to use it.
 *
 * cheatah has no methods yet, so the API is **flat and file-descriptor based**
 * (like the C sockets layer): you pass the integer fd returned by `socket()` /
 * `tcp_listen()` / `accept()` to the other calls. IPv4 + TCP only; host names are
 * resolved with `getaddrinfo` (so `"localhost"`, `"127.0.0.1"`, and DNS names all
 * work). Errors are reported as a negative return (or empty string for `recv`);
 * `last_error()` gives the matching `errno` text.
 *
 * `import socket` includes this header AND links `libcheatah_socket`. Unit tests:
 * `stdlib/tests/socket_test.cpp`; the suite runs under AddressSanitizer (the
 * `asan` preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * @note Every call is a thin wrapper over one or two syscalls. Only `recv` and
 *       `last_error` allocate (their returned `std::string`); the fd/status calls
 *       return a `long long` and do not allocate.
 */
#include <string>

namespace cheatah::socket {

// ---- high-level convenience (what a server/client usually wants) ----

/**
 * Create a TCP socket bound to @p host:@p port and put it in the listening state (sets
 *   `SO_REUSEADDR`).
 * @param host interface to bind ("127.0.0.1", "0.0.0.0", …).
 * @param port TCP port (0 = let the OS pick — read it back with local_port()).
 * @param backlog pending-connection queue length.
 * @return the listening fd, or -1 on error.
 * @note O(1) (a few syscalls); no heap.
 * @test CheatahSocket.Loopback
 */
long long tcp_listen(const std::string& host, long long port, long long backlog);

/**
 * Create a TCP socket and connect it to @p host:@p port.
 * @param host destination host (name or IP).
 * @param port destination port.
 * @return the connected fd, or -1 on error.
 * @note O(1) + DNS resolution; no heap.
 * @test CheatahSocket.Loopback
 */
long long tcp_connect(const std::string& host, long long port);

// ---- per-connection I/O ----

/**
 * Accept one pending connection.
 * @param fd a listening fd.
 * @return the connected client fd, or -1 on error.
 * @note O(1) syscall (blocks until a client arrives); no heap.
 * @test CheatahSocket.Loopback
 */
long long accept(long long fd);

/**
 * Receive up to @p bufsize bytes.
 * @param fd a connected fd.
 * @param bufsize maximum bytes to read.
 * @return the bytes read (binary-safe), or "" on EOF/error.
 * @note O(@p bufsize); allocates the returned string.
 * @test CheatahSocket.Loopback
 */
std::string recv(long long fd, long long bufsize);

/**
 * Send some of @p data (one `send`).
 * @param fd a connected fd.
 * @param data bytes to send.
 * @return bytes actually sent, or -1 on error.
 * @note O(n); no heap (`MSG_NOSIGNAL`, so a broken pipe never raises `SIGPIPE`).
 * @test CheatahSocket.Sendall
 */
long long send(long long fd, const std::string& data);

/**
 * Send @p data in full, looping until all bytes are written.
 * @param fd a connected fd.
 * @param data bytes to send.
 * @return 0 on success, -1 on error.
 * @note O(n); no heap.
 * @test CheatahSocket.Sendall
 */
long long sendall(long long fd, const std::string& data);

/**
 * Close a socket.
 * @param fd the fd to close.
 * @return 0 on success, -1 on error.
 * @note O(1) syscall; no heap.
 * @test CheatahSocket.BadFd
 */
long long close(long long fd);

// ---- low-level BSD primitives (for clients/servers built by hand) ----

/**
 * Create an IPv4 TCP socket.
 * @return the new fd, or -1 on error.
 * @note O(1); no heap.
 * @test CheatahSocket.ListenLowLevel
 */
long long socket();

/**
 * Enable `SO_REUSEADDR` on @p fd.
 * @param fd the socket.
 * @return 0 on success, -1 on error.
 * @note O(1); no heap.
 * @test CheatahSocket.ListenLowLevel
 */
long long set_reuseaddr(long long fd);

/**
 * Bind @p fd to @p host:@p port.
 * @param fd the socket.
 * @param host interface to bind.
 * @param port TCP port (0 = OS-assigned).
 * @return 0 on success, -1 on error.
 * @note O(1) + resolution; no heap.
 * @test CheatahSocket.ListenLowLevel, CheatahSocket.ResolveFailure
 */
long long bind(long long fd, const std::string& host, long long port);

/**
 * Mark @p fd as a passive (listening) socket.
 * @param fd the socket.
 * @param backlog queue length.
 * @return 0 on success, -1 on error.
 * @note O(1); no heap.
 * @test CheatahSocket.ListenLowLevel
 */
long long listen(long long fd, long long backlog);

/**
 * Connect @p fd to @p host:@p port.
 * @param fd the socket.
 * @param host destination.
 * @param port destination port.
 * @return 0 on success, -1 on error.
 * @note O(1) + DNS; no heap.
 * @test CheatahSocket.ConnectRefused
 */
long long connect(long long fd, const std::string& host, long long port);

/**
 * The local TCP port @p fd is bound to (useful after binding to port 0).
 * @param fd a bound socket.
 * @return the port, or -1 on error.
 * @note O(1) syscall; no heap.
 * @test CheatahSocket.Loopback
 */
long long local_port(long long fd);

/**
 * The message for the current `errno`.
 * @return `strerror(errno)`.
 * @note O(1); allocates the returned string.
 * @test CheatahSocket.ConnectRefused
 */
std::string last_error();

} // namespace cheatah::socket
