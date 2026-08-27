// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — Node: the dynamic JSON value. This is the single header for the Node
// TYPE (the class). Forward declarations for the value model (the Node name, the ArrayStorage
// concept, Array/Object/Member) live in fwd.hpp, so the token headers can compile standalone.
//
// Node is a thin CLASS wrapping a std::variant over the token types. It is a class (not a bare
// variant alias) for ONE reason: to be forward-declarable, which lets Array be templated on its
// storage TYPE with a real ArrayStorage concept — exactly symmetric with String / StringStorage.
// There is NO runtime polymorphism: no virtual functions, no base classes; dispatch is std::visit
// over the variant, resolved at compile time. The wrapper just exposes the variant via variant().

#include <type_traits>
#include <utility>
#include <variant>

#include "fwd.hpp"
#include "array.hpp"
#include "boolean.hpp"
#include "null.hpp"
#include "number.hpp"
#include "object.hpp"
#include "string.hpp"

namespace cheatah::parsers::json {

/**
 * @brief The dynamic JSON value: a thin class wrapping a std::variant over the token types
 *        (Null, Boolean, Number, owning/viewing String, Array, Object). There is no runtime
 *        polymorphism — dispatch is compile-time std::visit over variant(); the class exists to
 *        be forward-declarable so Array/Object can be templated on their storage type.
 */
class Node {
public:
    /// The set of JSON value alternatives this Node may hold (null is the first, default state).
    using variant_type = std::variant<Null, Boolean, Number,
                                      String<std::string_view>, String<std::string>,
                                      ArrayView, OwnedArray,
                                      ObjectView, OwnedObject>;

    Node() = default;  // JSON null (the variant's first alternative)

    /**
     * Construct from any one of the token alternatives (e.g. Number{3.5}); excludes Node itself
     * so the copy/move constructors are not hidden.
     * @tparam T one of the variant alternative types.
     * @param value the token value to store (forwarded into the variant).
     * @complexity O(1) — one move/copy of the token into the variant.
     * @alloc none for a moved-in token; a copied String<std::string> allocates its characters.
     * @test CheatahParsersJson.TokenClassesAndNodeVariant
     */
    template <class T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, Node>)
    Node(T&& value) : data_(std::forward<T>(value)) {}

    /**
     * The underlying variant, for dispatch with std::visit / std::get / std::get_if / emplace.
     * @return a mutable reference to the wrapped variant.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahParsersJson.TokenClassesAndNodeVariant
     */
    [[nodiscard]] variant_type& variant() noexcept { return data_; }
    /**
     * The underlying variant (const overload).
     * @return a const reference to the wrapped variant.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahParsersJson.TokenClassesAndNodeVariant
     */
    [[nodiscard]] const variant_type& variant() const noexcept { return data_; }

private:
    variant_type data_;
};

// Member holds two Nodes BY VALUE (key + value), so it is defined here — after Node is complete.
// It is forward-declared in fwd.hpp so Object / ObjectStorage can name vector<Member> /
// span<const Member> while Node is still being defined.
/**
 * @brief One key/value entry of a JSON object, holding both the key and value Nodes by value.
 *        Defined here (after Node is complete) and forward-declared in fwd.hpp.
 */
struct Member {
    Node first;   ///< the key — a string Node.
    Node second;  ///< the value Node.
};

}  // namespace cheatah::parsers::json
