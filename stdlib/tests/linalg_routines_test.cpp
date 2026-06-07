#include "ndarray.hpp"
#include "routines.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace nd = cheatah::ndarray;
namespace la = cheatah::linalg;

namespace {
nd::NDArray mat(std::size_t r, std::size_t c, std::vector<double> data) {
    return nd::reshape(nd::array(std::move(data)), {(long long)r, (long long)c});
}
bool close(double a, double b, double tol = 1e-9) { return std::fabs(a - b) < tol; }
}  // namespace

TEST(LinalgRoutines, ProductsAndTrace) {
    const nd::NDArray a = mat(2, 3, {1, 2, 3, 4, 5, 6});
    const nd::NDArray b = mat(3, 2, {7, 8, 9, 10, 11, 12});
    const nd::NDArray c = la::matmul(a, b);  // [[58,64],[139,154]]
    EXPECT_DOUBLE_EQ(nd::get(c, {0, 0}), 58);
    EXPECT_DOUBLE_EQ(nd::get(c, {0, 1}), 64);
    EXPECT_DOUBLE_EQ(nd::get(c, {1, 1}), 154);
    EXPECT_DOUBLE_EQ(la::dot(nd::array({1, 2, 3}), nd::array({4, 5, 6})), 32);  // 4+10+18
    EXPECT_DOUBLE_EQ(la::trace(mat(2, 2, {1, 2, 3, 4})), 5);  // 1+4
}

TEST(LinalgRoutines, SolveDetInv) {
    const nd::NDArray A = mat(2, 2, {4, 3, 6, 3});  // det = 12-18 = -6
    EXPECT_TRUE(close(la::det(A), -6.0));
    const nd::NDArray x = la::solve(A, nd::array({10, 12}));  // 4x+3y=10, 6x+3y=12 -> x=1,y=2
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

    // symmetric eigen: [[2,1],[1,2]] -> eigenvalues 3, 1
    const la::Eig e = la::eigh(mat(2, 2, {2, 1, 1, 2}));
    EXPECT_TRUE(close(nd::get(e.values, {0}), 3.0, 1e-6));
    EXPECT_TRUE(close(nd::get(e.values, {1}), 1.0, 1e-6));

    // general (real) eigenvalues of [[2,0],[0,5]] -> 5, 2
    const nd::NDArray ev = la::eigvals(mat(2, 2, {2, 0, 0, 5}));
    EXPECT_TRUE(close(nd::get(ev, {0}), 5.0, 1e-6));
    EXPECT_TRUE(close(nd::get(ev, {1}), 2.0, 1e-6));
}

TEST(LinalgRoutines, NormAndRank) {
    EXPECT_TRUE(close(la::norm(nd::array({3, 4})), 5.0));        // L2
    EXPECT_EQ(la::matrix_rank(mat(2, 2, {1, 2, 2, 4})), 1);      // rank-deficient
    EXPECT_EQ(la::matrix_rank(mat(2, 2, {1, 0, 0, 1})), 2);
}

TEST(LinalgRoutines, VdotInnerOuterKron) {
    const nd::NDArray a = nd::array({1, 2, 3});
    const nd::NDArray b = nd::array({4, 5, 6});
    EXPECT_DOUBLE_EQ(la::vdot(a, b), 32.0);   // 4+10+18
    EXPECT_DOUBLE_EQ(la::inner(a, b), 32.0);
    const nd::NDArray o = la::outer(nd::array({1, 2}), nd::array({3, 4}));  // [[3,4],[6,8]]
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
    // Non-symmetric (upper-triangular) [[2,1],[0,3]] -> eigenvalues 2, 3.
    const la::Eig e = la::eig(mat(2, 2, {2, 1, 0, 3}));
    const double a = nd::get(e.values, {0}), b = nd::get(e.values, {1});
    EXPECT_TRUE(close(std::min(a, b), 2.0, 1e-6));
    EXPECT_TRUE(close(std::max(a, b), 3.0, 1e-6));
    // eigvals on the same non-symmetric matrix exercises the general path too.
    const nd::NDArray ev = la::eigvals(mat(2, 2, {2, 1, 0, 3}));
    const double c = nd::get(ev, {0}), d = nd::get(ev, {1});
    EXPECT_TRUE(close(std::min(c, d), 2.0, 1e-6));
    EXPECT_TRUE(close(std::max(c, d), 3.0, 1e-6));
}
