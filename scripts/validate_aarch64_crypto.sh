#!/usr/bin/env bash
# Validate the AArch64 (ARMv8 AES + PMULL) crypto path WITHOUT ARM hardware, by cross-compiling
# the aead module for aarch64 and running it under QEMU user-mode emulation — which implements
# the ARM AES/PMULL instructions, so this is a real byte-for-byte correctness check (NIST vector
# + hardware-vs-portable equivalence across all sizes + ChaCha20-Poly1305 round-trip).
#
# It validates CORRECTNESS only; QEMU timings are not representative, so it does NOT benchmark.
# Needs: a static aarch64 C++ cross-compiler and qemu-aarch64. If absent, it fetches a portable
# musl toolchain + qemu-user-static into $TOOLS (no root). Set SKIP_FETCH=1 to require they exist.
#
#   scripts/validate_aarch64_crypto.sh
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

TOOLS="${TOOLS:-$HOME/.cache/cheatah-armtools}"
GXX="${AARCH64_GXX:-$TOOLS/aarch64-linux-musl-cross/bin/aarch64-linux-musl-g++}"
QEMU="${QEMU_AARCH64:-$TOOLS/qemu/usr/bin/qemu-aarch64-static}"

fetch() {
    mkdir -p "$TOOLS" && cd "$TOOLS"
    [ -x "$GXX" ] || { echo "[arm] fetching musl aarch64 toolchain…"
        curl -fsSL -o tc.tgz https://musl.cc/aarch64-linux-musl-cross.tgz && tar xzf tc.tgz && rm -f tc.tgz; }
    [ -x "$QEMU" ] || { echo "[arm] fetching qemu-aarch64-static…"
        apt-get download qemu-user-static 2>/dev/null && dpkg-deb -x qemu-user-static*.deb qemu && rm -f qemu-user-static*.deb; }
    cd "$(git rev-parse --show-toplevel)"
}
if [ ! -x "$GXX" ] || [ ! -x "$QEMU" ]; then
    [ "${SKIP_FETCH:-0}" = "1" ] && { echo "[arm] toolchain/qemu missing and SKIP_FETCH=1 — install them or unset"; exit 2; }
    command -v curl >/dev/null || { echo "[arm] need curl to fetch the toolchain"; exit 2; }
    fetch
fi
[ -x "$GXX" ] && [ -x "$QEMU" ] || { echo "[arm] aarch64 toolchain or qemu unavailable"; exit 2; }

SRC="$(mktemp -d)/v.cpp"
cat > "$SRC" <<'CPP'
#include "aead.hpp"
#include <cstdio>
#include <string>
namespace ae = cheatah::aead;
struct Rng { unsigned long long s; unsigned long long nx(){ s^=s<<13; s^=s>>7; s^=s<<17; return s; } };
static std::string rb(Rng& r, size_t n){ std::string o(n,0); for(char& c:o) c=(char)(r.nx()&0xFF); return o; }
static std::string hx(const std::string& b){ static const char* H="0123456789abcdef"; std::string o; for(unsigned char c:b){o+=H[c>>4];o+=H[c&15];} return o; }
int main(){
    // NIST GCM AES-128 test case 4 (known-answer).
    std::string key="feffe9928665731c6d6a8f9467308308", iv="cafebabefacedbaddecaf888";
    auto uh=[](const std::string& h){ auto n=[](char c){return c<='9'?c-'0':(c|32)-'a'+10;}; std::string o; for(size_t i=0;i+1<h.size();i+=2)o.push_back(char((n(h[i])<<4)|n(h[i+1]))); return o; };
    std::string aad=uh("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    std::string p=uh("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
    std::string exp="42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e0915bc94fbc3221a5db94fae95ae7121a47";
    bool kat = hx(ae::aes128gcm_encrypt(key,iv,aad,p))==exp && ae::aes128gcm_decrypt(key,iv,aad,ae::aes128gcm_encrypt(key,iv,aad,p))==p;
    // Hardware-vs-portable equivalence across every block-boundary size, random inputs.
    Rng r{0x1234567890abcdefULL}; int fails=0,total=0;
    size_t sizes[]={0,1,13,16,17,31,32,63,64,65,80,127,128,129,200,255,256,512,1000};
    for(size_t n: sizes) for(int t=0;t<4;++t){
        std::string k=hx(rb(r,16)),no=hx(rb(r,12)),a=rb(r,r.nx()%40),pt=rb(r,n);
        ae::set_force_portable_crypto(false); std::string hw=ae::aes128gcm_encrypt(k,no,a,pt);
        ae::set_force_portable_crypto(true);  std::string sw=ae::aes128gcm_encrypt(k,no,a,pt);
        ae::set_force_portable_crypto(false);
        ++total; if(hw!=sw||ae::aes128gcm_decrypt(k,no,a,hw)!=pt) ++fails;
    }
    std::string ck=hx(rb(r,32)),cn=hx(rb(r,12)); std::string cct=ae::chacha20poly1305_encrypt(ck,cn,"h","msg");
    bool ch = ae::chacha20poly1305_decrypt(ck,cn,"h",cct)=="msg";
    std::printf("aarch64 crypto: hw_active=%d kat=%d aesgcm=%d/%d chacha=%d -> %s\n",
                ae::crypto_hardware_active(), kat, total-fails, total, ch,
                (kat && fails==0 && ch)?"PASS":"FAIL");
    return (kat && fails==0 && ch)?0:1;
}
CPP

BIN="$(mktemp)"
echo "[arm] cross-compiling aead for aarch64 (per-function +crypto, baseline -march=armv8-a)…"
"$GXX" -static -std=c++20 -O2 -march=armv8-a -I stdlib/aead stdlib/aead/aead.cpp "$SRC" -o "$BIN" || { echo "[arm] FAILED to cross-compile"; exit 1; }
echo "[arm] running under qemu-aarch64…"
"$QEMU" "$BIN"; rc=$?
rm -f "$BIN"
exit $rc
