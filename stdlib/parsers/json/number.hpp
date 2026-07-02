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
     */
    Number(double value) : value_(value) {}
    ~Number() = default;


    /**
     * Read the numeric value (fixed for the token's lifetime; no setter).
     * @return the stored double.
     */
    [[nodiscard]] double value() const noexcept { return value_; }
};

}  // namespace cheatah::parsers::json
