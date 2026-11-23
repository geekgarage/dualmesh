// SPDX-License-Identifier: MIT
// DualMesh – MeshCore radio presets (data only).
//
// Source: the official MeshCore app API, https://api.meshcore.nz/api/v1/config
// ("suggested_radio_settings"), as documented in MeshCore/docs/radio_presets.md.
// Fetched 15 Nov 2026 (27 entries). MeshCore says presets are "unlikely to be
// changed" once added, but new ones appear; the list should be refreshable
// from SD later. Preamble/sync rules from MeshCore's RadioLib wrapper (MIT).
#pragma once
#include <cstddef>
#include <cstdint>

#include "lora_profile.h"

namespace dm {
namespace mc {

struct PresetInfo {
  const char* title;       // as shown in the MeshCore app
  uint32_t freq_hz;
  uint32_t bw_hz;
  uint8_t sf, cr;          // cr 5..8 = 4/5..4/8
  uint8_t path_hash_size;  // network setting; 0 = not specified by the preset
  bool deprecated;         // still listed, but replaced by a newer preset
};

constexpr uint8_t kSyncWord = 0x12;  // RadioLib "private network" sync word

// MeshCore >= 1.16 sends 32 preamble symbols at SF <= 8, otherwise 16.
inline uint16_t preambleSyms(uint8_t sf) { return sf <= 8 ? 32 : 16; }

// The list is the built-in presets plus any loaded with loadExtraPresets(): an extra preset with
// the same title as a built-in one replaces it in place, new titles are appended.
size_t presetCount();
const PresetInfo& presetAt(size_t i);
// Exact title match, or nullptr.
const PresetInfo* findPreset(const char* title);

// Extra presets from text (on the T-Deck: /dualmesh/meshcore/presets.txt), so new MeshCore
// presets can be added without new firmware. One per line:
//   title;freq_hz;bw_hz;sf;cr[;path_hash_size[;deprecated]]
// Empty lines and lines starting with '#' are ignored. Lines with invalid values (frequency
// outside 150-960 MHz, unknown bandwidth, SF outside 5-12, CR outside 5-8, title empty, longer
// than 39 characters or containing ';') are skipped and counted in *bad.
// Returns the number of presets accepted (max kMaxExtraPresets; replaces earlier extras).
constexpr int kMaxExtraPresets = 32;
int loadExtraPresets(const char* text, int* bad = nullptr);
void clearExtraPresets();
int extraPresetCount();  // accepted by the last load (both replacements and new)

// Complete PHY profile. Older nodes (< 1.16) still send 16 preamble symbols,
// which the scheduler must plan for (min_air_preamble_syms).
LoRaProfile makeProfile(const PresetInfo& p);
// Same for custom settings entered by the user.
LoRaProfile makeProfile(uint32_t freq_hz, uint32_t bw_hz, uint8_t sf, uint8_t cr);

}  // namespace mc
}  // namespace dm
