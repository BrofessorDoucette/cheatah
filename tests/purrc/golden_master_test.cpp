// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// Golden-master behavior lock for the cheatah frontend (lexer -> parser -> codegen).
//
// The v1.6 compiler-speed work is a PURE PERFORMANCE refactor: for a fixed .purr
// input and fixed flags, the frontend must emit BYTE-IDENTICAL C++ before and
// after. This test pins that invariant. It links cheatah::compiler and drives the
// frontend in-process — NO purrc fork, NO C++ backend — so it runs in milliseconds
// and isolates the transpiler from the (much slower) backend compile.
//
// For every program in a fixed on-disk corpus it serializes the FULL CodegenResult
// (source / header_source / impl_source / modules / diagnostics) across every flag
// combination purrc uses, and compares the bytes to a committed golden fixture under
// tests/purrc/golden/. Any difference = a forbidden behavior change.
//
//   Capture the goldens ONCE, on the pre-refactor baseline:
//       CHEATAH_GOLDEN_UPDATE=1 ctest -R 'golden-master'
//   then `git add tests/purrc/golden` and commit. Never regenerate during the
//   refactor — a diff is the point.
//
// The corpus (real .purr from the tree) spans structs / enums / interfaces / match /
// kwargs / cpp-blocks / doc-comments / imports / multiline arrays — the branches most
// likely to shift output under refactor. It is deliberately fork-free and fast so it
// can gate every optimization commit with zero tolerance.

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "codegen.hpp"
#include "parser.hpp"

#ifndef CHEATAH_SRC_ROOT
#define CHEATAH_SRC_ROOT "."
#endif
#ifndef GOLDEN_DIR
#define GOLDEN_DIR "."
#endif

namespace {

enum class Mode { Program, Library };

struct CorpusEntry {
    const char* path;  // relative to CHEATAH_SRC_ROOT
    Mode mode;
};

// The corpus. Program-mode entries drive codegen(); the one true library module
// (requests) additionally drives codegen_library() and, through parse_source's
// attach_docs, the doc-comment -> Javadoc-header path. Add real .purr here as the
// language grows — every entry widens the behavior lock.
const CorpusEntry kCorpus[] = {
    // Program mode — small to large, feature-diverse.
    {"gen/fizzbuzz.purr", Mode::Program},
    {"gen/grades.purr", Mode::Program},
    {"gen/http_response.purr", Mode::Program},
    {"gen/numerics.purr", Mode::Program},
    {"gen/palette.purr", Mode::Program},
    {"gen/shapes.purr", Mode::Program},
    {"gen/slices.purr", Mode::Program},   // slice reads + slice ASSIGNMENT (list resize, array fill)
    {"gen/vectors.purr", Mode::Program},
    {"scripts/bench/integral.purr", Mode::Program},
    {"scripts/bench/integral_threads.purr", Mode::Program},
    {"scripts/bench/mandelbrot.purr", Mode::Program},
    {"scripts/bench/nbody.purr", Mode::Program},
    {"scripts/bench/oscillator.purr", Mode::Program},
    {"tests/purrc/linalg_programs/det.purr", Mode::Program},
    {"tests/purrc/linalg_programs/eigvalsh.purr", Mode::Program},
    {"tests/purrc/linalg_programs/inv.purr", Mode::Program},
    {"tests/purrc/linalg_programs/matmul.purr", Mode::Program},
    {"tests/purrc/linalg_programs/solve.purr", Mode::Program},
    {"tests/purrc/linalg_programs/svdvals.purr", Mode::Program},
    {"tests/previously_broken/angle_bracket_types.purr", Mode::Program},
    {"tests/previously_broken/multiline_array.purr", Mode::Program},
    {"tests/previously_broken/struct_init.purr", Mode::Program},
    {"tests/previously_broken/timing_str.purr", Mode::Program},
    {"stdlib/scripts/tour.purr", Mode::Program},
    {"stdlib/memory/tests/cheatah_struct.purr", Mode::Program},
    {"stdlib/memory/tests/cheatah_ndarray.purr", Mode::Program},
    {"stdlib/memory/tests/cheatah_string.purr", Mode::Program},
    // Library mode — the canonical pure-cheatah module: large, doc-commented,
    // exercises codegen_library (header/impl split) and attach_docs.
    {"stdlib/requests/requests.purr", Mode::Library},
};

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << content;
    return static_cast<bool>(f);
}

// Slug for the golden filename: the relative path with separators/dots flattened.
std::string slug(const std::string& rel) {
    std::string s;
    s.reserve(rel.size());
    for (char c : rel) s += (c == '/' || c == '.') ? '_' : c;
    return s;
}

// The module name codegen_library wraps output in, derived from the file stem.
std::string module_name_of(const std::string& rel) {
    const std::size_t slash = rel.find_last_of('/');
    const std::size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    const std::size_t dot = rel.find('.', start);
    return rel.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
}

// Append one CodegenResult, fully, under a labeled section. Every observable field
// is serialized so the lock covers diagnostics and module lists, not just source.
void append_result(std::string& out, const std::string& label, const cheatah::CodegenResult& cg) {
    out += "===== " + label + " =====\n";
    out += "--- diagnostics (" + std::to_string(cg.diagnostics.size()) + ") ---\n";
    for (const std::string& d : cg.diagnostics) out += d + "\n";
    out += "--- modules (" + std::to_string(cg.modules.size()) + ") ---\n";
    for (const std::string& m : cg.modules) out += m + "\n";
    out += "--- source (" + std::to_string(cg.source.size()) + " bytes) ---\n";
    out += cg.source;
    out += "\n--- header_source (" + std::to_string(cg.header_source.size()) + " bytes) ---\n";
    out += cg.header_source;
    out += "\n--- impl_source (" + std::to_string(cg.impl_source.size()) + " bytes) ---\n";
    out += cg.impl_source;
    out += "\n";
}

// Serialize every flag combination purrc drives for this entry into one blob. The
// combos are the distinct emit paths: remove_unused on/off and the #line path
// (source_file empty vs set) for programs; transparent/opaque for libraries. Parsing
// (incl. its diagnostics) is captured once up front and shared across combos.
std::string serialize_entry(const CorpusEntry& e, const std::string& src) {
    const cheatah::ParseResult pr = cheatah::parse_source(src);

    std::string out;
    out += "######## " + std::string(e.path) + " ########\n";
    out += "parse diagnostics (" + std::to_string(pr.diagnostics.size()) + "):\n";
    for (const cheatah::Diagnostic& d : pr.diagnostics)
        out += "  " + std::to_string(d.pos.line) + ":" + std::to_string(d.pos.column) + ": " + d.message + "\n";

    if (e.mode == Mode::Program) {
        for (bool remove_unused : {true, false}) {
            for (const char* sf : {"", e.path}) {
                const cheatah::CodegenResult cg = cheatah::codegen(pr.program, sf, remove_unused);
                const std::string label = std::string("program remove_unused=") + (remove_unused ? "1" : "0") +
                                          " source_file=" + (sf[0] ? "set" : "empty");
                append_result(out, label, cg);
            }
        }
    } else {
        for (bool transparent : {true, false}) {
            cheatah::LibOptions opts;
            opts.module_name = module_name_of(e.path);
            opts.transparent = transparent;
            const cheatah::CodegenResult cg = cheatah::codegen_library(pr.program, opts);
            append_result(out, std::string("library transparent=") + (transparent ? "1" : "0"), cg);
        }
    }
    return out;
}

bool update_mode() {
    const char* v = std::getenv("CHEATAH_GOLDEN_UPDATE");
    return v != nullptr && v[0] == '1';
}

class GoldenMaster : public ::testing::TestWithParam<CorpusEntry> {};

TEST_P(GoldenMaster, EmittedCppIsByteIdentical) {
    const CorpusEntry e = GetParam();
    const std::string src_path = std::string(CHEATAH_SRC_ROOT) + "/" + e.path;
    const std::string src = read_file(src_path);
    ASSERT_FALSE(src.empty()) << "corpus source missing or empty: " << src_path;

    const std::string actual = serialize_entry(e, src);
    const std::string golden_path = std::string(GOLDEN_DIR) + "/" + slug(e.path) + ".golden";

    if (update_mode()) {
        ASSERT_TRUE(write_file(golden_path, actual)) << "cannot write golden: " << golden_path;
        GTEST_SKIP() << "wrote golden " << golden_path;
    }

    const std::string expected = read_file(golden_path);
    ASSERT_FALSE(expected.empty())
        << "golden missing: " << golden_path
        << "\n  capture it with: CHEATAH_GOLDEN_UPDATE=1 ctest -R golden-master";
    EXPECT_EQ(actual, expected)
        << "FRONTEND OUTPUT DRIFTED for " << e.path
        << " — the refactor changed emitted C++. This is a behavior change and must be reverted."
        << "\n  (If the change is intentional AND reviewed, re-capture with CHEATAH_GOLDEN_UPDATE=1.)";
}

std::string entry_name(const ::testing::TestParamInfo<CorpusEntry>& info) {
    return slug(info.param.path);
}

INSTANTIATE_TEST_SUITE_P(Corpus, GoldenMaster, ::testing::ValuesIn(kCorpus), entry_name);

} // namespace
