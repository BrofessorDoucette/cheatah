// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — the JSON number token (one numeric type: IEEE-754 double). See json.hpp.

namespace cheatah::parsers::json {

/** @brief A JSON number token (one numeric type: IEEE-754 double), e.g. Number{3.5}. */
class Number {
private:
    double value_ {0.0};

public:
    /**
     * Construct from a double.
     * @param value the numeric value.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahParsersJson.TokenClassesAndNodeVariant
     */
    Number(double value) : value_(value) {}
    ~Number() = default;


    /**
     * Read the numeric value (fixed for the token's lifetime; no setter).
     * @return the stored double.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahParsersJson.TokenClassesAndNodeVariant
     */
    [[nodiscard]] double value() const noexcept { return value_; }
};

}  // namespace cheatah::parsers::json
