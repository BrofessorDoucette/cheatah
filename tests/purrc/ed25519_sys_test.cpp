// System-level (whole-program) test for the `ed25519` stdlib module, written as a
// worked EXAMPLE of the signature workflow that backs cheatah's binary-integrity
// feature: a publisher signs a payload with a secret key, anyone holding only the
// PUBLIC key can verify it, and ANY tampering — to the payload, the signature, or the
// key — is rejected. This is the same Ed25519 the runtime uses to refuse a tampered
// `.so` (see integrity_e2e_test.cpp).
//
// Coverage — every function in stdlib/ed25519/ed25519.hpp:
//   generate, public_key, sign, verify.
//
// Deterministic: signing is deterministic (RFC 8032), and the generate() result is
// only exercised by property (a fresh key round-trips), so stdout is fixed.
#include "e2e_harness.hpp"

TEST(StdlibE2E, Ed25519) {
    e2e::expect_e2e("ed25519_sys", R"PURR(import io
import ed25519

# A publisher's keypair. In real use the secret stays offline (purrc --keygen); only
# the public key is shipped to verifiers. Here we use a fixed RFC 8032 seed so the
# signature is reproducible.
let secret = "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7"
let pub = ed25519.public_key(secret)
io.print("public:", pub)

# Sign a payload (think: the bytes of a compiled module).
let payload = "the trusted payload"
let sig = ed25519.sign(secret, payload)

# A verifier with ONLY the public key accepts the genuine payload+signature.
io.print("genuine accepted:", ed25519.verify(pub, payload, sig))

# An attacker who injects different bytes but keeps the old signature is rejected:
# the signature no longer matches the payload.
io.print("tampered payload rejected:", ed25519.verify(pub, "the INJECTED payload", sig) == false)

# Tampering with the signature itself is rejected.
let forged = "00" + sig[2:]
io.print("forged signature rejected:", ed25519.verify(pub, payload, forged) == false)

# A signature from a DIFFERENT key does not verify under this public key — a verifier
# only trusts payloads signed by the key it pins.
let other_secret = ed25519.generate()
let other_sig = ed25519.sign(other_secret, payload)
io.print("untrusted signer rejected:", ed25519.verify(pub, payload, other_sig) == false)

# A freshly generated key still produces a working, verifiable signature.
let fresh = ed25519.generate()
let fresh_pub = ed25519.public_key(fresh)
io.print("fresh keypair verifies:", ed25519.verify(fresh_pub, payload, ed25519.sign(fresh, payload)))
)PURR",
                        "public: fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025\n"
                        "genuine accepted: True\n"
                        "tampered payload rejected: True\n"
                        "forged signature rejected: True\n"
                        "untrusted signer rejected: True\n"
                        "fresh keypair verifies: True\n");
}
