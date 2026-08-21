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
