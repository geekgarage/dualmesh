// SPDX-License-Identifier: MIT
// DualMesh – den interne NVS (flash-partition "nvs") deles med launcheren og andre
// firmwares på T-Deck'en. DualMesh lægger ikke egne data dér; kun ESP-IDF selv gemmer
// Bluetooth-parringer ("bt_config.conf"), WiFi-opsætning ("nvs.net80211") og radio-
// kalibrering ("phy"). Her kan brugeren se navnerummene og rydde dem selektivt eller
// rydde det hele (Indst. > System > Intern NVS).
// Launcher: kører DualMesh fra en OTA-partition, og indeholder factory-partitionen en
// anden app, regnes den for en launcher (navnet læses fra dens app-beskrivelse).
#pragma once
#include <cstdint>

namespace dm {
namespace nvstool {

enum Kind : uint8_t { Bluetooth, Wifi, Phy, Other };

struct Ns {
  char name[16];
  uint16_t entries;
  Kind kind;
};

struct Stats {
  uint32_t used = 0, free = 0, total = 0, namespaces = 0;
};

struct Launcher {
  bool found = false;
  char name[32] = "";     // fx "Launcher" (factory-appens projektnavn)
  char running[17] = "";  // partitionen DualMesh kører fra (fx "app0", "ota_0")
};

bool stats(Stats& out);
int list(Ns* out, int max);  // navnerum med antal poster
const char* kindText(Kind k);
bool eraseNamespace(const char* ns);
bool eraseAll();  // hele NVS-partitionen (genstart bagefter)
const Launcher& launcher();

}  // namespace nvstool
}  // namespace dm
