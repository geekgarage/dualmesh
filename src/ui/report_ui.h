// SPDX-License-Identifier: MIT
// DualMesh – "Fejlrapport" (Indst. > System, brugerens ønske 17. nov 2026).
//
// Side 1: version, denne opstarts årsag og det seneste gemte nedbrud (core dump i flash) samt
// hvordan man sender den fulde log (websiden docs/rapport/ over USB, rensning på egen PC).
// Side 2 (Enter/klik): en stor QR-kode med et udfyldt GitHub-issue (diag::buildIssueUrl). Linket
// har kun version, opstartsårsag og nedbrudsadresser – ingen navne, nøgler, positioner eller
// beskeder. "x" to gange (inden 4 s) sletter det gemte nedbrud.
#pragma once
#include <Arduino.h>

#include <cstdint>
#include <cstdio>

#include "diag/crash_info.h"
#include "diag_report.h"
#include "ui/panel.h"
#include "ui/status_display.h"
#include "version.h"

namespace dm {

class ReportUi : public Panel {
 public:
  // reset_text: denne opstarts årsag på dansk (som i loggen).
  void begin(const char* board, const char* reset_text) {
    board_ = board;
    reset_text_ = reset_text;
  }
  const char* title() const override { return "Fejlrapport"; }
  void onOpen() override {
    page_ = 0;
    confirm_ms_ = 0;
    note_ = nullptr;
    refresh();
  }
  const char* qrText() const override { return page_ == 1 && url_[0] ? url_ : nullptr; }
  int qrSize() const override { return 210; }  // version 12 med 3 px pr. modul (195 px)

  // Side 1: nedbrud og forklaring. Side 2: stor QR-kode til venstre og kort tekst til højre.
  void view(PanelView& v, int) override {
    v.clear();
    if (page_ == 1) {
      // Ved siden af den store kode: forklaring (hvid), advarsel (note, gul) og hjælp (grå).
      snprintf(v.side, sizeof(v.side), "Scan med telefonen. Åbner et udfyldt GitHub-issue.");
      snprintf(v.note, sizeof(v.note), "Åbner GitHub-appen: tryk på Fejlrapport");  // appen viser skabelonvalget først
      v.note_fg = color::Yellow;
      snprintf(v.hint, sizeof(v.hint), "Backspace: tilbage");
      return;
    }
    snprintf(v.status, sizeof(v.status), "DualMesh %s (%.16s)", DM_FW_VERSION, DM_GIT_REV);
    snprintf(v.status2, sizeof(v.status2), "Denne opstart: %.45s", reset_text_ ? reset_text_ : "?");
    v.add(PanelRow::Header, color::Gold, "Seneste gemte nedbrud");
    if (crash_.present) {
      char t[40];
      snprintf(t, sizeof(t), "%s i %.15s", diag::causeName(crash_.cause), crash_.task);
      v.add(PanelRow::Info, color::Yellow, t);
      v.info("PC / adresse", "0x%08lx / 0x%08lx", (unsigned long)crash_.pc, (unsigned long)crash_.vaddr);
      v.add(PanelRow::Info, color::Gray, "Build", other_build_ ? color::Yellow : color::White, "%s",
            other_build_ ? "et ANDET build end dette" : "dette build");
    } else {
      v.add(PanelRow::Info, color::Gray, "Intet gemt.");
    }
    PanelRow* q = v.add(PanelRow::Item, color::Green, "Vis QR-kode til GitHub-issue");
    if (q) q->sel = true;
    v.add(PanelRow::Info, color::Gray, "Fuld log: T-Deck i USB, åbn", color::LightBlue, "");
    v.add(PanelRow::Info, color::LightBlue, DM_REPORT_URL_SHORT);
    if (note_) {
      snprintf(v.note, sizeof(v.note), "%s", note_);
      v.note_fg = color::Green;
    } else if (confirming()) {
      snprintf(v.note, sizeof(v.note), "Tryk x igen for at slette nedbruddet");
      v.note_fg = color::Yellow;
    } else {
      snprintf(v.note, sizeof(v.note), "Loggen renses på din PC, før noget sendes.");
    }
    snprintf(v.hint, sizeof(v.hint), "%s", crash_.present ? "x x: slet gemt nedbrud   Backspace: tilbage"
                                                         : "Backspace: tilbage");
    return;
  }
  void tapRow(int i) override {
    if (i >= 0 && i < PanelView::kMaxRows) click();  // kun QR-rækken kan vælges
  }

  void key(char c) override {
    if (c == '\r' || c == '\n') {
      page_ ^= 1;
      return;
    }
    if (c == 0x08 || c == 0x7F || c == 0x1B) {
      if (page_ == 1) page_ = 0;
      else requestClose();
      return;
    }
    if ((c == 'x' || c == 'X') && page_ == 0 && crash_.present) {
      if (confirming()) {
        confirm_ms_ = 0;
        note_ = crashinfo::erase() ? "Gemt nedbrud slettet" : "Kunne ikke slette nedbruddet";
        refresh();
      } else {
        confirm_ms_ = millis() | 1;
        note_ = nullptr;
      }
    }
  }
  void stepY(int) override {}
  void click() override { page_ ^= 1; }
  bool needsRedraw() const override {
    // Bekræftelsen "Tryk x igen" forsvinder af sig selv efter 4 s (én ny tegning).
    if (confirm_ms_ && millis() - confirm_ms_ >= 4000) {
      confirm_ms_ = 0;
      return true;
    }
    return false;
  }

 private:
  bool confirming() const { return confirm_ms_ && millis() - confirm_ms_ < 4000; }
  void refresh() {
    crashinfo::read(crash_);
    other_build_ = crashinfo::fromOtherBuild(crash_);
    diag::ReportInfo info;
    info.fw_version = DM_FW_VERSION;
    info.git_rev = DM_GIT_REV;
    info.board = board_;
    info.reset_reason = crashinfo::resetCode();
    info.crash = &crash_;
    diag::buildIssueUrl(DM_PROJECT_URL, info, url_, sizeof(url_));
  }

  const char* board_ = "T-Deck Plus";
  const char* reset_text_ = nullptr;
  const char* note_ = nullptr;
  diag::CrashSummary crash_;
  bool other_build_ = false;
  uint8_t page_ = 0;
  mutable uint32_t confirm_ms_ = 0;
  char url_[320] = "";
};

}  // namespace dm
