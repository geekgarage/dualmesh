// SPDX-License-Identifier: MIT
// DualMesh – WiFi-skærm (åbnes fra Indst. > WiFi-netværk).
//
//   Række 1-3: status (forbundet/IP/signal) og tid (NTP).
//   Række 4-12: "WiFi: til/fra", "Scan efter netværk", "Skjult netværk ...",
//               scannede netværk (stærkeste først; * = gemt, > = forbundet) og
//               gemte netværk, der ikke er set.
//   Række 13: note / spørgsmål.  Række 14: hjælp eller indtastning.
//   Klik/Enter: udfør / forbind (kode spørges om ved låste netværk); klik på det
//   forbundne netværk to gange = afbryd. Backspace på et gemt netværk = glem
//   (tryk to gange). Tom Backspace eller trackball venstre = tilbage.
#pragma once
#include <cstdint>

#include "ui/panel.h"

namespace dm {

class WifiMgr;

class WifiUi : public Panel {
 public:
  void begin(WifiMgr* w) { wifi_ = w; }
  const char* title() const override { return "WiFi"; }
  void onOpen() override;
  void view(PanelView& v, int rows) override;
  void tapRow(int i) override;
  void key(char c) override;
  void stepY(int dir) override;
  void click() override;
  bool needsRedraw() const override;

 private:
  enum ItemType : uint8_t { Toggle, Scan, Hidden, Seen, KnownOnly };
  struct Item {
    ItemType type;
    int idx;
  };
  static constexpr int kMaxItems = 3 + 24 + 8, kListTop = 4, kListRows = 9;
  int collect(Item* out) const;
  void activate(const Item& it);
  void note(const char* fmt, ...);
  const char* itemSsid(const Item& it) const;

  WifiMgr* wifi_ = nullptr;
  int sel_ = 0, top_ = 0;
  uint32_t drawn_version_ = 0, last_draw_ms_ = 0;
  int armed_ = -1;  // emne, der venter på andet klik/backspace
  uint8_t armed_kind_ = 0;  // 1 = afbryd, 2 = glem
  uint32_t armed_ms_ = 0;
  // Indtastning: 0 = ingen, 1 = SSID (skjult), 2 = kode.
  uint8_t input_ = 0;
  bool input_hidden_ = false;
  char in_ssid_[33] = "";
  char in_buf_[65] = "";
  char note_[48] = "";
  uint32_t note_ms_ = 0;
};

}  // namespace dm
