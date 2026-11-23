// SPDX-License-Identifier: MIT
#include "ui/nvs_ui.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "i18n.h"
#include "ui/status_display.h"

namespace dm {

namespace {
// Bekræftelsesordet i det valgte sprog (SLET ALT / ERASE ALL). Det danske ord virker altid også.
bool confirmed(const char* typed) {
  static const char* const kWord = "SLET ALT";  // oversættes i lang/*.txt (en: ERASE ALL)
  char w[24];
  const char* t = i18n::tr(kWord, w, sizeof(w));
  return strcmp(typed, kWord) == 0 || strcmp(typed, t) == 0;
}
}  // namespace

void NvsUi::refresh() {
  nvstool::stats(st_);
  n_ns_ = nvstool::list(ns_, kMaxNs);
  if (sel_ >= count()) sel_ = count() - 1;
}

void NvsUi::onOpen() {
  sel_ = 0;
  top_ = 0;
  armed_ = -1;
  typing_ = false;
  refresh();
}

void NvsUi::note(const char* t) {
  snprintf(note_, sizeof(note_), "%s", t);
  note_ms_ = millis();
}

void NvsUi::view(PanelView& v, int rows) {
  v.clear();
  const nvstool::Launcher& l = nvstool::launcher();
  snprintf(v.status, sizeof(v.status), "NVS: %lu af %lu poster brugt, %lu fri", (unsigned long)st_.used,
           (unsigned long)st_.total, (unsigned long)st_.free);
  if (l.found) snprintf(v.status2, sizeof(v.status2), "Startet fra launcher: %.30s", l.name);
  else snprintf(v.status2, sizeof(v.status2), "Ingen launcher (kører fra %.8s). DualMesh bruger SD.", l.running);
  if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
  v.scroll_up = top_ > 0;
  v.scroll_down = top_ + rows < count();
  for (int r = 0; r < rows && top_ + r < count(); ++r) {
    const int i = top_ + r;
    PanelRow* w = nullptr;
    if (i == 0) {
      w = v.add(PanelRow::Item, color::Gold, "Ryd Bluetooth-parringer");
    } else if (i == 1) {
      w = v.add(PanelRow::Item, color::Gold, "Ryd WiFi- og radiodata (ESP-IDF)");
    } else if (i == 2) {
      w = v.add(PanelRow::Item, color::Red, "Ryd HELE NVS ...");
    } else {
      const nvstool::Ns& n = ns_[i - kActions];
      w = v.add(PanelRow::Item, n.kind == nvstool::Other ? color::Gray : color::White, n.name, color::Gray,
                "%u poster, %s", unsigned(n.entries), nvstool::kindText(n.kind));
    }
    if (w && i == sel_ && !typing_) {
      w->sel = true;
      w->confirm = armed_ == i && millis() - armed_ms_ < 4000;
    }
  }
  const bool other = sel_ >= kActions && sel_ < count() && ns_[sel_ - kActions].kind == nvstool::Other;
  if (note_[0] && millis() - note_ms_ < 5000) {
    snprintf(v.note, sizeof(v.note), "%s", note_);
    v.note_fg = color::Green;
  } else if (armed_ >= 0 && millis() - armed_ms_ < 4000) {
    snprintf(v.note, sizeof(v.note), "Klik igen for at rydde");
    v.note_fg = color::Yellow;
  } else if (typing_) {
    snprintf(v.note, sizeof(v.note), "Skriv SLET ALT og tryk Enter:");
    v.note_fg = color::Red;
  } else {
    const char* why = sel_ == 0   ? "Telefoner skal parres igen. Genstarter."
                      : sel_ == 1 ? "WiFi-kalibrering laves igen. Genstarter."
                      : sel_ == 2 ? (l.found ? "Sletter OGSÅ launcherens indstillinger!"
                                             : "Sletter alle firmwares' NVS-data.")
                      : other     ? (l.found ? "Tilhører launcher eller anden firmware!" : "Tilhører en anden firmware.")
                                  : "Data fra ESP-IDF (Bluetooth/WiFi).";
    snprintf(v.note, sizeof(v.note), "%s", why);
    v.note_fg = sel_ == 2 || other ? color::Yellow : color::Gray;
  }
  if (typing_) {
    v.input = true;
    snprintf(v.input_text, sizeof(v.input_text), "%s_", buf_);
  } else {
    snprintf(v.hint, sizeof(v.hint), "Klik 2 gange: ryd   Backspace: tilbage");
  }
  return;
}

void NvsUi::tapRow(int i) {
  if (typing_ || i < 0) return;
  const int k = top_ + i;
  if (k >= count()) return;
  if (k != sel_) armed_ = -1;
  sel_ = k;
  click();
}

void NvsUi::stepY(int dir) {
  if (typing_) return;
  sel_ += dir;
  if (sel_ < 0) sel_ = 0;
  if (sel_ >= count()) sel_ = count() - 1;
  armed_ = -1;
}

bool NvsUi::eraseKind(nvstool::Kind a, nvstool::Kind b) {
  bool ok = true;
  int n = 0;
  for (int i = 0; i < n_ns_; ++i)
    if (ns_[i].kind == a || ns_[i].kind == b) {
      ok = nvstool::eraseNamespace(ns_[i].name) && ok;
      ++n;
    }
  if (!n) note("Intet at rydde");
  return n > 0 && ok;
}

void NvsUi::click() {
  if (typing_) {
    key('\r');
    return;
  }
  if (sel_ == 2) {
    typing_ = true;
    buf_[0] = 0;
    return;
  }
  if (armed_ != sel_ || millis() - armed_ms_ >= 4000) {
    armed_ = sel_;
    armed_ms_ = millis();
    return;
  }
  armed_ = -1;
  if (sel_ == 0) {
    if (eraseKind(nvstool::Bluetooth, nvstool::Bluetooth) && restart_) restart_("Bluetooth-parringer ryddet");
  } else if (sel_ == 1) {
    if (eraseKind(nvstool::Wifi, nvstool::Phy) && restart_) restart_("WiFi-/radiodata ryddet");
  } else if (sel_ < count()) {
    const nvstool::Ns n = ns_[sel_ - kActions];
    if (nvstool::eraseNamespace(n.name)) {
      char t[48];
      snprintf(t, sizeof(t), "Ryddet: %s", n.name);
      note(t);
      if (n.kind != nvstool::Other && restart_) restart_(t);
    } else {
      note("Kunne ikke rydde");
    }
  }
  refresh();
}

void NvsUi::key(char c) {
  const bool enter = c == '\r' || c == '\n';
  const bool bksp = c == 0x08 || c == 0x7F;
  if (!typing_) {
    if (enter) click();
    else if (bksp) requestClose();
    return;
  }
  const size_t n = strlen(buf_);
  if (bksp) {
    if (n) buf_[n - 1] = 0;
    else typing_ = false;
  } else if (enter) {
    typing_ = false;
    if (confirmed(buf_)) {
      if (nvstool::eraseAll() && restart_) restart_("hele NVS ryddet");
      else note("Rydning fejlede");
    } else {
      note("Fortrudt (ikke SLET ALT)");
    }
    buf_[0] = 0;
  } else if (uint8_t(c) >= 0x20 && n + 1 < sizeof(buf_)) {
    buf_[n] = c;
    buf_[n + 1] = 0;
  }
}

}  // namespace dm
