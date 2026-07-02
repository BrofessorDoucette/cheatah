// System tests for `requests` — THE FIRST PURE-CHEATAH STDLIB MODULE. Each test runs a real
// loopback HTTP server (cheatah::socket, C++ thread), then compiles + runs a .purr program
// that `import requests` and GETs from it over a genuine TCP connection, asserting stdout.
#include <string>
#include <thread>

#include "e2e_harness.hpp"
#include "socket.hpp"

namespace sock = cheatah::socket;

namespace {

// Accept one connection, read the request head, send @p response verbatim, close.
// @complexity O(1) @alloc the request buffer @test RequestsSys (helper)
void serve_once(long long listen_fd, std::string response) {
    const long long client = sock::accept(listen_fd);
    if (client < 0) return;
    std::string request;
    while (request.find("\r\n\r\n") == std::string::npos) {
        const std::string chunk = sock::recv(client, 4096);
        if (chunk.empty()) break;
        request += chunk;
    }
    sock::sendall(client, response);
    sock::close(client);
}

}  // namespace

// GET against a live server: status, ok(), body — through pure-cheatah HTTP.
TEST(RequestsSys, BasicGet) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server(serve_once, fd,
                       "HTTP/1.1 200 OK\r\nContent-Length: 18\r\n\r\nhello from cheatah");
    const std::string src = "import requests\nimport io\n"
                            "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                            "/greeting\")\n"
                            "io.print(r.status)\nio.print(r.ok())\nio.print(r.body)\n";
    e2e::expect_e2e("requests_basic_get", src, "200\nTrue\nhello from cheatah\n");
    server.join();
    sock::close(fd);
}

// A 404 is a COMPLETED exchange: ok() false, error empty, body real.
TEST(RequestsSys, NotFound) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server(serve_once, fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope");
    const std::string src = "import requests\nimport io\n"
                            "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                            "/missing\")\n"
                            "io.print(r.status)\nio.print(r.ok())\nio.print(r.error == \"\")\n"
                            "io.print(r.body)\n";
    e2e::expect_e2e("requests_not_found", src, "404\nFalse\nTrue\nnope\n");
    server.join();
    sock::close(fd);
}

// No Content-Length: the body is framed by connection close.
TEST(RequestsSys, EofFraming) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server(serve_once, fd, "HTTP/1.1 200 OK\r\n\r\nuntil the very end");
    const std::string src = "import requests\nimport io\n"
                            "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                            "/\")\nio.print(r.body)\n";
    e2e::expect_e2e("requests_eof", src, "until the very end\n");
    server.join();
    sock::close(fd);
}

// Error paths need no server: malformed URL, connection refused.
TEST(RequestsSys, ErrorPaths) {
    e2e::expect_e2e("requests_errors", R"PURR(import requests
import io
let b = requests.get("not a url")
io.print(b.error == "")
let c = requests.get("http://127.0.0.1:9/")
io.print(c.error == "")
)PURR", "False\nFalse\n");
}

// https against a peer that is not a TLS server: refused with a tls error, never silent.
TEST(RequestsSys, HttpsRefusesBadPeer) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread peer([fd]() {
        const long long client = sock::accept(fd);
        sock::sendall(client, "plain text, not TLS\r\n");
        sock::close(client);
    });
    const std::string src = "import requests\nimport io\nimport string\n"
                            "let o = requests.Options({.timeout_ms = 3000})\n"
                            "let r = requests.get(\"https://127.0.0.1:" + std::to_string(port) +
                            "/\", o)\nio.print(r.status)\nio.print(string.contains(r.error, \"tls\"))\n";
    e2e::expect_e2e("requests_https_bad_peer", src, "0\nTrue\n");
    peer.join();
    sock::close(fd);
}

// ---- parity matrix (ported from the C++ reference implementation's test suite) ----

// Chunked transfer-encoding is decoded (hex sizes, extensions ignored, trailer consumed).
TEST(RequestsSys, Chunked) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server(serve_once, fd,
                       "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                       "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n");
    const std::string src = "import requests\nimport io\n"
                            "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                            "/\")\nio.print(r.ok())\nio.print(r.body)\n";
    e2e::expect_e2e("requests_chunked", src, "True\nWikipedia\n");
    server.join();
    sock::close(fd);
}

// Response headers are stored lowercased: lookup is case-insensitive either way.
TEST(RequestsSys, HeaderLookup) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server(serve_once, fd,
                       "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nX-Custom-Tag: abc123\r\n\r\nx");
    const std::string src = "import requests\nimport io\n"
                            "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                            "/\")\n"
                            "io.print(r.header(\"x-custom-tag\"))\n"
                            "io.print(r.header(\"X-CUSTOM-TAG\"))\n"
                            "io.print(r.header(\"absent\") == \"\")\n";
    e2e::expect_e2e("requests_headers", src, "abc123\nabc123\nTrue\n");
    server.join();
    sock::close(fd);
}

// Query params are appended and percent-encoded ('&', space, '~' unreserved).
TEST(RequestsSys, QueryParams) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::string captured;
    std::thread server([fd, &captured]() {
        const long long client = sock::accept(fd);
        while (captured.find("\r\n\r\n") == std::string::npos) {
            const std::string chunk = sock::recv(client, 4096);
            if (chunk.empty()) break;
            captured += chunk;
        }
        sock::sendall(client, "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx");
        sock::close(client);
    });
    const std::string full = "import requests\nimport io\n"
                             "let o = requests.Options({.timeout_ms = 5000})\n"
                             "o.params[\"symbol\"] = \"S&P 500~\"\n"
                             "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                             "/q?fixed=1\", o)\nio.print(r.ok())\n";
    e2e::expect_e2e("requests_params", full, "True\n");
    server.join();
    sock::close(fd);
    EXPECT_NE(captured.find("GET /q?fixed=1&symbol=S%26P%20500~ HTTP/1.1"), std::string::npos)
        << captured;
}

// Custom request headers go on the wire.
TEST(RequestsSys, CustomHeaders) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::string captured;
    std::thread server([fd, &captured]() {
        const long long client = sock::accept(fd);
        while (captured.find("\r\n\r\n") == std::string::npos) {
            const std::string chunk = sock::recv(client, 4096);
            if (chunk.empty()) break;
            captured += chunk;
        }
        sock::sendall(client, "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx");
        sock::close(client);
    });
    const std::string full = "import requests\nimport io\n"
                             "let o = requests.Options({.timeout_ms = 5000})\n"
                             "o.headers[\"X-Api-Key\"] = \"secret\"\n"
                             "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                             "/\", o)\nio.print(r.ok())\n";
    e2e::expect_e2e("requests_custom_headers", full, "True\n");
    server.join();
    sock::close(fd);
    EXPECT_NE(captured.find("X-Api-Key: secret\r\n"), std::string::npos) << captured;
}

// 302 with a path-absolute Location is followed; the final URL lands in r.url.
TEST(RequestsSys, Redirect) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server([fd]() {
        serve_once(fd, "HTTP/1.1 302 Found\r\nLocation: /moved\r\nContent-Length: 0\r\n\r\n");
        serve_once(fd, "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nfound me");
    });
    const std::string src = "import requests\nimport io\nimport string\n"
                            "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                            "/start\")\nio.print(r.ok())\nio.print(r.body)\n"
                            "io.print(string.contains(r.url, \"/moved\"))\n";
    e2e::expect_e2e("requests_redirect", src, "True\nfound me\nTrue\n");
    server.join();
    sock::close(fd);
}

// A redirect loop stops at max_redirects with a clear error.
TEST(RequestsSys, RedirectLoop) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server([fd]() {
        for (int i = 0; i < 4; ++i)
            serve_once(fd, "HTTP/1.1 302 Found\r\nLocation: /again\r\nContent-Length: 0\r\n\r\n");
    });
    const std::string full = "import requests\nimport io\nimport string\n"
                             "let o = requests.Options({.timeout_ms = 5000, .max_redirects = 3})\n"
                             "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                             "/start\", o)\nio.print(r.ok())\n"
                             "io.print(string.contains(r.error, \"too many redirects\"))\n";
    e2e::expect_e2e("requests_redirect_loop", full, "False\nTrue\n");
    server.join();
    sock::close(fd);
}

// A server that never answers trips the socket timeout — bounded, not hung.
TEST(RequestsSys, Timeout) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server([fd]() {
        const long long client = sock::accept(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        sock::close(client);
    });
    const std::string full = "import requests\nimport io\n"
                             "let o = requests.Options({.timeout_ms = 150})\n"
                             "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                             "/slow\", o)\nio.print(r.ok())\nio.print(r.error == \"\")\n";
    const auto start = std::chrono::steady_clock::now();
    e2e::expect_e2e("requests_timeout", full, "False\nFalse\n");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    EXPECT_LT(elapsed.count(), 5000) << "timeout did not bound the request";
    server.join();
    sock::close(fd);
}

// End to end: GET a JSON body, then parse it STRAIGHT into a .purr struct via the typed
// reader (the schema is synthesized by purrc) — requests + parsers composing in pure cheatah.
TEST(RequestsSys, JsonIntegration) {
    const long long fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(fd, 0);
    const long long port = sock::local_port(fd);
    std::thread server(serve_once, fd,
                       "HTTP/1.1 200 OK\r\nContent-Length: 44\r\n"
                       "Content-Type: application/json\r\n\r\n"
                       R"({"symbol":"SPX","price":7386.65,"live":true})");
    const std::string src = "import requests\nimport parsers\nimport io\n"
                            "struct Quote {\n    symbol: str\n    price: float\n    live: bool\n}\n"
                            "let r = requests.get(\"http://127.0.0.1:" + std::to_string(port) +
                            "/quote\")\n"
                            "let q = Quote(\"\", 0.0, false)\n"
                            "if r.ok() and parsers.json.read(r.body, q) {\n"
                            "    io.print(q.symbol)\n    io.print(q.price)\n    io.print(q.live)\n}\n";
    e2e::expect_e2e("requests_json", src, "SPX\n7386.65\nTrue\n");
    server.join();
    sock::close(fd);
}
