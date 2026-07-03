// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file statistics.hpp
 * @brief cheatah `statistics` — descriptive statistics over numeric sequences,
 *        mirroring Python's `statistics` module. `import statistics` to use it.
 *
 * Header-only: every function is a template constrained by the `NumericRange`
 * concept, so it accepts any iterable of arithmetic values (`list[float]`,
 * `array[int]`, …) and is instantiated at the call site. Unit tests:
 * `stdlib/tests/statistics_test.cpp`; the suite runs under AddressSanitizer (the
 * `asan` preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * @note `n` below is the element count. Single-pass reductions are O(n) and
 *       allocation-free; `median` is the exception (it materializes and sorts a
 *       copy). Results are scalar `double`/`std::size_t` — no heap on return.
 */
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <type_traits>
#include <vector>

namespace cheatah::statistics {

/// NumericRange<R>: an iterable of arithmetic values (list[float], array[int], …).
template <typename R>
concept NumericRange =
    std::ranges::input_range<R> && std::is_arithmetic_v<std::ranges::range_value_t<R>>;

/**
 * Sum of the elements.
 *
 * Accumulates every element into a `double`, so an empty range sums to 0.0 and
 * integer inputs are widened before adding (no integer overflow).
 * @param data the numeric range.
 * @return Σ@p data as `double`.
 * @complexity O(n) single pass.
 * @alloc none.
 * @test CheatahStatistics.SumCountMean
 * @crtest StatisticsCompileRun.Sum
 * @systest StdlibE2E.Statistics
 */
template <NumericRange R>
double sum(const R& data) {
    double s = 0.0;
    for (const auto& x : data) s += static_cast<double>(x);
    return s;
}

/**
 * Element count.
 * @param data the numeric range.
 * @return the number of elements.
 * @complexity O(n) single pass.
 * @alloc none.
 * @test CheatahStatistics.SumCountMean
 * @crtest StatisticsCompileRun.Count
 * @systest StdlibE2E.Statistics
 */
template <NumericRange R>
std::size_t count(const R& data) {
    std::size_t n = 0;
    for (const auto& x : data) {
        (void)x;
        ++n;
    }
    return n;
}

/**
 * Arithmetic mean.
 *
 * Computes sum/count, but guards division by zero: an empty range returns 0.0
 * rather than NaN.
 * @param data the numeric range.
 * @return the mean, or 0.0 if empty.
 * @complexity O(n) (two passes: count + sum).
 * @alloc none.
 * @test CheatahStatistics.SumCountMean
 * @crtest StatisticsCompileRun.Mean
 * @systest StdlibE2E.Statistics
 */
template <NumericRange R>
double mean(const R& data) {
    const std::size_t n = count(data);
    return n == 0 ? 0.0 : sum(data) / static_cast<double>(n);
}

/**
 * Population variance (divide by N).
 *
 * Mean of the squared deviations from the mean, dividing by N (treats @p data as
 * the entire population). Returns 0.0 for an empty or single-element range.
 * @param data the numeric range.
 * @return the variance, or 0.0 if empty.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahStatistics.PopulationVarianceAndStdev
 * @crtest StatisticsCompileRun.Pvariance
 * @systest StdlibE2E.Statistics
 */
template <NumericRange R>
double pvariance(const R& data) {
    const double m = mean(data);
    double s = 0.0;
    std::size_t n = 0;
    for (const auto& x : data) {
        const double d = static_cast<double>(x) - m;
        s += d * d;
        ++n;
    }
    return n == 0 ? 0.0 : s / static_cast<double>(n);
}
/**
 * Population standard deviation.
 *
 * Square root of @ref pvariance, so it is 0.0 for empty or single-element ranges
 * and never negative.
 * @param data the numeric range.
 * @return √pvariance(@p data).
 * @complexity O(n).
 * @alloc none.
 * @test CheatahStatistics.PopulationVarianceAndStdev
 * @crtest StatisticsCompileRun.Pstdev
 * @systest StdlibE2E.Statistics
 */
template <NumericRange R>
double pstdev(const R& data) { return std::sqrt(pvariance(data)); }

/**
 * Sample variance (divide by N−1).
 *
 * Sum of squared deviations from the mean divided by N−1 (Bessel's correction,
 * estimating the variance of the wider population from a sample). Requires at
 * least two elements; an empty or single-element range returns 0.0 rather than
 * dividing by zero.
 * @param data the numeric range.
 * @return the variance, or 0.0 if fewer than 2 elements.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahStatistics.SampleVarianceAndStdev
 * @crtest StatisticsCompileRun.Variance
 * @systest StdlibE2E.Statistics
 */
template <NumericRange R>
double variance(const R& data) {
    const double m = mean(data);
    double s = 0.0;
    std::size_t n = 0;
    for (const auto& x : data) {
        const double d = static_cast<double>(x) - m;
        s += d * d;
        ++n;
    }
    return n > 1 ? s / static_cast<double>(n - 1) : 0.0;
}
/**
 * Sample standard deviation.
 *
 * Square root of @ref variance, so it is 0.0 when there are fewer than two
 * elements.
 * @param data the numeric range.
 * @return √variance(@p data).
 * @complexity O(n).
 * @alloc none.
 * @test CheatahStatistics.SampleVarianceAndStdev
 * @crtest StatisticsCompileRun.Stdev
 * @systest StdlibE2E.Statistics
 */
template <NumericRange R>
double stdev(const R& data) { return std::sqrt(variance(data)); }

/**
 * Median (mean of the two middle values when the count is even).
 *
 * Copies the elements into a `double` vector, sorts ascending, and returns the
 * middle value (averaging the two central values when the count is even).
 * Returns 0.0 for an empty range.
 * @param data the numeric range.
 * @return the median, or 0.0 if empty.
 * @complexity O(n log n) — copies the elements into a vector and sorts.
 * @alloc allocates a temporary `std::vector<double>`.
 * @test CheatahStatistics.MedianOddAndEven
 * @crtest StatisticsCompileRun.Median
 * @systest StdlibE2E.Statistics
 */
template <NumericRange R>
double median(const R& data) {
    std::vector<double> v;
    for (const auto& x : data) v.push_back(static_cast<double>(x));
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

} // namespace cheatah::statistics
