<!-- cheatah-bench-stamp v1
     suite:        render-kernel
     generated:    2026-08-20
     commit:       2b3a0b8
     host:         12th Gen Intel(R) Core(TM) i7-12700H, 20 CPUs, Linux 7.0.11-76070011-generic (governor=powersave)
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  CPython 3.12 (xml.etree / expat)
     harness:      rounds=9, striated (every configuration runs once per round)
     statistic:    median wall clock; speedup = median of per-round PAIRED ratios
     watch:        stdlib/parsers/xml/, docs/gen-cheatah/gen_bench.purr, docs/gen-cheatah/gen_bench_parallel.purr, docs/gen-cheatah/gen_bench_compare.purr
     publishable:  true

     PRODUCED BY:
       bash scripts/bench/build-harness.sh docs/gen-cheatah/gen_bench_compare.purr /tmp/gbc.so && cheatah /tmp/gbc.so docs/bench/render-kernel.md
-->

| generator | median | spread (σ) | vs CPython |
|-----------|-------:|-----------:|-----------:|
| CPython (`xml.etree`, native `expat`) | 23.8 ms | ±2.9 ms | 1.0× |
| cheatah, single-threaded (`gen_bench.purr`) | 12.2 ms | ±1.9 ms | **2.03×** |
| cheatah, 4 threads over a shared `memory.Owner` (`gen_bench_parallel.purr`) | 6.2 ms | ±0.4 ms | **3.86×** |
