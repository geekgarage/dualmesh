// SPDX-License-Identifier: MIT
#include "net/mt_pki.h"

#include <AES.h>
#include <Arduino.h>
#include <Curve25519.h>
#include <SHA256.h>
#include <esp_random.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>

#include "storage/sd_crypt.h"
#include "storage/sd_log.h"

extern "C" {
#include "ed_25519.h"
#include "fe.h"
#include "ge.h"
#include "sc.h"
#include "sha512.h"
}

namespace dm {
namespace mt_pki {

namespace {

constexpr const char* kFile = "/sd/dualmesh/meshtastic/identity.txt";  // HEMMELIG

uint8_t g_priv[32];      // clampet X25519-skalar
uint8_t g_pub[32];       // X25519 offentlig nøgle
uint8_t g_ed_a[32];      // XEdDSA-skalar (evt. negeret)
uint8_t g_ed_pub[32];    // Ed25519 offentlig nøgle (fortegnsbit 0)
bool g_ready = false;
uint32_t g_num = 0;

// Cache af fælles nøgler (peer -> SHA-256(X25519)), så X25519 ikke regnes for hver pakke.
struct Shared {
  uint8_t peer[32];
  uint8_t key[32];
  uint32_t used;
  bool valid;
};
Shared g_cache[8];

void clamp(uint8_t k[32]) {
  k[0] &= 0xF8;
  k[31] = uint8_t((k[31] & 0x7F) | 0x40);
}

void wipe(void* p, size_t n) {
  volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
}

// XEdDSA-nøglepar ud fra den clampede X25519-skalar: A = a·B; fortegnsbit 1 -> brug -a.
void deriveEd() {
  ge_p3 A;
  ge_scalarmult_base(&A, g_priv);
  ge_p3_tobytes(g_ed_pub, &A);
  memcpy(g_ed_a, g_priv, 32);
  if (g_ed_pub[31] & 0x80) {
    // -a mod L = a · (L - 1) + 0
    static const uint8_t kLminus1[32] = {0xec, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                         0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                                         0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};
    static const uint8_t kZero[32] = {};
    uint8_t neg[32];
    sc_muladd(neg, g_priv, kLminus1, kZero);
    memcpy(g_ed_a, neg, 32);
    wipe(neg, sizeof(neg));
    g_ed_pub[31] &= 0x7F;
  }
}

bool setPrivate(const uint8_t priv[32]) {
  memcpy(g_priv, priv, 32);
  clamp(g_priv);
  static const uint8_t base[32] = {9};
  if (!Curve25519::eval(g_pub, g_priv, base)) return false;
  deriveEd();
  g_num = crc32(g_pub, 32);
  memset(g_cache, 0, sizeof(g_cache));
  g_ready = true;
  return true;
}

bool sharedKey(const uint8_t* peer, uint8_t key[32]) {
  static uint32_t tick = 0;
  ++tick;
  int slot = 0;
  for (int i = 0; i < 8; ++i) {
    if (g_cache[i].valid && memcmp(g_cache[i].peer, peer, 32) == 0) {
      g_cache[i].used = tick;
      memcpy(key, g_cache[i].key, 32);
      return true;
    }
    if (!g_cache[i].valid || g_cache[i].used < g_cache[slot].used) slot = i;
  }
  uint8_t s[32];
  if (!Curve25519::eval(s, g_priv, peer)) return false;
  static const uint8_t zero[32] = {};
  if (memcmp(s, zero, 32) == 0) return false;  // svagt punkt
  SHA256 h;
  h.update(s, 32);
  h.finalize(key, 32);
  wipe(s, sizeof(s));
  g_cache[slot] = Shared{};
  memcpy(g_cache[slot].peer, peer, 32);
  memcpy(g_cache[slot].key, key, 32);
  g_cache[slot].used = tick;
  g_cache[slot].valid = true;
  return true;
}

// ---- AES-CCM (RFC 3610), L = 2, M = 8, ingen AAD ----
void ccmCtr(AES256& aes, const uint8_t nonce13[13], uint16_t i, uint8_t out[16]) {
  uint8_t a[16];
  a[0] = 1;  // L - 1
  memcpy(a + 1, nonce13, 13);
  a[14] = uint8_t(i >> 8);
  a[15] = uint8_t(i);
  aes.encryptBlock(out, a);
}

void ccmMac(AES256& aes, const uint8_t nonce13[13], const uint8_t* plain, size_t n, uint8_t tag[8]) {
  uint8_t x[16], b[16];
  b[0] = uint8_t(((8 - 2) / 2) << 3 | (2 - 1));  // ingen AAD, M = 8, L = 2
  memcpy(b + 1, nonce13, 13);
  b[14] = uint8_t(n >> 8);
  b[15] = uint8_t(n);
  aes.encryptBlock(x, b);
  for (size_t off = 0; off < n; off += 16) {
    const size_t k = n - off < 16 ? n - off : 16;
    for (size_t j = 0; j < 16; ++j) x[j] ^= j < k ? plain[off + j] : 0;
    aes.encryptBlock(x, x);
  }
  memcpy(tag, x, 8);
}

void makeNonce(uint32_t from, uint32_t id, uint32_t extra, uint8_t n13[13]) {
  uint8_t n[16] = {};
  memcpy(n, &id, 4);         // pakke-id (64 bit LE; øverste 32 bit 0) ...
  memcpy(n + 4, &extra, 4);  // ... byte 4-7 overskrives af den ekstra nonce
  memcpy(n + 8, &from, 4);
  memcpy(n13, n, 13);
}

void signingBuffer(const SignFields& f, uint8_t* buf, size_t& len) {
  uint8_t* w = buf;
  *w++ = 0x01;
  const uint32_t v[8] = {f.from, f.id, f.to, f.portnum, f.request_id, f.reply_id, f.emoji,
                         f.has_bitfield ? f.bitfield : 0};
  for (uint32_t x : v) {
    memcpy(w, &x, 4);
    w += 4;
  }
  *w++ = uint8_t((f.want_response ? 1 : 0) | (f.has_bitfield ? 2 : 0));
  if (f.payload_len) memcpy(w, f.payload, f.payload_len);
  len = 34 + f.payload_len;
}

}  // namespace

uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    c ^= d[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return c ^ 0xFFFFFFFFu;
}

bool begin(bool sd) {
  if (g_ready) return true;
  uint8_t priv[32] = {};
  bool have = false;
  if (sd) {
    if (FILE* f = sdcrypt::openRead(kFile)) {
      char line[96];
      while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "priv=", 5) != 0 || strlen(line) < 5 + 64) continue;
        have = true;
        for (int i = 0; i < 32 && have; ++i) {
          char t[3] = {line[5 + 2 * i], line[6 + 2 * i], 0};
          char* e;
          priv[i] = uint8_t(strtoul(t, &e, 16));
          have = !*e;
        }
      }
      sdcrypt::closeRead(f);
    } else if (sdcrypt::unreadable(kFile)) {
      DLOG.println("[MT] identitet kan ikke dekrypteres - bruger en midlertidig noegle (filen roeres ikke)");
      sd = false;
    }
  }
  if (!have) {
    esp_fill_random(priv, sizeof(priv));
    if (sd) {
      mkdir("/sd/dualmesh/meshtastic", 0775);
      FILE* f = sdcrypt::openWrite();
      if (f) {
        uint8_t c[32];
        memcpy(c, priv, 32);
        clamp(c);
        fprintf(f, "; DualMesh Meshtastic-identitet (HEMMELIG): X25519 privat noegle\npriv=");
        for (int i = 0; i < 32; ++i) fprintf(f, "%02x", c[i]);
        fprintf(f, "\n");
        wipe(c, sizeof(c));
        const bool ok = sdcrypt::commit(f, kFile);
        DLOG.printf("[MT] ny identitet lavet og gemt krypteret: %s\n", ok ? "OK" : "FEJL");
      }
    } else {
      DLOG.println("[MT] ingen SD: midlertidig noegle (nyt nodenummer ved hver opstart)");
    }
  }
  const bool ok = setPrivate(priv);
  wipe(priv, sizeof(priv));
  if (ok) DLOG.printf("[MT] identitet klar: nodenummer !%08lx (CRC-32 af noeglen)\n", (unsigned long)g_num);
  return ok;
}

bool ready() { return g_ready; }
const uint8_t* publicKey() { return g_pub; }
uint32_t nodeNum() { return g_num; }

size_t encrypt(const uint8_t* peer, uint32_t from, uint32_t id, const uint8_t* plain, size_t n, uint8_t* out) {
  if (!g_ready || n > 0xFFFF) return 0;
  uint8_t key[32];
  if (!sharedKey(peer, key)) return 0;
  uint32_t extra = esp_random();
  uint8_t n13[13];
  makeNonce(from, id, extra, n13);
  AES256 aes;
  aes.setKey(key, 32);
  wipe(key, sizeof(key));
  uint8_t tag[8], s[16];
  ccmMac(aes, n13, plain, n, tag);
  for (size_t off = 0; off < n; off += 16) {
    ccmCtr(aes, n13, uint16_t(off / 16 + 1), s);
    for (size_t j = 0; j < 16 && off + j < n; ++j) out[off + j] = plain[off + j] ^ s[j];
  }
  ccmCtr(aes, n13, 0, s);
  for (int j = 0; j < 8; ++j) out[n + j] = tag[j] ^ s[j];
  memcpy(out + n + 8, &extra, 4);
  return n + 12;
}

int decrypt(const uint8_t* peer, uint32_t from, uint32_t id, const uint8_t* in, size_t n, uint8_t* out) {
  if (!g_ready || n <= 12) return -1;
  const size_t m = n - 12;
  uint8_t key[32];
  if (!sharedKey(peer, key)) return -1;
  uint32_t extra;
  memcpy(&extra, in + m + 8, 4);
  uint8_t n13[13];
  makeNonce(from, id, extra, n13);
  AES256 aes;
  aes.setKey(key, 32);
  wipe(key, sizeof(key));
  uint8_t s[16], tag[8];
  for (size_t off = 0; off < m; off += 16) {
    ccmCtr(aes, n13, uint16_t(off / 16 + 1), s);
    for (size_t j = 0; j < 16 && off + j < m; ++j) out[off + j] = in[off + j] ^ s[j];
  }
  ccmMac(aes, n13, out, m, tag);
  ccmCtr(aes, n13, 0, s);
  uint8_t diff = 0;
  for (int j = 0; j < 8; ++j) diff |= uint8_t(tag[j] ^ s[j] ^ in[m + j]);
  if (diff) {
    wipe(out, m);
    return -1;
  }
  return int(m);
}

bool sign(const SignFields& f, uint8_t sig[64]) {
  if (!g_ready || f.payload_len > 240) return false;
  uint8_t msg[34 + 240];
  size_t len = 0;
  signingBuffer(f, msg, len);
  // r = SHA-512(0xFE || 31 x 0xFF || a || M || Z) mod L, med 64 tilfældige byte Z (XEdDSA).
  uint8_t pad[32], z[64], r[64], h[64];
  pad[0] = 0xFE;
  memset(pad + 1, 0xFF, 31);
  esp_fill_random(z, sizeof(z));
  sha512_context c;
  sha512_init(&c);
  sha512_update(&c, pad, 32);
  sha512_update(&c, g_ed_a, 32);
  sha512_update(&c, msg, len);
  sha512_update(&c, z, sizeof(z));
  sha512_final(&c, r);
  sc_reduce(r);
  ge_p3 R;
  ge_scalarmult_base(&R, r);
  ge_p3_tobytes(sig, &R);
  // h = SHA-512(R || A || M) mod L; s = h·a + r mod L.
  sha512_init(&c);
  sha512_update(&c, sig, 32);
  sha512_update(&c, g_ed_pub, 32);
  sha512_update(&c, msg, len);
  sha512_final(&c, h);
  sc_reduce(h);
  sc_muladd(sig + 32, h, g_ed_a, r);
  wipe(r, sizeof(r));
  wipe(z, sizeof(z));
  return true;
}

bool verify(const uint8_t* peer_x25519, const SignFields& f, const uint8_t sig[64]) {
  if (f.payload_len > 240) return false;
  // Ed25519-nøgle ud fra X25519-nøglen u: y = (u - 1) / (u + 1), fortegnsbit 0.
  fe u, one, um1, up1, inv, y;
  uint8_t pk[32];
  fe_frombytes(u, peer_x25519);
  fe_1(one);
  fe_sub(um1, u, one);
  fe_add(up1, u, one);
  fe_invert(inv, up1);
  fe_mul(y, um1, inv);
  fe_tobytes(pk, y);
  pk[31] &= 0x7F;
  uint8_t msg[34 + 240];
  size_t len = 0;
  signingBuffer(f, msg, len);
  return ed25519_verify(sig, msg, len, pk) == 1;
}

bool selfTest() {
  if (!g_ready) return false;
  // CRC-32 af "123456789" = 0xCBF43926 (standard testværdi).
  const bool crc_ok = crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u;
  // PKI med os selv som modpart (X25519 er symmetrisk).
  const char* txt = "PKI-selvtest æøå 😀";
  uint8_t enc[96], dec[96];
  const size_t n = encrypt(g_pub, g_num, 0x1234, reinterpret_cast<const uint8_t*>(txt), strlen(txt), enc);
  const int m = n ? decrypt(g_pub, g_num, 0x1234, enc, n, dec) : -1;
  const bool ccm_ok = m == int(strlen(txt)) && memcmp(dec, txt, size_t(m)) == 0;
  enc[3] ^= 1;  // én ændret bit skal afvises
  const bool tamper_ok = decrypt(g_pub, g_num, 0x1234, enc, n, dec) < 0;
  // Signatur
  SignFields f{};
  f.from = g_num;
  f.id = 0x5678;
  f.to = 0xFFFFFFFFu;
  f.portnum = 4;
  f.payload = reinterpret_cast<const uint8_t*>(txt);
  f.payload_len = strlen(txt);
  uint8_t sig[64];
  const bool sig_ok = sign(f, sig) && verify(g_pub, f, sig);
  f.id ^= 1;
  const bool sig_bad_ok = !verify(g_pub, f, sig);
  const bool ok = crc_ok && ccm_ok && tamper_ok && sig_ok && sig_bad_ok;
  DLOG.printf("[MT] PKI-selvtest: crc32 %s, ccm %s, manipulation afvist %s, xeddsa %s, forkert signatur afvist %s -> %s\n",
              crc_ok ? "OK" : "FEJL", ccm_ok ? "OK" : "FEJL", tamper_ok ? "OK" : "FEJL", sig_ok ? "OK" : "FEJL",
              sig_bad_ok ? "OK" : "FEJL", ok ? "BESTAAET" : "FEJLEDE");
  return ok;
}

}  // namespace mt_pki
}  // namespace dm
