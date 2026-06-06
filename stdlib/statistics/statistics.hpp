#pragma once

// cheatah statistics — descriptive statistics over numeric sequences, mirroring
// https://docs.python.org/3/library/statistics.html. Templated over any range of
// numbers (constrained by the NumericRange concept for clear errors). Header-only.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <type_traits>
#include <vector>

namespace cheatah::statistics {

// NumericRange<R>: an iterable of arithmetic values (list[float], array[int], …).
template <typename R>
concept NumericRange =
    std::ranges::input_range<R> && std::is_arithmetic_v<std::ranges::range_value_t<R>>;

template <NumericRange R>
double sum(const R& data) {
    double s = 0.0;
    for (const auto& x : data) s += static_cast<double>(x);
    return s;
}

template <NumericRange R>
std::size_t count(const R& data) {
    std::size_t n = 0;
    for (const auto& x : data) {
        (void)x;
        ++n;
    }
    return n;
}

template <NumericRange R>
double mean(const R& data) {
    const std::size_t n = count(data);
    return n == 0 ? 0.0 : sum(data) / static_cast<double>(n);
}

// Population variance / std-dev (divide by N).
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
template <NumericRange R>
double pstdev(const R& data) { return std::sqrt(pvariance(data)); }

// Sample variance / std-dev (divide by N-1).
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
template <NumericRange R>
double stdev(const R& data) { return std::sqrt(variance(data)); }

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
