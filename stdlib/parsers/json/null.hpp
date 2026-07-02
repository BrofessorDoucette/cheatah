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
     */
    [[nodiscard]] int* value() const noexcept { return nullptr; }

};

}  // namespace cheatah::parsers::json
