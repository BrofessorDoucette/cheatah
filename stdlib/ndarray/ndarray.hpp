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
     *
     * Leaves shape and strides empty and the buffer holding no elements; note this
     * is distinct from a 0-d scalar (see @ref scalar), whose buffer holds one element.
     * @complexity O(1).
     * @alloc allocates the empty shared buffer.
     * @test CheatahNDArray.ToStringScalar
     * @systest StdlibE2E.Ndarray
     */
    NDArray();
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
    explicit NDArray(std::vector<std::size_t> shape, double fill = 0.0);  // contiguous
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
    NDArray(std::shared_ptr<std::vector<double>> data, std::vector<std::size_t> shape,
            std::vector<std::ptrdiff_t> strides, std::size_t offset);

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
    std::size_t size() const;  // product of dims (1 for a 0-d scalar)

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
    double at(const std::vector<std::size_t>& index) const;  // element via strides
    /**
     * The shared backing buffer.
     * @return reference to the element buffer shared_ptr.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastingAdd
     * @systest StdlibE2E.Ndarray
     */
    const std::shared_ptr<std::vector<double>>& buffer() const { return data_; }
    /**
     * The flat offset into the buffer where this view starts.
     * @return the offset.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahNDArray.BroadcastTo
     * @systest StdlibE2E.Ndarray
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
/**
 * A zero-copy view of @p a stretched to @p target (size-1 / missing dims get stride 0).
 *
 * Right-aligns @p a's dims under @p target, keeping the original stride where dims
 * match and using stride 0 where a size-1 (or missing leading) dim is stretched;
 * throws if @p a has more dims than @p target or a non-1 dim doesn't match the target.
 * @param a source array.
 * @param target the shape to stretch to.
 * @return a VIEW sharing @p a's buffer (no element copy).
 * @complexity O(target ndim).
 * @alloc allocates only the small strides vector for the view, not the data;
 *   throws if @p a is not broadcastable to @p target.
 * @test CheatahNDArray.BroadcastTo
 * @systest StdlibE2E.Ndarray
 */
NDArray broadcast_to(const NDArray& a, const std::vector<std::size_t>& target);

// ---- factories (shapes arrive from cheatah as list[int]) ----
/**
 * 1-D array from a list of values.
 *
 * Creates a contiguous array of shape `[values.size()]` and copies the values into
 * its buffer in order; an empty list yields a 1-D array of length 0.
 * @param values the elements.
 * @return a contiguous 1-D NDArray.
 * @complexity O(n).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>).
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Array
 * @systest StdlibE2E.Ndarray
 */
NDArray array(const std::vector<double>& values);  // 1-D from a list[float]
/**
 * 0-D scalar array (broadcasts to anything).
 *
 * Produces an empty-shape array whose buffer holds the single @p value; because it
 * has no dimensions it broadcasts against any shape under NumPy rules.
 * @param value the single element.
 * @return a 0-d NDArray.
 * @complexity O(1).
 * @alloc allocates a new NDArray buffer holding one element.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast, CheatahNDArray.ToStringScalar
 * @crtest NdarrayCompileRun.Scalar
 * @systest StdlibE2E.Ndarray
 */
NDArray scalar(double value);                      // 0-D (broadcasts to anything)
/**
 * Array of @p shape filled with 0.
 *
 * Converts the signed dims to sizes (throwing on any negative) and builds a fresh
 * contiguous array filled with 0.0.
 * @param shape the dimensions (rejects negatives).
 * @return a zero-filled NDArray.
 * @complexity O(size).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws on
 *   negative or overflowing dims.
 * @test CheatahNDArray.ShapeFactoriesAndReductions,
 *   CheatahNDArray.RejectsMaliciousShapesAndIndices
 * @crtest NdarrayCompileRun.Zeros
 * @systest StdlibE2E.Ndarray
 */
NDArray zeros(const std::vector<long long>& shape);
/**
 * Array of @p shape filled with 1.
 *
 * Converts the signed dims to sizes (throwing on any negative) and builds a fresh
 * contiguous array filled with 1.0.
 * @param shape the dimensions (rejects negatives).
 * @return a one-filled NDArray.
 * @complexity O(size).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws on
 *   negative or overflowing dims.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Ones
 * @systest StdlibE2E.Ndarray
 */
NDArray ones(const std::vector<long long>& shape);
/**
 * Array of @p shape filled with @p value.
 *
 * Converts the signed dims to sizes (throwing on any negative) and builds a fresh
 * contiguous array with every element set to @p value.
 * @param shape the dimensions (rejects negatives).
 * @param value the fill value.
 * @return a filled NDArray.
 * @complexity O(size).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws on
 *   negative or overflowing dims.
 * @test CheatahNDArray.RejectsMaliciousShapesAndIndices
 * @crtest NdarrayCompileRun.Full
 * @systest StdlibE2E.Ndarray
 */
NDArray full(const std::vector<long long>& shape, double value);
/**
 * 1-D range `[start, stop)` stepping by @p step (Python `range`-like).
 *
 * Accumulates values from @p start, advancing by @p step while below @p stop (or
 * above it when @p step is negative), so a step pointing away from @p stop yields an
 * empty array; throws when @p step is zero. Uses floating-point accumulation, so
 * rounding may affect the final count.
 * @param start first value.
 * @param stop exclusive bound.
 * @param step increment (non-zero).
 * @return a 1-D NDArray of the generated values.
 * @complexity O(count) where count = number of steps.
 * @alloc allocates a new NDArray buffer
 *   (shared_ptr<vector<double>>); throws if @p step is zero.
 * @test CheatahNDArray.Arange
 * @crtest NdarrayCompileRun.Arange
 * @systest StdlibE2E.Ndarray
 */
NDArray arange(double start, double stop, double step);
/**
 * Reshape @p a to @p shape (same element count).
 *
 * Reads @p a's elements in C-order (so it works through arbitrary views/broadcasts)
 * into a brand-new contiguous buffer with the requested shape, i.e. it copies rather
 * than aliasing; throws if the new size differs from @p a's or any dim is negative.
 * @param a source array.
 * @param shape the new dimensions (rejects negatives).
 * @return a new contiguous NDArray with the data laid out in C-order.
 * @complexity O(size).
 * @alloc flattens through views and allocates a new NDArray buffer
 *   (shared_ptr<vector<double>>); throws on size mismatch or negative dims.
 * @test CheatahNDArray.BroadcastingAdd, CheatahNDArray.ReshapeSizeMismatchThrows
 * @crtest NdarrayCompileRun.Reshape
 * @systest StdlibE2E.Ndarray
 */
NDArray reshape(const NDArray& a, const std::vector<long long>& shape);

// ---- element-wise ops (broadcasting) ----
/**
 * Element-wise add with broadcasting.
 *
 * Broadcasts both operands to their common shape, then writes `av + bv` into a new
 * contiguous result; throws if the shapes are not broadcast-compatible.
 * @param a first operand.
 * @param b second operand.
 * @return `a + b` broadcast to their common shape.
 * @complexity O(size of result).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws
 *   if shapes don't broadcast.
 * @test CheatahNDArray.BroadcastingAdd
 * @crtest NdarrayCompileRun.Add
 * @systest StdlibE2E.Ndarray
 */
NDArray add(const NDArray& a, const NDArray& b);
/**
 * Element-wise subtract with broadcasting.
 *
 * Broadcasts both operands to their common shape, then writes `av - bv` into a new
 * contiguous result; throws if the shapes are not broadcast-compatible.
 * @param a first operand.
 * @param b second operand.
 * @return `a - b` broadcast to their common shape.
 * @complexity O(size of result).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws
 *   if shapes don't broadcast.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 * @crtest NdarrayCompileRun.Sub
 * @systest StdlibE2E.Ndarray
 */
NDArray sub(const NDArray& a, const NDArray& b);
/**
 * Element-wise multiply with broadcasting.
 *
 * Broadcasts both operands to their common shape, then writes `av * bv` into a new
 * contiguous result; throws if the shapes are not broadcast-compatible.
 * @param a first operand.
 * @param b second operand.
 * @return `a * b` broadcast to their common shape.
 * @complexity O(size of result).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws
 *   if shapes don't broadcast.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 * @crtest NdarrayCompileRun.Mul
 * @systest StdlibE2E.Ndarray
 */
NDArray mul(const NDArray& a, const NDArray& b);
/**
 * Element-wise divide with broadcasting.
 *
 * Broadcasts both operands to their common shape, then writes `av / bv` into a new
 * contiguous result; throws if the shapes are not broadcast-compatible. Division by
 * zero follows IEEE-754 double semantics (yields inf or nan), not an exception.
 * @param a numerator.
 * @param b denominator.
 * @return `a / b` broadcast to their common shape.
 * @complexity O(size of result).
 * @alloc allocates a new NDArray buffer (shared_ptr<vector<double>>); throws
 *   if shapes don't broadcast.
 * @test CheatahNDArray.ElementwiseAndScalarBroadcast
 * @crtest NdarrayCompileRun.Divide
 * @systest StdlibE2E.Ndarray
 */
NDArray divide(const NDArray& a, const NDArray& b);

// ---- reductions / access / display ----
/**
 * Sum of all elements.
 *
 * Adds every element across all axes (a full reduction to a scalar, not a per-axis
 * sum), walking the array in C-order so views are handled correctly; an empty array
 * sums to 0.0.
 * @param a the array.
 * @return the total.
 * @complexity O(size).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Sum
 * @systest StdlibE2E.Ndarray
 */
double sum(const NDArray& a);
/**
 * Mean of all elements (0 for an empty array).
 *
 * A full reduction across all axes: divides the total @ref sum by the element count;
 * returns 0.0 (rather than dividing by zero) when the array is empty.
 * @param a the array.
 * @return the average.
 * @complexity O(size).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.Mean
 * @systest StdlibE2E.Ndarray
 */
double mean(const NDArray& a);
/**
 * Read one element by signed multi-index.
 *
 * The cheatah-facing wrapper over @ref NDArray::at that converts the signed coordinates
 * to sizes (throwing on any negative) before the rank/range-checked stride lookup.
 * @param a the array.
 * @param index one coordinate per dimension (rejects negatives).
 * @return the element value.
 * @complexity O(ndim).
 * @alloc none; bounds-checks rank/range and throws on a bad index.
 * @test CheatahNDArray.ShapeFactoriesAndReductions,
 *   CheatahNDArray.RejectsMaliciousShapesAndIndices
 * @crtest NdarrayCompileRun.Get
 * @systest StdlibE2E.Ndarray
 */
double get(const NDArray& a, const std::vector<long long>& index);
/**
 * The shape as signed dims.
 *
 * The cheatah-facing view of the shape: copies the unsigned dimensions into a
 * `long long` vector (cheatah integers are signed); a 0-d array yields an empty list.
 * @param a the array.
 * @return a vector of the dimensions.
 * @complexity O(ndim).
 * @alloc allocates a small vector.
 * @test CheatahNDArray.ShapeFactoriesAndReductions
 * @crtest NdarrayCompileRun.ShapeOf
 * @systest StdlibE2E.Ndarray
 */
std::vector<long long> shape_of(const NDArray& a);
/**
 * The element count as a signed value.
 *
 * The cheatah-facing form of @ref NDArray::size, returning the overflow-checked
 * product of the dims as a signed `long long` (1 for a 0-d array).
 * @param a the array.
 * @return the number of elements.
 * @complexity O(ndim).
 * @alloc none.
 * @test CheatahNDArray.ShapeFactoriesAndReductions, CheatahNDArray.Arange
 * @crtest NdarrayCompileRun.SizeOf
 * @systest StdlibE2E.Ndarray
 */
long long size_of(const NDArray& a);
/**
 * Render as a nested-bracket string, e.g. `"[[1, 2], [3, 4]]"`.
 *
 * Recurses dimension by dimension, wrapping each axis in `[`...`]` and joining
 * siblings with `", "`, formatting each double with the default `ostream` precision;
 * a 0-d scalar renders as the bare number with no brackets.
 * @param a the array.
 * @return the textual representation.
 * @complexity O(size).
 * @alloc allocates the result string.
 * @test CheatahNDArray.ToStringScalar, CheatahNDArray.BroadcastingAdd
 * @crtest NdarrayCompileRun.ToString
 * @systest StdlibE2E.Ndarray
 */
std::string to_string(const NDArray& a);  // e.g. "[[1, 2], [3, 4]]"

} // namespace cheatah::ndarray
