#include "random.hpp"

#include <random>

namespace cheatah::purrscript::random {

namespace {
std::mt19937_64& engine() {
    static std::mt19937_64 e{std::random_device{}()};
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

} // namespace cheatah::purrscript::random
