// SPDX-License-Identifier: MIT
#include "ui/contact_colors.h"

#include <Arduino.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "net/meshcore_node.h"
#include "net/msg_store.h"
#include "storage/sd_log.h"

namespace dm {
namespace ccolor {

namespace {
// Lyse farve på sort, forskellige fra vores egne beskeder (grøn/cyan).
struct C {
  uint16_t rgb;
  const char* name;
};
const C kPal[kCount] = {
    {0xFFE0, "gul"},      {0xFD20, "orange"},    {0xFB2C, "laks"},    {0xF81F, "magenta"},
    {0xA45F, "violet"},   {0x5D9F, "lyseblå"},   {0xAFE5, "lime"},    {0xFEA0, "guld"},
    {0xFC9F, "pink"},     {0x9FF3, "mint"},      {0xFDB8, "fersken"}, {0x867F, "himmelblå"},
    {0xFFFF, "hvid"},
};
constexpr const char* kFile = "/sd/dualmesh/meshcore/colors.txt";
constexpr const char* kTmp = "/sd/dualmesh/meshcore/colors.tmp";
constexpr int kMaxCustom = 64, kMaxNames = 400;

struct Custom {
  uint8_t key[6];
  int8_t idx;
};
Custom g_custom[kMaxCustom];
int g_n_custom = 0;
bool g_sd = false;

struct NameEntry {
  uint32_t name_hash;
  uint8_t key[6];
  uint8_t count;
  uint32_t lastmod;  // for den valgte (nyeste) nøgle med navnet
};
NameEntry* g_names = nullptr;  // PSRAM
int g_n_names = 0;

void save() {
  if (!g_sd) return;
  mkdir("/sd/dualmesh/meshcore", 0775);
  FILE* f = fopen(kTmp, "w");
  if (!f) return;
  fprintf(f, "; DualMesh kontaktfarver: noegle (6 byte hex)<TAB>farvenummer 0-%d\n", kCount - 1);
  for (int i = 0; i < g_n_custom; ++i) {
    const uint8_t* k = g_custom[i].key;
    fprintf(f, "%02x%02x%02x%02x%02x%02x\t%d\n", k[0], k[1], k[2], k[3], k[4], k[5], g_custom[i].idx);
  }
  const bool ok = fflush(f) == 0 && !ferror(f);
  fclose(f);
  if (!ok) return;
  remove(kFile);
  rename(kTmp, kFile);
}
}  // namespace

uint16_t rgb(int idx) { return kPal[(idx % kCount + kCount) % kCount].rgb; }
const char* name(int idx) { return kPal[(idx % kCount + kCount) % kCount].name; }

void load(bool sd) {
  g_sd = sd;
  g_n_custom = 0;
  if (!sd) return;
  FILE* f = fopen(kFile, "r");
  if (!f) return;
  char line[64];
  while (fgets(line, sizeof(line), f) && g_n_custom < kMaxCustom) {
    if (line[0] == ';' || strlen(line) < 14) continue;
    Custom& c = g_custom[g_n_custom];
    bool ok = true;
    for (int i = 0; i < 6 && ok; ++i) {
      char b[3] = {line[2 * i], line[2 * i + 1], 0};
      char* e;
      c.key[i] = uint8_t(strtoul(b, &e, 16));
      ok = !*e;
    }
    c.idx = int8_t(atoi(line + 13));
    if (ok && c.idx >= 0 && c.idx < kCount) ++g_n_custom;
  }
  fclose(f);
}

int autoFor(const uint8_t* pub6) { return int(fnv1a(pub6, 6) % kCount); }

bool custom(const uint8_t* pub6) {
  for (int i = 0; i < g_n_custom; ++i)
    if (!memcmp(g_custom[i].key, pub6, 6)) return true;
  return false;
}

int forKey(const uint8_t* pub6) {
  for (int i = 0; i < g_n_custom; ++i)
    if (!memcmp(g_custom[i].key, pub6, 6)) return g_custom[i].idx;
  return autoFor(pub6);
}

static uint32_t g_gen = 0;
uint32_t generation() { return g_gen; }

void set(const uint8_t* pub6, int idx) {
  ++g_gen;
  int i = 0;
  while (i < g_n_custom && memcmp(g_custom[i].key, pub6, 6)) ++i;
  if (idx < 0 || idx == autoFor(pub6)) {  // automatisk: fjern valget
    if (i < g_n_custom) g_custom[i] = g_custom[--g_n_custom];
  } else {
    if (i == g_n_custom) {
      if (g_n_custom >= kMaxCustom) return;
      ++g_n_custom;
    }
    memcpy(g_custom[i].key, pub6, 6);
    g_custom[i].idx = int8_t(idx);
  }
  save();
}

void refresh(McNode* node) {
  if (!node) return;
  if (!g_names) g_names = static_cast<NameEntry*>(heap_caps_calloc(kMaxNames, sizeof(NameEntry), MALLOC_CAP_SPIRAM));
  if (!g_names) return;
  int n = 0;
  ContactsIterator it = node->startContactsIterator();
  ContactInfo c;
  while (n < kMaxNames && it.hasNext(node, c)) {
    const uint32_t h = fnv1a(c.name, strlen(c.name));
    int k = 0;
    while (k < n && g_names[k].name_hash != h) ++k;
    if (k == n) {
      g_names[n] = {h, {}, 0, c.lastmod};
      memcpy(g_names[n].key, c.id.pub_key, 6);
      ++n;
    } else if (c.lastmod > g_names[k].lastmod) {  // nyeste identitet med navnet bestemmer farven
      memcpy(g_names[k].key, c.id.pub_key, 6);
      g_names[k].lastmod = c.lastmod;
    }
    if (g_names[k].count < 255) ++g_names[k].count;
  }
  uint32_t h = 2166136261u ^ uint32_t(n);
  for (int i = 0; i < n; ++i)
    for (int b = 0; b < int(sizeof(NameEntry)); ++b)
      h = (h ^ reinterpret_cast<const uint8_t*>(&g_names[i])[b]) * 16777619u;
  static uint32_t last = 0;
  if (h != last) {
    last = h;
    ++g_gen;
  }
  g_n_names = n;
}

int forName(const char* nm, bool* dup, bool* known) {
  const uint32_t h = fnv1a(nm, strlen(nm));
  for (int i = 0; i < g_n_names; ++i)
    if (g_names[i].name_hash == h) {
      if (dup) *dup = g_names[i].count > 1;
      if (known) *known = true;
      return forKey(g_names[i].key);
    }
  if (dup) *dup = false;
  if (known) *known = false;
  // Ukendt afsender (ingen advert hørt): farve ud fra navnet.
  uint8_t k[6] = {};
  const uint32_t a = h, b = h * 2654435761u;
  memcpy(k, &a, 4);
  memcpy(k + 4, &b, 2);
  return autoFor(k);
}

}  // namespace ccolor
}  // namespace dm
