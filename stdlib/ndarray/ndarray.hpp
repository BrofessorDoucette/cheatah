#pragma once

/**
 * @file ndarray.hpp
 * @brief cheatah `ndarray` — our own numpy-flavored N-dimensional array (of
 *        doubles) with NumPy broadcasting, surfaced as a `NDArray` class plus free
 *        functions (a .purr program writes `ndarray.zeros([2, 3])`).
 *        See https://numpy.org/doc/stable/user/basics.broadcasting.html.
 *
 * `import ndarray` includes this header and links `libcheatah_ndarray`. Unit tests:
 * `stdlib/tests/ndarray_test.cpp`; the suite runs under AddressSanitizer (the `asan`
 * preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * @note Design (the "pointers + a bit of thinking"): the elements live in a shared
 *       buffer (`std::shared_ptr<std::vector<double>>`) and an array is a VIEW into
 *       it — {shape, strides, offset}. That makes reshape and **broadcast**
 *       zero-copy: to stretch a dimension of size 1 we give it a stride of 0, so
 *       every index along it reads the same element. Shared ownership = memory-safe,
 *       no manual frees. `size` below is the element count (product of dims).
 */
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <execution>
#include <initializer_list>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace cheatah::ndarray {

/// Numeric<T>: an arithmetic element type an ndarray can store (int or float
/// family). Storage, construction, and elementwise +-* require only this.
template <typename T>
concept Numeric = std::is_arithmetic_v<T>;
/// FloatingPoint<T>: a real floating type. The linalg decompositions (solve, inv,
/// det, svd, eig) need division/√, so they constrain to this — calling them on an
/// integer array fails with a clear "FloatingPoint not satisfied", not template spam.
template <typename T>
concept FloatingPoint = std::floating_point<T>;

namespace detail {
/// C-order (row-major) strides for a shape.
inline std::vector<std::ptrdiff_t> contiguous_strides(const std::vector<std::size_t>& shape) {
    std::vector<std::ptrdiff_t> s(shape.size());
    std::ptrdiff_t step = 1;
    for (std::size_t i = shape.size(); i-- > 0;) {
        s[i] = step;
        step *= static_cast<std::ptrdiff_t>(shape[i]);
    }
    return s;
}
/// Overflow-checked product of the dimensions (a wrapped size_t would under-allocate,
/// turning later element access into out-of-bounds writes — reject it up front).
inline std::size_t product(const std::vector<std::size_t>& shape) {
    std::size_t t = 1;
    for (std::size_t d : shape) {
        if (d != 0 && t > std::numeric_limits<std::size_t>::max() / d) {
            throw std::runtime_error("ndarray: shape too large (size overflow)");
        }
        t *= d;
    }
    return t;
}
/// Convert signed dims/indices to sizes, rejecting negatives (a negative cast to
/// size_t becomes huge -> under-allocation / OOB). Validate at the boundary.
inline std::vector<std::size_t> to_size(const std::vector<long long>& v) {
    std::vector<std::size_t> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] < 0) throw std::runtime_error("ndarray: negative dimension or index");
        out[i] = static_cast<std::size_t>(v[i]);
    }
    return out;
}
/// Advance a C-order multi-index odometer; false when it wraps past the end.
inline bool next_index(std::vector<std::size_t>& idx, const std::vector<std::size_t>& shape) {
    for (std::size_t i = shape.size(); i-- > 0;) {
        if (++idx[i] < shape[i]) return true;
        idx[i] = 0;
    }
    return false;
}
}  // namespace detail

/**
 * @brief An N-dimensional array of `T` (a Numeric element type): a view
 *        ({shape, strides, offset}) over a shared element buffer.
 *
 * Copies are cheap and share the buffer; reshape/broadcast produce new views without
 * copying elements. Index math goes through @ref at, which bounds-checks. The element
 * type is deduced from the data (e.g. `array([1,2,3])` is integer, `array([1.0,…])`
 * is double); `NDArray` is the default `basic_ndarray<double>`.
 */
template <Numeric T>
class basic_ndarray {
public:
    using value_type = T;  ///< The stored element type (the `Numeric` `T`).
    /**
     * Construct an empty 0-d array with a fresh empty buffer.
     *
     * Leaves shape and strides empty and the buffer holding no elements; note this
     * is distinct from a 0-d scalar (see @ref scalar), whose buffer holds one element.
     * @complexity O(1).
     * @alloc allocates the empty shared buffer.
     * @test CheatahNDArray.ToStringScalar
     * @systest StdlibE2E.Ndarray
     */
    basic_ndarray() : data_(std::make_shared<std::vector<T>>()) {}
    /**
     * Construct a contiguous array of @p shape filled with @p fill.
     *
     * Allocates a fresh buffer of `product(shape)` doubles all set to @p fill and
     * computes C-order (row-major) strides; the dimension product is overflow-checked.
     * @param shape the dimensions.
     * @param fill value for every element.
     * @complexity O(size).
     * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>) of
     *   `product(shape)` elements; throws if the shape overflows size_t.
     * @test CheatahNDArray.ShapeFactoriesAndReductions
     * @systest StdlibE2E.Ndarray
     */
    explicit basic_ndarray(std::vector<std::size_t> shape, T fill = T{})  // contiguous
        : data_(std::make_shared<std::vector<T>>()), shape_(std::move(shape)) {
        data_->assign(detail::product(shape_), fill);
        strides_ = detail::contiguous_strides(shape_);
    }
    /**
     * Construct a view from explicit buffer/shape/strides/offset (used by views).
     *
     * Stores the supplied members verbatim with no validation or copy, so the new
     * array shares ownership of @p data; callers (e.g. @ref broadcast_to) are
     * responsible for passing strides/offset that stay within the buffer.
     * @param data the shared element buffer.
     * @param shape the dimensions.
     * @param strides element strides per dimension.
     * @param offset starting flat offset into @p data.
     * @complexity O(1).
     * @alloc shares @p data, no element copy.
     * @test CheatahNDArray.BroadcastTo
     * @systest StdlibE2E.Ndarray
     */
    basic_ndarray(std::shared_ptr<std::vector<T>> data, std::vector<std::size_t> shape,
                  std::vector<std::ptrdiff_t> strides, std::size_t offset)
        : data_(std::move(data)), shape_(std::move(shape)), strides_(std::move(strides)),
          offset_(offset) {}

    /**
     * The shape (dimensions).
     * @return reference to the shape vector.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.ShapeFactoriesAndReductions
     * @systest StdlibE2E.Ndarray
     */
    const std::vector<std::size_t>& shape() const { return shape_; }
    /**
     * The element strides.
     * @return reference to the strides vector.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastTo
     * @systest StdlibE2E.Ndarray
     */
    const std::vector<std::ptrdiff_t>& strides() const { return strides_; }
    /**
     * The number of dimensions (rank).
     * @return `shape().size()`.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastingAdd
     * @systest StdlibE2E.Ndarray
     */
    std::size_t ndim() const { return shape_.size(); }
    /**
     * The element count (product of dims; 1 for a 0-d scalar).
     *
     * Recomputes the overflow-checked product of the shape on each call rather than
     * caching it; an empty (no-dimension) shape yields 1.
     * @return the number of elements.
     * @complexity O(ndim).
     * @alloc none; throws on size overflow.
     * @test CheatahNDArray.ShapeFactoriesAndReductions
     * @systest StdlibE2E.Ndarray
     */
    std::size_t size() const { return detail::product(shape_); }  // 1 for a 0-d scalar

    /**
     * Read one element by multi-index (via strides).
     *
     * Computes the flat buffer position as `offset + sum(index[i] * strides[i])`, so
     * it correctly resolves views (including broadcast dims with stride 0); throws if
     * @p index has the wrong rank or any coordinate is out of range for its dimension.
     * @param index one coordinate per dimension.
     * @return the element value.
     * @complexity O(ndim).
     * @alloc none; bounds-checks rank and range, throwing on a bad index.
     * @test CheatahNDArray.ShapeFactoriesAndReductions,
     *   CheatahNDArray.RejectsMaliciousShapesAndIndices
     * @systest StdlibE2E.Ndarray
     */
    T at(const std::vector<std::size_t>& index) const {  // element via strides
        // Bounds-check: a wrong-rank or out-of-range index would otherwise compute an
        // offset outside the backing buffer (out-of-bounds read).
        if (index.size() != shape_.size()) {
            throw std::runtime_error("ndarray: index has the wrong number of dimensions");
        }
        std::ptrdiff_t off = static_cast<std::ptrdiff_t>(offset_);
        for (std::size_t i = 0; i < index.size(); ++i) {
            if (index[i] >= shape_[i]) throw std::runtime_error("ndarray: index out of range");
            off += static_cast<std::ptrdiff_t>(index[i]) * strides_[i];
        }
        return (*data_)[static_cast<std::size_t>(off)];
    }
    /**
     * The shared backing buffer.
     * @return reference to the element buffer shared_ptr.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastingAdd
     * @systest StdlibE2E.Ndarray
     */
    const std::shared_ptr<std::vector<T>>& buffer() const { return data_; }
    /**
     * The flat offset into the buffer where this view starts.
     * @return the offset.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastTo
     * @systest StdlibE2E.Ndarray
     */
    std::size_t offset() const { return offset_; }

    /**
     * Python-style text rendering, e.g. `"[[1, 2], [3, 4]]"` — the `str()` hook that
     * makes an NDArray printable via `io.print` (io's `HasStr` protocol). Defers to
     * the free `to_string`; defined out-of-line below, where `to_string` is declared.
     * @return the array formatted as nested brackets.
     * @complexity O(n) in the element count.
     * @alloc allocates the result string.
     * @test CheatahNDArray.ToString
     * @systest StdlibE2E.Ndarray
     */
    std::string str() const;

private:
    std::shared_ptr<std::vector<T>> data_;
    std::vector<std::size_t> shape_;
    std::vector<std::ptrdiff_t> strides_;  // element strides
    std::size_t offset_ = 0;
};

/// The default ndarray element type is `double` — `NDArray` names that
/// instantiation (the std::string ↔ std::basic_string<char> pattern), so existing
/// code and the linalg routines keep working unchanged.
using NDArray = basic_ndarray<double>;

/**
 * The broadcast result shape of two shapes (NumPy rules).
 *
 * Aligns the shapes from the trailing (rightmost) dimension, treating missing
 * leading dims as 1; each output dim is the non-1 input dim, and two unequal dims
 * that are both not 1 are incompatible and throw.
 * @param a first shape.
 * @param b second shape.
 * @return the broadcast shape (trailing-aligned).
 * @complexity O(max(ndim)).
 * @alloc allocates the small result vector; throws if the shapes are incompatible.
 * @test CheatahNDArray.BroadcastShapeRules
 * @systest StdlibE2E.Ndarray
 */
std::vector<std::size_t> broadcast_shapes(const std::vector<std::size_t>& a,
                                          const std::vector<std::size_t>& b);

// ==========================================================================
//  Templated free functions. The element type T is deduced from the data
//  (array([1,2,3]) -> long long, array([1.0,…]) -> double); every op is
//  constrained by Numeric. Elementwise ops vectorize via the std::execution
//  policies (declarative SIMD); broadcasting/strided views fall back to a
//  C-order scalar walk. NDArray (= basic_ndarray<double>) is the default.
// ==========================================================================

/**
 * Whether @p a is a contiguous C-order block (no broadcast/stride-0/permuted view),
 * so its elements live consecutively from `offset()` and can be walked flatly.
 * @param a the array (or view) to test.
 * @return true if @p a's strides are the C-order strides for its shape.
 * @complexity O(ndim).
 * @alloc none.
 * @test CheatahNDArray.BroadcastingAdd
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
inline bool is_contiguous(const basic_ndarray<T>& a) {
    return a.strides() == detail::contiguous_strides(a.shape());
}

/**
 * A zero-copy view of @p a stretched to @p target (size-1 / missing dims get stride 0).
 * @param a source array.
 * @param target the shape to stretch to.
 * @return a VIEW sharing @p a's buffer (no element copy).
 * @test CheatahNDArray.BroadcastTo
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> broadcast_to(const basic_ndarray<T>& a, const std::vector<std::size_t>& target) {
    const std::size_t n = target.size();
    if (a.ndim() > n) throw std::runtime_error("ndarray: cannot broadcast to fewer dimensions");
    std::vector<std::ptrdiff_t> ns(n, 0);  // stretched / missing dims -> stride 0
    const std::size_t pad = n - a.ndim();
    for (std::size_t i = 0; i < a.ndim(); ++i) {
        const std::size_t adim = a.shape()[i];
        if (adim == target[pad + i]) {
            ns[pad + i] = a.strides()[i];
        } else if (adim != 1) {
            throw std::runtime_error("ndarray: shape not broadcastable to target");
        }  // adim == 1 -> stride stays 0 (stretch)
    }
    return basic_ndarray<T>(a.buffer(), target, ns, a.offset());
}

// ---- factories (shapes arrive from cheatah as list[int]) ----
/**
 * 1-D array from a list of values; the element type is the list's element type
 * (`array([1,2,3])` is integer, `array([1.0,…])` is double).
 * @param values the elements, copied into a fresh contiguous buffer.
 * @return a contiguous 1-D `basic_ndarray<T>`.
 * @complexity O(n).
 * @alloc allocates a new buffer of `values.size()` elements.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Array
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> array(const std::vector<T>& values) {
    basic_ndarray<T> a(std::vector<std::size_t>{values.size()});
    *a.buffer() = values;
    return a;
}
/**
 * `array({1, 2, 3})` — braced-list overload (deduces T from the initializer_list,
 * which the `std::vector<T>` overload can't do directly).
 * @param values the elements as a braced list.
 * @return a contiguous 1-D `basic_ndarray<T>`.
 * @complexity O(n).
 * @alloc allocates a new buffer.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> array(std::initializer_list<T> values) {
    return array(std::vector<T>(values));
}
/**
 * 0-D scalar array (broadcasts to anything); element type deduced from @p value.
 * @param value the single element.
 * @return a 0-d `basic_ndarray<T>`.
 * @complexity O(1).
 * @alloc allocates a one-element buffer.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 * @crtest NdarrayCompileRun.Scalar
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> scalar(T value) {
    basic_ndarray<T> a;  // 0-d
    a.buffer()->assign(1, value);
    return a;
}
/**
 * Array of @p shape filled with 0 (a `double` array by default; rejects negatives).
 * @param shape the dimensions (signed; throws on a negative).
 * @return a zero-filled `NDArray`.
 * @complexity O(size).
 * @alloc allocates a new buffer; throws on negative/overflowing dims.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Zeros
 * @systest StdlibE2E.Ndarray
 */
inline NDArray zeros(const std::vector<long long>& shape) {
    return NDArray(detail::to_size(shape), 0.0);
}
/**
 * Array of @p shape filled with 1 (a `double` array by default; rejects negatives).
 * @param shape the dimensions (signed; throws on a negative).
 * @return a one-filled `NDArray`.
 * @complexity O(size).
 * @alloc allocates a new buffer; throws on negative/overflowing dims.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Ones
 * @systest StdlibE2E.Ndarray
 */
inline NDArray ones(const std::vector<long long>& shape) {
    return NDArray(detail::to_size(shape), 1.0);
}
/**
 * Array of @p shape filled with @p value; element type deduced from @p value.
 * @param shape the dimensions (signed; throws on a negative).
 * @param value the fill value (its type is the array's element type).
 * @return a filled `basic_ndarray<T>`.
 * @complexity O(size).
 * @alloc allocates a new buffer; throws on negative/overflowing dims.
 * @test CheatahNDArray.RejectsMaliciousShapesAndIndices
 * @crtest NdarrayCompileRun.Full
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> full(const std::vector<long long>& shape, T value) {
    return basic_ndarray<T>(detail::to_size(shape), value);
}
/**
 * 1-D range `[start, stop)` stepping by @p step; element type deduced from the args.
 * @param start first value.
 * @param stop exclusive bound.
 * @param step increment (throws if zero); a step pointing away from @p stop yields empty.
 * @return a 1-D `basic_ndarray<T>` of the generated values.
 * @complexity O(count).
 * @alloc allocates a new buffer; throws if @p step is zero.
 * @test CheatahNDArray.Arange
 * @crtest NdarrayCompileRun.Arange
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> arange(T start, T stop, T step) {
    if (step == T{}) throw std::runtime_error("ndarray: arange step must be non-zero");
    std::vector<T> v;
    for (T x = start; (step > T{}) ? (x < stop) : (x > stop); x += step) v.push_back(x);
    return array(v);
}
/**
 * Reshape @p a to @p shape (same element count); reads in C-order so views/broadcasts
 * are flattened into a fresh contiguous buffer (a copy, not an alias).
 * @param a source array.
 * @param shape the new dimensions (signed; throws on a negative).
 * @return a new contiguous `basic_ndarray<T>` with the data in C-order.
 * @complexity O(size).
 * @alloc allocates a new buffer; throws on size mismatch or negative dims.
 * @test CheatahNDArray.BroadcastingAdd, CheatahNDArray.ReshapeSizeMismatchThrows
 * @crtest NdarrayCompileRun.Reshape
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> reshape(const basic_ndarray<T>& a, const std::vector<long long>& shape) {
    const std::vector<std::size_t> ns = detail::to_size(shape);
    if (detail::product(ns) != a.size()) {
        throw std::runtime_error("ndarray: cannot reshape, size mismatch");
    }
    basic_ndarray<T> out(ns);
    auto& buf = *out.buffer();
    std::vector<std::size_t> idx(a.ndim(), 0);
    std::size_t flat = 0;
    do {
        buf[flat++] = a.at(idx);
    } while (a.ndim() != 0 && detail::next_index(idx, a.shape()));
    return out;
}

// ---- element-wise ops (broadcasting, vectorized) ----
/**
 * Broadcast @p a and @p b to their common shape and apply @p op elementwise into a
 * fresh contiguous result. Fast path: when both operands are contiguous, a flat
 * `std::transform` under the `unseq` policy (SIMD); otherwise a C-order scalar walk.
 * @param a first operand.
 * @param b second operand.
 * @param op the binary operation applied to corresponding elements.
 * @return `op(a, b)` broadcast to the common shape; throws if shapes don't broadcast.
 * @complexity O(size of result).
 * @alloc allocates the result buffer.
 * @test CheatahNDArray.BroadcastingAdd
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T, typename Op>
basic_ndarray<T> binary_op(const basic_ndarray<T>& a, const basic_ndarray<T>& b, Op op) {
    const std::vector<std::size_t> rshape = broadcast_shapes(a.shape(), b.shape());
    const basic_ndarray<T> av = broadcast_to(a, rshape);
    const basic_ndarray<T> bv = broadcast_to(b, rshape);
    basic_ndarray<T> out(rshape);
    auto& obuf = *out.buffer();
    if (is_contiguous(av) && is_contiguous(bv)) {
        const auto& abuf = *av.buffer();
        const auto& bbuf = *bv.buffer();
        std::transform(std::execution::unseq, abuf.begin() + av.offset(),
                       abuf.begin() + av.offset() + obuf.size(), bbuf.begin() + bv.offset(),
                       obuf.begin(), op);
        return out;
    }
    std::vector<std::size_t> idx(rshape.size(), 0);
    std::size_t flat = 0;
    do {
        obuf[flat++] = op(av.at(idx), bv.at(idx));
    } while (!rshape.empty() && detail::next_index(idx, rshape));
    return out;
}
/**
 * Element-wise `a + b` with broadcasting.
 * @param a first operand.
 * @param b second operand.
 * @return `a + b` broadcast to the common shape.
 * @complexity O(size of result). @alloc allocates the result.
 * @test CheatahNDArray.BroadcastingAdd
 * @crtest NdarrayCompileRun.Add
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> add(const basic_ndarray<T>& a, const basic_ndarray<T>& b) {
    return binary_op(a, b, [](T x, T y) { return x + y; });
}
/**
 * Element-wise `a - b` with broadcasting.
 * @param a first operand.
 * @param b second operand.
 * @return `a - b` broadcast to the common shape.
 * @complexity O(size of result). @alloc allocates the result.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 * @crtest NdarrayCompileRun.Sub
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> sub(const basic_ndarray<T>& a, const basic_ndarray<T>& b) {
    return binary_op(a, b, [](T x, T y) { return x - y; });
}
/**
 * Element-wise `a * b` with broadcasting.
 * @param a first operand.
 * @param b second operand.
 * @return `a * b` broadcast to the common shape.
 * @complexity O(size of result). @alloc allocates the result.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 * @crtest NdarrayCompileRun.Mul
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> mul(const basic_ndarray<T>& a, const basic_ndarray<T>& b) {
    return binary_op(a, b, [](T x, T y) { return x * y; });
}
/**
 * Element-wise `a / b` with broadcasting (an integer element type does integer division).
 * @param a numerator.
 * @param b denominator (float division follows IEEE-754: /0 yields inf/nan, no throw).
 * @return `a / b` broadcast to the common shape.
 * @complexity O(size of result). @alloc allocates the result.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 * @crtest NdarrayCompileRun.Divide
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
basic_ndarray<T> divide(const basic_ndarray<T>& a, const basic_ndarray<T>& b) {
    return binary_op(a, b, [](T x, T y) { return x / y; });
}

// ---- reductions / access / display ----
/**
 * Sum of all elements — a full reduction across every axis (vectorized via
 * `std::reduce(unseq)` when contiguous, else a C-order walk); empty sums to 0.
 * @param a the array.
 * @return the total, as the element type @p T.
 * @complexity O(size).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Sum
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
T sum(const basic_ndarray<T>& a) {
    if (is_contiguous(a)) {
        const auto& buf = *a.buffer();
        return std::reduce(std::execution::unseq, buf.begin() + a.offset(),
                           buf.begin() + a.offset() + a.size(), T{});
    }
    T s{};
    std::vector<std::size_t> idx(a.ndim(), 0);
    do {
        s += a.at(idx);
    } while (a.ndim() != 0 && detail::next_index(idx, a.shape()));
    return s;
}
/**
 * Mean of all elements, always as a `double` (0.0 for an empty array — no divide-by-zero).
 * @param a the array.
 * @return the average as a double.
 * @complexity O(size).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Mean
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
double mean(const basic_ndarray<T>& a) {
    const std::size_t n = a.size();
    return n == 0 ? 0.0 : static_cast<double>(sum(a)) / static_cast<double>(n);
}
/**
 * Read one element by signed multi-index (the cheatah-facing wrapper over @ref
 * basic_ndarray::at; rejects negative coordinates).
 * @param a the array.
 * @param index one coordinate per dimension (signed; throws on a negative).
 * @return the element value (type @p T); throws on a wrong-rank/out-of-range index.
 * @complexity O(ndim).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Get
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
T get(const basic_ndarray<T>& a, const std::vector<long long>& index) {
    return a.at(detail::to_size(index));
}
/**
 * The shape as signed dims (cheatah integers are signed; a 0-d array yields an empty list).
 * @param a the array.
 * @return the dimensions as a `long long` vector.
 * @complexity O(ndim).
 * @alloc allocates the result vector.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.ShapeOf
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
std::vector<long long> shape_of(const basic_ndarray<T>& a) {
    std::vector<long long> out(a.ndim());
    for (std::size_t i = 0; i < a.ndim(); ++i) out[i] = static_cast<long long>(a.shape()[i]);
    return out;
}
/**
 * The element count as a signed value (1 for a 0-d array).
 * @param a the array.
 * @return the number of elements as a `long long`.
 * @complexity O(ndim).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.SizeOf
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
long long size_of(const basic_ndarray<T>& a) {
    return static_cast<long long>(a.size());
}

namespace detail {
/// Recursively format @p a into nested brackets (each element via `operator<<`).
template <Numeric T>
void format_rec(const basic_ndarray<T>& a, std::vector<std::size_t>& idx, std::size_t dim,
                std::string& out) {
    if (dim == a.ndim()) {
        std::ostringstream os;
        os << a.at(idx);
        out += os.str();
        return;
    }
    out += "[";
    for (std::size_t i = 0; i < a.shape()[dim]; ++i) {
        if (i != 0) out += ", ";
        idx[dim] = i;
        format_rec(a, idx, dim + 1, out);
    }
    out += "]";
}
}  // namespace detail

/**
 * Render as a nested-bracket string, e.g. `"[[1, 2], [3, 4]]"` (a 0-d scalar renders
 * as the bare number). Each element is formatted with the default `ostream` precision.
 * @param a the array.
 * @return the textual representation.
 * @complexity O(size).
 * @alloc allocates the result string.
 * @test CheatahNDArray.ToStringScalar, CheatahNDArray.BroadcastingAdd
 * @crtest NdarrayCompileRun.ToString
 * @systest StdlibE2E.Ndarray
 */
template <Numeric T>
std::string to_string(const basic_ndarray<T>& a) {
    if (a.ndim() == 0) {
        std::ostringstream os;
        os << a.at({});
        return os.str();
    }
    std::vector<std::size_t> idx(a.ndim(), 0);
    std::string out;
    detail::format_rec(a, idx, 0, out);
    return out;
}

template <Numeric T>
inline std::string basic_ndarray<T>::str() const {
    return to_string(*this);
}

} // namespace cheatah::ndarray
