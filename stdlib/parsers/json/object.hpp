// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — the JSON object token, mirroring Array<S>: a class template over its
// backing storage type S (ObjectStorage), MOVE-ONLY. Members read uniformly as a
// std::span<const Member> via value(), whether owned or viewed:
//   - Object<std::vector<Member>>     (OwnedObject) — owns its members (self-contained).
//   - Object<std::span<const Member>> (ObjectView)  — views members in a Parser's pool, no copy.
//
// Being a template, its members instantiate lazily (so the constructor can be in-class even
// though Node is recursive — no out-of-line workaround needed). No runtime polymorphism.

#include <span>
#include <utility>
#include <vector>

#include "fwd.hpp"  // forward declarations + the ObjectStorage concept (standalone-clean)

namespace cheatah::parsers::json {

/**
 * @brief A JSON object token, templated on its backing storage @p S: Object<std::vector<Member>>
 *        (OwnedObject) owns its members, Object<std::span<const Member>> (ObjectView) views members
 *        in a Parser's pool with no copy. Move-only; both backings read uniformly via value().
 * @tparam S the member storage: std::vector<Member> (owning) or std::span<const Member> (viewing).
 */
template <ObjectStorage S>
class Object {
private:
    S value_;

public:
    /**
     * Construct from the backing storage (moved in).
     * @param value the member storage (an owning vector or a non-owning span).
     */
    Object(S value) : value_(std::move(value)) {}
    ~Object() = default;

    // Move-only: no object is copied; all are moved by default.
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    /**
     * Move-construct, taking over the other object's storage.
     * @param other the object to move from.
     */
    Object(Object&& other) noexcept = default;
    /**
     * Move-assign, taking over the other object's storage.
     * @param other the object to move from.
     * @return reference to this object.
     */
    Object& operator=(Object&& other) noexcept = default;

    /**
     * Read the members uniformly as a view, whether owned or viewed (no setter).
     * @return a std::span<const Member> over the object's key/value members.
     */
    [[nodiscard]] std::span<const Member> value() const noexcept { return value_; }
};

}  // namespace cheatah::parsers::json
