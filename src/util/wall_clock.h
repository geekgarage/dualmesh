// SPDX-License-Identifier: MIT
// DualMesh – fælles ur (rigtig tid). T-Deck'en har intet batteri-RTC, så tiden
// sættes fra den bedste kilde, der er til rådighed:
//   GPS (bedst) > NTP over WiFi > Mesh (3 enige MeshCore-afsendere).
// En dårligere kilde overskriver aldrig en bedre. Når tiden sættes, opdateres
// både ESP32'ens systemur (time(), localtime med TZ fra settings.ini) og
// MeshCores RTC (bruges i vores egne pakker).
#pragma once
#include <cstdint>

namespace mesh {
class RTCClock;
}

namespace dm {
namespace wallclock {

enum Source : uint8_t { None, Mesh, App, Ntp, Gps };  // App = telefonens tid via MeshCore-appen

void setRtc(mesh::RTCClock* rtc);
// Sæt tiden. Returnerer false, hvis en bedre kilde allerede har sat den
// (samme kilde må gerne opdatere). Tider før 1/1-2025 afvises.
bool set(uint32_t unix, Source src);
bool valid();
uint32_t now();          // unix-tid, 0 hvis ukendt
Source source();
const char* sourceName(Source s);
uint32_t setAtMs();      // millis() da tiden sidst blev sat
// "TT:MM" i lokal tid (eller "--:--").
void formatHm(uint32_t unix, char* out, int n);

}  // namespace wallclock
}  // namespace dm
