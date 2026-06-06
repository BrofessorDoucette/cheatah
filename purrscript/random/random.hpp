#pragma once

// cheatah random — pseudo-random numbers, mirroring the core of
// https://docs.python.org/3/library/random.html. Backed by a seedable Mersenne
// Twister (std::mt19937_64). `gauss` gives normal deviates for Monte Carlo.
#include <cstddef>
#include <ranges>

namespace cheatah::random {

void seed(unsigned long long s);            // make the stream reproducible
double random();                            // uniform double in [0, 1)
double uniform(double a, double b);         // uniform double in [a, b]
long long randint(long long a, long long b);  // uniform int in [a, b] (inclusive)
double gauss(double mu, double sigma);      // normal deviate

// choice(seq): a random element of a random-access sequence (list/array).
template <std::ranges::random_access_range R>
std::ranges::range_value_t<R> choice(const R& seq) {
    const auto n = static_cast<long long>(std::ranges::size(seq));
    return seq[static_cast<std::size_t>(randint(0, n - 1))];
}

} // namespace cheatah::random
