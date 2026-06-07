#include "linalg.hpp"
#include "ndarray.hpp"

#include <gtest/gtest.h>

// Smoke tests for the linear-algebra core, under GoogleTest. Linked against the
// STATIC library, so a passing run proves libcheatah_linalg.a is usable end to
// end. Focused behaviours (decompositions, solves, …) live in the routines
// suite; this file just confirms the library links and its entry points work.

namespace nd = cheatah::ndarray;
namespace la = cheatah::linalg;

TEST(LinalgSmoke, SimdFeaturesReported) {
    EXPECT_FALSE(la::simd_features().empty());
    EXPECT_GE(la::simd_lane_doubles(), 1);
}

TEST(LinalgSmoke, SimdScalarFallback) {
    // The no-SIMD fallback (unreachable on this SIMD build) tested directly.
    EXPECT_EQ(la::detail::scalar_if_empty(""), "scalar");
    EXPECT_EQ(la::detail::scalar_if_empty("AVX2;FMA"), "AVX2;FMA");
}

TEST(LinalgSmoke, DotMatchesHandComputed) {
    EXPECT_DOUBLE_EQ(la::dot(nd::array({1, 2, 3, 4}), nd::array({5, 6, 7, 8})), 70.0);
}

TEST(LinalgSmoke, MatmulMatchesHandComputed) {
    const nd::NDArray a = nd::reshape(nd::array({1, 2, 3, 4, 5, 6}), {2, 3});
    const nd::NDArray b = nd::reshape(nd::array({7, 8, 9, 10, 11, 12}), {3, 2});
    const nd::NDArray c = la::matmul(a, b);  // [[58, 64], [139, 154]]
    EXPECT_DOUBLE_EQ(nd::get(c, {0, 0}), 58.0);
    EXPECT_DOUBLE_EQ(nd::get(c, {1, 1}), 154.0);
}
