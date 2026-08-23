// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// In-process unit tests for the `parsers.xml` slab-DOM reader (parsers/xml/xml.cpp). xml.cpp is
// compiled DIRECTLY into this test binary (see CMakeLists) so its coverage is measured here — the
// tests below exercise every parse branch (elements/attrs/text/entities/CDATA/comments/PI/DOCTYPE,
// self-closing, and the lenient malformed-input paths) and every navigation function.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "xml/xml.hpp"

namespace xml = cheatah::parsers::xml;

namespace {

// The id of the document element (first top-level element) for convenience.
int doc_elem(const xml::Document& d, const char* tag) {
    return xml::find(d, xml::root(d), tag);
}

// cppcheck-suppress syntaxError  // cppcheck's tokenizer mishandles the embedded " in the raw string
TEST(ParsersXml, ParsesElementsAttrsAndText) {
    auto d = xml::parse(R"(<doc><item id="1" flag>hello</item></doc>)");
    const int doc = doc_elem(d, "doc");
    ASSERT_GE(doc, 0);
    EXPECT_TRUE(xml::is_element(d, doc));
    EXPECT_EQ(xml::tag(d, doc), "doc");
    const int item = xml::find(d, doc, "item");
    ASSERT_GE(item, 0);
    EXPECT_EQ(xml::attr(d, item, "id"), "1");
    EXPECT_TRUE(xml::has_attr(d, item, "flag"));      // valueless attribute
    EXPECT_EQ(xml::attr(d, item, "flag"), "");
    EXPECT_FALSE(xml::has_attr(d, item, "missing"));
    EXPECT_EQ(xml::attr(d, item, "missing"), "");
    EXPECT_EQ(xml::text(d, item), "hello");
}

TEST(ParsersXml, AttributeQuotingForms) {
    auto d = xml::parse(R"(<e a="dq" b='sq' c=bare d = "spaced" />)");
    const int e = doc_elem(d, "e");
    ASSERT_GE(e, 0);
    EXPECT_EQ(xml::attr(d, e, "a"), "dq");
    EXPECT_EQ(xml::attr(d, e, "b"), "sq");
    EXPECT_EQ(xml::attr(d, e, "c"), "bare");    // unquoted value
    EXPECT_EQ(xml::attr(d, e, "d"), "spaced");  // whitespace around '='
    // self-closing element has no children
    EXPECT_TRUE(xml::children(d, e).empty());
}

TEST(ParsersXml, EntityDecodingAllForms) {
    // the 5 predefined + decimal + hex numeric across the UTF-8 size ranges
    auto d = xml::parse("<t>&amp;&lt;&gt;&quot;&apos; &#65; &#169; &#x4E2D; &#x1F600;</t>");
    const int t = doc_elem(d, "t");
    ASSERT_GE(t, 0);
    EXPECT_EQ(xml::text(d, t),
              std::string("&<>\"' ") + "A" + " \xC2\xA9" + " \xE4\xB8\xAD" + " \xF0\x9F\x98\x80");
    // attribute values are decoded too
    auto d2 = xml::parse(R"(<t v="a&amp;b"/>)");
    EXPECT_EQ(xml::attr(d2, doc_elem(d2, "t"), "v"), "a&b");
}

TEST(ParsersXml, UnknownAndMalformedReferencesKeptVerbatim) {
    // unknown named entity, bare '&', numeric with no digits, out-of-range, zero, and a '&'
    // whose ';' is past the 32-char window are all left as-is.
    auto d = xml::parse("<t>&bogus; A & B &#; &#12z; &#x110000; &#0; "
                        "&reallyreallyreallyreallyreallylongname;</t>");
    const std::string got = xml::text(d, doc_elem(d, "t"));
    EXPECT_NE(got.find("&bogus;"), std::string::npos);
    EXPECT_NE(got.find("A & B"), std::string::npos);
    EXPECT_NE(got.find("&#;"), std::string::npos);
    EXPECT_NE(got.find("&#12z;"), std::string::npos);   // invalid digit inside a numeric ref
    EXPECT_NE(got.find("&#x110000;"), std::string::npos);
    EXPECT_NE(got.find("&#0;"), std::string::npos);
    EXPECT_NE(got.find("&really"), std::string::npos);
}

TEST(ParsersXml, NestedTextIsCollectedRecursively) {
    auto d = xml::parse("<p>a<b>B<i>C</i></b>d</p>");
    const int p = doc_elem(d, "p");
    EXPECT_EQ(xml::text(d, p), "aBCd");           // whole subtree
    const int b = xml::find(d, p, "b");
    EXPECT_EQ(xml::text(d, b), "BC");
}

TEST(ParsersXml, FindFindallIterChildren) {
    auto d = xml::parse("<r><x/><x/><y><x/></y>text</r>");
    const int r = doc_elem(d, "r");
    EXPECT_EQ(xml::findall(d, r, "x").size(), 2u);   // direct children only
    EXPECT_EQ(xml::find(d, r, "y"), *(xml::findall(d, r, "y").data()));
    EXPECT_EQ(xml::find(d, r, "nope"), -1);
    EXPECT_TRUE(xml::findall(d, r, "nope").empty());
    // iter() walks the whole subtree, in document order, including a self match
    EXPECT_EQ(xml::iter(d, r, "x").size(), 3u);      // two direct + one under <y>
    EXPECT_EQ(xml::iter(d, r, "r").size(), 1u);      // matches self
    // children() includes the trailing text node
    bool saw_text = false;
    for (int c : xml::children(d, r)) if (!xml::is_element(d, c)) saw_text = true;
    EXPECT_TRUE(saw_text);
}

TEST(ParsersXml, CommentsCdataPiPrologDoctype) {
    auto d = xml::parse("<?xml version=\"1.0\"?>\n<!DOCTYPE doc>\n"
                        "<doc><!-- a comment --><![CDATA[<raw> & ]]></doc>");
    const int doc = doc_elem(d, "doc");
    ASSERT_GE(doc, 0);
    // CDATA is literal (not entity-decoded), comment produces no node/text
    EXPECT_EQ(xml::text(d, doc), "<raw> & ");
}

TEST(ParsersXml, IgnorableVsSignificantWhitespace) {
    // whitespace between top-level nodes is dropped; whitespace inside an element is kept
    auto d = xml::parse("  <a>  </a>  ");
    const int a = doc_elem(d, "a");
    ASSERT_GE(a, 0);
    EXPECT_EQ(xml::text(d, a), "  ");
    // no stray text node at the document root (only the <a> element)
    int elems = 0;
    for (int c : xml::children(d, xml::root(d))) if (xml::is_element(d, c)) ++elems;
    EXPECT_EQ(elems, 1);
    EXPECT_EQ(xml::children(d, xml::root(d)).size(), 1u);
}

TEST(ParsersXml, LenientOnMalformedInput) {
    // none of these may crash; each returns a usable (possibly empty) document
    EXPECT_EQ(xml::children(xml::parse(""), 0).size(), 0u);
    xml::parse("plain text with no tags");
    xml::parse("a < b, 3 < 4");                    // bare '<' as text
    xml::parse("<unterminated attr=\"x");          // tag runs to EOF
    xml::parse("<!-- comment never closed");        // comment to EOF
    xml::parse("<![CDATA[ never closed");           // CDATA to EOF
    xml::parse("<a><b></c></a>");                   // mismatched close (stray </c>)
    xml::parse("</stray>");                         // close with no open element
    xml::parse("<a/>trailing");                     // self-close then trailing text
    auto d = xml::parse("<a><b>deep</b>");          // missing </a>: still navigable
    EXPECT_EQ(xml::tag(d, doc_elem(d, "a")), "a");
    SUCCEED();
}

TEST(ParsersXml, OutOfRangeAndTypeMismatchedIdsAreSafe) {
    auto d = xml::parse("<a>x</a>");
    // invalid ids never read out of bounds
    EXPECT_FALSE(xml::is_element(d, -1));
    EXPECT_FALSE(xml::is_element(d, 9999));
    EXPECT_EQ(xml::tag(d, -1), "");
    EXPECT_EQ(xml::attr(d, 9999, "z"), "");
    EXPECT_FALSE(xml::has_attr(d, 9999, "z"));
    EXPECT_EQ(xml::text(d, -1), "");
    EXPECT_TRUE(xml::children(d, -1).empty());
    EXPECT_EQ(xml::find(d, -1, "z"), -1);
    EXPECT_TRUE(xml::findall(d, -1, "z").empty());
    EXPECT_TRUE(xml::iter(d, -1, "z").empty());
    // tag() on a text node returns ""
    const int a = doc_elem(d, "a");
    const int textnode = xml::children(d, a)[0];
    EXPECT_FALSE(xml::is_element(d, textnode));
    EXPECT_EQ(xml::tag(d, textnode), "");
}

}  // namespace
