// SPDX-License-Identifier: MIT
// DualMesh – kryptering af hemmelige filer på SD-kortet (brugerens ønske 15. nov 2026):
//   /sd/dualmesh/wifi.txt                 WiFi-koder
//   /sd/dualmesh/meshcore/identity.txt    MeshCores private nøgle
//   /sd/dualmesh/meshcore/channels.txt    MeshCore-kanalnøgler
//   /sd/dualmesh/meshtastic/channels.txt  Meshtastic-kanalnøgler (PSK)
//
// Brugeren vælger nøglens kilde (Indst. > System > SD-kryptering, `sd_crypto`):
//   fra      – almindelig tekst
//   chip-ID  – nøgle afledt af chippens unikke ID (eFuse OPTIONAL_UNIQUE_ID + MAC).
//              Beskytter et tabt SD-kort; ikke mod den, der har selve T-Deck'en.
//   kode     – nøgle afledt af en kode, der skrives ved hver opstart (SHA-256 x 10000
//              med salt). Indtil koden er skrevet, er de hemmelige filer låst.
//   eFuse    – nøgle = HMAC fra en eFuse-nøgle (formål HMAC_UP), som software ikke kan
//              læse. Findes der allerede en sådan nøgle, tilbydes den; ellers kan en ny
//              brændes (UOMVENDELIGT, bruger én af chippens 6 nøglepladser).
// Ingen nøgle gemmes på SD eller i den interne NVS. crypto.txt (ikke hemmelig) har kun
// salt og et kontroltal til kode-tilstanden.
//
// Filformat: "DMC1" + tilstand (1 byte) + IV (12) + AES-256-GCM-krypteret indhold + tag (16).
// Filer uden "DMC1" læses som almindelig tekst (gamle filer) og krypteres ved næste skrivning.
// En fil, der ikke kan dekrypteres (forkert nøgle, andet kort/T-Deck), overskrives ALDRIG.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace dm {
namespace sdcrypt {

enum Mode : uint8_t { Off = 0, Chip = 1, Code = 2, Efuse = 3 };

const char* modeName(Mode m);  // "fra", "chip-ID", "kode", "eFuse"
bool modeFromName(const char* s, Mode& out);

// Opstart (efter SD er monteret). Kode-tilstand: låst, indtil unlock() lykkes.
// eFuse-tilstand uden nøgle i chippen: låst (filerne kan ikke læses her).
void init(Mode m);
Mode mode();
bool locked();
bool unlock(const char* code);
const char* status();  // kort tekst til skærm/log

// eFuse: eksisterende HMAC_UP-nøgle (blok 0-5) eller -1; antal frie nøglepladser.
int efuseKeyBlock();
int efuseFreeBlocks();
// Brænder en ny tilfældig HMAC_UP-nøgle. UOMVENDELIGT. Returnerer blok eller -1.
int efuseBurnNew();

// Skift tilstand: læser alle hemmelige filer med den nuværende nøgle og skriver dem
// med den nye. code bruges kun ved Code. false + err ved fejl (intet ændret).
bool change(Mode m, const char* code, char* err, size_t err_n);

// Læs: dekrypteret (eller almindelig) fil som FILE* (fmemopen); nullptr hvis den
// mangler eller ikke kan dekrypteres. Luk med closeRead().
FILE* openRead(const char* path);
void closeRead(FILE* f);
// Skriv: hukommelsesstrøm; commit() krypterer og skriver atomisk (path.tmp -> path).
FILE* openWrite();
bool commit(FILE* f, const char* path);
// Filen findes, men kunne ikke dekrypteres i denne opstart.
bool unreadable(const char* path);
// Brugeren erstatter filen bevidst (profil-import): må overskrives igen.
void clearUnreadable(const char* path);
// Nøgle ud fra en kode/adgangskode – også til profil-eksport. kdf 1 = gammel (SHA-256 x
// 10000, kun til at læse gamle filer), 2 = PBKDF2-HMAC-SHA256 med kPbkdf2Rounds runder.
constexpr int kKdfOld = 1, kKdfPbkdf2 = 2;
constexpr uint32_t kPbkdf2Rounds = 30000;  // ca. 1,9 s på ESP32-S3 (100.000 tog 6,2 s)
void deriveKey(const char* code, const uint8_t salt[16], uint8_t out[32], int kdf = kKdfPbkdf2);
// Overskriv en fil med nuller og slet den (så gamle hemmeligheder ikke kan gendannes fra
// kortets sektorer; SD-kortets egen slidudjævning kan dog stadig have en kopi).
void shred(const char* path);
// PBKDF2: vores hurtige udgave mod mbedtls (bruges i profil-selvtesten). true = ens.
bool pbkdfSelfTest();

}  // namespace sdcrypt
}  // namespace dm
