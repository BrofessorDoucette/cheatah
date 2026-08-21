<!-- cheatah-bench-stamp v1
     suite:        linalg-vs-eigen
     generated:    2026-08-20
     commit:       b97c491 (dirty)
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
| BM_dot/16384 | 1.76 µs | ±62.87 ns IQR | eigen | 2.66 µs | ±194.54 ns IQR | 1.51x | faster |
| BM_dot/64 | 10.33 ns | ±0.40 ns IQR | eigen | 6.90 ns | ±0.24 ns IQR | 0.67x | **slower** |
| BM_inv/32 | 6.85 µs | ±1.14 µs IQR | eigen | 11.50 µs | ±304.44 ns IQR | 1.68x | faster |
| BM_inv/64 | 41.85 µs | ±2.91 µs IQR | eigen | 68.89 µs | ±1.07 µs IQR | 1.65x | faster |
| BM_matmul/32 | 3.25 µs | ±1.15 µs IQR | eigen | 3.96 µs | ±81.54 ns IQR | 1.22x | faster |
| BM_matmul/96 | 91.05 µs | ±33.39 µs IQR | eigen | 97.69 µs | ±2.00 µs IQR | 1.07x | parity |
| BM_solve/32 | 3.65 µs | ±59.79 ns IQR | eigen | 4.23 µs | ±225.91 ns IQR | 1.16x | faster |
| BM_solve/64 | 16.89 µs | ±335.60 ns IQR | eigen | 21.08 µs | ±492.88 ns IQR | 1.25x | faster |

**Tally** (a difference counts only above 1.15x AND 0.25 ns) — vs eigen: **6 faster / 1 parity / 1 slower**.
- Loss vs eigen: `BM_dot/64` — cheatah 10.33 ns vs 6.90 ns (1.50x slower)
