// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — Cursor: the parser's read position over the source text, the half-open
// range [it, end). A plain aggregate; the recursive-descent parser advances `it` toward `end`.

namespace cheatah::parsers::json {

/**
 * @brief The parser's read position over the source text as a half-open range [it, end);
 *        the recursive-descent parser advances @c it toward @c end.
 */
struct Cursor {
    const char* it;   ///< the current read position (advances during parsing).
    const char* end;  ///< one past the last byte of the source text.
};

}  // namespace cheatah::parsers::json
