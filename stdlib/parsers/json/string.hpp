// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — the JSON string token (decoded, un-escaped). See json.hpp.

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cheatah::parsers::json {

// String holds its characters one of two ways, chosen by the storage type S, which is DEDUCED
// from the constructor argument (see the deduction guides below):
//
//   - String<std::string_view> — a non-owning VIEW. Zero-copy. This is what a const char* /
//     string literal or an existing buffer becomes. The viewed characters must outlive the
//     String; string literals live in the binary for the whole program, so String{"x"} can
//     refer to that const data as its own value, safely, forever.
//   - String<std::string>      — OWNS its characters (an rvalue std::string moved in).
//
// A const char* is ALWAYS wrapped in a std::string_view and is NEVER silently copied into a
// std::string. S is constrained to exactly those two storage types.
// Valid backing storage for a String: either owning (std::string) or non-owning (std::string_view).
template <typename S>
concept StringStorage = std::is_same_v<S, std::string> || std::is_same_v<S, std::string_view>;

/**
 * @brief A decoded (un-escaped) JSON string token, storing its characters either owned
 *        (String<std::string>) or as a zero-copy view (String<std::string_view>), chosen by
 *        the storage type @p S via the deduction guides below.
 * @tparam S the backing storage: std::string (owning) or std::string_view (viewing).
 */
template <StringStorage S>
class String {
private:
    S value_;

public:
    /**
     * Construct from the backing storage. Owning S=std::string moves the rvalue in; viewing
     * S=std::string_view just stores the (cheap) view — the characters are never copied here.
     * @param value the string storage (moved in).
     * @complexity O(1) — a std::string move or a std::string_view copy; characters are never copied.
     * @alloc none.
     * @test CheatahParsersJson.ContainerTokenLifecycle
     */
    String(S value) : value_(std::move(value)) {}
    ~String() = default;

    /**
     * Read the characters uniformly as a view, whether owning or viewing (no setter).
     * @return a std::string_view over the stored characters.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahParsersJson.ContainerTokenLifecycle
     */
    [[nodiscard]] std::string_view value() const noexcept { return value_; }
};

/**
 * Deduction guides — route each argument to the right storage. A const char* and a
 * std::string_view become VIEWS (never a std::string); an rvalue std::string is OWNED. These
 * user-defined guides are preferred over the implicit one, so String{"lit"} deduces
 * String<std::string_view>, not String<const char*>.
 * @complexity O(1) — deduction is compile-time; the chosen constructor moves or views.
 * @alloc none.
 * @test CheatahParsersJson.ContainerTokenLifecycle
 */
String(const char*) -> String<std::string_view>;
/** @copydoc String(const char*) */
String(std::string_view) -> String<std::string_view>;
/** @copydoc String(const char*) */
String(std::string) -> String<std::string>;

}  // namespace cheatah::parsers::json
