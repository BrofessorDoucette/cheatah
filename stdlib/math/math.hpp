#pragma once

/**
 * @file math.hpp
 * @brief cheatah `math` — scalar math: Python's `math` module plus the
 *        `abs`/`min`/`max`/`pow` built-ins. `import math` to use it.
 *
 * Every function here is **pure and allocation-free** (operates on `double` /
 * `long long` by value). Unit tests: `stdlib/tests/math_test.cpp`. The whole
 * suite runs under AddressSanitizer (the `asan` preset) and Valgrind
 * (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * Doc convention (see also the other stdlib headers): each function documents
 * its runtime complexity with @complexity, its heap allocation with @alloc, and
 * the @test that covers it.
 */
#include <cmath>
#include <concepts>
#include <limits>
#include <type_traits>

namespace cheatah::math {

// Concepts naming what each scalar op needs, so a misuse fails with the concept's
// name rather than a deep template error (see constrain-all-templates policy).
/// Numeric<T>: an arithmetic type — the int/float family `abs`/`pow` operate on.
template <typename T>
concept Numeric = std::is_arithmetic_v<T>;
/// Ordered<T>: `<`-comparable, so `min`/`max` can pick the smaller/larger.
template <typename T>
concept Ordered = requires(const T& a, const T& b) {
    { a < b } -> std::convertible_to<bool>;
};

// ---- constants ----
inline constexpr double pi = 3.14159265358979323846;   ///< π.
inline constexpr double e = 2.71828182845904523536;    ///< Euler's number e.
inline constexpr double tau = 2.0 * pi;                ///< τ = 2π.
inline constexpr double inf = std::numeric_limits<double>::infinity();    ///< +∞.
inline constexpr double nan = std::numeric_limits<double>::quiet_NaN();   ///< quiet NaN.

// ---- math-related built-ins (templated) ----

/**
 * Absolute value.
 * @param x any signed value.
 * @return |@p x|.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahMath.BuiltinLikeOps
 * @crtest MathCompileRun.Abs
 * @systest StdlibE2E.Math
 */
template <Numeric T>
T abs(T x) { return x < T{} ? -x : x; }

/**
 * Smallest of two-or-more values (variadic; the overloads chain to fold extra args).
 *
 * Returns a reference bound to whichever argument compares smaller; on a tie
 * (neither `b < a`) it returns @p a, the first argument. Because the result is
 * a reference into the caller's arguments, it dangles if the operands are
 * temporaries that outlive the call expression.
 * @param a,b the values to compare (`operator<` required).
 * @return a reference to the minimum.
 * @complexity O(n) in the argument count.
 * @alloc none.
 * @test CheatahMath.BuiltinLikeOps
 * @crtest MathCompileRun.Min
 * @systest StdlibE2E.Math
 */
template <Ordered T>
const T& min(const T& a, const T& b) { return (b < a) ? b : a; }
/**
 * Smallest of three-or-more values (folds the extra args onto the two-argument overload).
 * @param a,b the first two values.
 * @param rest the remaining values (`operator<` required).
 * @return a reference to the minimum.
 * @test CheatahMath.BuiltinLikeOps
 * @crtest MathCompileRun.Min
 * @systest StdlibE2E.Math
 */
template <Ordered T, Ordered... Rest>
const T& min(const T& a, const T& b, const Rest&... rest) { return min(min(a, b), rest...); }

/**
 * Largest of two-or-more values (variadic; the overloads chain to fold extra args).
 *
 * Returns a reference bound to whichever argument compares larger; on a tie
 * (neither `a < b`) it returns @p a, the first argument. As with min, the
 * returned reference dangles if the operands are temporaries.
 * @param a,b the values to compare (`operator<` required).
 * @return a reference to the maximum.
 * @complexity O(n) in the argument count.
 * @alloc none.
 * @test CheatahMath.BuiltinLikeOps
 * @crtest MathCompileRun.Max
 * @systest StdlibE2E.Math
 */
template <Ordered T>
const T& max(const T& a, const T& b) { return (a < b) ? b : a; }
/**
 * Largest of three-or-more values (folds the extra args onto the two-argument overload).
 * @param a,b the first two values.
 * @param rest the remaining values (`operator<` required).
 * @return a reference to the maximum.
 * @test CheatahMath.BuiltinLikeOps
 * @crtest MathCompileRun.Max
 * @systest StdlibE2E.Math
 */
template <Ordered T, Ordered... Rest>
const T& max(const T& a, const T& b, const Rest&... rest) { return max(max(a, b), rest...); }

/**
 * Power.
 *
 * Both operands are cast to `double` and forwarded to `std::pow`, so this
 * follows IEEE-754 semantics (e.g. `pow(0, 0)` is 1, and a negative base with a
 * non-integer exponent yields NaN); integer arguments lose exactness beyond
 * 2^53.
 * @param base the base.
 * @param exp the exponent.
 * @return @p base raised to @p exp (computed as `double`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahMath.BuiltinLikeOps
 * @crtest MathCompileRun.Pow
 * @systest StdlibE2E.Math
 */
template <Numeric Base, Numeric Exp>
double pow(Base base, Exp exp) {
    return std::pow(static_cast<double>(base), static_cast<double>(exp));
}

// ---- scalar functions (compiled into the library) ----
// All of the following are O(1) time with no heap allocation.

/**
 * Square root.
 *
 * Returns NaN (rather than throwing) for a negative radicand; `sqrt(-0.0)` is
 * `-0.0` and `sqrt(+inf)` is `+inf`.
 * @param x radicand (NaN if @p x < 0).
 * @return √@p x.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 * @crtest MathCompileRun.Sqrt
 * @systest StdlibE2E.Math
 */
double sqrt(double x);
/**
 * Cube root.
 *
 * Defined for the whole real line, including negatives (unlike sqrt): the
 * result keeps the sign of @p x, so `cbrt(-8)` is `-2`.
 * @param x any real.
 * @return ∛@p x.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Cbrt
 * @systest StdlibE2E.Math
 */
double cbrt(double x);
/**
 * Absolute value of a double.
 * @param x any real.
 * @return |@p x|.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Fabs
 * @systest StdlibE2E.Math
 */
double fabs(double x);
/**
 * Round toward −∞.
 *
 * Returns the largest integral value not greater than @p x as a `double`;
 * already-integral, NaN, and ±∞ inputs are returned unchanged, and the sign of
 * zero is preserved.
 * @param x any real.
 * @return ⌊@p x⌋.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 * @crtest MathCompileRun.Floor
 * @systest StdlibE2E.Math
 */
double floor(double x);
/**
 * Round toward +∞.
 *
 * Returns the smallest integral value not less than @p x as a `double`;
 * already-integral, NaN, and ±∞ inputs are returned unchanged. For @p x in
 * (−1, 0) the result is `-0.0`.
 * @param x any real.
 * @return ⌈@p x⌉.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 * @crtest MathCompileRun.Ceil
 * @systest StdlibE2E.Math
 */
double ceil(double x);
/**
 * Round toward zero.
 *
 * Discards the fractional part, rounding toward zero rather than ±∞ (so it
 * differs from floor on negatives, e.g. `trunc(-2.7)` is `-2.0`); the sign of
 * @p x, NaN, and ±∞ are preserved.
 * @param x any real.
 * @return @p x with the fraction dropped.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Trunc
 * @systest StdlibE2E.Math
 */
double trunc(double x);
/**
 * Round to nearest (half away from zero).
 *
 * Halfway cases are rounded away from zero, not to even, so `round(2.5)` is `3`
 * and `round(-2.5)` is `-3` — this differs from Python's banker's rounding;
 * NaN and ±∞ pass through unchanged.
 * @param x any real.
 * @return rounded @p x.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 * @crtest MathCompileRun.Round
 * @systest StdlibE2E.Math
 */
double round(double x);
/**
 * Exponential.
 *
 * Overflows to `+inf` for large @p x and underflows to `0` for large negative
 * @p x; `exp(-inf)` is `0` and `exp(+inf)` is `+inf`.
 * @param x any real.
 * @return e^@p x.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Exp
 * @systest StdlibE2E.Math
 */
double exp(double x);
/**
 * Natural logarithm.
 *
 * Returns `-inf` for @p x == 0 and NaN (rather than throwing) for negative
 * @p x; out-of-domain input never raises an exception.
 * @param x > 0.
 * @return ln(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Log
 * @systest StdlibE2E.Math
 */
double log(double x);
/**
 * Base-2 logarithm.
 *
 * Returns `-inf` for @p x == 0 and NaN for negative @p x, matching log's
 * out-of-domain behavior.
 * @param x > 0.
 * @return log₂(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 * @crtest MathCompileRun.Log2
 * @systest StdlibE2E.Math
 */
double log2(double x);
/**
 * Base-10 logarithm.
 *
 * Returns `-inf` for @p x == 0 and NaN for negative @p x, matching log's
 * out-of-domain behavior.
 * @param x > 0.
 * @return log₁₀(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Log10
 * @systest StdlibE2E.Math
 */
double log10(double x);
/**
 * Sine.
 *
 * The argument is interpreted in radians; precision degrades for very large
 * magnitudes due to argument reduction, and `sin(±inf)` is NaN.
 * @param x radians.
 * @return sin(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 * @crtest MathCompileRun.Sin
 * @systest StdlibE2E.Math
 */
double sin(double x);
/**
 * Cosine.
 *
 * The argument is interpreted in radians; precision degrades for very large
 * magnitudes due to argument reduction, and `cos(±inf)` is NaN.
 * @param x radians.
 * @return cos(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 * @crtest MathCompileRun.Cos
 * @systest StdlibE2E.Math
 */
double cos(double x);
/**
 * Tangent.
 *
 * The argument is in radians; near the poles (odd multiples of π/2, which are
 * not exactly representable) the result is a large finite value rather than
 * ±∞, and `tan(±inf)` is NaN.
 * @param x radians.
 * @return tan(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 * @crtest MathCompileRun.Tan
 * @systest StdlibE2E.Math
 */
double tan(double x);
/**
 * Arcsine.
 *
 * Returns a value in [−π/2, π/2]; arguments outside [−1, 1] yield NaN rather
 * than throwing.
 * @param x in [−1, 1].
 * @return asin(@p x) in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 * @crtest MathCompileRun.Asin
 * @systest StdlibE2E.Math
 */
double asin(double x);
/**
 * Arccosine.
 *
 * Returns a value in [0, π]; arguments outside [−1, 1] yield NaN rather than
 * throwing.
 * @param x in [−1, 1].
 * @return acos(@p x) in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 * @crtest MathCompileRun.Acos
 * @systest StdlibE2E.Math
 */
double acos(double x);
/**
 * Arctangent.
 *
 * Accepts the whole real line and returns a value in (−π/2, π/2), approaching
 * ±π/2 as @p x → ±∞.
 * @param x any real.
 * @return atan(@p x) in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 * @crtest MathCompileRun.Atan
 * @systest StdlibE2E.Math
 */
double atan(double x);
/**
 * Two-argument arctangent.
 *
 * Uses the signs of both arguments to select the correct quadrant, returning a
 * value in (−π, π]; it is well-defined when @p x is zero (including the
 * `atan2(0, 0)` case, which returns 0).
 * @param y,x the coordinates.
 * @return atan2(@p y, @p x) in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 * @crtest MathCompileRun.Atan2
 * @systest StdlibE2E.Math
 */
double atan2(double y, double x);
/**
 * Hypotenuse.
 *
 * Computes the 2-norm while avoiding intermediate overflow/underflow that a
 * naive `sqrt(x*x + y*y)` would suffer; returns `+inf` if either argument is
 * infinite (even when the other is NaN).
 * @param x,y the legs.
 * @return √(@p x²+@p y²) without overflow.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 * @crtest MathCompileRun.Hypot
 * @systest StdlibE2E.Math
 */
double hypot(double x, double y);
/**
 * Floating-point remainder.
 *
 * Returns `x - n*y` for the integer `n` truncated toward zero, so the result
 * takes the sign of the dividend @p x (unlike a Python-style modulo); a zero
 * divisor yields NaN rather than throwing.
 * @param x,y dividend, divisor.
 * @return @p x mod @p y.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Fmod
 * @systest StdlibE2E.Math
 */
double fmod(double x, double y);
/**
 * Copy sign.
 *
 * Takes the magnitude from @p x and the sign bit from @p y; because it copies
 * the IEEE sign bit, it distinguishes `+0.0` from `-0.0` and works even when
 * @p x is NaN.
 * @param x magnitude source,
 * @param y sign source.
 * @return |@p x| with @p y's sign.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Copysign
 * @systest StdlibE2E.Math
 */
double copysign(double x, double y);
/**
 * Radians → degrees.
 * @param radians angle in radians.
 * @return the angle in degrees.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 * @crtest MathCompileRun.Degrees
 * @systest StdlibE2E.Math
 */
double degrees(double radians);
/**
 * Degrees → radians.
 * @param degrees angle in degrees.
 * @return the angle in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 * @crtest MathCompileRun.Radians
 * @systest StdlibE2E.Math
 */
double radians(double degrees);
/**
 * Is NaN?
 *
 * The reliable NaN test, since NaN compares unequal to everything including
 * itself (so `x != x` is the only other portable check).
 * @param x any real.
 * @return true iff @p x is NaN.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.IsFiniteIsNanIsInf
 * @crtest MathCompileRun.Isnan
 * @systest StdlibE2E.Math
 */
bool isnan(double x);
/**
 * Is infinite?
 *
 * True for both `+inf` and `-inf`; false for NaN (use isnan for that) and for
 * every finite value.
 * @param x any real.
 * @return true iff @p x is ±∞.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.IsFiniteIsNanIsInf
 * @crtest MathCompileRun.Isinf
 * @systest StdlibE2E.Math
 */
bool isinf(double x);
/**
 * Is finite?
 * @param x any real.
 * @return true iff @p x is neither NaN nor ±∞.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.IsFiniteIsNanIsInf
 * @crtest MathCompileRun.Isfinite
 * @systest StdlibE2E.Math
 */
bool isfinite(double x);

/**
 * Greatest common divisor.
 *
 * Operates on the absolute values via the Euclidean algorithm, so the result is
 * non-negative; `gcd(0, 0)` is 0 and `gcd(n, 0)` is `|n|`. Passing
 * `LLONG_MIN` overflows when negated.
 * @param a,b integers.
 * @return gcd(|@p a|, |@p b|).
 * @complexity O(log min(a,b)) time.
 * @alloc none.
 * @test CheatahMath.Integer
 * @crtest MathCompileRun.Gcd
 * @systest StdlibE2E.Math
 */
long long gcd(long long a, long long b);
/**
 * Factorial.
 *
 * Computed by an iterative product from 2; @p n of 0 or 1 returns 1, and any
 * negative @p n also returns 1 since the loop never executes (no error is
 * raised). Results past 20! silently overflow `long long`.
 * @param n ≥ 0 (small; overflows `long long` past 20!).
 * @return @p n!.
 * @complexity O(@p n) time.
 * @alloc none.
 * @test CheatahMath.Integer
 * @crtest MathCompileRun.Factorial
 * @systest StdlibE2E.Math
 */
long long factorial(long long n);

} // namespace cheatah::math
