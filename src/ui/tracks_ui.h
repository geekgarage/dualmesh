// SPDX-License-Identifier: MIT
// DualMesh – "Gemte spor" (kortets menu): optagne spor, nyeste først. Klik/Enter på et spor
// giver valgene "Vis på kortet" og "Slet sporet" (to klik). Backspace betyder altid tilbage
// (fra valgene til listen, fra listen til kortet), så man ikke kan slette ved et uheld.
#pragma once
#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "map/track.h"
#include "ui/panel.h"

namespace dm {

class TracksUi : public Panel {
 public:
  using ShowFn = void (*)(const char* name);
  void begin(TrackLog* t, ShowFn show) {
    t_ = t;
    show_ = show;
  }
  const char* title() const override { return "Gemte spor"; }
  void onOpen() override {
    n_ = t_ ? t_->list(names_, kMax) : 0;
    sel_ = top_ = 0;
    act_ = -1;
    asel_ = 0;
    del_armed_ = false;
  }
  void view(PanelView& v, int rows) override {
    v.clear();
    if (act_ >= 0) {  // valgene for ét spor
      char lab[40];
      pretty(names_[act_], lab, sizeof(lab));
      snprintf(v.status, sizeof(v.status), "%s", lab);
      PanelRow* a = v.add(PanelRow::Item, color::White, "Vis på kortet");
      if (a) a->sel = asel_ == 0;
      PanelRow* b = v.add(PanelRow::Item, color::Red, del_armed_ ? "Bekræft: slet sporet" : "Slet sporet");
      if (b) {
        b->sel = asel_ == 1;
        b->confirm = del_armed_;
      }
      snprintf(v.hint, sizeof(v.hint), "Klik: vælg   Backspace: tilbage");
      return;
    }
    if (t_ && t_->recording())
      snprintf(v.status, sizeof(v.status), "Optager: %s (%d punkter)", t_->name(), t_->live().count());
    else
      snprintf(v.status, sizeof(v.status), "Ingen optagelse i gang");
    if (t_ && t_->shownName()[0]) snprintf(v.status2, sizeof(v.status2), "Vises: %s", t_->shownName());
    if (!n_) v.add(PanelRow::Info, color::Gray, "Ingen gemte spor endnu.");
    if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
    v.scroll_up = top_ > 0;
    v.scroll_down = top_ + rows < n_;
    for (int r = 0; r < rows && top_ + r < n_; ++r) {
      const int i = top_ + r;
      char lab[40];
      pretty(names_[i], lab, sizeof(lab));
      const bool shown = t_ && !std::strcmp(t_->shownName(), names_[i]);
      PanelRow* w = v.add(PanelRow::Item, color::White, lab, color::Gray, "%s", shown ? "vises" : "");
      if (w) w->sel = i == sel_;
    }
    snprintf(v.note, sizeof(v.note), "Filerne: /dualmesh/tracks/ (csv og gpx)");
    snprintf(v.hint, sizeof(v.hint), "Klik: vis eller slet   Backspace: tilbage");
  }
  void tapRow(int i) override {
    if (act_ >= 0) {
      if (i == 0 || i == 1) {
        asel_ = i;
        click();
      }
      return;
    }
    if (i < 0 || top_ + i >= n_) return;
    sel_ = top_ + i;
    click();
  }
  void key(char c) override {
    if (c == '\r' || c == '\n') {
      click();
    } else if (c == 0x08 || c == 0x7F || c == 0x1B) {
      if (act_ >= 0) act_ = -1;  // tilbage til listen
      else requestClose();       // tilbage til kortet
      del_armed_ = false;
    }
  }
  void stepY(int dir) override {
    del_armed_ = false;
    if (act_ >= 0) {
      asel_ = asel_ + dir < 0 ? 0 : (asel_ + dir > 1 ? 1 : asel_ + dir);
      return;
    }
    sel_ += dir;
    if (sel_ < 0) sel_ = 0;
    if (sel_ >= n_) sel_ = n_ ? n_ - 1 : 0;
  }
  void click() override {
    if (act_ < 0) {  // åbn valgene for det valgte spor
      if (sel_ < n_) {
        act_ = sel_;
        asel_ = 0;
        del_armed_ = false;
      }
      return;
    }
    if (asel_ == 0) {
      if (show_) show_(names_[act_]);
    } else if (!del_armed_) {
      del_armed_ = true;  // første klik: bekræft
    } else {
      if (t_) t_->remove(names_[act_]);
      onOpen();
    }
  }
  bool needsRedraw() const override { return true; }

 private:
  // "20261122-143005" -> "22-11-2026 14:30"; andre navne uændret.
  static void pretty(const char* n, char* out, size_t len) {
    if (std::strlen(n) == 15 && n[8] == '-')
      snprintf(out, len, "%.2s-%.2s-%.4s %.2s:%.2s", n + 6, n + 4, n, n + 9, n + 11);
    else
      snprintf(out, len, "%s", n);
  }
  static constexpr int kMax = 40;
  TrackLog* t_ = nullptr;
  ShowFn show_ = nullptr;
  char names_[kMax][28] = {};
  int n_ = 0, sel_ = 0, top_ = 0;
  int act_ = -1, asel_ = 0;  // valgene for spor nr. act_ (-1 = listen)
  bool del_armed_ = false;
};

}  // namespace dm
