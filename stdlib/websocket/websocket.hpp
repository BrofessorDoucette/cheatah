#pragma once

/**
 * @file websocket.hpp
 * @brief cheatah `websocket` — a from-scratch, low-latency WebSocket CLIENT
 *        (RFC 6455) over the cheatah `tls` 1.3 client and `socket`. `import
 *        websocket` to use it. No external libraries.
 *
 * Built for SPEED. The receive path is the hot path and is allocation-quiet:
 *  - one read buffer per session, REUSED across every frame (no per-frame heap);
 *  - server-to-client frames are unmasked by the protocol (RFC 6455 §5.1), so
 *    recv does ZERO unmasking work — it slices the payload straight out of the
 *    buffer;
 *  - frame headers are parsed in place (no header object is materialized);
 *  - the TLS layer is drained in large chunks, so many frames are decoded per
 *    underlying read/decrypt.
 * The send path masks (clients MUST, §5.3) with a 64-bit-word XOR (8 bytes per
 * step), but sends are rare (subscribe/control) so they are off the hot path.
 *
 * The cheatah-facing API is the owning `websocket::Client` guard, created by
 * `websocket.open(...)` / `websocket.open_url("wss://...")`: it sends a close frame and tears
 * down the TLS session + socket automatically when it goes out of scope, so a cheatah program
 * cannot leak the heap Session. wss:// only (WebSocket over TLS) — the transport is always
 * encrypted, like the rest of cheatah's net stack. The flat handle-based calls (an integer
 * session id) live in websocket_lowlevel.hpp (C++ only).
 *
 * Threading: a session is single-owner; do not call recv and send for the same
 * session from two threads at once. Separate sessions are independent.
 */

#include <string>

namespace cheatah::websocket {

// The low-level, handle-based API (connect / connect_url / send_text / recv / close / shutdown,
// keyed by an integer session id backed by a heap Session) is C++-only and lives in
// websocket_lowlevel.hpp. It is intentionally NOT part of this cheatah-facing header: a cheatah
// program cannot reach it, so it cannot leak the heap Session — it uses the owning
// `websocket::Client` guard + `websocket.open()`/`open_url()` below, which free the Session
// automatically at scope exit. `websocket::Client` is implemented on top of that low-level API.

// ---- owning RAII client (the `with`-friendly, leak-proof API) ----

/**
 * @brief An owning WebSocket client — closes the connection (frame + TLS + socket) on destruction.
 *
 * The RAII counterpart to the handle-based calls above. A `Client` owns one session; when it is
 * destroyed (scope exit out of a `with` body, including via exception) or explicitly close()d, the
 * close frame is sent and the TLS session and TCP socket are torn down, so
 * `with websocket.open_url(url) as ws { … }` cannot leak the session, its `tls` session, or its
 * fd. Move-only: the copy operations are deleted and a moved-from `Client` is left closed.
 */
class Client {
public:
    /**
     * Construct a closed client (owns nothing).
     * @complexity O(1).
     * @alloc none.
     * @test CheatahWebSocket.ClientDefaultIsClosed
     */
    Client() = default;
    /**
     * Adopt an existing session handle (e.g. from connect()); the `Client` now owns it.
     * @param session a session handle to take ownership of (0 for a closed client).
     * @complexity O(1).
     * @alloc none.
     * @test CheatahWebSocket.ClientMoveTransfersOwnership
     */
    explicit Client(long long session) : session_(session) {}
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    /**
     * Move-construct, taking over @p other's session (the moved-from `Client` becomes closed).
     * @param other the client to move from.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahWebSocket.ClientMoveTransfersOwnership
     */
    Client(Client&& other) noexcept : session_(other.session_) { other.session_ = 0; }
    /**
     * Move-assign, closing this session first, then taking over @p other's (which becomes closed).
     * @param other the client to move from.
     * @return reference to this client.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahWebSocket.ClientMoveTransfersOwnership
     */
    Client& operator=(Client&& other) noexcept;
    /**
     * Close the connection if still open (close frame + TLS + socket teardown).
     * @complexity one TLS write + teardown.
     * @alloc a small close frame (when still open).
     * @test CheatahWebSocket.ClientDefaultIsClosed
     */
    ~Client();

    /**
     * Is a connection open?
     * @return true iff this owns an open session.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahWebSocket.ClientDefaultIsClosed
     */
    bool is_open() const { return session_ != 0; }
    /**
     * The raw session handle (for the low-level calls).
     * @return the owned handle, or 0 when closed.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahWebSocket.ClientMoveTransfersOwnership
     */
    long long id() const { return session_; }
    /**
     * Send one application TEXT message as a single masked frame (see the free send_text()).
     * @param message the UTF-8 payload.
     * @return the number of payload bytes sent.
     * @throws std::runtime_error on a transport error.
     * @complexity O(message length).
     * @alloc one frame buffer sized to the message.
     * @systest WebSocketSys.EchoRoundTrip
     */
    long long send_text(const std::string& message);
    /**
     * Receive the next application message (see the free recv()).
     * @return the message payload; "" once the peer has closed.
     * @throws std::runtime_error on a transport/protocol error.
     * @complexity O(message length).
     * @alloc the returned payload.
     * @systest WebSocketSys.EchoRoundTrip
     */
    std::string recv();
    /**
     * Wake a reader blocked in recv() WITHOUT freeing the session (see the free shutdown()).
     * @return 0 on success, -1 on error.
     * @complexity O(1) + one syscall.
     * @alloc none.
     * @systest WebSocketSys.EchoRoundTrip
     */
    long long shutdown();
    /**
     * Close the connection now (idempotent — the destructor will not close it again).
     * @return 0 on success, -1 if already closed.
     * @complexity one TLS write + teardown.
     * @alloc a small close frame (when still open).
     * @test CheatahWebSocket.ClientDefaultIsClosed
     */
    long long close();

private:
    long long session_ = 0;
};

/**
 * Open a secure WebSocket connection and return it as an owning Client (the RAII,
 * `with`-friendly form of connect()).
 * @param host the server host, e.g. "echo.websocket.org".
 * @param port the TLS port, normally 443.
 * @param path the request path, e.g. "/".
 * @param server_name the TLS SNI / Host (usually == @p host).
 * @return an owning Client.
 * @throws std::runtime_error on connect/TLS/upgrade failure.
 * @complexity one TCP + one TLS handshake + one HTTP round trip.
 * @alloc the session.
 * @systest WebSocketSys.EchoRoundTrip
 */
Client open(const std::string& host, long long port, const std::string& path,
            const std::string& server_name);

/**
 * Open a secure WebSocket connection from a `wss://host[:port]/path` URL and return it as an
 * owning Client (the RAII, `with`-friendly form of connect_url()).
 * @param url the wss URL.
 * @return an owning Client.
 * @throws std::runtime_error on a non-wss scheme or a connect failure.
 * @complexity as open().
 * @alloc the session.
 * @test CheatahWebSocket.ClientDefaultIsClosed
 */
Client open_url(const std::string& url);

}  // namespace cheatah::websocket
