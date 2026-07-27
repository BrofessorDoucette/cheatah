// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level tests for cheatah's module-integrity feature, written as worked EXAMPLES
// of how cheatah keeps a tampered/injected binary from being run. Each test drives the
// REAL toolchain end to end:
//
//   purrc --keygen <prefix>          generate an Ed25519 signing keypair
//   purrc app.purr -o app.so --sign  build a module and sign it (writes app.so.sig +
//                                    app.so.sha512)
//   cheatah [--verify] app.so        the runtime verifies BEFORE it loads/runs
//
// The story they tell: a genuine signed module runs; flip a single byte (an injection
// into the binary) and the runtime REFUSES to load it; demand a signature and an
// unsigned or untrusted-signer module is refused. This is the same Ed25519 + SHA-512
// exercised by ed25519_sys_test.cpp / hashlib_sys_test.cpp, now guarding the loader.
//
// Suite name ModuleIntegrity (not *CompileRun*) so the QA gate always runs it.
#include "e2e_harness.hpp"

#include <fstream>
#include <string>

namespace {

const std::string kTmp = PURR_TEST_TMP;

// Run a command, returning {exit_code, combined_stdout/stderr}.
struct Proc { int code; std::string out; };
Proc run_cmd(const std::string& cmd) {
    int code = -1;
    const std::string out = e2e::run_capture(cmd + " 2>&1", code);
    return {code, out};
}

std::string q(const std::string& s) { return "\"" + s + "\""; }

void write_purr(const std::string& path, const std::string& src) {
    std::ofstream f(path);
    f << src;
}

// Flip one byte in the middle of a file (simulate an injection into the binary).
void flip_a_byte(const std::string& path) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    const std::streamoff at = size / 2;
    f.seekg(at);
    char c = 0;
    f.read(&c, 1);
    c = static_cast<char>(c ^ 0xFF);
    f.seekp(at);
    f.write(&c, 1);
}

bool exists(const std::string& p) { std::ifstream f(p); return f.good(); }
void remove_file(const std::string& p) { std::remove(p.c_str()); }

// Build a module at <name>.so from @p src; returns its path. Optionally sign/checksum it
// via @p extra (e.g. "--sign key.key", "--checksum").
std::string build_module_src(const std::string& name, const std::string& src,
                             const std::string& extra = "") {
    const std::string purr = kTmp + "/" + name + ".purr";
    const std::string so = kTmp + "/" + name + ".so";
    write_purr(purr, src);
    const Proc r = run_cmd(std::string(PURRC_PATH) + " " + q(purr) + " -o " + q(so) + " " + extra);
    EXPECT_EQ(r.code, 0) << name << ": purrc failed:\n" << r.out;
    return so;
}

// The default tiny module used by most tests.
std::string build_module(const std::string& name, const std::string& extra = "") {
    return build_module_src(name, "import io\nio.print(\"integrity ok\")\n", extra);
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream f(path);
    f << text;
}
std::string read_text(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Generate a keypair at <prefix>.{key,pub}; returns the public-key hex.
std::string keygen(const std::string& prefix) {
    EXPECT_EQ(run_cmd(std::string(PURRC_PATH) + " --keygen \"" + prefix + "\"").code, 0);
    std::string pub = read_text(prefix + ".pub");
    while (!pub.empty() && (pub.back() == '\n' || pub.back() == ' ')) pub.pop_back();
    return pub;
}

std::string CH(const std::string& args) { return std::string(CHEATAH_RUNTIME_PATH) + " " + args; }

} // namespace

// A genuine signed module: the runtime verifies the signature against the trusted key
// and runs it.
TEST(ModuleIntegrity, SignedModuleVerifiesAndRuns) {
    const std::string key = kTmp + "/genuine";
    ASSERT_EQ(run_cmd(std::string(PURRC_PATH) + " --keygen " + q(key)).code, 0);
    const std::string so = build_module("genuine_app", "--sign " + q(key + ".key"));
    ASSERT_TRUE(exists(so + ".sig"));
    ASSERT_TRUE(exists(so + ".sha512"));

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(key + ".pub") + " " + CH("--verify " + q(so)));
    EXPECT_EQ(r.code, 0) << r.out;
    EXPECT_EQ(r.out, "integrity ok\n");
}

// Inject a byte into a checksummed module: even with NO verification flag, the auto
// checksum catches the corruption and the runtime refuses to load it.
TEST(ModuleIntegrity, InjectedByteFailsChecksum) {
    const std::string so = build_module("corrupt_app", "--checksum");
    ASSERT_TRUE(exists(so + ".sha512"));
    flip_a_byte(so);

    const Proc r = run_cmd(CH(q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("checksum mismatch"), std::string::npos) << r.out;
}

// Inject a byte into a SIGNED module (no checksum sidecar): strict verification rejects
// it because the Ed25519 signature no longer matches the bytes.
TEST(ModuleIntegrity, InjectedByteFailsSignature) {
    const std::string key = kTmp + "/sigtamper";
    ASSERT_EQ(run_cmd(std::string(PURRC_PATH) + " --keygen " + q(key)).code, 0);
    const std::string so = build_module("sigtamper_app", "--sign " + q(key + ".key"));
    remove_file(so + ".sha512");  // isolate the signature as the only gate
    flip_a_byte(so);

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(key + ".pub") + " " + CH("--verify " + q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("does not verify"), std::string::npos) << r.out;
}

// A module signed by an UNTRUSTED key (an attacker re-signs their injected build with
// their own key) is refused: the verifier only trusts the keys it pins.
TEST(ModuleIntegrity, UntrustedSignerRejected) {
    const std::string good = kTmp + "/good_signer";
    const std::string evil = kTmp + "/evil_signer";
    ASSERT_EQ(run_cmd(std::string(PURRC_PATH) + " --keygen " + q(good)).code, 0);
    ASSERT_EQ(run_cmd(std::string(PURRC_PATH) + " --keygen " + q(evil)).code, 0);
    const std::string so = build_module("untrusted_app", "--sign " + q(evil + ".key"));

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(good + ".pub") + " " + CH("--verify " + q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("untrusted key"), std::string::npos) << r.out;
}

// Strict mode REQUIRES a signature: an unsigned module is refused (fail-closed), so an
// attacker cannot bypass the check just by stripping the .sig.
TEST(ModuleIntegrity, StrictRefusesUnsignedModule) {
    const std::string key = kTmp + "/strict_unsigned";
    ASSERT_EQ(run_cmd(std::string(PURRC_PATH) + " --keygen " + q(key)).code, 0);
    const std::string so = build_module("strict_unsigned_app");  // no sidecars

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(key + ".pub") + " " + CH("--verify " + q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("no signature found"), std::string::npos) << r.out;
}

// Default (non-strict) mode preserves today's behavior with zero overhead: a module with
// no sidecars at all just runs.
TEST(ModuleIntegrity, UnsignedRunsByDefault) {
    const std::string so = build_module("plain_app");  // no sidecars
    const Proc r = run_cmd(CH(q(so)));
    EXPECT_EQ(r.code, 0) << r.out;
    EXPECT_EQ(r.out, "integrity ok\n");
}

// Strict mode can be turned on by ENVIRONMENT alone (CHEATAH_VERIFY=strict), no flag —
// a genuine module runs, an unsigned one is refused.
TEST(ModuleIntegrity, EnvStrictEnforcesWithoutFlag) {
    const std::string key = kTmp + "/env_strict";
    keygen(key);
    const std::string so = build_module("env_strict_app", "--sign " + q(key + ".key"));
    const std::string env = "CHEATAH_VERIFY=strict CHEATAH_TRUST=" + q(key + ".pub") + " ";

    EXPECT_EQ(run_cmd(env + CH(q(so))).code, 0);                 // signed + trusted -> runs
    const std::string un = build_module("env_strict_unsigned");  // no sidecars
    EXPECT_NE(run_cmd(env + CH(q(un))).code, 0);                  // unsigned -> refused
}

// The trust set can be supplied per-run with --trust (no environment), and strict mode
// can't be turned back off — there is no --no-verify, so a stray flag can't downgrade it.
TEST(ModuleIntegrity, TrustFlagWorksAndStrictIsNonDowngradable) {
    const std::string key = kTmp + "/trustflag";
    keygen(key);
    const std::string so = build_module("trustflag_app", "--sign " + q(key + ".key"));

    EXPECT_EQ(run_cmd(CH("--verify --trust " + q(key + ".pub") + " " + q(so))).code, 0);
    // There is no flag to disable verification, so an unsigned module stays refused.
    const std::string un = build_module("trustflag_unsigned");
    EXPECT_NE(run_cmd(CH("--verify --trust " + q(key + ".pub") + " " + q(un))).code, 0);
    // An unknown option is rejected outright (can't sneak a "--no-verify" past it).
    EXPECT_NE(run_cmd(CH("--no-verify " + q(so))).code, 0);
}

// A trust file may hold MULTIPLE keys; a module signed by ANY one of them verifies. (Uses
// two distinct keypairs.)
TEST(ModuleIntegrity, MultipleTrustedKeysOneSigns) {
    const std::string k1 = kTmp + "/multi1", k2 = kTmp + "/multi2";
    const std::string pub1 = keygen(k1), pub2 = keygen(k2);
    const std::string trust = kTmp + "/multi.trust";
    write_text(trust, pub1 + "\n" + pub2 + "\n");                 // trust BOTH keys
    const std::string so = build_module("multi_app", "--sign " + q(k2 + ".key"));  // signed by #2

    EXPECT_EQ(run_cmd("CHEATAH_TRUST=" + q(trust) + " " + CH("--verify " + q(so))).code, 0);
}

// The trust file ignores blank lines and #-comments, so it can be annotated.
TEST(ModuleIntegrity, TrustFileIgnoresCommentsAndBlanks) {
    const std::string key = kTmp + "/commented";
    const std::string pub = keygen(key);
    const std::string trust = kTmp + "/commented.trust";
    write_text(trust, "# cheatah release keys\n\n  # the 2026 signing key\n" + pub + "  release-signer\n\n");
    const std::string so = build_module("commented_app", "--sign " + q(key + ".key"));

    EXPECT_EQ(run_cmd("CHEATAH_TRUST=" + q(trust) + " " + CH("--verify " + q(so))).code, 0);
}

// The signature binds to the module's BYTES: a valid signature lifted from a DIFFERENT
// module (even one signed by the same trusted key) does not verify here. So an attacker
// can't paste a genuine signature onto an injected binary.
TEST(ModuleIntegrity, SignatureFromAnotherModuleRejected) {
    const std::string key = kTmp + "/binder";
    const std::string pub = keygen(key);
    const std::string a = build_module_src("bind_a", "import io\nio.print(\"A\")\n",
                                           "--sign " + q(key + ".key"));
    const std::string b = build_module_src("bind_b", "import io\nio.print(\"B\")\n",
                                           "--sign " + q(key + ".key"));
    write_text(b + ".sig", read_text(a + ".sig"));  // paste A's (genuine) signature onto B

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(key + ".pub") + " " + CH("--verify " + q(b)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("does not verify"), std::string::npos) << r.out;
}

// Claiming a trusted public key in the .sig while the signature was actually made by a
// DIFFERENT key is rejected — you must possess the trusted key's secret to sign.
TEST(ModuleIntegrity, ClaimedTrustedKeyButForeignSignatureRejected) {
    const std::string trusted = kTmp + "/legit", attacker = kTmp + "/forger";
    const std::string tpub = keygen(trusted), apub = keygen(attacker);
    const std::string so = build_module("claimed_app", "--sign " + q(attacker + ".key"));
    // Rewrite the .sig to CLAIM the trusted key, keeping the attacker's signature bytes.
    std::string sig = read_text(so + ".sig");
    const auto at = sig.find(apub);
    ASSERT_NE(at, std::string::npos);
    sig.replace(at, apub.size(), tpub);
    write_text(so + ".sig", sig);

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(trusted + ".pub") + " " + CH("--verify " + q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("does not verify"), std::string::npos) << r.out;
}

// A malformed signature file in strict mode is refused (fail-closed), not ignored.
TEST(ModuleIntegrity, MalformedSignatureFileRejected) {
    const std::string key = kTmp + "/malformed";
    keygen(key);
    const std::string so = build_module("malformed_app", "--sign " + q(key + ".key"));
    write_text(so + ".sig", "cheatah-sig v1\npubkey not-hex\nsig garbage\n");

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(key + ".pub") + " " + CH("--verify " + q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("malformed signature"), std::string::npos) << r.out;
}

// Verifying does not disturb sys.argv forwarding: leading flags are consumed, and the
// program still sees its own arguments (argv[0] is the module, argv[1:] the user args).
TEST(ModuleIntegrity, ArgsForwardedUnderVerify) {
    const std::string key = kTmp + "/argv";
    keygen(key);
    const std::string so = build_module_src(
        "argv_app",
        "import io\nimport sys\nio.print(len(sys.argv))\nio.print(sys.argv[1], sys.argv[2], sys.argv[3])\n",
        "--sign " + q(key + ".key"));

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(key + ".pub") + " " +
                           CH("--verify " + q(so) + " alpha beta gamma"));
    EXPECT_EQ(r.code, 0) << r.out;
    EXPECT_EQ(r.out, "4\nalpha beta gamma\n");
}

// The .sha512 sidecar is plain sha512sum format, so standard coreutils validate it too.
TEST(ModuleIntegrity, ChecksumSidecarIsSha512sumCompatible) {
    if (run_cmd("command -v sha512sum").code != 0) GTEST_SKIP() << "sha512sum not available";
    const std::string name = "sha512sum_app";
    const std::string so = build_module(name, "--checksum");
    // sha512sum -c resolves the filename relative to its cwd, so run it in the temp dir.
    EXPECT_EQ(run_cmd("cd " + q(kTmp) + " && sha512sum -c " + q(name + ".so.sha512")).code, 0);
    flip_a_byte(so);
    EXPECT_NE(run_cmd("cd " + q(kTmp) + " && sha512sum -c " + q(name + ".so.sha512")).code, 0);
}

// ============================================================================
// C-runtime validation (suite RuntimeCheck): the <module>.rt build-runtime manifest
// is checked against the HOST before loading, and is signed by a key SEPARATE from the
// code-signing key. These run the real toolchain end to end.
// ============================================================================
namespace {
// Replace the "<key> ..." line of a module's .rt manifest with "<key> <val>".
void rewrite_rt_field(const std::string& so, const std::string& key, const std::string& val) {
    std::istringstream in(read_text(so + ".rt"));
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + " ", 0) == 0) out << key << " " << val << "\n";
        else out << line << "\n";
    }
    write_text(so + ".rt", out.str());
}
}  // namespace

// A module whose recorded build runtime matches this host loads and runs (the manifest is
// emitted unsigned with --runtime).
TEST(RuntimeCheck, CompatibleRuntimeRuns) {
    const std::string so = build_module("rt_compat_app", "--runtime");
    ASSERT_TRUE(exists(so + ".rt"));
    const Proc r = run_cmd(CH(q(so)));
    EXPECT_EQ(r.code, 0) << r.out;
    EXPECT_EQ(r.out, "integrity ok\n");
}

// The two tests below assert a GLIBC-SPECIFIC refusal message. Off glibc the runtime records its C
// runtime as "none" (compiler/build_fingerprint.hpp), so there is nothing for a fingerprint to be
// incompatible WITH and the message never appears. Skipping is honest; failing would be reporting
// the absence of a platform's libc as a defect.
#if defined(__GLIBC__)
constexpr bool kHostHasGlibc = true;
#else
constexpr bool kHostHasGlibc = false;
#endif

// A module that demands a newer glibc than the host has is refused cleanly (not a cryptic
// dlopen failure) — even in default mode, since loading it would crash.
TEST(RuntimeCheck, IncompatibleGlibcRefused) {
    if (!kHostHasGlibc) GTEST_SKIP() << "no glibc on this platform — nothing to be incompatible with";
    const std::string so = build_module("rt_glibc_app", "--runtime");
    rewrite_rt_field(so, "libc", "99.9");  // pretend it was built against a far-future glibc
    const Proc r = run_cmd(CH(q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("needs glibc >= 99.9"), std::string::npos) << r.out;
}

// A module built for a different CPU architecture is refused.
TEST(RuntimeCheck, IncompatibleArchRefused) {
    const std::string so = build_module("rt_arch_app", "--runtime");
    rewrite_rt_field(so, "arch", "sparc64");
    const Proc r = run_cmd(CH(q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("built for arch 'sparc64'"), std::string::npos) << r.out;
}

// A malformed runtime manifest is refused.
TEST(RuntimeCheck, MalformedRuntimeManifestRefused) {
    const std::string so = build_module("rt_malformed_app", "--runtime");
    write_text(so + ".rt", "not a manifest\n");
    const Proc r = run_cmd(CH(q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("malformed build-runtime manifest"), std::string::npos) << r.out;
}

// The runtime manifest is signed by a SEPARATE runtime key; with the runtime trust set,
// strict mode verifies it (alongside the code signature under the code trust).
TEST(RuntimeCheck, SeparateRuntimeKeyVerifies) {
    const std::string codek = kTmp + "/rt_code", rtk = kTmp + "/rt_runtime";
    keygen(codek);
    keygen(rtk);
    const std::string so = build_module("rt_signed_app",
                                        "--sign " + q(codek + ".key") + " --sign-runtime " + q(rtk + ".key"));
    ASSERT_TRUE(exists(so + ".rt.sig"));
    const std::string env = "CHEATAH_VERIFY=strict CHEATAH_TRUST=" + q(codek + ".pub") +
                            " CHEATAH_RT_TRUST=" + q(rtk + ".pub") + " ";
    EXPECT_EQ(run_cmd(env + CH(q(so))).code, 0);
}

// The two features use DISTINCT keys: the code-signing key cannot stand in for the
// runtime key (or vice-versa). Trusting the code key as the runtime trust rejects the
// runtime signature, and trusting the runtime key as the code trust rejects the code
// signature.
TEST(RuntimeCheck, CodeAndRuntimeKeysAreNotInterchangeable) {
    const std::string codek = kTmp + "/distinct_code", rtk = kTmp + "/distinct_rt";
    keygen(codek);
    keygen(rtk);
    const std::string so = build_module("rt_distinct_app",
                                        "--sign " + q(codek + ".key") + " --sign-runtime " + q(rtk + ".key"));

    // Code key used where the RUNTIME key is expected -> runtime signature refused.
    const Proc a = run_cmd("CHEATAH_VERIFY=strict CHEATAH_TRUST=" + q(codek + ".pub") +
                           " CHEATAH_RT_TRUST=" + q(codek + ".pub") + " " + CH(q(so)));
    EXPECT_NE(a.code, 0);
    EXPECT_NE(a.out.find("untrusted runtime key"), std::string::npos) << a.out;

    // Runtime key used where the CODE key is expected -> code signature refused.
    const Proc b = run_cmd("CHEATAH_VERIFY=strict CHEATAH_TRUST=" + q(rtk + ".pub") +
                           " CHEATAH_RT_TRUST=" + q(rtk + ".pub") + " " + CH(q(so)));
    EXPECT_NE(b.code, 0);
    EXPECT_NE(b.out.find("untrusted key"), std::string::npos) << b.out;
}

// Tampering the signed runtime manifest is caught in strict mode with the runtime trust.
TEST(RuntimeCheck, RuntimeManifestTamperRejected) {
    const std::string codek = kTmp + "/rttamper_code", rtk = kTmp + "/rttamper_rt";
    keygen(codek);
    keygen(rtk);
    const std::string so = build_module("rt_tamper_app",
                                        "--sign " + q(codek + ".key") + " --sign-runtime " + q(rtk + ".key"));
    rewrite_rt_field(so, "libcxx", "libstdc++-3");  // alter the signed manifest (still ABI-compatible)
    const Proc r = run_cmd("CHEATAH_VERIFY=strict CHEATAH_TRUST=" + q(codek + ".pub") +
                           " CHEATAH_RT_TRUST=" + q(rtk + ".pub") + " " + CH(q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("runtime manifest signature does not verify"), std::string::npos) << r.out;
}

// The headline guarantee, stated plainly: a code-signed library that is then TAMPERED
// with (a byte injected into the binary) is refused, and the program NEVER RUNS — cheatah
// shuts down before handing control to the module, so its output never appears.
TEST(ModuleIntegrity, CodeSignedThenTamperedNeverRuns) {
    const std::string key = kTmp + "/tamper_shutdown";
    keygen(key);
    const std::string so = build_module("tamper_shutdown_app", "--sign " + q(key + ".key"));
    flip_a_byte(so);  // inject into the signed binary

    const Proc r = run_cmd("CHEATAH_TRUST=" + q(key + ".pub") + " " + CH("--verify " + q(so)));
    EXPECT_NE(r.code, 0) << r.out;
    EXPECT_EQ(r.out.find("integrity ok"), std::string::npos) << "TAMPERED MODULE EXECUTED:\n" << r.out;
    EXPECT_NE(r.out.find("refusing to load"), std::string::npos) << r.out;
}

// The C-runtime story, stated plainly: when the recorded C runtime is tampered to be
// incompatible with the host (here, made to demand a far-future glibc), cheatah detects it
// and shuts down IMMEDIATELY — before loading or running the module (no output appears).
TEST(RuntimeCheck, TamperedCRuntimeShutsDownBeforeRunning) {
    if (!kHostHasGlibc) GTEST_SKIP() << "no glibc on this platform — nothing to be incompatible with";
    const std::string so = build_module("rt_shutdown_app", "--runtime");
    rewrite_rt_field(so, "libc", "99.9");  // the C runtime fingerprint no longer matches reality

    const Proc r = run_cmd(CH(q(so)));
    EXPECT_NE(r.code, 0) << r.out;
    EXPECT_EQ(r.out.find("integrity ok"), std::string::npos) << "ran on an incompatible runtime:\n" << r.out;
    EXPECT_NE(r.out.find("needs glibc >= 99.9"), std::string::npos) << r.out;
}

// And when the C-runtime manifest is SIGNED (separate key) and then tampered, the signature
// catches it and the runtime shuts down before running — even when the tampered values are
// otherwise ABI-compatible, so only the signature can detect the edit.
TEST(RuntimeCheck, SignedCRuntimeTamperShutsDownBeforeRunning) {
    const std::string codek = kTmp + "/rtsd_code", rtk = kTmp + "/rtsd_rt";
    keygen(codek);
    keygen(rtk);
    const std::string so = build_module("rt_signed_shutdown",
                                        "--sign " + q(codek + ".key") + " --sign-runtime " + q(rtk + ".key"));
    rewrite_rt_field(so, "libcxx", "libstdc++-1");  // ABI-compatible value, but not what was signed

    const Proc r = run_cmd("CHEATAH_VERIFY=strict CHEATAH_TRUST=" + q(codek + ".pub") +
                           " CHEATAH_RT_TRUST=" + q(rtk + ".pub") + " " + CH(q(so)));
    EXPECT_NE(r.code, 0) << r.out;
    EXPECT_EQ(r.out.find("integrity ok"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find("runtime manifest signature does not verify"), std::string::npos) << r.out;
}

// Strict mode with a runtime trust configured REQUIRES the manifest: a module without a
// .rt is refused (an attacker can't strip the runtime check).
TEST(RuntimeCheck, StrictRequiresRuntimeManifest) {
    const std::string codek = kTmp + "/rtreq_code", rtk = kTmp + "/rtreq_rt";
    keygen(codek);
    keygen(rtk);
    const std::string so = build_module("rt_req_app", "--sign " + q(codek + ".key"));  // code-signed, NO .rt
    const Proc r = run_cmd("CHEATAH_VERIFY=strict CHEATAH_TRUST=" + q(codek + ".pub") +
                           " CHEATAH_RT_TRUST=" + q(rtk + ".pub") + " " + CH(q(so)));
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("no build-runtime manifest"), std::string::npos) << r.out;
}
