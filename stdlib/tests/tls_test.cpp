// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Unit tests for the `tls` module's key schedule against the RFC 8448 trace constants
// (the published TLS 1.3 test vectors, SHA-256 suite — suite-independent for the schedule).
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <algorithm>
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
// X25519 key_share is present, the legacy_session_id length (echoed by the server), and the
// signature_algorithms list (ext 13; empty = extension omitted, the lenient-parse case).
std::string make_client_hello(const std::vector<unsigned>& suites, bool tls13, bool x25519,
                              unsigned sid_len = 0, const std::vector<unsigned>& sig_algs = {}) {
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
    if (!sig_algs.empty()) {       // signature_algorithms: the u16-pair list
        std::string sa;
        be16(sa, static_cast<unsigned>(sig_algs.size() * 2));
        for (unsigned a : sig_algs) be16(sa, a);
        be16(ext, 13);
        be16(ext, static_cast<unsigned>(sa.size()));
        ext += sa;
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
    std::string pub, sid, sa;
    unsigned suite = 0;

    // Both suites offered -> ChaCha20-Poly1305 preferred; the X25519 share + session id come back.
    ASSERT_TRUE(d::parse_client_hello(make_client_hello({0x1303, 0x1301}, true, true, 4),
                                      pub, suite, sid, sa));
    EXPECT_EQ(suite, 0x1303u);
    EXPECT_EQ(pub, std::string(32, 'K'));
    EXPECT_EQ(sid, std::string(4, 'S'));
    EXPECT_EQ(sa, "");  // extension omitted -> lenient parse, empty list

    // Only AES-128-GCM offered -> negotiate it.
    ASSERT_TRUE(d::parse_client_hello(make_client_hello({0x1301}, true, true), pub, suite, sid, sa));
    EXPECT_EQ(suite, 0x1301u);
}

TEST(CheatahTls, ParseClientHelloSurfacesSignatureAlgorithms) {
    namespace d = cheatah::tls::detail;
    std::string pub, sid, sa;
    unsigned suite = 0;

    // A browser-shaped offer: ECDSA P-256 + Ed25519 + RSA-PSS. The raw u16 pairs come back in
    // order, so the server can check containment without re-parsing.
    ASSERT_TRUE(d::parse_client_hello(
        make_client_hello({0x1303}, true, true, 0, {0x0403, 0x0807, 0x0804}), pub, suite, sid, sa));
    ASSERT_EQ(sa.size(), std::size_t{6});
    const auto u16 = [&](std::size_t i) {
        return (static_cast<unsigned>(static_cast<unsigned char>(sa[i])) << 8) |
               static_cast<unsigned char>(sa[i + 1]);
    };
    EXPECT_EQ(u16(0), 0x0403u);
    EXPECT_EQ(u16(2), 0x0807u);
    EXPECT_EQ(u16(4), 0x0804u);

    // A stale list from a previous parse never leaks into a hello without the extension.
    ASSERT_TRUE(d::parse_client_hello(make_client_hello({0x1303}, true, true), pub, suite, sid, sa));
    EXPECT_EQ(sa, "");
}

TEST(CheatahTls, ParseClientHelloRejectsMalformed) {
    namespace d = cheatah::tls::detail;
    std::string pub, sid, sa;
    unsigned suite = 0;

    EXPECT_FALSE(d::parse_client_hello("", pub, suite, sid, sa));        // too short
    EXPECT_FALSE(d::parse_client_hello(std::string("\x02\x00\x00\x00", 4),
                                       pub, suite, sid, sa));            // not a client_hello
    EXPECT_FALSE(d::parse_client_hello(make_client_hello({0x9999}, true, true),
                                       pub, suite, sid, sa));            // no cipher suite in common
    EXPECT_FALSE(d::parse_client_hello(make_client_hello({0x1303}, false, true),
                                       pub, suite, sid, sa));            // no TLS 1.3 offered
    EXPECT_FALSE(d::parse_client_hello(make_client_hello({0x1303}, true, false),
                                       pub, suite, sid, sa));            // no X25519 key share

    // Truncations inside each length-prefixed field must be refused, never over-read. Cutting a
    // well-formed hello at increasing offsets walks the bounds checks (session id, cipher-suite
    // list, compression, extensions, key-share body) in turn.
    const std::string full = make_client_hello({0x1303, 0x1301}, true, true, 4, {0x0403});
    for (std::size_t cut = 4; cut < full.size(); ++cut) {
        EXPECT_FALSE(d::parse_client_hello(full.substr(0, cut), pub, suite, sid, sa))
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

// The multi-block reader behind full-chain Certificate emission: every block in order, and any
// malformed block poisons the whole read — a chain with a hole is worse than no chain.
TEST(CheatahTls, PemBlocksExtractsChains) {
    namespace d = cheatah::tls::detail;
    const std::string one = "-----BEGIN CERTIFICATE-----\naGk=\n-----END CERTIFICATE-----\n";
    const std::string two = one + "-----BEGIN CERTIFICATE-----\neW8=\n-----END CERTIFICATE-----\n";
    const auto chain = d::pem_blocks(two, "CERTIFICATE");
    ASSERT_EQ(chain.size(), std::size_t{2});
    EXPECT_EQ(chain[0], "hi");
    EXPECT_EQ(chain[1], "yo");  // "yo" base64 is "eW8="
    ASSERT_EQ(d::pem_blocks(one, "CERTIFICATE").size(), std::size_t{1});
    EXPECT_TRUE(d::pem_blocks(two, "PRIVATE KEY").empty());  // label absent -> empty, not error

    const std::string bad =
        one + "-----BEGIN CERTIFICATE-----\n!!!!\n-----END CERTIFICATE-----\n";
    EXPECT_TRUE(d::pem_blocks(bad, "CERTIFICATE").empty());  // one bad block poisons the read
    EXPECT_TRUE(d::pem_blocks("-----BEGIN CERTIFICATE-----\naGk=\n", "CERTIFICATE").empty());
}

// The P-256 private-scalar parse: PKCS#8 and SEC1 shapes both yield the scalar; a key on another
// curve (no prime256v1 OID) is refused rather than misread.
TEST(CheatahTls, EcP256ScalarFromPem) {
    namespace d = cheatah::tls::detail;
    const std::string scalar(32, '\x11');
    // The pieces the parser anchors on, in PKCS#8 order: the prime256v1 OID TLV, then the
    // SEC1 ECPrivateKey's version + scalar (02 01 01 04 20 <d32>).
    const std::string oid = {0x06, 0x08, 0x2a, static_cast<char>(0x86), 0x48,
                             static_cast<char>(0xce), 0x3d, 0x03, 0x01, 0x07};
    const std::string ver_and_scalar = std::string({0x02, 0x01, 0x01, 0x04, 0x20}) + scalar;
    const auto pem_of = [](const std::string& der, const std::string& label) {
        return "-----BEGIN " + label + "-----\n" + cheatah::hashlib::base64_encode(der) +
               "\n-----END " + label + "-----\n";
    };

    EXPECT_EQ(d::ec_p256_scalar_from_pem(pem_of(oid + ver_and_scalar, "PRIVATE KEY")), scalar);
    EXPECT_EQ(d::ec_p256_scalar_from_pem(pem_of(ver_and_scalar + oid, "EC PRIVATE KEY")), scalar);

    // P-384's OID (2B 81 04 00 22) instead of prime256v1: refuse, never misread the scalar.
    const std::string p384_oid = {0x06, 0x05, 0x2b, static_cast<char>(0x81), 0x04, 0x00, 0x22};
    EXPECT_EQ(d::ec_p256_scalar_from_pem(pem_of(p384_oid + ver_and_scalar, "PRIVATE KEY")), "");
    EXPECT_EQ(d::ec_p256_scalar_from_pem("not a key"), "");
    EXPECT_EQ(d::ec_p256_scalar_from_pem(pem_of(oid, "PRIVATE KEY")), "");  // OID but no scalar
}

// The ClientHello's cipher ORDER is a wire-format decision that depends on the host CPU: with AES-NI
// we lead with AES-GCM, without it ChaCha20 leads. Read inline, whichever branch does not match the
// build machine was dead code no test could reach — so the ordering went unpinned on every machine
// except the one nobody was testing on. Taking the decision as a parameter makes both orders
// checkable anywhere.
TEST(CheatahTls, CipherPreferenceFollowsHardware) {
    const auto suites = [](bool hw) {
        std::string body;
        cheatah::tls::detail::append_cipher_preference(body, hw);
        EXPECT_EQ(body.size(), 6u) << "exactly three suites, two bytes each";
        std::vector<unsigned> out;
        for (std::size_t i = 0; i + 1 < body.size(); i += 2) {
            out.push_back((static_cast<unsigned char>(body[i]) << 8) |
                          static_cast<unsigned char>(body[i + 1]));
        }
        return out;
    };

    // With hardware AES, AES-GCM runs multi-GB/s and must be offered first.
    EXPECT_EQ(suites(true), (std::vector<unsigned>{0x1302, 0x1301, 0x1303}));
    // Without it, scalar ChaCha20 is our faster path, so it leads.
    EXPECT_EQ(suites(false), (std::vector<unsigned>{0x1303, 0x1301, 0x1302}));

    // Both orders offer the SAME three suites — the preference changes, never the capability, so a
    // server that honours client order can always still pick something we can actually speak.
    auto a = suites(true), b = suites(false);
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    EXPECT_EQ(a, b);
}
