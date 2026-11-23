// SPDX-License-Identifier: MIT
#include "storage/sd_store.h"

#include <dirent.h>
#include <errno.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "driver/sdspi_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "storage/sd_fastwrite.h"
#include "storage/sd_log.h"

namespace dm {

static constexpr const char* kRoot = "/sd";
static constexpr const char* kDir = "/sd/dualmesh";
static constexpr const char* kFile = "/sd/dualmesh/settings.ini";
static constexpr const char* kTmp = "/sd/dualmesh/settings.tmp";
static constexpr const char* kBak = "/sd/dualmesh/settings.bak";

SdState SdStore::begin(spi_host_device_t host, int cs_pin, int max_khz) {
  host_ = host;
  cs_ = cs_pin;
  khz_ = max_khz;
  if (mount(false)) return state_ = SdState::Ready;
  probe();  // hvorfor kunne det ikke monteres?
  if (state_ == SdState::NotStarted) {
    // Helt tomt kort: formatér uden at spørge (der er intet at miste).
    if (mount(true)) {
      auto_formatted_ = true;
      return state_ = SdState::Ready;
    }
    state_ = SdState::Error;
  }
  return state_;
}

bool SdStore::mount(bool format_if_unreadable) {
  sdmmc_host_t h = SDSPI_HOST_DEFAULT();
  h.slot = host_;
  h.max_freq_khz = khz_;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.host_id = host_;
  dev.gpio_cs = gpio_num_t(cs_);
  esp_vfs_fat_mount_config_t mc = {};
  mc.format_if_mount_failed = format_if_unreadable;
  mc.max_files = 4;
  mc.allocation_unit_size = 32 * 1024;  // store klynger: hurtigere til kortfliser/arkiv
  sdmmc_card_t* card = nullptr;
  last_err_ = esp_vfs_fat_sdspi_mount(kRoot, &h, &dev, &mc, &card);
  if (last_err_ != ESP_OK) return false;
  card_ = card;
  // Egen skrivedriver: bussen slippes, mens kortet skriver (radioen må ikke vente).
  const int khz = card->max_freq_khz && card->max_freq_khz < khz_ ? card->max_freq_khz : khz_;
  if (!sdfw::install(card, host_, cs_, khz)) DLOG.println("[SD] egen skrivedriver kunne ikke startes - bruger ESP-IDF's");
  mkdir(kDir, 0775);  // findes den allerede, er det fint
  return true;
}

static uint32_t rd32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

// Navngiver filsystemet i en boot-sektor og forklarer, hvorfor FAT ikke kunne monteres.
void SdStore::classifyVbr(const uint8_t* v) {
  const size_t n = std::strlen(diag_);
  if (!std::memcmp(v + 3, "EXFAT   ", 8)) { fs_name_ = "exFAT"; return; }
  if (!std::memcmp(v + 3, "NTFS    ", 8)) { fs_name_ = "NTFS"; return; }
  const uint16_t bps = rd16(v + 11);
  const uint8_t spc = v[13];
  const uint16_t rsv = rd16(v + 14);
  const uint8_t nfat = v[16];
  const uint16_t root = rd16(v + 17);
  const uint32_t tot = rd16(v + 19) ? rd16(v + 19) : rd32(v + 32);
  const uint32_t fsz = rd16(v + 22) ? rd16(v + 22) : rd32(v + 36);
  if (!bps || !spc) { fs_name_ = "ukendt"; return; }
  const uint32_t root_secs = (uint32_t(root) * 32 + bps - 1) / bps;
  const uint32_t data = tot - rsv - nfat * fsz - root_secs;
  const uint32_t clusters = data / spc;
  const bool is32 = !std::memcmp(v + 82, "FAT32   ", 8);
  std::snprintf(diag_ + n, sizeof(diag_) - n,
                "oem='%.8s' %s, %u B/sektor, %u KB klynger, %lu klynger", v + 3,
                is32 ? "FAT32" : "FAT12/16", bps, unsigned(spc) * bps / 1024,
                (unsigned long)clusters);
  // FatFs (og FAT-specifikationen) afgør FAT-typen ud fra antal klynger:
  // FAT32 kræver mindst 65 525. Linux' mkdosfs laver gerne FAT32 med færre.
  fs_name_ = (is32 && clusters < 65525) ? "FAT32 med for faa klynger (mkdosfs)"
                                         : "FAT, men kan ikke monteres";
}

// Læser kortets sektor 0 for at skelne "tomt" fra "andet filsystem".
void SdStore::probe() {
  state_ = SdState::NoCard;
  sdmmc_host_t h = SDSPI_HOST_DEFAULT();
  h.max_freq_khz = khz_;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.host_id = host_;
  dev.gpio_cs = gpio_num_t(cs_);
  sdspi_host_init();
  sdspi_dev_handle_t handle;
  const int mount_err = last_err_;
  if ((last_err_ = sdspi_host_init_device(&dev, &handle)) != ESP_OK) return;
  h.slot = handle;
  sdmmc_card_t card = {};
  uint8_t* sec = static_cast<uint8_t*>(heap_caps_malloc(512, MALLOC_CAP_DMA));
  if (sec && (last_err_ = sdmmc_card_init(&h, &card)) == ESP_OK &&
      (last_err_ = sdmmc_read_sectors(&card, sec, 0, 1)) == ESP_OK) {
    last_err_ = mount_err;  // kortet svarer: den interessante fejl er monteringens
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
      state_ = SdState::NotStarted;  // ingen boot-signatur: tomt kort
      fs_name_ = "tomt";
    } else {
      state_ = SdState::Unsupported;
      // Superfloppy (filsystem direkte i sektor 0) eller MBR med første partition.
      uint32_t lba = 0;
      if (std::memcmp(sec + 3, "EXFAT   ", 8) && std::memcmp(sec + 3, "NTFS    ", 8) &&
          std::memcmp(sec + 82, "FAT32   ", 8) && std::memcmp(sec + 54, "FAT", 3)) {
        const uint8_t t = sec[0x1BE + 4];
        lba = rd32(sec + 0x1BE + 8);
        std::snprintf(diag_, sizeof(diag_), "MBR type 0x%02X start %lu; ", t, (unsigned long)lba);
        if (!lba || sdmmc_read_sectors(&card, sec, lba, 1) != ESP_OK) {
          fs_name_ = t == 0xEE ? "GPT" : t == 0x83 ? "Linux" : "ukendt";
          heap_caps_free(sec);
          sdspi_host_remove_device(handle);
          return;
        }
      }
      classifyVbr(sec);
    }
  }
  heap_caps_free(sec);
  sdspi_host_remove_device(handle);
}

bool SdStore::formatCard() {
  if (state_ == SdState::NoCard) return false;
  if (mounted()) {
    sdfw::uninstall();
    esp_vfs_fat_sdcard_unmount(kRoot, static_cast<sdmmc_card_t*>(card_));
    card_ = nullptr;
  }
  // Ødelæg boot-sektoren, så montering fejler og ESP-IDF formaterer forfra
  // (virker både for exFAT/NTFS og for et FAT-kort, brugeren vil have tømt).
  sdmmc_host_t h = SDSPI_HOST_DEFAULT();
  h.max_freq_khz = khz_;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.host_id = host_;
  dev.gpio_cs = gpio_num_t(cs_);
  sdspi_dev_handle_t handle;
  bool wiped = false;
  if (sdspi_host_init_device(&dev, &handle) == ESP_OK) {
    h.slot = handle;
    sdmmc_card_t card = {};
    uint8_t* sec = static_cast<uint8_t*>(heap_caps_calloc(1, 512, MALLOC_CAP_DMA));
    wiped = sec && sdmmc_card_init(&h, &card) == ESP_OK &&
            sdmmc_write_sectors(&card, sec, 0, 1) == ESP_OK;
    heap_caps_free(sec);
    sdspi_host_remove_device(handle);
  }
  if (!wiped || !mount(true)) {
    state_ = SdState::Error;
    return false;
  }
  state_ = SdState::Ready;
  fs_name_ = "";
  diag_[0] = 0;
  n_ = 0;
  dirty_ = false;
  return true;
}

static bool removeTree(const char* path) {
  DIR* d = opendir(path);
  if (!d) return std::remove(path) == 0 || errno == ENOENT;
  bool ok = true;
  char child[256];
  while (struct dirent* e = readdir(d)) {
    if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
    std::snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
    struct stat st;
    if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) ok &= removeTree(child);
    else ok &= std::remove(child) == 0;
  }
  closedir(d);
  return ok && rmdir(path) == 0;
}

static bool endsWith(const char* s, const char* suf) {
  const size_t a = std::strlen(s), b = std::strlen(suf);
  return a >= b && strcasecmp(s + a - b, suf) == 0;
}

// /dualmesh/maps/: kortpakkerne (*.pmtiles og halvt hentede *.pmtiles.part) bliver; de er store,
// skrives aldrig af DualMesh og indeholder ingen personlige data. Alt andet (gemte prikker,
// installed.txt, undermapper) slettes.
static bool clearMapsKeepPacks(const char* dir) {
  DIR* d = opendir(dir);
  if (!d) return true;
  char names[64][64];
  int n = 0;
  while (struct dirent* e = readdir(d)) {
    if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
    if (endsWith(e->d_name, ".pmtiles") || endsWith(e->d_name, ".pmtiles.part")) continue;
    if (n < 64) std::snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
  }
  closedir(d);
  bool ok = true;
  for (int i = 0; i < n; ++i) {
    char child[160];
    std::snprintf(child, sizeof(child), "%s/%s", dir, names[i]);
    ok &= removeTree(child);
  }
  return ok;
}

bool SdStore::resetDualMesh() {
  n_ = 0;
  dirty_ = false;
  if (!mounted()) return true;  // intet gemt, intet at slette
  // Alt i /dualmesh/ slettes UNDTAGEN eksporterede profiler (/dualmesh/eksport/), så en
  // profil, der er eksporteret lige før nulstillingen, kan importeres igen, og kortpakkerne i
  // /dualmesh/maps/ (store filer, man ikke selv skriver; ønske 23. nov 2026).
  bool ok = true;
  DIR* d = opendir(kDir);
  if (d) {
    char names[48][64];
    int n = 0;
    while (struct dirent* e = readdir(d))
      if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..") && std::strcmp(e->d_name, "eksport") &&
          n < 48)
        std::snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
    closedir(d);
    for (int i = 0; i < n; ++i) {
      char child[128];
      std::snprintf(child, sizeof(child), "%s/%s", kDir, names[i]);
      if (!std::strcmp(names[i], "maps")) ok &= clearMapsKeepPacks(child);
      else ok &= removeTree(child);
    }
  }
  mkdir(kDir, 0775);
  return ok;
}

const char* SdStore::status() const {
  switch (state_) {
    case SdState::Ready: return auto_formatted_ ? "OK (formateret)" : "OK";
    case SdState::NoCard: return "intet kort";
    case SdState::Unsupported: return "ukendt format";
    case SdState::Error: return "fejl";
    default: return "starter";
  }
}

int SdStore::find(const char* key) const {
  for (size_t i = 0; i < n_; ++i)
    if (std::strcmp(e_[i].key, key) == 0) return int(i);
  return -1;
}

bool SdStore::ensure() {
  if (!e_) e_ = static_cast<Entry*>(heap_caps_calloc(kMax, sizeof(Entry), MALLOC_CAP_SPIRAM));
  return e_ != nullptr;
}

bool SdStore::parse(const char* path) {
  if (!ensure()) return false;
  FILE* f = std::fopen(path, "r");
  if (!f) return false;
  char line[kKey + kVal + 8];
  n_ = 0;
  while (std::fgets(line, sizeof(line), f) && n_ < kMax) {
    char* s = line;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '#' || *s == '\r' || *s == '\n' || !*s) continue;
    char* eq = std::strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    char* v = eq + 1;
    v[std::strcspn(v, "\r\n")] = 0;
    for (char* k = eq - 1; k >= s && (*k == ' ' || *k == '\t'); --k) *k = 0;
    while (*v == ' ' || *v == '\t') ++v;
    std::strncpy(e_[n_].key, s, kKey - 1);
    std::strncpy(e_[n_].val, v, kVal - 1);
    ++n_;
  }
  std::fclose(f);
  return true;
}

bool SdStore::load() {
  if (!mounted()) return false;
  dirty_ = false;
  if (parse(kFile)) return true;
  if (parse(kBak)) {  // strømsvigt midt i en gemning: brug sidste gode kopi
    dirty_ = true;
    return true;
  }
  n_ = 0;
  return false;
}

bool SdStore::save() {
  if (!mounted() || !dirty_) return true;
  FILE* f = std::fopen(kTmp, "w");
  if (!f) return false;
  std::fprintf(f, "# DualMesh-indstillinger. Maa redigeres paa en PC (noegle=vaerdi).\n");
  for (size_t i = 0; i < n_; ++i) std::fprintf(f, "%s=%s\n", e_[i].key, e_[i].val);
  std::fflush(f);
  fsync(fileno(f));
  const bool ok = std::ferror(f) == 0;
  std::fclose(f);
  if (!ok) return false;
  // FAT kan ikke omdøbe oven i en eksisterende fil: ini -> bak, tmp -> ini.
  std::remove(kBak);
  std::rename(kFile, kBak);
  if (std::rename(kTmp, kFile) != 0) return false;
  dirty_ = false;
  return true;
}

const char* SdStore::get(const char* key) const {
  const int i = find(key);
  return i < 0 ? nullptr : e_[i].val;
}

long SdStore::getInt(const char* key, long def) const {
  const char* v = get(key);
  if (!v || !*v) return def;
  char* end = nullptr;
  const long x = std::strtol(v, &end, 10);
  return (end && *end == 0) ? x : def;
}

void SdStore::set(const char* key, const char* value) {
  if (!ensure()) return;
  int i = find(key);
  if (i < 0) {
    if (n_ >= kMax) {
      DLOG.printf("[SD] settings.ini er fuld (%u noegler) - '%s' gemmes IKKE\n", unsigned(kMax), key);
      return;
    }
    i = int(n_++);
    std::strncpy(e_[i].key, key, kKey - 1);
    e_[i].val[0] = 0;
  }
  if (std::strncmp(e_[i].val, value, kVal - 1) == 0) return;
  std::strncpy(e_[i].val, value, kVal - 1);
  e_[i].val[kVal - 1] = 0;
  dirty_ = true;
}

void SdStore::setInt(const char* key, long value) {
  char b[24];
  std::snprintf(b, sizeof(b), "%ld", value);
  set(key, b);
}

}  // namespace dm
