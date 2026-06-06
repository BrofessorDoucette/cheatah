#include "vector.hpp"

#include <stdexcept>

namespace cheatah::linalg {

double dot(std::span<const double> a, std::span<const double> b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("cheatah::linalg::dot: operands differ in length");
    }
    // Straight reference loop for now. With -march=native (Release) the compiler
    // auto-vectorizes this; hand-written intrinsics / GPU kernels land later
    // behind the same signature.
    double acc = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        acc += a[i] * b[i];
    }
    return acc;
}

} // namespace cheatah::linalg
