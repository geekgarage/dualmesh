// SPDX-License-Identifier: MIT
// DualMesh – "Kortpakker" (kortets menu, genvej k): pakkerne i /dualmesh/maps/ med navn, zoom
// og størrelse. Klik på en pakke = valgene "Gå til" og "Slet pakken" (to klik). Backspace =
// tilbage. Nye pakker lægges på med USB-drevet (Indst. > System > SD som USB-drev).
// Halvt hentede pakker (<fil>.part fra "Hent kortpakker") står nederst og kan slettes (to klik).
#pragma once
#include <Arduino.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>

#include "map/map_view.h"
#include "map/pmtiles.h"
#include "net/dl.h"
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
    scanParts();
  }
  void view(PanelView& v, int rows) override {
    v.clear();
    const int n = m_ ? m_->packCount() : 0;
    if (act_ >= n && act_ < n + nparts_) {  // halvt hentet fil
      const Part& pt = parts_[act_ - n];
      snprintf(v.status, sizeof(v.status), "%.60s", pt.name);
      snprintf(v.status2, sizeof(v.status2), "halvt hentet, %lu MB", (unsigned long)(pt.bytes >> 20));
      if (dl::busy()) {
        v.add(PanelRow::Info, color::Gray, "Stop hentningen først");
      } else {
        PanelRow* b = v.add(PanelRow::Item, color::Red, armed_ ? "Bekræft: slet filen" : "Slet den halvt hentede fil");
        if (b) {
          b->sel = true;
          b->confirm = armed_;
        }
      }
      snprintf(v.hint, sizeof(v.hint), "Klik: vælg   Backspace: tilbage");
      return;
    }
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
    if (!n && !nparts_) {
      v.add(PanelRow::Info, color::Gray, "Ingen kortpakker.");
      v.add(PanelRow::Info, color::Gray, "Læg .pmtiles-filer på via USB-drevet");
    }
    if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
    v.scroll_up = top_ > 0;
    v.scroll_down = top_ + rows < n + nparts_;
    for (int r = 0; r < rows && top_ + r < n + nparts_; ++r) {
      const int i = top_ + r;
      PanelRow* w;
      if (i < n) {
        const PmTiles* p = m_->pack(i);
        w = v.add(PanelRow::Item, color::White, p->name(), color::Gray, "z%u-%u  %lu MB", p->minZoom(),
                  p->maxZoom(), (unsigned long)((p->fileSize() + 524288u) / 1048576u));
      } else {
        const Part& pt = parts_[i - n];
        w = v.add(PanelRow::Item, color::White, pt.name, color::Yellow, "halvt hentet, %lu MB",
                  (unsigned long)(pt.bytes >> 20));
      }
      if (w) w->sel = i == sel_;
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
    if (!m_ || top_ + i >= m_->packCount() + nparts_ || i < 0) return;
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
    const int n = (m_ ? m_->packCount() : 0) + nparts_;
    sel_ += dir;
    if (sel_ < 0) sel_ = 0;
    if (sel_ >= n) sel_ = n ? n - 1 : 0;
  }
  void click() override {
    if (!m_) return;
    const int n = m_->packCount();
    if (act_ >= n) {  // halvt hentet: slet med to klik
      if (dl::busy()) return;
      if (!armed_) {
        armed_ = true;
        return;
      }
      if (act_ - n < nparts_) ::remove(parts_[act_ - n].path);
      act_ = -1;
      armed_ = false;
      sel_ = top_ = 0;
      scanParts();
      return;
    }
    if (act_ < 0) {
      if (sel_ < n + nparts_) {
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
  struct Part {
    char path[96];
    char name[48];
    uint64_t bytes;
  };
  static constexpr int kParts = 16;

  void scanParts() {
    nparts_ = 0;
    DIR* d = opendir("/sd/dualmesh/maps");
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
      const size_t len = std::strlen(e->d_name);
      if (len < 6 || std::strcmp(e->d_name + len - 5, ".part") != 0 || nparts_ >= kParts) continue;
      Part& p = parts_[nparts_];
      snprintf(p.path, sizeof(p.path), "/sd/dualmesh/maps/%s", e->d_name);
      struct stat st;
      if (stat(p.path, &st) != 0) continue;
      p.bytes = uint64_t(st.st_size);
      snprintf(p.name, sizeof(p.name), "%.*s", int(len - 5), e->d_name);  // uden .part
      if (char* dot = std::strstr(p.name, ".pmtiles")) *dot = 0;
      for (char* c = p.name; *c; ++c)
        if (*c == '-' || *c == '_') *c = ' ';
      ++nparts_;
    }
    closedir(d);
  }

  Part parts_[kParts] = {};
  int nparts_ = 0;
  MapView* m_ = nullptr;
  GoFn go_ = nullptr;
  int sel_ = 0, top_ = 0, act_ = -1, asel_ = 0;
  bool armed_ = false;
};

}  // namespace dm
