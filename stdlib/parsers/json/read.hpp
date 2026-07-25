// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json — read<T>(): parse JSON DIRECTLY into a typed struct, no Node/variant DOM.
//
//   struct Series { std::string symbol; std::vector<Ohlc> data; };   // + schema<> specializations
//   Series s;
//   if (json::read(text, s)) use(s);
//
// Each field is dispatched on its STATIC type (the concept set below), so a number lands in its
// typed field via std::from_chars with no intermediate double, a string is copied (std::string) or
// viewed (std::string_view), nested structs recurse on their schema, std::vector<T> reads a
// variable-length array, std::array<T, N> reads a FIXED N-element array inline (no allocation), and
// std::optional<T> reads JSON null as nullopt. Unknown keys are skipped. Validate is the same
// compile-time switch as the DOM parser: with Validate=false every bounds/structure check is
// `if constexpr`-removed for trusted input (UB on malformed input). No runtime polymorphism.
//
// The data-structure recursion (nested structs / vectors) is bounded by the SCHEMA depth, which is
// fixed at compile time — not attacker-controlled — so natural recursion here cannot be overflowed
// by deep input. (Skipping an unknown value of attacker-controlled depth uses detail::skip_value,
// which is iterative.)

#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "cursor.hpp"
#include "scan.hpp"
#include "schema.hpp"

namespace cheatah::parsers::json {

namespace detail {

template <class>
struct is_vector : std::false_type {};
template <class E, class A>
struct is_vector<std::vector<E, A>> : std::true_type {};

template <class>
struct is_std_array : std::false_type {};
template <class E, std::size_t N>
struct is_std_array<std::array<E, N>> : std::true_type {};

template <class>
struct is_optional : std::false_type {};
template <class E>
struct is_optional<std::optional<E>> : std::true_type {};

// Mutually-recursive workers (value -> array/object -> value). Declared first so they can call one
// another regardless of definition order.
//
// read_value is FORCE-inlined: the per-instantiation call graph is acyclic (a type cannot contain
// itself), so this is safe, and it matters — the optimizer's size heuristic otherwise outlines the
// scalar instantiations (read_value<long>, read_value<string_view>), adding a function-call round
// trip to every scalar member; inlining the whole hot path removes it.
// @complexity O(value size)  @alloc only what the field type owns  @test JsonRead.Scalars
template <bool Validate, class T>
[[gnu::always_inline]] inline bool read_value(Cursor& c, T& out);
template <bool Validate, class Vec>
bool read_array(Cursor& c, Vec& out);
template <bool Validate, class Arr>
bool read_fixed_array(Cursor& c, Arr& out);
template <bool Validate, class T, class... Fields>
bool read_object(Cursor& c, T& out, const ObjectSchema<Fields...>& sch);

// Read a JSON string body into an owned std::string (decoding escapes) or a zero-copy view (only
// when unescaped — an escaped string has no contiguous source to view).
// @complexity O(|string|)  @alloc only for an owned std::string  @test JsonRead.ViewVsOwned
template <bool Validate, class Str>
bool read_string(Cursor& c, Str& out) {
    if constexpr (Validate) {
        if (c.it == c.end || *c.it != '"') {
            return false;
        }
    }
    std::string_view raw;
    bool esc = false;
    if (!scan_string(c, raw, esc)) {
        return false;
    }
    if constexpr (std::is_same_v<Str, std::string_view>) {
        if (esc) {
            return false;  // cannot view an escaped string; declare the field std::string instead
        }
        out = raw;
        return true;
    } else {  // std::string (owned)
        if (!esc) {
            out.assign(raw);
            return true;
        }
        return decode_escapes(raw, out);
    }
}

template <bool Validate, class T>
inline bool read_value(Cursor& c, T& out) {
    skip_ws(c);
    if constexpr (Validate) {
        if (c.it == c.end) {
            return false;
        }
    }
    if constexpr (std::is_same_v<T, bool>) {
        if (match(c, "true")) {
            out = true;
            return true;
        }
        if (match(c, "false")) {
            out = false;
            return true;
        }
        return false;
    } else if constexpr (std::is_arithmetic_v<T>) {  // integral or floating (bool handled above)
        return parse_arithmetic(c, out);
    } else if constexpr (std::is_same_v<T, std::string> ||
                         std::is_same_v<T, std::string_view>) {
        return read_string<Validate>(c, out);
    } else if constexpr (is_optional<T>::value) {
        if (match(c, "null")) {
            out.reset();
            return true;
        }
        return read_value<Validate>(c, out.emplace());
    } else if constexpr (is_std_array<T>::value) {
        return read_fixed_array<Validate>(c, out);  // fixed N elements, inline (no allocation)
    } else if constexpr (is_vector<T>::value) {
        return read_array<Validate>(c, out);
    } else if constexpr (HasSchema<T>) {
        return read_object<Validate>(c, out, schema<T>);
    } else {
        static_assert(sizeof(T) == 0,
                      "json::read: type is not a supported scalar/container and has no schema<T>");
        return false;
    }
}

// Read a variable-length JSON array into a std::vector, element by element, in place.
// @complexity O(array size)  @alloc vector growth (capacity reused via clear)  @test JsonRead.Vectors
template <bool Validate, class Vec>
bool read_array(Cursor& c, Vec& out) {
    if constexpr (Validate) {
        if (c.it == c.end || *c.it != '[') {
            return false;
        }
    }
    ++c.it;  // skip '['
    skip_ws(c);
    if constexpr (Validate) {
        if (c.it == c.end) {
            return false;
        }
    }
    out.clear();
    if (*c.it == ']') {
        ++c.it;
        return true;  // empty array
    }
    for (;;) {
        // Construct the element in place and read straight into it — no temporary, no move.
        if (!read_value<Validate>(c, out.emplace_back())) {
            return false;
        }
        skip_ws(c);
        if constexpr (Validate) {
            if (c.it == c.end) {
                return false;
            }
        }
        const char sep = *c.it++;
        if (sep == ']') {
            return true;
        }
        if (sep != ',') {
            return false;  // expected ',' or ']'
        }
    }
}

// Read a JSON array of EXACTLY N elements into a std::array<E, N> — fixed size, stored inline, so
// it never allocates. A wrong element count (too few / too many) is a parse error under Validate.
// @complexity O(N)  @alloc none  @test JsonRead.FixedArrays
template <bool Validate, class Arr>
bool read_fixed_array(Cursor& c, Arr& out) {
    constexpr std::size_t kSize = std::tuple_size_v<Arr>;
    if constexpr (Validate) {
        if (c.it == c.end || *c.it != '[') {
            return false;
        }
    }
    ++c.it;  // skip '['
    for (std::size_t i = 0; i < kSize; ++i) {
        if (!read_value<Validate>(c, out[i])) {
            return false;
        }
        skip_ws(c);
        if constexpr (Validate) {
            // every element but the last is followed by ',', the last by ']' — anything else is
            // the wrong arity or a missing separator.
            if (c.it == c.end || *c.it != (i + 1 == kSize ? ']' : ',')) {
                return false;
            }
        }
        ++c.it;  // consume the ',' (between elements) or the closing ']' (after the last)
    }
    if constexpr (kSize == 0) {  // std::array<E, 0>: no elements, so still consume the ']'
        skip_ws(c);
        if constexpr (Validate) {
            if (c.it == c.end || *c.it != ']') {
                return false;
            }
        }
        ++c.it;
    }
    return true;
}

// Match `key` against the schema's fields; read the value into the matching member, or skip it.
// Read one complete `"key": value` member into the matching field of `out`. On entry the cursor is
// at the opening quote of the key (or the whitespace before it on the general path).
//
// FAST PATH — predicted-literal match: `hint` names the field we EXPECT next (the one after the
// previous match), because real JSON nearly always lists keys in schema order. We compare the
// input bytes directly against that field's `"name":` literal — one short memcmp replaces the
// whole key pipeline (quote scan, key compare, colon handling). In-order compact input takes this
// path for every member. Anything else — whitespace inside the member syntax, out-of-order,
// unknown, or escaped keys — leaves the cursor untouched and falls back to the general scan.
// @complexity O(|key| + value)  @alloc only a temp std::string when the key itself is escaped (rare); otherwise none of its own  @test JsonRead.OutOfOrderKeys
template <bool Validate, class T, class... Fields>
bool read_one_member(Cursor& c, T& out, const ObjectSchema<Fields...>& sch, std::size_t& hint) {
    constexpr std::size_t kCount = sizeof...(Fields);
    bool ok = true;
    std::size_t matched = kCount;
    const auto read_into = [&](const auto& f, std::size_t index) {
        matched = index;
        ok = read_value<Validate>(c, out.*(f.ptr));
    };

    // ---- fast path: does the input start with the predicted field's `"name":` bytes? ----
    const auto try_predicted = [&](const auto& f, std::size_t index) {
        const std::string_view name = f.name;
        const char* const p = c.it;
        if (static_cast<std::size_t>(c.end - p) < name.size() + 3) {
            return false;  // not enough bytes for `"name":` — let the general path decide
        }
        if (p[0] != '"' || p[name.size() + 1] != '"' || p[name.size() + 2] != ':' ||
            std::memcmp(p + 1, name.data(), name.size()) != 0) {
            return false;  // not the predicted key (cursor untouched)
        }
        c.it = p + name.size() + 3;  // step past `"name":` in one go
        read_into(f, index);
        return true;
    };
    const auto predicted = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (((Is == hint) && try_predicted(std::get<Is>(sch.fields), Is)) || ...);
    };

    if (!predicted(std::make_index_sequence<kCount>{})) {
        // ---- general path: scan the key, then match it against the fields ----
        if constexpr (Validate) {
            if (c.it == c.end || *c.it != '"') {
                return false;  // key must be a string
            }
        }
        std::string_view key;
        bool esc = false;
        if (!scan_string(c, key, esc)) {
            return false;
        }
        std::string key_decoded;  // only used when the key itself contains escapes (rare)
        if (esc) {
            if (!decode_escapes(key, key_decoded)) {
                return false;
            }
            key = key_decoded;
        }
        skip_ws(c);
        if constexpr (Validate) {
            if (c.it == c.end || *c.it != ':') {
                return false;
            }
        }
        ++c.it;  // skip ':'
        const auto try_field = [&](const auto& f, std::size_t index) {
            if (f.name != key) {
                return false;  // not this field — keep looking
            }
            read_into(f, index);
            return true;  // matched — stop the fold
        };
        // Search [hint, N) first, then wrap to [0, hint) — the `Is >= hint` guards on skipped
        // indices are just integer compares, and the `||` folds short-circuit at the first match.
        const auto search = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return ((Is >= hint && try_field(std::get<Is>(sch.fields), Is)) || ...) ||
                   ((Is < hint && try_field(std::get<Is>(sch.fields), Is)) || ...);
        };
        if (!search(std::make_index_sequence<kCount>{})) {
            return skip_value(c);  // a key not in the schema: discard its value
        }
    }
    if (!ok) {
        return false;
    }
    hint = (matched + 1 == kCount) ? 0 : matched + 1;
    return true;
}

// Read a JSON object member-by-member into the schema'd struct `out`.
// @complexity O(object size)  @alloc only what the field types own  @test JsonRead.NestedStructs
template <bool Validate, class T, class... Fields>
bool read_object(Cursor& c, T& out, const ObjectSchema<Fields...>& sch) {
    if constexpr (Validate) {
        if (c.it == c.end || *c.it != '{') {
            return false;
        }
    }
    ++c.it;  // skip '{'
    skip_ws(c);
    if constexpr (Validate) {
        if (c.it == c.end) {
            return false;
        }
    }
    if (*c.it == '}') {
        ++c.it;
        return true;  // empty object
    }
    std::size_t hint = 0;  // the field we expect NEXT (keys usually arrive in schema order)
    for (;;) {
        skip_ws(c);
        if (!read_one_member<Validate>(c, out, sch, hint)) {  // `"key": value` (predicted or scanned)
            return false;
        }
        skip_ws(c);
        if constexpr (Validate) {
            if (c.it == c.end) {
                return false;
            }
        }
        const char sep = *c.it++;
        if (sep == '}') {
            return true;
        }
        if (sep != ',') {
            return false;  // expected ',' or '}'
        }
    }
}

}  // namespace detail

/**
 * Parse `text` directly into `out` (a struct with a schema<>, a supported scalar, std::vector,
 * std::array, std::optional, or std::string/std::string_view). Returns true on success. With
 * Validate=true the input is fully checked (including no trailing junk); with Validate=false every
 * bounds/structure check is compiled out for trusted, well-formed input (malformed input is then
 * undefined behavior, and no success/trailing check is performed — it always returns true).
 *
 * std::string fields OWN their characters (safe to outlive `text`); std::string_view fields VIEW
 * `text` (which must then outlive `out`) and reject escaped strings.
 *
 * @complexity O(n) in the input length
 * @alloc only the owned std::string fields / vector growth in `out` — no DOM, no Node tree
 * @test CheatahParsersJson.TypedReadWithUnknownKeys
 * @test CheatahParsersJson.TypedReadOptionalAndReject
 * @test CheatahParsersJson.TypedReadEscapesAndKeyOrder
 */
template <bool Validate = true, class T>
[[nodiscard]] bool read(std::string_view text, T& out) {
    Cursor c{text.data(), text.data() + text.size()};
    if constexpr (Validate) {
        if (!detail::read_value<true>(c, out)) {
            return false;
        }
        detail::skip_ws(c);
        return c.it == c.end;  // reject trailing junk
    } else {
        // Trusted input: read with all checks compiled out, no success test, no trailing-junk check.
        detail::read_value<false>(c, out);
        return true;
    }
}

}  // namespace cheatah::parsers::json
