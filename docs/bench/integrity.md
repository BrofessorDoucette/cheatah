<!-- cheatah-bench-stamp v1
     suite:        integrity
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
     layout:       solo
     watch:        runtime/, stdlib/hashlib/, stdlib/ed25519/, tests/benchmarks/integrity_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='integrity' \
           CHEATAH_BENCH_LAYOUT='solo' \
           CHEATAH_BENCH_WATCH='runtime/, stdlib/hashlib/, stdlib/ed25519/, tests/benchmarks/integrity_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=^BM_Integrity --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/integrity.json --benchmark_format=console
-->

| Op | median | spread | throughput |
|---|--:|--:|--:|
| `BM_Integrity/Checksum/1M` | 2.87 ms | ±103.41 µs IQR | 0.34 GiB/s |
| `BM_Integrity/Checksum/64K` | 157.05 µs | ±4.32 µs IQR | 0.39 GiB/s |
| `BM_Integrity/Full/1M` | 9.55 ms | ±143.26 µs IQR | 0.10 GiB/s |
| `BM_Integrity/Full/64K` | 4.65 ms | ±94.96 µs IQR | 0.01 GiB/s |
| `BM_Integrity/Off/1M` | 1.53 µs | ±203.42 ns IQR | 638.54 GiB/s |
| `BM_Integrity/Off/64K` | 1.48 µs | ±19.83 ns IQR | 41.11 GiB/s |
| `BM_Integrity/Signed/1M` | 7.88 ms | ±215.73 µs IQR | 0.12 GiB/s |
| `BM_Integrity/Signed/64K` | 2.00 ms | ±38.51 µs IQR | 0.03 GiB/s |
