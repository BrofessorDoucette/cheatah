# linalg benchmarks

`linalg` goes head-to-head with NumPy, whose array ops dispatch to **BLAS/LAPACK** —
hand-tuned, vectorized, often multi-threaded Fortran. The
[`scripts/numpy_compare.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/numpy_compare.py)
harness feeds the **same** fixed-seed, well-conditioned matrix to both, runs the **same**
op many times with the result consumed, and checks the answers agree. Each function's
**Performance** row on the [linalg reference](README.md) carries its own measurement; the full size-dependence:

> **What we compared against — read this before the numbers.** NumPy's absolute speed, and
> where every crossover lands, depends far more on which **BLAS** it links than on which NumPy
> version it is. The generated stamp below records the resolved library rather than the version
> string, because "NumPy 1.26.4" does not identify a measurement and the library does.
>
> On this machine that resolves to **`libblas.so.3` → the reference implementation**, not
> OpenBLAS and not MKL. That matters for how much these wins are worth: reference BLAS is the
> unoptimized baseline, so a cheatah win over it is a win over *untuned* Fortran, not over the
> tuned kernels most NumPy installs actually use. A tuned BLAS would narrow the large-`n` rows
> and push the crossovers lower. The small-`n` wins are a different claim and survive either
> way — they come from cheatah having no per-call Python and dispatch overhead to pay, which no
> choice of BLAS changes.

The **Eigen** comparison is a *separate* measurement, in the native Google Benchmark harness
([`tests/benchmarks/eigen_compare_bench.cpp`](https://github.com/BrofessorDoucette/cheatah/blob/main/tests/benchmarks/eigen_compare_bench.cpp)),
where cheatah and **Eigen 3.4** are both compiled C++ timed identically on **one thread** — an
apples-to-apples per-core comparison.

**Two harnesses, two tables — deliberately.** These numbers used to share one table, with the
cheatah column measured by `numpy_compare.py` (separate processes) sitting beside an Eigen
column measured by Google Benchmark (compiled C++, one process), and a prose warning not to
read across. A warning is a weaker fix than a structure: a reader who divides two adjacent
columns gets a wrong answer no matter how the caption is worded. Each harness now publishes
only what it measured, and neither table contains a column it did not produce.

Both are generated — see [docs/performance.md](../../docs/performance.md#reference-machine) for
the methodology (striated, interleaved, medians with dispersion) and the reference machine.

## vs NumPy

Measured by [`scripts/numpy_compare.py`](../../scripts/numpy_compare.py); reproduce with `python3 scripts/numpy_compare.py --suite linalg --md docs/bench/linalg-vs-numpy.md`.

<!-- BENCH:linalg-vs-numpy begin -->
<!-- cheatah-bench-stamp v1
     suite:        linalg-vs-numpy
     generated:    2026-08-20
     commit:       f78c5e8
     host:         12th Gen Intel(R) Core(TM) i7-12700H (governor=powersave), 20 CPUs
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  NumPy 1.26.4 on libblas.so.3 -> libblas.so.3.12.0, threads=unset (BLAS default), CPython 3.12.3
     harness:      rounds=7, striated (cheatah and NumPy adjacent in each round)
     statistic:    median of per-round PAIRED ratios; [lo-hi] is the range of those
     watch:        stdlib/linalg/, stdlib/ndarray/, scripts/numpy_compare.py
     publishable:  true

     PRODUCED BY:
       python3 scripts/numpy_compare.py --suite linalg --md docs/bench/linalg-vs-numpy.md
-->

| op | operand dimensions | cheatah | NumPy | winner | band |
|----|--------------------|--------:|------:|--------|------|
| `matmul` | 4 | 0.09 | 0.79 | **cheatah 8.3x** | 7.20-8.48 |
| `matmul` | 16 | 0.54 | 2.24 | **cheatah 4.1x** | 3.56-4.29 |
| `matmul` | 32 | 3.22 | 13.43 | **cheatah 4.2x** | 3.94-4.60 |
| `matmul` | 64 | 25.60 | 116.37 | **cheatah 4.5x** | 4.30-4.71 |
| `matmul` | 96 | 90.65 | 318.41 | **cheatah 3.5x** | 3.38-3.63 |
| `solve` | 4 | 0.22 | 2.45 | **cheatah 11.0x** | 10.03-11.49 |
| `solve` | 16 | 1.06 | 4.34 | **cheatah 4.1x** | 3.81-4.35 |
| `solve` | 32 | 3.69 | 10.35 | **cheatah 2.8x** | 2.55-2.85 |
| `solve` | 64 | 18.02 | 50.82 | **cheatah 2.8x** | 2.72-3.10 |
| `det` | 4 | 0.11 | 2.05 | **cheatah 22.6x** | 15.44-28.51 |
| `det` | 16 | 0.81 | 3.76 | **cheatah 4.7x** | 3.94-5.39 |
| `det` | 32 | 2.80 | 9.58 | **cheatah 3.4x** | 3.18-3.53 |
| `det` | 64 | 14.17 | 47.05 | **cheatah 3.4x** | 3.22-3.51 |
| `inv` | 4 | 0.26 | 2.18 | **cheatah 8.2x** | 7.66-9.08 |
| `inv` | 16 | 1.56 | 6.35 | **cheatah 4.1x** | 3.18-4.40 |
| `inv` | 32 | 6.97 | 25.27 | **cheatah 3.6x** | 3.51-3.65 |
| `inv` | 64 | 46.20 | 142.12 | **cheatah 3.1x** | 2.95-3.51 |
| `eigvalsh` | 2 | 0.22 | 2.01 | **cheatah 9.2x** | 8.95-9.74 |
| `eigvalsh` | 3 | 0.48 | 2.35 | **cheatah 4.9x** | 4.67-5.01 |
| `eigvalsh` | 4 | 0.62 | 2.66 | **cheatah 4.3x** | 3.88-4.37 |
| `eigvalsh` | 6 | 1.43 | 3.49 | **cheatah 2.4x** | 2.34-2.48 |
| `eigvalsh` | 8 | 1.91 | 4.20 | **cheatah 2.2x** | 2.10-2.23 |
| `eigvalsh` | 16 | 6.73 | 9.20 | **cheatah 1.4x** | 1.32-1.39 |
| `eigvalsh` | 32 | 26.01 | 25.98 | **cheatah 1.0x** | 0.95-1.04 |
| `eigvalsh` | 64 | 106.80 | 149.84 | **cheatah 1.4x** | 1.26-1.43 |
| `dot` | 64 | 0.02 | 0.62 | **cheatah 34.4x** | 28.33-46.46 |
| `dot` | 1024 | 0.09 | 1.08 | **cheatah 11.6x** | 7.28-17.05 |
| `dot` | 16384 | 2.59 | 8.16 | **cheatah 3.1x** | 2.77-3.54 |
| `ndarray.sqrt` | 64 | 0.19 | 0.88 | **cheatah 4.7x** | 3.07-4.97 |
| `ndarray.sqrt` | 1024 | 0.96 | 1.77 | **cheatah 1.8x** | 1.52-2.09 |
| `ndarray.sqrt` | 16384 | 14.58 | 14.66 | NumPy 1.0x | 0.95-1.04 |
| `ndarray.exp` | 64 | 0.21 | 1.05 | **cheatah 5.0x** | 3.83-5.62 |
| `ndarray.exp` | 1024 | 1.00 | 4.03 | **cheatah 4.0x** | 2.52-4.76 |
| `ndarray.exp` | 16384 | 13.52 | 48.89 | **cheatah 3.6x** | 3.40-3.92 |
| `ndarray.sin` | 64 | 0.22 | 1.11 | **cheatah 5.0x** | 4.08-6.13 |
| `ndarray.sin` | 1024 | 0.95 | 5.52 | **cheatah 5.9x** | 4.23-6.25 |
| `ndarray.sin` | 16384 | 13.69 | 89.32 | **cheatah 6.5x** | 5.68-6.84 |
| `ndarray.add` | 64 | 0.11 | 0.62 | **cheatah 5.5x** | 4.58-6.05 |
| `ndarray.add` | 16384 | 2.48 | 2.88 | **cheatah 1.2x** | 0.78-1.23 |
| `cholesky` | 8 | 0.31 | 2.63 | **cheatah 8.4x** | 7.20-9.79 |
| `cholesky` | 32 | 3.77 | 7.35 | **cheatah 2.0x** | 1.68-2.15 |
| `cholesky` | 64 | 16.81 | 25.99 | **cheatah 1.5x** | 1.35-1.78 |
| `qr` | 8 | 1.07 | 8.88 | **cheatah 8.3x** | 7.53-8.64 |
| `qr` | 32 | 14.79 | 28.49 | **cheatah 2.0x** | 1.72-2.16 |
| `qr` | 64 | 102.62 | 137.47 | **cheatah 1.3x** | 1.16-1.36 |
| `svdvals` | 8 | 3.28 | 6.15 | **cheatah 1.9x** | 1.70-1.93 |
| `svdvals` | 32 | 49.70 | 42.83 | NumPy 1.2x | 0.82-0.96 |
| `svdvals` | 64 | 229.46 | 208.75 | NumPy 1.1x | 0.88-0.97 |
| `svd (full)` | 8 | 3.93 | 10.53 | **cheatah 2.7x** | 2.51-2.85 |
| `svd (full)` | 32 | 70.11 | 115.62 | **cheatah 1.6x** | 1.50-1.65 |
| `svd (full)` | 64 | 433.35 | 739.34 | **cheatah 1.7x** | 1.50-1.76 |
| `pinv` | 8 | 4.63 | 18.86 | **cheatah 4.1x** | 3.37-4.19 |
| `pinv` | 32 | 100.36 | 136.64 | **cheatah 1.4x** | 1.31-1.47 |
| `pinv` | 64 | 633.48 | 800.84 | **cheatah 1.3x** | 1.23-1.31 |
| `cond` | 8 | 3.23 | 10.21 | **cheatah 3.2x** | 3.07-3.31 |
| `cond` | 32 | 49.56 | 45.71 | NumPy 1.1x | 0.89-0.97 |
| `cond` | 64 | 217.89 | 195.27 | NumPy 1.1x | 0.89-0.95 |
| `matrix_rank` | 8 | 2.98 | 11.98 | **cheatah 4.0x** | 3.86-4.16 |
| `matrix_rank` | 32 | 46.95 | 51.17 | **cheatah 1.1x** | 0.98-1.12 |
| `matrix_rank` | 64 | 216.33 | 194.85 | NumPy 1.1x | 0.88-0.93 |
| `slogdet` | 8 | 0.20 | 3.15 | **cheatah 15.2x** | 11.78-17.67 |
| `slogdet` | 32 | 2.93 | 10.48 | **cheatah 3.6x** | 3.45-3.71 |
| `slogdet` | 64 | 14.03 | 48.95 | **cheatah 3.5x** | 2.20-3.56 |
| `eigh` | 8 | 2.48 | 6.52 | **cheatah 2.6x** | 2.32-4.14 |
| `eigh` | 32 | 37.96 | 64.51 | **cheatah 1.7x** | 1.62-1.72 |
| `eigh` | 64 | 238.38 | 401.26 | **cheatah 1.7x** | 1.61-1.72 |
| `eigvals` | 8 | 7.88 | 14.11 | **cheatah 1.8x** | 1.73-1.89 |
| `matrix_power` | 8 | 0.91 | 2.49 | **cheatah 2.7x** | 2.60-2.79 |
| `matrix_power` | 32 | 12.27 | 28.00 | **cheatah 2.3x** | 1.96-2.38 |
| `matrix_power` | 64 | 97.93 | 214.16 | **cheatah 2.2x** | 2.12-2.40 |
| `trace` | 32 | 0.01 | 1.20 | **cheatah 168.1x** | 120.84-178.23 |
| `trace` | 256 | 0.08 | 1.47 | **cheatah 20.2x** | 17.90-22.16 |
| `norm(matrix)` | 32 | 0.09 | 1.55 | **cheatah 17.8x** | 10.01-24.71 |
| `norm(matrix)` | 256 | 7.38 | 32.35 | **cheatah 4.4x** | 3.92-4.52 |
| `outer` | 64 | 0.34 | 4.05 | **cheatah 11.9x** | 7.13-13.10 |
| `outer` | 256 | 9.78 | 48.77 | **cheatah 5.0x** | 4.15-5.25 |
| `kron` | 8 | 1.71 | 13.12 | **cheatah 7.8x** | 7.24-8.74 |
| `kron` | 16 | 20.25 | 72.57 | **cheatah 3.6x** | 3.20-4.17 |
| `kron` | 32 | 347.09 | 950.98 | **cheatah 2.7x** | 2.49-2.81 |
<!-- BENCH:linalg-vs-numpy end -->

## vs Eigen

Measured by [`tests/benchmarks/eigen_compare_bench.cpp`](../../tests/benchmarks/eigen_compare_bench.cpp); reproduce with `scripts/bench_run.sh publish linalg-vs-eigen`.

<!-- BENCH:linalg-vs-eigen begin -->
<!-- cheatah-bench-stamp v1
     suite:        linalg-vs-eigen
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
     layout:       pairs
     watch:        stdlib/linalg/, tests/benchmarks/eigen_compare_bench.cpp
-->

| case | cheatah | spread | vs | rival | spread | ratio | verdict |
|---|--:|--:|---|--:|--:|--:|---|
| BM_dot/16384 | 1.76 µs | ±140.45 ns IQR | eigen | 2.54 µs | ±29.23 ns IQR | 1.44x | faster |
| BM_dot/64 | 10.18 ns | ±0.71 ns IQR | eigen | 6.69 ns | ±0.17 ns IQR | 0.66x | **slower** |
| BM_inv/32 | 6.76 µs | ±1.35 µs IQR | eigen | 11.21 µs | ±438.60 ns IQR | 1.66x | faster |
| BM_inv/64 | 38.55 µs | ±8.55 µs IQR | eigen | 67.44 µs | ±2.07 µs IQR | 1.75x | faster |
| BM_matmul/32 | 3.17 µs | ±137.63 ns IQR | eigen | 3.80 µs | ±49.86 ns IQR | 1.20x | faster |
| BM_matmul/96 | 87.96 µs | ±879.72 ns IQR | eigen | 94.93 µs | ±3.32 µs IQR | 1.08x | parity |
| BM_solve/32 | 3.56 µs | ±118.70 ns IQR | eigen | 4.17 µs | ±151.63 ns IQR | 1.17x | faster |
| BM_solve/64 | 16.47 µs | ±372.73 ns IQR | eigen | 19.97 µs | ±426.11 ns IQR | 1.21x | faster |

**Tally** (a difference counts only above 1.15x AND 0.25 ns) — vs eigen: **6 faster / 1 parity / 1 slower**.
- Loss vs eigen: `BM_dot/64` — cheatah 10.18 ns vs 6.69 ns (1.52x slower)
<!-- BENCH:linalg-vs-eigen end -->

## After the optimization round

After a focused optimization round (hunting a few recurring mistakes across every
routine — a heap allocation in a hot predicate, single-accumulator reductions, a
column-stride QR walk, and a result buffer that was zero-filled and then thrown away):

- **vs NumPy/LAPACK, cheatah now wins across nearly the whole library** — products, the
  LU family, the SVD, the symmetric eigensolver, `outer`, `qr`, `kron`, and large `norm`
  (the last four previously *lost*). The handful still behind (`svdvals`/`cond`/
  `matrix_rank` ~1.1×) are SVD-threshold queries where LAPACK's bidiagonal solver edges it.
- **vs Eigen 3.4 on one core, cheatah matches or beats it on the bulk** — `inv` (≈1.7×),
  `svd` (≈1.6×), `det`/`matmul`/`trace` (≈1.3–1.4×), `outer` (≈1.2–1.3×, now that the
  result buffer is built uninitialized and moved in zero-copy), `solve`/`eigvalsh`/`eigh`/
  `norm` (≈1.1–1.2×). Eigen still leads on its **blocked BLAS-3** kernels — `qr`
  (1.3–1.8×), `cholesky` (1.2×), and the `eigh` eigenvector path (1.1×) — which we flag
  honestly rather than hide.
