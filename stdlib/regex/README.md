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

Four engines — cheatah, `std::regex`, Boost.Regex and **Google RE2** — race the same programs
over identical inputs in the standalone benchmark project `stdlib/regex/bench/` (it fetches and
builds all three rivals from pinned sources; nothing is ever linked into cheatah itself). Every
timed case is **output-verified across all engines first** — the binary aborts if any engine
disagrees on any benchmarked input — and a differential suite (`rxdiff`) additionally checks
cheatah against RE2-as-oracle on thousands of generated inputs.

Representative medians below; the full table follows. Pinned P-core, medians of 7
repetitions. Across all 83 cases the tally is: vs RE2 **69 faster / 14 parity / 0 slower**;
vs `std::regex` 64 / 0 / 0; vs Boost 61 / 2 / 1 — Boost's win is compiling a 64-byte pure
literal, analysis it skips and match time repays:

| case | cheatah | std::regex | Boost | RE2 | vs RE2 |
|---|--:|--:|--:|--:|--:|
| `status=200` on a 4 MB log | 27.2 ns | 659.5 ns | 95.1 ns | 52.2 ns | 1.9× |
| `[0-9]+` (search) | 6.7 ns | 75.2 ns | 48.0 ns | 32.0 ns | 4.8× |
| `1274$` (end-anchored, 4 MB) | 6.3 ns | 36.4 ms | 110.5 ns | 34.2 ns | 5.4× |
| find-all `[0-9]+` (256 KB) | 464.6 µs | 3.48 ms | 3.35 ms | 1.87 ms | 4.0× |
| 64 MB absent-pattern scan | 3.49 ms | 537 ms | 35.6 ms | 5.46 ms | 1.6× |
| compile `[a-z]+@[a-z.]+` | 207.3 ns | 23.4 µs | 1.06 µs | 2.17 µs | 10.5× |
| **ReDoS** `(a+)+$`, N=28 | 3.4 ns | *hangs (seconds)* | *throws* | 29.7 ns | 8.8× |
| **ReDoS at 16 MB** `c[ab]*$` | 1.01 ms | *unrunnable* | *unrunnable* | 20.1 ms | 20.0× |

<details>
<summary><b>The complete comparison — every benchmarked case, all four engines</b></summary>

<!-- RXTABLE:FULL -->
<!-- generated by rxbench (stdlib/regex/bench) on 2026-08-13 — medians; ratio = rival/cheatah, >1 means cheatah is faster; verdicts use the 1.15x + 0.25 ns band -->

| case | cheatah | std::regex | boost | RE2 | vs std | vs boost | vs RE2 |
|---|--:|--:|--:|--:|--:|--:|--:|
| compile_alternation | 442.6 ns | 647.3 ns | 803.5 ns | 2.83 us | 1.46x ✅ | 1.82x ✅ | 6.39x ✅ |
| compile_class_email | 207.3 ns | 23.36 us | 1.06 us | 2.17 us | 112.64x ✅ | 5.11x ✅ | 10.47x ✅ |
| compile_ip | 300.5 ns | 48.99 us | 1.59 us | 3.28 us | 162.99x ✅ | 5.31x ✅ | 10.91x ✅ |
| compile_literal | 280.6 ns | 433.7 ns | 333.0 ns | 2.01 us | 1.55x ✅ | 1.19x ✅ | 7.17x ✅ |
| compile_repetition | 282.4 ns | 24.13 us | 1.67 us | 3.87 us | 85.48x ✅ | 5.92x ✅ | 13.70x ✅ |
| compilescale_alt50 | 8.93 us | 17.48 us | 12.89 us | 13.31 us | 1.96x ✅ | 1.44x ✅ | 1.49x ✅ |
| compilescale_literal64 | 1.40 us | 2.56 us | 623.2 ns | 7.21 us | 1.84x ✅ | 0.45x ❌ | 5.16x ✅ |
| compilescale_nest100 | 3.18 us | 8.55 us | 4.30 us | 23.27 us | 2.69x ✅ | 1.35x ✅ | 7.32x ✅ |
| find_anchor_end | 11.4 ns | 36.696 ms | 113.2 ns | 35.6 ns | 3221572.83x ✅ | 9.94x ✅ | 3.12x ✅ |
| find_digits | 14.2 ns | 76.4 ns | 50.9 ns | 62.4 ns | 5.40x ✅ | 3.59x ✅ | 4.41x ✅ |
| find_email | 116.7 ns | 1.06 us | 148.0 ns | 132.6 ns | 9.05x ✅ | 1.27x ✅ | 1.14x ⚪ |
| find_ip_absent | 4.530 ms | 62.382 ms | 9.881 ms | 4.840 ms | 13.77x ✅ | 2.18x ✅ | 1.07x ⚪ |
| findall_alternation | 176.46 us | 4.384 ms | 222.34 us | 492.05 us | 24.84x ✅ | 1.26x ✅ | 2.79x ✅ |
| findall_digits | 464.62 us | 3.483 ms | 3.352 ms | 1.865 ms | 7.50x ✅ | 7.21x ✅ | 4.01x ✅ |
| findall_key_value | 212.66 us | 2.596 ms | 288.90 us | 348.39 us | 12.21x ✅ | 1.36x ✅ | 1.64x ✅ |
| findall_word | 468.15 us | 3.587 ms | 2.854 ms | 1.852 ms | 7.66x ✅ | 6.10x ✅ | 3.95x ✅ |
| findlate_xmarker_4M | 57.17 us | 35.242 ms | 1.299 ms | 60.73 us | 616.40x ✅ | 22.72x ✅ | 1.06x ⚪ |
| full_digits_yes | 6.1 ns | 80.4 ns | 77.9 ns | 23.4 ns | 13.25x ✅ | 12.85x ✅ | 3.86x ✅ |
| full_email_no | 5.3 ns | 69.1 ns | 57.2 ns | 22.9 ns | 13.02x ✅ | 10.79x ✅ | 4.32x ✅ |
| full_email_yes | 14.6 ns | 227.5 ns | 139.9 ns | 35.3 ns | 15.62x ✅ | 9.61x ✅ | 2.42x ✅ |
| hugescan | 3.492 ms | 537.043 ms | 35.556 ms | 5.456 ms | 153.79x ✅ | 10.18x ✅ | 1.56x ✅ |
| pat_alt_absent | 2.475 ms | 65.625 ms | 2.372 ms | 4.731 ms | 26.51x ✅ | 0.96x ⚪ | 1.91x ✅ |
| pat_alternation | 19.3 ns | 423.4 ns | 54.4 ns | 58.4 ns | 21.99x ✅ | 2.83x ✅ | 3.04x ✅ |
| pat_anchor_end | 6.3 ns | 36.437 ms | 110.5 ns | 34.2 ns | 5795637.95x ✅ | 17.57x ✅ | 5.43x ✅ |
| pat_anchor_start | 6.5 ns | 60.7 ns | 46.7 ns | 33.6 ns | 9.28x ✅ | 7.15x ✅ | 5.13x ✅ |
| pat_class_quant | 16.6 ns | 234.2 ns | 55.4 ns | 56.0 ns | 14.14x ✅ | 3.35x ✅ | 3.38x ✅ |
| pat_digits | 6.7 ns | 75.2 ns | 48.0 ns | 32.0 ns | 11.20x ✅ | 7.15x ✅ | 4.77x ✅ |
| pat_dotstar | 94.9 ns | 1.37 us | 97.6 ns | 114.2 ns | 14.40x ✅ | 1.03x ⚪ | 1.20x ✅ |
| pat_email_absent | 4.881 ms | 85.309 ms | 31.591 ms | 4.742 ms | 17.48x ✅ | 6.47x ✅ | 0.97x ⚪ |
| pat_email | 60.7 ns | 1.03 us | 344.8 ns | 96.0 ns | 17.01x ✅ | 5.68x ✅ | 1.58x ✅ |
| pat_escapes | 29.3 ns | 267.6 ns | 180.3 ns | 37.1 ns | 9.12x ✅ | 6.15x ✅ | 1.27x ✅ |
| pat_ip_absent | 4.505 ms | 61.140 ms | 22.635 ms | 4.746 ms | 13.57x ✅ | 5.02x ✅ | 1.05x ⚪ |
| pat_key_value | 18.8 ns | 667.8 ns | 79.5 ns | 42.1 ns | 35.60x ✅ | 4.24x ✅ | 2.24x ✅ |
| pat_literal_absent | 755.66 us | 37.614 ms | 4.103 ms | 1.099 ms | 49.78x ✅ | 5.43x ✅ | 1.45x ✅ |
| pat_literal_present | 27.2 ns | 659.5 ns | 95.1 ns | 52.2 ns | 24.28x ✅ | 3.50x ✅ | 1.92x ✅ |
| pat_nested_groups | 32.8 ns | 732.4 ns | 108.0 ns | 78.7 ns | 22.36x ✅ | 3.30x ✅ | 2.40x ✅ |
| pat_prefix_class | 13.1 ns | 389.9 ns | 61.2 ns | 35.8 ns | 29.86x ✅ | 4.68x ✅ | 2.75x ✅ |
| pat_repetition | 38.9 ns | 1.84 us | 826.1 ns | 77.0 ns | 47.30x ✅ | 21.24x ✅ | 1.98x ✅ |
| pat_word | 16.3 ns | 251.5 ns | 285.6 ns | 57.9 ns | 15.46x ✅ | 17.56x ✅ | 3.56x ✅ |
| real_find_hex | 31.7 ns | 721.6 ns | 117.6 ns | 81.7 ns | 22.79x ✅ | 3.72x ✅ | 2.58x ✅ |
| real_findall_ts | 346.97 us | 3.532 ms | 650.55 us | 430.03 us | 10.18x ✅ | 1.87x ✅ | 1.24x ✅ |
| real_hex_absent | 95.99 us | 37.594 ms | 3.305 ms | 708.43 us | 391.66x ✅ | 34.44x ✅ | 7.38x ✅ |
| real_hex | 19.2 ns | 718.7 ns | 110.4 ns | 40.8 ns | 37.36x ✅ | 5.74x ✅ | 2.12x ✅ |
| real_keylist | 215.2 ns | 4.33 us | 3.69 us | 211.6 ns | 20.12x ✅ | 17.12x ✅ | 0.98x ⚪ |
| real_quoted | 19.9 ns | 155.0 ns | 63.9 ns | 41.8 ns | 7.79x ✅ | 3.21x ✅ | 2.10x ✅ |
| real_timestamp | 44.2 ns | 656.6 ns | 235.9 ns | 84.7 ns | 14.84x ✅ | 5.33x ✅ | 1.92x ✅ |
| real_uuid | 163.0 ns | 1.91 us | 669.2 ns | 159.6 ns | 11.70x ✅ | 4.11x ✅ | 0.98x ⚪ |
| redos2_alt2_N16 | 3.4 ns | — | — | 29.9 ns | — | — | 8.80x ✅ |
| redos2_alt2_N28 | 3.4 ns | — | — | 29.6 ns | — | — | 8.72x ✅ |
| redos2_dotstar3_4M | 1.563 ms | — | — | 4.688 ms | — | — | 3.00x ✅ |
| redos_altstar_N16 | 7.7 ns | — | — | 39.6 ns | — | — | 5.16x ✅ |
| redos_altstar_N20 | 8.4 ns | — | — | 46.7 ns | — | — | 5.55x ✅ |
| redos_altstar_N24 | 9.5 ns | — | — | 50.6 ns | — | — | 5.32x ✅ |
| redos_altstar_N28 | 10.6 ns | — | — | 54.0 ns | — | — | 5.09x ✅ |
| redos_nested_N16 | 3.3 ns | — | — | 29.9 ns | — | — | 9.02x ✅ |
| redos_nested_N20 | 3.4 ns | — | — | 30.2 ns | — | — | 8.92x ✅ |
| redos_nested_N24 | 3.5 ns | — | — | 29.8 ns | — | — | 8.57x ✅ |
| redos_nested_N28 | 3.4 ns | — | — | 29.7 ns | — | — | 8.79x ✅ |
| run_class_absent_16M | 714.88 us | — | — | 20.544 ms | — | — | 28.74x ✅ |
| run_class_present_16M | 728.88 us | — | — | 20.951 ms | — | — | 28.74x ✅ |
| run_padded_literal_16M | 325.91 us | 138.601 ms | 5.117 ms | 331.01 us | 425.27x ✅ | 15.70x ✅ | 1.02x ⚪ |
| run_spaces_16M | 7.719 ms | 146.403 ms | 201.096 ms | 21.006 ms | 18.97x ✅ | 26.05x ✅ | 2.72x ✅ |
| run_tailclass_16M | 3.910 ms | — | — | 20.334 ms | — | — | 5.20x ✅ |
| shape_absent | 55.26 us | 32.677 ms | 1.268 ms | 59.71 us | 591.35x ✅ | 22.94x ✅ | 1.08x ⚪ |
| shape_end | 57.07 us | 32.486 ms | 1.278 ms | 59.03 us | 569.21x ✅ | 22.40x ✅ | 1.03x ⚪ |
| shape_everywhere | 18.6 ns | 471.8 ns | 78.1 ns | 40.8 ns | 25.43x ✅ | 4.21x ✅ | 2.20x ✅ |
| shape_start | 16.5 ns | 86.3 ns | 51.1 ns | 37.2 ns | 5.23x ✅ | 3.10x ✅ | 2.25x ✅ |
| sweep_16K | 117.1 ns | 133.44 us | 5.45 us | 142.9 ns | 1139.82x ✅ | 46.54x ✅ | 1.22x ✅ |
| sweep_16M | 349.67 us | 136.144 ms | 5.507 ms | 384.21 us | 389.35x ✅ | 15.75x ✅ | 1.10x ⚪ |
| sweep_1K | 13.5 ns | 8.43 us | 377.2 ns | 36.4 ns | 626.01x ✅ | 28.00x ✅ | 2.70x ✅ |
| sweep_1M | 10.27 us | 8.487 ms | 338.89 us | 10.89 us | 826.44x ✅ | 33.00x ✅ | 1.06x ⚪ |
| sweep_256K | 2.38 us | 2.106 ms | 83.82 us | 2.44 us | 885.13x ✅ | 35.22x ✅ | 1.03x ⚪ |
| tiny_digit_hit | 6.8 ns | 61.8 ns | 49.3 ns | 30.1 ns | 9.07x ✅ | 7.25x ✅ | 4.42x ✅ |
| tiny_digit_miss | 6.4 ns | 55.6 ns | 54.2 ns | 29.9 ns | 8.67x ✅ | 8.46x ✅ | 4.67x ✅ |
| tiny_email_hit | 8.8 ns | 74.1 ns | 56.4 ns | 30.5 ns | 8.39x ✅ | 6.38x ✅ | 3.45x ✅ |
| tiny_email_miss | 12.7 ns | 91.5 ns | 67.0 ns | 30.3 ns | 7.20x ✅ | 5.28x ✅ | 2.39x ✅ |
| tiny_full_tok16 | 14.9 ns | 201.6 ns | 98.5 ns | 34.7 ns | 13.50x ✅ | 6.59x ✅ | 2.32x ✅ |
| xl_email_absent_16M | 21.139 ms | — | — | 20.132 ms | — | — | 0.95x ⚪ |
| xl_find_budget_16M | 734.46 us | 205.415 ms | 232.186 ms | 24.132 ms | 279.68x ✅ | 316.13x ✅ | 32.86x ✅ |
| xl_literal_storm_16M | 3.317 ms | — | — | 4.464 ms | — | — | 1.35x ✅ |
| xl_redos_altstar_16M | 3.952 ms | — | — | 19.950 ms | — | — | 5.05x ✅ |
| xl_redos_nested_16M | 3.3 ns | — | — | 29.7 ns | — | — | 8.93x ✅ |
| xl_reverse_alive_16M | 1.007 ms | — | — | 20.132 ms | — | — | 20.00x ✅ |

**Tally** — vs std: **64 faster / 0 parity / 0 slower**; vs boost: **61 faster / 2 parity / 1 slower**; vs re2: **69 faster / 14 parity / 0 slower**.

cheatah ties or beats RE2 on **every** case.

</details>

**Honest reading.** RE2 is timed in its out-of-box configuration (UTF-8, leftmost-first) — the
configuration real RE2 users get; offsets are cross-checked against RE2's longest-match +
Latin-1 mode, which matches cheatah's documented leftmost-longest byte semantics. Rows where
both engines sit at memory bandwidth (large absent scans that reduce to `memchr`) cannot
separate meaningfully and score as parity — that is physics, not engine quality. On the
pathological inputs, `std::regex` runs exponentially (seconds at N=28; unrunnable at 16 MB —
those rows exclude it) and **Boost aborts with a "complexity exceeded" exception**; cheatah and
RE2 both stay linear, and cheatah answers faster. Because it is a DFA, **no input can ever make
cheatah backtrack**.

Reproduce:

```sh
cmake -S stdlib/regex/bench -B build/regexbench -DCMAKE_BUILD_TYPE=Release
cmake --build build/regexbench -j
./build/regexbench/rxdiff                       # differential tests vs RE2-as-oracle
taskset -c 4 env RXBENCH_ASSERT=1 RXBENCH_TABLE=/tmp/rxtable.md \
    ./build/regexbench/rxbench --benchmark_repetitions=7 \
    --benchmark_report_aggregates_only=true     # exit 0 = no case slower than RE2
```

(`taskset` pins to one performance core on hybrid CPUs; `RXBENCH_ASSERT=1` makes the run fail
if cheatah is slower than RE2 anywhere; `RXBENCH_TABLE` writes the full Markdown table.)

## Design

- **Lazy DFA** over a Thompson NFA — linear time, no backtracking, ReDoS-proof.
- **One load per byte.** Transition entries store the next state's *row byte-offset* directly,
  so the hot loop's carried dependency is a single `[table + state + byte*4]` load; the accept
  flag lives in a 257th slot of each row and is tested off the critical path.
- **Single-pass unanchored search.** The compiled-in `.*?` prefix keeps every start position
  alive in one DFA state, so an absent pattern costs exactly one visit per byte — no
  per-candidate restarts, O(n) always.
- **Reversed program for `$`.** A pattern anchored only at the end runs *backward* from the
  end of the input: one pass answers existence and yields the leftmost begin, and a wrong tail
  dies in a handful of bytes.
- **Acceleration armed in the start state.** While no partial match is alive, the scan jumps:
  `memchr` for a single required first byte; for a required literal chain, `memchr`+verify that
  flips to a 32-wide branchless front+back block compare under a false-positive storm; a
  first-set LUT otherwise.
- **Self-loop skipping.** Bytes the start state maps back onto itself are learned into a LUT
  and skipped wholesale; and whenever *any* state maps a byte onto itself, the entire
  consecutive run of that byte is jumped with an 8-byte SWAR scan — long padding/whitespace
  runs cost almost nothing.
- **`find` candidate budget.** Leftmost-longest is decided per candidate start (with the same
  skip battery between candidates); on candidate-dense absent input, a budget triggers one
  O(n) existence pass instead of quadratic scanning.
- **Value-semantic `Pattern`** — shares a compiled program + reusable DFA cache (start states
  included), so copying is cheap and a warm pattern's search allocates nothing.
- **Zero intermediate-string allocation** — matching touches only the input bytes; `find`'s
  only allocation is the one owned copy of the matched bytes (skippable via the offsets).
