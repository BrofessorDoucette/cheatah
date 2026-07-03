// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Sophisticated multi-module system-level test: a small "Monte Carlo
// simulation" app that only passes if `random`, `math`, `statistics`, and `io`
// all cooperate end to end (purrc + the C++ backend + the runtime + the linked
// stdlib).
//
// The program:
//   - estimates pi by sampling points in the unit square and counting those in
//     the quarter unit circle (random.random + math.sqrt),
//   - asserts reproducibility by running the same seeded estimate twice and
//     comparing for equality,
//   - summarizes seeded gaussian draws with statistics.mean/stdev (math.round
//     keeps the printed values stable),
//   - exercises random.randint / random.uniform as well.
//
// Everything is seeded, so the printed numbers are reproducible byte-for-byte.
// The expected stdout below was verified by compiling and running the program
// three times (twice from one build, once from a fresh compile) and confirming
// identical output before hardcoding.

#include "e2e_harness.hpp"


TEST(SystemApps, MonteCarlo) {
    e2e::expect_e2e("app_montecarlo", R"PURR(import io
import math
import random
import statistics

# Monte Carlo estimate of pi: sample points in the unit square and count
# the fraction that land inside the quarter unit circle. Seeded RNG makes
# the estimate fully reproducible, so the printed result is deterministic.
fn estimate_pi(seed, n) {
    random.seed(seed)
    let inside = 0
    let i = 0
    for i in range(0, n) {
        let x = random.random()
        let y = random.random()
        if math.sqrt(x * x + y * y) <= 1.0 {
            inside = inside + 1
        }
    }
    return 4.0 * inside / n
}

let n = 50000

# (1) Deterministic pi estimate (rounded so the printed value is stable).
let pi_hat = estimate_pi(2024, n)
let pi_rounded = math.round(pi_hat * 1000.0) / 1000.0

# (2) Reproducibility: same seed twice => byte-identical estimate.
let pi_again = estimate_pi(2024, n)
let reproducible = pi_hat == pi_again

# (3) Summarize 10 seeded gauss draws with `statistics`.
let samples = [0.0,  0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
random.seed(99)
let k = 0
for k in  range(0,10) {
    samples[k] = random.gauss(100.0, 15.0)
}
let mu = math.round(statistics.mean(samples) * 100.0) / 100.0
let sd = math.round(statistics.stdev(samples) * 100.0) / 100.0

# (4) randint / uniform sanity, also seeded.
random.seed(7)
let d1 = random.randint(1, 6)
let d2 = random.randint(1, 6)
let u = math.round(random.uniform(0.0, 1.0) * 1000.0) / 1000.0

io.print("pi ~=", pi_rounded)
io.print("reproducible:", reproducible)
io.print("abs err <= 0.05:", math.fabs(pi_hat -3.14159265) <= 0.05)
io.print("gauss mean:", mu, "stdev:", sd)
io.print("dice:", d1, d2, "uniform:", u)
)PURR",
               "pi ~= 3.141\n"
               "reproducible: True\n"
               "abs err <= 0.05: True\n"
               "gauss mean: 100.48 stdev: 15.24\n"
               "dice: 5 6 uniform: 0.117\n");
}
