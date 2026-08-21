<!-- cheatah-bench-stamp v1
     suite:        crypto-vs-openssl
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
     layout:       throughput
     watch:        stdlib/aead/, stdlib/hashlib/, tests/benchmarks/crypto_openssl_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='crypto-vs-openssl' \
           CHEATAH_BENCH_LAYOUT='throughput' \
           CHEATAH_BENCH_WATCH='stdlib/aead/, stdlib/hashlib/, tests/benchmarks/crypto_openssl_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=Crypto --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/crypto-vs-openssl.json --benchmark_format=console
-->

| Primitive | cheatah | OpenSSL | gap |
|-----------|--------:|--------:|----:|
| AES-128-GCM (AES-NI + PCLMULQDQ) | **3.66 GiB/s** | 3.45 GiB/s | **parity** (1.06×) |
| SHA-512 | 0.46 GiB/s | 0.82 GiB/s | 1.78× slower |
| ChaCha20-Poly1305 | 0.42 GiB/s | 1.70 GiB/s | 4.04× slower |
| HMAC-SHA256 | 0.32 GiB/s | 1.37 GiB/s | 4.30× slower |
| SHA-256 | 0.33 GiB/s | 1.78 GiB/s | 5.43× slower |
