// SPDX-License-Identifier: MIT
// DualMesh – eksport/import af profilen (Indst. > Profil). Se storage/profile_io.h.
//   Eksport: advarsel (denne enhed får en NY profil bagefter) -> Enter -> skriv en
//   adgangskode (mindst 6 tegn) to gange -> fil på SD-kortet, kontrolleret ved at læse
//   den tilbage -> identitet og navne slettes -> genstart med ny profil.
//   Import: vælg en .dmp-fil, skriv adgangskoden, se indholdet, bekræft med Enter
//   -> profilen erstattes, og T-Deck'en genstarter.
#pragma once
#include <Arduino.h>

#include <cstdint>

#include "storage/profile_io.h"
#include "ui/panel.h"

namespace dm {

class SdStore;

class ProfileUi : public Panel {
 public:
  using RestartFn = void (*)(uint32_t delay_ms);  // 0 = straks
  void begin(SdStore* store, RestartFn restart) {
    store_ = store;
    restart_ = restart;
  }
  void openExport() { import_ = false; }
  void openImport() { import_ = true; }
  const char* title() const override { return import_ ? "Importér profil" : "Eksportér profil"; }
  void onOpen() override;
  void view(PanelView& v, int rows) override;
  void tapRow(int i) override;
  void key(char c) override;
  void stepY(int dir) override;
  void click() override;
  bool needsRedraw() const override { return millis() - last_draw_ms_ >= 500; }

 private:
  uint32_t last_draw_ms_ = 0;
  enum Step : uint8_t { Warn, Pick, Pass, Pass2, Confirm, Done };
  void note(const char* t);
  void enter();

  SdStore* store_ = nullptr;
  RestartFn restart_ = nullptr;
  bool import_ = false;
  Step step_ = Pass;
  static constexpr int kMaxFiles = 12;
  char files_[kMaxFiles][96] = {};
  int n_files_ = 0, sel_ = 0;
  char buf_[40] = "", pass_[40] = "";
  profile_io::Info info_;
  char result_[96] = "";
  char note_[48] = "";
  uint32_t note_ms_ = 0;
  uint32_t done_ms_ = 0;  // eksport færdig: genstart efter et øjeblik
};

}  // namespace dm
