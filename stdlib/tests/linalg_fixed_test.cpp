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

// ---- The type is not secretly limited to the graphics sizes -----------------------------------

TEST(LinalgFixed, WorksBeyondTheAliasedSizes) {
    // The aliases stop at 4 because that is where graphics stops; the TYPE does not. This also
    // exercises `dot`'s general recursive pairwise sum, which the 2/3/4 cases short-circuit past.
    la::Vec<double, 8> a;
    la::Vec<double, 8> b;
    for (std::size_t i = 0; i < 8; ++i) {
        a[i] = static_cast<double>(i + 1);  // 1..8
        b[i] = 1.0;
    }
    EXPECT_DOUBLE_EQ(la::dot(a, b), 36.0);       // 1+2+...+8
    EXPECT_DOUBLE_EQ(la::squared_norm(b), 8.0);  // eight ones
    EXPECT_DOUBLE_EQ(la::norm(b), std::sqrt(8.0));
    EXPECT_DOUBLE_EQ(la::norm(la::normalize(a)), 1.0);

    // An odd length exercises the uneven split of the recursion (5 = 2 + 3).
    la::Vec<float, 5> odd{1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
    EXPECT_FLOAT_EQ(la::dot(odd, odd), 55.0F);  // 1+4+9+16+25

    // And a bigger matrix still multiplies, transposes and transforms.
    const la::Mat<double, 5, 5> identity5 = la::Mat<double, 5, 5>::identity();
    EXPECT_DOUBLE_EQ(la::trace(identity5), 5.0);
    EXPECT_TRUE(la::transpose(identity5) == identity5);
    const la::Vec<double, 5> five{1.0, 2.0, 3.0, 4.0, 5.0};
    const la::Vec<double, 5> through = identity5 * five;
    EXPECT_TRUE(through == five);
    EXPECT_TRUE(la::matmul(identity5, identity5) == identity5);
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

// ---- The GLSL/GLM surface: geometry ------------------------------------------------------------

TEST(LinalgFixed, Geometry) {
    const la::vec3f a{1.0F, 2.0F, 3.0F};
    const la::vec3f b{4.0F, 6.0F, 8.0F};
    EXPECT_FLOAT_EQ(la::distance(a, b), std::sqrt(9.0F + 16.0F + 25.0F));
    EXPECT_FLOAT_EQ(la::distance_squared(a, b), 50.0F);
    EXPECT_DOUBLE_EQ(la::distance(la::vec2d{0.0, 0.0}, la::vec2d{3.0, 4.0}), 5.0);

    // reflect off the floor (normal +y): the y-component flips, x and z survive.
    const la::vec3f down{1.0F, -1.0F, 0.0F};
    const la::vec3f up{0.0F, 1.0F, 0.0F};
    EXPECT_TRUE(la::reflect(down, up) == (la::vec3f{1.0F, 1.0F, 0.0F}));
    // A vector reflected twice about the same normal returns to itself (dot with unit normal).
    EXPECT_TRUE(la::reflect(la::reflect(down, up), up) == down);

    // refract with equal indices (eta = 1) does not bend, so a unit vector stays unit.
    const la::vec3f incident = la::normalize(la::vec3f{1.0F, -1.0F, 0.0F});
    EXPECT_FLOAT_EQ(la::norm(la::refract(incident, up, 1.0F)), 1.0F);
    // Total internal reflection returns the zero vector.
    const la::vec2d grazing = la::normalize(la::vec2d{1.0, -0.01});
    EXPECT_TRUE(la::refract(grazing, la::vec2d{0.0, 1.0}, 5.0) == la::vec2d{});

    // faceforward keeps a normal on the incident's side. dot(nref, I) < 0 -> return n unchanged.
    EXPECT_TRUE(la::faceforward(up, down, up) == up);
    const la::vec3f away{0.0F, 1.0F, 0.0F};
    EXPECT_TRUE(la::faceforward(up, la::vec3f{0.0F, 1.0F, 0.0F}, away) == (la::vec3f{0.0F, -1.0F, 0.0F}));
}

// ---- The GLSL/GLM surface: component-wise common builtins ---------------------------------------

TEST(LinalgFixed, CommonUnary) {
    EXPECT_TRUE(la::abs(la::vec4f{-1.0F, 2.0F, -3.0F, 0.0F}) == (la::vec4f{1.0F, 2.0F, 3.0F, 0.0F}));
    EXPECT_TRUE(la::sign(la::vec3f{-2.0F, 0.0F, 5.0F}) == (la::vec3f{-1.0F, 0.0F, 1.0F}));
    // Works on a matrix too — it is elementwise over the whole array.
    EXPECT_TRUE(la::abs(la::mat2d{-1.0, 2.0, -3.0, 4.0}) == (la::mat2d{1.0, 2.0, 3.0, 4.0}));
    EXPECT_TRUE(la::sign(la::mat2f{-4.0F, 0.0F, 8.0F, -1.0F}) == (la::mat2f{-1.0F, 0.0F, 1.0F, -1.0F}));
    EXPECT_TRUE(la::abs(la::vec2d{-1.5, -2.5}) == (la::vec2d{1.5, 2.5}));
}

TEST(LinalgFixed, MinMaxClamp) {
    const la::vec3f a{1.0F, 5.0F, 3.0F};
    const la::vec3f b{4.0F, 2.0F, 6.0F};
    EXPECT_TRUE(la::min(a, b) == (la::vec3f{1.0F, 2.0F, 3.0F}));
    EXPECT_TRUE(la::max(a, b) == (la::vec3f{4.0F, 5.0F, 6.0F}));
    EXPECT_TRUE(la::min(la::vec3f{1.0F, 5.0F, 9.0F}, 4.0F) == (la::vec3f{1.0F, 4.0F, 4.0F}));
    EXPECT_TRUE(la::max(la::vec3f{1.0F, 5.0F, 9.0F}, 4.0F) == (la::vec3f{4.0F, 5.0F, 9.0F}));

    // scalar-bound clamp — pinning a colour to [0, 1]
    EXPECT_TRUE(la::clamp(la::vec4f{-1.0F, 0.5F, 2.0F, 0.0F}, 0.0F, 1.0F) ==
                (la::vec4f{0.0F, 0.5F, 1.0F, 0.0F}));
    // per-element bounds
    EXPECT_TRUE(la::clamp(la::vec3d{5.0, -5.0, 0.5}, la::vec3d{0.0, 0.0, 0.0},
                          la::vec3d{1.0, 1.0, 1.0}) == (la::vec3d{1.0, 0.0, 0.5}));
    // matrices too (double, to exercise that instantiation)
    EXPECT_TRUE(la::min(la::mat2d{1.0, 4.0, 3.0, 2.0}, la::mat2d{2.0, 2.0, 2.0, 2.0}) ==
                (la::mat2d{1.0, 2.0, 2.0, 2.0}));
    EXPECT_TRUE(la::max(la::mat2d::filled(1.0), 3.0) == la::mat2d::filled(3.0));
}

TEST(LinalgFixed, MixStep) {
    // mix with a scalar factor is a lerp
    EXPECT_TRUE(la::mix(la::vec3f{0.0F, 0.0F, 0.0F}, la::vec3f{2.0F, 4.0F, 6.0F}, 0.5F) ==
                (la::vec3f{1.0F, 2.0F, 3.0F}));
    EXPECT_TRUE(la::mix(la::vec2d{1.0, 1.0}, la::vec2d{3.0, 5.0}, 0.0) == (la::vec2d{1.0, 1.0}));
    EXPECT_TRUE(la::mix(la::vec2d{1.0, 1.0}, la::vec2d{3.0, 5.0}, 1.0) == (la::vec2d{3.0, 5.0}));
    // per-element factor
    EXPECT_TRUE(la::mix(la::vec3f{0.0F, 0.0F, 0.0F}, la::vec3f{10.0F, 10.0F, 10.0F},
                        la::vec3f{0.0F, 0.5F, 1.0F}) == (la::vec3f{0.0F, 5.0F, 10.0F}));

    // step: below the edge is 0, at or above is 1
    EXPECT_TRUE(la::step(2.0F, la::vec3f{1.0F, 2.0F, 3.0F}) == (la::vec3f{0.0F, 1.0F, 1.0F}));
    EXPECT_TRUE(la::step(0.0, la::vec2d{-1.0, 1.0}) == (la::vec2d{0.0, 1.0}));

    // smoothstep: clamped at the edges, 0.5 at the midpoint, Hermite in between
    const la::vec4f s = la::smoothstep(0.0F, 1.0F, la::vec4f{-1.0F, 0.0F, 0.5F, 2.0F});
    EXPECT_FLOAT_EQ(s[0], 0.0F);
    EXPECT_FLOAT_EQ(s[1], 0.0F);
    EXPECT_FLOAT_EQ(s[2], 0.5F);
    EXPECT_FLOAT_EQ(s[3], 1.0F);
    // monotone and within [0,1]
    const la::vec2d q = la::smoothstep(0.0, 10.0, la::vec2d{2.5, 7.5});
    EXPECT_GT(q[1], q[0]);
    EXPECT_GE(q[0], 0.0);
    EXPECT_LE(q[1], 1.0);
}

// ---- The GLSL/GLM surface: matrix builtins -----------------------------------------------------

TEST(LinalgFixed, MatrixExtras) {
    // Hadamard product multiplies corresponding entries (NOT the matrix product).
    const la::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};
    const la::mat2f k{2.0F, 0.0F, 0.0F, 2.0F};
    EXPECT_TRUE(la::matrix_comp_mult(m, k) == (la::mat2f{2.0F, 0.0F, 0.0F, 8.0F}));

    // outer product: (i, j) = c[i] * r[j]
    const la::Mat<float, 2, 3> op = la::outer_product(la::vec2f{1.0F, 2.0F}, la::vec3f{3.0F, 4.0F, 5.0F});
    EXPECT_FLOAT_EQ(op(0, 0), 3.0F);
    EXPECT_FLOAT_EQ(op(0, 2), 5.0F);
    EXPECT_FLOAT_EQ(op(1, 1), 8.0F);
    // outer_product(c, r) == c as a column times r as a row, so its rank is one: rows are multiples.
    EXPECT_FLOAT_EQ(op(1, 0) / op(0, 0), 2.0F);

    // inverse_transpose carries normals: for an orthonormal (rotation) matrix it equals the matrix
    // itself, since transpose(inverse(R)) = transpose(transpose(R)) = R.
    const double c = std::cos(0.7);
    const double s = std::sin(0.7);
    const la::mat3d rot{c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0};
    const la::mat3d it = la::inverse_transpose(rot);
    for (std::size_t i = 0; i < 9; ++i) { EXPECT_NEAR(it.data()[i], rot.data()[i], 1e-12); }
    // For a non-uniform scale S = diag(2, 4), inverse_transpose is diag(1/2, 1/4).
    const la::mat2d scale{2.0, 0.0, 0.0, 4.0};
    const la::mat2d nrm = la::inverse_transpose(scale);
    EXPECT_NEAR(nrm(0, 0), 0.5, 1e-12);
    EXPECT_NEAR(nrm(1, 1), 0.25, 1e-12);
    // Singular still throws (via inverse).
    EXPECT_THROW((void)la::inverse_transpose(la::mat3d{}), std::domain_error);
}

// ---- Enum subscripting: a scoped enum names an axis, and only when indexing --------------------

namespace {
/// A caller's scoped enum. It stays strongly typed everywhere except at a subscript, which is the
/// whole point of ndarray::Subscript.
enum class Axis : std::size_t { X = 0, Y = 1, Z = 2 };
enum class Basis : std::size_t { Right = 0, Up = 1, Forward = 2 };
}  // namespace

TEST(LinalgFixed, EnumIndexingOnVectorsAndMatrices) {
    la::vec3f v{7.0F, 8.0F, 9.0F};
    // read a component by name
    EXPECT_FLOAT_EQ(v[Axis::X], 7.0F);
    EXPECT_FLOAT_EQ(v[Axis::Z], 9.0F);
    // write by name (non-const overload)
    v[Axis::Y] = 42.0F;
    EXPECT_FLOAT_EQ(v[1], 42.0F);
    // const overload
    const la::vec3f& cv = v;
    EXPECT_FLOAT_EQ(cv[Axis::Y], 42.0F);
    // a plain integer still resolves the ordinary overload
    EXPECT_FLOAT_EQ(v[std::size_t{0}], 7.0F);

    la::mat3f m = la::mat3f::identity();
    // both indices named
    EXPECT_FLOAT_EQ(m(Axis::Y, Axis::Y), 1.0F);
    EXPECT_FLOAT_EQ(m(Axis::X, Axis::Y), 0.0F);
    // mixed: one enum, one integer
    EXPECT_FLOAT_EQ(m(Axis::Z, std::size_t{2}), 1.0F);
    EXPECT_FLOAT_EQ(m(std::size_t{0}, Axis::X), 1.0F);
    // write by name
    m(Axis::X, Axis::Z) = 5.0F;
    EXPECT_FLOAT_EQ(m(0, 2), 5.0F);
    // const overloads (both-enum and mixed)
    const la::mat3f& cm = m;
    EXPECT_FLOAT_EQ(cm(Axis::X, Axis::Z), 5.0F);
    EXPECT_FLOAT_EQ(cm(Axis::Y, std::size_t{1}), 1.0F);
    EXPECT_FLOAT_EQ(cm(std::size_t{2}, Axis::Z), 1.0F);
}

TEST(LinalgFixed, NamedRowsAndColumns) {
    const la::mat3f id = la::mat3f::identity();
    // a basis vector by name — the axis an enum was made for
    EXPECT_TRUE(la::column(id, Basis::Forward) == (la::vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(la::column(id, Basis::Right) == (la::vec3f{1.0F, 0.0F, 0.0F}));
    // a plain integer index still works
    EXPECT_TRUE(la::column(id, 1) == (la::vec3f{0.0F, 1.0F, 0.0F}));
    EXPECT_TRUE(la::row(id, 2) == (la::vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(la::row(id, Axis::X) == (la::vec3f{1.0F, 0.0F, 0.0F}));

    // On a real (column-major) transform, column j is the image of basis vector j.
    const la::mat3f t{2.0F, 0.0F, 1.0F, 0.0F, 3.0F, 2.0F, 0.0F, 0.0F, 1.0F};
    EXPECT_TRUE(la::column(t, Axis::X) == (la::vec3f{2.0F, 0.0F, 0.0F}));  // where x-hat lands
    EXPECT_TRUE(la::row(t, Axis::X) == (la::vec3f{2.0F, 0.0F, 1.0F}));
    // A non-square matrix: row length is the column count and vice-versa.
    const la::Mat<double, 2, 3> wide{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    EXPECT_TRUE(la::row(wide, 1) == (la::vec3d{4.0, 5.0, 6.0}));
    EXPECT_TRUE(la::column(wide, 2) == (la::vec2d{3.0, 6.0}));
}

// ---- from_indices: the one-pass elementwise builder the component-wise ops ride on --------------

TEST(LinalgFixed, FromIndices) {
    // A vector built from its flat index.
    const la::vec4f v = la::vec4f::from_indices([](std::size_t i) { return static_cast<float>(i * i); });
    EXPECT_TRUE(v == (la::vec4f{0.0F, 1.0F, 4.0F, 9.0F}));

    // For a matrix the index runs over STORAGE order (column-major), so building the identity by
    // "1 on the diagonal" means indices divisible by rows+1 — the same fact identity() uses.
    const la::mat3f id =
        la::mat3f::from_indices([](std::size_t i) { return i % 4 == 0 ? 1.0F : 0.0F; });
    EXPECT_TRUE(id == la::mat3f::identity());

    // It is usable at compile time.
    constexpr la::vec3d ramp = la::vec3d::from_indices([](std::size_t i) { return static_cast<double>(i); });
    static_assert(ramp[2] == 2.0);

    // Column-major storage is observable: element k of the buffer is what f(k) returned.
    const la::mat2f m = la::mat2f::from_indices([](std::size_t i) { return static_cast<float>(i); });
    EXPECT_EQ(m.data()[0], 0.0F);
    EXPECT_EQ(m.data()[3], 3.0F);
    EXPECT_EQ(m(0, 0), 0.0F);
    EXPECT_EQ(m(0, 1), 2.0F);  // flat index 2 is (row 0, col 1) in column-major
}
