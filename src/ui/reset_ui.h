// SPDX-License-Identifier: MIT
// DualMesh – Nulstil DualMesh (Indst. > System). Sletter alt i /dualmesh/ på SD-kortet
// UNDTAGEN eksporterede profiler (/dualmesh/eksport/). Launcher, andre mapper og den
// interne NVS røres ikke.
//   Vælg "Eksportér profil og nulstil" (adgangskode to gange; filen kontrolleres) eller
//   "Nulstil uden eksport". Bekræft med NULSTIL + Enter. Derefter genstart.
#pragma once
#include <Arduino.h>

#include <cstdint>

#include "ui/panel.h"

namespace dm {

class SdStore;

class ResetUi : public Panel {
 public:
  using ResetFn = bool (*)();      // nulstil (true = OK)
  using RestartFn = void (*)(uint32_t delay_ms);
  void begin(SdStore* store, ResetFn reset, RestartFn restart) {
    store_ = store;
    reset_ = reset;
    restart_ = restart;
  }
  const char* title() const override { return "Nulstil DualMesh"; }
  void onOpen() override;
  void view(PanelView& v, int rows) override;
  void tapRow(int i) override;
  void key(char c) override;
  void stepY(int dir) override;
  void click() override;
  bool needsRedraw() const override { return millis() - last_draw_ms_ >= 500; }

 private:
  enum Step : uint8_t { Choose, Pass, Pass2, Confirm };
  void note(const char* t);
  void enter();

  SdStore* store_ = nullptr;
  ResetFn reset_ = nullptr;
  RestartFn restart_ = nullptr;
  Step step_ = Choose;
  int sel_ = 0;
  bool exported_ = false;
  char buf_[40] = "", pass_[40] = "", file_[96] = "";
  char note_[48] = "";
  uint32_t note_ms_ = 0, last_draw_ms_ = 0;
};

}  // namespace dm
