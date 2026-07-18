# cheatah aead module — ChaCha20-Poly1305 authenticated encryption (RFC 8439), from scratch.
# Compiled at -O3 -funroll-loops -march=native (the ndarray/linalg precedent) so the scalar
# ChaCha20 / Poly1305 ARX loops auto-vectorize — AES-GCM already runtime-dispatches AES-NI, so
# this lifts the software cipher path + handshake without touching the byte-identical KAT contract.
add_cheatah_library(aead SOURCES aead.cpp)
target_compile_options(cheatah_aead_obj PRIVATE ${CHEATAH_MODULE_OPTFLAGS})
