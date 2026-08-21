// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// bench_pairs — the ONE place that knows how a cheatah benchmark is paired with its
// competitor twin.
//
// The suffix convention differs per file for historical reasons, and renaming is not an
// option: scripts/bench_gate.sh greps `_(fixed|glm)$` to build its shards, and
// tests/benchmarks/compiler_bench_baseline.csv is keyed by benchmark name. So instead of
// one convention enforced by renaming, this header holds the whole table:
//
//     eigen_compare_bench.cpp    BM_<op>_cheatah/<n>   vs  BM_<op>_eigen/<n>
//     glm_compare_bench.cpp      BM_<op><n>_cheatah    vs  BM_<op><n>_glm
//     fixed_glm_bench.cpp        BM_<op>_<type>_fixed  vs  BM_<op>_<type>_glm
//     crypto_openssl_bench.cpp   BM_Crypto<P>_Cheatah  vs  BM_Crypto<P>_OpenSSL
//
// A name splits into <stem>/<args>; the last '_'-delimited token of the stem names the
// side. Comparison is case-insensitive, because the crypto file capitalizes and the
// numeric files do not.
//
// The landmine this header exists to defuse: BM_CryptoChaCha20Poly1305_Cheatah_Into ends
// in `Into`, which is not a side token at all. Its side is the token BEFORE that. Treated
// naively it would either be dropped or — worse — read as a rival of the plain
// `_Cheatah` row and reported as cheatah losing to itself. Here it classifies as OURS
// with variant `Into`, and the variant stays in the pair key so it can never pair with
// anything but an identically-named rival (there is none, so it simply reports alone).
#ifndef CHEATAH_TESTS_BENCHMARKS_BENCH_PAIRS_HPP
#define CHEATAH_TESTS_BENCHMARKS_BENCH_PAIRS_HPP

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace cheatah::bench {

// Which library a benchmark row measures.
enum class Side { Ours, Rival, Unknown };

struct Pairing {
    Side side = Side::Unknown;
    std::string key;    // name with the side token removed; two sides of a pair share it
    std::string label;  // the rival's display name ("glm", "eigen", "openssl", …); ours: ""
};

namespace detail {

// Side tokens, matched case-insensitively against the stem's last '_'-delimited token.
inline constexpr std::array<std::string_view, 2> kOurs{"cheatah", "fixed"};
inline constexpr std::array<std::string_view, 6> kRivals{"eigen", "glm",   "openssl",
                                                         "std",   "boost", "re2"};

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

// The canonical lowercase label for a token, or "" when the token names no side.
inline std::string_view classify(std::string_view token, Side& side) {
    for (std::string_view o : kOurs) {
        if (iequals(token, o)) { side = Side::Ours; return o; }
    }
    for (std::string_view r : kRivals) {
        if (iequals(token, r)) { side = Side::Rival; return r; }
    }
    side = Side::Unknown;
    return {};
}

}  // namespace detail

// How a rival's name is written in a published table. `classify_benchmark` returns the
// lowercase token it matched on (that is what pairing needs); a reader wants the library's
// own capitalisation. Kept beside kRivals so the vocabulary has exactly one home.
inline std::string display_label(std::string_view label) {
    if (detail::iequals(label, "glm")) return "GLM";
    if (detail::iequals(label, "openssl")) return "OpenSSL";
    if (detail::iequals(label, "eigen")) return "Eigen";
    if (detail::iequals(label, "re2")) return "RE2";
    if (detail::iequals(label, "std")) return "std::regex";
    if (detail::iequals(label, "boost")) return "Boost";
    return std::string(label);
}

// Classify one benchmark name. `name` is Run::run_name.str() — it carries any `/args`
// suffix but NOT the `_median`/`_mean` aggregate suffix (that lives on Run::aggregate_name).
inline Pairing classify_benchmark(std::string_view name) {
    Pairing p;

    // Split <stem>/<args>. Args stay attached to the key so BM_dot_cheatah/64 pairs with
    // BM_dot_eigen/64 and never with BM_dot_eigen/4096.
    const std::size_t slash = name.find('/');
    const std::string_view stem = name.substr(0, slash);
    const std::string_view args = (slash == std::string_view::npos) ? std::string_view{}
                                                                   : name.substr(slash);

    const std::size_t under = stem.rfind('_');
    if (under == std::string_view::npos) return p;

    Side side = Side::Unknown;
    std::string_view label = detail::classify(stem.substr(under + 1), side);

    if (side == Side::Unknown) {
        // Not a side token — this may be a VARIANT of ours, e.g. `..._Cheatah_Into`. Look
        // one token further left; if that names a side, the trailing token is the variant.
        const std::size_t prev = stem.substr(0, under).rfind('_');
        if (prev == std::string_view::npos) return p;
        const std::string_view variant = stem.substr(under + 1);
        label = detail::classify(stem.substr(prev + 1, under - prev - 1), side);
        if (side == Side::Unknown) return p;
        // Keep the variant IN the key: a variant must never pair with the plain row.
        p.side = side;
        p.key = std::string(stem.substr(0, prev)) + "_" + std::string(variant) + std::string(args);
        p.label = (side == Side::Rival) ? std::string(label) : std::string{};
        return p;
    }

    p.side = side;
    p.key = std::string(stem.substr(0, under)) + std::string(args);
    p.label = (side == Side::Rival) ? std::string(label) : std::string{};
    return p;
}

}  // namespace cheatah::bench

#endif  // CHEATAH_TESTS_BENCHMARKS_BENCH_PAIRS_HPP
