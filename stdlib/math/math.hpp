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
#include <limits>

namespace cheatah::math {

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
 */
template <typename T>
T abs(T x) { return x < T{} ? -x : x; }

/**
 * Smallest of two-or-more values (variadic; the overloads chain to fold extra args).
 * @param a,b the values to compare (`operator<` required).
 * @return a reference to the minimum.
 * @complexity O(n) in the argument count.
 * @alloc none.
 * @test CheatahMath.BuiltinLikeOps
 */
template <typename T>
const T& min(const T& a, const T& b) { return (b < a) ? b : a; }
/**
 * Smallest of three-or-more values (folds the extra args onto the two-argument overload).
 * @param a,b the first two values.
 * @param rest the remaining values (`operator<` required).
 * @return a reference to the minimum.
 * @test CheatahMath.BuiltinLikeOps
 */
template <typename T, typename... Rest>
const T& min(const T& a, const T& b, const Rest&... rest) { return min(min(a, b), rest...); }

/**
 * Largest of two-or-more values (variadic; the overloads chain to fold extra args).
 * @param a,b the values to compare (`operator<` required).
 * @return a reference to the maximum.
 * @complexity O(n) in the argument count.
 * @alloc none.
 * @test CheatahMath.BuiltinLikeOps
 */
template <typename T>
const T& max(const T& a, const T& b) { return (a < b) ? b : a; }
/**
 * Largest of three-or-more values (folds the extra args onto the two-argument overload).
 * @param a,b the first two values.
 * @param rest the remaining values (`operator<` required).
 * @return a reference to the maximum.
 * @test CheatahMath.BuiltinLikeOps
 */
template <typename T, typename... Rest>
const T& max(const T& a, const T& b, const Rest&... rest) { return max(max(a, b), rest...); }

/**
 * Power.
 * @param base the base.
 * @param exp the exponent.
 * @return @p base raised to @p exp (computed as `double`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahMath.BuiltinLikeOps
 */
template <typename Base, typename Exp>
double pow(Base base, Exp exp) {
    return std::pow(static_cast<double>(base), static_cast<double>(exp));
}

// ---- scalar functions (compiled into the library) ----
// All of the following are O(1) time with no heap allocation.

/**
 * Square root.
 * @param x radicand (NaN if @p x < 0).
 * @return √@p x.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 */
double sqrt(double x);
/**
 * Cube root.
 * @param x any real.
 * @return ∛@p x.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double cbrt(double x);
/**
 * Absolute value of a double.
 * @param x any real.
 * @return |@p x|.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double fabs(double x);
/**
 * Round toward −∞.
 * @param x any real.
 * @return ⌊@p x⌋.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 */
double floor(double x);
/**
 * Round toward +∞.
 * @param x any real.
 * @return ⌈@p x⌉.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 */
double ceil(double x);
/**
 * Round toward zero.
 * @param x any real.
 * @return @p x with the fraction dropped.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double trunc(double x);
/**
 * Round to nearest (half away from zero).
 * @param x any real.
 * @return rounded @p x.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 */
double round(double x);
/**
 * Exponential.
 * @param x any real.
 * @return e^@p x.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double exp(double x);
/**
 * Natural logarithm.
 * @param x > 0.
 * @return ln(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double log(double x);
/**
 * Base-2 logarithm.
 * @param x > 0.
 * @return log₂(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 */
double log2(double x);
/**
 * Base-10 logarithm.
 * @param x > 0.
 * @return log₁₀(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double log10(double x);
/**
 * Sine.
 * @param x radians.
 * @return sin(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 */
double sin(double x);
/**
 * Cosine.
 * @param x radians.
 * @return cos(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 */
double cos(double x);
/**
 * Tangent.
 * @param x radians.
 * @return tan(@p x).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 */
double tan(double x);
/**
 * Arcsine.
 * @param x in [−1, 1].
 * @return asin(@p x) in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 */
double asin(double x);
/**
 * Arccosine.
 * @param x in [−1, 1].
 * @return acos(@p x) in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 */
double acos(double x);
/**
 * Arctangent.
 * @param x any real.
 * @return atan(@p x) in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 */
double atan(double x);
/**
 * Two-argument arctangent.
 * @param y,x the coordinates.
 * @return atan2(@p y, @p x) in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.Trigonometry
 */
double atan2(double y, double x);
/**
 * Hypotenuse.
 * @param x,y the legs.
 * @return √(@p x²+@p y²) without overflow.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 */
double hypot(double x, double y);
/**
 * Floating-point remainder.
 * @param x,y dividend, divisor.
 * @return @p x mod @p y.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double fmod(double x, double y);
/**
 * Copy sign.
 * @param x magnitude source,
 * @param y sign source.
 * @return |@p x| with @p y's sign.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double copysign(double x, double y);
/**
 * Radians → degrees.
 * @param radians angle in radians.
 * @return the angle in degrees.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.ScalarFunctions
 */
double degrees(double radians);
/**
 * Degrees → radians.
 * @param degrees angle in degrees.
 * @return the angle in radians.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.TranscendentalAndRounding
 */
double radians(double degrees);
/**
 * Is NaN?
 * @param x any real.
 * @return true iff @p x is NaN.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.IsFiniteIsNanIsInf
 */
bool isnan(double x);
/**
 * Is infinite?
 * @param x any real.
 * @return true iff @p x is ±∞.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.IsFiniteIsNanIsInf
 */
bool isinf(double x);
/**
 * Is finite?
 * @param x any real.
 * @return true iff @p x is neither NaN nor ±∞.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahMath.IsFiniteIsNanIsInf
 */
bool isfinite(double x);

/**
 * Greatest common divisor.
 * @param a,b integers.
 * @return gcd(|@p a|, |@p b|).
 * @complexity O(log min(a,b)) time.
 * @alloc none.
 * @test CheatahMath.Integer
 */
long long gcd(long long a, long long b);
/**
 * Factorial.
 * @param n ≥ 0 (small; overflows `long long` past 20!).
 * @return @p n!.
 * @complexity O(@p n) time.
 * @alloc none.
 * @test CheatahMath.Integer
 */
long long factorial(long long n);

} // namespace cheatah::math
