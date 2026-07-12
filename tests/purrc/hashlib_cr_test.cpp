// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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

// Standard NIST/FIPS-180 SHA-384 test vector for "abc".
TEST(HashlibCompileRun, Sha384) {
    e2e::expect_e2e("hashlib_sha384", R"PURR(import io
import hashlib
io.print(hashlib.sha384("abc"))
)PURR", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
        "8086072ba1e7cc2358baeca134c825a7\n");
}

// Standard NIST/FIPS-180 SHA-512 test vector for "abc".
TEST(HashlibCompileRun, Sha512) {
    e2e::expect_e2e("hashlib_sha512", R"PURR(import io
import hashlib
io.print(hashlib.sha512("abc"))
)PURR", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f\n");
}

// The raw digest forms return the bytes (32 / 48 / 64), not hex — assert their length so the
// program output stays deterministic.
TEST(HashlibCompileRun, Sha256Digest) {
    e2e::expect_e2e("hashlib_sha256_digest", R"PURR(import io
import hashlib
io.print(len(hashlib.sha256_digest("abc")))
)PURR", "32\n");
}

TEST(HashlibCompileRun, Sha384Digest) {
    e2e::expect_e2e("hashlib_sha384_digest", R"PURR(import io
import hashlib
io.print(len(hashlib.sha384_digest("abc")))
)PURR", "48\n");
}

TEST(HashlibCompileRun, Sha512Digest) {
    e2e::expect_e2e("hashlib_sha512_digest", R"PURR(import io
import hashlib
io.print(len(hashlib.sha512_digest("abc")))
)PURR", "64\n");
}
