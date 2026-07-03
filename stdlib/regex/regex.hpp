#pragma once

/**
 * @file regex.hpp
 * @brief `regex` — a fast, from-scratch regular-expression engine. `import regex`.
 *
 * A **linear-time** matcher: the pattern compiles to a Thompson NFA and runs as a **lazy
 * DFA** (RE2-style), so matching is O(n) in the input length with **no backtracking** — it
 * cannot blow up on adversarial patterns the way `std::regex` and backtracking engines
 * (Boost) do. A compiled @ref Pattern owns its program **and a reusable DFA cache**, so
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
 */
Pattern compile(std::string_view pattern);

/**
 * Whether @p re matches **anywhere** in @p text (an unanchored search).
 * @param re a compiled pattern.
 * @param text the input to search.
 * @return true if some substring of @p text matches.
 * @complexity O(n) amortized in the length of @p text (lazy-DFA, no backtracking); O(n·m) worst case
 *             while new DFA states are still being built (m = pattern size).
 * @alloc none beyond growing the shared DFA cache; never an intermediate string.
 */
bool search(const Pattern& re, std::string_view text);

/**
 * Whether @p re matches the **entire** @p text (anchored at both ends).
 * @param re a compiled pattern.
 * @param text the input.
 * @return true iff all of @p text matches.
 * @complexity O(n) amortized in the length of @p text; O(n·m) worst case while new DFA states are
 *             still being built (m = pattern size).
 * @alloc none beyond growing the shared DFA cache; never an intermediate string.
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
 * @complexity O(n) amortized; O(n·m) worst case while new DFA states are built. The DFA-state cache is
 *             shared across start positions.
 * @alloc the matching itself allocates nothing beyond growing the shared DFA cache; on a hit, `.text`
 *        owns one copy of the matched bytes (`end - begin`). Use `.begin`/`.end` to avoid even that.
 */
Match find(const Pattern& re, std::string_view text);

}  // namespace cheatah::regex
