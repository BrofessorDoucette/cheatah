// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file host_backend.hpp
 * @brief cheatah `linalg` — HOST (CPU) defaults for the backend-dispatch CPOs.
 *
 * These are the `tag_invoke` overloads the CPOs in backend.hpp resolve to for host
 * `basic_ndarray` operands. They are found by ADL through the CPO tag type (both the tag
 * and these overloads live in `cheatah::linalg::cpo`), so a device extension's own
 * overloads coexist without collision. Each host default delegates to the existing
 * concrete out-parameter kernel in routines.cpp — the proven SIMD path — so the generic
 * front and the hand-tuned kernel share one implementation.
 *
 * This is an internal implementation header included at the TAIL of routines.hpp, AFTER
 * the concrete out-parameter kernels are declared (the qualified calls below are looked up
 * at that point). It is not meant to be included on its own.
 */
#include <type_traits>
#include <utility>
#include <vector>

#include "backend.hpp"

namespace cheatah::linalg::cpo {

/// @cond INTERNAL — backend customization points (implementation detail of the CPOs above)
/// Host default for @ref matmul_into: delegates to the concrete out-parameter `matmul`
/// (the double / complex SIMD kernels), which validates shapes, rejects aliasing, and packs
/// a strided operand once. Constrained so real·complex or f64·f32 mixes never reach it.
template <HostArray Out, HostArray A, HostArray B>
    requires std::same_as<element_t<A>, element_t<B>> && std::same_as<element_t<Out>, element_t<A>>
void tag_invoke(matmul_into_t, Out& out, const A& a, const B& b) {
    ::cheatah::linalg::matmul(out, a, b);
}

/// Host default for @ref make_like: an uninitialized host array of the prototype's element
/// type and the requested shape — `basic_ndarray<T>::uninitialized`, which skips the throwaway
/// zero-fill because the caller (a kernel) overwrites every element.
template <HostArray Proto>
[[nodiscard]] auto tag_invoke(make_like_t, const Proto&, std::vector<std::size_t> shape) {
    return std::remove_cvref_t<Proto>::uninitialized(std::move(shape));
}
/// @endcond

}  // namespace cheatah::linalg::cpo
