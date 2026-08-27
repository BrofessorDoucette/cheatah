// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// cheatah-deps: ndarray
#pragma once

/**
 * @file fixarray.hpp
 * @brief cheatah `fixarray` — fixed-extent arrays (@ref cheatah::fixarray::Fixed): exactly like an
 *        @ref cheatah::ndarray::NDArray, only faster.
 *
 * An @ref cheatah::ndarray::NDArray carries its shape at runtime and its elements on the heap, which
 * is what makes it general. When the shape is known at compile time and tiny — a 3-D direction, a
 * 4×4 transform — that generality is the whole cost: a heap allocation, a stride computation and an
 * indirection per operation, to move sixteen floats.
 *
 * @ref cheatah::fixarray::Fixed is the same idea with the shape moved into the type. The extents are
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
 * using namespace cheatah::fixarray;
 * vec3f up{0.0F, 1.0F, 0.0F};        // a 3-vector, 12 bytes, no allocation
 * mat4f m = mat4f::identity();       // a 4x4, 64 bytes — exactly a push constant
 * vec3f v = normalize(cross(up, w)); // numpy's vocabulary, glm's speed
 * ```
 *
 * Rank 1 (a vector) and rank 2 (a matrix) are supported; higher ranks are a mechanical extension of
 * the same storage and are added when a caller needs one.
 *
 * This module is templates only — header-only, nothing is compiled into a library — so the caller's
 * optimization flags apply. The `linalg` module remains the home of the heavy, shape-generic numerics
 * on `NDArray` (LU, QR, SVD, eigen); `Fixed` owns the small closed forms where a general factorization
 * would cost more than the answer.
 *
 * **Performance.** Benchmarked against [GLM](https://github.com/g-truc/glm) over the complete overlap
 * of the two APIs — 160 pairs, every operation, sizes 2/3/4, `float` and `double`, with the outputs
 * verified identical before either is timed — `Fixed` is **faster than or at parity with GLM on every
 * one** (20 faster, 140 at parity, none slower; medians over 9 interleaved repetitions, a win counting
 * only above both 1.15x and 0.25 ns). It wins where structure pays: `mat4f::identity()`
 * 2.69×, `mat4f * mat4f` 1.70×, `mat4f + mat4f` 2.03×, `inverse(mat4d)` 1.38×. No intrinsics — the code is
 * shaped so the compiler vectorizes it. A regression gate (`scripts/bench_gate.sh`) keeps it true.
 * See the @ref performance "Small fixed-size math vs GLM" section for the how and the numbers.
 */

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "ndarray.hpp"

namespace cheatah::fixarray {

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
    /// @complexity O(size).
    /// @alloc none.
    /// @test Fixarray.DefaultIsZero
    constexpr Fixed() = default;

    /**
     * Construct from exactly @ref size elements, written in READING order: a matrix is given row by
     * row, the way it appears on paper, regardless of how it is stored. Arguments are converted to
     * @p T, so a `vec3f` accepts the doubles a cheatah program computes with.
     * @tparam Args the argument types; each must be convertible to @p T.
     * @param args the elements in reading order; exactly @ref size of them.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.MatrixIndexing
     * @crtest FixarrayCompileRun.ConstructAndDot
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
     * @test Fixarray.Identity
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
     * @test Fixarray.Filled
     */
    static constexpr Fixed filled(T value) {
        Fixed result;
        for (std::size_t i = 0; i < size; ++i) { result.data_[i] = value; }
        return result;
    }

    /**
     * Build an array elementwise: each element `i` of the flat, contiguous buffer is `f(i)`. This is
     * the allocation-free, single-pass way to write a component-wise operation — no default zeroing
     * and no separate copy to overwrite, so a call like `abs` or `min` compiles to one vector pass
     * (`minps`/`maxpd`) rather than two. The index `i` runs over the storage order (column-major for
     * a matrix), which is exactly what an elementwise operation wants.
     * @tparam F a callable `T(std::size_t)`.
     * @param f produces element `i` from its flat index.
     * @return the array whose element `i` is `f(i)`.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.FromIndices
     */
    template <class F>
    static constexpr Fixed from_indices(F&& f) {
        return from_indices_impl(std::forward<F>(f), std::make_index_sequence<size>{});
    }

    /**
     * Element @p i of a vector.
     * @param i the index, `0 <= i < size`.
     * @return a reference to the element.
     * @complexity O(1).
     * @alloc none.
     * @test Fixarray.VectorIndexing
     */
    constexpr T& operator[](std::size_t i)
        requires(rank == 1)
    {
        return data_[i];
    }

    /// The same, indexed by a scoped `enum class` column label (see @ref ndarray::Subscript): the one
    /// place an enum is spent as an index, so `v[Axis::Z]` reads column Z while `Axis` stays strong
    /// everywhere else.
    /// @tparam Ix the enum index type.
    /// @param i the element to address, named by an enumerator.
    /// @return a reference to the element.
    /// @complexity O(1). @alloc none.
    /// @test Fixarray.EnumIndexingOnVectorsAndMatrices
    template <::cheatah::ndarray::Subscript Ix>
        requires(rank == 1 && std::is_enum_v<Ix>)
    constexpr T& operator[](Ix i) {
        return data_[static_cast<std::size_t>(::cheatah::ndarray::subscript_index(i))];
    }

    /**
     * Element @p i of a vector (read-only).
     * @param i the index, `0 <= i < size`.
     * @return a const reference to the element.
     * @complexity O(1).
     * @alloc none.
     * @test Fixarray.VectorIndexing
     */
    constexpr const T& operator[](std::size_t i) const
        requires(rank == 1)
    {
        return data_[i];
    }

    /// Read-only element by a scoped `enum class` column label (see @ref ndarray::Subscript).
    /// @tparam Ix the enum index type.
    /// @param i the element to address, named by an enumerator.
    /// @return a const reference to the element.
    /// @complexity O(1). @alloc none.
    /// @test Fixarray.EnumIndexingOnVectorsAndMatrices
    template <::cheatah::ndarray::Subscript Ix>
        requires(rank == 1 && std::is_enum_v<Ix>)
    constexpr const T& operator[](Ix i) const {
        return data_[static_cast<std::size_t>(::cheatah::ndarray::subscript_index(i))];
    }

    /**
     * Element (@p row, @p col) of a matrix. The index is mathematical; the storage is column-major.
     * @param row the row, `0 <= row < rows`.
     * @param col the column, `0 <= col < cols`.
     * @return a reference to the element.
     * @complexity O(1).
     * @alloc none.
     * @test Fixarray.MatrixIndexing
     */
    constexpr T& operator()(std::size_t row, std::size_t col)
        requires(rank == 2)
    {
        return data_[col * rows + row];
    }

    /// The same, with either index a scoped `enum class` label (see @ref ndarray::Subscript) — a named
    /// row or column of a fixed matrix. Mixed integer/enum is allowed; at least one must be an enum, so
    /// the plain `std::size_t` overload still owns the all-integer call.
    /// @tparam R the row index type. @tparam C the column index type; at least one is an enum.
    /// @param row the row to address. @param col the column to address.
    /// @return a reference to the element.
    /// @complexity O(1). @alloc none.
    /// @test Fixarray.EnumIndexingOnVectorsAndMatrices
    template <::cheatah::ndarray::Subscript R, ::cheatah::ndarray::Subscript C>
        requires(rank == 2 && (std::is_enum_v<R> || std::is_enum_v<C>))
    constexpr T& operator()(R row, C col) {
        return (*this)(static_cast<std::size_t>(::cheatah::ndarray::subscript_index(row)),
                       static_cast<std::size_t>(::cheatah::ndarray::subscript_index(col)));
    }

    /**
     * Element (@p row, @p col) of a matrix, read-only. Mathematical index; column-major storage.
     * @param row the row, `0 <= row < rows`.
     * @param col the column, `0 <= col < cols`.
     * @return a const reference to the element.
     * @complexity O(1).
     * @alloc none.
     * @test Fixarray.MatrixIndexing
     */
    constexpr const T& operator()(std::size_t row, std::size_t col) const
        requires(rank == 2)
    {
        return data_[col * rows + row];
    }

    /// Read-only (@p row, @p col) with either index a scoped `enum class` label (see @ref
    /// ndarray::Subscript).
    /// @tparam R the row index type. @tparam C the column index type; at least one is an enum.
    /// @param row the row to address. @param col the column to address.
    /// @return a const reference to the element.
    /// @complexity O(1). @alloc none.
    /// @test Fixarray.EnumIndexingOnVectorsAndMatrices
    template <::cheatah::ndarray::Subscript R, ::cheatah::ndarray::Subscript C>
        requires(rank == 2 && (std::is_enum_v<R> || std::is_enum_v<C>))
    constexpr const T& operator()(R row, C col) const {
        return (*this)(static_cast<std::size_t>(::cheatah::ndarray::subscript_index(row)),
                       static_cast<std::size_t>(::cheatah::ndarray::subscript_index(col)));
    }

    /**
     * A pointer to the elements, contiguous — column-major for a matrix, which is exactly the order a
     * GPU uniform, a push constant or a BLAS call expects, so an upload is a copy not a transpose.
     * @return the first element's address.
     * @complexity O(1).
     * @alloc none.
     * @test Fixarray.Data
     */
    constexpr T* data() { return data_.data(); }

    /**
     * A pointer to the elements, contiguous and column-major for a matrix (read-only).
     * @return the first element's address.
     * @complexity O(1).
     * @alloc none.
     * @test Fixarray.Data
     */
    constexpr const T* data() const { return data_.data(); }

    /**
     * Elementwise equality. Exact, as `==` on the elements is exact — no tolerance is applied to
     * floating-point values.
     * @param other the array to compare with.
     * @return true iff every element matches.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.Equality
     */
    constexpr bool operator==(const Fixed& other) const = default;

    /**
     * Add @p other elementwise, in place.
     * @param other the array to add.
     * @return a reference to this array.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.Arithmetic
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
     * @test Fixarray.Arithmetic
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
     * @test Fixarray.Arithmetic
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
     * @test Fixarray.Arithmetic
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
     * @test Fixarray.Arithmetic
     */
    friend constexpr Fixed operator+(Fixed a, const Fixed& b) { return a += b; }

    /**
     * Elementwise difference.
     * @param a,b the arrays to subtract.
     * @return `a - b`.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.Arithmetic
     */
    friend constexpr Fixed operator-(Fixed a, const Fixed& b) { return a -= b; }

    /**
     * Negation.
     * @param a the array to negate.
     * @return `-a`.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.Arithmetic
     */
    friend constexpr Fixed operator-(Fixed a) { return a *= static_cast<T>(-1); }

    /**
     * Scale by a scalar.
     * @param a the array. @param scalar the factor.
     * @return `a * scalar`.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.Arithmetic
     */
    friend constexpr Fixed operator*(Fixed a, T scalar) { return a *= scalar; }

    /**
     * Scale by a scalar.
     * @param scalar the factor. @param a the array.
     * @return `scalar * a`.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.Arithmetic
     */
    friend constexpr Fixed operator*(T scalar, Fixed a) { return a *= scalar; }

    /**
     * Divide by a scalar.
     * @param a the array. @param scalar the divisor.
     * @return `a / scalar`.
     * @complexity O(size).
     * @alloc none.
     * @test Fixarray.Arithmetic
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
    static constexpr Fixed identity_impl(std::index_sequence<I...> /*unused*/)
        requires(rank == 2 && rows == cols)
    {
        return Fixed(std::array<T, size>{(I % (rows + 1) == 0 ? T{1} : T{0})...});
    }

    /// @ref from_indices's worker: aggregate-initialises the buffer from `f(0) … f(size-1)`, fully
    /// unrolled, so there is no loop, no zeroing, and no pointer through which the operands alias.
    /// @tparam F the element-producing callable.
    /// @tparam I the flat indices 0 … size-1.
    /// @param f produces each element from its flat index.
    /// @return the array of `f(i)`.
    template <class F, std::size_t... I>
    static constexpr Fixed from_indices_impl(F&& f, std::index_sequence<I...> /*unused*/) {  // NOLINT(cppcoreguidelines-missing-std-forward): f is invoked once per index; forwarding inside the pack expansion would move it repeatedly
        return Fixed(std::array<T, size>{static_cast<T>(f(I))...});
    }

    /// The elements, inline: a vector in order, a matrix column by column. Zero by default.
    std::array<T, size> data_{};
};

/// A fixed-extent vector of @p N elements.
/// @tparam T the element type. @tparam N the length.
template <ndarray::Field T, std::size_t N>
using Vec = Fixed<T, N>;

/// A fixed-extent matrix of @p R rows and @p C columns — column-major storage, mathematical
/// `(row, col)` indexing (see the file doc).
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
 * @test Fixarray.DotAndCross
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
 * @test Fixarray.DotAndCross
 */
template <ndarray::Field T, std::size_t N>
constexpr T dot(const Vec<T, N>& a, const Vec<T, N>& b) {
    // Two regimes, split by what the compiler does with the products. Below 4, an odd width — a
    // 3-vector of doubles is 24 bytes — spills a products array to the stack, so the terms are
    // written out and stay in registers. At 4 and above the array is a whole SIMD register (or a
    // clean multiple), and the loop packs the products into one `mulps`/`mulpd` instead of N scalar
    // multiplies — which is how this *beats* a scalar dot rather than merely matching it. Both paths
    // sum pairwise, so the associativity (and the rounding) is identical either way.
    if constexpr (N == 1) {
        return a[0] * b[0];
    } else if constexpr (N == 2) {
        return a[0] * b[0] + a[1] * b[1];
    } else if constexpr (N == 3) {
        return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
    } else {
        std::array<T, N> products{};
        for (std::size_t i = 0; i < N; ++i) { products[i] = a[i] * b[i]; }
        return detail::pairwise_sum(products);
    }
}

/**
 * Cross product of two 3-vectors — the vector perpendicular to both, right-handed.
 * @tparam T the element type.
 * @param a,b the vectors.
 * @return `a × b`.
 * @complexity O(1).
 * @alloc none.
 * @test Fixarray.DotAndCross
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
 * @warning for a complex element this is the bilinear `Σ vᵢ²` (matching @ref dot), not
 *          the Hermitian `Σ |vᵢ|²` — it is the squared Euclidean LENGTH only for real
 *          elements.
 * @test Fixarray.NormAndNormalize
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
 * @test Fixarray.NormAndNormalize
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
 * @test Fixarray.NormAndNormalize
 */
template <ndarray::FloatingPoint T, std::size_t N>
Vec<T, N> normalize(const Vec<T, N>& v) {
    const T squared = squared_norm(v);
    if (squared == T{0}) { throw std::domain_error("fixarray::normalize: the zero vector has no direction"); }
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
 * @test Fixarray.TransposeAndTrace
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
 * @test Fixarray.TransposeAndTrace
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
 * @test Fixarray.Matmul
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
 * @test Fixarray.Matmul
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
 * @test Fixarray.Matmul
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
 * @test Fixarray.DeterminantAndInverse
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
 * @test Fixarray.DeterminantAndInverse
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
 * @test Fixarray.DeterminantAndInverse
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
 * @test Fixarray.DeterminantAndInverse
 */
template <ndarray::FloatingPoint T>
constexpr Mat<T, 2, 2> inverse(const Mat<T, 2, 2>& m) {
    const T det = determinant(m);
    if (det == T{0}) { throw std::domain_error("fixarray::inverse: the matrix is singular"); }
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
 * @test Fixarray.DeterminantAndInverse
 */
template <ndarray::FloatingPoint T>
constexpr Mat<T, 3, 3> inverse(const Mat<T, 3, 3>& m) {
    const T det = determinant(m);
    if (det == T{0}) { throw std::domain_error("fixarray::inverse: the matrix is singular"); }
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
 * @test Fixarray.DeterminantAndInverse
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
    if (det == T{0}) { throw std::domain_error("fixarray::inverse: the matrix is singular"); }
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

// ---- Geometry: the operations a renderer and a physics solver reach for ------------------------
// These are the GLSL/GLM geometric builtins, by their standard names, over @ref Fixed vectors: the
// same mathematics, evaluated in registers with no allocation. They reuse the products above, so a
// change to @ref dot or the operators reaches them too.

/**
 * The Euclidean distance between two points — `norm(a - b)`.
 * @tparam T the element type; floating-point, since the result is a root.
 * @tparam N the dimension.
 * @param a,b the points.
 * @return `‖a − b‖`.
 * @complexity O(N). @alloc none.
 * @test Fixarray.Geometry
 */
template <ndarray::FloatingPoint T, std::size_t N>
T distance(const Vec<T, N>& a, const Vec<T, N>& b) {
    return norm(a - b);
}

/**
 * The squared distance between two points — `squared_norm(a - b)`. Prefer it to @ref distance when
 * only comparing distances: it skips the square root.
 * @tparam T the element type. @tparam N the dimension.
 * @param a,b the points.
 * @return `‖a − b‖²`.
 * @complexity O(N). @alloc none.
 * @test Fixarray.Geometry
 */
template <ndarray::Field T, std::size_t N>
constexpr T distance_squared(const Vec<T, N>& a, const Vec<T, N>& b) {
    return squared_norm(a - b);
}

/**
 * Reflect an incident vector about a surface normal — `I − 2 (N·I) N`, the GLSL `reflect`. @p normal
 * is assumed unit length, as GLSL requires.
 * @tparam T the element type. @tparam N the dimension.
 * @param incident the incoming vector.
 * @param normal the unit surface normal.
 * @return the reflected vector.
 * @complexity O(N). @alloc none.
 * @test Fixarray.Geometry
 */
template <ndarray::Field T, std::size_t N>
constexpr Vec<T, N> reflect(const Vec<T, N>& incident, const Vec<T, N>& normal) {
    return incident - (T{2} * dot(normal, incident)) * normal;
}

/**
 * Refract an incident vector through a surface — the GLSL `refract`. @p incident and @p normal are
 * assumed unit length. On total internal reflection (a negative radicand) the result is the zero
 * vector, exactly as GLSL specifies.
 * @tparam T the element type; floating-point.
 * @tparam N the dimension.
 * @param incident the unit incoming vector.
 * @param normal the unit surface normal.
 * @param eta the ratio of indices of refraction (source over destination).
 * @return the refracted vector, or the zero vector under total internal reflection.
 * @complexity O(N). @alloc none.
 * @test Fixarray.Geometry
 */
template <ndarray::FloatingPoint T, std::size_t N>
Vec<T, N> refract(const Vec<T, N>& incident, const Vec<T, N>& normal, T eta) {
    const T cos_i = dot(normal, incident);
    const T k = T{1} - eta * eta * (T{1} - cos_i * cos_i);
    if (k < T{0}) { return Vec<T, N>{}; }
    return eta * incident - (eta * cos_i + std::sqrt(k)) * normal;
}

/**
 * Orient a normal to face a viewer — the GLSL `faceforward`: return @p n when @p reference points
 * against the incident direction (`dot(reference, incident) < 0`), `-n` otherwise. Used to keep a
 * surface normal on the camera's side.
 * @tparam T the element type. @tparam N the dimension.
 * @param n the normal to orient.
 * @param incident the incident vector.
 * @param reference the reference normal the result is oriented against.
 * @return @p n or `-n`.
 * @complexity O(N). @alloc none.
 * @test Fixarray.Geometry
 */
template <ndarray::Numeric T, std::size_t N>
constexpr Vec<T, N> faceforward(const Vec<T, N>& n, const Vec<T, N>& incident,
                                const Vec<T, N>& reference) {
    return dot(reference, incident) < T{0} ? n : -n;
}

// ---- Component-wise functions: the GLSL/GLM "common" builtins over a whole array ----------------
// Each applies elementwise to every element of a @ref Fixed — a vector or a matrix alike — so a
// renderer clamps a colour, a physics step limits a velocity, and a noise field mixes two samples in
// the same vocabulary. They read and write the flat buffer, so they are correct whatever the storage
// order, and they copy their first argument rather than zero a result and overwrite it.

/**
 * The absolute value of every element.
 * @tparam T the element type; a real number, so `< 0` is meaningful.
 * @tparam Dims the extents.
 * @param x the array.
 * @return `|x|` elementwise.
 * @complexity O(size). @alloc none.
 * @test Fixarray.CommonUnary
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> abs(const Fixed<T, Dims...>& x) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        const T v = x.data()[i];
        return v < T{0} ? -v : v;
    });
}

/**
 * The sign of every element: `-1`, `0`, or `+1`.
 * @tparam T the element type; a real number.
 * @tparam Dims the extents.
 * @param x the array.
 * @return the elementwise sign.
 * @complexity O(size). @alloc none.
 * @test Fixarray.CommonUnary
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> sign(const Fixed<T, Dims...>& x) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        const T v = x.data()[i];
        return static_cast<T>((T{0} < v) - (v < T{0}));
    });
}

/**
 * The smaller of each corresponding pair of elements.
 * @tparam T the element type; a real number.
 * @tparam Dims the extents.
 * @param a,b the arrays.
 * @return `min(aᵢ, bᵢ)` elementwise.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MinMaxClamp
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> min(const Fixed<T, Dims...>& a, const Fixed<T, Dims...>& b) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        const T ai = a.data()[i];
        const T bi = b.data()[i];
        return ai < bi ? ai : bi;  // a branchless min lowers to minps/minpd
    });
}

/**
 * Each element capped at the scalar @p s — `min(xᵢ, s)`.
 * @tparam T the element type; a real number.
 * @tparam Dims the extents.
 * @param x the array. @param s the ceiling applied to every element.
 * @return `min(xᵢ, s)` elementwise.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MinMaxClamp
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> min(const Fixed<T, Dims...>& x, T s) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        const T v = x.data()[i];
        return v < s ? v : s;
    });
}

/**
 * The larger of each corresponding pair of elements.
 * @tparam T the element type; a real number.
 * @tparam Dims the extents.
 * @param a,b the arrays.
 * @return `max(aᵢ, bᵢ)` elementwise.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MinMaxClamp
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> max(const Fixed<T, Dims...>& a, const Fixed<T, Dims...>& b) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        const T ai = a.data()[i];
        const T bi = b.data()[i];
        return ai < bi ? bi : ai;  // branchless max -> maxps/maxpd
    });
}

/**
 * Each element raised to the scalar @p s — `max(xᵢ, s)`.
 * @tparam T the element type; a real number.
 * @tparam Dims the extents.
 * @param x the array. @param s the floor applied to every element.
 * @return `max(xᵢ, s)` elementwise.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MinMaxClamp
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> max(const Fixed<T, Dims...>& x, T s) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        const T v = x.data()[i];
        return v < s ? s : v;
    });
}

/**
 * Constrain every element to `[lo, hi]` — the GLSL `clamp` with scalar bounds, the common case of
 * pinning a colour to `[0, 1]`.
 * @tparam T the element type; a real number.
 * @tparam Dims the extents.
 * @param x the array. @param lo the lower bound. @param hi the upper bound.
 * @return `min(max(xᵢ, lo), hi)` elementwise.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MinMaxClamp
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> clamp(const Fixed<T, Dims...>& x, T lo, T hi) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        const T v = x.data()[i];
        const T low = v < lo ? lo : v;
        return hi < low ? hi : low;  // min(max(v, lo), hi), branchless
    });
}

/**
 * Constrain every element between the corresponding bounds — the GLSL `clamp` with per-element
 * bounds.
 * @tparam T the element type; a real number.
 * @tparam Dims the extents.
 * @param x the array. @param lo the lower bounds. @param hi the upper bounds.
 * @return `min(max(xᵢ, loᵢ), hiᵢ)` elementwise.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MinMaxClamp
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> clamp(const Fixed<T, Dims...>& x, const Fixed<T, Dims...>& lo,
                                  const Fixed<T, Dims...>& hi) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        const T v = x.data()[i];
        const T l = lo.data()[i];
        const T h = hi.data()[i];
        const T low = v < l ? l : v;
        return h < low ? h : low;
    });
}

/**
 * Linear interpolation — the GLSL `mix`: `a (1 − t) + b t`, with a scalar blend @p t (0 gives @p a,
 * 1 gives @p b). Composed from the operators, so it inherits their vectorization.
 * @tparam T the element type; floating-point.
 * @tparam Dims the extents.
 * @param a,b the endpoints. @param t the blend factor.
 * @return the interpolated array.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MixStep
 */
template <ndarray::FloatingPoint T, std::size_t... Dims>
constexpr Fixed<T, Dims...> mix(const Fixed<T, Dims...>& a, const Fixed<T, Dims...>& b, T t) {
    return a * (T{1} - t) + b * t;
}

/**
 * Linear interpolation with a per-element blend — the GLSL `mix` whose factor @p t is an array.
 * @tparam T the element type; floating-point.
 * @tparam Dims the extents.
 * @param a,b the endpoints. @param t the per-element blend factors.
 * @return the interpolated array.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MixStep
 */
template <ndarray::FloatingPoint T, std::size_t... Dims>
constexpr Fixed<T, Dims...> mix(const Fixed<T, Dims...>& a, const Fixed<T, Dims...>& b,
                                const Fixed<T, Dims...>& t) {
    return Fixed<T, Dims...>::from_indices(
        [&](std::size_t i) { return a.data()[i] * (T{1} - t.data()[i]) + b.data()[i] * t.data()[i]; });
}

/**
 * A step at @p edge — the GLSL `step`: `0` where an element is below @p edge, `1` at or above.
 * @tparam T the element type; a real number.
 * @tparam Dims the extents.
 * @param edge the threshold. @param x the array.
 * @return `xᵢ < edge ? 0 : 1` elementwise.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MixStep
 */
template <ndarray::Numeric T, std::size_t... Dims>
constexpr Fixed<T, Dims...> step(T edge, const Fixed<T, Dims...>& x) {
    return Fixed<T, Dims...>::from_indices(
        [&](std::size_t i) { return x.data()[i] < edge ? T{0} : T{1}; });
}

/**
 * A smooth Hermite transition from 0 to 1 across `[edge0, edge1]` — the GLSL `smoothstep`, with
 * everything below @p edge0 giving 0 and everything above @p edge1 giving 1.
 * @tparam T the element type; floating-point.
 * @tparam Dims the extents.
 * @param edge0 the lower edge. @param edge1 the upper edge. @param x the array.
 * @return the smoothstepped array.
 * @complexity O(size). @alloc none.
 * @test Fixarray.MixStep
 */
template <ndarray::FloatingPoint T, std::size_t... Dims>
constexpr Fixed<T, Dims...> smoothstep(T edge0, T edge1, const Fixed<T, Dims...>& x) {
    return Fixed<T, Dims...>::from_indices([&](std::size_t i) {
        T t = (x.data()[i] - edge0) / (edge1 - edge0);
        t = t < T{0} ? T{0} : (T{1} < t ? T{1} : t);  // NOLINT(readability-avoid-nested-conditional-operator): branchless clamp — the Fixed-vs-GLM perf gate measures this (if/else was 1.5-1.7x slower)
        return t * t * (T{3} - T{2} * t);
    });
}

// ---- Matrix builtins that are not the ordinary product -----------------------------------------

/**
 * The elementwise (Hadamard) product — the GLSL `matrixCompMult`. Named apart from `operator*`
 * precisely because `*` is the matrix product; this multiplies corresponding entries.
 * @tparam T the element type. @tparam R the rows. @tparam C the columns.
 * @param a,b the matrices.
 * @return the elementwise product.
 * @complexity O(R·C). @alloc none.
 * @test Fixarray.MatrixExtras
 */
template <ndarray::Field T, std::size_t R, std::size_t C>
constexpr Mat<T, R, C> matrix_comp_mult(const Mat<T, R, C>& a, const Mat<T, R, C>& b) {
    return Mat<T, R, C>::from_indices([&](std::size_t i) { return a.data()[i] * b.data()[i]; });
}

/**
 * The outer product of a column and a row — the GLSL `outerProduct`: an `R×C` matrix whose
 * `(i, j)` entry is `c[i] · r[j]`. A rank-one update, the workhorse of a covariance accumulation.
 * @tparam T the element type. @tparam R the length of @p c (the rows). @tparam C the length of
 *         @p r (the columns).
 * @param c the column vector. @param r the row vector.
 * @return the `R×C` outer product.
 * @complexity O(R·C). @alloc none.
 * @test Fixarray.MatrixExtras
 */
template <ndarray::Field T, std::size_t R, std::size_t C>
constexpr Mat<T, R, C> outer_product(const Vec<T, R>& c, const Vec<T, C>& r) {
    // Column-major flat index k addresses row k%R of column k/R, so element k is c[k%R] * r[k/R].
    return Mat<T, R, C>::from_indices([&](std::size_t k) { return c[k % R] * r[k / R]; });
}

/**
 * The inverse transpose of a matrix — `transpose(inverse(m))`, the GLSL `inverseTranspose`. This is
 * the matrix that carries normals correctly under a non-uniform transform, so lighting stays right.
 * @tparam T the element type; floating-point.
 * @tparam N the dimension.
 * @param m the matrix; must be non-singular.
 * @return `(m⁻¹)ᵀ`.
 * @throws std::domain_error when @p m is singular (via @ref inverse).
 * @complexity O(1) at the fixed sizes. @alloc none.
 * @test Fixarray.MatrixExtras
 */
template <ndarray::FloatingPoint T, std::size_t N>
constexpr Mat<T, N, N> inverse_transpose(const Mat<T, N, N>& m) {
    return transpose(inverse(m));
}

// ---- Named rows and columns: where an enum earns its keep --------------------------------------
// GLM indexes a matrix by column (`m[j]`). These free accessors do the same for a @ref Fixed, and
// take an @ref ndarray::Subscript — a plain integer OR a scoped `enum class` whose ordinal names the
// axis — so a basis vector reads as `column(view, Axis::Forward)` while `Axis` stays a strong type
// everywhere else. This is the same door the index operators open, kept open for the free functions.

/**
 * Extract one row of a matrix as a vector.
 * @tparam T the element type. @tparam R the rows. @tparam C the columns.
 * @tparam Ix the index type: an integer, or a scoped `enum class` naming the row.
 * @param m the matrix. @param i the row, `0 <= i < R`.
 * @return the `C`-vector of that row.
 * @complexity O(C). @alloc none.
 * @test Fixarray.NamedRowsAndColumns
 */
template <ndarray::Field T, std::size_t R, std::size_t C, ::cheatah::ndarray::Subscript Ix>
constexpr Vec<T, C> row(const Mat<T, R, C>& m, Ix i) {
    const auto ri = static_cast<std::size_t>(::cheatah::ndarray::subscript_index(i));
    Vec<T, C> result;
    for (std::size_t c = 0; c < C; ++c) { result[c] = m(ri, c); }
    return result;
}

/**
 * Extract one column of a matrix as a vector — a basis vector of the transform. This is the axis a
 * scoped enum was made to name: `column(view, Axis::Right)`.
 * @tparam T the element type. @tparam R the rows. @tparam C the columns.
 * @tparam Ix the index type: an integer, or a scoped `enum class` naming the column.
 * @param m the matrix. @param j the column, `0 <= j < C`.
 * @return the `R`-vector of that column.
 * @complexity O(R). @alloc none.
 * @test Fixarray.NamedRowsAndColumns
 */
template <ndarray::Field T, std::size_t R, std::size_t C, ::cheatah::ndarray::Subscript Ix>
constexpr Vec<T, R> column(const Mat<T, R, C>& m, Ix j) {
    const auto cj = static_cast<std::size_t>(::cheatah::ndarray::subscript_index(j));
    Vec<T, R> result;
    for (std::size_t r = 0; r < R; ++r) { result[r] = m(r, cj); }
    return result;
}

// ---- display ----
/**
 * Render @p v the way an `NDArray` renders — numpy-style nested brackets, each element through the
 * SHARED scalar formatter (so `i8`/`u8` elements print as NUMBERS, `f32`/`f64` plainly, and a
 * `complex` as `a+bj`). A vector is `[a, b, c]`; a matrix is `[[…], […]]` in reading `(row, column)`
 * order — regardless of the column-major storage. This is what `io.print`/`io.str`/`str()` show.
 * @param v the value to format.
 * @return the bracketed text.
 * @complexity O(@ref Fixed::size).
 * @alloc allocates the result string and a formatting stream per element.
 * @test Fixarray.ToStringMatchesTheNDArrayRendering
 */
template <ndarray::Field T, std::size_t... Dims>
std::string to_string(const Fixed<T, Dims...>& v) {
    using F = Fixed<T, Dims...>;
    std::string out = "[";
    if constexpr (F::rank == 1) {
        for (std::size_t i = 0; i < F::size; ++i) {
            if (i != 0) out += ", ";
            out += ::cheatah::ndarray::detail::format_scalar(v[i]);
        }
    } else {
        for (std::size_t r = 0; r < F::rows; ++r) {
            if (r != 0) out += ", ";
            out += "[";
            for (std::size_t c = 0; c < F::cols; ++c) {
                if (c != 0) out += ", ";
                out += ::cheatah::ndarray::detail::format_scalar(v(r, c));
            }
            out += "]";
        }
    }
    return out + "]";
}

/**
 * Stream @p v (the nested-bracket @ref to_string form), so a `Fixed` is directly Streamable — a
 * cheatah `io.print(v)` / `io.str(v)` finds this by ADL, exactly as it does for an `NDArray` or a
 * primitive.
 * @param os the stream. @param v the value. @return @p os.
 * @complexity O(@ref Fixed::size).
 * @alloc allocates the intermediate string and a formatting stream per element.
 * @test Fixarray.StreamInsertionUsesTheToStringForm
 */
template <ndarray::Field T, std::size_t... Dims>
std::ostream& operator<<(std::ostream& os, const Fixed<T, Dims...>& v) {
    return os << to_string(v);
}

}  // namespace cheatah::fixarray

// cheatah's value-position subscript `v[i]` / `m[i, j]` lowers to builtins::index(obj, i, ...).
// These give it the fixarray meaning: a vector element via operator[], a matrix element via
// operator(row, col). An index may be a scoped-enum column label (ndarray::Subscript), matching the
// NDArray subscript. (The ndarray overloads live beside these; both are found by the qualified call.)
namespace cheatah::builtins {

/** Vector element read `v[i]`. @param v the vector. @param i the index (or enum label). @return the element. @complexity O(1). @alloc none. @test Fixarray.BuiltinsIndexLowersSubscripts */
template <::cheatah::ndarray::Field T, std::size_t... Dims, ::cheatah::ndarray::Subscript Ix>
T index(const ::cheatah::fixarray::Fixed<T, Dims...>& v, Ix i) {
    return v[i];
}

/** Matrix element read `m[i, j]`. @param m the matrix. @param i the row. @param j the column (or enum label). @return the element. @complexity O(1). @alloc none. @test Fixarray.BuiltinsIndexLowersSubscripts */
template <::cheatah::ndarray::Field T, std::size_t... Dims,
          ::cheatah::ndarray::Subscript I, ::cheatah::ndarray::Subscript J>
T index(const ::cheatah::fixarray::Fixed<T, Dims...>& m, I i, J j) {
    return m(i, j);
}

}  // namespace cheatah::builtins
