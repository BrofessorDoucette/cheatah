#include "simd.hpp"

namespace cheatah::purrscript::linalg {

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

    if (features.empty()) {
        features = "scalar";
    }
    return features;
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

} // namespace cheatah::purrscript::linalg
