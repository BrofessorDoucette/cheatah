// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "ed25519.hpp"

#include "hashlib.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>  // getentropy
#endif

// Ed25519 (RFC 8032), implemented from scratch — no external crypto component. The
// field/curve arithmetic follows the public-domain TweetNaCl reference algorithm
// (Bernstein, van Gastel, Janssen, Lange, Schwabe, Smetsers), reimplemented in C++ and
// validated against the RFC 8032 known-answer vectors in stdlib/tests/ed25519_test.cpp.
// SHA-512 comes from cheatah::hashlib (the same self-contained hash the runtime links).
namespace cheatah::ed25519 {

namespace {

using u8 = std::uint8_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;
using gf = std::array<i64, 16>;  // a GF(2^255-19) element: 16 limbs, ~16 bits each

constexpr gf gf0{};
constexpr gf gf1{1};
constexpr gf D{0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
               0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
constexpr gf D2{0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
constexpr gf X{0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
               0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
constexpr gf Y{0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
               0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
constexpr gf I{0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
               0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};

// The group order L = 2^252 + 27742317777372353535851937790883648493, little-endian.
constexpr i64 LCONST[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
                            0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                            0,    0,    0,    0,    0,    0,    0,    0,
                            0,    0,    0,    0,    0,    0,    0,    0x10};

void set25519(gf& r, const gf& a) { r = a; }

void car25519(gf& o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += (1LL << 16);
        const i64 c = o[i] >> 16;
        // i<15: carry into the next limb; i==15: carry wraps as 38*(c-1) (since 2^256≡38).
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

void sel25519(gf& p, gf& q, int b) {
    const i64 c = ~(b - 1);
    for (int i = 0; i < 16; ++i) {
        const i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack25519(u8* o, const gf& n) {
    gf m{}, t = n;
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = static_cast<u8>(t[i] & 0xff);
        o[2 * i + 1] = static_cast<u8>(t[i] >> 8);
    }
}

int neq25519(const gf& a, const gf& b) {
    u8 c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    // crypto_verify_32 (constant-time): 0 if equal.
    unsigned diff = 0;
    for (int i = 0; i < 32; ++i) diff |= static_cast<unsigned>(c[i] ^ d[i]);
    return (1 & ((diff - 1) >> 8)) - 1;  // 0 if equal, -1 otherwise
}

u8 par25519(const gf& a) {
    u8 d[32];
    pack25519(d, a);
    return d[0] & 1;
}

void unpack25519(gf& o, const u8* n) {
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (static_cast<i64>(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

void A(gf& o, const gf& a, const gf& b) {
    for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}
void Z(gf& o, const gf& a, const gf& b) {
    for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}
void M(gf& o, const gf& a, const gf& b) {
    i64 t[31] = {0};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    car25519(o);
    car25519(o);
}
void S(gf& o, const gf& a) { M(o, a, a); }

void inv25519(gf& o, const gf& i_) {
    gf c = i_;
    for (int a = 253; a >= 0; --a) {
        S(c, c);
        if (a != 2 && a != 4) M(c, c, i_);
    }
    o = c;
}

void pow2523(gf& o, const gf& i_) {
    gf c = i_;
    for (int a = 250; a >= 0; --a) {
        S(c, c);
        if (a != 1) M(c, c, i_);
    }
    o = c;
}

// A curve point in extended coordinates p = [X, Y, Z, T].
void add(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    Z(a, p[1], p[0]);
    Z(t, q[1], q[0]);
    M(a, a, t);
    A(b, p[0], p[1]);
    A(t, q[0], q[1]);
    M(b, b, t);
    M(c, p[3], q[3]);
    M(c, c, D2);
    M(d, p[2], q[2]);
    A(d, d, d);
    Z(e, b, a);
    Z(f, d, c);
    A(g, d, c);
    A(h, b, a);
    M(p[0], e, f);
    M(p[1], h, g);
    M(p[2], g, f);
    M(p[3], e, h);
}

void cswap(gf p[4], gf q[4], u8 b) {
    for (int i = 0; i < 4; ++i) sel25519(p[i], q[i], b);
}

void pack(u8* r, gf p[4]) {
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= par25519(tx) << 7;
}

void scalarmult(gf p[4], gf q[4], const u8* s) {
    set25519(p[0], gf0);
    set25519(p[1], gf1);
    set25519(p[2], gf1);
    set25519(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const u8 b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b);
        add(q, p);
        add(p, p);
        cswap(p, q, b);
    }
}

void scalarbase(gf p[4], const u8* s) {
    gf q[4];
    set25519(q[0], X);
    set25519(q[1], Y);
    set25519(q[2], gf1);
    M(q[3], X, Y);
    scalarmult(p, q, s);
}

void modL(u8* r, i64 x[64]) {
    for (int i = 63; i >= 32; --i) {
        i64 carry = 0;
        int j = i - 32;
        for (; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * LCONST[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    i64 carry = 0;
    for (int j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * LCONST[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; ++j) x[j] -= carry * LCONST[j];
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = static_cast<u8>(x[i] & 255);
    }
}

void reduce(u8* r) {
    i64 x[64];
    for (int i = 0; i < 64; ++i) x[i] = static_cast<u64>(r[i]);
    for (int i = 0; i < 64; ++i) r[i] = 0;
    modL(r, x);
}

// Whether the 32-byte little-endian scalar @p s is canonical, i.e. s < L (the group
// order). RFC 8032 strict verification rejects a non-canonical S, which closes the
// signature-malleability door (a forger can't add a multiple of L to S).
bool scalar_is_canonical(const u8* s) {
    for (int i = 31; i >= 0; --i) {
        const u8 li = static_cast<u8>(LCONST[i]);
        if (s[i] < li) return true;
        if (s[i] > li) return false;
    }
    return false;  // s == L is NOT canonical
}

int unpackneg(gf r[4], const u8 p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    S(num, r[1]);
    M(den, num, D);
    Z(num, num, r[2]);
    A(den, r[2], den);
    S(den2, den);
    S(den4, den2);
    M(den6, den4, den2);
    M(t, den6, num);
    M(t, t, den);
    pow2523(t, t);
    M(t, t, num);
    M(t, t, den);
    M(t, t, den);
    M(r[0], t, den);
    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num)) M(r[0], r[0], I);
    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num)) return -1;
    if (par25519(r[0]) == (p[31] >> 7)) Z(r[0], gf0, r[0]);
    M(r[3], r[0], r[1]);
    return 0;
}

// SHA-512 of n bytes -> 64-byte digest, via cheatah::hashlib.
void sha512(u8* out, const u8* in, std::size_t n) {
    const std::string d =
        hashlib::sha512_digest(std::string_view(reinterpret_cast<const char*>(in), n));
    for (int i = 0; i < 64; ++i) out[i] = static_cast<u8>(d[i]);
}

// ---- byte/hex helpers: the ONE canonical implementation lives in hashlib ----
using hashlib::from_hex;   // hex -> bytes (throws on odd length / non-hex); inverse of to_hex.
using hashlib::to_hex;     // bytes -> lowercase hex — both the (u8*, n) and string_view overloads.

void secure_random(u8* out, std::size_t n) {
#if defined(_WIN32)
    if (::BCryptGenRandom(nullptr, out, static_cast<unsigned long>(n),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("ed25519: BCryptGenRandom failed");
#else
    std::size_t off = 0;
    while (off < n) {
        const std::size_t chunk = (n - off < 256) ? (n - off) : 256;
        if (::getentropy(out + off, chunk) != 0) throw std::runtime_error("ed25519: getentropy failed");
        off += chunk;
    }
#endif
}

// Derive the 32-byte public key into pub from a 32-byte seed.
void public_from_seed(u8 pub[32], const u8 seed[32]) {
    u8 d[64];
    sha512(d, seed, 32);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    gf p[4];
    scalarbase(p, d);
    pack(pub, p);
}

} // namespace

std::string public_key(std::string_view secret_hex) {
    const std::string seed = from_hex(secret_hex);
    if (seed.size() != 32) throw std::invalid_argument("ed25519: secret seed must be 32 bytes (64 hex)");
    u8 pub[32];
    public_from_seed(pub, reinterpret_cast<const u8*>(seed.data()));
    return to_hex(pub, 32);
}

std::string generate() {
    u8 seed[32];
    secure_random(seed, 32);
    return to_hex(seed, 32);
}

std::string sign(std::string_view secret_hex, std::string_view message) {
    const std::string seed = from_hex(secret_hex);
    if (seed.size() != 32) throw std::invalid_argument("ed25519: secret seed must be 32 bytes (64 hex)");
    const u8* sd = reinterpret_cast<const u8*>(seed.data());
    const std::size_t n = message.size();

    u8 d[64];
    sha512(d, sd, 32);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;  // a = d[0..31] (clamped scalar)

    u8 pub[32];
    {
        gf p[4];
        scalarbase(p, d);
        pack(pub, p);
    }

    // r = SHA512(prefix || M),  prefix = d[32..63]
    std::string rbuf_in(reinterpret_cast<const char*>(d + 32), 32);
    rbuf_in.append(message.data(), n);
    u8 r[64];
    sha512(r, reinterpret_cast<const u8*>(rbuf_in.data()), rbuf_in.size());
    reduce(r);

    // R = r * B
    u8 R[32];
    {
        gf p[4];
        scalarbase(p, r);
        pack(R, p);
    }

    // k = SHA512(R || A || M)
    std::string kbuf_in(reinterpret_cast<const char*>(R), 32);
    kbuf_in.append(reinterpret_cast<const char*>(pub), 32);
    kbuf_in.append(message.data(), n);
    u8 h[64];
    sha512(h, reinterpret_cast<const u8*>(kbuf_in.data()), kbuf_in.size());
    reduce(h);

    // S = (r + k*a) mod L
    i64 x[64] = {0};
    for (int i = 0; i < 32; ++i) x[i] = static_cast<u64>(r[i]);
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j) x[i + j] += static_cast<i64>(h[i]) * static_cast<i64>(d[j]);
    u8 Sout[32];
    modL(Sout, x);

    u8 sig[64];
    for (int i = 0; i < 32; ++i) sig[i] = R[i];
    for (int i = 0; i < 32; ++i) sig[32 + i] = Sout[i];
    return to_hex(sig, 64);
}

bool verify(std::string_view public_hex, std::string_view message, std::string_view signature_hex) {
    std::string pub, sig;
    try {
        pub = from_hex(public_hex);
        sig = from_hex(signature_hex);
    } catch (const std::exception&) {
        return false;  // malformed hex -> reject
    }
    if (pub.size() != 32 || sig.size() != 64) return false;
    const u8* A_ = reinterpret_cast<const u8*>(pub.data());
    const u8* sg = reinterpret_cast<const u8*>(sig.data());

    if (!scalar_is_canonical(sg + 32)) return false;  // reject non-canonical S (S >= L)

    gf q[4];
    if (unpackneg(q, A_) != 0) return false;  // not a valid public key point

    // h = SHA512(R || A || M)
    std::string hin(reinterpret_cast<const char*>(sg), 32);  // R
    hin.append(reinterpret_cast<const char*>(A_), 32);       // A
    hin.append(message.data(), message.size());             // M
    u8 h[64];
    sha512(h, reinterpret_cast<const u8*>(hin.data()), hin.size());
    reduce(h);

    // p = S*B - h*A  (q already holds -A)
    gf p[4];
    scalarmult(p, q, h);   // p = h * (-A)
    gf g[4];
    scalarbase(g, sg + 32);  // S * B
    add(p, g);

    u8 t[32];
    pack(t, p);
    // Accept iff the recomputed R equals the signature's R, constant-time.
    unsigned diff = 0;
    for (int i = 0; i < 32; ++i) diff |= static_cast<unsigned>(sg[i] ^ t[i]);
    return diff == 0;
}

} // namespace cheatah::ed25519
