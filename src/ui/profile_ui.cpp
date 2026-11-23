// SPDX-License-Identifier: MIT
#include "ui/profile_ui.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "ui/status_display.h"

namespace dm {

namespace {
const char* baseName(const char* p) {
  const char* s = strrchr(p, '/');
  return s ? s + 1 : p;
}
void wipe(char* p, size_t n) {
  volatile char* v = p;
  while (n--) *v++ = 0;
}
}  // namespace

void ProfileUi::onOpen() {
  wipe(buf_, sizeof(buf_));
  wipe(pass_, sizeof(pass_));
  result_[0] = 0;
  sel_ = 0;
  if (import_) {
    n_files_ = profile_io::listFiles(files_, kMaxFiles);
    step_ = Pick;
  } else {
    step_ = Warn;
  }
}

void ProfileUi::note(const char* t) {
  snprintf(note_, sizeof(note_), "%s", t);
  note_ms_ = millis();
}

void ProfileUi::view(PanelView& v, int rows) {
  last_draw_ms_ = millis();
  v.clear();
  v.status_fg = color::Yellow;
  if (!import_ && step_ == Warn) {
    snprintf(v.status, sizeof(v.status), "Eksportér profil");
    snprintf(v.status2, sizeof(v.status2), "Identitet, navne, kanaler, kontakter og WiFi-net");
    snprintf(v.status3, sizeof(v.status3), "krypteret fil");
    v.add(PanelRow::Header, color::Red, "DENNE T-DECK FÅR EN NY PROFIL");
    v.add(PanelRow::Info, color::Yellow, "Efter eksporten slettes identiteten og");
    v.add(PanelRow::Info, color::Yellow, "navnet her; en ny laves ved genstart.");
    v.add(PanelRow::Info, color::Yellow, "Så kører samme profil aldrig på to");
    v.add(PanelRow::Info, color::Yellow, "enheder. Kanaler, kontakter og WiFi");
    v.add(PanelRow::Info, color::Yellow, "bliver. Gem filen godt!");
  } else if (!import_) {
    snprintf(v.status, sizeof(v.status), "Eksportér profil");
    snprintf(v.status2, sizeof(v.status2), "Flyt filen til den nye enhed og vælg Importér dér.");
    if (step_ == Done) {
      v.add(PanelRow::Info, color::Gray, "Gemt", color::Green, "%.36s", baseName(result_));
      v.info("Mappe", "/dualmesh/eksport/");
      v.add(PanelRow::Blank, color::Gray, "");
      v.add(PanelRow::Info, color::Yellow, "Denne T-Deck har fået en ny profil");
      v.add(PanelRow::Info, color::Yellow, "og genstarter om få sekunder.");
      v.add(PanelRow::Info, color::Gray, "Kopiér filen væk, før du nulstiller.");
    } else {
      v.add(PanelRow::Info, color::Red, "Denne T-Deck får en ny profil bagefter.");
    }
  } else if (step_ == Pick) {
    snprintf(v.status, sizeof(v.status), "Importér profil: vælg fil");
    snprintf(v.status2, sizeof(v.status2), "Fra /dualmesh/eksport/ eller SD-kortets rod");
    if (!n_files_) {
      v.add(PanelRow::Info, color::White, "Ingen .dmp-filer fundet.");
      v.add(PanelRow::Info, color::Gray, "Læg filen på kortet fra en PC.");
    }
    if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
    const int top = sel_ >= rows ? sel_ - rows + 1 : 0;
    v.scroll_up = top > 0;
    v.scroll_down = top + rows < n_files_;
    for (int i = top; i < n_files_ && i < top + rows; ++i) {
      PanelRow* r = v.add(PanelRow::Item, color::White, baseName(files_[i]));
      if (r) r->sel = i == sel_;
    }
  } else {
    snprintf(v.status, sizeof(v.status), "Importér: %.50s", baseName(files_[sel_]));
    if (step_ == Confirm) {
      v.info("Navn", "%.32s", info_.name[0] ? info_.name : "(standard)");
      v.info("Kort navn", "%.4s", info_.short_name[0] ? info_.short_name : "(auto)");
      v.add(PanelRow::Info, color::Gray, "MeshCore-identitet", info_.identity ? color::White : color::Red, "%s",
            info_.identity ? "ja" : "NEJ");
      v.info("Filer / WiFi-net", "%d / %d", info_.files, info_.wifi);
      v.add(PanelRow::Header, color::Red, "Den nuværende profil ERSTATTES");
      v.add(PanelRow::Info, color::Gray, "Eksportér den først, hvis du vil beholde");
      v.add(PanelRow::Info, color::Gray, "den. T-Deck'en genstarter.");
    } else {
      v.add(PanelRow::Info, color::White, "Skriv adgangskoden til filen.");
    }
  }
  if (note_[0] && millis() - note_ms_ < 5000) {
    snprintf(v.note, sizeof(v.note), "%s", note_);
    v.note_fg = color::Green;
  } else {
    const char* q = step_ == Pass      ? (import_ ? "Adgangskode:" : "Ny adgangskode (mindst 6 tegn):")
                    : step_ == Pass2   ? "Gentag adgangskoden:"
                    : step_ == Confirm ? "Enter = erstat profilen og genstart"
                    : step_ == Warn    ? "Enter = fortsæt   Backspace = fortryd"
                                       : "";
    snprintf(v.note, sizeof(v.note), "%s", q);
    v.note_fg = color::Yellow;
  }
  if (step_ == Pass || step_ == Pass2) {
    v.input = true;
    size_t k = 0;
    for (size_t i = 0; buf_[i] && k < sizeof(v.input_text) - 2; ++i) v.input_text[k++] = '*';
    v.input_text[k++] = '_';
    v.input_text[k] = 0;
  } else {
    snprintf(v.hint, sizeof(v.hint), "%s", step_ == Pick ? "Klik: vælg   Backspace: tilbage" : "Backspace: tilbage");
  }
  return;
}

void ProfileUi::tapRow(int i) {
  if (step_ != Pick || !n_files_ || i < 0) return;
  const int top = sel_ >= 7 ? sel_ - 7 + 1 : 0;  // som i view() med AppUi::kPanelRows
  if (top + i >= n_files_) return;
  sel_ = top + i;
  enter();
}

void ProfileUi::stepY(int dir) {
  if (step_ != Pick) return;
  sel_ += dir;
  if (sel_ < 0) sel_ = 0;
  if (sel_ >= n_files_) sel_ = n_files_ ? n_files_ - 1 : 0;
}

void ProfileUi::click() { enter(); }

void ProfileUi::enter() {
  char err[48] = "";
  switch (step_) {
    case Warn:
      step_ = Pass;
      break;
    case Pick:
      if (n_files_) step_ = Pass;
      break;
    case Pass:
      if (import_) {
        if (profile_io::peek(files_[sel_], buf_, info_, err, sizeof(err))) {
          snprintf(pass_, sizeof(pass_), "%s", buf_);
          step_ = Confirm;
        } else {
          note(err);
        }
      } else if (strlen(buf_) < 6) {
        note("Mindst 6 tegn");
        return;
      } else {
        snprintf(pass_, sizeof(pass_), "%s", buf_);
        step_ = Pass2;
      }
      break;
    case Pass2:
      if (strcmp(pass_, buf_) != 0) {
        note("Adgangskoderne er forskellige");
        step_ = Pass;
      } else if (profile_io::exportTo(store_, pass_, result_, sizeof(result_), err, sizeof(err))) {
        // Kontrol: filen skal kunne læses med koden, før identiteten her slettes.
        profile_io::Info chk;
        if (!profile_io::peek(result_, pass_, chk, err, sizeof(err)) || !chk.identity) {
          note("Kontrol fejlede - intet slettet");
          step_ = Pass;
        } else {
          profile_io::retireIdentity(store_);
          step_ = Done;
          done_ms_ = millis();
          if (restart_) restart_(8000);  // genstart om 8 s (også hvis skærmen lukkes)
          note("Eksporteret og kontrolleret");
        }
      } else {
        note(err);
        step_ = Pass;
      }
      wipe(pass_, sizeof(pass_));
      break;
    case Confirm:
      if (profile_io::importFrom(store_, files_[sel_], pass_, info_, err, sizeof(err))) {
        wipe(pass_, sizeof(pass_));
        if (restart_) restart_(0);
      } else {
        note(err);
      }
      break;
    case Done:
      if (restart_) restart_(0);  // ny profil kræver genstart
      break;
  }
  wipe(buf_, sizeof(buf_));
}

void ProfileUi::key(char c) {
  const bool enter_key = c == '\r' || c == '\n';
  const bool bksp = c == 0x08 || c == 0x7F;
  if (enter_key) {
    enter();
    return;
  }
  if (step_ == Pass || step_ == Pass2) {
    const size_t n = strlen(buf_);
    if (bksp) {
      if (n) buf_[n - 1] = 0;
      else requestClose();
    } else if (uint8_t(c) >= 0x20 && n + 1 < sizeof(buf_)) {
      buf_[n] = c;
      buf_[n + 1] = 0;
    }
    return;
  }
  if (bksp && step_ != Done) requestClose();
}

}  // namespace dm
