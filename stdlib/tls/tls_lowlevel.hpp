#pragma once

/**
 * @file tls_lowlevel.hpp
 * @brief cheatah `tls` — the LOW-LEVEL, handle-based C++ API (C++ callers only).
 *
 * These raw functions create and drive a TLS session by an integer id: client_connect()
 * returns a session id; send/recv/close/shutdown take it. Because a session id must be
 * closed by hand, they are a MANUAL-OWNERSHIP surface and are intentionally kept OUT of the
 * cheatah-facing `tls.hpp` — a cheatah program cannot reach them (so it cannot leak a
 * session), and uses the owning `tls::Conn` guard + `tls.open()` instead (see tls.hpp). This
 * header exists for C++ code (and the module's own implementation/tests) that needs the flat
 * handle API; `tls::Conn` is implemented on top of it.
 */
#include <string>

#include "tls.hpp"

namespace cheatah::tls {

/**
 * Run the TLS 1.3 handshake as a client over connected fd @p fd, requesting @p server_name
 * via SNI and authenticating the server as described in tls.hpp.
 *
 * @param fd a CONNECTED TCP socket.
 * @param server_name the hostname for SNI and (future) certificate name checks.
 * @return a session handle (>= 1), or -1 on failure (see last_error()).
 * @complexity one network round trip + O(handshake bytes) crypto.
 * @alloc the session state.
 * @test CheatahTls.KeySchedule
 * @crtest TlsSys.HandshakeAgainstOpenssl
 * @systest TlsSys.HttpsGet
 */
long long client_connect(long long fd, const std::string& server_name);

/**
 * Encrypt and send @p data as TLS application data.
 *
 * @param session a session handle from client_connect().
 * @param data the plaintext to encrypt and transmit.
 * @return 0 on success, -1 on error.
 * @complexity O(|data|).
 * @alloc the ciphertext record(s).
 * @test TlsSys.HandshakeAgainstOpenssl
 * @crtest TlsSys.HttpsGet
 * @systest TlsSys.HttpsGet
 */
long long send(long long session, const std::string& data);

/**
 * Receive and decrypt application data (one record's worth, up to @p bufsize bytes are
 * returned per call; the remainder is buffered). "" means clean close_notify, peer EOF,
 * or an error — check last_error().
 *
 * @param session a session handle from client_connect().
 * @param bufsize the maximum number of plaintext bytes to return this call.
 * @return the decrypted plaintext (up to @p bufsize bytes), or "" on clean close/EOF/error.
 * @complexity O(record size).
 * @alloc the returned plaintext.
 * @test TlsSys.HandshakeAgainstOpenssl
 * @crtest TlsSys.HttpsGet
 * @systest TlsSys.HttpsGet
 */
std::string recv(long long session, long long bufsize);

/**
 * Send close_notify and forget the session (the TCP fd stays open and is the caller's).
 *
 * @param session the session handle to close.
 * @return 0 on success, -1 for an unknown session.
 * @complexity O(log n) session-table lookup + a close_notify write.
 * @alloc a small close_notify record (when the session is still open).
 * @test TlsSys.HandshakeAgainstOpenssl
 * @crtest TlsSys.HttpsGet
 * @systest TlsSys.HttpsGet
 */
long long close(long long session);

/**
 * Wake a reader blocked in recv() on @p session WITHOUT closing/erasing it (the
 * socket is half-closed so the blocking recv returns EOF). For clean shutdown:
 * call shutdown(), join the reader thread, THEN close().
 * @param session the session whose blocked reader should be woken.
 * @return 0 on success, -1 for an unknown session or a syscall error.
 * @complexity O(log n) lookup + one syscall. @alloc none.
 */
long long shutdown(long long session);

} // namespace cheatah::tls
