// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// p256_bench — ECDSA P-256 sign/verify throughput. Verify is the TLS hot path
// (run once per handshake); sign is the JWT/ES256 path. Run with the release
// preset: ./build/release/bin/cheatah_benchmarks --benchmark_filter=P256

#include <benchmark/benchmark.h>

#include <string>

#include "hashlib.hpp"
#include "p256.hpp"

namespace {

std::string unhex(const std::string& h) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
    };
    std::string out;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        out.push_back(static_cast<char>((nib(h[i]) << 4) | nib(h[i + 1])));
    return out;
}

const std::string kPriv = unhex("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721");
const std::string kPub =
    unhex("60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6"
          "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299");

void BM_P256_Verify(benchmark::State& state) {
    const std::string hash = cheatah::hashlib::sha256_digest("sample");
    const std::string sig = cheatah::p256::sign_raw(kPriv, hash);
    for (auto _ : state) {
        benchmark::DoNotOptimize(cheatah::p256::verify_raw(kPub, hash, sig));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_P256_Verify);

void BM_P256_Sign(benchmark::State& state) {
    const std::string hash = cheatah::hashlib::sha256_digest("sample");
    for (auto _ : state) {
        benchmark::DoNotOptimize(cheatah::p256::sign_raw(kPriv, hash));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_P256_Sign);

}  // namespace
