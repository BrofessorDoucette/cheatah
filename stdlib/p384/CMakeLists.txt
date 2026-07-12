# cheatah p384 module — NIST P-384 (secp384r1) ECDSA verification, from scratch.
# The arithmetic is the width-generic core in stdlib/p256/ec_core.hpp (shared with
# p256), so this target only needs p256's include dir — it links no p256 code.
add_cheatah_library(p384 SOURCES p384.cpp)
target_include_directories(cheatah_p384_obj PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../p256)
