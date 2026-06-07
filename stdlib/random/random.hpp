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
 * Doc convention (see also the other stdlib headers): each function documents
 * its runtime complexity with @complexity, its heap allocation with @alloc, and
 * the @test that covers it.
 */
#include <cstddef>
#include <ranges>

namespace cheatah::random {

/**
 * Seed the shared engine, making the stream reproducible.
 *
 * Reseeds the single process-wide Mersenne Twister; all `random`/`uniform`/
 * `randint`/`gauss`/`choice` calls draw from it, so two runs seeded with the same
 * value produce identical sequences. Until `seed` is called the engine is seeded
 * non-deterministically from `std::random_device`.
 * @param s the seed.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahRandom.SeedMakesTheStreamReproducible
 * @crtest RandomCompileRun.Seed
 * @systest StdlibE2E.Random
 */
void seed(unsigned long long s);
/**
 * Uniform random double.
 *
 * Draws from the shared engine with a uniform real distribution over the
 * half-open unit interval, so 0.0 can occur but 1.0 cannot.
 * @return a value in [0, 1).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahRandom.RandomInUnitInterval
 * @crtest RandomCompileRun.Random
 * @systest StdlibE2E.Random
 */
double random();
/**
 * Uniform random double in a range.
 *
 * Scales a uniform real distribution to span the given bounds; the caller is
 * expected to pass @p a ≤ @p b (the bounds are not reordered or validated).
 * @param a,b the bounds.
 * @return a value in [@p a, @p b].
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahRandom.UniformInRange
 * @crtest RandomCompileRun.Uniform
 * @systest StdlibE2E.Random
 */
double uniform(double a, double b);
/**
 * Uniform random integer.
 *
 * Returns each integer in the closed range with equal probability; both @p a and
 * @p b are attainable, and @p a == @p b always yields that value.
 * @param a,b inclusive bounds.
 * @return an integer in [@p a, @p b].
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahRandom.RandintInclusiveRange
 * @crtest RandomCompileRun.Randint
 * @systest StdlibE2E.Random
 */
long long randint(long long a, long long b);
/**
 * Normal (Gaussian) deviate.
 *
 * Samples the normal distribution N(@p mu, @p sigma²) from the shared engine; the
 * result is unbounded and can fall on either side of the mean.
 * @param mu mean.
 * @param sigma standard deviation.
 * @return a normal sample.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahRandom.GaussIsFiniteAndReproducible
 * @crtest RandomCompileRun.Gauss
 * @systest StdlibE2E.Random
 */
double gauss(double mu, double sigma);

/**
 * Random element of a random-access sequence (list/array).
 *
 * Picks a uniformly random index in [0, size) via @ref randint and returns a copy
 * of that element. The sequence must be non-empty — an empty @p seq passes an
 * inverted range to @ref randint, which is undefined.
 * @param seq the sequence to pick from (must be non-empty).
 * @return a copy of a uniformly chosen element.
 * @complexity O(1) time.
 * @alloc none (copies one element; via @ref randint).
 * @test CheatahRandom.Choice
 * @crtest RandomCompileRun.Choice
 * @systest StdlibE2E.Random
 */
template <std::ranges::random_access_range R>
std::ranges::range_value_t<R> choice(const R& seq) {
    const auto n = static_cast<long long>(std::ranges::size(seq));
    return seq[static_cast<std::size_t>(randint(0, n - 1))];
}

} // namespace cheatah::random
