// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file fixed.hpp
 * @brief cheatah `linalg` — fixed-extent arrays (@ref cheatah::linalg::Fixed): exactly like an
 *        @ref cheatah::ndarray::NDArray, only faster.
 *
 * An @ref cheatah::ndarray::NDArray carries its shape at runtime and its elements on the heap, which
 * is what makes it general. When the shape is known at compile time and tiny — a 3-D direction, a
 * 4×4 transform — that generality is the whole cost: a heap allocation, a stride computation and an
 * indirection per operation, to move sixteen floats.
 *
 * @ref cheatah::linalg::Fixed is the same idea with the shape moved into the type. The extents are
 * template parameters, the elements live inline (a `std::array`, so the value is trivially copyable
 * and sits on the stack or straight inside another struct), the loops have compile-time trip counts
 * and auto-vectorize, and nothing allocates. **These are the types to reach for in high-performance
 * applications — a renderer's transforms, a physics solver's contact frames, a filter's small
 * state** — where the same matrix is built and consumed millions of times a second.
 *
 * Everything else is deliberately the same as `NDArray`: element types are the same @ref
 * cheatah::ndarray::Field, the mathematical index is `(row, column)`, the vocabulary is numpy's
 * (@ref dot, @ref matmul, @ref transpose, @ref determinant, @ref inverse), and results agree
 * elementwise. Reach for `NDArray` when the shape is data; reach for `Fixed` when the shape is a
 * fact about the program.
 *
 * **One deliberate difference: a matrix is stored COLUMN-MAJOR**, where `NDArray` is row-major. The
 * indexing you write is unchanged — `m(row, col)` means what it says, and the constructor still
 * takes elements in reading order — but @ref Fixed::data() hands back columns, not rows. Two reasons,
 * both measured: `m * v` becomes a sum of scaled columns, which is contiguous, vertical, and
 * vectorizes, instead of four horizontal dot products that cost a shuffle network; and the buffer is
 * already in the order graphics APIs (GLSL, SPIR-V, Metal) and GLM expect, so uploading a transform
 * is a copy rather than a transpose. Only reach for `data()` when you mean the raw buffer.
 *
 * ```
 * using namespace cheatah::linalg;
 * vec3f up{0.0F, 1.0F, 0.0F};        // a 3-vector, 12 bytes, no allocation
 * mat4f m = mat4f::identity();       // a 4x4, 64 bytes — exactly a push constant
 * vec3f v = normalize(cross(up, w)); // numpy's vocabulary, glm's speed
 * ```
 *
 * Rank 1 (a vector) and rank 2 (a matrix) are supported; higher ranks are a mechanical extension of
 * the same storage and are added when a caller needs one.
 *
 * This header is templates only — nothing is compiled into `libcheatah_linalg` — so the caller's
 * optimization flags apply. The routines in @ref routines.hpp remain the home of the heavy,
 * shape-generic numerics (LU, QR, SVD, eigen); `Fixed` owns the small closed forms where a general
 * factorization would cost more than the answer.
 */

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "ndarray.hpp"

namespace cheatah::linalg {

/// The product of a pack of extents — a fixed array's element count, `1` for an empty pack.
/// @tparam Dims the extents.
template <std::size_t... Dims>
inline constexpr std::size_t extent_product = (std::size_t{1} * ... * Dims);

/**
 * Constrains a @ref Fixed to a supported rank: 1 (a vector) or 2 (a matrix). Higher ranks are a
 * mechanical extension of the same storage, added when a caller needs one — the concept is what
 * turns "not yet" into a readable compile error instead of a template-instantiation wall.
 * @tparam Rank the number of extents.
 */
template <std::size_t Rank>
concept SupportedRank = (Rank == 1 || Rank == 2);

/**
 * A fixed-extent, inline-stored array — an @ref cheatah::ndarray::NDArray whose shape lives in the
 * type. Trivially copyable and allocation-free; a matrix is stored column-major (see the file doc).
 *
 * @tparam T the element type; any @ref cheatah::ndarray::Field, exactly as `NDArray` accepts.
 * @tparam Dims the extents. One extent is a vector, two are a matrix (rows, then columns).
 */
template <ndarray::Field T, std::size_t... Dims>
    requires SupportedRank<sizeof...(Dims)> && (((Dims > 0) && ...))
class Fixed {
  public:
    /// The element type.
    using value_type = T;

    /// The number of extents: 1 for a vector, 2 for a matrix.
    static constexpr std::size_t rank = sizeof...(Dims);
    /// The total number of elements.
    static constexpr std::size_t size = extent_product<Dims...>;
    /// The extents, in order (rows, then columns for a matrix).
    static constexpr std::array<std::size_t, rank> shape{Dims...};

    /// Rows — the first extent (a vector has one row).
    static constexpr std::size_t rows = shape[0];
    /// Columns — the second extent, or 1 for a vector.
    static constexpr std::size_t cols = rank == 2 ? shape[1] : 1;

    /// Every element zero — the additive identity, and what a default-constructed value holds.
    constexpr Fixed() = default;

    /**
     * Construct from exactly @ref size elements, written in READING order: a matrix is given row by
     * row, the way it appears on paper, regardless of how it is stored. Arguments are converted to
     * @p T, so a `vec3f` accepts the doubles a cheatah program computes with.
     * @tparam Args the argument types; each must be convertible to @p T.
     * @param args the elements in reading order; exactly @ref size of them.
     */
    template <class... Args>
        requires(sizeof...(Args) == size) && (std::convertible_to<Args, T> && ...)
    explicit constexpr Fixed(Args... args) {
        const std::array<T, size> reading_order{static_cast<T>(args)...};
        if constexpr (rank == 1) {
            data_ = reading_order;
        } else {
            for (std::size_t r = 0; r < rows; ++r) {
                for (std::size_t c = 0; c < cols; ++c) { data_[c * rows + r] = reading_order[r * cols + c]; }
            }
        }
    }

    /**
     * The square identity: ones on the diagonal, zeros elsewhere.
     * @return the identity matrix.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Identity
     */
    static constexpr Fixed identity()
        requires(rank == 2 && rows == cols)
    {
        // Built element by element in place. Zeroing the buffer and then poking the diagonal would
        // store every byte twice; this stores each once, and the compiler folds it to a constant.
        // In a square column-major buffer the diagonal is exactly the indices divisible by rows + 1.
        return identity_impl(std::make_index_sequence<size>{});
    }

    /**
     * Every element set to @p value — `filled(0)` is the zero value, `filled(1)` a matrix of ones.
     * @param value the element to repeat.
     * @return the filled array.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Filled
     */
    static constexpr Fixed filled(T value) {
        Fixed result;
        for (std::size_t i = 0; i < size; ++i) { result.data_[i] = value; }
        return result;
    }

    /**
     * Element @p i of a vector.
     * @param i the index, `0 <= i < size`.
     * @return a reference to the element.
     * @complexity O(1).
     * @alloc none.
     * @test LinalgFixed.VectorIndexing
     */
    constexpr T& operator[](std::size_t i)
        requires(rank == 1)
    {
        return data_[i];
    }

    /**
     * Element @p i of a vector (read-only).
     * @param i the index, `0 <= i < size`.
     * @return a const reference to the element.
     * @complexity O(1).
     * @alloc none.
     * @test LinalgFixed.VectorIndexing
     */
    constexpr const T& operator[](std::size_t i) const
        requires(rank == 1)
    {
        return data_[i];
    }

    /**
     * Element (@p row, @p col) of a matrix. The index is mathematical; the storage is column-major.
     * @param row the row, `0 <= row < rows`.
     * @param col the column, `0 <= col < cols`.
     * @return a reference to the element.
     * @complexity O(1).
     * @alloc none.
     * @test LinalgFixed.MatrixIndexing
     */
    constexpr T& operator()(std::size_t row, std::size_t col)
        requires(rank == 2)
    {
        return data_[col * rows + row];
    }

    /**
     * Element (@p row, @p col) of a matrix, read-only. Mathematical index; column-major storage.
     * @param row the row, `0 <= row < rows`.
     * @param col the column, `0 <= col < cols`.
     * @return a const reference to the element.
     * @complexity O(1).
     * @alloc none.
     * @test LinalgFixed.MatrixIndexing
     */
    constexpr const T& operator()(std::size_t row, std::size_t col) const
        requires(rank == 2)
    {
        return data_[col * rows + row];
    }

    /**
     * A pointer to the elements, contiguous — column-major for a matrix, which is exactly the order a
     * GPU uniform, a push constant or a BLAS call expects, so an upload is a copy not a transpose.
     * @return the first element's address.
     * @complexity O(1).
     * @alloc none.
     * @test LinalgFixed.Data
     */
    constexpr T* data() { return data_.data(); }

    /**
     * A pointer to the elements, contiguous and column-major for a matrix (read-only).
     * @return the first element's address.
     * @complexity O(1).
     * @alloc none.
     * @test LinalgFixed.Data
     */
    constexpr const T* data() const { return data_.data(); }

    /**
     * Elementwise equality. Exact, as `==` on the elements is exact — floating-point values compare
     * only if they are bit-for-bit equal.
     * @param other the array to compare with.
     * @return true iff every element matches.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Equality
     */
    constexpr bool operator==(const Fixed& other) const = default;

    /**
     * Add @p other elementwise, in place.
     * @param other the array to add.
     * @return a reference to this array.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    constexpr Fixed& operator+=(const Fixed& other) {
        for (std::size_t i = 0; i < size; ++i) { data_[i] += other.data_[i]; }
        return *this;
    }

    /**
     * Subtract @p other elementwise, in place.
     * @param other the array to subtract.
     * @return a reference to this array.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    constexpr Fixed& operator-=(const Fixed& other) {
        for (std::size_t i = 0; i < size; ++i) { data_[i] -= other.data_[i]; }
        return *this;
    }

    /**
     * Scale every element by @p scalar, in place.
     * @param scalar the factor.
     * @return a reference to this array.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    constexpr Fixed& operator*=(T scalar) {
        for (std::size_t i = 0; i < size; ++i) { data_[i] *= scalar; }
        return *this;
    }

    /**
     * Divide every element by @p scalar, in place.
     * @param scalar the divisor.
     * @return a reference to this array.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    constexpr Fixed& operator/=(T scalar) {
        for (std::size_t i = 0; i < size; ++i) { data_[i] /= scalar; }
        return *this;
    }

    /**
     * Elementwise sum.
     * @param a,b the arrays to add.
     * @return `a + b`.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    friend constexpr Fixed operator+(Fixed a, const Fixed& b) { return a += b; }

    /**
     * Elementwise difference.
     * @param a,b the arrays to subtract.
     * @return `a - b`.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    friend constexpr Fixed operator-(Fixed a, const Fixed& b) { return a -= b; }

    /**
     * Negation.
     * @param a the array to negate.
     * @return `-a`.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    friend constexpr Fixed operator-(Fixed a) { return a *= static_cast<T>(-1); }

    /**
     * Scale by a scalar.
     * @param a the array. @param scalar the factor.
     * @return `a * scalar`.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    friend constexpr Fixed operator*(Fixed a, T scalar) { return a *= scalar; }

    /**
     * Scale by a scalar.
     * @param scalar the factor. @param a the array.
     * @return `scalar * a`.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    friend constexpr Fixed operator*(T scalar, Fixed a) { return a *= scalar; }

    /**
     * Divide by a scalar.
     * @param a the array. @param scalar the divisor.
     * @return `a / scalar`.
     * @complexity O(size).
     * @alloc none.
     * @test LinalgFixed.Arithmetic
     */
    friend constexpr Fixed operator/(Fixed a, T scalar) { return a /= scalar; }

  private:
    /// Adopt an already-built element buffer, skipping the zero-initialization of the default
    /// constructor. Private: the buffer's layout (column-major for a matrix) is an implementation
    /// detail that only the members below may rely on.
    explicit constexpr Fixed(const std::array<T, size>& elements) : data_(elements) {}

    /// @ref identity's worker: emits each element exactly once, with no zeroing pass.
    /// @tparam I the flat indices 0 … size-1.
    /// @return the identity matrix.
    template <std::size_t... I>
    static constexpr Fixed identity_impl(std::index_sequence<I...>)
        requires(rank == 2 && rows == cols)
    {
        return Fixed(std::array<T, size>{(I % (rows + 1) == 0 ? T{1} : T{0})...});
    }

    /// The elements, inline: a vector in order, a matrix column by column. Zero by default.
    std::array<T, size> data_{};
};

/// A fixed-extent vector of @p N elements.
/// @tparam T the element type. @tparam N the length.
template <ndarray::Field T, std::size_t N>
using Vec = Fixed<T, N>;

/// A fixed-extent matrix of @p R rows and @p C columns, row-major.
/// @tparam T the element type. @tparam R the rows. @tparam C the columns.
template <ndarray::Field T, std::size_t R, std::size_t C>
using Mat = Fixed<T, R, C>;

/// A 2-D vector of `float`.
using vec2f = Vec<float, 2>;
/// A 3-D vector of `float` — a direction, a position, a colour.
using vec3f = Vec<float, 3>;
/// A 4-D vector of `float` — a homogeneous point, an RGBA colour.
using vec4f = Vec<float, 4>;
/// A 2-D vector of `double`.
using vec2d = Vec<double, 2>;
/// A 3-D vector of `double`.
using vec3d = Vec<double, 3>;
/// A 4-D vector of `double`.
using vec4d = Vec<double, 4>;

/// A 2×2 matrix of `float`.
using mat2f = Mat<float, 2, 2>;
/// A 3×3 matrix of `float` — a rotation, or a normal matrix.
using mat3f = Mat<float, 3, 3>;
/// A 4×4 matrix of `float` — a transform; exactly the 64 bytes of a push constant.
using mat4f = Mat<float, 4, 4>;
/// A 2×2 matrix of `double`.
using mat2d = Mat<double, 2, 2>;
/// A 3×3 matrix of `double`.
using mat3d = Mat<double, 3, 3>;
/// A 4×4 matrix of `double`.
using mat4d = Mat<double, 4, 4>;

namespace detail {

/**
 * Sum @p n elements PAIRWISE rather than left to right. A serial `sum += x[i]` chains each add on
 * the previous one, so the loop runs at the latency of an addition; halving the array instead lets
 * independent adds issue together, and — the reason numerics people reach for it — the rounding
 * error grows as O(log n) instead of O(n).
 * @tparam T the element type. @tparam N the array length.
 * @param values the products to sum.
 * @return their sum.
 * @complexity O(N).
 * @alloc none.
 * @test LinalgFixed.DotAndCross
 */
template <ndarray::Field T, std::size_t N>
constexpr T pairwise_sum(const std::array<T, N>& values) {
    if constexpr (N == 1) {
        return values[0];
    } else if constexpr (N == 2) {
        return values[0] + values[1];
    } else if constexpr (N == 3) {
        return (values[0] + values[1]) + values[2];
    } else if constexpr (N == 4) {
        return (values[0] + values[1]) + (values[2] + values[3]);
    } else {
        constexpr std::size_t half = N / 2;
        std::array<T, half> lo{};
        std::array<T, N - half> hi{};
        for (std::size_t i = 0; i < half; ++i) { lo[i] = values[i]; }
        for (std::size_t i = half; i < N; ++i) { hi[i - half] = values[i]; }
        return pairwise_sum(lo) + pairwise_sum(hi);
    }
}

}  // namespace detail

/**
 * Inner product of two vectors — Σ aᵢbᵢ, the same quantity @ref dot(const NDArray&, const NDArray&)
 * computes, without the allocation. Summed pairwise, so it is both faster and more accurate than a
 * left-to-right accumulation.
 * @tparam T the element type. @tparam N the length.
 * @param a,b the vectors.
 * @return the inner product.
 * @complexity O(N).
 * @alloc none.
 * @test LinalgFixed.DotAndCross
 */
template <ndarray::Field T, std::size_t N>
constexpr T dot(const Vec<T, N>& a, const Vec<T, N>& b) {
    std::array<T, N> products{};
    for (std::size_t i = 0; i < N; ++i) { products[i] = a[i] * b[i]; }
    return detail::pairwise_sum(products);
}

/**
 * Cross product of two 3-vectors — the vector perpendicular to both, right-handed.
 * @tparam T the element type.
 * @param a,b the vectors.
 * @return `a × b`.
 * @complexity O(1).
 * @alloc none.
 * @test LinalgFixed.DotAndCross
 */
template <ndarray::Field T>
constexpr Vec<T, 3> cross(const Vec<T, 3>& a, const Vec<T, 3>& b) {
    return Vec<T, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                     a[0] * b[1] - a[1] * b[0]};
}

/**
 * The squared Euclidean length of a vector — `dot(v, v)`. Prefer it to @ref norm when only comparing
 * lengths: it avoids the square root.
 * @tparam T the element type. @tparam N the length.
 * @param v the vector.
 * @return `Σ vᵢ²`.
 * @complexity O(N).
 * @alloc none.
 * @test LinalgFixed.NormAndNormalize
 */
template <ndarray::Field T, std::size_t N>
constexpr T squared_norm(const Vec<T, N>& v) {
    return dot(v, v);
}

/**
 * The Euclidean length of a vector.
 * @tparam T the element type; floating-point, since the result is a root.
 * @tparam N the length.
 * @param v the vector.
 * @return `sqrt(Σ vᵢ²)`.
 * @complexity O(N).
 * @alloc none.
 * @test LinalgFixed.NormAndNormalize
 */
template <ndarray::FloatingPoint T, std::size_t N>
T norm(const Vec<T, N>& v) {
    return std::sqrt(squared_norm(v));
}

/**
 * The unit vector pointing the same way as @p v.
 * @tparam T the element type; floating-point.
 * @tparam N the length.
 * @param v the vector; must not be the zero vector.
 * @return `v / norm(v)`.
 * @throws std::domain_error when @p v has zero length, since it has no direction.
 * @complexity O(N).
 * @alloc none.
 * @test LinalgFixed.NormAndNormalize
 */
template <ndarray::FloatingPoint T, std::size_t N>
Vec<T, N> normalize(const Vec<T, N>& v) {
    const T squared = squared_norm(v);
    if (squared == T{0}) { throw std::domain_error("linalg::normalize: the zero vector has no direction"); }
    // One reciprocal, then N multiplies. Dividing each component instead costs N divides, and a
    // divide is roughly three times the latency of a multiply.
    const T inverse_length = T{1} / std::sqrt(squared);
    return v * inverse_length;
}

/**
 * The transpose of a matrix — rows become columns.
 * @tparam T the element type. @tparam R the rows. @tparam C the columns.
 * @param m the matrix.
 * @return the `C×R` transpose.
 * @complexity O(R·C).
 * @alloc none.
 * @test LinalgFixed.TransposeAndTrace
 */
template <ndarray::Field T, std::size_t R, std::size_t C>
constexpr Mat<T, C, R> transpose(const Mat<T, R, C>& m) {
    Mat<T, C, R> result;
    for (std::size_t r = 0; r < R; ++r) {
        for (std::size_t c = 0; c < C; ++c) { result(c, r) = m(r, c); }
    }
    return result;
}

/**
 * The trace of a square matrix — the sum of its diagonal.
 * @tparam T the element type. @tparam N the dimension.
 * @param m the matrix.
 * @return `Σ mᵢᵢ`.
 * @complexity O(N).
 * @alloc none.
 * @test LinalgFixed.TransposeAndTrace
 */
template <ndarray::Field T, std::size_t N>
constexpr T trace(const Mat<T, N, N>& m) {
    T sum{};
    for (std::size_t i = 0; i < N; ++i) { sum += m(i, i); }
    return sum;
}

/**
 * Matrix product — the same `A·B` @ref matmul(const NDArray&, const NDArray&) computes, with the
 * shapes checked by the compiler rather than at runtime.
 * @tparam T the element type. @tparam R the rows of @p a. @tparam K the shared dimension.
 * @tparam C the columns of @p b.
 * @param a,b the matrices.
 * @return the `R×C` product.
 * @complexity O(R·K·C).
 * @alloc none.
 * @test LinalgFixed.Matmul
 */
template <ndarray::Field T, std::size_t R, std::size_t K, std::size_t C>
constexpr Mat<T, R, C> matmul(const Mat<T, R, K>& a, const Mat<T, K, C>& b) {
    Mat<T, R, C> result;
    // Column c of the product is Σₖ (column k of a) · b(k, c): a sum of SCALED COLUMNS. With
    // column-major storage each of those columns is contiguous, so the inner loop is a plain vertical
    // multiply-add that vectorizes. The first term SEEDS the column rather than adding to a zeroed
    // one, which spares a store-then-reload of the accumulator.
    for (std::size_t c = 0; c < C; ++c) {
        const T first = b(0, c);
        for (std::size_t r = 0; r < R; ++r) { result(r, c) = first * a(r, 0); }
        for (std::size_t k = 1; k < K; ++k) {
            const T scale = b(k, c);
            for (std::size_t r = 0; r < R; ++r) { result(r, c) += scale * a(r, k); }
        }
    }
    return result;
}

/**
 * Matrix product, spelled `a * b`.
 * @tparam T the element type. @tparam R the rows of @p a. @tparam K the shared dimension.
 * @tparam C the columns of @p b.
 * @param a,b the matrices.
 * @return `matmul(a, b)`.
 * @complexity O(R·K·C).
 * @alloc none.
 * @test LinalgFixed.Matmul
 */
template <ndarray::Field T, std::size_t R, std::size_t K, std::size_t C>
constexpr Mat<T, R, C> operator*(const Mat<T, R, K>& a, const Mat<T, K, C>& b) {
    return matmul(a, b);
}

/**
 * Transform a vector by a matrix — `A·v`, treating @p v as a column.
 * @tparam T the element type. @tparam R the rows. @tparam C the columns, and @p v's length.
 * @param m the matrix. @param v the vector.
 * @return the `R`-vector `m · v`.
 * @complexity O(R·C).
 * @alloc none.
 * @test LinalgFixed.Matmul
 */
template <ndarray::Field T, std::size_t R, std::size_t C>
constexpr Vec<T, R> operator*(const Mat<T, R, C>& m, const Vec<T, C>& v) {
    // `m · v` is Σⱼ (column j of m) · v[j] — a sum of scaled columns, not a stack of row dot
    // products. Column-major storage makes each column contiguous, so this is a vertical
    // multiply-add with no horizontal reduction and no shuffles. The first column seeds the result
    // rather than adding into a zeroed one, which spares a pass over it.
    Vec<T, R> result;
    const T first = v[0];
    for (std::size_t r = 0; r < R; ++r) { result[r] = first * m(r, 0); }
    for (std::size_t c = 1; c < C; ++c) {
        const T scale = v[c];
        for (std::size_t r = 0; r < R; ++r) { result[r] += scale * m(r, c); }
    }
    return result;
}

/**
 * The determinant of a 2×2 matrix, in closed form.
 * @tparam T the element type.
 * @param m the matrix.
 * @return `det(m)`.
 * @complexity O(1).
 * @alloc none.
 * @test LinalgFixed.DeterminantAndInverse
 */
template <ndarray::Field T>
constexpr T determinant(const Mat<T, 2, 2>& m) {
    return m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0);
}

/**
 * The determinant of a 3×3 matrix, by the rule of Sarrus.
 * @tparam T the element type.
 * @param m the matrix.
 * @return `det(m)`.
 * @complexity O(1).
 * @alloc none.
 * @test LinalgFixed.DeterminantAndInverse
 */
template <ndarray::Field T>
constexpr T determinant(const Mat<T, 3, 3>& m) {
    return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
           m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
           m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}

/**
 * The determinant of a 4×4 matrix, by cofactor expansion on 2×2 minors — the form a transform
 * matrix meets, and cheaper than an LU factorization at this size.
 * @tparam T the element type.
 * @param m the matrix.
 * @return `det(m)`.
 * @complexity O(1).
 * @alloc none.
 * @test LinalgFixed.DeterminantAndInverse
 */
template <ndarray::Field T>
constexpr T determinant(const Mat<T, 4, 4>& m) {
    const T s0 = m(0, 0) * m(1, 1) - m(1, 0) * m(0, 1);
    const T s1 = m(0, 0) * m(1, 2) - m(1, 0) * m(0, 2);
    const T s2 = m(0, 0) * m(1, 3) - m(1, 0) * m(0, 3);
    const T s3 = m(0, 1) * m(1, 2) - m(1, 1) * m(0, 2);
    const T s4 = m(0, 1) * m(1, 3) - m(1, 1) * m(0, 3);
    const T s5 = m(0, 2) * m(1, 3) - m(1, 2) * m(0, 3);

    const T c5 = m(2, 2) * m(3, 3) - m(3, 2) * m(2, 3);
    const T c4 = m(2, 1) * m(3, 3) - m(3, 1) * m(2, 3);
    const T c3 = m(2, 1) * m(3, 2) - m(3, 1) * m(2, 2);
    const T c2 = m(2, 0) * m(3, 3) - m(3, 0) * m(2, 3);
    const T c1 = m(2, 0) * m(3, 2) - m(3, 0) * m(2, 2);
    const T c0 = m(2, 0) * m(3, 1) - m(3, 0) * m(2, 1);

    return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
}

/**
 * The inverse of a 2×2 matrix, in closed form.
 * @tparam T the element type; floating-point, since the inverse divides.
 * @param m the matrix; must be non-singular.
 * @return `m⁻¹`.
 * @throws std::domain_error when @p m is singular (zero determinant).
 * @complexity O(1).
 * @alloc none.
 * @test LinalgFixed.DeterminantAndInverse
 */
template <ndarray::FloatingPoint T>
constexpr Mat<T, 2, 2> inverse(const Mat<T, 2, 2>& m) {
    const T det = determinant(m);
    if (det == T{0}) { throw std::domain_error("linalg::inverse: the matrix is singular"); }
    const T inv_det = T{1} / det;
    Mat<T, 2, 2> result;
    result(0, 0) = m(1, 1) * inv_det;
    result(0, 1) = -m(0, 1) * inv_det;
    result(1, 0) = -m(1, 0) * inv_det;
    result(1, 1) = m(0, 0) * inv_det;
    return result;
}

/**
 * The inverse of a 3×3 matrix, by its adjugate — the normal matrix a renderer needs.
 * @tparam T the element type; floating-point.
 * @param m the matrix; must be non-singular.
 * @return `m⁻¹`.
 * @throws std::domain_error when @p m is singular (zero determinant).
 * @complexity O(1).
 * @alloc none.
 * @test LinalgFixed.DeterminantAndInverse
 */
template <ndarray::FloatingPoint T>
constexpr Mat<T, 3, 3> inverse(const Mat<T, 3, 3>& m) {
    const T det = determinant(m);
    if (det == T{0}) { throw std::domain_error("linalg::inverse: the matrix is singular"); }
    const T inv_det = T{1} / det;
    Mat<T, 3, 3> result;
    result(0, 0) = (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) * inv_det;
    result(0, 1) = (m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2)) * inv_det;
    result(0, 2) = (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) * inv_det;
    result(1, 0) = (m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2)) * inv_det;
    result(1, 1) = (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) * inv_det;
    result(1, 2) = (m(0, 2) * m(1, 0) - m(0, 0) * m(1, 2)) * inv_det;
    result(2, 0) = (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0)) * inv_det;
    result(2, 1) = (m(0, 1) * m(2, 0) - m(0, 0) * m(2, 1)) * inv_det;
    result(2, 2) = (m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0)) * inv_det;
    return result;
}

/**
 * The inverse of a 4×4 matrix, by its adjugate over the 2×2 minors — the transform a camera
 * inverts every frame.
 * @tparam T the element type; floating-point.
 * @param m the matrix; must be non-singular.
 * @return `m⁻¹`.
 * @throws std::domain_error when @p m is singular (zero determinant).
 * @complexity O(1).
 * @alloc none.
 * @test LinalgFixed.DeterminantAndInverse
 */
template <ndarray::FloatingPoint T>
constexpr Mat<T, 4, 4> inverse(const Mat<T, 4, 4>& m) {
    const T s0 = m(0, 0) * m(1, 1) - m(1, 0) * m(0, 1);
    const T s1 = m(0, 0) * m(1, 2) - m(1, 0) * m(0, 2);
    const T s2 = m(0, 0) * m(1, 3) - m(1, 0) * m(0, 3);
    const T s3 = m(0, 1) * m(1, 2) - m(1, 1) * m(0, 2);
    const T s4 = m(0, 1) * m(1, 3) - m(1, 1) * m(0, 3);
    const T s5 = m(0, 2) * m(1, 3) - m(1, 2) * m(0, 3);

    const T c5 = m(2, 2) * m(3, 3) - m(3, 2) * m(2, 3);
    const T c4 = m(2, 1) * m(3, 3) - m(3, 1) * m(2, 3);
    const T c3 = m(2, 1) * m(3, 2) - m(3, 1) * m(2, 2);
    const T c2 = m(2, 0) * m(3, 3) - m(3, 0) * m(2, 3);
    const T c1 = m(2, 0) * m(3, 2) - m(3, 0) * m(2, 2);
    const T c0 = m(2, 0) * m(3, 1) - m(3, 0) * m(2, 1);

    const T det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    if (det == T{0}) { throw std::domain_error("linalg::inverse: the matrix is singular"); }
    const T d = T{1} / det;

    Mat<T, 4, 4> r;
    r(0, 0) = (m(1, 1) * c5 - m(1, 2) * c4 + m(1, 3) * c3) * d;
    r(0, 1) = (-m(0, 1) * c5 + m(0, 2) * c4 - m(0, 3) * c3) * d;
    r(0, 2) = (m(3, 1) * s5 - m(3, 2) * s4 + m(3, 3) * s3) * d;
    r(0, 3) = (-m(2, 1) * s5 + m(2, 2) * s4 - m(2, 3) * s3) * d;

    r(1, 0) = (-m(1, 0) * c5 + m(1, 2) * c2 - m(1, 3) * c1) * d;
    r(1, 1) = (m(0, 0) * c5 - m(0, 2) * c2 + m(0, 3) * c1) * d;
    r(1, 2) = (-m(3, 0) * s5 + m(3, 2) * s2 - m(3, 3) * s1) * d;
    r(1, 3) = (m(2, 0) * s5 - m(2, 2) * s2 + m(2, 3) * s1) * d;

    r(2, 0) = (m(1, 0) * c4 - m(1, 1) * c2 + m(1, 3) * c0) * d;
    r(2, 1) = (-m(0, 0) * c4 + m(0, 1) * c2 - m(0, 3) * c0) * d;
    r(2, 2) = (m(3, 0) * s4 - m(3, 1) * s2 + m(3, 3) * s0) * d;
    r(2, 3) = (-m(2, 0) * s4 + m(2, 1) * s2 - m(2, 3) * s0) * d;

    r(3, 0) = (-m(1, 0) * c3 + m(1, 1) * c1 - m(1, 2) * c0) * d;
    r(3, 1) = (m(0, 0) * c3 - m(0, 1) * c1 + m(0, 2) * c0) * d;
    r(3, 2) = (-m(3, 0) * s3 + m(3, 1) * s1 - m(3, 2) * s0) * d;
    r(3, 3) = (m(2, 0) * s3 - m(2, 1) * s1 + m(2, 2) * s0) * d;
    return r;
}

}  // namespace cheatah::linalg
