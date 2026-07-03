// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — Document: the root of a parsed tree. It is just a Node, named for
// intent at call sites (parse() returns a Document; dump() takes one).

#include "node.hpp"

namespace cheatah::parsers::json {

using Document = Node;

}  // namespace cheatah::parsers::json
