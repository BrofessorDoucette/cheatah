// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run unit tests for the `random` module: one test per function. Each
// writes a tiny .purr that calls a single random function, compiles it with
// purrc, runs it under the cheatah runtime, and asserts the exact stdout.
// Complements the in-process unit tests (stdlib/tests/random_test.cpp) and the
// per-module system-level test (StdlibE2E.Random).
//
// RNG is non-deterministic until seeded, so every program seeds first and then
// asserts a DETERMINISTIC property (degenerate range, in-bounds membership, or
// same-seed reproducibility) — never a raw random value.
#include "e2e_harness.hpp"

// seed: same seed => same stream, proven by drawing twice and comparing.
TEST(RandomCompileRun, Seed) {
    e2e::expect_e2e("random_seed", R"PURR(import io
import random
random.seed(42)
let a = random.random()
random.seed(42)
let b = random.random()
io.print(a == b)
)PURR", "True\n");
}

// random: seeded value lies in the half-open unit interval [0, 1).
TEST(RandomCompileRun, Random) {
    e2e::expect_e2e("random_random", R"PURR(import io
import random
random.seed(42)
let r = random.random()
io.print(0.0 <= r and r < 1.0)
)PURR", "True\n");
}

// uniform: reproducible under a fixed seed and within the requested bounds.
TEST(RandomCompileRun, Uniform) {
    e2e::expect_e2e("random_uniform", R"PURR(import io
import random
random.seed(42)
let a = random.uniform(-2.0, 3.0)
random.seed(42)
let b = random.uniform(-2.0, 3.0)
io.print(a == b and -2.0 <= a and a <= 3.0)
)PURR", "True\n");
}

// randint: a degenerate range is fully deterministic — randint(5, 5) == 5.
TEST(RandomCompileRun, Randint) {
    e2e::expect_e2e("random_randint", R"PURR(import io
import random
random.seed(42)
io.print(random.randint(5, 5))
)PURR", "5\n");
}

// gauss: reproducible under a fixed seed (same seed => same deviate).
TEST(RandomCompileRun, Gauss) {
    e2e::expect_e2e("random_gauss", R"PURR(import io
import random
random.seed(42)
let a = random.gauss(0.0, 1.0)
random.seed(42)
let b = random.gauss(0.0, 1.0)
io.print(a == b)
)PURR", "True\n");
}

// choice: picking from an all-equal sequence is deterministic (always 99).
TEST(RandomCompileRun, Choice) {
    e2e::expect_e2e("random_choice", R"PURR(import io
import random
random.seed(42)
let xs = [99, 99, 99]
io.print(random.choice(xs))
)PURR", "99\n");
}
