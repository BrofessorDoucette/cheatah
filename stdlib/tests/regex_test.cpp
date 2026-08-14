// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// Direct C++ unit tests for cheatah::regex — the in-process complement to the RegexE2E
// suite (tests/purrc/regex_e2e_test.cpp, which drives the engine through compiled .purr).
// regex.cpp is a DIRECT source of this test binary, so these tests carry the module's
// line/function coverage; the matrix below deliberately reaches every compile-error string,
// every matcher fast path, the empty-class first-set fallback, and the DFA state-budget
// throw. The engine's semantics are additionally cross-checked against Google RE2 by the
// standalone differential suite (stdlib/regex/bench/rxdiff.cpp).

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <string_view>

#include "regex.hpp"

namespace rx = cheatah::regex;

namespace {

rx::Match find(const char* pat, std::string_view text) {
    rx::Pattern p = rx::compile(pat);
    EXPECT_TRUE(p.ok) << pat << ": " << p.error;
    return rx::find(p, text);
}

bool search(const char* pat, std::string_view text) {
    rx::Pattern p = rx::compile(pat);
    EXPECT_TRUE(p.ok) << pat << ": " << p.error;
    return rx::search(p, text);
}

bool full(const char* pat, std::string_view text) {
    rx::Pattern p = rx::compile(pat);
    EXPECT_TRUE(p.ok) << pat << ": " << p.error;
    return rx::full_match(p, text);
}

}  // namespace

// ---- compile: every documented error string, exactly --------------------------------

TEST(Regex, CompileErrorStrings) {
    struct Case { const char* pat; const char* err; };
    const Case cases[] = {
        {"(", "unbalanced '('"},
        {"a(b", "unbalanced '('"},
        {"((a)", "unbalanced '('"},
        {"[a-", "unbalanced '['"},
        {"[abc", "unbalanced '['"},
        {"[\\]", "unbalanced '['"},      // the escaped ']' is a class member, so the class never closes
        {"a\\", "trailing backslash"},
        {"*a", "unexpected metacharacter"},
        {"+a", "unexpected metacharacter"},
        {"?", "unexpected metacharacter"},
        {"a|*", "unexpected metacharacter"},
        {"(|*)", "unexpected metacharacter"},  // the inner error survives the enclosing group
        {")", "unexpected trailing input"},
        {"a)b", "unexpected trailing input"},
    };
    for (const Case& c : cases) {
        rx::Pattern p = rx::compile(c.pat);
        EXPECT_FALSE(p.ok) << c.pat;
        EXPECT_EQ(p.error, c.err) << c.pat;
        EXPECT_EQ(p.impl, nullptr) << c.pat;
    }
}

TEST(Regex, PatternLengthCapAtBoundary) {
    // compile() spends ~40 bytes of program per pattern byte, so length is capped (64 KiB)
    // the same way nesting depth is: a crafted huge pattern is rejected, not allocated.
    EXPECT_TRUE(rx::compile(std::string(64 * 1024, 'a')).ok);
    rx::Pattern p = rx::compile(std::string(64 * 1024 + 1, 'a'));
    EXPECT_FALSE(p.ok);
    EXPECT_EQ(p.error, "pattern too long");
}

TEST(Regex, DepthCapAtParseBoundary) {
    // 1000 nested groups parse; 1001 hit kMaxParseDepth. The cap protects the C++ stack.
    std::string deep_ok(1000, '(');
    deep_ok += "a";
    deep_ok += std::string(1000, ')');
    EXPECT_TRUE(rx::compile(deep_ok).ok);

    std::string too_deep(1001, '(');
    too_deep += "a";
    too_deep += std::string(1001, ')');
    rx::Pattern p = rx::compile(too_deep);
    EXPECT_FALSE(p.ok);
    EXPECT_EQ(p.error, "pattern nested too deeply");
}

TEST(Regex, InvalidPatternIsInertInEveryMatcher) {
    rx::Pattern bad = rx::compile("(");
    EXPECT_FALSE(rx::search(bad, "abc"));
    EXPECT_FALSE(rx::full_match(bad, "abc"));
    EXPECT_FALSE(rx::find(bad, "abc").matched);
    rx::Pattern empty_default;  // never touched compile() at all
    EXPECT_FALSE(rx::search(empty_default, "abc"));
}

// ---- syntax matrix ------------------------------------------------------------------

TEST(Regex, LiteralsAndEscapedMetacharacters) {
    EXPECT_TRUE(search("status=200", "a status=200 b"));
    EXPECT_FALSE(search("status=500", "a status=200 b"));
    EXPECT_TRUE(search("a\\.b", "xa.by"));
    EXPECT_FALSE(search("a\\.b", "xaXby"));
    EXPECT_TRUE(search("\\*\\\\", "x*\\y"));
    EXPECT_TRUE(search("\\q", "q"));  // unknown escape is the literal byte
}

TEST(Regex, DotMatchesAnyByteExceptNewline) {
    EXPECT_TRUE(search("a.c", "abc"));
    EXPECT_TRUE(search("a.c", "a\tc"));
    EXPECT_FALSE(search("a.c", std::string_view("a\nc", 3)));
    EXPECT_TRUE(full(".*", "any thing"));
    EXPECT_FALSE(search(".", std::string_view("\n", 1)));
}

TEST(Regex, CharacterClasses) {
    EXPECT_TRUE(full("[abc]+", "cabba"));
    EXPECT_FALSE(search("[abc]", "xyz"));
    EXPECT_TRUE(full("[a-z0-9]+", "id42"));
    EXPECT_TRUE(full("[a-]+", "a-a-"));        // trailing '-' is a literal
    EXPECT_TRUE(full("[]a]+", "]a]"));          // ']' first is a literal member
    EXPECT_TRUE(full("[\\d]+", "042"));         // escape inside a class
    EXPECT_TRUE(full("[\\q]+", "qq"));          // unknown escape inside a class: literal
    EXPECT_TRUE(search("[^ ]+", "  word  "));
    EXPECT_FALSE(search("[^a]", "aaa"));
    EXPECT_FALSE(search("[^a]", std::string_view("\n", 1)));  // negated classes exclude newline
    EXPECT_FALSE(search("[z-a]", "qrz"));       // reversed range: silently empty
}

TEST(Regex, PerlEscapeClasses) {
    EXPECT_TRUE(full("\\d+", "12345"));
    EXPECT_FALSE(search("\\d", "abc"));
    EXPECT_TRUE(full("\\w+", "wor_d9"));
    EXPECT_FALSE(search("\\w", " .!"));
    EXPECT_TRUE(full("\\s+", " \t\r\n\f\v"));   // cheatah's \s includes \v (Python-style)
    EXPECT_TRUE(full("\\S+", "abc!"));
    EXPECT_FALSE(search("\\S", " \t"));
    EXPECT_TRUE(full("\\D+", "ab\nc"));         // \D is a plain complement: it DOES match \n
    EXPECT_TRUE(full("\\W+", " ,!\n"));
    EXPECT_FALSE(search("\\W", "aZ9_"));
}

TEST(Regex, Quantifiers) {
    EXPECT_TRUE(full("ab*c", "ac"));
    EXPECT_TRUE(full("ab*c", "abbbc"));
    EXPECT_TRUE(full("ab+c", "abc"));
    EXPECT_FALSE(full("ab+c", "ac"));
    EXPECT_TRUE(full("ab?c", "ac"));
    EXPECT_TRUE(full("ab?c", "abc"));
    EXPECT_FALSE(full("ab?c", "abbc"));
    EXPECT_TRUE(full("a**", "aaa"));            // stacked quantifiers are accepted
    EXPECT_TRUE(full("a+?", ""));               // parsed as (a+)? — laziness has no meaning here
}

TEST(Regex, AlternationAndGrouping) {
    EXPECT_TRUE(search("INFO|WARN|ERROR", "a WARN b"));
    EXPECT_FALSE(search("FATAL|PANIC", "a WARN b"));
    EXPECT_TRUE(full("a|", "a"));
    EXPECT_TRUE(full("a|", ""));                // the empty right branch matches ""
    EXPECT_TRUE(full("(a(b(c)))", "abc"));
    EXPECT_TRUE(full("(ab|c)+", "cababc"));
    EXPECT_TRUE(full("((a|b)(c|d))+", "acbd"));
    EXPECT_TRUE(full("()", ""));
}

TEST(Regex, Anchors) {
    EXPECT_TRUE(search("^2026", "2026-07-02"));
    EXPECT_FALSE(search("^2026", "x 2026"));
    EXPECT_TRUE(search("1274$", "bytes=1274"));
    EXPECT_FALSE(search("1274$", "1274 bytes"));
    EXPECT_FALSE(search("ab$", "b"));  // the reverse scan stays alive all the way to offset 0
    EXPECT_TRUE(full("^abc$", "abc"));
    EXPECT_FALSE(full("^abc$", "abcd"));
    EXPECT_TRUE(search("^$", ""));
    EXPECT_FALSE(search("^$", "x"));
}

// ---- match semantics ----------------------------------------------------------------

TEST(Regex, FindIsLeftmostLongest) {
    rx::Match m = find("a|ab", "xaby");
    EXPECT_TRUE(m.matched);
    EXPECT_EQ(m.begin, 1u);
    EXPECT_EQ(m.end, 3u);      // longest at the leftmost start — not the Perl 'a'
    EXPECT_EQ(m.text, "ab");

    m = find("abc|b", "abc");   // the leftmost START wins even when a shorter match ends first
    EXPECT_EQ(m.begin, 0u);
    EXPECT_EQ(m.end, 3u);

    m = find("ab|bc", "abc");   // leftmost start, then its longest end — never "bc"
    EXPECT_EQ(m.begin, 0u);
    EXPECT_EQ(m.end, 2u);

    m = find("[0-9]+", "ab123cd45");
    EXPECT_EQ(m.begin, 2u);
    EXPECT_EQ(m.end, 5u);
    EXPECT_EQ(m.text, "123");
}

TEST(Regex, FindOffsetsAndOwnedText) {
    std::string hay = "request id=48213 user=bob@example.com";
    rx::Match m = find("[a-z]+@[a-z.]+", hay);
    EXPECT_TRUE(m.matched);
    EXPECT_EQ(hay.substr(m.begin, m.end - m.begin), "bob@example.com");
    EXPECT_EQ(m.text, "bob@example.com");  // owned copy, independent of hay's lifetime

    rx::Match none = find("[0-9]+z", hay);
    EXPECT_FALSE(none.matched);
    EXPECT_EQ(none.begin, 0u);
    EXPECT_EQ(none.end, 0u);
    EXPECT_TRUE(none.text.empty());
}

TEST(Regex, FindWithAnchors) {
    rx::Match m = find("^a+", "aab");
    EXPECT_EQ(m.begin, 0u);
    EXPECT_EQ(m.end, 2u);
    EXPECT_FALSE(find("^x", "ab").matched);

    m = find("[0-9]+$", "ab123");
    EXPECT_EQ(m.begin, 2u);
    EXPECT_EQ(m.end, 5u);
    EXPECT_FALSE(find("[0-9]+$", "123ab").matched);

    m = find("(ab|cd)+$", "xxabcd");
    EXPECT_EQ(m.begin, 2u);
    EXPECT_EQ(m.end, 6u);
}

TEST(Regex, EmptyMatches) {
    EXPECT_TRUE(search("a*", "bbb"));           // an empty match anywhere satisfies search
    EXPECT_TRUE(search("a*$", "bbb"));          // ...including at end-of-input under '$'

    rx::Match m = find("a*", "bbb");
    EXPECT_TRUE(m.matched);
    EXPECT_EQ(m.begin, 0u);
    EXPECT_EQ(m.end, 0u);                       // leftmost empty match

    m = find("$", "abc");
    EXPECT_TRUE(m.matched);
    EXPECT_EQ(m.begin, 3u);                     // the empty match AT end-of-input
    EXPECT_EQ(m.end, 3u);

    m = find("^", "abc");
    EXPECT_EQ(m.begin, 0u);
    EXPECT_EQ(m.end, 0u);

    m = find("a*$", "baa");
    EXPECT_EQ(m.begin, 1u);                     // leftmost start whose match reaches the end
    EXPECT_EQ(m.end, 3u);
    m = find("a*$", "bbb");
    EXPECT_EQ(m.begin, 3u);
    EXPECT_EQ(m.end, 3u);

    EXPECT_TRUE(full("a*", ""));
    EXPECT_TRUE(full("(a|)b?", ""));
}

// ---- matcher fast paths -------------------------------------------------------------

TEST(Regex, LiteralChainFrontAndBackSkip) {
    // A pattern that begins with a >=2-byte literal chain arms the front+back candidate probe:
    // memchr the first byte, verify the last byte at its fixed distance, only then run the DFA.
    EXPECT_TRUE(search("status=500", "xx status=500 yy"));            // real hit
    EXPECT_FALSE(search("status=500", "sabcdefg50 status=200x"));     // two false positives, no match
    EXPECT_FALSE(search("status=500", "no such letter at all"));      // front byte absent
    EXPECT_FALSE(search("status=500", "stat"));                       // input shorter than the chain
    EXPECT_TRUE(full("status=500", "status=500"));

    rx::Match m = find("xyz", "wwxyzw");                              // the same probe drives find
    EXPECT_EQ(m.begin, 2u);
    EXPECT_EQ(m.end, 5u);
    EXPECT_FALSE(find("xyz", "xxyw zw").matched);

    // A front-byte storm (>8 false hits) flips the probe into its 16-wide branchless scan;
    // exercise that path absent, present (found inside a block), and via the short tail.
    std::string storm;
    for (int i = 0; i < 40; ++i) storm += "sabcdefg50 ";               // 's'…'0' at the probe
    EXPECT_FALSE(search("status=500", storm));                        // distance, never the literal
    EXPECT_TRUE(search("status=500", storm + "status=500"));          // found in the short tail
    EXPECT_TRUE(search("status=500", storm + "status=500" + std::string(64, '.')));  // in a block
    EXPECT_TRUE(search("status=500", storm.substr(0, 11 * 9 + 3) + "status=500"));
    std::string mid = storm;
    mid[220] = 'q';                                                   // a masked false positive
    EXPECT_FALSE(search("status=500", mid));

    // False hits SPREAD over more than 1 KiB must NOT flip to the block scan — the window
    // resets and memchr keeps sweeping (the rare-front regime where memchr is unbeatable).
    std::string sparse;
    for (int i = 0; i < 24; ++i) sparse += "sabcdefg50" + std::string(190, '.');
    EXPECT_FALSE(search("status=500", sparse));
    EXPECT_TRUE(search("status=500", sparse + "status=500"));
}

TEST(Regex, FindBudgetFallbackOnDenseAbsentInput) {
    // Thousands of 1-byte-dead candidates: after the candidate budget, one unanchored pass
    // proves absence (O(n) total) — and with a match past the budget, the loop carries on.
    std::string xs(6000, 'x');
    EXPECT_FALSE(find("x[0-9]", xs).matched);
    rx::Match m = find("x[0-9]", xs + "x5");
    EXPECT_TRUE(m.matched);
    EXPECT_EQ(m.begin, 6000u);
    EXPECT_EQ(m.text, "x5");
}

TEST(Regex, SearchFastPaths) {
    // Single required first byte -> the memchr path (hit, and miss -> early out).
    EXPECT_TRUE(search("z[0-9]", "abc z5 d"));
    EXPECT_FALSE(search("z[0-9]", "abc d e f"));
    // A candidate that fails mid-run must not stop the scan ('a' at 0 fails, 2 succeeds).
    EXPECT_TRUE(search("ab", "axab"));
    // Multi-byte first set -> the bitset skip path (hit, and exhaustion).
    EXPECT_TRUE(search("INFO|WARN", "xx WARN yy"));
    EXPECT_FALSE(search("INFO|WARN", "xx yy zz"));
    // Dense first set still terminates on absence.
    EXPECT_FALSE(search("[a-z]+@[a-z]+", "no email here!"));

    // The empty character class can never match: its first set is empty, which exercises
    // the no-first-byte-information fallback (a full unanchored DFA pass).
    rx::Pattern empty_class = rx::compile("[^\\d\\D]");
    ASSERT_TRUE(empty_class.ok);
    EXPECT_FALSE(rx::search(empty_class, "abc"));
    EXPECT_FALSE(rx::search(empty_class, ""));
    EXPECT_FALSE(rx::find(empty_class, "abc").matched);
}

TEST(Regex, FullMatchIsAnchoredBothEnds) {
    EXPECT_TRUE(full("[0-9]+", "48213"));
    EXPECT_FALSE(full("[0-9]+", "48213x"));
    EXPECT_FALSE(full("[0-9]+", "x48213"));
    EXPECT_FALSE(full("[0-9]+", ""));
    EXPECT_TRUE(full("[a-z]+@[a-z.]+", "bob@example.com"));
    EXPECT_FALSE(full("[a-z]+@[a-z.]+", "bob@Example.com"));
}

TEST(Regex, PatternIsReusableAndCheapToCopy) {
    rx::Pattern p = rx::compile("[a-z]+=[0-9]+");
    ASSERT_TRUE(p.ok);
    EXPECT_TRUE(rx::search(p, "k=1"));
    EXPECT_TRUE(rx::search(p, "key=42"));      // warm cache, same object
    rx::Pattern copy = p;                       // shares the compiled program + DFA cache
    EXPECT_TRUE(rx::search(copy, "x=9"));
    EXPECT_FALSE(rx::search(copy, "no pairs"));
}

// ---- adversarial: linear time, bounded memory ---------------------------------------

TEST(Regex, RedosPatternsAnswerFast) {
    // Catastrophic-backtracking classics: a lazy DFA answers these in linear time.
    std::string as(64, 'a');
    EXPECT_FALSE(search("(a+)+$", as + "!"));
    EXPECT_TRUE(search("(a+)+$", as));
    EXPECT_FALSE(search("(a|a)*c", as));
    EXPECT_TRUE(search("(a|a)*c", as + "c"));
    EXPECT_FALSE(search("(x+x+)+y", std::string(64, 'x')));
}

TEST(Regex, AdversarialLargeInputsStayLinear) {
    // The same hostile shapes as RedosPatternsAnswerFast, at a megabyte — a linear engine
    // must stay boring here (a backtracker would not finish). Each row pins the answer AND
    // walks one of the large-input machinery paths: the reverse pass alive end-to-end, the
    // single forward pass over dense candidates, and the find candidate budget.
    const std::string a1m(1u << 20, 'a');
    EXPECT_FALSE(search("(a+)+$", a1m + "!"));
    EXPECT_TRUE(search("(a+)+$", a1m));
    EXPECT_FALSE(search("(a|a)*c", a1m));
    EXPECT_TRUE(search("(a|a)*c", a1m + "c"));
    EXPECT_FALSE(search("c[ab]*$", a1m));            // reverse DFA alive across the whole input
    EXPECT_TRUE(search("c[ab]*$", "c" + a1m));
    EXPECT_FALSE(search("[a-z]+@[a-z.]+", std::string(1u << 20, 'z')));  // dense-candidate scan
    rx::Match m = find("(a+)+$", a1m);               // reverse find: leftmost begin at offset 0
    EXPECT_TRUE(m.matched);
    EXPECT_EQ(m.begin, 0u);
    EXPECT_EQ(m.end, a1m.size());
    EXPECT_FALSE(find("x[0-9]", std::string(1u << 20, 'x')).matched);    // budget fallback
}

TEST(Regex, SameByteRunSkipStaysExact) {
    // A state that maps a byte back onto itself lets the scan jump the whole same-byte run
    // (SWAR, 8 bytes per compare). These pin the answers on every run-skip path: forward to
    // exhaustion, forward into a match, a run that ends mid-input, the short-run tail, and
    // the backward skip through an ACCEPTING self-loop (leftmost begin must stay exact).
    const std::string a1m(1u << 20, 'a');
    EXPECT_FALSE(search("[ab]+c", a1m));
    EXPECT_TRUE(search("[ab]+c", a1m + "c"));
    EXPECT_FALSE(search("[ab]+c", a1m + std::string(100, 'x')));
    EXPECT_TRUE(search("a+b", std::string(20, 'a') + "b"));
    rx::Match m = find("[ab]+$", "x" + a1m);
    EXPECT_TRUE(m.matched);
    EXPECT_EQ(m.begin, 1u);
    EXPECT_EQ(m.end, 1u + a1m.size());
}

TEST(Regex, StateBudgetThrowsInsteadOfExhaustingMemory) {
    // "[ab]*a[ab]{18}c" (written out — bounded repeats are not syntax) superposes one DFA
    // state per distinct "which of the last 18 bytes were 'a'" set: up to 2^18 subsets, far
    // past the 100k-state budget. The trailing 'c' never appears in the pure-a/b input, so
    // the run never accepts and never dies — it just keeps minting fresh window shapes until
    // the budget trips; the engine must throw the documented runtime_error rather than
    // allocate without bound.
    std::string pat = "[ab]*a";
    for (int i = 0; i < 18; ++i) pat += "[ab]";
    pat += "c";
    rx::Pattern p = rx::compile(pat);
    ASSERT_TRUE(p.ok);
    std::string hay(512 * 1024, 'a');
    std::uint32_t lcg = 12345;
    for (char& c : hay) {
        lcg = lcg * 1664525u + 1013904223u;
        c = (lcg >> 16) & 1 ? 'a' : 'b';
    }
    EXPECT_THROW((void)rx::search(p, hay), std::runtime_error);
}
