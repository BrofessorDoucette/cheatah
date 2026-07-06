// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Unit tests for the `tls` module's key schedule against the RFC 8448 trace constants
// (the published TLS 1.3 test vectors, SHA-256 suite — suite-independent for the schedule).
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

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

void be16(std::string& o, unsigned v) {
    o.push_back(static_cast<char>((v >> 8) & 0xFF));
    o.push_back(static_cast<char>(v & 0xFF));
}

// A ClientHello handshake message with control over the fields parse_client_hello gates on:
// the offered cipher suites, whether supported_versions advertises TLS 1.3, whether a valid
// X25519 key_share is present, and the legacy_session_id length (echoed by the server).
std::string make_client_hello(const std::vector<unsigned>& suites, bool tls13, bool x25519,
                              unsigned sid_len = 0) {
    std::string body;
    be16(body, 0x0303);            // legacy_version
    body.append(32, 'R');          // random
    body.push_back(static_cast<char>(sid_len));
    body.append(sid_len, 'S');     // legacy_session_id
    be16(body, static_cast<unsigned>(suites.size() * 2));
    for (unsigned s : suites) be16(body, s);
    body.push_back(1);             // legacy_compression_methods length
    body.push_back(0);             // null compression
    std::string ext;
    if (tls13) {                   // supported_versions: [TLS 1.3]
        be16(ext, 43);
        be16(ext, 3);
        ext.push_back(2);
        be16(ext, 0x0304);
    }
    if (x25519) {                  // key_share: one X25519 entry
        std::string entry;
        be16(entry, 0x001d);
        be16(entry, 32);
        entry.append(32, 'K');
        std::string ks;
        be16(ks, static_cast<unsigned>(entry.size()));
        ks += entry;
        be16(ext, 51);
        be16(ext, static_cast<unsigned>(ks.size()));
        ext += ks;
    }
    be16(body, static_cast<unsigned>(ext.size()));
    body += ext;
    std::string msg;
    msg.push_back(1);              // client_hello
    msg.push_back(static_cast<char>((body.size() >> 16) & 0xFF));
    msg.push_back(static_cast<char>((body.size() >> 8) & 0xFF));
    msg.push_back(static_cast<char>(body.size() & 0xFF));
    msg += body;
    return msg;
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

// The server's ClientHello parser: accept a well-formed hello (preferring ChaCha20), fall back to
// AES-128-GCM when only that is offered, and reject every malformed / unsupported shape. Driving the
// parser directly (a test seam) covers each refusal branch without a live network peer.
TEST(CheatahTls, ParseClientHelloAcceptsAndNegotiates) {
    namespace d = cheatah::tls::detail;
    std::string pub, sid;
    unsigned suite = 0;

    // Both suites offered -> ChaCha20-Poly1305 preferred; the X25519 share + session id come back.
    ASSERT_TRUE(d::parse_client_hello(make_client_hello({0x1303, 0x1301}, true, true, 4),
                                      pub, suite, sid));
    EXPECT_EQ(suite, 0x1303u);
    EXPECT_EQ(pub, std::string(32, 'K'));
    EXPECT_EQ(sid, std::string(4, 'S'));

    // Only AES-128-GCM offered -> negotiate it.
    ASSERT_TRUE(d::parse_client_hello(make_client_hello({0x1301}, true, true), pub, suite, sid));
    EXPECT_EQ(suite, 0x1301u);
}

TEST(CheatahTls, ParseClientHelloRejectsMalformed) {
    namespace d = cheatah::tls::detail;
    std::string pub, sid;
    unsigned suite = 0;

    EXPECT_FALSE(d::parse_client_hello("", pub, suite, sid));            // too short
    EXPECT_FALSE(d::parse_client_hello(std::string("\x02\x00\x00\x00", 4),
                                       pub, suite, sid));                // not a client_hello
    EXPECT_FALSE(d::parse_client_hello(make_client_hello({0x9999}, true, true),
                                       pub, suite, sid));                // no cipher suite in common
    EXPECT_FALSE(d::parse_client_hello(make_client_hello({0x1303}, false, true),
                                       pub, suite, sid));                // no TLS 1.3 offered
    EXPECT_FALSE(d::parse_client_hello(make_client_hello({0x1303}, true, false),
                                       pub, suite, sid));                // no X25519 key share

    // Truncations inside each length-prefixed field must be refused, never over-read. Cutting a
    // well-formed hello at increasing offsets walks the bounds checks (session id, cipher-suite
    // list, compression, extensions, key-share body) in turn.
    const std::string full = make_client_hello({0x1303, 0x1301}, true, true, 4);
    for (std::size_t cut = 4; cut < full.size(); ++cut) {
        EXPECT_FALSE(d::parse_client_hello(full.substr(0, cut), pub, suite, sid))
            << "truncation at " << cut << " must be rejected";
    }
}

// PEM block extraction (strict base64) and the Ed25519 PKCS#8 seed parse, incl. the reject paths.
TEST(CheatahTls, PemBlockExtractsAndRejects) {
    namespace d = cheatah::tls::detail;
    // "hi" base64 is "aGk=" -> a well-formed CERTIFICATE block round-trips to its bytes.
    const std::string pem = "-----BEGIN CERTIFICATE-----\naGk=\n-----END CERTIFICATE-----\n";
    EXPECT_EQ(d::pem_block(pem, "CERTIFICATE"), "hi");
    EXPECT_EQ(d::pem_block(pem, "PRIVATE KEY"), "");        // label absent
    EXPECT_EQ(d::pem_block("-----BEGIN CERTIFICATE-----\naGk=\n", "CERTIFICATE"), "");  // no END

    // A real PKCS#8 Ed25519 key DER: 302e020100300506032b657004220420 || 32-byte seed.
    std::string der = {static_cast<char>(0x30), 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06,
                       0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20};
    der.append(32, static_cast<char>(0xAB));
    EXPECT_EQ(d::ed25519_seed_from_pkcs8(der), std::string(32, static_cast<char>(0xAB)));
    EXPECT_EQ(d::ed25519_seed_from_pkcs8("not a key"), "");   // pattern absent
}
