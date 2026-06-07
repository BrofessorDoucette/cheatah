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
 * @param data the numeric range.
 * @return Σ@p data as `double`.
 * @note O(n) single pass; no heap.
 * @test CheatahStatistics.SumCountMean
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
 * @note O(n) single pass; no heap.
 * @test CheatahStatistics.SumCountMean
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
 * @param data the numeric range.
 * @return the mean, or 0.0 if empty.
 * @note O(n) (two passes: count + sum); no heap.
 * @test CheatahStatistics.SumCountMean
 */
template <NumericRange R>
double mean(const R& data) {
    const std::size_t n = count(data);
    return n == 0 ? 0.0 : sum(data) / static_cast<double>(n);
}

/**
 * Population variance (divide by N).
 * @param data the numeric range.
 * @return the variance, or 0.0 if empty.
 * @note O(n); no heap.
 * @test CheatahStatistics.PopulationVarianceAndStdev
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
 * @param data the numeric range.
 * @return √pvariance(@p data).
 * @note O(n); no heap.
 * @test CheatahStatistics.PopulationVarianceAndStdev
 */
template <NumericRange R>
double pstdev(const R& data) { return std::sqrt(pvariance(data)); }

/**
 * Sample variance (divide by N−1).
 * @param data the numeric range.
 * @return the variance, or 0.0 if fewer than 2 elements.
 * @note O(n); no heap.
 * @test CheatahStatistics.SampleVarianceAndStdev
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
 * @param data the numeric range.
 * @return √variance(@p data).
 * @note O(n); no heap.
 * @test CheatahStatistics.SampleVarianceAndStdev
 */
template <NumericRange R>
double stdev(const R& data) { return std::sqrt(variance(data)); }

/**
 * Median (mean of the two middle values when the count is even).
 * @param data the numeric range.
 * @return the median, or 0.0 if empty.
 * @note O(n log n) — copies the elements into a vector and sorts; allocates a temporary
 *   `std::vector<double>`.
 * @test CheatahStatistics.MedianOddAndEven
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
