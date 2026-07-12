// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// x509.hpp — minimal X.509 v3 certificate parsing + path validation for the cheatah TLS 1.3
// client, so `https://` is authenticated (MITM-proof) and not merely encrypted. No OpenSSL.
//
// It parses the DER certificate chain a server presents, then verifies:
//   1. HOSTNAME — the leaf's subjectAltName dNSNames match the requested host (RFC 6125 wildcards).
//   2. VALIDITY — every cert's notBefore <= now <= notAfter.
//   3. CHAIN — each cert is signed by the next, up to a certificate in a trusted CA store (the
//      system trust bundle, or a caller-supplied one), and intermediates carry basicConstraints CA.
//
// Signature algorithms supported for chain/leaf signatures: sha256WithRSAEncryption and
// sha384WithRSAEncryption (PKCS#1 v1.5), ecdsa-with-SHA256 and ecdsa-with-SHA384 (the issuer key on
// P-256 or P-384 — the hash comes from the signature OID, the curve from the issuer's SPKI, in any
// pairing), and Ed25519. Anything else (e.g. SHA-512, rsassa-PSS chain signatures, other curves)
// FAILS CLOSED — the connection is refused, never accepted unverified. Everything runs on cheatah's
// own crypto (rsa_verify, p256, p384, ed25519, hashlib) and the from-scratch DER walker below.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "ed25519.hpp"
#include "hashlib.hpp"
#include "p256.hpp"
#include "p384.hpp"
#include "rsa_verify.hpp"

namespace cheatah::tls::x509 {

// ---- small helpers ---------------------------------------------------------------------------

// Byte<->hex and base64 are the ONE canonical implementation in hashlib. X.509 PEM parsing needs a
// FAIL-CLOSED base64 (a malformed body must be rejected, not decoded to garbage), so it passes
// hashlib::base64_decode's `strict` flag; see the call site below.
using cheatah::hashlib::to_hex;

inline char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
inline std::string lower(const std::string& s) {
    std::string r(s);
    for (char& c : r) c = lower_ascii(c);
    return r;
}

// Days since the Unix epoch for a civil (proleptic Gregorian) date — Howard Hinnant's algorithm,
// so no libc timegm/timezone dependency (deterministic, testable).
inline long long days_from_civil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

// ---- DER walker ------------------------------------------------------------------------------

// Read one DER TLV at d[pos] bounded by `end`. On success sets tag + content range [cbeg,cend),
// advances pos PAST the value, and returns true. Rejects out-of-bounds / >4-byte lengths.
inline bool tlv(const std::string& d, std::size_t& pos, std::size_t end, unsigned char& tag,
                std::size_t& cbeg, std::size_t& cend) {
    if (pos >= end) return false;
    tag = static_cast<unsigned char>(d[pos++]);
    if (pos >= end) return false;
    const unsigned char lb = static_cast<unsigned char>(d[pos++]);
    std::size_t len;
    if (!(lb & 0x80)) {
        len = lb;
    } else {
        const int nb = lb & 0x7F;
        if (nb == 0 || nb > 4 || pos + static_cast<std::size_t>(nb) > end) return false;
        len = 0;
        for (int i = 0; i < nb; ++i) len = (len << 8) | static_cast<unsigned char>(d[pos++]);
    }
    if (len > end - pos) return false;
    cbeg = pos;
    cend = pos + len;
    pos = cend;
    return true;
}

// ---- parsed certificate ----------------------------------------------------------------------

/// One parsed X.509 certificate: the fields path validation needs (no full DN/extension model).
struct Cert {
    std::string tbs;       ///< TBSCertificate bytes (tag+len+value) — exactly what the signature covers
    std::string issuer;    ///< raw DER of the issuer Name (SEQUENCE) — matched against a CA's subject
    std::string subject;   ///< raw DER of the subject Name
    std::string spki;      ///< SubjectPublicKeyInfo (tag+len+value) — the key that signs the child cert
    std::string sig_oid;   ///< signatureAlgorithm OID (content bytes)
    std::string sig;       ///< signatureValue (raw signature, BIT STRING minus the unused-bits byte)
    std::vector<std::string> san_dns;  ///< subjectAltName dNSName entries
    long long not_before = 0;  ///< validity start (Unix time)
    long long not_after = 0;   ///< validity end (Unix time)
    bool is_ca = false;    ///< basicConstraints cA == TRUE
};

// OID content bytes (i.e. the value after the 0x06 tag+len).
inline const std::string& oid_san() { static const std::string s({0x55, 0x1D, 0x11}); return s; }         // 2.5.29.17
inline const std::string& oid_bc() { static const std::string s({0x55, 0x1D, 0x13}); return s; }          // 2.5.29.19
inline const std::string& oid_rsa_sha256() {
    static const std::string s({0x2A, static_cast<char>(0x86), 0x48, static_cast<char>(0x86),
                                static_cast<char>(0xF7), 0x0D, 0x01, 0x01, 0x0B});
    return s;  // 1.2.840.113549.1.1.11
}
inline const std::string& oid_rsa_sha384() {
    static const std::string s({0x2A, static_cast<char>(0x86), 0x48, static_cast<char>(0x86),
                                static_cast<char>(0xF7), 0x0D, 0x01, 0x01, 0x0C});
    return s;  // 1.2.840.113549.1.1.12
}
inline const std::string& oid_ecdsa_sha256() {
    static const std::string s({0x2A, static_cast<char>(0x86), 0x48, static_cast<char>(0xCE),
                                0x3D, 0x04, 0x03, 0x02});
    return s;  // 1.2.840.10045.4.3.2
}
inline const std::string& oid_ecdsa_sha384() {
    static const std::string s({0x2A, static_cast<char>(0x86), 0x48, static_cast<char>(0xCE),
                                0x3D, 0x04, 0x03, 0x03});
    return s;  // 1.2.840.10045.4.3.3
}
inline const std::string& oid_ed25519() { static const std::string s({0x2B, 0x65, 0x70}); return s; }     // 1.3.101.112
// EC named curves (the AlgorithmIdentifier's second OID inside an id-ecPublicKey SPKI).
inline const std::string& oid_prime256v1() {
    static const std::string s({0x2A, static_cast<char>(0x86), 0x48, static_cast<char>(0xCE),
                                0x3D, 0x03, 0x01, 0x07});
    return s;  // 1.2.840.10045.3.1.7
}
inline const std::string& oid_secp384r1() {
    static const std::string s({0x2B, static_cast<char>(0x81), 0x04, 0x00, 0x22});
    return s;  // 1.3.132.0.34
}

// Parse an ASN.1 UTCTime (YYMMDDHHMMSSZ) or GeneralizedTime (YYYYMMDDHHMMSSZ) to a Unix timestamp.
inline bool parse_time(const std::string& s, unsigned char tag, long long& out) {
    std::size_t i = 0;
    auto two = [&](int& v) -> bool {
        if (i + 1 >= s.size() || s[i] < '0' || s[i] > '9' || s[i + 1] < '0' || s[i + 1] > '9')
            return false;
        v = (s[i] - '0') * 10 + (s[i + 1] - '0');
        i += 2;
        return true;
    };
    long long year;
    if (tag == 0x17) {  // UTCTime: 2-digit year (RFC 5280: <50 -> 20xx, else 19xx)
        int yy;
        if (!two(yy)) return false;
        year = yy < 50 ? 2000 + yy : 1900 + yy;
    } else if (tag == 0x18) {  // GeneralizedTime: 4-digit year
        int hi, lo;
        if (!two(hi) || !two(lo)) return false;
        year = hi * 100 + lo;
    } else {
        return false;
    }
    int mon, day, hh, mm, ss;
    if (!two(mon) || !two(day) || !two(hh) || !two(mm) || !two(ss)) return false;
    if (i >= s.size() || s[i] != 'Z') return false;  // require UTC ('Z')
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || hh > 23 || mm > 59 || ss > 60) return false;
    out = days_from_civil(year, static_cast<unsigned>(mon), static_cast<unsigned>(day)) * 86400LL +
          hh * 3600LL + mm * 60LL + ss;
    return true;
}

inline void parse_san(const std::string& d, std::size_t ob, std::size_t oe, Cert& c) {
    std::size_t p = ob;
    unsigned char tag;
    std::size_t b, e;
    if (!tlv(d, p, oe, tag, b, e) || tag != 0x30) return;  // SEQUENCE OF GeneralName
    // The loop bound must be the SEQUENCE's own end, held apart from `e` (each element's
    // content end) — sharing one variable stopped the walk after the FIRST GeneralName,
    // so a host matched by any later dNSName was refused (visible on multi-SAN CDN certs).
    std::size_t sp = b, se = e;
    while (sp < se) {
        if (!tlv(d, sp, se, tag, b, e)) return;
        if (tag == 0x82) c.san_dns.push_back(d.substr(b, e - b));  // dNSName [2] IA5String
    }
}

inline void parse_basic_constraints(const std::string& d, std::size_t ob, std::size_t oe, Cert& c) {
    std::size_t p = ob;
    unsigned char tag;
    std::size_t b, e;
    if (!tlv(d, p, oe, tag, b, e) || tag != 0x30) return;  // SEQUENCE
    std::size_t sp = b;
    if (sp < e && tlv(d, sp, e, tag, b, e) && tag == 0x01 && e > b &&
        static_cast<unsigned char>(d[b]) != 0x00)
        c.is_ca = true;  // cA BOOLEAN TRUE
}

inline void parse_extensions(const std::string& d, std::size_t cb, std::size_t ce, Cert& c) {
    std::size_t p = cb;
    unsigned char tag;
    std::size_t b, e;
    if (!tlv(d, p, ce, tag, b, e) || tag != 0x30) return;  // the wrapped SEQUENCE OF Extension
    std::size_t sp = b, se = e;
    while (sp < se) {
        if (!tlv(d, sp, se, tag, b, e) || tag != 0x30) return;  // Extension SEQUENCE
        std::size_t xp = b, xe = e;
        unsigned char t;
        std::size_t ob, oe;
        if (!tlv(d, xp, xe, t, ob, oe) || t != 0x06) continue;  // extnID OID
        const std::string oid = d.substr(ob, oe - ob);
        std::size_t save = xp;
        if (!tlv(d, xp, xe, t, ob, oe)) return;
        if (t != 0x01) xp = save;                              // optional critical BOOLEAN
        else if (!tlv(d, xp, xe, t, ob, oe)) return;
        if (t != 0x04) continue;                               // extnValue OCTET STRING
        if (oid == oid_san()) parse_san(d, ob, oe, c);
        else if (oid == oid_bc()) parse_basic_constraints(d, ob, oe, c);
    }
}

// Parse a full DER Certificate. Returns false on any structural error (fail closed).
inline bool parse_cert(const std::string& der, Cert& c) {
    std::size_t pos = 0;
    unsigned char tag;
    std::size_t cb, ce;
    if (!tlv(der, pos, der.size(), tag, cb, ce) || tag != 0x30) return false;  // Certificate SEQUENCE
    std::size_t ip = cb, ie = ce;

    const std::size_t tbs_start = ip;
    if (!tlv(der, ip, ie, tag, cb, ce) || tag != 0x30) return false;           // tbsCertificate
    c.tbs = der.substr(tbs_start, ip - tbs_start);
    std::size_t tp = cb, te = ce;

    std::size_t save = tp;
    if (!tlv(der, tp, te, tag, cb, ce)) return false;
    if (tag != 0xA0) tp = save;                                                // optional [0] version
    if (!tlv(der, tp, te, tag, cb, ce) || tag != 0x02) return false;           // serialNumber INTEGER
    if (!tlv(der, tp, te, tag, cb, ce) || tag != 0x30) return false;           // inner signature alg

    std::size_t iss_start = tp;
    if (!tlv(der, tp, te, tag, cb, ce) || tag != 0x30) return false;           // issuer Name
    c.issuer = der.substr(iss_start, tp - iss_start);

    if (!tlv(der, tp, te, tag, cb, ce) || tag != 0x30) return false;           // validity SEQUENCE
    {
        std::size_t vp = cb, ve = ce;
        unsigned char vt;
        std::size_t vb, vce;
        if (!tlv(der, vp, ve, vt, vb, vce) || !parse_time(der.substr(vb, vce - vb), vt, c.not_before))
            return false;
        if (!tlv(der, vp, ve, vt, vb, vce) || !parse_time(der.substr(vb, vce - vb), vt, c.not_after))
            return false;
    }

    std::size_t subj_start = tp;
    if (!tlv(der, tp, te, tag, cb, ce) || tag != 0x30) return false;           // subject Name
    c.subject = der.substr(subj_start, tp - subj_start);

    std::size_t spki_start = tp;
    if (!tlv(der, tp, te, tag, cb, ce) || tag != 0x30) return false;           // subjectPublicKeyInfo
    c.spki = der.substr(spki_start, tp - spki_start);

    while (tp < te) {                                                          // optional [1]/[2]/[3]
        if (!tlv(der, tp, te, tag, cb, ce)) return false;
        if (tag == 0xA3) parse_extensions(der, cb, ce, c);                     // extensions [3]
    }

    if (!tlv(der, ip, ie, tag, cb, ce) || tag != 0x30) return false;           // signatureAlgorithm
    {
        std::size_t ap = cb, ae = ce;
        unsigned char at;
        std::size_t ab, ace;
        if (!tlv(der, ap, ae, at, ab, ace) || at != 0x06) return false;        // OID
        c.sig_oid = der.substr(ab, ace - ab);
    }
    if (!tlv(der, ip, ie, tag, cb, ce) || tag != 0x03) return false;           // signatureValue BITSTR
    if (ce <= cb || static_cast<unsigned char>(der[cb]) != 0x00) return false; // 0 unused bits
    c.sig = der.substr(cb + 1, ce - cb - 1);
    return true;
}

// Extract a raw Ed25519 public key (32 bytes) from a SubjectPublicKeyInfo, or "" if not Ed25519.
inline std::string ed25519_key(const std::string& spki) {
    std::size_t p = 0;
    unsigned char tag;
    std::size_t cb, ce;
    if (!tlv(spki, p, spki.size(), tag, cb, ce) || tag != 0x30) return "";     // SPKI SEQUENCE
    std::size_t sp = cb, se = ce;
    if (!tlv(spki, sp, se, tag, cb, ce) || tag != 0x30) return "";             // AlgorithmIdentifier
    {
        std::size_t ap = cb, ae = ce;
        unsigned char at;
        std::size_t ob, oe;
        if (!tlv(spki, ap, ae, at, ob, oe) || at != 0x06) return "";
        if (spki.compare(ob, oe - ob, oid_ed25519()) != 0) return "";
    }
    if (!tlv(spki, sp, se, tag, cb, ce) || tag != 0x03) return "";             // BIT STRING
    if (ce - cb != 33 || static_cast<unsigned char>(spki[cb]) != 0x00) return "";
    return spki.substr(cb + 1, 32);
}

// The named-curve OID (content bytes) of an id-ecPublicKey SubjectPublicKeyInfo — the
// AlgorithmIdentifier's second OID — or "" when the SPKI is not an EC key with a named curve.
inline std::string ec_named_curve(const std::string& spki) {
    std::size_t p = 0;
    unsigned char tag;
    std::size_t cb, ce;
    if (!tlv(spki, p, spki.size(), tag, cb, ce) || tag != 0x30) return "";  // SPKI SEQUENCE
    std::size_t sp = cb, se = ce;
    if (!tlv(spki, sp, se, tag, cb, ce) || tag != 0x30) return "";          // AlgorithmIdentifier
    std::size_t ap = cb, ae = ce;
    if (!tlv(spki, ap, ae, tag, cb, ce) || tag != 0x06) return "";          // id-ecPublicKey OID
    if (!tlv(spki, ap, ae, tag, cb, ce) || tag != 0x06) return "";          // namedCurve OID
    return spki.substr(cb, ce - cb);
}

// Verify @p child's signature under the public key in @p issuer_spki, dispatching on the algorithm.
// Unsupported algorithms return false (fail closed).
inline bool verify_cert_sig(const Cert& child, const std::string& issuer_spki) {
    const std::string& oid = child.sig_oid;
    if (oid == oid_rsa_sha256() || oid == oid_rsa_sha384()) {
        std::string nb, eb;
        if (!rsa::parse_rsa_pubkey(issuer_spki, nb, eb)) return false;
        const std::string di = oid == oid_rsa_sha384()
                                   ? rsa::digestinfo_prefix_sha384() + hashlib::sha384_digest(child.tbs)
                                   : rsa::digestinfo_prefix_sha256() + hashlib::sha256_digest(child.tbs);
        return rsa::verify_pkcs1v15(nb, eb, di, child.sig);
    }
    if (oid == oid_ecdsa_sha256() || oid == oid_ecdsa_sha384()) {
        // The HASH comes from the signature OID; the CURVE comes from the issuer's key. Real
        // chains mix them freely (Sectigo: a P-256 intermediate signed ecdsa-with-SHA384 by a
        // P-384 root), so route on the SPKI's named-curve OID and allow either pairing.
        const std::string digest = oid == oid_ecdsa_sha384() ? hashlib::sha384_digest(child.tbs)
                                                             : hashlib::sha256_digest(child.tbs);
        const std::string curve = ec_named_curve(issuer_spki);
        if (curve == oid_prime256v1()) {
            const std::string pt = p256::spki_ec_point(issuer_spki);
            if (pt.size() != 64) return false;
            return p256::verify_der(pt, digest, child.sig);
        }
        if (curve == oid_secp384r1()) {
            const std::string pt = p384::spki_ec_point(issuer_spki);
            if (pt.size() != 96) return false;
            return p384::verify_der(pt, digest, child.sig);
        }
        return false;  // an EC issuer key on any other curve — refused, not accepted unverified
    }
    if (oid == oid_ed25519()) {
        const std::string k = ed25519_key(issuer_spki);
        if (k.size() != 32) return false;
        return ed25519::verify(to_hex(k), child.tbs, to_hex(child.sig));
    }
    return false;  // SHA-512, rsassa-PSS chain sigs, etc. — refused rather than accepted unverified
}

// RFC 6125 hostname match: exact (case-insensitive), or a single leftmost-label wildcard
// (`*.example.com` matches `a.example.com`, not `example.com` or `a.b.example.com`).
inline bool match_dns(const std::string& pat, const std::string& host) {
    if (pat.empty() || host.empty()) return false;
    if (pat == host) return true;
    if (pat.size() > 2 && pat[0] == '*' && pat[1] == '.') {
        const std::size_t dot = host.find('.');
        if (dot == std::string::npos || dot == 0) return false;
        return host.substr(0, dot).find('.') == std::string::npos && host.substr(dot) == pat.substr(1);
    }
    return false;
}

inline bool host_matches(const Cert& leaf, const std::string& host) {
    const std::string h = lower(host);
    for (const std::string& pat : leaf.san_dns)
        if (match_dns(lower(pat), h)) return true;
    return false;  // require a SAN match — the deprecated subject-CN fallback is intentionally absent
}

// ---- trust store ------------------------------------------------------------------------------

/// Trusted CA certificates, indexed by subject DN (a child's issuer DN is looked up here).
struct TrustStore {
    std::unordered_map<std::string, std::vector<Cert>> by_subject;  ///< subject DN -> trusted certs
    /// @return true when no CA certificate is loaded.
    bool empty() const { return by_subject.empty(); }
};

inline void add_pem(const std::string& pem, TrustStore& store) {
    const std::string begin = "-----BEGIN CERTIFICATE-----";
    const std::string end = "-----END CERTIFICATE-----";
    std::size_t p = 0;
    while ((p = pem.find(begin, p)) != std::string::npos) {
        const std::size_t s = p + begin.size();
        const std::size_t e = pem.find(end, s);
        if (e == std::string::npos) break;
        const std::string der = hashlib::base64_decode(pem.substr(s, e - s), /*strict=*/true);
        Cert c;
        if (!der.empty() && parse_cert(der, c)) store.by_subject[c.subject].push_back(std::move(c));
        p = e + end.size();
    }
}

// The candidate CA-bundle paths, in priority order: an explicit @p cafile, else $SSL_CERT_FILE,
// else the common system-bundle locations.
inline std::vector<std::string> default_ca_paths(const std::string& cafile) {
    std::vector<std::string> paths;
    if (!cafile.empty()) {
        paths.push_back(cafile);
        return paths;
    }
    if (const char* env = std::getenv("SSL_CERT_FILE")) paths.push_back(env);
    paths.push_back("/etc/ssl/certs/ca-certificates.crt");  // Debian/Ubuntu
    paths.push_back("/etc/pki/tls/certs/ca-bundle.crt");    // RHEL/Fedora
    paths.push_back("/etc/ssl/cert.pem");                   // Alpine/BSD/macOS
    return paths;
}

// Build a trust store from the first readable, non-empty PEM bundle among @p paths.
inline TrustStore load_trust_files(const std::vector<std::string>& paths) {
    TrustStore store;
    for (const std::string& path : paths) {
        std::ifstream f(path, std::ios::binary);
        if (!f) continue;
        const std::string pem((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        add_pem(pem, store);
        if (!store.empty()) break;
    }
    return store;
}

// Load the CA trust store (caller-supplied @p cafile, else $SSL_CERT_FILE, else the system bundle).
inline TrustStore load_trust(const std::string& cafile) {
    return load_trust_files(default_ca_paths(cafile));
}

// ---- path validation --------------------------------------------------------------------------

// Validate a server-presented DER chain (leaf first) for @p host at time @p now against @p store.
// Returns true iff the leaf is valid for the host, every cert is in its validity window, and the
// chain of signatures reaches a trusted CA (with intermediates marked CA). On failure, @p err
// carries a short reason. Fail-closed throughout.
inline bool validate(const std::vector<std::string>& der_chain, const std::string& host,
                     const TrustStore& store, long long now, std::string& err) {
    if (der_chain.empty()) {
        err = "empty certificate chain";
        return false;
    }
    std::vector<Cert> chain;
    chain.reserve(der_chain.size());
    for (const std::string& der : der_chain) {
        Cert c;
        if (!parse_cert(der, c)) {
            err = "malformed certificate in chain";
            return false;
        }
        chain.push_back(std::move(c));
    }
    if (!host_matches(chain[0], host)) {
        err = "certificate is not valid for host '" + host + "'";
        return false;
    }
    std::size_t idx = 0;
    for (int depth = 0; depth <= static_cast<int>(chain.size()); ++depth) {
        const Cert& cur = chain[idx];
        if (now < cur.not_before || now > cur.not_after) {
            err = "certificate is expired or not yet valid";
            return false;
        }
        // Does a trusted CA (subject == cur.issuer) sign cur? If so the chain terminates.
        const auto it = store.by_subject.find(cur.issuer);
        if (it != store.by_subject.end()) {
            for (const Cert& anchor : it->second)
                if (now >= anchor.not_before && now <= anchor.not_after &&
                    verify_cert_sig(cur, anchor.spki))
                    return true;
        }
        // Otherwise the next server-provided cert must be cur's issuer (a CA that signed it).
        if (idx + 1 >= chain.size()) {
            err = "certificate chain does not reach a trusted CA";
            return false;
        }
        const Cert& issuer = chain[idx + 1];
        if (issuer.subject != cur.issuer) {
            err = "broken certificate chain (issuer/subject mismatch)";
            return false;
        }
        if (!issuer.is_ca) {
            err = "an intermediate certificate is not a CA";
            return false;
        }
        if (!verify_cert_sig(cur, issuer.spki)) {
            err = "a certificate signature does not verify";
            return false;
        }
        idx += 1;
    }
    // Unreachable: each iteration returns or advances idx by 1, and idx+1 >= chain.size() returns
    // above, so the loop (which runs chain.size()+1 times) always returns first. A defensive backstop.
    err = "certificate chain too long";  // LCOV_EXCL_LINE: provably-unreachable loop backstop
    return false;                        // LCOV_EXCL_LINE
}

}  // namespace cheatah::tls::x509
