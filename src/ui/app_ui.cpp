// SPDX-License-Identifier: MIT
#include "ui/app_ui.h"

#include "storage/sd_log.h"
#include "geo_text.h"
#include "net/channel_scan.h"
#include "ui/contact_colors.h"
#include "version.h"
#include "ui/lv/lv_port.h"
#include "lvgl.h"
#include "util/wall_clock.h"

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esp_heap_caps.h"

namespace dm {

namespace {
constexpr int kOvMaxCells = KeyMap::kMaxVariants;  // varianter i specialtegn-bjælken

}  // namespace

// Fælles med den nye brugerflade (app_ui_lv.cpp).
const char* contactTypeName(uint8_t t) {
  switch (t) {
    case ADV_TYPE_CHAT: return "chat";
    case ADV_TYPE_REPEATER: return "repeat";
    case ADV_TYPE_ROOM: return "rum";
    case ADV_TYPE_SENSOR: return "sensor";
    default: return "?";
  }
}

void contactAge(char* out, size_t n, uint32_t secs) {
  if (secs < 90) snprintf(out, n, "%lus", (unsigned long)secs);
  else if (secs < 5400) snprintf(out, n, "%lum", (unsigned long)(secs / 60));
  else if (secs < 172800) snprintf(out, n, "%lut", (unsigned long)(secs / 3600));
  else snprintf(out, n, "%lud", (unsigned long)(secs / 86400));
}

void AppUi::begin(StatusDisplay* d, McNode* node, McRadio* radio, MsgStore* store, Keyboard* kb,
                  Trackball* tb, StatusViewFn status_view, const Settings& s, KeyMap* km,
                  Touch* touch) {
  km_ = km;
  touch_ = touch;
  d_ = d;
  node_ = node;
  radio_ = radio;
  store_ = store;
  kb_ = kb;
  tb_ = tb;
  status_panel_.vfn = status_view;
  applySettings(s);
  last_wake_ms_ = millis();
  contacts_ = static_cast<ContactInfo*>(
      heap_caps_calloc(kMaxContacts, sizeof(ContactInfo), MALLOC_CAP_SPIRAM));
  dirty_ = true;
}

void AppUi::applySettings(const Settings& s) {
  set_ = s;
  if (set_.sensitivity < 1) set_.sensitivity = 1;
  if (set_.sensitivity > 10) set_.sensitivity = 10;
  if (d_) d_->setBrightness(set_.screen_brightness);
  if (kb_lit_ && kb_) kb_->setBacklight(set_.kb_backlight);
}

void AppUi::screenWake(const char* why) {
  last_wake_ms_ = millis();
  if (dimmed_) {
    dimmed_ = false;
    d_->setDimmed(false);
  }
  if (!screen_off_) return;
  screen_off_ = false;
  if (power_fn_) power_fn_(true);  // fuld fart til tegningen (strømspar)
  d_->setPower(true);
  draw(true);  // tegningerne blev sprunget over, mens skærmen var slukket
  DLOG.printf("[UI] skaerm taendt (%s)\n", why);
}

void AppUi::screenSleep() {
  if (screen_off_) return;
  screen_off_ = true;
  dimmed_ = false;
  d_->setPower(false);
  DLOG.printf("[UI] skaerm slukket efter %u s uden input\n", unsigned(set_.screen_timeout_s));
  if (power_fn_) power_fn_(false);
}

void AppUi::activity() {
  last_activity_ms_ = millis();
  last_wake_ms_ = last_activity_ms_;
  if (!kb_lit_ && kb_ && set_.kb_backlight) {
    kb_->setBacklight(set_.kb_backlight);
    kb_lit_ = true;
  }
}

// Pulser pr. skridt: følsomhed 10 = 1 puls, 5 = 3, 1 = 6.
static int pulsesPerStep(uint8_t sens) { return 1 + (10 - sens) * 5 / 9; }

void AppUi::tick() {
  if (!d_) return;
  const uint32_t now = millis();
  // Er skærmen slukket, vækker input den kun (bruges ikke), så man ikke kommer
  // til at vælge eller skrive noget i blinde.
  const bool dark = screen_off_;
  bool input = false;
  if (kb_ && now - last_kb_ms_ >= 10) {
    last_kb_ms_ = now;
    for (int i = 0; i < 8; ++i) {  // tøm en hurtig tastefølge
      const char c = kb_->poll();
      if (!c) break;
      input = true;
      if (!dark) handleKey(c);
    }
    if (kb_->takeActivity()) {  // alle taster, også Shift/Alt/Sym/Mic
      activity();
      input = true;
    }
  }
  if (tb_) {
    const TrackballEvent e = tb_->read(set_.click_delay_ms);
    if (e.dx || e.dy || e.click) {
      activity();
      input = true;
    }
    if (!dark) handleTrackball(e);
    sosHold(tb_->held());  // virker også med slukket skærm
  }
  if (touch_ && now - last_touch_ms_ >= 20) {
    last_touch_ms_ = now;
    // Tryk udløses ved slip, så et træk (scroll i Chat, brugerens ønske 17. nov 2026) ikke
    // også bliver til et tryk på en besked eller fane. Vækker berøringen skærmen, bruges den
    // kun til det (intet vælges i blinde).
    int x, y;
    const Touch::Phase ph = touch_->poll(x, y);
    if (ph == Touch::Phase::Down) {
      activity();
      input = true;
      td_x_ = x;
      td_y_ = y;
      td_last_y_ = y;
      td_last_x_ = x;
      td_drag_ = false;
      td_acc_ = 0;
      td_wake_ = dark;
    } else if (ph == Touch::Phase::Move && !td_wake_) {
      activity();
      const int dy = y - td_y_;
      if (!td_drag_ && (dy > kDragStartPx || dy < -kDragStartPx || x - td_x_ > kDragStartPx ||
                        td_x_ - x > kDragStartPx))
        td_drag_ = true;
      if (td_drag_ && scr_ == ScrChat) {
        // Fingeren ned = ældre beskeder kommer til syne (som på en telefon).
        td_acc_ += y - td_last_y_;
        while (td_acc_ >= kChatStepPx || td_acc_ <= -kChatStepPx) {
          const int step = td_acc_ > 0 ? 1 : -1;
          td_acc_ -= step * kChatStepPx;
          chat_scroll_ += step;
          if (chat_scroll_ < 0) chat_scroll_ = 0;
          dirty_ = true;
        }
      }
      if (td_drag_ && scr_ == ScrMap && map_ && !menu_open_ && td_y_ >= 22) {  // ikke fra fanelinjen
        // Kortet følger fingeren (som på en telefon).
        const int mx = td_last_x_ - x, my = td_last_y_ - y;
        if (mx || my) {
          map_follow_ = false;
          map_->panPx(mx, my);
          dirty_ = true;
        }
      }
      td_last_y_ = y;
      td_last_x_ = x;
    } else if (ph == Touch::Phase::Up) {
      if (!td_wake_ && !td_drag_ && !dark) handleTap(td_x_, td_y_);
      td_wake_ = false;
    }
  }
  if (input) screenWake("input");  // tænder en slukket og lysner en dæmpet skærm
  // Delt lokation: når samtalen er valgt (Chat åben), står teksten klar i indtastningen.
  if (share_pending_ && scr_ == ScrChat && tgt_ != TgtNone && !menu_open_) {
    share_pending_ = false;
    char t[48];
    const int n = geo::formatLoc(t, sizeof(t), share_lat_, share_lon_);
    if (in_len_ && in_len_ + 1 < sizeof(in_)) in_[in_len_++] = ' ';
    if (n > 0 && in_len_ + size_t(n) < sizeof(in_)) {
      memcpy(in_ + in_len_, t, size_t(n) + 1);
      in_len_ = uint16_t(in_len_ + n);
    }
    typing_ = true;
    snprintf(note_, sizeof(note_), share_save_ ? "Enter sender (prikken gemmes)" : "Enter sender lokationen");
    note_ms_ = millis();
    dirty_ = true;
  } else if (share_pending_ && (scr_ == ScrMap || scr_ == ScrSettings)) {
    share_pending_ = share_save_ = false;  // deling fortrudt
  }
  // Den første samtale: MeshCore #Public (når noden kører).
  if (tgt_ == TgtNone && node_ && node_->channelCount() > 0) openChannel(0, 0, false);
  else if (tgt_ == TgtNone && chan_ && chan_->mtOn() && scan_ && scan_->mtChannelCount() > 0 &&
           (!node_ || !node_->channelCount()))
    openChannel(1, 0, false);  // MeshCore slået fra: Meshtastics primære kanal
  if (store_) {
    // Læst = Chat-fanen vises på en tændt skærm.
    store_->setViewActive(scr_ == ScrChat && !screen_off_);
    // Ny besked fra en anden node (kanal eller DM, begge net) vækker skærmen.
    const uint32_t u = store_->totalUnread();
    if (u > last_unread_) screenWake("ny besked");
    if (u != last_unread_) dirty_ = true;
    last_unread_ = u;
  }
  // Frisk klokke herfra: tastatur/touch læses over I²C ovenfor, så activity() og
  // send() har stemplet tider EFTER `now`. Med `now` løb differencen rundt (uint32)
  // og slukkede tastaturlyset/fjernede beskeden straks ("blinker kun kort").
  const uint32_t t = millis();
  if (kb_lit_ && t - last_activity_ms_ >= uint32_t(set_.kb_backlight_timeout_s) * 1000u) {
    kb_->setBacklight(0);
    kb_lit_ = false;
  }
  // Skærmslukning: kun på batteri (medmindre screen_timeout_on_usb). Sættes USB
  // i, tændes skærmen igen.
  const bool usb = set_.usb_connected && set_.usb_connected();
  const bool may_sleep = set_.screen_timeout_s > 0 && (!usb || set_.screen_timeout_on_usb);
  // USB taget ud (eller timeout slået til): nedtællingen starter nu, ikke fra sidste
  // input (ellers slukkede skærmen med det samme, fordi tiden var gået på USB).
  if (may_sleep && !may_sleep_) last_wake_ms_ = t;
  may_sleep_ = may_sleep;
  if (!may_sleep) {
    if (screen_off_) screenWake("USB");
  } else if (!screen_off_) {
    const uint32_t idle = t - last_wake_ms_, limit = uint32_t(set_.screen_timeout_s) * 1000u;
    if (idle >= limit) {
      screenSleep();
    } else if (!dimmed_ && limit > 5000 && idle >= limit - 5000) {
      dimmed_ = true;  // dæmp 5 s før skærmen slukker
      d_->setDimmed(true);
    }
  }

  static uint32_t colors_ms = 0;
  if (node_ && (colors_ms == 0 || t - colors_ms > 10000)) {  // navn -> kontaktfarve
    colors_ms = t ? t : 1;
    ccolor::refresh(node_);
  }
  if (mt_ && mt_->version() != seen_mt_version_) {  // ny/ændret Meshtastic-node
    seen_mt_version_ = mt_->version();
    if (scr_ == ScrContacts) dirty_ = true;
  }
  if (store_ && store_->version() != seen_version_) {
    seen_version_ = store_->version();
    if (scr_ == ScrChat || scr_ == ScrContacts) dirty_ = true;
  }
  if (note_[0] && t - note_ms_ > 4000) {
    note_[0] = 0;
    dirty_ = true;
  }
  // Kortet: én flise indlæses ad gangen (SD-bussen deles med radioen), og følg-tilstanden
  // holder egen position i midten.
  if (scr_ == ScrMap && map_ && !screen_off_) {
    if (map_->pump(1)) dirty_ = true;
    if (track_ && track_->version() != track_seen_) {  // nyt punkt i sporet / andet spor vist
      track_seen_ = track_->version();
      dirty_ = true;
    }
    if (caches_ && caches_->version() != caches_seen_) {  // fundet/mål ændret
      caches_seen_ = caches_->version();
      dirty_ = true;
    }
    static uint32_t follow_ms = 0;
    if (map_follow_ && t - follow_ms > 2000) {
      follow_ms = t;
      mapCenterSelf();
    }
  }
  // Status-skærmen opdateres hvert sekund; de andre ved ændringer (og hvert 5. s for alder).
  const uint32_t period =
      (scr_ == ScrStatus || scr_ == ScrSettings || scr_ == ScrChannels) ? 1000 : 5000;
  if (scr_ == ScrChannels && chan_ && chan_->needsRedraw()) dirty_ = true;
  if (panelOn() && panel_->needsRedraw()) dirty_ = true;
  if (sos_confirm_ && t - sos_confirm_ms_ > 15000) {  // ingen svar: fortryd
    sos_confirm_ = false;
    dirty_ = true;
  }
  // Slukket skærm: tegn ikke (sparer SPI-tid for radioen); alt tegnes ved opvågning.
  if (!screen_off_ && (dirty_ || now - last_draw_ms_ >= period)) draw(false);
  if (!screen_off_) {
    const uint32_t t_lv = micros();
    lvport::handle();
    const uint32_t us = micros() - t_lv;
    if (us > max_lv_us_) max_lv_us_ = us;
  }
}

void AppUi::handleKey(char c) {
  dirty_ = true;
  if (sos_confirm_) {  // Enter = bekræft, alt andet fortryder
    sos_confirm_ = false;
    const bool on = sos_active_ && sos_active_();
    if ((c == '\r' || c == '\n') && sos_set_) {
      sos_set_(!on);
      snprintf(note_, sizeof(note_), on ? "SOS stoppet" : "SOS AKTIVERET");
    } else {
      snprintf(note_, sizeof(note_), "SOS fortrudt");
    }
    note_ms_ = millis();
    return;
  }
  if (upd_notice_) {  // popup om ny firmware: Enter vælger, Backspace/Esc = Senere
    if (c == '\r' || c == '\n') updNoticeChoose(upd_sel_);
    else if (c == 0x08 || c == 0x7F || c == 0x1B) updNoticeChoose(1);
    return;
  }
  if (menu_open_) {  // tandhjulsmenu: Enter udfører, Backspace/Esc lukker
    if (c == '\r' || c == '\n') menuRun();
    else if (c == 0x08 || c == 0x7F || c == 0x1B) menuClose();
    return;
  }
  if (c == Keyboard::kBackspaceRepeat) {  // holdt Backspace: sletter kun tekst, aldrig andet
    bool text;
    if (panelOn()) text = false;  // underskærmene: kun enkelte tryk
    else if (scr_ == ScrSettings) text = menu_ && menu_->textLen() > 0;
    else if (scr_ == ScrChannels) text = chan_ && chan_->inputLen() > 0;
    else text = in_len_ > 0;
    if (!text) return;
    c = 0x08;
  }
  if (tab_focus_) {  // fanelinjen: Enter går ind i listen; ellers virker tasten som i listen
    tab_focus_ = false;
    if (c == '\r' || c == '\n' || c == 0x08 || c == 0x7F) return;
  }
  if (panelOn()) {
    panel_->key(c);
    if (panel_->wantsClose()) openPanel(nullptr);
    return;
  }
  if (scr_ == ScrMap) {  // kortet tager alle taster (zoom, centrér, tilbage)
    handleMapKey(c);
    return;
  }
  if (c == kKeyEmoji) {  // Mic: emoji-vælgeren (kun i Chat/Kontakt)
    if (!panel_ && scr_ != ScrSettings && scr_ != ScrChannels) emojiToggle();
    return;
  }
  if (scr_ == ScrSettings || scr_ == ScrChannels) {  // menuen/kanalfanen tager alle taster
    ov_n_ = 0;  // ny tast bekræfter et evt. valgt specialtegn
    if (scr_ == ScrSettings) {
      if (menu_) menu_->key(c);
    } else if (chan_) {
      chan_->key(c);
    }
    // Specialtegn som i Chat, når tasten blev skrevet i et tekstfelt, der tillader dem.
    char* b;
    size_t cap;
    if (uint8_t(c) >= 0x20 && uint8_t(c) < 0x7F && extField(b, cap)) {
      const size_t n = strlen(b);
      if (n && b[n - 1] == c) showVariants(c, true);
    }
    return;
  }
  if (c == '\r' || c == '\n') {
    ov_n_ = 0;
    if (typing_ || in_len_) {
      send();
    } else if (scr_ == ScrContacts && contactRows() > 0) {
      openContact(sel_);
    }
    return;
  }
  if (c == 0x08 || c == 0x7F) {  // backspace
    ov_n_ = 0;
    if (in_len_) {
      do {
        --in_len_;
      } while (in_len_ && (uint8_t(in_[in_len_]) & 0xC0) == 0x80);
      in_[in_len_] = 0;
    } else if (scr_ == ScrChat) {
      typing_ = false;  // Backspace i et tomt felt: tilbage til listen
      goBack();
    } else if (typing_) {
      typing_ = false;  // backspace i tom linje annullerer
    } else if (scr_ == ScrContacts && contactRows() > 0) {
      deleteContact(sel_);  // to gange = slet (brugerens ønske 17. nov 2026)
    }
    return;
  }
  if (uint8_t(c) < 0x20) return;  // andre styretegn ignoreres
  if (!typing_) {
    if (scr_ == ScrContacts && contactRows() > 0) openContact(sel_);  // skriv DM
    else if (scr_ != ScrChat) switchTo(ScrChat);  // skriv til den åbne samtale
    startTyping(false);
  }
  if (in_len_ + 1 < sizeof(in_) && in_len_ < MAX_TEXT_LEN) {
    in_[in_len_++] = c;
    in_[in_len_] = 0;
    showVariants(c);
  }
}

// Kortet: + / o = zoom ind, - / i = zoom ud (tasternes påtryk), c = centrér på egen position (og følg den),
// Enter/m/d = kortets menu (del/gem/fjern), Backspace = fanelinjen.
void AppUi::handleMapKey(char c) {
  if (!map_) return;
  if (c == 0x08 || c == 0x7F || c == 0x1B) {
    tab_focus_ = true;
  } else if (c == '+' || c == 'o' || c == 'O' || c == '=') {  // O-tasten har "+" påtrykt
    map_->setZoom(map_->zoom() + 1);
  } else if (c == '-' || c == 'i' || c == 'I') {  // I-tasten har "-" påtrykt
    map_->setZoom(map_->zoom() - 1);
  } else if (c == 'c' || c == 'C') {
    map_follow_ = true;
    mapCenterSelf();
  } else if (c == '\r' || c == '\n' || c == 'm' || c == 'M' || c == 'd' || c == 'D') {
    openMapMenu();
  } else if ((c == 'g' || c == 'G') && caches_panel_) {  // genvej: Geocaches
    openPanel(caches_panel_);
  } else if ((c == 's' || c == 'S') && tracks_panel_) {  // genvej: Gemte spor
    openPanel(tracks_panel_);
  } else if ((c == 'k' || c == 'K') && maps_panel_) {  // genvej: Kortpakker
    openPanel(maps_panel_);
  }
}

void AppUi::notify(const char* msg) {
  snprintf(note_, sizeof(note_), "%s", msg ? msg : "");
  note_ms_ = millis();
  dirty_ = true;
}

void AppUi::showOnMap(double lat, double lon) {
  if (!map_) return;
  if (panel_) openPanel(nullptr);
  switchTo(ScrMap);  // (første åbning centrerer på GPS; derfor sættes centrum bagefter)
  tab_focus_ = false;
  map_follow_ = false;
  map_->setCenter(lat, lon);
  dirty_ = true;
}

bool AppUi::mapTarget(double& lat, double& lon) {
  if (map_ && map_->centered() && !map_follow_) {
    lat = map_->centerLat();
    lon = map_->centerLon();
    return true;
  }
  MapMarker m[1];
  if (markers_ && markers_(m, 1) > 0 && m[0].kind == 0) {
    lat = m[0].lat;
    lon = m[0].lon;
    return true;
  }
  return false;
}

int AppUi::pointNearCenter() {
  if (!points_ || !map_) return -1;
  int best = -1;
  double bd = 15.0 * 15.0;  // højst 15 px fra sigtekornet
  for (int i = 0; i < points_->count(); ++i) {
    const double dx = map_->worldX(points_->at(i).lon) - map_->cx();
    const double dy = map_->worldY(points_->at(i).lat) - map_->cy();
    if (dx * dx + dy * dy < bd) {
      bd = dx * dx + dy * dy;
      best = i;
    }
  }
  return best;
}

void AppUi::openMapMenu() {
  menu_n_ = menu_sel_ = 0;
  menu_info_[0] = 0;
  double lat, lon;
  if (!mapTarget(lat, lon)) {
    snprintf(note_, sizeof(note_), "Ingen position: flyt kortet eller vent på GPS");
    note_ms_ = millis();
    dirty_ = true;
    return;
  }
  share_lat_ = lat;
  share_lon_ = lon;
  geo::formatLoc(menu_title_, sizeof(menu_title_), lat, lon);
  menuAdd("Del i en samtale", MaLocShare);
  if (mt_ && scan_ && mt_->dataChannelIndex() >= 0 && mt_->dataChannelIndex() < scan_->mtChannelCount()) {
    char t[44];  // waypoint på "Kanal for data" (som position/telemetri)
    snprintf(t, sizeof(t), "Send som waypoint (%.20s)", scan_->mtChannel(mt_->dataChannelIndex()).name);
    menuAdd(t, MaWaypoint);
  }
  if (points_) {
    menuAdd("Del og gem på kortet", MaLocShareSave);
    menuAdd("Gem på kortet", MaLocSave);
    const int p = pointNearCenter();
    if (p >= 0) {
      char t[44];
      snprintf(t, sizeof(t), "Fjern prik (%.24s)", points_->at(p).label);
      menuAdd(t, MaLocRemove);
    }
    if (points_->count() > 0) {
      char t[44];
      snprintf(t, sizeof(t), "Ryd alle gemte prikker (%d)", points_->count());
      menuAdd(t, MaLocClearAll);
    }
  }
  if (track_) {
    if (track_->recording()) menuAdd("Stop og gem spor", MaTrackStop);
    else menuAdd("Start spor", MaTrackStart);
    if (tracks_panel_) menuAdd("Gemte spor", MaTrackList);
    if (caches_panel_) menuAdd("Geocaches", MaCaches);
    if (maps_panel_) menuAdd("Kortpakker", MaMaps);
    if (track_->shownName()[0]) menuAdd("Skjul vist spor", MaTrackHide);
  }
  menu_open_ = true;
  dirty_ = true;
}

void AppUi::openLocMenu(double lat, double lon) {
  menu_n_ = menu_sel_ = 0;
  menu_info_[0] = 0;
  loc_lat_ = lat;
  loc_lon_ = lon;
  geo::formatLoc(menu_title_, sizeof(menu_title_), lat, lon);
  menuAdd("Vis på kortet", MaLocView);
  if (points_) menuAdd("Gem på kortet", MaLocKeep);
  menu_open_ = true;
  dirty_ = true;
}

void AppUi::mapCenterSelf() {
  if (!map_ || !markers_) return;
  MapMarker m[1];
  if (markers_(m, 1) > 0 && m[0].kind == 0) {
    map_->setCenter(m[0].lat, m[0].lon);
  } else if (map_follow_) {
    map_follow_ = false;
    snprintf(note_, sizeof(note_), "Ingen GPS-position endnu");
    note_ms_ = millis();
  }
  dirty_ = true;
}

bool AppUi::extField(char*& buf, size_t& cap) {
  if (scr_ == ScrSettings && !panel_ && menu_) return menu_->textField(buf, cap);
  if (scr_ == ScrChannels && chan_) return chan_->textField(buf, cap);
  return false;
}

bool AppUi::overlayOn() {
  if (ov_n_ <= 0) return false;
  if (!ov_ext_) return typing_;
  char* b;
  size_t cap;
  return extField(b, cap);
}

void AppUi::showVariants(char key, bool ext) {
  ov_ext_ = ext;
  ov_key_ = key;
  ov_sel_ = -1;
  ov_n_ = km_ ? km_->variants(key, ov_vars_, kOvMaxCells) : 0;
}

// Erstat det sidst skrevne tegn med variant idx (eller grundtasten ved -1).
void AppUi::applyVariant(int idx) {
  char* buf = in_;
  size_t cap = sizeof(in_);
  if (ov_ext_ && !extField(buf, cap)) return;
  const size_t len = strlen(buf);
  if (!ov_n_ || len == 0) return;
  int start = int(len) - 1;
  while (start > 0 && (uint8_t(buf[start]) & 0xC0) == 0x80) --start;
  char rep[5] = {ov_key_, 0};
  if (idx >= 0 && idx < ov_n_) snprintf(rep, sizeof(rep), "%s", ov_vars_[idx]);
  const size_t n = strlen(rep);
  if (start + n + 1 > cap) return;  // ingen plads til det længere tegn
  memcpy(buf + start, rep, n);
  buf[start + n] = 0;
  if (!ov_ext_) in_len_ = uint16_t(start + n);
  ov_sel_ = idx;
  dirty_ = true;
}

void AppUi::handleTap(int x, int y) {
  dirty_ = true;
  tab_focus_ = false;  // touch vælger direkte; markøren i fanelinjen er til trackballen
  if (lv_panel_ && y >= lvPanelTop()) {
    const int r = lvPanelRowAt(y);
    if (upd_notice_ && !sos_confirm_) {  // popup: række 0 = Opdatér nu, 1 = Senere
      if (r == 0 || r == 1) updNoticeChoose(r);
      return;
    }
    if (r >= 0 && panelOn() && !sos_confirm_) {  // SOS bekræftes kun med taster
      panel_->tapRow(r);
      if (panel_->wantsClose()) openPanel(nullptr);
    }
    return;
  }
  lvTap(x, y);
}

void AppUi::channelRemoved(int net, int idx) {
  const bool mc = net == 0 && tgt_ == TgtMcChannel, mt = net == 1 && tgt_ == TgtMtChannel;
  if (!mc && !mt) return;
  if (tgt_idx_ == idx) openChannel(net, 0, false);  // den åbne kanal er væk: Public/primær
  else if (tgt_idx_ > idx) --tgt_idx_;              // listen er rykket én op
  dirty_ = true;
}

void AppUi::contactRemoved() {
  if (tgt_ != TgtMcDm && tgt_ != TgtMtDm) return;
  if (node_ && node_->channelCount() > 0) openChannel(0, 0, false);
  else openChannel(1, 0, false);
}

// Backspace på en kontakt: første gang spørges (titellinjen), anden gang inden 4 s slettes.
// MeshCore: kontakten fjernes og contacts.txt gemmes; Meshtastic: noden fjernes fra
// nodedatabasen. Begge kommer igen, hvis de sender en ny advert/NodeInfo. DM-historikken
// bliver liggende på SD.
void AppUi::deleteContact(int i) {
  const uint32_t now = millis();
  if (del_sel_ != i || now - del_ms_ > 4000) {
    del_sel_ = i;
    del_ms_ = now;
    char name[40] = "";
    if (i < n_contacts_) snprintf(name, sizeof(name), "%s", contacts_[i].name);
    else if (mt_ && i - n_contacts_ < mt_->count()) mt_->nameOf(mt_->at(i - n_contacts_).num, name, sizeof(name));
    snprintf(note_, sizeof(note_), "Slet %.24s? Backspace igen = ja", name);
    note_ms_ = now;
    dirty_ = true;
    return;
  }
  del_sel_ = -1;
  bool ok = false;
  char name[40] = "";
  bool was_open = false;
  if (i < n_contacts_ && node_) {
    snprintf(name, sizeof(name), "%s", contacts_[i].name);
    was_open = tgt_ == TgtMcDm && std::memcmp(tgt_pub_, contacts_[i].id.pub_key, PUB_KEY_SIZE) == 0;
    ContactInfo* ct = node_->lookupContactByPubKey(contacts_[i].id.pub_key, PUB_KEY_SIZE);
    ok = ct && node_->removeContact(*ct);
    if (ok) {
      node_->markContactsDirty();
      node_->saveContactsIfDirty(0);
    }
  } else if (mt_ && i - n_contacts_ < mt_->count()) {
    const uint32_t num = mt_->at(i - n_contacts_).num;
    mt_->nameOf(num, name, sizeof(name));
    was_open = tgt_ == TgtMtDm && tgt_mt_ == num;
    ok = mt_->removeNode(num);
  }
  if (ok) DLOG.printf("[UI] kontakt slettet: %s\n", name);
  if (ok && was_open) contactRemoved();
  snprintf(note_, sizeof(note_), ok ? "Slettet: %.30s" : "Kunne ikke slette %.20s", name);
  note_ms_ = now;
  refreshContacts();
  if (sel_ >= contactRows()) sel_ = contactRows() ? contactRows() - 1 : 0;
  dirty_ = true;
}

void AppUi::openChannel(int net, int idx, bool show) {
  if (!store_) return;
  char conv[20] = "", title[40] = "";
  if (net == 0 && node_) {
    ChannelDetails cd;
    if (idx < 0 || idx >= node_->channelCount() || !node_->channelAt(idx, cd)) return;
    McNode::channelConvId(cd.channel, conv, sizeof(conv));
    snprintf(tgt_name_, sizeof(tgt_name_), "%s%s", cd.name[0] == '#' ? "" : "#", cd.name);
    snprintf(title, sizeof(title), "%s (MC)", tgt_name_);
    tgt_ = TgtMcChannel;
  } else if (net == 1 && scan_) {
    if (idx < 0 || idx >= scan_->mtChannelCount()) return;
    const MtChannel& c = scan_->mtChannel(idx);
    snprintf(conv, sizeof(conv), "%s", c.conv);
    snprintf(tgt_name_, sizeof(tgt_name_), "%s", c.name);
    snprintf(title, sizeof(title), "%s (MT)", c.name);
    tgt_ = TgtMtChannel;
  } else {
    return;
  }
  tgt_idx_ = idx;
  if (scr_ == ScrChat) draftSave();  // samtaleskift i chatten: kladden bliver ved den gamle
  store_->open(conv, title);
  if (scr_ == ScrChat) draftLoad();
  chat_scroll_ = 0;
  if (show) switchTo(ScrChat);
  dirty_ = true;
}

void AppUi::openDm(const ContactInfo& c, bool show) {
  if (!store_) return;
  char conv[20], title[40];
  McNode::dmConvId(c.id.pub_key, conv, sizeof(conv));
  std::memcpy(tgt_pub_, c.id.pub_key, PUB_KEY_SIZE);
  snprintf(tgt_name_, sizeof(tgt_name_), "%s", c.name);
  snprintf(title, sizeof(title), "DM %s", c.name);
  tgt_ = TgtMcDm;
  if (scr_ == ScrChat) draftSave();  // samtaleskift i chatten: kladden bliver ved den gamle
  store_->open(conv, title);
  if (scr_ == ScrChat) draftLoad();
  chat_scroll_ = 0;
  if (show) switchTo(ScrChat);
  dirty_ = true;
}

void AppUi::openContact(int i) {
  if (i < 0) return;
  if (i < n_contacts_) openDm(contacts_[i]);
  else if (mt_ && i - n_contacts_ < mt_->count()) openMtDm(mt_->at(i - n_contacts_).num);
}

void AppUi::openMtDm(uint32_t num, bool show) {
  if (!store_ || !mt_) return;
  char conv[20], title[48], name[40];
  MtNode::dmConvId(num, conv, sizeof(conv));
  mt_->nameOf(num, name, sizeof(name));
  snprintf(tgt_name_, sizeof(tgt_name_), "%s", name);
  snprintf(title, sizeof(title), "DM %s (MT)", name);
  tgt_ = TgtMtDm;
  tgt_mt_ = num;
  if (scr_ == ScrChat) draftSave();  // samtaleskift i chatten: kladden bliver ved den gamle
  store_->open(conv, title);
  if (scr_ == ScrChat) draftLoad();
  chat_scroll_ = 0;
  if (show) switchTo(ScrChat);
  dirty_ = true;
}

void AppUi::openPanel(Panel* p) {
  if (panel_) panel_->clearClose();
  panel_ = p;
  if (!p && menu_ && !menu_->changed()) menu_->load();  // panelet kan have ændret indstillinger
  if (p) {
    if (scr_ != ScrMap) scr_ = ScrSettings;  // kortet kan selv åbne et panel (fx Gemte spor)
    p->clearClose();
    p->onOpen();
  }
  dirty_ = true;
}

void AppUi::switchTo(Screen s) {
  const Screen prev = scr_;
  if (s != prev || s != ScrSettings) panel_ = nullptr;  // et panel hører til sin fane (Indst./Kort)
  menu_open_ = false;                      // tandhjulsmenuen hører til den åbne samtale
  if (s == ScrChat && prev != ScrChat) chat_parent_ = prev == ScrContacts ? ScrContacts : ScrChannels;
  if (prev == ScrChat && s != ScrChat) {  // ud af chatten: skrevet tekst bliver en kladde
    draftSave();
    emo_open_ = false;
    ov_n_ = 0;
  }
  scr_ = s;
  if (scr_ == ScrChat) draftLoad();
  if (scr_ == ScrContacts) refreshContacts();
  // Første gang kortet åbnes: start ved egen position, hvis GPS'en kender den (ellers i den
  // første kortpakkes centrum).
  static bool map_opened = false;
  if (scr_ == ScrMap && map_ && !map_opened) {
    map_opened = true;
    MapMarker m[1];
    if (markers_ && markers_(m, 1) > 0 && m[0].kind == 0) {
      map_->setCenter(m[0].lat, m[0].lon);
      map_follow_ = true;
    }
  }
  // Tilbage fra chatten: markøren bliver på kanalen, man kom fra.
  if (scr_ == ScrChannels && chan_ && prev != ScrChat) chan_->onEnter();
  // Menuen viser de aktuelle værdier, medmindre der er ændringer, der ikke er gemt.
  if (scr_ == ScrSettings && menu_ && !menu_->changed()) menu_->load();
  dirty_ = true;
}

void AppUi::goBack() {
  if (scr_ != ScrChat) return;
  tab_focus_ = false;
  switchTo(chat_parent_);
}

void AppUi::draftSave() {
  const char* conv = store_ ? store_->openId() : "";
  if (conv[0]) {
    int slot = -1;
    for (int i = 0; i < kDrafts && slot < 0; ++i)
      if (!std::strcmp(drafts_[i].conv, conv)) slot = i;
    if (in_len_) {
      if (slot < 0)
        for (int i = 0; i < kDrafts && slot < 0; ++i)
          if (!drafts_[i].conv[0]) slot = i;
      if (slot < 0) slot = draft_next_++ % kDrafts;  // fuld: overskriv den ældste
      snprintf(drafts_[slot].conv, sizeof(drafts_[0].conv), "%s", conv);
      snprintf(drafts_[slot].text, sizeof(drafts_[0].text), "%s", in_);
    } else if (slot >= 0) {
      drafts_[slot].conv[0] = 0;  // feltet er tømt: kladden er væk
    }
  }
  in_len_ = 0;
  in_[0] = 0;
  typing_ = false;
}

void AppUi::draftLoad() {
  const char* conv = store_ ? store_->openId() : "";
  if (in_len_ || !conv[0]) return;
  for (int i = 0; i < kDrafts; ++i)
    if (!std::strcmp(drafts_[i].conv, conv)) {
      snprintf(in_, sizeof(in_), "%s", drafts_[i].text);
      in_len_ = uint16_t(std::strlen(in_));
      typing_ = in_len_ > 0;
      drafts_[i].conv[0] = 0;
      dirty_ = true;
      return;
    }
}

void AppUi::startTyping(bool dm) {
  if (!typing_ && !in_len_) in_[0] = 0;  // en hentet kladde bevares
  typing_ = true;
  dm_ = dm;
  if (dm && sel_ < n_contacts_) {
    snprintf(dm_name_, sizeof(dm_name_), "%s", contacts_[sel_].name);
    std::memcpy(dm_pub_, contacts_[sel_].id.pub_key, PUB_KEY_SIZE);
  }
  if (scr_ != ScrChat && !dm) scr_ = ScrChat;
  dirty_ = true;
}

void AppUi::send() {
  if (in_len_ == 0) {  // tom Enter annullerer
    typing_ = false;
    return;
  }
  bool ok = false;
  bool drop = false;  // teksten kan ikke sendes her: ryd den alligevel
  if (tgt_ == TgtMtChannel) {
    ok = mt_ && mt_->sendText(tgt_idx_, in_);
    snprintf(note_, sizeof(note_), ok ? "Sendt til %.30s" : "Ikke sendt (Må sende er fra?)", tgt_name_);
    drop = !ok;
  } else if (tgt_ == TgtMtDm && (!std::strcmp(in_, "/trace") || !std::strcmp(in_, "/traceroute"))) {
    // Kommando i en Meshtastic-DM: traceroute til noden (resultatet kommer i samtalen).
    ok = mt_ && mt_->sendTraceroute(tgt_mt_);
    snprintf(note_, sizeof(note_), ok ? "Traceroute sendt, venter på svar" : "Traceroute ikke sendt");
    drop = true;
  } else if (tgt_ == TgtMtDm) {
    const int r = mt_ ? mt_->sendDm(tgt_mt_, in_) : 0;
    ok = r > 0;
    if (r < 0) snprintf(note_, sizeof(note_), "Nøgle ukendt - spurgt efter NodeInfo");
    else snprintf(note_, sizeof(note_), ok ? "DM sendt til %.26s" : "DM fejlede (%.24s)", tgt_name_);
    drop = r == 0;
  } else if (!radio_ || !radio_->txEnabled()) {
    snprintf(note_, sizeof(note_), "\"Må sende\" er fra (Indst. > MeshCore)");
    drop = true;
  } else if (tgt_ == TgtMcDm) {
    ContactInfo* c = node_->lookupContactByPubKey(tgt_pub_, PUB_KEY_SIZE);
    ok = c && node_->sendDirect(*c, in_);
    snprintf(note_, sizeof(note_), ok ? "DM sendt til %.26s" : "DM fejlede (%.24s)", tgt_name_);
  } else if (tgt_ == TgtMcChannel) {
    ok = node_->sendChannel(tgt_idx_, in_);
    snprintf(note_, sizeof(note_), ok ? "Sendt til %.30s" : "Kunne ikke sende", tgt_name_);
  }
  if (tgt_ == TgtMcDm || tgt_ == TgtMcChannel || tgt_ == TgtMtChannel || tgt_ == TgtMtDm)  // feltloggen
    DLOG.printf("[UI] %s til %s: %s (%s)\n",
                in_[0] == '/' && tgt_ == TgtMtDm ? "kommando" : (tgt_ == TgtMcDm || tgt_ == TgtMtDm) ? "DM" : "kanalbesked",
                tgt_name_, in_,
                ok ? "sat i koe" : "FEJL");
  note_ms_ = millis();
  if (ok && share_save_ && points_) {  // "Del og gem": prikken får samtalens navn
    double la, lo;
    if (geo::parseLoc(in_, la, lo)) {
      points_->add(la, lo, tgt_name_);
      share_save_ = false;
    }
  }
  if (ok || drop) {
    typing_ = false;
    in_len_ = 0;
    in_[0] = 0;
    chat_scroll_ = 0;
  }
  dirty_ = true;
}

// Akselås og dødzone (brugerens ønske 15. nov 2026: scroll op/ned må ikke hoppe til
// siden). Den akse, der sidst gav et skridt, spærrer den anden i kLockMs; spredte
// sidepulser glemmes efter kIdleMs uden sidebevægelse, så de ikke lægges sammen
// under en lang scroll; fanebytte kræver dødzone x pulser-pr.-skridt.
static constexpr uint32_t kLockMs = 500, kIdleMs = 400;

void AppUi::lockAxis(uint8_t axis) {
  lock_axis_ = axis;
  lock_ms_ = millis();
  if (axis == 1) acc_y_ = 0;
  else acc_x_ = 0;
}

void AppUi::handleTrackball(const TrackballEvent& e) {
  const int pps = pulsesPerStep(set_.sensitivity);
  const uint32_t now = millis();
  int dx = set_.invert_x ? -e.dx : e.dx;
  int dy = set_.invert_y ? -e.dy : e.dy;
  const bool locked = lock_axis_ && now - lock_ms_ < kLockMs;
  if (locked && lock_axis_ == 2) dx = 0;  // lodret bevægelse i gang
  if (locked && lock_axis_ == 1) dy = 0;  // vandret bevægelse i gang
  if (dx) {
    if (now - last_x_ms_ > kIdleMs) acc_x_ = 0;
    last_x_ms_ = now;
  }
  acc_x_ += dx;
  acc_y_ += dy;
  if (upd_notice_ && !sos_confirm_) {  // popup om ny firmware: op/ned vælger, klik udfører
    while (acc_y_ >= pps || acc_y_ <= -pps) {
      const int step = acc_y_ > 0 ? 1 : -1;
      acc_y_ -= step * pps;
      upd_sel_ = step > 0 ? 1 : 0;
      lockAxis(2);
    }
    acc_x_ = 0;
    if (e.click) updNoticeChoose(upd_sel_);
    dirty_ = true;
    return;
  }
  if (menu_open_) {  // tandhjulsmenu: op/ned vælger, klik udfører, sidelæns lukker
    while (acc_y_ >= pps || acc_y_ <= -pps) {
      const int step = acc_y_ > 0 ? 1 : -1;
      acc_y_ -= step * pps;
      menu_sel_ = (menu_sel_ + step + menu_n_) % (menu_n_ ? menu_n_ : 1);
      lockAxis(2);
    }
    if (acc_x_ >= pps * set_.deadzone || acc_x_ <= -pps * set_.deadzone) {
      acc_x_ = 0;
      menuClose();
    }
    if (e.click) menuRun();
    dirty_ = true;
    return;
  }
  if (emo_open_ && typing_) {  // emoji-vælgeren: trackball flytter, klik indsætter
    const int n = emojiCount();
    while (acc_x_ >= pps || acc_x_ <= -pps) {
      const int step = acc_x_ > 0 ? 1 : -1;
      acc_x_ -= step * pps;
      emo_sel_ += step;
      lockAxis(1);
    }
    while (acc_y_ >= pps || acc_y_ <= -pps) {
      const int step = acc_y_ > 0 ? 1 : -1;
      acc_y_ -= step * pps;
      emo_sel_ += step * kEmoCols;
      lockAxis(2);
    }
    if (emo_sel_ < 0) emo_sel_ = 0;
    if (emo_sel_ >= n) emo_sel_ = n - 1;
    if (e.click) emojiInsert(emo_sel_);
    dirty_ = true;
    return;
  }
  if (overlayOn()) {
    while (acc_x_ >= pps || acc_x_ <= -pps) {
      const int step = acc_x_ > 0 ? 1 : -1;
      acc_x_ -= step * pps;
      int s = ov_sel_ + step;
      if (s < -1) s = ov_n_ - 1;
      if (s >= ov_n_) s = -1;
      applyVariant(s);
      lockAxis(1);
    }
  }
  if (panelOn()) {  // underskærm: venstre-ryk lukker
    if (panel_->wantsX()) {
      while (acc_x_ >= pps || acc_x_ <= -pps) {
        const int step = acc_x_ > 0 ? 1 : -1;
        acc_x_ -= step * pps;
        panel_->stepX(step);
        dirty_ = true;
        lockAxis(1);
      }
    }
    const int dzp = set_.deadzone < 1 ? 1 : set_.deadzone;
    if (acc_x_ <= -dzp * pps) {
      acc_x_ = 0;
      lockAxis(1);
      openPanel(nullptr);
      return;
    }
    if (acc_x_ >= dzp * pps) acc_x_ = 0;  // højre gør intet i et panel
    while (acc_y_ >= pps || acc_y_ <= -pps) {
      const int step = acc_y_ > 0 ? 1 : -1;
      acc_y_ -= step * pps;
      lockAxis(2);
      panel_->stepY(step);
      dirty_ = true;
    }
    if (e.click) {
      panel_->click();
      if (panel_ && panel_->wantsClose()) openPanel(nullptr);
      dirty_ = true;
    }
    return;
  }
  // I menuen bruges venstre/højre til at ændre en værdi eller vælge Gem/Annuller.
  if (scr_ == ScrSettings && menu_ && menu_->wantsX()) {
    while (acc_x_ >= pps || acc_x_ <= -pps) {
      const int step = acc_x_ > 0 ? 1 : -1;
      acc_x_ -= step * pps;
      menu_->stepX(step);
      dirty_ = true;
      lockAxis(1);
    }
  }
  if (scr_ == ScrChannels && chan_ && chan_->wantsX()) {  // filteret Alle/MC/MT
    while (acc_x_ >= pps || acc_x_ <= -pps) {
      const int step = acc_x_ > 0 ? 1 : -1;
      acc_x_ -= step * pps;
      chan_->stepX(step);
      dirty_ = true;
      lockAxis(1);
    }
  }
  // Kortet: trackballen panorerer i begge akser (ingen dødzone/akselås), klik centrerer på egen
  // position. Fra fanelinjen går ned/klik ind i kortet som i de andre faner.
  if (scr_ == ScrMap && map_ && !tab_focus_) {
    const int ms = set_.map_sensitivity < 1 ? 1 : (set_.map_sensitivity > 10 ? 10 : set_.map_sensitivity);
    const int kPxPerStep = 4 * ms + 4;  // standard 5 = 24 px
    const int sx = acc_x_ / pps, sy = acc_y_ / pps;
    if (sx || sy) {
      acc_x_ -= sx * pps;
      acc_y_ -= sy * pps;
      map_follow_ = false;  // man har selv flyttet kortet
      map_->panPx(sx * kPxPerStep, sy * kPxPerStep);
      dirty_ = true;
    }
    if (e.click) {
      map_follow_ = true;
      mapCenterSelf();
    }
    return;
  }
  // Sidelæns kræver et tydeligt ryk (dødzone x pulser pr. skridt). Tre lag (22. nov 2026):
  // fanelinjen (venstre/højre = fane), listen (venstre = fanelinjen, højre = ind) og chatten
  // (venstre = tilbage til listen; skrevet tekst gemmes som kladde).
  const int dz = set_.deadzone < 1 ? 1 : set_.deadzone;
  if (acc_x_ >= dz * pps || acc_x_ <= -dz * pps) {
    const int dir = acc_x_ > 0 ? 1 : -1;
    lockAxis(1);
    acc_x_ = 0;
    if (tab_focus_) {
      switchTo(Screen(kFirstTab + (scr_ - kFirstTab + kTabs + dir) % kTabs));
      tab_focus_ = true;
    } else if (scr_ == ScrChat) {
      if (dir < 0) goBack();
    } else if (scr_ == ScrChannels && chan_ && chan_->inputActive()) {
      // der skrives navn/nøgle til en ny kanal: sidelæns gør intet
    } else if (dir < 0) {
      // Venstre i en liste hopper ikke selv op i fanelinjen (ønske 22. nov 2026: man ruller
      // selv op dertil). I Indst. folder venstre den åbne gruppe sammen.
      if (scr_ == ScrSettings && menu_) menu_->collapse();
    } else if (scr_ == ScrContacts) {
      if (contactRows() > 0) openContact(sel_);
    } else if (scr_ == ScrChannels && chan_) {
      if (!chan_->atTop()) chan_->click();  // på filteret: kun klik/Enter
    } else if (scr_ == ScrSettings && menu_) {
      menu_->click();
    }
    dirty_ = true;
  }
  while (acc_y_ >= pps || acc_y_ <= -pps) {
    const int step = acc_y_ > 0 ? 1 : -1;
    acc_y_ -= step * pps;
    lockAxis(2);  // lodret skridt: sidebevægelse spærres et øjeblik
    if (tab_focus_) {
      if (step > 0) tab_focus_ = false;  // ned: ind i listen
    } else if (scr_ == ScrSettings && menu_) {
      if (step < 0 && menu_->atTop()) tab_focus_ = true;  // op over øverste række
      else menu_->stepY(step);
    } else if (scr_ == ScrChannels && chan_) {
      if (step < 0 && chan_->atTop()) tab_focus_ = true;
      else chan_->stepY(step);
    } else if (scr_ == ScrChat) {
      chat_scroll_ -= step;  // ned = nyere
      if (chat_scroll_ < 0) chat_scroll_ = 0;
    } else if (scr_ == ScrContacts && !typing_) {
      if (step < 0 && sel_ <= 0) tab_focus_ = true;
      sel_ += step;
      if (sel_ < 0) sel_ = 0;
      if (sel_ >= contactRows()) sel_ = contactRows() ? contactRows() - 1 : 0;
    }
    dirty_ = true;
  }
  if (e.click && tab_focus_) {  // klik i fanelinjen: ind i listen
    tab_focus_ = false;
    dirty_ = true;
    return;
  }
  if (e.click) {
    if (overlayOn() && ov_sel_ >= 0) ov_n_ = 0;  // bekræft det valgte tegn
    else if (scr_ == ScrSettings && menu_) ov_n_ = 0, menu_->click();
    else if (scr_ == ScrChannels && chan_) ov_n_ = 0, chan_->click();
    else if (typing_ && in_len_) send();
    else if (scr_ == ScrContacts && contactRows() > 0) openContact(sel_);
    else if (scr_ == ScrChat) {
      // Første klik hopper til de nyeste beskeder; står man der, åbner klik tandhjulsmenuen
      // (kanalens eller DM-partnerens), så den også kan bruges uden touch.
      if (chat_scroll_ == 0 && gearAllowed()) openTitleMenu();
      chat_scroll_ = 0;
    }
    dirty_ = true;
  }
}

void AppUi::refreshContacts() {
  if (!contacts_ || !node_) return;
  n_contacts_ = node_->contactsSnapshot(contacts_, kMaxContacts);
  if (sel_ >= contactRows()) sel_ = contactRows() ? contactRows() - 1 : 0;
  last_contacts_ms_ = millis();
}

// Alt tegnes med LVGL (fase 4). Fanerne har eget design; underskærme, Status og
// SOS-bekræftelsen beskriver sig som en PanelView, som lvDrawPanel tegner.
void AppUi::draw(bool force) {
  if (splash_until_) {  // opstartslogoet skal stå i mindst 5 s (brugerens ønske)
    if (int32_t(millis() - splash_until_) < 0) return;
    DLOG.printf("[UI] opstartslogo fjernes efter %lu ms oppetid\n", (unsigned long)millis());
    splash_until_ = 0;
    force = true;
  }
  if (!lvport::begin(d_)) return;
  const uint32_t t_draw = micros();
  last_draw_ms_ = millis();
  dirty_ = false;
  // Underskærm, Status eller SOS-bekræftelse: datamodellen tegnes af lvDrawPanel.
  lv_panel_ = sos_confirm_ || upd_notice_ || scr_ == ScrStatus || panelOn();
  if (sos_confirm_) sosConfirmView(pview_);
  else if (upd_notice_) updNoticeView(pview_);
  else if (panelOn()) panel_->view(pview_, kPanelRows);  // også et panel over kortet
  else if (scr_ == ScrStatus) status_panel_.view(pview_, kPanelRows);
  lvDrawChat(force);
  lvport::handle();
  const uint32_t us = micros() - t_draw;
  if (us > max_draw_us_) max_draw_us_ = us;
}

void AppUi::takeUiStats(uint32_t& draw_us, uint32_t& lv_us, const char*& screen) {
  draw_us = max_draw_us_;
  lv_us = max_lv_us_;
  max_draw_us_ = max_lv_us_ = 0;
  static const char* names[ScrCount] = {"Status", "Chat", "Kanal", "Kontakt", "Kort", "Indst."};  // som Screen
  screen = sos_confirm_ ? "SOS-bekræftelse" : (panel_ ? panel_->title() : names[scr_]);
}

// SOS: klikket holdt i 5 s (også med slukket skærm) -> bekræftelsesskærm.
static constexpr uint32_t kSosHoldMs = 5000;

void AppUi::sosHold(bool down) {
  if (!sos_set_) return;
  const uint32_t now = millis();
  if (!down) {
    hold_ms_ = 0;
    hold_fired_ = false;
    return;
  }
  if (!hold_ms_) hold_ms_ = now ? now : 1;
  if (!hold_fired_ && now - hold_ms_ >= kSosHoldMs) {
    hold_fired_ = true;
    typing_ = false;
    ov_n_ = 0;
    sos_confirm_ = true;
    sos_confirm_ms_ = now;
    screenWake("SOS");
    dirty_ = true;
    DLOG.println("[SOS] trackball holdt i 5 s: venter paa bekraeftelse (Enter)");
  }
}

// SOS-bekræftelsen i samme model som underskærmene (rød bjælke, forklaring, 112-advarsel).
void AppUi::showUpdateNotice(const char* ver, UpdateOpenFn open) {
  if (!ver || !ver[0] || !std::strcmp(ver, upd_ver_)) return;  // samme version: kun én gang
  snprintf(upd_ver_, sizeof(upd_ver_), "%s", ver);
  upd_open_ = open;
  upd_sel_ = 0;
  upd_notice_ = true;
  dirty_ = true;
}

void AppUi::updNoticeChoose(int sel) {
  upd_notice_ = false;
  dirty_ = true;
  if (sel == 0 && upd_open_) upd_open_();
}

void AppUi::updNoticeView(PanelView& v) {
  v.clear();
  snprintf(v.title, sizeof(v.title), "Ny firmware");
  snprintf(v.status, sizeof(v.status), "DualMesh %s er klar på GitHub", upd_ver_);
  v.status_fg = color::Green;
  snprintf(v.status2, sizeof(v.status2), "Du har %s", DM_FW_VERSION);
  PanelRow* a = v.add(PanelRow::Item, color::Green, "Opdatér nu");
  if (a) a->sel = upd_sel_ == 0;
  PanelRow* b = v.add(PanelRow::Item, color::White, "Senere");
  if (b) b->sel = upd_sel_ == 1;
  snprintf(v.hint, sizeof(v.hint), "Klik: vælg   Backspace: senere");
}

void AppUi::sosConfirmView(PanelView& v) {
  const bool on = sos_active_ && sos_active_();
  v.clear();
  snprintf(v.title, sizeof(v.title), "SOS");
  if (on) {
    v.add(PanelRow::Header, color::Red, "STOP SOS?");
    v.add(PanelRow::Info, color::White, "SOS sendes nu hvert 5. minut.");
    v.add(PanelRow::Blank, color::Gray, "");
    v.add(PanelRow::Info, color::Yellow, "Enter = stop SOS");
    v.add(PanelRow::Info, color::Gray, "Anden tast = fortsæt SOS");
  } else {
    v.add(PanelRow::Header, color::Red, "SEND SOS?");
    v.add(PanelRow::Info, color::White, "Sender SOS med din position på MeshCore");
    v.add(PanelRow::Info, color::White, "Public og Meshtastics primære kanal");
    v.add(PanelRow::Info, color::White, "hvert 5. min, til du stopper den (hold");
    v.add(PanelRow::Info, color::White, "trackball 5 s igen). Position sendes,");
    v.add(PanelRow::Info, color::White, "også hvis deling er slået fra.");
    snprintf(v.hint, sizeof(v.hint), "Enter = SEND SOS     Anden tast = fortryd");
  }
  snprintf(v.note, sizeof(v.note), "Ikke en erstatning for 112: ring 112!");
  v.note_fg = color::Salmon;
}

// Beskedens tidspunkt (unix, 0 = ukendt). Beskeder modtaget, før uret blev sat
// (ts = 0), regnes tilbage fra oppetiden, hvis de er fra denne opstart.
uint32_t chatMsgTs(const Msg& m, uint32_t now_ts) {
  if (m.ts) return m.ts;
  if (now_ts && m.rx_ms) return now_ts - (millis() - m.rx_ms) / 1000;
  return 0;
}

// Dag som tal (år * 1000 + dag i året, lokal tid); 0 = ukendt.
int chatDayKey(uint32_t ts) {
  if (!ts) return 0;
  const time_t t = ts;
  struct tm tm;
  localtime_r(&t, &tm);
  return (tm.tm_year + 1900) * 1000 + tm.tm_yday;
}

// Datolinje: "I dag, 15. november", "I går, 14. november", "lørdag 8. november"
// (årstal med, hvis det ikke er i år), eller "Ukendt dato".
void chatDayText(uint32_t ts, uint32_t now_ts, char* out, size_t n) {
  static const char* kDays[] = {"søndag", "mandag", "tirsdag", "onsdag", "torsdag", "fredag", "lørdag"};
  static const char* kMonths[] = {"januar", "februar", "marts", "april", "maj", "juni", "juli",
                                  "august", "september", "oktober", "november", "december"};
  if (!ts) {
    snprintf(out, n, "Ukendt dato");
    return;
  }
  const time_t t = ts;
  struct tm tm;
  localtime_r(&t, &tm);
  const int d = chatDayKey(ts), today = chatDayKey(now_ts);
  const int yesterday = now_ts ? chatDayKey(now_ts - 86400) : -1;
  if (now_ts && d == today)
    snprintf(out, n, "I dag, %d. %s", tm.tm_mday, kMonths[tm.tm_mon]);
  else if (now_ts && d == yesterday)
    snprintf(out, n, "I går, %d. %s", tm.tm_mday, kMonths[tm.tm_mon]);
  else if (now_ts && d / 1000 == today / 1000)
    snprintf(out, n, "%s %d. %s", kDays[tm.tm_wday], tm.tm_mday, kMonths[tm.tm_mon]);
  else
    snprintf(out, n, "%s %d. %s %d", kDays[tm.tm_wday], tm.tm_mday, kMonths[tm.tm_mon], tm.tm_year + 1900);
}

// ---- Tandhjulsmenuer ------------------------------------------------------------------

bool AppUi::gearAllowed() const {
  return scr_ == ScrChat && !note_[0] && tgt_ != TgtNone && store_ && store_->openId()[0];
}

void AppUi::menuAdd(const char* label, MenuAct a) {
  if (menu_n_ >= kMenuMax) return;
  snprintf(menu_items_[menu_n_].label, sizeof(menu_items_[0].label), "%s", label);
  menu_items_[menu_n_].act = a;
  ++menu_n_;
}

// Afsenderens menu. in_channel: navnet kommer fra en boble i en kanal; ellers er det DM'ens modpart.
void AppUi::openUserMenu(const char* name, bool in_channel) {
  menu_n_ = menu_sel_ = 0;
  menu_info_[0] = 0;
  menu_mt_ = 0;
  menu_mc_ok_ = false;
  menu_in_dm_ = !in_channel;
  snprintf(menu_name_, sizeof(menu_name_), "%s", name ? name : "");
  const char* conv = store_ ? store_->openId() : "";
  if (in_channel) {
    menu_net_ = std::strncmp(conv, "mtc_", 4) == 0 ? 2 : 1;
    if (menu_net_ == 2 && mt_) {  // Meshtastic: noden ud fra navnet (eller "!xxxxxxxx")
      if (menu_name_[0] == '!' && std::strlen(menu_name_) == 9) menu_mt_ = uint32_t(std::strtoul(menu_name_ + 1, nullptr, 16));
      for (int i = 0; !menu_mt_ && i < mt_->count(); ++i) {
        const MtNodeEntry& e = mt_->at(i);
        if (!std::strcmp(e.long_name, menu_name_) || !std::strcmp(e.short_name, menu_name_)) menu_mt_ = e.num;
      }
    } else if (menu_net_ == 1) {  // MeshCore: kontakten ud fra navnet
      refreshContacts();
      for (int i = 0; i < n_contacts_; ++i)
        if (!std::strcmp(contacts_[i].name, menu_name_)) {
          std::memcpy(menu_mc_pub_, contacts_[i].id.pub_key, PUB_KEY_SIZE);
          menu_mc_ok_ = true;
          break;
        }
    }
  } else if (tgt_ == TgtMtDm) {
    menu_net_ = 2;
    menu_mt_ = tgt_mt_;
  } else if (tgt_ == TgtMcDm) {
    menu_net_ = 1;
    std::memcpy(menu_mc_pub_, tgt_pub_, PUB_KEY_SIZE);
    menu_mc_ok_ = true;
  } else {
    return;
  }
  snprintf(menu_title_, sizeof(menu_title_), LV_SYMBOL_SETTINGS " %.30s", menu_name_);
  if (in_channel) {
    char t[44];
    snprintf(t, sizeof(t), "Nævn @%.30s", menu_name_);
    menuAdd(t, MaMention);
    if (menu_mt_ || menu_mc_ok_) menuAdd("Skriv DM", MaDm);
  }
  if (menu_net_ == 2 && menu_mt_ && mt_) {
    menuAdd("Traceroute", MaTrace);
    menuAdd("Bed om position", MaReqPos);
    menuAdd("Bed om telemetri", MaReqTelem);
    menuAdd("Bed om NodeInfo (navn, nøgle)", MaReqNode);
  }
  if (!in_channel) {
    menuAdd("Del min position", MaLocMyPos);
    if (test_.toggle && test_.priv && test_.priv(conv))
      menuAdd(test_.on && test_.on(conv) ? "Testbesked: slå fra" : "Testbesked: slå til", MaTest);
    menuAdd("Ryd samtalen", MaClear);
  }
  menu_open_ = menu_n_ > 0;
  dirty_ = true;
}

// Kanalens menu (tandhjulet foran kanalnavnet).
void AppUi::openChanMenu() {
  if (tgt_ != TgtMcChannel && tgt_ != TgtMtChannel) return;
  menu_n_ = menu_sel_ = 0;
  menu_info_[0] = 0;
  menu_in_dm_ = false;
  menu_net_ = tgt_ == TgtMtChannel ? 2 : 1;
  snprintf(menu_title_, sizeof(menu_title_), LV_SYMBOL_SETTINGS " %.30s", tgt_name_);
  const char* conv = store_ ? store_->openId() : "";
  menuAdd("Vis nøgle (til at dele kanalen)", MaKey);
  menuAdd("Del min position", MaLocMyPos);
  if (test_.toggle && test_.priv && test_.priv(conv))
    menuAdd(test_.on && test_.on(conv) ? "Testbesked: slå fra" : "Testbesked: slå til", MaTest);
  menuAdd("Ryd chatten", MaClear);
  menu_open_ = true;
  dirty_ = true;
}

void AppUi::openTitleMenu() {
  if (tgt_ == TgtMcChannel || tgt_ == TgtMtChannel) openChanMenu();
  else openUserMenu(tgt_name_, false);
}

void AppUi::menuRun() {
  if (!menu_open_ || menu_sel_ < 0 || menu_sel_ >= menu_n_) return;
  const MenuAct a = menu_items_[menu_sel_].act;
  const char* conv = store_ ? store_->openId() : "";
  note_ms_ = millis();
  switch (a) {
    case MaMention:
      menuClose();
      mentionInsert(menu_name_);
      return;
    case MaDm:
      menuClose();
      if (menu_net_ == 2 && menu_mt_) {
        openMtDm(menu_mt_);
      } else if (menu_mc_ok_ && node_) {
        ContactInfo* c = node_->lookupContactByPubKey(menu_mc_pub_, PUB_KEY_SIZE);
        if (c) openDm(*c);
      }
      return;
    case MaTrace:
    case MaReqPos:
    case MaReqTelem:
    case MaReqNode: {
      menuClose();
      if (!mt_ || !menu_mt_) return;
      bool ok;
      const char* what;
      if (a == MaTrace) {
        ok = mt_->sendTraceroute(menu_mt_);
        what = "Traceroute";
      } else {
        const uint32_t port = a == MaReqPos ? 3 : a == MaReqTelem ? 67 : 4;
        ok = mt_->requestInfo(menu_mt_, port);
        what = a == MaReqPos ? "Position" : a == MaReqTelem ? "Telemetri" : "NodeInfo";
      }
      if (!menu_in_dm_) openMtDm(menu_mt_);  // svaret står i DM'en
      snprintf(note_, sizeof(note_), ok ? "%s: spurgt, svaret kommer her" : "%s: ikke sendt", what);
      note_ms_ = millis();
      return;
    }
    case MaTest: {
      menuClose();
      const int r = test_.toggle ? test_.toggle(conv) : -1;
      snprintf(note_, sizeof(note_), r > 0 ? "Testbesked slået til her" : r == 0 ? "Testbesked slået fra"
                                                                               : "Testbesked: kun DM/privat kanal");
      return;
    }
    case MaKey: {
      if (tgt_ == TgtMcChannel && node_) {
        ChannelDetails d;
        if (node_->channelAt(tgt_idx_, d)) {
          bool long_key = false;
          for (int i = 16; i < 32; ++i) long_key |= d.channel.secret[i] != 0;
          char hex[65];
          const int nb = long_key ? 32 : 16;
          for (int i = 0; i < nb; ++i) snprintf(hex + 2 * i, 3, "%02x", d.channel.secret[i]);
          snprintf(menu_info_, sizeof(menu_info_), "Nøgle (hex): %s", hex);
        }
      } else if (tgt_ == TgtMtChannel && scan_ && tgt_idx_ < scan_->mtChannelCount()) {
        snprintf(menu_info_, sizeof(menu_info_), "PSK (base64): %s", scan_->mtChannel(tgt_idx_).psk_b64);
      }
      dirty_ = true;
      return;  // menuen bliver åben, så nøglen kan læses
    }
    case MaClear:
      snprintf(menu_items_[menu_sel_].label, sizeof(menu_items_[0].label), "Bekræft: ryd (kan ikke fortrydes)");
      menu_items_[menu_sel_].act = MaClearYes;
      dirty_ = true;
      return;
    case MaClearYes:
      menuClose();
      if (store_) store_->clear(conv);
      chat_scroll_ = 0;
      snprintf(note_, sizeof(note_), "Samtalen er ryddet");
      return;
    case MaLocShare:
    case MaLocShareSave:
      // Vælg samtalen i Kanal/Kontakt; teksten lægges i indtastningen, når Chat åbnes.
      menuClose();
      share_pending_ = true;
      share_save_ = a == MaLocShareSave;
      switchTo(ScrChannels);
      snprintf(note_, sizeof(note_), "Vælg kanal eller kontakt at dele i");
      note_ms_ = millis();
      return;
    case MaLocSave:
      menuClose();
      if (points_) points_->add(share_lat_, share_lon_, "Gemt");
      snprintf(note_, sizeof(note_), "Gemt på kortet");
      return;
    case MaLocRemove: {
      menuClose();
      const int p = pointNearCenter();
      if (p >= 0 && points_) points_->remove(p);
      snprintf(note_, sizeof(note_), "Prikken er fjernet");
      return;
    }
    case MaLocClearAll:  // to klik, som "Ryd samtalen"
      snprintf(menu_items_[menu_sel_].label, sizeof(menu_items_[0].label), "Bekræft: ryd alle prikker");
      menu_items_[menu_sel_].act = MaLocClearAllYes;
      dirty_ = true;
      return;
    case MaLocClearAllYes:
      menuClose();
      if (points_) points_->clear();
      snprintf(note_, sizeof(note_), "Alle gemte prikker er ryddet");
      return;
    case MaTrackStart:
      menuClose();
      if (track_ && track_->start()) snprintf(note_, sizeof(note_), "Spor optages");
      else snprintf(note_, sizeof(note_), "Spor kræver SD-kort");
      return;
    case MaTrackStop:
      menuClose();
      if (track_) track_->stop();
      snprintf(note_, sizeof(note_), "Spor gemt (se Gemte spor)");
      return;
    case MaTrackHide:
      menuClose();
      if (track_) track_->hide();
      return;
    case MaTrackList:  // listen åbnes over kortet; lukkes den, er man tilbage på kortet
      menuClose();
      if (tracks_panel_) openPanel(tracks_panel_);
      return;
    case MaCaches:
      menuClose();
      if (caches_panel_) openPanel(caches_panel_);
      return;
    case MaMaps:
      menuClose();
      if (maps_panel_) openPanel(maps_panel_);
      return;
    case MaWaypoint: {  // sigtekornets punkt som Meshtastic-waypoint (navn: en gemt prik her, ellers tid)
      menuClose();
      if (!mt_) return;
      char name[32];
      const int p = pointNearCenter();
      if (p >= 0 && points_) {
        snprintf(name, sizeof(name), "%.30s", points_->at(p).label);
      } else {
        char hm[8];
        wallclock::formatHm(wallclock::now(), hm, sizeof(hm));
        snprintf(name, sizeof(name), "DualMesh %s", hm);
      }
      uint32_t id = 0;
      const bool ok = mt_->sendWaypoint(mt_->dataChannelIndex(), share_lat_, share_lon_, name, "", id);
      if (ok && points_) points_->upsertWaypoint(id, share_lat_, share_lon_, name);
      snprintf(note_, sizeof(note_), ok ? "Waypoint sendt: %.30s" : "Waypoint ikke sendt", name);
      return;
    }
    case MaLocMyPos: {  // chattens menu: egen GPS-position i indtastningen
      menuClose();
      MapMarker m[1];
      if (!markers_ || markers_(m, 1) < 1 || m[0].kind != 0) {
        snprintf(note_, sizeof(note_), "Ingen GPS-position endnu");
        return;
      }
      share_lat_ = m[0].lat;
      share_lon_ = m[0].lon;
      share_pending_ = true;
      share_save_ = false;
      return;
    }
    case MaLocView:
      menuClose();
      showOnMap(loc_lat_, loc_lon_);
      return;
    case MaLocKeep: {
      menuClose();
      const char* t = store_ && store_->openTitle()[0] ? store_->openTitle() : tgt_name_;
      if (!std::strncmp(t, "DM ", 3)) t += 3;
      if (points_) points_->add(loc_lat_, loc_lon_, t);
      snprintf(note_, sizeof(note_), "Gemt på kortet (%.24s)", t);
      return;
    }
  }
}

}  // namespace dm
