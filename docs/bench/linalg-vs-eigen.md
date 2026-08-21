<!-- cheatah-bench-stamp v1
     suite:        linalg-vs-eigen
     generated:    2026-08-20
     commit:       2b3a0b8
     host:         pop-os, 20 CPUs @ 4600 MHz
     cpu-scaling:  enabled
     build:        Clang 18.1.3 (1ubuntu1), Google Benchmark v1.9.5
     competitors:  Eigen 3.4.0, GLM GLM: version 0.9.9.8, OpenSSL 3.0.13 30 Jan 2024
     harness:      reps=9, min_time=0.3s, random-interleaving=on
     statistic:    median real time per case; spread = IQR over
                   repetitions, or `sd` where
                   --benchmark_report_aggregates_only hid the raw runs
     publishable:  true
     layout:       pairs
     watch:        stdlib/linalg/, tests/benchmarks/eigen_compare_bench.cpp
-->

| case | cheatah | spread | vs | rival | spread | ratio | verdict |
|---|--:|--:|---|--:|--:|--:|---|
| BM_dot/16384 | 1.76 µs | ±140.45 ns IQR | eigen | 2.54 µs | ±29.23 ns IQR | 1.44x | faster |
| BM_dot/64 | 10.18 ns | ±0.71 ns IQR | eigen | 6.69 ns | ±0.17 ns IQR | 0.66x | **slower** |
| BM_inv/32 | 6.76 µs | ±1.35 µs IQR | eigen | 11.21 µs | ±438.60 ns IQR | 1.66x | faster |
| BM_inv/64 | 38.55 µs | ±8.55 µs IQR | eigen | 67.44 µs | ±2.07 µs IQR | 1.75x | faster |
| BM_matmul/32 | 3.17 µs | ±137.63 ns IQR | eigen | 3.80 µs | ±49.86 ns IQR | 1.20x | faster |
| BM_matmul/96 | 87.96 µs | ±879.72 ns IQR | eigen | 94.93 µs | ±3.32 µs IQR | 1.08x | parity |
| BM_solve/32 | 3.56 µs | ±118.70 ns IQR | eigen | 4.17 µs | ±151.63 ns IQR | 1.17x | faster |
| BM_solve/64 | 16.47 µs | ±372.73 ns IQR | eigen | 19.97 µs | ±426.11 ns IQR | 1.21x | faster |

**Tally** (a difference counts only above 1.15x AND 0.25 ns) — vs eigen: **6 faster / 1 parity / 1 slower**.
- Loss vs eigen: `BM_dot/64` — cheatah 10.18 ns vs 6.69 ns (1.52x slower)
