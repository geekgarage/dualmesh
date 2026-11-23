// SPDX-License-Identifier: MIT
#include "ui/reset_ui.h"

#include <cstdio>
#include <cstring>

#include "storage/profile_io.h"
#include "storage/sd_log.h"

namespace dm {

namespace {
void wipe(char* p, size_t n) {
  volatile char* v = p;
  while (n--) *v++ = 0;
}
const char* baseName(const char* p) {
  const char* s = strrchr(p, '/');
  return s ? s + 1 : p;
}
}  // namespace

void ResetUi::onOpen() {
  step_ = Choose;
  sel_ = 0;
  exported_ = false;
  file_[0] = 0;
  wipe(buf_, sizeof(buf_));
  wipe(pass_, sizeof(pass_));
}

void ResetUi::note(const char* t) {
  snprintf(note_, sizeof(note_), "%s", t);
  note_ms_ = millis();
}

void ResetUi::view(PanelView& v, int) {
  last_draw_ms_ = millis();
  v.clear();
  snprintf(v.status, sizeof(v.status), "Nulstil DualMesh");
  v.status_fg = color::Red;
  snprintf(v.status2, sizeof(v.status2), "Bevares: eksporterede profiler, launcher og NVS");
  v.add(PanelRow::Info, color::White, "Sletter indstillinger, identitet,");
  v.add(PanelRow::Info, color::White, "kontakter, kanaler, beskeder, WiFi-net,");
  v.add(PanelRow::Info, color::White, "logs og lærte radioværdier.");
  v.add(PanelRow::Blank, color::Gray, "");
  if (step_ == Choose) {
    PanelRow* a = v.add(PanelRow::Item, color::Gold, "Eksportér profil og nulstil");
    PanelRow* b = v.add(PanelRow::Item, color::White, "Nulstil uden eksport");
    if (a) a->sel = sel_ == 0;
    if (b) b->sel = sel_ == 1;
  } else if (step_ == Confirm) {
    if (exported_) v.add(PanelRow::Info, color::Gray, "Eksporteret", color::Green, "%.30s", baseName(file_));
    else v.add(PanelRow::Info, color::Yellow, "Ingen eksport: identiteten forsvinder.");
    v.add(PanelRow::Header, color::Red, "Skriv NULSTIL og tryk Enter");
  } else {
    v.add(PanelRow::Info, color::White, "Adgangskode til profil-filen");
    v.add(PanelRow::Info, color::Gray, "(mindst 6 tegn; bruges ved import).");
  }
  if (note_[0] && millis() - note_ms_ < 5000) {
    snprintf(v.note, sizeof(v.note), "%s", note_);
    v.note_fg = color::Green;
  } else if (step_ == Choose) {
    snprintf(v.note, sizeof(v.note), "%s",
             sel_ == 0 ? "Profilen kan importeres igen bagefter." : "Identiteten kan IKKE gendannes!");
    v.note_fg = sel_ == 0 ? color::Gray : color::Yellow;
  } else {
    snprintf(v.note, sizeof(v.note), "%s",
             step_ == Pass ? "Ny adgangskode:" : step_ == Pass2 ? "Gentag adgangskoden:" : "Bekræft:");
    v.note_fg = color::Yellow;
  }
  if (step_ == Choose) {
    snprintf(v.hint, sizeof(v.hint), "Op/ned: vælg   Klik: OK   Backspace: tilbage");
  } else {
    v.input = true;
    size_t k = 0;
    for (size_t i = 0; buf_[i] && k < sizeof(v.input_text) - 2; ++i)
      v.input_text[k++] = step_ == Confirm ? buf_[i] : '*';
    v.input_text[k++] = '_';
    v.input_text[k] = 0;
  }
  return;
}

void ResetUi::tapRow(int i) {
  if (step_ != Choose) return;
  if (i == 4 || i == 5) {  // rækkerne efter de 3 forklaringslinjer og den tomme
    sel_ = i - 4;
    enter();
  }
}

void ResetUi::stepY(int dir) {
  if (step_ != Choose) return;
  sel_ = sel_ + dir < 0 ? 0 : (sel_ + dir > 1 ? 1 : sel_ + dir);
}

void ResetUi::click() { enter(); }

void ResetUi::enter() {
  char err[48] = "";
  switch (step_) {
    case Choose:
      step_ = sel_ == 0 ? Pass : Confirm;
      break;
    case Pass:
      if (strlen(buf_) < 6) {
        note("Mindst 6 tegn");
        return;
      }
      snprintf(pass_, sizeof(pass_), "%s", buf_);
      step_ = Pass2;
      break;
    case Pass2:
      if (strcmp(pass_, buf_) != 0) {
        note("Adgangskoderne er forskellige");
        step_ = Pass;
      } else {
        profile_io::Info chk;
        if (profile_io::exportTo(store_, pass_, file_, sizeof(file_), err, sizeof(err)) &&
            profile_io::peek(file_, pass_, chk, err, sizeof(err)) && chk.identity) {
          exported_ = true;
          step_ = Confirm;
        } else {
          note(err[0] ? err : "Eksport fejlede - intet slettet");
          step_ = Choose;
        }
      }
      wipe(pass_, sizeof(pass_));
      break;
    case Confirm:
      if (strcmp(buf_, "NULSTIL") != 0) {
        note("Fortrudt (ikke NULSTIL)");
        step_ = Choose;
      } else {
        DLOG.printf("[SD] Nulstil DualMesh fra menuen (eksport: %s)\n", exported_ ? file_ : "nej");
        const bool ok = reset_ && reset_();
        note(ok ? "Nulstillet - genstarter" : "Nulstilling fejlede delvist");
        if (restart_) restart_(0);  // straks: intet i RAM må skrive filerne igen
      }
      break;
  }
  wipe(buf_, sizeof(buf_));
}

void ResetUi::key(char c) {
  const bool enter_key = c == '\r' || c == '\n';
  const bool bksp = c == 0x08 || c == 0x7F;
  if (enter_key) {
    enter();
    return;
  }
  if (step_ == Choose) {
    if (bksp) requestClose();
    return;
  }
  const size_t n = strlen(buf_);
  if (bksp) {
    if (n) buf_[n - 1] = 0;
    else step_ = Choose;
  } else if (uint8_t(c) >= 0x20 && n + 1 < sizeof(buf_)) {
    buf_[n] = c;
    buf_[n + 1] = 0;
  }
}

}  // namespace dm
