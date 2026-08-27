// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// cheatah::fixarray::Fixed — the fixed-extent arrays. Two things are being proved here:
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

#include "fixarray.hpp"

#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "ndarray.hpp"
#include "routines.hpp"

namespace fa = cheatah::fixarray;
namespace la = cheatah::linalg;  // linalg routines (inv/det) for the NDArray cross-check below
namespace nd = cheatah::ndarray;

namespace {

/// Elementwise closeness, so a float test and a double test share one predicate.
template <class M>
::testing::AssertionResult Close(const M& a, const M& b, double eps = 1e-5) {
    for (std::size_t i = 0; i < M::size; ++i) {
        const auto lhs = static_cast<double>(a.data()[i]);
        const auto rhs = static_cast<double>(b.data()[i]);
        if (std::fabs(lhs - rhs) > eps) {
            return ::testing::AssertionFailure()
                   << "element " << i << ": " << lhs << " vs " << rhs;
        }
    }
    return ::testing::AssertionSuccess();
}

}  // namespace

// ---- The reason this type exists: no allocation, no padding, no vtable. -------------------------

TEST(Fixarray, IsAPlainValueOfExactlyItsElements) {
    static_assert(sizeof(fa::vec2f) == 2 * sizeof(float));
    static_assert(sizeof(fa::vec3f) == 3 * sizeof(float));
    static_assert(sizeof(fa::vec4f) == 4 * sizeof(float));
    static_assert(sizeof(fa::mat3f) == 9 * sizeof(float));
    static_assert(sizeof(fa::mat4f) == 64);  // exactly a 4x4 push constant
    static_assert(sizeof(fa::mat4d) == 128);
    static_assert(std::is_trivially_copyable_v<fa::mat4f>);
    static_assert(std::is_standard_layout_v<fa::mat4f>);

    // The shape is in the type, so it costs nothing at runtime.
    static_assert(fa::vec3f::rank == 1);
    static_assert(fa::vec3f::size == 3);
    static_assert(fa::vec3f::rows == 3);
    static_assert(fa::vec3f::cols == 1);
    static_assert(fa::mat4f::rank == 2);
    static_assert(fa::mat4f::size == 16);
    static_assert(fa::mat4f::rows == 4);
    static_assert(fa::mat4f::cols == 4);
    static_assert(fa::mat4f::shape[0] == 4 && fa::mat4f::shape[1] == 4);
    static_assert(std::is_same_v<fa::vec3f::value_type, float>);
    static_assert(fa::extent_product<2, 3, 4> == 24);

    // A non-square, non-vector shape is just as ordinary.
    static_assert(fa::Mat<float, 2, 3>::rows == 2);
    static_assert(fa::Mat<float, 2, 3>::cols == 3);
    SUCCEED();
}

// ---- Construction, indexing, data() -------------------------------------------------------------

TEST(Fixarray, DefaultIsZero) {
    const fa::vec3f v;
    EXPECT_EQ(v[0], 0.0F);
    EXPECT_EQ(v[1], 0.0F);
    EXPECT_EQ(v[2], 0.0F);
    const fa::mat2d m;
    EXPECT_EQ(m(0, 0), 0.0);
    EXPECT_EQ(m(1, 1), 0.0);
}

// A fixed-size array is FILLED by a slice assignment — the extent is part of the type, so the
// values are copied in and nothing is resized.
TEST(Fixarray, SliceAssignCopiesIn) {
    using V = fa::Fixed<float, 4>;
    V v{1.0F, 2.0F, 3.0F, 4.0F};
    cheatah::builtins::slice_assign(v, 1, 3, std::vector<float>{9.0F, 9.0F});
    EXPECT_FLOAT_EQ(v[0], 1.0F);           // outside the slice: untouched
    EXPECT_FLOAT_EQ(v[1], 9.0F);
    EXPECT_FLOAT_EQ(v[2], 9.0F);
    EXPECT_FLOAT_EQ(v[3], 4.0F);
    EXPECT_EQ(V::size, 4U);                // the extent is compile-time and cannot move
    // negatives count from the end, exactly as for a list
    cheatah::builtins::slice_assign(v, -2, -1, std::vector<float>{5.0F});
    EXPECT_FLOAT_EQ(v[2], 5.0F);
    // a source of the wrong length is an error, never a partial write
    EXPECT_THROW(cheatah::builtins::slice_assign(v, 0, 2, std::vector<float>{1.0F}),
                 std::runtime_error);
    EXPECT_FLOAT_EQ(v[0], 1.0F);           // and it really did not write
}

TEST(Fixarray, VectorIndexing) {
    fa::vec3f v{1.0F, 2.0F, 3.0F};
    EXPECT_EQ(v[0], 1.0F);
    EXPECT_EQ(v[2], 3.0F);
    v[1] = 9.0F;  // non-const
    EXPECT_EQ(v[1], 9.0F);
    const fa::vec3f& cv = v;
    EXPECT_EQ(cv[1], 9.0F);  // const

    // Arguments convert: a cheatah program computes in double and stores a float vector.
    const fa::vec3f from_doubles{1.0, 2.0, 3.0};
    EXPECT_EQ(from_doubles[2], 3.0F);
}

TEST(Fixarray, MatrixIndexing) {
    fa::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};  // row-major
    EXPECT_EQ(m(0, 0), 1.0F);
    EXPECT_EQ(m(0, 1), 2.0F);
    EXPECT_EQ(m(1, 0), 3.0F);
    EXPECT_EQ(m(1, 1), 4.0F);
    m(1, 0) = 7.0F;  // non-const
    EXPECT_EQ(m(1, 0), 7.0F);
    const fa::mat2f& cm = m;
    EXPECT_EQ(cm(1, 0), 7.0F);  // const
}

TEST(Fixarray, Data) {
    // The constructor takes elements in READING order...
    fa::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};
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
    const fa::mat2f& cm = m;
    EXPECT_EQ(cm.data()[3], 4.0F);  // const

    // A vector has one order and no ambiguity.
    const fa::vec3f v{7.0F, 8.0F, 9.0F};
    EXPECT_EQ(v.data()[1], 8.0F);
}

TEST(Fixarray, Identity) {
    constexpr fa::mat3f compile_time = fa::mat3f::identity();  // usable at compile time
    static_assert(compile_time(0, 0) == 1.0F);
    static_assert(compile_time(0, 1) == 0.0F);

    // ...and at run time. A constexpr function nobody executes is a function nobody proved runs.
    fa::mat3f runtime = fa::mat3f::identity();
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_EQ(runtime(r, c), r == c ? 1.0F : 0.0F);
        }
    }
    runtime(2, 2) = 5.0F;  // the non-const matrix accessor on this instantiation
    EXPECT_EQ(runtime(2, 2), 5.0F);
    EXPECT_EQ(fa::mat4d::identity()(3, 3), 1.0);
    EXPECT_EQ(fa::mat2d::identity()(0, 0), 1.0);
}

TEST(Fixarray, Filled) {
    const fa::mat2f threes = fa::mat2f::filled(3.0F);
    EXPECT_EQ(threes(0, 0), 3.0F);
    EXPECT_EQ(threes(1, 1), 3.0F);
    EXPECT_EQ(fa::vec4d::filled(-1.0)[3], -1.0);
}

TEST(Fixarray, Equality) {
    const fa::vec3f a{1.0F, 2.0F, 3.0F};
    const fa::vec3f b{1.0F, 2.0F, 3.0F};
    const fa::vec3f c{1.0F, 2.0F, 4.0F};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a != b);
}

// ---- Arithmetic ---------------------------------------------------------------------------------

TEST(Fixarray, Arithmetic) {
    const fa::vec3f a{1.0F, 2.0F, 3.0F};
    const fa::vec3f b{4.0F, 5.0F, 6.0F};

    EXPECT_TRUE(a + b == (fa::vec3f{5.0F, 7.0F, 9.0F}));
    EXPECT_TRUE(b - a == (fa::vec3f{3.0F, 3.0F, 3.0F}));
    EXPECT_TRUE(-a == (fa::vec3f{-1.0F, -2.0F, -3.0F}));
    EXPECT_TRUE(a * 2.0F == (2.0F * a));           // scalar multiply, both orders
    EXPECT_TRUE((a * 2.0F) / 2.0F == a);           // and its inverse
    EXPECT_TRUE(a + (-a) == fa::vec3f{});          // additive inverse

    fa::vec3f m = a;
    m += b;
    EXPECT_TRUE(m == a + b);
    m -= b;
    EXPECT_TRUE(m == a);
    m *= 3.0F;
    EXPECT_TRUE(m == a * 3.0F);
    m /= 3.0F;
    EXPECT_TRUE(m == a);

    // Doubles behave the same.
    fa::mat2d dm{1.0, 2.0, 3.0, 4.0};
    dm += fa::mat2d::filled(1.0);
    EXPECT_EQ(dm(0, 0), 2.0);
    dm -= fa::mat2d::filled(1.0);
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
    fa::vec2d small{2.0, 4.0};
    small /= 2.0;
    EXPECT_EQ(small[1], 2.0);
    EXPECT_EQ((small / 2.0)[0], 0.5);
    EXPECT_EQ((fa::vec2f{1.0F, 2.0F} + fa::vec2f{1.0F, 1.0F})[1], 3.0F);
    EXPECT_EQ((fa::vec4d::filled(2.0) * 0.5)[0], 1.0);
}

// ---- Vector products ----------------------------------------------------------------------------

TEST(Fixarray, DotAndCross) {
    constexpr fa::vec3f a{1.0F, 2.0F, 3.0F};
    constexpr fa::vec3f b{4.0F, 5.0F, 6.0F};
    static_assert(fa::dot(a, b) == 32.0F);  // compile-time
    EXPECT_EQ(fa::dot(a, b), 32.0F);

    constexpr fa::vec3f c = fa::cross(a, b);
    static_assert(c[0] == -3.0F && c[1] == 6.0F && c[2] == -3.0F);

    // The identity that defines a cross product: perpendicular to both operands.
    EXPECT_EQ(fa::dot(c, a), 0.0F);
    EXPECT_EQ(fa::dot(c, b), 0.0F);
    // ...and anticommutative.
    EXPECT_TRUE(fa::cross(b, a) == -c);

    // Right-handed: x cross y == z.
    const fa::vec3d x{1.0, 0.0, 0.0};
    const fa::vec3d y{0.0, 1.0, 0.0};
    EXPECT_TRUE(fa::cross(x, y) == (fa::vec3d{0.0, 0.0, 1.0}));
    EXPECT_EQ(fa::dot(fa::vec4f{1.0F, 1.0F, 1.0F, 1.0F}, fa::vec4f{1.0F, 2.0F, 3.0F, 4.0F}), 10.0F);
}

TEST(Fixarray, NormAndNormalize) {
    const fa::vec3f v{3.0F, 4.0F, 0.0F};
    EXPECT_EQ(fa::squared_norm(v), 25.0F);
    EXPECT_FLOAT_EQ(fa::norm(v), 5.0F);

    const fa::vec3f unit = fa::normalize(v);
    EXPECT_FLOAT_EQ(fa::norm(unit), 1.0F);
    EXPECT_FLOAT_EQ(unit[0], 0.6F);
    EXPECT_FLOAT_EQ(unit[1], 0.8F);

    EXPECT_DOUBLE_EQ(fa::norm(fa::vec2d{0.0, 2.0}), 2.0);

    // Every instantiation must normalize, not merely refuse to: `normalize` is one reciprocal and a
    // multiply, and a size that only ever saw the throw path is a size nobody proved works.
    EXPECT_DOUBLE_EQ(fa::norm(fa::normalize(fa::vec2d{3.0, 4.0})), 1.0);
    EXPECT_FLOAT_EQ(fa::norm(fa::normalize(fa::vec2f{0.0F, 2.0F})), 1.0F);
    EXPECT_DOUBLE_EQ(fa::norm(fa::normalize(fa::vec4d{1.0, 1.0, 1.0, 1.0})), 1.0);
    EXPECT_FLOAT_EQ(fa::normalize(fa::vec2f{0.0F, 2.0F})[1], 1.0F);

    // The zero vector has no direction; saying so beats returning NaNs.
    EXPECT_THROW((void)fa::normalize(fa::vec3f{}), std::domain_error);
    EXPECT_THROW((void)fa::normalize(fa::vec2d{}), std::domain_error);
    EXPECT_THROW((void)fa::normalize(fa::vec4d{}), std::domain_error);
}

// ---- Matrix products, transpose, trace ----------------------------------------------------------

TEST(Fixarray, Matmul) {
    const fa::mat2f a{1.0F, 2.0F, 3.0F, 4.0F};
    const fa::mat2f b{5.0F, 6.0F, 7.0F, 8.0F};
    const fa::mat2f ab = fa::matmul(a, b);
    EXPECT_EQ(ab(0, 0), 19.0F);
    EXPECT_EQ(ab(0, 1), 22.0F);
    EXPECT_EQ(ab(1, 0), 43.0F);
    EXPECT_EQ(ab(1, 1), 50.0F);
    EXPECT_TRUE(a * b == ab);  // the operator spelling

    // Identity is the multiplicative identity, and matmul is associative.
    EXPECT_TRUE(a * fa::mat2f::identity() == a);
    EXPECT_TRUE(fa::mat2f::identity() * a == a);
    const fa::mat2f c{2.0F, 0.0F, 1.0F, 3.0F};
    EXPECT_TRUE(Close((a * b) * c, a * (b * c)));

    // Non-square shapes chain: (2x3)(3x2) -> 2x2.
    const fa::Mat<double, 2, 3> wide{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    const fa::Mat<double, 3, 2> tall{7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
    const fa::Mat<double, 2, 2> product = wide * tall;
    EXPECT_DOUBLE_EQ(product(0, 0), 58.0);
    EXPECT_DOUBLE_EQ(product(1, 1), 154.0);

    // Matrix times vector.
    const fa::vec2f v = a * fa::vec2f{1.0F, 1.0F};
    EXPECT_EQ(v[0], 3.0F);
    EXPECT_EQ(v[1], 7.0F);
    const fa::vec3d w = fa::mat3d::identity() * fa::vec3d{1.0, 2.0, 3.0};
    EXPECT_TRUE(w == (fa::vec3d{1.0, 2.0, 3.0}));
    const fa::Vec<double, 2> rect = wide * fa::vec3d{1.0, 1.0, 1.0};
    EXPECT_DOUBLE_EQ(rect[0], 6.0);
    EXPECT_DOUBLE_EQ(rect[1], 15.0);
}

TEST(Fixarray, TransposeAndTrace) {
    const fa::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};
    const fa::mat2f t = fa::transpose(m);
    EXPECT_EQ(t(0, 1), 3.0F);
    EXPECT_EQ(t(1, 0), 2.0F);
    EXPECT_TRUE(fa::transpose(t) == m);  // an involution
    EXPECT_EQ(fa::trace(m), 5.0F);
    EXPECT_EQ(fa::trace(fa::mat4d::identity()), 4.0);

    // A non-square transpose swaps the shape.
    const fa::Mat<float, 2, 3> wide{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    const fa::Mat<float, 3, 2> narrow = fa::transpose(wide);
    static_assert(decltype(narrow)::rows == 3 && decltype(narrow)::cols == 2);
    EXPECT_EQ(narrow(2, 0), 3.0F);
    EXPECT_EQ(narrow(0, 1), 4.0F);
}

// ---- Determinant and inverse --------------------------------------------------------------------

TEST(Fixarray, DeterminantAndInverse) {
    // 2x2
    const fa::mat2f m2{4.0F, 7.0F, 2.0F, 6.0F};
    EXPECT_FLOAT_EQ(fa::determinant(m2), 10.0F);
    EXPECT_TRUE(Close(fa::inverse(m2) * m2, fa::mat2f::identity()));
    EXPECT_TRUE(Close(m2 * fa::inverse(m2), fa::mat2f::identity()));

    // 3x3
    const fa::mat3d m3{2.0, -1.0, 0.0, -1.0, 2.0, -1.0, 0.0, -1.0, 2.0};
    EXPECT_DOUBLE_EQ(fa::determinant(m3), 4.0);
    EXPECT_TRUE(Close(fa::inverse(m3) * m3, fa::mat3d::identity(), 1e-12));

    // 4x4
    const fa::mat4d m4{1.0, 2.0, 0.0, 1.0, 0.0, 1.0, 3.0, 0.0,
                       2.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0};
    EXPECT_TRUE(Close(fa::inverse(m4) * m4, fa::mat4d::identity(), 1e-12));
    EXPECT_TRUE(Close(m4 * fa::inverse(m4), fa::mat4d::identity(), 1e-12));
    EXPECT_TRUE(Close(fa::inverse(fa::inverse(m4)), m4, 1e-10));  // an involution

    // det(I) == 1 at every supported size, and det(AB) == det(A)det(B).
    EXPECT_FLOAT_EQ(fa::determinant(fa::mat2f::identity()), 1.0F);
    EXPECT_DOUBLE_EQ(fa::determinant(fa::mat3d::identity()), 1.0);
    EXPECT_DOUBLE_EQ(fa::determinant(fa::mat4d::identity()), 1.0);
    const fa::mat3d other{1.0, 2.0, 3.0, 0.0, 1.0, 4.0, 5.0, 6.0, 0.0};
    EXPECT_NEAR(fa::determinant(m3 * other), fa::determinant(m3) * fa::determinant(other), 1e-9);
    EXPECT_FLOAT_EQ(fa::determinant(fa::mat4f{1.0F, 2.0F, 0.0F, 1.0F, 0.0F, 1.0F, 3.0F, 0.0F,
                                              2.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 2.0F}),
                    static_cast<float>(fa::determinant(m4)));

    // A singular matrix has no inverse, and says so rather than returning infinities.
    EXPECT_EQ(fa::determinant(fa::mat2f{}), 0.0F);
    EXPECT_THROW((void)fa::inverse(fa::mat2f{}), std::domain_error);
    EXPECT_THROW((void)fa::inverse(fa::mat3d{}), std::domain_error);
    EXPECT_THROW((void)fa::inverse(fa::mat4d{}), std::domain_error);

    // Rank-deficient, not merely all-zero: two identical rows.
    EXPECT_THROW((void)fa::inverse(fa::mat3d{1.0, 2.0, 3.0, 1.0, 2.0, 3.0, 4.0, 5.0, 7.0}),
                 std::domain_error);
}

// ---- The type is not secretly limited to the graphics sizes -----------------------------------

TEST(Fixarray, WorksBeyondTheAliasedSizes) {
    // The aliases stop at 4 because that is where graphics stops; the TYPE does not. This also
    // exercises `dot`'s general recursive pairwise sum, which the 2/3/4 cases short-circuit past.
    fa::Vec<double, 8> a;
    fa::Vec<double, 8> b;
    for (std::size_t i = 0; i < 8; ++i) {
        a[i] = static_cast<double>(i + 1);  // 1..8
        b[i] = 1.0;
    }
    EXPECT_DOUBLE_EQ(fa::dot(a, b), 36.0);       // 1+2+...+8
    EXPECT_DOUBLE_EQ(fa::squared_norm(b), 8.0);  // eight ones
    EXPECT_DOUBLE_EQ(fa::norm(b), std::sqrt(8.0));
    EXPECT_DOUBLE_EQ(fa::norm(fa::normalize(a)), 1.0);

    // An odd length exercises the uneven split of the recursion (5 = 2 + 3).
    fa::Vec<float, 5> odd{1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
    EXPECT_FLOAT_EQ(fa::dot(odd, odd), 55.0F);  // 1+4+9+16+25

    // And a bigger matrix still multiplies, transposes and transforms.
    const fa::Mat<double, 5, 5> identity5 = fa::Mat<double, 5, 5>::identity();
    EXPECT_DOUBLE_EQ(fa::trace(identity5), 5.0);
    EXPECT_TRUE(fa::transpose(identity5) == identity5);
    const fa::Vec<double, 5> five{1.0, 2.0, 3.0, 4.0, 5.0};
    const fa::Vec<double, 5> through = identity5 * five;
    EXPECT_TRUE(through == five);
    EXPECT_TRUE(fa::matmul(identity5, identity5) == identity5);
}

// ---- The same answers as NDArray, which is the promise the name makes -------------------------

TEST(Fixarray, AgreesWithTheDynamicNDArray) {
    // `Fixed` claims to be "NDArray, only faster". The claim is only worth making if the answers
    // agree; a 3x3 inverse and determinant are where that is easiest to check.
    const std::vector<double> values{2.0, -1.0, 0.0, -1.0, 2.0, -1.0, 0.0, -1.0, 2.0};

    const fa::mat3d fixed{values[0], values[1], values[2], values[3], values[4],
                          values[5], values[6], values[7], values[8]};
    const fa::mat3d fixed_inv = fa::inverse(fixed);

    const nd::NDArray dynamic = nd::reshape(nd::array(values), {3, 3});
    const nd::NDArray dynamic_inv = la::inv(dynamic);

    for (long long r = 0; r < 3; ++r) {
        for (long long c = 0; c < 3; ++c) {
            EXPECT_NEAR(fixed_inv(static_cast<std::size_t>(r), static_cast<std::size_t>(c)),
                        nd::get(dynamic_inv, {r, c}), 1e-12);
        }
    }
    EXPECT_NEAR(fa::determinant(fixed), la::det(dynamic), 1e-12);
}

// ---- The GLSL/GLM surface: geometry ------------------------------------------------------------

TEST(Fixarray, Geometry) {
    const fa::vec3f a{1.0F, 2.0F, 3.0F};
    const fa::vec3f b{4.0F, 6.0F, 8.0F};
    EXPECT_FLOAT_EQ(fa::distance(a, b), std::sqrt(9.0F + 16.0F + 25.0F));
    EXPECT_FLOAT_EQ(fa::distance_squared(a, b), 50.0F);
    EXPECT_DOUBLE_EQ(fa::distance(fa::vec2d{0.0, 0.0}, fa::vec2d{3.0, 4.0}), 5.0);

    // reflect off the floor (normal +y): the y-component flips, x and z survive.
    const fa::vec3f down{1.0F, -1.0F, 0.0F};
    const fa::vec3f up{0.0F, 1.0F, 0.0F};
    EXPECT_TRUE(fa::reflect(down, up) == (fa::vec3f{1.0F, 1.0F, 0.0F}));
    // A vector reflected twice about the same normal returns to itself (dot with unit normal).
    EXPECT_TRUE(fa::reflect(fa::reflect(down, up), up) == down);

    // refract with equal indices (eta = 1) does not bend, so a unit vector stays unit.
    const fa::vec3f incident = fa::normalize(fa::vec3f{1.0F, -1.0F, 0.0F});
    EXPECT_FLOAT_EQ(fa::norm(fa::refract(incident, up, 1.0F)), 1.0F);
    // Total internal reflection returns the zero vector.
    const fa::vec2d grazing = fa::normalize(fa::vec2d{1.0, -0.01});
    EXPECT_TRUE(fa::refract(grazing, fa::vec2d{0.0, 1.0}, 5.0) == fa::vec2d{});

    // faceforward keeps a normal on the incident's side. dot(nref, I) < 0 -> return n unchanged.
    EXPECT_TRUE(fa::faceforward(up, down, up) == up);
    const fa::vec3f away{0.0F, 1.0F, 0.0F};
    EXPECT_TRUE(fa::faceforward(up, fa::vec3f{0.0F, 1.0F, 0.0F}, away) == (fa::vec3f{0.0F, -1.0F, 0.0F}));
}

// ---- The GLSL/GLM surface: component-wise common builtins ---------------------------------------

TEST(Fixarray, CommonUnary) {
    EXPECT_TRUE(fa::abs(fa::vec4f{-1.0F, 2.0F, -3.0F, 0.0F}) == (fa::vec4f{1.0F, 2.0F, 3.0F, 0.0F}));
    EXPECT_TRUE(fa::sign(fa::vec3f{-2.0F, 0.0F, 5.0F}) == (fa::vec3f{-1.0F, 0.0F, 1.0F}));
    // Works on a matrix too — it is elementwise over the whole array.
    EXPECT_TRUE(fa::abs(fa::mat2d{-1.0, 2.0, -3.0, 4.0}) == (fa::mat2d{1.0, 2.0, 3.0, 4.0}));
    EXPECT_TRUE(fa::sign(fa::mat2f{-4.0F, 0.0F, 8.0F, -1.0F}) == (fa::mat2f{-1.0F, 0.0F, 1.0F, -1.0F}));
    EXPECT_TRUE(fa::abs(fa::vec2d{-1.5, -2.5}) == (fa::vec2d{1.5, 2.5}));
}

TEST(Fixarray, MinMaxClamp) {
    const fa::vec3f a{1.0F, 5.0F, 3.0F};
    const fa::vec3f b{4.0F, 2.0F, 6.0F};
    EXPECT_TRUE(fa::min(a, b) == (fa::vec3f{1.0F, 2.0F, 3.0F}));
    EXPECT_TRUE(fa::max(a, b) == (fa::vec3f{4.0F, 5.0F, 6.0F}));
    EXPECT_TRUE(fa::min(fa::vec3f{1.0F, 5.0F, 9.0F}, 4.0F) == (fa::vec3f{1.0F, 4.0F, 4.0F}));
    EXPECT_TRUE(fa::max(fa::vec3f{1.0F, 5.0F, 9.0F}, 4.0F) == (fa::vec3f{4.0F, 5.0F, 9.0F}));

    // scalar-bound clamp — pinning a colour to [0, 1]
    EXPECT_TRUE(fa::clamp(fa::vec4f{-1.0F, 0.5F, 2.0F, 0.0F}, 0.0F, 1.0F) ==
                (fa::vec4f{0.0F, 0.5F, 1.0F, 0.0F}));
    // per-element bounds
    EXPECT_TRUE(fa::clamp(fa::vec3d{5.0, -5.0, 0.5}, fa::vec3d{0.0, 0.0, 0.0},
                          fa::vec3d{1.0, 1.0, 1.0}) == (fa::vec3d{1.0, 0.0, 0.5}));
    // matrices too (double, to exercise that instantiation)
    EXPECT_TRUE(fa::min(fa::mat2d{1.0, 4.0, 3.0, 2.0}, fa::mat2d{2.0, 2.0, 2.0, 2.0}) ==
                (fa::mat2d{1.0, 2.0, 2.0, 2.0}));
    EXPECT_TRUE(fa::max(fa::mat2d::filled(1.0), 3.0) == fa::mat2d::filled(3.0));
}

TEST(Fixarray, MixStep) {
    // mix with a scalar factor is a lerp
    EXPECT_TRUE(fa::mix(fa::vec3f{0.0F, 0.0F, 0.0F}, fa::vec3f{2.0F, 4.0F, 6.0F}, 0.5F) ==
                (fa::vec3f{1.0F, 2.0F, 3.0F}));
    EXPECT_TRUE(fa::mix(fa::vec2d{1.0, 1.0}, fa::vec2d{3.0, 5.0}, 0.0) == (fa::vec2d{1.0, 1.0}));
    EXPECT_TRUE(fa::mix(fa::vec2d{1.0, 1.0}, fa::vec2d{3.0, 5.0}, 1.0) == (fa::vec2d{3.0, 5.0}));
    // per-element factor
    EXPECT_TRUE(fa::mix(fa::vec3f{0.0F, 0.0F, 0.0F}, fa::vec3f{10.0F, 10.0F, 10.0F},
                        fa::vec3f{0.0F, 0.5F, 1.0F}) == (fa::vec3f{0.0F, 5.0F, 10.0F}));

    // step: below the edge is 0, at or above is 1
    EXPECT_TRUE(fa::step(2.0F, fa::vec3f{1.0F, 2.0F, 3.0F}) == (fa::vec3f{0.0F, 1.0F, 1.0F}));
    EXPECT_TRUE(fa::step(0.0, fa::vec2d{-1.0, 1.0}) == (fa::vec2d{0.0, 1.0}));

    // smoothstep: clamped at the edges, 0.5 at the midpoint, Hermite in between
    const fa::vec4f s = fa::smoothstep(0.0F, 1.0F, fa::vec4f{-1.0F, 0.0F, 0.5F, 2.0F});
    EXPECT_FLOAT_EQ(s[0], 0.0F);
    EXPECT_FLOAT_EQ(s[1], 0.0F);
    EXPECT_FLOAT_EQ(s[2], 0.5F);
    EXPECT_FLOAT_EQ(s[3], 1.0F);
    // monotone and within [0,1]
    const fa::vec2d q = fa::smoothstep(0.0, 10.0, fa::vec2d{2.5, 7.5});
    EXPECT_GT(q[1], q[0]);
    EXPECT_GE(q[0], 0.0);
    EXPECT_LE(q[1], 1.0);
}

// ---- The GLSL/GLM surface: matrix builtins -----------------------------------------------------

TEST(Fixarray, MatrixExtras) {
    // Hadamard product multiplies corresponding entries (NOT the matrix product).
    const fa::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};
    const fa::mat2f k{2.0F, 0.0F, 0.0F, 2.0F};
    EXPECT_TRUE(fa::matrix_comp_mult(m, k) == (fa::mat2f{2.0F, 0.0F, 0.0F, 8.0F}));

    // outer product: (i, j) = c[i] * r[j]
    const fa::Mat<float, 2, 3> op = fa::outer_product(fa::vec2f{1.0F, 2.0F}, fa::vec3f{3.0F, 4.0F, 5.0F});
    EXPECT_FLOAT_EQ(op(0, 0), 3.0F);
    EXPECT_FLOAT_EQ(op(0, 2), 5.0F);
    EXPECT_FLOAT_EQ(op(1, 1), 8.0F);
    // outer_product(c, r) == c as a column times r as a row, so its rank is one: rows are multiples.
    EXPECT_FLOAT_EQ(op(1, 0) / op(0, 0), 2.0F);

    // inverse_transpose carries normals: for an orthonormal (rotation) matrix it equals the matrix
    // itself, since transpose(inverse(R)) = transpose(transpose(R)) = R.
    const double c = std::cos(0.7);
    const double s = std::sin(0.7);
    const fa::mat3d rot{c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0};
    const fa::mat3d it = fa::inverse_transpose(rot);
    for (std::size_t i = 0; i < 9; ++i) { EXPECT_NEAR(it.data()[i], rot.data()[i], 1e-12); }
    // For a non-uniform scale S = diag(2, 4), inverse_transpose is diag(1/2, 1/4).
    const fa::mat2d scale{2.0, 0.0, 0.0, 4.0};
    const fa::mat2d nrm = fa::inverse_transpose(scale);
    EXPECT_NEAR(nrm(0, 0), 0.5, 1e-12);
    EXPECT_NEAR(nrm(1, 1), 0.25, 1e-12);
    // Singular still throws (via inverse).
    EXPECT_THROW((void)fa::inverse_transpose(fa::mat3d{}), std::domain_error);
}

// ---- Enum subscripting: a scoped enum names an axis, and only when indexing --------------------

namespace {
/// A caller's scoped enum. It stays strongly typed everywhere except at a subscript, which is the
/// whole point of ndarray::Subscript.
enum class Axis : std::uint8_t { X = 0, Y = 1, Z = 2 };
enum class Basis : std::uint8_t { Right = 0, Up = 1, Forward = 2 };
}  // namespace

TEST(Fixarray, EnumIndexingOnVectorsAndMatrices) {
    fa::vec3f v{7.0F, 8.0F, 9.0F};
    // read a component by name
    EXPECT_FLOAT_EQ(v[Axis::X], 7.0F);
    EXPECT_FLOAT_EQ(v[Axis::Z], 9.0F);
    // write by name (non-const overload)
    v[Axis::Y] = 42.0F;
    EXPECT_FLOAT_EQ(v[1], 42.0F);
    // const overload
    const fa::vec3f& cv = v;
    EXPECT_FLOAT_EQ(cv[Axis::Y], 42.0F);
    // a plain integer still resolves the ordinary overload
    EXPECT_FLOAT_EQ(v[std::size_t{0}], 7.0F);

    fa::mat3f m = fa::mat3f::identity();
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
    const fa::mat3f& cm = m;
    EXPECT_FLOAT_EQ(cm(Axis::X, Axis::Z), 5.0F);
    EXPECT_FLOAT_EQ(cm(Axis::Y, std::size_t{1}), 1.0F);
    EXPECT_FLOAT_EQ(cm(std::size_t{2}, Axis::Z), 1.0F);
}

TEST(Fixarray, NamedRowsAndColumns) {
    const fa::mat3f id = fa::mat3f::identity();
    // a basis vector by name — the axis an enum was made for
    EXPECT_TRUE(fa::column(id, Basis::Forward) == (fa::vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(fa::column(id, Basis::Right) == (fa::vec3f{1.0F, 0.0F, 0.0F}));
    // a plain integer index still works
    EXPECT_TRUE(fa::column(id, 1) == (fa::vec3f{0.0F, 1.0F, 0.0F}));
    EXPECT_TRUE(fa::row(id, 2) == (fa::vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(fa::row(id, Axis::X) == (fa::vec3f{1.0F, 0.0F, 0.0F}));

    // On a real (column-major) transform, column j is the image of basis vector j.
    const fa::mat3f t{2.0F, 0.0F, 1.0F, 0.0F, 3.0F, 2.0F, 0.0F, 0.0F, 1.0F};
    EXPECT_TRUE(fa::column(t, Axis::X) == (fa::vec3f{2.0F, 0.0F, 0.0F}));  // where x-hat lands
    EXPECT_TRUE(fa::row(t, Axis::X) == (fa::vec3f{2.0F, 0.0F, 1.0F}));
    // A non-square matrix: row length is the column count and vice-versa.
    const fa::Mat<double, 2, 3> wide{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    EXPECT_TRUE(fa::row(wide, 1) == (fa::vec3d{4.0, 5.0, 6.0}));
    EXPECT_TRUE(fa::column(wide, 2) == (fa::vec2d{3.0, 6.0}));
}

// ---- from_indices: the one-pass elementwise builder the component-wise ops ride on --------------

TEST(Fixarray, FromIndices) {
    // A vector built from its flat index.
    const fa::vec4f v = fa::vec4f::from_indices([](std::size_t i) { return static_cast<float>(i * i); });
    EXPECT_TRUE(v == (fa::vec4f{0.0F, 1.0F, 4.0F, 9.0F}));

    // For a matrix the index runs over STORAGE order (column-major), so building the identity by
    // "1 on the diagonal" means indices divisible by rows+1 — the same fact identity() uses.
    const fa::mat3f id =
        fa::mat3f::from_indices([](std::size_t i) { return i % 4 == 0 ? 1.0F : 0.0F; });
    EXPECT_TRUE(id == fa::mat3f::identity());

    // It is usable at compile time.
    constexpr fa::vec3d ramp = fa::vec3d::from_indices([](std::size_t i) { return static_cast<double>(i); });
    static_assert(ramp[2] == 2.0);

    // Column-major storage is observable: element k of the buffer is what f(k) returned.
    const fa::mat2f m = fa::mat2f::from_indices([](std::size_t i) { return static_cast<float>(i); });
    EXPECT_EQ(m.data()[0], 0.0F);
    EXPECT_EQ(m.data()[3], 3.0F);
    EXPECT_EQ(m(0, 0), 0.0F);
    EXPECT_EQ(m(0, 1), 2.0F);  // flat index 2 is (row 0, col 1) in column-major
}

// ---- display: to_string and the stream operator, in the NDArray's nested-bracket form -----------

TEST(Fixarray, ToStringMatchesTheNDArrayRendering) {
    // A vector is one bracket level; elements go through the SHARED scalar formatter
    // (1.5 prints "1.5", a whole number prints with no trailing ".0").
    EXPECT_EQ(fa::to_string(fa::vec3f{1.5F, -2.0F, 3.0F}), "[1.5, -2, 3]");
    // A matrix renders in reading (row, column) order regardless of the column-major storage.
    EXPECT_EQ(fa::to_string(fa::mat2f{1.0F, 2.0F, 3.0F, 4.0F}), "[[1, 2], [3, 4]]");
    // The double instantiation formats identically.
    EXPECT_EQ(fa::to_string(fa::vec2d{0.25, 42.0}), "[0.25, 42]");
}

TEST(Fixarray, StreamInsertionUsesTheToStringForm) {
    std::ostringstream vs;
    vs << fa::vec3f{1.0F, 2.5F, -3.0F};
    EXPECT_EQ(vs.str(), "[1, 2.5, -3]");
    std::ostringstream ms;
    ms << fa::mat2f{1.0F, 2.0F, 3.0F, 4.0F};
    EXPECT_EQ(ms.str(), "[[1, 2], [3, 4]]");
}

// ---- builtins::index — what cheatah's value-position subscript v[i] / m[i, j] lowers to ---------

TEST(Fixarray, BuiltinsIndexLowersSubscripts) {
    const fa::vec3f v{7.0F, 8.0F, 9.0F};
    EXPECT_FLOAT_EQ(cheatah::builtins::index(v, std::size_t{1}), 8.0F);
    EXPECT_FLOAT_EQ(cheatah::builtins::index(v, Axis::Z), 9.0F);  // enum labels work here too

    const fa::mat2f m{1.0F, 2.0F, 3.0F, 4.0F};  // reading order
    EXPECT_FLOAT_EQ(cheatah::builtins::index(m, std::size_t{1}, std::size_t{0}), 3.0F);
    EXPECT_FLOAT_EQ(cheatah::builtins::index(m, Axis::X, Axis::Y), 2.0F);  // (row 0, col 1)
}
