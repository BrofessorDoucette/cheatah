// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level (whole-program) test for the `hashlib` stdlib module. Unlike the
// per-function compile-run test (tests/purrc/hashlib_cr_test.cpp), this drives a
// single cohesive program that hashes several inputs through the module and
// asserts its exact stdout, exercising the function across multiple message
// lengths in one run.
//
// All three are standard NIST/FIPS-180 SHA-256 test vectors:
//   ""   -> the canonical empty-string digest,
//   "abc"-> the one-block vector,
//   the 56-byte message -> the two-block vector.
//
// Coverage — every function in stdlib/hashlib/hashlib.hpp:
//   sha256.
#include "e2e_harness.hpp"

TEST(StdlibE2E, Hashlib) {
    e2e::expect_e2e("hashlib_sys", R"PURR(import io
import hashlib

io.print(hashlib.sha256(""))
io.print(hashlib.sha256("abc"))
io.print(hashlib.sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))
io.print(hashlib.sha512(""))
io.print(hashlib.sha512("abc"))
# The raw digests are the same bytes the hex form spells, so their lengths are fixed.
io.print(len(hashlib.sha256_digest("abc")), len(hashlib.sha512_digest("abc")))
)PURR",
                        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
                        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
                        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1\n"
                        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
                        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e\n"
                        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f\n"
                        "32 64\n");
}
