// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Byte-for-byte cross-check of the hashlib digests / HMAC / base64 against the system
// `openssl` CLI over a range of inputs — empty, short, long, a sentence, and ALL 256 byte
// values — so we validate against an independent reference implementation, not only the
// fixed standard vectors in hashlib_test.cpp. The whole suite SKIPS when `openssl` is
// unavailable, so it strengthens assurance where present without becoming a build/runtime
// dependency. Input bytes go through a temp FILE (never the shell), so arbitrary bytes are
// compared exactly; only ASCII, quote-free HMAC keys appear on the command line.
#include <gtest/gtest.h>

#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "hashlib.hpp"

namespace hl = cheatah::hashlib;

namespace {

bool has_openssl() { return std::system("openssl version >/dev/null 2>&1") == 0; }

// Run @p cmd, capture its stdout.
std::string capture(const std::string& cmd) {
    std::array<char, 4096> buf{};
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), p)) out += buf.data();
    pclose(p);
    return out;
}

// The first whitespace-delimited token — openssl `-r` prints "<hex> *<file>".
std::string first_token(const std::string& s) {
    std::string t;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
        t += c;
    }
    return t;
}

// Strip trailing newlines (openssl base64 ends with one).
std::string chomp(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

// Lowercase hex of a raw byte string — hmac_sha256/512 return RAW bytes (by design),
// while openssl `-r` prints hex, so encode hl's output before comparing.
std::string to_hex(const std::string& raw) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        out.push_back(kHex[c >> 4]);
        out.push_back(kHex[c & 0xF]);
    }
    return out;
}

// Write @p data to a fresh temp file; returns its path (removed by the caller).
std::string write_tmp(const std::string& data) {
    char path[] = "/tmp/cheatah_ossl_XXXXXX";
    const int fd = mkstemp(path);
    if (fd >= 0) {
        ssize_t off = 0;
        while (off < static_cast<ssize_t>(data.size())) {
            const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
            if (n <= 0) break;
            off += n;
        }
        ::close(fd);
    }
    return std::string(path);
}

// The shared input corpus: empty, short, long-repeat, a sentence, and every byte 0x00..0xFF.
const std::vector<std::string>& inputs() {
    static const std::vector<std::string> v = [] {
        std::string all;
        for (int i = 0; i < 256; ++i) all.push_back(static_cast<char>(i));
        return std::vector<std::string>{"", "abc", std::string(1000, 'a'),
                                        "The quick brown fox jumps over the lazy dog", all};
    }();
    return v;
}

}  // namespace

TEST(HashlibVsOpenssl, Sha256) {
    if (!has_openssl()) GTEST_SKIP() << "openssl CLI not available";
    for (const auto& in : inputs()) {
        const std::string f = write_tmp(in);
        const std::string ref = first_token(capture("openssl dgst -sha256 -r '" + f + "'"));
        std::remove(f.c_str());
        ASSERT_EQ(ref.size(), 64u) << "openssl output unexpected for input of size " << in.size();
        EXPECT_EQ(hl::sha256(in), ref) << "sha256 mismatch for input of size " << in.size();
    }
}

TEST(HashlibVsOpenssl, Sha512) {
    if (!has_openssl()) GTEST_SKIP() << "openssl CLI not available";
    for (const auto& in : inputs()) {
        const std::string f = write_tmp(in);
        const std::string ref = first_token(capture("openssl dgst -sha512 -r '" + f + "'"));
        std::remove(f.c_str());
        ASSERT_EQ(ref.size(), 128u);
        EXPECT_EQ(hl::sha512(in), ref) << "sha512 mismatch for input of size " << in.size();
    }
}

TEST(HashlibVsOpenssl, HmacSha256) {
    if (!has_openssl()) GTEST_SKIP() << "openssl CLI not available";
    for (const std::string key : {std::string("k"), std::string("secretkey"),
                                  std::string(40, 'K')}) {
        for (const auto& in : inputs()) {
            const std::string f = write_tmp(in);
            const std::string ref = first_token(
                capture("openssl dgst -sha256 -hmac '" + key + "' -r '" + f + "'"));
            std::remove(f.c_str());
            ASSERT_EQ(ref.size(), 64u);
            EXPECT_EQ(to_hex(hl::hmac_sha256(key, in)), ref)
                << "hmac-sha256 mismatch (key '" << key << "', input size " << in.size() << ")";
        }
    }
}

TEST(HashlibVsOpenssl, HmacSha512) {
    if (!has_openssl()) GTEST_SKIP() << "openssl CLI not available";
    for (const std::string key : {std::string("k"), std::string("secretkey")}) {
        for (const auto& in : inputs()) {
            const std::string f = write_tmp(in);
            const std::string ref = first_token(
                capture("openssl dgst -sha512 -hmac '" + key + "' -r '" + f + "'"));
            std::remove(f.c_str());
            ASSERT_EQ(ref.size(), 128u);
            EXPECT_EQ(to_hex(hl::hmac_sha512(key, in)), ref)
                << "hmac-sha512 mismatch (key '" << key << "', input size " << in.size() << ")";
        }
    }
}

TEST(HashlibVsOpenssl, Base64Encode) {
    if (!has_openssl()) GTEST_SKIP() << "openssl CLI not available";
    for (const auto& in : inputs()) {
        const std::string f = write_tmp(in);
        const std::string ref = chomp(capture("openssl base64 -A -in '" + f + "'"));
        std::remove(f.c_str());
        EXPECT_EQ(hl::base64_encode(in), ref) << "base64 mismatch for input of size " << in.size();
    }
}