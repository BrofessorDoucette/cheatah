// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — forward declarations for the whole value model (NOT a second Node;
// the Node class lives only in node.hpp). These forwards must precede array.hpp / object.hpp so
// that each token header can stand on its own (and so IntelliSense can compile them in isolation)
// without pulling in the recursive Node class definition.
//
// Node is a forward-declarable class, which is what lets Array AND Object be templated on their
// storage TYPE with real concepts (ArrayStorage / ObjectStorage), each with an owning backing
// (std::vector, self-contained) and a viewing backing (std::span, into a Parser's pool).

#include <span>
#include <type_traits>
#include <vector>

namespace cheatah::parsers::json {

class Node;  // the value type; defined in node.hpp

// ---- arrays ------------------------------------------------------------------

// Backing storage for an Array: owning std::vector<Node> or non-owning std::span<const Node>.
template <typename S>
concept ArrayStorage =
    std::is_same_v<S, std::vector<Node>> || std::is_same_v<S, std::span<const Node>>;

template <ArrayStorage S>
class Array;  // defined in array.hpp

using OwnedArray = Array<std::vector<Node>>;     // owns its elements (self-contained)
using ArrayView = Array<std::span<const Node>>;  // views elements in a pool (zero-copy)

// ---- objects -----------------------------------------------------------------

// One object member: a key (a string Node) and its value. A forward-declared STRUCT (not a
// std::pair alias) so it can be named here while Node is incomplete and defined in node.hpp after
// Node — std::span<const Member> would otherwise eagerly instantiate pair<Node,Node> (via span's
// iterator concepts) while Node is still incomplete. (Node itself is forward-declarable, so
// span<const Node> for ArrayView does not have this problem.)
struct Member;

// Backing storage for an Object, mirroring ArrayStorage: owning vector or non-owning span.
template <typename S>
concept ObjectStorage =
    std::is_same_v<S, std::vector<Member>> || std::is_same_v<S, std::span<const Member>>;

template <ObjectStorage S>
class Object;  // defined in object.hpp

using OwnedObject = Object<std::vector<Member>>;     // owns its members (self-contained)
using ObjectView = Object<std::span<const Member>>;  // views members in a pool (zero-copy)

}  // namespace cheatah::parsers::json
