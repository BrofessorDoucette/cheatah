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
 *
 * Performs socket()/set_reuseaddr/bind/listen in one shot; on any failure it closes the
 *   partially-created socket and returns -1, so the caller never leaks an fd. On success the
 *   returned fd is owned by the caller and must be passed to close() when done.
 * @param host interface to bind ("127.0.0.1", "0.0.0.0", …).
 * @param port TCP port (0 = let the OS pick — read it back with local_port()).
 * @param backlog pending-connection queue length.
 * @return the listening fd, or -1 on error.
 * @complexity O(1) (a few syscalls).
 * @alloc none.
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
 * @alloc none.
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
 * @alloc allocates the returned string.
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
 * @alloc none.
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
 * @alloc none.
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

} // namespace cheatah::socket
