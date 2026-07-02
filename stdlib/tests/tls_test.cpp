// Unit tests for the `tls` module's key schedule against the RFC 8448 trace constants
// (the published TLS 1.3 test vectors, SHA-256 suite — suite-independent for the schedule).
#include <gtest/gtest.h>

#include <string>

#include "hashlib.hpp"
#include "tls.hpp"

namespace {
std::string hex_of(std::string_view raw) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (const char ch : raw) {
        out.push_back(kHex[static_cast<unsigned char>(ch) >> 4]);
        out.push_back(kHex[static_cast<unsigned char>(ch) & 0xF]);
    }
    return out;
}
}  // namespace

// RFC 8448 §3: the early secret HKDF-Extract(0, 0^32) and the "derived" secret from it.
TEST(CheatahTls, KeySchedule) {
    const std::string zeros(32, '\0');
    const std::string early = cheatah::hashlib::hkdf_extract(std::string(), zeros);
    EXPECT_EQ(hex_of(early), "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a");
    const std::string derived = cheatah::tls::detail::derive_secret(early, "derived", "");
    EXPECT_EQ(hex_of(derived), "6f2615a108c702c5678f54fc9dbab69716c076189c48250cebeac3576c3611ba");
}

// HKDF-Expand-Label structure: deterministic, length-exact, label-sensitive.
TEST(CheatahTls, ExpandLabel) {
    const std::string secret(32, '\x42');
    const std::string a = cheatah::tls::detail::expand_label(secret, "key", "", 32);
    const std::string b = cheatah::tls::detail::expand_label(secret, "iv", "", 12);
    EXPECT_EQ(a.size(), std::size_t{32});
    EXPECT_EQ(b.size(), std::size_t{12});
    EXPECT_NE(a.substr(0, 12), b);  // different labels -> unrelated output
    EXPECT_EQ(a, cheatah::tls::detail::expand_label(secret, "key", "", 32));  // deterministic
}
