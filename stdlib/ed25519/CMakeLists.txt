# cheatah ed25519 module — RFC 8032 public-key signatures, implemented from scratch
# (no external crypto dependency). Uses SHA-512 from hashlib, so it links it (hashlib
# must be added before this module in stdlib/CMakeLists.txt).
add_cheatah_library(ed25519 SOURCES ed25519.cpp)

target_link_libraries(cheatah_ed25519_obj PUBLIC cheatah::hashlib_static)
foreach(_lib cheatah_ed25519_static cheatah_ed25519_shared)
    target_link_libraries(${_lib} PUBLIC cheatah::hashlib_static)
endforeach()

# generate() draws its seed from the OS CSPRNG — BCryptGenRandom on Windows.
if(WIN32)
    target_link_libraries(cheatah_ed25519_shared PUBLIC bcrypt)
    target_link_libraries(cheatah_ed25519_static INTERFACE bcrypt)
endif()
