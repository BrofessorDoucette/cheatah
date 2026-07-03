// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — PoolBuilder: the construction policy for the POOLED parse path.
//
// The (now iterative) grammar in json.cpp drives a Builder as a small STACK MACHINE: begin_array/
// begin_object open a container, add_element/add_member append children to the innermost open one,
// and finish_array/finish_object close it and return its Node. PoolBuilder accumulates children on
// reused scratch stacks, then on finish commits them contiguously into a reused POOL and returns a
// span VIEW (ArrayView/ObjectView) — zero per-container heap allocation. The pools persist across
// parses (one PoolBuilder, reused by a Parser), so after warm-up allocations amortize to ~0
// (the reusable-parser model). No runtime polymorphism — a plain policy type.

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "document.hpp"  // Node, Member, ArrayView, ObjectView (via node.hpp)

namespace cheatah::parsers::json {

/**
 * @brief The construction policy for the POOLED JSON parse path: a stack machine
 *        (begin/add/finish) that accumulates children on reused scratch stacks and commits them
 *        contiguously into reused pools, returning span VIEWS (ArrayView/ObjectView) — zero
 *        per-container heap allocation, amortized to ~0 across parses. A plain policy type (no
 *        runtime polymorphism).
 */
class PoolBuilder {
public:
    /// False: unescaped strings are zero-copy String<std::string_view> into the SOURCE text, so a
    /// Document from this pooled path is valid only while the source (and these pools) outlive it.
    static constexpr bool owns_strings = false;

    /**
     * Prepare for a fresh parse: drop the previous result but keep capacity, then reserve a safe
     * upper bound so the pools never reallocate mid-parse (every committed span stays valid). The
     * bounds are structural: an array element costs >= 2 input bytes ("0," / "0]") and an object
     * member >= 4 (`"":0`), so size/2 nodes and size/4 members can never be exceeded. NOTE this is
     * still O(input) memory reserved UP FRONT (~24x the text size) — for untrusted input, bound the
     * document size before parsing, or use the owning path, which allocates only what the document
     * actually contains. Reused capacity makes this ~free once warm.
     *
     * @param text_size the byte length of the source about to be parsed (sizes the pool reserve).
     * @complexity O(1) amortized (no realloc after warm-up)
     * @alloc grows the pools once (~text_size/2 nodes + text_size/4 members); reused thereafter
     * @test Json.ParseObject
     */
    void reset(std::size_t text_size) {
        node_pool_.clear();
        member_pool_.clear();
        node_scratch_.clear();
        member_scratch_.clear();
        open_bases_.clear();
        node_pool_.reserve(text_size / 2 + 1);
        member_pool_.reserve(text_size / 4 + 1);
    }

    /// Open an array: remember where its elements start on the node scratch stack.
    /// @complexity O(1)  @alloc amortized open_bases_ growth  @test Json.PooledForms
    void begin_array() { open_bases_.push_back(node_scratch_.size()); }
    /// Open an object: remember where its members start on the member scratch stack.
    /// @complexity O(1)  @alloc amortized open_bases_ growth  @test Json.PooledForms
    void begin_object() { open_bases_.push_back(member_scratch_.size()); }

    /// Append an element to the innermost open array.
    /// @param n the element node to append (moved in).
    /// @complexity O(1)  @alloc amortized scratch growth  @test Json.PooledForms
    void add_element(Node&& n) { node_scratch_.push_back(std::move(n)); }
    /// Append a member to the innermost open object.
    /// @param m the key/value member to append (moved in).
    /// @complexity O(1)  @alloc amortized scratch growth  @test Json.PooledForms
    void add_member(Member&& m) { member_scratch_.push_back(std::move(m)); }

    /**
     * Close the innermost open array: commit its children into the pool and wrap them in an
     * ArrayView span.
     * @return a Node holding the closed array as an ArrayView into the pool.
     * @complexity O(children) @alloc none (pool is pre-reserved) @test Json.ParseObject
     */
    Node finish_array() {
        const std::size_t base = open_bases_.back();
        open_bases_.pop_back();
        Node n;
        n.variant().emplace<ArrayView>(commit_nodes(base));
        return n;
    }
    /**
     * Close the innermost open object: commit its members into the pool and wrap them in an
     * ObjectView span.
     * @return a Node holding the closed object as an ObjectView into the pool.
     * @complexity O(members) @alloc none @test Json.ParseObject
     */
    Node finish_object() {
        const std::size_t base = open_bases_.back();
        open_bases_.pop_back();
        Node n;
        n.variant().emplace<ObjectView>(commit_members(base));
        return n;
    }

private:
    // Move the scratch-stack tail [base, size) contiguously into the pool, pop it off the stack,
    // and return a stable span view of it. The pool is reserved, so push_back never reallocates.
    // @complexity O(children)  @alloc none (pool pre-reserved)  @test Json.PooledForms
    std::span<const Node> commit_nodes(std::size_t base) {
        const std::size_t count = node_scratch_.size() - base;
        const std::size_t offset = node_pool_.size();
        for (std::size_t i = base; i < node_scratch_.size(); ++i) {
            node_pool_.push_back(std::move(node_scratch_[i]));
        }
        node_scratch_.resize(base);
        return std::span<const Node>(node_pool_.data() + offset, count);
    }
    std::span<const Member> commit_members(std::size_t base) {
        const std::size_t count = member_scratch_.size() - base;
        const std::size_t offset = member_pool_.size();
        for (std::size_t i = base; i < member_scratch_.size(); ++i) {
            member_pool_.push_back(std::move(member_scratch_[i]));
        }
        member_scratch_.resize(base);
        return std::span<const Member>(member_pool_.data() + offset, count);
    }

    std::vector<Node> node_pool_;          // array elements (stable; ArrayView spans point here)
    std::vector<Member> member_pool_;      // object members (stable; ObjectView spans point here)
    std::vector<Node> node_scratch_;       // build stack for in-progress array elements
    std::vector<Member> member_scratch_;   // build stack for in-progress object members
    std::vector<std::size_t> open_bases_;  // scratch base of each currently-open container (LIFO)
};

}  // namespace cheatah::parsers::json
