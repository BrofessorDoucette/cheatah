// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level test: least-squares regression written in cheatah, using the
// native ndarray + linalg standard library. For each dimensionality n we build a
// well-conditioned m×n design matrix X and a known coefficient vector β, form the
// exact targets y = X·β in cheatah (linalg.matmul), recover β̂ = lstsq(X, y), and
// confirm β̂ == β. The whole regression runs as a compiled .purr program; this test
// just compiles it with purrc, runs it under the cheatah runtime, and checks output.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

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

namespace {

std::string run_capture(const std::string& cmd, int& exit_code) {
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

// A deterministic, well-conditioned design-matrix entry: small values in [-3, 3]
// with a strong diagonal on the top n×n block (so X has full column rank and the
// least-squares problem is well-conditioned). m = n + 5 over-determines the fit.
double design(int i, int j, int n) {
    double v = static_cast<double>(((i * 7 + j * 3 + 1) % 7) - 3);
    if (i < n && i == j) v += static_cast<double>(10 * n);
    return v;
}

void expect_lsq_recovers(int n) {
    const int m = n + 5;

    std::ostringstream xs, bs;
    xs << std::fixed << std::setprecision(1);
    bs << std::fixed << std::setprecision(1);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j) xs << ((i || j) ? ", " : "") << design(i, j, n);
    for (int i = 0; i < n; ++i) bs << (i ? ", " : "") << (static_cast<double>(i + 1) * 0.5);

    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/lsq" + std::to_string(n) + ".purr";
    const std::string mod = tmp + "/lsq" + std::to_string(n) + ".so";
    {
        std::ofstream f(purr);
        f << "import io\nimport ndarray\nimport linalg\n";
        f << "let Xflat = [" << xs.str() << "]\n";
        f << "let beta = [" << bs.str() << "]\n";
        f << "let X = ndarray.reshape(ndarray.array(Xflat), [" << m << ", " << n << "])\n";
        f << "let b = ndarray.reshape(ndarray.array(beta), [" << n << ", 1])\n";
        f << "let y = linalg.matmul(X, b)\n";        // exact targets, m×1
        f << "let est = linalg.lstsq(X, y)\n";       // recovered coefficients, n×1
        f << "let maxerr = 0.0\n";
        f << "for i in range(" << n << ") {\n";
        f << "    let d = ndarray.get(est, [i, 0]) - beta[i]\n";
        f << "    if d < 0.0 { d = 0.0 - d }\n";
        f << "    if d > maxerr { maxerr = d }\n";
        f << "}\n";
        f << "io.print(\"dim\", " << n << ", \"recovered\", maxerr < 0.000001)\n";
    }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << "purrc failed to compile (n=" << n << ")";

    int rc = -1;
    const std::string out = run_capture(std::string(CHEATAH_RUNTIME_PATH) + " \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, "dim " + std::to_string(n) + " recovered True\n");
}

}  // namespace

TEST(LinalgLsqE2E, Recovers2D) { expect_lsq_recovers(2); }
TEST(LinalgLsqE2E, Recovers3D) { expect_lsq_recovers(3); }
TEST(LinalgLsqE2E, Recovers5D) { expect_lsq_recovers(5); }
TEST(LinalgLsqE2E, Recovers10D) { expect_lsq_recovers(10); }
TEST(LinalgLsqE2E, Recovers15D) { expect_lsq_recovers(15); }
