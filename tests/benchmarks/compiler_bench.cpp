// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// Micro-benchmarks for the cheatah frontend (lexer -> parser -> codegen) — the
// transpilation half of purrc, and the half that governs iteration time. The
// backend C++ compile is measured separately (it dominates wall-clock, but it is
// the system compiler's cost, not ours); this file times ONLY the code we own.
//
// Each stage is isolated so a win is attributable:
//   BM_tokenize    — tokenize(src)                     (lexer only)
//   BM_parse       — parse(tokens)                     (parser only; pre-tokenized)
//   BM_codegen     — codegen(program)                  (codegen only; pre-parsed)
//   BM_frontend    — parse_source(src) + codegen(...)  (the full per-file cost purrc pays)
//
// over three real sources of increasing size (small / medium / large), so a change
// can be seen to help the case it targets without hurting the others. Every result is
// guarded by benchmark::DoNotOptimize / ClobberMemory so -O3 cannot elide the work —
// the codegen result string especially, which is otherwise dead. bytes/op is reported
// (SetBytesProcessed) so throughput, not just latency, is visible.
//
// Build: cmake --build --preset release --target cheatah_benchmarks
// Run:   ./build/release/bin/cheatah_benchmarks --benchmark_filter='^BM_(tokenize|parse|codegen|frontend)'
// Gate:  scripts/compiler_bench_gate.sh   (regression vs committed baseline)

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#ifndef CHEATAH_SRC_ROOT
#define CHEATAH_SRC_ROOT "."
#endif

namespace {

std::string read_file(const std::string& rel) {
    std::ifstream f(std::string(CHEATAH_SRC_ROOT) + "/" + rel, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Three real programs spanning the size range. Small: a tight loop. Medium: a
// numeric mini-app. Large: the requests module (~1.3k lines, the biggest .purr we
// ship). Read once at startup; a benchmark that finds an empty source aborts loudly
// rather than silently timing nothing.
struct Source {
    const char* tag;
    const char* path;
    std::string text;
};

Source g_sources[] = {
    {"small", "gen/fizzbuzz.purr", {}},
    {"medium", "scripts/bench/nbody.purr", {}},
    {"large", "stdlib/requests/requests.purr", {}},
};

const Source& source(std::size_t i) {
    Source& s = g_sources[i];
    if (s.text.empty()) {
        s.text = read_file(s.path);
        if (s.text.empty()) {
            static_cast<void>(std::fprintf(stderr, "compiler_bench: corpus source missing: %s\n", s.path));
            std::abort();
        }
    }
    return s;
}

void BM_tokenize(benchmark::State& state) {
    const Source& s = source(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        cheatah::LexResult r = cheatah::tokenize(s.text);
        benchmark::DoNotOptimize(r.tokens.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(s.text.size()));
}

void BM_parse(benchmark::State& state) {
    const Source& s = source(static_cast<std::size_t>(state.range(0)));
    const cheatah::LexResult lex = cheatah::tokenize(s.text);  // pre-tokenized: time only the parser
    for (auto _ : state) {
        cheatah::ParseResult r = cheatah::parse(lex.tokens);
        benchmark::DoNotOptimize(&r.program);
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(s.text.size()));
}

void BM_codegen(benchmark::State& state) {
    const Source& s = source(static_cast<std::size_t>(state.range(0)));
    const cheatah::ParseResult pr = cheatah::parse_source(s.text);  // pre-parsed: time only codegen
    for (auto _ : state) {
        cheatah::CodegenResult cg = cheatah::codegen(pr.program);
        benchmark::DoNotOptimize(cg.source.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(s.text.size()));
}

// The full per-file frontend cost purrc pays on every compile: lex + parse + codegen.
void BM_frontend(benchmark::State& state) {
    const Source& s = source(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        const cheatah::ParseResult pr = cheatah::parse_source(s.text);
        cheatah::CodegenResult cg = cheatah::codegen(pr.program);
        benchmark::DoNotOptimize(cg.source.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(s.text.size()));
}

// Register each stage across the three sizes, named BM_<stage>/<tag> so the gate can
// pair current vs baseline by exact name.
void register_all() {
    for (std::size_t i = 0; i < std::size(g_sources); ++i) {
        const char* tag = g_sources[i].tag;
        const auto arg = static_cast<std::int64_t>(i);
        benchmark::RegisterBenchmark(std::string("BM_tokenize/") + tag, BM_tokenize)->Arg(arg);
        benchmark::RegisterBenchmark(std::string("BM_parse/") + tag, BM_parse)->Arg(arg);
        benchmark::RegisterBenchmark(std::string("BM_codegen/") + tag, BM_codegen)->Arg(arg);
        benchmark::RegisterBenchmark(std::string("BM_frontend/") + tag, BM_frontend)->Arg(arg);
    }
}

const int kRegistered = (register_all(), 0);

} // namespace