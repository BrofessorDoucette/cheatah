// Compile-run unit tests for the `ed25519` module: one test per function, each a tiny
// .purr compiled by purrc and run under the cheatah runtime. Keys/signatures are hex.
// The deterministic cases use the RFC 8032 §7.1 known-answer vectors; generate() is
// non-deterministic, so it is asserted by PROPERTY (a fresh key still signs+verifies).
// Complements the in-process unit tests (stdlib/tests/ed25519_test.cpp) and the
// per-module system-level test (StdlibE2E.Ed25519).
#include "e2e_harness.hpp"

// RFC 8032 TEST 3: seed c5aa… -> public key fc51….
TEST(Ed25519CompileRun, PublicKey) {
    e2e::expect_e2e("ed25519_public_key", R"PURR(import io
import ed25519
io.print(ed25519.public_key("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7"))
)PURR", "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025\n");
}

// Sign a message and verify it — the round trip a recipient performs.
TEST(Ed25519CompileRun, SignVerify) {
    e2e::expect_e2e("ed25519_sign_verify", R"PURR(import io
import ed25519
let secret = "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7"
let pub = ed25519.public_key(secret)
let sig = ed25519.sign(secret, "hello cheatah")
io.print(ed25519.verify(pub, "hello cheatah", sig))
)PURR", "True\n");
}

// generate() makes a fresh secret seed; it must still produce a working keypair.
TEST(Ed25519CompileRun, Generate) {
    e2e::expect_e2e("ed25519_generate", R"PURR(import io
import ed25519
let secret = ed25519.generate()
let pub = ed25519.public_key(secret)
let sig = ed25519.sign(secret, "msg")
io.print(len(secret), ed25519.verify(pub, "msg", sig))
)PURR", "64 True\n");
}
