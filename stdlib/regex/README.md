# regex

A from-scratch, backtracking-free regular-expression engine for cheatah — `import regex`.

The pattern compiles to a Thompson NFA and runs as a **lazy DFA** (the RE2 approach). `search` and
`full_match` are **O(n) in the input with no backtracking**; `find` retries candidate starts to settle
leftmost-longest, so its worst case is quadratic — polynomial, never exponential. Adversarial patterns
cannot blow it up the way they hang `std::regex` or make Boost.Regex throw. It is **statically typed**
and **never allocates an intermediate string**: matching touches only your input bytes (as a
`string_view`) and integer program-counters. `find` returns the matched bytes as an <b>owned `str`</b>
(no borrow, nothing to dangle), plus offsets so you can slice your own input zero-copy.

## Using it from cheatah

`compile` is the expensive step; matching is cheap and reuses a warm DFA cache. Compile a pattern
once and reuse it.

### `search` / `full_match` — yes/no answers

```purr
import io
import regex

let re = regex.compile("[0-9]+")
let text = "order 4567 shipped"
io.print(regex.search(re, text))          # True  — matches somewhere in `text`
io.print(regex.full_match(re, "4567"))    # True  — the WHOLE string is digits
io.print(regex.full_match(re, "x4567"))   # False — full match is anchored at both ends
```

### `find` — the match, with the bytes you **own**

`find` returns the leftmost-longest match: `matched`, the byte offsets `begin`/`end`, and `text` — an
<b>owned `str`</b> holding a copy of the matched bytes, safe to keep, pass around, or return — even
when the input was a throwaway literal, `m.text` outlives it.

```purr
import io
import regex

let re   = regex.compile("[a-z]+@[a-z.]+")
let text = "contact bob@example.com now"
let m    = regex.find(re, text)
if m.matched {
    io.print(m.text)            # bob@example.com  — an owned str, safe to keep
    io.print(m.begin, m.end)    # 8 23             — byte offsets into your input
}
```

**Zero-copy when you want it.** The only allocation on a hit is `text`'s one copy. To skip even
that, ignore `text` and slice your own (already-owned) input with the offsets:

```purr
import io
import regex

let re   = regex.compile("[a-z]+@[a-z.]+")
let text = "contact bob@example.com now"
let m    = regex.find(re, text)
if m.matched {
    io.print(text[m.begin:m.end])   # bob@example.com — sliced from your own string
}
```

### A worked example — pull every number out of a line

```purr
import io
import regex

fn main() {
    let re = regex.compile("[0-9]+")
    let line = "2026-07-02 id=48213 status=200 bytes=1274"
    let rest = line
    for _ in range(0, 10) {
        let m = regex.find(re, rest)
        if not m.matched { break }
        io.print("number:", m.text)    # an owned str — no copy needed, it already owns its bytes
        rest = rest[m.end:]            # advance past this match
    }
}

main()
```

## Supported syntax

| Construct | Meaning |
|---|---|
| `abc` | literal bytes |
| `.` | any byte except newline |
| `[a-z0-9_]` / `[^…]` | character class (ranges, negation); a negated class never matches newline |
| `\d \D \w \W \s \S` | digit / word / whitespace classes (and their complements) |
| `\. \* \\` … | an escaped metacharacter (a literal) |
| `*` `+` `?` | zero-or-more / one-or-more / optional |
| `a\|b\|c` | alternation |
| `(…)` | grouping |
| `^` … `$` | anchor to the start / end of the input |

Backreferences and lookaround are **intentionally unsupported** — they are exactly the features
that force backtracking and the exponential blow-ups this engine exists to avoid.

## Performance

Four engines — cheatah, `std::regex`, Boost.Regex and **Google RE2** — race the same programs over
identical inputs in the standalone benchmark project `stdlib/regex/bench/` (it fetches and builds all
three rivals from pinned sources; nothing is ever linked into cheatah itself). Every timed case is
**output-verified before anything is timed** — every engine that can finish the case must agree, and
the binary aborts on any disagreement — and a differential suite (`rxdiff`) additionally checks
cheatah against RE2-as-oracle on thousands of generated inputs.

The representative medians, the complete per-case comparison (which carries its own tally), the
honest reading of the parity rows and the commands that reproduce every number are on the
[regex benchmarks](BENCHMARKS.md) page. The claim the tables back — and `RXBENCH_ASSERT=1`
fails the run the moment it stops holding — is that cheatah ties or beats RE2 on every case; that no
input can make it backtrack is a property of the DFA, not of the benchmark.

## Design

- **Lazy DFA** over a Thompson NFA — linear-time search, no backtracking, ReDoS-proof.
- **One load per byte.** Transition entries store the next state's *row byte-offset* directly,
  so the hot loop's carried dependency is a single `[table + state + byte*4]` load; the accept
  flag lives in a 257th slot of each row and is tested off the critical path.
- **Single-pass unanchored search.** The compiled-in `.*?` prefix keeps every start position
  alive in one DFA state, so an absent pattern costs exactly one visit per byte — no
  per-candidate restarts, O(n) always.
- <b>Reversed program for `$`.</b> A pattern anchored only at the end runs *backward* from the
  end of the input: one pass answers existence and yields the leftmost begin, and a wrong tail
  dies in a handful of bytes.
- **Acceleration armed in the start state.** While no partial match is alive, the scan jumps:
  `memchr` for a single required first byte; for a required literal chain, `memchr`+verify that
  flips to a 32-wide branchless front+back block compare under a false-positive storm; a
  first-set LUT otherwise.
- **Self-loop skipping.** Bytes the start state maps back onto itself are learned into a LUT
  and skipped wholesale; and in the unanchored and reversed scans, whenever a state maps a byte
  onto itself, the entire consecutive run of that byte is jumped with an 8-byte SWAR scan — long
  padding/whitespace runs cost almost nothing.
- <b>`find` candidate budget.</b> Leftmost-longest is decided per candidate start (with the same
  skip battery between candidates); on candidate-dense absent input, a budget triggers one
  O(n) existence pass instead of quadratic scanning.
- <b>Value-semantic `Pattern`</b> — shares a compiled program + reusable DFA cache (start states
  included), so copying is cheap and a warm pattern's search allocates nothing.
- **Zero intermediate-string allocation** — matching touches only the input bytes; `find`'s
  only allocation is the one owned copy of the matched bytes (skippable via the offsets).
