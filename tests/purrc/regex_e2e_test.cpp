// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// regex end-to-end adversarial suite (suite RegexE2E).
//
// Each test compiles a real .purr program with purrc and runs it under the cheatah runtime, checking
// exact stdout — so this exercises the WHOLE pipeline (compiler + runtime + libcheatah_regex.a) the
// way a user hits it. The emphasis is adversarial: catastrophic-backtracking (ReDoS) inputs that must
// stay fast and correct, anchoring/quantifier/class edge cases, and the leftmost-longest contract.
//
// Every expected string here was captured from the built engine (build/debug) — not guessed.
// `regex.find` returns `.text` as an OWNED `str` (the library copies the matched bytes), so it is
// always safe — no borrow, no dangling, even off a temporary input.
//
// These are NOT named *CompileRun*, so they run by default in the gate (ctest stage). They are fast
// (small inputs; the DFA is linear, so even the ReDoS rows return in microseconds).

#include "e2e_harness.hpp"

using e2e::expect_e2e;

// ── yes/no answers: search (unanchored) and full_match (anchored both ends) ───────────────

TEST(RegexE2E, SearchPresentAbsent) {
    expect_e2e("regex_search", R"PURR(import io
import regex
let d = regex.compile("[0-9]+")
io.print(regex.search(d, "order 4567 shipped"))
io.print(regex.search(d, "no digits here"))
)PURR",
    "True\nFalse\n");
}

TEST(RegexE2E, FullMatchIsAnchoredBothEnds) {
    expect_e2e("regex_fullmatch", R"PURR(import io
import regex
let d = regex.compile("[0-9]+")
io.print(regex.full_match(d, "4567"))
io.print(regex.full_match(d, "x4567"))
io.print(regex.full_match(d, "4567x"))
)PURR",
    "True\nFalse\nFalse\n");
}

// ── find: leftmost-longest match, offsets, and the OWNED matched bytes ─────────────────────

TEST(RegexE2E, FindOffsetsAndOwnedText) {
    expect_e2e("regex_find", R"PURR(import io
import regex
let email = regex.compile("[a-z]+@[a-z.]+")
let text = "contact bob@example.com now"
let m = regex.find(email, text)
io.print(m.matched)
io.print(m.begin, m.end)
io.print(m.text)
io.print(m.text)
)PURR",
    "True\n8 23\nbob@example.com\nbob@example.com\n");
}

TEST(RegexE2E, FindIsLeftmostLongest) {
    expect_e2e("regex_greedy", R"PURR(import io
import regex
let g = regex.compile("a+")
let s = "baaab"
let m = regex.find(g, s)
io.print(m.text, m.begin, m.end)
)PURR",
    "aaa 1 4\n");
}

TEST(RegexE2E, FindAlternationLeftmost) {
    expect_e2e("regex_alt", R"PURR(import io
import regex
let alt = regex.compile("cat|dog|bird")
let s = "I have a dog and a cat"
let m = regex.find(alt, s)
io.print(m.text, m.begin, m.end)
)PURR",
    "dog 9 12\n");
}

// ── the whole point: adversarial ReDoS inputs stay fast AND correct (a backtracker hangs) ──

TEST(RegexE2E, ReDoSNestedQuantifierReturnsFalseFast) {
    // (a+)+$ over a long run of 'a' ending in '!' — a classic catastrophic-backtracking pattern.
    // A backtracking engine explores ~2^n paths; the lazy DFA answers in linear time.
    expect_e2e("regex_redos1", R"PURR(import io
import regex
let evil = regex.compile("(a+)+$")
let s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!"
io.print(regex.search(evil, s))
)PURR",
    "False\n");
}

TEST(RegexE2E, ReDoSAlternationStarReturnsFalseFast) {
    expect_e2e("regex_redos2", R"PURR(import io
import regex
let evil = regex.compile("(a|a)*c")
let s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
io.print(regex.search(evil, s))
)PURR",
    "False\n");
}

// ── anchors ───────────────────────────────────────────────────────────────────────────────

TEST(RegexE2E, Anchors) {
    expect_e2e("regex_anchors", R"PURR(import io
import regex
io.print(regex.search(regex.compile("^foo"), "foo bar"))
io.print(regex.search(regex.compile("^foo"), "bar foo"))
io.print(regex.search(regex.compile("bar$"), "foo bar"))
io.print(regex.search(regex.compile("bar$"), "bar foo"))
)PURR",
    "True\nFalse\nTrue\nFalse\n");
}

// ── empty matches: a pattern that can match "" matches EVERYWHERE the anchors allow ───────
// (Pins the a*$ hole: the first-byte candidate scan must not skip the empty match at
// end-of-input — see PreviouslyBroken.RegexEmptyMatchAtEndOfInput for the full story.)

TEST(RegexE2E, EmptyMatchAtEveryAnchoring) {
    expect_e2e("regex_empty", R"PURR(import io
import regex
io.print(regex.search(regex.compile("a*$"), "bbb"))
io.print(regex.search(regex.compile("x*"), "yyy"))
io.print(regex.search(regex.compile("^$"), ""))
io.print(regex.search(regex.compile("^$"), "a"))
io.print(regex.search(regex.compile("$"), "abc"))
let m = regex.find(regex.compile("x*"), "yyy")
io.print(m.matched, m.begin, m.end)
)PURR",
    "True\n"          // a*$ — empty match at end-of-input
    "True\n"          // x* unanchored — empty match at position 0
    "True\n"          // ^$ on "" — the empty input matches
    "False\n"         // ^$ on "a" — both anchors can NOT hold around a byte
    "True\n"          // bare $ — empty match at end of any input
    "True 0 0\n");    // find pins the leftmost empty match at [0,0)
}

// ── quantifiers: * + ? ─────────────────────────────────────────────────────────────────────

TEST(RegexE2E, Quantifiers) {
    expect_e2e("regex_quant", R"PURR(import io
import regex
let plus = regex.compile("ab+c")
io.print(regex.search(plus, "ac"))
io.print(regex.search(plus, "abc"))
io.print(regex.search(plus, "abbbbc"))
io.print(regex.full_match(regex.compile("ab*c"), "ac"))
io.print(regex.full_match(regex.compile("colou?r"), "color"))
io.print(regex.full_match(regex.compile("colou?r"), "colour"))
)PURR",
    "False\nTrue\nTrue\nTrue\nTrue\nTrue\n");
}

// ── character classes, negation, and the \d \w \s escapes ─────────────────────────────────

TEST(RegexE2E, CharacterClasses) {
    expect_e2e("regex_classes", R"PURR(import io
import regex
io.print(regex.full_match(regex.compile("[^0-9]+"), "abcDEF"))
io.print(regex.full_match(regex.compile("\\d+"), "12345"))
io.print(regex.full_match(regex.compile("\\w+"), "abc_123"))
io.print(regex.search(regex.compile("\\s"), "a b"))
io.print(regex.full_match(regex.compile("[A-Z]+"), "abc"))
io.print(regex.full_match(regex.compile("[A-Z]+"), "ABC"))
)PURR",
    "True\nTrue\nTrue\nTrue\nFalse\nTrue\n");
}

// ── `.` matches any byte EXCEPT newline ────────────────────────────────────────────────────

TEST(RegexE2E, DotExcludesNewline) {
    expect_e2e("regex_dot", R"PURR(import io
import regex
let dot = regex.compile("a.b")
io.print(regex.search(dot, "axb"))
io.print(regex.search(dot, "a\nb"))
)PURR",
    "True\nFalse\n");
}

// ── escaped metacharacters are literals ────────────────────────────────────────────────────

TEST(RegexE2E, EscapedDotIsLiteral) {
    expect_e2e("regex_escape", R"PURR(import io
import regex
let re = regex.compile("a\\.b")
io.print(regex.full_match(re, "a.b"))
io.print(regex.full_match(re, "axb"))
)PURR",
    "True\nFalse\n");
}

// ── a malformed pattern is rejected, not thrown: Pattern.ok == false ───────────────────────

TEST(RegexE2E, MalformedPatternReportsNotOk) {
    expect_e2e("regex_bad", R"PURR(import io
import regex
io.print(regex.compile("[0-9]+").ok)
io.print(regex.compile("[").ok)
)PURR",
    "True\nFalse\n");
}

// ── a worked loop: pull every number out of a line by advancing past each match ────────────

TEST(RegexE2E, FindAllNumbersByAdvancing) {
    expect_e2e("regex_findall", R"PURR(import io
import regex
let re = regex.compile("[0-9]+")
let line = "id=48213 status=200 bytes=1274"
let rest = line
for _ in range(0, 10) {
    let f = regex.find(re, rest)
    if not f.matched { break }
    io.print(f.text)
    rest = rest[f.end:]
}
)PURR",
    "48213\n200\n1274\n");
}
