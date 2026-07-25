// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — a from-scratch JSON parser (pure C++, no deps).
//
// This header declares the public API; the implementation lives in json.cpp. The value model is
// Node (json/node.hpp) — a class wrapping a std::variant; access its alternatives via .variant().
//
// Two parse paths trade copying for lifetime (chosen by the Builder, no runtime polymorphism):
//   * Parser::parse (pooled) is ZERO-COPY — an unescaped string is a String<std::string_view> into
//     the SOURCE text, so `text` (and the Parser's pools) must outlive the Document.
//   * the free parse() / Parser::parse_owning produce a SELF-CONTAINED Document — strings are
//     copied into owned String<std::string> — safe to return, cache, or outlive `text`.
// An escaped string is always decoded into owned storage. No runtime polymorphism anywhere.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursor.hpp"        // Cursor (used by Parser's private parse methods)
#include "document.hpp"      // Document (= Node) + node.hpp
#include "pool_builder.hpp"  // PoolBuilder (the pooled construction policy the Parser owns)

namespace cheatah::parsers::json {

/**
 * Read a Node's characters when it is a string (either backing), else an empty view.
 *
 * @complexity O(1)
 * @alloc none
 * @test ParsersJsonDom.ToViewReadsBothBackingsAndRejectsNonStrings
 */
[[nodiscard]] std::string_view to_view(const Node& value) noexcept;

/**
 * Parse `text` into a SELF-CONTAINED Document (owning containers AND owned strings — every string
 * is copied, not a view), so the result is safe to return, cache, or outlive `text`. On success
 * *ok is set true; on malformed input *ok is set false and the result is JSON null. For the
 * zero-copy, source-viewing form (no string copies), reuse a Parser and call Parser::parse.
 *
 * Validate is a COMPILE-TIME switch: the default (true) does full bounds/structure checking and
 * rejects malformed input; parse<false>(...) strips every such check from the binary via
 * `if constexpr` (see the Parser docs) and does not write *ok — for trusted, known-well-formed
 * input only, as feeding it malformed input is undefined behavior.
 *
 * @complexity O(n) in the input length
 * @alloc allocates the owned document tree (arrays, objects, and copied strings)
 * @test ParsersJsonDom.ParsesEveryScalarKind
 */
template <bool Validate = true>
[[nodiscard]] Document parse(std::string_view text, bool* ok = nullptr);

/**
 * Serialize a Document to compact JSON text (string contents are re-escaped).
 *
 * @complexity O(nodes)
 * @alloc the returned string
 * @test ParsersJsonDom.OwningContainersAndDumpRoundTrip
 */
[[nodiscard]] std::string dump(const Document& value);

/**
 * Serialize a Document by APPENDING to the caller's buffer — stream into a preallocated/reused
 * std::string rather than allocating a fresh one. This is the efficient path (push_back/append
 * into one growing buffer); it deliberately avoids std::stringstream, which adds formatting,
 * locale, and virtual-streambuf overhead per write. Reserve `out` once and reuse it across calls.
 *
 * @complexity O(nodes)
 * @alloc none of its own (grows `out` only if its capacity is exceeded)
 * @test ParsersJsonDom.OwningContainersAndDumpRoundTrip
 */
void dump(const Document& value, std::string& out);

/**
 * A reusable parser that owns reusable node/member POOLS. Parser::parse() builds a Document whose
 * arrays/objects are VIEWS (ArrayView/ObjectView) into these pools — zero per-container heap
 * allocation. Reusing ONE Parser across many parses amortizes the pool allocation/page-faults to
 * ~0 after warm-up (the reusable-parser model), which is the whole point of option B.
 *
 * LIFETIME: the returned Document VIEWS this Parser's pools, so it is valid only until the next
 * parse() on this Parser, and only while the Parser is alive. (For a self-contained, owning
 * Document — e.g. for the cache — use the free parse() above instead.) No runtime polymorphism.
 *
 * VALIDATION: every parse method takes a compile-time `bool Validate` template parameter, defaulted
 * to true. With Validate=true the grammar checks bounds/structure and rejects malformed input
 * (result JSON null, *ok=false). With Validate=false those checks are guarded by `if constexpr` and
 * therefore removed from the binary ENTIRELY — there is no runtime flag and no branch, and *ok is
 * not written at all (an unchecked parse has no validity to report). The unchecked form is for
 * trusted, known-well-formed input (e.g. our own cache); feeding it malformed input is undefined
 * behavior. Call it as p.parse<false>(text) / p.parse_owning<false>(text).
 *
 * @complexity O(n) in the input length
 * @alloc the pools, reused across parses (amortized ~0 after warm-up); owned only for escaped
 *   strings
 * @test ParsersJsonDom.PooledParserYieldsViewsIntoSource
 */
class Parser {
public:
    /**
     * Parse @p text into a Document whose arrays/objects are VIEWS (ArrayView/ObjectView) into this
     * Parser's reused pools — zero per-container allocation, amortized to ~0 across parses. The
     * result is valid only until the next parse or dump() on this Parser, and while the Parser lives.
     * @tparam Validate when true (default) reject malformed input; when false all bounds/structure
     *   checks are compiled out (trusted, known-well-formed input only — see the class doc).
     * @param text the JSON source to parse.
     * @param ok if non-null, set to true on success and false on a parse error (only written when
     *   Validate is true).
     * @return the parsed Document (JSON null on error when validating).
     * @complexity O(|text|)
     * @alloc none after warm-up (reused pools); owned only for escaped strings
     * @test ParsersJsonDom.PooledParserYieldsViewsIntoSource
     */
    template <bool Validate = true>
    [[nodiscard]] Document parse(std::string_view text, bool* ok = nullptr);

    /**
     * Parse @p text into a self-contained OWNING Document (OwnedArray/OwnedObject AND owned
     * String<std::string> — strings are copied, not views), fully independent of this Parser and of
     * @p text once returned. This is what the free parse() and the cache use.
     * @tparam Validate as for parse().
     * @param text the JSON source to parse.
     * @param ok if non-null, set to true on success and false on a parse error (Validate=true only).
     * @return a self-contained parsed Document (JSON null on error when validating).
     * @complexity O(|text|)
     * @alloc allocates the owned document tree (arrays, objects, and copied strings)
     * @test ParsersJsonDom.OwningParseOutlivesItsSource
     * @crtest ParsersCompileRun.JsonDomParse
     */
    template <bool Validate = true>
    [[nodiscard]] Document parse_owning(std::string_view text, bool* ok = nullptr);

    /**
     * Serialize @p value into the Parser's own REUSED buffer and return a view of it — no per-call
     * allocation after warm-up. The view is valid until the next dump() on this Parser.
     * @param value the document to serialize.
     * @return a std::string_view of the serialized JSON (valid until the next dump()).
     * @complexity O(output size)
     * @alloc none after warm-up (the buffer is reused)
     * @test ParsersJsonDom.PooledParserYieldsViewsIntoSource
     */
    [[nodiscard]] std::string_view dump(const Document& value);

private:
    // ONE ITERATIVE grammar (this IS "all parsing in one class") — the recursion is unrolled into a
    // loop over an explicit frame_stack_, so nesting depth costs heap, not C++ call frames (no stack
    // overflow on adversarially deep input). It is parameterized on (1) a compile-time `bool
    // Validate` that `if constexpr`-gates every bounds/structure check, and (2) a Builder policy
    // (PoolBuilder -> ArrayView/ObjectView, or OwningBuilder -> OwnedArray/OwnedObject) driven as a
    // stack machine (begin/add/finish). Compile-time dispatch; no runtime polymorphism. Defined and
    // instantiated for both validation modes and both builders in json.cpp.
    template <bool Validate, class Builder>
    bool parse_value(Cursor& c, Node& out, Builder& b);

    // One open container on the parse stack. The Builder owns the partial container itself; the
    // grammar only needs to know whether it is an object (so it reads keys) and, for an object, the
    // pending key awaiting its value.
    struct Frame {
        bool is_object;
        Node key;  // the in-progress object key (unused for arrays)
    };

    PoolBuilder pool_;                // pooled construction policy, reused across parse() (view path)
    std::vector<Frame> frame_stack_;  // explicit recursion stack, reused across parses
    std::string dump_buf_;            // reused serialization buffer for dump()
};

}  // namespace cheatah::parsers::json
