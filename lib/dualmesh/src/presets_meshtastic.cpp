// SPDX-License-Identifier: MIT
#include "presets_meshtastic.h"

#include <cmath>

namespace dm {
namespace mt {

// bw: sub-GHz / 2.4 GHz in Hz. 15.6 kHz is the SX126x 15.625 kHz setting.
static const PresetInfo kPresets[kPresetCount] = {
    {Preset::LONG_FAST, "LongFast", 250000, 812500, 11, 5, false},
    {Preset::LONG_SLOW, "LongSlow", 125000, 406250, 12, 8, false},
    // Deprecated: no longer offered, kept only so the enum value is known. Never allowed.
    {Preset::VERY_LONG_SLOW, "VLongSlow", 62500, 0, 12, 8, true},
    {Preset::MEDIUM_SLOW, "MediumSlow", 250000, 812500, 10, 5, false},
    {Preset::MEDIUM_FAST, "MediumFast", 250000, 812500, 9, 5, false},
    {Preset::SHORT_SLOW, "ShortSlow", 250000, 812500, 8, 5, false},
    {Preset::SHORT_FAST, "ShortFast", 250000, 812500, 7, 5, false},
    {Preset::LONG_MODERATE, "LongMod", 125000, 406250, 11, 8, false},
    {Preset::SHORT_TURBO, "ShortTurbo", 500000, 1625000, 7, 5, false},
    {Preset::LONG_TURBO, "LongTurbo", 500000, 1625000, 11, 8, false},
    {Preset::LITE_FAST, "LiteFast", 125000, 125000, 9, 5, false},
    {Preset::LITE_SLOW, "LiteSlow", 125000, 125000, 10, 5, false},
    {Preset::NARROW_FAST, "NarrowFast", 62500, 62500, 7, 6, false},
    {Preset::NARROW_SLOW, "NarrowSlow", 62500, 62500, 8, 6, false},
    {Preset::TINY_FAST, "TinyFast", 15625, 15625, 7, 5, false},
    {Preset::TINY_SLOW, "TinySlow", 15625, 15625, 8, 6, false},
    {Preset::MEDIUM_TURBO, "MediumTurbo", 500000, 1625000, 9, 5, false},
};

struct PlanInfo {
  double spacing_mhz;  // gap between slots, also at the start of the band
  double padding_mhz;  // gap on each side of a slot
  bool licensed_only;
};
static const PlanInfo kPlans[] = {
    /* Std     */ {0.0, 0.0, false},
    /* Eu868   */ {0.0, 0.0, false},
    /* Lite    */ {0.4, 0.0375, false},
    /* Narrow  */ {0.0, 0.0104, false},
    /* Ham20k  */ {0.0, 0.0022, true},   // 15.6 kHz occupying a 20 kHz channel
    /* Ham100k */ {0.0, 0.01875, true},  // 62.5 kHz occupying a 100 kHz channel
    /* Unset   */ {0.0, 0.0, false},
};

using R = Region;
using P = Preset;
// Unlisted regions (UA_868, EU_874, EU_917) exist in the enum but are not
// offered by current firmware or docs: no band is known, so they never yield a
// profile (freq 0..0, listed = false).
static const RegionInfo kRegions[] = {
    {R::US, "US", 902.0, 928.0, 100, 30, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::EU_433, "EU_433", 433.0, 434.0, 10, 10, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::EU_868, "EU_868", 869.4, 869.65, 10, 27, false, Plan::Eu868, P::LONG_FAST, 0, true},
    {R::EU_866, "EU_866", 865.6, 867.6, 2.5f, 27, false, Plan::Lite, P::LITE_FAST, 0, true},
    {R::EU_N_868, "EU_N_868", 869.4, 869.65, 10, 27, false, Plan::Narrow, P::NARROW_SLOW, 1, true},
    {R::CN, "CN", 470.0, 510.0, 100, 19, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::JP, "JP", 920.5, 923.5, 100, 13, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::ANZ, "ANZ", 915.0, 928.0, 100, 30, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::ANZ_433, "ANZ_433", 433.05, 434.79, 100, 14, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::RU, "RU", 868.7, 869.2, 100, 20, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::KR, "KR", 920.0, 923.0, 100, 23, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::TW, "TW", 920.0, 925.0, 100, 27, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::IN, "IN", 865.0, 867.0, 100, 30, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::NZ_865, "NZ_865", 864.0, 868.0, 100, 36, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::TH, "TH", 920.0, 925.0, 10, 27, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::UA_433, "UA_433", 433.0, 434.7, 10, 10, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::MY_433, "MY_433", 433.0, 435.0, 100, 20, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::MY_919, "MY_919", 919.0, 924.0, 100, 27, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::SG_923, "SG_923", 917.0, 925.0, 100, 20, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::PH_433, "PH_433", 433.0, 434.7, 100, 10, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::PH_868, "PH_868", 868.0, 869.4, 100, 14, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::PH_915, "PH_915", 915.0, 918.0, 100, 24, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::KZ_433, "KZ_433", 433.075, 434.775, 100, 10, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::KZ_863, "KZ_863", 863.0, 868.0, 100, 30, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::NP_865, "NP_865", 865.0, 868.0, 100, 30, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::BR_902, "BR_902", 902.0, 907.5, 100, 30, false, Plan::Std, P::LONG_FAST, 0, true},
    {R::ITU1_2M, "ITU1_2M", 144.0, 146.0, 100, 30, false, Plan::Ham20k, P::TINY_FAST, 26, true},
    {R::ITU2_2M, "ITU2_2M", 144.0, 148.0, 100, 30, false, Plan::Ham20k, P::TINY_FAST, 51, true},
    {R::ITU3_2M, "ITU3_2M", 144.0, 148.0, 100, 30, false, Plan::Ham20k, P::TINY_FAST, 33, true},
    {R::ITU2_125CM, "ITU2_125CM", 220.0, 225.0, 100, 30, false, Plan::Ham100k, P::NARROW_SLOW, 37, true},
    {R::ITU1_70CM, "ITU1_70CM", 430.0, 440.0, 100, 30, false, Plan::Ham100k, P::NARROW_SLOW, 37, true},
    {R::ITU2_70CM, "ITU2_70CM", 420.0, 450.0, 100, 30, false, Plan::Ham100k, P::NARROW_SLOW, 137, true},
    {R::ITU3_70CM, "ITU3_70CM", 430.0, 450.0, 100, 30, false, Plan::Ham100k, P::NARROW_SLOW, 37, true},
    {R::LORA_24, "LORA_24", 2400.0, 2483.5, 100, 10, true, Plan::Std, P::LONG_FAST, 0, true},
    {R::UA_868, "UA_868", 0, 0, 100, 0, false, Plan::Std, P::LONG_FAST, 0, false},
    {R::EU_874, "EU_874", 0, 0, 100, 0, false, Plan::Std, P::LONG_FAST, 0, false},
    {R::EU_917, "EU_917", 0, 0, 100, 0, false, Plan::Std, P::LONG_FAST, 0, false},
    {R::UNSET, "UNSET", 902.0, 928.0, 100, 30, false, Plan::Unset, P::LONG_FAST, 0, false},
};

const PresetInfo* preset(Preset p) {
  for (const auto& x : kPresets)
    if (x.id == p) return &x;
  return nullptr;
}

const RegionInfo* region(Region r) {
  for (const auto& x : kRegions)
    if (x.id == r) return &x;
  return nullptr;
}

size_t regionCount() { return sizeof(kRegions) / sizeof(kRegions[0]); }
const RegionInfo& regionAt(size_t i) { return kRegions[i]; }

static const char* const kPresetEnumNames[kPresetCount] = {
    "LONG_FAST",   "LONG_SLOW",   "VERY_LONG_SLOW", "MEDIUM_SLOW", "MEDIUM_FAST", "SHORT_SLOW",
    "SHORT_FAST",  "LONG_MODERATE", "SHORT_TURBO", "LONG_TURBO",  "LITE_FAST",   "LITE_SLOW",
    "NARROW_FAST", "NARROW_SLOW", "TINY_FAST",      "TINY_SLOW",   "MEDIUM_TURBO"};

const char* presetEnumName(Preset p) {
  return size_t(p) < kPresetCount ? kPresetEnumNames[size_t(p)] : "?";
}

static bool sameNoCase(const char* a, const char* b) {
  if (!a || !b) return false;
  for (; *a && *b; ++a, ++b) {
    char x = *a, y = *b;
    if (x >= 'a' && x <= 'z') x = char(x - 32);
    if (y >= 'a' && y <= 'z') y = char(y - 32);
    if (x != y) return false;
  }
  return *a == *b;
}

const RegionInfo* regionByName(const char* name) {
  for (const auto& r : kRegions)
    if (sameNoCase(r.name, name)) return &r;
  return nullptr;
}

const PresetInfo* presetByName(const char* name) {
  for (const auto& p : kPresets)
    if (sameNoCase(p.name, name) || sameNoCase(presetEnumName(p.id), name)) return &p;
  return nullptr;
}

bool planLicensedOnly(Plan p) { return kPlans[size_t(p)].licensed_only; }

bool regionAllowsPreset(Region r, Preset p) {
  const RegionInfo* ri = region(r);
  const PresetInfo* pi = preset(p);
  if (!ri || !pi || pi->deprecated) return false;
  switch (ri->plan) {
    case Plan::Std:
      return p != P::LITE_FAST && p != P::LITE_SLOW && p != P::NARROW_FAST &&
             p != P::NARROW_SLOW && p != P::TINY_FAST && p != P::TINY_SLOW;
    case Plan::Eu868:  // no 500 kHz presets in the 250 kHz EU band
      return p == P::LONG_FAST || p == P::LONG_SLOW || p == P::MEDIUM_SLOW ||
             p == P::MEDIUM_FAST || p == P::SHORT_SLOW || p == P::SHORT_FAST ||
             p == P::LONG_MODERATE;
    case Plan::Lite: return p == P::LITE_FAST || p == P::LITE_SLOW;
    case Plan::Narrow:
    case Plan::Ham100k: return p == P::NARROW_FAST || p == P::NARROW_SLOW;
    case Plan::Ham20k: return p == P::TINY_FAST || p == P::TINY_SLOW;
    case Plan::Unset: return p == P::LONG_FAST;
  }
  return false;
}

static double bwMhz(const RegionInfo& r, const PresetInfo& p) {
  return (r.wide_lora ? p.wide_bw_hz : p.bw_hz) / 1e6;
}

static double slotWidthMhz(const RegionInfo& r, const PresetInfo& p) {
  const PlanInfo& pl = kPlans[size_t(r.plan)];
  return pl.spacing_mhz + 2 * pl.padding_mhz + bwMhz(r, p);
}

uint32_t numSlots(Region r, Preset p) {
  const RegionInfo* ri = region(r);
  const PresetInfo* pi = preset(p);
  if (!ri || !pi) return 0;
  const double span = ri->freq_end_mhz - ri->freq_start_mhz + kPlans[size_t(ri->plan)].spacing_mhz;
  return uint32_t(std::lround(span / slotWidthMhz(*ri, *pi)));
}

double slotFrequencyMhz(Region r, Preset p, uint32_t slot0) {
  const RegionInfo* ri = region(r);
  const PresetInfo* pi = preset(p);
  if (!ri || !pi) return 0;
  const double f = ri->freq_start_mhz + bwMhz(*ri, *pi) / 2 +
                   kPlans[size_t(ri->plan)].padding_mhz + slot0 * slotWidthMhz(*ri, *pi);
  return std::round(f * 1e6) / 1e6;  // whole Hz
}

uint32_t djb2(const char* s) {
  uint32_t h = 5381;
  while (s && *s) h = h * 33u + uint8_t(*s++);
  return h;
}

uint32_t defaultSlot(Region r, Preset p, const char* channel_name) {
  const RegionInfo* ri = region(r);
  const PresetInfo* pi = preset(p);
  const uint32_t n = numSlots(r, p);
  if (!ri || !pi || n == 0) return 0;
  if (ri->override_slot > 0) return uint32_t(ri->override_slot - 1);
  const char* name = (channel_name && *channel_name) ? channel_name : pi->name;
  return djb2(name) % n;
}

static bool sx1262Bandwidth(uint32_t bw) {
  switch (bw) {
    case 7813: case 10417: case 15625: case 20833: case 31250:
    case 41667: case 62500: case 125000: case 250000: case 500000: return true;
    default: return false;
  }
}

bool sx1262Supports(Region r, Preset p) {
  const RegionInfo* ri = region(r);
  const PresetInfo* pi = preset(p);
  if (!ri || !pi || ri->wide_lora) return false;
  return ri->freq_start_mhz >= 150.0 && ri->freq_end_mhz <= 960.0 && sx1262Bandwidth(pi->bw_hz);
}

bool makeProfile(Region r, Preset p, LoRaProfile& out, const char* channel_name, uint32_t slot1) {
  const RegionInfo* ri = region(r);
  const PresetInfo* pi = preset(p);
  if (!ri || !pi || !ri->listed || !regionAllowsPreset(r, p)) return false;
  const uint32_t n = numSlots(r, p);
  if (n == 0 || slot1 > n) return false;
  const uint32_t s0 = slot1 ? slot1 - 1 : defaultSlot(r, p, channel_name);
  LoRaProfile x;
  x.name = "Meshtastic";
  x.net = NetId::Meshtastic;
  x.freq_hz = uint32_t(std::lround(slotFrequencyMhz(r, p, s0) * 1e6));
  x.bw_hz = ri->wide_lora ? pi->wide_bw_hz : pi->bw_hz;
  x.sf = pi->sf;
  x.cr = pi->cr;
  x.sync_word = kSyncWord;
  x.preamble_syms = ri->wide_lora ? kWidePreambleSyms : kPreambleSyms;
  x.min_air_preamble_syms = 0;
  out = x;
  return true;
}

}  // namespace mt
}  // namespace dm
