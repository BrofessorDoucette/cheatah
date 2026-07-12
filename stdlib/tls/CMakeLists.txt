# cheatah tls module — a from-scratch TLS 1.3 client built on the cheatah crypto modules
# (x25519 + aead + hashlib HKDF + ed25519 certificate verification). No OpenSSL.
add_cheatah_library(tls SOURCES tls.cpp)
target_link_libraries(cheatah_tls_obj PRIVATE
    cheatah_x25519_obj cheatah_aead_obj cheatah_hashlib_obj cheatah_ed25519_obj cheatah_socket_obj cheatah_p256_obj
    cheatah_p384_obj)
