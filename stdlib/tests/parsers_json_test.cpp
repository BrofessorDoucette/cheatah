// In-process unit tests for the HEADER-ONLY parts of the `parsers` module that cheatah_tests can
// instantiate WITHOUT linking the compiled DOM parser (parsers/json/json.cpp): the URL parser
// (parsers::url::Parser), the JSON schema factories (field/object), the typed struct reader
// (read<T>() — read.hpp, which drives the shared scan.hpp scanners), the pooled construction policy
// (PoolBuilder), the SIMD scan primitives (simd.hpp), and the JSON token classes (Boolean/Null/
// Number/Node). Every symbol here is header-only, so json.cpp stays OUT of this in-process
// denominator (parsers_static is deliberately NOT linked — see CMakeLists); we cover the header
// surface directly rather than by running the separate SIMD DOM parser.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "json/cursor.hpp"        // Cursor: the [it, end) read position the scanners advance
#include "json/node.hpp"          // Node + the Boolean/Null/Number token classes
#include "json/pool_builder.hpp"  // PoolBuilder: the pooled (viewing) construction policy
#include "json/read.hpp"          // read<T>(): parse straight into a typed struct (+ scan.hpp)
#include "json/scan.hpp"          // detail:: scanners (parse_double_fast, decode_escapes, ...)
#include "json/schema.hpp"
#include "json/simd.hpp"          // simd:: whitespace / quote-or-backslash primitives
#include "url/url.hpp"

namespace json = cheatah::parsers::json;
namespace jdet = cheatah::parsers::json::detail;
namespace jsimd = cheatah::parsers::json::simd;
namespace url = cheatah::parsers::url;

namespace {

// A struct to hang a runtime-built schema off of (mirrors how requests' Options/Response get a
// synthesized schema<>). field()/object() are constexpr but also run at runtime.
struct Point {
    long long x;
    std::string label;
};

// field() and object() are constexpr factories; requests uses them at compile time via schema<>.
// Building a schema at RUNTIME here executes their bodies so they are covered.
// cppcheck-suppress syntaxError  // cppcheck mis-parses the member-pointer/decltype schema template
TEST(CheatahParsers, SchemaFactoriesAtRuntime) {
    volatile bool run = true;  // defeat constant-folding so the factories execute at runtime
    if (run) {
        const auto f = json::field("x", &Point::x);
        const auto sch = json::object(f, json::field("label", &Point::label));
        EXPECT_EQ(std::tuple_size_v<decltype(sch.fields)>, 2u);
        EXPECT_EQ(std::get<0>(sch.fields).name, "x");
        EXPECT_EQ(std::get<1>(sch.fields).name, "label");
    }
}

// The URL parser: scheme://host[:port][/path][?query], the exact grammar requests speaks.
TEST(CheatahParsers, UrlParserComponents) {
    url::Parser p;
    url::Url u;
    // Explicit port + path + query.
    ASSERT_TRUE(p.parse("http://example.com:8080/a/b?x=1", u));
    EXPECT_EQ(u.scheme, "http");
    EXPECT_EQ(u.host, "example.com");
    EXPECT_EQ(u.port, 8080);
    EXPECT_EQ(u.target, "/a/b?x=1");
    // Default port (no colon), scheme lowercased.
    ASSERT_TRUE(p.parse("HTTP://host/path", u));
    EXPECT_EQ(u.scheme, "http");
    EXPECT_EQ(u.port, 80);
    EXPECT_EQ(u.target, "/path");
    // https default port; absent path -> "/".
    ASSERT_TRUE(p.parse("https://secure.example", u));
    EXPECT_EQ(u.port, 443);
    EXPECT_EQ(u.target, "/");
    // Bare-query form: no path, query present -> "/?...".
    ASSERT_TRUE(p.parse("http://h?q=2", u));
    EXPECT_EQ(u.target, "/?q=2");
    // Max valid port.
    ASSERT_TRUE(p.parse("http://host:65535/", u));
    EXPECT_EQ(u.port, 65535);
}

TEST(CheatahParsers, UrlParserRejects) {
    url::Parser p;
    url::Url u;
    EXPECT_FALSE(p.parse("no-scheme-sep", u));       // missing "://"
    EXPECT_FALSE(p.parse("://host", u));             // empty scheme
    EXPECT_FALSE(p.parse("ftp://host", u));          // unsupported scheme
    EXPECT_FALSE(p.parse("http:///path", u));        // empty authority
    EXPECT_FALSE(p.parse("http://user@host/", u));   // userinfo rejected
    EXPECT_FALSE(p.parse("http://host/p#frag", u));  // fragment rejected
    EXPECT_FALSE(p.parse("http://host:abc/", u));    // non-numeric port
    EXPECT_FALSE(p.parse("http://host:0/", u));      // port too low
    EXPECT_FALSE(p.parse("http://host:99999/", u));  // port too high
    EXPECT_FALSE(p.parse("http://:8080/", u));       // empty host with port
    EXPECT_FALSE(p.parse("http://host:/", u));       // empty port digits
    EXPECT_FALSE(p.parse("http://host:123456/", u)); // >5 port digits
}

// ----------------------------------------------------------------------------
// The header-only JSON token classes (node.hpp / number.hpp / boolean.hpp / null.hpp): construct
// each token and read it back through value(), and exercise Node's variant() (mutable + const).
// ----------------------------------------------------------------------------
TEST(CheatahParsersJson, TokenClassesAndNodeVariant) {
    // Number / Boolean / Null: constructor + value() accessor.
    EXPECT_DOUBLE_EQ(json::Number{3.5}.value(), 3.5);
    EXPECT_TRUE(json::Boolean{true}.value());
    EXPECT_FALSE(json::Boolean{false}.value());
    EXPECT_EQ(json::Null{}.value(), nullptr);

    // Node wraps a token in its variant; variant() has a mutable and a const overload.
    json::Node node{json::Number{42.0}};
    EXPECT_DOUBLE_EQ(std::get<json::Number>(node.variant()).value(), 42.0);   // mutable variant()
    node.variant().emplace<json::Boolean>(true);                              // mutate via mutable ref
    const json::Node& cref = node;
    EXPECT_TRUE(std::get<json::Boolean>(cref.variant()).value());             // const variant()
}

// ----------------------------------------------------------------------------
// The Array / Object / String container tokens (array.hpp / object.hpp / string.hpp): each is a
// move-only class over its backing storage. Construct, move-construct, move-assign, read via value(),
// and let them destruct — over BOTH the owning and viewing backings.
// ----------------------------------------------------------------------------
TEST(CheatahParsersJson, ContainerTokenLifecycle) {
    // String: owning (std::string) + viewing (std::string_view). Deduction guides pick the backing.
    json::String owned{std::string("owned")};      // String<std::string>
    EXPECT_EQ(owned.value(), "owned");
    json::String view{std::string_view("viewed")}; // String<std::string_view>
    EXPECT_EQ(view.value(), "viewed");

    // OwnedArray: build from a vector<Node>, move-construct and move-assign it.
    std::vector<json::Node> elems;
    elems.emplace_back(json::Number{1.0});
    elems.emplace_back(json::Boolean{false});
    json::OwnedArray arr{std::move(elems)};
    json::OwnedArray arr2{std::move(arr)};          // move-construct
    EXPECT_EQ(arr2.value().size(), 2u);
    json::OwnedArray arr3{std::vector<json::Node>{}};
    arr3 = std::move(arr2);                          // move-assign
    ASSERT_EQ(arr3.value().size(), 2u);
    EXPECT_DOUBLE_EQ(std::get<json::Number>(arr3.value()[0].variant()).value(), 1.0);

    // OwnedObject: build from a vector<Member>, move-construct and move-assign it.
    std::vector<json::Member> mem;
    mem.push_back(json::Member{json::Node{json::String{std::string("k")}},
                               json::Node{json::Number{7.0}}});
    json::OwnedObject obj{std::move(mem)};
    json::OwnedObject obj2{std::move(obj)};          // move-construct
    EXPECT_EQ(obj2.value().size(), 1u);
    json::OwnedObject obj3{std::vector<json::Member>{}};
    obj3 = std::move(obj2);                          // move-assign
    ASSERT_EQ(obj3.value().size(), 1u);
    EXPECT_DOUBLE_EQ(std::get<json::Number>(obj3.value()[0].second.variant()).value(), 7.0);
}

// ----------------------------------------------------------------------------
// The SIMD scan primitives (simd.hpp): whitespace-skip (fast test + long run) and quote/backslash
// find, over inputs long enough to exercise the AVX2 32-byte block AND its scalar tail.
// ----------------------------------------------------------------------------
TEST(CheatahParsersJson, SimdScanPrimitives) {
    EXPECT_TRUE(jsimd::is_whitespace(' '));
    EXPECT_TRUE(jsimd::is_whitespace('\t'));
    EXPECT_FALSE(jsimd::is_whitespace('x'));

    // skip_whitespace: a >40-byte run of spaces then 'X' — past the fast path, through the block scan.
    const std::string ws = std::string(40, ' ') + "X" + std::string(5, ' ');
    const char* p = jsimd::skip_whitespace(ws.data(), ws.data() + ws.size());
    ASSERT_LT(p, ws.data() + ws.size());
    EXPECT_EQ(*p, 'X');
    // fast path: first byte already non-whitespace -> returns `it` unchanged.
    const std::string none = "abc";
    EXPECT_EQ(jsimd::skip_whitespace(none.data(), none.data() + none.size()), none.data());

    // find_quote_or_backslash: 40 ordinary bytes then a quote, then a backslash later.
    const std::string body = std::string(40, 'a') + "\"more\\x";
    const char* q = jsimd::find_quote_or_backslash(body.data(), body.data() + body.size());
    ASSERT_LT(q, body.data() + body.size());
    EXPECT_EQ(*q, '"');
    const char* bs = jsimd::find_quote_or_backslash(q + 1, body.data() + body.size());
    EXPECT_EQ(*bs, '\\');
}

// ----------------------------------------------------------------------------
// The shared low-level scanners (scan.hpp detail::): number parsing (Clinger fast path + fallbacks),
// string scanning, and escape decoding incl. \uXXXX and a surrogate pair (append_utf8 / hex4).
// ----------------------------------------------------------------------------
TEST(CheatahParsersJson, ScanNumbersAndEscapes) {
    auto parse_d = [](std::string_view s) {
        json::Cursor c{s.data(), s.data() + s.size()};
        double out = 0.0;
        EXPECT_TRUE(jdet::parse_double_fast(c, out)) << s;
        return out;
    };
    EXPECT_DOUBLE_EQ(parse_d("0"), 0.0);
    EXPECT_DOUBLE_EQ(parse_d("-3"), -3.0);
    EXPECT_DOUBLE_EQ(parse_d("2.5"), 2.5);
    EXPECT_DOUBLE_EQ(parse_d("-12.5e2"), -1250.0);
    EXPECT_DOUBLE_EQ(parse_d("1e-3"), 0.001);
    // Outside the exact fast window (25 digits) -> std::from_chars fallback path.
    EXPECT_DOUBLE_EQ(parse_d("1234567890123456789012345"), 1234567890123456789012345.0);

    // parse_arithmetic on an integral T: the base-10 loop, negative, and the overflow-refetch branch.
    auto parse_ll = [](std::string_view s) {
        json::Cursor c{s.data(), s.data() + s.size()};
        long long out = 0;
        EXPECT_TRUE(jdet::parse_arithmetic(c, out)) << s;
        return out;
    };
    EXPECT_EQ(parse_ll("42"), 42);
    EXPECT_EQ(parse_ll("-9223372036854775808"), -9223372036854775807LL - 1);  // INT64_MIN, refetch path
    {   // unsigned rejects a negative literal
        const std::string neg = "-1";
        json::Cursor c{neg.data(), neg.data() + neg.size()};
        unsigned long long u = 0;
        EXPECT_FALSE(jdet::parse_arithmetic(c, u));
    }

    // decode_escapes drives append_utf8 for every \uXXXX form via the 1/2/3/4-byte arms:
    //   A -> 'A' (1 byte), é -> U+00E9 (2 bytes), € -> U+20AC (3 bytes),
    //   😀 -> U+1F600 (surrogate pair -> 4 bytes). Plus the simple two-char
    //   escapes \t \b \f \r \/ \\ \". The input is the LITERAL backslash-escape text.
    {
        // Build the escape text explicitly (each backslash is a real byte, not a C escape).
        const std::string raw =
            "a\\tb\\nA\\u0041\\u00e9\\u20ac\\ud83d\\ude00\\b\\f\\r\\/\\\\\\\"z";
        std::string decoded;
        ASSERT_TRUE(jdet::decode_escapes(raw, decoded));
        EXPECT_EQ(decoded,
                  std::string("a\tb\nAA\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\b\f\r/\\\"z"));
    }
    {   // scan_string over a quoted literal with an embedded escaped quote
        const std::string src = R"("he\"llo")";
        json::Cursor c{src.data(), src.data() + src.size()};
        std::string_view inner;
        bool esc = false;
        ASSERT_TRUE(jdet::scan_string(c, inner, esc));
        EXPECT_TRUE(esc);
        EXPECT_EQ(inner, R"(he\"llo)");
    }
    {   // scan_string on a plain (escape-free) string: esc stays false, bulk path.
        const std::string src = R"("plain")";
        json::Cursor c{src.data(), src.data() + src.size()};
        std::string_view inner;
        bool esc = true;
        ASSERT_TRUE(jdet::scan_string(c, inner, esc));
        EXPECT_FALSE(esc);
        EXPECT_EQ(inner, "plain");
    }
    {   // an UNTERMINATED string (no closing quote) -> scan_string returns false.
        const std::string src = "\"no end";
        json::Cursor c{src.data(), src.data() + src.size()};
        std::string_view inner;
        bool esc = false;
        EXPECT_FALSE(jdet::scan_string(c, inner, esc));
    }
    {   // a dangling escape at end of input -> scan_string returns false.
        const std::string src = "\"x\\";
        json::Cursor c{src.data(), src.data() + src.size()};
        std::string_view inner;
        bool esc = false;
        EXPECT_FALSE(jdet::scan_string(c, inner, esc));
    }
    // match(): the literal matcher used by skip_value's t/f/n arms — hit, miss, and too-short.
    {
        const std::string t = "true", n = "nullish";
        json::Cursor ct{t.data(), t.data() + t.size()};
        EXPECT_TRUE(jdet::match(ct, "true"));
        json::Cursor cn{n.data(), n.data() + n.size()};
        EXPECT_TRUE(jdet::match(cn, "null"));
        const std::string sh = "tr";
        json::Cursor cs{sh.data(), sh.data() + sh.size()};
        EXPECT_FALSE(jdet::match(cs, "true"));  // too short to match
    }
    // Malformed escapes are rejected (bad \u hex, dangling backslash, lone high surrogate, and an
    // unknown escape selector \x -> the switch default).
    {
        std::string out;
        EXPECT_FALSE(jdet::decode_escapes(R"(\uZZZZ)", out));
        EXPECT_FALSE(jdet::decode_escapes("\\", out));
        EXPECT_FALSE(jdet::decode_escapes(R"(\uD83Dx)", out));
        EXPECT_FALSE(jdet::decode_escapes("\\x", out));  // unknown selector -> default: return false
    }
    {   // an input ENDING exactly at an escape's end takes the post-loop `return true` (no npos run).
        std::string out;
        EXPECT_TRUE(jdet::decode_escapes("\\t", out));   // just "\t" -> a single tab, loop exits at end
        EXPECT_EQ(out, "\t");
    }
    // skip_value: discard a complete nested value in one call — the object holds an array, a string,
    // and the three literal forms true/false/null (the t/f/n match arms) plus a number.
    {
        const std::string src = R"({"a":[1,2,{"b":true}],"c":"x","d":false,"e":null,"f":-3.5} tail)";
        json::Cursor c{src.data(), src.data() + src.size()};
        ASSERT_TRUE(jdet::skip_value(c));
        EXPECT_EQ(std::string_view(c.it, static_cast<std::size_t>(c.end - c.it)), " tail");
    }
    // skip_value rejects malformed shapes: a lone closing brace, and stray punctuation.
    {
        const std::string bad = "}";
        json::Cursor c{bad.data(), bad.data() + bad.size()};
        EXPECT_FALSE(jdet::skip_value(c));
    }
    {
        const std::string bad = ",";
        json::Cursor c{bad.data(), bad.data() + bad.size()};
        EXPECT_FALSE(jdet::skip_value(c));
    }
    {   // skip_value over just a bare literal (false) leaves the cursor at end.
        const std::string src = "false";
        json::Cursor c{src.data(), src.data() + src.size()};
        EXPECT_TRUE(jdet::skip_value(c));
        EXPECT_EQ(c.it, c.end);
    }
}

// ----------------------------------------------------------------------------
// PoolBuilder (pool_builder.hpp): the pooled construction stack machine. Drive it as the DOM parser
// would — begin/add/finish for a nested array + object — and read the resulting ArrayView/ObjectView.
// ----------------------------------------------------------------------------
TEST(CheatahParsersJson, PoolBuilderStackMachine) {
    json::PoolBuilder b;
    b.reset(64);  // reserve pools

    // Build the array [1, "k": inner-object]  ->  actually: an array holding a Number then an object.
    b.begin_array();
    b.add_element(json::Node{json::Number{1.0}});
    b.begin_object();  // an object nested inside the array
    b.add_member(json::Member{json::Node{json::Number{0.0}}, json::Node{json::Boolean{true}}});
    json::Node inner_obj = b.finish_object();  // commit_members -> ObjectView
    b.add_element(std::move(inner_obj));
    json::Node arr = b.finish_array();          // commit_nodes -> ArrayView

    ASSERT_TRUE(std::holds_alternative<json::ArrayView>(arr.variant()));
    const std::span<const json::Node> elems = std::get<json::ArrayView>(arr.variant()).value();
    ASSERT_EQ(elems.size(), 2u);
    EXPECT_DOUBLE_EQ(std::get<json::Number>(elems[0].variant()).value(), 1.0);
    ASSERT_TRUE(std::holds_alternative<json::ObjectView>(elems[1].variant()));
    const auto members = std::get<json::ObjectView>(elems[1].variant()).value();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_TRUE(std::get<json::Boolean>(members[0].second.variant()).value());
}

// ----------------------------------------------------------------------------
// The typed struct reader read<T>() (read.hpp -> detail::skip_value + integral parse_arithmetic).
// ----------------------------------------------------------------------------

struct Trade {
    long long qty;
    double price;
    std::string sym;
    std::optional<long long> lot;
    std::vector<long long> tags;
};

}  // namespace

// schema<T> is a variable template; a struct opts in by specializing it with an object(field...)
// description (the same non-intrusive shape requests synthesizes for its Response structs).
namespace cheatah::parsers::json {
template <>
inline constexpr auto schema<Trade> = object(
    field("qty", &Trade::qty),
    field("price", &Trade::price),
    field("sym", &Trade::sym),
    field("lot", &Trade::lot),
    field("tags", &Trade::tags));
}  // namespace cheatah::parsers::json

namespace {

// read<T>() dispatches each field on its STATIC type: integral (parse_arithmetic base-10 loop),
// double, string, optional (null -> nullopt), vector. An UNKNOWN key exercises detail::skip_value
// over a nested value (object containing an array/number), which must be discarded and skipped.
TEST(CheatahParsersJson, TypedReadWithUnknownKeys) {
    Trade t{};
    const bool ok = json::read(
        R"({"qty": -100, "extra": {"junk": [1, 2, {"deep": true}], "s": "skip\tme"},
            "price": 3.25, "sym": "AAPL", "lot": null, "tags": [10, 20, 30]})",
        t);
    ASSERT_TRUE(ok);
    EXPECT_EQ(t.qty, -100);                 // negative integral, base-10 loop
    EXPECT_DOUBLE_EQ(t.price, 3.25);
    EXPECT_EQ(t.sym, "AAPL");
    EXPECT_FALSE(t.lot.has_value());        // JSON null -> nullopt
    ASSERT_EQ(t.tags.size(), 3u);
    EXPECT_EQ(t.tags[1], 20);
}

// A present optional and a malformed body (missing '}') that read<T> must reject.
TEST(CheatahParsersJson, TypedReadOptionalAndReject) {
    Trade t{};
    ASSERT_TRUE(json::read(R"({"qty":1,"price":2,"sym":"x","lot":7,"tags":[]})", t));
    ASSERT_TRUE(t.lot.has_value());
    EXPECT_EQ(*t.lot, 7);

    Trade bad{};
    EXPECT_FALSE(json::read(R"({"qty":1,"price":2,"sym":"x","lot":null,"tags":[1)", bad));
}

// Keys in NON-schema order (exercises the hint-wrap field search), an ESCAPED string VALUE
// (read_string -> decode_escapes), and an ESCAPED KEY (q == 'q', decode_escapes on the key).
TEST(CheatahParsersJson, TypedReadEscapesAndKeyOrder) {
    Trade t{};
    // "tags" first, then "sym" with a \t escape, then the escaped key "qty" (== "qty").
    const bool ok = json::read(
        "{\"tags\":[1,2],\"sym\":\"a\\tb\",\"price\":1.5,\"\\u0071ty\":9,\"lot\":null}", t);
    ASSERT_TRUE(ok);
    EXPECT_EQ(t.qty, 9);                       // matched via the escaped key
    EXPECT_EQ(t.sym, std::string("a\tb"));     // value decoded through decode_escapes
    ASSERT_EQ(t.tags.size(), 2u);
    EXPECT_EQ(t.tags[0], 1);
    EXPECT_FALSE(t.lot.has_value());
}

}  // namespace
