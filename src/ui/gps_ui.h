// SPDX-License-Identifier: MIT
// DualMesh – GPS-status (åbnes fra Indst. > GPS-status). Kun visning:
// fix, satellitter, position, højde, fart, tid og SOS-tilstand.
// Klik/Enter: GPS til/fra. Backspace eller trackball venstre: tilbage.
#pragma once
#include <cstdint>

#include "ui/panel.h"

namespace dm {

class Gps;
class SdStore;

class GpsUi : public Panel {
 public:
  using SosTextFn = const char* (*)();  // kort SOS-status (nullptr = ingen linje)
  void begin(Gps* g, SdStore* store, SosTextFn sos) {
    gps_ = g;
    store_ = store;
    sos_ = sos;
  }
  const char* title() const override { return "GPS"; }
  void view(PanelView& v, int rows) override;
  void tapRow(int i) override {
    if (i == 0) click();
  }
  void key(char c) override;
  void stepY(int) override {}
  void click() override;
  bool needsRedraw() const override;

 private:
  Gps* gps_ = nullptr;
  SdStore* store_ = nullptr;
  SosTextFn sos_ = nullptr;
  uint32_t last_draw_ms_ = 0;
};

}  // namespace dm
