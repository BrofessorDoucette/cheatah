// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — the JSON array token, now EXACTLY symmetric with String<S>: a class
// template over its backing storage type S, constrained by the ArrayStorage concept. MOVE-ONLY
// (no array is ever copied; all are moved by default). Both backings read uniformly as a
// std::span<const Node> via value():
//   - Array<std::vector<Node>>     (OwnedArray) — owns its elements.
//   - Array<std::span<const Node>> (ArrayView)  — views elements stored elsewhere, no copy.
//
// No runtime polymorphism: the backing is selected at compile time by the template argument.

#include <span>
#include <utility>
#include <vector>

#include "fwd.hpp"  // forward declarations + the ArrayStorage concept (standalone-clean)

namespace cheatah::parsers::json {

/**
 * @brief A JSON array token, templated on its backing storage @p S: Array<std::vector<Node>>
 *        (OwnedArray) owns its elements, Array<std::span<const Node>> (ArrayView) views elements
 *        stored elsewhere with no copy. Move-only; both backings read uniformly via value().
 * @tparam S the element storage: std::vector<Node> (owning) or std::span<const Node> (viewing).
 */
template <ArrayStorage S>
class Array {
private:
    S value_;

public:
    /**
     * Construct from the backing storage (moved in).
     * @param value the element storage (an owning vector or a non-owning span).
     */
    Array(S value) : value_(std::move(value)) {}
    ~Array() = default;

    // Move-only: no array is copied; all are moved by default.
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
    /**
     * Move-construct, taking over the other array's storage.
     * @param other the array to move from.
     */
    Array(Array&& other) noexcept = default;
    /**
     * Move-assign, taking over the other array's storage.
     * @param other the array to move from.
     * @return reference to this array.
     */
    Array& operator=(Array&& other) noexcept = default;

    /**
     * Read the elements uniformly as a view, whether owned or viewed (no setter).
     * @return a std::span<const Node> over the array's elements.
     */
    [[nodiscard]] std::span<const Node> value() const noexcept { return value_; }
};

}  // namespace cheatah::parsers::json
