// SPDX-License-Identifier: MIT
// DualMesh – Meshtastic-compatible regions and modem presets (data only).
//
// The numbers are interoperability facts collected from the public Meshtastic
// documentation (meshtastic.org: LoRa configuration and frequency calculator,
// protobuf enum values) on 15 Nov 2026. No Meshtastic source code is used.
// Region and preset codes match the over-the-air/protobuf enum values, so a
// channel URL or admin message can be mapped 1:1 later.
#pragma once
#include <cstddef>
#include <cstdint>

#include "lora_profile.h"

namespace dm {
namespace mt {

// Region codes (Config.LoRaConfig.RegionCode).
enum class Region : uint8_t {
  UNSET = 0, US = 1, EU_433 = 2, EU_868 = 3, CN = 4, JP = 5, ANZ = 6, KR = 7, TW = 8,
  RU = 9, IN = 10, NZ_865 = 11, TH = 12, LORA_24 = 13, UA_433 = 14, UA_868 = 15,
  MY_433 = 16, MY_919 = 17, SG_923 = 18, PH_433 = 19, PH_868 = 20, PH_915 = 21,
  ANZ_433 = 22, KZ_433 = 23, KZ_863 = 24, NP_865 = 25, BR_902 = 26, ITU1_2M = 27,
  ITU2_2M = 28, EU_866 = 29, EU_874 = 30, EU_917 = 31, EU_N_868 = 32, ITU3_2M = 33,
  ITU1_70CM = 34, ITU2_70CM = 35, ITU3_70CM = 36, ITU2_125CM = 37,
};

// Modem presets (Config.LoRaConfig.ModemPreset).
enum class Preset : uint8_t {
  LONG_FAST = 0, LONG_SLOW = 1, VERY_LONG_SLOW = 2, MEDIUM_SLOW = 3, MEDIUM_FAST = 4,
  SHORT_SLOW = 5, SHORT_FAST = 6, LONG_MODERATE = 7, SHORT_TURBO = 8, LONG_TURBO = 9,
  LITE_FAST = 10, LITE_SLOW = 11, NARROW_FAST = 12, NARROW_SLOW = 13, TINY_FAST = 14,
  TINY_SLOW = 15, MEDIUM_TURBO = 16,
};
constexpr size_t kPresetCount = 17;

struct PresetInfo {
  Preset id;
  const char* name;      // display name; an unnamed primary channel is called this
  uint32_t bw_hz;        // sub-GHz bandwidth
  uint32_t wide_bw_hz;   // bandwidth on 2.4 GHz ("wide LoRa") regions
  uint8_t sf, cr;        // cr 5..8 = 4/5..4/8
  bool deprecated;       // in the enum, no longer offered by current firmware
};

// Channel plan shared by several regions.
enum class Plan : uint8_t { Std, Eu868, Lite, Narrow, Ham20k, Ham100k, Unset };

struct RegionInfo {
  Region id;
  const char* name;
  double freq_start_mhz, freq_end_mhz;
  float duty_cycle_pct;  // 100 = no limit
  int8_t power_limit_dbm;
  bool wide_lora;        // 2.4 GHz
  Plan plan;
  Preset default_preset;
  int16_t override_slot; // > 0: fixed 1-based default slot; 0: hash of the channel name
  bool listed;           // offered by current Meshtastic firmware/docs
};

constexpr uint8_t kSyncWord = 0x2B;
constexpr uint16_t kPreambleSyms = 16;
constexpr uint16_t kWidePreambleSyms = 12;  // 2.4 GHz regions

const PresetInfo* preset(Preset p);
const RegionInfo* region(Region r);
size_t regionCount();
const RegionInfo& regionAt(size_t i);

// Case-insensitive lookup: region "EU_868"; preset "LONG_FAST" or "LongFast".
const RegionInfo* regionByName(const char* name);
const PresetInfo* presetByName(const char* name);
const char* presetEnumName(Preset p);  // "LONG_FAST"

bool planLicensedOnly(Plan p);
// Presets a region accepts (the EU_868/EU_866/EU_N_868 trio each own a disjoint set).
bool regionAllowsPreset(Region r, Preset p);

// Channel plan maths (as documented for the Meshtastic frequency calculator).
uint32_t numSlots(Region r, Preset p);
double slotFrequencyMhz(Region r, Preset p, uint32_t slot0);  // 0-based slot
uint32_t djb2(const char* s);
// Default 0-based slot: the region's fixed slot, else hash(channel name) % slots.
// channel_name == nullptr or "" means the unnamed default channel (= preset name).
uint32_t defaultSlot(Region r, Preset p, const char* channel_name = nullptr);

// Can the T-Deck's SX1262 (150-960 MHz, its bandwidth set) operate this?
bool sx1262Supports(Region r, Preset p);

// Complete PHY profile. slot1 = 0 picks the default slot, otherwise 1-based.
// Returns false if the region/preset combination is not valid.
bool makeProfile(Region r, Preset p, LoRaProfile& out, const char* channel_name = nullptr,
                 uint32_t slot1 = 0);

}  // namespace mt
}  // namespace dm
