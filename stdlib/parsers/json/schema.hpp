#pragma once

// cheatah::parsers::json — compile-time SCHEMA for reading JSON straight into user structs.
//
// A struct opts in NON-INTRUSIVELY by specializing the `schema<T>` variable template with an
// `object(field(...), ...)` description — no base class, no macro, no member function, so it works
// for third-party / aggregate types you cannot edit:
//
//   struct Ohlc { std::string date; double open; double close; };
//   template<> inline constexpr auto schema<Ohlc> = object(
//       field("date",  &Ohlc::date),
//       field("open",  &Ohlc::open),
//       field("close", &Ohlc::close));
//
// read.hpp consumes this at compile time to parse directly into the fields (no Node/variant DOM).
// Fully static; no runtime polymorphism.

#include <string_view>
#include <tuple>
#include <type_traits>

namespace cheatah::parsers::json {

/**
 * @brief One mapping from a JSON object key to a struct data member (a key name plus a
 *        pointer-to-member).
 * @tparam Class the struct type owning the member.
 * @tparam Member the member's value type.
 */
template <class Class, class Member>
struct Field {
    std::string_view name;   ///< the JSON object key this field maps to.
    Member Class::*ptr;      ///< pointer to the target data member of @p Class.
    using class_type = Class;    ///< the struct type owning the member.
    using member_type = Member;  ///< the member's value type.
};

// Build one key->member mapping. @complexity O(1) constexpr  @alloc none  @test JsonRead.NestedStructs
template <class Class, class Member>
constexpr Field<Class, Member> field(std::string_view name, Member Class::*ptr) noexcept {
    return Field<Class, Member>{name, ptr};
}

/**
 * @brief A struct's schema: an ordered list of Fields. Declaration order is cosmetic — at parse
 *        time each JSON key is matched to a field BY NAME, so the JSON's key order does not matter.
 * @tparam Fields the Field types making up the schema.
 */
template <class... Fields>
struct ObjectSchema {
    std::tuple<Fields...> fields;  ///< the field mappings, held as a tuple.
};

// Bundle fields into a schema. @complexity O(1) constexpr  @alloc none  @test JsonRead.NestedStructs
template <class... Fields>
constexpr ObjectSchema<Fields...> object(Fields... fs) noexcept {
    return ObjectSchema<Fields...>{std::tuple<Fields...>{fs...}};
}

// Sentinel type for "this T has no schema". The primary template yields it; a user gives T a schema
// by specializing: `template<> inline constexpr auto schema<Foo> = object(...);`.
struct no_schema {};
template <class T>
inline constexpr no_schema schema{};

// True iff T has a user-declared schema specialization (so read<>() treats it as a JSON object).
template <class T>
concept HasSchema = !std::is_same_v<std::remove_cvref_t<decltype(schema<T>)>, no_schema>;

}  // namespace cheatah::parsers::json
