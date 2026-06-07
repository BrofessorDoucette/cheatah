// Compile-run unit tests for the `math` module: one test per function. Each writes
// a tiny .purr that calls a single math function, compiles it with purrc, runs it
// under the cheatah runtime, and asserts the exact stdout. Complements the
// in-process unit tests (stdlib/tests/math_test.cpp) and the per-module
// system-level test (StdlibE2E.Math).
#include "e2e_harness.hpp"

TEST(MathCompileRun, Sqrt) {
    e2e::expect_e2e("math_sqrt", R"PURR(import io
import math
io.print(math.sqrt(2.0))
)PURR", "1.41421\n");
}

TEST(MathCompileRun, Cbrt) {
    e2e::expect_e2e("math_cbrt", R"PURR(import io
import math
io.print(math.cbrt(27.0))
)PURR", "3\n");
}

TEST(MathCompileRun, Fabs) {
    e2e::expect_e2e("math_fabs", R"PURR(import io
import math
io.print(math.fabs(-3.5))
)PURR", "3.5\n");
}

TEST(MathCompileRun, Floor) {
    e2e::expect_e2e("math_floor", R"PURR(import io
import math
io.print(math.floor(3.7))
)PURR", "3\n");
}

TEST(MathCompileRun, Ceil) {
    e2e::expect_e2e("math_ceil", R"PURR(import io
import math
io.print(math.ceil(3.2))
)PURR", "4\n");
}

TEST(MathCompileRun, Trunc) {
    e2e::expect_e2e("math_trunc", R"PURR(import io
import math
io.print(math.trunc(-2.9))
)PURR", "-2\n");
}

TEST(MathCompileRun, Round) {
    e2e::expect_e2e("math_round", R"PURR(import io
import math
io.print(math.round(2.5))
)PURR", "3\n");
}

TEST(MathCompileRun, Exp) {
    e2e::expect_e2e("math_exp", R"PURR(import io
import math
io.print(math.exp(1.0))
)PURR", "2.71828\n");
}

TEST(MathCompileRun, Log) {
    e2e::expect_e2e("math_log", R"PURR(import io
import math
io.print(math.log(math.e))
)PURR", "1\n");
}

TEST(MathCompileRun, Log2) {
    e2e::expect_e2e("math_log2", R"PURR(import io
import math
io.print(math.log2(8.0))
)PURR", "3\n");
}

TEST(MathCompileRun, Log10) {
    e2e::expect_e2e("math_log10", R"PURR(import io
import math
io.print(math.log10(1000.0))
)PURR", "3\n");
}

TEST(MathCompileRun, Sin) {
    e2e::expect_e2e("math_sin", R"PURR(import io
import math
io.print(math.sin(0.0))
)PURR", "0\n");
}

TEST(MathCompileRun, Cos) {
    e2e::expect_e2e("math_cos", R"PURR(import io
import math
io.print(math.cos(0.0))
)PURR", "1\n");
}

TEST(MathCompileRun, Tan) {
    e2e::expect_e2e("math_tan", R"PURR(import io
import math
io.print(math.tan(0.0))
)PURR", "0\n");
}

TEST(MathCompileRun, Asin) {
    e2e::expect_e2e("math_asin", R"PURR(import io
import math
io.print(math.asin(1.0))
)PURR", "1.5708\n");
}

TEST(MathCompileRun, Acos) {
    e2e::expect_e2e("math_acos", R"PURR(import io
import math
io.print(math.acos(1.0))
)PURR", "0\n");
}

TEST(MathCompileRun, Atan) {
    e2e::expect_e2e("math_atan", R"PURR(import io
import math
io.print(math.atan(1.0))
)PURR", "0.785398\n");
}

TEST(MathCompileRun, Atan2) {
    e2e::expect_e2e("math_atan2", R"PURR(import io
import math
io.print(math.atan2(1.0, 1.0))
)PURR", "0.785398\n");
}

TEST(MathCompileRun, Hypot) {
    e2e::expect_e2e("math_hypot", R"PURR(import io
import math
io.print(math.hypot(3.0, 4.0))
)PURR", "5\n");
}

TEST(MathCompileRun, Fmod) {
    e2e::expect_e2e("math_fmod", R"PURR(import io
import math
io.print(math.fmod(7.0, 3.0))
)PURR", "1\n");
}

TEST(MathCompileRun, Copysign) {
    e2e::expect_e2e("math_copysign", R"PURR(import io
import math
io.print(math.copysign(3.0, -1.0))
)PURR", "-3\n");
}

TEST(MathCompileRun, Degrees) {
    e2e::expect_e2e("math_degrees", R"PURR(import io
import math
io.print(math.degrees(math.pi))
)PURR", "180\n");
}

TEST(MathCompileRun, Radians) {
    e2e::expect_e2e("math_radians", R"PURR(import io
import math
io.print(math.radians(180.0))
)PURR", "3.14159\n");
}

TEST(MathCompileRun, Isnan) {
    e2e::expect_e2e("math_isnan", R"PURR(import io
import math
io.print(math.isnan(math.nan))
)PURR", "True\n");
}

TEST(MathCompileRun, Isinf) {
    e2e::expect_e2e("math_isinf", R"PURR(import io
import math
io.print(math.isinf(math.inf))
)PURR", "True\n");
}

TEST(MathCompileRun, Isfinite) {
    e2e::expect_e2e("math_isfinite", R"PURR(import io
import math
io.print(math.isfinite(1.0))
)PURR", "True\n");
}

TEST(MathCompileRun, Gcd) {
    e2e::expect_e2e("math_gcd", R"PURR(import io
import math
io.print(math.gcd(12, 18))
)PURR", "6\n");
}

TEST(MathCompileRun, Factorial) {
    e2e::expect_e2e("math_factorial", R"PURR(import io
import math
io.print(math.factorial(5))
)PURR", "120\n");
}

TEST(MathCompileRun, Abs) {
    e2e::expect_e2e("math_abs", R"PURR(import io
import math
io.print(math.abs(-7))
)PURR", "7\n");
}

TEST(MathCompileRun, Min) {
    e2e::expect_e2e("math_min", R"PURR(import io
import math
io.print(math.min(3, 9, 1, 5))
)PURR", "1\n");
}

TEST(MathCompileRun, Max) {
    e2e::expect_e2e("math_max", R"PURR(import io
import math
io.print(math.max(3, 9, 1, 5))
)PURR", "9\n");
}

TEST(MathCompileRun, Pow) {
    e2e::expect_e2e("math_pow", R"PURR(import io
import math
io.print(math.pow(2, 10))
)PURR", "1024\n");
}

TEST(MathCompileRun, ConstPi) {
    e2e::expect_e2e("math_pi", R"PURR(import io
import math
io.print(math.pi)
)PURR", "3.14159\n");
}

TEST(MathCompileRun, ConstE) {
    e2e::expect_e2e("math_const_e", R"PURR(import io
import math
io.print(math.e)
)PURR", "2.71828\n");
}

TEST(MathCompileRun, ConstTau) {
    e2e::expect_e2e("math_tau", R"PURR(import io
import math
io.print(math.tau)
)PURR", "6.28319\n");
}

TEST(MathCompileRun, ConstInf) {
    e2e::expect_e2e("math_inf", R"PURR(import io
import math
io.print(math.inf)
)PURR", "inf\n");
}

TEST(MathCompileRun, ConstNan) {
    e2e::expect_e2e("math_const_nan", R"PURR(import io
import math
io.print(math.nan)
)PURR", "nan\n");
}
