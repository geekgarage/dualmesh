// SPDX-License-Identifier: MIT
// DualMesh – kontaktfarver (Indst. > Beskeder > Kontaktfarver). Se ui/contact_colors.h.
//   Op/ned: vælg kontakt (nyeste først).  Venstre/højre: skift farve (gemmes straks).
//   Backspace: tilbage til automatisk farve.  Klik/Enter: tilbage til menuen.
#pragma once
#include <cstdint>

#include "ui/panel.h"

struct ContactInfo;

namespace dm {

class McNode;

class ColorsUi : public Panel {
 public:
  void begin(McNode* node) { node_ = node; }
  const char* title() const override { return "Kontaktfarver"; }
  void onOpen() override;
  void view(PanelView& v, int rows) override;
  void tapRow(int i) override;
  void key(char c) override;
  void stepY(int dir) override;
  bool wantsX() const override { return n_ > 0; }
  void stepX(int dir) override;
  void click() override { requestClose(); }
  bool needsRedraw() const override { return true; }

 private:
  static constexpr int kMax = 64, kTop = 2, kRows = 10;
  McNode* node_ = nullptr;
  ContactInfo* list_ = nullptr;  // PSRAM
  int n_ = 0, sel_ = 0, top_ = 0;
};

}  // namespace dm
