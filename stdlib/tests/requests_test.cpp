// In-process unit tests for `requests` — the pure-cheatah HTTP module (requests.hpp,
// generated from requests.purr). The subprocess e2e suite (tests/purrc/requests_sys_test.cpp)
// runs the module inside a real cheatah program and does NOT contribute to stdlib coverage;
// these tests instantiate requests.hpp's templated functions directly and drive them against
// a real cheatah::socket loopback HTTP server thread, so every line is exercised in-process.
//
// The one implementation, one real socket: the "server" here is just a C++ thread on a
// loopback cheatah::socket replaying scripted HTTP/1.1 bytes — not a second HTTP client.

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "requests.hpp"
#include "socket.hpp"

namespace req = cheatah::requests;
namespace sk = cheatah::socket;

namespace {

// A loopback HTTP server that accepts `responses.size()` sequential connections and
// replies to each with the corresponding scripted response (reading the request head
// first). Used for single exchanges and multi-hop redirect chains.
class LoopbackServer {
   public:
    explicit LoopbackServer(std::vector<std::string> responses)
        : responses_(std::move(responses)) {
        fd_ = sk::tcp_listen("127.0.0.1", 0, 8);
        port_ = sk::local_port(fd_);
        thread_ = std::thread([this] { run(); });
    }
    ~LoopbackServer() {
        stop();
    }
    long long port() const { return port_; }
    std::string url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }
    void stop() {
        if (fd_ >= 0) {
            done_ = true;
            // Wake a thread parked in accept() with a throwaway self-connection —
            // closing a listening socket does not reliably unblock accept() on Linux.
            const long long waker = sk::tcp_connect("127.0.0.1", port_);
            if (waker >= 0) sk::close(waker);
            if (thread_.joinable()) thread_.join();
            sk::close(fd_);
            fd_ = -1;
        }
    }

   private:
    void run() {
        for (const auto& resp : responses_) {
            const long long client = sk::accept(fd_);
            if (client < 0 || done_) {
                if (client >= 0) sk::close(client);
                return;
            }
            std::string request;
            while (request.find("\r\n\r\n") == std::string::npos) {
                const std::string chunk = sk::recv(client, 4096);
                if (chunk.empty()) break;
                request += chunk;
            }
            sk::sendall(client, resp);
            sk::close(client);
        }
    }
    std::vector<std::string> responses_;
    long long fd_ = -1;
    long long port_ = 0;
    std::atomic<bool> done_{false};
    std::thread thread_;
};

// Default Options (30 s timeout, 5 redirects) so single-arg get() and option-carrying
// get() both get exercised.
req::Options defaults() {
    return req::Options{.timeout_ms = 30000, .max_redirects = 5};
}

}  // namespace

// A plain 200 with Content-Length: status, ok(), body, header() (case-insensitive).
TEST(CheatahRequests, BasicGetContentLength) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello"});
    const auto r = req::get(s.url("/greeting"));
    EXPECT_EQ(r.status, 200);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.body, "hello");
    EXPECT_EQ(r.error, "");
    EXPECT_EQ(r.header(std::string("CONTENT-TYPE")), "text/plain");  // lowercased key lookup
    EXPECT_EQ(r.header(std::string("X-Missing")), "");
}

// A 404 is a completed exchange: ok() false but error empty.
TEST(CheatahRequests, NotFoundIsCompleted) {
    LoopbackServer s({"HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope"});
    const auto r = req::get(s.url("/missing"), defaults());
    EXPECT_EQ(r.status, 404);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error, "");
    EXPECT_EQ(r.body, "nope");
}

// No Content-Length and no chunked framing: the body runs to connection close.
TEST(CheatahRequests, EofFramedBody) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\n\r\nuntil the very end"});
    const auto r = req::get(s.url("/"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.body, "until the very end");
}

// Chunked transfer-encoding: hex sizes, a chunk extension (`;`), then the 0 terminator.
TEST(CheatahRequests, ChunkedBody) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\n\r\n"});
    const auto r = req::get(s.url("/"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.body, "Wikipedia");
}

// A malformed chunk size (non-hex digit) is reported as an error.
TEST(CheatahRequests, ChunkedMalformedSize) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\noops\r\n0\r\n\r\n"});
    const auto r = req::get(s.url("/"));
    EXPECT_NE(r.error, "");
}

// Chunked framing truncated before the declared chunk bytes -> error.
TEST(CheatahRequests, ChunkedTruncatedBody) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFF\r\nshort"});
    const auto r = req::get(s.url("/"));
    EXPECT_NE(r.error, "");
}

// Chunked stream that closes before any CRLF-terminated size line -> error.
TEST(CheatahRequests, ChunkedClosedInsideSizeLine) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4"});
    const auto r = req::get(s.url("/"));
    EXPECT_NE(r.error, "");
}

// Query params are appended percent-encoded; existing '?' in the target uses '&'.
TEST(CheatahRequests, QueryParamsPercentEncoded) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"});
    auto o = defaults();
    o.params["a b"] = "c/d";  // space and slash must be percent-encoded
    const auto r = req::get(s.url("/search?x=1"), o);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.body, "ok");
}

// Custom headers are sent; a caller-supplied User-Agent suppresses the default.
TEST(CheatahRequests, CustomHeaders) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"});
    auto o = defaults();
    o.headers["X-Test"] = "1";
    o.headers["User-Agent"] = "mine/1.0";
    const auto r = req::get(s.url("/"), o);
    EXPECT_TRUE(r.ok());
}

// A single redirect (302) with an absolute Location is followed to the final 200.
// Two hops on the SAME server: /a -> absolute URL /b -> 200. The server is bound
// first so its real port can be embedded in the redirect target.
TEST(CheatahRequests, RedirectAbsolute) {
    const long long fd = sk::tcp_listen("127.0.0.1", 0, 8);
    ASSERT_GE(fd, 0);
    const long long port = sk::local_port(fd);
    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    std::thread server([fd, base] {
        const std::vector<std::string> responses = {
            "HTTP/1.1 302 Found\r\nLocation: " + base + "/b\r\nContent-Length: 0\r\n\r\n",
            "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\ndone"};
        for (const auto& resp : responses) {
            const long long client = sk::accept(fd);
            if (client < 0) return;
            std::string request;
            while (request.find("\r\n\r\n") == std::string::npos) {
                const std::string chunk = sk::recv(client, 4096);
                if (chunk.empty()) break;
                request += chunk;
            }
            sk::sendall(client, resp);
            sk::close(client);
        }
    });
    const auto r = req::get(base + "/a");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.body, "done");
    server.join();
    sk::close(fd);
}

// A redirect with a host-relative Location ("/next") is resolved against scheme/host/port.
TEST(CheatahRequests, RedirectRelative) {
    LoopbackServer srv({"HTTP/1.1 301 Moved\r\nLocation: /next\r\nContent-Length: 0\r\n\r\n",
                        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"});
    const auto r = req::get(srv.url("/start"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.body, "ok");
}

// A redirect whose Location is neither absolute nor root-relative is unsupported.
TEST(CheatahRequests, RedirectUnsupportedRelative) {
    LoopbackServer srv({"HTTP/1.1 307 Temporary Redirect\r\nLocation: sideways\r\nContent-Length: 0\r\n\r\n"});
    const auto r = req::get(srv.url("/x"));
    EXPECT_NE(r.error, "");
    EXPECT_FALSE(r.ok());
}

// A 3xx without any Location header is an error.
TEST(CheatahRequests, RedirectMissingLocation) {
    LoopbackServer srv({"HTTP/1.1 308 Permanent Redirect\r\nContent-Length: 0\r\n\r\n"});
    const auto r = req::get(srv.url("/x"));
    EXPECT_NE(r.error, "");
}

// A redirect loop exhausts max_redirects and returns the "too many redirects" error.
TEST(CheatahRequests, RedirectLoopExhausts) {
    // A server that always redirects to itself; max_redirects = 1 caps it quickly.
    std::vector<std::string> loop;
    for (int i = 0; i < 6; ++i)
        loop.push_back("HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n");
    LoopbackServer srv(std::move(loop));
    auto o = defaults();
    o.max_redirects = 1;
    const auto r = req::get(srv.url("/loop"), o);
    EXPECT_NE(r.error, "");
    EXPECT_EQ(r.status, 0);
}

// Zero/negative option fields fall back to documented defaults (timeout 30 s, 5 hops).
TEST(CheatahRequests, OptionDefaultsApplied) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nz"});
    req::Options o{};  // all-zero -> defaults kick in
    const auto r = req::get(s.url("/"), o);
    EXPECT_TRUE(r.ok());
}

// A malformed URL never connects: error set, status 0.
TEST(CheatahRequests, MalformedUrl) {
    const auto r = req::get(std::string("not a url"));
    EXPECT_NE(r.error, "");
    EXPECT_EQ(r.status, 0);
}

// A refused connection (nothing listening on port 9) comes back as a transport error.
TEST(CheatahRequests, ConnectionRefused) {
    const auto r = req::get(std::string("http://127.0.0.1:9/"));
    EXPECT_NE(r.error, "");
}

// A response head with no HTTP/ prefix is malformed.
TEST(CheatahRequests, MalformedResponseHead) {
    LoopbackServer s({"GARBAGE / not http\r\n\r\nbody"});
    const auto r = req::get(s.url("/"));
    EXPECT_NE(r.error, "");
}

// A connection that closes before a complete head (\r\n\r\n) is an error.
TEST(CheatahRequests, IncompleteHead) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n"});  // no blank line
    const auto r = req::get(s.url("/"));
    EXPECT_NE(r.error, "");
}

// A status line too short to hold a 3-digit code is malformed.
TEST(CheatahRequests, MalformedStatusLine) {
    LoopbackServer s({"HTTP/1.1\r\nContent-Length: 0\r\n\r\n"});  // no space + code
    const auto r = req::get(s.url("/"));
    EXPECT_NE(r.error, "");
}

// Content-Length larger than the body actually received -> error.
TEST(CheatahRequests, ContentLengthUnderrun) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nonly-a-bit"});
    const auto r = req::get(s.url("/"));
    EXPECT_NE(r.error, "");
}

// Lowercase hex chunk sizes decode too (parse_hex a-f branch).
TEST(CheatahRequests, ChunkedLowercaseHexSize) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "a\r\n0123456789\r\n0\r\n\r\n"});  // 0xa = 10 bytes
    const auto r = req::get(s.url("/"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.body, "0123456789");
}

// The module's ABI identity marker returns its name.
TEST(CheatahRequests, ModuleAbiMarker) {
    EXPECT_STREQ(req::module_abi(), "requests");
}

// A peer that accepts then immediately closes (RST) makes the request write fail; the
// exchange returns a "send failed"/transport error rather than hanging.
TEST(CheatahRequests, SendFailsToClosedPeer) {
    const long long fd = sk::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sk::local_port(fd);
    std::thread peer([fd] {
        const long long client = sk::accept(fd);
        if (client >= 0) sk::close(client);  // drop immediately, before reading
    });
    // A large custom header forces a big write, so if the peer has gone the send fails.
    auto o = defaults();
    o.timeout_ms = 3000;
    o.headers["X-Big"] = std::string(4 * 1024 * 1024, 'A');
    const auto r = req::get("http://127.0.0.1:" + std::to_string(port) + "/", o);
    // Either the send failed or the read saw an immediate EOF — both are non-ok errors,
    // never a 2xx success against a peer that never answered.
    EXPECT_FALSE(r.ok());
    peer.join();
    sk::close(fd);
}

// The https path: connecting a TLS client to a peer that speaks plain bytes fails at
// the handshake, surfacing a "tls:" error (never a silent success). Exercises the
// scheme=="https" branch and the tls::client_connect(<0) failure handling in get_once.
TEST(CheatahRequests, HttpsRefusedByNonTlsPeer) {
    const long long fd = sk::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sk::local_port(fd);
    std::thread peer([fd] {
        const long long client = sk::accept(fd);
        if (client >= 0) {
            sk::sendall(client, "plain text, not TLS\r\n");
            sk::close(client);
        }
    });
    auto o = defaults();
    o.timeout_ms = 3000;
    const auto r = req::get("https://127.0.0.1:" + std::to_string(port) + "/", o);
    EXPECT_EQ(r.status, 0);
    EXPECT_NE(r.error.find("tls"), std::string::npos);
    peer.join();
    sk::close(fd);
}
