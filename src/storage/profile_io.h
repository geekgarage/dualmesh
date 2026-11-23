// SPDX-License-Identifier: MIT
// DualMesh – eksport/import af profilen, så den kan flyttes til en ny enhed
// (brugerens ønske 15. nov 2026; Indst. > Profil).
//
// Profilen = MeshCore-identiteten (privat nøgle), navn og kort navn, MeshCore-kanaler,
// MeshCore-kontakter, Meshtastic-kanaler og WiFi-netværk. Den skrives som én fil,
// krypteret med en adgangskode, brugeren vælger (AES-256-GCM, nøgle = SHA-256 x 10000
// med salt), fordi chip-ID-/eFuse-nøglen kun virker på den gamle enhed.
//   Fil: "DMX1" + salt (16) + IV (12) + krypteret indhold + tag (16).
//   Gemmes i /sd/dualmesh/eksport/<navn>-<dato>.dmp. Import finder *.dmp dér og i
//   SD-kortets rod (så en fil kan lægges på kortet fra en PC).
// Efter en eksport får den gamle enhed en NY profil (brugerens krav 15. nov 2026), så den
// samme identitet ikke ender på to enheder ved en forglemmelse: retireIdentity() sletter
// identiteten og navnene; ved næste opstart laves et nyt nøglepar og standardnavn.
// Kanaler, kontakter og WiFi-net bliver på enheden.
// Import erstatter de nuværende filer (de skrives med den aktuelle SD-kryptering) og
// navnene i settings.ini; derefter skal T-Deck'en genstarte.
#pragma once
#include <cstddef>

namespace dm {

class SdStore;

namespace profile_io {

struct Info {
  char name[32] = "";
  char short_name[8] = "";
  bool identity = false;
  int files = 0;
  int wifi = 0;  // antal WiFi-netværk
};

bool exportTo(SdStore* store, const char* password, char* out_path, size_t n, char* err, size_t en);
int listFiles(char (*out)[96], int max);
// Gammel enhed efter eksport: slet identiteten og navnene (kræver genstart).
bool retireIdentity(SdStore* store);
// Dekryptér og læs indholdet (uden at skrive noget).
bool peek(const char* path, const char* password, Info& info, char* err, size_t en);
// Skriv profilen. Kræver genstart bagefter.
bool importFrom(SdStore* store, const char* path, const char* password, Info& info, char* err,
                size_t en);

}  // namespace profile_io
}  // namespace dm
