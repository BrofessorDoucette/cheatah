// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — the JSON boolean token (`true` / `false`). See json.hpp.

namespace cheatah::parsers::json {

/** @brief A JSON `true` / `false` token, constructed from its bool: Boolean{true}. */
class Boolean {
private:

    bool value_ {false};

public:
    /**
     * Construct from a bool.
     * @param value the boolean value (`true` or `false`).
     */
    Boolean(bool value) : value_(value) {}

    /**
     * Read the boolean value (fixed for the token's lifetime; no setter).
     * @return the stored bool.
     */
    [[nodiscard]] bool value() const noexcept { return value_; }
};

}  // namespace cheatah::parsers::json
