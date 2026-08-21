<!-- cheatah-bench-stamp v1
     suite:        thread-scaling
     generated:    2026-08-20
     commit:       2b3a0b8
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
| 1 | 176 ms | ±5 ms | — | 0.416268 |
| 2 | 93 ms | ±4 ms | **1.88×** | 0.416268 |
| 4 | 56 ms | ±3 ms | **3.16×** | 0.416268 |
| 8 | 39 ms | ±3 ms | **4.46×** | 0.416268 |
