# cheatah hashlib module — its own static + shared library; linked only on `import hashlib`.
# -O3 -funroll-loops -march=native (the ndarray/linalg precedent): SHA-256/512's scalar round
# loops auto-vectorize, speeding HKDF + the TLS transcript hash on the handshake path. KAT-guarded.
add_cheatah_library(hashlib SOURCES hashlib.cpp)
target_compile_options(cheatah_hashlib_obj PRIVATE ${CHEATAH_MODULE_OPTFLAGS})
