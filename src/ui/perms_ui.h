// SPDX-License-Identifier: MIT
// DualMesh – telemetri-tilladelser pr. kontakt (Indst. > MeshCore > Telem.-kontakter).
// Som i MeshCore gemmes de i kontaktens flag (bit 1 telemetri, bit 2 position, bit 3
// miljø; bit 0 er "favorit") og deles med MeshCore-appen.
//   Op/ned: vælg kontakt.  Venstre/højre: - -> T -> TP -> TPM.  Klik/Bksp: tilbage.
#pragma once
#include <Arduino.h>

#include <cstdint>

#include "ui/panel.h"

struct ContactInfo;

namespace dm {

class McNode;

class PermsUi : public Panel {
 public:
  void begin(McNode* node) { node_ = node; }
  const char* title() const override { return "Telemetri-kontakter"; }
  void onOpen() override;
  void view(PanelView& v, int rows) override;
  void tapRow(int i) override;
  void key(char c) override;
  void stepY(int dir) override;
  bool wantsX() const override { return n_ > 0; }
  void stepX(int dir) override;
  void click() override { requestClose(); }
  bool needsRedraw() const override { return millis() - last_ms_ >= 500; }

 private:
  static constexpr int kMax = 64, kTop = 2, kRows = 9;
  McNode* node_ = nullptr;
  ContactInfo* list_ = nullptr;  // PSRAM
  int n_ = 0, sel_ = 0, top_ = 0;
  uint32_t last_ms_ = 0;
};

}  // namespace dm
