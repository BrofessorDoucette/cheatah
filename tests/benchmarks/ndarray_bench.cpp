// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Micro-benchmarks for cheatah::ndarray — the generic numeric core
// (`basic_ndarray<T>`). These exercise the element-wise ops (vectorized via the
// `std::execution::unseq` policy on the contiguous fast path) and the full
// reduction, across element TYPES (double and integer — the type is part of the
// array) and SIZES, plus the strided/broadcast fallback for comparison.
//
// Build with the `release` preset (`-O3 -march=native`) for meaningful numbers:
//   ./build/release/bin/cheatah_benchmarks --benchmark_filter=ndarray|Elementwise|Sum
#include "ndarray.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

namespace nd = cheatah::ndarray;

namespace {

// A contiguous 1-D array of `n` ascending values of element type T (so the array's
// element type — and thus the kernel that gets instantiated/vectorized — is T).
template <typename T>
nd::basic_ndarray<T> iota_array(std::size_t n) {
    std::vector<T> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<T>(i % 1024);
    return nd::array(v);
}

// Element-wise add of two same-shape contiguous arrays — the `std::transform(unseq)`
// SIMD fast path. Templated so the SAME kernel is measured for double and integer.
template <typename T>
void BM_ElementwiseAdd(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    const auto a = iota_array<T>(n);
    const auto b = iota_array<T>(n);
    for (auto _ : state) {
        benchmark::DoNotOptimize(nd::add(a, b));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n) * 2 *
                            static_cast<std::int64_t>(sizeof(T)));
}
BENCHMARK_TEMPLATE(BM_ElementwiseAdd, double)->Arg(1024)->Arg(65536)->Arg(1 << 20);
BENCHMARK_TEMPLATE(BM_ElementwiseAdd, long long)->Arg(1024)->Arg(65536)->Arg(1 << 20);

// Full reduction (sum across all elements) — the `std::reduce(unseq)` path.
template <typename T>
void BM_Sum(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    const auto a = iota_array<T>(n);
    for (auto _ : state) {
        benchmark::DoNotOptimize(nd::sum(a));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n) *
                            static_cast<std::int64_t>(sizeof(T)));
}
BENCHMARK_TEMPLATE(BM_Sum, double)->Arg(1024)->Arg(65536)->Arg(1 << 20);
BENCHMARK_TEMPLATE(BM_Sum, long long)->Arg(1024)->Arg(65536)->Arg(1 << 20);

// Broadcast add (a 0-d scalar stretched over a vector) — the strided, stride-0
// odometer FALLBACK. Compare against BM_ElementwiseAdd<double> at the same size to
// see what the contiguous SIMD fast path buys.
void BM_BroadcastAddFallback(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    const auto a = iota_array<double>(n);
    const nd::NDArray s = nd::scalar(2.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(nd::add(a, s));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_BroadcastAddFallback)->Arg(1024)->Arg(65536);

}  // namespace
