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
#include <cmath>
#include <complex>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>  // placement new, for the default-init buffer allocator
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>  // std::forward / std::move
#include <version>  // __cpp_lib_execution feature-test macro
#include <vector>

// The unsequenced execution policy lets std::transform/std::reduce vectorize. libstdc++
// provides it; Apple's libc++ historically ships no usable <execution>, so guard on the
// feature-test macro and fall back to the plain (policy-less) overloads where it's
// absent. This is speed-neutral: `unseq` is unsequenced (no threads, no TBB) and for
// these simple element-wise loops the -O3 -march=native auto-vectorizer produces the
// same SIMD either way — the transcendental vectorization comes from ufunc_simd.cpp's
// libmvec/Accelerate kernels, not from this policy.
#if defined(__cpp_lib_execution)
#include <execution>
#define CHEATAH_UNSEQ std::execution::unseq,
#else
#define CHEATAH_UNSEQ
#endif

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

/// \cond INTERNAL
template <typename T>
struct is_complex : std::false_type {};
template <typename U>
struct is_complex<std::complex<U>> : std::bool_constant<std::is_floating_point_v<U>> {};
/// \endcond

/// Whether `T` is a `std::complex` of a floating type — the trait behind @ref Field.
template <typename T>
inline constexpr bool is_complex_v = is_complex<T>::value;

/// Field<T>: a scalar an ndarray can store — a real arithmetic type OR a
/// `std::complex` of a floating type. This is what makes **complex** matrices and
/// vectors first-class (Hermitian operators, complex wavefunctions), and lets a
/// REAL matrix yield the COMPLEX eigenvalues it mathematically has.
template <typename T>
concept Field = std::is_arithmetic_v<T> || is_complex_v<T>;

/// \cond INTERNAL
template <typename T>
struct real_base {
    using type = T;
};
template <typename U>
struct real_base<std::complex<U>> {
    using type = U;
};
/// \endcond

/// The real type underlying a @ref Field `T` (`double` for both `double` and
/// `complex<double>`).
template <typename T>
using real_base_t = typename real_base<T>::type;

/// complex_of_t<T>: the complex type over T's real base. `eig`/`eigvals` return an
/// array of these, because a real matrix can have complex eigenvalues (conjugate
/// pairs) — e.g. the rotation matrix [[0,-1],[1,0]] has eigenvalues ±i.
template <typename T>
using complex_of_t = std::complex<real_base_t<T>>;

namespace detail {
/// \cond INTERNAL
/// An allocator identical to `std::allocator<T>` in every respect EXCEPT that
/// DEFAULT (no-value) construction — what `vector(n)` / `resize(n)` perform — leaves a
/// trivially-constructible element UNINITIALIZED instead of value-initializing it to 0.
///
/// Every ndarray op that allocates a result buffer it then overwrites in full (binary
/// ops, ufuncs, reshape, array()) would otherwise pay a throwaway zero-fill of the whole
/// buffer first. That wasted write pass is hidden on compute-heavy ops but DOMINATES
/// bandwidth-bound ones — `add` was ≈1.5× of NumPy purely from the extra memset. With
/// this allocator the sizing path skips it; the value-filling forms (`assign(n, v)`,
/// `vector(n, v)` used by zeros/full/scalar) are untouched and still initialize.
template <typename T>
struct default_init_allocator : std::allocator<T> {
    using std::allocator<T>::allocator;
    template <typename U>
    struct rebind {
        using other = default_init_allocator<U>;
    };
    /// Default construction: a trivially-constructible element is left uninitialized.
    template <typename U>
    void construct(U* p) noexcept(std::is_nothrow_default_constructible_v<U>) {
        ::new (static_cast<void*>(p)) U;  // default-init (no `()`): no zeroing for trivial U
    }
    /// Every other construction (value-fill, copy, emplace) behaves exactly as normal.
    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
};
/// \endcond

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

/// Peel `std::vector<>` layers off a (possibly deeply nested) list type to reach the
/// leaf scalar — `nested_scalar_t<std::vector<std::vector<double>>>` is `double`.
template <typename V> struct nested_scalar { using type = V; };
template <typename U> struct nested_scalar<std::vector<U>> {
    using type = typename nested_scalar<U>::type;
};
template <typename V> using nested_scalar_t = typename nested_scalar<V>::type;

/// Whether `V` is a `std::vector<…>` (used to tell a nested list from a scalar leaf).
template <typename V> inline constexpr bool is_std_vector_v = false;
template <typename U> inline constexpr bool is_std_vector_v<std::vector<U>> = true;

/// Flatten a scalar leaf into the C-order buffer (recursion base case).
template <Field T>
void nested_collect(const T& x, std::vector<T>& flat, std::vector<std::size_t>&, std::size_t) {
    flat.push_back(x);
}
/// Walk a nested list: record each axis length the first time it is seen, reject a
/// ragged list (a row whose length differs from its siblings — numpy does too), and
/// flatten the leaves in C-order. The leaf scalar must be a @ref Field.
template <typename U>
    requires Field<nested_scalar_t<U>>
void nested_collect(const std::vector<U>& v, std::vector<nested_scalar_t<U>>& flat,
                    std::vector<std::size_t>& shape, std::size_t depth) {
    if (depth == shape.size()) shape.push_back(v.size());
    else if (shape[depth] != v.size())
        throw std::runtime_error("ndarray: array(...) ragged nested list (a row's length "
                                 "differs from its siblings)");
    for (const U& e : v) nested_collect(e, flat, shape, depth + 1);
}
}  // namespace detail

/// The backing store of an ndarray: a flat, contiguous, shared element buffer. It uses
/// @ref detail::default_init_allocator so a freshly-sized result buffer that an op is
/// about to overwrite in full is not needlessly zero-filled first. zeros/full/scalar,
/// which value-fill, are unaffected — only the no-value sizing path skips initialization.
template <Field T>
using buffer_t = std::vector<T, detail::default_init_allocator<T>>;

/**
 * @brief An N-dimensional array of `T` (a @ref Field element type — real or complex):
 *        a view ({shape, strides, offset}) over a shared element buffer.
 *
 * Copies are cheap and share the buffer; reshape/broadcast produce new views without
 * copying elements. Index math goes through @ref at, which bounds-checks. The element
 * type is deduced from the data (e.g. `array([1,2,3])` is integer, `array([1.0,…])`
 * is double); `NDArray` is the default `basic_ndarray<double>`. Complex element types
 * (`std::complex<double>`) make complex matrices/vectors — and the complex eigenvalues
 * a real matrix can have — first-class.
 */
template <Field T>
class basic_ndarray {
public:
    using value_type = T;  ///< The stored element type (the @ref Field `T`).
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
    basic_ndarray() : data_(std::make_shared<buffer_t<T>>()) {}
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
        : data_(std::make_shared<buffer_t<T>>()), shape_(std::move(shape)) {
        // resize (default-init: no zero pass) then std::fill — the fill goes through
        // operator= on built elements, which keeps libstdc++'s memset/SIMD fast path.
        // (vector::assign(n, v) would route through the allocator's construct, which a
        // default-init allocator forces element-by-element — measurably slower.)
        data_->resize(detail::product(shape_));
        std::fill(data_->begin(), data_->end(), fill);
        strides_ = detail::contiguous_strides(shape_);
    }
    /**
     * Build a contiguous array of @p shape whose buffer is sized but left
     * UNINITIALIZED — for internal ops (binary ops, ufuncs, reshape, array) that
     * immediately write every element, so paying for a zero-fill first is pure waste.
     * @param shape the dimensions.
     * @return an array of @p shape with an uninitialized buffer.
     * @complexity O(1) beyond the allocation (no element initialization).
     * @alloc allocates an uninitialized `product(shape)`-element buffer; overflow-checked.
     * @test CheatahNDArray.BroadcastingAdd
     * @systest StdlibE2E.Ndarray
     */
    static basic_ndarray uninitialized(std::vector<std::size_t> shape) {
        basic_ndarray out;
        out.shape_ = std::move(shape);
        out.data_->resize(detail::product(out.shape_));  // default-init alloc -> no zero-fill
        out.strides_ = detail::contiguous_strides(out.shape_);
        return out;
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
    basic_ndarray(std::shared_ptr<buffer_t<T>> data, std::vector<std::size_t> shape,
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
    const std::shared_ptr<buffer_t<T>>& buffer() const { return data_; }
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

    /**
     * Pretty-print hook used by `io.print`: renders the array in nested-bracket form but
     * ABBREVIATES a large array with `...` (numpy-style edge items), so printing a big array
     * stays readable. `io.rprint` (and `str()`/`operator<<`) keep the FULL untruncated form —
     * slice the array and `rprint` the subset to see everything.
     * @param os destination stream.
     * @param indent unused (arrays are self-delimiting with brackets); present so `io.print`
     *   detects this hook uniformly with struct pretty-printers.
     * @complexity O(printed elements).
     * @alloc allocates intermediate strings.
     * @test CheatahNDArray.PrettyPrintAbbreviatesLarge
     * @systest StdlibE2E.Ndarray
     */
    void cheatah_pretty_print(std::ostream& os, long long indent) const;

private:
    std::shared_ptr<buffer_t<T>> data_;
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
template <Field T>
inline bool is_contiguous(const basic_ndarray<T>& a) {
    // C-order contiguity WITHOUT materializing the reference strides: walk the dims
    // back-to-front and check each stride equals the running size product. The old
    // `strides() == contiguous_strides(shape())` heap-allocated a vector on every call
    // — a fixed cost that dominated small-n reductions (e.g. dot, where it ran twice
    // per call). Same result as the comparison, zero allocation. O(ndim).
    const std::vector<std::size_t>& shape = a.shape();
    const std::vector<std::ptrdiff_t>& strides = a.strides();
    std::ptrdiff_t expect = 1;
    for (std::size_t i = shape.size(); i-- > 0;) {
        if (strides[i] != expect) return false;
        expect *= static_cast<std::ptrdiff_t>(shape[i]);
    }
    return true;
}

/**
 * A zero-copy view of @p a stretched to @p target (size-1 / missing dims get stride 0).
 * @param a source array.
 * @param target the shape to stretch to.
 * @return a VIEW sharing @p a's buffer (no element copy).
 * @test CheatahNDArray.BroadcastTo
 * @systest StdlibE2E.Ndarray
 */
template <Field T>
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
template <Field T>
basic_ndarray<T> array(const std::vector<T>& values) {
    basic_ndarray<T> a = basic_ndarray<T>::uninitialized({values.size()});
    std::copy(values.begin(), values.end(), a.buffer()->begin());
    return a;
}
/**
 * N-dimensional array from a **nested** list — `array([[1, 2], [3, 4]])` is 2-D,
 * `array([[[1],[2]],[[3],[4]]])` is 3-D, and so on to any depth. The shape is read off
 * the nesting (outer list = axis 0, …) and the leaf scalar type is deduced; the list
 * must be **rectangular** (every sibling row the same length) or it throws, exactly as
 * numpy rejects a ragged array. Selected only when the argument is itself a list of
 * lists, so it never competes with the 1-D @ref array overload above.
 * @tparam V the element type of the outer list — itself a `std::vector<…>` whose leaf
 *   is a @ref Field.
 * @param values the nested list (rows, planes, …), copied into a fresh C-order buffer.
 * @return a contiguous `basic_ndarray<T>` of the inferred shape.
 * @complexity O(size).
 * @alloc allocates one buffer of `product(shape)` elements; throws on a ragged list.
 * @test CheatahNDArray.NestedArrayConstruction
 * @crtest NdarrayCompileRun.NestedArray
 * @systest StdlibE2E.Ndarray
 */
template <typename V>
    requires detail::is_std_vector_v<V> && Field<detail::nested_scalar_t<V>>
basic_ndarray<detail::nested_scalar_t<V>> array(const std::vector<V>& values) {
    using T = detail::nested_scalar_t<V>;
    std::vector<std::size_t> shape;
    std::vector<T> flat;
    detail::nested_collect(values, flat, shape, 0);
    basic_ndarray<T> a = basic_ndarray<T>::uninitialized(shape);
    std::move(flat.begin(), flat.end(), a.buffer()->begin());
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
template <Field T>
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
template <Field T>
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
template <Field T>
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
template <Field T>
basic_ndarray<T> reshape(const basic_ndarray<T>& a, const std::vector<long long>& shape) {
    const std::vector<std::size_t> ns = detail::to_size(shape);
    if (detail::product(ns) != a.size()) {
        throw std::runtime_error("ndarray: cannot reshape, size mismatch");
    }
    basic_ndarray<T> out = basic_ndarray<T>::uninitialized(ns);  // every element is written below
    auto& buf = *out.buffer();
    // Contiguous source (the common case — e.g. reshaping a freshly built array): copy
    // the flat block in one shot instead of walking a per-element bounds-checked
    // odometer.
    if (is_contiguous(a)) {
        const T* src = a.buffer()->data() + a.offset();
        std::copy(src, src + a.size(), buf.begin());
        return out;
    }
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
template <Field T, typename Op>
basic_ndarray<T> binary_op(const basic_ndarray<T>& a, const basic_ndarray<T>& b, Op op) {
    const std::vector<std::size_t> rshape = broadcast_shapes(a.shape(), b.shape());
    basic_ndarray<T> out = basic_ndarray<T>::uninitialized(rshape);  // every element written below
    auto& obuf = *out.buffer();
    // Scalar fast paths: `array ⊕ scalar` (or the reverse) is by far the most common
    // broadcast, and the general strided walk below does a bounds-checked at() per
    // element (no SIMD). When the other operand is a single value over a contiguous
    // full-shape array, it's a flat loop we hand to the unseq transform so it vectorizes
    // the same way the array⊕array path does.
    if (b.size() == 1 && a.shape() == rshape && is_contiguous(a)) {
        const T s = (*b.buffer())[b.offset()];
        const auto first = a.buffer()->begin() + a.offset();
        std::transform(CHEATAH_UNSEQ first, first + obuf.size(), obuf.begin(),
                       [s, op](T x) { return op(x, s); });
        return out;
    }
    if (a.size() == 1 && b.shape() == rshape && is_contiguous(b)) {
        const T s = (*a.buffer())[a.offset()];
        const auto first = b.buffer()->begin() + b.offset();
        std::transform(CHEATAH_UNSEQ first, first + obuf.size(), obuf.begin(),
                       [s, op](T x) { return op(s, x); });
        return out;
    }
    const basic_ndarray<T> av = broadcast_to(a, rshape);
    const basic_ndarray<T> bv = broadcast_to(b, rshape);
    if (is_contiguous(av) && is_contiguous(bv)) {
        const auto& abuf = *av.buffer();
        const auto& bbuf = *bv.buffer();
        std::transform(CHEATAH_UNSEQ abuf.begin() + av.offset(),
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
template <Field T>
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
template <Field T>
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
template <Field T>
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
template <Field T>
basic_ndarray<T> divide(const basic_ndarray<T>& a, const basic_ndarray<T>& b) {
    return binary_op(a, b, [](T x, T y) { return x / y; });
}

// ---- complex support ----
namespace detail {
/// Map @p a element-wise through @p f into a fresh contiguous array of element type
/// `U` (which may differ from `T` — e.g. complex→real for @ref real). Contiguous
/// fast path via `std::transform(unseq)`; otherwise a C-order walk.
template <typename U, Field T, typename F>
basic_ndarray<U> map_array(const basic_ndarray<T>& a, F f) {
    basic_ndarray<U> out = basic_ndarray<U>::uninitialized(a.shape());  // fully written below
    auto& obuf = *out.buffer();
    if (is_contiguous(a)) {
        const auto& abuf = *a.buffer();
        std::transform(CHEATAH_UNSEQ abuf.begin() + a.offset(),
                       abuf.begin() + a.offset() + a.size(), obuf.begin(), f);
        return out;
    }
    std::vector<std::size_t> idx(a.ndim(), 0);
    std::size_t flat = 0;
    do {
        obuf[flat++] = f(a.at(idx));
    } while (a.ndim() != 0 && next_index(idx, a.shape()));
    return out;
}

// Out-of-line, separately-compiled (-ffast-math) double-precision SIMD kernels for the
// element-wise ufuncs — see ufunc_simd.cpp. They vectorize the transcendentals through
// libmvec, which the default flags cannot; isolating -ffast-math to that file keeps the
// rest of cheatah's arithmetic strict.
void simd_sqrt_f64(const double*, double*, std::size_t);
void simd_cbrt_f64(const double*, double*, std::size_t);
void simd_exp_f64(const double*, double*, std::size_t);
void simd_log_f64(const double*, double*, std::size_t);
void simd_sin_f64(const double*, double*, std::size_t);
void simd_cos_f64(const double*, double*, std::size_t);
void simd_tan_f64(const double*, double*, std::size_t);

/// Map a ufunc over @p a: a *contiguous double* array goes through the precompiled SIMD
/// @p kernel; everything else (float, or a strided/broadcast view) uses the generic
/// scalar @p fallback. Same result either way — the kernel just vectorizes the hot case.
template <FloatingPoint T, class Kernel, class Fallback>
basic_ndarray<T> map_ufunc(const basic_ndarray<T>& a, Kernel kernel, Fallback fallback) {
    if constexpr (std::is_same_v<T, double>) {
        if (is_contiguous(a)) {
            basic_ndarray<T> out = basic_ndarray<T>::uninitialized(a.shape());  // kernel fills it
            kernel(a.buffer()->data() + a.offset(), out.buffer()->data(), a.size());
            return out;
        }
    }
    return map_array<T>(a, fallback);
}
}  // namespace detail

/**
 * Build a complex array from real and imaginary parts (element-wise `re + im·j`),
 * broadcasting the two together — the way to construct a complex matrix/vector
 * (a wavefunction, a Hermitian operator) since cheatah literals are real.
 * @param re the real parts (a real floating array).
 * @param im the imaginary parts (a real floating array, broadcast against @p re).
 * @return a `basic_ndarray<std::complex<T>>` of `re + im·j`; throws if the shapes don't broadcast.
 * @complexity O(size of result).
 * @alloc allocates the result buffer.
 * @test CheatahNDArray.ComplexConstructAndParts
 * @crtest NdarrayCompileRun.Complex
 * @systest StdlibE2E.NdarrayComplex
 */
template <FloatingPoint T>
basic_ndarray<std::complex<T>> complex(const basic_ndarray<T>& re, const basic_ndarray<T>& im) {
    using C = std::complex<T>;
    const std::vector<std::size_t> rshape = broadcast_shapes(re.shape(), im.shape());
    const basic_ndarray<T> rv = broadcast_to(re, rshape);
    const basic_ndarray<T> iv = broadcast_to(im, rshape);
    basic_ndarray<C> out(rshape);
    auto& obuf = *out.buffer();
    std::vector<std::size_t> idx(rshape.size(), 0);
    std::size_t flat = 0;
    do {
        obuf[flat++] = C(rv.at(idx), iv.at(idx));
    } while (!rshape.empty() && detail::next_index(idx, rshape));
    return out;
}
/**
 * Element-wise complex conjugate (`a − b·j` for each `a + b·j`); on a real array it
 * is the identity (a copy). Type-preserving. Used to form Hermitian adjoints and
 * conjugate-linear inner products.
 * @param a the array.
 * @return a fresh array of the same element type with each element conjugated.
 * @complexity O(size).
 * @alloc allocates the result buffer.
 * @test CheatahNDArray.ComplexConstructAndParts
 * @crtest NdarrayCompileRun.Conj
 * @systest StdlibE2E.NdarrayComplex
 */
template <Field T>
basic_ndarray<T> conj(const basic_ndarray<T>& a) {
    return detail::map_array<T>(a, [](T x) -> T {
        if constexpr (is_complex_v<T>) {
            return std::conj(x);
        } else {
            return x;
        }
    });
}
/**
 * The real parts as a real array (the identity on a real array). For `a + b·j` it
 * returns `a`.
 * @param a the array.
 * @return a `basic_ndarray<real_base_t<T>>` of the real parts.
 * @complexity O(size).
 * @alloc allocates the result buffer.
 * @test CheatahNDArray.ComplexConstructAndParts
 * @crtest NdarrayCompileRun.Real
 * @systest StdlibE2E.NdarrayComplex
 */
template <Field T>
basic_ndarray<real_base_t<T>> real(const basic_ndarray<T>& a) {
    using R = real_base_t<T>;
    return detail::map_array<R>(a, [](T x) -> R {
        if constexpr (is_complex_v<T>) {
            return x.real();
        } else {
            return x;
        }
    });
}
/**
 * The imaginary parts as a real array (all zeros for a real array). For `a + b·j` it
 * returns `b`.
 * @param a the array.
 * @return a `basic_ndarray<real_base_t<T>>` of the imaginary parts.
 * @complexity O(size).
 * @alloc allocates the result buffer.
 * @test CheatahNDArray.ComplexConstructAndParts
 * @crtest NdarrayCompileRun.Imag
 * @systest StdlibE2E.NdarrayComplex
 */
template <Field T>
basic_ndarray<real_base_t<T>> imag(const basic_ndarray<T>& a) {
    using R = real_base_t<T>;
    return detail::map_array<R>(a, [](T x) -> R {
        if constexpr (is_complex_v<T>) {
            return x.imag();
        } else {
            return R{0};
        }
    });
}

// ---- element-wise math (numpy-style ufuncs) ----
// These are the array counterparts of the scalar `math` module — mirroring Python's
// split: `math.sqrt(x)` for a scalar, `ndarray.sqrt(a)` (≈ `numpy.sqrt`) for a whole
// array. A contiguous `double` array routes through a precompiled SIMD kernel
// (ufunc_simd.cpp) that vectorizes via glibc's libmvec — so `exp`/`sin`/… run at vector
// speed and beat NumPy's ufuncs; other element types / strided views fall back to a
// scalar map (see detail::map_ufunc).
/**
 * Element-wise square root (the array form of `math.sqrt`; ≈ `numpy.sqrt`).
 * @param a a floating-point array.
 * @return a fresh same-shape array with `√x` for each element.
 * @complexity O(size). @alloc allocates the result buffer.
 * @test CheatahNDArray.ElementwiseMath
 * @crtest NdarrayCompileRun.Sqrt
 * @systest StdlibE2E.NdarrayMath
 */
template <FloatingPoint T>
basic_ndarray<T> sqrt(const basic_ndarray<T>& a) {
    return detail::map_ufunc<T>(a, detail::simd_sqrt_f64, [](T x) { return std::sqrt(x); });
}
/**
 * Element-wise cube root (the array form of `math.cbrt`; ≈ `numpy.cbrt`).
 * @param a a floating-point array.
 * @return a fresh same-shape array with `∛x` for each element.
 * @complexity O(size). @alloc allocates the result buffer.
 * @test CheatahNDArray.ElementwiseMath
 * @systest StdlibE2E.NdarrayMath
 */
template <FloatingPoint T>
basic_ndarray<T> cbrt(const basic_ndarray<T>& a) {
    return detail::map_ufunc<T>(a, detail::simd_cbrt_f64, [](T x) { return std::cbrt(x); });
}
/**
 * Element-wise eˣ (the array form of `math.exp`; ≈ `numpy.exp`).
 * @param a a floating-point array.
 * @return a fresh same-shape array with `exp(x)` for each element.
 * @complexity O(size). @alloc allocates the result buffer.
 * @test CheatahNDArray.ElementwiseMath
 * @crtest NdarrayCompileRun.Exp
 * @systest StdlibE2E.NdarrayMath
 */
template <FloatingPoint T>
basic_ndarray<T> exp(const basic_ndarray<T>& a) {
    return detail::map_ufunc<T>(a, detail::simd_exp_f64, [](T x) { return std::exp(x); });
}
/**
 * Element-wise natural log (the array form of `math.log`; ≈ `numpy.log`).
 * @param a a floating-point array.
 * @return a fresh same-shape array with `ln(x)` for each element.
 * @complexity O(size). @alloc allocates the result buffer.
 * @test CheatahNDArray.ElementwiseMath
 * @systest StdlibE2E.NdarrayMath
 */
template <FloatingPoint T>
basic_ndarray<T> log(const basic_ndarray<T>& a) {
    return detail::map_ufunc<T>(a, detail::simd_log_f64, [](T x) { return std::log(x); });
}
/**
 * Element-wise sine (the array form of `math.sin`; ≈ `numpy.sin`).
 * @param a a floating-point array (radians).
 * @return a fresh same-shape array with `sin(x)` for each element.
 * @complexity O(size). @alloc allocates the result buffer.
 * @test CheatahNDArray.ElementwiseMath
 * @crtest NdarrayCompileRun.Sin
 * @systest StdlibE2E.NdarrayMath
 */
template <FloatingPoint T>
basic_ndarray<T> sin(const basic_ndarray<T>& a) {
    return detail::map_ufunc<T>(a, detail::simd_sin_f64, [](T x) { return std::sin(x); });
}
/**
 * Element-wise cosine (the array form of `math.cos`; ≈ `numpy.cos`).
 * @param a a floating-point array (radians).
 * @return a fresh same-shape array with `cos(x)` for each element.
 * @complexity O(size). @alloc allocates the result buffer.
 * @test CheatahNDArray.ElementwiseMath
 * @systest StdlibE2E.NdarrayMath
 */
template <FloatingPoint T>
basic_ndarray<T> cos(const basic_ndarray<T>& a) {
    return detail::map_ufunc<T>(a, detail::simd_cos_f64, [](T x) { return std::cos(x); });
}
/**
 * Element-wise tangent (the array form of `math.tan`; ≈ `numpy.tan`).
 * @param a a floating-point array (radians).
 * @return a fresh same-shape array with `tan(x)` for each element.
 * @complexity O(size). @alloc allocates the result buffer.
 * @test CheatahNDArray.ElementwiseMath
 * @systest StdlibE2E.NdarrayMath
 */
template <FloatingPoint T>
basic_ndarray<T> tan(const basic_ndarray<T>& a) {
    return detail::map_ufunc<T>(a, detail::simd_tan_f64, [](T x) { return std::tan(x); });
}
/**
 * Element-wise absolute value (the array form of `math.abs`; ≈ `numpy.abs`).
 * @param a a floating-point array.
 * @return a fresh same-shape array with `|x|` for each element.
 * @complexity O(size). @alloc allocates the result buffer.
 * @test CheatahNDArray.ElementwiseMath
 * @systest StdlibE2E.NdarrayMath
 */
template <FloatingPoint T>
basic_ndarray<T> abs(const basic_ndarray<T>& a) {
    return detail::map_array<T>(a, [](T x) { return std::fabs(x); });
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
template <Field T>
T sum(const basic_ndarray<T>& a) {
    if (is_contiguous(a)) {
        // Eight independent accumulators so the reduction vectorizes (a single running
        // sum — or a plain std::reduce, which libstdc++ left-folds for FP without
        // -ffast-math — serializes on FP-add latency, the dot/norm mistake).
        const T* p = a.buffer()->data() + a.offset();
        const std::size_t n = a.size();
        T s0{}, s1{}, s2{}, s3{}, s4{}, s5{}, s6{}, s7{};
        std::size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            s0 += p[i + 0]; s1 += p[i + 1]; s2 += p[i + 2]; s3 += p[i + 3];
            s4 += p[i + 4]; s5 += p[i + 5]; s6 += p[i + 6]; s7 += p[i + 7];
        }
        T s = ((s0 + s1) + (s2 + s3)) + ((s4 + s5) + (s6 + s7));
        for (; i < n; ++i) s += p[i];
        return s;
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
template <Field T>
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
template <Field T>
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
template <Field T>
long long size_of(const basic_ndarray<T>& a) {
    return static_cast<long long>(a.size());
}

namespace detail {
/// Format one element. Real types go through `operator<<`; a complex element is
/// rendered Python-style as `a+bj` / `a-bj` (not the `std::complex` default
/// `(a,b)`), so a complex spectrum prints the way a cheatah user expects.
template <typename T>
std::string format_scalar(const T& v) {
    std::ostringstream os;
    if constexpr (is_complex_v<T>) {
        using R = real_base_t<T>;
        // Flush negative zero to +0 so a conjugate prints "1+0j", not "1+-0j".
        const auto nz = [](R x) -> R { return x == R{0} ? R{0} : x; };
        os << nz(v.real());
        if (v.imag() < R{0}) {
            os << "-" << nz(-v.imag()) << "j";
        } else {
            os << "+" << nz(v.imag()) << "j";
        }
    } else {
        os << v;
    }
    return os.str();
}

/// Recursively format @p a into nested brackets (each element via `format_scalar`).
template <Field T>
void format_rec(const basic_ndarray<T>& a, std::vector<std::size_t>& idx, std::size_t dim,
                std::string& out) {
    if (dim == a.ndim()) {
        out += format_scalar(a.at(idx));
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

/// Like format_rec but ABBREVIATES a large array: an axis longer than `2*edge` shows its
/// first and last `edge` items with `...` between, recursively. Summarization is enabled by
/// @p summarize (the caller turns it on only past a total-size threshold), so small arrays
/// print in full.
template <Field T>
void format_rec_trunc(const basic_ndarray<T>& a, std::vector<std::size_t>& idx, std::size_t dim,
                      std::string& out, std::size_t edge, bool summarize) {
    if (dim == a.ndim()) {
        out += format_scalar(a.at(idx));
        return;
    }
    const std::size_t n = a.shape()[dim];
    const bool trunc = summarize && n > 2 * edge;
    out += "[";
    bool first = true;
    for (std::size_t i = 0; i < n; ++i) {
        if (trunc && i >= edge && i < n - edge) {
            if (i == edge) {
                if (!first) out += ", ";
                out += "...";
                first = false;
            }
            continue;  // skip the abbreviated middle
        }
        if (!first) out += ", ";
        first = false;
        idx[dim] = i;
        format_rec_trunc(a, idx, dim + 1, out, edge, summarize);
    }
    out += "]";
}

/// to_string, but ABBREVIATED with `...` when the array is large (total size beyond a
/// numpy-style threshold) — the readable default for `io.print`. `io.rprint`/`to_string`
/// keep the full form.
template <Field T>
std::string to_string_pretty(const basic_ndarray<T>& a) {
    if (a.ndim() == 0) return format_scalar(a.at({}));
    constexpr std::size_t kEdge = 3;        // items kept at each end of an abbreviated axis
    constexpr std::size_t kThreshold = 1000;  // only summarize past this many elements (numpy)
    std::vector<std::size_t> idx(a.ndim(), 0);
    std::string out;
    format_rec_trunc(a, idx, 0, out, kEdge, a.size() > kThreshold);
    return out;
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
template <Field T>
std::string to_string(const basic_ndarray<T>& a) {
    if (a.ndim() == 0) {
        return detail::format_scalar(a.at({}));
    }
    std::vector<std::size_t> idx(a.ndim(), 0);
    std::string out;
    detail::format_rec(a, idx, 0, out);
    return out;
}

template <Field T>
inline std::string basic_ndarray<T>::str() const {
    return to_string(*this);
}

/**
 * Stream an array to a `std::ostream` (the FULL nested-bracket form) — so an NDArray is
 * directly Streamable, like a primitive or a cheatah struct, without going through
 * `to_string`/`str()`. `io.rprint`, `str()`, and a struct that holds an array all stream it
 * this way; `io.print` instead uses @ref cheatah_pretty_print to abbreviate large arrays.
 * @param os the stream.
 * @param a the array.
 * @return @p os.
 * @complexity O(size).
 * @alloc allocates the intermediate string.
 * @test CheatahNDArray.ToString
 * @systest StdlibE2E.Ndarray
 */
template <Field T>
std::ostream& operator<<(std::ostream& os, const basic_ndarray<T>& a) {
    return os << to_string(a);
}

template <Field T>
inline void basic_ndarray<T>::cheatah_pretty_print(std::ostream& os, long long) const {
    os << detail::to_string_pretty(*this);
}

} // namespace cheatah::ndarray
