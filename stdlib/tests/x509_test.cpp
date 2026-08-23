// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// x509_test — offline unit tests for the from-scratch X.509 certificate parser + path validator
// (stdlib/tls/x509.hpp), which makes the TLS client MITM-proof. Fixtures are throwaway OpenSSL
// certs embedded as DER hex in x509_test_certs.hpp.
//
// Regenerate fixtures (from a scratch dir):
//   openssl genrsa -out rsa_ca.key 2048
//   openssl req -x509 -new -key rsa_ca.key -sha256 -days 3650 -subj "/CN=cheatah Test RSA CA" \
//     -addext "basicConstraints=critical,CA:TRUE" -out rsa_ca.pem
//   ... (RSA/EC/Ed25519 leaves for example.test, a *.wild.test leaf, a root->intermediate->leaf
//        chain, a non-CA "intermediate", and an RSA+SHA-512 unsupported-algorithm leaf;
//        SHA-384-era sets: a secp384r1 CA + leaf signed -sha384, a prime256v1 intermediate
//        signed -sha384 by that P-384 CA with a -sha256 P-256 leaf under it (the Sectigo shape),
//        an RSA CA + leaf signed -sha384, a prime256v1 CA whose leaf is signed -sha384, and a
//        multi-SAN leaf with subjectAltName=DNS:first.test,DNS:second.test,DNS:third.test) ...
//   then `openssl x509 -in X.pem -outform DER | xxd -p` for each.

#include <cstdlib>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "x509.hpp"
#include "x509_test_certs.hpp"

namespace x = cheatah::tls::x509;

namespace {

std::string unhex(const std::string& h) {
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    std::string out;
    out.reserve(h.size() / 2);
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        out.push_back(static_cast<char>((v(h[i]) << 4) | v(h[i + 1])));
    return out;
}

// A trust store containing the given CA certificates (DER hex).
x::TrustStore store_of(std::initializer_list<std::string> ca_hex) {
    x::TrustStore s;
    for (const std::string& h : ca_hex) {
        x::Cert c;
        EXPECT_TRUE(x::parse_cert(unhex(h), c));
        s.by_subject[c.subject].push_back(c);
    }
    return s;
}

std::string b64encode(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const unsigned n = (static_cast<unsigned char>(in[i]) << 16) |
                           (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(T[(n >> 6) & 63]);
        out.push_back(T[n & 63]);
    }
    if (i < in.size()) {
        unsigned n = static_cast<unsigned char>(in[i]) << 16;
        if (i + 1 < in.size()) n |= static_cast<unsigned char>(in[i + 1]) << 8;
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(i + 1 < in.size() ? T[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

constexpr long long kNow = 1798761600;      // 2027-01-01 — inside every fixture's validity window
constexpr long long kExpired = 1893456000;  // 2030-01-01 — after the leaves' notAfter (2028-10)
constexpr long long kEarly = 1577836800;    // 2020-01-01 — before the leaves' notBefore (2026-07)

}  // namespace

// ---- parsing ----------------------------------------------------------------------------------

TEST(CheatahX509, ParsesCertificateFields) {
    x::Cert leaf;
    ASSERT_TRUE(x::parse_cert(unhex(x509test::kRsaLeaf), leaf));
    EXPECT_FALSE(leaf.subject.empty());
    EXPECT_FALSE(leaf.tbs.empty());
    EXPECT_FALSE(leaf.sig.empty());
    ASSERT_EQ(leaf.san_dns.size(), 1u);
    EXPECT_EQ(leaf.san_dns[0], "example.test");
    EXPECT_FALSE(leaf.is_ca);
    EXPECT_LT(leaf.not_before, kNow);
    EXPECT_GT(leaf.not_after, kNow);

    x::Cert ca;
    ASSERT_TRUE(x::parse_cert(unhex(x509test::kRsaCa), ca));
    EXPECT_TRUE(ca.is_ca);  // basicConstraints CA:TRUE
}

TEST(CheatahX509, ParseRejectsMalformed) {
    x::Cert c;
    EXPECT_FALSE(x::parse_cert("", c));
    EXPECT_FALSE(x::parse_cert(std::string("\x30\x03\x02\x01\x00", 5), c));  // SEQUENCE but not a cert
    EXPECT_FALSE(x::parse_cert("not der at all", c));
    // A truncated real cert.
    const std::string der = unhex(x509test::kRsaLeaf);
    EXPECT_FALSE(x::parse_cert(der.substr(0, der.size() / 2), c));
}

// A certificate with a malformed validity time (a non-digit in notBefore or notAfter) is rejected.
TEST(CheatahX509, ParseRejectsBadValidityTime) {
    const std::string base = unhex(x509test::kRsaLeaf);
    const std::string utctime("\x17\x0d", 2);  // UTCTime tag + length 13 (YYMMDDHHMMSSZ)
    const std::size_t t1 = base.find(utctime);
    ASSERT_NE(t1, std::string::npos);
    x::Cert c;
    std::string bad_nb = base;
    bad_nb[t1 + 2] = 'Q';  // first year digit -> non-digit -> parse_time fails on notBefore
    EXPECT_FALSE(x::parse_cert(bad_nb, c));

    const std::size_t t2 = base.find(utctime, t1 + 2);  // the second UTCTime = notAfter
    ASSERT_NE(t2, std::string::npos);
    std::string bad_na = base;
    bad_na[t2 + 2] = 'Q';  // notBefore stays valid; notAfter fails to parse
    EXPECT_FALSE(x::parse_cert(bad_na, c));
}

// ---- signature algorithms (chain verification) ------------------------------------------------

TEST(CheatahX509, ValidatesRsaChain) {
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kRsaLeaf)}, "example.test",
                            store_of({x509test::kRsaCa}), kNow, err))
        << err;
}

TEST(CheatahX509, ValidatesEcdsaChain) {
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kEcLeaf)}, "example.test",
                            store_of({x509test::kEcCa}), kNow, err))
        << err;
}

TEST(CheatahX509, ValidatesEd25519Chain) {
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kEdLeaf)}, "example.test",
                            store_of({x509test::kEdCa}), kNow, err))
        << err;
}

TEST(CheatahX509, ValidatesP384Chain) {
    // A P-384 leaf signed ecdsa-with-SHA384 by a P-384 CA — the full-P-384 shape.
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kP384Leaf)}, "example.test",
                            store_of({x509test::kP384Ca}), kNow, err))
        << err;
}

TEST(CheatahX509, ValidatesP256IntermediateSignedByP384Root) {
    // The Sectigo/api.github.com shape: a P-256 leaf signed ecdsa-with-SHA256 by a P-256
    // intermediate whose own cert is signed ecdsa-with-SHA384 by a P-384 root. One chain
    // exercises both ECDSA dispatch arms (curve-by-issuer-SPKI, hash-by-signature-OID).
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kMixedLeaf), unhex(x509test::kMixedIntCa)},
                            "mixed.test", store_of({x509test::kP384Ca}), kNow, err))
        << err;
}

TEST(CheatahX509, ValidatesRsaSha384Chain) {
    // sha384WithRSAEncryption (Sectigo's RSA issuing chains, cdn.jsdelivr.net).
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kRsa384Leaf)}, "example.test",
                            store_of({x509test::kRsa384Ca}), kNow, err))
        << err;
}

TEST(CheatahX509, ValidatesP256KeySigningSha384) {
    // The other hash/curve pairing: ecdsa-with-SHA384 under a P-256 issuer key (a 48-byte
    // digest into the 32-byte-scalar curve — the FIPS 186-4 leftmost-bits truncation).
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kP256S384Leaf)}, "example.test",
                            store_of({x509test::kP256S384Ca}), kNow, err))
        << err;
}

TEST(CheatahX509, RejectsUnsupportedEcCurve) {
    // An ecdsa-with-SHA384 signature under a P-521 issuer key: the signature OID is
    // supported but the ISSUER curve is neither prime256v1 nor secp384r1 — the curve
    // dispatch must fail CLOSED rather than guess a curve.
    std::string err;
    EXPECT_FALSE(x::validate({unhex(x509test::kP521Leaf)}, "example.test",
                             store_of({x509test::kP521Ca}), kNow, err));
    EXPECT_NE(err.find("trusted CA"), std::string::npos) << err;
}

TEST(CheatahX509, RejectsTamperedP384Signature) {
    std::string leaf = unhex(x509test::kP384Leaf);
    leaf[leaf.size() - 1] ^= 0x01;
    std::string err;
    EXPECT_FALSE(x::validate({leaf}, "example.test", store_of({x509test::kP384Ca}), kNow, err));
}

TEST(CheatahX509, ValidatesMultiHopChain) {
    // leaf (deep.test) -> intermediate CA -> root CA (in the store). Exercises the chain walk and
    // the basicConstraints-CA check on the intermediate.
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kDeepLeaf), unhex(x509test::kIntCa)}, "deep.test",
                            store_of({x509test::kRsaCa}), kNow, err))
        << err;
}

// ---- hostname (RFC 6125) ----------------------------------------------------------------------

// Every dNSName in a multi-SAN certificate is parsed — the SAN walk once stopped after the
// first GeneralName, so a host matched by any later SAN (the norm on CDN-shared certs, e.g.
// Fastly's) was refused as "not valid for host".
TEST(CheatahX509, ParsesAllSubjectAltNames) {
    x::Cert leaf;
    ASSERT_TRUE(x::parse_cert(unhex(x509test::kMultiSanLeaf), leaf));
    ASSERT_EQ(leaf.san_dns.size(), 3u);
    EXPECT_EQ(leaf.san_dns[0], "first.test");
    EXPECT_EQ(leaf.san_dns[1], "second.test");
    EXPECT_EQ(leaf.san_dns[2], "third.test");
}

TEST(CheatahX509, MatchesLaterSan) {
    const auto store = store_of({x509test::kP256S384Ca});
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kMultiSanLeaf)}, "second.test", store, kNow, err))
        << err;
    EXPECT_TRUE(x::validate({unhex(x509test::kMultiSanLeaf)}, "third.test", store, kNow, err))
        << err;
    EXPECT_FALSE(x::validate({unhex(x509test::kMultiSanLeaf)}, "fourth.test", store, kNow, err));
}

TEST(CheatahX509, WildcardHostname) {
    const auto store = store_of({x509test::kRsaCa});
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kRsaWild)}, "foo.wild.test", store, kNow, err)) << err;
    EXPECT_TRUE(x::validate({unhex(x509test::kRsaWild)}, "FOO.WILD.TEST", store, kNow, err));  // ci
    EXPECT_FALSE(x::validate({unhex(x509test::kRsaWild)}, "wild.test", store, kNow, err));       // bare
    EXPECT_FALSE(x::validate({unhex(x509test::kRsaWild)}, "a.b.wild.test", store, kNow, err));   // 2 labels
    EXPECT_FALSE(x::validate({unhex(x509test::kRsaWild)}, "foo.other.test", store, kNow, err));  // wrong
}

TEST(CheatahX509, RejectsWrongHost) {
    std::string err;
    EXPECT_FALSE(x::validate({unhex(x509test::kRsaLeaf)}, "evil.test",
                             store_of({x509test::kRsaCa}), kNow, err));
    EXPECT_NE(err.find("host"), std::string::npos) << err;
}

// ---- validity period --------------------------------------------------------------------------

TEST(CheatahX509, RejectsExpiredAndNotYetValid) {
    const auto store = store_of({x509test::kRsaCa});
    std::string err;
    EXPECT_FALSE(x::validate({unhex(x509test::kRsaLeaf)}, "example.test", store, kExpired, err));
    EXPECT_NE(err.find("expired"), std::string::npos) << err;
    EXPECT_FALSE(x::validate({unhex(x509test::kRsaLeaf)}, "example.test", store, kEarly, err));
}

// ---- trust / chain rejection ------------------------------------------------------------------

TEST(CheatahX509, RejectsUntrustedSelfSignedOrUnknownCa) {
    std::string err;
    // Empty store: a valid-looking leaf that chains to no trusted CA is refused (this is the
    // self-signed / attacker-generated-cert case that "leaf key possession only" used to accept).
    EXPECT_FALSE(x::validate({unhex(x509test::kRsaLeaf)}, "example.test", x::TrustStore{}, kNow, err));
    EXPECT_NE(err.find("trusted CA"), std::string::npos) << err;
}

TEST(CheatahX509, RejectsBrokenChain) {
    std::string err;  // second cert is not the leaf's issuer
    EXPECT_FALSE(x::validate({unhex(x509test::kRsaLeaf), unhex(x509test::kEcCa)}, "example.test",
                             x::TrustStore{}, kNow, err));
    EXPECT_NE(err.find("issuer/subject"), std::string::npos) << err;
}

TEST(CheatahX509, RejectsNonCaIntermediate) {
    std::string err;  // notca_int has the right subject DN but basicConstraints CA:FALSE
    EXPECT_FALSE(x::validate({unhex(x509test::kDeepLeaf), unhex(x509test::kNotcaInt)}, "deep.test",
                             store_of({x509test::kRsaCa}), kNow, err));
    EXPECT_NE(err.find("not a CA"), std::string::npos) << err;
}

TEST(CheatahX509, RejectsTamperedSignature) {
    // Multi-hop: corrupt the leaf's signature so the intermediate's verify fails.
    std::string leaf = unhex(x509test::kDeepLeaf);
    leaf[leaf.size() - 1] ^= 0x01;
    std::string err;
    EXPECT_FALSE(x::validate({leaf, unhex(x509test::kIntCa)}, "deep.test",
                             store_of({x509test::kRsaCa}), kNow, err));
    EXPECT_NE(err.find("signature"), std::string::npos) << err;
}

TEST(CheatahX509, RejectsUnsupportedSignatureAlgorithm) {
    // An RSA+SHA-512 leaf: cheatah verifies only SHA-256 chain signatures, so this fails CLOSED
    // (refused) rather than being accepted unverified.
    std::string err;
    EXPECT_FALSE(x::validate({unhex(x509test::kSha512Leaf)}, "example.test",
                             store_of({x509test::kRsaCa}), kNow, err));
}

TEST(CheatahX509, RejectsEmptyChain) {
    std::string err;
    EXPECT_FALSE(x::validate({}, "example.test", store_of({x509test::kRsaCa}), kNow, err));
}

// ---- PEM / base64 / trust loading -------------------------------------------------------------

TEST(CheatahX509, LoadsPemBundle) {
    const std::string pem = "-----BEGIN CERTIFICATE-----\n" +
                            b64encode(unhex(x509test::kRsaCa)) + "\n-----END CERTIFICATE-----\n";
    x::TrustStore store;
    x::add_pem(pem, store);
    EXPECT_FALSE(store.empty());
    std::string err;
    EXPECT_TRUE(x::validate({unhex(x509test::kRsaLeaf)}, "example.test", store, kNow, err)) << err;

    // A PEM whose body is not valid base64 is skipped (no crash, no cert added).
    x::TrustStore bad;
    x::add_pem("-----BEGIN CERTIFICATE-----\n!!!not base64!!!\n-----END CERTIFICATE-----\n", bad);
    EXPECT_TRUE(bad.empty());
}

TEST(CheatahX509, LoadTrustFilesTriesPathsInOrder) {
    const std::string dir = ::testing::TempDir();
    const std::string good = dir + "/cheatah_x509_ca.pem";
    const std::string empty = dir + "/cheatah_x509_empty.pem";
    { std::ofstream(good) << "-----BEGIN CERTIFICATE-----\n"
                          << b64encode(unhex(x509test::kRsaCa)) << "\n-----END CERTIFICATE-----\n"; }
    { std::ofstream(empty) << "no certificates here\n"; }
    // nonexistent -> continue; empty -> read but no cert (no break); good -> loaded.
    const x::TrustStore store = x::load_trust_files({dir + "/does_not_exist.pem", empty, good});
    EXPECT_FALSE(store.empty());
    static_cast<void>(std::remove(good.c_str()));   // best-effort tmp cleanup
    static_cast<void>(std::remove(empty.c_str()));
    // No readable bundle at all -> empty store.
    EXPECT_TRUE(x::load_trust_files({dir + "/nope1.pem"}).empty());
    EXPECT_TRUE(x::load_trust("/nonexistent/cheatah/ca.pem").empty());
}

TEST(CheatahX509, DefaultCaPaths) {
    EXPECT_EQ(x::default_ca_paths("/my/ca.pem"), (std::vector<std::string>{"/my/ca.pem"}));
    ::setenv("SSL_CERT_FILE", "/env/ca.pem", 1);
    const auto with_env = x::default_ca_paths("");
    ASSERT_FALSE(with_env.empty());
    EXPECT_EQ(with_env.front(), "/env/ca.pem");
    EXPECT_EQ(with_env.size(), 4u);  // env + 3 system locations
    ::unsetenv("SSL_CERT_FILE");
    EXPECT_EQ(x::default_ca_paths("").size(), 3u);  // just the 3 system locations
}

// ---- time + hostname helpers (direct, for the GeneralizedTime + error branches) ---------------

TEST(CheatahX509, ParseTime) {
    long long t = 0;
    EXPECT_TRUE(x::parse_time("270101000000Z", 0x17, t));         // UTCTime -> 2027
    EXPECT_EQ(t, 1798761600);
    EXPECT_TRUE(x::parse_time("491231235959Z", 0x17, t));         // UTCTime <50 -> 2049
    EXPECT_TRUE(x::parse_time("990101000000Z", 0x17, t));         // UTCTime >=50 -> 1999
    EXPECT_TRUE(x::parse_time("20500101000000Z", 0x18, t));       // GeneralizedTime -> 2050
    EXPECT_FALSE(x::parse_time("2027", 0x18, t));                 // too short
    EXPECT_FALSE(x::parse_time("27010100000", 0x17, t));          // too short
    EXPECT_FALSE(x::parse_time("2701010000xx", 0x17, t));         // non-digit
    EXPECT_FALSE(x::parse_time("271301000000Z", 0x17, t));        // month 13
    EXPECT_FALSE(x::parse_time("270101000000X", 0x17, t));        // no 'Z'
    EXPECT_FALSE(x::parse_time("270101000000Z", 0x99, t));        // unknown time tag
}

TEST(CheatahX509, MatchDns) {
    EXPECT_TRUE(x::match_dns("a.example.com", "a.example.com"));
    EXPECT_TRUE(x::match_dns("*.example.com", "a.example.com"));
    EXPECT_FALSE(x::match_dns("*.example.com", "example.com"));
    EXPECT_FALSE(x::match_dns("*.example.com", "a.b.example.com"));
    EXPECT_FALSE(x::match_dns("*.example.com", "a.other.com"));
    EXPECT_FALSE(x::match_dns("", "a.example.com"));
    EXPECT_FALSE(x::match_dns("a.example.com", ""));
    EXPECT_FALSE(x::match_dns("*.", "a."));  // wildcard with nothing after the dot in the host
}
