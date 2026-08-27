# p256 benchmarks

Micro-benchmarks of the [`p256`](README.md) module (`tests/benchmarks/p256_bench.cpp`,
release build, one core):

Measured by [`tests/benchmarks/p256_bench.cpp`](../../tests/benchmarks/p256_bench.cpp); reproduce with `scripts/bench_run.sh publish p256`.

<!-- BENCH:p256 begin -->
<!-- cheatah-bench-stamp v1
     suite:        p256
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
     layout:       solo
     watch:        stdlib/p256/, tests/benchmarks/p256_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='p256' \
           CHEATAH_BENCH_LAYOUT='solo' \
           CHEATAH_BENCH_WATCH='stdlib/p256/, tests/benchmarks/p256_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=^BM_P256_ --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/p256.json --benchmark_format=console
-->

| Op | median | spread | throughput |
|---|--:|--:|--:|
| `BM_P256_Sign` | 58.34 µs | ±770.07 ns IQR | 17.1 k/s |
| `BM_P256_Verify` | 88.69 µs | ±588.83 ns IQR | 11.3 k/s |
<!-- BENCH:p256 end -->

Verification runs once per P-256 chain link plus once for CertificateVerify —
negligible next to a network round trip; signing is the per-message JWT path.
