# regex

A from-scratch, **linear-time** regular-expression engine for cheatah — `import regex`.

The pattern compiles to a Thompson NFA and runs as a **lazy DFA** (the RE2 approach), so a match
is **O(n) in the input length with no backtracking**. It cannot blow up on adversarial patterns
the way `std::regex` (which hangs) and backtracking engines like Boost.Regex (which *throws* rather
than hang) do. It is **statically typed** and **never allocates an intermediate string**: matching
touches only your input bytes (as a `string_view`) and integer program-counters. `find` returns the
matched bytes as an <b>owned `str`</b> (no borrow, nothing to dangle), plus offsets so you can slice your
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
<b>owned `str`</b>. The library copies the matched bytes into `text`, so it is always safe to keep, pass
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

Representative medians below; the full table follows, and **carries its own tally** — a count
restated in prose is a count that drifts the moment anyone re-measures, and this one already
had. Pinned P-core, medians over interleaved repetitions. Boost's single win is compiling a
64-byte pure literal: analysis Boost skips and match time repays.

Two 16 MB literal scans — `run_padded_literal_16M` and `sweep_16M` — sit close enough to the
1.15× threshold that they change verdict between runs: one measurement had them losing to RE2
at 1.18× and 1.27×, the next had them at parity. They are memcmp-bound rather than
automaton-bound, so the comparison is really between two prefilters doing almost identical
work. `RXBENCH_ASSERT=1` fails on any RE2 loss and will therefore go red on these
occasionally; the response to that is to read the margin, not to widen the threshold.

<!-- BENCH:regex-representative begin -->
<!-- cheatah-bench-stamp v1
     suite:        regex-representative
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     competitors:  std::regex, Boost.Regex, Google RE2
     statistic:    median real time per case; `vs RE2` = re2/cheatah
     harness:      medians of repeated runs, random-interleaved
     watch:        stdlib/regex/, stdlib/regex/bench/rxbench.cpp
     publishable:  true

     PRODUCED BY:
       RXBENCH_REP_TABLE=docs/bench/regex-representative.md \
           RXBENCH_ROWS='pat_literal_present=`status=200` on a 4 MB log;pat_digits=`[0-9]+` (search);pat_anchor_end=`1274$` (end-anchored, 4 MB);findall_digits=find-all `[0-9]+` (256 KB);hugescan=64 MB absent-pattern scan;compile_class_email=compile `[a-z]+@[a-z.]+`;redos2_alt2_N28=**ReDoS** `(a|aa)+$`, N=28;xl_redos_altstar_16M=**ReDoS at 16 MB** `(a|a)*c`' \
           ./build/regexbench/rxbench --benchmark_repetitions=7 \
           --benchmark_enable_random_interleaving=true
-->

| case | cheatah | std::regex | Boost | RE2 | vs RE2 |
|---|--:|--:|--:|--:|--:|
| `status=200` on a 4 MB log |      27.4 ns |     686.0 ns |      96.3 ns |      46.6 ns | 1.7× |
| `[0-9]+` (search) |       6.8 ns |      74.2 ns |      44.7 ns |      29.2 ns | 4.3× |
| `1274$` (end-anchored, 4 MB) |       5.9 ns |    35.913 ms |     107.1 ns |      30.0 ns | 5.1× |
| find-all `[0-9]+` (256 KB) |    419.80 us |     3.599 ms |     3.556 ms |     1.781 ms | 4.2× |
| 64 MB absent-pattern scan |     3.427 ms |   530.583 ms |    34.977 ms |     5.203 ms | 1.5× |
| compile `[a-z]+@[a-z.]+` |     230.6 ns |     22.87 us |     986.1 ns |      1.93 us | 8.4× |
| **ReDoS** `(a|aa)+$`, N=28 |       3.4 ns | — | — |      28.7 ns | 8.4× |
| **ReDoS at 16 MB** `(a|a)*c` |     3.863 ms | — | — |    19.462 ms | 5.0× |
<!-- BENCH:regex-representative end -->

<details>
<summary><b>The complete comparison — every benchmarked case, all four engines</b></summary>

<!-- BENCH:regex-vs-engines begin -->
<!-- cheatah-bench-stamp v1
     suite:        regex-vs-engines
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     competitors:  std::regex, Boost.Regex, Google RE2
     statistic:    median real time per case; ratio = rival/cheatah, >1 means cheatah is faster
     harness:      verdicts use the 1.15x + 0.25 ns band
     watch:        stdlib/regex/, stdlib/regex/bench/rxbench.cpp
     publishable:  true

     PRODUCED BY:
       RXBENCH_ASSERT=1 RXBENCH_TABLE=docs/bench/regex-vs-engines.md \
           ./build/regexbench/rxbench --benchmark_repetitions=7 \
           --benchmark_enable_random_interleaving=true \
           --benchmark_report_aggregates_only=true
-->

| case | cheatah | std::regex | boost | RE2 | vs std | vs boost | vs RE2 |
|---|--:|--:|--:|--:|--:|--:|--:|
| compile_alternation | 452.2 ns | 644.7 ns | 764.6 ns | 2.46 us | 1.43x ✅ | 1.69x ✅ | 5.45x ✅ |
| compile_class_email | 230.6 ns | 22.87 us | 986.1 ns | 1.93 us | 99.16x ✅ | 4.28x ✅ | 8.36x ✅ |
| compile_ip | 314.9 ns | 47.71 us | 1.51 us | 2.97 us | 151.52x ✅ | 4.80x ✅ | 9.42x ✅ |
| compile_literal | 296.1 ns | 440.6 ns | 325.6 ns | 1.74 us | 1.49x ✅ | 1.10x ⚪ | 5.88x ✅ |
| compile_repetition | 295.1 ns | 23.31 us | 1.56 us | 3.35 us | 79.00x ✅ | 5.28x ✅ | 11.35x ✅ |
| compilescale_alt50 | 9.19 us | 17.70 us | 12.24 us | 11.50 us | 1.93x ✅ | 1.33x ✅ | 1.25x ✅ |
| compilescale_literal64 | 1.39 us | 2.26 us | 603.6 ns | 6.51 us | 1.62x ✅ | 0.43x ❌ | 4.69x ✅ |
| compilescale_nest100 | 2.06 us | 5.78 us | 2.85 us | 21.87 us | 2.81x ✅ | 1.38x ✅ | 10.62x ✅ |
| find_anchor_end | 12.4 ns | 36.482 ms | 109.3 ns | 32.1 ns | 2949088.87x ✅ | 8.84x ✅ | 2.60x ✅ |
| find_digits | 13.2 ns | 75.2 ns | 50.4 ns | 58.9 ns | 5.70x ✅ | 3.82x ✅ | 4.47x ✅ |
| find_email | 110.5 ns | 900.3 ns | 136.6 ns | 135.5 ns | 8.15x ✅ | 1.24x ✅ | 1.23x ✅ |
| find_ip_absent | 4.502 ms | 59.071 ms | 9.910 ms | 4.610 ms | 13.12x ✅ | 2.20x ✅ | 1.02x ⚪ |
| findall_alternation | 184.63 us | 3.987 ms | 215.23 us | 463.48 us | 21.60x ✅ | 1.17x ✅ | 2.51x ✅ |
| findall_digits | 419.80 us | 3.599 ms | 3.556 ms | 1.781 ms | 8.57x ✅ | 8.47x ✅ | 4.24x ✅ |
| findall_key_value | 172.27 us | 2.472 ms | 276.65 us | 328.12 us | 14.35x ✅ | 1.61x ✅ | 1.90x ✅ |
| findall_word | 434.96 us | 3.429 ms | 2.820 ms | 1.754 ms | 7.88x ✅ | 6.48x ✅ | 4.03x ✅ |
| findlate_xmarker_4M | 55.99 us | 32.656 ms | 1.245 ms | 56.91 us | 583.20x ✅ | 22.23x ✅ | 1.02x ⚪ |
| full_digits_yes | 6.2 ns | 78.4 ns | 65.5 ns | 21.4 ns | 12.68x ✅ | 10.59x ✅ | 3.46x ✅ |
| full_email_no | 5.5 ns | 70.1 ns | 54.4 ns | 20.1 ns | 12.79x ✅ | 9.91x ✅ | 3.66x ✅ |
| full_email_yes | 15.2 ns | 144.4 ns | 121.0 ns | 32.6 ns | 9.48x ✅ | 7.94x ✅ | 2.14x ✅ |
| hugescan | 3.427 ms | 530.583 ms | 34.977 ms | 5.203 ms | 154.84x ✅ | 10.21x ✅ | 1.52x ✅ |
| pat_alt_absent | 2.464 ms | 61.758 ms | 2.205 ms | 4.613 ms | 25.06x ✅ | 0.89x ⚪ | 1.87x ✅ |
| pat_alternation | 18.3 ns | 387.1 ns | 53.6 ns | 46.3 ns | 21.17x ✅ | 2.93x ✅ | 2.53x ✅ |
| pat_anchor_end | 5.9 ns | 35.913 ms | 107.1 ns | 30.0 ns | 6120637.23x ✅ | 18.26x ✅ | 5.12x ✅ |
| pat_anchor_start | 6.5 ns | 61.9 ns | 46.0 ns | 30.6 ns | 9.50x ✅ | 7.06x ✅ | 4.69x ✅ |
| pat_class_quant | 16.1 ns | 241.2 ns | 52.8 ns | 42.5 ns | 15.01x ✅ | 3.29x ✅ | 2.65x ✅ |
| pat_digits | 6.8 ns | 74.2 ns | 44.7 ns | 29.2 ns | 10.98x ✅ | 6.61x ✅ | 4.33x ✅ |
| pat_dotstar | 97.0 ns | 778.5 ns | 103.7 ns | 107.6 ns | 8.03x ✅ | 1.07x ⚪ | 1.11x ⚪ |
| pat_email_absent | 4.811 ms | 79.822 ms | 30.973 ms | 4.622 ms | 16.59x ✅ | 6.44x ✅ | 0.96x ⚪ |
| pat_email | 58.5 ns | 901.8 ns | 331.1 ns | 90.4 ns | 15.41x ✅ | 5.66x ✅ | 1.54x ✅ |
| pat_escapes | 29.3 ns | 261.5 ns | 180.4 ns | 33.7 ns | 8.92x ✅ | 6.15x ✅ | 1.15x ✅ |
| pat_ip_absent | 4.441 ms | 59.151 ms | 22.742 ms | 4.766 ms | 13.32x ✅ | 5.12x ✅ | 1.07x ⚪ |
| pat_key_value | 20.2 ns | 547.8 ns | 78.5 ns | 37.2 ns | 27.15x ✅ | 3.89x ✅ | 1.84x ✅ |
| pat_literal_absent | 661.57 us | 36.927 ms | 4.101 ms | 938.17 us | 55.82x ✅ | 6.20x ✅ | 1.42x ✅ |
| pat_literal_present | 27.4 ns | 686.0 ns | 96.3 ns | 46.6 ns | 25.05x ✅ | 3.52x ✅ | 1.70x ✅ |
| pat_nested_groups | 27.6 ns | 657.0 ns | 105.9 ns | 74.3 ns | 23.81x ✅ | 3.84x ✅ | 2.69x ✅ |
| pat_prefix_class | 13.6 ns | 384.5 ns | 61.9 ns | 31.5 ns | 28.29x ✅ | 4.55x ✅ | 2.32x ✅ |
| pat_repetition | 37.9 ns | 1.18 us | 797.0 ns | 74.4 ns | 31.18x ✅ | 21.02x ✅ | 1.96x ✅ |
| pat_word | 16.0 ns | 252.5 ns | 302.4 ns | 42.8 ns | 15.75x ✅ | 18.86x ✅ | 2.67x ✅ |
| real_find_hex | 32.8 ns | 640.0 ns | 110.2 ns | 77.3 ns | 19.53x ✅ | 3.36x ✅ | 2.36x ✅ |
| real_findall_ts | 311.31 us | 3.118 ms | 630.82 us | 409.55 us | 10.02x ✅ | 2.03x ✅ | 1.32x ✅ |
| real_hex_absent | 92.54 us | 34.425 ms | 3.097 ms | 676.84 us | 371.99x ✅ | 33.47x ✅ | 7.31x ✅ |
| real_hex | 19.7 ns | 631.5 ns | 105.2 ns | 38.1 ns | 32.12x ✅ | 5.35x ✅ | 1.94x ✅ |
| real_keylist | 218.0 ns | 3.52 us | 3.44 us | 206.0 ns | 16.17x ✅ | 15.78x ✅ | 0.94x ⚪ |
| real_quoted | 21.6 ns | 117.8 ns | 60.2 ns | 38.1 ns | 5.45x ✅ | 2.79x ✅ | 1.76x ✅ |
| real_timestamp | 39.1 ns | 582.9 ns | 234.5 ns | 88.2 ns | 14.93x ✅ | 6.00x ✅ | 2.26x ✅ |
| real_uuid | 146.5 ns | 1.71 us | 645.7 ns | 151.7 ns | 11.65x ✅ | 4.41x ✅ | 1.04x ⚪ |
| redos2_alt2_N16 | 3.5 ns | — | — | 28.1 ns | — | — | 8.12x ✅ |
| redos2_alt2_N28 | 3.4 ns | — | — | 28.7 ns | — | — | 8.42x ✅ |
| redos2_dotstar3_4M | 1.568 ms | — | — | 4.637 ms | — | — | 2.96x ✅ |
| redos_altstar_N16 | 7.9 ns | — | — | 35.2 ns | — | — | 4.48x ✅ |
| redos_altstar_N20 | 8.8 ns | — | — | 40.9 ns | — | — | 4.65x ✅ |
| redos_altstar_N24 | 9.7 ns | — | — | 48.2 ns | — | — | 4.95x ✅ |
| redos_altstar_N28 | 10.4 ns | — | — | 50.3 ns | — | — | 4.81x ✅ |
| redos_nested_N16 | 3.4 ns | — | — | 28.5 ns | — | — | 8.41x ✅ |
| redos_nested_N20 | 3.4 ns | — | — | 29.0 ns | — | — | 8.65x ✅ |
| redos_nested_N24 | 3.4 ns | — | — | 28.2 ns | — | — | 8.30x ✅ |
| redos_nested_N28 | 3.5 ns | — | — | 28.6 ns | — | — | 8.25x ✅ |
| run_class_absent_16M | 565.70 us | — | — | 19.088 ms | — | — | 33.74x ✅ |
| run_class_present_16M | 654.84 us | — | — | 19.438 ms | — | — | 29.68x ✅ |
| run_padded_literal_16M | 315.82 us | 126.676 ms | 5.096 ms | 268.74 us | 401.10x ✅ | 16.13x ✅ | 0.85x ❌ |
| run_spaces_16M | 7.920 ms | 132.345 ms | 203.788 ms | 19.814 ms | 16.71x ✅ | 25.73x ✅ | 2.50x ✅ |
| run_tailclass_16M | 3.843 ms | — | — | 19.787 ms | — | — | 5.15x ✅ |
| shape_absent | 57.94 us | 32.225 ms | 1.210 ms | 58.06 us | 556.19x ✅ | 20.88x ✅ | 1.00x ⚪ |
| shape_end | 58.57 us | 31.984 ms | 1.276 ms | 56.96 us | 546.08x ✅ | 21.78x ✅ | 0.97x ⚪ |
| shape_everywhere | 20.1 ns | 476.9 ns | 78.6 ns | 37.8 ns | 23.76x ✅ | 3.92x ✅ | 1.88x ✅ |
| shape_start | 17.2 ns | 83.4 ns | 50.4 ns | 33.0 ns | 4.86x ✅ | 2.93x ✅ | 1.92x ✅ |
| sweep_16K | 115.0 ns | 131.82 us | 5.21 us | 137.3 ns | 1146.46x ✅ | 45.28x ✅ | 1.19x ✅ |
| sweep_16M | 366.64 us | 138.297 ms | 5.144 ms | 288.78 us | 377.21x ✅ | 14.03x ✅ | 0.79x ❌ |
| sweep_1K | 13.9 ns | 8.38 us | 362.3 ns | 33.7 ns | 603.44x ✅ | 26.08x ✅ | 2.42x ✅ |
| sweep_1M | 10.35 us | 8.340 ms | 327.49 us | 10.67 us | 805.61x ✅ | 31.64x ✅ | 1.03x ⚪ |
| sweep_256K | 2.31 us | 2.082 ms | 81.65 us | 2.31 us | 899.88x ✅ | 35.29x ✅ | 1.00x ⚪ |
| tiny_digit_hit | 6.9 ns | 58.6 ns | 44.7 ns | 28.1 ns | 8.55x ✅ | 6.53x ✅ | 4.10x ✅ |
| tiny_digit_miss | 6.4 ns | 52.6 ns | 51.6 ns | 29.0 ns | 8.22x ✅ | 8.06x ✅ | 4.53x ✅ |
| tiny_email_hit | 9.0 ns | 68.2 ns | 55.0 ns | 28.9 ns | 7.58x ✅ | 6.10x ✅ | 3.21x ✅ |
| tiny_email_miss | 13.4 ns | 82.2 ns | 62.3 ns | 28.9 ns | 6.14x ✅ | 4.65x ✅ | 2.16x ✅ |
| tiny_full_tok16 | 14.9 ns | 134.3 ns | 86.9 ns | 32.7 ns | 9.02x ✅ | 5.84x ✅ | 2.20x ✅ |
| xl_email_absent_16M | 20.967 ms | — | — | 19.522 ms | — | — | 0.93x ⚪ |
| xl_find_budget_16M | 659.10 us | 189.613 ms | 224.469 ms | 22.893 ms | 287.68x ✅ | 340.57x ✅ | 34.73x ✅ |
| xl_literal_storm_16M | 2.858 ms | — | — | 3.970 ms | — | — | 1.39x ✅ |
| xl_redos_altstar_16M | 3.863 ms | — | — | 19.462 ms | — | — | 5.04x ✅ |
| xl_redos_nested_16M | 3.4 ns | — | — | 27.9 ns | — | — | 8.12x ✅ |
| xl_reverse_alive_16M | 623.15 us | — | — | 19.551 ms | — | — | 31.37x ✅ |

**Tally** — vs std: **64 faster / 0 parity / 0 slower**; vs boost: **60 faster / 3 parity / 1 slower**; vs re2: **69 faster / 12 parity / 2 slower**.

Losses vs RE2:
- vs re2: run_padded_literal_16M — cheatah    315.82 us vs re2    268.74 us  (1.175x slower)
- vs re2: sweep_16M — cheatah    366.64 us vs re2    288.78 us  (1.269x slower)
<!-- BENCH:regex-vs-engines end -->

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
    --benchmark_enable_random_interleaving=true \
    --benchmark_report_aggregates_only=true     # exit 0 = no case slower than RE2
```

(`taskset` pins to one performance core on hybrid CPUs; `RXBENCH_ASSERT=1` makes the run fail
if cheatah is slower than RE2 anywhere; `RXBENCH_TABLE` writes the full Markdown table.)

`--benchmark_enable_random_interleaving` matters more here than the repetition count. All four
engines are registered per case, so without it cheatah's seven repetitions run as one block,
then `std::regex`'s, then Boost's, then RE2's — and any clock or thermal drift over that window
lands on whichever engine ran last rather than cancelling out of the ratio. Interleaved, the
repetitions of all four are scattered through the run, which is what makes a per-case
`rival / cheatah` figure a property of the engines rather than of the ordering.

## Design

- **Lazy DFA** over a Thompson NFA — linear time, no backtracking, ReDoS-proof.
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
  and skipped wholesale; and whenever *any* state maps a byte onto itself, the entire
  consecutive run of that byte is jumped with an 8-byte SWAR scan — long padding/whitespace
  runs cost almost nothing.
- <b>`find` candidate budget.</b> Leftmost-longest is decided per candidate start (with the same
  skip battery between candidates); on candidate-dense absent input, a budget triggers one
  O(n) existence pass instead of quadratic scanning.
- <b>Value-semantic `Pattern`</b> — shares a compiled program + reusable DFA cache (start states
  included), so copying is cheap and a warm pattern's search allocates nothing.
- **Zero intermediate-string allocation** — matching touches only the input bytes; `find`'s
  only allocation is the one owned copy of the matched bytes (skippable via the offsets).
