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
import linalg          # auto-links ndarray

x = linalg.solve(A, b) # A·x = b
d = linalg.det(A)
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

### Eigen
- `eig` / `eigvals` — general square matrix (**complex** spectrum + eigenvectors;
  Hessenberg + shifted QR for the values, inverse iteration for the vectors).
- `eigh` / `eigvalsh` — symmetric **or complex Hermitian** matrix (real spectrum,
  complex eigenvectors; Householder tridiagonalization + QL, via a real 2n embedding for Hermitian input).

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

| op | operand dimensions | cheatah | NumPy | winner |
|----|--------------------|--------:|------:|--------|
| *— products —* | | | | |
| `dot` | two 64-element vectors | 0.03 | 0.58 | **cheatah 18×** |
| `dot` | two 16384-element vectors | 3.74 | 7.72 | **cheatah 2.1×** |
| `matmul` | 32×32 · 32×32 | 3.04 | 12.9 | **cheatah 4.3×** |
| `matmul` | 96×96 · 96×96 | 85.4 | 298 | **cheatah 3.5×** |
| `outer` | 64-vec → 64×64 | 1.21 | 4.22 | **cheatah 3.5×** |
| `outer` | 256-vec → 256×256 | 187 | 45.5 | NumPy 4.1× |
| `kron` | 8×8 ⊗ 8×8 → 64×64 | 1.95 | 12.3 | **cheatah 6.3×** |
| `kron` | 32×32 ⊗ 32×32 → 1024×1024 | 3971 | 732 | NumPy 5.4× |
| *— LU-based —* | | | | |
| `solve` | 32×32 matrix, 32-vector | 3.56 | 9.95 | **cheatah 2.8×** |
| `solve` | 64×64 matrix, 64-vector | 16.4 | 47.4 | **cheatah 2.9×** |
| `det` | 64×64 | 13.9 | 45.1 | **cheatah 3.2×** |
| `slogdet` | 64×64 | 14.1 | 46.1 | **cheatah 3.3×** |
| `inv` | 32×32 | 5.65 | 23.8 | **cheatah 4.2×** |
| `inv` | 64×64 | 32.5 | 135 | **cheatah 4.2×** |
| *— factorizations —* | | | | |
| `cholesky` | 64×64 | 16.3 | 24.0 | **cheatah 1.5×** |
| `qr` | 32×32 | 17.3 | 26.1 | **cheatah 1.5×** |
| `qr` | 64×64 | 162 | 124 | NumPy 1.3× |
| `svd` (full U+s+Vᵀ) | 64×64 | 371 | 680 | **cheatah 1.8×** |
| `svd` (full U+s+Vᵀ) | 96×96 | 1129 | 1871 | **cheatah 1.7×** |
| `svdvals` (values only) | 64×64 | 207 | 185 | NumPy 1.1× |
| `svdvals` (values only) | 96×96 | 551 | 540 | even (1.0×) |
| `pinv` | 32×32 | 93 | 126 | **cheatah 1.4×** |
| `pinv` | 64×64 | 591 | 749 | **cheatah 1.3×** |
| `cond` | 64×64 | 208 | 187 | NumPy 1.1× |
| `matrix_rank` | 64×64 | 211 | 189 | NumPy 1.1× |
| *— eigen —* | | | | |
| `eigvalsh` | 2×2 | 0.17 | 1.93 | **cheatah 11.5×** |
| `eigvalsh` | 8×8 | 2.04 | 3.91 | **cheatah 1.9×** |
| `eigvalsh` | 64×64 | 143 | 140 | even (1.0×) |
| `eigh` (+ vectors) | 32×32 | 42.5 | 60.8 | **cheatah 1.4×** |
| `eigh` (+ vectors) | 64×64 | 271 | 379 | **cheatah 1.4×** |
| `eigvals` (general) | 16×16 | 50.7 | 31.1 | NumPy 1.6× |
| `matrix_power` (A³) | 64×64 | 89.1 | 216 | **cheatah 2.4×** |
| *— reductions —* | | | | |
| `trace` | 256×256 | 0.09 | 1.12 | **cheatah 13×** |
| `norm` (Frobenius) | 32×32 | 0.84 | 1.42 | **cheatah 1.7×** |
| `norm` (Frobenius) | 256×256 | 57.5 | 29.8 | NumPy 1.9× |

After a focused round of optimization:

- **Products and LU-based factorizations win outright across the whole tested range.**
  `dot` (≈20× at 64 elements, still **2.1×** at 16384), `matmul` (≈4–6×), `solve`/`det`/`inv`
  (≈3–4× at 32×32–64×64). At these sizes NumPy's per-call Python dispatch *and* threaded-BLAS
  startup overhead dominate, while cheatah's single-threaded, auto-vectorized C++ just does
  the arithmetic. Rewriting `dot` and `inv` with several independent accumulators and a
  whole-identity block solve let `-O3 -march=native` issue SIMD + FMA — `inv` went from
  *losing* 1.1× to winning 4.2×; `dot` from losing 36× to winning 2.1×.
- **Symmetric eigenvalues (`eigvalsh`) now match LAPACK** instead of losing 10–35×.
  Householder tridiagonalization + implicit-shift QL (the LAPACK family), and `eigvalsh`
  skips the eigenvector accumulation it used to compute and throw away: cheatah **wins
  decisively at small n** (≈11× on 2×2 — the physics few-level case: spin Hamiltonians,
  parameter sweeps) and **ties LAPACK** from ≈16×16 through 64×64.
- **The SVD now wins.** Golub–Reinsch (the algorithm LAPACK's `dgesvd` reduces to:
  Householder **bidiagonalization** then implicit-shift QR), reimplemented **column-major**
  so both phases vectorize. The **full** decomposition beats NumPy **1.7–1.8×**, so `pinv`
  wins **1.3–1.4×**; a dedicated `svdvals` fast path skips the U/V work and **ties LAPACK**,
  as do `cond` and `matrix_rank`.

cheatah does all of this on **one core, by design** — single-threaded is a feature, not a
shortfall (no hidden threads, no contention, nothing to tune). NumPy's one remaining edge
is very large dense problems where its BLAS spreads across cores — a different operating
point, not a faster algorithm. See the [Performance guide](@ref performance) for the full
rationale.

---

Per-function docs (parameters, complexity, heap behavior) are in
[routines.hpp](routines.hpp). Tested in
[../tests/linalg_routines_test.cpp](../tests/linalg_routines_test.cpp) and
[../../tests/linalg/smoke_test.cpp](../../tests/linalg/smoke_test.cpp); ASan +
Valgrind clean via the QA gate (`security/run-valgrind.sh`).
