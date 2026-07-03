// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file ownable.hpp
 * @brief The `Ownable` concept for `Owner<T>`, plus the container-shape concepts (`Mapping` /
 *        `Indexed`) that light up the keyed / indexed `write` setters.
 *
 * `write` is a SETTER — it never hands back a mutable reference. To reach INTO a common data structure
 * without replacing the whole thing, the write lease offers deduced overloads:
 *   - `w.write(value)`         — replace the whole object.
 *   - `w.write(index, value)`  — set element `index` of a sequence (vector / array / string / ndarray).
 *   - `w.write(key,   value)`  — set `key` of a mapping   (map / unordered_map / dict).
 * Which overloads exist is decided by these concepts, so the user is *forced* to use the right form
 * for the type they own (e.g. `w.write(i, v)` for a sequence — there is no `w.write()[i]`).
 */

#include <concepts>
#include <cstddef>

namespace cheatah::memory {

/// A type `Owner<T>` can hold: movable (so we can take it) and whole-value assignable (so
/// `w.write(value)` works). Everything the engine and the whole-value setter need.
template <class T>
concept Ownable = std::movable<T>;

/// A key→value mapping (std::map / std::unordered_map / a cheatah dict): exposes `key_type` +
/// `mapped_type` and `operator[](key)`. Enables `w.write(key, value)`.
template <class T>
concept Mapping = requires { typename T::key_type; typename T::mapped_type; } &&
                  requires(T& c, const typename T::key_type& k) { c[k]; };

/// A positionally-indexed sequence (vector / array / string / ndarray …): `c[size_t]` is valid AND it
/// is not a mapping (so a `map<size_t, V>` routes to the keyed setter, never the index one). Enables
/// `w.write(index, value)` and `r.read(index)`.
template <class T>
concept Indexed = (!Mapping<T>) && requires(T& c, std::size_t i) { c[i]; };

/// Has a first element — enables `r.read_front()` (vector / deque / list / string …).
template <class T>
concept HasFront = requires(const T& c) { c.front(); };

/// Has a last element — enables `r.read_back()`.
template <class T>
concept HasBack = requires(const T& c) { c.back(); };

}  // namespace cheatah::memory
