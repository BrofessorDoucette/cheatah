# ---- cheatah standard library ----
#
# The standard-library MODULES (io, os, …) — each its own dual static+shared
# library, linked into a cheatah program ONLY when it `import`s the module. The
# language toolchain and the purrc compiler live in ../compiler.

# Standard-library modules (each its own static + shared library via the helper).
include(${PROJECT_SOURCE_DIR}/cmake/CheatahLibrary.cmake)
add_subdirectory(builtins)  # always-available built-in functions
add_subdirectory(io)
add_subdirectory(os)
add_subdirectory(string)
add_subdirectory(math)
add_subdirectory(time)
add_subdirectory(datetime)
add_subdirectory(random)
add_subdirectory(statistics)
add_subdirectory(socket)
add_subdirectory(x25519)    # X25519 key exchange (RFC 7748) — TLS crypto core
add_subdirectory(aead)      # ChaCha20-Poly1305 AEAD (RFC 8439) — TLS record cipher
add_subdirectory(tls)       # TLS 1.3 client (RFC 8446) on the cheatah crypto modules
add_subdirectory(websocket) # RFC 6455 WebSocket client on tls + socket
add_subdirectory(p256)      # NIST P-256 ECDSA (secp256r1), from scratch
add_subdirectory(sys)
# ndarray + linalg AND the crypto modules (hashlib, ed25519) are added at the root
# (before this dir): linalg links ndarray, and purrc + the runtime both link the crypto.

# Convenience aggregate: `cmake --build … --target cheatah_stdlib` builds EVERY
# stdlib module's static + shared library at once (registered by add_cheatah_library,
# including ndarray + linalg added at the root before this point).
get_property(_cheatah_stdlib_targets GLOBAL PROPERTY CHEATAH_STDLIB_TARGETS)
add_custom_target(cheatah_stdlib DEPENDS ${_cheatah_stdlib_targets})

# cheatah's unit tests (the compiler frontend + the stdlib modules) live here.
# The compiler tests link cheatah::compiler (defined in ../compiler, added first).
if(CHEATAH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
