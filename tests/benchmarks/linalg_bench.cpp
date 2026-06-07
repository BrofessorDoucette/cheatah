// Micro-benchmarks for cheatah::linalg (the numpy-style routines on ndarray).
#include "linalg.hpp"
#include "ndarray.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

namespace nd = cheatah::ndarray;
namespace la = cheatah::linalg;

namespace {

// A deterministic, well-conditioned m×n design-matrix entry (matches the LSQ e2e
// test): small values with a strong diagonal on the top n×n block.
double design(long long i, long long j, long long n) {
    double v = static_cast<double>(((i * 7 + j * 3 + 1) % 7) - 3);
    if (i < n && i == j) v += static_cast<double>(10 * n);
    return v;
}

// 1-D dot product across a sweep of vector lengths. `items/bytes_processed` let
// Google Benchmark report throughput.
void BM_Dot(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    const nd::NDArray a = nd::ones({static_cast<long long>(n)});
    const nd::NDArray b = nd::full({static_cast<long long>(n)}, 2.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(la::dot(a, b));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n) * 2 * sizeof(double));
}
BENCHMARK(BM_Dot)->Arg(64)->Arg(1024)->Arg(65536);

// Square matrix multiply across a sweep of dimensions — the O(n^3) hot path.
void BM_Matmul(benchmark::State& state) {
    const long long n = state.range(0);
    const nd::NDArray a = nd::reshape(nd::ones({n * n}), {n, n});
    const nd::NDArray b = nd::reshape(nd::full({n * n}, 2.0), {n, n});
    for (auto _ : state) {
        benchmark::DoNotOptimize(la::matmul(a, b));
    }
    state.SetItemsProcessed(state.iterations() * n * n * n);
}
BENCHMARK(BM_Matmul)->Arg(16)->Arg(64)->Arg(128);

// Least-squares regression (lstsq = pinv·y, SVD-based) across the dimensionalities
// the system-level tests cover: 2, 3, 5, 10, 15 parameters, each over-determined
// with m = n + 5 samples. This is the hot path for the regression e2e tests.
void BM_LeastSquares(benchmark::State& state) {
    const long long n = state.range(0);
    const long long m = n + 5;
    std::vector<double> xdata(static_cast<std::size_t>(m * n));
    for (long long i = 0; i < m; ++i)
        for (long long j = 0; j < n; ++j)
            xdata[static_cast<std::size_t>(i * n + j)] = design(i, j, n);
    const nd::NDArray X = nd::reshape(nd::array(xdata), {m, n});
    const nd::NDArray y = nd::reshape(nd::ones({m}), {m, 1});  // m×1 targets
    for (auto _ : state) {
        benchmark::DoNotOptimize(la::lstsq(X, y));
    }
    state.SetLabel(std::to_string(n) + "D");
}
BENCHMARK(BM_LeastSquares)->Arg(2)->Arg(3)->Arg(5)->Arg(10)->Arg(15);

} // namespace
