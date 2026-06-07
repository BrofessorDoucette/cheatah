# cheatah `linalg`

NumPy-style linear algebra on `ndarray`, with SIMD-accelerated contiguous kernels.

The routines mirror [numpy.linalg](https://numpy.org/doc/stable/reference/routines.linalg.html)
and operate on `ndarray::NDArray` (2-D = matrix, 1-D = vector). NDArray is
double-only, so eigenvalue routines return real spectra and throw on a complex
pair. Kernels are compiled at `-O3 -march=native` so the hot loops auto-vectorize.

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
- `eig` / `eigvals` — general square matrix (real spectrum; Hessenberg + shifted QR).
- `eigh` / `eigvalsh` — symmetric matrix (cyclic Jacobi).

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
- `simd_features` — instruction sets this build targets (e.g. `AVX2;FMA`, `NEON`, `scalar`).
- `simd_lane_doubles` — widest SIMD lane width in `double`s.

---

Per-function docs (parameters, complexity, heap behavior) are in
[routines.hpp](routines.hpp). Tested in
[../tests/linalg_routines_test.cpp](../tests/linalg_routines_test.cpp) and
[../../tests/linalg/smoke_test.cpp](../../tests/linalg/smoke_test.cpp); ASan +
Valgrind clean via the QA gate (`security/run-valgrind.sh`).
