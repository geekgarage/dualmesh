// SPDX-License-Identifier: MIT
// DualMesh – indstillinger og data på microSD-kortet.
//
// Alt der skal overleve en genstart, ligger på SD-kortet (ikke i den interne
// NVS): indstillinger, lærte radioværdier og senere beskeder, kontakter og
// kortfliser. Kode og "varme" data bliver i flash/RAM: SD-kortet er langsommere
// og deler SPI-bus med radioen.
//
// Alle DualMesh-filer ligger under /dualmesh/ på kortet. Andre filer (fx en
// launchers firmwarefiler) røres aldrig; "nulstil" sletter kun /dualmesh/.
//
// Filformat: /sd/dualmesh/settings.ini, én "nøgle=værdi" pr. linje, '#' er
// kommentar. Læsbart og redigerbart på en PC. Gemmes atomisk (tmp-fil + .bak),
// så et strømsvigt midt i en skrivning aldrig efterlader en ødelagt fil.
//
// Trådsikkerhed: begin() kaldes i setup() før radio-tasken starter; derefter
// bruges resten kun fra loop()-tasken (senere GUI-tasken).
#pragma once
#include <cstddef>
#include <cstdint>

#include "driver/spi_common.h"

namespace dm {

enum class SdState : uint8_t {
  NotStarted,
  NoCard,       // intet kort (eller det svarer ikke)
  Ready,        // FAT monteret, DualMesh kan gemme
  Unsupported,  // kortet har et filsystem vi ikke kan læse (exFAT, NTFS, ...): spørg før formatering
  Error,
};

class SdStore {
 public:
  // Monterer kortet på en allerede initialiseret SPI-bus. Et helt tomt kort
  // (ingen boot-signatur) formateres automatisk som FAT: der er intet at miste.
  // Et kort med et ukendt filsystem formateres ALDRIG uden formatCard().
  SdState begin(spi_host_device_t host, int cs_pin, int max_khz = 20000);
  SdState state() const { return state_; }
  bool mounted() const { return state_ == SdState::Ready; }
  const char* status() const;             // kort tekst til skærm/log
  const char* foundFs() const { return fs_name_; }  // fx "exFAT" ved Unsupported
  bool autoFormatted() const { return auto_formatted_; }
  int lastError() const { return last_err_; }  // esp_err_t fra sidste fejl (diagnose)
  const char* diag() const { return diag_; }   // fx MBR/boot-sektor ved ukendt format

  // Formaterer HELE kortet som FAT (sletter alt). Kun efter brugerens
  // udtrykkelige bekræftelse – i dag via seriel monitor, senere i GUI'en.
  bool formatCard();

  // Sletter kun DualMesh' egne data (/dualmesh/) og glemmer indstillingerne i
  // RAM. Alt andet på kortet (launcher, andre firmwares) bliver urørt, og
  // eksporterede profiler (/dualmesh/eksport/) bevares.
  bool resetDualMesh();

  bool load();  // læser settings.ini (eller .bak hvis den mangler)
  bool save();  // skriver kun hvis noget er ændret

  const char* get(const char* key) const;
  long getInt(const char* key, long def) const;
  void set(const char* key, const char* value);
  void setInt(const char* key, long value);
  bool dirty() const { return dirty_; }

 private:
  // Højst kMax nøgler (indstillinger + lærte radioværdier); 64 var for lidt (17. nov 2026: nye
  // nøgler blev stille droppet). Ligger i PSRAM.
  static constexpr size_t kMax = 192, kKey = 48, kVal = 128;  // fx testbeskedens tekst
  struct Entry { char key[kKey]; char val[kVal]; };
  int find(const char* key) const;
  bool parse(const char* path);
  bool mount(bool format_if_unreadable);
  void probe();  // læser sektor 0 når FAT-montering fejler
  void classifyVbr(const uint8_t* vbr);

  Entry* e_ = nullptr;  // PSRAM, kMax poster
  bool ensure();
  size_t n_ = 0;
  bool dirty_ = false, auto_formatted_ = false;
  SdState state_ = SdState::NotStarted;
  const char* fs_name_ = "";
  spi_host_device_t host_ = SPI2_HOST;
  int cs_ = -1, khz_ = 20000;
  void* card_ = nullptr;  // sdmmc_card_t*
  int last_err_ = 0;
  char diag_[128] = "";
};

}  // namespace dm
