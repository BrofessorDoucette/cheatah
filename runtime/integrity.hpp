// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// Module-integrity verification for the cheatah runtime.
//
// Before the runtime dlopen()s a module (which runs native code in-process), it can
// check two sidecar files placed next to `<module>`:
//
//   <module>.sha512   a plain SHA-512 of the module (sha512sum-compatible). Detects
//                     ACCIDENTAL corruption. Auto-checked whenever it is present. Module
//                     integrity uses a 512-bit digest throughout; SHA-256 stays in the
//                     `hashlib` stdlib module for applications but is not used for signing.
//   <module>.sig      an Ed25519 signature over the module's bytes, plus the signer's
//                     public key. Detects deliberate TAMPERING — only the holder of the
//                     matching secret key could have produced it. Enforced only in
//                     strict mode, and only against a configured set of trusted keys.
//   <module>.rt       the C/C++ runtime the module was COMPILED against (arch, glibc,
//                     libstdc++). Checked against the host before loading, so a module
//                     built on a newer runtime is refused cleanly instead of failing in
//                     dlopen. Optionally signed (<module>.rt.sig) by a SEPARATE runtime
//                     key — the code-signing key and the runtime key are distinct.
//
// Trust model (see SECURITY.md): given an untampered runtime and a trusted public key,
// a module that does not carry a valid signature from a trusted signer is refused in
// strict mode. It does NOT defend against an attacker who can rewrite the runtime
// binary or the trusted-key file themselves.

#include <string>
#include <cstdint>
#include <vector>

namespace cheatah::integrity {

enum class Policy : std::uint8_t {
    Off,     // default: only the basic .sha512 checksum is enforced (when present).
    Strict,  // additionally REQUIRE a valid .sig from a trusted key (fail-closed).
};

struct Result {
    bool ok = false;
    std::string error;      // human-readable reason when !ok
    std::string load_path;  // path to hand to dlopen — binds the load to the verified bytes
    int fd = -1;            // open fd backing load_path (Linux); release() closes it
};

/**
 * Verify the module at @p canonical_path under @p policy against @p trusted_keys (each a
 * 64-char hex Ed25519 public key). Opens the file once and verifies the bytes it read,
 * returning in Result.load_path a path that refers to those exact bytes (on Linux,
 * /proc/self/fd/<fd>), so the subsequent dlopen cannot be raced onto a different file.
 * On any failure, ok is false and error explains why (fail-closed).
 *
 * PERFORMANCE — paid ONCE at load, never during execution; this is the whole cost of
 * turning verification on (consult it before doing so):
 * @complexity Off policy with no sidecars present: O(1) — the module is not even read.
 *   Otherwise O(n) in the module size for one SHA-512 pass (memory-bandwidth bound,
 *   ~GB/s), plus O(n) again for one Ed25519 verification when a signature is checked
 *   (the verify hashes the bytes once more, then two fixed-cost scalar multiplications,
 *   well under a millisecond).
 * @alloc Off policy with no sidecars: none. Otherwise allocates one in-memory copy of
 *   the module's bytes (read from the fd) plus small sidecar/text buffers; all freed
 *   before this returns.
 * @param canonical_path the already-sanitized module path.
 * @param policy Off (only an existing .sha512 is enforced) or Strict (a valid signature
 *   from a trusted key is REQUIRED).
 * @param trusted_keys the pinned code-signing Ed25519 public keys (64 hex each).
 * @param trusted_runtime_keys the pinned RUNTIME public keys — a SEPARATE set used only
 *   for the `<module>.rt` build-runtime manifest. Empty means the `.rt` (if present) is
 *   still checked for ABI compatibility but its signature is not required.
 * @return a Result; on success, dlopen Result.load_path then call release().
 * @test ModuleIntegrity.SignedModuleVerifiesAndRuns,
 *   ModuleIntegrity.InjectedByteFailsChecksum, ModuleIntegrity.InjectedByteFailsSignature,
 *   ModuleIntegrity.UntrustedSignerRejected, ModuleIntegrity.StrictRefusesUnsignedModule,
 *   ModuleIntegrity.UnsignedRunsByDefault, RuntimeCheck.CompatibleRuntimeRuns,
 *   RuntimeCheck.IncompatibleGlibcRefused, RuntimeCheck.SeparateRuntimeKeyVerifies
 */
Result verify_module(const std::string& canonical_path, Policy policy,
                     const std::vector<std::string>& trusted_keys,
                     const std::vector<std::string>& trusted_runtime_keys = {});

/**
 * Close the fd held by a Result (call after dlopen, success or failure).
 * @param r the Result whose backing fd to close (no-op if none).
 * @complexity O(1) — one close().
 * @alloc none.
 * @test ModuleIntegrity.SignedModuleVerifiesAndRuns
 */
void release(Result& r);

/**
 * Load the trusted public keys (one 64-hex key per non-empty, non-`#` line) from @p path.
 * @param path the trust file (e.g. ~/.config/cheatah/trusted.pub).
 * @return the keys; empty if the file is absent or unreadable.
 * @complexity O(m) in the file size.
 * @alloc allocates the returned key strings.
 * @test ModuleIntegrity.SignedModuleVerifiesAndRuns, ModuleIntegrity.UntrustedSignerRejected
 */
std::vector<std::string> load_trusted_keys(const std::string& path);

} // namespace cheatah::integrity
