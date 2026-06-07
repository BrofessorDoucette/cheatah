// Compile-run unit tests for the `hashlib` module: one test per function. Each
// writes a tiny .purr that calls a single hashlib function on a known input,
// compiles it with purrc, runs it under the cheatah runtime, and asserts the
// exact stdout. Complements the in-process unit tests
// (stdlib/tests/hashlib_test.cpp) and the per-module system-level test
// (StdlibE2E.Hashlib).
#include "e2e_harness.hpp"

// Standard NIST/FIPS-180 SHA-256 test vector for "abc".
TEST(HashlibCompileRun, Sha256) {
    e2e::expect_e2e("hashlib_sha256", R"PURR(import io
import hashlib
io.print(hashlib.sha256("abc"))
)PURR", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n");
}
