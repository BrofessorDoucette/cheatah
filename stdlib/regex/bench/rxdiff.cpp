// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// rxdiff — differential tests: cheatah::regex vs Google RE2 as a correctness oracle.
//
// RE2 is configured with longest_match + Latin-1 (`eng::Re2Longest`), which matches cheatah's
// documented leftmost-LONGEST byte semantics exactly, so `search`, `full_match` and `find`
// (offsets AND matched text) must agree byte-for-byte on every pattern in the supported subset.
//
// Documented cheatah deviations are folded into the ORACLE pattern, never skipped silently:
//   - a negated class excludes '\n' in cheatah, so `[^ ]` is checked against RE2's `[^ \n]`;
//   - cheatah's `\s` includes '\v' (Python-style) and RE2's does not, so `\s`/`\S` rows filter
//     '\v' out of the generated inputs.
// Unsupported syntax ({n,m}, \b, mid-pattern anchors, (?...)) is out of corpus by construction.
//
// Every failure prints a hex-escaped reproducer that pastes directly into a unit test.

#include "engines.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Pat {
    const char* ch;             // the cheatah pattern
    const char* re2 = nullptr;  // oracle rewrite when a documented deviation applies (else same)
    bool strip_vtab = false;    // pattern uses \s or \S: drop '\v' from inputs (RE2's \s lacks it)
    const char* seeds[3] = {nullptr, nullptr, nullptr};  // strings worth embedding in inputs
};

const Pat kCorpus[] = {
    // literals
    {"abc", nullptr, false, {"abc", "xxabcxx"}},
    {"a", nullptr, false, {"a"}},
    {"status=200", nullptr, false, {"status=200", "status=500"}},
    // classes
    {"[abc]+", nullptr, false, {"cabba"}},
    {"[a-z]+", nullptr, false, {"hello"}},
    {"[a-]+", nullptr, false, {"a-a"}},
    {"[^ ]+", "[^ \n]+", false, {"two words"}},
    {"[^a-z]+", "[^a-z\n]+", false, {"UP123low"}},
    {"[a-cx-z]+", nullptr, false, {"abxyzc"}},
    {"x[0-9]y", nullptr, false, {"x5y", "x55y"}},
    // escapes
    {"\\d+", nullptr, false, {"12345", "a1b22c"}},
    {"\\w+", nullptr, false, {"wor_d9"}},
    {"\\s+", nullptr, true, {"a  b"}},
    {"\\S+", nullptr, true, {"a  b"}},
    {"\\D+", nullptr, false, {"abc123"}},
    {"\\W+", nullptr, false, {"a, b!"}},
    {"a\\.b", nullptr, false, {"a.b", "axb"}},
    {"\\*+", nullptr, false, {"a**b"}},
    {"[\\d]+", nullptr, false, {"42"}},
    // quantifiers
    {"a*", nullptr, false, {"aaa", "bbb"}},
    {"a+", nullptr, false, {"aaa", "baab"}},
    {"ab?c", nullptr, false, {"ac", "abc"}},
    {"a?a?a?aaa", nullptr, false, {"aaa", "aaaaaa"}},
    {"(ab)*", nullptr, false, {"ababab"}},
    {"(ab)+c?", nullptr, false, {"ababc"}},
    {"(a*)*b", nullptr, false, {"aaab"}},
    // alternation (leftmost-longest stressors)
    {"a|ab", nullptr, false, {"xaby", "ab"}},
    {"abc|b", nullptr, false, {"abc", "xbc"}},
    {"ab|bc", nullptr, false, {"abc", "xbc"}},
    {"aa|ba", nullptr, false, {"baa", "aaa"}},
    {"(a|ab)(c|bcd)", nullptr, false, {"abcd", "ac"}},
    {"a|", nullptr, false, {"a", "b"}},
    {"INFO|WARN|ERROR", nullptr, false, {"a WARN b"}},
    {"x|y|z", nullptr, false, {"wzv"}},
    // groups
    {"(a(b(c)))", nullptr, false, {"abc"}},
    {"(ab|c)+", nullptr, false, {"cababc"}},
    {"((a|b)(c|d))+", nullptr, false, {"acbd"}},
    {"()", nullptr, false, {"a"}},
    {"(a|)b?", nullptr, false, {"ab", "b"}},
    // anchors (pattern-edge only — the supported subset)
    {"^abc", nullptr, false, {"abcdef", "xabc"}},
    {"abc$", nullptr, false, {"xxabc", "abcx"}},
    {"^abc$", nullptr, false, {"abc", "abcd"}},
    {"^", nullptr, false, {"anything"}},
    {"$", nullptr, false, {"anything"}},
    {"^a*", nullptr, false, {"aaab"}},
    {"a*$", nullptr, false, {"baa", "bbb"}},
    {"[0-9]+$", nullptr, false, {"ab123", "123ab"}},
    {"(ab|cd)+$", nullptr, false, {"xxabcd", "abcdx"}},
    {"1274$", nullptr, false, {"bytes=1274", "1274 bytes"}},
    {"x$", nullptr, false, {"box", "xob"}},
    {"^(a|b)+", nullptr, false, {"abba", "cab"}},
    // dot (excludes newline in both)
    {".", nullptr, false, {"a"}},
    {".+", nullptr, false, {"line one"}},
    {"a.c", nullptr, false, {"abc", "a\nc"}},
    {".*b", nullptr, false, {"aaab", "b"}},
    // realistic composites
    {"[a-z]+@[a-z.]+", nullptr, false, {"bob@example.com", "a@b"}},
    {"id=[0-9]+", nullptr, false, {"id=48213"}},
    {"[0-9]+\\.[0-9]+", nullptr, false, {"3.14"}},
    {"([a-z]+=[^ ]+ ?)+", "([a-z]+=[^ \n]+ ?)+", false, {"k=v x=y "}},
    {"\"[^\"]*\"", "\"[^\"\n]*\"", false, {"say \"hi\" now"}},
    // adversarial shapes (linear here, exponential in backtrackers)
    {"(a+)+$", nullptr, false, {"aaaa!", "aaaa"}},
    {"(a|a)*c", nullptr, false, {"aaac", "aaaa"}},
    {"(x+x+)+y", nullptr, false, {"xxxxy", "xxxx"}},
};

std::uint64_t fnv1a(std::string_view s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

std::string hex_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 4);
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
            out += static_cast<char>(c);
        } else {
            char buf[8];
            snprintf(buf, sizeof buf, "\\x%02X", c);
            out += buf;
        }
    }
    return out;
}

// A biased ASCII alphabet that actually exercises the corpus (letters the patterns use, digits,
// separators) plus the odd metacharacter-looking literal.
std::string rand_ascii(std::mt19937& rng, std::size_t len) {
    static constexpr char kAlpha[] = "aabbccxyz0123 .@=_-|INFOWARN\"!k=v";
    std::string s(len, ' ');
    for (char& c : s) c = kAlpha[rng() % (sizeof kAlpha - 1)];
    return s;
}

std::string rand_bytes(std::mt19937& rng, std::size_t len) {
    std::string s(len, '\0');
    for (char& c : s) c = static_cast<char>(rng() & 0xFF);
    return s;
}

std::vector<std::string> gen_inputs(const Pat& p) {
    std::mt19937 rng(static_cast<std::uint32_t>(0xC0FFEEu ^ fnv1a(p.ch)));
    std::vector<std::string> out = {"",  "a",   "b",   "ab",  "ba",  "abc", "xaby",
                                    "baa", "bbb", "xa",  "ax",  "aab", "abb", "\n",
                                    "a\nb", "\na", "a\n"};
    for (const char* s : p.seeds)
        if (s) out.push_back(s);
    for (int i = 0; i < 14; ++i) out.push_back(rand_ascii(rng, 1 + rng() % 48));
    for (int i = 0; i < 14; ++i) out.push_back(rand_bytes(rng, 1 + rng() % 48));
    for (int i = 0; i < 6; ++i) {  // newline-heavy
        std::string s = rand_ascii(rng, 32);
        for (char& c : s)
            if (rng() % 5 == 0) c = '\n';
        out.push_back(s);
    }
    for (const char* s0 : p.seeds) {  // seed embeddings, doublings, and 1-byte near-misses
        if (!s0) continue;
        const std::string mid = s0;
        std::string s = rand_ascii(rng, rng() % 8) + mid + rand_ascii(rng, rng() % 8);
        out.push_back(s);
        out.push_back(mid + mid);
        out.push_back(mid + "\n" + mid);
        for (int k = 0; k < 5 && !s.empty(); ++k) {
            std::string t = s;
            t[rng() % t.size()] = static_cast<char>('a' + rng() % 26);
            out.push_back(t);
        }
    }
    if (p.strip_vtab)
        for (std::string& s : out)
            for (char& c : s)
                if (c == '\v') c = 'x';
    return out;
}

}  // namespace

TEST(RxDiff, AgreesWithRe2OnCorpus) {
    for (const Pat& p : kCorpus) {
        auto ch = eng::try_compile<eng::Cheatah>(p.ch);
        auto oracle = eng::try_compile<eng::Re2Longest>(p.re2 ? p.re2 : p.ch);
        ASSERT_TRUE(ch != nullptr) << "cheatah rejects corpus pattern: " << p.ch;
        ASSERT_TRUE(oracle != nullptr) << "RE2 rejects oracle pattern for: " << p.ch;
        for (const std::string& in : gen_inputs(p)) {
            SCOPED_TRACE("pattern \"" + std::string(p.ch) + "\" input \"" + hex_escape(in) + "\"");
            const std::string_view t(in);
            ASSERT_EQ(eng::Cheatah::search(*ch, t), eng::Re2Longest::search(*oracle, t));
            ASSERT_EQ(eng::Cheatah::full(*ch, t), eng::Re2Longest::full(*oracle, t));
            std::size_t cb = 0, ce = 0, ob = 0, oe = 0;
            const bool cf = eng::Cheatah::find(*ch, t, cb, ce);
            const bool of = eng::Re2Longest::find(*oracle, t, ob, oe);
            ASSERT_EQ(cf, of);
            if (cf) {
                EXPECT_EQ(cb, ob);
                EXPECT_EQ(ce, oe);
                auto m = cheatah::regex::find(*ch, t);
                EXPECT_EQ(m.text, in.substr(ob, oe - ob));
            }
        }
    }
}

TEST(RxDiff, AdversarialLargeAgreesWithRe2) {
    // The hostile shapes at half a megabyte, cross-checked against the oracle: search AND
    // full_match booleans plus find offsets must agree on inputs this large too (the large-
    // input fast paths — reverse pass, single forward pass, candidate budget — all fire).
    std::mt19937 rng(0xBADCAFE);
    std::string as(512 * 1024, 'a');
    std::string xs(512 * 1024, 'x');
    std::string ab(512 * 1024, 'a');
    for (char& ch : ab)
        if (rng() & 1) ch = 'b';
    const std::string* inputs[] = {&as, &xs, &ab};
    const char* pats[] = {"(a+)+$", "(a|a)*c", "c[ab]*$", "a*$", "[ab]+$", "x[0-9]", "(x+x+)+y"};
    for (const char* pat : pats) {
        auto ch = eng::try_compile<eng::Cheatah>(pat);
        auto oracle = eng::try_compile<eng::Re2Longest>(pat);
        ASSERT_TRUE(ch && oracle) << pat;
        for (const std::string* in : inputs) {
            SCOPED_TRACE(std::string(pat) + " over " + std::to_string(in->size()) + " bytes");
            const std::string_view t(*in);
            ASSERT_EQ(eng::Cheatah::search(*ch, t), eng::Re2Longest::search(*oracle, t));
            ASSERT_EQ(eng::Cheatah::full(*ch, t), eng::Re2Longest::full(*oracle, t));
            std::size_t cb = 0, ce = 0, ob = 0, oe = 0;
            const bool cf = eng::Cheatah::find(*ch, t, cb, ce);
            const bool of = eng::Re2Longest::find(*oracle, t, ob, oe);
            ASSERT_EQ(cf, of);
            if (cf) {
                EXPECT_EQ(cb, ob);
                EXPECT_EQ(ce, oe);
            }
        }
    }
}

TEST(RxDiff, CompileErrorAgreement) {
    // Patterns BOTH engines must reject (the shared malformed set).
    const char* bad[] = {"(", "a(b", "((a)", "[a-", "[abc", "a\\", "*a", "+a", "?a", ")", "a)"};
    for (const char* p : bad) {
        EXPECT_FALSE(cheatah::regex::compile(p).ok) << p;
        EXPECT_TRUE(eng::try_compile<eng::Re2Longest>(p) == nullptr) << p;
    }
    // And every corpus pattern must be accepted by both (the loop above ASSERTs it too, but a
    // compile-only sweep localizes a corpus typo instantly).
    for (const Pat& p : kCorpus) {
        EXPECT_TRUE(cheatah::regex::compile(p.ch).ok) << p.ch;
        EXPECT_TRUE(eng::try_compile<eng::Re2Longest>(p.re2 ? p.re2 : p.ch) != nullptr) << p.ch;
    }
}
