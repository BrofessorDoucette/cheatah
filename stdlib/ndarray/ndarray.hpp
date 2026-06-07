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
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace cheatah::ndarray {

/**
 * @brief An N-dimensional array of doubles: a view ({shape, strides, offset}) over
 *        a shared element buffer.
 *
 * Copies are cheap and share the buffer; reshape/broadcast produce new views without
 * copying elements. Index math goes through @ref at, which bounds-checks.
 */
class NDArray {
public:
    /**
     * Construct an empty 0-d array with a fresh empty buffer.
     * @complexity O(1).
     * @alloc allocates the empty shared buffer.
     * @test CheatahNDArray.ToStringScalar
     */
    NDArray();
    /**
     * Construct a contiguous array of @p shape filled with @p fill.
     * @param shape the dimensions.
     * @param fill value for every element.
     * @complexity O(size).
     * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>) of
     *   `product(shape)` elements; throws if the shape overflows size_t.
     * @test CheatahNDArray.ShapeFactoriesAndReductions
     */
    explicit NDArray(std::vector<std::size_t> shape, double fill = 0.0);  // contiguous
    /**
     * Construct a view from explicit buffer/shape/strides/offset (used by views).
     * @param data the shared element buffer.
     * @param shape the dimensions.
     * @param strides element strides per dimension.
     * @param offset starting flat offset into @p data.
     * @complexity O(1).
     * @alloc shares @p data, no element copy.
     * @test CheatahNDArray.BroadcastTo
     */
    NDArray(std::shared_ptr<std::vector<double>> data, std::vector<std::size_t> shape,
            std::vector<std::ptrdiff_t> strides, std::size_t offset);

    /**
     * The shape (dimensions).
     * @return reference to the shape vector.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.ShapeFactoriesAndReductions
     */
    const std::vector<std::size_t>& shape() const { return shape_; }
    /**
     * The element strides.
     * @return reference to the strides vector.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastTo
     */
    const std::vector<std::ptrdiff_t>& strides() const { return strides_; }
    /**
     * The number of dimensions (rank).
     * @return `shape().size()`.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastingAdd
     */
    std::size_t ndim() const { return shape_.size(); }
    /**
     * The element count (product of dims; 1 for a 0-d scalar).
     * @return the number of elements.
     * @complexity O(ndim).
     * @alloc none; throws on size overflow.
     * @test CheatahNDArray.ShapeFactoriesAndReductions
     */
    std::size_t size() const;  // product of dims (1 for a 0-d scalar)

    /**
     * Read one element by multi-index (via strides).
     * @param index one coordinate per dimension.
     * @return the element value.
     * @complexity O(ndim).
     * @alloc none; bounds-checks rank and range, throwing on a bad index.
     * @test CheatahNDArray.ShapeFactoriesAndReductions,
     *   CheatahNDArray.RejectsMaliciousShapesAndIndices
     */
    double at(const std::vector<std::size_t>& index) const;  // element via strides
    /**
     * The shared backing buffer.
     * @return reference to the element buffer shared_ptr.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastingAdd
     */
    const std::shared_ptr<std::vector<double>>& buffer() const { return data_; }
    /**
     * The flat offset into the buffer where this view starts.
     * @return the offset.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastTo
     */
    std::size_t offset() const { return offset_; }

private:
    std::shared_ptr<std::vector<double>> data_;
    std::vector<std::size_t> shape_;
    std::vector<std::ptrdiff_t> strides_;  // element strides
    std::size_t offset_ = 0;
};

/**
 * The broadcast result shape of two shapes (NumPy rules).
 * @param a first shape.
 * @param b second shape.
 * @return the broadcast shape (trailing-aligned).
 * @complexity O(max(ndim)).
 * @alloc allocates the small result vector; throws if the shapes are incompatible.
 * @test CheatahNDArray.BroadcastShapeRules
 */
std::vector<std::size_t> broadcast_shapes(const std::vector<std::size_t>& a,
                                          const std::vector<std::size_t>& b);
/**
 * A zero-copy view of @p a stretched to @p target (size-1 / missing dims get stride 0).
 * @param a source array.
 * @param target the shape to stretch to.
 * @return a VIEW sharing @p a's buffer (no element copy).
 * @complexity O(target ndim).
 * @alloc allocates only the small strides vector for the view, not the data;
 *   throws if @p a is not broadcastable to @p target.
 * @test CheatahNDArray.BroadcastTo
 */
NDArray broadcast_to(const NDArray& a, const std::vector<std::size_t>& target);

// ---- factories (shapes arrive from cheatah as list[int]) ----
/**
 * 1-D array from a list of values.
 * @param values the elements.
 * @return a contiguous 1-D NDArray.
 * @complexity O(n).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>).
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 */
NDArray array(const std::vector<double>& values);  // 1-D from a list[float]
/**
 * 0-D scalar array (broadcasts to anything).
 * @param value the single element.
 * @return a 0-d NDArray.
 * @complexity O(1).
 * @alloc allocates a new NDArray buffer holding one element.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast, CheatahNDArray.ToStringScalar
 */
NDArray scalar(double value);                      // 0-D (broadcasts to anything)
/**
 * Array of @p shape filled with 0.
 * @param shape the dimensions (rejects negatives).
 * @return a zero-filled NDArray.
 * @complexity O(size).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws on
 *   negative or overflowing dims.
 * @test CheatahNDArray.ShapeFactoriesAndReductions,
 *   CheatahNDArray.RejectsMaliciousShapesAndIndices
 */
NDArray zeros(const std::vector<long long>& shape);
/**
 * Array of @p shape filled with 1.
 * @param shape the dimensions (rejects negatives).
 * @return a one-filled NDArray.
 * @complexity O(size).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws on
 *   negative or overflowing dims.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 */
NDArray ones(const std::vector<long long>& shape);
/**
 * Array of @p shape filled with @p value.
 * @param shape the dimensions (rejects negatives).
 * @param value the fill value.
 * @return a filled NDArray.
 * @complexity O(size).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws on
 *   negative or overflowing dims.
 * @test CheatahNDArray.RejectsMaliciousShapesAndIndices
 */
NDArray full(const std::vector<long long>& shape, double value);
/**
 * 1-D range `[start, stop)` stepping by @p step (Python `range`-like).
 * @param start first value.
 * @param stop exclusive bound.
 * @param step increment (non-zero).
 * @return a 1-D NDArray of the generated values.
 * @complexity O(count) where count = number of steps.
 * @alloc allocates a new NDArray buffer
 *   (shared_ptr<vector<double>>); throws if @p step is zero.
 * @test CheatahNDArray.Arange
 */
NDArray arange(double start, double stop, double step);
/**
 * Reshape @p a to @p shape (same element count).
 * @param a source array.
 * @param shape the new dimensions (rejects negatives).
 * @return a new contiguous NDArray with the data laid out in C-order.
 * @complexity O(size).
 * @alloc flattens through views and allocates a new NDArray buffer
 *   (shared_ptr<vector<double>>); throws on size mismatch or negative dims.
 * @test CheatahNDArray.BroadcastingAdd, CheatahNDArray.ReshapeSizeMismatchThrows
 */
NDArray reshape(const NDArray& a, const std::vector<long long>& shape);

// ---- element-wise ops (broadcasting) ----
/**
 * Element-wise add with broadcasting.
 * @param a first operand.
 * @param b second operand.
 * @return `a + b` broadcast to their common shape.
 * @complexity O(size of result).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws
 *   if shapes don't broadcast.
 * @test CheatahNDArray.BroadcastingAdd
 */
NDArray add(const NDArray& a, const NDArray& b);
/**
 * Element-wise subtract with broadcasting.
 * @param a first operand.
 * @param b second operand.
 * @return `a - b` broadcast to their common shape.
 * @complexity O(size of result).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws
 *   if shapes don't broadcast.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 */
NDArray sub(const NDArray& a, const NDArray& b);
/**
 * Element-wise multiply with broadcasting.
 * @param a first operand.
 * @param b second operand.
 * @return `a * b` broadcast to their common shape.
 * @complexity O(size of result).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws
 *   if shapes don't broadcast.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 */
NDArray mul(const NDArray& a, const NDArray& b);
/**
 * Element-wise divide with broadcasting.
 * @param a numerator.
 * @param b denominator.
 * @return `a / b` broadcast to their common shape.
 * @complexity O(size of result).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws
 *   if shapes don't broadcast.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 */
NDArray divide(const NDArray& a, const NDArray& b);

// ---- reductions / access / display ----
/**
 * Sum of all elements.
 * @param a the array.
 * @return the total.
 * @complexity O(size).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 */
double sum(const NDArray& a);
/**
 * Mean of all elements (0 for an empty array).
 * @param a the array.
 * @return the average.
 * @complexity O(size).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 */
double mean(const NDArray& a);
/**
 * Read one element by signed multi-index.
 * @param a the array.
 * @param index one coordinate per dimension (rejects negatives).
 * @return the element value.
 * @complexity O(ndim).
 * @alloc none; bounds-checks rank/range and throws on a bad index.
 * @test CheatahNDArray.ShapeFactoriesAndReductions,
 *   CheatahNDArray.RejectsMaliciousShapesAndIndices
 */
double get(const NDArray& a, const std::vector<long long>& index);
/**
 * The shape as signed dims.
 * @param a the array.
 * @return a vector of the dimensions.
 * @complexity O(ndim).
 * @alloc allocates a small vector.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 */
std::vector<long long> shape_of(const NDArray& a);
/**
 * The element count as a signed value.
 * @param a the array.
 * @return the number of elements.
 * @complexity O(ndim).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions, CheatahNDArray.Arange
 */
long long size_of(const NDArray& a);
/**
 * Render as a nested-bracket string, e.g. `"[[1, 2], [3, 4]]"`.
 * @param a the array.
 * @return the textual representation.
 * @complexity O(size).
 * @alloc allocates the result string.
 * @test CheatahNDArray.ToStringScalar, CheatahNDArray.BroadcastingAdd
 */
std::string to_string(const NDArray& a);  // e.g. "[[1, 2], [3, 4]]"

} // namespace cheatah::ndarray
