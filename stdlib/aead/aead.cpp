// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "aead.hpp"

#include <cstdint>
#include <cstring>

#include "aes_gcm_ni.hpp"  // AES-NI + PCLMULQDQ fast path for AES-128-GCM (runtime-dispatched)

// ChaCha20-Poly1305 AEAD (RFC 8439) from scratch. ChaCha20 is the 20-round ARX block
// function keyed per RFC; Poly1305 is the one-time authenticator over r,s derived from
// block 0. The AEAD construction MACs aad || pad || ciphertext || pad || lengths and
// appends the 16-byte tag. The tag comparison on decrypt is constant-time.

namespace cheatah::aead {
namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

u32 rotl(u32 x, int n) { return (x << n) | (x >> (32 - n)); }

// One ChaCha quarter round on four state words. @complexity O(1) @alloc none
// @test CheatahAead.Rfc8439Encrypt
void quarter(u32& a, u32& b, u32& c, u32& d) {
    a += b; d ^= a; d = rotl(d, 16);
    c += d; b ^= c; b = rotl(b, 12);
    a += b; d ^= a; d = rotl(d, 8);
    c += d; b ^= c; b = rotl(b, 7);
}

// The ChaCha20 block function: 64 bytes of keystream for (key, counter, nonce).
// @complexity O(1) — 20 rounds @alloc none @test CheatahAead.Rfc8439Encrypt
void chacha_block(const u32 key[8], u32 counter, const u32 nonce[3], unsigned char out[64]) {
    u32 s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,  // "expand 32-byte k"
                 key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
                 counter, nonce[0], nonce[1], nonce[2]};
    u32 w[16];
    std::memcpy(w, s, sizeof w);
    for (int round = 0; round < 10; ++round) {  // 10 double rounds = 20 rounds
        quarter(w[0], w[4], w[8], w[12]);
        quarter(w[1], w[5], w[9], w[13]);
        quarter(w[2], w[6], w[10], w[14]);
        quarter(w[3], w[7], w[11], w[15]);
        quarter(w[0], w[5], w[10], w[15]);
        quarter(w[1], w[6], w[11], w[12]);
        quarter(w[2], w[7], w[8], w[13]);
        quarter(w[3], w[4], w[9], w[14]);
    }
    for (int i = 0; i < 16; ++i) {
        const u32 v = w[i] + s[i];
        out[4 * i] = static_cast<unsigned char>(v);
        out[4 * i + 1] = static_cast<unsigned char>(v >> 8);
        out[4 * i + 2] = static_cast<unsigned char>(v >> 16);
        out[4 * i + 3] = static_cast<unsigned char>(v >> 24);
    }
}

// XOR `data` with the ChaCha20 keystream starting at block `counter0`.
// @complexity O(n) @alloc the returned string @test CheatahAead.Rfc8439Encrypt
std::string chacha_xor(const u32 key[8], const u32 nonce[3], u32 counter0, std::string_view data) {
    std::string out(data);
    unsigned char block[64];
    for (std::size_t off = 0; off < out.size(); off += 64) {
        chacha_block(key, counter0 + static_cast<u32>(off / 64), nonce, block);
        const std::size_t n = std::min<std::size_t>(64, out.size() - off);
        for (std::size_t i = 0; i < n; ++i) {
            out[off + i] = static_cast<char>(static_cast<unsigned char>(out[off + i]) ^ block[i]);
        }
    }
    return out;
}

// Poly1305 over the already-assembled MAC input, keyed by (r, s) from ChaCha block 0.
// 26-bit limbs in u64 lanes — the standard portable shape. @complexity O(n) @alloc none
// @test CheatahAead.Rfc8439Encrypt
void poly1305(const unsigned char rs[32], std::string_view msg, unsigned char tag[16]) {
    u32 r0, r1, r2, r3, r4;
    {  // load and clamp r (RFC 8439 §2.5)
        u32 t[4];
        std::memcpy(t, rs, 16);
        t[0] &= 0x0fffffff; t[1] &= 0x0ffffffc; t[2] &= 0x0ffffffc; t[3] &= 0x0ffffffc;
        r0 = t[0] & 0x3ffffff;
        r1 = ((t[0] >> 26) | (t[1] << 6)) & 0x3ffffff;
        r2 = ((t[1] >> 20) | (t[2] << 12)) & 0x3ffffff;
        r3 = ((t[2] >> 14) | (t[3] << 18)) & 0x3ffffff;
        r4 = (t[3] >> 8) & 0x3ffffff;
    }
    const u32 s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    u32 h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

    std::size_t pos = 0;
    while (pos < msg.size()) {
        unsigned char block[17] = {0};
        const std::size_t n = std::min<std::size_t>(16, msg.size() - pos);
        std::memcpy(block, msg.data() + pos, n);
        block[n] = 1;  // the high bit of the (padded) block
        pos += n;

        u32 t[4];
        std::memcpy(t, block, 16);
        h0 += t[0] & 0x3ffffff;
        h1 += ((t[0] >> 26) | (t[1] << 6)) & 0x3ffffff;
        h2 += ((t[1] >> 20) | (t[2] << 12)) & 0x3ffffff;
        h3 += ((t[2] >> 14) | (t[3] << 18)) & 0x3ffffff;
        h4 += (t[3] >> 8) | (static_cast<u32>(block[16]) << 24);

        const u64 d0 = (u64)h0 * r0 + (u64)h1 * s4 + (u64)h2 * s3 + (u64)h3 * s2 + (u64)h4 * s1;
        const u64 d1 = (u64)h0 * r1 + (u64)h1 * r0 + (u64)h2 * s4 + (u64)h3 * s3 + (u64)h4 * s2;
        const u64 d2 = (u64)h0 * r2 + (u64)h1 * r1 + (u64)h2 * r0 + (u64)h3 * s4 + (u64)h4 * s3;
        const u64 d3 = (u64)h0 * r3 + (u64)h1 * r2 + (u64)h2 * r1 + (u64)h3 * r0 + (u64)h4 * s4;
        u64 d4 = (u64)h0 * r4 + (u64)h1 * r3 + (u64)h2 * r2 + (u64)h3 * r1 + (u64)h4 * r0;

        u64 c = d0 >> 26; h0 = d0 & 0x3ffffff;
        const u64 e1 = d1 + c; c = e1 >> 26; h1 = e1 & 0x3ffffff;
        const u64 e2 = d2 + c; c = e2 >> 26; h2 = e2 & 0x3ffffff;
        const u64 e3 = d3 + c; c = e3 >> 26; h3 = e3 & 0x3ffffff;
        d4 += c; c = d4 >> 26; h4 = d4 & 0x3ffffff;
        h0 += static_cast<u32>(c * 5); c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += static_cast<u32>(c);
    }

    // final reduction mod 2^130 - 5, then the trial subtraction (constant-time select)
    u32 c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    u32 g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    u32 g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    u32 g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    u32 g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    const u32 g4 = h4 + c - (1u << 26);

    const u32 mask = (g4 >> 31) - 1;  // all-ones when h >= p (take g), zero otherwise
    h0 = (h0 & ~mask) | (g0 & mask);
    h1 = (h1 & ~mask) | (g1 & mask);
    h2 = (h2 & ~mask) | (g2 & mask);
    h3 = (h3 & ~mask) | (g3 & mask);
    h4 = (h4 & ~mask) | (g4 & mask);

    // h += s (the second 16 bytes of rs), little-endian, then serialize
    const u64 f0 = ((h0) | (h1 << 26)) & 0xffffffffull;
    const u64 f1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffffull;
    const u64 f2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffffull;
    const u64 f3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffffull;
    u32 s_part[4];
    std::memcpy(s_part, rs + 16, 16);
    const u64 f[4] = {f0, f1, f2, f3};
    u64 carry_word = 0;
    for (int i = 0; i < 4; ++i) {
        const u64 sum = f[i] + s_part[i] + carry_word;  // 32-bit lanes with carry between them
        carry_word = sum >> 32;
        tag[4 * i] = static_cast<unsigned char>(sum);
        tag[4 * i + 1] = static_cast<unsigned char>(sum >> 8);
        tag[4 * i + 2] = static_cast<unsigned char>(sum >> 16);
        tag[4 * i + 3] = static_cast<unsigned char>(sum >> 24);
    }
}

// Assemble the AEAD MAC input (aad || pad16 || ct || pad16 || len(aad) || len(ct)) and tag it.
// @complexity O(n) @alloc the assembled buffer @test CheatahAead.Rfc8439Encrypt
void aead_tag(const u32 key[8], const u32 nonce[3], std::string_view aad, std::string_view ct,
              unsigned char tag[16]) {
    unsigned char block0[64];
    chacha_block(key, 0, nonce, block0);  // rs = the first 32 bytes of block 0

    std::string mac_input;
    mac_input.reserve(aad.size() + ct.size() + 32);
    mac_input.append(aad);
    mac_input.append((16 - aad.size() % 16) % 16, '\0');
    mac_input.append(ct);
    mac_input.append((16 - ct.size() % 16) % 16, '\0');
    unsigned char lens[16];
    const u64 alen = aad.size(), clen = ct.size();
    for (int i = 0; i < 8; ++i) {
        lens[i] = static_cast<unsigned char>(alen >> (8 * i));
        lens[8 + i] = static_cast<unsigned char>(clen >> (8 * i));
    }
    mac_input.append(reinterpret_cast<const char*>(lens), 16);
    poly1305(block0, mac_input, tag);
}

// hex -> n bytes (false on malformed). @complexity O(n) @alloc none @test CheatahAead.RejectsTamper
bool hex_bytes(std::string_view hex, unsigned char* out, std::size_t n) {
    if (hex.size() != 2 * n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        unsigned v = 0;
        for (int k = 0; k < 2; ++k) {
            const char ch = hex[2 * i + k];
            v <<= 4;
            if (ch >= '0' && ch <= '9') v |= static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') v |= static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') v |= static_cast<unsigned>(ch - 'A' + 10);
            else return false;
        }
        out[i] = static_cast<unsigned char>(v);
    }
    return true;
}

bool load_key_nonce(std::string_view key_hex, std::string_view nonce_hex, u32 key[8], u32 nonce[3]) {
    unsigned char kb[32], nb[12];
    if (!hex_bytes(key_hex, kb, 32) || !hex_bytes(nonce_hex, nb, 12)) return false;
    std::memcpy(key, kb, 32);    // little-endian words per RFC 8439
    std::memcpy(nonce, nb, 12);
    return true;
}

// ===================== AES-128-GCM (TLS_AES_128_GCM_SHA256) =====================
// AES-128 (encrypt only — GCM never AES-decrypts) + GHASH over GF(2^128) + GCM mode. Byte-oriented
// and correctness-first (no T-tables); the record cipher is exercised once per TLS record.

// The AES S-box (FIPS-197).
const unsigned char kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

// Expand a 16-byte AES-128 key into 11 round keys (176 bytes). State/round-key byte layout is
// column-major: byte (row r, col c) at index 4*c + r.
void aes128_key_expand(const unsigned char key[16], unsigned char rk[176]) {
    static const unsigned char rcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    std::memcpy(rk, key, 16);
    int r = 0;
    for (int i = 16; i < 176; i += 4) {
        unsigned char t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
        if (i % 16 == 0) {  // RotWord + SubWord + Rcon on the first word of each round key
            const unsigned char a0 = t[0];
            t[0] = static_cast<unsigned char>(kSbox[t[1]] ^ rcon[r++]);
            t[1] = kSbox[t[2]];
            t[2] = kSbox[t[3]];
            t[3] = kSbox[a0];
        }
        for (int j = 0; j < 4; ++j) rk[i + j] = static_cast<unsigned char>(rk[i - 16 + j] ^ t[j]);
    }
}

unsigned char xtime(unsigned char x) {
    return static_cast<unsigned char>((x << 1) ^ ((x >> 7) * 0x1b));  // ·2 in GF(2^8)
}

// Encrypt one 16-byte block (AES-128, 10 rounds).
void aes128_encrypt_block(const unsigned char rk[176], const unsigned char in[16],
                          unsigned char out[16]) {
    unsigned char s[16];
    for (int i = 0; i < 16; ++i) s[i] = static_cast<unsigned char>(in[i] ^ rk[i]);  // round 0
    for (int round = 1; round <= 10; ++round) {
        for (int i = 0; i < 16; ++i) s[i] = kSbox[s[i]];  // SubBytes
        unsigned char t;                                   // ShiftRows
        t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
        if (round != 10) {  // MixColumns
            for (int c = 0; c < 4; ++c) {
                unsigned char* col = s + 4 * c;
                const unsigned char a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = static_cast<unsigned char>(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
                col[1] = static_cast<unsigned char>(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
                col[2] = static_cast<unsigned char>(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
                col[3] = static_cast<unsigned char>((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
            }
        }
        const unsigned char* r_k = rk + 16 * round;       // AddRoundKey
        for (int i = 0; i < 16; ++i) s[i] = static_cast<unsigned char>(s[i] ^ r_k[i]);
    }
    std::memcpy(out, s, 16);
}

// GF(2^128) multiply (SP 800-38D): out = X · Y, big-endian bit order, reduction poly R = 0xe1<<120.
void gf_mult(const unsigned char X[16], const unsigned char Y[16], unsigned char out[16]) {
    unsigned char Z[16] = {0}, V[16];
    std::memcpy(V, Y, 16);
    for (int i = 0; i < 128; ++i) {
        if ((X[i / 8] >> (7 - (i % 8))) & 1)
            for (int j = 0; j < 16; ++j) Z[j] ^= V[j];
        const unsigned char lsb = V[15] & 1;
        for (int j = 15; j > 0; --j) V[j] = static_cast<unsigned char>((V[j] >> 1) | ((V[j - 1] & 1) << 7));
        V[0] >>= 1;
        if (lsb) V[0] ^= 0xe1;
    }
    std::memcpy(out, Z, 16);
}

// GHASH-accumulate the zero-padded `data` into Y (Y := (Y XOR block)·H per 16-byte block).
void ghash_blocks(unsigned char Y[16], const unsigned char H[16], const unsigned char* p,
                  std::size_t n) {
    for (std::size_t off = 0; off < n; off += 16) {
        unsigned char b[16] = {0};
        const std::size_t m = std::min<std::size_t>(16, n - off);
        std::memcpy(b, p + off, m);
        for (int i = 0; i < 16; ++i) Y[i] ^= b[i];
        unsigned char t[16];
        gf_mult(Y, H, t);
        std::memcpy(Y, t, 16);
    }
}

// The GCM tag: GHASH(AAD || pad || C || pad || [len(AAD)bits]_64 || [len(C)bits]_64) XOR E(J0).
void gcm_tag(const unsigned char rk[176], const unsigned char H[16], const unsigned char J0[16],
             std::string_view aad, std::string_view ct, unsigned char tag[16]) {
    unsigned char Y[16] = {0};
    ghash_blocks(Y, H, reinterpret_cast<const unsigned char*>(aad.data()), aad.size());
    ghash_blocks(Y, H, reinterpret_cast<const unsigned char*>(ct.data()), ct.size());
    unsigned char lb[16] = {0};
    const u64 abits = static_cast<u64>(aad.size()) * 8, cbits = static_cast<u64>(ct.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        lb[7 - i] = static_cast<unsigned char>(abits >> (8 * i));
        lb[15 - i] = static_cast<unsigned char>(cbits >> (8 * i));
    }
    for (int i = 0; i < 16; ++i) Y[i] ^= lb[i];
    unsigned char t[16];
    gf_mult(Y, H, t);
    unsigned char ej0[16];
    aes128_encrypt_block(rk, J0, ej0);
    for (int i = 0; i < 16; ++i) tag[i] = static_cast<unsigned char>(t[i] ^ ej0[i]);
}

void inc32(unsigned char ctr[16]) {  // increment the rightmost 32 bits (big-endian)
    for (int j = 15; j >= 12; --j)
        if (++ctr[j] != 0) break;
}

// GCTR: XOR `data` in place with the AES-CTR keystream starting at counter `ctr` (advanced).
void gctr(const unsigned char rk[176], unsigned char ctr[16], std::string& data) {
    unsigned char ks[16];
    for (std::size_t off = 0; off < data.size(); off += 16) {
        aes128_encrypt_block(rk, ctr, ks);
        const std::size_t n = std::min<std::size_t>(16, data.size() - off);
        for (std::size_t i = 0; i < n; ++i)
            data[off + i] = static_cast<char>(static_cast<unsigned char>(data[off + i]) ^ ks[i]);
        inc32(ctr);
    }
}

// Force the portable (non-AES-NI) AES-GCM path — a testing/determinism hook so the scalar
// reference is exercised even on CPUs where the hardware path is the default.
bool g_force_portable = false;

} // namespace

void set_force_portable_crypto(bool on) { g_force_portable = on; }

namespace {
bool aes_gcm_use_hw() { return accel::available() && !g_force_portable; }
}  // namespace

bool crypto_hardware_active() { return aes_gcm_use_hw(); }

std::string chacha20poly1305_encrypt(std::string_view key_hex, std::string_view nonce_hex,
                                     std::string_view aad, std::string_view plaintext) {
    u32 key[8], nonce[3];
    if (!load_key_nonce(key_hex, nonce_hex, key, nonce)) return "";
    std::string ct = chacha_xor(key, nonce, 1, plaintext);  // counter starts at 1 (0 keys the MAC)
    unsigned char tag[16];
    aead_tag(key, nonce, aad, ct, tag);
    ct.append(reinterpret_cast<const char*>(tag), 16);
    return ct;
}

std::string chacha20poly1305_decrypt(std::string_view key_hex, std::string_view nonce_hex,
                                     std::string_view aad, std::string_view ciphertext) {
    u32 key[8], nonce[3];
    if (!load_key_nonce(key_hex, nonce_hex, key, nonce) || ciphertext.size() < 16) return "";
    const std::string_view ct = ciphertext.substr(0, ciphertext.size() - 16);
    const std::string_view given = ciphertext.substr(ciphertext.size() - 16);
    unsigned char tag[16];
    aead_tag(key, nonce, aad, ct, tag);
    unsigned char diff = 0;  // constant-time compare: never early-exit on a mismatching byte
    for (int i = 0; i < 16; ++i) {
        diff |= tag[i] ^ static_cast<unsigned char>(given[i]);
    }
    if (diff != 0) return "";
    return chacha_xor(key, nonce, 1, ct);
}

std::string aes128gcm_encrypt(std::string_view key_hex, std::string_view nonce_hex,
                              std::string_view aad, std::string_view plaintext) {
    unsigned char kb[16], nb[12];
    if (!hex_bytes(key_hex, kb, 16) || !hex_bytes(nonce_hex, nb, 12)) return "";
    if (aes_gcm_use_hw()) return accel::aes128gcm_encrypt(kb, nb, aad, plaintext);
    // --- portable reference path (identical output; this is how AES-128-GCM works) ---
    unsigned char rk[176];
    aes128_key_expand(kb, rk);
    unsigned char H[16], zero[16] = {0};
    aes128_encrypt_block(rk, zero, H);                 // hash subkey H = E(0)
    unsigned char J0[16] = {0};
    std::memcpy(J0, nb, 12);
    J0[15] = 1;                                        // J0 = nonce || 0x00000001
    std::string ct(plaintext);
    unsigned char ctr[16];
    std::memcpy(ctr, J0, 16);
    inc32(ctr);                                        // CTR starts at inc32(J0)
    gctr(rk, ctr, ct);
    unsigned char tag[16];
    gcm_tag(rk, H, J0, aad, ct, tag);
    ct.append(reinterpret_cast<const char*>(tag), 16);
    return ct;
}

std::string aes128gcm_decrypt(std::string_view key_hex, std::string_view nonce_hex,
                              std::string_view aad, std::string_view ciphertext) {
    unsigned char kb[16], nb[12];
    if (!hex_bytes(key_hex, kb, 16) || !hex_bytes(nonce_hex, nb, 12) || ciphertext.size() < 16)
        return "";
    if (aes_gcm_use_hw()) return accel::aes128gcm_decrypt(kb, nb, aad, ciphertext);
    // --- portable reference path (identical output; this is how AES-128-GCM works) ---
    unsigned char rk[176];
    aes128_key_expand(kb, rk);
    unsigned char H[16], zero[16] = {0};
    aes128_encrypt_block(rk, zero, H);
    unsigned char J0[16] = {0};
    std::memcpy(J0, nb, 12);
    J0[15] = 1;
    const std::string_view ct = ciphertext.substr(0, ciphertext.size() - 16);
    const std::string_view given = ciphertext.substr(ciphertext.size() - 16);
    unsigned char tag[16];
    gcm_tag(rk, H, J0, aad, ct, tag);
    unsigned char diff = 0;  // constant-time tag compare
    for (int i = 0; i < 16; ++i) diff |= tag[i] ^ static_cast<unsigned char>(given[i]);
    if (diff != 0) return "";
    std::string pt(ct);
    unsigned char ctr[16];
    std::memcpy(ctr, J0, 16);
    inc32(ctr);
    gctr(rk, ctr, pt);
    return pt;
}

} // namespace cheatah::aead
