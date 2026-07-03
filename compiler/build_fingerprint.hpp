// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file build_fingerprint.hpp
 * @brief The C/C++ runtime a translation unit was COMPILED against — CPU arch, the glibc
 *        headers version, and the libstdc++/libc++ ABI release.
 *
 * Both purrc and the cheatah runtime are compiled by the SAME toolchain that builds the
 * modules (`CHEATAH_CXX`), so purrc's compile-time fingerprint is the module's build
 * runtime, and the runtime's is the host it ships on. purrc records this in a `<mod>.rt`
 * manifest; the runtime compares it against the ACTUAL host runtime (the live glibc via
 * `gnu_get_libc_version()`) before loading, so a module built against a newer C runtime
 * than the host provides is refused with a clear message instead of a cryptic
 * symbol-version `dlopen` failure.
 *
 * Header-only and dependency-free so it can be included from both compiler/ and runtime/.
 */
#include <string>

namespace cheatah {

// CPU architecture, from the compiler's target macros.
inline const char* build_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__)
    return "arm";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__riscv) && __riscv_xlen == 64
    return "riscv64";
#elif defined(__powerpc64__)
    return "ppc64";
#else
    return "unknown";
#endif
}

// The glibc version the unit was compiled against ("2.39"), or "none" off glibc. This is
// the MINIMUM glibc the module needs — its symbol versions resolve only against a glibc at
// least this new.
inline std::string build_libc() {
#if defined(__GLIBC__)
    return std::to_string(__GLIBC__) + "." + std::to_string(__GLIBC_MINOR__);
#else
    return "none";
#endif
}

// The C++ standard-library ABI the unit was compiled against. libstdc++ exposes
// `_GLIBCXX_RELEASE` (its major ABI release, e.g. 14); libc++ exposes `_LIBCPP_VERSION`.
inline std::string build_libcxx() {
#if defined(_GLIBCXX_RELEASE)
    return "libstdc++-" + std::to_string(_GLIBCXX_RELEASE);
#elif defined(_LIBCPP_VERSION)
    return "libc++-" + std::to_string(_LIBCPP_VERSION);
#else
    return "unknown";
#endif
}

// The `<mod>.rt` manifest body: a tiny line-based, signable text record of the build
// runtime. Keep the format in sync with the runtime's parser (runtime/integrity.cpp).
inline std::string build_runtime_manifest() {
    return std::string("cheatah-rt v1\narch ") + build_arch() + "\nlibc " + build_libc() +
           "\nlibcxx " + build_libcxx() + "\n";
}

} // namespace cheatah
