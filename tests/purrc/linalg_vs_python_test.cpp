// System-level tests for the (newly optimized) linalg library. Each runs a checked-in
// .purr program through the real purrc → cheatah pipeline and cross-checks its output,
// number for number, against an equivalent NumPy program. The .purr/.py sources live in
// tests/purrc/linalg_programs/ so they can be opened and edited in the editor — open a
// .purr there to see exactly what each test runs.
//
// Same fixed, well-conditioned symmetric 4×4 matrix feeds every op. Skipped (not
// failed) when python3 + numpy is unavailable, so the suite stays green without numpy.
#include <string>

#include "e2e_harness.hpp"

#ifndef LINALG_PROGRAMS_DIR
#define LINALG_PROGRAMS_DIR "."
#endif

namespace {
std::string prog(const char* base, const char* ext) {
    return std::string(LINALG_PROGRAMS_DIR) + "/" + base + ext;
}
// Cross-check linalg_programs/<base>.purr against linalg_programs/<base>.py.
void check(const char* base, bool sorted = false) {
    e2e::expect_purr_matches_python(base, prog(base, ".purr"), prog(base, ".py"),
                                    /*tol=*/1e-4, sorted);
}
}  // namespace

TEST(LinalgVsPython, Solve)    { check("solve"); }
TEST(LinalgVsPython, Det)      { check("det"); }
TEST(LinalgVsPython, Inv)      { check("inv"); }
TEST(LinalgVsPython, Matmul)   { check("matmul"); }
// Spectra: numpy and cheatah may order the values differently, so compare unordered.
TEST(LinalgVsPython, Eigvalsh) { check("eigvalsh", /*sorted=*/true); }
TEST(LinalgVsPython, Svdvals)  { check("svdvals", /*sorted=*/true); }
