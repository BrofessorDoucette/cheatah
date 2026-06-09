// cheatah::linalg vs GLM — small fixed-size vector/matrix math (the 2-, 3-, 4-D
// graphics regime GLM is built for). This is deliberately GLM's home turf and an
// honest stress test of cheatah's *dynamic* NDArray at tiny sizes: GLM's types are
// compile-time-fixed and stack-allocated (a mat4 multiply is a handful of SIMD
// instructions, zero heap), while cheatah's NDArray is heap-backed and shape-generic.
// Double precision on both sides (glm::dmat*/dvec*) so it's the same arithmetic.
//
// Pairs are BM_<op>_cheatah / BM_<op>_glm. Build: cmake --preset release-benchmarks.
#include "linalg.hpp"
#include "ndarray.hpp"

#include <vector>

#include <glm/glm.hpp>
#include <benchmark/benchmark.h>

namespace nd = cheatah::ndarray;
namespace la = cheatah::linalg;

namespace {

// Deterministic, diagonally-dominant n×n matrix as a cheatah NDArray.
nd::NDArray cheatah_matrix(long long n) {
    std::vector<double> buf(static_cast<std::size_t>(n * n));
    for (long long i = 0; i < n; ++i)
        for (long long j = 0; j < n; ++j) {
            double v = static_cast<double>(((i * 131 + j * 97 + 1) % 7) - 3);
            if (i == j) v += static_cast<double>(10 * n);
            buf[static_cast<std::size_t>(i * n + j)] = v;
        }
    return nd::reshape(nd::array(buf), {n, n});
}
template <typename M>
M glm_fill(long long n) {  // same entries into a GLM column-major matrix
    M m(1.0);
    for (long long i = 0; i < n; ++i)
        for (long long j = 0; j < n; ++j) {
            double v = static_cast<double>(((i * 131 + j * 97 + 1) % 7) - 3);
            if (i == j) v += static_cast<double>(10 * n);
            m[static_cast<int>(j)][static_cast<int>(i)] = v;  // GLM is column-major
        }
    return m;
}

// ---- vector dot (length 4) ---------------------------------------------
void BM_dot4_cheatah(benchmark::State& state) {
    const nd::NDArray a = nd::array({1.0, 2.0, 3.0, 4.0}), b = nd::array({4.0, 3.0, 2.0, 1.0});
    for (auto _ : state) benchmark::DoNotOptimize(la::dot(a, b));
}
void BM_dot4_glm(benchmark::State& state) {
    glm::dvec4 a(1.0, 2.0, 3.0, 4.0), b(4.0, 3.0, 2.0, 1.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(a);  // force re-read each iteration (defeat folding)
        benchmark::DoNotOptimize(b);
        benchmark::DoNotOptimize(glm::dot(a, b));
    }
}
BENCHMARK(BM_dot4_cheatah);
BENCHMARK(BM_dot4_glm);

// ---- matrix multiply (2×2, 3×3, 4×4) -----------------------------------
template <long long N, typename GM>
void bm_matmul_cheatah(benchmark::State& state) {
    const nd::NDArray a = cheatah_matrix(N), b = cheatah_matrix(N);
    for (auto _ : state) benchmark::DoNotOptimize(la::matmul(a, b));
}
template <long long N, typename GM>
void bm_matmul_glm(benchmark::State& state) {
    GM a = glm_fill<GM>(N), b = glm_fill<GM>(N);
    for (auto _ : state) {
        benchmark::DoNotOptimize(a);
        benchmark::DoNotOptimize(b);
        GM c = a * b;
        benchmark::DoNotOptimize(&c);
    }
}
BENCHMARK(bm_matmul_cheatah<2, glm::dmat2>)->Name("BM_matmul2_cheatah");
BENCHMARK(bm_matmul_glm<2, glm::dmat2>)->Name("BM_matmul2_glm");
BENCHMARK(bm_matmul_cheatah<3, glm::dmat3>)->Name("BM_matmul3_cheatah");
BENCHMARK(bm_matmul_glm<3, glm::dmat3>)->Name("BM_matmul3_glm");
BENCHMARK(bm_matmul_cheatah<4, glm::dmat4>)->Name("BM_matmul4_cheatah");
BENCHMARK(bm_matmul_glm<4, glm::dmat4>)->Name("BM_matmul4_glm");

// ---- matrix inverse (2×2, 3×3, 4×4) ------------------------------------
template <long long N, typename GM>
void bm_inv_cheatah(benchmark::State& state) {
    const nd::NDArray a = cheatah_matrix(N);
    for (auto _ : state) benchmark::DoNotOptimize(la::inv(a));
}
template <long long N, typename GM>
void bm_inv_glm(benchmark::State& state) {
    GM a = glm_fill<GM>(N);
    for (auto _ : state) {
        benchmark::DoNotOptimize(a);
        GM c = glm::inverse(a);
        benchmark::DoNotOptimize(&c);
    }
}
BENCHMARK(bm_inv_cheatah<2, glm::dmat2>)->Name("BM_inv2_cheatah");
BENCHMARK(bm_inv_glm<2, glm::dmat2>)->Name("BM_inv2_glm");
BENCHMARK(bm_inv_cheatah<3, glm::dmat3>)->Name("BM_inv3_cheatah");
BENCHMARK(bm_inv_glm<3, glm::dmat3>)->Name("BM_inv3_glm");
BENCHMARK(bm_inv_cheatah<4, glm::dmat4>)->Name("BM_inv4_cheatah");
BENCHMARK(bm_inv_glm<4, glm::dmat4>)->Name("BM_inv4_glm");

// ---- determinant (4×4) -------------------------------------------------
void BM_det4_cheatah(benchmark::State& state) {
    const nd::NDArray a = cheatah_matrix(4);
    for (auto _ : state) benchmark::DoNotOptimize(la::det(a));
}
void BM_det4_glm(benchmark::State& state) {
    glm::dmat4 a = glm_fill<glm::dmat4>(4);
    for (auto _ : state) {
        benchmark::DoNotOptimize(a);
        benchmark::DoNotOptimize(glm::determinant(a));
    }
}
BENCHMARK(BM_det4_cheatah);
BENCHMARK(BM_det4_glm);

}  // namespace
