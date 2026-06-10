#include "integrity.hpp"

#include "build_fingerprint.hpp"
#include "ed25519.hpp"
#include "hashlib.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__GLIBC__)
#include <gnu/libc-version.h>  // gnu_get_libc_version (the LIVE host glibc)
#endif

namespace cheatah::integrity {

namespace {

// Read a small text sidecar by path (empty if absent/unreadable). Capped — sidecars are
// tiny (a digest, a signature, a short key list); never read an unbounded file here.
constexpr std::size_t kMaxSidecarBytes = 1024u * 1024u;  // 1 MiB
std::string read_text_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string out(kMaxSidecarBytes + 1, '\0');
    f.read(out.data(), static_cast<std::streamsize>(out.size()));
    const std::size_t got = static_cast<std::size_t>(f.gcount());
    if (got > kMaxSidecarBytes) return {};  // oversized — treat as unreadable
    out.resize(got);
    return out;
}

// Lowercase-hex view of raw bytes.
std::string hex_of(const std::string& raw) {
    static const char d[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char b : raw) {
        out.push_back(d[b >> 4]);
        out.push_back(d[b & 0xF]);
    }
    return out;
}

bool is_hex(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// The first whitespace-delimited token (sha256sum format is "<hex>  <name>"); also
// handles a bare "<hex>".
std::string first_token(const std::string& s) {
    std::string tok;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) break;
        tok.push_back(c);
    }
    return tok;
}

// Parse <module>.sig — a tiny line-based format:
//   cheatah-sig v1
//   pubkey <64 hex>
//   sig    <128 hex>
// Requires the version header and EXACTLY one pubkey + one sig (a duplicate is rejected,
// so a crafted multi-key sidecar can't slip a second value past tooling). Returns true
// and fills pubkey/sig (lowercased hex) on success.
bool parse_sig(const std::string& text, std::string& pubkey, std::string& sig) {
    std::istringstream in(text);
    std::string line;
    bool have_header = false, have_pub = false, have_sig = false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key, val;
        ls >> key >> val;
        if (key == "cheatah-sig") { if (val != "v1") return false; have_header = true; }
        else if (key == "pubkey") { if (have_pub) return false; pubkey = val; have_pub = true; }
        else if (key == "sig") { if (have_sig) return false; sig = val; have_sig = true; }
    }
    for (char& c : pubkey) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : sig) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return have_header && have_pub && have_sig && pubkey.size() == 64 && sig.size() == 128 &&
           is_hex(pubkey) && is_hex(sig);
}

// A module larger than this is refused before reading — an integrity check should never
// be tricked into an unbounded allocation. Real loadable modules are far smaller.
constexpr std::size_t kMaxModuleBytes = 1024u * 1024u * 1024u;  // 1 GiB

// Parse a `<module>.rt` manifest (cheatah-rt v1; arch/libc/libcxx lines). Returns true and
// fills the fields on success.
bool parse_rt(const std::string& text, std::string& arch, std::string& libc, std::string& libcxx) {
    std::istringstream in(text);
    std::string line;
    bool header = false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key, val;
        ls >> key >> val;
        if (key == "cheatah-rt") { if (val != "v1") return false; header = true; }
        else if (key == "arch") arch = val;
        else if (key == "libc") libc = val;
        else if (key == "libcxx") libcxx = val;
    }
    return header && !arch.empty() && !libc.empty() && !libcxx.empty();
}

// Compare dotted "major.minor" versions: <0 if a<b, 0 if equal, >0 if a>b.
int cmp_ver(const std::string& a, const std::string& b) {
    int amaj = 0, amin = 0, bmaj = 0, bmin = 0;
    std::sscanf(a.c_str(), "%d.%d", &amaj, &amin);
    std::sscanf(b.c_str(), "%d.%d", &bmaj, &bmin);
    if (amaj != bmaj) return amaj < bmaj ? -1 : 1;
    if (amin != bmin) return amin < bmin ? -1 : 1;
    return 0;
}

// Whether the build runtime recorded in @p rt_text is compatible with THIS host. Returns
// an empty string when compatible, otherwise a human-readable reason to refuse. The host
// glibc is the LIVE running version (gnu_get_libc_version), not a compile-time guess.
std::string runtime_incompatibility(const std::string& rt_text) {
    std::string arch, libc, libcxx;
    if (!parse_rt(rt_text, arch, libc, libcxx)) return "malformed build-runtime manifest (.rt)";
    if (arch != cheatah::build_arch())
        return "module was built for arch '" + arch + "', but this host is '" +
               std::string(cheatah::build_arch()) + "'";
    if (libc != "none") {
#if defined(__GLIBC__)
        const std::string host_libc = ::gnu_get_libc_version();
        if (cmp_ver(host_libc, libc) < 0)
            return "module needs glibc >= " + libc + ", but this host has glibc " + host_libc;
#endif
    }
    // libstdc++ is backward-compatible (newer runs older-built code), so the host's
    // release must be at least the module's.
    const std::string host_cxx = cheatah::build_libcxx();
    if (libcxx.rfind("libstdc++-", 0) == 0 && host_cxx.rfind("libstdc++-", 0) == 0) {
        const int mod_rel = std::atoi(libcxx.c_str() + 10);
        const int host_rel = std::atoi(host_cxx.c_str() + 10);
        if (mod_rel > host_rel)
            return "module needs libstdc++ release >= " + std::to_string(mod_rel) +
                   ", but this host has " + std::to_string(host_rel);
    }
    return "";
}

// Read the whole module: on POSIX keep the fd open and return a /proc/self/fd path so the
// caller dlopens the very bytes we hashed (no verify-then-load race). `fd_bound` records
// whether that binding succeeded; in strict mode we refuse if it did not, rather than
// silently load by path (which would reopen a possibly-swapped inode).
struct Opened {
    bool ok = false;
    bool fd_bound = false;  // load_path refers to the exact verified fd (not just the path)
    std::string bytes;
    std::string load_path;
    int fd = -1;
};
Opened open_and_read(const std::string& path) {
    Opened o;
#if !defined(_WIN32)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return o;
    // Re-check on the very fd we will hash + load (not a separately-opened path): a
    // regular, non-world-writable file. This closes the gap between the earlier path
    // sniff and the bytes actually verified.
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (st.st_mode & S_IWOTH) ||
        static_cast<std::size_t>(st.st_size) > kMaxModuleBytes) {
        ::close(fd);
        return o;
    }
    std::string buf;
    char tmp[65536];
    ssize_t n;
    while ((n = ::read(fd, tmp, sizeof tmp)) > 0) {
        buf.append(tmp, static_cast<std::size_t>(n));
        if (buf.size() > kMaxModuleBytes) { ::close(fd); return o; }
    }
    if (n < 0) { ::close(fd); return o; }
    o.ok = true;
    o.bytes = std::move(buf);
    o.fd = fd;
    // Prefer a path that names THIS open fd so the dlopen can't be raced onto another
    // inode: /proc/self/fd on Linux, /dev/fd on macOS/BSD. Fall back to the plain path
    // (marked unbound) only if neither exists (some sandboxes).
    o.load_path = "/proc/self/fd/" + std::to_string(fd);
    if (::access(o.load_path.c_str(), R_OK) != 0)
        o.load_path = "/dev/fd/" + std::to_string(fd);
    if (::access(o.load_path.c_str(), R_OK) == 0) {
        o.fd_bound = true;
    } else {
        o.load_path = path;
        o.fd_bound = false;
    }
    return o;
#else
    std::ifstream f(path, std::ios::binary);
    if (!f) return o;
    std::ostringstream ss;
    ss << f.rdbuf();
    o.ok = true;
    o.bytes = ss.str();
    if (o.bytes.size() > kMaxModuleBytes) { o.ok = false; return o; }
    o.load_path = path;
    o.fd_bound = false;  // no fd binding on Windows
    return o;
#endif
}

} // namespace

std::vector<std::string> load_trusted_keys(const std::string& path) {
    std::vector<std::string> keys;
    const std::string text = read_text_file(path);
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string tok = first_token(line);
        if (tok.empty() || tok[0] == '#') continue;
        for (char& c : tok) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tok.size() == 64 && is_hex(tok)) keys.push_back(tok);
    }
    return keys;
}

Result verify_module(const std::string& canonical_path, Policy policy,
                     const std::vector<std::string>& trusted_keys,
                     const std::vector<std::string>& trusted_runtime_keys) {
    Result r;
    auto exists = [](const std::string& p) { std::ifstream f(p, std::ios::binary); return f.good(); };
    const bool has_sum = exists(canonical_path + ".sha256");
    const bool has_sig = exists(canonical_path + ".sig");
    const bool has_rt = exists(canonical_path + ".rt");

    // Zero-overhead default: nothing to check and not in strict mode — don't even read
    // the module, just load it by path as the runtime always has.
    if (policy == Policy::Off && !has_sum && !has_sig && !has_rt) {
        r.ok = true;
        r.load_path = canonical_path;
        return r;
    }

    Opened o = open_and_read(canonical_path);
    if (!o.ok) {
        r.error = "cannot read module for verification (unreadable, too large, or not a "
                  "regular file): " + canonical_path;
        return r;
    }
    r.fd = o.fd;
    r.load_path = o.load_path;

    // Strict mode must load the EXACT bytes it verified. On POSIX, if the load couldn't be
    // bound to the verified fd (no /proc/self/fd or /dev/fd), refuse rather than load by
    // path (which a TOCTOU attacker could swap). Windows has no fd-based module load, so
    // there it loads by path (the bytes were still verified; the residual dlopen race is
    // documented in SECURITY.md). Off mode tolerates no binding on any platform.
#if !defined(_WIN32)
    if (policy == Policy::Strict && !o.fd_bound) {
        release(r);
        r.error = "cannot securely bind the module for loading on this platform "
                  "(no /proc/self/fd or /dev/fd); strict verification refused";
        return r;
    }
#endif

    // --- Basic tier: SHA-256 checksum (auto when the sidecar is present) ---
    const std::string sum_text = read_text_file(canonical_path + ".sha256");
    if (!sum_text.empty()) {
        std::string want = first_token(sum_text);
        for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string got = hashlib::sha256(o.bytes);
        if (want != got) {
            release(r);
            r.error = "checksum mismatch (corrupt or altered): " + canonical_path + ".sha256";
            return r;
        }
    }

    // --- Signed tier: Ed25519 signature (enforced in strict mode) ---
    const std::string sig_text = has_sig ? read_text_file(canonical_path + ".sig") : std::string();

    if (policy == Policy::Strict) {
        if (trusted_keys.empty()) {
            release(r);
            r.error = "strict verification requested but no trusted key is configured "
                      "(set CHEATAH_TRUST to a public-key file)";
            return r;
        }
        if (!has_sig) {
            release(r);
            r.error = "strict verification requested but no signature found: " +
                      canonical_path + ".sig";
            return r;
        }
    }

    if (has_sig) {
        std::string pubkey, sig;
        if (!parse_sig(sig_text, pubkey, sig)) {
            if (policy == Policy::Strict) {
                release(r);
                r.error = "malformed signature file: " + canonical_path + ".sig";
                return r;
            }
        } else {
            const bool trusted = std::find(trusted_keys.begin(), trusted_keys.end(), pubkey) !=
                                 trusted_keys.end();
            const bool valid = ed25519::verify(pubkey, o.bytes, sig);
            if (policy == Policy::Strict && (!trusted || !valid)) {
                release(r);
                r.error = !valid ? "signature does not verify (tampered): " + canonical_path
                                 : "module is signed by an untrusted key: " + pubkey;
                return r;
            }
        }
    }

    // --- C-runtime tier: the <module>.rt build-runtime manifest, signed by a SEPARATE
    // runtime key. The ABI compatibility check runs whenever the manifest is present (a
    // mismatch would otherwise crash in dlopen); the signature is required only in strict
    // mode when a runtime trust set is configured. ---
    if (policy == Policy::Strict && !trusted_runtime_keys.empty() && !has_rt) {
        release(r);
        r.error = "strict runtime verification requested but no build-runtime manifest "
                  "found: " + canonical_path + ".rt";
        return r;
    }
    if (has_rt) {
        const std::string rt_text = read_text_file(canonical_path + ".rt");
        const std::string incompat = runtime_incompatibility(rt_text);
        if (!incompat.empty()) {
            release(r);
            r.error = incompat;
            return r;
        }
        const bool has_rtsig = exists(canonical_path + ".rt.sig");
        if (policy == Policy::Strict && !trusted_runtime_keys.empty()) {
            if (!has_rtsig) {
                release(r);
                r.error = "strict runtime verification requested but no runtime signature: " +
                          canonical_path + ".rt.sig";
                return r;
            }
            std::string pubkey, sig;
            const std::string rtsig_text = read_text_file(canonical_path + ".rt.sig");
            if (!parse_sig(rtsig_text, pubkey, sig)) {
                release(r);
                r.error = "malformed runtime signature file: " + canonical_path + ".rt.sig";
                return r;
            }
            const bool trusted = std::find(trusted_runtime_keys.begin(), trusted_runtime_keys.end(),
                                           pubkey) != trusted_runtime_keys.end();
            const bool valid = ed25519::verify(pubkey, rt_text, sig);
            if (!trusted || !valid) {
                release(r);
                r.error = !valid
                    ? "runtime manifest signature does not verify (tampered): " + canonical_path + ".rt"
                    : "build-runtime manifest is signed by an untrusted runtime key: " + pubkey;
                return r;
            }
        }
    }

    r.ok = true;
    return r;
}

void release(Result& r) {
#if !defined(_WIN32)
    if (r.fd >= 0) ::close(r.fd);
#endif
    r.fd = -1;
}

} // namespace cheatah::integrity
