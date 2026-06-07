#pragma once

/**
 * @file random.hpp
 * @brief cheatah `random` — pseudo-random numbers, mirroring the core of
 *        Python's `random` module. Backed by a seedable Mersenne Twister
 *        (`std::mt19937_64`); `gauss` gives normal deviates for Monte Carlo.
 *        `import random` to use it.
 *
 * Unit tests: `stdlib/tests/random_test.cpp`; the suite runs under
 * AddressSanitizer (the `asan` preset) and Valgrind
 * (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * Doc convention (see also the other stdlib headers): each function notes its
 * runtime complexity, whether it touches the heap, and the @test that covers it.
 */
#include <cstddef>
#include <ranges>

namespace cheatah::random {

/**
 * Seed the shared engine, making the stream reproducible.
 * @param s the seed.
 * @note O(1) time; no heap.
 * @test CheatahRandom.SeedMakesTheStreamReproducible
 */
void seed(unsigned long long s);
/**
 * Uniform random double.
 * @return a value in [0, 1).
 * @note O(1) time; no heap.
 * @test CheatahRandom.RandomInUnitInterval
 */
double random();
/**
 * Uniform random double in a range.
 * @param a,b the bounds.
 * @return a value in [@p a, @p b].
 * @note O(1) time; no heap.
 * @test CheatahRandom.UniformInRange
 */
double uniform(double a, double b);
/**
 * Uniform random integer.
 * @param a,b inclusive bounds.
 * @return an integer in [@p a, @p b].
 * @note O(1) time; no heap.
 * @test CheatahRandom.RandintInclusiveRange
 */
long long randint(long long a, long long b);
/**
 * Normal (Gaussian) deviate.
 * @param mu mean.
 * @param sigma standard deviation.
 * @return a normal sample.
 * @note O(1) time; no heap.
 * @test CheatahRandom.GaussIsFiniteAndReproducible
 */
double gauss(double mu, double sigma);

/**
 * Random element of a random-access sequence (list/array).
 * @param seq the sequence to pick from (must be non-empty).
 * @return a copy of a uniformly chosen element.
 * @note O(1) time; no heap of its own (copies one element; via @ref randint).
 * @test CheatahRandom.Choice
 */
template <std::ranges::random_access_range R>
std::ranges::range_value_t<R> choice(const R& seq) {
    const auto n = static_cast<long long>(std::ranges::size(seq));
    return seq[static_cast<std::size_t>(randint(0, n - 1))];
}

} // namespace cheatah::random
