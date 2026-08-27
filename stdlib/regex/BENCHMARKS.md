# regex benchmarks

The generated benchmark tables for the [`regex`](README.md) module — a from-scratch,
linear-time lazy DFA — against `std::regex`, Boost.Regex and **Google RE2**. How the race is
set up (the standalone `stdlib/regex/bench/` project, output verification across all four
engines before anything is timed, the `rxdiff` differential suite) is on the module page;
this page holds the numbers, their stamps, and the commands that reproduce them.

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

## Representative cases

Measured by [`stdlib/regex/bench/rxbench.cpp`](../../stdlib/regex/bench/rxbench.cpp); reproduce with `RXBENCH_REP_TABLE=docs/bench/regex-representative.md ./build/regexbench/rxbench`.

<!-- BENCH:regex-representative begin -->
<!-- cheatah-bench-stamp v1
     suite:        regex-representative
     generated:    2026-08-20
     commit:       2b3a0b8
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
| `status=200` on a 4 MB log |      26.5 ns |     649.9 ns |      95.0 ns |      46.6 ns | 1.8× |
| `[0-9]+` (search) |       6.8 ns |      74.1 ns |      44.5 ns |      28.7 ns | 4.3× |
| `1274$` (end-anchored, 4 MB) |       6.0 ns |    35.713 ms |     107.9 ns |      30.0 ns | 5.0× |
| find-all `[0-9]+` (256 KB) |    428.83 us |     3.512 ms |     3.304 ms |     1.748 ms | 4.1× |
| 64 MB absent-pattern scan |     3.391 ms |   526.047 ms |    35.253 ms |     5.144 ms | 1.5× |
| compile `[a-z]+@[a-z.]+` |     218.8 ns |     22.30 us |      1.02 us |      1.96 us | 9.0× |
| **ReDoS** `(a|aa)+$`, N=28 |       3.4 ns | — | — |      28.1 ns | 8.2× |
| **ReDoS at 16 MB** `(a|a)*c` |     3.927 ms | — | — |    19.393 ms | 4.9× |
<!-- BENCH:regex-representative end -->

## The complete comparison — every benchmarked case, all four engines

Measured by [`stdlib/regex/bench/rxbench.cpp`](../../stdlib/regex/bench/rxbench.cpp); reproduce with `RXBENCH_TABLE=docs/bench/regex-vs-engines.md ./build/regexbench/rxbench`.

<!-- BENCH:regex-vs-engines begin -->
<!-- cheatah-bench-stamp v1
     suite:        regex-vs-engines
     generated:    2026-08-20
     commit:       2b3a0b8
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
| compile_alternation | 448.5 ns | 645.2 ns | 772.3 ns | 2.46 us | 1.44x ✅ | 1.72x ✅ | 5.48x ✅ |
| compile_class_email | 218.8 ns | 22.30 us | 1.02 us | 1.96 us | 101.96x ✅ | 4.65x ✅ | 8.97x ✅ |
| compile_ip | 308.7 ns | 47.35 us | 1.47 us | 2.88 us | 153.39x ✅ | 4.77x ✅ | 9.34x ✅ |
| compile_literal | 288.6 ns | 438.4 ns | 330.0 ns | 1.74 us | 1.52x ✅ | 1.14x ⚪ | 6.02x ✅ |
| compile_repetition | 289.9 ns | 23.54 us | 1.56 us | 3.42 us | 81.20x ✅ | 5.37x ✅ | 11.79x ✅ |
| compilescale_alt50 | 9.18 us | 17.77 us | 12.15 us | 11.45 us | 1.94x ✅ | 1.32x ✅ | 1.25x ✅ |
| compilescale_literal64 | 1.40 us | 2.61 us | 598.1 ns | 6.50 us | 1.87x ✅ | 0.43x ❌ | 4.65x ✅ |
| compilescale_nest100 | 3.26 us | 8.41 us | 4.18 us | 20.59 us | 2.58x ✅ | 1.28x ✅ | 6.32x ✅ |
| find_anchor_end | 12.2 ns | 35.799 ms | 109.6 ns | 33.3 ns | 2944495.28x ✅ | 9.02x ✅ | 2.74x ✅ |
| find_digits | 12.9 ns | 74.3 ns | 49.5 ns | 65.8 ns | 5.74x ✅ | 3.82x ✅ | 5.09x ✅ |
| find_email | 108.9 ns | 997.0 ns | 140.4 ns | 132.9 ns | 9.16x ✅ | 1.29x ✅ | 1.22x ✅ |
| find_ip_absent | 4.457 ms | 58.101 ms | 9.978 ms | 4.601 ms | 13.04x ✅ | 2.24x ✅ | 1.03x ⚪ |
| findall_alternation | 193.18 us | 3.826 ms | 212.66 us | 462.62 us | 19.81x ✅ | 1.10x ⚪ | 2.39x ✅ |
| findall_digits | 428.83 us | 3.512 ms | 3.304 ms | 1.748 ms | 8.19x ✅ | 7.71x ✅ | 4.08x ✅ |
| findall_key_value | 172.80 us | 2.613 ms | 269.43 us | 332.84 us | 15.12x ✅ | 1.56x ✅ | 1.93x ✅ |
| findall_word | 433.75 us | 3.569 ms | 2.799 ms | 1.721 ms | 8.23x ✅ | 6.45x ✅ | 3.97x ✅ |
| findlate_xmarker_4M | 56.19 us | 31.797 ms | 1.198 ms | 56.72 us | 565.85x ✅ | 21.31x ✅ | 1.01x ⚪ |
| full_digits_yes | 6.1 ns | 79.2 ns | 66.1 ns | 21.1 ns | 12.97x ✅ | 10.82x ✅ | 3.46x ✅ |
| full_email_no | 5.5 ns | 69.8 ns | 52.2 ns | 20.1 ns | 12.73x ✅ | 9.53x ✅ | 3.66x ✅ |
| full_email_yes | 15.2 ns | 231.3 ns | 118.7 ns | 32.0 ns | 15.25x ✅ | 7.82x ✅ | 2.11x ✅ |
| hugescan | 3.391 ms | 526.047 ms | 35.253 ms | 5.144 ms | 155.15x ✅ | 10.40x ✅ | 1.52x ✅ |
| pat_alt_absent | 2.398 ms | 61.334 ms | 2.255 ms | 4.554 ms | 25.57x ✅ | 0.94x ⚪ | 1.90x ✅ |
| pat_alternation | 17.9 ns | 377.2 ns | 52.7 ns | 45.2 ns | 21.10x ✅ | 2.95x ✅ | 2.53x ✅ |
| pat_anchor_end | 6.0 ns | 35.713 ms | 107.9 ns | 30.0 ns | 5957941.81x ✅ | 18.01x ✅ | 5.00x ✅ |
| pat_anchor_start | 6.5 ns | 60.6 ns | 45.9 ns | 29.4 ns | 9.26x ✅ | 7.01x ✅ | 4.49x ✅ |
| pat_class_quant | 16.0 ns | 231.9 ns | 52.1 ns | 42.3 ns | 14.53x ✅ | 3.26x ✅ | 2.65x ✅ |
| pat_digits | 6.8 ns | 74.1 ns | 44.5 ns | 28.7 ns | 10.96x ✅ | 6.58x ✅ | 4.25x ✅ |
| pat_dotstar | 94.2 ns | 1.40 us | 100.9 ns | 105.9 ns | 14.87x ✅ | 1.07x ⚪ | 1.12x ⚪ |
| pat_email_absent | 5.019 ms | 79.415 ms | 31.412 ms | 4.545 ms | 15.82x ✅ | 6.26x ✅ | 0.91x ⚪ |
| pat_email | 59.8 ns | 987.7 ns | 321.3 ns | 89.0 ns | 16.52x ✅ | 5.38x ✅ | 1.49x ✅ |
| pat_escapes | 29.4 ns | 254.4 ns | 174.6 ns | 33.3 ns | 8.64x ✅ | 5.93x ✅ | 1.13x ⚪ |
| pat_ip_absent | 4.344 ms | 58.363 ms | 22.530 ms | 4.602 ms | 13.43x ✅ | 5.19x ✅ | 1.06x ⚪ |
| pat_key_value | 20.0 ns | 657.8 ns | 77.2 ns | 37.0 ns | 32.87x ✅ | 3.86x ✅ | 1.85x ✅ |
| pat_literal_absent | 660.41 us | 36.971 ms | 4.085 ms | 926.98 us | 55.98x ✅ | 6.19x ✅ | 1.40x ✅ |
| pat_literal_present | 26.5 ns | 649.9 ns | 95.0 ns | 46.6 ns | 24.56x ✅ | 3.59x ✅ | 1.76x ✅ |
| pat_nested_groups | 27.4 ns | 683.8 ns | 106.3 ns | 74.2 ns | 24.96x ✅ | 3.88x ✅ | 2.71x ✅ |
| pat_prefix_class | 13.5 ns | 388.7 ns | 59.0 ns | 31.0 ns | 28.79x ✅ | 4.37x ✅ | 2.30x ✅ |
| pat_repetition | 37.7 ns | 1.83 us | 775.1 ns | 73.6 ns | 48.56x ✅ | 20.55x ✅ | 1.95x ✅ |
| pat_word | 16.6 ns | 246.0 ns | 298.9 ns | 42.9 ns | 14.80x ✅ | 17.98x ✅ | 2.58x ✅ |
| real_find_hex | 31.1 ns | 661.8 ns | 111.2 ns | 76.5 ns | 21.30x ✅ | 3.58x ✅ | 2.46x ✅ |
| real_findall_ts | 306.47 us | 3.153 ms | 618.55 us | 411.05 us | 10.29x ✅ | 2.02x ✅ | 1.34x ✅ |
| real_hex_absent | 92.24 us | 33.788 ms | 3.148 ms | 672.72 us | 366.30x ✅ | 34.12x ✅ | 7.29x ✅ |
| real_hex | 18.9 ns | 666.9 ns | 103.6 ns | 37.3 ns | 35.31x ✅ | 5.48x ✅ | 1.97x ✅ |
| real_keylist | 212.2 ns | 4.03 us | 3.44 us | 204.9 ns | 18.98x ✅ | 16.23x ✅ | 0.97x ⚪ |
| real_quoted | 23.8 ns | 145.5 ns | 59.5 ns | 36.2 ns | 6.12x ✅ | 2.50x ✅ | 1.52x ✅ |
| real_timestamp | 38.9 ns | 589.7 ns | 240.0 ns | 85.5 ns | 15.15x ✅ | 6.16x ✅ | 2.20x ✅ |
| real_uuid | 151.3 ns | 1.75 us | 638.3 ns | 151.6 ns | 11.58x ✅ | 4.22x ✅ | 1.00x ⚪ |
| redos2_alt2_N16 | 3.4 ns | — | — | 27.9 ns | — | — | 8.22x ✅ |
| redos2_alt2_N28 | 3.4 ns | — | — | 28.1 ns | — | — | 8.19x ✅ |
| redos2_dotstar3_4M | 1.542 ms | — | — | 4.551 ms | — | — | 2.95x ✅ |
| redos_altstar_N16 | 7.8 ns | — | — | 35.4 ns | — | — | 4.54x ✅ |
| redos_altstar_N20 | 8.7 ns | — | — | 41.2 ns | — | — | 4.71x ✅ |
| redos_altstar_N24 | 9.5 ns | — | — | 45.7 ns | — | — | 4.83x ✅ |
| redos_altstar_N28 | 10.5 ns | — | — | 50.3 ns | — | — | 4.78x ✅ |
| redos_nested_N16 | 3.4 ns | — | — | 27.8 ns | — | — | 8.11x ✅ |
| redos_nested_N20 | 3.4 ns | — | — | 28.0 ns | — | — | 8.20x ✅ |
| redos_nested_N24 | 3.4 ns | — | — | 28.0 ns | — | — | 8.22x ✅ |
| redos_nested_N28 | 3.4 ns | — | — | 28.0 ns | — | — | 8.32x ✅ |
| run_class_absent_16M | 647.76 us | — | — | 19.510 ms | — | — | 30.12x ✅ |
| run_class_present_16M | 633.67 us | — | — | 19.298 ms | — | — | 30.45x ✅ |
| run_padded_literal_16M | 281.32 us | 125.707 ms | 4.846 ms | 281.31 us | 446.85x ✅ | 17.23x ✅ | 1.00x ⚪ |
| run_spaces_16M | 7.762 ms | 133.966 ms | 198.959 ms | 19.435 ms | 17.26x ✅ | 25.63x ✅ | 2.50x ✅ |
| run_tailclass_16M | 3.873 ms | — | — | 19.305 ms | — | — | 4.98x ✅ |
| shape_absent | 56.53 us | 32.284 ms | 1.258 ms | 56.86 us | 571.05x ✅ | 22.25x ✅ | 1.01x ⚪ |
| shape_end | 56.52 us | 32.772 ms | 1.213 ms | 56.54 us | 579.85x ✅ | 21.47x ✅ | 1.00x ⚪ |
| shape_everywhere | 19.8 ns | 468.1 ns | 77.9 ns | 36.9 ns | 23.64x ✅ | 3.93x ✅ | 1.86x ✅ |
| shape_start | 16.6 ns | 83.2 ns | 51.5 ns | 32.5 ns | 5.01x ✅ | 3.10x ✅ | 1.95x ✅ |
| sweep_16K | 116.6 ns | 130.88 us | 5.16 us | 136.5 ns | 1122.14x ✅ | 44.27x ✅ | 1.17x ✅ |
| sweep_16M | 300.90 us | 133.809 ms | 5.212 ms | 301.48 us | 444.69x ✅ | 17.32x ✅ | 1.00x ⚪ |
| sweep_1K | 13.7 ns | 8.27 us | 357.0 ns | 32.6 ns | 605.26x ✅ | 26.13x ✅ | 2.38x ✅ |
| sweep_1M | 10.35 us | 8.369 ms | 317.06 us | 10.42 us | 808.69x ✅ | 30.64x ✅ | 1.01x ⚪ |
| sweep_256K | 2.32 us | 2.125 ms | 79.58 us | 2.32 us | 916.55x ✅ | 34.32x ✅ | 1.00x ⚪ |
| tiny_digit_hit | 6.7 ns | 57.1 ns | 44.4 ns | 28.8 ns | 8.48x ✅ | 6.60x ✅ | 4.27x ✅ |
| tiny_digit_miss | 6.3 ns | 53.8 ns | 51.8 ns | 27.8 ns | 8.58x ✅ | 8.25x ✅ | 4.43x ✅ |
| tiny_email_hit | 9.1 ns | 66.3 ns | 54.5 ns | 28.7 ns | 7.27x ✅ | 5.98x ✅ | 3.15x ✅ |
| tiny_email_miss | 13.2 ns | 81.2 ns | 62.2 ns | 28.4 ns | 6.16x ✅ | 4.72x ✅ | 2.15x ✅ |
| tiny_full_tok16 | 14.7 ns | 189.6 ns | 84.6 ns | 32.3 ns | 12.92x ✅ | 5.76x ✅ | 2.20x ✅ |
| xl_email_absent_16M | 20.989 ms | — | — | 19.460 ms | — | — | 0.93x ⚪ |
| xl_find_budget_16M | 661.76 us | 187.178 ms | 224.054 ms | 23.313 ms | 282.85x ✅ | 338.57x ✅ | 35.23x ✅ |
| xl_literal_storm_16M | 2.846 ms | — | — | 3.959 ms | — | — | 1.39x ✅ |
| xl_redos_altstar_16M | 3.927 ms | — | — | 19.393 ms | — | — | 4.94x ✅ |
| xl_redos_nested_16M | 3.5 ns | — | — | 27.8 ns | — | — | 8.02x ✅ |
| xl_reverse_alive_16M | 651.06 us | — | — | 19.429 ms | — | — | 29.84x ✅ |

**Tally** — vs std: **64 faster / 0 parity / 0 slower**; vs boost: **59 faster / 4 parity / 1 slower**; vs re2: **68 faster / 15 parity / 0 slower**.

cheatah ties or beats RE2 on **every** case.
<!-- BENCH:regex-vs-engines end -->

## Honest reading

RE2 is timed in its out-of-box configuration (UTF-8, leftmost-first) — the
configuration real RE2 users get; offsets are cross-checked against RE2's longest-match +
Latin-1 mode, which matches cheatah's documented leftmost-longest byte semantics. Rows where
both engines sit at memory bandwidth (large absent scans that reduce to `memchr`) cannot
separate meaningfully and score as parity — that is physics, not engine quality. On the
pathological inputs, `std::regex` runs exponentially (seconds at N=28; unrunnable at 16 MB —
those rows exclude it) and **Boost aborts with a "complexity exceeded" exception**; cheatah and
RE2 both stay linear, and cheatah answers faster. Because it is a DFA, **no input can ever make
cheatah backtrack**.

## Reproduce

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
