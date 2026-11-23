// SPDX-License-Identifier: MIT
// DualMesh – "Kortpakker" (kortets menu, genvej k): pakkerne i /dualmesh/maps/ med navn, zoom
// og størrelse. Klik på en pakke = valgene "Gå til" og "Slet pakken" (to klik). Backspace =
// tilbage. Nye pakker lægges på med USB-drevet (Indst. > System > SD som USB-drev).
#pragma once
#include <Arduino.h>

#include <cstdio>

#include "map/map_view.h"
#include "map/pmtiles.h"
#include "ui/panel.h"

namespace dm {

class MapsUi : public Panel {
 public:
  using GoFn = void (*)(int pack);  // vis pakken på kortet
  void begin(MapView* m, GoFn go) {
    m_ = m;
    go_ = go;
  }
  const char* title() const override { return "Kortpakker"; }
  void onOpen() override {
    sel_ = top_ = 0;
    act_ = -1;
    armed_ = false;
  }
  void view(PanelView& v, int rows) override {
    v.clear();
    const int n = m_ ? m_->packCount() : 0;
    if (act_ >= 0 && act_ < n) {
      const PmTiles* p = m_->pack(act_);
      snprintf(v.status, sizeof(v.status), "%.60s", p->name());
      snprintf(v.status2, sizeof(v.status2), "zoom %u-%u, %lu MB", p->minZoom(), p->maxZoom(),
               (unsigned long)(p->fileSize() / 1048576u));
      PanelRow* a = v.add(PanelRow::Item, color::White, "Gå til");
      if (a) a->sel = asel_ == 0;
      PanelRow* b = v.add(PanelRow::Item, color::Red, armed_ ? "Bekræft: slet pakken" : "Slet pakken");
      if (b) {
        b->sel = asel_ == 1;
        b->confirm = armed_;
      }
      snprintf(v.hint, sizeof(v.hint), "Klik: vælg   Backspace: tilbage");
      return;
    }
    snprintf(v.status, sizeof(v.status), "%d kortpakke(r) i /dualmesh/maps/", n);
    if (!n) {
      v.add(PanelRow::Info, color::Gray, "Ingen kortpakker.");
      v.add(PanelRow::Info, color::Gray, "Læg .pmtiles-filer på via USB-drevet");
    }
    if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
    v.scroll_up = top_ > 0;
    v.scroll_down = top_ + rows < n;
    for (int r = 0; r < rows && top_ + r < n; ++r) {
      const PmTiles* p = m_->pack(top_ + r);
      PanelRow* w = v.add(PanelRow::Item, color::White, p->name(), color::Gray, "z%u-%u  %lu MB", p->minZoom(),
                          p->maxZoom(), (unsigned long)((p->fileSize() + 524288u) / 1048576u));
      if (w) w->sel = top_ + r == sel_;
    }
    snprintf(v.note, sizeof(v.note), "© OpenStreetMap-bidragydere");
    snprintf(v.hint, sizeof(v.hint), "Klik: gå til eller slet   Backspace: tilbage");
  }
  void tapRow(int i) override {
    if (act_ >= 0) {
      if (i == 0 || i == 1) {
        asel_ = i;
        click();
      }
      return;
    }
    if (!m_ || top_ + i >= m_->packCount() || i < 0) return;
    sel_ = top_ + i;
    click();
  }
  void key(char c) override {
    if (c == '\r' || c == '\n') {
      click();
    } else if (c == 0x08 || c == 0x7F || c == 0x1B) {
      if (act_ >= 0) act_ = -1;
      else requestClose();
      armed_ = false;
    }
  }
  void stepY(int dir) override {
    armed_ = false;
    if (act_ >= 0) {
      asel_ = dir > 0 ? 1 : 0;
      return;
    }
    const int n = m_ ? m_->packCount() : 0;
    sel_ += dir;
    if (sel_ < 0) sel_ = 0;
    if (sel_ >= n) sel_ = n ? n - 1 : 0;
  }
  void click() override {
    if (!m_) return;
    if (act_ < 0) {
      if (sel_ < m_->packCount()) {
        act_ = sel_;
        asel_ = 0;
        armed_ = false;
      }
      return;
    }
    if (asel_ == 0) {
      if (go_) go_(act_);
    } else if (!armed_) {
      armed_ = true;
    } else {
      m_->removePack(act_);
      act_ = -1;
      armed_ = false;
      sel_ = top_ = 0;
    }
  }
  bool needsRedraw() const override { return true; }

 private:
  MapView* m_ = nullptr;
  GoFn go_ = nullptr;
  int sel_ = 0, top_ = 0, act_ = -1, asel_ = 0;
  bool armed_ = false;
};

}  // namespace dm
