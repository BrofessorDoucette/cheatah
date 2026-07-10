// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// cheatah::linalg::Fixed — the fixed-extent arrays. Two things are being proved here:
//
//   1. The MATH is right, checked against identities rather than transcribed constants
//      (inverse(m)·m == I, cross(a,b)·a == 0, transpose(transpose(m)) == m). An identity cannot be
//      satisfied by a typo the way a hand-copied expected value can.
//   2. The COST is right. `Fixed` exists only because NDArray allocates; if it ever stopped being a
//      trivially copyable value of exactly its elements' size, it would have lost its reason to
//      exist. That is a static_assert, not a comment.
//
// Both float and double are instantiated: `Fixed` is a template, so an untested instantiation is
// untested code.

#include "fixed.hpp"

#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "ndarray.hpp"
#include "routines.hpp"

namespace la = cheatah::linalg;
namespace nd = cheatah::ndarray;

namespace {

/// Elementwise closeness, so a float test and a double test share one predicate.
template <class M>
::testing::AssertionResult Close(const M& a, const M& b, double eps = 1e-5) {
    for (std::size_t i = 0; i < M::size; ++i) {
        const double lhs = static_cast<double>(a.data()[i]);
        const double rhs = static_cast<double>(b.data()[i]);
        if (std::fabs(lhs - rhs) > eps) {
            return ::testing::AssertionFailure()
                   << "element " << i << ": " << lhs << " vs " << rhs;
        }
    }
    return ::testing::AssertionSuccess();
}

}  // namespace

// ---- The reason this type exists: no allocation, no padding, no vtable. -------------------------

TEST(LinalgFixed, IsAPlainValueOfExactlyItsElements) {
    static_assert(sizeof(la::vec2f) == 2 * sizeof(float));
    static_assert(sizeof(la::vec3f) == 3 * sizeof(float));
    static_assert(sizeof(la::vec4f) == 4 * sizeof(float));
    static_assert(sizeof(la::mat3f) == 9 * sizeof(float));
    static_assert(sizeof(la::mat4f) == 64);  // exactly a 4x4 push constant
    static_assert(sizeof(la::mat4d) == 128);
    static_assert(std::is_trivially_copyable_v<la::mat4f>);
    static_assert(std::is_standard_layout_v<la::mat4f>);

    // The shape is in the type, so it costs nothing at runtime.
    static_assert(la::vec3f::rank == 1);
    static_assert(la::vec3f::size == 3);
    static_assert(la::vec3f::rows == 3);
    static_assert(la::vec3f::cols == 1);
    static_assert(la::mat4f::rank == 2);
    static_assert(la::mat4f::size == 16);
    static_assert(la::mat4f::rows == 4);
    static_assert(la::mat4f::cols == 4);
    static_assert(la::mat4f::shape[0] == 4 && la::mat4f::shape[1] == 4);
    static_assert(std::is_same_v<la::vec3f::value_type, float>);
    static_assert(la::extent_product<2, 3, 4> == 24);

    // A non-square, non-vector shape is just as ordinary.
    static_assert(la::Mat<float, 2, 3>::rows == 2);
    static_assert(la::Mat<float, 2, 3>::cols == 3);
    SUCCEED();
}

// ---- Construction, indexing, data() -------------------------------------------------------------

TEST(LinalgFixed, DefaultIsZero) {
    const la::vec3f v;
    EXPECT_EQ(v[0], 0.0F);
    EXPECT_EQ(v[1], 0.0F);
    EXPECT_EQ(v[2], 0.0F);
    const la::mat2d m;
    EXPECT_EQ(m(0, 0), 0.0);
    EXPECT_EQ(m(1, 1), 0.0);
}

TEST(LinalgFixed, VectorIndexing) {
    la::vec3f v{1.0F, 2.0F, 3.0F};
    EXPECT_EQ(v[0], 1.0F);
    EXPECT_EQ(v[2], 3.0F);
    v[1] = 9.0F;  // non-const
    EXPECT_EQ(v[1], 9.0F);
    const la::vec3f& cv = v;
    EXPECT_EQ(cv[1], 9.0F);  // const

    // Arguments convert: a cheatah program computes in double and stores a float vector.
    const la::vec3f from_doubles{1.0, 2.0, 3.0};
    EXPECT_EQ(from_doubles[2], 3.0F);
}

TEST(LinalgFixed, MatrixIndexing) {
    la::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};  // row-major
    EXPECT_EQ(m(0, 0), 1.0F);
    EXPECT_EQ(m(0, 1), 2.0F);
    EXPECT_EQ(m(1, 0), 3.0F);
    EXPECT_EQ(m(1, 1), 4.0F);
    m(1, 0) = 7.0F;  // non-const
    EXPECT_EQ(m(1, 0), 7.0F);
    const la::mat2f& cm = m;
    EXPECT_EQ(cm(1, 0), 7.0F);  // const
}

TEST(LinalgFixed, Data) {
    // The constructor takes elements in READING order...
    la::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};
    EXPECT_EQ(m(0, 0), 1.0F);
    EXPECT_EQ(m(0, 1), 2.0F);
    EXPECT_EQ(m(1, 0), 3.0F);
    EXPECT_EQ(m(1, 1), 4.0F);

    // ...but a matrix is STORED column by column, which is what a GPU uniform, a push constant and
    // GLM all expect. So the buffer reads 1, 3, 2, 4 — column 0, then column 1.
    EXPECT_EQ(m.data()[0], 1.0F);
    EXPECT_EQ(m.data()[1], 3.0F);
    EXPECT_EQ(m.data()[2], 2.0F);
    EXPECT_EQ(m.data()[3], 4.0F);

    m.data()[1] = 5.0F;  // non-const: element (1, 0), since that is where the buffer says it lives
    EXPECT_EQ(m(1, 0), 5.0F);
    const la::mat2f& cm = m;
    EXPECT_EQ(cm.data()[3], 4.0F);  // const

    // A vector has one order and no ambiguity.
    const la::vec3f v{7.0F, 8.0F, 9.0F};
    EXPECT_EQ(v.data()[1], 8.0F);
}

TEST(LinalgFixed, Identity) {
    constexpr la::mat3f compile_time = la::mat3f::identity();  // usable at compile time
    static_assert(compile_time(0, 0) == 1.0F);
    static_assert(compile_time(0, 1) == 0.0F);

    // ...and at run time. A constexpr function nobody executes is a function nobody proved runs.
    la::mat3f runtime = la::mat3f::identity();
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_EQ(runtime(r, c), r == c ? 1.0F : 0.0F);
        }
    }
    runtime(2, 2) = 5.0F;  // the non-const matrix accessor on this instantiation
    EXPECT_EQ(runtime(2, 2), 5.0F);
    EXPECT_EQ(la::mat4d::identity()(3, 3), 1.0);
    EXPECT_EQ(la::mat2d::identity()(0, 0), 1.0);
}

TEST(LinalgFixed, Filled) {
    const la::mat2f threes = la::mat2f::filled(3.0F);
    EXPECT_EQ(threes(0, 0), 3.0F);
    EXPECT_EQ(threes(1, 1), 3.0F);
    EXPECT_EQ(la::vec4d::filled(-1.0)[3], -1.0);
}

TEST(LinalgFixed, Equality) {
    const la::vec3f a{1.0F, 2.0F, 3.0F};
    const la::vec3f b{1.0F, 2.0F, 3.0F};
    const la::vec3f c{1.0F, 2.0F, 4.0F};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a != b);
}

// ---- Arithmetic ---------------------------------------------------------------------------------

TEST(LinalgFixed, Arithmetic) {
    const la::vec3f a{1.0F, 2.0F, 3.0F};
    const la::vec3f b{4.0F, 5.0F, 6.0F};

    EXPECT_TRUE(a + b == (la::vec3f{5.0F, 7.0F, 9.0F}));
    EXPECT_TRUE(b - a == (la::vec3f{3.0F, 3.0F, 3.0F}));
    EXPECT_TRUE(-a == (la::vec3f{-1.0F, -2.0F, -3.0F}));
    EXPECT_TRUE(a * 2.0F == (2.0F * a));           // scalar multiply, both orders
    EXPECT_TRUE((a * 2.0F) / 2.0F == a);           // and its inverse
    EXPECT_TRUE(a + (-a) == la::vec3f{});          // additive inverse

    la::vec3f m = a;
    m += b;
    EXPECT_TRUE(m == a + b);
    m -= b;
    EXPECT_TRUE(m == a);
    m *= 3.0F;
    EXPECT_TRUE(m == a * 3.0F);
    m /= 3.0F;
    EXPECT_TRUE(m == a);

    // Doubles behave the same.
    la::mat2d dm{1.0, 2.0, 3.0, 4.0};
    dm += la::mat2d::filled(1.0);
    EXPECT_EQ(dm(0, 0), 2.0);
    dm -= la::mat2d::filled(1.0);
    EXPECT_EQ(dm(0, 0), 1.0);
    dm *= 2.0;
    EXPECT_EQ(dm(1, 1), 8.0);
    dm /= 2.0;
    EXPECT_EQ(dm(1, 1), 4.0);
    EXPECT_EQ((-dm)(1, 1), -4.0);
    EXPECT_EQ((dm + dm)(0, 0), 2.0);
    EXPECT_EQ((dm - dm)(0, 0), 0.0);
    EXPECT_EQ((2.0 * dm)(0, 0), 2.0);
    EXPECT_EQ((dm / 2.0)(1, 1), 2.0);

    // Every alias is a real instantiation; exercise the smaller ones so none is merely declared.
    la::vec2d small{2.0, 4.0};
    small /= 2.0;
    EXPECT_EQ(small[1], 2.0);
    EXPECT_EQ((small / 2.0)[0], 0.5);
    EXPECT_EQ((la::vec2f{1.0F, 2.0F} + la::vec2f{1.0F, 1.0F})[1], 3.0F);
    EXPECT_EQ((la::vec4d::filled(2.0) * 0.5)[0], 1.0);
}

// ---- Vector products ----------------------------------------------------------------------------

TEST(LinalgFixed, DotAndCross) {
    constexpr la::vec3f a{1.0F, 2.0F, 3.0F};
    constexpr la::vec3f b{4.0F, 5.0F, 6.0F};
    static_assert(la::dot(a, b) == 32.0F);  // compile-time
    EXPECT_EQ(la::dot(a, b), 32.0F);

    constexpr la::vec3f c = la::cross(a, b);
    static_assert(c[0] == -3.0F && c[1] == 6.0F && c[2] == -3.0F);

    // The identity that defines a cross product: perpendicular to both operands.
    EXPECT_EQ(la::dot(c, a), 0.0F);
    EXPECT_EQ(la::dot(c, b), 0.0F);
    // ...and anticommutative.
    EXPECT_TRUE(la::cross(b, a) == -c);

    // Right-handed: x cross y == z.
    const la::vec3d x{1.0, 0.0, 0.0};
    const la::vec3d y{0.0, 1.0, 0.0};
    EXPECT_TRUE(la::cross(x, y) == (la::vec3d{0.0, 0.0, 1.0}));
    EXPECT_EQ(la::dot(la::vec4f{1.0F, 1.0F, 1.0F, 1.0F}, la::vec4f{1.0F, 2.0F, 3.0F, 4.0F}), 10.0F);
}

TEST(LinalgFixed, NormAndNormalize) {
    const la::vec3f v{3.0F, 4.0F, 0.0F};
    EXPECT_EQ(la::squared_norm(v), 25.0F);
    EXPECT_FLOAT_EQ(la::norm(v), 5.0F);

    const la::vec3f unit = la::normalize(v);
    EXPECT_FLOAT_EQ(la::norm(unit), 1.0F);
    EXPECT_FLOAT_EQ(unit[0], 0.6F);
    EXPECT_FLOAT_EQ(unit[1], 0.8F);

    EXPECT_DOUBLE_EQ(la::norm(la::vec2d{0.0, 2.0}), 2.0);

    // Every instantiation must normalize, not merely refuse to: `normalize` is one reciprocal and a
    // multiply, and a size that only ever saw the throw path is a size nobody proved works.
    EXPECT_DOUBLE_EQ(la::norm(la::normalize(la::vec2d{3.0, 4.0})), 1.0);
    EXPECT_FLOAT_EQ(la::norm(la::normalize(la::vec2f{0.0F, 2.0F})), 1.0F);
    EXPECT_DOUBLE_EQ(la::norm(la::normalize(la::vec4d{1.0, 1.0, 1.0, 1.0})), 1.0);
    EXPECT_FLOAT_EQ(la::normalize(la::vec2f{0.0F, 2.0F})[1], 1.0F);

    // The zero vector has no direction; saying so beats returning NaNs.
    EXPECT_THROW((void)la::normalize(la::vec3f{}), std::domain_error);
    EXPECT_THROW((void)la::normalize(la::vec2d{}), std::domain_error);
    EXPECT_THROW((void)la::normalize(la::vec4d{}), std::domain_error);
}

// ---- Matrix products, transpose, trace ----------------------------------------------------------

TEST(LinalgFixed, Matmul) {
    const la::mat2f a{1.0F, 2.0F, 3.0F, 4.0F};
    const la::mat2f b{5.0F, 6.0F, 7.0F, 8.0F};
    const la::mat2f ab = la::matmul(a, b);
    EXPECT_EQ(ab(0, 0), 19.0F);
    EXPECT_EQ(ab(0, 1), 22.0F);
    EXPECT_EQ(ab(1, 0), 43.0F);
    EXPECT_EQ(ab(1, 1), 50.0F);
    EXPECT_TRUE(a * b == ab);  // the operator spelling

    // Identity is the multiplicative identity, and matmul is associative.
    EXPECT_TRUE(a * la::mat2f::identity() == a);
    EXPECT_TRUE(la::mat2f::identity() * a == a);
    const la::mat2f c{2.0F, 0.0F, 1.0F, 3.0F};
    EXPECT_TRUE(Close((a * b) * c, a * (b * c)));

    // Non-square shapes chain: (2x3)(3x2) -> 2x2.
    const la::Mat<double, 2, 3> wide{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    const la::Mat<double, 3, 2> tall{7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
    const la::Mat<double, 2, 2> product = wide * tall;
    EXPECT_DOUBLE_EQ(product(0, 0), 58.0);
    EXPECT_DOUBLE_EQ(product(1, 1), 154.0);

    // Matrix times vector.
    const la::vec2f v = a * la::vec2f{1.0F, 1.0F};
    EXPECT_EQ(v[0], 3.0F);
    EXPECT_EQ(v[1], 7.0F);
    const la::vec3d w = la::mat3d::identity() * la::vec3d{1.0, 2.0, 3.0};
    EXPECT_TRUE(w == (la::vec3d{1.0, 2.0, 3.0}));
    const la::Vec<double, 2> rect = wide * la::vec3d{1.0, 1.0, 1.0};
    EXPECT_DOUBLE_EQ(rect[0], 6.0);
    EXPECT_DOUBLE_EQ(rect[1], 15.0);
}

TEST(LinalgFixed, TransposeAndTrace) {
    const la::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};
    const la::mat2f t = la::transpose(m);
    EXPECT_EQ(t(0, 1), 3.0F);
    EXPECT_EQ(t(1, 0), 2.0F);
    EXPECT_TRUE(la::transpose(t) == m);  // an involution
    EXPECT_EQ(la::trace(m), 5.0F);
    EXPECT_EQ(la::trace(la::mat4d::identity()), 4.0);

    // A non-square transpose swaps the shape.
    const la::Mat<float, 2, 3> wide{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    const la::Mat<float, 3, 2> narrow = la::transpose(wide);
    static_assert(decltype(narrow)::rows == 3 && decltype(narrow)::cols == 2);
    EXPECT_EQ(narrow(2, 0), 3.0F);
    EXPECT_EQ(narrow(0, 1), 4.0F);
}

// ---- Determinant and inverse --------------------------------------------------------------------

TEST(LinalgFixed, DeterminantAndInverse) {
    // 2x2
    const la::mat2f m2{4.0F, 7.0F, 2.0F, 6.0F};
    EXPECT_FLOAT_EQ(la::determinant(m2), 10.0F);
    EXPECT_TRUE(Close(la::inverse(m2) * m2, la::mat2f::identity()));
    EXPECT_TRUE(Close(m2 * la::inverse(m2), la::mat2f::identity()));

    // 3x3
    const la::mat3d m3{2.0, -1.0, 0.0, -1.0, 2.0, -1.0, 0.0, -1.0, 2.0};
    EXPECT_DOUBLE_EQ(la::determinant(m3), 4.0);
    EXPECT_TRUE(Close(la::inverse(m3) * m3, la::mat3d::identity(), 1e-12));

    // 4x4
    const la::mat4d m4{1.0, 2.0, 0.0, 1.0, 0.0, 1.0, 3.0, 0.0,
                       2.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0};
    EXPECT_TRUE(Close(la::inverse(m4) * m4, la::mat4d::identity(), 1e-12));
    EXPECT_TRUE(Close(m4 * la::inverse(m4), la::mat4d::identity(), 1e-12));
    EXPECT_TRUE(Close(la::inverse(la::inverse(m4)), m4, 1e-10));  // an involution

    // det(I) == 1 at every supported size, and det(AB) == det(A)det(B).
    EXPECT_FLOAT_EQ(la::determinant(la::mat2f::identity()), 1.0F);
    EXPECT_DOUBLE_EQ(la::determinant(la::mat3d::identity()), 1.0);
    EXPECT_DOUBLE_EQ(la::determinant(la::mat4d::identity()), 1.0);
    const la::mat3d other{1.0, 2.0, 3.0, 0.0, 1.0, 4.0, 5.0, 6.0, 0.0};
    EXPECT_NEAR(la::determinant(m3 * other), la::determinant(m3) * la::determinant(other), 1e-9);
    EXPECT_FLOAT_EQ(la::determinant(la::mat4f{1.0F, 2.0F, 0.0F, 1.0F, 0.0F, 1.0F, 3.0F, 0.0F,
                                              2.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 2.0F}),
                    static_cast<float>(la::determinant(m4)));

    // A singular matrix has no inverse, and says so rather than returning infinities.
    EXPECT_EQ(la::determinant(la::mat2f{}), 0.0F);
    EXPECT_THROW((void)la::inverse(la::mat2f{}), std::domain_error);
    EXPECT_THROW((void)la::inverse(la::mat3d{}), std::domain_error);
    EXPECT_THROW((void)la::inverse(la::mat4d{}), std::domain_error);

    // Rank-deficient, not merely all-zero: two identical rows.
    EXPECT_THROW((void)la::inverse(la::mat3d{1.0, 2.0, 3.0, 1.0, 2.0, 3.0, 4.0, 5.0, 7.0}),
                 std::domain_error);
}

// ---- The same answers as NDArray, which is the promise the name makes -------------------------

TEST(LinalgFixed, AgreesWithTheDynamicNDArray) {
    // `Fixed` claims to be "NDArray, only faster". The claim is only worth making if the answers
    // agree; a 3x3 inverse and determinant are where that is easiest to check.
    const std::vector<double> values{2.0, -1.0, 0.0, -1.0, 2.0, -1.0, 0.0, -1.0, 2.0};

    const la::mat3d fixed{values[0], values[1], values[2], values[3], values[4],
                          values[5], values[6], values[7], values[8]};
    const la::mat3d fixed_inv = la::inverse(fixed);

    const nd::NDArray dynamic = nd::reshape(nd::array(values), {3, 3});
    const nd::NDArray dynamic_inv = la::inv(dynamic);

    for (long long r = 0; r < 3; ++r) {
        for (long long c = 0; c < 3; ++c) {
            EXPECT_NEAR(fixed_inv(static_cast<std::size_t>(r), static_cast<std::size_t>(c)),
                        nd::get(dynamic_inv, {r, c}), 1e-12);
        }
    }
    EXPECT_NEAR(la::determinant(fixed), la::det(dynamic), 1e-12);
}
