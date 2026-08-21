<!-- cheatah-bench-stamp v1
     suite:        fixarray-vs-glm
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
     layout:       opstype
     watch:        stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='fixarray-vs-glm' \
           CHEATAH_BENCH_LAYOUT='opstype' \
           CHEATAH_BENCH_WATCH='stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=_(fixed|glm)$ --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/fixarray-vs-glm.json --benchmark_format=console
-->

<details><summary><b>Full GLM comparison — all 160 operations</b> (ns, lower is better; ± is the IQR over 9 interleaved repetitions)</summary>


#### Vectors

| operation | type | `Fixed` | GLM | ratio | |
|---|---|--:|--:|:--:|---|
| `abs` | vec3d | 0.47 ±0.02 | 0.82 ±0.02 | 0.57× | 🟢 faster |
| `abs` | vec3f | 0.45 ±0.01 | 0.84 ±0.01 | 0.54× | 🟢 faster |
| `abs` | vec4d | 0.46 ±0.01 | 0.78 ±0.02 | 0.58× | 🟢 faster |
| `abs` | vec4f | 0.46 ±0.01 | 0.83 ±0.02 | 0.55× | 🟢 faster |
| `add` | vec2d | 0.45 ±0.00 | 0.45 ±0.00 | 1.01× | ⬜ parity |
| `add` | vec2f | 0.36 ±0.03 | 0.23 ±0.01 | 1.58× | ⬜ parity |
| `add` | vec3d | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `add` | vec3f | 0.36 ±0.01 | 0.42 ±0.00 | 0.86× | ⬜ parity |
| `add` | vec4d | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `add` | vec4f | 0.45 ±0.01 | 0.46 ±0.01 | 0.99× | ⬜ parity |
| `clamp` | vec3d | 0.45 ±0.01 | 0.45 ±0.00 | 0.99× | ⬜ parity |
| `clamp` | vec3f | 0.49 ±0.01 | 0.51 ±0.01 | 0.96× | ⬜ parity |
| `clamp` | vec4d | 0.45 ±0.01 | 0.45 ±0.01 | 0.99× | ⬜ parity |
| `clamp` | vec4f | 0.45 ±0.00 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `cross` | vec3d | 0.68 ±0.03 | 0.70 ±0.02 | 0.98× | ⬜ parity |
| `cross` | vec3f | 0.76 ±0.07 | 0.76 ±0.02 | 1.00× | ⬜ parity |
| `distance2` | vec3d | 0.65 ±0.02 | 0.83 ±0.02 | 0.78× | ⬜ parity |
| `distance2` | vec3f | 0.71 ±0.02 | 0.84 ±0.03 | 0.85× | ⬜ parity |
| `distance2` | vec4d | 1.17 ±0.01 | 1.16 ±0.03 | 1.01× | ⬜ parity |
| `distance2` | vec4f | 1.34 ±0.01 | 1.35 ±0.02 | 0.99× | ⬜ parity |
| `distance` | vec3d | 1.35 ±0.02 | 1.35 ±0.02 | 1.00× | ⬜ parity |
| `distance` | vec3f | 0.84 ±0.02 | 0.89 ±0.01 | 0.94× | ⬜ parity |
| `distance` | vec4d | 1.16 ±0.02 | 1.16 ±0.01 | 1.00× | ⬜ parity |
| `distance` | vec4f | 1.31 ±0.04 | 1.29 ±0.02 | 1.02× | ⬜ parity |
| `divs` | vec2d | 0.45 ±0.01 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `divs` | vec2f | 0.23 ±0.00 | 0.22 ±0.00 | 1.00× | ⬜ parity |
| `divs` | vec3d | 0.46 ±0.00 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `divs` | vec3f | 0.46 ±0.02 | 0.45 ±0.00 | 1.02× | ⬜ parity |
| `divs` | vec4d | 0.47 ±0.01 | 0.45 ±0.00 | 1.03× | ⬜ parity |
| `divs` | vec4f | 0.45 ±0.01 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `dot` | vec2d | 0.45 ±0.01 | 0.45 ±0.01 | 0.99× | ⬜ parity |
| `dot` | vec2f | 0.41 ±0.04 | 0.36 ±0.01 | 1.13× | ⬜ parity |
| `dot` | vec3d | 0.58 ±0.01 | 0.46 ±0.01 | 1.26× | ⬜ parity |
| `dot` | vec3f | 0.57 ±0.01 | 0.54 ±0.02 | 1.07× | ⬜ parity |
| `dot` | vec4d | 0.68 ±0.02 | 0.68 ±0.01 | 1.00× | ⬜ parity |
| `dot` | vec4f | 0.87 ±0.04 | 1.02 ±0.02 | 0.85× | ⬜ parity |
| `len2` | vec2d | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `len2` | vec2f | 0.31 ±0.01 | 0.30 ±0.00 | 1.02× | ⬜ parity |
| `len2` | vec3d | 0.45 ±0.01 | 0.42 ±0.02 | 1.07× | ⬜ parity |
| `len2` | vec3f | 0.45 ±0.01 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `len2` | vec4d | 0.51 ±0.01 | 0.52 ±0.01 | 1.00× | ⬜ parity |
| `len2` | vec4f | 0.61 ±0.01 | 0.69 ±0.01 | 0.89× | ⬜ parity |
| `len` | vec2d | 1.36 ±0.02 | 1.36 ±0.04 | 1.00× | ⬜ parity |
| `len` | vec2f | 0.68 ±0.01 | 0.68 ±0.01 | 1.00× | ⬜ parity |
| `len` | vec3d | 1.35 ±0.01 | 1.36 ±0.01 | 0.99× | ⬜ parity |
| `len` | vec3f | 0.69 ±0.01 | 0.69 ±0.01 | 1.01× | ⬜ parity |
| `len` | vec4d | 1.35 ±0.03 | 1.36 ±0.01 | 0.99× | ⬜ parity |
| `len` | vec4f | 0.75 ±0.01 | 0.82 ±0.01 | 0.91× | ⬜ parity |
| `max` | vec3d | 0.45 ±0.01 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `max` | vec3f | 0.40 ±0.01 | 0.39 ±0.01 | 1.02× | ⬜ parity |
| `max` | vec4d | 0.45 ±0.01 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `max` | vec4f | 0.46 ±0.01 | 0.44 ±0.01 | 1.02× | ⬜ parity |
| `min` | vec3d | 0.45 ±0.01 | 0.46 ±0.02 | 0.98× | ⬜ parity |
| `min` | vec3f | 0.42 ±0.02 | 0.43 ±0.00 | 0.98× | ⬜ parity |
| `min` | vec4d | 0.46 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `min` | vec4f | 0.45 ±0.01 | 0.46 ±0.01 | 0.99× | ⬜ parity |
| `mix` | vec3d | 0.63 ±0.02 | 0.55 ±0.10 | 1.15× | ⬜ parity |
| `mix` | vec3f | 0.57 ±0.01 | 0.57 ±0.01 | 0.99× | ⬜ parity |
| `mix` | vec4d | 0.56 ±0.03 | 0.66 ±0.01 | 0.85× | ⬜ parity |
| `mix` | vec4f | 0.55 ±0.01 | 0.56 ±0.01 | 1.00× | ⬜ parity |
| `muls` | vec2d | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `muls` | vec2f | 0.26 ±0.01 | 0.23 ±0.00 | 1.16× | ⬜ parity |
| `muls` | vec3d | 0.45 ±0.01 | 0.45 ±0.00 | 1.01× | ⬜ parity |
| `muls` | vec3f | 0.45 ±0.01 | 0.45 ±0.01 | 0.99× | ⬜ parity |
| `muls` | vec4d | 0.45 ±0.01 | 0.46 ±0.01 | 0.98× | ⬜ parity |
| `muls` | vec4f | 0.45 ±0.00 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `neg` | vec2d | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `neg` | vec2f | 0.23 ±0.00 | 0.23 ±0.01 | 1.00× | ⬜ parity |
| `neg` | vec3d | 0.45 ±0.01 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `neg` | vec3f | 0.45 ±0.01 | 0.46 ±0.01 | 0.99× | ⬜ parity |
| `neg` | vec4d | 0.45 ±0.00 | 0.45 ±0.01 | 0.99× | ⬜ parity |
| `neg` | vec4f | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `normalize` | vec2d | 2.35 ±0.04 | 2.29 ±0.04 | 1.03× | ⬜ parity |
| `normalize` | vec2f | 1.36 ±0.02 | 1.36 ±0.04 | 0.99× | ⬜ parity |
| `normalize` | vec3d | 2.39 ±0.06 | 2.30 ±0.04 | 1.04× | ⬜ parity |
| `normalize` | vec3f | 1.41 ±0.03 | 1.40 ±0.01 | 1.01× | ⬜ parity |
| `normalize` | vec4d | 2.41 ±0.03 | 2.38 ±0.05 | 1.01× | ⬜ parity |
| `normalize` | vec4f | 1.43 ±0.04 | 1.45 ±0.03 | 0.99× | ⬜ parity |
| `reflect` | vec3d | 2.89 ±0.07 | 2.79 ±0.06 | 1.04× | ⬜ parity |
| `reflect` | vec3f | 2.63 ±0.05 | 2.62 ±0.07 | 1.01× | ⬜ parity |
| `reflect` | vec4d | 3.31 ±0.08 | 3.47 ±0.05 | 0.95× | ⬜ parity |
| `reflect` | vec4f | 3.48 ±0.10 | 3.67 ±0.11 | 0.95× | ⬜ parity |
| `sign` | vec3d | 0.96 ±0.02 | 1.17 ±0.02 | 0.82× | ⬜ parity |
| `sign` | vec3f | 0.91 ±0.02 | 1.26 ±0.02 | 0.72× | 🟢 faster |
| `sign` | vec4d | 0.94 ±0.02 | 1.58 ±0.03 | 0.60× | 🟢 faster |
| `sign` | vec4f | 0.74 ±0.01 | 1.81 ±0.03 | 0.41× | 🟢 faster |
| `smoothstep` | vec3d | 1.30 ±0.02 | 1.17 ±0.01 | 1.11× | ⬜ parity |
| `smoothstep` | vec3f | 1.29 ±0.02 | 1.17 ±0.03 | 1.10× | ⬜ parity |
| `smoothstep` | vec4d | 1.30 ±0.04 | 1.18 ±0.02 | 1.10× | ⬜ parity |
| `smoothstep` | vec4f | 1.40 ±0.01 | 1.26 ±0.02 | 1.10× | ⬜ parity |
| `step` | vec3d | 0.45 ±0.00 | 0.46 ±0.01 | 0.98× | ⬜ parity |
| `step` | vec3f | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `step` | vec4d | 0.45 ±0.01 | 0.45 ±0.00 | 0.99× | ⬜ parity |
| `step` | vec4f | 0.45 ±0.00 | 0.68 ±0.01 | 0.66× | ⬜ parity |
| `sub` | vec2d | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `sub` | vec2f | 0.23 ±0.01 | 0.23 ±0.01 | 1.02× | ⬜ parity |
| `sub` | vec3d | 0.46 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `sub` | vec3f | 0.42 ±0.01 | 0.36 ±0.01 | 1.15× | ⬜ parity |
| `sub` | vec4d | 0.46 ±0.01 | 0.45 ±0.00 | 1.01× | ⬜ parity |
| `sub` | vec4f | 0.46 ±0.01 | 0.46 ±0.01 | 1.00× | ⬜ parity |

#### Matrices

| operation | type | `Fixed` | GLM | ratio | |
|---|---|--:|--:|:--:|---|
| `add` | mat2d | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `add` | mat2f | 0.45 ±0.01 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `add` | mat3d | 0.92 ±0.02 | 0.90 ±0.04 | 1.02× | ⬜ parity |
| `add` | mat3f | 0.67 ±0.01 | 1.14 ±0.01 | 0.59× | 🟢 faster |
| `add` | mat4d | 1.43 ±0.04 | 1.46 ±0.03 | 0.98× | ⬜ parity |
| `add` | mat4f | 0.68 ±0.02 | 1.38 ±0.12 | 0.49× | 🟢 faster |
| `compmult` | mat3d | 0.90 ±0.03 | 0.91 ±0.15 | 0.99× | ⬜ parity |
| `compmult` | mat3f | 0.67 ±0.01 | 1.12 ±0.03 | 0.60× | 🟢 faster |
| `compmult` | mat4d | 1.53 ±0.05 | 1.48 ±0.05 | 1.03× | ⬜ parity |
| `compmult` | mat4f | 0.69 ±0.03 | 1.36 ±0.03 | 0.51× | 🟢 faster |
| `det` | mat2d | 0.39 ±0.01 | 0.42 ±0.01 | 0.94× | ⬜ parity |
| `det` | mat2f | 0.46 ±0.01 | 0.46 ±0.01 | 1.00× | ⬜ parity |
| `det` | mat3d | 1.12 ±0.01 | 1.13 ±0.04 | 0.99× | ⬜ parity |
| `det` | mat3f | 1.11 ±0.02 | 1.12 ±0.02 | 0.98× | ⬜ parity |
| `det` | mat4d | 3.75 ±0.06 | 3.64 ±0.06 | 1.03× | ⬜ parity |
| `det` | mat4f | 3.78 ±0.09 | 3.67 ±0.06 | 1.03× | ⬜ parity |
| `identity` | mat2d | 0.46 ±0.01 | 0.46 ±0.01 | 0.99× | ⬜ parity |
| `identity` | mat2f | 0.23 ±0.01 | 0.23 ±0.00 | 1.01× | ⬜ parity |
| `identity` | mat3d | 1.12 ±0.02 | 1.36 ±0.03 | 0.83× | ⬜ parity |
| `identity` | mat3f | 0.91 ±0.01 | 0.92 ±0.03 | 1.00× | ⬜ parity |
| `identity` | mat4d | 1.13 ±0.01 | 2.50 ±0.07 | 0.45× | 🟢 faster |
| `identity` | mat4f | 0.68 ±0.01 | 1.81 ±0.04 | 0.37× | 🟢 faster |
| `inverse` | mat2d | 0.97 ±0.01 | 0.94 ±0.01 | 1.03× | ⬜ parity |
| `inverse` | mat2f | 1.03 ±0.02 | 0.92 ±0.01 | 1.12× | ⬜ parity |
| `inverse` | mat3d | 3.58 ±0.03 | 3.79 ±0.07 | 0.95× | ⬜ parity |
| `inverse` | mat3f | 3.61 ±0.03 | 3.82 ±0.10 | 0.94× | ⬜ parity |
| `inverse` | mat4d | 12.46 ±0.18 | 17.38 ±0.39 | 0.72× | 🟢 faster |
| `inverse` | mat4f | 11.25 ±0.04 | 14.75 ±0.32 | 0.76× | 🟢 faster |
| `invtrans` | mat3d | 4.39 ±0.27 | 4.67 ±0.08 | 0.94× | ⬜ parity |
| `invtrans` | mat3f | 4.29 ±0.12 | 4.00 ±0.07 | 1.07× | ⬜ parity |
| `invtrans` | mat4d | 12.67 ±0.35 | 15.22 ±0.18 | 0.83× | 🟢 faster |
| `invtrans` | mat4f | 11.57 ±0.18 | 14.12 ±0.17 | 0.82× | 🟢 faster |
| `matmul` | mat2d | 0.91 ±0.02 | 0.90 ±0.02 | 1.01× | ⬜ parity |
| `matmul` | mat2f | 0.92 ±0.01 | 0.91 ±0.02 | 1.01× | ⬜ parity |
| `matmul` | mat3d | 3.37 ±0.04 | 3.43 ±0.09 | 0.98× | ⬜ parity |
| `matmul` | mat3f | 3.72 ±0.05 | 3.73 ±0.12 | 1.00× | ⬜ parity |
| `matmul` | mat4d | 5.85 ±0.12 | 6.05 ±0.74 | 0.97× | ⬜ parity |
| `matmul` | mat4f | 3.43 ±0.06 | 5.92 ±0.16 | 0.58× | 🟢 faster |
| `matvec` | mat2d | 0.45 ±0.01 | 0.47 ±0.01 | 0.97× | ⬜ parity |
| `matvec` | mat2f | 0.45 ±0.00 | 0.47 ±0.01 | 0.96× | ⬜ parity |
| `matvec` | mat3d | 0.99 ±0.03 | 0.99 ±0.02 | 1.00× | ⬜ parity |
| `matvec` | mat3f | 0.99 ±0.02 | 0.99 ±0.02 | 1.00× | ⬜ parity |
| `matvec` | mat4d | 1.49 ±0.04 | 1.46 ±0.02 | 1.02× | ⬜ parity |
| `matvec` | mat4f | 1.46 ±0.03 | 1.47 ±0.04 | 1.00× | ⬜ parity |
| `muls` | mat2d | 0.49 ±0.01 | 0.45 ±0.01 | 1.10× | ⬜ parity |
| `muls` | mat2f | 0.45 ±0.01 | 0.45 ±0.01 | 0.99× | ⬜ parity |
| `muls` | mat3d | 0.90 ±0.00 | 0.91 ±0.02 | 1.00× | ⬜ parity |
| `muls` | mat3f | 0.68 ±0.01 | 0.90 ±0.01 | 0.75× | ⬜ parity |
| `muls` | mat4d | 1.12 ±0.01 | 1.14 ±0.02 | 0.99× | ⬜ parity |
| `muls` | mat4f | 0.68 ±0.01 | 1.35 ±0.03 | 0.51× | 🟢 faster |
| `outer` | mat3d | 0.91 ±0.04 | 0.92 ±0.03 | 0.99× | ⬜ parity |
| `outer` | mat3f | 0.90 ±0.02 | 0.91 ±0.03 | 0.99× | ⬜ parity |
| `outer` | mat4d | 1.23 ±0.03 | 1.21 ±0.02 | 1.02× | ⬜ parity |
| `outer` | mat4f | 0.75 ±0.01 | 1.24 ±0.03 | 0.61× | 🟢 faster |
| `transpose` | mat2d | 0.68 ±0.01 | 0.68 ±0.01 | 1.00× | ⬜ parity |
| `transpose` | mat2f | 0.45 ±0.01 | 0.45 ±0.01 | 1.00× | ⬜ parity |
| `transpose` | mat3d | 1.36 ±0.02 | 1.37 ±0.02 | 0.99× | ⬜ parity |
| `transpose` | mat3f | 1.13 ±0.03 | 1.13 ±0.02 | 1.00× | ⬜ parity |
| `transpose` | mat4d | 2.03 ±0.02 | 2.06 ±0.04 | 0.98× | ⬜ parity |
| `transpose` | mat4f | 2.03 ±0.05 | 2.01 ±0.04 | 1.01× | ⬜ parity |

**20 faster, 140 at parity, 0 slower** across 160 operations.

</details>
