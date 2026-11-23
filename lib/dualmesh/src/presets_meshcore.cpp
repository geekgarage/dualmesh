// SPDX-License-Identifier: MIT
#include "presets_meshcore.h"

#include <cstring>

namespace dm {
namespace mc {

// Order and titles as served by the MeshCore app API on 15 Nov 2026.
static const PresetInfo kPresets[] = {
    {"Australia", 915800000, 250000, 10, 5, 2, false},
    {"Australia (Narrow)", 916575000, 62500, 7, 7, 2, false},
    {"Australia (Mid)", 915075000, 125000, 9, 5, 2, false},
    {"Australia: SA, WA", 923125000, 62500, 8, 8, 2, false},
    {"Australia: QLD", 923125000, 62500, 8, 5, 2, false},
    {"Brazil", 923125000, 62500, 8, 8, 0, false},
    {"Canada", 910525000, 62500, 7, 5, 3, false},
    {"Costa Rica", 910525000, 125000, 11, 5, 0, false},
    {"EU/UK (Narrow)", 869618000, 62500, 8, 8, 0, false},
    {"EU/UK (Deprecated)", 869525000, 250000, 11, 5, 0, true},
    {"Czech Republic (Narrow)", 869432000, 62500, 7, 5, 2, false},
    {"EU 433MHz (Long Range)", 433650000, 250000, 11, 5, 0, false},
    {"EU 433MHz (Narrow)", 433650000, 62500, 8, 8, 0, false},
    {"Hungary", 869618000, 62500, 7, 5, 2, false},
    {"Netherlands", 869618000, 62500, 7, 5, 0, false},
    {"Netherlands (Limburg)", 869618000, 62500, 8, 8, 2, false},
    {"New Zealand (Narrow)", 917375000, 62500, 7, 5, 2, false},
    {"New Zealand (Gisborne)", 917375000, 250000, 11, 5, 1, false},
    {"Portugal 433", 433375000, 62500, 9, 6, 0, false},
    {"Portugal 868", 869618000, 62500, 7, 6, 0, false},
    {"Slovakia", 869618000, 62500, 7, 5, 2, false},
    {"Switzerland", 869618000, 62500, 8, 8, 0, false},
    {"Thailand", 921000000, 62500, 7, 5, 0, false},
    {"USA", 910525000, 62500, 7, 5, 0, false},
    {"USA - Southern California", 927875000, 62500, 7, 5, 3, false},
    {"Vietnam (Narrow)", 920250000, 62500, 8, 5, 0, false},
    {"Vietnam (Deprecated)", 920250000, 250000, 11, 5, 0, true},
};

static constexpr size_t kBuiltin = sizeof(kPresets) / sizeof(kPresets[0]);

// Extra presets (loadExtraPresets). Titles are copied, as the text buffer is temporary.
static char g_titles[kMaxExtraPresets][40];
static PresetInfo g_extra[kMaxExtraPresets];
static int g_n_extra = 0;
static int g_override[kBuiltin];  // index into g_extra replacing built-in i, or -1
static int g_appended[kMaxExtraPresets];  // indices into g_extra that are new titles
static int g_n_appended = 0;
static bool g_init = false;

static void initOverrides() {
  if (g_init) return;
  for (size_t i = 0; i < kBuiltin; ++i) g_override[i] = -1;
  g_init = true;
}

size_t presetCount() { return kBuiltin + size_t(g_n_appended); }

const PresetInfo& presetAt(size_t i) {
  initOverrides();
  if (i < kBuiltin) return g_override[i] >= 0 ? g_extra[g_override[i]] : kPresets[i];
  const size_t k = i - kBuiltin;
  return k < size_t(g_n_appended) ? g_extra[g_appended[k]] : kPresets[0];
}

const PresetInfo* findPreset(const char* title) {
  if (!title) return nullptr;
  for (size_t i = 0; i < presetCount(); ++i)
    if (std::strcmp(presetAt(i).title, title) == 0) return &presetAt(i);
  return nullptr;
}

void clearExtraPresets() {
  g_init = false;
  initOverrides();
  g_n_extra = 0;
  g_n_appended = 0;
}

int extraPresetCount() { return g_n_extra; }

namespace {
bool validBw(uint32_t bw) {
  static const uint32_t kBw[] = {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000};
  for (uint32_t b : kBw)
    if (b == bw) return true;
  return false;
}

// Næste felt (til ';' eller linjeslut); false, hvis der ikke er flere tegn på linjen.
bool field(const char*& p, const char* end, char* out, size_t cap) {
  if (p > end) return false;
  const char* s = p;
  while (p < end && *p != ';') ++p;
  size_t n = size_t(p - s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) --n;
  while (n && (*s == ' ' || *s == '\t')) ++s, --n;
  if (n >= cap) n = cap - 1;
  std::memcpy(out, s, n);
  out[n] = 0;
  ++p;  // forbi ';' (eller linjeslut)
  return true;
}

bool parseU(const char* s, uint32_t& v) {
  if (!*s) return false;
  uint64_t x = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') return false;
    x = x * 10 + uint64_t(*s - '0');
    if (x > 0xFFFFFFFFull) return false;
  }
  v = uint32_t(x);
  return true;
}
}  // namespace

int loadExtraPresets(const char* text, int* bad) {
  clearExtraPresets();
  int errors = 0;
  const char* p = text;
  while (p && *p) {
    const char* eol = std::strchr(p, '\n');
    const char* end = eol ? eol : p + std::strlen(p);
    const char* q = p;
    while (q < end && (*q == ' ' || *q == '\t')) ++q;
    if (q < end && *q != '#' && *q != '\r') {
      char f[7][48] = {};
      int nf = 0;
      const char* c = p;
      while (nf < 7 && field(c, end, f[nf], sizeof(f[nf]))) ++nf;
      uint32_t freq = 0, bw = 0, sf = 0, cr = 0, phs = 0, dep = 0;
      const bool ok = nf >= 5 && f[0][0] && std::strlen(f[0]) < sizeof(g_titles[0]) && parseU(f[1], freq) &&
                      parseU(f[2], bw) && parseU(f[3], sf) && parseU(f[4], cr) &&
                      (nf < 6 || !f[5][0] || parseU(f[5], phs)) && (nf < 7 || !f[6][0] || parseU(f[6], dep)) &&
                      freq >= 150000000u && freq <= 960000000u && validBw(bw) && sf >= 5 && sf <= 12 &&
                      cr >= 5 && cr <= 8 && phs <= 3 && dep <= 1 && c > end;  // c > end: højst 7 felter
      if (!ok || g_n_extra >= kMaxExtraPresets) {
        ++errors;
      } else {
        const int k = g_n_extra++;
        std::memcpy(g_titles[k], f[0], std::strlen(f[0]) + 1);
        g_extra[k] = PresetInfo{g_titles[k], freq, bw, uint8_t(sf), uint8_t(cr), uint8_t(phs), dep != 0};
        int replaced = -1;
        for (size_t i = 0; i < kBuiltin; ++i)
          if (std::strcmp(kPresets[i].title, g_titles[k]) == 0) replaced = int(i);
        if (replaced >= 0) {
          g_override[replaced] = k;
        } else {
          int dup = -1;  // samme nye titel to gange i filen: den sidste gælder
          for (int a = 0; a < g_n_appended; ++a)
            if (std::strcmp(g_extra[g_appended[a]].title, g_titles[k]) == 0) dup = a;
          if (dup >= 0) g_appended[dup] = k;
          else g_appended[g_n_appended++] = k;
        }
      }
    }
    p = eol ? eol + 1 : nullptr;
  }
  if (bad) *bad = errors;
  return g_n_extra;
}

LoRaProfile makeProfile(uint32_t freq_hz, uint32_t bw_hz, uint8_t sf, uint8_t cr) {
  LoRaProfile x;
  x.name = "MeshCore";
  x.net = NetId::MeshCore;
  x.freq_hz = freq_hz;
  x.bw_hz = bw_hz;
  x.sf = sf;
  x.cr = cr;
  x.sync_word = kSyncWord;
  x.preamble_syms = preambleSyms(sf);
  // Nodes older than 1.16 send 16 symbols even at SF <= 8.
  x.min_air_preamble_syms = x.preamble_syms > 16 ? 16 : 0;
  return x;
}

LoRaProfile makeProfile(const PresetInfo& p) { return makeProfile(p.freq_hz, p.bw_hz, p.sf, p.cr); }

}  // namespace mc
}  // namespace dm
