// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — the JSON `null` token. One class per JSON kind; see json.hpp.

namespace cheatah::parsers::json {

/** @brief The JSON `null` literal token. */
class Null {

public:

    Null() = default;
    ~Null() = default;

    /**
     * The value of `null` (there is no data; provided for a uniform value() interface).
     * @return always nullptr.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahParsersJson.TokenClassesAndNodeVariant
     */
    [[nodiscard]] int* value() const noexcept { return nullptr; }

};

}  // namespace cheatah::parsers::json
