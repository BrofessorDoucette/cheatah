# cheatah `linalg`

NumPy-style linear algebra on `ndarray`, with SIMD-accelerated contiguous kernels.

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

> **What we compared against.** vs **NumPy 1.26.4** (CPython 3.12.3, `x86_64`) on the
> **system BLAS/LAPACK**. NumPy's absolute speed — and where the crossovers land —
> depends heavily on its BLAS (reference vs OpenBLAS vs MKL) and thread count; a faster
> BLAS pushes crossovers *lower*. We pin the version so the comparison is reproducible.

The **Eigen** time and **vs Eigen** verdict are a *separate* measurement, in the native
Google Benchmark harness ([`tests/benchmarks/eigen_compare_bench.cpp`](https://github.com/BrofessorDoucette/cheatah/blob/main/tests/benchmarks/eigen_compare_bench.cpp)),
where cheatah and **Eigen 3.4** are both compiled C++ timed identically on **one
thread** — an apples-to-apples per-core comparison ("—" = not benchmarked). (Because it is
a different harness from the NumPy columns, the `Eigen ÷ cheatah` ratio is taken there, not
against the `cheatah` column on the left.)

| op | operand dimensions | cheatah | Eigen | NumPy | vs Eigen | vs NumPy |
|----|--------------------|--------:|------:|------:|--------|--------|
| *— products —* | | | | | | |
| `dot` | two 64-element vectors | 0.01 | 0.01 | 0.59 | even | **cheatah 53×** |
| `dot` | two 16384-element vectors | 1.94 | 2.52 | 7.75 | **cheatah 1.4×** | **cheatah 4.0×** |
| `matmul` | 32×32 · 32×32 | 2.58 | 3.73 | 12.9 | **cheatah 1.4×** | **cheatah 5.0×** |
| `matmul` | 96×96 · 96×96 | 71.7 | 94.0 | 315 | **cheatah 1.3×** | **cheatah 4.4×** |
| `outer` | 64-vec → 64×64 | 0.47 | 0.57 | 3.56 | **cheatah 1.2×** | **cheatah 7.6×** |
| `outer` | 256-vec → 256×256 | 8.04 | 9.04 | 47.1 | **cheatah 1.3×** | **cheatah 5.9×** |
| `kron` | 8×8 ⊗ 8×8 → 64×64 | 1.01 | — | 11.8 | — | **cheatah 12×** |
| `kron` | 32×32 ⊗ 32×32 → 1024×1024 | 325 | — | 799 | — | **cheatah 2.5×** |
| *— LU-based —* | | | | | | |
| `solve` | 32×32 matrix, 32-vector | 3.49 | 4.00 | 9.95 | **cheatah 1.2×** | **cheatah 2.8×** |
| `solve` | 64×64 matrix, 64-vector | 16.6 | 20.0 | 47.8 | **cheatah 1.2×** | **cheatah 2.9×** |
| `det` | 64×64 | 13.8 | 19.1 | 45.1 | **cheatah 1.4×** | **cheatah 3.3×** |
| `slogdet` | 64×64 | 13.9 | — | 46.1 | — | **cheatah 3.3×** |
| `inv` | 32×32 | 5.85 | 11.1 | 23.8 | **cheatah 1.75×** | **cheatah 4.1×** |
| `inv` | 64×64 | 31.5 | 65.4 | 134 | **cheatah 1.7×** | **cheatah 4.2×** |
| *— factorizations —* | | | | | | |
| `cholesky` | 64×64 | 13.0 | 10.9 | 24.2 | Eigen 1.2× | **cheatah 1.9×** |
| `qr` | 32×32 | 11.1 | 6.48 | 26.1 | Eigen 1.8× | **cheatah 2.4×** |
| `qr` | 64×64 | 62.1 | 54.1 | 124 | Eigen 1.3× | **cheatah 2.0×** |
| `svd` (full U+s+Vᵀ) | 64×64 | 364 | 375 | 652 | **cheatah 1.6×** | **cheatah 1.8×** |
| `svdvals` (values only) | 64×64 | 202 | 138 | 187 | **cheatah 1.2×** | NumPy 1.1× |
| `pinv` | 64×64 | 592 | — | 734 | — | **cheatah 1.2×** |
| `cond` | 64×64 | 206 | — | 195 | — | NumPy 1.1× |
| `matrix_rank` | 64×64 | 209 | — | 198 | — | NumPy 1.1× |
| *— eigen —* | | | | | | |
| `eigvalsh` | 2×2 | 0.14 | — | 1.93 | — | **cheatah 14×** |
| `eigvalsh` | 8×8 | 1.69 | 2.16 | 3.91 | **cheatah 1.1×** | **cheatah 2.3×** |
| `eigvalsh` | 64×64 | 99.5 | 108 | 143 | **cheatah 1.2×** | **cheatah 1.4×** |
| `eigh` (+ vectors) | 32×32 | 36.1 | 38.8 | 61.4 | **cheatah 1.1×** | **cheatah 1.7×** |
| `eigh` (+ vectors) | 64×64 | 225 | 191 | 379 | Eigen 1.1× | **cheatah 1.7×** |
| `eigvals` (general) | 8×8 | 6.25 | — | 11.7 | — | **cheatah 1.9×** |
| `matrix_power` (A³) | 64×64 | 80.0 | — | 218 | — | **cheatah 2.7×** |
| *— reductions —* | | | | | | |
| `trace` | 256×256 | 0.08 | 0.08 | 1.12 | **cheatah 1.4×** | **cheatah 15×** |
| `norm` (Frobenius) | 32×32 | 0.09 | 0.11 | 1.41 | **cheatah 1.3×** | **cheatah 16×** |
| `norm` (Frobenius) | 256×256 | 7.16 | 7.27 | 29.8 | even | **cheatah 4.2×** |

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
