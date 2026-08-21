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
