// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// cheatah::linalg vs Eigen — a native, single-threaded C++ dense-linear-algebra
// face-off (Eigen is the reference C++ library). Both sides run the SAME operation on
// the SAME deterministic data at the SAME sizes the NumPy comparison uses, built at
// `-O3 -march=native`. Eigen is held to one thread (it is single-threaded by default
// without OpenMP anyway) so this is an apples-to-apples per-core comparison — the
// operating point cheatah is designed for.
//
// Pairs are named BM_<op>_cheatah / BM_<op>_eigen so the two rows sit together in the
// report. Build: cmake --preset release-benchmarks (CHEATAH_BUILD_BENCHMARKS=ON).
#include "linalg.hpp"
#include "ndarray.hpp"

#include <cstdint>
#include <vector>

#include <Eigen/Dense>
#include <benchmark/benchmark.h>

namespace nd = cheatah::ndarray;
namespace la = cheatah::linalg;

// Hold Eigen to a single thread so this is a strict per-core comparison (cheatah's
// numeric core is single-threaded by design). Without OpenMP Eigen is single-threaded
// anyway — this just makes it explicit and defensive. Runs before main().
static const int kEigenThreads = (Eigen::setNbThreads(1), Eigen::nbThreads());

namespace {

// Deterministic, diagonally-dominant (well-conditioned) matrix entry.
double gen(long long i, long long j, long long n) {
    double v = static_cast<double>(((i * 131 + j * 97 + 1) % 7) - 3);
    if (i == j) v += static_cast<double>(10 * n);
    return v;
}

// Build the same n×n matrix as a cheatah NDArray…
nd::NDArray cheatah_matrix(long long n) {
    std::vector<double> buf(static_cast<std::size_t>(n * n));
    for (long long i = 0; i < n; ++i)
        for (long long j = 0; j < n; ++j) buf[static_cast<std::size_t>(i * n + j)] = gen(i, j, n);
    return nd::reshape(nd::array(buf), {n, n});
}
// …and as an Eigen matrix.
Eigen::MatrixXd eigen_matrix(long long n) {
    Eigen::MatrixXd m(n, n);
    for (long long i = 0; i < n; ++i)
        for (long long j = 0; j < n; ++j) m(i, j) = gen(i, j, n);
    return m;
}

// A symmetric well-conditioned matrix (for the symmetric eigensolver).
double sym(long long i, long long j, long long n) {
    const long long a = i < j ? i : j, b = i < j ? j : i;
    double v = static_cast<double>(((a * 53 + b * 29 + 1) % 7) - 3);
    if (i == j) v += static_cast<double>(10 * n);
    return v;
}
nd::NDArray cheatah_sym(long long n) {
    std::vector<double> buf(static_cast<std::size_t>(n * n));
    for (long long i = 0; i < n; ++i)
        for (long long j = 0; j < n; ++j) buf[static_cast<std::size_t>(i * n + j)] = sym(i, j, n);
    return nd::reshape(nd::array(buf), {n, n});
}
Eigen::MatrixXd eigen_sym(long long n) {
    Eigen::MatrixXd m(n, n);
    for (long long i = 0; i < n; ++i)
        for (long long j = 0; j < n; ++j) m(i, j) = sym(i, j, n);
    return m;
}

// ---- dot (length sweep) -------------------------------------------------
void BM_dot_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = nd::full({n}, 1.0), b = nd::full({n}, 2.0);
    for (auto _ : state) benchmark::DoNotOptimize(la::dot(a, b));
}
void BM_dot_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::VectorXd a = Eigen::VectorXd::Constant(n, 1.0), b = Eigen::VectorXd::Constant(n, 2.0);
    for (auto _ : state) benchmark::DoNotOptimize(a.dot(b));
}
BENCHMARK(BM_dot_cheatah)->Arg(64)->Arg(16384);
BENCHMARK(BM_dot_eigen)->Arg(64)->Arg(16384);

// ---- matmul -------------------------------------------------------------
void BM_matmul_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_matrix(n), b = cheatah_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::matmul(a, b));
}
void BM_matmul_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_matrix(n), b = eigen_matrix(n);
    for (auto _ : state) { Eigen::MatrixXd c = a * b; benchmark::DoNotOptimize(c.data()); }
}
BENCHMARK(BM_matmul_cheatah)->Arg(32)->Arg(96);
BENCHMARK(BM_matmul_eigen)->Arg(32)->Arg(96);

// ---- solve (A x = b) ----------------------------------------------------
void BM_solve_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_matrix(n), b = nd::full({n}, 1.0);
    for (auto _ : state) benchmark::DoNotOptimize(la::solve(a, b));
}
void BM_solve_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_matrix(n);
    const Eigen::VectorXd b = Eigen::VectorXd::Constant(n, 1.0);
    for (auto _ : state) { Eigen::VectorXd x = a.partialPivLu().solve(b); benchmark::DoNotOptimize(x.data()); }
}
BENCHMARK(BM_solve_cheatah)->Arg(32)->Arg(64);
BENCHMARK(BM_solve_eigen)->Arg(32)->Arg(64);

// ---- inv ----------------------------------------------------------------
void BM_inv_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::inv(a));
}
void BM_inv_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_matrix(n);
    for (auto _ : state) { Eigen::MatrixXd c = a.inverse(); benchmark::DoNotOptimize(c.data()); }
}
BENCHMARK(BM_inv_cheatah)->Arg(32)->Arg(64);
BENCHMARK(BM_inv_eigen)->Arg(32)->Arg(64);

// ---- det ----------------------------------------------------------------
void BM_det_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::det(a));
}
void BM_det_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(a.determinant());
}
BENCHMARK(BM_det_cheatah)->Arg(64);
BENCHMARK(BM_det_eigen)->Arg(64);

// ---- eigvalsh (symmetric eigenvalues) -----------------------------------
void BM_eigvalsh_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_sym(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::eigvalsh(a));
}
void BM_eigvalsh_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_sym(n);
    for (auto _ : state) {
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(a, Eigen::EigenvaluesOnly);
        benchmark::DoNotOptimize(es.eigenvalues().data());
    }
}
BENCHMARK(BM_eigvalsh_cheatah)->Arg(8)->Arg(64);
BENCHMARK(BM_eigvalsh_eigen)->Arg(8)->Arg(64);

// ---- svdvals (singular values only) -------------------------------------
void BM_svdvals_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::svdvals(a));
}
void BM_svdvals_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_matrix(n);
    for (auto _ : state) {
        Eigen::BDCSVD<Eigen::MatrixXd> svd(a);  // no U/V computed -> singular values only
        benchmark::DoNotOptimize(svd.singularValues().data());
    }
}
BENCHMARK(BM_svdvals_cheatah)->Arg(64);
BENCHMARK(BM_svdvals_eigen)->Arg(64);

// ---- svd (full U + singular values + Vᵀ) --------------------------------
void BM_svd_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::svd(a).s);
}
void BM_svd_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_matrix(n);
    for (auto _ : state) {
        Eigen::BDCSVD<Eigen::MatrixXd> svd(a, Eigen::ComputeFullU | Eigen::ComputeFullV);
        benchmark::DoNotOptimize(svd.singularValues().data());
    }
}
BENCHMARK(BM_svd_cheatah)->Arg(64)->Arg(96);
BENCHMARK(BM_svd_eigen)->Arg(64)->Arg(96);

// ---- cholesky -----------------------------------------------------------
void BM_cholesky_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_sym(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::cholesky(a));
}
void BM_cholesky_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_sym(n);
    for (auto _ : state) { Eigen::MatrixXd L = a.llt().matrixL(); benchmark::DoNotOptimize(L.data()); }
}
BENCHMARK(BM_cholesky_cheatah)->Arg(64);
BENCHMARK(BM_cholesky_eigen)->Arg(64);

// ---- qr -----------------------------------------------------------------
void BM_qr_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::qr(a).r);
}
void BM_qr_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_matrix(n);
    for (auto _ : state) {
        Eigen::HouseholderQR<Eigen::MatrixXd> qr(a);
        Eigen::MatrixXd r = qr.matrixQR().triangularView<Eigen::Upper>();
        benchmark::DoNotOptimize(r.data());
    }
}
BENCHMARK(BM_qr_cheatah)->Arg(32)->Arg(64);
BENCHMARK(BM_qr_eigen)->Arg(32)->Arg(64);

// ---- eigh (eigenvalues + eigenvectors) ----------------------------------
void BM_eigh_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_sym(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::eigh(a).values);
}
void BM_eigh_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_sym(n);
    for (auto _ : state) {
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(a);  // values + vectors
        benchmark::DoNotOptimize(es.eigenvalues().data());
    }
}
BENCHMARK(BM_eigh_cheatah)->Arg(32)->Arg(64);
BENCHMARK(BM_eigh_eigen)->Arg(32)->Arg(64);

// ---- outer product ------------------------------------------------------
void BM_outer_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray u = nd::full({n}, 1.5), v = nd::full({n}, 2.0);
    for (auto _ : state) benchmark::DoNotOptimize(la::outer(u, v));
}
void BM_outer_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::VectorXd u = Eigen::VectorXd::Constant(n, 1.5), v = Eigen::VectorXd::Constant(n, 2.0);
    for (auto _ : state) { Eigen::MatrixXd c = u * v.transpose(); benchmark::DoNotOptimize(c.data()); }
}
BENCHMARK(BM_outer_cheatah)->Arg(64)->Arg(256);
BENCHMARK(BM_outer_eigen)->Arg(64)->Arg(256);

// ---- trace + Frobenius norm ---------------------------------------------
void BM_trace_cheatah(benchmark::State& state) {
    const nd::NDArray a = cheatah_matrix(256);
    for (auto _ : state) benchmark::DoNotOptimize(la::trace(a));
}
void BM_trace_eigen(benchmark::State& state) {
    const Eigen::MatrixXd a = eigen_matrix(256);
    for (auto _ : state) benchmark::DoNotOptimize(a.trace());
}
BENCHMARK(BM_trace_cheatah);
BENCHMARK(BM_trace_eigen);

void BM_norm_cheatah(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = cheatah_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(la::norm(a));
}
void BM_norm_eigen(benchmark::State& state) {
    const long long n = state.range(0);
    const Eigen::MatrixXd a = eigen_matrix(n);
    for (auto _ : state) benchmark::DoNotOptimize(a.norm());
}
BENCHMARK(BM_norm_cheatah)->Arg(32)->Arg(256);
BENCHMARK(BM_norm_eigen)->Arg(32)->Arg(256);

}  // namespace
