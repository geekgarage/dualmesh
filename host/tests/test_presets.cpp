// SPDX-License-Identifier: MIT
// Tests for the Meshtastic region/preset tables and the MeshCore preset list.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "presets_meshcore.h"
#include "presets_meshtastic.h"

using namespace dm;

static uint32_t mtFreq(mt::Region r, mt::Preset p, const char* ch = nullptr) {
  LoRaProfile x;
  const bool ok = mt::makeProfile(r, p, x, ch);
  assert(ok);
  (void)ok;
  return x.freq_hz;
}

int main() {
  using mt::Preset;
  using mt::Region;

  // Well-known Meshtastic default frequencies (unnamed primary channel).
  assert(mtFreq(Region::EU_868, Preset::LONG_FAST) == 869525000);
  assert(mtFreq(Region::US, Preset::LONG_FAST) == 906875000);
  assert(mtFreq(Region::ANZ, Preset::LONG_FAST) == 919875000);
  assert(mt::numSlots(Region::US, Preset::LONG_FAST) == 104);
  assert(mt::numSlots(Region::EU_868, Preset::LONG_FAST) == 1);

  // Narrow/Lite plans with padding and spacing.
  assert(mt::numSlots(Region::EU_N_868, Preset::NARROW_SLOW) == 3);
  assert(mtFreq(Region::EU_N_868, Preset::NARROW_SLOW) == 869441650);  // fixed slot 1
  assert(mt::numSlots(Region::EU_866, Preset::LITE_FAST) == 4);
  assert(std::fabs(mt::slotFrequencyMhz(Region::EU_866, Preset::LITE_FAST, 0) - 865.7) < 1e-9);

  // Region/preset rules.
  assert(!mt::regionAllowsPreset(Region::EU_868, Preset::SHORT_TURBO));
  assert(!mt::regionAllowsPreset(Region::EU_868, Preset::LITE_FAST));
  assert(mt::regionAllowsPreset(Region::US, Preset::LONG_TURBO));
  assert(!mt::regionAllowsPreset(Region::US, Preset::VERY_LONG_SLOW));
  assert(mt::planLicensedOnly(mt::region(Region::ITU1_70CM)->plan));
  LoRaProfile dummy;
  assert(!mt::makeProfile(Region::EU_874, Preset::LONG_FAST, dummy));  // unlisted
  assert(!mt::sx1262Supports(Region::LORA_24, Preset::LONG_FAST));      // 2.4 GHz
  assert(!mt::sx1262Supports(Region::ITU1_2M, Preset::TINY_FAST));      // 144 MHz
  assert(mt::sx1262Supports(Region::ITU1_70CM, Preset::NARROW_SLOW));

  // A named channel hashes its own name instead of the preset name.
  assert(mt::defaultSlot(Region::US, Preset::LONG_FAST, "") ==
         mt::defaultSlot(Region::US, Preset::LONG_FAST));
  // 2.4 GHz uses the wide bandwidth and a 12-symbol preamble.
  LoRaProfile w;
  assert(mt::makeProfile(Region::LORA_24, Preset::LONG_FAST, w));
  assert(w.bw_hz == 812500 && w.preamble_syms == 12);

  // Profile matches the EU LongFast profile the firmware used before.
  LoRaProfile lf;
  assert(mt::makeProfile(Region::EU_868, Preset::LONG_FAST, lf));
  assert(lf.bw_hz == 250000 && lf.sf == 11 && lf.cr == 5 && lf.sync_word == 0x2B &&
         lf.preamble_syms == 16);

  // Name lookups used by settings.ini.
  assert(mt::regionByName("eu_868") && mt::regionByName("eu_868")->id == Region::EU_868);
  assert(mt::presetByName("LONG_FAST") && mt::presetByName("longfast")->id == Preset::LONG_FAST);
  assert(mt::presetByName("NarrowSlow")->id == Preset::NARROW_SLOW);
  assert(!mt::regionByName("XX") && !mt::presetByName(""));
  assert(std::strcmp(mt::presetEnumName(Preset::MEDIUM_TURBO), "MEDIUM_TURBO") == 0);

  // MeshCore.
  assert(mc::presetCount() == 27);
  const mc::PresetInfo* eu = mc::findPreset("EU/UK (Narrow)");
  assert(eu);
  const LoRaProfile m = mc::makeProfile(*eu);
  assert(m.freq_hz == 869618000 && m.bw_hz == 62500 && m.sf == 8 && m.cr == 8);
  assert(m.sync_word == 0x12 && m.preamble_syms == 32 && m.min_air_preamble_syms == 16);
  const LoRaProfile au = mc::makeProfile(*mc::findPreset("Australia"));
  assert(au.sf == 10 && au.preamble_syms == 16 && au.min_air_preamble_syms == 0);

  // Ekstra presets fra SD (presets.txt): ny titel lægges til, samme titel erstatter, fejl tælles.
  {
    int bad = -1;
    const char* txt =
        "# kommentar\n"
        "\n"
        "Testland;868100000;125000;9;5;2\r\n"
        "EU/UK (Narrow);869620000;62500;8;7\n"
        "Gammel;433500000;250000;11;5;0;1\n"
        "Testland;868300000;125000;9;5\n"          // samme nye titel igen: den sidste gælder
        "Forkert BW;868100000;100000;9;5\n"
        "For lav;100000000;125000;9;5\n"
        "SF 13;868100000;125000;13;5\n"
        "For mange;868100000;125000;9;5;0;0;9\n"
        ";868100000;125000;9;5\n"
        "Kun tre;868100000;125000\n";
    assert(mc::loadExtraPresets(txt, &bad) == 4);
    assert(bad == 6);
    assert(mc::presetCount() == 29);  // 27 + Testland + Gammel
    const mc::PresetInfo* t = mc::findPreset("Testland");
    assert(t && t->freq_hz == 868300000 && t->path_hash_size == 0 && !t->deprecated);
    const mc::PresetInfo* g = mc::findPreset("Gammel");
    assert(g && g->deprecated && g->sf == 11);
    const mc::PresetInfo* e2 = mc::findPreset("EU/UK (Narrow)");
    assert(e2 && e2->freq_hz == 869620000 && e2->cr == 7);
    assert(std::strcmp(mc::presetAt(8).title, "EU/UK (Narrow)") == 0);  // samme plads som før
    mc::clearExtraPresets();
    assert(mc::presetCount() == 27 && !mc::findPreset("Testland"));
    assert(mc::findPreset("EU/UK (Narrow)")->freq_hz == 869618000);
  }

  // Every listed, SX1262-capable Meshtastic region yields a valid default profile.
  size_t ok_regions = 0;
  for (size_t i = 0; i < mt::regionCount(); ++i) {
    const auto& r = mt::regionAt(i);
    if (!r.listed || !mt::sx1262Supports(r.id, r.default_preset)) continue;
    LoRaProfile x;
    assert(mt::makeProfile(r.id, r.default_preset, x));
    assert(x.freq_hz >= uint32_t(r.freq_start_mhz * 1e6) &&
           x.freq_hz <= uint32_t(r.freq_end_mhz * 1e6));
    ++ok_regions;
  }
  std::printf("presets OK: %zu Meshtastic-regioner virker paa SX1262, %zu MeshCore-presets\n",
              ok_regions, mc::presetCount());
  return 0;
}
