// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file backend.hpp
 * @brief cheatah `linalg` — the two-layer (element T + container Array) generic fronts.
 *
 * Every routine takes TWO template layers: the element `T` and the container template
 * `Array`, with a `requires` concept enforcing the container. Both operands are spelled
 * `Array<T>`, so a host⊗device or f64⊗f32 mix cannot deduce a single `Array`/`T` and is a
 * compile error — the location/element firewall is FREE, via deduction, with no runtime
 * check and no `SameLocation` clause on the common binary ops.
 *
 * Each op is a pair of same-named overloads:
 *   - a 2-arg **allocating front** `Array<T> op(const Array<T>&, const Array<T>&)` that
 *     allocates the result with `Array<T>::uninitialized(...)` and calls the out-param form;
 *   - a 3-arg **out-parameter kernel** `void op(Array<T>& out, …)`, split by concept: the
 *     HOST overload (declared here, defined in routines.cpp) runs the raw-pointer SIMD kernel;
 *     a device extension supplies a `requires DeviceArray<Array<T>>` overload in ITS namespace,
 *     found by ADL. Mutually exclusive concepts → no ambiguity, and cheatah never names the
 *     extension. (No CPO objects, no tag_invoke — plain concept-constrained overloads.)
 */
#include <stdexcept>
#include <vector>

#include "concepts.hpp"

namespace cheatah::linalg {

/// @cond INTERNAL
/// the allocation-free out-parameter kernel (HOST overload; a device
/// extension adds its own `requires DeviceArray<Array<T>>` overload). Declared here so the
/// allocating front below can call it; defined + explicitly instantiated in routines.cpp.
/**
 * Matmul into the CALLER'S buffer @p out (out FIRST) — no result allocation (a hot loop hands
 * the same scratch every call).
 * @tparam T the element type.
 * @tparam Array the (host) container template.
 * @param out contiguous [a.rows, b.cols] destination, overwritten; must NOT alias @p a or @p b.
 * @param a,b the operands.
 * @complexity O(n³) (× B for a batch).
 * @alloc none for contiguous operands (product written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.MatmulIntoReusesBuffer
 * @test LinalgRoutines.ComplexMatmulIntoReusesBuffer
 */
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void matmul(Array<T>& out, const Array<T>& a, const Array<T>& b);
/// @endcond

/**
 * Matrix multiply — the allocating front. Both operands are `Array<T>` (so host⊗device / element
 * mixes fail to deduce and are compile errors); requires both to be 2-D with matching inner
 * dimensions — or both 3-D for the BATCHED product `[B,M,K] @ [B,K,N] → [B,M,N]` (equal batch
 * counts, strict: no broadcast batching). Allocates the result via `Array<T>::uninitialized` and
 * fills it through the out-parameter kernel — the host SIMD path, or a device shader when
 * `Array` is a device container.
 * @tparam T the element type (`double` / `std::complex<double>`).
 * @tparam Array the container template.
 * @param a m×k matrix, or a B×m×k batch of matrices.
 * @param b k×p matrix, or a B×k×p batch.
 * @return m×p product (or the B×m×p batch), an `Array<T>` of the same container and element.
 * @complexity O(n³) (× B for a batch).
 * @alloc allocates only the result; operands read in place (a strided host view packs once).
 * @concurrency deliberately single-threaded (the fastest-per-core contract); parallelize
 *        across independent products in the caller.
 * @test LinalgRoutines.ProductsAndTrace
 * @test LinalgRoutines.BatchedMatmul
 * @crtest LinalgCompileRun.Matmul
 * @systest StdlibE2E.Linalg
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] Array<T> matmul(const Array<T>& a, const Array<T>& b) {
    if (a.ndim() == 3 || b.ndim() == 3) {
        if (a.ndim() != 3 || b.ndim() != 3)
            throw std::runtime_error("linalg: batched matmul expects two 3-D operands");
        if (a.shape()[0] != b.shape()[0])
            throw std::runtime_error("linalg: batched matmul batch-count mismatch");
        if (a.shape()[2] != b.shape()[1])
            throw std::runtime_error("linalg: matmul inner dimension mismatch");
        Array<T> out = Array<T>::uninitialized({a.shape()[0], a.shape()[1], b.shape()[2]});
        matmul(out, a, b);
        return out;
    }
    if (a.ndim() != 2 || b.ndim() != 2)
        throw std::runtime_error("linalg: matmul expects 2-D matrices");
    if (a.shape()[1] != b.shape()[0])
        throw std::runtime_error("linalg: matmul inner dimension mismatch");
    Array<T> out = Array<T>::uninitialized({a.shape()[0], b.shape()[1]});
    matmul(out, a, b);
    return out;
}

/**
 * The flattened length of a vector-shaped operand — 1-D, or 2-D with a size-1 row/column
 * (throws otherwise). Reads only host-resident shape metadata, so it is valid for ANY located
 * container, device arrays included; the shared validation step of every vector front below.
 * @tparam A the (located) container type.
 * @param a the operand whose vector length is wanted.
 * @return the element count of the flattened vector.
 * @complexity O(1).
 * @alloc none.
 * @test LinalgRoutines.ProductsAndTrace
 */
template <NumericArray A>
[[nodiscard]] inline std::size_t vector_len(const A& a) {
    if (a.ndim() == 1) return a.shape()[0];
    if (a.ndim() == 2 && (a.shape()[0] == 1 || a.shape()[1] == 1)) return a.size();
    throw std::runtime_error("linalg: expected a 1-D vector");
}

// ---- reductions (dot / vdot / inner / trace): the scalar-out kernel pattern ----
// Same two-layer seam as matmul, with a SCALAR out-parameter: the front validates and calls the
// unqualified `op(out, …)`, which resolves to the HOST kernel below (routines.cpp) or a device
// extension's `requires DeviceArray<Array<T>>` overload via ADL.

/// @cond INTERNAL
/// the scalar-out reduction kernels (HOST overloads; a device extension adds its
/// own `requires DeviceArray<Array<T>>` overloads). Declared here so the allocating fronts below
/// can call them; defined + explicitly instantiated in routines.cpp.
/**
 * Bilinear dot product Σ aᵢbᵢ into the caller's scalar @p out (out FIRST) — the reduction analogue
 * of the out-param matmul kernel, shared by real and complex elements.
 * @tparam T the element type; @tparam Array the (host) container template.
 * @param out receives the scalar sum. @param a,b same-length vectors (validated by the front).
 * @test LinalgRoutines.ProductsAndTrace
 */
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void dot(T& out, const Array<T>& a, const Array<T>& b);
/**
 * Hermitian inner product Σ conj(aᵢ)·bᵢ into @p out (bilinear for a real element — the
 * conjugation is an `if constexpr` branch in the host kernel).
 * @tparam T the element type; @tparam Array the (host) container template.
 * @param out receives the scalar sum. @param a,b same-length vectors (validated by the front).
 * @test LinalgRoutines.VdotInnerOuterKron
 */
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void vdot(T& out, const Array<T>& a, const Array<T>& b);
/**
 * Bilinear inner product Σ aᵢbᵢ into @p out (numpy's `inner`; identical to @ref dot for
 * flattened vectors).
 * @tparam T the element type; @tparam Array the (host) container template.
 * @param out receives the scalar sum. @param a,b same-length vectors (validated by the front).
 * @test LinalgRoutines.VdotInnerOuterKron
 */
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void inner(T& out, const Array<T>& a, const Array<T>& b);
/**
 * Trace (diagonal sum) into @p out — strided diagonal read, no copy.
 * @tparam T the element type; @tparam Array the (host) container template.
 * @param out receives the diagonal sum. @param a a 2-D matrix (validated by the front).
 * @test LinalgRoutines.ProductsAndTrace
 */
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void trace(T& out, const Array<T>& a);
/// @endcond

/**
 * Dot product: 1-D inner product (vectors flattened) — the bilinear Σ aᵢbᵢ. Flattens each
 * operand to a vector (1-D, or 2-D with a size-1 row/column) and throws if either is not
 * vector-shaped or the lengths differ. Both operands are `Array<T>` (the deduction firewall).
 * @tparam T the element type.
 * @tparam Array the container template.
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ as the scalar `T`.
 * @complexity O(n).
 * @alloc none for contiguous operands (read in place); a non-contiguous view packs once O(n).
 * @test LinalgRoutines.ProductsAndTrace
 * @test LinalgRoutines.ComplexProducts
 * @crtest LinalgCompileRun.Dot
 * @crtest LinalgCompileRun.ComplexDot
 * @systest StdlibE2E.Linalg
 * @systest StdlibE2E.LinalgComplex
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] T dot(const Array<T>& a, const Array<T>& b) {
    if (vector_len(a) != vector_len(b))
        throw std::runtime_error("linalg: dot dimension mismatch");
    T out;
    dot(out, a, b);
    return out;
}

/**
 * Vector dot product. For a REAL element this is the bilinear Σ aᵢbᵢ (identical to @ref dot and
 * @ref inner); for a **complex** element it is the conjugate-linear Hermitian inner product
 * ⟨a, b⟩ = Σ conj(aᵢ)·bᵢ (numpy's `vdot`, conjugating the first argument); `vdot(a, a)` is ‖a‖².
 * @tparam T the element type.
 * @tparam Array the container template.
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ (real) or Σ conj(aᵢ)·bᵢ (complex), as the scalar `T`.
 * @complexity O(n).
 * @alloc none for contiguous operands; a non-contiguous view packs once O(n).
 * @test LinalgRoutines.VdotInnerOuterKron
 * @test LinalgRoutines.ComplexProducts
 * @crtest LinalgCompileRun.Vdot
 * @crtest LinalgCompileRun.ComplexVdot
 * @systest StdlibE2E.Linalg
 * @systest StdlibE2E.LinalgComplex
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] T vdot(const Array<T>& a, const Array<T>& b) {
    if (vector_len(a) != vector_len(b))
        throw std::runtime_error("linalg: dot dimension mismatch");
    T out;
    vdot(out, a, b);
    return out;
}

/**
 * Inner product of two vectors — the bilinear Σ aᵢbᵢ (numpy's `inner`; same as @ref dot).
 * @tparam T the element type.
 * @tparam Array the container template.
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ as the scalar `T`.
 * @complexity O(n).
 * @alloc none for contiguous operands; a non-contiguous view packs once O(n).
 * @test LinalgRoutines.VdotInnerOuterKron
 * @crtest LinalgCompileRun.Inner
 * @systest StdlibE2E.Linalg
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] T inner(const Array<T>& a, const Array<T>& b) {
    if (vector_len(a) != vector_len(b))
        throw std::runtime_error("linalg: dot dimension mismatch");
    T out;
    inner(out, a, b);
    return out;
}

/**
 * Trace: the sum of the matrix diagonal, as the scalar `T`. Requires a 2-D matrix (throws
 * otherwise); rectangular matrices sum min(r, c) diagonal entries.
 * @tparam T the element type.
 * @tparam Array the container template.
 * @param a a 2-D matrix.
 * @return Σ aᵢᵢ as the scalar `T`.
 * @complexity O(min(r, c)).
 * @alloc none (strided diagonal read straight from the buffer).
 * @test LinalgRoutines.ProductsAndTrace
 * @crtest LinalgCompileRun.Trace
 * @systest StdlibE2E.Linalg
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] T trace(const Array<T>& a) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    T out;
    trace(out, a);
    return out;
}

// ---- products with array results (outer / conj_transpose / kron): the matmul pattern ----

/// @cond INTERNAL
/// the allocation-free out-parameter kernels (HOST overloads; a device extension
/// adds its own `requires DeviceArray<Array<T>>` overloads). Declared here so the allocating
/// fronts below can call them; defined + explicitly instantiated in routines.cpp.
/**
 * Outer product into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref outer, writing the rank-1 result straight into @p out with no allocation.
 * @param out destination; a contiguous n×m matrix, overwritten. Must NOT alias @p a or @p b.
 * @param a length-n vector.
 * @param b length-m vector.
 * @complexity O(n·m).
 * @alloc none for contiguous operands (result written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.OuterIntoReusesBuffer
 */
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void outer(Array<T>& out, const Array<T>& a, const Array<T>& b);
/**
 * Conjugate transpose into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref conj_transpose, writing the c×r adjoint straight into @p out with no allocation.
 * @param out destination; a contiguous c×r matrix (for an r×c input), overwritten. Must NOT
 *        alias @p a (it reads A while writing the transpose — not an in-place op).
 * @param a a 2-D matrix.
 * @complexity O(r·c).
 * @alloc none for a contiguous operand (adjoint written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.ConjTransposeIntoReusesBuffer
 */
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void conj_transpose(Array<T>& out, const Array<T>& a);
/**
 * Kronecker product into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref kron, writing the block product straight into @p out with no allocation.
 * @param out destination; a contiguous (m·p)×(k·q) matrix, overwritten. Must NOT alias @p a or @p b.
 * @param a m×k matrix.
 * @param b p×q matrix.
 * @complexity O(n⁴) in the output area.
 * @alloc none for contiguous operands (block product written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.KronIntoReusesBuffer
 */
template <ndarray::Field T, template <typename> class Array>
    requires HostArray<Array<T>>
void kron(Array<T>& out, const Array<T>& a, const Array<T>& b);
/// @endcond

/**
 * Outer product of two vectors.
 *
 * Flattens both operands to vectors and forms the full rank-1 matrix; any pair
 * of vector lengths is accepted (no matching constraint). Allocates the result via
 * `Array<T>::uninitialized` and fills it through the out-parameter kernel — the host SIMD
 * path, or a device shader when `Array` is a device container (selected by concept).
 * @param a length-n vector.
 * @param b length-m vector.
 * @return n×m matrix aᵢbⱼ.
 * @complexity O(n·m).
 * @alloc allocates only the n×m result; operands read in place when contiguous.
 * @test LinalgRoutines.VdotInnerOuterKron
 * @crtest LinalgCompileRun.Outer
 * @systest StdlibE2E.Linalg
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] Array<T> outer(const Array<T>& a, const Array<T>& b) {
    Array<T> out = Array<T>::uninitialized({vector_len(a), vector_len(b)});
    outer(out, a, b);
    return out;
}

/**
 * Conjugate transpose (Hermitian adjoint) Aᴴ: transpose, then conjugate every entry (a plain
 * transpose for a real element — the conjugation is compiled out). A matrix is Hermitian iff
 * `conj_transpose(A) == A`.
 * @param a a 2-D matrix.
 * @return the c×r adjoint of an r×c input; throws on non-2-D input.
 * @complexity O(r·c).
 * @alloc allocates only the c×r result; a non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.ComplexProducts
 * @crtest LinalgCompileRun.ConjTranspose
 * @systest StdlibE2E.LinalgComplex
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] Array<T> conj_transpose(const Array<T>& a) {
    if (a.ndim() != 2) throw std::runtime_error("linalg: expected a 2-D matrix");
    Array<T> out = Array<T>::uninitialized({a.shape()[1], a.shape()[0]});
    conj_transpose(out, a);
    return out;
}

/**
 * Kronecker product.
 *
 * Requires both operands to be 2-D (throws otherwise) and replaces each entry of
 * @p a with that scalar times the whole of @p b, giving the (m·p)×(k·q) block
 * matrix; no dimension matching is needed.
 * @param a m×k matrix.
 * @param b p×q matrix.
 * @return (m·p)×(k·q) block product.
 * @complexity O(n⁴) in the output area.
 * @alloc allocates only the (m·p)×(k·q) result; a non-contiguous operand packs once into scratch.
 * @test LinalgRoutines.VdotInnerOuterKron
 * @crtest LinalgCompileRun.Kron
 * @systest StdlibE2E.Linalg
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] Array<T> kron(const Array<T>& a, const Array<T>& b) {
    if (a.ndim() != 2 || b.ndim() != 2)
        throw std::runtime_error("linalg: kron expects 2-D matrices");
    // Each output dimension is a PRODUCT of two input dims, so it must be overflow-checked
    // BEFORE it collapses into the shape vector: `product({m*p, k*q})` would only catch a wrap
    // of the final area, not a wrap of m*p (or k*q) alone, which under-allocates and lets the
    // kernel write out of bounds. `product({x, y})` is the shared checked multiply — it throws
    // on wrap instead. (See ndarray::detail::product; same class as the ndarray shape-overflow fix.)
    const std::size_t orows = ndarray::detail::product({a.shape()[0], b.shape()[0]});
    const std::size_t ocols = ndarray::detail::product({a.shape()[1], b.shape()[1]});
    Array<T> out = Array<T>::uninitialized({orows, ocols});
    kron(out, a, b);
    return out;
}

}  // namespace cheatah::linalg
