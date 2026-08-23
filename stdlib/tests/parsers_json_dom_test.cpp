// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// In-process unit tests for the COMPILED JSON DOM parser/serializer (parsers/json/json.cpp).
// json.cpp is compiled DIRECTLY into this test binary (see CMakeLists) so its coverage is
// measured here — the tests below exercise both parse paths (pooled zero-copy views via
// Parser::parse, self-contained owning Documents via Parser::parse_owning / the free parse()),
// both Validate modes (checked + trusted), the full error battery of the validating grammar,
// the depth cap, the iterative serializer (dump: all node kinds, string re-escaping, deep
// nesting), and to_view. Complements parsers_json_test.cpp, which covers the header-only
// surface (scanners, builders, tokens, the typed struct reader).

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "json/json.hpp"

namespace json = cheatah::parsers::json;

namespace {

// Parse with the free (owning, validating) parse() and require success.
json::Document parse_ok(std::string_view text) {
    bool ok = false;
    json::Document d = json::parse(text, &ok);
    EXPECT_TRUE(ok) << "rejected: " << text;
    return d;
}

TEST(ParsersJsonDom, ParsesEveryScalarKind) {
    EXPECT_TRUE(std::holds_alternative<json::Boolean>(parse_ok("true").variant()));
    EXPECT_TRUE(std::get<json::Boolean>(parse_ok("true").variant()).value());
    EXPECT_FALSE(std::get<json::Boolean>(parse_ok("false").variant()).value());
    EXPECT_TRUE(std::holds_alternative<json::Null>(parse_ok("null").variant()));
    EXPECT_DOUBLE_EQ(std::get<json::Number>(parse_ok("-2.5e2").variant()).value(), -250.0);
    // Leading/trailing whitespace is fine; the ok pointer may be omitted entirely.
    EXPECT_DOUBLE_EQ(std::get<json::Number>(json::parse("  42  ").variant()).value(), 42.0);
    // A plain string becomes an OWNED string in the free (owning) parse.
    const json::Document s = parse_ok(R"("hi")");
    ASSERT_TRUE(std::holds_alternative<json::String<std::string>>(s.variant()));
    EXPECT_EQ(json::to_view(s), "hi");
    // An escaped string is decoded (always into owned storage).
    EXPECT_EQ(json::to_view(parse_ok(R"("a\tb\u00e9")")), "a\tb\xC3\xA9");
}

TEST(ParsersJsonDom, ToViewReadsBothBackingsAndRejectsNonStrings) {
    // Owned backing (from the owning parse) — checked above too, via a fresh node here.
    json::Node owned{json::String{std::string("own")}};
    EXPECT_EQ(json::to_view(owned), "own");
    // Viewing backing (as the pooled parse produces).
    json::Node view{json::String{std::string_view("view")}};
    EXPECT_EQ(json::to_view(view), "view");
    // Not a string at all: an empty view, not a crash.
    json::Node num{json::Number{1.0}};
    EXPECT_EQ(json::to_view(num), "");
}

TEST(ParsersJsonDom, OwningContainersAndDumpRoundTrip) {
    const std::string src =
        R"({"a":[1,true,null,"s"],"b":{"nested":{"x":-1.5}},"empty_a":[],"empty_o":{},"t":"q\"z"})";
    const json::Document d = parse_ok(src);
    ASSERT_TRUE(std::holds_alternative<json::OwnedObject>(d.variant()));
    const auto& members = std::get<json::OwnedObject>(d.variant()).value();
    ASSERT_EQ(members.size(), 5u);
    EXPECT_EQ(json::to_view(members[0].first), "a");
    ASSERT_TRUE(std::holds_alternative<json::OwnedArray>(members[0].second.variant()));
    EXPECT_EQ(std::get<json::OwnedArray>(members[0].second.variant()).value().size(), 4u);
    // Compact serialization round-trips byte-for-byte (keys ordered, strings re-escaped).
    const std::string compact =
        R"({"a":[1,true,null,"s"],"b":{"nested":{"x":-1.5}},"empty_a":[],"empty_o":{},"t":"q\"z"})";
    EXPECT_EQ(json::dump(d), compact);
    // The appending overload appends — it must not clobber what is already in the buffer.
    std::string out = "prefix:";
    json::dump(d, out);
    EXPECT_EQ(out, "prefix:" + compact);
}

TEST(ParsersJsonDom, DumpReescapesStringsAndFormatsNumbers) {
    // Every escape arm of dump_string: quote, backslash, \b \f \n \r \t, and a control
    // byte below 0x20 that has no short form (-> \u0001).
    const json::Document s = parse_ok(R"(["\"\\\b\f\n\r\t\u0001"])");
    EXPECT_EQ(json::dump(s), R"(["\"\\\b\f\n\r\t\u0001"])");
    // Numbers use shortest to_chars form: integral values drop the '.0'.
    EXPECT_EQ(json::dump(parse_ok("[3,-2.5,0.125]")), "[3,-2.5,0.125]");
    EXPECT_EQ(json::dump(parse_ok("true")), "true");   // scalar root, no container frame
    EXPECT_EQ(json::dump(parse_ok("false")), "false");
    EXPECT_EQ(json::dump(parse_ok("null")), "null");
}

TEST(ParsersJsonDom, PooledParserYieldsViewsIntoSource) {
    json::Parser p;
    const std::string src = R"({"key":[10,"plain","esc\nq"]})";
    bool ok = false;
    json::Document d = p.parse(src, &ok);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(std::holds_alternative<json::ObjectView>(d.variant()));
    const auto members = std::get<json::ObjectView>(d.variant()).value();
    ASSERT_EQ(members.size(), 1u);
    // The key and the plain string are zero-copy VIEWS into `src`.
    ASSERT_TRUE(
        std::holds_alternative<json::String<std::string_view>>(members[0].first.variant()));
    EXPECT_EQ(json::to_view(members[0].first), "key");
    ASSERT_TRUE(std::holds_alternative<json::ArrayView>(members[0].second.variant()));
    const auto elems = std::get<json::ArrayView>(members[0].second.variant()).value();
    ASSERT_EQ(elems.size(), 3u);
    EXPECT_DOUBLE_EQ(std::get<json::Number>(elems[0].variant()).value(), 10.0);
    const auto& plain = elems[1].variant();
    ASSERT_TRUE(std::holds_alternative<json::String<std::string_view>>(plain));
    const std::string_view pv = std::get<json::String<std::string_view>>(plain).value();
    EXPECT_GE(pv.data(), src.data());                        // really points into the source
    EXPECT_LE(pv.data() + pv.size(), src.data() + src.size());
    // The ESCAPED string was decoded into owned storage even on the pooled path.
    ASSERT_TRUE(std::holds_alternative<json::String<std::string>>(elems[2].variant()));
    EXPECT_EQ(json::to_view(elems[2]), "esc\nq");
    // The pooled Document serializes like any other, and the Parser is reusable: a second
    // parse invalidates-and-replaces, and Parser::dump reuses its internal buffer.
    EXPECT_EQ(std::string(p.dump(d)), src);
    json::Document d2 = p.parse("[1,2]", &ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(std::string(p.dump(d2)), "[1,2]");             // buffer reused across dumps
}

TEST(ParsersJsonDom, OwningParseOutlivesItsSource) {
    json::Parser p;
    json::Document d;
    {
        std::string temp = R"({"k":"value with \u20ac","n":[false]})";
        bool ok = false;
        d = p.parse_owning(temp, &ok);
        ASSERT_TRUE(ok);
        temp.assign(temp.size(), 'X');  // scribble over the source: owned nodes must not care
    }
    ASSERT_TRUE(std::holds_alternative<json::OwnedObject>(d.variant()));
    const auto& members = std::get<json::OwnedObject>(d.variant()).value();
    ASSERT_EQ(members.size(), 2u);
    EXPECT_EQ(json::to_view(members[0].first), "k");
    EXPECT_EQ(json::to_view(members[0].second), "value with \xE2\x82\xAC");
    EXPECT_EQ(json::dump(d), R"({"k":"value with €","n":[false]})");
}

TEST(ParsersJsonDom, ValidatingParseRejectsMalformed) {
    json::Parser p;
    // Every rejection must ALSO yield a JSON-null document, not a partial tree.
    const char* bad[] = {
        "",                    // no value at all
        "   ",                 // only whitespace
        "{1:2}",               // object key is not a string
        "{\"a\"",              // key then end-of-input (no ':')
        "{\"a\" 1}",           // missing ':' between key and value
        "{\"a\":}",            // missing value
        "{\"ab",               // unterminated key string
        R"({"\uZZ":1})",       // key with a malformed \u escape
        "[",                   // unterminated right after '['
        "{",                   // unterminated right after '{'
        "[1",                  // value then end-of-input (no ',' or ']')
        "[1,",                 // dangling ',' then end-of-input
        "[1;2]",               // ';' is neither ',' nor ']'
        "[1}",                 // wrong closer for an array
        "{\"a\":1]",           // wrong closer for an object
        "tru",                 // truncated literal true
        "fals",                // truncated literal false
        "nul",                 // truncated literal null
        "x",                   // not a value at all
        R"(["\uZZZZ"])",       // string value with a malformed escape
        "\"no end",            // unterminated string value
        "1 x",                 // trailing junk after a complete value
        "[]]",                 // trailing junk after a complete container
    };
    for (const char* text : bad) {
        bool ok = true;
        json::Document owning = json::parse(text, &ok);
        EXPECT_FALSE(ok) << "free parse accepted: " << text;
        EXPECT_TRUE(std::holds_alternative<json::Null>(owning.variant())) << text;
        ok = true;
        json::Document pooled = p.parse(text, &ok);
        EXPECT_FALSE(ok) << "pooled parse accepted: " << text;
        EXPECT_TRUE(std::holds_alternative<json::Null>(pooled.variant())) << text;
    }
    // Rejection with ok == nullptr must not crash (the result is still null).
    EXPECT_TRUE(std::holds_alternative<json::Null>(json::parse("[oops").variant()));
}

TEST(ParsersJsonDom, DepthCapAndDeepDumpRoundTrip) {
    // 1000 levels of array nesting is accepted; the ITERATIVE dump round-trips it without
    // touching the C++ call stack.
    const std::string deep_ok = std::string(1000, '[') + "7" + std::string(1000, ']');
    bool ok = false;
    const json::Document d = json::parse(deep_ok, &ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(json::dump(d), deep_ok);
    // 1001 levels breaches kMaxParseDepth: rejected, result null — for arrays and objects.
    const std::string deep_bad = std::string(1001, '[') + "7" + std::string(1001, ']');
    ok = true;
    EXPECT_TRUE(std::holds_alternative<json::Null>(json::parse(deep_bad, &ok).variant()));
    EXPECT_FALSE(ok);
    std::string deep_obj;
    for (int i = 0; i < 1001; ++i) deep_obj += "{\"k\":";
    ok = true;
    (void)json::parse(deep_obj + "1", &ok);
    EXPECT_FALSE(ok);
}

TEST(ParsersJsonDom, TrustedUncheckedParseMatchesValidated) {
    // Validate=false strips every check at compile time; on WELL-FORMED input all four
    // unchecked entry points must produce the same document as the validating ones.
    const std::string src = R"({"a":[1,"two",{"b":null}],"c":true,"d":"e\tf"})";
    const std::string expect = json::dump(parse_ok(src));
    EXPECT_EQ(json::dump(json::parse<false>(src)), expect);        // free, owning
    json::Parser p;
    EXPECT_EQ(std::string(p.dump(p.parse<false>(src))), expect);   // pooled views
    EXPECT_EQ(json::dump(p.parse_owning<false>(src)), expect);     // reusable owning
    // Scalar through the unchecked path too (no container frames at all).
    EXPECT_EQ(json::dump(json::parse<false>("12.5")), "12.5");
}

}  // namespace
