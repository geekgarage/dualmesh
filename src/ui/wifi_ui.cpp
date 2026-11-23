// SPDX-License-Identifier: MIT
#include "ui/wifi_ui.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <lvgl.h>  // LV_SYMBOL_OK

#include "net/wifi_mgr.h"
#include "ui/status_display.h"
#include "util/wall_clock.h"

namespace dm {

void WifiUi::onOpen() {
  sel_ = 0;
  top_ = 0;
  input_ = 0;
  armed_ = -1;
  drawn_version_ = 0;
  // Åbnes skærmen med WiFi slået til, scannes der med det samme.
  if (wifi_ && wifi_->enabled() && !wifi_->scanning()) wifi_->startScan();
}

void WifiUi::note(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(note_, sizeof(note_), fmt, ap);
  va_end(ap);
  note_ms_ = millis();
}

bool WifiUi::needsRedraw() const {
  if (!wifi_) return false;
  return wifi_->version() != drawn_version_ || millis() - last_draw_ms_ > 1000;
}

int WifiUi::collect(Item* out) const {
  int n = 0;
  out[n++] = {Toggle, 0};
  if (!wifi_ || !wifi_->enabled()) return n;
  out[n++] = {Scan, 0};
  out[n++] = {Hidden, 0};
  for (int i = 0; i < wifi_->netCount() && n < kMaxItems; ++i) out[n++] = {Seen, i};
  // Gemte netværk, der ikke er i scannet (fx skjulte eller uden for rækkevidde).
  for (int k = 0; k < wifi_->knownCount() && n < kMaxItems; ++k) {
    bool seen = false;
    for (int i = 0; i < wifi_->netCount(); ++i)
      if (std::strcmp(wifi_->net(i).ssid, wifi_->known(k).ssid) == 0) seen = true;
    if (!seen) out[n++] = {KnownOnly, k};
  }
  return n;
}

const char* WifiUi::itemSsid(const Item& it) const {
  if (it.type == Seen) return wifi_->net(it.idx).ssid;
  if (it.type == KnownOnly) return wifi_->known(it.idx).ssid;
  return "";
}

// LVGL-modellen (samme indhold som draw(), men som rækker med etiket og værdi).
void WifiUi::view(PanelView& v, int rows) {
  last_draw_ms_ = millis();
  v.clear();
  if (!wifi_) return;
  drawn_version_ = wifi_->version();
  using WS = WifiMgr::State;
  const WS st = wifi_->state();
  char ip[20];
  if (st == WS::Connected) {
    snprintf(v.status, sizeof(v.status), "Forbundet til %.40s", wifi_->ssid());
    v.status_fg = color::Green;
    wifi_->ipText(ip, sizeof(ip));
    snprintf(v.status2, sizeof(v.status2), "IP %s   signal %d dBm", ip, wifi_->rssi());
  } else if (st == WS::Connecting) {
    snprintf(v.status, sizeof(v.status), "Forbinder til %.40s ...", wifi_->ssid());
    v.status_fg = color::Yellow;
  } else if (st == WS::Failed && wifi_->lastError()[0]) {
    snprintf(v.status, sizeof(v.status), "Fejl: %.50s", wifi_->ssid());
    v.status_fg = color::Red;
    snprintf(v.status2, sizeof(v.status2), "%.60s", wifi_->lastError());
  } else {
    snprintf(v.status, sizeof(v.status), "WiFi: %s", wifi_->stateText());
    v.status_fg = st == WS::Off ? color::Gray : color::White;
    if (wifi_->scanning()) snprintf(v.status2, sizeof(v.status2), "Scanner ...");
  }
  char hm[8];
  wallclock::formatHm(wallclock::now(), hm, sizeof(hm));
  if (!wallclock::valid())
    snprintf(v.status3, sizeof(v.status3), "Tid: ukendt (venter på NTP, GPS eller mesh)");
  else
    snprintf(v.status3, sizeof(v.status3), "Tid: %s fra %s%s", hm, wallclock::sourceName(wallclock::source()),
             wifi_->lastNtpMs() ? "   NTP ok" : "");

  static Item items[kMaxItems];
  const int n = collect(items);
  if (sel_ >= n) sel_ = n - 1;
  if (sel_ < 0) sel_ = 0;
  if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
  v.scroll_up = top_ > 0;
  v.scroll_down = top_ + rows < n;
  for (int r = 0; r < rows && top_ + r < n; ++r) {
    const int k = top_ + r;
    const Item& it = items[k];
    PanelRow& w = v.rows[v.nrows++];
    w.kind = PanelRow::Item;
    w.sel = k == sel_ && !input_;
    w.confirm = w.sel && armed_ == k && millis() - armed_ms_ < 4000;
    switch (it.type) {
      case Toggle:
        snprintf(w.label, sizeof(w.label), "WiFi");
        snprintf(w.value, sizeof(w.value), "%s", wifi_->enabled() ? "TIL" : "FRA");
        w.vfg = wifi_->enabled() ? color::Green : color::Gray;
        break;
      case Scan:
        snprintf(w.label, sizeof(w.label), "Scan efter netværk");
        snprintf(w.value, sizeof(w.value), "%s", wifi_->scanning() ? "scanner ..." : "");
        break;
      case Hidden:
        snprintf(w.label, sizeof(w.label), "Forbind til skjult netværk ...");
        break;
      case Seen: {
        const WifiMgr::Net& e = wifi_->net(it.idx);
        const bool cur = st == WS::Connected && std::strcmp(e.ssid, wifi_->ssid()) == 0;
        snprintf(w.label, sizeof(w.label), "%s%.32s", cur ? LV_SYMBOL_OK " " : "", e.ssid);
        snprintf(w.value, sizeof(w.value), "%s%d dBm  %s", e.known ? "gemt  " : "", int(e.rssi),
                 e.secure ? "låst" : "åben");
        w.fg = cur ? color::Green : (e.known ? color::Yellow : color::White);
        break;
      }
      case KnownOnly: {
        const WifiMgr::Known& kk = wifi_->known(it.idx);
        snprintf(w.label, sizeof(w.label), "%.32s", kk.ssid);
        snprintf(w.value, sizeof(w.value), "gemt  %s", kk.hidden ? "skjult" : "ikke set");
        w.fg = color::Gray;
        break;
      }
    }
  }
  if (n == 1) {  // kun "WiFi: FRA"
    PanelRow& w = v.rows[v.nrows++];
    w.kind = PanelRow::Info;
    snprintf(w.label, sizeof(w.label), "Slå WiFi til for at scanne");
    w.fg = color::Gray;
  }

  if (note_[0] && millis() - note_ms_ < 5000) {
    snprintf(v.note, sizeof(v.note), "%s", note_);
    v.note_fg = color::Green;
  } else if (armed_ >= 0 && millis() - armed_ms_ < 4000) {
    snprintf(v.note, sizeof(v.note), "%s",
             armed_kind_ == 1 ? "Klik igen for at afbryde forbindelsen" : "Backspace igen for at glemme netværket");
    v.note_fg = color::Yellow;
  } else if (input_ == 1) {
    snprintf(v.note, sizeof(v.note), "Navn (SSID) på det skjulte netværk:");
    v.note_fg = color::Yellow;
  } else if (input_ == 2) {
    snprintf(v.note, sizeof(v.note), "Kode til %.40s:", in_ssid_);
    v.note_fg = color::Yellow;
  } else {
    snprintf(v.note, sizeof(v.note), LV_SYMBOL_OK " = forbundet   gul = gemt");
  }
  if (input_) {
    v.input = true;
    const size_t len = std::strlen(in_buf_);
    snprintf(v.input_text, sizeof(v.input_text), "%s_", len > 60 ? in_buf_ + (len - 60) : in_buf_);
  } else {
    snprintf(v.hint, sizeof(v.hint), "Klik: vælg/forbind   Backspace: glem eller tilbage");
  }
  return;
}

void WifiUi::tapRow(int i) {
  if (input_ || i < 0) return;
  sel_ = top_ + i;
  click();
}

void WifiUi::stepY(int dir) {
  if (input_) return;
  sel_ += dir;
  if (sel_ < 0) sel_ = 0;
  armed_ = -1;
}

void WifiUi::activate(const Item& it) {
  switch (it.type) {
    case Toggle:
      wifi_->setEnabled(!wifi_->enabled(), true);
      if (wifi_->enabled()) wifi_->startScan();
      note(wifi_->enabled() ? "WiFi slået til" : "WiFi slået fra");
      return;
    case Scan:
      wifi_->startScan();
      return;
    case Hidden:
      input_ = 1;
      input_hidden_ = true;
      in_buf_[0] = 0;
      return;
    case Seen:
    case KnownOnly: {
      const char* ssid = itemSsid(it);
      const bool cur = wifi_->state() == WifiMgr::State::Connected && std::strcmp(ssid, wifi_->ssid()) == 0;
      const int sel = sel_;
      if (cur) {
        if (armed_ == sel && armed_kind_ == 1 && millis() - armed_ms_ < 4000) {
          armed_ = -1;
          wifi_->disconnect();
          note("Afbrudt");
        } else {
          armed_ = sel;
          armed_kind_ = 1;
          armed_ms_ = millis();
        }
        return;
      }
      const int k = wifi_->findKnown(ssid);
      if (k >= 0) {
        const WifiMgr::Known& kn = wifi_->known(k);
        wifi_->connect(kn.ssid, kn.pass, kn.hidden, false);
        note("Forbinder til %.28s", ssid);
      } else if (it.type == Seen && !wifi_->net(it.idx).secure) {
        wifi_->connect(ssid, "", false, true);
        note("Forbinder til %.28s", ssid);
      } else {
        snprintf(in_ssid_, sizeof(in_ssid_), "%s", ssid);
        input_ = 2;
        input_hidden_ = false;
        in_buf_[0] = 0;
      }
      return;
    }
  }
}

void WifiUi::click() {
  if (!wifi_) return;
  if (input_) {
    key('\r');
    return;
  }
  static Item items[kMaxItems];
  const int n = collect(items);
  if (sel_ < n) activate(items[sel_]);
}

void WifiUi::key(char c) {
  if (!wifi_) return;
  const bool enter = c == '\r' || c == '\n';
  const bool bksp = c == 0x08 || c == 0x7F;
  if (input_) {
    const size_t len = std::strlen(in_buf_);
    if (enter) {
      if (input_ == 1) {
        if (!len) return;
        snprintf(in_ssid_, sizeof(in_ssid_), "%s", in_buf_);
        in_buf_[0] = 0;
        input_ = 2;  // åbent skjult netværk: tom kode + Enter
      } else {
        wifi_->connect(in_ssid_, in_buf_, input_hidden_, true);
        note("Forbinder til %.28s", in_ssid_);
        input_ = 0;
        in_buf_[0] = 0;
      }
    } else if (bksp) {
      if (len) {
        size_t l = len;
        do {
          --l;
        } while (l && (uint8_t(in_buf_[l]) & 0xC0) == 0x80);
        in_buf_[l] = 0;
      } else {
        input_ = 0;  // tom Backspace fortryder
      }
    } else if (uint8_t(c) >= 0x20 && len + 1 < sizeof(in_buf_)) {
      in_buf_[len] = c;
      in_buf_[len + 1] = 0;
    }
    return;
  }
  if (enter) {
    click();
    return;
  }
  if (bksp) {
    static Item items[kMaxItems];
    const int n = collect(items);
    if (sel_ < n && (items[sel_].type == Seen || items[sel_].type == KnownOnly)) {
      const char* ssid = itemSsid(items[sel_]);
      if (wifi_->findKnown(ssid) >= 0) {
        if (armed_ == sel_ && armed_kind_ == 2 && millis() - armed_ms_ < 4000) {
          armed_ = -1;
          wifi_->forget(ssid);
          note("Glemt: %.32s", ssid);
        } else {
          armed_ = sel_;
          armed_kind_ = 2;
          armed_ms_ = millis();
        }
        return;
      }
    }
    requestClose();
  }
}

}  // namespace dm
