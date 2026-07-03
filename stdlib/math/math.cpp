// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "math.hpp"

namespace cheatah::math {

double sqrt(double x) { return std::sqrt(x); }
double cbrt(double x) { return std::cbrt(x); }
double fabs(double x) { return std::fabs(x); }
double floor(double x) { return std::floor(x); }
double ceil(double x) { return std::ceil(x); }
double trunc(double x) { return std::trunc(x); }
double round(double x) { return std::round(x); }
double exp(double x) { return std::exp(x); }
double log(double x) { return std::log(x); }
double log2(double x) { return std::log2(x); }
double log10(double x) { return std::log10(x); }
double sin(double x) { return std::sin(x); }
double cos(double x) { return std::cos(x); }
double tan(double x) { return std::tan(x); }
double asin(double x) { return std::asin(x); }
double acos(double x) { return std::acos(x); }
double atan(double x) { return std::atan(x); }
double atan2(double y, double x) { return std::atan2(y, x); }
double hypot(double x, double y) { return std::hypot(x, y); }
double fmod(double x, double y) { return std::fmod(x, y); }
double copysign(double x, double y) { return std::copysign(x, y); }
double degrees(double radians) { return radians * (180.0 / pi); }
double radians(double degrees) { return degrees * (pi / 180.0); }
bool isnan(double x) { return std::isnan(x); }
bool isinf(double x) { return std::isinf(x); }
bool isfinite(double x) { return std::isfinite(x); }

long long gcd(long long a, long long b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b != 0) {
        const long long t = a % b;
        a = b;
        b = t;
    }
    return a;
}
long long factorial(long long n) {
    long long r = 1;
    for (long long i = 2; i <= n; ++i) r *= i;
    return r;
}

} // namespace cheatah::math
