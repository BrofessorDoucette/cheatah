// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "ndarray.hpp"
#include "routines.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace nd = cheatah::ndarray;
namespace la = cheatah::linalg;

namespace {
nd::NDArray mat(std::size_t r, std::size_t c, std::vector<double> data) {
    return nd::reshape(nd::array(std::move(data)), {(long long)r, (long long)c});
}
bool close(double a, double b, double tol = 1e-9) { return std::fabs(a - b) < tol; }
// The general eig()/eigvals() return a complex spectrum; these read one element and
// compare against a complex (or, implicitly, a real) expectation.
std::complex<double> cget(const la::CNDArray& v, std::vector<long long> idx) {
    return nd::get(v, std::move(idx));
}
bool cclose(std::complex<double> a, std::complex<double> b, double tol = 1e-6) {
    return std::abs(a - b) < tol;
}
using C = std::complex<double>;
la::CNDArray cvec(std::vector<C> data) { return nd::array(std::move(data)); }
la::CNDArray cmat(std::size_t r, std::size_t c, std::vector<C> data) {
    return nd::reshape(nd::array(std::move(data)), {(long long)r, (long long)c});
}
}  // namespace

TEST(LinalgRoutines, ProductsAndTrace) {
    const nd::NDArray a = mat(2, 3, {1, 2, 3, 4, 5, 6});
    const nd::NDArray b = mat(3, 2, {7, 8, 9, 10, 11, 12});
    const nd::NDArray c = la::matmul(a, b);  // [[58,64],[139,154]]
    EXPECT_DOUBLE_EQ(nd::get(c, {0, 0}), 58);
    EXPECT_DOUBLE_EQ(nd::get(c, {0, 1}), 64);
    EXPECT_DOUBLE_EQ(nd::get(c, {1, 1}), 154);
    EXPECT_DOUBLE_EQ(la::dot(nd::array({1.0, 2.0, 3.0}), nd::array({4.0, 5.0, 6.0})), 32);  // 4+10+18
    EXPECT_DOUBLE_EQ(la::trace(mat(2, 2, {1, 2, 3, 4})), 5);  // 1+4
}

// The user-provided-output overload writes the SAME result into the caller's buffer with NO
// reallocation: the buffer's data pointer is identical before and after, and the values are correct.
TEST(LinalgRoutines, MatmulIntoReusesBuffer) {
    const nd::NDArray a = mat(2, 3, {1, 2, 3, 4, 5, 6});
    const nd::NDArray b = mat(3, 2, {7, 8, 9, 10, 11, 12});
    nd::NDArray out = nd::zeros({2, 2});
    const double* const before = out.buffer()->data() + out.offset();  // capture the buffer identity
    la::matmul(out, a, b);                                             // [[58,64],[139,154]] into out
    EXPECT_EQ(out.buffer()->data() + out.offset(), before);           // SAME buffer — no reallocation
    EXPECT_DOUBLE_EQ(nd::get(out, {0, 0}), 58);
    EXPECT_DOUBLE_EQ(nd::get(out, {0, 1}), 64);
    EXPECT_DOUBLE_EQ(nd::get(out, {1, 0}), 139);
    EXPECT_DOUBLE_EQ(nd::get(out, {1, 1}), 154);
    // matches the allocating overload exactly
    const nd::NDArray c = la::matmul(a, b);
    EXPECT_DOUBLE_EQ(nd::get(out, {1, 1}), nd::get(c, {1, 1}));
    // a wrong-shaped out, and an out that aliases an input (matmul is not in-place), are rejected.
    nd::NDArray wrong = nd::zeros({3, 3});
    EXPECT_THROW(la::matmul(wrong, a, b), std::runtime_error);
    nd::NDArray sq = mat(2, 2, {1, 2, 3, 4});
    EXPECT_THROW(la::matmul(sq, sq, sq), std::runtime_error);
    // a non-2-D operand to the out-form is rejected up front ("expects 2-D matrices").
    nd::NDArray vec = nd::array({1.0, 2.0, 3.0});  // 1-D
    nd::NDArray out2 = nd::zeros({2, 2});
    EXPECT_THROW(la::matmul(out2, vec, b), std::runtime_error);
}

// The memory-bound products / transposes have GENUINELY zero-allocation out-param overloads: they
// write their kernel straight into the caller's buffer (data pointer identical before/after), match
// the allocating overload, and reject a wrong shape or an out that aliases an input.
TEST(LinalgRoutines, OuterIntoReusesBuffer) {
    const nd::NDArray a = nd::array({1.0, 2.0, 3.0});
    const nd::NDArray b = nd::array({4.0, 5.0});
    nd::NDArray out = nd::zeros({3, 2});
    const auto* before = out.buffer().get();
    la::outer(out, a, b);
    EXPECT_EQ(out.buffer().get(), before);  // SAME buffer — no reallocation
    const nd::NDArray ref = la::outer(a, b);
    EXPECT_DOUBLE_EQ(nd::get(out, {0, 0}), 4.0);                       // 1*4
    EXPECT_DOUBLE_EQ(nd::get(out, {2, 1}), nd::get(ref, {2, 1}));      // 3*5, matches allocating form
    nd::NDArray wrong = nd::zeros({2, 2});
    EXPECT_THROW(la::outer(wrong, a, b), std::runtime_error);
}

TEST(LinalgRoutines, KronIntoReusesBuffer) {
    const nd::NDArray a = mat(2, 2, {1, 0, 0, 1});
    const nd::NDArray b = mat(2, 2, {1, 2, 3, 4});
    nd::NDArray out = nd::zeros({4, 4});
    const auto* before = out.buffer().get();
    la::kron(out, a, b);
    EXPECT_EQ(out.buffer().get(), before);
    const nd::NDArray ref = la::kron(a, b);
    EXPECT_DOUBLE_EQ(nd::get(out, {0, 1}), nd::get(ref, {0, 1}));
    EXPECT_DOUBLE_EQ(nd::get(out, {3, 3}), nd::get(ref, {3, 3}));
    // A non-2-D operand is rejected up front ("kron expects 2-D matrices").
    nd::NDArray vec = nd::array({1.0, 2.0});  // 1-D
    EXPECT_THROW(la::kron(out, vec, b), std::runtime_error);
    // An out that ALIASES an input is rejected by reject_alias (kron is not computed in place).
    nd::NDArray alias = mat(2, 2, {1, 0, 0, 1});
    EXPECT_THROW(la::kron(alias, alias, b), std::runtime_error);
}

TEST(LinalgRoutines, ConjTransposeIntoReusesBuffer) {
    const la::CNDArray M = cmat(2, 3, {C(1, 1), C(2, 0), C(3, -1), C(0, 2), C(1, 0), C(4, 4)});
    la::CNDArray out = cmat(3, 2, std::vector<C>(6));
    const auto* before = out.buffer().get();
    la::conj_transpose(out, M);
    EXPECT_EQ(out.buffer().get(), before);
    const la::CNDArray ref = la::conj_transpose(M);
    EXPECT_TRUE(cclose(cget(out, {0, 0}), C(1, -1)));
    EXPECT_TRUE(cclose(cget(out, {2, 1}), cget(ref, {2, 1})));
}

TEST(LinalgRoutines, ComplexMatmulIntoReusesBuffer) {
    const la::CNDArray a = cmat(2, 3, {C(1, 0), C(2, 0), C(3, 0), C(4, 0), C(5, 0), C(6, 0)});
    const la::CNDArray b = cmat(3, 2, {C(1, 1), C(0, 0), C(0, 1), C(1, 0), C(2, 0), C(0, 1)});
    la::CNDArray out = cmat(2, 2, std::vector<C>(4));
    const auto* before = out.buffer().get();
    la::matmul(out, a, b);
    EXPECT_EQ(out.buffer().get(), before);
    const la::CNDArray ref = la::matmul(a, b);
    EXPECT_TRUE(cclose(cget(out, {0, 0}), cget(ref, {0, 0})));
    EXPECT_TRUE(cclose(cget(out, {1, 1}), cget(ref, {1, 1})));
}

// The O(n³) factorizations reuse the caller's OUTPUT buffer (data pointer identical before/after)
// and match the allocating overload — their internal factorization workspace is allocated regardless.
TEST(LinalgRoutines, FactorizationOutReusesBuffer) {
    const nd::NDArray A = mat(2, 2, {4, 3, 6, 3});
    const nd::NDArray spd = mat(2, 2, {4, 2, 2, 3});
    const nd::NDArray sym = mat(2, 2, {2, 1, 1, 2});
    const nd::NDArray gen = mat(2, 2, {2, 0, 0, 5});
    {  // solve
        nd::NDArray out = nd::zeros({2});
        const auto* b = out.buffer().get();
        la::solve(out, A, nd::array({10.0, 12.0}));
        EXPECT_EQ(out.buffer().get(), b);
        EXPECT_TRUE(close(nd::get(out, {0}), 1.0));
        EXPECT_TRUE(close(nd::get(out, {1}), 2.0));
    }
    {  // inv
        nd::NDArray out = nd::zeros({2, 2});
        const auto* b = out.buffer().get();
        la::inv(out, A);
        EXPECT_EQ(out.buffer().get(), b);
        const nd::NDArray ref = la::inv(A);
        EXPECT_TRUE(close(nd::get(out, {0, 0}), nd::get(ref, {0, 0})));
    }
    {  // lstsq (2-D column rhs); on a square system it equals solve
        nd::NDArray out = nd::zeros({2, 1});
        const auto* b = out.buffer().get();
        la::lstsq(out, A, mat(2, 1, {10, 12}));
        EXPECT_EQ(out.buffer().get(), b);
        EXPECT_TRUE(close(nd::get(out, {0, 0}), 1.0, 1e-6));
    }
    {  // cholesky
        nd::NDArray out = nd::zeros({2, 2});
        const auto* b = out.buffer().get();
        la::cholesky(out, spd);
        EXPECT_EQ(out.buffer().get(), b);
        const nd::NDArray ref = la::cholesky(spd);
        EXPECT_TRUE(close(nd::get(out, {0, 0}), nd::get(ref, {0, 0})));
    }
    {  // pinv
        nd::NDArray out = nd::zeros({2, 2});
        const auto* b = out.buffer().get();
        la::pinv(out, A);
        EXPECT_EQ(out.buffer().get(), b);
        const nd::NDArray ref = la::pinv(A);
        EXPECT_TRUE(close(nd::get(out, {0, 0}), nd::get(ref, {0, 0}), 1e-6));
    }
    {  // matrix_power
        nd::NDArray out = nd::zeros({2, 2});
        const auto* b = out.buffer().get();
        la::matrix_power(out, A, 2);
        EXPECT_EQ(out.buffer().get(), b);
        const nd::NDArray ref = la::matrix_power(A, 2);
        EXPECT_TRUE(close(nd::get(out, {0, 0}), nd::get(ref, {0, 0})));
    }
    {  // svdvals
        nd::NDArray out = nd::zeros({2});
        const auto* b = out.buffer().get();
        la::svdvals(out, sym);
        EXPECT_EQ(out.buffer().get(), b);
        EXPECT_TRUE(close(nd::get(out, {0}), 3.0, 1e-6));
    }
    {  // eigvalsh (symmetric)
        nd::NDArray out = nd::zeros({2});
        const auto* b = out.buffer().get();
        la::eigvalsh(out, sym);
        EXPECT_EQ(out.buffer().get(), b);
        EXPECT_TRUE(close(nd::get(out, {0}), 3.0, 1e-6));
    }
    {  // eigvals (general, complex out)
        la::CNDArray out = cvec(std::vector<C>(2));
        const auto* b = out.buffer().get();
        la::eigvals(out, gen);
        EXPECT_EQ(out.buffer().get(), b);
        EXPECT_TRUE(cclose(cget(out, {0}), 5.0));
    }
    {  // eigvalsh (complex Hermitian, real out)
        const la::CNDArray H = cmat(2, 2, {C(2, 0), C(1, 1), C(1, -1), C(3, 0)});
        nd::NDArray out = nd::zeros({2});
        const auto* b = out.buffer().get();
        la::eigvalsh(out, H);
        EXPECT_EQ(out.buffer().get(), b);
        EXPECT_TRUE(close(nd::get(out, {0}), 4.0, 1e-6));
    }
}

// The multi-output decompositions reuse EVERY caller-provided output buffer (one per factor).
TEST(LinalgRoutines, DecompositionOutReusesBuffer) {
    {  // qr
        const nd::NDArray A = mat(3, 2, {1, 0, 1, 1, 0, 1});
        nd::NDArray q = nd::zeros({3, 2}), r = nd::zeros({2, 2});
        const auto* bq = q.buffer().get();
        const auto* br = r.buffer().get();
        la::qr(q, r, A);
        EXPECT_EQ(q.buffer().get(), bq);
        EXPECT_EQ(r.buffer().get(), br);
        const la::QR ref = la::qr(A);
        EXPECT_TRUE(close(nd::get(q, {0, 0}), nd::get(ref.q, {0, 0}), 1e-6));
        EXPECT_TRUE(close(nd::get(r, {0, 0}), nd::get(ref.r, {0, 0}), 1e-6));
    }
    {  // svd
        const nd::NDArray A = mat(2, 2, {2, 0, 0, 3});
        nd::NDArray u = nd::zeros({2, 2}), s = nd::zeros({2}), vh = nd::zeros({2, 2});
        const auto* bu = u.buffer().get();
        const auto* bs = s.buffer().get();
        const auto* bv = vh.buffer().get();
        la::svd(u, s, vh, A);
        EXPECT_EQ(u.buffer().get(), bu);
        EXPECT_EQ(s.buffer().get(), bs);
        EXPECT_EQ(vh.buffer().get(), bv);
        EXPECT_TRUE(close(nd::get(s, {0}), 3.0, 1e-6));
    }
    {  // eigh (symmetric, real)
        nd::NDArray vals = nd::zeros({2}), vecs = nd::zeros({2, 2});
        const auto* bvl = vals.buffer().get();
        const auto* bvc = vecs.buffer().get();
        la::eigh(vals, vecs, mat(2, 2, {2, 1, 1, 2}));
        EXPECT_EQ(vals.buffer().get(), bvl);
        EXPECT_EQ(vecs.buffer().get(), bvc);
        EXPECT_TRUE(close(nd::get(vals, {0}), 3.0, 1e-6));
    }
    {  // eigh (complex Hermitian: real values, complex vectors)
        const la::CNDArray H = cmat(2, 2, {C(2, 0), C(1, 1), C(1, -1), C(3, 0)});
        nd::NDArray vals = nd::zeros({2});
        la::CNDArray vecs = cmat(2, 2, std::vector<C>(4));
        const auto* bvl = vals.buffer().get();
        const auto* bvc = vecs.buffer().get();
        la::eigh(vals, vecs, H);
        EXPECT_EQ(vals.buffer().get(), bvl);
        EXPECT_EQ(vecs.buffer().get(), bvc);
        EXPECT_TRUE(close(nd::get(vals, {0}), 4.0, 1e-6));
    }
    {  // eig (general, complex values + vectors)
        la::CNDArray vals = cvec(std::vector<C>(2));
        la::CNDArray vecs = cmat(2, 2, std::vector<C>(4));
        const auto* bvl = vals.buffer().get();
        const auto* bvc = vecs.buffer().get();
        la::eig(vals, vecs, mat(2, 2, {2, 0, 0, 5}));
        EXPECT_EQ(vals.buffer().get(), bvl);
        EXPECT_EQ(vecs.buffer().get(), bvc);
        EXPECT_TRUE(cclose(cget(vals, {0}), 5.0));
    }
}

TEST(LinalgRoutines, SolveDetInv) {
    const nd::NDArray A = mat(2, 2, {4, 3, 6, 3});  // det = 12-18 = -6
    EXPECT_TRUE(close(la::det(A), -6.0));
    const nd::NDArray x = la::solve(A, nd::array({10.0, 12.0}));  // 4x+3y=10, 6x+3y=12 -> x=1,y=2
    EXPECT_TRUE(close(nd::get(x, {0}), 1.0));
    EXPECT_TRUE(close(nd::get(x, {1}), 2.0));
    const nd::NDArray Ai = la::inv(A);
    const nd::NDArray I = la::matmul(A, Ai);  // identity
    EXPECT_TRUE(close(nd::get(I, {0, 0}), 1.0));
    EXPECT_TRUE(close(nd::get(I, {0, 1}), 0.0));
    EXPECT_TRUE(close(nd::get(I, {1, 1}), 1.0));
}

TEST(LinalgRoutines, CholeskyAndQR) {
    const nd::NDArray A = mat(2, 2, {4, 2, 2, 3});  // SPD
    const nd::NDArray L = la::cholesky(A);
    const nd::NDArray LLt = la::matmul(L, la::matmul(la::inv(L), A));  // == A trivially; check L Lᵀ:
    // verify L·Lᵀ == A
    const nd::NDArray Lt = mat(2, 2, {nd::get(L, {0, 0}), nd::get(L, {1, 0}),
                                      nd::get(L, {0, 1}), nd::get(L, {1, 1})});
    const nd::NDArray rec = la::matmul(L, Lt);
    EXPECT_TRUE(close(nd::get(rec, {0, 0}), 4.0));
    EXPECT_TRUE(close(nd::get(rec, {1, 1}), 3.0));

    const la::QR qr = la::qr(mat(3, 2, {1, 0, 1, 1, 0, 1}));
    const nd::NDArray QtQ = la::matmul(la::pinv(qr.q), qr.q);  // Q has orthonormal cols
    EXPECT_TRUE(close(nd::get(QtQ, {0, 0}), 1.0, 1e-6));
    const nd::NDArray reQR = la::matmul(qr.q, qr.r);  // == original
    EXPECT_TRUE(close(nd::get(reQR, {0, 0}), 1.0, 1e-6));
    EXPECT_TRUE(close(nd::get(reQR, {1, 1}), 1.0, 1e-6));
}

TEST(LinalgRoutines, SvdAndEigh) {
    const nd::NDArray A = mat(2, 2, {2, 0, 0, 3});
    const la::SVD s = la::svd(A);
    EXPECT_TRUE(close(nd::get(s.s, {0}), 3.0, 1e-6));  // singular values 3, 2 (descending)
    EXPECT_TRUE(close(nd::get(s.s, {1}), 2.0, 1e-6));
    // svdvals (values-only fast path) agrees with svd().s
    const nd::NDArray sv = la::svdvals(A);
    EXPECT_TRUE(close(nd::get(sv, {0}), 3.0, 1e-6));
    EXPECT_TRUE(close(nd::get(sv, {1}), 2.0, 1e-6));

    // symmetric eigen: [[2,1],[1,2]] -> eigenvalues 3, 1
    const la::Eig e = la::eigh(mat(2, 2, {2, 1, 1, 2}));
    EXPECT_TRUE(close(nd::get(e.values, {0}), 3.0, 1e-6));
    EXPECT_TRUE(close(nd::get(e.values, {1}), 1.0, 1e-6));

    // general eigenvalues of [[2,0],[0,5]] -> 5, 2 (real, returned as complex)
    const la::CNDArray ev = la::eigvals(mat(2, 2, {2, 0, 0, 5}));
    EXPECT_TRUE(cclose(cget(ev, {0}), 5.0));
    EXPECT_TRUE(cclose(cget(ev, {1}), 2.0));
}

TEST(LinalgRoutines, ComplexProducts) {
    const la::CNDArray a = cvec({C(1, 2), C(3, -1)});
    const la::CNDArray b = cvec({C(0, 1), C(2, 0)});
    // Bilinear dot (no conjugation): (1+2j)(0+1j) + (3-1j)(2) = (-2+1j) + (6-2j) = 4-1j.
    EXPECT_TRUE(cclose(la::dot(a, b), C(4, -1)));
    // Hermitian inner product (conjugate the first): conj(a)·b = (1-2j)(0+1j)+(3+1j)(2) = (2+1j)+(6+2j) = 8+3j.
    EXPECT_TRUE(cclose(la::vdot(a, b), C(8, 3)));
    // vdot(a,a) is the real squared norm ‖a‖² = 1+4+9+1 = 15.
    EXPECT_TRUE(cclose(la::vdot(a, a), C(15, 0)));

    // Conjugate transpose (Hermitian adjoint): transpose + conjugate every entry.
    const la::CNDArray M = cmat(2, 2, {C(1, 1), C(2, 0), C(0, 0), C(3, -1)});
    const la::CNDArray H = la::conj_transpose(M);  // [[1-1j, 0],[2, 3+1j]]
    EXPECT_TRUE(cclose(cget(H, {0, 0}), C(1, -1)));
    EXPECT_TRUE(cclose(cget(H, {0, 1}), C(0, 0)));
    EXPECT_TRUE(cclose(cget(H, {1, 0}), C(2, 0)));
    EXPECT_TRUE(cclose(cget(H, {1, 1}), C(3, 1)));

    // Complex matmul: M · Mᴴ is Hermitian; check entry (0,0) = |1+1j|² + |2|² = 2 + 4 = 6.
    const la::CNDArray P = la::matmul(M, H);
    EXPECT_TRUE(cclose(cget(P, {0, 0}), C(6, 0)));
    EXPECT_TRUE(cclose(cget(P, {1, 1}), C(10, 0)));  // |0|² + |3-1j|² = 0 + 10

    // as_cvector accepts a 2-D N×1 / 1×N as a flat vector (like the real path)…
    const la::CNDArray col = cmat(2, 1, {C(1, 0), C(0, 1)});
    EXPECT_TRUE(cclose(la::dot(col, col), C(0, 0)));  // 1·1 + i·i = 1 − 1 = 0
    // …and rejects a genuine 2-D matrix where a vector is required.
    EXPECT_THROW(la::vdot(M, M), std::runtime_error);
}

TEST(LinalgRoutines, GeneralEigVectors) {
    // For a real matrix, eig() returns complex eigenvalues AND eigenvectors (via
    // inverse iteration). Verify A·v_k = λ_k·v_k for each column (phase-independent),
    // building a complex copy Ac of A so we can multiply the complex eigenvectors.
    const auto check = [](const std::vector<double>& data, std::size_t n) {
        const nd::NDArray A = mat(n, n, data);
        std::vector<C> cdata;
        for (double x : data) cdata.push_back(C(x, 0.0));
        const la::CNDArray Ac = cmat(n, n, cdata);
        const la::EigC e = la::eig(A);
        const la::CNDArray AV = la::matmul(Ac, e.vectors);
        for (std::size_t k = 0; k < n; ++k) {
            const C lam = cget(e.values, {(long long)k});
            for (std::size_t r = 0; r < n; ++r) {
                EXPECT_TRUE(cclose(cget(AV, {(long long)r, (long long)k}),
                                   lam * cget(e.vectors, {(long long)r, (long long)k}), 1e-5));
            }
        }
    };
    check({2, 1, 0, 3}, 2);          // real eigenvalues 3, 2 (upper-triangular)
    check({0, -1, 1, 0}, 2);         // complex conjugate pair ±i (rotation)
    check({0, 1, 2, 0}, 2);          // eigenvalues ±√2; forces a pivot row-swap in the solve
    check({2, 1, 1, 1, 2, 1, 1, 1, 2}, 3);  // symmetric -> real eigenvalues 4,1,1
}

TEST(LinalgRoutines, ComplexHermitianEigh) {
    // H = [[2, 1+i],[1-i, 3]] is Hermitian (conj_transpose(H) == H); eigenvalues 4, 1.
    const la::CNDArray H = cmat(2, 2, {C(2, 0), C(1, 1), C(1, -1), C(3, 0)});
    const nd::NDArray w = la::eigvalsh(H);  // real, descending
    EXPECT_TRUE(close(nd::get(w, {0}), 4.0, 1e-6));
    EXPECT_TRUE(close(nd::get(w, {1}), 1.0, 1e-6));

    const la::EighC e = la::eigh(H);
    EXPECT_TRUE(close(nd::get(e.values, {0}), 4.0, 1e-6));
    EXPECT_TRUE(close(nd::get(e.values, {1}), 1.0, 1e-6));
    // Verify the eigenpairs: H·V should equal V·diag(λ), independent of eigenvector
    // phase. So column k of H·V equals λ_k · column k of V.
    const la::CNDArray HV = la::matmul(H, e.vectors);
    for (int k = 0; k < 2; ++k) {
        const double lam = nd::get(e.values, {k});
        for (int r = 0; r < 2; ++r) {
            EXPECT_TRUE(cclose(cget(HV, {r, k}), lam * cget(e.vectors, {r, k})));
        }
    }
    // Eigenvectors are unit-norm: ⟨v,v⟩ = 1.
    for (int k = 0; k < 2; ++k) {
        const la::CNDArray vk = cvec({cget(e.vectors, {0, k}), cget(e.vectors, {1, k})});
        EXPECT_TRUE(cclose(la::vdot(vk, vk), C(1, 0)));
    }
}

TEST(LinalgRoutines, NormAndRank) {
    EXPECT_TRUE(close(la::norm(nd::array({3.0, 4.0})), 5.0));        // L2
    EXPECT_EQ(la::matrix_rank(mat(2, 2, {1, 2, 2, 4})), 1);      // rank-deficient
    EXPECT_EQ(la::matrix_rank(mat(2, 2, {1, 0, 0, 1})), 2);
}

TEST(LinalgRoutines, VdotInnerOuterKron) {
    const nd::NDArray a = nd::array({1.0, 2.0, 3.0});
    const nd::NDArray b = nd::array({4.0, 5.0, 6.0});
    EXPECT_DOUBLE_EQ(la::vdot(a, b), 32.0);   // 4+10+18
    EXPECT_DOUBLE_EQ(la::inner(a, b), 32.0);
    const nd::NDArray o = la::outer(nd::array({1.0, 2.0}), nd::array({3.0, 4.0}));  // [[3,4],[6,8]]
    EXPECT_DOUBLE_EQ(nd::get(o, {0, 0}), 3.0);
    EXPECT_DOUBLE_EQ(nd::get(o, {1, 1}), 8.0);
    const nd::NDArray k = la::kron(mat(2, 2, {1, 0, 0, 1}), mat(2, 2, {1, 2, 3, 4}));  // I⊗B
    EXPECT_DOUBLE_EQ(nd::get(k, {0, 0}), 1.0);
    EXPECT_DOUBLE_EQ(nd::get(k, {0, 1}), 2.0);
    EXPECT_DOUBLE_EQ(nd::get(k, {2, 2}), 1.0);  // second diagonal block
    EXPECT_DOUBLE_EQ(nd::get(k, {3, 3}), 4.0);
}

TEST(LinalgRoutines, MatrixPower) {
    const nd::NDArray A = mat(2, 2, {2, 0, 0, 3});
    const nd::NDArray A0 = la::matrix_power(A, 0);  // identity
    EXPECT_TRUE(close(nd::get(A0, {0, 0}), 1.0));
    EXPECT_TRUE(close(nd::get(A0, {1, 1}), 1.0));
    const nd::NDArray A3 = la::matrix_power(A, 3);  // diag(8, 27)
    EXPECT_TRUE(close(nd::get(A3, {0, 0}), 8.0));
    EXPECT_TRUE(close(nd::get(A3, {1, 1}), 27.0));
    const nd::NDArray Am1 = la::matrix_power(A, -1);  // diag(1/2, 1/3)
    EXPECT_TRUE(close(nd::get(Am1, {0, 0}), 0.5));
    EXPECT_TRUE(close(nd::get(Am1, {1, 1}), 1.0 / 3.0));
}

TEST(LinalgRoutines, SlogdetAndCond) {
    const la::SLogDet sd = la::slogdet(mat(2, 2, {4, 3, 6, 3}));  // det = -6
    EXPECT_TRUE(close(sd.sign, -1.0));
    EXPECT_TRUE(close(sd.logabsdet, std::log(6.0), 1e-9));
    EXPECT_TRUE(close(la::cond(mat(2, 2, {2, 0, 0, 2})), 1.0, 1e-6));  // well-conditioned
}

TEST(LinalgRoutines, Lstsq) {
    // lstsq(a, b) = pinv(a) · b and takes a 2-D b (column vector). On a square
    // system, least-squares == solve.
    const nd::NDArray A = mat(2, 2, {4, 3, 6, 3});
    const nd::NDArray x = la::lstsq(A, mat(2, 1, {10, 12}));  // -> [[1], [2]]
    EXPECT_TRUE(close(nd::get(x, {0, 0}), 1.0, 1e-6));
    EXPECT_TRUE(close(nd::get(x, {1, 0}), 2.0, 1e-6));
}

TEST(LinalgRoutines, EigvalshSymmetric) {
    const nd::NDArray w = la::eigvalsh(mat(2, 2, {2, 1, 1, 2}));  // eigenvalues 1, 3
    const double v0 = nd::get(w, {0}), v1 = nd::get(w, {1});
    EXPECT_TRUE(close(std::min(v0, v1), 1.0, 1e-6));
    EXPECT_TRUE(close(std::max(v0, v1), 3.0, 1e-6));
}

TEST(LinalgRoutines, GeneralEig) {
    // Non-symmetric (upper-triangular) [[2,1],[0,3]] -> eigenvalues 2, 3 (real).
    const la::EigC e = la::eig(mat(2, 2, {2, 1, 0, 3}));
    EXPECT_TRUE(cclose(cget(e.values, {0}), 3.0));  // descending
    EXPECT_TRUE(cclose(cget(e.values, {1}), 2.0));
    // eigvals on the same non-symmetric matrix exercises the general path too.
    const la::CNDArray ev = la::eigvals(mat(2, 2, {2, 1, 0, 3}));
    EXPECT_TRUE(cclose(cget(ev, {0}), 3.0));
    EXPECT_TRUE(cclose(cget(ev, {1}), 2.0));
}

TEST(LinalgRoutines, GeneralEigOnSymmetricPromotesToComplex) {
    // eig() on a symmetric matrix routes through eigh and PROMOTES the real spectrum
    // and eigenvectors to complex (imag 0): values are real-valued complex, and the
    // eigenvectors are present (a 2x2 complex matrix), unlike the non-symmetric case.
    const la::EigC e = la::eig(mat(2, 2, {2, 1, 1, 2}));  // eigenvalues 3, 1
    EXPECT_TRUE(cclose(cget(e.values, {0}), 3.0));
    EXPECT_TRUE(cclose(cget(e.values, {1}), 1.0));
    EXPECT_EQ(nd::size_of(e.vectors), 4);  // 2x2 eigenvectors present (promoted to complex)
    EXPECT_EQ(e.vectors.ndim(), 2u);
}

// ---- targeted tests for the deep numerical branches ----

namespace {
std::vector<double> sorted3(const nd::NDArray& v) {
    std::vector<double> s{nd::get(v, {0}), nd::get(v, {1}), nd::get(v, {2})};
    std::sort(s.begin(), s.end());
    return s;
}
// Same, for a complex spectrum known to be real (imaginary parts ≈ 0): the real parts.
std::vector<double> sorted3c(const la::CNDArray& v) {
    std::vector<double> s{cget(v, {0}).real(), cget(v, {1}).real(), cget(v, {2}).real()};
    std::sort(s.begin(), s.end());
    return s;
}
}  // namespace

TEST(LinalgRoutines, VdotInnerAcceptTwoDimVectors) {
    // A 2-D Nx1 / 1xN is treated as a flat vector by vdot/inner.
    EXPECT_DOUBLE_EQ(la::vdot(mat(3, 1, {1, 2, 3}), mat(3, 1, {4, 5, 6})), 32.0);
    EXPECT_DOUBLE_EQ(la::inner(mat(1, 3, {1, 2, 3}), mat(1, 3, {4, 5, 6})), 32.0);
}

TEST(LinalgRoutines, DetRequiresPivot) {
    EXPECT_TRUE(close(la::det(mat(2, 2, {0, 1, 1, 0})), -1.0));  // forces an LU row swap
}

TEST(LinalgRoutines, Eigvalsh3x3Dense) {
    // [[2,1,1],[1,2,1],[1,1,2]] = I + ones -> eigenvalues 4, 1, 1 (Jacobi rotations).
    const std::vector<double> v = sorted3(la::eigvalsh(mat(3, 3, {2, 1, 1, 1, 2, 1, 1, 1, 2})));
    EXPECT_TRUE(close(v[0], 1.0, 1e-6));
    EXPECT_TRUE(close(v[1], 1.0, 1e-6));
    EXPECT_TRUE(close(v[2], 4.0, 1e-6));
}

TEST(LinalgRoutines, GeneralEigvals3x3Dense) {
    // Same dense matrix through the general (Hessenberg + shifted-QR) path.
    const std::vector<double> v = sorted3c(la::eigvals(mat(3, 3, {2, 1, 1, 1, 2, 1, 1, 1, 2})));
    EXPECT_TRUE(close(v[0], 1.0, 1e-6));
    EXPECT_TRUE(close(v[2], 4.0, 1e-6));
}

TEST(LinalgRoutines, NonSymmetric3x3HessenbergPath) {
    // M = P·diag(2,3,5)·P⁻¹ — non-symmetric with real eigenvalues 2,3,5. Routes
    // through eigvals_general (Householder–Hessenberg + shifted QR for n≥3).
    const nd::NDArray M = mat(3, 3, {2.5, 0.5, -0.5, -1, 4, 1, -1.5, 1.5, 3.5});
    const std::vector<double> v = sorted3c(la::eigvals(M));
    EXPECT_TRUE(close(v[0], 2.0, 1e-6));
    EXPECT_TRUE(close(v[1], 3.0, 1e-6));
    EXPECT_TRUE(close(v[2], 5.0, 1e-6));
    EXPECT_TRUE(cclose(cget(la::eig(M).values, {0}), 5.0));  // descending; eig() too
}

TEST(LinalgRoutines, ComplexEigenvaluesOfRotation) {
    // A 2-D rotation [[0,-1],[1,0]] has eigenvalues ±i. The general eigensolver
    // returns the complex conjugate pair (descending by real, then imag: +i, then -i)
    // rather than throwing — complex spectra are first-class.
    const la::CNDArray ev = la::eigvals(mat(2, 2, {0, -1, 1, 0}));
    EXPECT_TRUE(cclose(cget(ev, {0}), std::complex<double>(0.0, 1.0)));
    EXPECT_TRUE(cclose(cget(ev, {1}), std::complex<double>(0.0, -1.0)));
    // A complex pair with a non-zero real part: [[1,-1],[1,1]] -> 1±i.
    const la::CNDArray ev2 = la::eigvals(mat(2, 2, {1, -1, 1, 1}));
    EXPECT_TRUE(cclose(cget(ev2, {0}), std::complex<double>(1.0, 1.0)));
    EXPECT_TRUE(cclose(cget(ev2, {1}), std::complex<double>(1.0, -1.0)));
}

TEST(LinalgRoutines, VdotRejectsNonVector) {
    EXPECT_THROW(la::vdot(mat(2, 2, {1, 2, 3, 4}), mat(2, 2, {1, 2, 3, 4})), std::runtime_error);
}

TEST(LinalgRoutines, NormOfMatrixIsFrobenius) {
    EXPECT_TRUE(close(la::norm(mat(2, 2, {1, 2, 2, 4})), 5.0));  // sqrt(1+4+4+16)
}

TEST(LinalgRoutines, PinvCondRankOnWideMatrix) {
    const nd::NDArray W = mat(2, 3, {1, 0, 0, 0, 1, 0});  // 2x3 (more cols than rows)
    const nd::NDArray P = la::pinv(W);                    // -> 3x2 (transpose-SVD branch)
    EXPECT_EQ(nd::shape_of(P), (std::vector<long long>{3, 2}));
    EXPECT_GE(la::cond(W), 1.0);
    EXPECT_EQ(la::matrix_rank(W), 2);
}

// ---- coverage: non-contiguous inputs, edge branches, and defensive throws ----
TEST(LinalgRoutines, NonContiguousInputs) {
    // A broadcast (stride-0) view is non-contiguous, exercising the packing fallback in
    // contig/as_matrix/as_vector/as_cmatrix (the contiguous fast path runs everywhere else).
    const nd::NDArray ncvec = nd::broadcast_to(nd::scalar(2.0), {4});       // [2,2,2,2]
    EXPECT_TRUE(close(la::dot(ncvec, ncvec), 16.0, 1e-12));                 // contig() pack path
    EXPECT_TRUE(close(la::norm(nd::broadcast_to(nd::scalar(3.0), {2, 2})), 6.0, 1e-12));
    const nd::NDArray ncmat = nd::broadcast_to(nd::array({1.0, 2.0, 3.0}), {3, 3});
    EXPECT_TRUE(close(la::det(ncmat), 0.0, 1e-9));                          // as_matrix pack path
    const nd::NDArray I3 = mat(3, 3, {1, 0, 0, 0, 1, 0, 0, 0, 1});
    const nd::NDArray x = la::solve(I3, nd::broadcast_to(nd::scalar(5.0), {3}));  // as_vector pack
    EXPECT_TRUE(close(nd::get(x, {0}), 5.0, 1e-9));
    const la::CNDArray nccx = nd::broadcast_to(nd::scalar(C(1, 1)), {2, 2});
    EXPECT_EQ(la::conj_transpose(nccx).ndim(), 2u);                        // complex contig() pack
    // complex eigvalsh extracts via as_cmatrix — a broadcast [[2,2],[2,2]] (Hermitian) packs.
    EXPECT_TRUE(close(nd::get(la::eigvalsh(nd::broadcast_to(nd::scalar(C(2, 0)), {2, 2})), {0}),
                      4.0, 1e-9));
}

TEST(LinalgRoutines, ComplexDotFourPlusElements) {
    // 4+ elements drives the multi-accumulator loop in cdot (both dot and the conjugating vdot).
    const la::CNDArray a = cvec({C(1, 1), C(2, 0), C(0, 1), C(1, -1), C(2, 2)});
    const la::CNDArray b = cvec({C(1, 0), C(0, 1), C(1, 1), C(2, 0), C(0, -1)});
    C dotref{}, vdotref{};
    for (long long i = 0; i < 5; ++i) {
        const C ai = cget(a, {i}), bi = cget(b, {i});
        dotref += ai * bi;
        vdotref += std::conj(ai) * bi;
    }
    EXPECT_TRUE(cclose(la::dot(a, b), dotref));     // non-conjugating multi-accumulator
    EXPECT_TRUE(cclose(la::vdot(a, b), vdotref));   // conjugating multi-accumulator
}

TEST(LinalgRoutines, ShapeAndConvergenceGuards) {
    const nd::NDArray v = nd::array({1.0, 2.0});
    EXPECT_THROW(la::matmul(v, v), std::runtime_error);                    // real matmul non-2D
    EXPECT_THROW(la::kron(v, v), std::runtime_error);                      // kron non-2D
    const la::CNDArray cv = cvec({C(1, 0), C(2, 0)});
    EXPECT_THROW(la::matmul(cv, cv), std::runtime_error);                  // complex matmul non-2D
    // inv that requires a row pivot (zero leading pivot)
    const nd::NDArray inv = la::inv(mat(2, 2, {0, 1, 1, 0}));
    EXPECT_TRUE(close(nd::get(inv, {0, 1}), 1.0, 1e-9));
    // values-only SVD on a WIDE matrix routes through the transpose branch
    EXPECT_TRUE(close(nd::get(la::svdvals(mat(2, 3, {1, 0, 0, 0, 1, 0})), {0}), 1.0, 1e-9));
    // NaN input never converges -> the defensive "did not converge" throws fire
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(la::eigvalsh(mat(2, 2, {nan, 0, 0, 1})), std::runtime_error);   // symmetric QL
    EXPECT_THROW(la::svdvals(mat(2, 2, {nan, 0, 0, 1})), std::runtime_error);    // SVD QR
}

TEST(LinalgRoutines, RankDeficientAndDiagonalPaths) {
    // A matrix with an exactly-zero column gives an exactly-zero singular value, which
    // drives the g==0 branch in U accumulation and the bulge cancellation in the SVD QR.
    const la::SVD s = la::svd(mat(3, 3, {1, 4, 0, 2, 5, 0, 3, 6, 0}));
    EXPECT_TRUE(close(nd::get(s.s, {2}), 0.0, 1e-9));
    // A diagonal symmetric matrix has all-zero off-diagonals -> the scale==0 branch in tred2.
    const la::Eig e = la::eigh(mat(3, 3, {2, 0, 0, 0, 5, 0, 0, 0, 7}));
    EXPECT_TRUE(close(nd::get(e.values, {0}), 7.0, 1e-9));
}

TEST(LinalgRoutines, SvdCancellationPath) {
    // An upper-bidiagonal matrix with an interior zero diagonal (w[1]=0) but a
    // non-negligible super-diagonal triggers the QR "cancel rv1" Givens sweep in U.
    (void)la::svd(mat(3, 3, {2, 3, 0, 0, 0, 4, 0, 0, 5}));
    (void)la::svd(mat(3, 3, {0, 5, 0, 0, 0, 5, 0, 0, 0}));
    (void)la::svd(mat(4, 4, {1, 9, 0, 0, 0, 0, 9, 0, 0, 0, 0, 9, 0, 0, 0, 1}));
    SUCCEED();
}

// Exercise the WIDE-UNROLL main loops of the multi-accumulator/blocked kernels: the
// existing tests use tiny matrices that only ever run the scalar remainder, leaving the
// 4-/8-wide vectorized bodies (ddot, real+complex matmul row-blocking, cholesky/qr/
// tred2/trace reductions) uncovered. These use n≥8 so the main loops run.
TEST(LinalgRoutines, WideKernelPaths) {
    // 8×8 SPD: diag 10, off-diag 1 (= 9·I + J). Eigenvalues {17, 9×7}, trace 80.
    std::vector<double> a8(64);
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t j = 0; j < 8; ++j) a8[i * 8 + j] = (i == j) ? 10.0 : 1.0;
    const nd::NDArray A = mat(8, 8, a8);

    // dot over ≥8 elements → ddot 8-wide body.
    EXPECT_DOUBLE_EQ(la::dot(nd::array(std::vector<double>(10, 1.0)),
                             nd::array(std::vector<double>(10, 2.0))), 20.0);
    // trace 8×8 → trace 4-wide body.
    EXPECT_DOUBLE_EQ(la::trace(A), 80.0);
    // matmul 8×8 → real 4-row block. A·I == A.
    std::vector<double> id8(64, 0.0);
    for (std::size_t i = 0; i < 8; ++i) id8[i * 8 + i] = 1.0;
    const nd::NDArray AI = la::matmul(A, mat(8, 8, id8));
    EXPECT_DOUBLE_EQ(nd::get(AI, {0, 0}), 10.0);
    EXPECT_DOUBLE_EQ(nd::get(AI, {1, 0}), 1.0);
    // cholesky 8×8 (j reaches ≥4 → 4-wide inner dot). Reconstruct A = L·Lᵀ.
    const nd::NDArray L = la::cholesky(A);
    double a00 = 0;
    for (long long k = 0; k < 8; ++k) a00 += nd::get(L, {0, k}) * nd::get(L, {0, k});
    EXPECT_NEAR(a00, 10.0, 1e-9);
    // qr 8×4 → reflect 4-wide body. R upper-triangular, Q·R == A_panel.
    std::vector<double> p(32);
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t j = 0; j < 4; ++j) p[i * 4 + j] = a8[i * 8 + j];
    const la::QR qr = la::qr(mat(8, 4, p));
    EXPECT_NEAR(nd::get(qr.r, {1, 0}), 0.0, 1e-9);  // upper-triangular
    // eigvalsh 8×8 → tred2 mat-vec 4-wide body. Largest eigenvalue 17, sum 80.
    const nd::NDArray w = la::eigvalsh(A);
    EXPECT_NEAR(nd::get(w, {0}), 17.0, 1e-7);
    double sw = 0;
    for (long long i = 0; i < 8; ++i) sw += nd::get(w, {i});
    EXPECT_NEAR(sw, 80.0, 1e-7);
    // eig() on a symmetric matrix → the reuse-the-extracted-A symmetric branch.
    const la::EigC e = la::eig(A);
    EXPECT_NEAR(cget(e.values, {0}).real(), 17.0, 1e-6);

    // complex matmul 8×8 → complex 4-row block.
    std::vector<C> z(64), zi(64, C{0, 0});
    for (std::size_t i = 0; i < 8; ++i) { z[i * 8 + i] = C{2, 0}; zi[i * 8 + i] = C{1, 0}; }
    const la::CNDArray Z = cmat(8, 8, z), I = cmat(8, 8, zi);
    EXPECT_TRUE(cclose(cget(la::matmul(Z, I), {3, 3}), C{2, 0}));
}

// Non-contiguous (broadcast/strided) operands take the scratch-packing fallback in the
// products/reductions, not the zero-copy fast path.
TEST(LinalgRoutines, StridedOperandFallback) {
    const nd::NDArray s = nd::broadcast_to(nd::scalar(2.0), {10});  // stride-0 view, len 10
    EXPECT_DOUBLE_EQ(la::dot(s, s), 40.0);                          // 10 · (2·2)
    EXPECT_NEAR(la::norm(s), std::sqrt(40.0), 1e-9);
    const la::CNDArray cs = nd::broadcast_to(nd::scalar(C{2, 0}), {6});
    EXPECT_TRUE(cclose(la::dot(cs, cs), C{24, 0}));   // 6·(2·2)
    EXPECT_TRUE(cclose(la::vdot(cs, cs), C{24, 0}));  // conj path, strided
}
