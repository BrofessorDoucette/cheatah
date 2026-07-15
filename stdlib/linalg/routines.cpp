// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "routines.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <vector>

// Dense linear-algebra routines on ndarray::NDArray. Algorithms reimplemented from
// standard numerical methods (LU w/ partial pivoting, Cholesky, Householder QR,
// Golub–Reinsch SVD (bidiagonalization + implicit QR), Householder-tridiagonal + QL symmetric eigen, Hessenberg+shifted-QR for
// the general real spectrum). Hot loops are contiguous so -O3 -march=native
// auto-vectorizes them (SIMD). The matrices are real (double) but the general
// eigensolvers return a COMPLEX spectrum (CNDArray) — a real matrix can have
// complex conjugate eigenvalue pairs — built from the real arithmetic below.
namespace cheatah::linalg {

/// @cond INTERNAL
using ndarray::NDArray;
/// @endcond

namespace {

// ---- extract / build contiguous row-major matrices & vectors ----
//
// IMPORTANT: read the shared buffer directly. The element accessor `a.at({i, j})`
// constructs a `std::vector` index *per call* (one heap allocation per element), so
// the old extractors did rows*cols allocations just to read a matrix. These pack
// C-order with a flat `copy_n` when the array is already contiguous (the common
// case — a freshly built matrix/vector), and a direct strided walk otherwise. No
// per-element allocation either way.

// Pack `a`'s elements into `out` (size a.size()) in C-order via direct buffer
// indexing. Used only for the non-contiguous (view/broadcast/permuted) fallback.
template <ndarray::Field T>
void pack_corder(const ndarray::basic_ndarray<T>& a, T* out) {
    const T* base = a.buffer()->data();
    const auto& shp = a.shape();
    const auto& st = a.strides();
    const std::size_t nd = shp.size();
    const std::ptrdiff_t off0 = static_cast<std::ptrdiff_t>(a.offset());
    std::vector<std::size_t> idx(nd, 0);
    const std::size_t total = a.size();
    for (std::size_t lin = 0; lin < total; ++lin) {
        std::ptrdiff_t off = off0;
        for (std::size_t d = 0; d < nd; ++d)
            off += static_cast<std::ptrdiff_t>(idx[d]) * st[d];
        out[lin] = base[static_cast<std::size_t>(off)];
        for (std::size_t d = nd; d-- > 0;) {  // C-order increment
            if (++idx[d] < shp[d]) break;
            idx[d] = 0;
        }
    }
}

// A read-only contiguous C-order pointer to `a`'s data. Zero-copy when `a` is
// already contiguous (returns straight into its buffer); otherwise packs into
// `scratch`. Use for routines that only READ their operands (the products).
template <ndarray::Field T>
const T* contig(const ndarray::basic_ndarray<T>& a, std::vector<T>& scratch) {
    if (ndarray::is_contiguous(a)) return a.buffer()->data() + a.offset();
    scratch.resize(a.size());
    pack_corder(a, scratch.data());
    return scratch.data();
}

std::vector<double> as_matrix(const NDArray& a, std::size_t& rows, std::size_t& cols) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    rows = a.shape()[0];
    cols = a.shape()[1];
    std::vector<double> m(rows * cols);
    if (ndarray::is_contiguous(a))
        std::copy_n(a.buffer()->data() + a.offset(), rows * cols, m.data());
    else
        pack_corder(a, m.data());
    return m;
}
// Validate that `a` is vector-shaped (1-D, or a 2-D row/column) and return its length.
template <ndarray::Field T>
std::size_t vector_len(const ndarray::basic_ndarray<T>& a) {
    if (a.ndim() == 1) return a.shape()[0];
    if (a.ndim() == 2 && (a.shape()[0] == 1 || a.shape()[1] == 1)) return a.size();
    throw std::runtime_error("linalg: expected a 1-D vector");
}
std::vector<double> as_vector(const NDArray& a, std::size_t& n) {
    n = vector_len(a);
    std::vector<double> v(n);
    if (ndarray::is_contiguous(a))
        std::copy_n(a.buffer()->data() + a.offset(), n, v.data());
    else
        pack_corder(a, v.data());
    return v;
}
// Wrap an already-computed buffer as a contiguous NDArray WITHOUT the throwaway
// zero-init that `NDArray(shape)` would do (it value-fills `product(shape)` elements
// that we then immediately overwrite — a full wasted pass, ruinous for big results
// like `outer`). Build straight from the buffer + C-order strides instead.
// Zero-copy: the result is ALREADY in the ndarray storage type (see ndarray::buffer_t),
// so move its buffer straight in — no element copy, no second large allocation. Use this
// for memory-bound results (outer, transpose, kron) where the result is as big as the
// work and an extra copy would dominate (and, for >128 KiB results, trip glibc's mmap
// threshold so the copy's fresh pages fault in one by one).
template <typename T>
ndarray::basic_ndarray<T> wrap_buffer(std::vector<std::size_t> shape, ndarray::buffer_t<T> data) {
    auto strides = ndarray::detail::contiguous_strides(shape);
    auto buf = std::make_shared<ndarray::buffer_t<T>>(std::move(data));
    return ndarray::basic_ndarray<T>(std::move(buf), std::move(shape), std::move(strides), 0);
}
// Plain-std::vector result: one bulk copy into the ndarray storage type. resize
// (default-init: no zero pass) + std::copy keeps libstdc++'s memmove fast path, so it is a
// single contiguous pass — negligible next to the O(n³) work of the routines that use it
// (matmul, inv, the SVD/eig family). (vector::assign through the default-init allocator
// would instead force an element-by-element copy, which is much slower.)
template <typename T>
ndarray::basic_ndarray<T> wrap_buffer(std::vector<std::size_t> shape, std::vector<T> data) {
    ndarray::buffer_t<T> buf;
    buf.resize(data.size());
    std::copy(data.begin(), data.end(), buf.begin());
    return wrap_buffer<T>(std::move(shape), std::move(buf));
}
NDArray make_matrix(std::size_t rows, std::size_t cols, std::vector<double> data) {
    return wrap_buffer<double>({rows, cols}, std::move(data));
}
NDArray make_vector(std::vector<double> data) {
    const std::size_t n = data.size();
    return wrap_buffer<double>({n}, std::move(data));
}
CNDArray make_cvector(std::vector<Cplx> data) {
    const std::size_t n = data.size();
    return wrap_buffer<Cplx>({n}, std::move(data));
}
CNDArray make_cmatrix(std::size_t rows, std::size_t cols, std::vector<Cplx> data) {
    return wrap_buffer<Cplx>({rows, cols}, std::move(data));
}
// Promote a freshly-built (contiguous, offset-0) real result to complex (imag 0).
CNDArray to_complex(const NDArray& a) {
    const auto& src = *a.buffer();
    return wrap_buffer<Cplx>(a.shape(), std::vector<Cplx>(src.begin(), src.end()));
}
// Descending order for a complex spectrum: by real part, then imaginary part.
bool cgreater(const Cplx& x, const Cplx& y) {
    if (x.real() != y.real()) return x.real() > y.real();
    return x.imag() > y.imag();
}
// ---- extract complex matrices & vectors (mirror as_matrix / as_vector) ----
std::vector<Cplx> as_cmatrix(const CNDArray& a, std::size_t& rows, std::size_t& cols) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    rows = a.shape()[0];
    cols = a.shape()[1];
    std::vector<Cplx> m(rows * cols);
    if (ndarray::is_contiguous(a))
        std::copy_n(a.buffer()->data() + a.offset(), rows * cols, m.data());
    else
        pack_corder(a, m.data());
    return m;
}
void require_square(std::size_t r, std::size_t c) {
    if (r != c) throw std::runtime_error("linalg: expected a square matrix");
}

// ---- LU decomposition with partial pivoting (in place on a copy) ----
struct LU {
    std::vector<double> a;  // L (below diag, unit) + U (diag/above), row-major n×n
    std::vector<std::size_t> piv;
    double sign;
    std::size_t n;
    bool singular;
};
LU lu_decompose(std::vector<double> a, std::size_t n) {
    std::vector<std::size_t> piv(n);
    std::vector<double> vv(n);
    double sign = 1.0;
    bool singular = false;
    for (std::size_t i = 0; i < n; ++i) {
        double big = 0.0;
        for (std::size_t j = 0; j < n; ++j) big = std::max(big, std::fabs(a[i * n + j]));
        if (big == 0.0) { singular = true; big = 1.0; }
        vv[i] = 1.0 / big;
    }
    for (std::size_t k = 0; k < n; ++k) {
        double big = 0.0;
        std::size_t imax = k;
        for (std::size_t i = k; i < n; ++i) {
            const double t = vv[i] * std::fabs(a[i * n + k]);
            if (t > big) { big = t; imax = i; }
        }
        if (k != imax) {
            for (std::size_t j = 0; j < n; ++j) std::swap(a[imax * n + j], a[k * n + j]);
            sign = -sign;
            vv[imax] = vv[k];
        }
        piv[k] = imax;
        if (a[k * n + k] == 0.0) { a[k * n + k] = 1e-300; singular = true; }
        for (std::size_t i = k + 1; i < n; ++i) {
            const double f = a[i * n + k] / a[k * n + k];
            a[i * n + k] = f;
            for (std::size_t j = k + 1; j < n; ++j) a[i * n + j] -= f * a[k * n + j];
        }
    }
    return {std::move(a), std::move(piv), sign, n, singular};
}
void lu_solve(const LU& lu, std::vector<double>& b) {
    const std::size_t n = lu.n;
    for (std::size_t k = 0; k < n; ++k) std::swap(b[k], b[lu.piv[k]]);
    for (std::size_t i = 0; i < n; ++i) {  // forward (unit L)
        double s = b[i];
        for (std::size_t j = 0; j < i; ++j) s -= lu.a[i * n + j] * b[j];
        b[i] = s;
    }
    for (std::size_t i = n; i-- > 0;) {  // back (U)
        double s = b[i];
        for (std::size_t j = i + 1; j < n; ++j) s -= lu.a[i * n + j] * b[j];
        b[i] = s / lu.a[i * n + i];
    }
}

// ---- Golub–Reinsch SVD: A(m×n) = U(m×n) diag(w) V(n×n)ᵀ, requires m ≥ n ----
struct SVDc {
    std::vector<double> u, w, v;
    std::size_t m, n;
};
// Overflow-safe √(a²+b²) for the QR sweeps. std::hypot is correctly-rounded and several
// times slower; called once per Givens rotation (O(n²) of them) it dominated the
// values-only SVD. This EISPACK form is plenty accurate and much faster.
inline double pythag(double a, double b) {
    const double aa = std::fabs(a), ab = std::fabs(b);
    if (aa > ab) { const double r = ab / aa; return aa * std::sqrt(1.0 + r * r); }
    if (ab == 0.0) return 0.0;
    const double r = aa / ab;
    return ab * std::sqrt(1.0 + r * r);
}
// The world-standard dense SVD (what LAPACK's dgesvd reduces to): Householder
// bidiagonalization to an upper-bidiagonal B = Uᵦᵀ A Vᵦ, then diagonalization of B by
// implicit-shift QR, accumulating the orthogonal factors. One reduction plus a
// quadratically-converging QR sweep — vastly fewer flops than one-sided Jacobi's
// repeated full passes. On input `a` is m×n row-major; on output it holds U (m×n).
SVDc svd_golub_reinsch(std::vector<double> a_rm, std::size_t m, std::size_t n,
                       bool want_uv = true) {
    // When @p want_uv is false only the singular values are produced: the U/V
    // accumulation and the (dominant) U/V Givens rotations in the QR sweep are skipped
    // — the same values-only fast path NumPy's `svd(compute_uv=False)` / `svdvals` take,
    // and what `cond`/`matrix_rank` need.
    // Work entirely COLUMN-MAJOR: U(r,c) = uc[c*m + r], V(r,c) = vc[c*n + r]. The bulk
    // of Golub–Reinsch is the length-m LEFT Householder reflectors — the column
    // reductions and their trailing-column updates. Column-major makes those unit-stride
    // so -O3 -march=native vectorizes them (FMA over contiguous columns); in row-major
    // they were stride-n and ran scalar, which is what left the bare SVD behind LAPACK.
    // The QR sweep (rotating whole U/V columns) is contiguous for the same reason.
    // Input arrives row-major; transpose it in once; uc holds U on output.
    std::vector<double> uc(n * m), vc(n * n, 0.0), w(n, 0.0), rv1(n, 0.0), tbuf(m, 0.0);
    for (std::size_t r = 0; r < m; ++r)
        for (std::size_t c = 0; c < n; ++c) uc[c * m + r] = a_rm[r * n + c];
    auto sign = [](double x, double s) { return s >= 0.0 ? std::fabs(x) : -std::fabs(x); };
    // `g` and `scale` carry across iterations: the super-diagonal rv1[i] is the previous
    // row-reflector's `scale * g`.
    double g = 0.0, scale = 0.0, anorm = 0.0;

    // --- Householder reduction to bidiagonal form (diagonal w, super-diagonal rv1) ---
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t l = i + 1;
        rv1[i] = scale * g;
        g = 0.0; scale = 0.0;
        double s = 0.0;
        double* Ui = &uc[i * m];                          // column i — contiguous
        for (std::size_t k = i; k < m; ++k) scale += std::fabs(Ui[k]);
        if (scale != 0.0) {                               // left (column) reflector -> w[i]
            for (std::size_t k = i; k < m; ++k) { Ui[k] /= scale; s += Ui[k] * Ui[k]; }
            double f = Ui[i];
            g = -sign(std::sqrt(s), f);
            const double h = f * g - s;
            Ui[i] = f - g;
            for (std::size_t j = l; j < n; ++j) {         // apply to trailing columns
                double* Uj = &uc[j * m];
                double sum = 0.0;
                for (std::size_t k = i; k < m; ++k) sum += Ui[k] * Uj[k];   // contiguous → SIMD
                const double fr = sum / h;
                for (std::size_t k = i; k < m; ++k) Uj[k] += fr * Ui[k];     // contiguous → SIMD
            }
            for (std::size_t k = i; k < m; ++k) Ui[k] *= scale;
        }
        w[i] = scale * g;
        g = 0.0; scale = 0.0; s = 0.0;
        // right (row) reflector over columns l..n — length n, the minor half (strided)
        if (l < n) {
            for (std::size_t k = l; k < n; ++k) scale += std::fabs(uc[k * m + i]);
            if (scale != 0.0) {
                for (std::size_t k = l; k < n; ++k) { uc[k * m + i] /= scale; s += uc[k * m + i] * uc[k * m + i]; }
                double f = uc[l * m + i];
                g = -sign(std::sqrt(s), f);
                const double h = f * g - s;
                uc[l * m + i] = f - g;
                for (std::size_t k = l; k < n; ++k) rv1[k] = uc[k * m + i] / h;
                // Trailing update A(l:m, l:n) += (A·u)·rv1ᵀ, done COLUMN-by-column so the
                // inner loops sweep contiguous rows of a column (vectorize) — the naive
                // row-by-row form strode across columns (stride m) and ran scalar.
                for (std::size_t j = l; j < m; ++j) tbuf[j] = 0.0;
                for (std::size_t k = l; k < n; ++k) {           // t[j] = Σ_k A(j,k)·u[k]
                    const double uk = uc[k * m + i];
                    const double* Ck = &uc[k * m];
                    for (std::size_t j = l; j < m; ++j) tbuf[j] += Ck[j] * uk;
                }
                for (std::size_t k = l; k < n; ++k) {           // A(j,k) += t[j]·rv1[k]
                    const double r = rv1[k];
                    double* Ck = &uc[k * m];
                    for (std::size_t j = l; j < m; ++j) Ck[j] += tbuf[j] * r;
                }
                for (std::size_t k = l; k < n; ++k) uc[k * m + i] *= scale;
            }
        }
        anorm = std::max(anorm, std::fabs(w[i]) + std::fabs(rv1[i]));
    }

    // --- accumulate the right-hand transformations into V (column-major vc) ---
    if (want_uv)
    for (std::size_t i = n; i-- > 0;) {
        const std::size_t l = i + 1;
        if (l < n) {
            if (g != 0.0) {
                for (std::size_t j = l; j < n; ++j)            // V(j,i); double division guards overflow
                    vc[i * n + j] = (uc[j * m + i] / uc[l * m + i]) / g;
                for (std::size_t j = l; j < n; ++j) {
                    double sum = 0.0;
                    for (std::size_t k = l; k < n; ++k) sum += uc[k * m + i] * vc[j * n + k];
                    for (std::size_t k = l; k < n; ++k) vc[j * n + k] += sum * vc[i * n + k];
                }
            }
            for (std::size_t j = l; j < n; ++j) { vc[j * n + i] = 0.0; vc[i * n + j] = 0.0; }
        }
        vc[i * n + i] = 1.0;
        g = rv1[i];
    }

    // --- accumulate the left-hand transformations into U (held in uc) ---
    if (want_uv)
    for (std::size_t i = n; i-- > 0;) {   // min(m,n) == n since m >= n
        const std::size_t l = i + 1;
        g = w[i];
        for (std::size_t j = l; j < n; ++j) uc[j * m + i] = 0.0;
        double* Ui = &uc[i * m];
        if (g != 0.0) {
            g = 1.0 / g;
            for (std::size_t j = l; j < n; ++j) {
                double* Uj = &uc[j * m];
                double sum = 0.0;
                for (std::size_t k = l; k < m; ++k) sum += Ui[k] * Uj[k];   // contiguous → SIMD
                const double f = (sum / Ui[i]) * g;
                for (std::size_t k = i; k < m; ++k) Uj[k] += f * Ui[k];      // contiguous → SIMD
            }
            for (std::size_t k = i; k < m; ++k) Ui[k] *= g;
        } else {
            for (std::size_t k = i; k < m; ++k) Ui[k] = 0.0;
        }
        Ui[i] += 1.0;
    }

    // U (uc) and V (vc) are already column-major, so the QR sweep's whole-column
    // rotations below are contiguous and vectorizable — no repacking needed.
    // --- diagonalize the bidiagonal form: implicit-shift QR with deflation ---
    const double eps = std::numeric_limits<double>::epsilon();
    for (std::size_t k = n; k-- > 0;) {
        for (int its = 0; its < 60; ++its) {
            bool flag = true;
            std::size_t l = k, nm = 0;
            while (true) {                 // find a negligible super-diagonal to split at
                if (l == 0) { flag = false; break; }     // rv1[0] is structurally 0
                if (std::fabs(rv1[l]) <= eps * anorm) { flag = false; break; }
                nm = l - 1;
                if (std::fabs(w[nm]) <= eps * anorm) break;
                --l;
            }
            if (flag) {                    // cancel rv1[l] via Givens rotations in U
                double c = 0.0, s = 1.0;
                for (std::size_t i = l; i <= k; ++i) {
                    double f = s * rv1[i];
                    rv1[i] = c * rv1[i];
                    if (std::fabs(f) <= eps * anorm) break;
                    double gg = w[i];
                    double h = pythag(f, gg);
                    w[i] = h; h = 1.0 / h;
                    c = gg * h; s = -f * h;
                    if (want_uv) {
                        double* Unm = &uc[nm * m];
                        double* Ui = &uc[i * m];
                        for (std::size_t j = 0; j < m; ++j) {
                            const double y = Unm[j], z = Ui[j];
                            Unm[j] = y * c + z * s;
                            Ui[j] = z * c - y * s;
                        }
                    }
                }
            }
            double z = w[k];
            if (l == k) {                  // converged: make the singular value non-negative
                if (z < 0.0) {
                    w[k] = -z;
                    if (want_uv) { double* Vk = &vc[k * n]; for (std::size_t j = 0; j < n; ++j) Vk[j] = -Vk[j]; }
                }
                break;
            }
            if (its == 59) throw std::runtime_error("linalg: SVD did not converge");
            double x = w[l];
            nm = k - 1;
            double y = w[nm], gg = rv1[nm], h = rv1[k];
            double f = ((y - z) * (y + z) + (gg - h) * (gg + h)) / (2.0 * h * y);
            gg = pythag(f, 1.0);
            f = ((x - z) * (x + z) + h * ((y / (f + sign(gg, f))) - h)) / x;
            double c = 1.0, s = 1.0;
            for (std::size_t j = l; j <= nm; ++j) {  // QR sweep: chase the bulge
                const std::size_t i = j + 1;
                gg = rv1[i]; y = w[i]; h = s * gg; gg = c * gg;
                z = pythag(f, h);
                rv1[j] = z; c = f / z; s = h / z;
                f = x * c + gg * s; gg = gg * c - x * s; h = y * s; y *= c;
                if (want_uv) {
                    double* Vj = &vc[j * n];
                    double* Vi = &vc[i * n];
                    for (std::size_t jj = 0; jj < n; ++jj) {  // rotate V columns j, i (contiguous)
                        const double vx = Vj[jj], vz = Vi[jj];
                        Vj[jj] = vx * c + vz * s;
                        Vi[jj] = vz * c - vx * s;
                    }
                }
                z = pythag(f, h);
                w[j] = z;
                if (z != 0.0) { z = 1.0 / z; c = f * z; s = h * z; }
                f = c * gg + s * y; x = c * y - s * gg;
                if (want_uv) {
                    double* Uj = &uc[j * m];
                    double* Ui = &uc[i * m];
                    for (std::size_t jj = 0; jj < m; ++jj) {  // rotate U columns j, i (contiguous)
                        const double uy = Uj[jj], uz = Ui[jj];
                        Uj[jj] = uy * c + uz * s;
                        Ui[jj] = uz * c - uy * s;
                    }
                }
            }
            rv1[l] = 0.0; rv1[k] = f; w[k] = x;
        }
    }

    // singular values come out non-negative but unordered — sort descending, carrying
    // the matching columns of U and V (read straight from the column-major buffers).
    std::vector<std::size_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](std::size_t x, std::size_t y) { return w[x] > w[y]; });
    SVDc out{std::vector<double>(want_uv ? m * n : 0), std::vector<double>(n),
             std::vector<double>(want_uv ? n * n : 0), m, n};
    for (std::size_t j = 0; j < n; ++j) out.w[j] = w[idx[j]];
    if (want_uv)
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t src = idx[j];
            const double* Uc = &uc[src * m];
            for (std::size_t i = 0; i < m; ++i) out.u[i * n + j] = Uc[i];
            const double* Vc = &vc[src * n];
            for (std::size_t i = 0; i < n; ++i) out.v[i * n + j] = Vc[i];
        }
    return out;
}

// ---- real symmetric eigensolver: Householder tridiagonalization (tred2) + ----
// ---- implicit-shift QL (tql2). ------------------------------------------------
// The O(n³) method LAPACK uses (one reduction + a QL sweep that converges in O(n)
// rotations), far cheaper than cyclic Jacobi's repeated full-matrix sweeps. `a` is a
// row-major n×n matrix ASSUMED symmetric (only the working triangle is used). Returns
// eigenvalues DESCENDING in `values`, with the matching orthonormal eigenvector as
// column j of the row-major `vectors` (vectors[i*n+j] = component i of eigenvector j).
void symmetric_eig(std::vector<double> z, std::size_t n, std::vector<double>& values,
                   std::vector<double>& vectors, bool want_vectors = true) {
    values.assign(n, 0.0);
    vectors.assign(want_vectors ? n * n : 0, 0.0);
    if (n == 0) return;
    if (n == 1) { values[0] = z[0]; if (want_vectors) vectors[0] = 1.0; return; }

    std::vector<double> d(n, 0.0), e(n, 0.0);

    // --- tred2: reduce symmetric z -> tridiagonal (d diagonal, e subdiagonal),
    //     leaving the accumulated orthogonal transform in z. ---
    for (std::size_t i = n - 1; i >= 1; --i) {
        const std::size_t l = i - 1;
        double h = 0.0;
        if (l > 0) {
            double scale = 0.0;
            for (std::size_t k = 0; k <= l; ++k) scale += std::fabs(z[i * n + k]);
            if (scale == 0.0) {
                e[i] = z[i * n + l];
            } else {
                for (std::size_t k = 0; k <= l; ++k) {
                    z[i * n + k] /= scale;
                    h += z[i * n + k] * z[i * n + k];
                }
                double f = z[i * n + l];
                double g = (f >= 0.0) ? -std::sqrt(h) : std::sqrt(h);
                e[i] = scale * g;
                h -= f * g;
                z[i * n + l] = f - g;
                // The active block [0..l]×[0..l] is kept FULL-symmetric (the rank-2
                // update below writes both triangles), so the matrix–vector product
                // p = A·u is a single contiguous, vectorizing row·u dot — no
                // column-stride walk. u is the Householder vector (row i). 2× the
                // update flops vs the packed form, but both phases now hit SIMD.
                f = 0.0;
                const double* ui = &z[i * n];   // Householder vector u (= row i)
                for (std::size_t j = 0; j <= l; ++j) {
                    z[j * n + i] = z[i * n + j] / h;   // store u/h in column i (for Q)
                    const double* zj = &z[j * n];
                    // full row · u, four independent accumulators so it vectorizes
                    // (a single running sum is FMA-latency-bound — the dot mistake).
                    double g0 = 0, g1 = 0, g2 = 0, g3 = 0;
                    std::size_t k = 0;
                    for (; k + 4 <= l + 1; k += 4) {
                        g0 += zj[k] * ui[k];         g1 += zj[k + 1] * ui[k + 1];
                        g2 += zj[k + 2] * ui[k + 2]; g3 += zj[k + 3] * ui[k + 3];
                    }
                    g = (g0 + g1) + (g2 + g3);
                    for (; k <= l; ++k) g += zj[k] * ui[k];
                    e[j] = g / h;
                    f += e[j] * ui[j];
                }
                const double hh = f / (h + h);
                for (std::size_t j = 0; j <= l; ++j) e[j] -= hh * ui[j];   // e := w = p/h − hh·u
                // Symmetric rank-2 update A −= u·wᵀ + w·uᵀ over the full block (w fully
                // formed above, so no in-place hazard); contiguous inner loop.
                for (std::size_t j = 0; j <= l; ++j) {
                    const double uj = ui[j], wj = e[j];
                    double* zj = &z[j * n];
                    for (std::size_t k = 0; k <= l; ++k) zj[k] -= uj * e[k] + wj * ui[k];
                }
            }
        } else {
            e[i] = z[i * n + l];
        }
        d[i] = h;
    }
    d[0] = 0.0;
    e[0] = 0.0;
    if (want_vectors) {
        for (std::size_t i = 0; i < n; ++i) {       // accumulate the transform into z
            if (d[i] != 0.0) {
                for (std::size_t j = 0; j < i; ++j) {
                    double g = 0.0;
                    for (std::size_t k = 0; k < i; ++k) g += z[i * n + k] * z[k * n + j];
                    for (std::size_t k = 0; k < i; ++k) z[k * n + j] -= g * z[k * n + i];
                }
            }
            d[i] = z[i * n + i];
            z[i * n + i] = 1.0;
            for (std::size_t j = 0; j < i; ++j) { z[j * n + i] = 0.0; z[i * n + j] = 0.0; }
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) d[i] = z[i * n + i];  // values only — skip Q
    }

    // --- tql2: implicit-shift QL on the tridiagonal (d, e), rotating z alongside. ---
    for (std::size_t i = 1; i < n; ++i) e[i - 1] = e[i];
    e[n - 1] = 0.0;
    for (std::size_t l = 0; l < n; ++l) {
        int iter = 0;
        std::size_t m;
        do {
            for (m = l; m + 1 < n; ++m) {
                const double dd = std::fabs(d[m]) + std::fabs(d[m + 1]);
                if (std::fabs(e[m]) <= 2.2e-16 * dd) break;
            }
            if (m != l) {
                if (iter++ == 50)
                    throw std::runtime_error("linalg: symmetric eigen QL did not converge");
                double g = (d[l + 1] - d[l]) / (2.0 * e[l]);
                double r = pythag(g, 1.0);
                g = d[m] - d[l] + e[l] / (g + (g >= 0.0 ? std::fabs(r) : -std::fabs(r)));
                double s = 1.0, c = 1.0, p = 0.0;
                bool zeroed = false;
                for (std::size_t i = m; i-- > l;) {        // i = m-1 … l
                    double f = s * e[i];
                    const double b = c * e[i];
                    r = pythag(f, g);
                    e[i + 1] = r;
                    if (r == 0.0) { d[i + 1] -= p; e[m] = 0.0; zeroed = true; break; }
                    s = f / r;
                    c = g / r;
                    g = d[i + 1] - p;
                    r = (d[i] - g) * s + 2.0 * c * b;
                    p = s * r;
                    d[i + 1] = g + p;
                    g = c * r - b;
                    if (want_vectors)
                        for (std::size_t k = 0; k < n; ++k) {   // rotate eigenvector columns
                            f = z[k * n + i + 1];
                            z[k * n + i + 1] = s * z[k * n + i] + c * f;
                            z[k * n + i] = c * z[k * n + i] - s * f;
                        }
                }
                if (!zeroed) { d[l] -= p; e[l] = g; e[m] = 0.0; }
            }
        } while (m != l);
    }

    // sort DESCENDING, carrying the matching eigenvector columns.
    std::vector<std::size_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](std::size_t x, std::size_t y) { return d[x] > d[y]; });
    for (std::size_t j = 0; j < n; ++j) values[j] = d[idx[j]];
    if (want_vectors)
        for (std::size_t j = 0; j < n; ++j)
            for (std::size_t i = 0; i < n; ++i) vectors[i * n + j] = z[i * n + idx[j]];
}

bool is_symmetric(const std::vector<double>& a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
            if (std::fabs(a[i * n + j] - a[j * n + i]) > 1e-12 * (1 + std::fabs(a[i * n + j])))
                return false;
    return true;
}

// Complex Hermitian eigensolver with REAL eigenvalues and COMPLEX eigenvectors,
// reusing the real symmetric tridiagonal-QL solver via the standard 2n×2n real embedding:
// for H = A + iB (A symmetric, B antisymmetric), the real symmetric matrix
//   M = [[A, -B], [B, A]]
// has each eigenvalue of H twice, and a real eigenvector [x; y] of M corresponds to
// the complex eigenvector x + iy of H (already unit-norm: |x|²+|y|² = 1). We take
// one representative per duplicated pair. @p evecs (when requested) is row-major n×n
// with column k the eigenvector for evals[k]; both come out sorted descending.
void hermitian_eig(const std::vector<Cplx>& H, std::size_t n, std::vector<double>& evals,
                   std::vector<Cplx>& evecs, bool want_vectors) {
    const std::size_t N = 2 * n;
    std::vector<double> M(N * N, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            const double re = H[i * n + j].real(), im = H[i * n + j].imag();
            M[i * N + j] = re;                  // top-left  A
            M[(i + n) * N + (j + n)] = re;      // bottom-right A
            M[i * N + (j + n)] = -im;           // top-right  -B
            M[(i + n) * N + j] = im;            // bottom-left B
        }
    std::vector<double> w, V;
    symmetric_eig(M, N, w, V, want_vectors); // 2n eigenvalues (desc, paired) + vectors
    evals.resize(n);
    for (std::size_t k = 0; k < n; ++k) evals[k] = w[2 * k];  // one of each equal pair
    if (want_vectors) {
        evecs.assign(n * n, Cplx{});
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t col = 2 * k;
            for (std::size_t p = 0; p < n; ++p) {
                const double x = V[p * N + col], y = V[(p + n) * N + col];
                evecs[p * n + k] = Cplx(x, y);  // column k = eigenvector for evals[k]
            }
        }
    }
}

// Complex LU with partial pivoting, factored in place on M (row-major n×n): the unit
// lower factor's multipliers are stored below the diagonal, U on/above it. Returns the
// pivot vector. Factor ONCE, then `complex_lu_solve` for each right-hand side — inverse
// iteration reuses the same (deliberately near-singular) M across several RHS.
std::vector<std::size_t> complex_lu(std::vector<Cplx>& M, std::size_t n) {
    std::vector<std::size_t> piv(n);
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t p = k;
        double best = std::abs(M[k * n + k]);
        for (std::size_t i = k + 1; i < n; ++i) {
            const double m = std::abs(M[i * n + k]);
            if (m > best) { best = m; p = i; }
        }
        piv[k] = p;
        if (p != k)
            for (std::size_t j = 0; j < n; ++j) std::swap(M[k * n + j], M[p * n + j]);
        const Cplx d = M[k * n + k];
        for (std::size_t i = k + 1; i < n; ++i) {
            const Cplx f = M[i * n + k] / d;
            M[i * n + k] = f;
            for (std::size_t j = k + 1; j < n; ++j) M[i * n + j] -= f * M[k * n + j];
        }
    }
    return piv;
}
// Solve (already-factored) M·x = b in place on @p b (forward unit-L, then back-U).
void complex_lu_solve(const std::vector<Cplx>& M, const std::vector<std::size_t>& piv,
                      std::vector<Cplx>& b, std::size_t n) {
    for (std::size_t k = 0; k < n; ++k)
        if (piv[k] != k) std::swap(b[k], b[piv[k]]);
    for (std::size_t i = 0; i < n; ++i) {
        Cplx s = b[i];
        for (std::size_t j = 0; j < i; ++j) s -= M[i * n + j] * b[j];
        b[i] = s;
    }
    for (std::size_t i = n; i-- > 0;) {
        Cplx s = b[i];
        for (std::size_t j = i + 1; j < n; ++j) s -= M[i * n + j] * b[j];
        b[i] = s / M[i * n + i];
    }
}

// Eigenvector of the real matrix @p A for (complex) eigenvalue @p lambda, by inverse
// iteration. C = A − (λ + tiny complex shift)·I is made just non-singular by the
// shift, then a few inverse-iteration steps converge to the eigenvector; the phase
// is fixed so the largest-magnitude component is real-positive (a stable, if
// arbitrary, choice — eigenvectors are only defined up to phase).
std::vector<Cplx> eigvector_inverse_iteration(const std::vector<double>& A, std::size_t n,
                                              Cplx lambda) {
    double scale = 1.0;
    for (double a : A) scale = std::max(scale, std::fabs(a));
    const Cplx shifted = lambda + Cplx(scale * 1e-10, scale * 1e-10);
    std::vector<Cplx> C(n * n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            C[i * n + j] = Cplx(A[i * n + j], 0.0) - (i == j ? shifted : Cplx{});
    const auto normalize = [&](std::vector<Cplx>& x) {
        double nrm = 0.0;
        for (const Cplx& z : x) nrm += std::norm(z);
        nrm = std::sqrt(nrm);
        for (Cplx& z : x) z /= nrm;
    };
    const std::vector<std::size_t> piv = complex_lu(C, n);  // factor ONCE, reuse per step
    std::vector<Cplx> v(n, Cplx(1.0, 0.0));
    normalize(v);
    for (int it = 0; it < 5; ++it) {
        complex_lu_solve(C, piv, v, n);  // in place on v — no per-step copy or re-factor
        normalize(v);
    }
    std::size_t mi = 0;
    double mb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double m = std::abs(v[i]);
        if (m > mb) {
            mb = m;
            mi = i;
        }
    }
    const Cplx phase = v[mi] / std::abs(v[mi]);  // unit-norm v -> mb > 0
    for (Cplx& z : v) z /= phase;
    return v;
}

// ---- general eigenvalues: Hessenberg reduction + shifted QR ----
// Real matrix in; COMPLEX spectrum out (a 2×2 block with negative discriminant is a
// conjugate pair, not an error). The arithmetic stays real; only the extracted
// eigenvalues are complex.
std::vector<Cplx> eigvals_general(std::vector<double> a, std::size_t n) {
    // Householder reduction to upper Hessenberg.
    std::vector<double> u(n);  // reflector, reused per column (entries < k unused)
    for (std::size_t k = 1; k + 1 < n; ++k) {
        double scale = 0.0;
        for (std::size_t i = k; i < n; ++i) scale += std::fabs(a[i * n + (k - 1)]);
        if (scale == 0.0) continue;
        double h = 0.0;
        for (std::size_t i = k; i < n; ++i) {
            u[i] = a[i * n + (k - 1)] / scale;
            h += u[i] * u[i];
        }
        double g = (u[k] >= 0 ? -std::sqrt(h) : std::sqrt(h));
        h -= u[k] * g;
        u[k] -= g;
        // A = (I - uuᵀ/h) A (I - uuᵀ/h)
        for (std::size_t j = 0; j < n; ++j) {  // right: columns
            double f = 0.0;
            for (std::size_t i = k; i < n; ++i) f += u[i] * a[j * n + i];
            f /= h;
            for (std::size_t i = k; i < n; ++i) a[j * n + i] -= f * u[i];
        }
        for (std::size_t i = 0; i < n; ++i) {  // left: rows
            double f = 0.0;
            for (std::size_t j = k; j < n; ++j) f += u[j] * a[j * n + i];
            f /= h;
            for (std::size_t j = k; j < n; ++j) a[j * n + i] -= f * u[j];
        }
        a[k * n + (k - 1)] = scale * g;
        for (std::size_t i = k + 1; i < n; ++i) a[i * n + (k - 1)] = 0.0;
    }

    // Shifted QR on the Hessenberg matrix. A 1×1 block is a real eigenvalue; a 2×2
    // block is two reals (disc ≥ 0) or a complex conjugate pair (disc < 0).
    std::vector<Cplx> w(n);
    std::vector<double> cs, sn;  // Givens rotations, reused per QR sweep (clear keeps capacity)
    long long hi = static_cast<long long>(n) - 1;
    const double eps = 1e-14;
    int iter = 0;
    while (hi >= 0) {
        long long l = hi;
        while (l > 0) {
            const double s = std::fabs(a[(l - 1) * n + (l - 1)]) + std::fabs(a[l * n + l]);
            if (std::fabs(a[l * n + (l - 1)]) <= eps * (s == 0 ? 1.0 : s)) break;
            --l;
        }
        if (l == hi) {                          // 1×1 block -> real eigenvalue
            w[hi] = a[hi * n + hi];
            --hi;
            iter = 0;
        } else if (l == hi - 1) {               // 2×2 block
            const std::size_t p = static_cast<std::size_t>(hi - 1), q = static_cast<std::size_t>(hi);
            const double app = a[p * n + p], aqq = a[q * n + q];
            const double apq = a[p * n + q], aqp = a[q * n + p];
            const double tr = app + aqq, det = app * aqq - apq * aqp;
            const double disc = tr * tr - 4.0 * det;
            if (disc >= 0.0) {                  // two real eigenvalues
                const double sq = std::sqrt(disc);
                w[p] = (tr + sq) / 2.0;
                w[q] = (tr - sq) / 2.0;
            } else {                            // complex conjugate pair
                const double im = std::sqrt(-disc) / 2.0;
                w[p] = Cplx(tr / 2.0, im);
                w[q] = Cplx(tr / 2.0, -im);
            }
            hi -= 2;
            iter = 0;
        } else {                                // QR sweep with Wilkinson shift
            if (++iter > 200) throw std::runtime_error("linalg: eigenvalue iteration did not converge");
            const double shift = a[hi * n + hi];
            for (long long i = l; i <= hi; ++i) a[i * n + i] -= shift;
            // one explicit QR step via Givens rotations on the Hessenberg block
            cs.clear();
            sn.clear();
            for (long long i = l; i < hi; ++i) {
                const double x = a[i * n + i], y = a[(i + 1) * n + i];
                const double r = pythag(x, y);
                const double c = r == 0 ? 1.0 : x / r, s = r == 0 ? 0.0 : y / r;
                cs.push_back(c);
                sn.push_back(s);
                for (long long j = i; j <= hi; ++j) {
                    const double t1 = a[i * n + j], t2 = a[(i + 1) * n + j];
                    a[i * n + j] = c * t1 + s * t2;
                    a[(i + 1) * n + j] = -s * t1 + c * t2;
                }
            }
            for (long long i = l; i < hi; ++i) {  // RQ: post-multiply
                const double c = cs[static_cast<std::size_t>(i - l)], s = sn[static_cast<std::size_t>(i - l)];
                for (long long j = l; j <= i + 1; ++j) {
                    const double t1 = a[j * n + i], t2 = a[j * n + (i + 1)];
                    a[j * n + i] = c * t1 + s * t2;
                    a[j * n + (i + 1)] = -s * t1 + c * t2;
                }
            }
            for (long long i = l; i <= hi; ++i) a[i * n + i] += shift;
        }
    }
    return w;
}

// ---- user-provided output buffers: reuse the caller's storage, no result NDArray allocation ----
// Every array-returning routine below also has an `out`-FIRST overload (like matmul and the ndarray
// elementwise ops) that writes into a caller-supplied array instead of allocating a fresh one, so a
// hot loop can hand the same scratch every call. `out_buf` validates the destination and returns a
// writable pointer into it; the memory-bound products/transposes write their kernel STRAIGHT into
// that pointer (genuinely zero result allocation). `copy_into` places an already-built result into
// it — used by the O(n³) factorizations, whose internal workspace is allocated regardless and for
// which the single O(n²) copy is negligible next to the decomposition.
template <ndarray::Field T>
T* out_buf(ndarray::basic_ndarray<T>& out, const std::vector<std::size_t>& shape) {
    if (out.shape() != shape || !ndarray::is_contiguous(out))
        throw std::runtime_error("linalg: out must be a contiguous array of the result's shape");
    return out.buffer()->data() + out.offset();
}
// Reject an out that aliases an operand still being READ through a zero-copy `contig` pointer
// (the products and the transpose). The factorizations copy their inputs out first, so they never
// call this — out may safely alias the input there.
template <ndarray::Field T>
void reject_alias(const ndarray::basic_ndarray<T>& out, const ndarray::basic_ndarray<T>& a) {
    if (out.buffer().get() == a.buffer().get())
        throw std::runtime_error("linalg: out must not alias an input (it is not computed in place)");
}
// Copy a freshly-built contiguous result into the caller's out buffer (validated, reuses its storage).
template <ndarray::Field T>
void copy_into(ndarray::basic_ndarray<T>& out, const ndarray::basic_ndarray<T>& result) {
    T* dst = out_buf(out, result.shape());
    std::copy_n(result.buffer()->data() + result.offset(), result.size(), dst);
}

}  // namespace

// ================= public routines =================

// ---- products ----
// The products only READ their operands, so they take a zero-copy `contig` pointer
// (straight into the array's own buffer when it is contiguous — the common case)
// and allocate nothing but the result.
//
// Reduction kernels use several independent accumulators. A single running sum
// serializes the loop on floating-point-add latency (the compiler may not reassociate
// FP adds without -ffast-math), so a plain `s += x[i]*y[i]` runs at ~one element per
// FADD latency. Independent lanes break that dependency chain, letting -O3
// -march=native issue SIMD + FMA and hit memory bandwidth instead of add latency.
namespace {
// The reduction kernel over any Field T with a compile-time conjugation choice (@ref Conj) —
// ONE kernel replacing the former real `ddot` and complex `cdot`. EIGHT independent accumulators
// break the FP-add dependency chain so -O3 -march=native emits SIMD+FMA and hits memory bandwidth
// instead of add latency. `term` conjugates the first operand only for a complex element under
// Conj::Conjugate (Hermitian inner product); for real T, or Conj::None, the conjugation branch is
// compiled OUT by `if constexpr` — zero cost, no dead loads.
template <ndarray::Field T, Conj C>
T dot_kernel(const T* x, const T* y, std::size_t n) {
    auto term = [](const T& xi, const T& yi) -> T {
        if constexpr (ndarray::is_complex_v<T> && C == Conj::Conjugate) return std::conj(xi) * yi;
        else return xi * yi;
    };
    T s0{}, s1{}, s2{}, s3{}, s4{}, s5{}, s6{}, s7{};
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        s0 += term(x[i + 0], y[i + 0]); s1 += term(x[i + 1], y[i + 1]);
        s2 += term(x[i + 2], y[i + 2]); s3 += term(x[i + 3], y[i + 3]);
        s4 += term(x[i + 4], y[i + 4]); s5 += term(x[i + 5], y[i + 5]);
        s6 += term(x[i + 6], y[i + 6]); s7 += term(x[i + 7], y[i + 7]);
    }
    T s = ((s0 + s1) + (s2 + s3)) + ((s4 + s5) + (s6 + s7));
    for (; i < n; ++i) s += term(x[i], y[i]);
    return s;
}
// Read two operands as contiguous pointers (zero-copy when contiguous, else pack once) and reduce.
template <ndarray::Field T, Conj C, template <typename> class Array>
T dot_reduce(const Array<T>& a, const Array<T>& b) {
    const std::size_t n = vector_len(a), m = vector_len(b);
    if (n != m) throw std::runtime_error("linalg: dot dimension mismatch");
    if (ndarray::is_contiguous(a) && ndarray::is_contiguous(b))
        return dot_kernel<T, C>(a.buffer()->data() + a.offset(), b.buffer()->data() + b.offset(), n);
    std::vector<T> sa, sb;
    return dot_kernel<T, C>(contig(a, sa), contig(b, sb), n);
}
}  // namespace

// dot / vdot / inner over any Field T and (host) container Array. `dot`/`inner` are bilinear
// (Σ aᵢbᵢ); `vdot` is the conjugate-linear Hermitian inner product Σ conj(aᵢ)·bᵢ (identical to dot
// for a real element). Both operands are Array<T> (the deduction firewall). Returns the scalar T.
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
T dot(const Array<T>& a, const Array<T>& b) { return dot_reduce<T, Conj::None>(a, b); }
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
T vdot(const Array<T>& a, const Array<T>& b) { return dot_reduce<T, Conj::Conjugate>(a, b); }
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
T inner(const Array<T>& a, const Array<T>& b) { return dot_reduce<T, Conj::None>(a, b); }
// Explicit instantiations: the host real + complex forms the library ships.
template double dot<double, ndarray::basic_ndarray>(const NDArray&, const NDArray&);
template Cplx dot<Cplx, ndarray::basic_ndarray>(const CNDArray&, const CNDArray&);
template double vdot<double, ndarray::basic_ndarray>(const NDArray&, const NDArray&);
template Cplx vdot<Cplx, ndarray::basic_ndarray>(const CNDArray&, const CNDArray&);
template double inner<double, ndarray::basic_ndarray>(const NDArray&, const NDArray&);

namespace {
// outer-product kernel over any Field T: writes rp[n×m] = x[i]·y[j]. Loop-invariant xi + a clean
// row pointer keep the inner store contiguous so it vectorizes.
template <ndarray::Field T>
void outer_kernel(T* rp, const T* x, const T* y, std::size_t n, std::size_t m) {
    for (std::size_t i = 0; i < n; ++i) {
        const T xi = x[i];               // loop-invariant scalar…
        T* ri = rp + i * m;              // …and a clean row pointer, so the inner
        for (std::size_t j = 0; j < m; ++j) ri[j] = xi * y[j];  // store vectorizes
    }
}
}  // namespace

// Outer product a⊗b (rank-1 n×m matrix) into the caller's buffer — the HOST out-parameter form
// (two-layer over element T and container Array). Writes the kernel straight into @p out.
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void outer(Array<T>& out, const Array<T>& a, const Array<T>& b) {
    const std::size_t n = vector_len(a), m = vector_len(b);
    reject_alias(out, a);
    reject_alias(out, b);
    T* rp = out_buf(out, {n, m});
    std::vector<T> sa, sb;
    outer_kernel<T>(rp, contig(a, sa), contig(b, sb), n, m);
}
// Allocating front: any pair of vector lengths, result is the n×m rank-1 matrix as Array<T>.
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] Array<T> outer(const Array<T>& a, const Array<T>& b) {
    Array<T> out = Array<T>::uninitialized({vector_len(a), vector_len(b)});
    outer(out, a, b);
    return out;
}
template void outer<double, ndarray::basic_ndarray>(NDArray&, const NDArray&, const NDArray&);
template NDArray outer<double, ndarray::basic_ndarray>(const NDArray&, const NDArray&);

namespace {
// The matmul kernel over ANY Field T (real or complex). The loop is element-generic — the
// only element-specific step is the `T{}` zero-fill — so ONE kernel now serves what used to be
// a `double*` and a `Cplx*` overload. Writes C[ar×bc] = A[ar×ac]·B[ac×bc] into the caller's @p C
// (zeroed, then accumulated). ikj keeps the inner (j) loop contiguous so it vectorizes; blocking
// FOUR rows of A reuses each B[k][j] load across four C rows (4 FMAs per B load instead of 1).
template <ndarray::Field T>
void matmul_kernel(T* C, const T* A, const T* B, std::size_t ar, std::size_t ac, std::size_t bc) {
    std::fill(C, C + ar * bc, T{});
    std::size_t i = 0;
    for (; i + 4 <= ar; i += 4) {
        T* c0 = &C[(i + 0) * bc]; T* c1 = &C[(i + 1) * bc];
        T* c2 = &C[(i + 2) * bc]; T* c3 = &C[(i + 3) * bc];
        for (std::size_t k = 0; k < ac; ++k) {
            const T a0 = A[(i + 0) * ac + k], a1 = A[(i + 1) * ac + k];
            const T a2 = A[(i + 2) * ac + k], a3 = A[(i + 3) * ac + k];
            const T* bk = &B[k * bc];
            for (std::size_t j = 0; j < bc; ++j) {
                const T bkj = bk[j];
                c0[j] += a0 * bkj; c1[j] += a1 * bkj; c2[j] += a2 * bkj; c3[j] += a3 * bkj;
            }
        }
    }
    for (; i < ar; ++i) {  // remainder rows (ar not a multiple of 4)
        T* ci = &C[i * bc];
        for (std::size_t k = 0; k < ac; ++k) {
            const T aik = A[i * ac + k];
            const T* bk = &B[k * bc];
            for (std::size_t j = 0; j < bc; ++j) ci[j] += aik * bk[j];
        }
    }
}
// Shared 2-D shape validation → (ar, ac, bc); throws on a non-2-D input or inner-dim mismatch.
template <ndarray::Field T>
void check_matmul(const ndarray::basic_ndarray<T>& a, const ndarray::basic_ndarray<T>& b,
                  std::size_t& ar, std::size_t& ac, std::size_t& bc) {
    if (a.ndim() != 2 || b.ndim() != 2)
        throw std::runtime_error("linalg: matmul expects 2-D matrices");
    ar = a.shape()[0]; ac = a.shape()[1];
    const std::size_t br = b.shape()[0]; bc = b.shape()[1];
    if (ac != br) throw std::runtime_error("linalg: matmul inner dimension mismatch");
}
}  // namespace

// Matmul into the caller's buffer @p out (out FIRST) — the HOST out-parameter kernel (the two-layer
// `template <Field T, template<typename> class Array> requires HostArray<Array<T>>` overload declared
// in backend.hpp). ONE definition unifying the former real and complex out-param functions. Validates
// shapes, rejects aliasing (out reads all of A and B while writing, so it is not in-place), packs a
// strided operand once, and runs the single matmul_kernel. The allocating matmul(a,b) front calls it.
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void matmul(Array<T>& out, const Array<T>& a, const Array<T>& b) {
    std::size_t ar, ac, bc;
    check_matmul(a, b, ar, ac, bc);
    reject_alias(out, a);
    reject_alias(out, b);
    T* C = out_buf(out, {ar, bc});
    std::vector<T> sa, sb;
    matmul_kernel<T>(C, contig(a, sa), contig(b, sb), ar, ac, bc);
}
// Explicit instantiations for the two host element types the library ships — both the out-param
// kernel and the allocating front — so the header templates link from other TUs and llvm coverage
// attributes their bodies to this TU.
template void matmul<double, ndarray::basic_ndarray>(NDArray&, const NDArray&, const NDArray&);
template void matmul<Cplx, ndarray::basic_ndarray>(CNDArray&, const CNDArray&, const CNDArray&);
template NDArray matmul<double, ndarray::basic_ndarray>(const NDArray&, const NDArray&);
template CNDArray matmul<Cplx, ndarray::basic_ndarray>(const CNDArray&, const CNDArray&);

namespace {
// (Conjugate-)transpose kernel over any Field T: D[c×r] = A[r×c]ᵀ, conjugated for a complex
// element (Hermitian adjoint). The conjugation is an `if constexpr` branch — a real element
// gets a plain transpose, a complex element the adjoint, from ONE kernel.
template <ndarray::Field T>
void transpose_kernel(T* D, const T* A, std::size_t r, std::size_t c) {
    for (std::size_t i = 0; i < r; ++i)
        for (std::size_t j = 0; j < c; ++j) {
            if constexpr (ndarray::is_complex_v<T>) D[j * r + i] = std::conj(A[i * c + j]);
            else D[j * r + i] = A[i * c + j];
        }
}
}  // namespace

// Conjugate transpose (Hermitian adjoint) Aᴴ into the caller's buffer — the HOST out-parameter
// form (two-layer). For a real element this is a plain transpose (conjugation compiled out).
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void conj_transpose(Array<T>& out, const Array<T>& a) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    const std::size_t r = a.shape()[0], c = a.shape()[1];
    reject_alias(out, a);  // reads A while writing the transposed out — not in place
    T* D = out_buf(out, {c, r});
    std::vector<T> sa;
    transpose_kernel<T>(D, contig(a, sa), r, c);
}
// Allocating front: the c×r adjoint as Array<T>.
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] Array<T> conj_transpose(const Array<T>& a) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    Array<T> out = Array<T>::uninitialized({a.shape()[1], a.shape()[0]});
    conj_transpose(out, a);
    return out;
}
template void conj_transpose<Cplx, ndarray::basic_ndarray>(CNDArray&, const CNDArray&);
template CNDArray conj_transpose<Cplx, ndarray::basic_ndarray>(const CNDArray&);

NDArray matrix_power(const NDArray& a, long long p) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    const std::size_t r = a.shape()[0], c = a.shape()[1];  // dims only — no copy
    require_square(r, c);
    std::vector<double> result(r * r, 0.0);
    for (std::size_t i = 0; i < r; ++i) result[i * r + i] = 1.0;  // identity
    NDArray acc = make_matrix(r, r, std::move(result));
    NDArray base = (p < 0) ? inv(a) : a;
    long long e = p < 0 ? -p : p;
    while (e > 0) {
        if (e & 1) acc = matmul(acc, base);
        base = matmul(base, base);
        e >>= 1;
    }
    return acc;
}

namespace {
// Kronecker-product kernel over any Field T: K[(ar·br)×(ac·bc)] = A⊗B, each A entry scaling the
// whole of B.
template <ndarray::Field T>
void kron_kernel(T* K, const T* A, const T* B, std::size_t ar, std::size_t ac,
                 std::size_t br, std::size_t bc) {
    const std::size_t kc = ac * bc;
    for (std::size_t i = 0; i < ar; ++i)
        for (std::size_t j = 0; j < ac; ++j)
            for (std::size_t p = 0; p < br; ++p)
                for (std::size_t q = 0; q < bc; ++q)
                    K[(i * br + p) * kc + (j * bc + q)] = A[i * ac + j] * B[p * bc + q];
}
// Shared 2-D validation → (ar, ac, br, bc); throws on a non-2-D operand.
template <ndarray::Field T, template <typename> class Array>
void kron_dims(const Array<T>& a, const Array<T>& b, std::size_t& ar, std::size_t& ac,
               std::size_t& br, std::size_t& bc) {
    if (a.ndim() != 2 || b.ndim() != 2)
        throw std::runtime_error("linalg: kron expects 2-D matrices");
    ar = a.shape()[0]; ac = a.shape()[1];
    br = b.shape()[0]; bc = b.shape()[1];
}
}  // namespace

// Kronecker product A⊗B into the caller's buffer — the HOST out-parameter form (two-layer).
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void kron(Array<T>& out, const Array<T>& a, const Array<T>& b) {
    std::size_t ar, ac, br, bc;
    kron_dims(a, b, ar, ac, br, bc);
    reject_alias(out, a);
    reject_alias(out, b);
    T* K = out_buf(out, {ar * br, ac * bc});
    std::vector<T> sa, sb;
    kron_kernel<T>(K, contig(a, sa), contig(b, sb), ar, ac, br, bc);
}
// Allocating front: the (ar·br)×(ac·bc) product as Array<T>.
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] Array<T> kron(const Array<T>& a, const Array<T>& b) {
    std::size_t ar, ac, br, bc;
    kron_dims(a, b, ar, ac, br, bc);
    Array<T> out = Array<T>::uninitialized({ar * br, ac * bc});
    kron(out, a, b);
    return out;
}
template void kron<double, ndarray::basic_ndarray>(NDArray&, const NDArray&, const NDArray&);
template NDArray kron<double, ndarray::basic_ndarray>(const NDArray&, const NDArray&);

// Trace (sum of the diagonal) — two-layer over element T and (host) container Array; returns the
// scalar T. Reads the diagonal straight from the buffer, no copy, even for a strided view.
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
T trace(const Array<T>& a) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    const std::size_t r = a.shape()[0], c = a.shape()[1];
    const T* base = a.buffer()->data();
    const std::ptrdiff_t off = static_cast<std::ptrdiff_t>(a.offset());
    const std::ptrdiff_t step = a.strides()[0] + a.strides()[1];  // (i,i) advances by s0+s1
    const std::size_t d = std::min(r, c);
    T s0{}, s1{}, s2{}, s3{};                 // independent lanes break the add-latency chain
    std::size_t i = 0;
    for (; i + 4 <= d; i += 4) {
        s0 += base[static_cast<std::size_t>(off + static_cast<std::ptrdiff_t>(i) * step)];
        s1 += base[static_cast<std::size_t>(off + static_cast<std::ptrdiff_t>(i + 1) * step)];
        s2 += base[static_cast<std::size_t>(off + static_cast<std::ptrdiff_t>(i + 2) * step)];
        s3 += base[static_cast<std::size_t>(off + static_cast<std::ptrdiff_t>(i + 3) * step)];
    }
    T s = (s0 + s1) + (s2 + s3);
    for (; i < d; ++i) s += base[static_cast<std::size_t>(off + static_cast<std::ptrdiff_t>(i) * step)];
    return s;
}
template double trace<double, ndarray::basic_ndarray>(const NDArray&);

double norm(const NDArray& a) {  // Frobenius (matrices) / L2 (vectors) — same flat sum
    // Frobenius/L2 norm is sqrt(x·x); reuse the multi-accumulator dot_kernel so the
    // squared-sum reaches memory bandwidth instead of serializing on FP-add latency.
    // Contiguous fast path reads straight from the buffer (no scratch allocation).
    if (ndarray::is_contiguous(a)) {
        const double* p = a.buffer()->data() + a.offset();
        return std::sqrt(dot_kernel<double, Conj::None>(p, p, a.size()));
    }
    std::vector<double> scratch;
    const double* p = contig(a, scratch);
    return std::sqrt(dot_kernel<double, Conj::None>(p, p, a.size()));
}

// ---- LU-based: solve / det / slogdet / inv / lstsq ----
NDArray solve(const NDArray& a, const NDArray& b) {
    std::size_t n, c;
    std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    const LU lu = lu_decompose(std::move(A), n);
    std::size_t bn;
    std::vector<double> x = as_vector(b, bn);
    if (bn != n) throw std::runtime_error("linalg: solve dimension mismatch");
    lu_solve(lu, x);
    return make_vector(std::move(x));
}

double det(const NDArray& a) {
    std::size_t n, c;
    std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    const LU lu = lu_decompose(std::move(A), n);
    double d = lu.sign;
    for (std::size_t i = 0; i < n; ++i) d *= lu.a[i * n + i];
    return d;
}

SLogDet slogdet(const NDArray& a) {
    std::size_t n, c;
    std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    const LU lu = lu_decompose(std::move(A), n);
    double sign = lu.sign, logabs = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = lu.a[i * n + i];
        if (d < 0) sign = -sign;
        logabs += std::log(std::fabs(d));
    }
    return {sign, logabs};
}

NDArray inv(const NDArray& a) {
    std::size_t n, c;
    std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    const LU lu = lu_decompose(std::move(A), n);
    const std::vector<double>& M = lu.a;  // L (unit, below diag) + U (on/above), row-major
    // Invert by solving L·U·X = P·I for the WHOLE identity at once. Doing the forward
    // and back substitution across all n columns turns each inner loop into a SAXPY
    // over a contiguous row (`X[i,:] -= M[i,j]·X[j,:]`), which auto-vectorizes — unlike
    // n separate single-RHS solves, whose substitution is a serial-reduction dot that
    // cannot vectorize (the reason a naive `inv` lost to LAPACK while `det` won).
    std::vector<double> X(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) X[i * n + i] = 1.0;   // identity
    for (std::size_t k = 0; k < n; ++k)                       // apply LU's row pivots: X = P·I
        if (lu.piv[k] != k)
            for (std::size_t col = 0; col < n; ++col) std::swap(X[k * n + col], X[lu.piv[k] * n + col]);
    for (std::size_t i = 0; i < n; ++i)                       // forward: unit-lower L·Y = P
        for (std::size_t j = 0; j < i; ++j) {
            const double f = M[i * n + j];
            for (std::size_t col = 0; col < n; ++col) X[i * n + col] -= f * X[j * n + col];
        }
    for (std::size_t i = n; i-- > 0;) {                       // back: upper U·X = Y
        for (std::size_t j = i + 1; j < n; ++j) {
            const double f = M[i * n + j];
            for (std::size_t col = 0; col < n; ++col) X[i * n + col] -= f * X[j * n + col];
        }
        const double d = M[i * n + i];
        for (std::size_t col = 0; col < n; ++col) X[i * n + col] /= d;
    }
    return make_matrix(n, n, std::move(X));
}

NDArray lstsq(const NDArray& a, const NDArray& b) {  // min ‖Ax−b‖ via the pseudo-inverse
    return matmul(pinv(a), b);
}

// ---- Cholesky ----
NDArray cholesky(const NDArray& a) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    const std::size_t n = a.shape()[0], c = a.shape()[1];
    require_square(n, c);
    std::vector<double> scratch;
    const double* A = contig(a, scratch);  // read-only — zero-copy when contiguous
    std::vector<double> L(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double* Li = &L[i * n];
        for (std::size_t j = 0; j <= i; ++j) {
            // s = A[i][j] − (row i · row j over k<j): four accumulators so the O(n³)
            // inner dot vectorizes instead of serializing on FP-sub latency.
            const double* Lj = &L[j * n];
            double d0 = 0, d1 = 0, d2 = 0, d3 = 0;
            std::size_t k = 0;
            for (; k + 4 <= j; k += 4) {
                d0 += Li[k] * Lj[k];         d1 += Li[k + 1] * Lj[k + 1];
                d2 += Li[k + 2] * Lj[k + 2]; d3 += Li[k + 3] * Lj[k + 3];
            }
            double s = A[i * n + j] - ((d0 + d1) + (d2 + d3));
            for (; k < j; ++k) s -= Li[k] * Lj[k];
            if (i == j) {
                if (s <= 0.0) throw std::runtime_error("linalg: matrix is not positive-definite");
                L[i * n + i] = std::sqrt(s);
            } else {
                L[i * n + j] = s / L[j * n + j];
            }
        }
    }
    return make_matrix(n, n, std::move(L));
}

// ---- Householder QR (reduced: Q is m×n, R is n×n) ----
QR qr(const NDArray& a) {
    std::size_t m, n;
    std::vector<double> A = as_matrix(a, m, n);
    if (m < n) throw std::runtime_error("linalg: qr requires rows >= cols");
    // Work on the TRANSPOSE At (n×m, row-major). A Householder QR repeatedly reads and
    // updates COLUMNS of A, which stride by n in row-major and don't vectorize (the
    // original cost ~3× Eigen); as ROWS of At those same operations are contiguous, and
    // the reductions are multi-accumulated like ddot.
    std::vector<double> At(n * m);
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < n; ++j) At[j * m + i] = A[i * n + j];
    std::vector<double> Q(m * m, 0.0);
    for (std::size_t i = 0; i < m; ++i) Q[i * m + i] = 1.0;
    std::vector<double> u(m);  // Householder vector, reused per column (entries < k unused)
    // Reflect: s = u · row (4 accumulators), then row -= (2 s / ‖u‖²) u — contiguous.
    auto reflect = [&u](double* row, std::size_t k, std::size_t m, double inv) {
        double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        std::size_t i = k;
        for (; i + 4 <= m; i += 4) {
            s0 += u[i] * row[i];         s1 += u[i + 1] * row[i + 1];
            s2 += u[i + 2] * row[i + 2]; s3 += u[i + 3] * row[i + 3];
        }
        double s = (s0 + s1) + (s2 + s3);
        for (; i < m; ++i) s += u[i] * row[i];
        s *= inv;
        for (i = k; i < m; ++i) row[i] -= s * u[i];
    };
    for (std::size_t k = 0; k < n; ++k) {
        double* Atk = &At[k * m];                 // column k of A == row k of At
        double nrm = 0.0;
        for (std::size_t i = k; i < m; ++i) nrm += Atk[i] * Atk[i];
        nrm = std::sqrt(nrm);
        if (nrm == 0.0) continue;
        const double alpha = Atk[k] >= 0 ? -nrm : nrm;
        for (std::size_t i = k; i < m; ++i) u[i] = Atk[i];
        u[k] -= alpha;
        double unorm2 = 0.0;
        for (std::size_t i = k; i < m; ++i) unorm2 += u[i] * u[i];
        if (unorm2 == 0.0) continue;
        const double inv = 2.0 / unorm2;
        for (std::size_t j = k; j < n; ++j) reflect(&At[j * m], k, m, inv);  // A's cols j≥k
        for (std::size_t j = 0; j < m; ++j) reflect(&Q[j * m], k, m, inv);   // Q = Q · Hₖ
    }
    std::vector<double> Qr(m * n), Rr(n * n, 0.0);  // reduced
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < n; ++j) Qr[i * n + j] = Q[i * m + j];
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i; j < n; ++j) Rr[i * n + j] = At[j * m + i];  // R[i][j]=A[i][j]=At[j][i]
    return {make_matrix(m, n, std::move(Qr)), make_matrix(n, n, std::move(Rr))};
}

// ---- SVD and its derived quantities ----
SVD svd(const NDArray& a) {
    std::size_t m, n;
    std::vector<double> A = as_matrix(a, m, n);
    if (m < n) throw std::runtime_error("linalg: svd requires rows >= cols (transpose otherwise)");
    const SVDc s = svd_golub_reinsch(std::move(A), m, n);
    // vh = Vᵀ
    std::vector<double> vh(n * n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) vh[i * n + j] = s.v[j * n + i];
    return {make_matrix(m, n, s.u), make_vector(s.w), make_matrix(n, n, std::move(vh))};
}

NDArray svdvals(const NDArray& a) {  // singular values only — skips the U/V work entirely
    std::size_t m, n;
    std::vector<double> A = as_matrix(a, m, n);
    SVDc s;
    if (m >= n) {
        s = svd_golub_reinsch(std::move(A), m, n, /*want_uv=*/false);
    } else {                          // A and Aᵀ share singular values; reduce the tall one
        std::vector<double> At(n * m);
        for (std::size_t i = 0; i < m; ++i)
            for (std::size_t j = 0; j < n; ++j) At[j * m + i] = A[i * n + j];
        s = svd_golub_reinsch(std::move(At), n, m, /*want_uv=*/false);
    }
    return make_vector(std::move(s.w));
}

NDArray pinv(const NDArray& a) {
    std::size_t m, n;
    std::vector<double> A = as_matrix(a, m, n);
    if (m >= n) {
        const SVDc s = svd_golub_reinsch(std::move(A), m, n);  // A = U(m×n) diag(w) V(n×n)ᵀ
        const double tsh = 0.5 * std::sqrt(double(m + n + 1)) * (s.w.empty() ? 0 : s.w[0]) * 1e-15;
        std::vector<double> p(n * m, 0.0);  // pinv = V diag(1/w) Uᵀ  -> n×m
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < m; ++j) {
                double acc = 0.0;
                for (std::size_t k = 0; k < n; ++k)
                    if (s.w[k] > tsh) acc += s.v[i * n + k] * (s.u[j * n + k] / s.w[k]);
                p[i * m + j] = acc;
            }
        return make_matrix(n, m, std::move(p));
    }
    // m < n: compute on Aᵀ (n×m, rows>=cols) then transpose the result.
    std::vector<double> At(n * m);
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < n; ++j) At[j * m + i] = A[i * n + j];
    const SVDc s = svd_golub_reinsch(std::move(At), n, m);  // Aᵀ = U(n×m) diag(w) V(m×m)ᵀ
    const double tsh = 0.5 * std::sqrt(double(n + m + 1)) * (s.w.empty() ? 0 : s.w[0]) * 1e-15;
    std::vector<double> res(n * m, 0.0);  // pinv(A) = (V diag(1/w) Uᵀ)ᵀ -> n×m
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            double acc = 0.0;
            for (std::size_t k = 0; k < m; ++k)
                if (s.w[k] > tsh) acc += s.v[i * m + k] * (s.u[j * m + k] / s.w[k]);
            res[j * m + i] = acc;  // transpose into the n×m result
        }
    return make_matrix(n, m, std::move(res));
}

double cond(const NDArray& a) {
    std::size_t m, n;
    std::vector<double> A = as_matrix(a, m, n);
    SVDc s;
    if (m >= n) {
        s = svd_golub_reinsch(std::move(A), m, n, /*want_uv=*/false);
    } else {
        std::vector<double> At(n * m);
        for (std::size_t i = 0; i < m; ++i)
            for (std::size_t j = 0; j < n; ++j) At[j * m + i] = A[i * n + j];
        s = svd_golub_reinsch(std::move(At), n, m, /*want_uv=*/false);
    }
    const double wmin = s.w.empty() ? 0 : s.w.back();
    return wmin == 0 ? std::numeric_limits<double>::infinity() : s.w.front() / wmin;
}

long long matrix_rank(const NDArray& a) {
    std::size_t m, n;
    std::vector<double> A = as_matrix(a, m, n);
    const bool tr = m < n;
    SVDc s;
    if (tr) {
        std::vector<double> At(n * m);
        for (std::size_t i = 0; i < m; ++i)
            for (std::size_t j = 0; j < n; ++j) At[j * m + i] = A[i * n + j];
        s = svd_golub_reinsch(std::move(At), n, m, /*want_uv=*/false);
    } else {
        s = svd_golub_reinsch(std::move(A), m, n, /*want_uv=*/false);
    }
    const double tsh = 0.5 * std::sqrt(double(m + n + 1)) * (s.w.empty() ? 0 : s.w[0]) * 1e-15;
    long long r = 0;
    for (double w : s.w)
        if (w > tsh) ++r;
    return r;
}

// ---- eigenvalues ----
Eig eigh(const NDArray& a) {
    std::size_t n, c;
    std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    std::vector<double> vals, vecs;
    symmetric_eig(std::move(A), n, vals, vecs);  // solver owns the copy — no second one
    return {make_vector(std::move(vals)), make_matrix(n, n, std::move(vecs))};
}
NDArray eigvalsh(const NDArray& a) {
    std::size_t n, c;
    std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    std::vector<double> vals, vecs;
    symmetric_eig(std::move(A), n, vals, vecs, /*want_vectors=*/false);
    return make_vector(std::move(vals));
}
EighC eigh(const CNDArray& a) {
    std::size_t n, c;
    const std::vector<Cplx> H = as_cmatrix(a, n, c);
    require_square(n, c);
    std::vector<double> vals;
    std::vector<Cplx> vecs;
    hermitian_eig(H, n, vals, vecs, /*want_vectors=*/true);
    return {make_vector(std::move(vals)), make_cmatrix(n, n, std::move(vecs))};
}
NDArray eigvalsh(const CNDArray& a) {
    std::size_t n, c;
    const std::vector<Cplx> H = as_cmatrix(a, n, c);
    require_square(n, c);
    std::vector<double> vals;
    std::vector<Cplx> vecs;
    hermitian_eig(H, n, vals, vecs, /*want_vectors=*/false);
    return make_vector(std::move(vals));
}
EigC eig(const NDArray& a) {
    std::size_t n, c;
    std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    if (is_symmetric(A, n)) {                // symmetric -> real spectrum + eigenvectors
        // Reuse the matrix we already extracted (eigh(a) would re-extract it — a
        // wasted O(n²) copy); the symmetric branch returns here, so moving A is safe.
        std::vector<double> rvals, rvecs;
        symmetric_eig(std::move(A), n, rvals, rvecs, /*want_vectors=*/true);
        return {to_complex(make_vector(std::move(rvals))),
                to_complex(make_matrix(n, n, std::move(rvecs)))};
    }
    std::vector<Cplx> vals = eigvals_general(A, n);
    std::sort(vals.begin(), vals.end(), cgreater);
    // Complex eigenvectors via inverse iteration: column k is the eigenvector for vals[k].
    std::vector<Cplx> vecs(n * n, Cplx{});
    for (std::size_t k = 0; k < n; ++k) {
        const std::vector<Cplx> vk = eigvector_inverse_iteration(A, n, vals[k]);
        for (std::size_t i = 0; i < n; ++i) vecs[i * n + k] = vk[i];
    }
    return {make_cvector(std::move(vals)), make_cmatrix(n, n, std::move(vecs))};
}
CNDArray eigvals(const NDArray& a) {
    std::size_t n, c;
    std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    std::vector<Cplx> vals;
    if (is_symmetric(A, n)) {                // symmetric -> real spectrum, values only
        std::vector<double> rvals, vecs;
        symmetric_eig(std::move(A), n, rvals, vecs, /*want_vectors=*/false);
        vals.assign(rvals.begin(), rvals.end());
    } else {
        vals = eigvals_general(std::move(A), n);
    }
    std::sort(vals.begin(), vals.end(), cgreater);
    return make_cvector(std::move(vals));
}

// ---- out-param (buffer-reuse) overloads for the factorizations and multi-output routines ----
// Each writes its result into the caller's pre-sized array(s) — out FIRST — so a sweep that
// repeats the same decomposition (e.g. cross-validating many models) can reuse one set of output
// buffers instead of allocating fresh arrays every call. These O(n³) routines allocate their
// factorization workspace internally regardless; the result is placed into @p out in one O(n²)
// pass (negligible next to the decomposition), reusing the caller's storage in place.
void matrix_power(NDArray& out, const NDArray& a, long long n) { copy_into(out, matrix_power(a, n)); }
void solve(NDArray& out, const NDArray& a, const NDArray& b) { copy_into(out, solve(a, b)); }
void inv(NDArray& out, const NDArray& a) { copy_into(out, inv(a)); }
void lstsq(NDArray& out, const NDArray& a, const NDArray& b) { matmul(out, pinv(a), b); }
void cholesky(NDArray& out, const NDArray& a) { copy_into(out, cholesky(a)); }
void pinv(NDArray& out, const NDArray& a) { copy_into(out, pinv(a)); }
void svdvals(NDArray& out, const NDArray& a) { copy_into(out, svdvals(a)); }
void eigvals(CNDArray& out, const NDArray& a) { copy_into(out, eigvals(a)); }
void eigvalsh(NDArray& out, const NDArray& a) { copy_into(out, eigvalsh(a)); }
void eigvalsh(NDArray& out, const CNDArray& a) { copy_into(out, eigvalsh(a)); }

// Multi-output decompositions: one out array per returned factor (struct field order, all outs first).
void qr(NDArray& q, NDArray& r, const NDArray& a) {
    const QR res = qr(a);
    copy_into(q, res.q);
    copy_into(r, res.r);
}
void svd(NDArray& u, NDArray& s, NDArray& vh, const NDArray& a) {
    const SVD res = svd(a);
    copy_into(u, res.u);
    copy_into(s, res.s);
    copy_into(vh, res.vh);
}
void eigh(NDArray& values, NDArray& vectors, const NDArray& a) {
    const Eig res = eigh(a);
    copy_into(values, res.values);
    copy_into(vectors, res.vectors);
}
void eigh(NDArray& values, CNDArray& vectors, const CNDArray& a) {
    const EighC res = eigh(a);
    copy_into(values, res.values);
    copy_into(vectors, res.vectors);
}
void eig(CNDArray& values, CNDArray& vectors, const NDArray& a) {
    const EigC res = eig(a);
    copy_into(values, res.values);
    copy_into(vectors, res.vectors);
}

} // namespace cheatah::linalg
