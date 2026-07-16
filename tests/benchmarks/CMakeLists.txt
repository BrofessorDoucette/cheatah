# Micro-benchmarks (Google Benchmark) for the same code paths the unit tests
# exercise. One executable; benchmark::benchmark_main supplies main(). Links the
# STATIC library, matching the unit tests.
#
# Build with the `release` preset for meaningful numbers (the default Debug build
# is unoptimized). Run: ./build/release/bin/cheatah_benchmarks
#
# As more primitives land, add a *_bench.cpp here next to its tests.
add_executable(cheatah_benchmarks
    linalg_bench.cpp
    ndarray_bench.cpp
    # Native C++ comparisons: cheatah's numeric core vs the reference libraries.
    eigen_compare_bench.cpp   # vs Eigen — dynamic dense linear algebra
    glm_compare_bench.cpp
    fixed_glm_bench.cpp        # vs GLM  — the FIXED-extent types, the complete API overlap     # vs GLM  — small fixed-size vector/matrix math
    # Per-tier cost of module-integrity verification — compiles the runtime's
    # integrity.cpp directly so it times the exact code path the host uses.
    integrity_bench.cpp
    p256_bench.cpp
    crypto_openssl_bench.cpp  # cheatah digests/HMAC/AEAD vs OpenSSL (libcrypto, if present)
    compiler_bench.cpp        # the cheatah frontend: lexer/parser/codegen transpilation speed
    ${PROJECT_SOURCE_DIR}/runtime/integrity.cpp
)

target_link_libraries(cheatah_benchmarks PRIVATE
    cheatah::compiler         # frontend under test (compiler_bench.cpp)
    cheatah::linalg_static
    cheatah::fixarray         # header-only: the fixed-extent types (fixed_glm_bench.cpp)
    cheatah::ndarray_static
    cheatah::ed25519_static   # ed25519 BEFORE hashlib (static archives resolve left-to-right)
    cheatah::p256_static
    cheatah::aead_static
    cheatah::hashlib_static
    benchmark::benchmark_main
)
target_include_directories(cheatah_benchmarks PRIVATE
    ${PROJECT_SOURCE_DIR}/runtime      # integrity.hpp
    ${PROJECT_SOURCE_DIR}/compiler     # build_fingerprint.hpp (the host runtime manifest)
    ${PROJECT_SOURCE_DIR}/stdlib/hashlib
    ${PROJECT_SOURCE_DIR}/stdlib/ed25519
    ${PROJECT_SOURCE_DIR}/stdlib/aead
    ${PROJECT_SOURCE_DIR}/stdlib/p256)

# compiler_bench.cpp reads real .purr from the source tree at run time.
target_compile_definitions(cheatah_benchmarks PRIVATE CHEATAH_SRC_ROOT="${PROJECT_SOURCE_DIR}")

# OpenSSL (libcrypto) — the reference for the crypto comparison benchmark. Optional: when
# absent, crypto_openssl_bench.cpp builds its cheatah-only rows (the OpenSSL rows are gated
# on CHEATAH_HAVE_OPENSSL), so the benchmark never hard-depends on it.
find_package(OpenSSL QUIET COMPONENTS Crypto)
if(OpenSSL_FOUND)
    target_link_libraries(cheatah_benchmarks PRIVATE OpenSSL::Crypto)
    target_compile_definitions(cheatah_benchmarks PRIVATE CHEATAH_HAVE_OPENSSL)
endif()

# Eigen (header-only, fetched in the top-level CMakeLists) and GLM (system headers).
if(DEFINED eigen_SOURCE_DIR)
    target_include_directories(cheatah_benchmarks SYSTEM PRIVATE ${eigen_SOURCE_DIR})
endif()
# GLM resolves from the system include path (/usr/include/glm) as <glm/glm.hpp>.
