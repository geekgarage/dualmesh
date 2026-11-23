// SPDX-License-Identifier: MIT
// DualMesh – "Geocaches" (kortets menu): de nærmeste caches (fra GPS, ellers kortets midte),
// venstre/højre = filter (alle / ikke fundet). Klik på en cache = detaljer: type, størrelse,
// sværhed/terræn, afstand og kurs, hint (vises først ved klik) og valgene Navigér hertil,
// Vis på kortet, Fundet, Ikke fundet. Backspace = tilbage (detaljer -> liste -> kortet).
#pragma once
#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "map/caches.h"
#include "ui/panel.h"

namespace dm {

class CachesUi : public Panel {
 public:
  using HereFn = bool (*)(double& lat, double& lon);  // GPS-position (eller kortets midte)
  using ShowFn = void (*)(double lat, double lon);
  using FetchFn = const char* (*)();  // hent fra Opencaching (returnerer en besked)
  void begin(Caches* c, HereFn here, ShowFn show, FetchFn fetch = nullptr) {
    c_ = c;
    here_ = here;
    show_ = show;
    fetch_ = fetch;
  }
  const char* title() const override { return "Geocaches"; }
  void onOpen() override {
    det_ = -1;
    sel_ = top_ = 0;
    refresh();
  }
  void view(PanelView& v, int rows) override {
    v.clear();
    if (millis() - last_ms_ > 3000) refresh();  // afstande følger med, mens man går
    if (det_ >= 0) {
      detail(v);
      return;
    }
    static const char* const kFilter[3] = {"alle", "ikke fundet", "slettede"};
    snprintf(v.status, sizeof(v.status), "Vis: < %s >   (%d i alt)", kFilter[filter_], c_ ? c_->count() : 0);
    if (fetch_msg_[0] && millis() - fetch_ms_ < 6000) {
      snprintf(v.note, sizeof(v.note), "%s", fetch_msg_);
      v.note_fg = color::Yellow;
    }
    if (!c_ || !c_->count()) {
      v.add(PanelRow::Info, color::Gray, "Ingen caches indlæst.");
      v.add(PanelRow::Info, color::Gray, "GPX-filer: /dualmesh/caches/ på SD");
      v.add(PanelRow::Info, color::Gray, "Eller o: hent fra Opencaching");
      snprintf(v.hint, sizeof(v.hint), "o: hent fra Opencaching   Backspace: tilbage");
      return;
    }
    if (!have_pos_) snprintf(v.status2, sizeof(v.status2), "Ingen position: afstand fra kortets midte");
    if (!n_) v.add(PanelRow::Info, color::Gray, filter_ == 2 ? "Ingen slettede caches." : "Ingen caches her.");
    if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
    v.scroll_up = top_ > 0;
    v.scroll_down = top_ + rows < n_;
    for (int r = 0; r < rows && top_ + r < n_; ++r) {
      const Geocache& g = c_->at(idx_[top_ + r]);
      char lab[48], dist[24];
      snprintf(lab, sizeof(lab), "%s%.40s", g.found == 1 ? "\xE2\x9C\x93 " : "", g.name);
      fmtDist(geoDistance(lat_, lon_, g.lat, g.lon), dist, sizeof(dist));
      PanelRow* w = v.add(PanelRow::Item, Caches::typeColor(g.type), lab, color::Gray, "%s %s", dist,
                          compass(geoBearing(lat_, lon_, g.lat, g.lon)));
      if (w) w->sel = top_ + r == sel_;
    }
    if (c_->target() >= 0 && !v.note[0])
      snprintf(v.note, sizeof(v.note), "Navigerer til %s", c_->at(c_->target()).code);
    snprintf(v.hint, sizeof(v.hint), "Klik: detaljer  V/H: filter  o: hent  e: field notes");
  }
  void tapRow(int i) override {
    if (det_ >= 0) {
      if (i >= kActRow0 && i < kActRow0 + kActs) {
        asel_ = i - kActRow0;
        click();
      } else if (i == kHintRow) {
        show_hint_ = !show_hint_;
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
      if (det_ >= 0) det_ = -1;
      else requestClose();
    } else if ((c == 'h' || c == 'H') && det_ >= 0) {
      show_hint_ = !show_hint_;
    } else if ((c == 'o' || c == 'O') && det_ < 0 && fetch_) {
      snprintf(fetch_msg_, sizeof(fetch_msg_), "%s", fetch_());
      fetch_ms_ = millis();
    } else if ((c == 'e' || c == 'E') && det_ < 0 && c_) {  // field notes til geocaching.com/Opencaching
      const int n = c_->exportFieldNotes();
      if (n < 0) snprintf(fetch_msg_, sizeof(fetch_msg_), "Field notes: kan ikke skrive på SD");
      else snprintf(fetch_msg_, sizeof(fetch_msg_), "%d field notes i caches/geocache_visits.txt", n);
      fetch_ms_ = millis();
    }
  }
  void stepY(int dir) override {
    del_armed_ = false;
    if (det_ >= 0) {
      asel_ = asel_ + dir < 0 ? 0 : (asel_ + dir >= kActs ? kActs - 1 : asel_ + dir);
      return;
    }
    sel_ += dir;
    if (sel_ < 0) sel_ = 0;
    if (sel_ >= n_) sel_ = n_ ? n_ - 1 : 0;
  }
  bool wantsX() const override { return det_ < 0; }
  void stepX(int dir) override {
    filter_ = (filter_ + 3 + (dir > 0 ? 1 : -1)) % 3;
    sel_ = top_ = 0;
    refresh();
  }
  void click() override {
    if (!c_) return;
    if (det_ < 0) {
      if (sel_ < n_) {
        det_ = idx_[sel_];
        asel_ = 0;
        show_hint_ = false;
        del_armed_ = false;
      }
      return;
    }
    const Geocache& g = c_->at(det_);
    if (asel_ != 4) del_armed_ = false;
    switch (asel_) {
      case 4:  // slet fra listen (to klik) / gendan
        if (g.found == 3) {
          c_->mark(det_, 0);
          det_ = -1;
          refresh();
        } else if (!del_armed_) {
          del_armed_ = true;
        } else {
          c_->mark(det_, 3);
          det_ = -1;
          del_armed_ = false;
          refresh();
        }
        return;
      case 0:  // navigér / stop
        c_->setTarget(c_->target() == det_ ? -1 : det_);
        if (c_->target() >= 0 && show_) show_(g.lat, g.lon);
        break;
      case 1:
        if (show_) show_(g.lat, g.lon);
        break;
      case 2:
        c_->mark(det_, g.found == 1 ? 0 : 1);
        break;
      case 3:
        c_->mark(det_, g.found == 2 ? 0 : 2);
        break;
    }
  }
  bool needsRedraw() const override { return true; }

 private:
  // Detaljer: række 0 type/størrelse/D/T, 1 hint, 2-6 valgene (7 rækker i et panel).
  static constexpr int kList = 30, kActs = 5, kHintRow = 1, kActRow0 = 2;
  static void fmtDist(double m, char* out, size_t n) {
    if (m < 1000) snprintf(out, n, "%d m", int(m + 0.5));
    else if (m < 10000) snprintf(out, n, "%.1f km", m / 1000);
    else snprintf(out, n, "%d km", int(m / 1000 + 0.5));
  }
  void refresh() {
    last_ms_ = millis();
    have_pos_ = here_ && here_(lat_, lon_);
    n_ = c_ ? c_->nearest(lat_, lon_, filter_, idx_, kList) : 0;
    if (sel_ >= n_) sel_ = n_ ? n_ - 1 : 0;
  }
  void detail(PanelView& v) {
    const Geocache& g = c_->at(det_);
    snprintf(v.title, sizeof(v.title), "%s", g.code);
    snprintf(v.status, sizeof(v.status), "%.60s", g.name);
    v.status_fg = Caches::typeColor(g.type);
    char dist[24];
    fmtDist(geoDistance(lat_, lon_, g.lat, g.lon), dist, sizeof(dist));
    snprintf(v.status2, sizeof(v.status2), "%s %s (%.0f°)%s", dist, compass(geoBearing(lat_, lon_, g.lat, g.lon)),
             geoBearing(lat_, lon_, g.lat, g.lon), have_pos_ ? "" : " fra kortets midte");
    char tl[32];
    snprintf(tl, sizeof(tl), "%s", Caches::typeName(g.type));
    v.info(tl, "%s  D%d%s/T%d%s", Caches::sizeName(g.size), g.diff2 / 2, g.diff2 % 2 ? ".5" : "", g.terr2 / 2,
           g.terr2 % 2 ? ".5" : "");
    const char* h = c_->hint(g);
    v.info("Hint", "%s", !*h ? "intet" : show_hint_ ? "" : "(klik/h for at se)");
    const char* acts[kActs] = {c_->target() == det_ ? "Stop navigation" : "Navigér hertil", "Vis på kortet",
                               g.found == 1 ? "Fortryd fundet" : "Fundet",
                               g.found == 2 ? "Fortryd ikke fundet" : "Ikke fundet",
                               g.found == 3 ? "Gendan i listen" : del_armed_ ? "Bekræft: slet fra listen"
                                                                            : "Slet fra listen"};
    for (int i = 0; i < kActs; ++i) {
      PanelRow* w = v.add(PanelRow::Item, i == 4 && g.found != 3 ? color::Red : color::White, acts[i]);
      if (w) {
        w->sel = asel_ == i;
        w->confirm = i == 4 && del_armed_;
      }
    }
    if (show_hint_ && *h) {
      snprintf(v.note, sizeof(v.note), "%.63s", h);
      v.note_fg = color::Yellow;
    }
    snprintf(v.hint, sizeof(v.hint), "Klik: vælg   h: hint   Backspace: tilbage");
  }
  Caches* c_ = nullptr;
  HereFn here_ = nullptr;
  ShowFn show_ = nullptr;
  FetchFn fetch_ = nullptr;
  char fetch_msg_[64] = "";
  uint32_t fetch_ms_ = 0;
  int idx_[kList] = {};
  int n_ = 0, sel_ = 0, top_ = 0, det_ = -1, asel_ = 0;
  int filter_ = 0;
  bool have_pos_ = false, show_hint_ = false, del_armed_ = false;
  double lat_ = 0, lon_ = 0;
  uint32_t last_ms_ = 0;
};

}  // namespace dm
