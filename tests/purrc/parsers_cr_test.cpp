// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run system tests for the `parsers` module (C++-authored: parsers.url + parsers.json
// + parsers.html)
// and the LANGUAGE features it exercises:
//   * dotted TYPE imports — `import parsers.url.Parser as Parser` aliases a class, and the
//     alias constructs (`Parser()`) and method-calls (`p.parse(...)`) like a local struct;
//   * compiler-synthesized JSON schemas — `import parsers` makes purrc emit a
//     cheatah::parsers::json::schema<> specialization for every struct the program defines,
//     so `parsers.json.read(text, value)` parses JSON STRAIGHT into user structs with no
//     user-written schema (the typed reader).
// Each test writes a .purr, compiles it with purrc, runs it, and asserts exact stdout.
#include "e2e_harness.hpp"

// Dotted type imports: both parsers.url types, construction, method call, field access.
TEST(ParsersCompileRun, UrlParserImport) {
    e2e::expect_e2e("parsers_url_import", R"PURR(import parsers.url.Parser as Parser
import parsers.url.Url as Url
import io

let p = Parser()
let u = Url()
if p.parse("http://example.com:8080/data?x=1", u) {
    io.print(u.host)
    io.print(u.port)
    io.print(u.target)
}
)PURR", "example.com\n8080\n/data?x=1\n");
}

// The URL parser rejects malformed input through the same .purr surface.
TEST(ParsersCompileRun, UrlParserRejects) {
    e2e::expect_e2e("parsers_url_rejects", R"PURR(import parsers.url.Parser as Parser
import parsers.url.Url as Url
import io

let p = Parser()
let u = Url()
if p.parse("ftp://example.com/x", u) {
    io.print("accepted")
} else {
    io.print("rejected")
}
)PURR", "rejected\n");
}

// The JSON DOM parser imports and runs (owning form -> self-contained Document).
TEST(ParsersCompileRun, JsonDomParse) {
    e2e::expect_e2e("parsers_json_dom", R"PURR(import parsers.json.Parser as JsonParser
import io

let jp = JsonParser()
let d = jp.parse_owning("{\"price\": 7386.65, \"live\": true}")
io.print("dom ok")
)PURR", "dom ok\n");
}

// THE TYPED READER: parsers.json.read parses JSON straight into a .purr struct — the schema
// is synthesized by purrc from the struct's typed fields (str/float/bool here).
TEST(ParsersCompileRun, TypedReader) {
    e2e::expect_e2e("parsers_typed_reader", R"PURR(import parsers
import io

struct Quote {
    symbol: str
    price: float
    live: bool
}

let q = Quote("", 0.0, false)
if parsers.json.read("{\"symbol\":\"SPX\",\"price\":7386.65,\"live\":true}", q) {
    io.print(q.symbol)
    io.print(q.price)
    io.print(q.live)
}
)PURR", "SPX\n7386.65\nTrue\n");
}

// Schema synthesis composes: nested structs and list<T> fields (vector reads).
TEST(ParsersCompileRun, TypedReaderNested) {
    e2e::expect_e2e("parsers_typed_nested", R"PURR(import parsers
import io

struct Row {
    v: float
}
struct Series {
    name: str
    rows: list<Row>
}

let rows: list<Row> = []
let s = Series("", rows)
if parsers.json.read("{\"name\":\"av\",\"rows\":[{\"v\":1.5},{\"v\":2.5}]}", s) {
    io.print(s.name)
    io.print(len(s.rows))
    io.print(s.rows[1].v)
}
)PURR", "av\n2\n2.5\n");
}

// parsers.html escaping from .purr: escape (with and without quote), then unescape decoding
// named + decimal + hex references back (the &copy; expectation is the two UTF-8 bytes of ©).
TEST(ParsersCompileRun, HtmlEscapeUnescape) {
    e2e::expect_e2e("parsers_html_escape", R"PURR(import parsers.html
import io

io.print(parsers.html.escape("<a href=\"x\">&'</a>"))
io.print(parsers.html.escape("q: \"hi\"", false))
io.print(parsers.html.unescape("&lt;p&gt; &amp; &#65;&#x42; &copy;"))
)PURR", "&lt;a href=&quot;x&quot;&gt;&amp;&#x27;&lt;/a&gt;\nq: \"hi\"\n<p> & AB \xC2\xA9\n");
}

// The tokenizing parser as DATA: a for-loop over parsers.html.parse walks every event kind
// (decl/starttag/comment/data/endtag/startendtag) in document order — the .purr shape that
// replaces Python's HTMLParser callback subclassing.
TEST(ParsersCompileRun, HtmlParseWalk) {
    e2e::expect_e2e("parsers_html_walk", R"PURR(import parsers.html
import io

let doc = "<!DOCTYPE html><ul id=\"m\"><!--nav--><li class=\"a\">One &amp; Two</li><br/></ul>"
for t in parsers.html.parse(doc) {
    io.print(t.kind + "|" + t.tag + "|" + t.data)
}
)PURR", "decl||DOCTYPE html\n"
        "starttag|ul|\n"
        "comment||nav\n"
        "starttag|li|\n"
        "data||One & Two\n"
        "endtag|li|\n"
        "startendtag|br|\n"
        "endtag|ul|\n");
}

// The attribute helpers on a start-tag token: get_attr decodes references and matches
// case-insensitively; has_attr sees valueless attributes and misses absent ones.
TEST(ParsersCompileRun, HtmlAttrHelpers) {
    e2e::expect_e2e("parsers_html_attrs", R"PURR(import parsers.html
import io

for t in parsers.html.parse("<a HREF=\"x&amp;y\" data-k>link</a>") {
    if t.kind == "starttag" {
        io.print(parsers.html.get_attr(t, "href"))
        io.print(parsers.html.has_attr(t, "data-k"))
        io.print(parsers.html.has_attr(t, "nope"))
    }
}
)PURR", "x&y\nTrue\nFalse\n");
}

// The validating reader REJECTS malformed input (and unknown keys are skipped, not errors).
TEST(ParsersCompileRun, TypedReaderRejectsMalformed) {
    e2e::expect_e2e("parsers_typed_rejects", R"PURR(import parsers
import io

struct Quote {
    price: float
}

let q = Quote(0.0)
if parsers.json.read("{\"price\":}", q) {
    io.print("accepted")
} else {
    io.print("rejected")
}
if parsers.json.read("{\"unknown\":[1,2],\"price\":3.5}", q) {
    io.print(q.price)
}
)PURR", "rejected\n3.5\n");
}
