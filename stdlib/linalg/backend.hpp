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

/// @cond INTERNAL — the allocation-free out-parameter kernel (HOST overload; a device
/// extension adds its own `requires DeviceArray<Array<T>>` overload). Declared here so the
/// allocating front below can call it; defined + explicitly instantiated in routines.cpp.
/**
 * Matmul into the CALLER'S buffer @p out (out FIRST) — no result allocation (a hot loop hands
 * the same scratch every call). ONE two-layer overload over `Array<T>` unifying the former real
 * and complex, host `NDArray`/`CNDArray` out-param functions.
 * @tparam T the element type; @tparam Array the (host) container template.
 * @param out contiguous [a.rows, b.cols] destination, overwritten; must NOT alias @p a or @p b.
 * @param a,b the operands.
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
 * dimensions. Allocates the m×p result via `Array<T>::uninitialized` (no throwaway zero-fill) and
 * fills it through the out-parameter kernel — the host SIMD path, or a device shader when `Array`
 * is a device container (selected by concept at compile time).
 * @tparam T the element type (`double` / `std::complex<double>`), @tparam Array the container template.
 * @param a m×k matrix.
 * @param b k×p matrix.
 * @return m×p product, an `Array<T>` of the same container and element as the operands.
 * @complexity O(n³).
 * @alloc allocates only the m×p result; operands read in place (a strided host view packs once).
 * @test LinalgRoutines.ProductsAndTrace
 * @crtest LinalgCompileRun.Matmul
 * @systest StdlibE2E.Linalg
 */
template <ndarray::Field T, template <typename> class Array>
    requires NumericArray<Array<T>>
[[nodiscard]] Array<T> matmul(const Array<T>& a, const Array<T>& b) {
    if (a.ndim() != 2 || b.ndim() != 2)
        throw std::runtime_error("linalg: matmul expects 2-D matrices");
    if (a.shape()[1] != b.shape()[0])
        throw std::runtime_error("linalg: matmul inner dimension mismatch");
    Array<T> out = Array<T>::uninitialized({a.shape()[0], b.shape()[1]});
    matmul(out, a, b);
    return out;
}

}  // namespace cheatah::linalg
