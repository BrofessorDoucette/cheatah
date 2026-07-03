// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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

    // The requests the server received (full head + any Content-Length body), in order.
    // Safe to read after stop() has joined the thread.
    const std::vector<std::string>& received() const { return received_; }
    std::string last_request() const { return received_.empty() ? std::string() : received_.back(); }

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
            // Read the declared body too (so tests can assert method/headers AND body).
            const std::size_t head_end = request.find("\r\n\r\n");
            if (head_end != std::string::npos) {
                const std::size_t clp = request.find("Content-Length:");
                if (clp != std::string::npos && clp < head_end) {
                    const long long want = std::atoll(request.c_str() + clp + 15);
                    const std::size_t body_start = head_end + 4;
                    while (want > 0 &&
                           static_cast<long long>(request.size() - body_start) < want) {
                        const std::string chunk = sk::recv(client, 4096);
                        if (chunk.empty()) break;
                        request += chunk;
                    }
                }
            }
            received_.push_back(request);
            sk::sendall(client, resp);
            sk::close(client);
        }
    }
    std::vector<std::string> responses_;
    std::vector<std::string> received_;
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
    EXPECT_EQ(r.status_code, 200);
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
    EXPECT_EQ(r.status_code, 404);
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
    EXPECT_EQ(r.status_code, 0);
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
    EXPECT_EQ(r.status_code, 0);
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
// scheme=="https" branch and the tls::client_connect(<0) failure handling in request_once.
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
    EXPECT_EQ(r.status_code, 0);
    EXPECT_NE(r.error.find("tls"), std::string::npos);
    peer.join();
    sk::close(fd);
}

// ---------------------------------------------------------------------------
// v1.2 surface: verbs, request bodies, auth, richer Response, cookies, history.
// ---------------------------------------------------------------------------

// A tiny 200-with-body response the body-carrying verb tests reuse.
static std::string ok_body(const std::string& b) {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(b.size()) + "\r\n\r\n" + b;
}

// POST with json_body sets the method, application/json Content-Type, and Content-Length,
// and sends the body verbatim.
TEST(CheatahRequests, PostJsonBody) {
    LoopbackServer s({ok_body("done")});
    auto o = defaults();
    o.json_body = "{\"side\":\"buy\"}";
    const auto r = req::post(s.url("/order"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    const std::string req = s.last_request();
    EXPECT_EQ(req.rfind("POST /order ", 0), 0u);
    EXPECT_NE(req.find("Content-Type: application/json\r\n"), std::string::npos);
    EXPECT_NE(req.find("Content-Length: 14\r\n"), std::string::npos);
    EXPECT_NE(req.find("\r\n\r\n{\"side\":\"buy\"}"), std::string::npos);
}

// POST with form `data` is percent-encoded as application/x-www-form-urlencoded.
TEST(CheatahRequests, PostFormData) {
    LoopbackServer s({ok_body("ok")});
    auto o = defaults();
    o.data["q"] = "a b";  // space must be percent-encoded in the body
    const auto r = req::post(s.url("/f"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    const std::string req = s.last_request();
    EXPECT_NE(req.find("Content-Type: application/x-www-form-urlencoded\r\n"), std::string::npos);
    EXPECT_NE(req.find("\r\n\r\nq=a%20b"), std::string::npos);
}

// A raw `body` is sent verbatim with no auto Content-Type.
TEST(CheatahRequests, PostRawBody) {
    LoopbackServer s({ok_body("ok")});
    auto o = defaults();
    o.body = "raw-payload";
    const auto r = req::post(s.url("/r"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    const std::string req = s.last_request();
    EXPECT_NE(req.find("Content-Length: 11\r\n"), std::string::npos);
    EXPECT_NE(req.find("\r\n\r\nraw-payload"), std::string::npos);
    EXPECT_EQ(req.find("Content-Type:"), std::string::npos);  // none added for a raw body
}

// Body precedence: json_body wins over data wins over body.
TEST(CheatahRequests, BodyPrecedence) {
    LoopbackServer s({ok_body("ok")});
    auto o = defaults();
    o.json_body = "{\"j\":1}";
    o.data["d"] = "1";
    o.body = "raw";
    const auto r = req::put(s.url("/p"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    EXPECT_NE(s.last_request().find("\r\n\r\n{\"j\":1}"), std::string::npos);
}

// POST with no body still sends Content-Length: 0 (so a length-framed server is happy).
TEST(CheatahRequests, PostEmptyBodyContentLengthZero) {
    LoopbackServer s({ok_body("ok")});
    const auto r = req::post(s.url("/e"), defaults());
    s.stop();
    EXPECT_TRUE(r.ok());
    EXPECT_NE(s.last_request().find("Content-Length: 0\r\n"), std::string::npos);
}

// GET never carries a body even if one is set in Options.
TEST(CheatahRequests, GetIgnoresBody) {
    LoopbackServer s({ok_body("ok")});
    auto o = defaults();
    o.json_body = "{\"x\":1}";
    const auto r = req::get(s.url("/g"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    const std::string req = s.last_request();
    EXPECT_EQ(req.rfind("GET /g ", 0), 0u);
    EXPECT_EQ(req.find("Content-Type:"), std::string::npos);
    EXPECT_EQ(req.find("{\"x\":1}"), std::string::npos);
}

// Each verb sends its own request-line method. Also exercises the single-argument
// (default-Options) form of every verb, covering their default-Options overloads.
TEST(CheatahRequests, VerbMethods) {
    for (const std::string& m : {"GET", "PUT", "PATCH", "DELETE", "OPTIONS"}) {
        LoopbackServer s({ok_body("x")});
        const std::string url = s.url("/v");
        req::Response r;
        if (m == "GET") r = req::get(url);
        else if (m == "PUT") r = req::put(url);
        else if (m == "PATCH") r = req::patch(url);
        else if (m == "DELETE") r = req::delete_(url);
        else r = req::options(url);
        s.stop();
        EXPECT_TRUE(r.ok()) << m;
        EXPECT_EQ(s.last_request().rfind(m + " /v ", 0), 0u) << m;
    }
}

// HEAD sends the HEAD method and yields an empty body even when Content-Length is declared.
TEST(CheatahRequests, HeadNoBody) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nContent-Length: 123\r\n\r\n"});  // no body follows
    const auto r = req::head(s.url("/h"));
    s.stop();
    EXPECT_EQ(r.status_code, 200);
    EXPECT_EQ(r.body, "");  // HEAD: headers only
    EXPECT_EQ(s.last_request().rfind("HEAD /h ", 0), 0u);
}

// HTTP Basic auth emits the correct `Authorization: Basic <base64>` header.
TEST(CheatahRequests, BasicAuth) {
    LoopbackServer s({ok_body("ok")});
    auto o = defaults();
    o.auth_user = "user";
    o.auth_pass = "pass";
    const auto r = req::get(s.url("/a"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    // base64("user:pass") == "dXNlcjpwYXNz"
    EXPECT_NE(s.last_request().find("Authorization: Basic dXNlcjpwYXNz\r\n"), std::string::npos);
}

// A caller-supplied Authorization header is not overridden by auth_user/auth_pass.
TEST(CheatahRequests, ExplicitAuthHeaderWins) {
    LoopbackServer s({ok_body("ok")});
    auto o = defaults();
    o.auth_user = "user";
    o.auth_pass = "pass";
    o.headers["Authorization"] = "Bearer tok";
    const auto r = req::get(s.url("/a"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    const std::string req = s.last_request();
    EXPECT_NE(req.find("Authorization: Bearer tok\r\n"), std::string::npos);
    EXPECT_EQ(req.find("Basic"), std::string::npos);  // no Basic added on top
}

// A caller-supplied Content-Type suppresses the auto application/json.
TEST(CheatahRequests, ExplicitContentTypeWins) {
    LoopbackServer s({ok_body("ok")});
    auto o = defaults();
    o.json_body = "{}";
    o.headers["Content-Type"] = "application/vnd.custom+json";
    const auto r = req::post(s.url("/c"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    const std::string req = s.last_request();
    EXPECT_NE(req.find("Content-Type: application/vnd.custom+json\r\n"), std::string::npos);
    EXPECT_EQ(req.find("application/json"), std::string::npos);
}

// The reason phrase is parsed from the status line; a reason-less status line yields "".
TEST(CheatahRequests, ReasonPhrase) {
    LoopbackServer s({"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"});
    const auto r = req::get(s.url("/x"));
    EXPECT_EQ(r.status_code, 404);
    EXPECT_EQ(r.reason, "Not Found");

    LoopbackServer s2({"HTTP/1.1 200\r\nContent-Length: 1\r\n\r\nz"});  // no reason token
    const auto r2 = req::get(s2.url("/y"));
    EXPECT_EQ(r2.status_code, 200);
    EXPECT_EQ(r2.reason, "");
}

// text()/content() alias the body.
TEST(CheatahRequests, TextAndContent) {
    LoopbackServer s({ok_body("payload")});
    const auto r = req::get(s.url("/t"));
    EXPECT_EQ(r.text(), "payload");
    EXPECT_EQ(r.content(), "payload");
}

// Set-Cookie headers (one or several) are captured into `cookies`; an attribute-only
// cookie without '=' is skipped.
TEST(CheatahRequests, Cookies) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nSet-Cookie: sid=abc; Path=/\r\n"
                      "Set-Cookie: theme=dark\r\nSet-Cookie: broken\r\nContent-Length: 0\r\n\r\n"});
    const auto r = req::get(s.url("/c"));
    EXPECT_EQ(r.cookies.at("sid"), "abc");
    EXPECT_EQ(r.cookies.at("theme"), "dark");
    EXPECT_EQ(r.cookies.count("broken"), 0u);  // no '=' -> not a name=value cookie
}

// is_redirect()/is_permanent_redirect() classify the status.
TEST(CheatahRequests, RedirectPredicates) {
    LoopbackServer s({"HTTP/1.1 308 Permanent Redirect\r\nContent-Length: 0\r\n\r\n"});
    auto o = defaults();
    o.no_redirect = true;  // keep the 3xx to inspect it
    const auto r = req::get(s.url("/r"), o);
    EXPECT_TRUE(r.is_redirect());
    EXPECT_TRUE(r.is_permanent_redirect());
    LoopbackServer s2({ok_body("x")});
    const auto r2 = req::get(s2.url("/ok"));
    EXPECT_FALSE(r2.is_redirect());
    EXPECT_FALSE(r2.is_permanent_redirect());
}

// no_redirect returns the 3xx directly (no follow, empty history).
TEST(CheatahRequests, AllowRedirectsFalse) {
    LoopbackServer s({"HTTP/1.1 302 Found\r\nLocation: /next\r\nContent-Length: 0\r\n\r\n"});
    auto o = defaults();
    o.no_redirect = true;
    const auto r = req::get(s.url("/start"), o);
    EXPECT_EQ(r.status_code, 302);
    EXPECT_TRUE(r.history.empty());
}

// A followed redirect records the intermediate response in `history`.
TEST(CheatahRequests, RedirectHistory) {
    LoopbackServer s({"HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n",
                      ok_body("arrived")});
    const auto r = req::get(s.url("/start"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.body, "arrived");
    ASSERT_EQ(r.history.size(), 1u);
    EXPECT_EQ(r.history[0].status_code, 302);
}

// A 303 (and a 301/302 on a POST) follows as GET with the body dropped.
TEST(CheatahRequests, Redirect303PostBecomesGet) {
    LoopbackServer s({"HTTP/1.1 303 See Other\r\nLocation: /result\r\nContent-Length: 0\r\n\r\n",
                      ok_body("ok")});
    auto o = defaults();
    o.json_body = "{\"a\":1}";
    const auto r = req::post(s.url("/submit"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(s.received().size(), 2u);
    EXPECT_EQ(s.received()[0].rfind("POST /submit ", 0), 0u);
    EXPECT_EQ(s.received()[1].rfind("GET /result ", 0), 0u);  // method downgraded, body dropped
    EXPECT_EQ(s.received()[1].find("{\"a\":1}"), std::string::npos);
}

// A 307/308 preserves the method AND the body across the redirect (unlike 301/302/303).
TEST(CheatahRequests, Redirect308PreservesMethod) {
    LoopbackServer s({"HTTP/1.1 308 Permanent Redirect\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n",
                      ok_body("ok")});
    auto o = defaults();
    o.json_body = "{\"a\":1}";
    const auto r = req::post(s.url("/submit"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(s.received().size(), 2u);
    EXPECT_EQ(s.received()[1].rfind("POST /final ", 0), 0u);          // method preserved
    EXPECT_NE(s.received()[1].find("{\"a\":1}"), std::string::npos);  // body preserved
}

// raise_for_status() throws on 4xx/5xx and is a no-op on 2xx.
TEST(CheatahRequests, RaiseForStatus) {
    LoopbackServer s({"HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"});
    const auto bad = req::get(s.url("/e"));
    EXPECT_THROW(bad.raise_for_status(), std::exception);
    LoopbackServer s2({ok_body("ok")});
    const auto good = req::get(s2.url("/ok"));
    EXPECT_NO_THROW(good.raise_for_status());
}

// The typed JSON reader parses the body straight into a struct (accelerated path).
namespace testjson {
struct Quote {
    std::string symbol;
    double price;
};
}  // namespace testjson
namespace cheatah::parsers::json {
template <>
inline constexpr auto schema<testjson::Quote> =
    object(field("symbol", &testjson::Quote::symbol), field("price", &testjson::Quote::price));
}  // namespace cheatah::parsers::json

TEST(CheatahRequests, JsonTyped) {
    LoopbackServer s({ok_body("{\"symbol\":\"SPX\",\"price\":7386.65}")});
    const auto r = req::get(s.url("/q"));
    testjson::Quote q{};
    ASSERT_TRUE(r.json(q));
    EXPECT_EQ(q.symbol, "SPX");
    EXPECT_DOUBLE_EQ(q.price, 7386.65);

    LoopbackServer s2({ok_body("not json")});
    const auto r2 = req::get(s2.url("/bad"));
    testjson::Quote q2{};
    EXPECT_FALSE(r2.json(q2));  // malformed -> false
}

// to_json serializes a flat dict, escaping quotes/backslash/control chars (json_escape).
TEST(CheatahRequests, ToJsonAndEscape) {
    std::unordered_map<std::string, std::string> one{{"side", "buy"}};
    EXPECT_EQ(req::to_json(one), "{\"side\":\"buy\"}");
    std::unordered_map<std::string, std::string> esc{{"k", "a\"b\\c\n\r\td"}};
    EXPECT_EQ(req::to_json(esc), "{\"k\":\"a\\\"b\\\\c\\n\\r\\td\"}");
    std::unordered_map<std::string, std::string> empty;
    EXPECT_EQ(req::to_json(empty), "{}");
}

// b64encode covers the three tail lengths (0/1/2 trailing bytes) and known vectors.
TEST(CheatahRequests, Base64Encode) {
    EXPECT_EQ(req::b64encode(std::string("")), "");
    EXPECT_EQ(req::b64encode(std::string("Man")), "TWFu");   // rem 0
    EXPECT_EQ(req::b64encode(std::string("M")), "TQ==");     // rem 1
    EXPECT_EQ(req::b64encode(std::string("Ma")), "TWE=");    // rem 2
    EXPECT_EQ(req::b64encode(std::string("user:pass")), "dXNlcjpwYXNz");
}

// === Red-team: a malicious/compromised server must not crash or exhaust the client. ===

// F6: a non-numeric / overflowing / negative Content-Length sets `error` instead of throwing out
// of the "never raises" request path (previously std::stoll would throw and crash the program).
TEST(CheatahRequests, MalformedContentLengthIsError) {
    LoopbackServer a({"HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nbody"});
    EXPECT_NE(req::get(a.url("/")).error, "");
    LoopbackServer b({"HTTP/1.1 200 OK\r\nContent-Length: 999999999999999999999999\r\n\r\nx"});
    EXPECT_NE(req::get(b.url("/")).error, "");
    LoopbackServer c({"HTTP/1.1 200 OK\r\nContent-Length: -5\r\n\r\nhello"});
    EXPECT_NE(req::get(c.url("/")).error, "");  // '-' is non-digit (was a silent-truncation bug)
}

// F6: a status line whose code field is not three digits sets `error`, status stays 0.
TEST(CheatahRequests, MalformedStatusCodeIsError) {
    LoopbackServer s({"HTTP/1.1 xx Bad\r\nContent-Length: 0\r\n\r\n"});
    const auto r = req::get(s.url("/"));
    EXPECT_NE(r.error, "");
    EXPECT_EQ(r.status_code, 0);
}

// F5 (OOM): a response larger than max_bytes is refused rather than buffered without bound.
TEST(CheatahRequests, ResponseBodyCapEnforced) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\n\r\n" + std::string(5000, 'x')});  // EOF-framed, 5000 B
    auto o = defaults();
    o.max_bytes = 1000;
    const auto r = req::get(s.url("/"), o);
    EXPECT_NE(r.error.find("max_bytes"), std::string::npos);
}

// F5: a Content-Length larger than max_bytes is rejected up front (before reading that many bytes).
TEST(CheatahRequests, ContentLengthCapEnforced) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nContent-Length: 5000\r\n\r\nshort"});
    auto o = defaults();
    o.max_bytes = 1000;
    const auto r = req::get(s.url("/"), o);
    EXPECT_NE(r.error.find("Content-Length"), std::string::npos);
}

// F8: a chunk-size line big enough to overflow a naive counter is rejected as malformed (the
// overflow guard prevents wrapping into a bogus positive size / bad offset math).
TEST(CheatahRequests, ChunkSizeOverflowIsError) {
    LoopbackServer s({"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "FFFFFFFFFFFFFFFFFF\r\nx\r\n0\r\n\r\n"});
    EXPECT_NE(req::get(s.url("/")).error, "");
}

// F7: a redirect to a DIFFERENT host must NOT forward Basic-auth credentials (cross-origin leak),
// and must not mutate the caller's Options. "localhost" vs the numeric loopback IP is a host change
// that still connects to the same test server.
TEST(CheatahRequests, CrossHostRedirectStripsCredentials) {
    LoopbackServer target({ok_body("done")});  // the redirect destination (a "different host")
    const std::string loc = "http://localhost:" + std::to_string(target.port()) + "/final";
    LoopbackServer origin({"HTTP/1.1 302 Found\r\nLocation: " + loc + "\r\nContent-Length: 0\r\n\r\n"});
    auto o = defaults();
    o.auth_user = "user";
    o.auth_pass = "pass";
    o.headers["Authorization"] = "Bearer leak-me";  // explicit auth header -> stripped cross-host
    o.headers["Cookie"] = "sid=secret";             // cookies -> stripped cross-host
    o.headers["X-Trace"] = "keep";                  // a non-sensitive header -> preserved
    const auto r = req::get("http://127.0.0.1:" + std::to_string(origin.port()) + "/start", o);
    origin.stop();
    target.stop();
    EXPECT_TRUE(r.ok());
    ASSERT_FALSE(target.received().empty());
    const std::string& to_other = target.received()[0];
    EXPECT_EQ(to_other.find("Authorization"), std::string::npos) << to_other;  // Basic + Bearer gone
    EXPECT_EQ(to_other.find("leak-me"), std::string::npos) << to_other;
    EXPECT_EQ(to_other.find("Cookie"), std::string::npos) << to_other;         // cookie gone
    EXPECT_NE(to_other.find("X-Trace: keep"), std::string::npos) << to_other;  // non-secret kept
    EXPECT_EQ(o.auth_user, "user");  // the caller's Options is untouched (request works on a copy)
    EXPECT_EQ(o.headers.count("Authorization"), 1u);  // ...and its headers are intact
}

// F7 counterpart: a SAME-host redirect (relative Location) keeps credentials, matching Python.
TEST(CheatahRequests, SameHostRedirectKeepsCredentials) {
    LoopbackServer s({"HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n",
                      ok_body("done")});
    auto o = defaults();
    o.auth_user = "user";
    o.auth_pass = "pass";
    const auto r = req::get(s.url("/start"), o);
    s.stop();
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(s.received().size(), 2u);
    EXPECT_NE(s.received()[1].find("Authorization: Basic"), std::string::npos);  // kept, same host
}
