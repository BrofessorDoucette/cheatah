// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Unit tests for the `x25519` module against the RFC 7748 test vectors (§5.2, §6.1) — the
// from-scratch field arithmetic and Montgomery ladder must match the spec bit for bit.
#include <gtest/gtest.h>

#include "x25519.hpp"

namespace x = cheatah::x25519;

// RFC 7748 §5.2 vector 1: scalar * point -> known output.
TEST(CheatahX25519, Rfc7748Vector1) {
    EXPECT_EQ(x::x25519("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
                        "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c"),
              "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
}

// RFC 7748 §5.2 vector 2 (different scalar/point pair).
TEST(CheatahX25519, Rfc7748Vector2) {
    EXPECT_EQ(x::x25519("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
                        "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493"),
              "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957");
}

// RFC 7748 §5.2: one iteration of the iterated test (scalar applied to the base point form).
TEST(CheatahX25519, IteratedOnce) {
    EXPECT_EQ(x::x25519("0900000000000000000000000000000000000000000000000000000000000000",
                        "0900000000000000000000000000000000000000000000000000000000000000"),
              "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079");
}

// RFC 7748 §6.1: the full Diffie-Hellman exchange — both public keys and the shared secret.
TEST(CheatahX25519, DiffieHellman) {
    const std::string alice_priv =
        "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
    const std::string bob_priv =
        "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
    const std::string alice_pub = x::x25519_base(alice_priv);
    const std::string bob_pub = x::x25519_base(bob_priv);
    EXPECT_EQ(alice_pub, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    EXPECT_EQ(bob_pub, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    const std::string shared = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";
    EXPECT_EQ(x::x25519(alice_priv, bob_pub), shared);
    EXPECT_EQ(x::x25519(bob_priv, alice_pub), shared);  // both sides agree
}

// Malformed input (wrong length, non-hex) and the all-zero contributory check return "".
TEST(CheatahX25519, RejectsMalformed) {
    EXPECT_EQ(x::x25519("abc", "abc"), "");
    EXPECT_EQ(x::x25519_base("zz46e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"),
              "");
    // a zero point yields a zero shared secret -> rejected (small-subgroup degeneracy)
    EXPECT_EQ(x::x25519("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
                        "0000000000000000000000000000000000000000000000000000000000000000"),
              "");
}
