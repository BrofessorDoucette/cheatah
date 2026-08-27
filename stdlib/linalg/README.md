# cheatah `linalg`

NumPy-style linear algebra on `ndarray`, with SIMD-accelerated contiguous kernels.

For the small-and-hot regime — a 3-D direction, a 4×4 transform built and consumed millions of
times a second — reach for the sibling [`fixarray`](../fixarray/README.md) module: the same
mathematics with the shape moved into the type (`vec3f`/`mat4f`, allocation-free, and never slower
than GLM anywhere the two overlap). `linalg` is the home of the heavy, shape-generic numerics below.

The routines mirror [numpy.linalg](https://numpy.org/doc/stable/reference/routines.linalg.html)
and operate on `ndarray::NDArray` (2-D = matrix, 1-D = vector). The general eigensolvers
`eig`/`eigvals` return a **complex** spectrum (`CNDArray`) — a real matrix can have complex
conjugate eigenvalue pairs, e.g. a rotation has ±i — while the Hermitian solvers
`eigh`/`eigvalsh` return a guaranteed-real spectrum, the same split as numpy.

## Usage

```purr
import ndarray
import linalg              # links ndarray for you

let A = ndarray.reshape(ndarray.array([4.0, 1.0, 1.0, 3.0]), [2, 2])
let b = ndarray.array([1.0, 2.0])
let x = linalg.solve(A, b) # A·x = b
let d = linalg.det(A)
```

## Functions

Every routine below returns a fresh result. Each also has an **out-parameter
overload** (`solve(out, A, b)`, `svd(u, s, vh, A)`, …) in [routines.hpp](routines.hpp)
that writes into caller-owned storage: the products and reductions stay off the heap on
contiguous operands, and the factorizations keep their private scratch.

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

All eigenvalue routines return the spectrum **descending** — numpy's `eigvalsh` returns
ascending — with eigenvector columns reordered to match.

```purr
import io
import ndarray
import linalg

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
SIMD here is **pure compiler auto-vectorization** (no intrinsics): contiguous, unit-stride
loops compiled at `-O3 -march=native`. These functions only *report* the build's capability:
- `simd_features` — instruction sets this build targets (e.g. `AVX2;FMA`, `NEON`, `scalar`).
- `simd_lane_doubles` — widest SIMD lane width in `double`s.

A build with **no SIMD** returns identical results, just slower — SIMD is never a correctness
dependency. The model, and its compile-time-dispatch limitation, is in [simd.hpp](simd.hpp).

## Performance

`linalg` goes head-to-head with NumPy, whose array ops dispatch to **BLAS/LAPACK** —
hand-tuned, vectorized, often multi-threaded Fortran — and, in a separate single-core
harness, with **Eigen 3.4**. Each function's **Performance** row on this page points at
the generated tables; the full size-dependence, and the BLAS caveat that governs how to
read it, is on the [linalg benchmarks](BENCHMARKS.md) page.

cheatah does all of this on **one core, by design** — no hidden threads, nothing to tune.
NumPy's and Eigen's remaining edges are the large blocked problems where threaded BLAS-3
kernels spread the work; the [Performance guide](@ref performance) has the rationale.

---

Per-function docs (parameters, complexity, heap behavior) are in
[routines.hpp](routines.hpp). Tested in
[../tests/linalg_routines_test.cpp](../tests/linalg_routines_test.cpp) and
[../tests/linalg_smoke_test.cpp](../tests/linalg_smoke_test.cpp); ASan +
Valgrind clean via the QA gate (`security/run-valgrind.sh`).
