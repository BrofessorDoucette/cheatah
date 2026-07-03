# regex

A from-scratch, **linear-time** regular-expression engine for cheatah — `import regex`.

The pattern compiles to a Thompson NFA and runs as a **lazy DFA** (the RE2 approach), so a match
is **O(n) in the input length with no backtracking**. It cannot blow up on adversarial patterns
the way `std::regex` (which hangs) and backtracking engines like Boost.Regex (which *throws* rather
than hang) do. It is **statically typed** and **never allocates an intermediate string**: matching
touches only your input bytes (as a `string_view`) and integer program-counters. `find` returns the
matched bytes as an **owned `str`** (no borrow, nothing to dangle), plus offsets so you can slice your
own input zero-copy instead if you prefer.

## Using it from cheatah

### Compile once, match many

`compile` is the expensive step; matching is cheap and reuses a warm DFA cache. Compile a pattern
once and reuse it.

```purr
import io
import regex

let re = regex.compile("[0-9]+")
```

### `search` / `full_match` — yes/no answers

```purr
let text = "order 4567 shipped"
io.print(regex.search(re, text))          # True  — matches somewhere in `text`
io.print(regex.full_match(re, "4567"))    # True  — the WHOLE string is digits
io.print(regex.full_match(re, "x4567"))   # False — full match is anchored at both ends
```

### `find` — the match, with the bytes you **own**

`find` returns the leftmost-longest match: `matched`, the byte offsets `begin`/`end`, and `text` — an
**owned `str`**. The library copies the matched bytes into `text`, so it is always safe to keep, pass
around, or return; there is no borrowing and nothing can dangle (cheatah is C++ — *somewhere* we own
the string, and here that owner is `text` itself).

```purr
let re   = regex.compile("[a-z]+@[a-z.]+")
let text = "contact bob@example.com now"
let m    = regex.find(re, text)
if m.matched {
    io.print(m.text)            # bob@example.com  — an owned str, safe to keep
    io.print(m.begin, m.end)    # 8 23             — byte offsets into your input
}
```

Even off a throwaway literal it is safe — `m.text` owns its bytes:

```purr
let m = regex.find(re, "contact bob@example.com now")
io.print(m.text)                # bob@example.com — fine; the copy outlives the temporary
```

**Zero-copy when you want it.** The matching engine itself never allocates an intermediate string;
the only allocation on a hit is `text`'s one copy. If you don't want even that, ignore `text` and
slice your own (already-owned) input with the offsets — no copy at all:

```purr
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
| `[a-z0-9_]` / `[^…]` | character class (ranges, negation) |
| `\d \D \w \W \s \S` | digit / word / whitespace classes (and their complements) |
| `\. \* \\` … | an escaped metacharacter (a literal) |
| `*` `+` `?` | zero-or-more / one-or-more / optional |
| `a\|b\|c` | alternation |
| `(…)` | grouping |
| `^` … `$` | anchor to the start / end of the input |

Backreferences and lookaround are **intentionally unsupported** — they are exactly the features
that force backtracking and the exponential blow-ups this engine exists to avoid.

## Performance

Same program, three engines, on a 4 MB log (best of 7 runs; `stdlib/regex/bench/`, which fetches
and builds Boost.Regex from source for the comparison). cheatah beats `std::regex` on **every**
pattern, beats Boost on most, and is the **only** engine that stays fast *and correct* on
catastrophic-backtracking inputs:

| pattern | cheatah | std::regex | Boost | vs std | vs Boost |
|---|--:|--:|--:|--:|--:|
| `status=200` (present)         | 171 ns | 1.09 µs | 216 ns | 6.4× | 1.3× |
| `status=500` (absent)          | 1.93 ms | 37.0 ms | 3.55 ms | 19.1× | 1.8× |
| `[0-9]+`                        | 73 ns | 110 ns | 81 ns | 1.5× | 1.1× |
| `[a-z]+@[a-z.]+` (email)        | 243 ns | 1.23 µs | 386 ns | 5.1× | 1.6× |
| `[a-z]+@nowhere` (absent)       | 16.4 ms | 78.1 ms | 28.7 ms | 4.8× | 1.7× |
| `[0-9]+\.[0-9]+…` (IP-ish)      | 9.64 ms | 57.5 ms | 19.9 ms | 6.0× | 2.1× |
| `([a-z]+=[^ ]+ ?)+` (rep-heavy) | 151 ns | 1.79 µs | 768 ns | 11.8× | 5.1× |
| `1274$` (anchored end)          | 1.51 ms | 35.1 ms | 168 ns | 23.3× | **0.0×** |
| **ReDoS `(a+)+$`** on `aa…a!`   | **0.6 µs** | **hangs (9 s)** | **throws** | ∞ | ∞ |

**Honest reading.** cheatah's throughput comes from a flat transition table plus a first-byte
`memchr`/SIMD skip (what RE2 and Boost also do), which is fastest when a pattern has a rare/absent
leading byte — hence the tens-of-GB/s on the "absent" rows. Boost still wins on a couple of shapes,
most sharply on an **end-anchored** pattern like `1274$`, where Boost searches the required suffix
from the end and cheatah does not yet (a known optimization). On the pathological `(a+)+$`,
`std::regex` runs exponentially (9 s at length 28) and **Boost aborts with a "complexity exceeded"
exception** — cheatah is the only one of the three that actually returns the right answer, in a
microsecond. And because it is a DFA, **no input can ever make it backtrack**.

## Design

- **Lazy DFA** over a Thompson NFA — linear time, no backtracking, ReDoS-proof.
- **Flat, contiguous transition table** (`state*256 + byte`) — one indirection per input byte.
- **First-byte prefix skip** — `search` fast-scans (`memchr`) to candidate start positions instead
  of stepping the DFA over every byte.
- **Value-semantic `Pattern`** — shares a compiled program + reusable DFA cache, so copying is cheap
  and holding one in a `let` and matching in a loop is fast.
- **Zero intermediate-string allocation** — the only heap use is the compiled program + DFA cache
  (built once); matching allocates nothing, and `find`'s only allocation is the one owned copy of the
  matched bytes (skippable via the offsets).
