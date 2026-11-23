// SPDX-License-Identifier: MIT
#include "storage/profile_io.h"

#include <AES.h>
#include <GCM.h>
#include <dirent.h>
#include <esp_random.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <strings.h>

#include "esp_heap_caps.h"
#include "storage/sd_crypt.h"
#include "storage/sd_log.h"
#include "storage/sd_store.h"
#include "util/wall_clock.h"

namespace dm {
namespace profile_io {

namespace {
constexpr const char* kExportDir = "/sd/dualmesh/eksport";
constexpr size_t kMax = 96 * 1024;  // hele profilen (kontakter fylder mest)

struct Part {
  const char* rel;  // navn i profilen (relativt til /sd/dualmesh/)
  bool secret;      // skrives/læses via SD-krypteringen
};
const Part kParts[] = {
    {"meshcore/identity.txt", true},
    {"meshcore/channels.txt", true},
    {"meshcore/contacts.txt", false},
    {"meshtastic/channels.txt", true},
    {"wifi.txt", true},
    {"mqtt.txt", true},
    {"ble.txt", true},
    {"meshtastic/identity.txt", true},
};

void fullPath(const char* rel, char* out, size_t n) { snprintf(out, n, "/sd/dualmesh/%s", rel); }

// Læs en fil (dekrypteret hvis hemmelig) ind i buf; returnerer længde eller -1.
long readPart(const Part& p, char* buf, size_t cap) {
  char path[64];
  fullPath(p.rel, path, sizeof(path));
  FILE* f = p.secret ? sdcrypt::openRead(path) : fopen(path, "rb");
  if (!f) return -1;
  const size_t n = fread(buf, 1, cap, f);
  if (p.secret) sdcrypt::closeRead(f);
  else fclose(f);
  return long(n);
}

bool writePart(const Part& p, const char* data, size_t n) {
  char path[64], dir[64];
  fullPath(p.rel, path, sizeof(path));
  snprintf(dir, sizeof(dir), "%s", path);
  if (char* sl = strrchr(dir, '/')) {
    *sl = 0;
    mkdir(dir, 0775);
  }
  if (p.secret) {
    sdcrypt::clearUnreadable(path);  // brugeren erstatter den bevidst
    FILE* f = sdcrypt::openWrite();
    if (!f) return false;
    fwrite(data, 1, n, f);
    return sdcrypt::commit(f, path);
  }
  char tmp[72];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE* f = fopen(tmp, "wb");
  if (!f) return false;
  const bool ok = fwrite(data, 1, n, f) == n && fflush(f) == 0 && !ferror(f);
  fclose(f);
  if (!ok) return false;
  remove(path);
  return rename(tmp, path) == 0;
}

void wipe(void* p, size_t n) {
  volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
}

char* alloc(size_t n) { return static_cast<char*>(heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM)); }

// Dekryptér en eksportfil til en ny buffer (nul-termineret). nullptr ved fejl.
char* decryptFile(const char* path, const char* password, size_t* out_n, char* err, size_t en) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    snprintf(err, en, "Kan ikke åbne filen");
    return nullptr;
  }
  char* raw = alloc(kMax + 64);
  const size_t n = raw ? fread(raw, 1, kMax + 64, f) : 0;
  fclose(f);
  if (!raw || n < 4 + 16 + 12 + 16 || (memcmp(raw, "DMX1", 4) != 0 && memcmp(raw, "DMX2", 4) != 0)) {
    snprintf(err, en, "Ikke en DualMesh-profil");
    heap_caps_free(raw);
    return nullptr;
  }
  uint8_t key[32];
  // DMX2 = PBKDF2; DMX1 = gammel afledning (gamle eksportfiler kan stadig importeres).
  sdcrypt::deriveKey(password, reinterpret_cast<uint8_t*>(raw + 4), key,
                     raw[3] == '2' ? sdcrypt::kKdfPbkdf2 : sdcrypt::kKdfOld);
  const size_t clen = n - 4 - 16 - 12 - 16;
  char* plain = alloc(clen + 1);
  GCM<AES256> g;
  g.setKey(key, 32);
  g.setIV(reinterpret_cast<uint8_t*>(raw + 20), 12);
  g.addAuthData(raw, 4);
  g.decrypt(reinterpret_cast<uint8_t*>(plain), reinterpret_cast<uint8_t*>(raw + 32), clen);
  const bool ok = g.checkTag(raw + 32 + clen, 16);
  wipe(key, sizeof(key));
  heap_caps_free(raw);
  if (!ok || strncmp(plain, "DUALMESH-PROFIL 1\n", 18) != 0) {
    snprintf(err, en, "Forkert adgangskode");
    wipe(plain, clen);
    heap_caps_free(plain);
    return nullptr;
  }
  plain[clen] = 0;
  *out_n = clen;
  return plain;
}

// Gennemløb indholdet: header-linjer (navn=, kortnavn=) og "=== FIL rel len ===".
// write = true: skriv filerne.
bool parse(char* p, size_t n, Info& info, bool write, char* err, size_t en) {
  char* end = p + n;
  char* line = p + 18;
  bool all_ok = true;
  while (line < end) {
    char* nl = static_cast<char*>(memchr(line, '\n', end - line));
    if (!nl) break;
    *nl = 0;
    if (!strncmp(line, "navn=", 5)) {
      snprintf(info.name, sizeof(info.name), "%s", line + 5);
    } else if (!strncmp(line, "kortnavn=", 9)) {
      snprintf(info.short_name, sizeof(info.short_name), "%s", line + 9);
    } else if (!strncmp(line, "=== FIL ", 8)) {
      char rel[48];
      unsigned long len = 0;
      if (sscanf(line + 8, "%47s %lu", rel, &len) != 2 || nl + 1 + len > end) {
        snprintf(err, en, "Profilen er beskadiget");
        return false;
      }
      const char* data = nl + 1;
      for (const Part& part : kParts)
        if (!strcmp(part.rel, rel)) {
          ++info.files;
          if (!strcmp(rel, "meshcore/identity.txt")) info.identity = strstr(data, "id=") != nullptr;
          if (!strcmp(rel, "wifi.txt"))
            for (const char* q = data; q < data + len; ++q)
              if (*q == '\n' && q + 1 < data + len && q[1] != ';') ++info.wifi;
          if (write && !writePart(part, data, len)) {
            DLOG.printf("[PROFIL] kunne ikke skrive %s\n", rel);
            all_ok = false;
          }
        }
      line = const_cast<char*>(data) + len;
      continue;
    } else if (!strcmp(line, "=== SLUT ===")) {
      break;
    }
    line = nl + 1;
  }
  if (!all_ok) snprintf(err, en, "Nogle filer kunne ikke skrives");
  return all_ok;
}
}  // namespace

bool exportTo(SdStore* store, const char* password, char* out_path, size_t n, char* err, size_t en) {
  if (!password || strlen(password) < 6) {
    snprintf(err, en, "Adgangskoden skal have mindst 6 tegn");
    return false;
  }
  char* plain = alloc(kMax);
  char* part = alloc(kMax);
  if (!plain || !part) {
    snprintf(err, en, "Ikke nok hukommelse");
    heap_caps_free(plain);
    heap_caps_free(part);
    return false;
  }
  const char* name = store ? store->get("meshcore_name") : "";
  const char* sname = store ? store->get("meshtastic_short_name") : "";
  size_t len = snprintf(plain, kMax, "DUALMESH-PROFIL 1\nnavn=%s\nkortnavn=%s\n", name ? name : "",
                        sname ? sname : "");
  bool fits = true;
  for (const Part& p : kParts) {
    const long pl = readPart(p, part, kMax);
    if (pl < 0) continue;  // findes ikke (fx ingen WiFi gemt)
    char hdr[80];
    const int hl = snprintf(hdr, sizeof(hdr), "=== FIL %s %ld ===\n", p.rel, pl);
    if (len + hl + pl + 16 >= kMax) {
      fits = false;
      break;
    }
    memcpy(plain + len, hdr, hl);
    len += hl;
    memcpy(plain + len, part, pl);
    len += pl;
  }
  len += snprintf(plain + len, kMax - len, "=== SLUT ===\n");
  wipe(part, kMax);
  heap_caps_free(part);
  if (!fits) {
    snprintf(err, en, "Profilen er for stor");
    wipe(plain, kMax);
    heap_caps_free(plain);
    return false;
  }
  // Kryptér
  uint8_t salt[16], iv[12], key[32], tag[16];
  esp_fill_random(salt, sizeof(salt));
  esp_fill_random(iv, sizeof(iv));
  sdcrypt::deriveKey(password, salt, key, sdcrypt::kKdfPbkdf2);
  uint8_t* ct = reinterpret_cast<uint8_t*>(alloc(len));
  GCM<AES256> g;
  g.setKey(key, 32);
  g.setIV(iv, 12);
  g.addAuthData("DMX2", 4);
  g.encrypt(ct, reinterpret_cast<uint8_t*>(plain), len);
  g.computeTag(tag, 16);
  wipe(key, sizeof(key));
  wipe(plain, kMax);
  heap_caps_free(plain);
  // Filnavn: navn (kun bogstaver/tal) + dato
  char safe[24] = "";
  size_t k = 0;
  for (const char* c = name && *name ? name : "DualMesh"; *c && k + 1 < sizeof(safe); ++c)
    if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' ||
        *c == '_')
      safe[k++] = *c;
  safe[k] = 0;
  char stamp[20] = "";
  const time_t t = wallclock::now();
  if (t) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(stamp, sizeof(stamp), "-%Y%m%d-%H%M", &tm);
  }
  mkdir("/sd/dualmesh", 0775);
  mkdir(kExportDir, 0775);
  snprintf(out_path, n, "%s/%s%s.dmp", kExportDir, safe, stamp);
  FILE* f = fopen(out_path, "wb");
  bool ok = f && fwrite("DMX2", 1, 4, f) == 4 && fwrite(salt, 1, 16, f) == 16 && fwrite(iv, 1, 12, f) == 12 &&
            fwrite(ct, 1, len, f) == len && fwrite(tag, 1, 16, f) == 16;
  if (f) {
    ok = ok && fflush(f) == 0;
    fclose(f);
  }
  heap_caps_free(ct);
  if (!ok) snprintf(err, en, "Kan ikke skrive filen");
  else DLOG.printf("[PROFIL] eksporteret til %s (%u byte)\n", out_path, unsigned(len));
  return ok;
}

bool retireIdentity(SdStore* store) {
  const char* id = "/sd/dualmesh/meshcore/identity.txt";
  sdcrypt::clearUnreadable(id);
  const bool ok = remove(id) == 0;
  if (store) {
    store->set("meshcore_name", "");
    store->set("meshtastic_short_name", "");
    store->save();
  }
  DLOG.printf("[PROFIL] gammel identitet slettet efter eksport (%s) - ny laves ved genstart\n",
              ok ? "OK" : "FEJL");
  return ok;
}

int listFiles(char (*out)[96], int max) {
  int n = 0;
  for (const char* dir : {kExportDir, "/sd"}) {
    DIR* d = opendir(dir);
    if (!d) continue;
    while (dirent* e = readdir(d)) {
      const size_t l = strlen(e->d_name);
      if (l < 5 || strcasecmp(e->d_name + l - 4, ".dmp") != 0 || n >= max) continue;
      snprintf(out[n++], 96, "%s/%s", dir, e->d_name);
    }
    closedir(d);
  }
  return n;
}

bool peek(const char* path, const char* password, Info& info, char* err, size_t en) {
  size_t n = 0;
  char* p = decryptFile(path, password, &n, err, en);
  if (!p) return false;
  info = Info{};
  const bool ok = parse(p, n, info, false, err, en);
  wipe(p, n);
  heap_caps_free(p);
  return ok;
}

bool importFrom(SdStore* store, const char* path, const char* password, Info& info, char* err, size_t en) {
  size_t n = 0;
  char* p = decryptFile(path, password, &n, err, en);
  if (!p) return false;
  info = Info{};
  const bool ok = parse(p, n, info, true, err, en);
  wipe(p, n);
  heap_caps_free(p);
  if (store) {
    store->set("meshcore_name", info.name);
    store->set("meshtastic_short_name", info.short_name);
    store->save();
  }
  DLOG.printf("[PROFIL] importeret fra %s: \"%s\", %d filer%s\n", path, info.name, info.files,
              ok ? "" : " (MED FEJL)");
  return ok;
}

}  // namespace profile_io
}  // namespace dm
