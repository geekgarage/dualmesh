// SPDX-License-Identifier: MIT
// DualMesh – SD-kryptering (Indst. > System > SD-kryptering) og oplåsning ved opstart
// i kode-tilstand. Se storage/sd_crypt.h.
//   Låst (kode): skriv koden + Enter.
//   Ellers: vælg fra / chip-ID / kode / eFuse med op/ned og klik:
//     fra, chip-ID: klik to gange.  kode: skriv ny kode to gange.
//     eFuse: findes der en nøgle i chippen, klik to gange for at bruge den; ellers
//     skriv BRUG EFUSE for at brænde en ny (UOMVENDELIGT; kun A-Z og mellemrum).
#pragma once
#include <Arduino.h>

#include <cstdint>

#include "storage/sd_crypt.h"
#include "ui/panel.h"

namespace dm {

class CryptoUi : public Panel {
 public:
  using UnlockedFn = void (*)();
  using ModeFn = void (*)(sdcrypt::Mode m);  // gem valget (settings.ini)
  void begin(UnlockedFn on_unlocked, ModeFn on_mode) {
    on_unlocked_ = on_unlocked;
    on_mode_ = on_mode;
  }
  const char* title() const override { return "SD-kryptering"; }
  void onOpen() override;
  void view(PanelView& v, int rows) override;
  void tapRow(int i) override;
  void key(char c) override;
  void stepY(int dir) override;
  void click() override;
  bool needsRedraw() const override { return millis() - last_draw_ms_ >= 500; }

 private:
  uint32_t last_draw_ms_ = 0;
  enum Input : uint8_t { None, Unlock, NewCode, RepeatCode, Burn };
  void note(const char* t);
  void choose();
  void finish(sdcrypt::Mode m, const char* code);

  UnlockedFn on_unlocked_ = nullptr;
  ModeFn on_mode_ = nullptr;
  int sel_ = 0;
  Input input_ = None;
  char buf_[40] = "", first_[40] = "";
  int armed_ = -1;
  uint32_t armed_ms_ = 0;
  char note_[48] = "";
  uint32_t note_ms_ = 0;
};

}  // namespace dm
