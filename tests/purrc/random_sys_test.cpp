// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level (whole-program) test for the `random` stdlib module. Unlike the
// per-function compile-run tests (tests/purrc/random_cr_test.cpp), this drives a
// single cohesive program through EVERY purr-callable random function and asserts
// its exact stdout, so the functions are exercised together.
//
// RNG is non-deterministic until seeded, so the program seeds first and then
// asserts DETERMINISTIC properties (same-seed reproducibility, in-bounds
// membership, degenerate range, all-equal choice) — never a raw random value.
// Each assertion prints True, confirming every function ran successfully.
//
// Coverage — every function in stdlib/random/random.hpp:
//   seed, random, uniform, randint, gauss, choice.
#include "e2e_harness.hpp"

TEST(StdlibE2E, Random) {
    e2e::expect_e2e("random_sys", R"PURR(import io
import random

# seed + random: same seed reproduces the same draw, which lies in [0, 1).
random.seed(123)
let r1 = random.random()
random.seed(123)
let r2 = random.random()
let random_ok = r1 == r2 and 0.0 <= r1 and r1 < 1.0

# uniform: reproducible under a fixed seed and within the requested bounds.
random.seed(123)
let u1 = random.uniform(-5.0, 5.0)
random.seed(123)
let u2 = random.uniform(-5.0, 5.0)
let uniform_ok = u1 == u2 and -5.0 <= u1 and u1 <= 5.0

# randint: reproducible over a wide range; a degenerate range pins the value.
random.seed(123)
let i1 = random.randint(1, 1000000)
random.seed(123)
let i2 = random.randint(1, 1000000)
let randint_ok = i1 == i2 and random.randint(5, 5) == 5

# gauss: same seed reproduces the same deviate.
random.seed(123)
let g1 = random.gauss(0.0, 1.0)
random.seed(123)
let g2 = random.gauss(0.0, 1.0)
let gauss_ok = g1 == g2

# choice: picking from an all-equal sequence is deterministic.
random.seed(123)
let xs = [7, 7, 7, 7]
let choice_ok = random.choice(xs) == 7

io.print(random_ok, uniform_ok, randint_ok, gauss_ok, choice_ok)
)PURR",
                        "True True True True True\n");
}
