// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file enums.hpp
 * @brief cheatah `linalg` — operation-variant enums used as non-type template parameters.
 *
 * Where two or more routines differ only by a compile-time choice, that choice is an `enum`
 * NTTP `if constexpr`-branched inside ONE templated body — so the variants share a single
 * definition and the compiler emits only the selected branch (zero runtime cost, no dead
 * loads). This keeps the maintained surface small while staying the fastest possible per
 * instantiation.
 */

#include <cstdint>

namespace cheatah::linalg {

/// Whether a reduction conjugates its first operand. `dot`/`inner` are bilinear (`Conj::None`,
/// Σ aᵢbᵢ); `vdot` is the conjugate-linear Hermitian inner product (`Conj::Conjugate`,
/// Σ conj(aᵢ)·bᵢ). For a REAL element the conjugate is the identity, so both fold to the same
/// code via `if constexpr (is_complex_v<T> && mode == Conj::Conjugate)`.
enum class Conj : std::uint8_t { None, Conjugate };

}  // namespace cheatah::linalg
