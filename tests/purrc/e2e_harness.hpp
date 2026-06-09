// Shared compile-run harness for the cheatah end-to-end test suites.
//
// expect_e2e(name, src, expected[, env_prefix]) writes a .purr program, compiles
// it with purrc into a loadable module, runs it under the cheatah runtime, and
// asserts the captured stdout equals `expected` (byte-for-byte). Used by:
//   - the per-function "compile-run" tests   (tests/purrc/compile_run/*)
//   - the per-module  "system-level"  tests  (stdlib_e2e_test.cpp)
//
// Programs must be DETERMINISTIC (no clocks/PIDs/unseeded RNG; datetime under
// TZ=UTC; RNG seeded; floats use io.print's default 6-significant-digit form).
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <gtest/gtest.h>

#ifndef PURRC_PATH
#define PURRC_PATH ""
#endif
#ifndef CHEATAH_RUNTIME_PATH
#define CHEATAH_RUNTIME_PATH ""
#endif
#ifndef PURR_TEST_TMP
#define PURR_TEST_TMP "."
#endif

namespace e2e {

inline std::string run_capture(const std::string& cmd, int& exit_code) {
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        exit_code = -1;
        return out;
    }
    std::array<char, 256> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out += buf.data();
    }
    const int status = pclose(pipe);
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return out;
}

// Compile `src` with purrc, run the module under the cheatah runtime (optionally
// with an env prefix like "TZ=UTC "), and assert the captured stdout equals
// `expected`. `name` makes the temp files and failure messages unique.
inline void expect_e2e(const std::string& name, const std::string& src,
                       const std::string& expected, const std::string& env_prefix = "") {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/" + name + "_e2e.purr";
    const std::string mod = tmp + "/" + name + "_e2e.so";
    { std::ofstream f(purr); f << src; }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << name << ": purrc failed to compile the program";

    int rc = -1;
    const std::string out =
        run_capture(env_prefix + "\"" + std::string(CHEATAH_RUNTIME_PATH) + "\" \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0) << name << ": program exited non-zero";
    EXPECT_EQ(out, expected) << name << ": stdout mismatch";
}

// ------------------------------------------------------------------------
// Compare a checked-in .purr program against an equivalent Python/NumPy program.
//
// These are SYSTEM tests for the numeric library: the .purr lives on disk (so it can
// be opened/edited in the editor) and is run through the real purrc → cheatah
// pipeline; the .py reference is run through python3 + numpy; the two stdout streams
// are reduced to their sequence of floating-point numbers and compared with a relative
// tolerance (cheatah's io.print emits ~6 significant digits). Skipped — not failed —
// when python3/numpy is unavailable, so the suite stays green on a numpy-less box.
// ------------------------------------------------------------------------

// Read a whole file into a string (empty if it can't be opened).
inline std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Every floating-point number appearing in `s`, in order (brackets/commas/labels are
// ignored), so "[1.5, -2e-1]" -> {1.5, -0.2}. Robust to numpy vs cheatah formatting.
inline std::vector<double> extract_floats(const std::string& s) {
    std::vector<double> v;
    const char* p = s.c_str();
    const char* end = p + s.size();
    while (p < end) {
        const bool starts = std::isdigit(static_cast<unsigned char>(*p)) || *p == '.' ||
            ((*p == '-' || *p == '+') && p + 1 < end &&
             (std::isdigit(static_cast<unsigned char>(p[1])) || p[1] == '.'));
        if (starts) {
            char* np = nullptr;
            const double d = std::strtod(p, &np);
            if (np != p) { v.push_back(d); p = np; continue; }
        }
        ++p;
    }
    return v;
}

// Whether `python3 -c "import numpy"` succeeds (cached for the whole test run).
inline bool python_numpy_available() {
    static const bool ok = [] {
        return std::system("python3 -c \"import numpy\" >/dev/null 2>&1") == 0;
    }();
    return ok;
}

// Compile + run a .purr file that already exists on disk; returns its stdout.
inline std::string run_purr_file(const std::string& name, const std::string& purr_path, int& rc) {
    const std::string mod = std::string(PURR_TEST_TMP) + "/" + name + "_prog.so";
    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr_path + "\" -o \"" + mod + "\"";
    if (std::system(compile.c_str()) != 0) { rc = -2; return {}; }
    return run_capture("\"" + std::string(CHEATAH_RUNTIME_PATH) + "\" \"" + mod + "\"", rc);
}

// Run `purr_path` (cheatah) and `py_path` (python3+numpy), and assert the two emit the
// same sequence of numbers within a relative tolerance. `sort_values` compares the
// numbers as a sorted multiset — use it for order-free results like a spectrum
// (eigenvalues / singular values) where the two libraries may order differently.
inline void expect_purr_matches_python(const std::string& name, const std::string& purr_path,
                                       const std::string& py_path, double tol = 1e-4,
                                       bool sort_values = false) {
    if (!python_numpy_available()) {
        GTEST_SKIP() << name << ": python3 + numpy not available — skipping cross-check";
    }
    ASSERT_FALSE(read_file(purr_path).empty()) << name << ": missing/empty .purr: " << purr_path;
    ASSERT_FALSE(read_file(py_path).empty()) << name << ": missing/empty .py: " << py_path;

    int crc = -1;
    const std::string ch_out = run_purr_file(name, purr_path, crc);
    ASSERT_EQ(crc, 0) << name << ": cheatah pipeline failed (compile or run) for " << purr_path;

    int prc = -1;
    const std::string py_out = run_capture("python3 \"" + py_path + "\"", prc);
    ASSERT_EQ(prc, 0) << name << ": python reference exited non-zero for " << py_path;

    std::vector<double> ch = extract_floats(ch_out);
    std::vector<double> py = extract_floats(py_out);
    ASSERT_EQ(ch.size(), py.size())
        << name << ": value count differs — cheatah " << ch.size() << " vs python " << py.size()
        << "\n  cheatah: " << ch_out << "  python:  " << py_out;
    if (sort_values) {
        std::sort(ch.begin(), ch.end());
        std::sort(py.begin(), py.end());
    }
    for (std::size_t i = 0; i < ch.size(); ++i) {
        EXPECT_LE(std::fabs(ch[i] - py[i]), tol * (1.0 + std::fabs(py[i])))
            << name << ": value " << i << " differs — cheatah " << ch[i] << " vs python " << py[i];
    }
}

}  // namespace e2e
