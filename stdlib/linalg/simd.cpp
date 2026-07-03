// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "simd.hpp"

#include <utility>

namespace cheatah::linalg {

namespace detail {
// By-value is intentional: the sole caller std::move()s into it, so the argument is
// move-constructed (no copy) and the result moves out — passing by value is the efficient
// choice here, not a redundant copy.
// cppcheck-suppress passedByValue
std::string scalar_if_empty(std::string features) {
    if (features.empty()) return "scalar";
    return features;
}
}  // namespace detail

std::string simd_features() {
    std::string features;
    const auto add = [&](const char* name) {
        if (!features.empty()) {
            features += ';';
        }
        features += name;
    };

#if defined(__AVX512F__)
    add("AVX512F");
#endif
#if defined(__AVX2__)
    add("AVX2");
#endif
#if defined(__AVX__)
    add("AVX");
#endif
#if defined(__FMA__)
    add("FMA");
#endif
#if defined(__SSE4_2__)
    add("SSE4.2");
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    add("NEON");
#endif

    return detail::scalar_if_empty(std::move(features));
}

int simd_lane_doubles() noexcept {
#if defined(__AVX512F__)
    return 8;
#elif defined(__AVX__)
    return 4;
#elif defined(__SSE2__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
    return 2;
#else
    return 1;
#endif
}

} // namespace cheatah::linalg
