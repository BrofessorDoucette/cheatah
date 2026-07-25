// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// In-process unit tests for the `parsers.html` escaper + tolerant tokenizing parser
// (parsers/html/html.cpp). html.cpp is compiled DIRECTLY into this test binary (see CMakeLists)
// so its coverage is measured here — the tests below exercise escape/unescape (every entity
// form and every malformed-reference shape), the full tokenizer (start/startend/end tags,
// attribute quoting forms, comments/declarations/PIs, raw-text <script>/<style>, and the
// lenient malformed-input paths), and the get_attr/has_attr helpers.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "html/html.hpp"

namespace html = cheatah::parsers::html;

namespace {

// A compact "kind|tag|data" rendering of one token, for order-sensitive assertions.
std::string sig(const html::Token& t) { return t.kind + "|" + t.tag + "|" + t.data; }

TEST(ParsersHtml, EscapeAllSpecialsWithAndWithoutQuote) {
    EXPECT_EQ(html::escape(R"(<a href="x">&'z)"),
              "&lt;a href=&quot;x&quot;&gt;&amp;&#x27;z");
    // quote=false leaves both quote characters verbatim but still escapes & < >.
    EXPECT_EQ(html::escape(R"(<"'>&)", false), R"(&lt;"'&gt;&amp;)");
    EXPECT_EQ(html::escape(""), "");
    EXPECT_EQ(html::escape("plain text"), "plain text");  // default arm only
}

TEST(ParsersHtml, UnescapeNamedAndNumericForms) {
    // Named entities from the table.
    EXPECT_EQ(html::unescape("&lt;p&gt; &amp; &quot;&apos;"), "<p> & \"'");
    EXPECT_EQ(html::unescape("&copy;"), "\xC2\xA9");
    EXPECT_EQ(html::unescape("&euro;"), "\xE2\x82\xAC");
    // Numeric decimal + hex (lower/upper x, lower/upper hex digits) across all four
    // UTF-8 encoding widths: 1-byte A, 2-byte ©, 3-byte 中, 4-byte 😀.
    EXPECT_EQ(html::unescape("&#65;"), "A");
    EXPECT_EQ(html::unescape("&#169;"), "\xC2\xA9");
    EXPECT_EQ(html::unescape("&#xa9;"), "\xC2\xA9");
    EXPECT_EQ(html::unescape("&#XA9;"), "\xC2\xA9");
    EXPECT_EQ(html::unescape("&#x4E2D;"), "\xE4\xB8\xAD");
    EXPECT_EQ(html::unescape("&#x1F600;"), "\xF0\x9F\x98\x80");
    EXPECT_EQ(html::unescape("no refs at all"), "no refs at all");
}

TEST(ParsersHtml, UnescapeLeavesMalformedVerbatim) {
    EXPECT_EQ(html::unescape("&bogus;"), "&bogus;");        // unknown name
    EXPECT_EQ(html::unescape("a & b"), "a & b");            // bare '&', no ';'
    EXPECT_EQ(html::unescape("&"), "&");                    // '&' at end of input
    EXPECT_EQ(html::unescape("&;"), "&;");                  // empty reference body
    EXPECT_EQ(html::unescape("&#;"), "&#;");                // numeric with no digits
    EXPECT_EQ(html::unescape("&#x;"), "&#x;");              // hex with no digits
    EXPECT_EQ(html::unescape("&#12z;"), "&#12z;");          // invalid decimal digit
    EXPECT_EQ(html::unescape("&#1A;"), "&#1A;");            // hex digit in a decimal ref
    EXPECT_EQ(html::unescape("&#x110000;"), "&#x110000;");  // beyond Unicode range
    EXPECT_EQ(html::unescape("&#0;"), "&#0;");              // NUL rejected
    // The ';' sits past the 32-char window, so the '&' is kept verbatim.
    EXPECT_EQ(html::unescape("&reallyreallyreallyreallyreallylongname;"),
              "&reallyreallyreallyreallyreallylongname;");
    // Round-trip: escape then unescape restores the original for the covered set.
    const std::string original = R"(<b class="x">&'</b>)";
    EXPECT_EQ(html::unescape(html::escape(original)), original);
}

TEST(ParsersHtml, ParsesStartDataEndInDocumentOrder) {
    const auto t = html::parse(R"(<div class="box">A &amp; B</div>)");
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(sig(t[0]), "starttag|div|");
    ASSERT_EQ(t[0].attrs.size(), 1u);
    EXPECT_EQ(t[0].attrs[0].name, "class");
    EXPECT_EQ(t[0].attrs[0].value, "box");
    EXPECT_EQ(sig(t[1]), "data||A & B");  // character references decoded in text
    EXPECT_EQ(sig(t[2]), "endtag|div|");
}

TEST(ParsersHtml, TagAndAttributeNamesAreLowercased) {
    const auto t = html::parse(R"(<DIV CLASS="Keep">x</DIV>)");
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0].tag, "div");
    EXPECT_EQ(t[2].tag, "div");
    ASSERT_EQ(t[0].attrs.size(), 1u);
    EXPECT_EQ(t[0].attrs[0].name, "class");
    EXPECT_EQ(t[0].attrs[0].value, "Keep");  // values keep their case
}

TEST(ParsersHtml, AttributeQuotingForms) {
    const auto t = html::parse(R"(<e a="dq" b='sq' c=bare d = "spaced" disabled f="x&amp;y">)");
    ASSERT_EQ(t.size(), 1u);
    ASSERT_EQ(t[0].attrs.size(), 6u);
    EXPECT_EQ(html::get_attr(t[0], "a"), "dq");
    EXPECT_EQ(html::get_attr(t[0], "b"), "sq");
    EXPECT_EQ(html::get_attr(t[0], "c"), "bare");    // unquoted value
    EXPECT_EQ(html::get_attr(t[0], "d"), "spaced");  // whitespace around '='
    EXPECT_TRUE(html::has_attr(t[0], "disabled"));   // valueless attribute
    EXPECT_EQ(html::get_attr(t[0], "disabled"), "");
    EXPECT_EQ(html::get_attr(t[0], "f"), "x&y");     // references decoded in values
}

TEST(ParsersHtml, AttributeEdgeCases) {
    // A stray '=' with no name is skipped; the 'b' after it still parses as an attribute.
    const auto stray = html::parse("<a =b>");
    ASSERT_EQ(stray.size(), 1u);
    ASSERT_EQ(stray[0].attrs.size(), 1u);
    EXPECT_EQ(stray[0].attrs[0].name, "b");
    EXPECT_EQ(stray[0].attrs[0].value, "");
    // Unterminated quoted value: consumed to end-of-input, tag still emitted.
    const auto unq = html::parse(R"(<a href="x)");
    ASSERT_EQ(unq.size(), 1u);
    EXPECT_EQ(unq[0].kind, "starttag");
    EXPECT_EQ(html::get_attr(unq[0], "href"), "x");
    // Unquoted value ending at end-of-input.
    const auto bare = html::parse("<a href=x");
    ASSERT_EQ(bare.size(), 1u);
    EXPECT_EQ(html::get_attr(bare[0], "href"), "x");
    // '=' with nothing after it: attribute present with an empty value.
    const auto dangling = html::parse("<a href=");
    ASSERT_EQ(dangling.size(), 1u);
    EXPECT_TRUE(html::has_attr(dangling[0], "href"));
    EXPECT_EQ(html::get_attr(dangling[0], "href"), "");
}

TEST(ParsersHtml, SelfClosingForms) {
    const auto t = html::parse("<br/><hr /><img src='p.png'/><wbr/ ><input disabled>");
    ASSERT_EQ(t.size(), 5u);
    EXPECT_EQ(sig(t[0]), "startendtag|br|");
    EXPECT_EQ(sig(t[1]), "startendtag|hr|");   // space before '/>'
    EXPECT_EQ(sig(t[2]), "startendtag|img|");
    EXPECT_EQ(html::get_attr(t[2], "src"), "p.png");
    EXPECT_EQ(sig(t[3]), "startendtag|wbr|");  // space AFTER the '/': still self-closing
    EXPECT_EQ(sig(t[4]), "starttag|input|");   // void element WITHOUT the slash: plain start
    EXPECT_TRUE(html::has_attr(t[4], "disabled"));
}

TEST(ParsersHtml, CommentDeclarationAndPi) {
    const auto t = html::parse("<!DOCTYPE html><!-- a -- comment --><?php echo 1; ?>");
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(sig(t[0]), "decl||DOCTYPE html");
    EXPECT_EQ(sig(t[1]), "comment|| a -- comment ");  // body between <!-- and -->
    EXPECT_EQ(sig(t[2]), "pi||php echo 1; ?");        // body between <? and >
    // Each construct unterminated: consumed to end-of-input, still one token.
    const auto uc = html::parse("<!-- never closed");
    ASSERT_EQ(uc.size(), 1u);
    EXPECT_EQ(sig(uc[0]), "comment|| never closed");
    const auto ud = html::parse("x<!DOCTYPE html");
    ASSERT_EQ(ud.size(), 2u);
    EXPECT_EQ(sig(ud[0]), "data||x");  // pending text flushed before the decl
    EXPECT_EQ(sig(ud[1]), "decl||DOCTYPE html");
    const auto up = html::parse("<?pi never closed");
    ASSERT_EQ(up.size(), 1u);
    EXPECT_EQ(sig(up[0]), "pi||pi never closed");
    // "<!" as the very last bytes: an empty declaration, not a crash.
    const auto bang = html::parse("y<!");
    ASSERT_EQ(bang.size(), 2u);
    EXPECT_EQ(sig(bang[1]), "decl||");
}

TEST(ParsersHtml, ScriptAndStyleAreRawText) {
    // References are NOT decoded inside script; the close tag matches case-insensitively.
    const auto t = html::parse("<SCRIPT>if (a &amp;& b < c) {}</SCRIPT><p>&amp;</p>");
    ASSERT_EQ(t.size(), 6u);
    EXPECT_EQ(sig(t[0]), "starttag|script|");
    EXPECT_EQ(sig(t[1]), "data||if (a &amp;& b < c) {}");  // verbatim body
    EXPECT_EQ(sig(t[2]), "endtag|script|");
    EXPECT_EQ(sig(t[4]), "data||&");                       // ordinary text IS decoded
    const auto s = html::parse("<style>a > b { color: red }</style>");
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(sig(s[1]), "data||a > b { color: red }");
    // Empty body: no data token between the tags.
    const auto e = html::parse("<script></script>");
    ASSERT_EQ(e.size(), 2u);
    EXPECT_EQ(e[0].kind, "starttag");
    EXPECT_EQ(e[1].kind, "endtag");
    // Unterminated: body runs to end-of-input, no close tag emitted.
    const auto u = html::parse("<script>var a = 1;");
    ASSERT_EQ(u.size(), 2u);
    EXPECT_EQ(sig(u[1]), "data||var a = 1;");
    // Close tag present but its '>' is missing: end tag still emitted, input consumed.
    const auto nc = html::parse("<script>x</script");
    ASSERT_EQ(nc.size(), 3u);
    EXPECT_EQ(sig(nc[1]), "data||x");
    EXPECT_EQ(sig(nc[2]), "endtag|script|");
    // A SELF-CLOSED <script/> has no raw-text body: following markup parses normally.
    const auto sc = html::parse("<script/><b>t</b>");
    ASSERT_EQ(sc.size(), 4u);
    EXPECT_EQ(sig(sc[0]), "startendtag|script|");
    EXPECT_EQ(sig(sc[1]), "starttag|b|");
}

TEST(ParsersHtml, MalformedMarkupIsLenient) {
    // Stray '<' before a non-name character is literal text.
    const auto lt = html::parse("a < b and 3<4");
    ASSERT_EQ(lt.size(), 1u);
    EXPECT_EQ(sig(lt[0]), "data||a < b and 3<4");
    // '<' as the very last byte.
    const auto tail = html::parse("x<");
    ASSERT_EQ(tail.size(), 1u);
    EXPECT_EQ(sig(tail[0]), "data||x<");
    // "</" with no name: literal text, not an end tag.
    const auto slash = html::parse("</ div>");
    ASSERT_EQ(slash.size(), 1u);
    EXPECT_EQ(sig(slash[0]), "data||</ div>");
    // "</" as the very last bytes.
    const auto se = html::parse("y</");
    ASSERT_EQ(se.size(), 1u);
    EXPECT_EQ(sig(se[0]), "data||y</");
    // An end tag whose '>' never comes: consumed to end-of-input.
    const auto ue = html::parse("a</div");
    ASSERT_EQ(ue.size(), 2u);
    EXPECT_EQ(sig(ue[0]), "data||a");
    EXPECT_EQ(sig(ue[1]), "endtag|div|");
    // End tag with junk between the name and '>': the junk is skipped.
    const auto junk = html::parse("</div junk>x");
    ASSERT_EQ(junk.size(), 2u);
    EXPECT_EQ(sig(junk[0]), "endtag|div|");
    EXPECT_EQ(sig(junk[1]), "data||x");
    // Unterminated start tag: attributes parsed, tag emitted, nothing after.
    const auto us = html::parse("<div class=\"x\" id=\"y");
    ASSERT_EQ(us.size(), 1u);
    EXPECT_EQ(us[0].kind, "starttag");
    EXPECT_EQ(html::get_attr(us[0], "id"), "y");
    // Empty input: no tokens.
    EXPECT_TRUE(html::parse("").empty());
}

TEST(ParsersHtml, NestedDocumentWalk) {
    const auto t = html::parse("<ul id=\"m\"><li>One</li><li>Two &gt; 1</li></ul>tail");
    ASSERT_EQ(t.size(), 9u);
    EXPECT_EQ(sig(t[0]), "starttag|ul|");
    EXPECT_EQ(sig(t[1]), "starttag|li|");
    EXPECT_EQ(sig(t[2]), "data||One");
    EXPECT_EQ(sig(t[3]), "endtag|li|");
    EXPECT_EQ(sig(t[4]), "starttag|li|");
    EXPECT_EQ(sig(t[5]), "data||Two > 1");
    EXPECT_EQ(sig(t[6]), "endtag|li|");
    EXPECT_EQ(sig(t[7]), "endtag|ul|");
    EXPECT_EQ(sig(t[8]), "data||tail");  // trailing text flushed at end-of-input
}

TEST(ParsersHtml, GetAttrHasAttrLookup) {
    const auto t = html::parse(R"(<a HREF="link" data-k>x</a>)");
    ASSERT_GE(t.size(), 1u);
    // Case-insensitive on BOTH sides: stored names are lowercased, queries are lowered too.
    EXPECT_EQ(html::get_attr(t[0], "href"), "link");
    EXPECT_EQ(html::get_attr(t[0], "HREF"), "link");
    EXPECT_TRUE(html::has_attr(t[0], "DATA-K"));
    EXPECT_EQ(html::get_attr(t[0], "data-k"), "");   // present but valueless
    EXPECT_FALSE(html::has_attr(t[0], "missing"));
    EXPECT_EQ(html::get_attr(t[0], "missing"), "");
    // A token with no attrs at all (the end tag).
    EXPECT_FALSE(html::has_attr(t[2], "href"));
    EXPECT_EQ(html::get_attr(t[2], "href"), "");
}

}  // namespace
