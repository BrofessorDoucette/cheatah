<!-- cheatah-bench-stamp v1
     suite:        whole-programs
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     host:         12th Gen Intel(R) Core(TM) i7-12700H, 20 CPUs, Linux 7.0.11-76070011-generic (governor=powersave)
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  CPython 3.12
     harness:      rounds=7, striated (every configuration runs once per round)
     statistic:    median of per-round PAIRED ratios; [lo–hi] is the range of those ratios
     watch:        compiler/, runtime/, scripts/bench/, scripts/app_compare.purr
     publishable:  true

     PRODUCED BY:
       bash scripts/bench/build-harness.sh scripts/app_compare.purr /tmp/ac.so && cheatah /tmp/ac.so docs/bench/whole-programs.md
-->

| program | what it does | cheatah | CPython | speedup |
|---------|--------------|--------:|--------:|--------:|
| **Mandelbrot** | escape-time over an 800×600 grid (≤256 iters) | 61 ms | 4451 ms | **74×** [66–77] |
| **Numerical integral** | trapezoid ∫ sin(x)·e^(−x/100), 20M points | 175 ms | 2482 ms | **14×** [13–18] |
| **RK4 ODE** | 4th-order Runge–Kutta, 4 000 000 steps | 44 ms | 2663 ms | **61×** [45–63] |
| **N-body** | direct O(N²) gravity, 256 bodies × 200 leapfrog steps | 32 ms | 2963 ms | **89×** [78–106] |
