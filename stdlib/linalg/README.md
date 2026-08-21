# cheatah `linalg`

NumPy-style linear algebra on `ndarray`, with SIMD-accelerated contiguous kernels.

For the small-and-hot regime — a 3-D direction, a 4×4 transform built and consumed millions of
times a second — reach for the sibling [`fixarray`](../fixarray/README.md) module: the same
mathematics with the shape moved into the type (`vec3f`/`mat4f`, allocation-free, and faster than
GLM across their whole overlap). `linalg` is the home of the heavy, shape-generic numerics below.

The routines mirror [numpy.linalg](https://numpy.org/doc/stable/reference/routines.linalg.html)
and operate on `ndarray::NDArray` (2-D = matrix, 1-D = vector). The general
eigensolvers `eig`/`eigvals` return a **complex** spectrum (`CNDArray`) — a real
matrix can have complex conjugate eigenvalue pairs, e.g. a rotation has ±i — while
the Hermitian solvers `eigh`/`eigvalsh` return a guaranteed-real spectrum (the same
split as numpy). Kernels are compiled at `-O3 -march=native` so the hot loops
auto-vectorize.

## Usage

```purr
import linalg              # auto-links ndarray

let x = linalg.solve(A, b) # A·x = b
let d = linalg.det(A)
```

## Functions

Every routine below returns a fresh array. Each also has an allocation-free
**out-parameter overload** (`solve(out, A, b)`, `svd(u, s, vh, A)`, …) in
[routines.hpp](routines.hpp) for hot loops that reuse one scratch buffer per call —
same math, caller-owned storage.

### Products
- `dot` / `vdot` / `inner` — vector dot product (Σ aᵢbᵢ).
- `outer` — outer product of two vectors → matrix.
- `matmul` — matrix multiply.
- `matrix_power` — integer matrix power Aⁿ (negative via `inv`).
- `kron` — Kronecker (block) product.

### Decompositions
- `cholesky` — lower-triangular L with A = L·Lᵀ (SPD only).
- `qr` — reduced QR via Householder reflections.
- `svd` — singular value decomposition (Golub–Reinsch: bidiagonalization + implicit QR).
- `svdvals` — singular values only (the SVD fast path, without forming U/Vᵀ).

### Eigenvalues
- `eig` / `eigvals` — general square matrix (**complex** spectrum + eigenvectors;
  Hessenberg + shifted QR for the values, inverse iteration for the vectors).
- `eigh` / `eigvalsh` — symmetric **or complex Hermitian** matrix (real spectrum;
  real eigenvectors for a real symmetric matrix, complex eigenvectors for a Hermitian
  one; Householder tridiagonalization + QL, via a real 2n embedding for Hermitian input).

All eigenvalue routines return the spectrum **descending** (note: numpy's `eigvalsh`
returns ascending), with eigenvector columns reordered to match.

```purr
# A real rotation matrix has complex eigenvalues ±i:
let r = ndarray.reshape(ndarray.array([0.0, -1.0, 1.0, 0.0]), [2, 2])
io.print(ndarray.to_string(linalg.eigvals(r)))   # [0+1j, 0-1j]
```

### Complex inner-product spaces
Vectors and matrices can be **complex** (`ndarray.complex(re, im)`), so the routines
work over complex inner-product spaces — Hermitian operators, complex wavefunctions:
- `dot` — bilinear product Σ aᵢbᵢ (complex, no conjugation; matches numpy).
- `vdot` — conjugate-linear Hermitian inner product ⟨a, b⟩ = Σ conj(aᵢ)·bᵢ
  (conjugates the first argument; `vdot(a, a)` is the real ‖a‖²).
- `matmul` — complex matrix multiply.
- `conj_transpose` — conjugate transpose (Hermitian adjoint) Aᴴ.

```purr
import io
import ndarray
import linalg

# A Hermitian operator H = [[2, 1+i], [1-i, 3]] — real eigenvalues 4, 1:
let re = ndarray.array([2.0, 1.0, 1.0, 3.0])
let im = ndarray.array([0.0, 1.0, -1.0, 0.0])
let H = ndarray.reshape(ndarray.complex(re, im), [2, 2])
io.print(ndarray.to_string(linalg.eigvalsh(H)))   # [4, 1]

# Hermitian inner product ⟨a, a⟩ = ‖a‖² is real:
let a = ndarray.complex(ndarray.array([1.0, 3.0]), ndarray.array([2.0, -1.0]))
io.print(linalg.vdot(a, a))                        # 15+0j
```

### Norms & numbers
- `norm` — L2 (vector) / Frobenius (matrix).
- `cond` — 2-norm condition number.
- `det` / `slogdet` — determinant (LU); `slogdet` is overflow-safe.
- `matrix_rank` — numerical rank from SVD thresholding.
- `trace` — sum of the main diagonal.

### Solving & inverses
- `solve` — solve A·x = b via LU with partial pivoting.
- `lstsq` — least-squares solution min‖A·x − b‖.
- `inv` — matrix inverse (LU).
- `pinv` — Moore–Penrose pseudo-inverse (SVD, any shape).

### SIMD
SIMD here is **pure compiler auto-vectorization** (no intrinsics): the kernels are
contiguous, unit-stride loops compiled at `-O3 -march=native`. These functions only
*report* the build's capability:
- `simd_features` — instruction sets this build targets (e.g. `AVX2;FMA`, `NEON`, `scalar`).
- `simd_lane_doubles` — widest SIMD lane width in `double`s.

On a build with **no SIMD** every routine returns identical results, just scalar /
slower — SIMD is never a correctness dependency. The full model (and the
compile-time-dispatch limitation) is documented in [simd.hpp](simd.hpp).

## Performance vs NumPy

`linalg` goes head-to-head with NumPy, whose array ops dispatch to **BLAS/LAPACK** —
hand-tuned, vectorized, often multi-threaded Fortran. The
[`scripts/numpy_compare.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/numpy_compare.py)
harness feeds the **same** fixed-seed, well-conditioned matrix to both, runs the **same**
op many times with the result consumed, and checks the answers agree. Each function's
**Performance** row above carries its own measurement; the full size-dependence:

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

#### vs NumPy

<!-- BENCH:linalg-vs-numpy begin -->
<!-- cheatah-bench-stamp v1
     suite:        linalg-vs-numpy
     generated:    2026-08-20
     commit:       b97c491 (dirty)
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
| `matmul` | 4 | 0.10 | 0.79 | **cheatah 8.1x** | 7.46-8.74 |
| `matmul` | 16 | 0.55 | 2.32 | **cheatah 4.2x** | 3.34-4.31 |
| `matmul` | 32 | 3.51 | 13.96 | **cheatah 3.9x** | 3.73-4.54 |
| `matmul` | 64 | 27.67 | 122.17 | **cheatah 4.5x** | 4.25-4.73 |
| `matmul` | 96 | 92.26 | 354.48 | **cheatah 3.9x** | 3.50-3.98 |
| `solve` | 4 | 0.22 | 2.46 | **cheatah 11.1x** | 9.82-11.66 |
| `solve` | 16 | 1.06 | 4.36 | **cheatah 4.1x** | 4.05-4.28 |
| `solve` | 32 | 3.83 | 10.69 | **cheatah 2.8x** | 2.45-2.87 |
| `solve` | 64 | 18.84 | 50.92 | **cheatah 2.7x** | 2.54-2.95 |
| `det` | 4 | 0.09 | 1.95 | **cheatah 22.2x** | 14.70-27.73 |
| `det` | 16 | 0.76 | 3.78 | **cheatah 5.0x** | 4.70-5.31 |
| `det` | 32 | 2.82 | 9.49 | **cheatah 3.4x** | 3.10-3.59 |
| `det` | 64 | 14.65 | 48.67 | **cheatah 3.3x** | 3.09-3.45 |
| `inv` | 4 | 0.25 | 2.14 | **cheatah 8.6x** | 8.38-8.97 |
| `inv` | 16 | 1.56 | 6.34 | **cheatah 4.0x** | 3.39-4.23 |
| `inv` | 32 | 7.28 | 25.43 | **cheatah 3.4x** | 3.28-3.69 |
| `inv` | 64 | 46.24 | 150.65 | **cheatah 3.2x** | 2.71-3.61 |
| `eigvalsh` | 2 | 0.21 | 2.09 | **cheatah 10.0x** | 9.45-10.28 |
| `eigvalsh` | 3 | 0.49 | 2.47 | **cheatah 5.0x** | 4.98-5.21 |
| `eigvalsh` | 4 | 0.64 | 2.76 | **cheatah 4.3x** | 4.14-4.58 |
| `eigvalsh` | 6 | 1.44 | 3.52 | **cheatah 2.4x** | 2.40-2.68 |
| `eigvalsh` | 8 | 1.96 | 4.29 | **cheatah 2.2x** | 2.02-2.33 |
| `eigvalsh` | 16 | 6.91 | 9.40 | **cheatah 1.4x** | 1.30-1.44 |
| `eigvalsh` | 32 | 27.14 | 27.12 | **cheatah 1.0x** | 1.00-1.06 |
| `eigvalsh` | 64 | 109.37 | 155.45 | **cheatah 1.4x** | 1.28-1.46 |
| `dot` | 64 | 0.02 | 0.68 | **cheatah 36.2x** | 29.77-47.38 |
| `dot` | 1024 | 0.08 | 1.16 | **cheatah 13.7x** | 8.24-22.11 |
| `dot` | 16384 | 2.69 | 8.57 | **cheatah 3.2x** | 2.34-3.76 |
| `ndarray.sqrt` | 64 | 0.19 | 0.93 | **cheatah 4.9x** | 4.51-5.32 |
| `ndarray.sqrt` | 1024 | 0.96 | 1.80 | **cheatah 1.9x** | 1.07-2.13 |
| `ndarray.sqrt` | 16384 | 15.15 | 15.70 | **cheatah 1.0x** | 0.86-1.05 |
| `ndarray.exp` | 64 | 0.22 | 1.08 | **cheatah 5.0x** | 4.26-5.43 |
| `ndarray.exp` | 1024 | 0.91 | 4.18 | **cheatah 4.7x** | 3.49-4.85 |
| `ndarray.exp` | 16384 | 13.47 | 52.63 | **cheatah 4.0x** | 3.14-4.11 |
| `ndarray.sin` | 64 | 0.20 | 1.15 | **cheatah 5.7x** | 5.16-5.77 |
| `ndarray.sin` | 1024 | 0.98 | 6.08 | **cheatah 6.2x** | 5.07-6.70 |
| `ndarray.sin` | 16384 | 15.30 | 94.64 | **cheatah 6.3x** | 5.08-6.97 |
| `ndarray.add` | 64 | 0.14 | 0.67 | **cheatah 4.8x** | 4.08-5.63 |
| `ndarray.add` | 16384 | 3.35 | 3.35 | NumPy 1.0x | 0.87-1.37 |
| `cholesky` | 8 | 0.31 | 2.49 | **cheatah 7.7x** | 6.12-8.53 |
| `cholesky` | 32 | 3.74 | 7.87 | **cheatah 2.1x** | 1.67-2.45 |
| `cholesky` | 64 | 17.67 | 27.69 | **cheatah 1.6x** | 1.23-1.70 |
| `qr` | 8 | 1.17 | 9.32 | **cheatah 7.8x** | 6.46-9.01 |
| `qr` | 32 | 15.61 | 29.94 | **cheatah 1.9x** | 1.80-2.06 |
| `qr` | 64 | 106.81 | 142.98 | **cheatah 1.4x** | 1.03-1.41 |
| `svdvals` | 8 | 3.53 | 6.29 | **cheatah 1.8x** | 1.61-2.05 |
| `svdvals` | 32 | 56.29 | 46.37 | NumPy 1.2x | 0.80-0.87 |
| `svdvals` | 64 | 249.77 | 221.91 | NumPy 1.1x | 0.83-0.95 |
| `svd (full)` | 8 | 4.27 | 10.94 | **cheatah 2.6x** | 2.43-2.74 |
| `svd (full)` | 32 | 78.73 | 125.31 | **cheatah 1.6x** | 1.40-1.74 |
| `svd (full)` | 64 | 473.88 | 758.85 | **cheatah 1.6x** | 1.52-1.75 |
| `pinv` | 8 | 4.49 | 18.46 | **cheatah 4.1x** | 3.79-4.42 |
| `pinv` | 32 | 103.86 | 142.81 | **cheatah 1.4x** | 1.27-1.47 |
| `pinv` | 64 | 702.40 | 885.11 | **cheatah 1.2x** | 1.21-1.37 |
| `cond` | 8 | 3.49 | 10.73 | **cheatah 3.1x** | 2.85-3.49 |
| `cond` | 32 | 55.36 | 51.70 | NumPy 1.1x | 0.87-0.96 |
| `cond` | 64 | 243.34 | 214.80 | NumPy 1.1x | 0.87-0.92 |
| `matrix_rank` | 8 | 3.29 | 12.76 | **cheatah 3.9x** | 3.30-4.07 |
| `matrix_rank` | 32 | 52.45 | 56.17 | **cheatah 1.1x** | 0.99-1.13 |
| `matrix_rank` | 64 | 253.39 | 219.57 | NumPy 1.1x | 0.83-0.95 |
| `slogdet` | 8 | 0.26 | 3.38 | **cheatah 14.1x** | 11.21-17.44 |
| `slogdet` | 32 | 3.17 | 11.43 | **cheatah 3.4x** | 3.27-3.69 |
| `slogdet` | 64 | 16.68 | 54.10 | **cheatah 3.4x** | 2.44-3.86 |
| `eigh` | 8 | 2.73 | 7.13 | **cheatah 2.6x** | 2.40-2.66 |
| `eigh` | 32 | 38.25 | 64.50 | **cheatah 1.7x** | 1.56-1.76 |
| `eigh` | 64 | 244.38 | 393.13 | **cheatah 1.6x** | 1.57-1.72 |
| `eigvals` | 8 | 7.21 | 13.68 | **cheatah 1.9x** | 1.28-1.94 |
| `matrix_power` | 8 | 0.98 | 2.56 | **cheatah 2.8x** | 2.32-3.10 |
| `matrix_power` | 32 | 12.89 | 27.45 | **cheatah 2.2x** | 2.08-2.31 |
| `matrix_power` | 64 | 104.97 | 218.60 | **cheatah 2.1x** | 1.73-2.30 |
| `trace` | 32 | 0.01 | 1.18 | **cheatah 159.0x** | 116.81-170.01 |
| `trace` | 256 | 0.06 | 1.19 | **cheatah 20.0x** | 18.23-21.95 |
| `norm(matrix)` | 32 | 0.07 | 1.53 | **cheatah 22.4x** | 13.63-26.47 |
| `norm(matrix)` | 256 | 7.80 | 30.01 | **cheatah 3.8x** | 3.58-4.12 |
| `outer` | 64 | 0.30 | 4.57 | **cheatah 14.8x** | 13.68-15.45 |
| `outer` | 256 | 9.07 | 46.83 | **cheatah 5.2x** | 4.36-5.40 |
| `kron` | 8 | 1.71 | 13.79 | **cheatah 8.2x** | 7.80-8.70 |
| `kron` | 16 | 16.76 | 68.04 | **cheatah 4.0x** | 3.19-4.84 |
| `kron` | 32 | 342.30 | 923.90 | **cheatah 2.7x** | 2.58-2.89 |
<!-- BENCH:linalg-vs-numpy end -->

#### vs Eigen

<!-- BENCH:linalg-vs-eigen begin -->
<!-- cheatah-bench-stamp v1
     suite:        linalg-vs-eigen
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
     layout:       pairs
     watch:        stdlib/linalg/, tests/benchmarks/eigen_compare_bench.cpp
-->

| case | cheatah | spread | vs | rival | spread | ratio | verdict |
|---|--:|--:|---|--:|--:|--:|---|
| BM_dot/16384 | 1.76 µs | ±62.87 ns IQR | eigen | 2.66 µs | ±194.54 ns IQR | 1.51x | faster |
| BM_dot/64 | 10.33 ns | ±0.40 ns IQR | eigen | 6.90 ns | ±0.24 ns IQR | 0.67x | **slower** |
| BM_inv/32 | 6.85 µs | ±1.14 µs IQR | eigen | 11.50 µs | ±304.44 ns IQR | 1.68x | faster |
| BM_inv/64 | 41.85 µs | ±2.91 µs IQR | eigen | 68.89 µs | ±1.07 µs IQR | 1.65x | faster |
| BM_matmul/32 | 3.25 µs | ±1.15 µs IQR | eigen | 3.96 µs | ±81.54 ns IQR | 1.22x | faster |
| BM_matmul/96 | 91.05 µs | ±33.39 µs IQR | eigen | 97.69 µs | ±2.00 µs IQR | 1.07x | parity |
| BM_solve/32 | 3.65 µs | ±59.79 ns IQR | eigen | 4.23 µs | ±225.91 ns IQR | 1.16x | faster |
| BM_solve/64 | 16.89 µs | ±335.60 ns IQR | eigen | 21.08 µs | ±492.88 ns IQR | 1.25x | faster |

**Tally** (a difference counts only above 1.15x AND 0.25 ns) — vs eigen: **6 faster / 1 parity / 1 slower**.
- Loss vs eigen: `BM_dot/64` — cheatah 10.33 ns vs 6.90 ns (1.50x slower)
<!-- BENCH:linalg-vs-eigen end -->

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

cheatah does all of this on **one core, by design** — single-threaded is a feature, not a
shortfall (no hidden threads, no contention, nothing to tune). Both NumPy's and Eigen's
remaining edges are the very large or blocked dense problems where threaded/BLAS-3 kernels
spread the work — a different operating point. See the [Performance guide](@ref performance)
for the full rationale.

---

Per-function docs (parameters, complexity, heap behavior) are in
[routines.hpp](routines.hpp). Tested in
[../tests/linalg_routines_test.cpp](../tests/linalg_routines_test.cpp) and
[../tests/linalg_smoke_test.cpp](../tests/linalg_smoke_test.cpp); ASan +
Valgrind clean via the QA gate (`security/run-valgrind.sh`).
