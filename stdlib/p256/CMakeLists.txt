# cheatah p256 module — NIST P-256 (secp256r1) ECDSA, from scratch. Uses hashlib's
# HMAC-SHA256 for the RFC 6979 deterministic nonce.
add_cheatah_library(p256 SOURCES p256.cpp DEPENDS hashlib)

# NOTE: the p256.cpp `#ifdef CHEATAH_P256_TESTING` white-box seams (testonly::reduce_mod_n_be,
# sign_raw_skip) are NEVER defined here — so no libcheatah_p256, in ANY build configuration
# (debug or release), ever contains test-only code. The seam is compiled into the TEST BINARY
# alone: stdlib/tests/CMakeLists.txt adds p256.cpp as a direct source of cheatah_tests with the
# CHEATAH_P256_TESTING define set. The shipped library is unconditionally clean.
