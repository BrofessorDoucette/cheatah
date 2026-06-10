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

void print_usage(std::ostream& os) {
    os << "usage: purrc <input.purr> [-o <output>]\n"
          "         [--checksum]               write <output>.sha512 (corruption check)\n"
          "         [--sign <keyfile>]         Ed25519-sign the module (code-signing key)\n"
          "         [--runtime]                write <output>.rt (build C-runtime manifest)\n"
          "         [--sign-runtime <keyfile>] sign <output>.rt (a SEPARATE runtime key)\n"
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
    if (output.empty()) {
        output = default_output(input);
    }

    std::ifstream in(input);
    if (!in) {
        std::cerr << "purrc: cannot open '" << input << "'\n";
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();

    const ParseResult pr = parse_source(source);
    if (!pr.ok()) {
        for (const Diagnostic& d : pr.diagnostics) {
            std::cerr << input << ":" << d.pos.line << ":" << d.pos.column << ": error: "
                      << d.message << "\n";
        }
        return 1;
    }

    const CodegenResult cg = codegen(pr.program);
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

    // The modules to compile/link: the imported ones, plus `builtins` which is
    // always available (no import) — so every program sees len(), hex(), etc.
    std::vector<std::string> modules = {"builtins"};
    modules.insert(modules.end(), cg.modules.begin(), cg.modules.end());

    // Transitive module dependencies (a module whose public API uses another). The
    // dependent must sort BEFORE its dependency (static archives resolve left-to-right):
    // "linalg" < "ndarray", "ed25519" < "hashlib" — both hold.
    static const std::map<std::string, std::vector<std::string>> kModuleDeps = {
        {"linalg", {"ndarray"}},     // the linalg routines operate on ndarray
        {"ed25519", {"hashlib"}},    // Ed25519 uses SHA-512 (hashlib) internally per RFC 8032
    };
    std::set<std::string> seen(modules.begin(), modules.end());
    for (std::size_t i = 0; i < modules.size(); ++i) {
        const auto it = kModuleDeps.find(modules[i]);
        if (it == kModuleDeps.end()) continue;
        for (const std::string& dep : it->second) {
            if (seen.insert(dep).second) modules.push_back(dep);
        }
    }

    // Build the C++ backend argv as a LIST and exec it directly — NO shell — so file
    // paths can never be interpreted as shell metacharacters (no command injection).
    // The compile+link flags (optimization, the native-arch flag, -shared/-fPIC, the
    // module extension, the vector-math link) are platform-computed in
    // cmake/Portability.cmake and baked in as '|'-joined strings, so this stays
    // platform-clean: Linux/macOS/Windows differ only in what CMake supplied.
    std::vector<std::string> args = {CHEATAH_CXX};
    for (const std::string& f : split_flags(CHEATAH_CXXFLAGS)) args.push_back(f);
    for (const std::string& m : modules) {
        args.push_back(std::string("-I") + CHEATAH_ROOT + "/" + m);
    }
    args.push_back(gen_path);
    for (const std::string& m : modules) {
        args.push_back(std::string(CHEATAH_LIB_DIR) + "/libcheatah_" + m + ".a");
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
