// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file backend.hpp
 * @brief cheatah `linalg` — the backend-dispatch seam the generic algorithms are built on.
 *
 * Each algorithm splits into a **generic front** (a concept-constrained template that
 * validates shapes and owns the allocation policy) and a **customization point** — a
 * `tag_invoke`-style CPO whose overload for a given operand LOCATION supplies the actual
 * kernel. cheatah defines the CPO tag and (in host_backend.hpp) the HOST default; a device
 * extension supplies its own `tag_invoke` in its namespace, found by ADL. The public header
 * therefore names the seam without ever naming the (private) device extension.
 *
 * Two CPOs support the matmul front below:
 *   - @ref cpo::matmul_into — the allocation-free primitive: `out ← a·b`, out first;
 *   - @ref cpo::make_like — an uninitialized result shaped like a requested output,
 *     sharing the prototype operand's container family and element type (the host default
 *     mirrors `basic_ndarray::uninitialized`; a device default returns a pooled buffer).
 *
 * The allocating front composes the two (make an uninitialized result, then fill it), so
 * the out-parameter no-allocation discipline is the primitive and the convenience form is
 * a thin generic on top — identical for host and device.
 */
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "concepts.hpp"

namespace cheatah::linalg {

/// @cond INTERNAL
namespace cpo {

/// The matmul customization point (out-parameter primitive). A location supplies the kernel
/// by overloading `tag_invoke(matmul_into_t, out, a, b)` in its own namespace; the host
/// default lives in host_backend.hpp. Operands must share a location (the compile-time
/// firewall) and an exact element type (the kernels do not mix real·complex or f64·f32).
struct matmul_into_t {
    template <NumericArray Out, NumericArray A, NumericArray B>
        requires SameLocation<Out, A> && SameLocation<A, B> &&
                 std::same_as<element_t<A>, element_t<B>> &&
                 std::same_as<element_t<Out>, element_t<A>>
    void operator()(Out& out, const A& a, const B& b) const {
        tag_invoke(*this, out, a, b);
    }
};
inline constexpr matmul_into_t matmul_into{};

/// The result-allocation customization point: build an uninitialized array of @p shape whose
/// container family and element type match @p proto. The host default returns
/// `basic_ndarray<element>::uninitialized(shape)` (no throwaway zero-fill); a device default
/// returns a pool-allocated device buffer. Lets the generic front allocate results without
/// knowing whether it is on the host or a device.
struct make_like_t {
    template <NumericArray Proto>
    [[nodiscard]] auto operator()(const Proto& proto, std::vector<std::size_t> shape) const {
        return tag_invoke(*this, proto, std::move(shape));
    }
};
inline constexpr make_like_t make_like{};

}  // namespace cpo
/// @endcond

/**
 * Matrix multiply — the generic front. Requires both operands to be 2-D with matching inner
 * dimensions (a's cols == b's rows), throwing on a mismatch or a non-2-D input, then routes
 * to the location-appropriate kernel through @ref cpo::matmul_into. On the host this runs the
 * SIMD-friendly ikj kernel (contiguous inner loop, four-row blocking so each `b` load is
 * reused across four output rows); on a device it runs the device kernel — selected at compile
 * time by the operands' location, with a host⊗device mix rejected as an unsatisfied constraint.
 * @tparam A,B @ref NumericArray operands sharing a location and an exact element type.
 * @param a m×k matrix.
 * @param b k×p matrix.
 * @return m×p product, an array of the same container family and element type as the operands.
 * @complexity O(n³).
 * @alloc allocates only the m×p result (via @ref cpo::make_like); the operands are read in
 *        place from their own buffers (a non-contiguous host view is packed once into scratch).
 * @test LinalgRoutines.ProductsAndTrace
 * @crtest LinalgCompileRun.Matmul
 * @systest StdlibE2E.Linalg
 */
template <NumericArray A, NumericArray B>
    requires SameLocation<A, B> && std::same_as<element_t<A>, element_t<B>>
[[nodiscard]] auto matmul(const A& a, const B& b) {
    if (a.ndim() != 2 || b.ndim() != 2)
        throw std::runtime_error("linalg: matmul expects 2-D matrices");
    if (a.shape()[1] != b.shape()[0])
        throw std::runtime_error("linalg: matmul inner dimension mismatch");
    auto out = cpo::make_like(a, std::vector<std::size_t>{a.shape()[0], b.shape()[1]});
    cpo::matmul_into(out, a, b);
    return out;
}

}  // namespace cheatah::linalg
