// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level test for the cheatah stdlib `math` module: ONE cohesive program
// that exercises EVERY public function and constant declared in
// stdlib/math/math.hpp (sqrt, cbrt, fabs, floor, ceil, trunc, round, exp, log,
// log2, log10, sin, cos, tan, asin, acos, atan, atan2, hypot, fmod, copysign,
// degrees, radians, isnan, isinf, isfinite, gcd, factorial, and the math-related
// built-ins abs/min/max/pow, plus the constants pi/e/tau/inf/nan).
//
// Unlike the per-function compile-run tests (math_cr_test.cpp), this drives a
// small geometry / number-theory pipeline that wires the functions together
// (round_to uses round+pow; unit_point uses cos/sin/hypot; the combined section
// reduces a fraction by gcd then norms it via pow/sqrt) and prints a single
// deterministic multi-line report.
//
// The expected stdout below was captured by compiling and running the program
// with the debug-build purrc + cheatah and confirming the output is identical
// across repeated runs (md5 stable) before hardcoding it. Nothing here is
// time/RNG dependent, so it is reproducible byte-for-byte.

#include "e2e_harness.hpp"

TEST(StdlibE2E, Math) {
    e2e::expect_e2e("math_sys", R"PURR(import io
import math

# A cohesive pipeline exercising every public function/constant in stdlib/math.
# It builds a little geometry/number-theory "report" from a few seeds.

# --- helpers ---------------------------------------------------------------

# Round a double to `digits` decimal places (uses round/pow).
fn round_to(x, digits) {
    let scale = math.pow(10, digits)
    return math.round(x * scale) / scale
}

# Polar -> cartesian distance check using trig + hypot.
fn unit_point(theta) {
    # returns hypot(cos, sin) which is 1 for any theta
    return math.hypot(math.cos(theta), math.sin(theta))
}

# --- 1) constants ----------------------------------------------------------
io.print("== constants ==")
io.print("pi", round_to(math.pi, 5), "e", round_to(math.e, 5), "tau", round_to(math.tau, 5))
io.print("inf", math.inf, "nan", math.nan)

# --- 2) roots and powers ---------------------------------------------------
let r = math.sqrt(2.0)             # ~1.41421
let c = math.cbrt(27.0)            # 3
let p = math.pow(2, 10)            # 1024
io.print("== roots/powers ==")
io.print("sqrt2", round_to(r, 5), "cbrt27", c, "pow", p)

# --- 3) rounding family ----------------------------------------------------
let v = -2.7
io.print("== rounding ==")
io.print("fabs", math.fabs(v), "floor", math.floor(v), "ceil", math.ceil(v))
io.print("trunc", math.trunc(v), "round", math.round(v))

# --- 4) exponentials / logs ------------------------------------------------
let x = math.exp(1.0)              # e
io.print("== exp/log ==")
io.print("exp1", round_to(x, 5))
io.print("ln_e", round_to(math.log(math.e), 5), "log2_8", math.log2(8.0), "log10_1000", math.log10(1000.0))

# --- 5) trigonometry -------------------------------------------------------
let theta = math.pi / 4.0          # 45 degrees
io.print("== trig ==")
io.print("sin", round_to(math.sin(theta), 5), "cos", round_to(math.cos(theta), 5), "tan", round_to(math.tan(theta), 5))
io.print("asin", round_to(math.asin(1.0), 5), "acos", round_to(math.acos(1.0), 5))
io.print("atan", round_to(math.atan(1.0), 5), "atan2", round_to(math.atan2(1.0, 1.0), 5))
io.print("unit_pt", round_to(unit_point(theta), 5))

# --- 6) misc real ----------------------------------------------------------
io.print("== misc ==")
io.print("hypot", math.hypot(3.0, 4.0), "fmod", math.fmod(7.0, 3.0), "copysign", math.copysign(3.0, -1.0))
io.print("degrees", math.degrees(math.pi), "radians", round_to(math.radians(180.0), 5))

# --- 7) classification -----------------------------------------------------
io.print("== classify ==")
io.print("isnan", math.isnan(math.nan), "isinf", math.isinf(math.inf), "isfinite", math.isfinite(1.0))

# --- 8) integer ops + builtins ---------------------------------------------
io.print("== integer/builtins ==")
io.print("gcd", math.gcd(48, 36), "fact", math.factorial(6))
io.print("abs", math.abs(-7), "min", math.min(3, 9, 1, 5), "max", math.max(3, 9, 1, 5))

# --- 9) a small combined computation ---------------------------------------
# Reduce a fraction 48/36 by its gcd, then take pow/sqrt of the pieces.
let g = math.gcd(48, 36)
let num = 48 / g
let den = 36 / g
let combined = math.sqrt(math.pow(num, 2) + math.pow(den, 2))
io.print("== combined ==")
io.print("reduced", num, den, "norm", round_to(combined, 5))
)PURR",
                   "== constants ==\n"
                   "pi 3.14159 e 2.71828 tau 6.28319\n"
                   "inf inf nan nan\n"
                   "== roots/powers ==\n"
                   "sqrt2 1.41421 cbrt27 3 pow 1024\n"
                   "== rounding ==\n"
                   "fabs 2.7 floor -3 ceil -2\n"
                   "trunc -2 round -3\n"
                   "== exp/log ==\n"
                   "exp1 2.71828\n"
                   "ln_e 1 log2_8 3 log10_1000 3\n"
                   "== trig ==\n"
                   "sin 0.70711 cos 0.70711 tan 1\n"
                   "asin 1.5708 acos 0\n"
                   "atan 0.7854 atan2 0.7854\n"
                   "unit_pt 1\n"
                   "== misc ==\n"
                   "hypot 5 fmod 1 copysign -3\n"
                   "degrees 180 radians 3.14159\n"
                   "== classify ==\n"
                   "isnan True isinf True isfinite True\n"
                   "== integer/builtins ==\n"
                   "gcd 12 fact 720\n"
                   "abs 7 min 1 max 9\n"
                   "== combined ==\n"
                   "reduced 4 3 norm 5\n");
}
