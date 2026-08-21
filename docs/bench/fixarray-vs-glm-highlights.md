<!-- cheatah-bench-stamp v1
     suite:        fixarray-vs-glm-highlights
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
     layout:       highlights
     watch:        stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='fixarray-vs-glm-highlights' \
           CHEATAH_BENCH_LAYOUT='highlights' \
           CHEATAH_BENCH_ROWS='BM_identity_mat4f=mat4f::identity();BM_matmul_mat4f=mat4f * mat4f;BM_add_mat4f=mat4f + mat4f;BM_inverse_mat4d=inverse(mat4d);BM_abs_vec4f=abs(vec4f);BM_dot_vec4f=dot(vec4f, vec4f)' \
           CHEATAH_BENCH_WATCH='stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=^BM_(identity_mat4f|matmul_mat4f|add_mat4f|inverse_mat4d|abs_vec4f|dot_vec4f)_(fixed|glm)$ --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/fixarray-vs-glm-highlights.json --benchmark_format=console
-->

| operation | `Fixed` | GLM | | |
|-----------|--------:|----:|---|---|
| `mat4f::identity()` | **0.68 ns** ±0.01 | 1.79 ns ±0.02 | 2.63× | faster |
| `mat4f * mat4f` | **3.39 ns** ±0.09 | 5.80 ns ±0.10 | 1.71× | faster |
| `mat4f + mat4f` | **0.70 ns** ±0.16 | 1.38 ns ±0.22 | 1.99× | faster |
| `inverse(mat4d)` | **12.37 ns** ±0.17 | 17.12 ns ±0.36 | 1.38× | faster |
| `abs(vec4f)` | **0.45 ns** ±0.00 | 0.83 ns ±0.02 | 1.85× | faster |
| `dot(vec4f, vec4f)` | 0.87 ns ±0.01 | 1.02 ns ±0.01 | 1.17× | parity — gap 0.15 ns |
