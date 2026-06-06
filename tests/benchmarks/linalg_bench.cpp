// Micro-benchmarks for cheatah::linalg (the numpy-style routines on ndarray).
#include "linalg.hpp"
#include "ndarray.hpp"

#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

namespace nd = cheatah::ndarray;
namespace la = cheatah::linalg;

namespace {

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

} // namespace
