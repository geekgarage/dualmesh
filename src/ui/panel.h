// SPDX-License-Identifier: MIT
// DualMesh – et "panel" er en underskærm, der åbnes fra Indst. (fx WiFi, GPS,
// Bluetooth). Det beskriver sit indhold som en datamodel (PanelView), som AppUi tegner med
// LVGL, og får alle taster. Trackball venstre (et tydeligt ryk) lukker panelet, medmindre
// det selv bruger venstre/højre (wantsX). Tom Backspace/Esc beder normalt om at lukke
// (wantsClose). Alt tegnes med LVGL (krav 22. nov 2026: intet tekstgitter).
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "ui/status_display.h"  // farverne (color::)

namespace dm {

// Fælles datamodel for underskærme i LVGL (valgt 22. nov 2026): panelet beskriver sine rækker,
// og AppUi tegner dem i samme stil som Indst.
struct PanelRow {
  enum Kind : uint8_t { Blank, Header, Item, Info };
  Kind kind = Blank;
  char label[48] = "";
  char value[40] = "";
  uint16_t fg = color::White;   // etiketten (Header: bjælkens farve)
  uint16_t vfg = color::Gray;   // værdien til højre
  bool sel = false;             // valgbjælken
  bool confirm = false;         // venter på andet klik/Backspace (gul bjælke)
};

struct PanelView {
  static constexpr int kMaxRows = 20;  // Status har ~17 rækker (rulles); andre bruger højst 7
  // Nulstil uden en midlertidig kopi på stakken (modellen fylder ~2 KB).
  void clear() {
    title[0] = status[0] = status2[0] = status3[0] = note[0] = input_text[0] = hint[0] = side[0] = 0;
    status_fg = color::White;
    note_fg = color::Gray;
    nrows = 0;
    scroll_up = scroll_down = input = false;
  }
  char title[40] = "";           // titelbjælken (tom = panelets title())
  char status[64] = "";          // øverste linje (fx "Forbundet: Hjemme")
  uint16_t status_fg = color::White;
  char status2[64] = "";         // anden linje (12 px), fx IP og signal
  char status3[64] = "";         // højre side af titelbjælken (12 px), fx tid
  PanelRow rows[kMaxRows];
  int nrows = 0;
  bool scroll_up = false, scroll_down = false;  // flere rækker over/under
  char note[64] = "";            // linjen under listen (besked, spørgsmål eller forklaring)
  uint16_t note_fg = color::Gray;
  bool input = false;            // gul indtastningslinje i stedet for hjælpen
  char input_text[80] = "";
  char hint[64] = "";            // hjælpelinjen nederst
  // Stor QR-kode (qrSize() > 104): koden fylder venstre side, og til højre vises kun denne
  // tekst (hvid), derunder `note` (i note_fg) og nederst `hint` (grå), alle ombrudt.
  char side[240] = "";

  // Tilføj en række (nullptr, når der ikke er plads). Værdien er printf-formateret.
  PanelRow* add(PanelRow::Kind k, uint16_t fg, const char* label, uint16_t vfg = color::White,
                const char* vfmt = nullptr, ...) {
    if (nrows >= kMaxRows) return nullptr;
    PanelRow& r = rows[nrows++];
    r = PanelRow{};
    r.kind = k;
    r.fg = fg;
    r.vfg = vfg;
    snprintf(r.label, sizeof(r.label), "%s", label ? label : "");
    if (vfmt) {
      va_list ap;
      va_start(ap, vfmt);
      vsnprintf(r.value, sizeof(r.value), vfmt, ap);
      va_end(ap);
    }
    return &r;
  }
  // Etiket (grå) + værdi (hvid) som ren visning.
  template <typename... A>
  PanelRow* info(const char* label, const char* vfmt, A... a) {
    return add(PanelRow::Info, color::Gray, label, color::White, vfmt, a...);
  }
};

class Panel {
 public:
  virtual ~Panel() = default;
  // Datamodellen til LVGL (højst `rows` listerækker vises).
  virtual void view(PanelView& v, int rows) = 0;
  // Tryk på listerække nr. i (indeks i PanelView::rows).
  virtual void tapRow(int) {}
  virtual const char* title() const = 0;
  virtual void onOpen() {}
  virtual void key(char c) = 0;
  virtual void stepY(int dir) = 0;
  virtual bool wantsX() const { return false; }
  virtual void stepX(int) {}
  virtual void click() = 0;
  virtual bool needsRedraw() const { return false; }
  // Tekst, der vises som QR-kode, eller nullptr. Op til 104 px: til højre for listen.
  virtual const char* qrText() const { return nullptr; }
  // QR-kodens størrelse i px. Over 104 fylder den venstre side, og kun PanelView::side vises.
  virtual int qrSize() const { return 104; }
  // Panelet vil lukkes (fx tom Backspace). Nulstilles af AppUi ved lukning.
  bool wantsClose() const { return close_; }
  void clearClose() { close_ = false; }

 protected:
  void requestClose() { close_ = true; }

 private:
  bool close_ = false;
};

}  // namespace dm
