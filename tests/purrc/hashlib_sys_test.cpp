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
)PURR",
                        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
                        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
                        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1\n");
}
