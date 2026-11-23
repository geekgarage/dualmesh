// SPDX-License-Identifier: MIT
#include "storage/sd_crypt.h"

#include <AES.h>
#include <Arduino.h>
#include <GCM.h>
#include <SHA256.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_hmac.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "esp_heap_caps.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "storage/sd_log.h"

namespace dm {
namespace sdcrypt {

namespace {
const char* const kSecretFiles[] = {
    "/sd/dualmesh/wifi.txt",
    "/sd/dualmesh/meshcore/identity.txt",
    "/sd/dualmesh/meshcore/channels.txt",
    "/sd/dualmesh/meshtastic/channels.txt",
    "/sd/dualmesh/mqtt.txt",
    "/sd/dualmesh/ble.txt",
    "/sd/dualmesh/meshtastic/identity.txt",
};
constexpr int kNumSecret = sizeof(kSecretFiles) / sizeof(kSecretFiles[0]);
constexpr const char* kCryptoFile = "/sd/dualmesh/crypto.txt";
constexpr const char* kCryptoTmp = "/sd/dualmesh/crypto.tmp";
constexpr size_t kMaxFile = 16384;  // de hemmelige filer er små
constexpr size_t kHdr = 4 + 1 + 12, kTag = 16;
constexpr int kKdfRounds = 10000;
constexpr const char* kEfuseMsg = "DualMesh-SD-efuse-v1";

Mode g_mode = Off;
bool g_have_key = false;  // nøglen er klar (ellers låst)
uint8_t g_key[32];
char g_status[48] = "fra";
// Filer, der ikke kunne dekrypteres i denne opstart (overskrives ikke).
char g_bad[kNumSecret][48];
int g_n_bad = 0;

struct Open {
  FILE* f;
  char* buf;
  size_t size;
  bool write;
};
Open g_open[4] = {};

void markBad(const char* path) {
  if (g_n_bad < kNumSecret && !unreadable(path)) snprintf(g_bad[g_n_bad++], sizeof(g_bad[0]), "%s", path);
}

void wipe(void* p, size_t n) {
  volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
}

void sha256(const void* a, size_t na, const void* b, size_t nb, const void* c, size_t nc, uint8_t out[32]) {
  SHA256 h;
  if (na) h.update(a, na);
  if (nb) h.update(b, nb);
  if (nc) h.update(c, nc);
  h.finalize(out, 32);
}

bool chipKey(uint8_t out[32]) {
  uint8_t uid[16] = {}, mac[6] = {};
  esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, uid, 128);
  esp_efuse_mac_get_default(mac);
  const char* label = "DualMesh-SD-chip-v1";
  sha256(label, strlen(label), uid, sizeof(uid), mac, sizeof(mac), out);
  return true;
}

int g_kdf = kKdfPbkdf2;  // kode-tilstandens nøgleafledning (fra crypto.txt)

// PBKDF2-HMAC-SHA256 (RFC 8018), 32 byte. HMAC-tilstandene for nøglen (ipad/opad) regnes
// kun én gang og kopieres i hver runde, så en runde koster to SHA-256-blokke (mbedtls'
// udgave regner forfra og var ~5x langsommere: 100.000 runder 9,9 s). Samme resultat;
// tjekkes mod mbedtls i profil-selvtesten.
void pbkdf2Fast(const char* pw, const uint8_t* salt, size_t slen, uint32_t iters, uint8_t out[32]) {
  uint8_t k[64] = {};
  const size_t pl = strlen(pw);
  if (pl > 64) {
    SHA256 h;
    h.update(pw, pl);
    h.finalize(k, 32);
  } else {
    memcpy(k, pw, pl);
  }
  uint8_t pad[64];
  SHA256 hi, ho;
  for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x36;
  hi.update(pad, 64);
  for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x5C;
  ho.update(pad, 64);
  uint8_t u[32];
  const uint8_t one[4] = {0, 0, 0, 1};
  SHA256 a = hi;
  a.update(salt, slen);
  a.update(one, 4);
  a.finalize(u, 32);
  SHA256 b = ho;
  b.update(u, 32);
  b.finalize(u, 32);
  memcpy(out, u, 32);
  for (uint32_t r = 1; r < iters; ++r) {
    SHA256 x = hi;
    x.update(u, 32);
    x.finalize(u, 32);
    SHA256 y = ho;
    y.update(u, 32);
    y.finalize(u, 32);
    for (int j = 0; j < 32; ++j) out[j] ^= u[j];
  }
  wipe(k, sizeof(k));
  wipe(pad, sizeof(pad));
}

// Selvtest: vores PBKDF2 mod mbedtls' (1000 runder) og tiden for kPbkdf2Rounds.
bool pbkdf2SelfTest() {
  const char* pw = "DualMesh-test";
  const uint8_t salt[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t a[32], b[32];
  pbkdf2Fast(pw, salt, 16, 1000, a);
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_pkcs5_pbkdf2_hmac(&ctx, reinterpret_cast<const unsigned char*>(pw), strlen(pw), salt, 16, 1000, 32, b);
  mbedtls_md_free(&ctx);
  const bool same = memcmp(a, b, 32) == 0;
  const uint32_t t0 = millis();
  pbkdf2Fast(pw, salt, 16, kPbkdf2Rounds, a);
  DLOG.printf("[KRYPTO] PBKDF2-selvtest: %s mbedtls, %lu runder paa %lu ms\n", same ? "samme som" : "FORSKELLIG fra",
              (unsigned long)kPbkdf2Rounds, (unsigned long)(millis() - t0));
  return same;
}

void codeKey(const char* code, const uint8_t salt[16], uint8_t out[32], int kdf) {
  if (kdf <= kKdfOld) {  // gammel afledning: kun til at læse gamle filer
    sha256(salt, 16, code, strlen(code), nullptr, 0, out);
    for (int i = 0; i < kKdfRounds; ++i) sha256(out, 32, salt, 16, code, strlen(code), out);
    return;
  }
  const uint32_t t0 = millis();
  pbkdf2Fast(code, salt, 16, kPbkdf2Rounds, out);
  static bool logged = false;
  if (!logged) {
    logged = true;
    DLOG.printf("[KRYPTO] PBKDF2 (%lu runder) tog %lu ms\n", (unsigned long)kPbkdf2Rounds,
                (unsigned long)(millis() - t0));
  }
}

bool efuseKey(uint8_t out[32]) {
  const int b = efuseKeyBlock();
  if (b < 0) return false;
  return esp_hmac_calculate(hmac_key_id_t(HMAC_KEY0 + b), kEfuseMsg, strlen(kEfuseMsg), out) == ESP_OK;
}

void toHex(const uint8_t* d, size_t n, char* out) {
  static const char* h = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = h[d[i] >> 4];
    out[2 * i + 1] = h[d[i] & 15];
  }
  out[2 * n] = 0;
}
bool fromHex(const char* s, uint8_t* d, size_t n) {
  if (strlen(s) < 2 * n) return false;
  for (size_t i = 0; i < n; ++i) {
    char b[3] = {s[2 * i], s[2 * i + 1], 0};
    char* e;
    d[i] = uint8_t(strtoul(b, &e, 16));
    if (*e) return false;
  }
  return true;
}

// Kontroltal for kode-tilstanden: første 16 byte af SHA-256(nøgle + tekst).
void checkOf(const uint8_t key[32], uint8_t out[16]) {
  uint8_t h[32];
  const char* t = "DualMesh-check";
  sha256(key, 32, t, strlen(t), nullptr, 0, h);
  memcpy(out, h, 16);
}

bool readCryptoFile(uint8_t salt[16], uint8_t check[16]) {
  FILE* f = fopen(kCryptoFile, "r");
  if (!f) return false;
  char line[96];
  bool s = false, c = false;
  g_kdf = kKdfOld;  // ingen kdf-linje: gammel fil
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "salt=", 5)) s = fromHex(line + 5, salt, 16);
    if (!strncmp(line, "check=", 6)) c = fromHex(line + 6, check, 16);
    if (!strncmp(line, "kdf=", 4)) g_kdf = atoi(line + 4);
  }
  fclose(f);
  return s && c;
}

bool writeCryptoFile(const uint8_t salt[16], const uint8_t check[16]) {
  char hs[33], hc[33];
  toHex(salt, 16, hs);
  toHex(check, 16, hc);
  FILE* f = fopen(kCryptoTmp, "w");
  if (!f) return false;
  fprintf(f, "; DualMesh SD-kryptering (kode-tilstand): salt og kontroltal, INGEN noegle\nsalt=%s\ncheck=%s\nkdf=%d\n",
          hs, hc, kKdfPbkdf2);
  const bool ok = fflush(f) == 0 && !ferror(f);
  fclose(f);
  if (!ok) return false;
  remove(kCryptoFile);
  return rename(kCryptoTmp, kCryptoFile) == 0;
}

// Læs hele filen (rå). nullptr hvis den mangler. *n = længde.
uint8_t* slurp(const char* path, size_t* n) {
  FILE* f = fopen(path, "rb");
  if (!f) return nullptr;
  uint8_t* b = static_cast<uint8_t*>(heap_caps_malloc(kMaxFile + 1, MALLOC_CAP_SPIRAM));
  if (!b) {
    fclose(f);
    return nullptr;
  }
  *n = fread(b, 1, kMaxFile, f);
  fclose(f);
  b[*n] = 0;
  return b;
}

// Dekryptér (eller tag som tekst). Resultatet i samme buffer; false = kan ikke læses.
bool decode(uint8_t* b, size_t* n, const uint8_t* key, bool have_key) {
  if (*n < 4 || memcmp(b, "DMC1", 4) != 0) return true;  // almindelig tekst
  if (!have_key || *n < kHdr + kTag) return false;
  GCM<AES256> g;
  g.setKey(key, 32);
  g.setIV(b + 5, 12);
  g.addAuthData(b, 5);
  const size_t clen = *n - kHdr - kTag;
  uint8_t* plain = static_cast<uint8_t*>(heap_caps_malloc(clen + 1, MALLOC_CAP_SPIRAM));
  if (!plain) return false;
  g.decrypt(plain, b + kHdr, clen);
  const bool ok = g.checkTag(b + kHdr + clen, kTag);
  if (ok) {
    memcpy(b, plain, clen);
    b[clen] = 0;
    *n = clen;
  }
  wipe(plain, clen);
  heap_caps_free(plain);
  return ok;
}

// Skriv indhold (krypteret efter mode/key) atomisk.
bool writeFile(const char* path, const uint8_t* d, size_t n, Mode m, const uint8_t* key) {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  // Mappen kan mangle (fx meshtastic/ ved første kanal).
  char dir[64];
  snprintf(dir, sizeof(dir), "%s", path);
  if (char* sl = strrchr(dir, '/')) {
    *sl = 0;
    mkdir(dir, 0775);
  }
  FILE* f = fopen(tmp, "wb");
  if (!f) return false;
  bool ok = true;
  if (m == Off) {
    ok = fwrite(d, 1, n, f) == n;
  } else {
    uint8_t hdr[kHdr];
    memcpy(hdr, "DMC1", 4);
    hdr[4] = uint8_t(m);
    esp_fill_random(hdr + 5, 12);
    uint8_t* c = static_cast<uint8_t*>(heap_caps_malloc(n + kTag, MALLOC_CAP_SPIRAM));
    if (!c) {
      fclose(f);
      return false;
    }
    GCM<AES256> g;
    g.setKey(key, 32);
    g.setIV(hdr + 5, 12);
    g.addAuthData(hdr, 5);
    g.encrypt(c, d, n);
    g.computeTag(c + n, kTag);
    ok = fwrite(hdr, 1, kHdr, f) == kHdr && fwrite(c, 1, n + kTag, f) == n + kTag;
    heap_caps_free(c);
  }
  ok = ok && fflush(f) == 0 && !ferror(f);
  fclose(f);
  if (!ok) {
    remove(tmp);
    return false;
  }
  shred(path);  // den gamle udgave (måske ukrypteret) må ikke kunne gendannes
  return rename(tmp, path) == 0;
}

// Gamle filer i almindelig tekst krypteres, så snart nøglen er klar.
void migratePlain() {
  if (!g_have_key || g_mode == Off) return;
  for (int i = 0; i < kNumSecret; ++i) {
    size_t n = 0;
    uint8_t* b = slurp(kSecretFiles[i], &n);
    if (!b) continue;
    if (n < 4 || memcmp(b, "DMC1", 4) != 0) {
      bool ok = writeFile(kSecretFiles[i], b, n, g_mode, g_key);
      // Kontrol: læs tilbage og sammenlign; ellers skrives den oprindelige tekst igen.
      size_t m = 0;
      uint8_t* back = ok ? slurp(kSecretFiles[i], &m) : nullptr;
      ok = back && decode(back, &m, g_key, true) && m == n && memcmp(back, b, n) == 0;
      if (back) {
        wipe(back, m);
        heap_caps_free(back);
      }
      if (!ok) writeFile(kSecretFiles[i], b, n, Off, nullptr);
      DLOG.printf("[KRYPTO] %s krypteret (%s)\n", kSecretFiles[i],
                  ok ? "OK, kontrolleret" : "FEJL - beholdt som tekst");
    }
    wipe(b, n);
    heap_caps_free(b);
  }
}

void setStatus() {
  if (g_mode == Off) snprintf(g_status, sizeof(g_status), "fra");
  else if (!g_have_key) snprintf(g_status, sizeof(g_status), "%s, LÅST", modeName(g_mode));
  else snprintf(g_status, sizeof(g_status), "%s", modeName(g_mode));
}
}  // namespace

const char* modeName(Mode m) {
  switch (m) {
    case Chip: return "chip-ID";
    case Code: return "kode";
    case Efuse: return "eFuse";
    default: return "fra";
  }
}

bool modeFromName(const char* s, Mode& out) {
  if (!s || !*s) return false;
  for (int i = 0; i <= 3; ++i)
    if (strcasecmp(s, modeName(Mode(i))) == 0) {
      out = Mode(i);
      return true;
    }
  return false;
}

int efuseKeyBlock() {
  esp_efuse_block_t b;
  if (!esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, &b)) return -1;
  return int(b) - int(EFUSE_BLK_KEY0);
}

int efuseFreeBlocks() {
  int n = 0;
  for (int b = EFUSE_BLK_KEY0; b < EFUSE_BLK_KEY_MAX; ++b)
    if (esp_efuse_key_block_unused(esp_efuse_block_t(b))) ++n;
  return n;
}

int efuseBurnNew() {
  const esp_efuse_block_t b = esp_efuse_find_unused_key_block();
  if (b == EFUSE_BLK_KEY_MAX) return -1;
  uint8_t k[32];
  esp_fill_random(k, sizeof(k));
  const esp_err_t e = esp_efuse_write_key(b, ESP_EFUSE_KEY_PURPOSE_HMAC_UP, k, sizeof(k));
  wipe(k, sizeof(k));
  DLOG.printf("[KRYPTO] eFuse-noegle braendt i blok %d: %s\n", int(b) - int(EFUSE_BLK_KEY0),
              e == ESP_OK ? "OK" : esp_err_to_name(e));
  return e == ESP_OK ? int(b) - int(EFUSE_BLK_KEY0) : -1;
}

void init(Mode m) {
  g_mode = m;
  g_have_key = false;
  g_n_bad = 0;
  if (m == Off) g_have_key = true;
  else if (m == Chip) g_have_key = chipKey(g_key);
  else if (m == Efuse) g_have_key = efuseKey(g_key);
  // Code: låst, til unlock()
  setStatus();
  DLOG.printf("[KRYPTO] SD-kryptering: %s\n", g_status);
  if (m == Efuse && !g_have_key)
    DLOG.println("[KRYPTO] ingen eFuse-noegle i denne chip - hemmelige filer kan ikke laeses");
  migratePlain();
}

Mode mode() { return g_mode; }
bool locked() { return !g_have_key; }
const char* status() { return g_status; }

bool unlock(const char* code) {
  if (g_mode != Code || g_have_key) return g_have_key;
  uint8_t salt[16], check[16], key[32], c2[16];
  if (!readCryptoFile(salt, check)) {
    DLOG.println("[KRYPTO] crypto.txt mangler - kan ikke laase op");
    return false;
  }
  const int kdf = g_kdf;
  codeKey(code, salt, key, kdf);
  checkOf(key, c2);
  if (memcmp(c2, check, 16) != 0) {
    wipe(key, sizeof(key));
    DLOG.println("[KRYPTO] forkert kode");
    return false;
  }
  memcpy(g_key, key, 32);
  wipe(key, sizeof(key));
  g_have_key = true;
  g_n_bad = 0;
  setStatus();
  DLOG.println("[KRYPTO] laast op med kode");
  migratePlain();
  if (kdf < kKdfPbkdf2) {  // gammel nøgleafledning: krypter alt igen med PBKDF2
    char err[48];
    const bool up = change(Code, code, err, sizeof(err));
    DLOG.printf("[KRYPTO] opgradering til PBKDF2: %s\n", up ? "OK" : err);
  }
  return true;
}

bool change(Mode m, const char* code, char* err, size_t err_n) {
  if (!g_have_key) {
    snprintf(err, err_n, "Lås op først");
    return false;
  }
  // 1) Læs alt med den nuværende nøgle.
  uint8_t* data[kNumSecret] = {};
  size_t len[kNumSecret] = {};
  bool ok = true;
  for (int i = 0; i < kNumSecret && ok; ++i) {
    data[i] = slurp(kSecretFiles[i], &len[i]);
    if (data[i] && !decode(data[i], &len[i], g_key, true)) {
      snprintf(err, err_n, "Kan ikke læse %s", strrchr(kSecretFiles[i], '/') + 1);
      ok = false;
    }
  }
  // 2) Ny nøgle.
  uint8_t key[32] = {}, salt[16], check[16];
  if (ok) {
    if (m == Chip) {
      chipKey(key);
    } else if (m == Efuse) {
      if (!efuseKey(key)) {
        snprintf(err, err_n, "Ingen eFuse-nøgle");
        ok = false;
      }
    } else if (m == Code) {
      if (!code || strlen(code) < 4) {
        snprintf(err, err_n, "Koden skal have mindst 4 tegn");
        ok = false;
      } else {
        esp_fill_random(salt, sizeof(salt));
        codeKey(code, salt, key, kKdfPbkdf2);
        checkOf(key, check);
        if (!writeCryptoFile(salt, check)) {
          snprintf(err, err_n, "Kan ikke skrive crypto.txt");
          ok = false;
        }
      }
    }
  }
  // 3) Skriv alt med den nye nøgle.
  for (int i = 0; i < kNumSecret && ok; ++i)
    if (data[i] && !writeFile(kSecretFiles[i], data[i], len[i], m, key)) {
      snprintf(err, err_n, "Kan ikke skrive %s", strrchr(kSecretFiles[i], '/') + 1);
      ok = false;  // (filer før denne er allerede skiftet; de øvrige er uændrede)
    }
  for (int i = 0; i < kNumSecret; ++i)
    if (data[i]) {
      wipe(data[i], len[i]);
      heap_caps_free(data[i]);
    }
  if (ok) {
    g_mode = m;
    memcpy(g_key, key, 32);
    g_have_key = true;
    g_n_bad = 0;
    setStatus();
    DLOG.printf("[KRYPTO] SD-kryptering skiftet til %s\n", modeName(m));
  }
  wipe(key, sizeof(key));
  return ok;
}

FILE* openRead(const char* path) {
  size_t n = 0;
  uint8_t* b = slurp(path, &n);
  if (!b) return nullptr;
  if (!decode(b, &n, g_key, g_have_key)) {
    DLOG.printf("[KRYPTO] %s kan ikke dekrypteres (%s) - bruges ikke og overskrives ikke\n", path,
                g_have_key ? "forkert noegle" : "laast");
    markBad(path);
    heap_caps_free(b);
    return nullptr;
  }
  FILE* f = fmemopen(b, n, "r");
  if (!f) {
    heap_caps_free(b);
    return nullptr;
  }
  for (Open& o : g_open)
    if (!o.f) {
      o = {f, reinterpret_cast<char*>(b), n, false};
      return f;
    }
  fclose(f);  // (sker ikke: højst få filer åbne ad gangen)
  heap_caps_free(b);
  return nullptr;
}

void closeRead(FILE* f) {
  if (!f) return;
  fclose(f);
  for (Open& o : g_open)
    if (o.f == f) {
      wipe(o.buf, o.size);
      heap_caps_free(o.buf);
      o = {};
    }
}

FILE* openWrite() {
  for (Open& o : g_open)
    if (!o.f) {
      o.buf = nullptr;
      o.size = 0;
      o.f = open_memstream(&o.buf, &o.size);
      o.write = true;
      return o.f;
    }
  return nullptr;
}

bool commit(FILE* f, const char* path) {
  Open* op = nullptr;
  for (Open& o : g_open)
    if (o.f == f) op = &o;
  if (!op) return false;
  fclose(f);  // færdiggør buf/size
  bool ok = false;
  if (unreadable(path)) {
    DLOG.printf("[KRYPTO] %s overskrives ikke (kunne ikke dekrypteres)\n", path);
  } else if (!g_have_key) {
    DLOG.printf("[KRYPTO] %s ikke gemt: SD-data er laast\n", path);
  } else {
    ok = writeFile(path, reinterpret_cast<uint8_t*>(op->buf), op->size, g_mode, g_key);
  }
  if (op->buf) {
    wipe(op->buf, op->size);
    free(op->buf);  // open_memstream bruger malloc
  }
  *op = {};
  return ok;
}

void clearUnreadable(const char* path) {
  for (int i = 0; i < g_n_bad; ++i)
    if (strcmp(g_bad[i], path) == 0) {
      for (int k = i; k + 1 < g_n_bad; ++k) memcpy(g_bad[k], g_bad[k + 1], sizeof(g_bad[0]));
      --g_n_bad;
      return;
    }
}

bool pbkdfSelfTest() { return pbkdf2SelfTest(); }

void deriveKey(const char* code, const uint8_t salt[16], uint8_t out[32], int kdf) {
  codeKey(code, salt, out, kdf);
}

void shred(const char* path) {
  FILE* f = fopen(path, "r+b");
  if (f) {
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    static const uint8_t zero[512] = {};
    while (n > 0) {
      const size_t k = n > long(sizeof(zero)) ? sizeof(zero) : size_t(n);
      if (fwrite(zero, 1, k, f) != k) break;
      n -= long(k);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
  }
  remove(path);
}

bool unreadable(const char* path) {
  for (int i = 0; i < g_n_bad; ++i)
    if (strcmp(g_bad[i], path) == 0) return true;
  return false;
}

}  // namespace sdcrypt
}  // namespace dm
