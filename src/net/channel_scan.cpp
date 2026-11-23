// SPDX-License-Identifier: MIT
#include "net/channel_scan.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include <sys/stat.h>

#include "net/msg_store.h"
#include "storage/sd_crypt.h"
#include "storage/sd_log.h"
#include "util/b64.h"

namespace dm {

namespace {
// Meshtastics standardnøgle (PSK-indeks 1, offentligt dokumenteret).
const uint8_t kMtDefaultKey[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                   0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

uint8_t xorBytes(const uint8_t* p, size_t n) {
  uint8_t x = 0;
  for (size_t i = 0; i < n; ++i) x ^= p[i];
  return x;
}
}  // namespace

static constexpr const char* kMtDir = "/sd/dualmesh/meshtastic";
static constexpr const char* kMtFile = "/sd/dualmesh/meshtastic/channels.txt";

void ChannelScan::begin(bool sd_mounted, const char* mt_primary, MsgStore* store) {
  n_mtc_ = 0;
  sd_ = sd_mounted;
  store_ = store;
  // Primær (standardnøgle) + dem fra SD.
  snprintf(mt_primary_, sizeof(mt_primary_), "%s", mt_primary && *mt_primary ? mt_primary : "LongFast");
  const char* note = nullptr;
  MtChannel c{};
  if (parseMt(mt_primary_, "AQ==", c, &note)) mtc_[n_mtc_++] = c;
  if (sd_) {
    if (FILE* f = sdcrypt::openRead(kMtFile)) {
      char line[128];
      while (std::fgets(line, sizeof(line), f) && n_mtc_ < kMaxMt) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\r\n")] = 0;
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        MtChannel m{};
        if (parseMt(line, tab + 1, m, &note)) mtc_[n_mtc_++] = m;
      }
      sdcrypt::closeRead(f);
    }
  }
  DLOG.printf("[KANAL] %d Meshtastic-kanaler (primaer: %s)\n", n_mtc_, mt_primary_);
  version_++;
}

bool ChannelScan::parseMt(const char* name, const char* psk_b64, MtChannel& out,
                          const char** note) const {
  memset(&out, 0, sizeof(out));
  if (!name || !*name || strlen(name) > 11) {
    *note = "Navn: 1-11 tegn";
    return false;
  }
  snprintf(out.name, sizeof(out.name), "%s", name);
  snprintf(out.psk_b64, sizeof(out.psk_b64), "%s", psk_b64 && *psk_b64 ? psk_b64 : "AQ==");
  uint8_t raw[32];
  const int n = b64Decode(out.psk_b64, raw, sizeof(raw));
  if (n == 0 || (n == 1 && raw[0] == 0)) {
    out.klen = 0;  // ingen kryptering
  } else if (n == 1) {  // kort nøgle: standardnøglen med sidste byte forskudt
    memcpy(out.key, kMtDefaultKey, 16);
    out.key[15] = uint8_t(out.key[15] + raw[0] - 1);
    out.klen = 16;
  } else if (n == 16 || n == 32) {
    memcpy(out.key, raw, size_t(n));
    out.klen = uint8_t(n);
  } else {
    *note = "PSK: base64 af 1, 16 eller 32 byte";
    return false;
  }
  out.hash = xorBytes(reinterpret_cast<const uint8_t*>(out.name), strlen(out.name)) ^
             xorBytes(out.key, out.klen);
  const uint32_t h = fnv1a(out.key, out.klen, fnv1a(out.name, strlen(out.name)));
  snprintf(out.conv, sizeof(out.conv), "mtc_%08lx", (unsigned long)h);
  return true;
}

bool ChannelScan::saveMt() const {
  if (!sd_) return false;
  mkdir("/sd/dualmesh", 0775);
  mkdir(kMtDir, 0775);
  FILE* f = sdcrypt::openWrite();
  if (!f) return false;
  std::fprintf(f, "# DualMesh Meshtastic-kanaler (den primaere er i settings.ini). "
                  "navn<TAB>psk-base64. HEMMELIG.\n");
  for (int i = 1; i < n_mtc_; ++i) std::fprintf(f, "%s\t%s\n", mtc_[i].name, mtc_[i].psk_b64);
  return sdcrypt::commit(f, kMtFile);
}

int ChannelScan::addMtChannel(const char* name, const char* psk_b64, const char** note) {
  static const char* dummy;
  if (!note) note = &dummy;
  MtChannel c{};
  if (!parseMt(name, psk_b64, c, note)) return -1;
  for (int i = 0; i < n_mtc_; ++i)
    if (strcmp(mtc_[i].conv, c.conv) == 0) {
      *note = "Kanalen findes allerede";
      return i;
    }
  if (n_mtc_ >= kMaxMt) {
    *note = "Højst 8 Meshtastic-kanaler";
    return -1;
  }
  mtc_[n_mtc_++] = c;
  *note = !sd_ ? "Tilføjet (gemmes ikke: intet SD)"
               : (saveMt() ? "Kanal tilføjet og gemt" : "Tilføjet, men IKKE gemt");
  DLOG.printf("[KANAL] Meshtastic-kanal tilfoejet: %s (hash 0x%02X)\n", c.name, c.hash);
  version_++;
  return n_mtc_ - 1;
}

bool ChannelScan::removeMtChannel(int i) {
  if (i < 1 || i >= n_mtc_) return false;  // den primære styres i Indst.
  char name[24];
  snprintf(name, sizeof(name), "%s", mtc_[i].name);
  for (int k = i; k + 1 < n_mtc_; ++k) mtc_[k] = mtc_[k + 1];
  --n_mtc_;
  mtc_[n_mtc_] = MtChannel{};
  const bool saved = saveMt();
  DLOG.printf("[KANAL] Meshtastic-kanal slettet: %s (%s)\n", name, saved ? "gemt" : sd_ ? "IKKE gemt" : "intet SD");
  version_++;
  return true;
}

void ChannelScan::dump() const {
  DLOG.printf("[KANAL] %d Meshtastic-kanaler:\n", n_mtc_);
  for (int i = 0; i < n_mtc_; ++i)
    DLOG.printf("[KANAL]   %d %-11s hash 0x%02X %s%s\n", i, mtc_[i].name, mtc_[i].hash,
                mtc_[i].klen ? (mtc_[i].klen == 16 ? "AES-128" : "AES-256") : "ukrypteret",
                i == 0 ? " (primaer)" : "");
}

}  // namespace dm
