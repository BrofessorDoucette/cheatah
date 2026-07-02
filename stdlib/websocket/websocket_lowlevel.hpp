#pragma once

/**
 * @file websocket_lowlevel.hpp
 * @brief cheatah `websocket` — the LOW-LEVEL, handle-based C++ API (C++ callers only).
 *
 * These raw functions create and drive a WebSocket session by an integer id: connect()/
 * connect_url() return a session id (a heap-allocated Session cast to a handle); send_text/
 * recv/close/shutdown take it. Because the session must be close()d by hand to free it, they
 * are a MANUAL-OWNERSHIP surface and are intentionally kept OUT of the cheatah-facing
 * `websocket.hpp` — a cheatah program cannot reach them (so it cannot leak the Session), and
 * uses the owning `websocket::Client` guard + `websocket.open()`/`open_url()` instead (see
 * websocket.hpp). `websocket::Client` is implemented on top of this low-level API.
 */

#include <string>

#include "websocket.hpp"

namespace cheatah::websocket {

/**
 * Open a secure WebSocket connection: TCP connect, TLS 1.3 handshake, then the
 * RFC 6455 upgrade handshake (a `Sec-WebSocket-Key` from the OS CSPRNG; the
 * server's `101 Switching Protocols` is required). Returns a session id used by
 * the other calls.
 * @param host the server host, e.g. "echo.websocket.org".
 * @param port the TLS port, normally 443.
 * @param path the request path, e.g. "/".
 * @param server_name the TLS SNI / Host (usually == @p host).
 * @return a session id (>= 0).
 * @throws std::runtime_error on connect/TLS/upgrade failure (message names the cause).
 * @complexity one TCP + one TLS handshake + one HTTP round trip.
 * @alloc the session (its reused read buffer is reserved here).
 * @test CheatahWebSocket.ConnectRejectsNon101
 * @systest WebSocketSys.EchoRoundTrip
 */
long long connect(const std::string& host, long long port, const std::string& path,
                  const std::string& server_name);

/**
 * Connect from a `wss://host[:port]/path` URL (convenience over connect()).
 * Only the wss scheme is accepted; port defaults to 443, path to "/".
 * @param url the wss URL.
 * @return a session id (>= 0).
 * @throws std::runtime_error on a non-wss scheme or a connect failure.
 * @complexity as connect().
 * @alloc the session.
 * @test CheatahWebSocket.ParsesWssUrl
 */
long long connect_url(const std::string& url);

/**
 * Send one application TEXT message as a single masked frame (clients MUST mask,
 * RFC 6455 §5.3; the 4-byte key is drawn from the OS CSPRNG). Suitable for the
 * JSON control/subscribe messages WebSocket APIs expect.
 * @param session the session id.
 * @param message the UTF-8 payload.
 * @return the number of payload bytes sent.
 * @throws std::runtime_error on a transport error.
 * @complexity O(message length) (a 64-bit-word XOR mask + one TLS write).
 * @alloc one frame buffer sized to the message.
 * @test CheatahWebSocket.SendMasksClientFrame
 * @systest WebSocketSys.EchoRoundTrip
 */
long long send_text(long long session, const std::string& message);

/**
 * Receive the next application message (text or binary), returned as bytes.
 * Reassembles fragmented frames and transparently answers control frames — a
 * ping is replied to with a pong, a close ends the stream — none of which the
 * caller sees. Blocks until a full application message arrives.
 * @param session the session id.
 * @return the message payload; an EMPTY string once the peer has closed.
 * @throws std::runtime_error on a transport/protocol error.
 * @complexity O(message length); server frames are unmasked so no XOR is done.
 * @alloc the returned payload (one copy out of the reused read buffer).
 * @test CheatahWebSocket.RecvReassemblesAndHandlesPing
 * @systest WebSocketSys.EchoRoundTrip
 */
std::string recv(long long session);

/**
 * Close the connection: send a close frame, then tear down TLS and the socket.
 * Idempotent; the session id is invalid afterward.
 * @param session the session id.
 * @return 0 on success.
 * @complexity one TLS write + teardown.
 * @alloc a small close frame (when still open).
 * @test CheatahWebSocket.CloseIsIdempotent
 */
long long close(long long session);

/**
 * Wake a reader blocked in recv() on @p session WITHOUT freeing it (half-closes the
 * socket so recv returns ""). The clean-shutdown sequence from another thread is:
 * set your stop flag, shutdown(session), join the reader, THEN close(session).
 * Safe to call concurrently with the reader's recv.
 * @param session the session whose blocked reader should be woken.
 * @return 0 on success, -1 for an unknown session or a syscall error.
 * @complexity O(1) + one syscall. @alloc none.
 */
long long shutdown(long long session);

} // namespace cheatah::websocket
