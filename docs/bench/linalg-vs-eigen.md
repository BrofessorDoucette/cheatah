<!-- cheatah-bench-stamp v1
     suite:        linalg-vs-eigen
     generated:    2026-08-27
     commit:       5891b8a
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

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='linalg-vs-eigen' \
           CHEATAH_BENCH_LAYOUT='pairs' \
           CHEATAH_BENCH_WATCH='stdlib/linalg/, tests/benchmarks/eigen_compare_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=^BM_(dot|matmul|solve|inv)_(cheatah|eigen) --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/linalg-vs-eigen.json --benchmark_format=console
-->

| case | cheatah | spread | vs | rival | spread | ratio | verdict |
|---|--:|--:|---|--:|--:|--:|---|
| BM_dot/16384 | 1.96 µs | ±249.61 ns IQR | eigen | 2.58 µs | ±71.26 ns IQR | 1.32x | faster |
| BM_dot/64 | 11.66 ns | ±1.57 ns IQR | eigen | 9.08 ns | ±0.37 ns IQR | 0.78x | **slower** |
| BM_inv/32 | 6.82 µs | ±1.34 µs IQR | eigen | 11.86 µs | ±133.38 ns IQR | 1.74x | faster |
| BM_inv/64 | 40.48 µs | ±8.56 µs IQR | eigen | 67.73 µs | ±1.32 µs IQR | 1.67x | faster |
| BM_matmul/32 | 3.23 µs | ±1.23 µs IQR | eigen | 3.90 µs | ±115.58 ns IQR | 1.21x | faster |
| BM_matmul/96 | 58.11 µs | ±32.71 µs IQR | eigen | 94.56 µs | ±2.47 µs IQR | 1.63x | faster |
| BM_solve/32 | 3.59 µs | ±175.70 ns IQR | eigen | 4.49 µs | ±71.85 ns IQR | 1.25x | faster |
| BM_solve/64 | 16.73 µs | ±1.91 µs IQR | eigen | 20.77 µs | ±525.30 ns IQR | 1.24x | faster |

**Tally** (a difference counts only above 1.15x AND 0.25 ns) — vs eigen: **7 faster / 0 parity / 1 slower**.
- Loss vs eigen: `BM_dot/64` — cheatah 11.66 ns vs 9.08 ns (1.28x slower)
