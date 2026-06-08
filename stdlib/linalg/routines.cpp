#include "routines.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <vector>

// Dense linear-algebra routines on ndarray::NDArray. Algorithms reimplemented from
// standard numerical methods (LU w/ partial pivoting, Cholesky, Householder QR,
// one-sided Jacobi SVD, cyclic Jacobi symmetric eigen, Hessenberg+shifted-QR for
// the general real spectrum). Hot loops are contiguous so -O3 -march=native
// auto-vectorizes them (SIMD). The matrices are real (double) but the general
// eigensolvers return a COMPLEX spectrum (CNDArray) — a real matrix can have
// complex conjugate eigenvalue pairs — built from the real arithmetic below.
namespace cheatah::linalg {

/// \cond INTERNAL
using ndarray::NDArray;
/// \endcond

namespace {

// ---- extract / build contiguous row-major matrices & vectors ----
std::vector<double> as_matrix(const NDArray& a, std::size_t& rows, std::size_t& cols) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    rows = a.shape()[0];
    cols = a.shape()[1];
    std::vector<double> m(rows * cols);
    for (std::size_t i = 0; i < rows; ++i)
        for (std::size_t j = 0; j < cols; ++j) m[i * cols + j] = a.at({i, j});
    return m;
}
std::vector<double> as_vector(const NDArray& a, std::size_t& n) {
    if (a.ndim() == 1) {
        n = a.shape()[0];
        std::vector<double> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = a.at({i});
        return v;
    }
    if (a.ndim() == 2 && (a.shape()[0] == 1 || a.shape()[1] == 1)) {
        n = a.size();
        std::vector<double> v;
        v.reserve(n);
        for (std::size_t i = 0; i < a.shape()[0]; ++i)
            for (std::size_t j = 0; j < a.shape()[1]; ++j) v.push_back(a.at({i, j}));
        return v;
    }
    throw std::runtime_error("linalg: expected a 1-D vector");
}
NDArray make_matrix(std::size_t rows, std::size_t cols, std::vector<double> data) {
    NDArray out(std::vector<std::size_t>{rows, cols});
    *out.buffer() = std::move(data);
    return out;
}
NDArray make_vector(std::vector<double> data) {
    NDArray out(std::vector<std::size_t>{data.size()});
    *out.buffer() = std::move(data);
    return out;
}
CNDArray make_cvector(std::vector<Cplx> data) {
    CNDArray out(std::vector<std::size_t>{data.size()});
    *out.buffer() = std::move(data);
    return out;
}
CNDArray make_cmatrix(std::size_t rows, std::size_t cols, std::vector<Cplx> data) {
    CNDArray out(std::vector<std::size_t>{rows, cols});
    *out.buffer() = std::move(data);
    return out;
}
// Promote a freshly-built (contiguous, offset-0) real result to complex (imag 0).
CNDArray to_complex(const NDArray& a) {
    CNDArray out(a.shape());
    const std::vector<double>& src = *a.buffer();
    *out.buffer() = std::vector<Cplx>(src.begin(), src.end());
    return out;
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
    for (std::size_t i = 0; i < rows; ++i)
        for (std::size_t j = 0; j < cols; ++j) m[i * cols + j] = a.at({i, j});
    return m;
}
std::vector<Cplx> as_cvector(const CNDArray& a, std::size_t& n) {
    if (a.ndim() == 1) {
        n = a.shape()[0];
        std::vector<Cplx> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = a.at({i});
        return v;
    }
    if (a.ndim() == 2 && (a.shape()[0] == 1 || a.shape()[1] == 1)) {
        n = a.size();
        std::vector<Cplx> v;
        v.reserve(n);
        for (std::size_t i = 0; i < a.shape()[0]; ++i)
            for (std::size_t j = 0; j < a.shape()[1]; ++j) v.push_back(a.at({i, j}));
        return v;
    }
    throw std::runtime_error("linalg: expected a 1-D vector");
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

// ---- one-sided Jacobi SVD: A(m×n) = U(m×n) diag(w) V(n×n)ᵀ ----
struct SVDc {
    std::vector<double> u, w, v;
    std::size_t m, n;
};
SVDc svd_jacobi(std::vector<double> b, std::size_t m, std::size_t n) {
    std::vector<double> v(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) v[i * n + i] = 1.0;
    const double eps = 1e-15;
    for (int sweep = 0; sweep < 80; ++sweep) {
        bool rotated = false;
        for (std::size_t p = 0; p < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                double alpha = 0, beta = 0, gamma = 0;
                for (std::size_t i = 0; i < m; ++i) {
                    const double bp = b[i * n + p], bq = b[i * n + q];
                    alpha += bp * bp;
                    beta += bq * bq;
                    gamma += bp * bq;
                }
                if (gamma == 0.0 || std::fabs(gamma) <= eps * std::sqrt(alpha * beta)) continue;
                rotated = true;
                const double zeta = (beta - alpha) / (2.0 * gamma);
                const double t = (zeta >= 0 ? 1.0 : -1.0) / (std::fabs(zeta) + std::sqrt(zeta * zeta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = c * t;
                for (std::size_t i = 0; i < m; ++i) {
                    const double bp = b[i * n + p], bq = b[i * n + q];
                    b[i * n + p] = c * bp - s * bq;
                    b[i * n + q] = s * bp + c * bq;
                }
                for (std::size_t i = 0; i < n; ++i) {
                    const double vp = v[i * n + p], vq = v[i * n + q];
                    v[i * n + p] = c * vp - s * vq;
                    v[i * n + q] = s * vp + c * vq;
                }
            }
        }
        if (!rotated) break;
    }
    std::vector<double> w(n), u(m * n);
    for (std::size_t j = 0; j < n; ++j) {
        double nrm = 0.0;
        for (std::size_t i = 0; i < m; ++i) nrm += b[i * n + j] * b[i * n + j];
        nrm = std::sqrt(nrm);
        w[j] = nrm;
        for (std::size_t i = 0; i < m; ++i) u[i * n + j] = nrm > 0 ? b[i * n + j] / nrm : 0.0;
    }
    std::vector<std::size_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](std::size_t x, std::size_t y) { return w[x] > w[y]; });
    SVDc out{std::vector<double>(m * n), std::vector<double>(n), std::vector<double>(n * n), m, n};
    for (std::size_t j = 0; j < n; ++j) {
        const std::size_t src = idx[j];
        out.w[j] = w[src];
        for (std::size_t i = 0; i < m; ++i) out.u[i * n + j] = u[i * n + src];
        for (std::size_t i = 0; i < n; ++i) out.v[i * n + j] = v[i * n + src];
    }
    return out;
}

// ---- cyclic Jacobi for a real symmetric matrix ----
void jacobi_symmetric(std::vector<double> a, std::size_t n, std::vector<double>& values,
                      std::vector<double>& vectors) {
    std::vector<double> v(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) v[i * n + i] = 1.0;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0;
        for (std::size_t p = 0; p < n; ++p)
            for (std::size_t q = p + 1; q < n; ++q) off += a[p * n + q] * a[p * n + q];
        if (off == 0.0) break;
        for (std::size_t p = 0; p < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                const double apq = a[p * n + q];
                if (apq == 0.0) continue;
                const double theta = (a[q * n + q] - a[p * n + p]) / (2.0 * apq);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c, tau = s / (1.0 + c);
                a[p * n + p] -= t * apq;
                a[q * n + q] += t * apq;
                a[p * n + q] = a[q * n + p] = 0.0;
                for (std::size_t r = 0; r < n; ++r) {
                    if (r == p || r == q) continue;
                    const double arp = a[r * n + p], arq = a[r * n + q];
                    a[r * n + p] = a[p * n + r] = arp - s * (arq + tau * arp);
                    a[r * n + q] = a[q * n + r] = arq + s * (arp - tau * arq);
                }
                for (std::size_t r = 0; r < n; ++r) {
                    const double vrp = v[r * n + p], vrq = v[r * n + q];
                    v[r * n + p] = vrp - s * (vrq + tau * vrp);
                    v[r * n + q] = vrq + s * (vrp - tau * vrq);
                }
            }
        }
    }
    std::vector<std::size_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](std::size_t x, std::size_t y) { return a[x * n + x] > a[y * n + y]; });
    values.resize(n);
    vectors.assign(n * n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        values[j] = a[idx[j] * n + idx[j]];
        for (std::size_t i = 0; i < n; ++i) vectors[i * n + j] = v[i * n + idx[j]];
    }
}

bool is_symmetric(const std::vector<double>& a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
            if (std::fabs(a[i * n + j] - a[j * n + i]) > 1e-12 * (1 + std::fabs(a[i * n + j])))
                return false;
    return true;
}

// Complex Hermitian eigensolver with REAL eigenvalues and COMPLEX eigenvectors,
// reusing the real symmetric Jacobi solver via the standard 2n×2n real embedding:
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
    jacobi_symmetric(M, N, w, V);               // 2n eigenvalues (desc, paired) + vectors
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

// Solve the complex linear system M·x = b (M row-major n×n) by LU with partial
// pivoting. Used by inverse iteration; M there is deliberately near-singular, which
// LU handles (it yields a large solution pointing along the eigenvector).
std::vector<Cplx> complex_solve(std::vector<Cplx> M, std::vector<Cplx> b, std::size_t n) {
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t piv = k;
        double best = std::abs(M[k * n + k]);
        for (std::size_t i = k + 1; i < n; ++i) {
            const double m = std::abs(M[i * n + k]);
            if (m > best) {
                best = m;
                piv = i;
            }
        }
        if (piv != k) {
            for (std::size_t j = 0; j < n; ++j) std::swap(M[k * n + j], M[piv * n + j]);
            std::swap(b[k], b[piv]);
        }
        const Cplx d = M[k * n + k];
        for (std::size_t i = k + 1; i < n; ++i) {
            const Cplx f = M[i * n + k] / d;
            for (std::size_t j = k; j < n; ++j) M[i * n + j] -= f * M[k * n + j];
            b[i] -= f * b[k];
        }
    }
    std::vector<Cplx> x(n);
    for (std::size_t ii = n; ii-- > 0;) {
        Cplx s = b[ii];
        for (std::size_t j = ii + 1; j < n; ++j) s -= M[ii * n + j] * x[j];
        x[ii] = s / M[ii * n + ii];
    }
    return x;
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
    std::vector<Cplx> v(n, Cplx(1.0, 0.0));
    normalize(v);
    for (int it = 0; it < 5; ++it) {
        std::vector<Cplx> w = complex_solve(C, v, n);
        normalize(w);
        v = std::move(w);
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
    for (std::size_t k = 1; k + 1 < n; ++k) {
        double scale = 0.0;
        for (std::size_t i = k; i < n; ++i) scale += std::fabs(a[i * n + (k - 1)]);
        if (scale == 0.0) continue;
        double h = 0.0;
        std::vector<double> u(n, 0.0);
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
            std::vector<double> cs, sn;
            for (long long i = l; i < hi; ++i) {
                const double x = a[i * n + i], y = a[(i + 1) * n + i];
                const double r = std::hypot(x, y);
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

}  // namespace

// ================= public routines =================

// ---- products ----
double dot(const NDArray& a, const NDArray& b) {
    std::size_t n, m;
    const std::vector<double> x = as_vector(a, n), y = as_vector(b, m);
    if (n != m) throw std::runtime_error("linalg: dot dimension mismatch");
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += x[i] * y[i];
    return s;
}
double vdot(const NDArray& a, const NDArray& b) { return dot(a, b); }
double inner(const NDArray& a, const NDArray& b) { return dot(a, b); }

// Complex products. `dot`/`inner` are bilinear (Σ aᵢbᵢ, no conjugation, like numpy);
// `vdot` is the conjugate-linear Hermitian inner product Σ conj(aᵢ)·bᵢ = ⟨a, b⟩.
Cplx dot(const CNDArray& a, const CNDArray& b) {
    std::size_t n, m;
    const std::vector<Cplx> x = as_cvector(a, n), y = as_cvector(b, m);
    if (n != m) throw std::runtime_error("linalg: dot dimension mismatch");
    Cplx s{};
    for (std::size_t i = 0; i < n; ++i) s += x[i] * y[i];
    return s;
}
Cplx vdot(const CNDArray& a, const CNDArray& b) {
    std::size_t n, m;
    const std::vector<Cplx> x = as_cvector(a, n), y = as_cvector(b, m);
    if (n != m) throw std::runtime_error("linalg: dot dimension mismatch");
    Cplx s{};
    for (std::size_t i = 0; i < n; ++i) s += std::conj(x[i]) * y[i];
    return s;
}

NDArray outer(const NDArray& a, const NDArray& b) {
    std::size_t n, m;
    const std::vector<double> x = as_vector(a, n), y = as_vector(b, m);
    std::vector<double> r(n * m);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < m; ++j) r[i * m + j] = x[i] * y[j];
    return make_matrix(n, m, std::move(r));
}

NDArray matmul(const NDArray& a, const NDArray& b) {
    std::size_t ar, ac, br, bc;
    const std::vector<double> A = as_matrix(a, ar, ac);
    const std::vector<double> B = as_matrix(b, br, bc);
    if (ac != br) throw std::runtime_error("linalg: matmul inner dimension mismatch");
    std::vector<double> C(ar * bc, 0.0);
    for (std::size_t i = 0; i < ar; ++i)  // ikj order -> contiguous inner loop (vectorizes)
        for (std::size_t k = 0; k < ac; ++k) {
            const double aik = A[i * ac + k];
            for (std::size_t j = 0; j < bc; ++j) C[i * bc + j] += aik * B[k * bc + j];
        }
    return make_matrix(ar, bc, std::move(C));
}

CNDArray matmul(const CNDArray& a, const CNDArray& b) {
    std::size_t ar, ac, br, bc;
    const std::vector<Cplx> A = as_cmatrix(a, ar, ac);
    const std::vector<Cplx> B = as_cmatrix(b, br, bc);
    if (ac != br) throw std::runtime_error("linalg: matmul inner dimension mismatch");
    std::vector<Cplx> C(ar * bc, Cplx{});
    for (std::size_t i = 0; i < ar; ++i)
        for (std::size_t k = 0; k < ac; ++k) {
            const Cplx aik = A[i * ac + k];
            for (std::size_t j = 0; j < bc; ++j) C[i * bc + j] += aik * B[k * bc + j];
        }
    return make_cmatrix(ar, bc, std::move(C));
}

// Conjugate transpose (Hermitian adjoint) Aᴴ: transpose, then conjugate every entry.
CNDArray conj_transpose(const CNDArray& a) {
    std::size_t r, c;
    const std::vector<Cplx> A = as_cmatrix(a, r, c);
    std::vector<Cplx> T(c * r);
    for (std::size_t i = 0; i < r; ++i)
        for (std::size_t j = 0; j < c; ++j) T[j * r + i] = std::conj(A[i * c + j]);
    return make_cmatrix(c, r, std::move(T));
}

NDArray matrix_power(const NDArray& a, long long p) {
    std::size_t r, c;
    as_matrix(a, r, c);
    require_square(r, c);
    std::vector<double> result(r * r, 0.0);
    for (std::size_t i = 0; i < r; ++i) result[i * r + i] = 1.0;  // identity
    NDArray acc = make_matrix(r, r, result);
    NDArray base = (p < 0) ? inv(a) : a;
    long long e = p < 0 ? -p : p;
    while (e > 0) {
        if (e & 1) acc = matmul(acc, base);
        base = matmul(base, base);
        e >>= 1;
    }
    return acc;
}

NDArray kron(const NDArray& a, const NDArray& b) {
    std::size_t ar, ac, br, bc;
    const std::vector<double> A = as_matrix(a, ar, ac);
    const std::vector<double> B = as_matrix(b, br, bc);
    std::vector<double> K(ar * br * ac * bc, 0.0);
    const std::size_t kc = ac * bc;
    for (std::size_t i = 0; i < ar; ++i)
        for (std::size_t j = 0; j < ac; ++j)
            for (std::size_t p = 0; p < br; ++p)
                for (std::size_t q = 0; q < bc; ++q)
                    K[(i * br + p) * kc + (j * bc + q)] = A[i * ac + j] * B[p * bc + q];
    return make_matrix(ar * br, kc, std::move(K));
}

double trace(const NDArray& a) {
    std::size_t r, c;
    const std::vector<double> A = as_matrix(a, r, c);
    double s = 0.0;
    for (std::size_t i = 0; i < std::min(r, c); ++i) s += A[i * c + i];
    return s;
}

double norm(const NDArray& a) {  // Frobenius (matrices) / L2 (vectors)
    double s = 0.0;
    if (a.ndim() <= 1) {
        std::size_t n;
        const std::vector<double> v = as_vector(a, n);
        for (double x : v) s += x * x;
    } else {
        std::size_t r, c;
        const std::vector<double> m = as_matrix(a, r, c);
        for (double x : m) s += x * x;
    }
    return std::sqrt(s);
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
    std::vector<double> result(n * n, 0.0);
    for (std::size_t col = 0; col < n; ++col) {
        std::vector<double> e(n, 0.0);
        e[col] = 1.0;
        lu_solve(lu, e);
        for (std::size_t i = 0; i < n; ++i) result[i * n + col] = e[i];
    }
    return make_matrix(n, n, std::move(result));
}

NDArray lstsq(const NDArray& a, const NDArray& b) {  // min ‖Ax−b‖ via the pseudo-inverse
    return matmul(pinv(a), b);
}

// ---- Cholesky ----
NDArray cholesky(const NDArray& a) {
    std::size_t n, c;
    const std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    std::vector<double> L(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double s = A[i * n + j];
            for (std::size_t k = 0; k < j; ++k) s -= L[i * n + k] * L[j * n + k];
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
    std::vector<double> Q(m * m, 0.0);
    for (std::size_t i = 0; i < m; ++i) Q[i * m + i] = 1.0;
    for (std::size_t k = 0; k < n; ++k) {
        double nrm = 0.0;
        for (std::size_t i = k; i < m; ++i) nrm += A[i * n + k] * A[i * n + k];
        nrm = std::sqrt(nrm);
        if (nrm == 0.0) continue;
        const double alpha = A[k * n + k] >= 0 ? -nrm : nrm;
        std::vector<double> u(m, 0.0);
        for (std::size_t i = k; i < m; ++i) u[i] = A[i * n + k];
        u[k] -= alpha;
        double unorm2 = 0.0;
        for (std::size_t i = k; i < m; ++i) unorm2 += u[i] * u[i];
        if (unorm2 == 0.0) continue;
        for (std::size_t j = k; j < n; ++j) {
            double s = 0.0;
            for (std::size_t i = k; i < m; ++i) s += u[i] * A[i * n + j];
            s = 2.0 * s / unorm2;
            for (std::size_t i = k; i < m; ++i) A[i * n + j] -= s * u[i];
        }
        for (std::size_t j = 0; j < m; ++j) {  // Q = Q · Hₖ
            double s = 0.0;
            for (std::size_t i = k; i < m; ++i) s += u[i] * Q[j * m + i];
            s = 2.0 * s / unorm2;
            for (std::size_t i = k; i < m; ++i) Q[j * m + i] -= s * u[i];
        }
    }
    std::vector<double> Qr(m * n), Rr(n * n, 0.0);  // reduced
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < n; ++j) Qr[i * n + j] = Q[i * m + j];
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i; j < n; ++j) Rr[i * n + j] = A[i * n + j];
    return {make_matrix(m, n, std::move(Qr)), make_matrix(n, n, std::move(Rr))};
}

// ---- SVD and its derived quantities ----
SVD svd(const NDArray& a) {
    std::size_t m, n;
    std::vector<double> A = as_matrix(a, m, n);
    if (m < n) throw std::runtime_error("linalg: svd requires rows >= cols (transpose otherwise)");
    const SVDc s = svd_jacobi(std::move(A), m, n);
    // vh = Vᵀ
    std::vector<double> vh(n * n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) vh[i * n + j] = s.v[j * n + i];
    return {make_matrix(m, n, s.u), make_vector(s.w), make_matrix(n, n, std::move(vh))};
}

NDArray pinv(const NDArray& a) {
    std::size_t m, n;
    std::vector<double> A = as_matrix(a, m, n);
    if (m >= n) {
        const SVDc s = svd_jacobi(std::move(A), m, n);  // A = U(m×n) diag(w) V(n×n)ᵀ
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
    const SVDc s = svd_jacobi(std::move(At), n, m);  // Aᵀ = U(n×m) diag(w) V(m×m)ᵀ
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
        s = svd_jacobi(std::move(A), m, n);
    } else {
        std::vector<double> At(n * m);
        for (std::size_t i = 0; i < m; ++i)
            for (std::size_t j = 0; j < n; ++j) At[j * m + i] = A[i * n + j];
        s = svd_jacobi(std::move(At), n, m);
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
        s = svd_jacobi(std::move(At), n, m);
    } else {
        s = svd_jacobi(std::move(A), m, n);
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
    const std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    std::vector<double> vals, vecs;
    jacobi_symmetric(A, n, vals, vecs);
    return {make_vector(std::move(vals)), make_matrix(n, n, std::move(vecs))};
}
NDArray eigvalsh(const NDArray& a) {
    std::size_t n, c;
    const std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    std::vector<double> vals, vecs;
    jacobi_symmetric(A, n, vals, vecs);
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
    const std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    if (is_symmetric(A, n)) {                // symmetric -> real spectrum + eigenvectors
        const Eig r = eigh(a);
        return {to_complex(r.values), to_complex(r.vectors)};
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
    const std::vector<double> A = as_matrix(a, n, c);
    require_square(n, c);
    std::vector<Cplx> vals;
    if (is_symmetric(A, n)) {                // symmetric -> real spectrum via Jacobi
        std::vector<double> rvals, vecs;
        jacobi_symmetric(A, n, rvals, vecs);
        vals.assign(rvals.begin(), rvals.end());
    } else {
        vals = eigvals_general(A, n);
    }
    std::sort(vals.begin(), vals.end(), cgreater);
    return make_cvector(std::move(vals));
}

} // namespace cheatah::linalg
