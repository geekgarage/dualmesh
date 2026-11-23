// SPDX-License-Identifier: MIT
// DualMesh – hentning over HTTPS fra GitHub (og andre): kataloger, tastaturlayouts, sprogfiler,
// kortpakker og firmwareopdateringer. Én hentning ad gangen i en egen task (TLS og SD tager tid,
// loop-tasken må ikke vente). Viderestillinger følges (GitHubs release-filer ligger på et andet
// domæne). Store filer hentes til <sti>.part og genoptages med Range efter en afbrydelse; først
// når alt er hentet, erstattes den gamle fil. OTA skrives direkte i den anden app-partition og
// kontrolleres med SHA-256, før den bliver valgt til næste opstart.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {
namespace dl {

enum class State { Idle, Busy, Done, Failed };

// Kendte adresser (repoet er offentligt).
constexpr const char* kRaw = "https://raw.githubusercontent.com/geekgarage/dualmesh/main/";
constexpr const char* kLatest = "https://github.com/geekgarage/dualmesh/releases/latest/download/";

// Lille tekstfil (katalog, version.txt) til hukommelsen (højst 64 KB). Hent den med text().
bool startText(const char* url);
// Fil til SD-kortet (path = fuld sti under /sd). resume = genoptag fra <path>.part.
// expect = forventet størrelse (0 = ukendt); for lidt plads på kortet afvises.
// sha256_hex (kun uden resume): filen gemmes kun, hvis SHA-256 passer (fx firmware til en launcher).
bool startFile(const char* url, const char* path, bool resume, uint64_t expect = 0, const char* sha256_hex = nullptr);
// Firmwareopdatering: billedet skrives i den næste app-partition; sha256_hex skal passe.
bool startOta(const char* url, uint32_t size, const char* sha256_hex);

// Done/Failed meldes én gang (msg = kort besked til brugeren); derefter Idle.
State poll(char* msg, size_t n);
bool busy();
void cancel();
// Fremskridt for den igangværende hentning: hentet, i alt (0 = ukendt), byte pr. sekund.
void progress(uint64_t& done, uint64_t& total, uint32_t& bps);
// Teksten fra den seneste startText (gyldig til næste start). Tom ved fejl.
const char* text();
// Ledig plads på SD-kortet i byte (0 = ukendt).
uint64_t sdFree();

}  // namespace dl
}  // namespace dm
