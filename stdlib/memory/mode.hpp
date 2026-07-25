// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file mode.hpp
 * @brief `memory` access-mode tags: `read`, `write`, `write_renewable`, and the `Mode` concept.
 *
 * Empty tag types that select a `Lease`'s mode at compile time (dispatch on type, zero storage).
 * Lowercase because they are tags/markers, not classes (cf. `std::in_place_t`); a cheatah user
 * spells them `memory.read` / `memory.write`.
 */

#include <type_traits>

namespace cheatah::memory {

struct read {};             ///< a shared, read-only lease (readers coexist — `std::shared_lock`-flavored, but no lock object is held).
struct write {};            ///< an exclusive, one-shot write lease (a writer is alone — `std::unique_lock`-flavored, but no lock object is held).
struct write_renewable {};  ///< an exclusive write lease that MAY re-lease — a distinct, visible smell.

/// The lease-mode concept — every `Lease`/`Request` template is constrained to one of the three tags
/// (per the project's constrain-all-templates policy).
template <class M>
concept Mode = std::is_same_v<M, read> || std::is_same_v<M, write> || std::is_same_v<M, write_renewable>;

/// True for the two exclusive (write) modes.
template <class M>
inline constexpr bool is_write_mode = std::is_same_v<M, write> || std::is_same_v<M, write_renewable>;

}  // namespace cheatah::memory
