<!-- cheatah-bench-stamp v1
     suite:        thread-scaling
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     host:         12th Gen Intel(R) Core(TM) i7-12700H, 20 CPUs, Linux 7.0.11-76070011-generic (governor=powersave)
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  none — cheatah against itself at 1/2/4/8 workers
     harness:      rounds=7, striated (every configuration runs once per round)
     statistic:    median wall clock; ± is the sample standard deviation
     watch:        stdlib/thread/, stdlib/memory/, scripts/bench/integral_threads.purr
     publishable:  true

     PRODUCED BY:
       purrc scripts/bench/integral_threads.purr -o /tmp/it.so --import-root scripts && cheatah /tmp/it.so docs/bench/thread-scaling.md
-->

| workers | wall time (median) | spread | speedup | integral |
|--------:|-------------------:|-------:|--------:|----------|
| 1 | 173 ms | ±1 ms | — | 0.416268 |
| 2 | 94 ms | ±5 ms | **1.84×** | 0.416268 |
| 4 | 57 ms | ±7 ms | **3.04×** | 0.416268 |
| 8 | 41 ms | ±3 ms | **4.18×** | 0.416268 |
