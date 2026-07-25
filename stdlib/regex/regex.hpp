// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file regex.hpp
 * @brief `regex` — a fast, from-scratch regular-expression engine. `import regex`.
 *
 * A **linear-time** matcher: the pattern compiles to a Thompson NFA and runs as a **lazy
 * DFA** (RE2-style), so one DFA pass is O(n) in the input length with **no backtracking** — it
 * cannot blow up exponentially on adversarial patterns the way `std::regex` and backtracking
 * engines (Boost) do. (An unanchored @ref search / @ref find may retry the anchored DFA from
 * successive start positions, so the worst case over the whole input is quadratic — still
 * polynomial, never exponential; see each function's `@complexity`.) A compiled @ref Pattern owns its program **and a reusable DFA cache**, so
 * repeated matches over one pattern warm the cache and pay only a byte-table lookup per input
 * character. It is statically typed and **never allocates an intermediate string**: matching
 * touches only the input bytes (as a `string_view`) and integer program-counters. @ref find
 * returns the matched bytes as an **owned `str`** (the library owns them — no borrow, so nothing
 * can dangle), plus the byte offsets for callers who prefer to slice their own input zero-copy.
 *
 * Supported syntax (a pragmatic subset): literals; `.` (any byte except newline); character
 * classes `[...]` with ranges and negation `[^...]`; the escapes `\d \D \w \W \s \S` and
 * escaped metacharacters (`\. \* \\` …); the quantifiers `* + ?`; alternation `|`; grouping
 * `(...)`; and the anchors `^` `$`. (Backreferences and lookaround are intentionally out of
 * scope — they are what force backtracking and the exponential blowups this engine avoids.)
 */
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace cheatah::regex {

/// The compiled program plus its lazy-DFA cache (an opaque implementation type; defined in the
/// translation unit). A @ref Pattern shares one by value.
struct Dfa;

/// A compiled regular expression. Value-semantic: it shares a compiled program + DFA cache, so
/// copying is cheap and reuse is fast. Build it once with @ref compile, then match many times.
struct Pattern {
    std::shared_ptr<Dfa> impl;   ///< the compiled program + reusable DFA cache (nullptr if !ok).
    bool ok = false;             ///< false if @ref compile rejected the source (see @ref error).
    std::string error;           ///< a human-readable reason when `ok` is false.
};

/**
 * Compile @p pattern into a reusable @ref Pattern. Never throws — on a malformed pattern it
 * returns a `Pattern` with `ok == false` and a message in `error`.
 * @param pattern the regular-expression source.
 * @return the compiled pattern (check `.ok`).
 * @complexity O(m) in the pattern length.
 * @alloc the compiled program + DFA cache on the heap (owned by the returned Pattern).
 * @systest RegexE2E.MalformedPatternReportsNotOk
 * @systest RegexE2E.SearchPresentAbsent
 */
Pattern compile(std::string_view pattern);

/**
 * Whether @p re matches **anywhere** in @p text (an unanchored search).
 * @param re a compiled pattern.
 * @param text the input to search.
 * @return true if some substring of @p text matches.
 * @complexity one DFA pass — O(n) amortized in the length of @p text — when the pattern is
 *             start-anchored or has no first-byte information (the built-in `.*?` prefix); the
 *             unanchored fast path re-runs the anchored DFA from each candidate first byte, so
 *             adversarial input (e.g. `a*b` over `aaa…a`) is O(n²) worst case — always polynomial,
 *             never exponential (no backtracking). Add O(m) per uncached transition while new DFA
 *             states are still being built (m = pattern size).
 * @alloc never copies the input; allocates only DFA machinery — a start-state closure scratch (and
 *        its intern key) per call, plus new states/transition rows while the shared cache is cold.
 * @warning a pathological pattern whose lazy DFA exceeds the state budget (100k states) throws
 *          `std::runtime_error` rather than exhausting memory.
 * @systest RegexE2E.SearchPresentAbsent
 * @systest RegexE2E.ReDoSNestedQuantifierReturnsFalseFast
 */
bool search(const Pattern& re, std::string_view text);

/**
 * Whether @p re matches the **entire** @p text (anchored at both ends).
 * @param re a compiled pattern.
 * @param text the input.
 * @return true iff all of @p text matches.
 * @complexity O(n) amortized in the length of @p text (a single anchored DFA pass); O(n·m) worst
 *             case while new DFA states are still being built (m = pattern size).
 * @alloc never copies the input; allocates only DFA machinery — a start-state closure scratch (and
 *        its intern key) per call, plus new states/transition rows while the shared cache is cold.
 * @warning a pathological pattern whose lazy DFA exceeds the state budget (100k states) throws
 *          `std::runtime_error` rather than exhausting memory.
 * @systest RegexE2E.FullMatchIsAnchoredBothEnds
 */
bool full_match(const Pattern& re, std::string_view text);

/// The result of @ref find: the leftmost-longest match. @ref text is an **owned `str`** — the library
/// copies the matched bytes into it, so it stays valid no matter what happens to the input (no borrow,
/// no dangling). @ref begin / @ref end are the byte offsets into the input, for callers who would
/// rather slice their own (already-owned) input zero-copy instead of using the copy.
struct Match {
    bool matched = false;         ///< whether a match was found.
    std::size_t begin = 0;        ///< byte offset of the match start in the input.
    std::size_t end = 0;          ///< byte offset one past the match end.
    std::string text;             ///< the matched bytes — OWNED (empty when `!matched`).
};

/**
 * Find the **leftmost-longest** match of @p re in @p text and return it as a @ref Match — the byte
 * offsets plus `.text`, an **owned copy** of the matched bytes (safe to keep past @p text).
 * @param re a compiled pattern.
 * @param text the input to search.
 * @return the match (check `.matched`); `.text` owns the matched bytes.
 * @complexity O(n) amortized per start position tried (the DFA-state cache is shared across start
 *             positions); an unanchored pattern retries from each successive position until a match,
 *             so a late or absent match is O(n²) worst case over the whole input — always polynomial,
 *             never exponential (no backtracking). Add O(m) per uncached transition while new DFA
 *             states are built.
 * @alloc the matching itself never copies the input — it allocates only DFA machinery (a start-state
 *        closure scratch per start position, plus new states while the cache is cold); on a hit,
 *        `.text` owns one copy of the matched bytes (`end - begin`). Use `.begin`/`.end` to avoid
 *        even that.
 * @warning a pathological pattern whose lazy DFA exceeds the state budget (100k states) throws
 *          `std::runtime_error` rather than exhausting memory.
 * @systest RegexE2E.FindOffsetsAndOwnedText
 * @systest RegexE2E.FindIsLeftmostLongest
 */
Match find(const Pattern& re, std::string_view text);

}  // namespace cheatah::regex
