#include "linalg.hpp"

#include <array>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

// Smoke tests for the linear-algebra core, under GoogleTest. Linked against the
// STATIC library, so a passing run proves libcheatah_linalg.a is usable end to
// end.
//
// SCAFFOLDING STAGE: these assert the implemented reference surface. As real
// kernels (SIMD/GPU matrix ops, decompositions) land, add focused
// TEST/TEST_F/TEST_P cases — one behavior per test — following red-green-refactor.
TEST(LinalgSmoke, VersionIsNonEmpty) {
    EXPECT_FALSE(std::string(cheatah::linalg::version()).empty())
        << "cheatah::linalg::version() returned an empty string";
}

TEST(LinalgSmoke, SimdFeaturesReported) {
    EXPECT_FALSE(cheatah::linalg::simd_features().empty());
    EXPECT_GE(cheatah::linalg::simd_lane_doubles(), 1);
}

TEST(LinalgSmoke, DotMatchesHandComputed) {
    const std::array<double, 4> a{1.0, 2.0, 3.0, 4.0};
    const std::array<double, 4> b{5.0, 6.0, 7.0, 8.0};
    EXPECT_DOUBLE_EQ(cheatah::linalg::dot(a, b), 70.0);  // 5+12+21+32
}

TEST(LinalgSmoke, DotOfEmptyIsZero) {
    // Disambiguate the span overload from the ndarray routine `dot`.
    EXPECT_DOUBLE_EQ(
        cheatah::linalg::dot(std::span<const double>{}, std::span<const double>{}), 0.0);
}

TEST(LinalgSmoke, DotRejectsSizeMismatch) {
    const std::array<double, 3> a{1.0, 2.0, 3.0};
    const std::array<double, 2> b{1.0, 2.0};
    EXPECT_THROW(cheatah::linalg::dot(a, b), std::invalid_argument);
}
