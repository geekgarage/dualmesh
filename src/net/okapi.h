// SPDX-License-Identifier: MIT
// DualMesh – geocaches fra Opencaching over WiFi (OKAPI, den åbne API på opencaching.de m.fl.).
// Kræver brugerens egen gratis API-nøgle (consumer key, Level 1). Hentningen kører i en egen
// task (TLS tager et par sekunder) og gemmer resultatet som GPX i
// /sd/dualmesh/caches/opencaching.gpx, så den almindelige GPX-læser bruges, og cachene også
// virker uden net bagefter.
#pragma once
#include <cstddef>

namespace dm {
namespace okapi {

enum class State { Idle, Busy, Done, Failed };

// Start en hentning omkring (lat, lon). false = mangler nøgle/WiFi eller er allerede i gang.
bool start(const char* site, const char* key, double lat, double lon, int radius_km);
// Done/Failed meldes én gang (msg = kort besked til brugeren); derefter Idle.
State poll(char* msg, size_t n);

}  // namespace okapi
}  // namespace dm
