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
    std::string archive;      // libcheatah_<m>.a to link, or "" if header-only
    bool cheatah_lib = false; // true: a purrc-emitted, signed cheatah module
    std::string header;       // the verified header path (cheatah modules only)
};

// Classify + locate an imported module. A cheatah library module is recognised by a signed
// header (`<root>/<m>/<m>.hpp.sha512`) on the module search path; anything else is a
// hand-written C++ stdlib module resolved from the baked toolchain root, exactly as before.
ResolvedModule resolve_module(const std::string& m) {
    for (const std::string& root : module_search_paths()) {
        const std::string dir = root + "/" + m;
        const std::string hdr = dir + "/" + m + ".hpp";
        if (file_exists(hdr + ".sha512")) {
            ResolvedModule r;
            r.cheatah_lib = true;
            r.include_dir = dir;
            r.header = hdr;
            const std::string ar = dir + "/libcheatah_" + m + ".a";
            if (file_exists(ar)) r.archive = ar;  // opaque module: link its hidden defs
            return r;
        }
    }
    ResolvedModule r;
    r.include_dir = std::string(CHEATAH_ROOT) + "/" + m;
    r.archive = std::string(CHEATAH_LIB_DIR) + "/libcheatah_" + m + ".a";
    return r;
}

// Emit @p input as a cheatah library module: write the importable header, and (opaque
// builds) compile the hidden definitions into libcheatah_<m>.a. Both artifacts get a
// SHA-512 checksum sidecar (so a consumer can confirm they have not changed) and, with
// --sign, an Ed25519 signature. Returns a process exit code.
int emit_library(const std::string& input, const std::string& source, std::string output,
                 bool transparent, const std::string& sign_key, bool remove_unused) {
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
    opts.transparent = transparent;
    opts.remove_unused = remove_unused;
    const CodegenResult cg = codegen_library(pr.program, opts);
    if (!cg.ok()) {
        for (const std::string& d : cg.diagnostics) std::cerr << "purrc: " << d << "\n";
        return 1;
    }

    if (!write_file(output, cg.header_source)) {
        std::cerr << "purrc: cannot write '" << output << "'\n";
        return 1;
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
    };
    std::set<std::string> seen(modules.begin(), modules.end());
    for (std::size_t i = 0; i < modules.size(); ++i) {
        const auto it = kDeps.find(modules[i]);
        if (it == kDeps.end()) continue;
        for (const std::string& dep : it->second)
            if (seen.insert(dep).second) modules.push_back(dep);
    }
    return modules;
}

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
    std::vector<std::string> args = {CHEATAH_CXX};
    for (const std::string& f : split_flags(CHEATAH_CXXFLAGS))
        if (f != "-shared") args.push_back(f);  // type-check only — don't try to link a module
    args.push_back("-fsyntax-only");
    for (const std::string& m : all_modules(cg.modules))
        args.push_back(std::string("-I") + resolve_module(m).include_dir);
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
          "         [--no-remove-variables]    keep unused locals (skip dead-variable removal)\n"
          "         [--no-optimize-cpp]        disable ALL generated-C++ optimizations\n"
          "       purrc --emit-library <input.purr> [-o <m>.hpp] [--transparent] [--sign <key>]\n"
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
    std::string input;
    std::string output;
    std::string sign_key;          // --sign <keyfile>: Ed25519-sign the built module (code key)
    std::string sign_runtime_key;  // --sign-runtime <keyfile>: sign the build-runtime manifest
    std::string keygen_prefix;     // --keygen <prefix>: just generate a keypair and exit
    bool want_checksum = false;    // --checksum: emit the <output>.sha512 sidecar
    bool want_runtime = false;     // --runtime: emit the <output>.rt build-runtime manifest
    bool emit_lib = false;         // --emit-library: emit an importable cheatah library module
    bool transparent = false;      // --transparent: inline the C++ source into the header (stdlib)
    bool want_check = false;       // --check: syntax-only, #line-mapped diagnostics (editor)
    bool want_validate_cpp = false;// --validate-cpp: validate the generated C++ before compiling
    bool no_remove_vars = false;   // --no-remove-variables: keep unused locals (opt out of DCE)
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
        } else if (a == "--check") {
            want_check = true;
        } else if (a == "--validate-cpp") {
            want_validate_cpp = true;
        } else if (a == "--no-remove-variables") {
            no_remove_vars = true;
        } else if (a == "--no-optimize-cpp") {
            // Umbrella that disables ALL generated-C++ optimizations. Currently that is just
            // dead-variable removal; future optimization opt-outs are added here too.
            no_remove_vars = true;
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

    // Syntax-only check (the editor's error provider): no module is produced.
    if (want_check) return run_check(input, source, /*remove_unused=*/!no_remove_vars);

    // Library mode emits an importable cheatah module (signed header [+ opaque archive]),
    // not a runnable program — it computes its own default output, so dispatch before the
    // program `.so` defaulting below.
    if (emit_lib)
        return emit_library(input, source, output, transparent, sign_key, !no_remove_vars);

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
    for (const ResolvedModule& rm : resolved) {
        args.push_back(std::string("-I") + rm.include_dir);
    }
    args.push_back(gen_path);
    for (const ResolvedModule& rm : resolved) {
        if (!rm.archive.empty()) args.push_back(rm.archive);
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
