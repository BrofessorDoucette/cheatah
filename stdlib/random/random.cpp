// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "random.hpp"

#include <cmath>
#include <cstdint>
#include <random>

namespace cheatah::random {

namespace {

// WHY THE DISTRIBUTIONS ARE WRITTEN OUT HERE INSTEAD OF USING <random>'s
//
// `std::mt19937_64` is fully specified by the standard: the same seed gives the same 64-bit stream
// on every implementation, forever. The DISTRIBUTIONS are not. `std::uniform_real_distribution`,
// `uniform_int_distribution` and `normal_distribution` are all implementation-defined, so libstdc++
// and libc++ turn one identical engine stream into DIFFERENT numbers.
//
// That quietly broke the promise `seed()` makes. A program seeded with 42 printed one pi estimate on
// Linux and another on macOS — caught the first time the test suite ran on Apple Silicon, by a test
// whose own comment claimed "seeded RNG makes the estimate fully reproducible". It was reproducible
// per platform, which is not the same thing and is the more dangerous kind of almost-true.
//
// Defining the mappings here makes a seed mean one thing everywhere. All three are the standard
// constructions, chosen so the arithmetic is exact and the consumption pattern is fixed:
// each call takes a known number of engine draws, so two runs cannot drift apart.

std::mt19937_64& engine() {
    // One engine PER THREAD: the `thread` module makes concurrent random() calls reachable, and a
    // single shared mt19937_64 would be a data race (torn state, lost advances). Each thread
    // self-seeds from std::random_device on first use; seed(s) seeds the CALLING thread only.
    thread_local std::mt19937_64 e{std::random_device{}()};
    return e;
}

/// A double in [0, 1) from ONE engine draw. The top 53 bits scaled by 2^-53: 53 is exactly the
/// mantissa width, so every representable value is reachable and the multiply is exact.
double canonical() {
    return static_cast<double>(engine()() >> 11) * 0x1.0p-53;
}

}  // namespace

void seed(unsigned long long s) { engine().seed(s); }

double random() { return canonical(); }

double uniform(double a, double b) { return a + (b - a) * canonical(); }

long long randint(long long a, long long b) {
    if (b < a) {
        return a;
    }
    // Unbiased over the inclusive range by REJECTION rather than modulo: taking `draw % span`
    // directly would favour the low end whenever span does not divide 2^64. Computed in unsigned
    // arithmetic so a range spanning the whole of long long (where b - a overflows a signed type)
    // is still handled exactly.
    const std::uint64_t span = static_cast<std::uint64_t>(b) - static_cast<std::uint64_t>(a) + 1u;
    if (span == 0u) {  // the full 64-bit range: every draw is already uniform
        return static_cast<long long>(engine()());
    }
    const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % span) - 1u;
    std::uint64_t draw = engine()();
    while (draw > limit) {  // discard the biased tail and redraw
        draw = engine()();
    }
    const std::uint64_t picked = static_cast<std::uint64_t>(a) + (draw % span);  // wraps back into [a, b]
    return static_cast<long long>(picked);
}

double gauss(double mu, double sigma) {
    // Box-Muller, using ONE of the two values it produces. Keeping the spare would be cheaper but
    // would make a call's output depend on how many gauss() calls preceded it, so the stream would
    // no longer be a pure function of the seed and the call sequence. Two draws per call, always.
    double u1 = canonical();
    while (u1 <= 0.0) {  // log(0) is undefined; a zero draw is astronomically rare but not impossible
        u1 = canonical();
    }
    const double u2 = canonical();
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    return mu + sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
}

}  // namespace cheatah::random
