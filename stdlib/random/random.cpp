// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "random.hpp"

#include <random>

namespace cheatah::random {

namespace {
std::mt19937_64& engine() {
    // One engine PER THREAD: the `thread` module makes concurrent random() calls reachable, and a
    // single shared mt19937_64 would be a data race (torn state, lost advances). Each thread
    // self-seeds from std::random_device on first use; seed(s) seeds the CALLING thread only.
    thread_local std::mt19937_64 e{std::random_device{}()};
    return e;
}
} // namespace

void seed(unsigned long long s) { engine().seed(s); }

double random() { return std::uniform_real_distribution<double>(0.0, 1.0)(engine()); }
double uniform(double a, double b) { return std::uniform_real_distribution<double>(a, b)(engine()); }
long long randint(long long a, long long b) {
    return std::uniform_int_distribution<long long>(a, b)(engine());
}
double gauss(double mu, double sigma) { return std::normal_distribution<double>(mu, sigma)(engine()); }

} // namespace cheatah::random
