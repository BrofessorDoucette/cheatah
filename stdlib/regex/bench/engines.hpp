// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// Uniform engine adapters so every benchmark body and parity check is engine-generic:
// cheatah::regex, std::regex, boost::regex and Google RE2 behind one static interface.
//
// TIMED RE2 rows use Re2Def — RE2's out-of-box configuration (UTF-8, leftmost-first), the
// configuration real RE2 users get, so the timing comparison is honest. OFFSET/COUNT parity
// is checked against Re2Longest (longest_match + Latin-1), the configuration that matches
// cheatah's documented leftmost-LONGEST byte semantics; Re2Longest is never timed.
// std::regex and boost::regex are leftmost-first, so they participate in boolean parity only.

#include "regex.hpp"

#include <boost/regex.hpp>
#include <re2/re2.h>

#include <concepts>
#include <cstddef>
#include <memory>
#include <regex>
#include <string>
#include <string_view>

namespace eng {

// The uniform static interface every adapter satisfies. `compile` may throw (std/boost report
// bad patterns that way); `ok` is the post-construction validity check (cheatah/RE2 never throw).
// `count_all` counts non-overlapping matches left to right, advancing one byte past an empty match.
template <class E>
concept Engine = requires(const typename E::Re& re, std::string_view t, std::size_t& b, std::size_t& e) {
    { E::name } -> std::convertible_to<const char*>;
    { E::compile(std::string{}) } -> std::same_as<typename E::Re>;
    { E::ok(re) } -> std::same_as<bool>;
    { E::search(re, t) } -> std::same_as<bool>;
    { E::full(re, t) } -> std::same_as<bool>;
    { E::find(re, t, b, e) } -> std::same_as<bool>;
    { E::count_all(re, t) } -> std::same_as<std::size_t>;
};

struct Cheatah {
    static constexpr const char* name = "cheatah";
    using Re = cheatah::regex::Pattern;
    static Re compile(const std::string& p) { return cheatah::regex::compile(p); }
    static bool ok(const Re& r) { return r.ok; }
    static bool search(const Re& r, std::string_view t) { return cheatah::regex::search(r, t); }
    static bool full(const Re& r, std::string_view t) { return cheatah::regex::full_match(r, t); }
    static bool find(const Re& r, std::string_view t, std::size_t& b, std::size_t& e) {
        auto m = cheatah::regex::find(r, t);
        if (!m.matched) return false;
        b = m.begin;
        e = m.end;
        return true;
    }
    static std::size_t count_all(const Re& r, std::string_view t) {
        std::size_t n = 0, pos = 0;
        while (pos <= t.size()) {
            auto m = cheatah::regex::find(r, t.substr(pos));
            if (!m.matched) break;
            ++n;
            pos += (m.end > m.begin) ? m.end : m.begin + 1;
        }
        return n;
    }
};

struct Std {
    static constexpr const char* name = "std";
    using Re = std::regex;
    static Re compile(const std::string& p) { return std::regex(p, std::regex::optimize); }
    static bool ok(const Re&) { return true; }  // std::regex reports bad patterns by throwing
    static bool search(const Re& r, std::string_view t) {
        return std::regex_search(t.data(), t.data() + t.size(), r);
    }
    static bool full(const Re& r, std::string_view t) {
        return std::regex_match(t.data(), t.data() + t.size(), r);
    }
    static bool find(const Re& r, std::string_view t, std::size_t& b, std::size_t& e) {
        std::cmatch m;
        if (!std::regex_search(t.data(), t.data() + t.size(), m, r)) return false;
        b = static_cast<std::size_t>(m.position(0));
        e = b + static_cast<std::size_t>(m.length(0));
        return true;
    }
    static std::size_t count_all(const Re& r, std::string_view t) {
        std::size_t n = 0;
        for (std::cregex_iterator it(t.data(), t.data() + t.size(), r), end; it != end; ++it) ++n;
        return n;
    }
};

struct Boost {
    static constexpr const char* name = "boost";
    using Re = boost::regex;
    static Re compile(const std::string& p) { return boost::regex(p); }
    static bool ok(const Re&) { return true; }  // boost::regex reports bad patterns by throwing
    static bool search(const Re& r, std::string_view t) {
        return boost::regex_search(t.data(), t.data() + t.size(), r);
    }
    static bool full(const Re& r, std::string_view t) {
        return boost::regex_match(t.data(), t.data() + t.size(), r);
    }
    static bool find(const Re& r, std::string_view t, std::size_t& b, std::size_t& e) {
        boost::cmatch m;
        if (!boost::regex_search(t.data(), t.data() + t.size(), m, r)) return false;
        b = static_cast<std::size_t>(m[0].first - t.data());
        e = static_cast<std::size_t>(m[0].second - t.data());
        return true;
    }
    static std::size_t count_all(const Re& r, std::string_view t) {
        std::size_t n = 0;
        for (boost::cregex_iterator it(t.data(), t.data() + t.size(), r), end; it != end; ++it) ++n;
        return n;
    }
};

// An RE2 options provider: a name for benchmark rows plus the RE2::Options to compile with.
template <class T>
concept Re2OptsProvider = requires {
    { T::name } -> std::convertible_to<const char*>;
    { T::options() } -> std::same_as<RE2::Options>;
};

template <Re2OptsProvider Opts>
struct Re2Engine {
    static constexpr const char* name = Opts::name;
    using Re = std::unique_ptr<re2::RE2>;  // RE2 objects are non-copyable
    static Re compile(const std::string& p) { return std::make_unique<re2::RE2>(p, Opts::options()); }
    static bool ok(const Re& r) { return r->ok(); }
    static bool search(const Re& r, std::string_view t) { return re2::RE2::PartialMatch(t, *r); }
    static bool full(const Re& r, std::string_view t) { return re2::RE2::FullMatch(t, *r); }
    static bool find(const Re& r, std::string_view t, std::size_t& b, std::size_t& e) {
        std::string_view g;
        if (!r->Match(t, 0, t.size(), re2::RE2::UNANCHORED, &g, 1)) return false;
        b = static_cast<std::size_t>(g.data() - t.data());
        e = b + g.size();
        return true;
    }
    static std::size_t count_all(const Re& r, std::string_view t) {
        std::size_t n = 0, pos = 0;
        std::string_view g;
        while (pos <= t.size() && r->Match(t, pos, t.size(), re2::RE2::UNANCHORED, &g, 1)) {
            ++n;
            const std::size_t b = static_cast<std::size_t>(g.data() - t.data());
            pos = g.empty() ? b + 1 : b + g.size();
        }
        return n;
    }
};

struct Re2DefOpts {
    static constexpr const char* name = "re2";
    static RE2::Options options() {
        RE2::Options o;
        o.set_log_errors(false);
        return o;
    }
};

struct Re2LongestOpts {
    static constexpr const char* name = "re2long";
    static RE2::Options options() {
        RE2::Options o;
        o.set_log_errors(false);
        o.set_longest_match(true);
        o.set_encoding(RE2::Options::EncodingLatin1);
        return o;
    }
};

using Re2Def = Re2Engine<Re2DefOpts>;      // timed: out-of-box RE2
using Re2Longest = Re2Engine<Re2LongestOpts>;  // oracle: cheatah-equivalent semantics, never timed

// Compile through the adapter, absorbing both failure styles (throwing and `!ok`).
// Returns nullptr when the engine rejects the pattern.
template <Engine E>
std::shared_ptr<typename E::Re> try_compile(const std::string& pat) {
    try {
        auto re = std::make_shared<typename E::Re>(E::compile(pat));
        if (!E::ok(*re)) return nullptr;
        return re;
    } catch (...) {
        return nullptr;
    }
}

}  // namespace eng
