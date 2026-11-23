// SPDX-License-Identifier: MIT
#include "ui/crypto_ui.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include <lvgl.h>  // LV_SYMBOL_OK

#include "ui/status_display.h"

namespace dm {

namespace {
struct Opt {
  const char* line;
  const char* d1;
  const char* d2;
};
const Opt kOpts[4] = {
    {"fra      almindelig tekst", "Filerne gemmes ukrypteret.", "Alle med SD-kortet kan læse koderne."},
    {"chip-ID  låst til denne T-Deck", "Beskytter et tabt SD-kort. Svag: ID'et", "kan læses af den, der har T-Deck'en."},
    {"kode     kode ved hver opstart", "Stærk. Skriv koden ved opstart. Glemt", "kode = WiFi-koder/nøgler/identitet tabt."},
    {"eFuse    nøgle i chippen", "Stærkest: nøglen kan ikke læses ud.", "Kun denne T-Deck kan læse filerne."},
};
}  // namespace

void CryptoUi::onOpen() {
  sel_ = int(sdcrypt::mode());
  armed_ = -1;
  input_ = sdcrypt::locked() && sdcrypt::mode() == sdcrypt::Code ? Unlock : None;
  buf_[0] = 0;
}

void CryptoUi::note(const char* t) {
  snprintf(note_, sizeof(note_), "%s", t);
  note_ms_ = millis();
}

void CryptoUi::view(PanelView& v, int) {
  last_draw_ms_ = millis();
  v.clear();
  snprintf(v.status, sizeof(v.status), "SD-kryptering: %s", sdcrypt::status());
  v.status_fg = sdcrypt::locked() ? color::Red : color::White;
  if (sdcrypt::locked() && sdcrypt::mode() == sdcrypt::Code) {
    v.add(PanelRow::Info, color::White, "WiFi-koder, kanalnøgler og MeshCore-");
    v.add(PanelRow::Info, color::White, "identiteten er krypteret med en kode.");
    v.add(PanelRow::Info, color::Yellow, "Skriv koden og tryk Enter.");
    v.add(PanelRow::Blank, color::Gray, "");
    v.add(PanelRow::Info, color::Gray, "Radioen kører imens; MeshCore og WiFi");
    v.add(PanelRow::Info, color::Gray, "starter, når koden er skrevet.");
  } else if (sdcrypt::locked()) {
    v.add(PanelRow::Info, color::Red, "Ingen eFuse-nøgle i denne chip:");
    v.add(PanelRow::Info, color::White, "de hemmelige filer kan ikke læses her.");
    v.add(PanelRow::Blank, color::Gray, "");
    v.add(PanelRow::Info, color::Gray, "Brug SD-kortet i den rigtige T-Deck,");
    v.add(PanelRow::Info, color::Gray, "eller nulstil DualMesh (R + JA, USB).");
  } else {
    const int kb = sdcrypt::efuseKeyBlock();
    if (kb >= 0) snprintf(v.status2, sizeof(v.status2), "eFuse: nøgle findes (blok %d) og kan bruges", kb);
    else snprintf(v.status2, sizeof(v.status2), "eFuse: ingen nøgle, %d frie pladser", sdcrypt::efuseFreeBlocks());
    static const char* const kNames[4] = {"Fra", "Chip-ID", "Kode", "eFuse"};
    static const char* const kWhat[4] = {"almindelig tekst", "låst til denne T-Deck", "kode ved hver opstart",
                                         "nøgle i chippen"};
    for (int i = 0; i < 4; ++i) {
      const bool cur = i == int(sdcrypt::mode());
      char lab[32];
      snprintf(lab, sizeof(lab), "%s%s", cur ? LV_SYMBOL_OK " " : "", kNames[i]);
      PanelRow* r = v.add(PanelRow::Item, cur ? color::Green : color::White, lab, color::Gray, "%s", kWhat[i]);
      if (r && i == sel_ && input_ == None) {
        r->sel = true;
        r->confirm = armed_ == i && millis() - armed_ms_ < 4000;
      }
    }
    v.add(PanelRow::Blank, color::Gray, "");
    v.add(PanelRow::Info, color::White, kOpts[sel_].d1);
    v.add(PanelRow::Info, color::White, kOpts[sel_].d2);
  }
  if (note_[0] && millis() - note_ms_ < 5000) {
    snprintf(v.note, sizeof(v.note), "%s", note_);
    v.note_fg = color::Green;
  } else if (armed_ >= 0 && millis() - armed_ms_ < 4000) {
    snprintf(v.note, sizeof(v.note), "%s",
             armed_ == int(sdcrypt::Efuse) ? "Klik igen: brug eFuse-nøglen" : "Klik igen for at skifte");
    v.note_fg = color::Yellow;
  } else if (input_ != None) {
    const char* q = input_ == Unlock ? "Kode:" : input_ == NewCode ? "Ny kode (mindst 4 tegn):"
                  : input_ == RepeatCode ? "Gentag koden:" : "UOMVENDELIGT! Skriv BRUG EFUSE og tryk Enter:";
    snprintf(v.note, sizeof(v.note), "%s", q);
    v.note_fg = input_ == Burn ? color::Red : color::Yellow;
  } else if (!sdcrypt::locked()) {
    snprintf(v.note, sizeof(v.note), LV_SYMBOL_OK " = nuværende");
  }
  if (input_ != None) {
    v.input = true;
    const size_t n = strlen(buf_);
    if (input_ == Burn) {
      snprintf(v.input_text, sizeof(v.input_text), "%s_", buf_);
    } else {  // koder vises som stjerner
      size_t k = 0;
      for (; k < n && k < sizeof(v.input_text) - 2; ++k) v.input_text[k] = '*';
      v.input_text[k++] = '_';
      v.input_text[k] = 0;
    }
  } else {
    snprintf(v.hint, sizeof(v.hint), "Op/ned: vælg   Klik: skift   Backspace: tilbage");
  }
  return;
}

void CryptoUi::tapRow(int i) {
  if (input_ != None || sdcrypt::locked() || i < 0 || i > 3) return;
  if (sel_ != i) armed_ = -1;
  sel_ = i;
  click();
}

void CryptoUi::stepY(int dir) {
  if (input_ != None || sdcrypt::locked()) return;
  sel_ += dir;
  if (sel_ < 0) sel_ = 0;
  if (sel_ > 3) sel_ = 3;
  armed_ = -1;
}

void CryptoUi::finish(sdcrypt::Mode m, const char* code) {
  char err[48];
  if (sdcrypt::change(m, code, err, sizeof(err))) {
    if (on_mode_) on_mode_(m);
    char t[48];
    snprintf(t, sizeof(t), "Skiftet til %s", sdcrypt::modeName(m));
    note(t);
  } else {
    note(err);
  }
}

void CryptoUi::choose() {
  const sdcrypt::Mode m = sdcrypt::Mode(sel_);
  if (m == sdcrypt::mode()) {
    note("Er allerede valgt");
    return;
  }
  if (m == sdcrypt::Code) {
    input_ = NewCode;
    buf_[0] = 0;
    return;
  }
  if (m == sdcrypt::Efuse && sdcrypt::efuseKeyBlock() < 0) {
    if (sdcrypt::efuseFreeBlocks() <= 0) {
      note("Ingen fri eFuse-plads");
      return;
    }
    input_ = Burn;
    buf_[0] = 0;
    return;
  }
  if (armed_ == sel_ && millis() - armed_ms_ < 4000) {
    armed_ = -1;
    finish(m, nullptr);
  } else {
    armed_ = sel_;
    armed_ms_ = millis();
  }
}

void CryptoUi::click() {
  if (input_ != None) {
    key('\r');
    return;
  }
  if (sdcrypt::locked()) return;
  choose();
}

void CryptoUi::key(char c) {
  const bool enter = c == '\r' || c == '\n';
  const bool bksp = c == 0x08 || c == 0x7F;
  if (input_ == None) {
    if (enter) click();
    else if (bksp) requestClose();
    return;
  }
  const size_t n = strlen(buf_);
  if (bksp) {
    if (n) {
      size_t l = n;
      do {
        --l;
      } while (l && (uint8_t(buf_[l]) & 0xC0) == 0x80);
      buf_[l] = 0;
    } else if (input_ == Unlock) {
      requestClose();  // kan åbnes igen fra Indst. > System > SD-kryptering
    } else {
      input_ = None;
    }
    return;
  }
  if (!enter) {
    if (uint8_t(c) >= 0x20 && n + 1 < sizeof(buf_)) {
      buf_[n] = c;
      buf_[n + 1] = 0;
    }
    return;
  }
  switch (input_) {
    case Unlock:
      if (sdcrypt::unlock(buf_)) {
        input_ = None;
        note("Låst op");
        if (on_unlocked_) on_unlocked_();
        requestClose();
      } else {
        note("Forkert kode");
      }
      break;
    case NewCode:
      if (n < 4) {
        note("Mindst 4 tegn");
        return;
      }
      snprintf(first_, sizeof(first_), "%s", buf_);
      input_ = RepeatCode;
      break;
    case RepeatCode:
      input_ = None;
      if (strcmp(first_, buf_) != 0) note("Koderne er forskellige");
      else finish(sdcrypt::Code, buf_);
      memset(first_, 0, sizeof(first_));
      break;
    case Burn:
      input_ = None;
      if (strcmp(buf_, "BRUG EFUSE") != 0) {
        note("Fortrudt (ikke BRUG EFUSE)");
      } else if (sdcrypt::efuseBurnNew() < 0) {
        note("eFuse-brænding fejlede");
      } else {
        finish(sdcrypt::Efuse, nullptr);
      }
      break;
    default:
      break;
  }
  memset(buf_, 0, sizeof(buf_));
}

}  // namespace dm
