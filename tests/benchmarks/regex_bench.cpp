// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// Micro-benchmarks for cheatah::regex — compile, search, find, full_match on a ~1 MB
// synthetic log. These are the cheatah-only rows the QA gate smoke-runs on every push;
// the full 4-engine comparison (vs std::regex, Boost.Regex, Google RE2) lives in the
// standalone project stdlib/regex/bench/ (see its CMakeLists.txt for how to run it).

#include <benchmark/benchmark.h>

#include <string>
#include <string_view>

#include "regex.hpp"

namespace {

const std::string& log_corpus() {
    static const std::string hay = [] {
        std::string s;
        const char* line =
            "2026-07-02 12:00:01 INFO  request id=48213 user=bob@example.com status=200 bytes=1274\n";
        s.reserve(1'100'000);
        while (s.size() < 1'000'000) s += line;
        s.pop_back();  // no trailing newline: '$' means the same thing as in other engines
        return s;
    }();
    return hay;
}

void BM_regex_compile(benchmark::State& state) {
    std::string pat = "[a-z]+@[a-z.]+";
    for (auto _ : state) {
        benchmark::DoNotOptimize(pat);
        auto re = cheatah::regex::compile(pat);
        benchmark::DoNotOptimize(&re);
    }
}
BENCHMARK(BM_regex_compile);

void BM_regex_search_present(benchmark::State& state) {
    auto re = cheatah::regex::compile("status=200");
    std::string_view hay = log_corpus();
    for (auto _ : state) {
        benchmark::DoNotOptimize(hay);
        bool r = cheatah::regex::search(re, hay);
        benchmark::DoNotOptimize(&r);
    }
}
BENCHMARK(BM_regex_search_present);

void BM_regex_search_absent(benchmark::State& state) {
    auto re = cheatah::regex::compile("CRITICAL[0-9]+");
    std::string_view hay = log_corpus();
    for (auto _ : state) {
        benchmark::DoNotOptimize(hay);
        bool r = cheatah::regex::search(re, hay);
        benchmark::DoNotOptimize(&r);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(hay.size()));
}
BENCHMARK(BM_regex_search_absent);

void BM_regex_find_first(benchmark::State& state) {
    auto re = cheatah::regex::compile("[a-z]+@[a-z.]+");
    std::string_view hay = log_corpus();
    for (auto _ : state) {
        benchmark::DoNotOptimize(hay);
        auto m = cheatah::regex::find(re, hay);
        benchmark::DoNotOptimize(&m);
    }
}
BENCHMARK(BM_regex_find_first);

void BM_regex_fullmatch(benchmark::State& state) {
    auto re = cheatah::regex::compile("[a-z]+@[a-z.]+");
    std::string_view tok = "bob@example.com";
    for (auto _ : state) {
        benchmark::DoNotOptimize(tok);
        bool r = cheatah::regex::full_match(re, tok);
        benchmark::DoNotOptimize(&r);
    }
}
BENCHMARK(BM_regex_fullmatch);

}  // namespace
