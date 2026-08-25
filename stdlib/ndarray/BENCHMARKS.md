# ndarray benchmarks

The element-wise math ufuncs are benchmarked against NumPy's vectorized equivalents
(same fixed-seed array to both, op run many times, results cross-checked) by
[`scripts/numpy_compare.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/numpy_compare.py).
Each function's **Performance** row on the [ndarray reference](README.md) carries its own number. The table below is
**generated** by that harness — see [docs/performance.md](../../docs/performance.md#reference-machine)
for the methodology (striated rounds, medians, paired ratios) and the reference machine. The
`band` column is the range of the per-round ratios, which on the large-array rows is wide
enough to matter: a bare headline would hide that `sqrt` at 16384 lands anywhere from 0.6× to
1.1× depending on the round.

<!-- BENCH:ndarray-vs-numpy begin -->
<!-- cheatah-bench-stamp v1
     suite:        ndarray-vs-numpy
     generated:    2026-08-20
     commit:       f78c5e8
     host:         12th Gen Intel(R) Core(TM) i7-12700H (governor=powersave), 20 CPUs
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  NumPy 1.26.4 on libblas.so.3 -> libblas.so.3.12.0, threads=unset (BLAS default), CPython 3.12.3
     harness:      rounds=7, striated (cheatah and NumPy adjacent in each round)
     statistic:    median of per-round PAIRED ratios; [lo-hi] is the range of those
     watch:        stdlib/ndarray/, scripts/numpy_compare.py
     publishable:  true

     PRODUCED BY:
       python3 scripts/numpy_compare.py --suite ndarray --md docs/bench/ndarray-vs-numpy.md
-->

| op | operand dimensions | cheatah | NumPy | winner | band |
|----|--------------------|--------:|------:|--------|------|
| `ndarray.sqrt` | 64 | 0.09 | 0.35 | **cheatah 3.9x** | 3.48-4.12 |
| `ndarray.sqrt` | 16384 | 11.85 | 11.89 | **cheatah 1.0x** | 0.83-1.03 |
| `ndarray.exp` | 16384 | 10.66 | 46.94 | **cheatah 4.4x** | 3.61-4.66 |
| `ndarray.sin` | 16384 | 11.45 | 95.75 | **cheatah 8.5x** | 6.31-9.39 |
| `X + scalar` | 16384 | 2.85 | 3.07 | **cheatah 1.1x** | 0.94-1.40 |
| `ndarray.add` | 16384 | 3.43 | 9.62 | **cheatah 2.6x** | 1.71-2.97 |
<!-- BENCH:ndarray-vs-numpy end -->
