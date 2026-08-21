// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// bench_labels — human-readable display names for benchmark keys.
//
// Some published tables want a name a reader recognises ("AES-128-GCM (AES-NI + PCLMULQDQ)")
// rather than the benchmark identifier ("BM_CryptoAes128Gcm"). The mapping has to live
// SOMEWHERE, and where it lives decides whether it drifts.
//
// It lives next to the BENCHMARK() registrations, via CHEATAH_BENCH_LABEL. That placement is
// the whole point:
//
//   * deleting a benchmark deletes its label in the same edit, so a label can never outlive
//     the thing it names;
//   * adding a benchmark without a label makes the raw key appear in the published table —
//     visibly wrong, rather than a silently missing row.
//
// The alternative — a table inside bench_main.cpp keyed by strings from a file it does not
// include — is a second list that nothing keeps in step with the first. That is exactly the
// class of drift this whole benchmarking effort exists to remove, so it is not used here.
//
// Keys are PAIR keys: the side suffix (_Cheatah / _OpenSSL) is already stripped by
// bench_pairs.hpp's classify_benchmark, so one label covers both sides of a comparison.
#ifndef CHEATAH_TESTS_BENCHMARKS_BENCH_LABELS_HPP
#define CHEATAH_TESTS_BENCHMARKS_BENCH_LABELS_HPP

#include <map>
#include <string>

namespace cheatah::bench {

// Function-local static: the registrars below run during static initialisation, and this
// guarantees the map exists by the time the first one calls it, whatever link order gives us.
inline std::map<std::string, std::string>& label_registry() {
    static std::map<std::string, std::string> m;
    return m;
}

struct LabelRegistrar {
    LabelRegistrar(const char* key, const char* display) { label_registry()[key] = display; }
};

// The display name, or the key itself when none was registered — never an empty cell.
inline std::string label_for(const std::string& key) {
    const auto it = label_registry().find(key);
    return it == label_registry().end() ? key : it->second;
}

}  // namespace cheatah::bench

// Two levels so __LINE__ expands before pasting.
#define CHEATAH_BENCH_LABEL_CAT_(a, b) a##b
#define CHEATAH_BENCH_LABEL_CAT(a, b) CHEATAH_BENCH_LABEL_CAT_(a, b)

#define CHEATAH_BENCH_LABEL(key, display)                                     \
    static const ::cheatah::bench::LabelRegistrar CHEATAH_BENCH_LABEL_CAT(     \
        cheatah_bench_label_, __LINE__){key, display}

#endif  // CHEATAH_TESTS_BENCHMARKS_BENCH_LABELS_HPP
