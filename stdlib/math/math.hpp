#pragma once

// cheatah math — mathematical utilities. Mirrors Python's `math` module plus
// the math-related built-ins (abs/min/max/round/pow), gathered here per the
// project's structure: `import math` to use them.
//
// FUTURE: this module will grow vector/matrix operations built on cheatah's
// custom linear-algebra core (cheatah::linalg); those will link the linalg
// library. The scalar surface below is dependency-free.
#include <cmath>
#include <limits>

namespace cheatah::math {

// ---- constants ----
inline constexpr double pi = 3.14159265358979323846;
inline constexpr double e = 2.71828182845904523536;
inline constexpr double tau = 2.0 * pi;
inline constexpr double inf = std::numeric_limits<double>::infinity();
inline constexpr double nan = std::numeric_limits<double>::quiet_NaN();

// ---- math-related built-ins (templated) ----
template <typename T>
T abs(T x) { return x < T{} ? -x : x; }

template <typename T>
const T& min(const T& a, const T& b) { return (b < a) ? b : a; }
template <typename T, typename... Rest>
const T& min(const T& a, const T& b, const Rest&... rest) { return min(min(a, b), rest...); }

template <typename T>
const T& max(const T& a, const T& b) { return (a < b) ? b : a; }
template <typename T, typename... Rest>
const T& max(const T& a, const T& b, const Rest&... rest) { return max(max(a, b), rest...); }

template <typename Base, typename Exp>
double pow(Base base, Exp exp) {
    return std::pow(static_cast<double>(base), static_cast<double>(exp));
}

// ---- scalar functions (compiled into the library) ----
double sqrt(double x);
double cbrt(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double trunc(double x);
double round(double x);
double exp(double x);
double log(double x);     // natural log
double log2(double x);
double log10(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double hypot(double x, double y);
double fmod(double x, double y);
double copysign(double x, double y);
double degrees(double radians);
double radians(double degrees);
bool isnan(double x);
bool isinf(double x);
bool isfinite(double x);

long long gcd(long long a, long long b);
long long factorial(long long n);

} // namespace cheatah::math
