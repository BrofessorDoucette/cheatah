// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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

#include <cstdint>
#include <string>

#include "websocket.hpp"

namespace cheatah::websocket {

/**
 * Open a secure WebSocket connection: TCP connect, TLS 1.3 handshake, then the
 * RFC 6455 upgrade handshake (a `Sec-WebSocket-Key` from the OS CSPRNG; the
 * server's `101 Switching Protocols` is required). Returns a session id used by
 * the other calls.
 * The TLS server is AUTHENTICATED by default (cert chain + hostname + expiry, like `tls::open`).
 * @param host the server host, e.g. "echo.websocket.org".
 * @param port the TLS port, normally 443.
 * @param path the request path, e.g. "/".
 * @param server_name the TLS SNI / Host (usually == @p host); matched against the certificate SAN.
 * @param insecure skip certificate validation (pinned/controlled peer only). Default false.
 * @param ca_file a PEM CA bundle to trust instead of the system store (empty = system default).
 * @param secure whether to run the connection over TLS. Default TRUE; false selects a
 *        PLAINTEXT WebSocket and is refused unless @p host is loopback.
 * @return a session id (>= 0).
 * @throws std::runtime_error on connect/TLS/validation/upgrade failure (message names the cause).
 * TLS IS THE DEFAULT AND STAYS THE DEFAULT. @p secure = false selects a PLAINTEXT WebSocket,
 * and connect() then refuses any host that is not loopback (127.0.0.1, ::1, localhost). It
 * exists for a local control plane — Chrome's DevTools endpoint speaks ws:// on loopback and
 * offers no TLS at all — so cleartext here can never reach the network. Every existing caller
 * is unchanged: omit the parameter and you get TLS.
 *
 * @warning @p insecure = true drops the MITM protection: ANY peer that holds its own
 *          certificate's key is accepted. Pinned/controlled peers only.
 * @complexity one TCP + one TLS handshake + one HTTP round trip.
 * @alloc the session and its underlying TLS session (the reused read buffer starts out
 *        holding any frame bytes that followed the upgrade response).
 * @concurrency blocks for the TCP/TLS/upgrade round trips.
 * @systest WebSocketSys.RefusesNon101
 * @systest WebSocketSys.EchoRoundTrip
 */
long long connect(const std::string& host, long long port, const std::string& path,
                  const std::string& server_name, bool insecure = false,
                  const std::string& ca_file = "", bool secure = true);

/**
 * Connect from a `wss://host[:port]/path` URL (convenience over connect()).
 * wss:// (TLS, port 443) and ws:// (PLAINTEXT, port 80) are accepted; path defaults to "/".
 * ws:// is refused unless the host is loopback — see connect()'s @p secure.
 * @param url the wss URL.
 * @param insecure skip certificate validation (pinned/controlled peer only). Default false.
 * @param ca_file a PEM CA bundle to trust instead of the system store (empty = system default).
 * @return a session id (>= 0).
 * @throws std::runtime_error on a non-wss scheme or a connect/validation failure.
 * @warning @p insecure = true drops the MITM protection (see connect()).
 * @complexity as connect().
 * @alloc the session.
 * @test CheatahWebSocket.ConnectUrlRejectsNonWss
 * @systest WebSocketSys.ConnectUrlParsingBranches
 */
long long connect_url(const std::string& url, bool insecure = false, const std::string& ca_file = "");

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
 * @concurrency a session is single-owner — do not send on one session from two threads
 *              at once (see the threading note in websocket.hpp).
 * @systest WebSocketSys.EchoRoundTrip
 * @systest WebSocketSys.ExtendedLength64
 */
long long send_text(long long session, const std::string& message);

/**
 * Receive the next application message (text or binary), returned as bytes.
 * Reassembles fragmented frames and transparently answers control frames — a
 * ping is replied to with a pong, a close ends the stream — none of which the
 * caller sees. Blocks until a full application message arrives.
 * @param session the session id.
 * @return the message payload; an EMPTY string once the peer has closed.
 * @throws std::runtime_error on a transport/protocol error, an oversized frame/message
 *         (over the per-frame / reassembly caps), or an RFC 6455 framing violation.
 * @complexity O(message length), BOUNDED by the frame + message caps — a hostile oversized
 *             frame is refused, not processed; server frames are unmasked so no XOR is done.
 * @alloc the returned payload (one copy out of the reused read buffer, which itself grows
 *        as the TLS stream is drained), bounded by the caps.
 * @concurrency blocks until a full message arrives; a session has ONE reader —
 *              shutdown() is the cross-thread wake-up.
 * @test CheatahWebSocket.AcceptsFragmentedMessage
 * @test CheatahWebSocket.SkipsPongThenReturnsData
 * @systest WebSocketSys.EchoRoundTrip
 */
std::string recv(long long session);

/**
 * Close the connection: send a close frame, then tear down TLS and the socket, and FREE the
 * heap Session. The session id is invalid afterward — never pass it to any call (including a
 * second close) again: the handle is the freed Session's address, so reuse is use-after-free.
 * The idempotent form is the owning guard's Client::close().
 * @param session the session id.
 * @return 0 on success.
 * @complexity one TLS write + teardown.
 * @alloc a small close frame (when still open).
 * @systest WebSocketSys.EchoRoundTrip
 * @systest WebSocketSys.ServerCloseYieldsEmptyRecv
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
 * @concurrency safe to call from another thread while the owner's recv() blocks —
 *              that wake-up is its purpose.
 * @systest WebSocketSys.ClientGuardRoundTrip
 */
long long shutdown(long long session);

#ifdef CHEATAH_WEBSOCKET_TESTING
namespace testonly {
/**
 * White-box seam (test builds only): create a session pre-loaded with raw frame bytes and
 * overridable per-session caps, so recv()'s RFC 6455 frame validation can be exercised against
 * crafted/hostile server frames with no TLS/socket. Free the returned handle with close().
 * @param frames the raw bytes recv() will parse (must be self-contained).
 * @param max_frame per-frame payload cap (0 -> default).
 * @param max_message reassembled-message cap (0 -> default).
 * @return a session handle usable with recv()/close().
 */
long long session_from_bytes(const std::string& frames, std::uint64_t max_frame,
                             std::uint64_t max_message);

/**
 * White-box seam (test builds only): drive the upgrade-request send FAILURE branch, which
 * connect() cannot reach without racing a peer reset. Uses an invalid descriptor, so the send
 * fails with EBADF deterministically.
 * @throws std::runtime_error always — "websocket: upgrade request failed: ...".
 * @complexity O(1).
 * @alloc one Session, destroyed on the throw path.
 * @test WebSocketPlaintext.UpgradeSendFailureIsReported
 */
void send_upgrade_on_closed_fd();

/**
 * White-box seam (test builds only): wrap an already-connected socket as a PLAINTEXT (`ws://`)
 * session, the transport a loopback connection uses. Lets shutdown()/close() be exercised on the
 * plaintext branch without standing up a TLS peer.
 * @param fd a connected socket the session takes ownership of.
 * @return a session handle usable with shutdown()/close().
 * @complexity O(1).
 * @alloc one Session.
 * @test CheatahWebSocket.PlaintextShutdownAndCloseUseTheSocket
 */
long long plaintext_session_on_fd(long long fd);
}  // namespace testonly
#endif  // CHEATAH_WEBSOCKET_TESTING

} // namespace cheatah::websocket
