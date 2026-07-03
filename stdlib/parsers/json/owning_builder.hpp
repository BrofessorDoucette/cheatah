// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — OwningBuilder: the construction policy for the OWNING parse path.
//
// Same STACK-MACHINE interface as PoolBuilder (begin/add/finish driven by the iterative grammar),
// but self-contained: each open container is built in its own std::vector and, on finish, the WHOLE
// vector is moved into an OwnedArray/OwnedObject (O(1) — no per-element copies). Strings are copied
// too (owns_strings), so the resulting Document owns its entire tree and is independent of any
// Parser, pool, or the source buffer — safe to return, cache, or outlive the input. No runtime
// polymorphism — a plain policy type.

#include <utility>
#include <vector>

#include "document.hpp"  // Node, Member, OwnedArray, OwnedObject (via node.hpp)

namespace cheatah::parsers::json {

/**
 * @brief The construction policy for the OWNING JSON parse path: the same stack-machine interface
 *        as PoolBuilder (begin/add/finish), but each container is built in its own std::vector and,
 *        on finish, the whole vector is moved into an OwnedArray/OwnedObject (O(1), no per-element
 *        copies). Strings are copied too, so the resulting Document owns its entire tree and is
 *        independent of any Parser, pool, or source buffer. A plain policy type (no polymorphism).
 */
class OwningBuilder {
public:
    /// True: strings are COPIED into owned String<std::string> (not views into the source), so a
    /// Document from this path is fully self-contained (cf. PoolBuilder::owns_strings == false).
    static constexpr bool owns_strings = true;

    /// Open an array by pushing a fresh, empty backing vector onto the array stack.
    /// @complexity O(1)  @alloc the stack slot (amortized)  @test Json.OwningForms
    void begin_array() { array_stack_.emplace_back(); }
    /// Open an object by pushing a fresh, empty backing vector onto the object stack.
    /// @complexity O(1)  @alloc the stack slot (amortized)  @test Json.OwningForms
    void begin_object() { object_stack_.emplace_back(); }

    /// Append an element to the innermost open array.
    /// @param n the element node to append (moved in).
    /// @complexity O(1) amortized  @alloc vector growth  @test Json.OwningForms
    void add_element(Node&& n) { array_stack_.back().push_back(std::move(n)); }
    /// Append a member to the innermost open object.
    /// @param m the key/value member to append (moved in).
    /// @complexity O(1) amortized  @alloc vector growth  @test Json.OwningForms
    void add_member(Member&& m) { object_stack_.back().push_back(std::move(m)); }

    /// Close the innermost open array: move its whole backing vector into an owned Node.
    /// @return a Node holding the closed array as an OwnedArray.
    /// @complexity O(1) (whole-vector move)  @alloc none  @test Json.OwningForms
    Node finish_array() {
        Node n;
        n.variant().emplace<OwnedArray>(std::move(array_stack_.back()));
        array_stack_.pop_back();
        return n;
    }
    /// Close the innermost open object: move its whole backing vector into an owned Node.
    /// @return a Node holding the closed object as an OwnedObject.
    /// @complexity O(1) (whole-vector move)  @alloc none  @test Json.OwningForms
    Node finish_object() {
        Node n;
        n.variant().emplace<OwnedObject>(std::move(object_stack_.back()));
        object_stack_.pop_back();
        return n;
    }

private:
    std::vector<std::vector<Node>> array_stack_;     // in-progress arrays (LIFO by nesting depth)
    std::vector<std::vector<Member>> object_stack_;  // in-progress objects (LIFO by nesting depth)
};

}  // namespace cheatah::parsers::json
