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
- `svd` — singular value decomposition (one-sided Jacobi).

### Eigen
- `eig` / `eigvals` — general square matrix (**complex** spectrum + eigenvectors;
  Hessenberg + shifted QR for the values, inverse iteration for the vectors).
- `eigh` / `eigvalsh` — symmetric **or complex Hermitian** matrix (real spectrum,
  complex eigenvectors; cyclic Jacobi, via a real 2n embedding for Hermitian input).

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
let H = ndarray.reshape(
    ndarray.complex(ndarray.array([2.0, 1.0, 1.0, 3.0]),
                    ndarray.array([0.0, 1.0, -1.0, 0.0])), [2, 2])
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

---

Per-function docs (parameters, complexity, heap behavior) are in
[routines.hpp](routines.hpp). Tested in
[../tests/linalg_routines_test.cpp](../tests/linalg_routines_test.cpp) and
[../../tests/linalg/smoke_test.cpp](../../tests/linalg/smoke_test.cpp); ASan +
Valgrind clean via the QA gate (`security/run-valgrind.sh`).
