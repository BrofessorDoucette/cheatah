// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// websocket.cpp — a fast, from-scratch RFC 6455 WebSocket client over the
// cheatah tls 1.3 client + socket. See websocket.hpp for the contract and the
// performance design (reused read buffer, unmasked-recv fast path, word-XOR
// masking on send, in-place header parsing, large TLS drains).

#include "websocket.hpp"
#include "websocket_lowlevel.hpp"  // the C++-only raw handle API this module implements (+ Client uses)
#include "tls_lowlevel.hpp"        // websocket rides tls's low-level (C++-only) session API

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "os.hpp"
#include "socket.hpp"
#include "tls.hpp"

namespace cheatah::websocket {

namespace {

// A session is heap-allocated and its address IS the handle — so send/recv do a
// single pointer cast, never a map lookup or lock, on the hot path (cheatah is
// single-trust; an invalid handle is a caller bug, like a bad pointer in C).
struct Session {
    long long fd = -1;       // the TCP fd (we own it; close it ourselves)
    long long tls = -1;      // the TLS session riding that fd
    std::string buf;         // read buffer, REUSED across every frame
    std::size_t pos = 0;     // parse offset into buf (consumed prefix is buf[0,pos))
    bool closed = false;
    std::uint64_t max_frame = 0;    // per-frame payload cap (0 -> the kMaxFramePayload default)
    std::uint64_t max_message = 0;  // reassembled-message cap (0 -> the kMaxMessageBytes default)
};

// Hard caps a client MUST impose itself — RFC 6455 sets no upper bound on a frame or a
// reassembled message, so a malicious server could otherwise (a) overflow `header + len`
// in size_t math and drive an out-of-bounds unmask, or (b) stream an unbounded body to
// exhaust memory. A single frame's payload and the reassembled message are each capped;
// anything larger fails the connection. Control frames are bounded to 125 bytes (§5.5).
constexpr std::uint64_t kMaxFramePayload = 64ull << 20;  // 64 MiB per frame
constexpr std::uint64_t kMaxMessageBytes = 64ull << 20;  // 64 MiB reassembled total
constexpr std::uint64_t kMaxControlPayload = 125;        // RFC 6455 §5.5

[[nodiscard]] Session* as_session(long long h) {
    return reinterpret_cast<Session*>(static_cast<std::uintptr_t>(h));  // NOLINT(performance-no-int-to-ptr): the integer handle IS the session address by contract (see as_handle)
}
[[nodiscard]] long long as_handle(Session* s) {
    return static_cast<long long>(reinterpret_cast<std::uintptr_t>(s));
}

// Standard base64 (for the Sec-WebSocket-Key). Tiny inputs (16/raw bytes).
[[nodiscard]] std::string base64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const std::uint32_t n = (static_cast<unsigned char>(in[i]) << 16) |
                                (static_cast<unsigned char>(in[i + 1]) << 8) |
                                static_cast<unsigned char>(in[i + 2]);
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(T[(n >> 6) & 63]);
        out.push_back(T[n & 63]);
    }
    if (i < in.size()) {
        std::uint32_t n = static_cast<unsigned char>(in[i]) << 16;
        if (i + 1 < in.size()) n |= static_cast<unsigned char>(in[i + 1]) << 8;
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(i + 1 < in.size() ? T[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

void destroy(Session* s) {
    if (s == nullptr) return;
    if (s->tls >= 0) tls::close(s->tls);
    if (s->fd >= 0) socket::close(s->fd);
    delete s;
}

// Ensure the buffer holds at least @p need unconsumed bytes (from pos), draining
// the TLS stream in big chunks. Compacts the consumed prefix only when needed,
// so steady-state framing does no front-erase per frame.
// The transport, chosen per session. A Session carries `tls = -1` when it is PLAINTEXT — a
// sentinel the struct has always declared and destroy() has always honoured. Everything above
// the transport (masking, framing, reassembly) is identical either way, so these two are the
// only places the difference exists.
//
// Pointer for the session, const reference for the payload: that is this file's convention
// (see send_frame), because a session arrives as a handle cast straight to Session* and never
// through a lookup, while a payload is an ordinary parameter.
long long sess_send(Session* s, const std::string& data) {
    return s->tls >= 0 ? tls::send(s->tls, data) : socket::send(s->fd, data);
}

std::string sess_recv(Session* s, long long n) {
    return s->tls >= 0 ? tls::recv(s->tls, n) : socket::recv(s->fd, n);
}

std::string sess_error(Session* s) {
    return s->tls >= 0 ? tls::last_error() : socket::last_error();
}

void ensure(Session* s, std::size_t need) {
    while (s->buf.size() - s->pos < need) {
        if (s->pos > 0 && (s->pos == s->buf.size() || s->pos >= (1u << 16))) {
            s->buf.erase(0, s->pos);
            s->pos = 0;
        }
        std::string chunk = sess_recv(s, 1 << 16);  // 64 KiB drains
        if (chunk.empty()) throw std::runtime_error("websocket: connection closed by peer");
        s->buf.append(chunk);
    }
}

// Build one frame header for a client (always-masked) text/control send.
void put_header(std::string& frame, unsigned char opcode, std::size_t n) {
    frame.push_back(static_cast<char>(0x80 | opcode));  // FIN | opcode
    if (n < 126) {
        frame.push_back(static_cast<char>(0x80 | n));  // MASK | len
    } else if (n <= 0xFFFF) {
        frame.push_back(static_cast<char>(0x80 | 126));
        frame.push_back(static_cast<char>((n >> 8) & 0xFF));
        frame.push_back(static_cast<char>(n & 0xFF));
    } else {
        frame.push_back(static_cast<char>(0x80 | 127));
        for (int sh = 56; sh >= 0; sh -= 8) frame.push_back(static_cast<char>((n >> sh) & 0xFF));
    }
}

// XOR-mask @p n bytes of @p src into @p dst with the 4-byte @p key, eight bytes
// per step (the key tiled into a 64-bit word) — the fast masking path.
void mask_into(char* dst, const char* src, std::size_t n, const unsigned char key[4]) {
    std::uint64_t k64 = 0;
    unsigned char tile[8] = {key[0], key[1], key[2], key[3], key[0], key[1], key[2], key[3]};
    std::memcpy(&k64, tile, 8);
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        std::uint64_t w = 0;
        std::memcpy(&w, src + i, 8);
        w ^= k64;
        std::memcpy(dst + i, &w, 8);
    }
    for (; i < n; ++i) dst[i] = static_cast<char>(src[i] ^ key[i & 3]);
}

long long send_frame(Session* s, unsigned char opcode, const std::string& payload) {
    const std::size_t n = payload.size();
    std::string frame;
    frame.reserve(n + 14);
    put_header(frame, opcode, n);
    const std::string key = os::urandom(4);
    const unsigned char k[4] = {static_cast<unsigned char>(key[0]), static_cast<unsigned char>(key[1]),
                                static_cast<unsigned char>(key[2]), static_cast<unsigned char>(key[3])};
    frame.append(reinterpret_cast<const char*>(k), 4);
    const std::size_t off = frame.size();
    frame.resize(off + n);
    mask_into(frame.data() + off, payload.data(), n, k);
    if (sess_send(s, frame) < 0)
        throw std::runtime_error("websocket: send failed: " + sess_error(s));
    return static_cast<long long>(n);
}

}  // namespace

/// @cond INTERNAL — the C++-only low-level session API (websocket_lowlevel.hpp); cheatah uses the Client guard
// Whether @p host names this machine. Plaintext is permitted ONLY here: a cleartext WebSocket
// that cannot leave the loopback interface is a local control plane (Chrome's DevTools port is
// the motivating one), not a network protocol.
bool is_loopback(const std::string& host) {
    return host == "127.0.0.1" || host == "::1" || host == "[::1]" || host == "localhost";
}

// Send the upgrade request, or destroy the session and report why.
//
// Extracted so the failure branch is REACHABLE from a test. It cannot be forced through
// connect(): it needs the peer to have reset the connection between tcp_connect returning and
// this very send, which is a race, and a test that only sometimes covers a line is a test that
// only sometimes passes. testonly::send_upgrade_on_closed_fd drives THIS function with an
// invalid fd, where socket::send fails with EBADF every time.
void send_upgrade(Session* s, const std::string& req) {
    if (sess_send(s, req) < 0) {
        const std::string err = sess_error(s);
        destroy(s);
        throw std::runtime_error("websocket: upgrade request failed: " + err);
    }
}

long long connect(const std::string& host, long long port, const std::string& path,
                  const std::string& server_name, bool insecure, const std::string& ca_file,
                  bool secure) {
    // TLS is the default and the norm. Plaintext must be asked for explicitly AND can only ever
    // reach this machine — so no amount of configuration turns this into a cleartext socket to
    // the internet.
    if (!secure && !is_loopback(host))
        throw std::runtime_error(
            "websocket: plaintext ws:// is allowed only to a loopback host (127.0.0.1, ::1, "
            "localhost); refusing " + host + " — use wss://");

    const long long fd = socket::tcp_connect(host, port);
    if (fd < 0) throw std::runtime_error("websocket: TCP connect to " + host + " failed");
    long long tlss = -1;
    if (secure) {
        tlss = tls::client_connect(fd, server_name, insecure, ca_file);
        if (tlss < 0) {
            socket::close(fd);
            throw std::runtime_error("websocket: TLS handshake failed: " + tls::last_error());
        }
    }
    auto* s = new Session();
    s->fd = fd;
    s->tls = tlss;      // -1 => plaintext; sess_send/sess_recv branch on it

    const std::string key = base64(os::urandom(16));
    const std::string req = "GET " + path + " HTTP/1.1\r\n" + "Host: " + server_name + "\r\n" +
                            "Upgrade: websocket\r\n" + "Connection: Upgrade\r\n" +
                            "Sec-WebSocket-Key: " + key + "\r\n" + "Sec-WebSocket-Version: 13\r\n\r\n";
    send_upgrade(s, req);

    // Read the response header block (up to and including the blank line). Any
    // bytes after it are the start of the WebSocket frame stream and stay in buf.
    std::size_t hdr_end = std::string::npos;
    for (;;) {
        const std::string chunk = sess_recv(s, 1 << 12);
        if (chunk.empty()) {
            destroy(s);
            throw std::runtime_error("websocket: connection closed during upgrade");
        }
        s->buf.append(chunk);
        hdr_end = s->buf.find("\r\n\r\n");
        if (hdr_end != std::string::npos) break;
        if (s->buf.size() > (1u << 16)) {
            destroy(s);
            throw std::runtime_error("websocket: upgrade response too large / not a WebSocket server");
        }
    }
    const std::string status = s->buf.substr(0, s->buf.find("\r\n"));
    if (status.find(" 101") == std::string::npos) {
        destroy(s);
        throw std::runtime_error("websocket: server did not switch protocols: " + status);
    }
    s->pos = hdr_end + 4;  // frame stream begins after the blank line
    return as_handle(s);
}

long long connect_url(const std::string& url, bool insecure, const std::string& ca_file) {
    // wss:// is the default and behaves exactly as it always has. ws:// exists for a local
    // control plane and has to be spelled out; connect() then refuses any non-loopback host.
    bool secure = true;
    std::string scheme = "wss://";
    if (url.compare(0, scheme.size(), scheme) != 0) {
        scheme = "ws://";
        if (url.compare(0, scheme.size(), scheme) != 0)
            throw std::runtime_error("websocket: only wss:// and ws:// URLs are supported: " + url);
        secure = false;
    }
    const std::size_t host_start = scheme.size();
    const std::size_t slash = url.find('/', host_start);
    const std::string authority =
        url.substr(host_start, slash == std::string::npos ? std::string::npos : slash - host_start);
    const std::string path = slash == std::string::npos ? "/" : url.substr(slash);
    const std::size_t colon = authority.find(':');
    const std::string host = authority.substr(0, colon);
    long long port = secure ? 443 : 80;  // the scheme's default port unless the authority names one
    if (colon != std::string::npos) port = static_cast<long long>(std::stol(authority.substr(colon + 1)));
    return connect(host, port, path, host, insecure, ca_file, secure);
}

long long send_text(long long session, const std::string& message) {
    return send_frame(as_session(session), 0x1, message);
}

std::string recv(long long session) {
    Session* s = as_session(session);
    if (s->closed) return {};
    const std::uint64_t max_frame = s->max_frame != 0 ? s->max_frame : kMaxFramePayload;
    const std::uint64_t max_message = s->max_message != 0 ? s->max_message : kMaxMessageBytes;
    std::string message;       // reassembly buffer for fragmented messages
    bool fragmenting = false;  // mid multi-frame message
    for (;;) {
        ensure(s, 2);
        const auto b0 = static_cast<unsigned char>(s->buf[s->pos]);
        const auto b1 = static_cast<unsigned char>(s->buf[s->pos + 1]);
        const bool fin = (b0 & 0x80) != 0;
        const int opcode = b0 & 0x0F;
        const bool masked = (b1 & 0x80) != 0;  // server frames are unmasked
        std::uint64_t len = b1 & 0x7F;
        std::size_t header = 2;
        if (len == 126) {
            ensure(s, 4);
            len = (static_cast<std::uint64_t>(static_cast<unsigned char>(s->buf[s->pos + 2])) << 8) |
                  static_cast<unsigned char>(s->buf[s->pos + 3]);
            header = 4;
        } else if (len == 127) {
            ensure(s, 10);
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | static_cast<unsigned char>(s->buf[s->pos + 2 + i]);
            header = 10;
        }
        // Validate BEFORE trusting `len` in size math or a copy: reserved bits must be
        // clear (no extension negotiated), a control frame must be <=125 bytes and not
        // fragmented, and no frame may exceed the cap. Capping `len` here is what makes
        // `header + len` below unable to overflow size_t.
        if ((b0 & 0x70) != 0) throw std::runtime_error("websocket: reserved bits set");
        if ((opcode & 0x08) != 0 && (len > kMaxControlPayload || !fin))
            throw std::runtime_error("websocket: invalid control frame");
        if (len > max_frame) throw std::runtime_error("websocket: frame too large");

        std::size_t mask_off = 0;
        if (masked) {
            mask_off = header;
            header += 4;
        }
        ensure(s, header + len);
        char* payload = s->buf.data() + s->pos + header;
        if (masked) {  // protocol-irregular for a server, but unmask defensively
            const unsigned char k[4] = {static_cast<unsigned char>(s->buf[s->pos + mask_off]),
                                        static_cast<unsigned char>(s->buf[s->pos + mask_off + 1]),
                                        static_cast<unsigned char>(s->buf[s->pos + mask_off + 2]),
                                        static_cast<unsigned char>(s->buf[s->pos + mask_off + 3])};
            mask_into(payload, payload, len, k);
        }

        switch (opcode) {
            // No braces on the control cases: neither declares anything needing the scope, and
            // a `}` after a continue/return is unreachable by construction — it shows up
            // forever as an uncovered line that no test can ever reach.
            case 0x9:  // ping -> pong with the same payload (control, off hot path)
                send_frame(s, 0xA, std::string(payload, len));
                s->pos += header + len;
                continue;
            case 0xA:  // pong -> ignore
                s->pos += header + len;
                continue;
            case 0x8:  // close -> echo close, mark done, signal EOF
                send_frame(s, 0x8, std::string());
                s->pos += header + len;
                s->closed = true;
                return {};
            case 0x1:  // text
            case 0x2:  // binary
                if (fragmenting)
                    throw std::runtime_error("websocket: new data frame during a fragmented message");
                if (fin) {  // single-frame message (the common case) — no reassembly buffer
                    std::string out(payload, len);
                    s->pos += header + len;
                    return out;
                }
                message.append(payload, len);  // first fragment (frame cap bounds it; total capped below)
                s->pos += header + len;
                fragmenting = true;
                continue;
            case 0x0:  // continuation
                if (!fragmenting)
                    throw std::runtime_error("websocket: continuation frame with no message in progress");
                if (message.size() + len > max_message)
                    throw std::runtime_error("websocket: message too large");
                message.append(payload, len);
                s->pos += header + len;
                if (fin) return message;
                continue;
            default:  // 0x3-0x7, 0xB-0xF are reserved/undefined — fail the connection
                throw std::runtime_error("websocket: unknown opcode");
        }
    }
}

#ifdef CHEATAH_WEBSOCKET_TESTING
namespace testonly {
// A white-box seam (test builds only): build a session pre-loaded with raw frame bytes and
// overridable caps, so recv()'s frame parser can be driven with crafted/hostile server frames
// without any TLS/socket. The frames must be self-contained (recv never has to read more).
// Free it with close(). Compiled ONLY into cheatah_tests; absent from the shipped library.
long long session_from_bytes(const std::string& frames, std::uint64_t max_frame,
                             std::uint64_t max_message) {
    auto* s = new Session();
    s->buf = frames;
    s->max_frame = max_frame;
    s->max_message = max_message;
    return as_handle(s);
}

// Drive send_upgrade()'s failure branch deterministically: a plaintext session on an invalid
// descriptor, so socket::send returns EBADF with no peer, no timing and no network.
void send_upgrade_on_closed_fd() {
    auto* s = new Session();
    s->fd = -1;    // invalid on purpose
    s->tls = -1;   // plaintext, so sess_send takes the socket:: branch
    send_upgrade(s, std::string("GET / HTTP/1.1\r\n\r\n"));
    destroy(s);    // unreachable: send_upgrade throws, having destroyed it
}
}  // namespace testonly
#endif  // CHEATAH_WEBSOCKET_TESTING

long long shutdown(long long session) {
    // Wake a reader blocked in recv() WITHOUT freeing the session: half-close the
    // underlying socket so recv returns "". The owner still calls close() after it
    // has joined the reader. Safe to call from another thread concurrently with the
    // reader's recv (that is exactly what ::shutdown is for).
    Session* s = as_session(session);
    if (s == nullptr || s->tls < 0) return -1;
    if (s->tls < 0) return socket::shutdown(s->fd);
    return tls::shutdown(s->tls);
}

long long close(long long session) {
    Session* s = as_session(session);
    if (s == nullptr) return 0;
    if (!s->closed && s->tls >= 0) {
        try {
            send_frame(s, 0x8, std::string());
        } catch (...) {}  // NOLINT(bugprone-empty-catch): a best-effort close frame — the peer may already be gone, and the session is torn down regardless
    }
    destroy(s);
    return 0;
}
/// @endcond

// ---- owning RAII client ----
// Each method forwards to the handle-based free function above; the guard adds deterministic
// close() (close frame + TLS + socket teardown) on scope exit, so a `with` block cannot leak.

Client& Client::operator=(Client&& other) noexcept {
    if (this != &other) {
        if (session_ != 0) cheatah::websocket::close(session_);
        session_ = other.session_;
        other.session_ = 0;
    }
    return *this;
}
Client::~Client() {
    if (session_ != 0) cheatah::websocket::close(session_);
}
long long Client::send_text(const std::string& message) const {
    return cheatah::websocket::send_text(session_, message);
}
std::string Client::recv() const { return cheatah::websocket::recv(session_); }
long long Client::shutdown() const { return cheatah::websocket::shutdown(session_); }
long long Client::close() {
    if (session_ == 0) return -1;
    const long long rc = cheatah::websocket::close(session_);
    session_ = 0;
    return rc;
}
Client open(const std::string& host, long long port, const std::string& path,
            const std::string& server_name, bool insecure, const std::string& ca_file,
            bool secure) {
    return Client(connect(host, port, path, server_name, insecure, ca_file, secure));
}
Client open_url(const std::string& url, bool insecure, const std::string& ca_file) {
    return Client(connect_url(url, insecure, ca_file));
}

}  // namespace cheatah::websocket
