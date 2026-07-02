// Compile-run system tests for the `parsers` module (C++-authored: parsers.url + parsers.json)
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
