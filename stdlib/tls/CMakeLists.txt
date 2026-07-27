# cheatah tls module — a from-scratch TLS 1.3 client built on the cheatah crypto modules
# (x25519 + aead + hashlib HKDF + ed25519 certificate verification). No OpenSSL.
add_cheatah_library(tls
    SOURCES tls.cpp
    DEPENDS x25519 aead hashlib ed25519 socket p256 p384)
