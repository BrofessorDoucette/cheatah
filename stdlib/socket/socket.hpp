// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file socket.hpp
 * @brief cheatah `socket` — a small wrapper around BSD/POSIX TCP sockets,
 *        in the spirit of Python's `socket`. `import socket` to use it.
 *
 * The recommended API is the owning `socket::Conn` / `socket::Listener` guards (from
 * `socket.open(host, port)` / `socket.serve(host, port, backlog)`), which close their fd at
 * scope exit. A **flat, file-descriptor-based** API is also available for hand-built servers:
 * pass the integer fd from `socket()` / `tcp_listen()` / `accept()` to the other calls (an
 * unclosed fd there is a resource leak — prefer the guards). IPv4 + TCP only; host names are
 * resolved with `getaddrinfo` (so `"localhost"`, `"127.0.0.1"`, and DNS names all work).
 * Errors are a negative return (or empty string for `recv`); `last_error()` gives the `errno` text.
 *
 * `import socket` includes this header AND links `libcheatah_socket`. Unit tests:
 * `stdlib/tests/socket_test.cpp`; the suite runs under AddressSanitizer (the
 * `asan` preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * @note Every call is a thin wrapper over one or two syscalls. Only `recv` and
 *       `last_error` allocate (their returned `std::string`, plus `recv`'s reused
 *       per-thread scratch buffer); the fd/status calls return a `long long` and do
 *       not allocate (the resolver's transient `getaddrinfo` list is freed in-call).
 */
#include <string>

namespace cheatah::socket {

// ---- high-level convenience (what a server/client usually wants) ----

/**
 * Create a TCP socket bound to @p host:@p port and put it in the listening state (sets
 *   `SO_REUSEADDR`).
 *
 * Performs socket()/set_reuseaddr/bind/listen in one shot; on any failure it closes the
 *   partially-created socket and returns -1, so the caller never leaks an fd. On success the
 *   returned fd is owned by the caller and must be passed to close() when done.
 * @param host interface to bind ("127.0.0.1", "0.0.0.0", …).
 * @param port TCP port (0 = let the OS pick — read it back with local_port()).
 * @param backlog pending-connection queue length.
 * @return the listening fd, or -1 on error.
 * @complexity O(1) + resolution (a few syscalls).
 * @alloc none (the resolver's transient `getaddrinfo` list is freed before returning).
 * @test CheatahSocket.Loopback
 * @crtest SocketCompileRun.TcpListen
 * @systest StdlibE2E.Socket
 */
long long tcp_listen(const std::string& host, long long port, long long backlog);

/**
 * Create a TCP socket and connect it to @p host:@p port.
 *
 * The host is resolved via `getaddrinfo`, so names, "localhost", and dotted IPs all work; the
 *   connect blocks until the handshake completes or fails. On failure the socket is closed and
 *   -1 is returned; on success the caller owns the connected fd and must close() it.
 * @param host destination host (name or IP).
 * @param port destination port.
 * @return the connected fd, or -1 on error.
 * @complexity O(1) + DNS resolution.
 * @alloc none (the resolver's transient `getaddrinfo` list is freed before returning).
 * @concurrency blocks until the TCP handshake completes or fails.
 * @test CheatahSocket.Loopback
 * @crtest SocketCompileRun.TcpConnect
 * @systest StdlibE2E.Socket
 */
long long tcp_connect(const std::string& host, long long port);

// ---- per-connection I/O ----

/**
 * Accept one pending connection.
 *
 * Blocks until a client connects, then returns a new fd for that one connection (the listening
 *   fd stays open for further accepts). The returned client fd is owned by the caller and must be
 *   closed separately; the peer address is discarded.
 * @param fd a listening fd.
 * @return the connected client fd, or -1 on error.
 * @complexity O(1) syscall (blocks until a client arrives).
 * @alloc none.
 * @concurrency blocks the calling thread until a client connects.
 * @test CheatahSocket.Loopback
 * @crtest SocketCompileRun.Accept
 * @systest StdlibE2E.Socket
 */
long long accept(long long fd);

/**
 * Receive up to @p bufsize bytes.
 *
 * Blocks for one `recv` and returns whatever bytes arrive (possibly fewer than @p bufsize); the
 *   result is binary-safe, so a returned string may contain embedded NULs and its length is the
 *   true byte count. A clean EOF (peer closed) and an error both yield "", so check last_error()
 *   to tell them apart; @p bufsize <= 0 also returns "" without touching the socket.
 * @param fd a connected fd.
 * @param bufsize maximum bytes to read.
 * @return the bytes read (binary-safe), or "" on EOF/error.
 * @complexity O(@p bufsize).
 * @alloc allocates the returned string (and grows a reused per-thread scratch buffer
 *        up to @p bufsize on first use).
 * @concurrency blocks until data, EOF, or the set_timeout() deadline; a shutdown() from
 *              another thread wakes it with EOF.
 * @test CheatahSocket.Loopback
 * @crtest SocketCompileRun.Recv
 * @systest StdlibE2E.Socket
 */
std::string recv(long long fd, long long bufsize);

/**
 * Send some of @p data (one `send`).
 *
 * Issues a single `send`, which may transmit fewer bytes than supplied (a partial send); the
 *   caller is responsible for re-sending the remainder, or use sendall() to loop automatically.
 * @param fd a connected fd.
 * @param data bytes to send.
 * @return bytes actually sent, or -1 on error.
 * @complexity O(n).
 * @alloc none (`MSG_NOSIGNAL`, so a broken pipe never raises `SIGPIPE`).
 * @test CheatahSocket.Sendall
 * @crtest SocketCompileRun.Send
 * @systest StdlibE2E.Socket
 */
long long send(long long fd, const std::string& data);

/**
 * Send @p data in full, looping until all bytes are written.
 *
 * Repeatedly calls `send` on the unsent remainder until every byte is written, so unlike send()
 *   there are no partial sends to handle; it aborts with -1 the moment a `send` returns <= 0
 *   (error or peer hang-up), in which case some bytes may already have been transmitted.
 * @param fd a connected fd.
 * @param data bytes to send.
 * @return 0 on success, -1 on error.
 * @complexity O(n).
 * @alloc none.
 * @concurrency may block while the peer's receive window is full; bounded per `send`
 *              by the set_timeout() send deadline.
 * @test CheatahSocket.Sendall
 * @crtest SocketCompileRun.Sendall
 * @systest StdlibE2E.Socket
 */
long long sendall(long long fd, const std::string& data);

/**
 * Close a socket.
 *
 * Releases the fd back to the OS; after this the fd is invalid and must not be reused. Closing an
 *   already-closed or never-opened fd fails with -1 (EBADF), which is how the BadFd test exercises
 *   the error path.
 * @param fd the fd to close.
 * @return 0 on success, -1 on error.
 * @complexity O(1) syscall.
 * @alloc none.
 * @test CheatahSocket.BadFd
 * @crtest SocketCompileRun.Close
 * @systest StdlibE2E.Socket
 */
long long close(long long fd);

/**
 * Half-close @p fd in both directions (::shutdown SHUT_RDWR) WITHOUT releasing it.
 * A blocking recv() on another thread returns immediately (EOF) — the safe way to
 * wake a reader for a clean shutdown. The fd is still owned by the caller and must
 * be close()d afterwards.
 * @param fd the fd to half-close.
 * @return 0 on success, -1 on error.
 * @complexity O(1) syscall.
 * @alloc none.
 * @concurrency safe to call from another thread while a recv() on @p fd blocks — waking
 *              that reader is exactly what it is for.
 * @test CheatahSocket.TimeoutThenShutdown
 */
long long shutdown(long long fd);

// ---- low-level BSD primitives (for clients/servers built by hand) ----

/**
 * Create an IPv4 TCP socket.
 *
 * Allocates an unbound, unconnected AF_INET/SOCK_STREAM fd; you must follow up with bind()+listen()
 *   or connect() before it can carry data, and close() it when done.
 * @return the new fd, or -1 on error.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahSocket.ListenLowLevel
 * @crtest SocketCompileRun.Socket
 * @systest StdlibE2E.Socket
 */
long long socket();

/**
 * Enable `SO_REUSEADDR` on @p fd.
 *
 * Lets a subsequent bind() reuse a local address still lingering in TIME_WAIT, so a restarted
 *   server can re-listen on the same port immediately; call it before bind().
 * @warning `SO_REUSEADDR` trades TIME_WAIT protection for restartability: by skipping the
 *          kernel's cooldown, delayed segments from a previous connection on the same
 *          address can in principle reach the new socket.
 * @param fd the socket.
 * @return 0 on success, -1 on error.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahSocket.ListenLowLevel
 * @crtest SocketCompileRun.SetReuseaddr
 * @systest StdlibE2E.Socket
 */
long long set_reuseaddr(long long fd);

/**
 * Bind @p fd to @p host:@p port.
 *
 * Resolves @p host via `getaddrinfo` and assigns the resulting local address to the socket; a
 *   resolution failure returns -1 with errno set to EADDRNOTAVAIL (see the ResolveFailure test).
 * @param fd the socket.
 * @param host interface to bind.
 * @param port TCP port (0 = OS-assigned).
 * @return 0 on success, -1 on error.
 * @complexity O(1) + resolution.
 * @alloc none (the resolver's transient `getaddrinfo` list is freed before returning).
 * @test CheatahSocket.ListenLowLevel, CheatahSocket.ResolveFailure
 * @crtest SocketCompileRun.Bind
 * @systest StdlibE2E.Socket
 */
long long bind(long long fd, const std::string& host, long long port);

/**
 * Mark @p fd as a passive (listening) socket.
 *
 * Switches an already-bound socket into the listening state so accept() can pull connections from
 *   it; @p backlog caps how many fully-established connections may queue before new ones are refused.
 * @param fd the socket.
 * @param backlog queue length.
 * @return 0 on success, -1 on error.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahSocket.ListenLowLevel
 * @crtest SocketCompileRun.Listen
 * @systest StdlibE2E.Socket
 */
long long listen(long long fd, long long backlog);

/**
 * Connect @p fd to @p host:@p port.
 *
 * Resolves @p host and blocks until the TCP handshake succeeds or fails; a refused connection
 *   returns -1 with errno ECONNREFUSED (see the ConnectRefused test). Unlike tcp_connect() it does
 *   not close the fd on failure — the caller still owns @p fd.
 * @param fd the socket.
 * @param host destination.
 * @param port destination port.
 * @return 0 on success, -1 on error.
 * @complexity O(1) + DNS.
 * @alloc none (the resolver's transient `getaddrinfo` list is freed before returning).
 * @concurrency blocks until the TCP handshake completes or fails.
 * @test CheatahSocket.ConnectRefused
 * @crtest SocketCompileRun.Connect
 * @systest StdlibE2E.Socket
 */
long long connect(long long fd, const std::string& host, long long port);

/**
 * The local TCP port @p fd is bound to (useful after binding to port 0).
 *
 * Reads the address actually assigned via `getsockname` and returns its port in host byte order;
 *   this is the way to discover the ephemeral port the OS chose when you bound to port 0.
 * @param fd a bound socket.
 * @return the port, or -1 on error.
 * @complexity O(1) syscall.
 * @alloc none.
 * @test CheatahSocket.Loopback
 * @crtest SocketCompileRun.LocalPort
 * @systest StdlibE2E.Socket
 */
long long local_port(long long fd);

/**
 * Bound both blocking directions of @p fd by @p timeout_ms (SO_RCVTIMEO + SO_SNDTIMEO), so a
 * silent peer cannot hang a recv/send forever. A recv that times out returns "" (check
 * last_error() to distinguish from EOF). @p timeout_ms <= 0 clears the timeouts (block forever).
 *
 * @param fd a socket.
 * @param timeout_ms the per-operation bound in milliseconds.
 * @return 0 on success, -1 on error.
 * @complexity O(1) (two setsockopt calls).
 * @alloc none.
 * @test CheatahSocket.TimeoutThenShutdown
 */
long long set_timeout(long long fd, long long timeout_ms);

/**
 * The message for the current `errno`.
 *
 * Returns the human-readable text for the thread's current `errno`; call it right after a function
 *   reports failure (a -1 return, or "" from recv), since any later syscall may overwrite `errno`.
 * @return `strerror(errno)`.
 * @complexity O(1).
 * @alloc allocates the returned string.
 * @test CheatahSocket.ConnectRefused
 * @crtest SocketCompileRun.LastError
 * @systest StdlibE2E.Socket
 */
std::string last_error();

// ---- owning RAII connections (the `with`-friendly, leak-proof API) ----

/**
 * @brief An owning TCP connection — a socket fd whose destructor closes it.
 *
 * The RAII counterpart to the fd-based calls above, and the C++/cheatah analog of a
 * Python socket used in a `with` block. A `Conn` owns exactly one fd; when it is
 * destroyed (scope exit, including a `return`/`break`/exception out of a `with` body)
 * or explicitly close()d, the fd is released — so a connection opened with
 * `with socket.open(host, port) as c { … }` cannot leak. Move-only: copying would give
 * two owners of one fd and double-close it, so the copy operations are deleted and a
 * moved-from `Conn` is left closed.
 */
class Conn {
public:
    /**
     * Construct a closed connection (owns no fd).
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ConnDefaultIsClosed
     */
    Conn() = default;
    /**
     * Adopt an already-connected fd (e.g. from tcp_connect()/accept()); the `Conn` now owns it.
     * @param fd a connected fd to take ownership of (-1 for a closed connection).
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ConnGuardClosesOnScopeExit
     */
    explicit Conn(long long fd) : fd_(fd) {}
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    /**
     * Move-construct, taking over @p other's fd (the moved-from `Conn` becomes closed).
     * @param other the connection to move from.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ConnMoveTransfersOwnership
     */
    Conn(Conn&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    /**
     * Move-assign, closing this fd first, then taking over @p other's (which becomes closed).
     * @param other the connection to move from.
     * @return reference to this connection.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ConnMoveTransfersOwnership
     */
    Conn& operator=(Conn&& other) noexcept;
    /**
     * Close the fd if still open.
     * @complexity O(1) syscall.
     * @alloc none.
     * @test CheatahSocket.ConnGuardClosesOnScopeExit
     */
    ~Conn();

    /**
     * Is a connection open?
     * @return true iff this owns an open fd.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ConnDefaultIsClosed
     */
    bool is_open() const { return fd_ >= 0; }
    /**
     * The raw fd, for the low-level calls or to hand to tls.open(conn.fd(), …).
     * @return the owned fd, or -1 when closed.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ConnGuardClosesOnScopeExit
     */
    long long fd() const { return fd_; }
    /**
     * Send some of @p data (one send(); see the free send()).
     * @param data bytes to send.
     * @return bytes actually sent, or -1 on error.
     * @complexity O(n).
     * @alloc none.
     * @test CheatahSocket.ConnLoopback
     */
    long long send(const std::string& data) const;
    /**
     * Send @p data in full, looping until every byte is written (see the free sendall()).
     * @param data bytes to send.
     * @return 0 on success, -1 on error.
     * @complexity O(n).
     * @alloc none.
     * @test CheatahSocket.ConnLoopback
     */
    long long sendall(const std::string& data) const;
    /**
     * Receive up to @p bufsize bytes (see the free recv()).
     * @param bufsize maximum bytes to read.
     * @return the bytes read (binary-safe), or "" on EOF/error.
     * @complexity O(@p bufsize).
     * @alloc allocates the returned string (plus the free recv()'s reused per-thread
     *        scratch buffer on growth).
     * @concurrency blocks until data, EOF, or the set_timeout() deadline.
     * @test CheatahSocket.ConnLoopback
     */
    std::string recv(long long bufsize) const;
    /**
     * Bound both blocking directions by @p timeout_ms (see the free set_timeout()).
     * @param timeout_ms per-operation bound in milliseconds (<= 0 clears it).
     * @return 0 on success, -1 on error.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ConnLoopback
     */
    long long set_timeout(long long timeout_ms) const;
    /**
     * The local TCP port this fd is bound to (see the free local_port()).
     * @return the port, or -1 on error.
     * @complexity O(1) syscall.
     * @alloc none.
     * @test CheatahSocket.ConnLoopback
     */
    long long local_port() const;
    /**
     * Half-close both directions WITHOUT releasing the fd (see the free shutdown()) — wakes a
     * blocked reader for a clean shutdown; still call close() (or let the destructor) afterward.
     * @return 0 on success, -1 on error.
     * @complexity O(1) syscall.
     * @alloc none.
     * @test CheatahSocket.ConnLoopback
     */
    long long shutdown() const;
    /**
     * Close the fd now (idempotent — the destructor will not close it again).
     * @return 0 on success, -1 if already closed or on error.
     * @complexity O(1) syscall.
     * @alloc none.
     * @test CheatahSocket.ConnGuardClosesOnScopeExit
     */
    long long close();

private:
    long long fd_ = -1;
};

/**
 * @brief An owning listening socket; accept() yields owning Conn clients; the destructor closes it.
 *
 * The server-side RAII guard: `with socket.serve(host, port, backlog) as server { … }` keeps the
 * listening fd for the block and closes it on exit. Each accept() returns an owning Conn, so a
 * whole server loop leaks neither the listener nor its clients. Move-only, like Conn.
 */
class Listener {
public:
    /**
     * Construct a closed listener (owns no fd).
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ListenerDefaultIsClosed
     */
    Listener() = default;
    /**
     * Adopt an already-listening fd (e.g. from tcp_listen()); the `Listener` now owns it.
     * @param fd a listening fd to take ownership of (-1 for a closed listener).
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ListenerLoopback
     */
    explicit Listener(long long fd) : fd_(fd) {}
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    /**
     * Move-construct, taking over @p other's fd (the moved-from `Listener` becomes closed).
     * @param other the listener to move from.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ListenerLoopback
     */
    Listener(Listener&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    /**
     * Move-assign, closing this fd first, then taking over @p other's (which becomes closed).
     * @param other the listener to move from.
     * @return reference to this listener.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ListenerLoopback
     */
    Listener& operator=(Listener&& other) noexcept;
    /**
     * Close the listening fd if still open.
     * @complexity O(1) syscall.
     * @alloc none.
     * @test CheatahSocket.ListenerLoopback
     */
    ~Listener();

    /**
     * Is the listener open?
     * @return true iff this owns an open listening fd.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ListenerDefaultIsClosed
     */
    bool is_open() const { return fd_ >= 0; }
    /**
     * The raw listening fd.
     * @return the owned fd, or -1 when closed.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahSocket.ListenerLoopback
     */
    long long fd() const { return fd_; }
    /**
     * Accept one pending connection, returned as an owning Conn (the listener stays open).
     * @return an owning Conn for the client (its is_open() is false on error).
     * @complexity O(1) syscall (blocks until a client arrives).
     * @alloc none.
     * @concurrency blocks the calling thread until a client connects.
     * @test CheatahSocket.ConnLoopback
     */
    Conn accept() const;
    /**
     * The local TCP port this listener is bound to (useful after binding to port 0).
     * @return the port, or -1 on error.
     * @complexity O(1) syscall.
     * @alloc none.
     * @test CheatahSocket.ListenerLoopback
     */
    long long local_port() const;
    /**
     * Close the listening fd now (idempotent — the destructor will not close it again).
     * @return 0 on success, -1 if already closed or on error.
     * @complexity O(1) syscall.
     * @alloc none.
     * @test CheatahSocket.ListenerLoopback
     */
    long long close();

private:
    long long fd_ = -1;
};

/**
 * Open a client TCP connection to @p host:@p port and return it as an owning Conn (the
 * RAII, `with`-friendly form of tcp_connect()).
 * @param host destination host (name or IP).
 * @param port destination port.
 * @return an owning Conn; on failure its is_open() is false (see last_error()).
 * @complexity O(1) + DNS resolution.
 * @alloc none beyond the Conn itself.
 * @concurrency blocks until the TCP handshake completes or fails.
 * @test CheatahSocket.ConnLoopback
 */
Conn open(const std::string& host, long long port);

/**
 * Create a listening server socket bound to @p host:@p port and return it as an owning
 * Listener (the RAII, `with`-friendly form of tcp_listen()).
 * @param host interface to bind ("127.0.0.1", "0.0.0.0", …).
 * @param port TCP port (0 = let the OS pick — read it back with Listener::local_port()).
 * @param backlog pending-connection queue length.
 * @return an owning Listener; on failure its is_open() is false (see last_error()).
 * @complexity O(1) + resolution (a few syscalls).
 * @alloc none beyond the Listener itself.
 * @test CheatahSocket.ListenerLoopback
 */
Listener serve(const std::string& host, long long port, long long backlog);

} // namespace cheatah::socket
