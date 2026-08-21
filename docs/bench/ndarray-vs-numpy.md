<!-- cheatah-bench-stamp v1
     suite:        ndarray-vs-numpy
     generated:    2026-08-20
     commit:       b97c491 (dirty)
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
| `ndarray.sqrt` | 64 | 0.09 | 0.37 | **cheatah 4.0x** | 2.51-5.99 |
| `ndarray.sqrt` | 16384 | 12.13 | 12.32 | NumPy 1.0x | 0.88-1.08 |
| `ndarray.exp` | 16384 | 11.00 | 47.79 | **cheatah 4.4x** | 3.04-4.50 |
| `ndarray.sin` | 16384 | 12.56 | 106.05 | **cheatah 8.3x** | 6.89-9.13 |
| `X + scalar` | 16384 | 2.79 | 3.37 | **cheatah 1.2x** | 0.84-1.36 |
| `ndarray.add` | 16384 | 3.97 | 6.58 | **cheatah 1.6x** | 1.37-2.41 |
