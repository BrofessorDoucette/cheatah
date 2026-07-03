// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// purrc — the cheatah compiler.
//
// Compiles a .purr program into a loadable module that the cheatah runtime runs:
//   purrc <input.purr> [-o <output>]
//
// Pipeline: read -> lex -> parse -> codegen C++ -> invoke the C++ backend to build
// a shared module (exporting purr_main()). The module statically links the stdlib
// libraries the program imported, so it is self-contained when the runtime loads it.

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>  // _spawnvp / _P_WAIT
#else
#include <fcntl.h>     // open with 0600 (restrict the secret key file)
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "build_fingerprint.hpp"
#include "codegen.hpp"
#include "ed25519.hpp"
#include "hashlib.hpp"
#include "parser.hpp"
#include "version.hpp"

#ifndef CHEATAH_ROOT
#define CHEATAH_ROOT ""
#endif
#ifndef CHEATAH_LIB_DIR
#define CHEATAH_LIB_DIR ""
#endif
#ifndef CHEATAH_CXX
#define CHEATAH_CXX "c++"
#endif
// The archiver used to bundle an opaque library module's compiled object into
// libcheatah_<m>.a (baked by CMake as CMAKE_AR; the default is the Linux fallback).
#ifndef CHEATAH_AR
#define CHEATAH_AR "ar"
#endif
// Platform-computed by cmake/Portability.cmake; '|'-joined flag lists + the loadable
// module extension. The defaults below are the Linux fallback (e.g. a non-CMake build).
#ifndef CHEATAH_CXXFLAGS
#define CHEATAH_CXXFLAGS "-std=c++20|-O3|-DNDEBUG|-fno-math-errno|-march=native|-fPIC|-shared|-pthread|-w"
#endif
#ifndef CHEATAH_MATHLINK
#define CHEATAH_MATHLINK "-lm"
#endif
#ifndef CHEATAH_MODULE_EXT
#define CHEATAH_MODULE_EXT ".so"
#endif

using namespace cheatah;

namespace cheatah::purrc {

// Thrown by validate_cpp() when the generated C++ fails validation (see --validate-cpp).
// A distinct type so callers can catch exactly this failure mode (vs. a backend compile or
// I/O error) and report it as "the transpiler produced C++ that did not pass validation".
class CppValidationException : public std::runtime_error {
public:
    explicit CppValidationException(const std::string& message) : std::runtime_error(message) {}
};

// Validate the GENERATED C++ translation unit AFTER codegen but BEFORE the backend compiles
// it (opt-in via --validate-cpp). This is the hook for a future static check of the emitted
// .gen.cpp — e.g. asserting invariants about the code purrc produced. NOT IMPLEMENTED yet:
// it is intentionally a no-op so the flag + call site + exception type exist as wiring. When
// implemented, throw CppValidationException(reason) on failure.
inline void validate_cpp(const std::string& generated_source, const std::string& gen_path) {
    (void)generated_source;
    (void)gen_path;
    // TODO: validate the emitted C++; on failure `throw CppValidationException("<reason>");`.
}

}  // namespace cheatah::purrc

namespace {

// Run a program directly (NO shell) so arguments are passed verbatim — file paths can't
// be interpreted as shell syntax. POSIX uses fork + execvp; Windows has no fork, so it
// uses _spawnvp (also PATH-searching, also waits). Returns the exit code, or -1.
int run_process(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
#if defined(_WIN32)
    const intptr_t rc = _spawnvp(_P_WAIT, argv[0], argv.data());
    return rc < 0 ? -1 : static_cast<int>(rc);
#else
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv.data());
        std::perror("purrc: exec C++ compiler");
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// Split a '|'-joined flag string (baked in by CMake) into args, dropping empties.
// '|' never appears in a compiler flag, so this is unambiguous.
std::vector<std::string> split_flags(const std::string& joined) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(joined);
    while (std::getline(ss, cur, '|'))
        if (!cur.empty()) out.push_back(cur);
    return out;
}

// Default output: drop a trailing ".purr", append the platform module extension
// (.so / .dylib / .dll).
std::string default_output(const std::string& input) {
    std::string base = input;
    const std::string ext = ".purr";
    if (base.size() >= ext.size() && base.compare(base.size() - ext.size(), ext.size(), ext) == 0) {
        base.erase(base.size() - ext.size());
    }
    return base + CHEATAH_MODULE_EXT;
}

// ---- integrity sidecars: checksum + Ed25519 signing -----------------------------------
std::string read_binary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << data;
    return static_cast<bool>(f);
}

// Write a SECRET file, owner-read/write only from the moment it is created (no window in
// which the seed exists at default permissions). POSIX: open with mode 0600.
bool write_secret_file(const std::string& path, const std::string& data) {
#if !defined(_WIN32)
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const ssize_t w = ::write(fd, p, left);
        if (w <= 0) { ::close(fd); return false; }
        p += w;
        left -= static_cast<std::size_t>(w);
    }
    return ::close(fd) == 0;
#else
    return write_file(path, data);  // Windows ACLs are out of scope here
#endif
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string base_name(const std::string& p) {
    const std::size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

// Directory portion of @p p ("." when it has none) — where a library's sidecars/archive
// are written next to its header.
std::string dir_name(const std::string& p) {
    const std::size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

// Write <output>.sha512 in sha512sum format (so `sha512sum -c` also validates it).
// Module integrity uses SHA-512 — a 512-bit digest — throughout; SHA-256 remains in the
// `hashlib` stdlib module for applications that want it, but is not used for signing.
bool write_checksum(const std::string& output) {
    const std::string hex = cheatah::hashlib::sha512(read_binary(output));
    return write_file(output + ".sha512", hex + "  " + base_name(output) + "\n");
}

// Sign @p data with the Ed25519 secret seed in @p keyfile, writing @p sigpath in the
// cheatah-sig format. @p label names the artifact in messages. Returns 0 on success.
int sign_blob(const std::string& keyfile, const std::string& data, const std::string& sigpath,
              const std::string& label) {
    const std::string secret = trim(read_binary(keyfile));
    if (secret.size() != 64) {
        std::cerr << "purrc: '" << keyfile << "' is not a 64-hex Ed25519 secret key\n";
        return 1;
    }
    std::string pub, sig;
    try {
        pub = cheatah::ed25519::public_key(secret);
        sig = cheatah::ed25519::sign(secret, data);
    } catch (const std::exception& e) {
        std::cerr << "purrc: " << label << " signing failed: " << e.what() << "\n";
        return 1;
    }
    if (!write_file(sigpath, "cheatah-sig v1\npubkey " + pub + "\nsig " + sig + "\n")) {
        std::cerr << "purrc: cannot write '" << sigpath << "'\n";
        return 1;
    }
    std::cerr << "purrc: " << label << " -> " << sigpath << " (public key " << pub << ")\n";
    return 0;
}

// Sign <output> with the CODE-signing key in @p keyfile (writes <output>.sig + a SHA-512
// checksum sidecar).
int sign_output(const std::string& output, const std::string& keyfile) {
    write_checksum(output);
    return sign_blob(keyfile, read_binary(output), output + ".sig", "signed " + output);
}

// Write the build C-runtime manifest <output>.rt (arch / glibc / libstdc++ the module was
// compiled against — see compiler/build_fingerprint.hpp).
bool write_runtime_manifest(const std::string& output) {
    return write_file(output + ".rt", cheatah::build_runtime_manifest());
}

// Write <output>.rt and sign it with the RUNTIME key in @p rtkey (a SEPARATE key from the
// code-signing key) -> <output>.rt.sig. Returns 0 on success.
int sign_runtime(const std::string& output, const std::string& rtkey) {
    if (!write_runtime_manifest(output)) {
        std::cerr << "purrc: cannot write '" << output << ".rt'\n";
        return 1;
    }
    return sign_blob(rtkey, cheatah::build_runtime_manifest(), output + ".rt.sig",
                     "runtime-signed " + output);
}

// Generate an Ed25519 keypair into <prefix>.key (secret, mode 0600) + <prefix>.pub.
int keygen(const std::string& prefix) {
    std::string secret, pub;
    try {
        secret = cheatah::ed25519::generate();
        pub = cheatah::ed25519::public_key(secret);
    } catch (const std::exception& e) {
        std::cerr << "purrc: key generation failed: " << e.what() << "\n";
        return 1;
    }
    const std::string keyfile = prefix + ".key";
    if (!write_secret_file(keyfile, secret + "\n") || !write_file(prefix + ".pub", pub + "\n")) {
        std::cerr << "purrc: cannot write keypair files for '" << prefix << "'\n";
        return 1;
    }
    std::cout << "purrc: wrote " << keyfile << " (SECRET — keep private) and " << prefix
              << ".pub\npublic key: " << pub << "\n";
    return 0;
}

// ---- cheatah library modules (purrc --emit-library) -------------------------------------
// A cheatah library module is a `.purr` compiled to an IMPORTABLE header (+, for opaque
// builds, a static archive) living in `namespace cheatah::<m>`. Unlike a hand-written C++
// stdlib module, purrc SIGNS its header: a `<m>.hpp.sha512` sidecar sits next to it. purrc
// uses that sidecar both to recognise a cheatah module at import time and to verify the
// header (and archive) have not changed before compiling a consumer against them.

bool file_exists(const std::string& p) { return std::ifstream(p, std::ios::binary).good(); }

// Extra ':'-separated roots to search for cheatah library modules (each holds <m>/<m>.hpp),
// so external/opaque modules — and tests — resolve without rebuilding purrc. The baked
// stdlib root is always searched last (first-party modules live there too).
// Roots supplied for THIS invocation: `--import-root <dir>` (repeatable, e.g. populated by a
// package manager from a project's dependencies) plus the directory of the file being
// compiled (so a module sitting next to the source resolves with no configuration — like
// Python importing a sibling). Searched BEFORE the env path and the baked stdlib root.
std::vector<std::string> g_import_roots;

// Extra inputs for the FINAL link, supplied by `--link <arg>` (repeatable): a compiled
// archive (or a `-l` flag) for a dependency whose definitions the build provides rather than
// the compiler — e.g. an external project's own static library for a module it `import`s by
// relative path / --import-root. Appended after the program object so its references resolve.
std::vector<std::string> g_link_inputs;

// Extra C++ compile flags passed straight through to the backend compile (`--cxxflag <flag>`,
// repeatable; also the env var CHEATAH_CXXFLAGS_EXTRA, whitespace-separated). This is how a build /
// the biome package manager forwards an extension's REQUIRED compile options — e.g. a GPU extension
// that needs `-fblocks`, `-DCHEATAH_GPU_BACKEND_METAL`, or an `-I<sdk>/include`. Injected ahead of the
// generated source so `-include` / `-I` / `-D` take effect, and applied to the program build, the
// `--emit-library` object build, and `--check`.
std::vector<std::string> g_cxx_flags;

// Append the whitespace-separated flags in CHEATAH_CXXFLAGS_EXTRA to g_cxx_flags (biome / a CMake
// build sets this in the environment for a whole build tree).
void load_env_cxx_flags() {
    const char* e = std::getenv("CHEATAH_CXXFLAGS_EXTRA");
    if (!e) return;
    std::istringstream toks{std::string(e)};
    std::string t;
    while (toks >> t) g_cxx_flags.push_back(t);
}

std::vector<std::string> module_search_paths() {
    std::vector<std::string> roots;
    if (const char* mp = std::getenv("CHEATAH_MODULE_PATH")) {
        std::string cur;
        std::istringstream ss(mp);
        while (std::getline(ss, cur, ':')) if (!cur.empty()) roots.push_back(cur);
    }
    roots.push_back(CHEATAH_ROOT);
    return roots;
}

// Parse a `<hex>  <name>` sha512sum sidecar -> the lowercased hex digest ("" on failure).
std::string read_checksum_hex(const std::string& path) {
    std::ifstream f(path);
    std::string hex;
    if (!(f >> hex)) return "";
    for (char& c : hex) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return hex;
}

// Parse a `cheatah-sig v1` sidecar -> (pubkey, sig) hex; false on malformed input.
bool read_sig(const std::string& path, std::string& pub, std::string& sig) {
    std::ifstream f(path);
    std::string line;
    if (!std::getline(f, line) || line.rfind("cheatah-sig", 0) != 0) return false;
    pub.clear();
    sig.clear();
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string tag, val;
        ls >> tag >> val;
        if (tag == "pubkey") pub = val;
        else if (tag == "sig") sig = val;
    }
    return !pub.empty() && !sig.empty();
}

// Trust list (authorized signing public keys) for STRICT verification. Reuses the runtime
// convention: a file named by $CHEATAH_TRUST, one 64-hex key per non-comment line. Empty
// (the default) => checksum-only: a change/corruption is STILL always caught.
std::vector<std::string> load_trust() {
    std::vector<std::string> keys;
    const char* tp = std::getenv("CHEATAH_TRUST");
    if (!tp) return keys;
    std::ifstream f(tp);
    std::string line;
    while (std::getline(f, line)) {
        const std::string k = trim(line);
        if (!k.empty() && k[0] != '#') keys.push_back(k);
    }
    return keys;
}

// Verify one artifact (a module header or its archive) against its sidecars. The checksum
// is ALWAYS enforced (detects any change). When @p trusted is non-empty a valid `.sig` from
// a trusted key is additionally REQUIRED (fail-closed). "" on success, else the reason.
std::string verify_artifact(const std::string& path, const std::vector<std::string>& trusted) {
    const std::string want = read_checksum_hex(path + ".sha512");
    if (want.empty()) return "missing/unreadable checksum '" + path + ".sha512'";
    if (cheatah::hashlib::sha512(read_binary(path)) != want)
        return "checksum mismatch for '" + path + "' (it changed since signing)";
    if (trusted.empty()) return "";  // checksum-only ("Off")
    std::string pub, sig;
    if (!read_sig(path + ".sig", pub, sig))
        return "missing/invalid signature for '" + path + "'";
    bool trust_ok = false;
    for (const std::string& k : trusted) if (k == pub) { trust_ok = true; break; }
    if (!trust_ok) return "'" + path + "' is signed by an untrusted key (" + pub + ")";
    if (!cheatah::ed25519::verify(pub, read_binary(path), sig))
        return "bad signature for '" + path + "'";
    return "";
}

// A resolved import: where its header is included from, and the archive to link (empty for
// a header-only transparent cheatah module or — historically — none).
struct ResolvedModule {
    std::string include_dir;  // -I<this> so `#include "<m>.hpp"` resolves
    std::string archive;      // libcheatah_<m>.a to link, or "" if header-only / built elsewhere
    bool cheatah_lib = false; // true: a purrc-emitted, signed cheatah module
    std::string header;       // the resolved header path
    bool resolved = false;    // false: no header found in any root (caller reports a clear error)
};

// Classify + locate an imported module `m`, searching, in order: the relative/declared roots
// (the source's own dir + `--import-root`), the env path, then the baked stdlib root. A module
// is whatever exposes a `<m>.hpp` — either as `<root>/<m>/<m>.hpp` (a package dir) or
// `<root>/<m>.hpp` (a header sitting right next to the source, Python-sibling style). A signed
// sidecar (`.hpp.sha512`) marks a verified cheatah library; an unsigned header is a plain
// local/external module (its compiled definitions, if any, are linked by the build, not here).
// A co-located `libcheatah_<m>.a` is linked if present, but is NOT required.
ResolvedModule resolve_module(const std::string& m) {
    // 1) Caller-declared / relative roots (`--import-root` + the source's own dir). A module
    //    here may be a signed cheatah library OR a plain header — as a package dir
    //    (<root>/<m>/<m>.hpp) or a sibling file (<root>/<m>.hpp), Python-style. Its compiled
    //    definitions, if any, are the build's job; a co-located archive is linked if present
    //    but not required. This is the path an external project / package manager uses.
    for (const std::string& root : g_import_roots) {
        for (const std::string& inc : {root + "/" + m, root}) {
            const std::string hdr = inc + "/" + m + ".hpp";
            if (!file_exists(hdr)) continue;
            ResolvedModule r;
            r.include_dir = inc;
            r.header = hdr;
            r.resolved = true;
            r.cheatah_lib = file_exists(hdr + ".sha512");
            const std::string ar = inc + "/libcheatah_" + m + ".a";
            if (file_exists(ar)) r.archive = ar;
            return r;
        }
    }
    // 2) Signed cheatah library modules on the env path / baked root (unchanged behavior).
    for (const std::string& root : module_search_paths()) {
        const std::string dir = root + "/" + m;
        const std::string hdr = dir + "/" + m + ".hpp";
        if (file_exists(hdr + ".sha512")) {
            ResolvedModule r;
            r.cheatah_lib = true;
            r.include_dir = dir;
            r.header = hdr;
            r.resolved = true;
            const std::string ar = dir + "/libcheatah_" + m + ".a";
            if (file_exists(ar)) r.archive = ar;  // opaque module: link its hidden defs
            return r;
        }
    }
    // 3) Baked toolchain fallback (first-party stdlib modules under the toolchain root; their
    //    archives live in the lib dir). A HEADER-ONLY stdlib module (e.g. `memory`) has no archive,
    //    so link it only if the .a actually exists — matching the guards at (1)/(2). Setting a
    //    nonexistent archive here would fail the link of every header-only import.
    ResolvedModule r;
    r.include_dir = std::string(CHEATAH_ROOT) + "/" + m;
    const std::string ar = std::string(CHEATAH_LIB_DIR) + "/libcheatah_" + m + ".a";
    if (file_exists(ar)) r.archive = ar;
    r.header = r.include_dir + "/" + m + ".hpp";
    r.resolved = file_exists(r.header);
    return r;
}

// Report any imported module that resolves to no header. A clear compiler error (telling the
// user exactly how to point the compiler at the module) beats the confusing downstream C++
// "<m>.hpp: No such file or directory". Returns true iff every module resolved.
bool all_modules_resolved(const std::vector<std::string>& modules, const std::string& src) {
    bool ok = true;
    for (const std::string& m : modules) {
        if (resolve_module(m).resolved) continue;
        ok = false;
        std::cerr << "purrc: " << src << ": cannot resolve `import " << m << "`: no '" << m
                  << "/" << m << ".hpp' or '" << m << ".hpp' relative to the source, and no "
                     "module '" << m << "' on the import path. Place it next to the source, "
                     "pass --import-root <dir>, or declare it as a dependency (cheatah.toml).\n";
    }
    return ok;
}

// External link flags a module's header declares via a `// cheatah-link:` marker (scanned
// from the first few lines, like `// cheatah-deps:`). A module whose hidden definitions
// call into a native library it STATICALLY BUNDLES into its own archive still needs the
// final link to pull in that library's own system dependencies (e.g. a module bundling an
// HTTP client adds `-lcurl`; one spawning threads adds `-lpthread`). Each whitespace-
// separated token is appended verbatim to the consumer's link command. Returns the tokens
// in header order ("" never produced). Empty for the vast majority of modules.
std::vector<std::string> module_link_flags(const std::string& header_path) {
    std::vector<std::string> flags;
    std::ifstream hdr(header_path);
    std::string line;
    const std::string tag = "// cheatah-link:";
    for (int scanned = 0; scanned < 8 && std::getline(hdr, line); ++scanned) {
        if (line.compare(0, tag.size(), tag) != 0) continue;
        std::istringstream toks(line.substr(tag.size()));
        std::string t;
        while (toks >> t) flags.push_back(t);
        break;
    }
    return flags;
}

// Emit @p input as a cheatah library module: write the importable header, and (opaque
// builds) compile the hidden definitions into libcheatah_<m>.a. Both artifacts get a
// SHA-512 checksum sidecar (so a consumer can confirm they have not changed) and, with
// --sign, an Ed25519 signature. Returns a process exit code.
int emit_library(const std::string& input, const std::string& source, std::string output,
                 bool transparent, const std::string& sign_key, bool remove_unused, bool split,
                 const std::string& reexport_ns) {
    if (output.empty()) {
        std::string base = input;
        const std::string ext = ".purr";
        if (base.size() >= ext.size() && base.compare(base.size() - ext.size(), ext.size(), ext) == 0)
            base.erase(base.size() - ext.size());
        output = base + ".hpp";
    }
    std::string name = base_name(output);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".hpp") == 0)
        name.erase(name.size() - 4);

    const ParseResult pr = parse_source(source);
    if (!pr.ok()) {
        for (const Diagnostic& d : pr.diagnostics)
            std::cerr << input << ":" << d.pos.line << ":" << d.pos.column << ": error: "
                      << d.message << "\n";
        return 1;
    }
    LibOptions opts;
    opts.module_name = name;
    // --split needs the header/impl SPLIT codegen (out-of-line defs in impl_source), the same split
    // opaque mode uses — but we emit it as a sibling .cpp instead of compiling + hiding it. So a split
    // build is never "transparent" (which would inline everything into the header).
    opts.transparent = transparent && !split;
    opts.remove_unused = remove_unused;
    const CodegenResult cg = codegen_library(pr.program, opts);
    if (!cg.ok()) {
        for (const std::string& d : cg.diagnostics) std::cerr << "purrc: " << d << "\n";
        return 1;
    }

    // Record the module's own imports in the header (`// cheatah-deps: ...`), so a program
    // importing THIS module transitively resolves and links them (see all_modules below).
    std::string header_text = cg.header_source;
    if (!cg.modules.empty()) {
        std::string marker = "// cheatah-deps:";
        for (const std::string& dep : cg.modules) marker += " " + dep;
        marker += "\n";
        const std::size_t first_nl = header_text.find('\n');
        header_text.insert(first_nl == std::string::npos ? header_text.size() : first_nl + 1,
                           marker);
    }
    // --reexport: append a host-namespace alias so a consumer can reference <ns>::<m> directly — the
    // job a hand-written re-export shim used to do. The module itself stays ::cheatah::<m>.
    if (!reexport_ns.empty()) {
        header_text += "\n// Re-exported under the host namespace (--reexport " + reexport_ns + "):\n";
        header_text += "namespace " + reexport_ns + " { namespace " + name + " = ::cheatah::" + name
                     + "; }\n";
    }
    if (!write_file(output, header_text)) {
        std::cerr << "purrc: cannot write '" << output << "'\n";
        return 1;
    }

    // --split: pure transpilation. Emit the out-of-line definitions as a sibling <m>.cpp (portable
    // C++ that #includes <m>.hpp) and STOP — no C++ compile, no archive, no signing. The host build
    // compiles the .cpp with any C++ compiler and its own flags (release, -march=native, ...), so a
    // .purr module drops into a normal C++ build as a plain <m>.hpp + <m>.cpp pair (no gen/ folder,
    // no purrc-managed archive). Templates/inline stay in the header; long defs live in the source.
    if (split) {
        std::string cpp_path = output;
        if (cpp_path.size() > 4 && cpp_path.compare(cpp_path.size() - 4, 4, ".hpp") == 0)
            cpp_path.replace(cpp_path.size() - 4, 4, ".cpp");
        else
            cpp_path += ".cpp";
        if (!write_file(cpp_path, cg.impl_source)) {
            std::cerr << "purrc: cannot write '" << cpp_path << "'\n";
            return 1;
        }
        std::cerr << "purrc: " << input << " -> " << output << " + " << cpp_path << " (split)\n";
        return 0;
    }

    // Opaque: compile the hidden definitions to an object and archive them. The object
    // compile reuses the program flags but targets a relocatable object (-c, no -shared).
    if (!transparent) {
        const std::string impl_path = output + ".impl.gen.cpp";
        const std::string obj_path = output + ".impl.o";
        const std::string archive = dir_name(output) + "/libcheatah_" + name + ".a";
        if (!write_file(impl_path, cg.impl_source)) {
            std::cerr << "purrc: cannot write '" << impl_path << "'\n";
            return 1;
        }
        std::vector<std::string> cc = {CHEATAH_CXX};
        for (const std::string& f : split_flags(CHEATAH_CXXFLAGS))
            if (f != "-shared") cc.push_back(f);
        cc.push_back("-c");
        for (const std::string& f : g_cxx_flags) cc.push_back(f);      // build/biome passthrough
        cc.push_back(std::string("-I") + CHEATAH_ROOT + "/builtins");  // the cheatah prelude
        cc.push_back(std::string("-I") + dir_name(output));            // <m>.hpp
        for (const std::string& dep : cg.modules)
            cc.push_back(std::string("-I") + CHEATAH_ROOT + "/" + dep);
        cc.push_back(impl_path);
        cc.push_back("-o");
        cc.push_back(obj_path);
        if (run_process(cc) != 0) {
            std::cerr << "purrc: C++ backend failed building '" << name << "' implementation\n";
            return 1;
        }
        std::vector<std::string> ar = {CHEATAH_AR, "rcs", archive, obj_path};
        if (run_process(ar) != 0) {
            std::cerr << "purrc: archiver failed for '" << archive << "'\n";
            return 1;
        }
        // Delete the intermediate impl TU + object: keeping the generated `.cpp` would
        // LEAK the very source an opaque build exists to hide. The signed archive is the
        // only shipped artifact carrying the definitions.
        std::remove(impl_path.c_str());
        std::remove(obj_path.c_str());
        if (!sign_key.empty()) {
            if (const int rc = sign_output(archive, sign_key); rc != 0) return rc;
        } else if (!write_checksum(archive)) {
            std::cerr << "purrc: cannot write '" << archive << ".sha512'\n";
            return 1;
        }
        std::cerr << "purrc: " << input << " -> " << output << " + " << archive
                  << " (opaque)\n";
    } else {
        std::cerr << "purrc: " << input << " -> " << output << " (transparent)\n";
    }

    // The header always carries a checksum so a consumer can verify it is unchanged; with
    // --sign it is additionally Ed25519-signed.
    if (!sign_key.empty()) {
        if (const int rc = sign_output(output, sign_key); rc != 0) return rc;
    } else if (!write_checksum(output)) {
        std::cerr << "purrc: cannot write '" << output << ".sha512'\n";
        return 1;
    }
    return 0;
}

// builtins (always available) + the imported modules + their transitive deps, in link
// order (a dependent sorts before its dependency: linalg<ndarray, ed25519<hashlib).
std::vector<std::string> all_modules(const std::vector<std::string>& imported) {
    std::vector<std::string> modules = {"builtins"};
    modules.insert(modules.end(), imported.begin(), imported.end());
    static const std::map<std::string, std::vector<std::string>> kDeps = {
        {"linalg", {"ndarray"}},
        {"ed25519", {"hashlib"}},
        {"tls", {"x25519", "aead", "hashlib", "ed25519", "socket", "p256"}},
        {"websocket", {"tls", "socket", "os"}},
        {"p256", {"hashlib"}},
    };
    std::set<std::string> seen(modules.begin(), modules.end());
    for (std::size_t i = 0; i < modules.size(); ++i) {
        const auto it = kDeps.find(modules[i]);
        if (it != kDeps.end()) {
            for (const std::string& dep : it->second)
                if (seen.insert(dep).second) modules.push_back(dep);
        }
        // A purrc-emitted cheatah library records its own imports in a `// cheatah-deps:`
        // marker (written by emit_library), so .purr-authored modules — requests is the
        // first — pull in their dependencies (socket, parsers, ...) without a hardcoded map.
        const ResolvedModule r = resolve_module(modules[i]);
        if (r.resolved && !r.header.empty()) {
            // Any resolved module (signed cheatah lib OR a plain/relative header) may declare
            // its own imports in a `// cheatah-deps:` marker, so its transitive deps are pulled
            // in without a hardcoded map. (Dedup via `seen` keeps this overlap with kDeps safe.)
            std::ifstream hdr(r.header);
            std::string line;
            for (int scanned = 0; scanned < 8 && std::getline(hdr, line); ++scanned) {
                const std::string tag = "// cheatah-deps:";
                if (line.compare(0, tag.size(), tag) != 0) continue;
                std::istringstream deps(line.substr(tag.size()));
                std::string dep;
                while (deps >> dep)
                    if (seen.insert(dep).second) modules.push_back(dep);
                break;
            }
        }
    }
    return modules;
}

// Semantic validation of the `constexpr` surface (`constexpr let` / `constexpr fn` /
// `constexpr match`). It surfaces FRIENDLY, .purr-located messages in `--check` BEFORE the
// (often cryptic) C++ backend errors a misuse would otherwise produce. It is scope-aware so
// the editor never gets a false positive: a `constexpr let` may be shadowed by a later plain
// `let` of the same name in an inner scope, and only the nearest binding decides.
class ConstexprChecker {
public:
    ConstexprChecker(const std::string& file, std::vector<std::string>& errors)
        : file_(file), errors_(errors) {}

    // Returns true iff no constexpr misuse was found.
    bool check(const Program& prog) {
        scopes_.emplace_back();  // global scope
        for (const StmtPtr& s : prog.body)
            if (s) check_stmt(*s);
        scopes_.pop_back();
        return errors_.empty();
    }

private:
    const std::string& file_;
    std::vector<std::string>& errors_;
    std::vector<std::map<std::string, bool>> scopes_;  // name -> bound by `constexpr let`?

    void bind(const std::string& n, bool is_constexpr) { scopes_.back()[n] = is_constexpr; }
    // 1 = nearest binding is constexpr, 0 = runtime, -1 = unbound (param/global/unknown).
    int nearest(const std::string& n) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto f = it->find(n);
            if (f != it->end()) return f->second ? 1 : 0;
        }
        return -1;
    }
    void err(unsigned line, const std::string& msg) {
        errors_.push_back(file_ + ":" + std::to_string(line) + ": error: " + msg);
    }
    void check_scope(const Block& body) {
        scopes_.emplace_back();
        for (const StmtPtr& s : body)
            if (s) check_stmt(*s);
        scopes_.pop_back();
    }

    void check_stmt(const Stmt& s) {
        switch (s.kind) {
            case StmtKind::Let: {
                const auto& l = static_cast<const Let&>(s);
                if (l.is_constexpr && !l.value)
                    err(s.line, "`constexpr let " + l.name + "` needs an initializer known at "
                                "compile time — a `constexpr` with no value is meaningless");
                bind(l.name, l.is_constexpr && l.value != nullptr);
                break;
            }
            case StmtKind::Assign: {
                const auto& a = static_cast<const Assign&>(s);
                if (a.target->kind == ExprKind::Ident &&
                    nearest(static_cast<const Ident&>(*a.target).name) == 1) {
                    const std::string& n = static_cast<const Ident&>(*a.target).name;
                    err(s.line, "cannot reassign `" + n + "`: it is `constexpr` (a compile-time "
                                "constant). Use a plain `let " + n + "` if it must change.");
                }
                break;
            }
            case StmtKind::If: {
                const auto& n = static_cast<const If&>(s);
                check_scope(n.then_body);
                check_scope(n.else_body);
                break;
            }
            case StmtKind::While: check_scope(static_cast<const While&>(s).body); break;
            case StmtKind::For:   check_scope(static_cast<const For&>(s).body); break;
            case StmtKind::With:  check_scope(static_cast<const With&>(s).body); break;
            case StmtKind::Try: {
                const auto& t = static_cast<const Try&>(s);
                check_scope(t.body);
                check_scope(t.catch_body);
                break;
            }
            case StmtKind::Match: {
                const auto& m = static_cast<const Match&>(s);
                for (const MatchCase& c : m.cases) check_scope(c.body);
                break;
            }
            case StmtKind::FnDef: {
                const auto& f = static_cast<const FnDef&>(s);
                scopes_.emplace_back();
                for (const std::string& p : f.params) bind(p, false);  // params are runtime values
                for (const StmtPtr& st : f.body)
                    if (st) check_stmt(*st);
                scopes_.pop_back();
                break;
            }
            default:
                break;  // other statements carry no constexpr bindings of interest
        }
    }
};

// `--check`: lex/parse/codegen and run the C++ backend in SYNTAX-ONLY mode, with `#line`
// directives so every diagnostic points at the original .purr (the VS Code extension runs
// this to surface errors — a forgotten `let`, an unresolved symbol, a wrong argument count
// or type — exactly where the C++ type system rejects them). No module/output is produced.
// Diagnostics go to stderr; returns 0 iff the program is well-formed.
int run_check(const std::string& input, const std::string& source, bool remove_unused) {
    const ParseResult pr = parse_source(source);
    if (!pr.ok()) {
        for (const Diagnostic& d : pr.diagnostics)
            std::cerr << input << ":" << d.pos.line << ":" << d.pos.column << ": error: "
                      << d.message << "\n";
        return 1;
    }
    // Friendly, .purr-located checks of the `constexpr` surface BEFORE the C++ backend — a
    // misuse (e.g. reassigning a `constexpr let`) gets a clear message here instead of a
    // cryptic "assignment of read-only variable" from the backend.
    {
        std::vector<std::string> cx_errors;
        if (!ConstexprChecker(input, cx_errors).check(pr.program)) {
            for (const std::string& e : cx_errors) std::cerr << e << "\n";
            return 1;
        }
    }
    const CodegenResult cg = codegen(pr.program, input, remove_unused);  // source_file -> #line
    if (!cg.ok()) {
        for (const std::string& d : cg.diagnostics) std::cerr << "purrc: " << d << "\n";
        return 1;
    }
    const std::string gen_path = input + ".check.gen.cpp";
    if (!write_file(gen_path, cg.source)) {
        std::cerr << "purrc: cannot write '" << gen_path << "'\n";
        return 1;
    }
    const std::vector<std::string> mods = all_modules(cg.modules);
    if (!all_modules_resolved(mods, input)) {
        std::remove(gen_path.c_str());
        return 1;
    }
    std::vector<std::string> args = {CHEATAH_CXX};
    for (const std::string& f : split_flags(CHEATAH_CXXFLAGS))
        if (f != "-shared") args.push_back(f);  // type-check only — don't try to link a module
    args.push_back("-fsyntax-only");
    for (const std::string& m : mods)
        args.push_back(std::string("-I") + resolve_module(m).include_dir);
    for (const std::string& f : g_cxx_flags) args.push_back(f);  // same flags as the real build
    args.push_back(gen_path);
    const int rc = run_process(args);
    std::remove(gen_path.c_str());  // a transient TU; the .purr is the source of truth
    return rc == 0 ? 0 : 1;
}

void print_usage(std::ostream& os) {
    os << "usage: purrc <input.purr> [-o <output>]\n"
          "         [--checksum]               write <output>.sha512 (corruption check)\n"
          "         [--sign <keyfile>]         Ed25519-sign the module (code-signing key)\n"
          "         [--runtime]                write <output>.rt (build C-runtime manifest)\n"
          "         [--sign-runtime <keyfile>] sign <output>.rt (a SEPARATE runtime key)\n"
          "         [--validate-cpp]           validate the generated C++ before compiling\n"
          "         [--import-root <dir>]      resolve `import`s from <dir> too (repeatable;\n"
          "                                    a package manager passes one per dependency)\n"
          "         [--link <archive|-lflag>]  extra input for the final link (repeatable; the\n"
          "                                    build supplies a dependency's compiled archive)\n"
          "         [--cxxflag <flag>]         extra C++ COMPILE flag, forwarded verbatim to the\n"
          "                                    backend (repeatable; e.g. -fblocks, -DFOO, -I<dir>).\n"
          "                                    Also read from CHEATAH_CXXFLAGS_EXTRA (biome sets it)\n"
          "         [--no-remove-variables]    keep unused locals (skip dead-variable removal)\n"
          "         [--no-optimize-cpp]        disable ALL generated-C++ optimizations\n"
          "         [--no-crypto-selftest]     trust SIMD crypto from CPUID, skip the runtime\n"
          "                                    hardware-crypto power-on self-test (on by default)\n"
          "       purrc --emit-library <input.purr> [-o <m>.hpp] [--transparent] [--sign <key>]\n"
          "         [--split]                  transpile to a portable <m>.hpp + <m>.cpp pair (no\n"
          "                                    C++ compile); the host build compiles the .cpp itself\n"
          "         [--reexport <ns>]          also expose the module as <ns>::<m> (namespace alias)\n"
          "                                    emit an importable cheatah library module\n"
          "                                    (signed header; opaque by default — hides the\n"
          "                                    source in libcheatah_<m>.a; --transparent\n"
          "                                    inlines the C++ source into the header)\n"
          "       purrc --check <input.purr>   syntax/type-check only; report errors against\n"
          "                                    the .purr source (used by the editor) — no output\n"
          "       purrc --keygen <prefix>      generate an Ed25519 keypair\n"
          "       purrc --version | --help\n"
          "\n"
          "Compiles a .purr program to a loadable module the cheatah runtime runs.\n";
}

} // namespace

int main(int argc, char** argv) {
    load_env_cxx_flags();  // CHEATAH_CXXFLAGS_EXTRA (build/biome-wide passthrough); --cxxflag adds more
    std::string input;
    std::string output;
    std::string sign_key;          // --sign <keyfile>: Ed25519-sign the built module (code key)
    std::string sign_runtime_key;  // --sign-runtime <keyfile>: sign the build-runtime manifest
    std::string keygen_prefix;     // --keygen <prefix>: just generate a keypair and exit
    bool want_checksum = false;    // --checksum: emit the <output>.sha512 sidecar
    bool want_runtime = false;     // --runtime: emit the <output>.rt build-runtime manifest
    bool emit_lib = false;         // --emit-library: emit an importable cheatah library module
    bool transparent = false;      // --transparent: inline the C++ source into the header (stdlib)
    bool split = false;            // --split: transpile to a portable <m>.hpp + <m>.cpp pair (decls
                                   // + templates in the header, out-of-line defs in the source) and
                                   // STOP — no C++ compile/archive. The host build compiles the .cpp
                                   // with ANY C++ compiler + its own flags. No purr_main/runtime.
    std::string reexport_ns;       // --reexport <ns>: also expose the module under <ns>::<m> (a
                                   // namespace alias appended to the header), so a host project writes
                                   // <ns>::<m>::… instead of ::cheatah::<m>::… — no hand-written shim.
    bool want_check = false;       // --check: syntax-only, #line-mapped diagnostics (editor)
    bool want_validate_cpp = false;// --validate-cpp: validate the generated C++ before compiling
    bool no_remove_vars = false;   // --no-remove-variables: keep unused locals (opt out of DCE)
    bool no_crypto_selftest = false;  // --no-crypto-selftest: skip the runtime hardware-crypto
                                      // power-on self-test (trust CPUID). The self-test is ON by
                                      // default so a program never runs an unverified SIMD crypto
                                      // path on the machine it was built for.
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--version" || a == "-v") {
            std::cout << "cheatah purrc " << version() << "\n";
            return 0;
        } else if (a == "--help" || a == "-h") {
            print_usage(std::cout);
            return 0;
        } else if (a == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (a == "--sign" && i + 1 < argc) {
            sign_key = argv[++i];
        } else if (a == "--sign-runtime" && i + 1 < argc) {
            sign_runtime_key = argv[++i];
        } else if (a == "--keygen" && i + 1 < argc) {
            keygen_prefix = argv[++i];
        } else if (a == "--checksum") {
            want_checksum = true;
        } else if (a == "--runtime") {
            want_runtime = true;
        } else if (a == "--emit-library") {
            emit_lib = true;
        } else if (a == "--transparent") {
            transparent = true;
        } else if (a == "--split") {
            split = true;
        } else if (a == "--reexport" && i + 1 < argc) {
            reexport_ns = argv[++i];
        } else if (a == "--check") {
            want_check = true;
        } else if (a == "--validate-cpp") {
            want_validate_cpp = true;
        } else if (a == "--import-root" && i + 1 < argc) {
            // A directory to resolve `import`s from (repeatable) — e.g. a package manager
            // points the compiler at each declared dependency. Searched before the env path.
            g_import_roots.push_back(argv[++i]);
        } else if (a == "--link" && i + 1 < argc) {
            // An extra archive (or -l flag) for the final link (repeatable) — the build
            // provides a dependency's compiled definitions; the compiler just resolves headers.
            g_link_inputs.push_back(argv[++i]);
        } else if (a == "--cxxflag" && i + 1 < argc) {
            // An extra C++ COMPILE flag (repeatable) forwarded verbatim to the backend — e.g.
            // `-fblocks`, `-DCHEATAH_GPU_BACKEND_METAL`, `-I<sdk>/include`. The build/biome supplies
            // an extension's required compile options this way (also CHEATAH_CXXFLAGS_EXTRA).
            g_cxx_flags.push_back(argv[++i]);
        } else if (a == "--no-remove-variables") {
            no_remove_vars = true;
        } else if (a == "--no-optimize-cpp") {
            // Umbrella that disables ALL generated-C++ optimizations. Currently that is just
            // dead-variable removal; future optimization opt-outs are added here too.
            no_remove_vars = true;
        } else if (a == "--no-crypto-selftest") {
            no_crypto_selftest = true;
        } else if (input.empty()) {
            input = a;
        }
    }

    // Key generation is standalone — it builds nothing.
    if (!keygen_prefix.empty()) return keygen(keygen_prefix);

    if (input.empty()) {
        print_usage(std::cerr);
        return 2;
    }

    std::ifstream in(input);
    if (!in) {
        std::cerr << "purrc: cannot open '" << input << "'\n";
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();

    // Resolve `import`s relative to the file being compiled FIRST (Python-style: a module
    // next to the source, or in a subfolder, just works). Declared --import-root dirs follow.
    g_import_roots.insert(g_import_roots.begin(), dir_name(input));

    // Syntax-only check (the editor's error provider): no module is produced.
    if (want_check) return run_check(input, source, /*remove_unused=*/!no_remove_vars);

    // Library mode emits an importable cheatah module (signed header [+ opaque archive]),
    // not a runnable program — it computes its own default output, so dispatch before the
    // program `.so` defaulting below.
    if (emit_lib)
        return emit_library(input, source, output, transparent, sign_key, !no_remove_vars, split,
                            reexport_ns);

    if (output.empty()) {
        output = default_output(input);
    }

    const ParseResult pr = parse_source(source);
    if (!pr.ok()) {
        for (const Diagnostic& d : pr.diagnostics) {
            std::cerr << input << ":" << d.pos.line << ":" << d.pos.column << ": error: "
                      << d.message << "\n";
        }
        return 1;
    }

    const CodegenResult cg = codegen(pr.program, /*source_file=*/"", /*remove_unused=*/!no_remove_vars);
    if (!cg.ok()) {
        for (const std::string& d : cg.diagnostics) {
            std::cerr << "purrc: " << d << "\n";
        }
        return 1;
    }

    // Emit the generated C++ next to the output (kept, so it can be inspected).
    const std::string gen_path = output + ".gen.cpp";
    {
        std::ofstream gen(gen_path);
        if (!gen) {
            std::cerr << "purrc: cannot write '" << gen_path << "'\n";
            return 1;
        }
        gen << cg.source;
    }

    // Optional validation of the GENERATED C++ — after codegen, before the backend compiles
    // it. Off unless --validate-cpp is given. The check itself is not implemented yet (the
    // hook is wired); when it is, a failure surfaces as a CppValidationException caught here.
    if (want_validate_cpp) {
        try {
            cheatah::purrc::validate_cpp(cg.source, gen_path);
        } catch (const cheatah::purrc::CppValidationException& e) {
            std::cerr << "purrc: generated C++ failed validation: " << e.what() << "\n";
            return 1;
        }
    }

    // The modules to compile/link: builtins (always available) + the imported ones + their
    // transitive deps, in link order.
    const std::vector<std::string> modules = all_modules(cg.modules);
    if (!all_modules_resolved(modules, input)) return 1;

    // Resolve every module before linking. Hand-written C++ stdlib modules link their baked
    // archive exactly as before; a purrc-emitted cheatah library module (a `.purr` someone
    // already ran through `--emit-library`) is VERIFIED first — its signed header, and the
    // archive when opaque, must be unchanged — so a tampered binary dependency fails the
    // build closed. The checksum is always enforced; $CHEATAH_TRUST adds Ed25519 signatures.
    const std::vector<std::string> trusted = load_trust();
    std::vector<ResolvedModule> resolved;
    resolved.reserve(modules.size());
    for (const std::string& m : modules) {
        ResolvedModule rm = resolve_module(m);
        if (rm.cheatah_lib) {
            std::string err = verify_artifact(rm.header, trusted);
            if (err.empty() && !rm.archive.empty()) err = verify_artifact(rm.archive, trusted);
            if (!err.empty()) {
                std::cerr << "purrc: refusing to import '" << m << "': " << err << "\n";
                return 1;
            }
            std::cerr << "purrc: verified cheatah module '" << m << "' (" << rm.header << ")\n";
        }
        resolved.push_back(std::move(rm));
    }

    // Build the C++ backend argv as a LIST and exec it directly — NO shell — so file
    // paths can never be interpreted as shell metacharacters (no command injection).
    // The compile+link flags (optimization, the native-arch flag, -shared/-fPIC, the
    // module extension, the vector-math link) are platform-computed in
    // cmake/Portability.cmake and baked in as '|'-joined strings, so this stays
    // platform-clean: Linux/macOS/Windows differ only in what CMake supplied.
    std::vector<std::string> args = {CHEATAH_CXX};
    for (const std::string& f : split_flags(CHEATAH_CXXFLAGS)) args.push_back(f);
    // Opt out of the runtime hardware-crypto power-on self-test (on by default). When set, a
    // capable CPU's SIMD crypto path is trusted from CPUID alone, with no known-answer check.
    if (no_crypto_selftest) args.push_back("-DCHEATAH_NO_CRYPTO_SELFTEST");
    for (const ResolvedModule& rm : resolved) {
        args.push_back(std::string("-I") + rm.include_dir);
    }
    // Build/biome-supplied passthrough flags (`--cxxflag` / CHEATAH_CXXFLAGS_EXTRA), before the source.
    for (const std::string& f : g_cxx_flags) args.push_back(f);
    args.push_back(gen_path);
    for (const ResolvedModule& rm : resolved) {
        if (!rm.archive.empty()) args.push_back(rm.archive);
    }
    // Build-supplied link inputs (`--link`): archives/flags for dependencies whose compiled
    // definitions the BUILD provides (e.g. an external project's library for a module it
    // imports by relative path / --import-root, with no co-located archive). After the
    // program object so their members resolve its undefined references.
    for (const std::string& a : g_link_inputs) args.push_back(a);
    // External libraries a module's header declares (`// cheatah-link:`) — the system
    // dependencies of a native library a module statically bundles. Honoured for ANY resolved
    // module (not just signed cheatah libs), so a relative/--import-root module's native deps
    // (e.g. monitor's -lvulkan) are linked too. After the archives so they resolve those
    // archives' undefined references (link order: users before deps).
    for (const ResolvedModule& rm : resolved) {
        if (!rm.resolved || rm.header.empty()) continue;
        for (const std::string& f : module_link_flags(rm.header)) args.push_back(f);
    }
    // After the module archives so their references resolve: the platform's vector libm
    // (glibc libmvec via -lm on Linux, the Accelerate framework on macOS) carries the
    // SIMD symbols ndarray's ufunc kernels call.
    for (const std::string& f : split_flags(CHEATAH_MATHLINK)) args.push_back(f);
    args.push_back("-o");
    args.push_back(output);

    const int rc = run_process(args);
    if (rc != 0) {
        std::cerr << "purrc: C++ backend failed (exit " << rc << ")\n";
        return 1;
    }
    std::cerr << "purrc: " << input << " -> " << output << "\n";

    // Integrity sidecars (after the module exists). The two tiers are independent and use
    // SEPARATE keys: code signing (--sign, the module bytes) and build-runtime signing
    // (--sign-runtime, the arch/glibc/libstdc++ manifest).
    if (!sign_key.empty()) {
        if (const int srv = sign_output(output, sign_key); srv != 0) return srv;
    } else if (want_checksum) {
        if (!write_checksum(output)) {
            std::cerr << "purrc: cannot write '" << output << ".sha512'\n";
            return 1;
        }
        std::cerr << "purrc: wrote " << output << ".sha512\n";
    }
    if (!sign_runtime_key.empty()) {
        if (const int rrv = sign_runtime(output, sign_runtime_key); rrv != 0) return rrv;
    } else if (want_runtime) {
        if (!write_runtime_manifest(output)) {
            std::cerr << "purrc: cannot write '" << output << ".rt'\n";
            return 1;
        }
        std::cerr << "purrc: wrote " << output << ".rt\n";
    }
    return 0;
}
