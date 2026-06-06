// Micro-benchmarks for cheatah::purrscript::linalg (exercised by tests/linalg/smoke_test.cpp).
#include "linalg.hpp"

#include <vector>

#include <benchmark/benchmark.h>

namespace {

void BM_Version(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(cheatah::purrscript::linalg::version());
    }
}
BENCHMARK(BM_Version);

// Dot product across a sweep of vector lengths — the kernel SIMD/GPU backends
// will accelerate. `bytes_processed` lets Google Benchmark report throughput.
void BM_Dot(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    std::vector<double> a(n, 1.5);
    std::vector<double> b(n, 2.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(cheatah::purrscript::linalg::dot(a, b));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n) * 2 * sizeof(double));
}
BENCHMARK(BM_Dot)->Arg(64)->Arg(1024)->Arg(65536);

} // namespace
