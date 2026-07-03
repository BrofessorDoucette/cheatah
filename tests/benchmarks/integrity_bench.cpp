// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Micro-benchmarks for the per-load cost of each module-integrity tier
// (runtime/integrity.cpp). Module verification is paid exactly ONCE, when the runtime
// loads a module just before dlopen — never during the program's execution. So this
// measures verify_module() directly (no process spawn, no program run) to isolate the
// pure added work of each tier, at a couple of representative module sizes.
//
//   Off       no sidecars            — the baseline: the module isn't even read.
//   Checksum  + <mod>.sha512         — one SHA-512 pass over the module bytes.
//   Signed    + <mod>.sig (strict)   — SHA-512 checksum + one Ed25519 verification.
//   Full      + <mod>.rt/.rt.sig     — all of the above + parse/compare the runtime
//                                       manifest and verify its (separately-keyed) signature.
//
// Build with the `release` preset; run e.g.
//   ./build/release/bin/cheatah_benchmarks --benchmark_filter=Integrity
#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "build_fingerprint.hpp"  // build_runtime_manifest() — the host's own manifest
#include "ed25519.hpp"
#include "hashlib.hpp"
#include "integrity.hpp"

namespace {
namespace fs = std::filesystem;
namespace ig = cheatah::integrity;

std::string base_name(const std::string& p) {
    const std::size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}
void write_file(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << data;
}
std::string sig_sidecar(std::string_view secret, const std::string& message) {
    const std::string pub = cheatah::ed25519::public_key(secret);
    const std::string sig = cheatah::ed25519::sign(secret, message);
    return "cheatah-sig v1\npubkey " + pub + "\nsig " + sig + "\n";
}

// A fake module of `size` bytes on disk, plus the sidecars for tiers up to `tier`:
//   0 none · 1 +.sha512 · 2 +.sig · 3 +.rt/.rt.sig. The .rt is the HOST's own manifest, so
// the compatibility check passes and we time the success path, not an early rejection.
struct Fixture {
    std::string path, pubkey, rt_pubkey;
};
Fixture make_fixture(std::size_t size, int tier, const std::string& tag) {
    const fs::path dir = fs::temp_directory_path() / "cheatah_integrity_bench";
    fs::create_directories(dir);
    Fixture fx;
    fx.path = (dir / ("mod_" + tag + ".bin")).string();

    std::string bytes(size, '\0');  // deterministic pseudo-random fill (no real .so needed)
    std::uint32_t s = 0x9e3779b9u;
    for (std::size_t i = 0; i < size; ++i) {
        s = s * 1103515245u + 12345u;
        bytes[i] = static_cast<char>(s >> 16);
    }
    write_file(fx.path, bytes);

    if (tier >= 1) {
        const std::string hex = cheatah::hashlib::sha512(bytes);
        write_file(fx.path + ".sha512", hex + "  " + base_name(fx.path) + "\n");
    }
    if (tier >= 2) {
        const std::string secret = cheatah::ed25519::generate();
        fx.pubkey = cheatah::ed25519::public_key(secret);
        write_file(fx.path + ".sig", sig_sidecar(secret, bytes));
    }
    if (tier >= 3) {
        const std::string manifest = cheatah::build_runtime_manifest();
        write_file(fx.path + ".rt", manifest);
        const std::string rt_secret = cheatah::ed25519::generate();
        fx.rt_pubkey = cheatah::ed25519::public_key(rt_secret);
        write_file(fx.path + ".rt.sig", sig_sidecar(rt_secret, manifest));
    }
    return fx;
}

void BM_Integrity(benchmark::State& state, std::size_t size, int tier) {
    // Build the fixture once (outside the timed loop) and reuse it across iterations.
    static std::map<std::string, Fixture> cache;
    const std::string tag = std::to_string(size) + "_" + std::to_string(tier);
    auto it = cache.find(tag);
    if (it == cache.end()) it = cache.emplace(tag, make_fixture(size, tier, tag)).first;
    const Fixture& fx = it->second;

    const ig::Policy policy = (tier >= 2) ? ig::Policy::Strict : ig::Policy::Off;
    const std::vector<std::string> keys = (tier >= 2) ? std::vector<std::string>{fx.pubkey}
                                                      : std::vector<std::string>{};
    const std::vector<std::string> rt_keys =
        (tier >= 3) ? std::vector<std::string>{fx.rt_pubkey} : std::vector<std::string>{};

    for (auto _ : state) {
        ig::Result r = ig::verify_module(fx.path, policy, keys, rt_keys);
        if (!r.ok) {
            state.SkipWithError(("verify_module failed: " + r.error).c_str());
            break;
        }
        ig::release(r);  // close the fd verify_module opened, so we don't leak descriptors
        benchmark::DoNotOptimize(r.fd);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(size));
}
}  // namespace

// 64 KiB ≈ a small program module; 1 MiB ≈ a larger one with several imported modules.
BENCHMARK_CAPTURE(BM_Integrity, Off/64K, 64u * 1024, 0);
BENCHMARK_CAPTURE(BM_Integrity, Checksum/64K, 64u * 1024, 1);
BENCHMARK_CAPTURE(BM_Integrity, Signed/64K, 64u * 1024, 2);
BENCHMARK_CAPTURE(BM_Integrity, Full/64K, 64u * 1024, 3);
BENCHMARK_CAPTURE(BM_Integrity, Off/1M, 1024u * 1024, 0);
BENCHMARK_CAPTURE(BM_Integrity, Checksum/1M, 1024u * 1024, 1);
BENCHMARK_CAPTURE(BM_Integrity, Signed/1M, 1024u * 1024, 2);
BENCHMARK_CAPTURE(BM_Integrity, Full/1M, 1024u * 1024, 3);
