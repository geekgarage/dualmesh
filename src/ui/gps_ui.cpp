// SPDX-License-Identifier: MIT
#include "ui/gps_ui.h"

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "hal/gps.h"
#include "storage/sd_store.h"
#include "ui/status_display.h"
#include "util/wall_clock.h"

namespace dm {

bool GpsUi::needsRedraw() const { return millis() - last_draw_ms_ > 1000; }

void GpsUi::view(PanelView& v, int) {
  last_draw_ms_ = millis();
  v.clear();
  if (!gps_) return;
  const GpsFix& f = gps_->fix();
  const Gps::State st = gps_->state();
  snprintf(v.status, sizeof(v.status), "GPS: %s", gps_->stateText());
  v.status_fg = st == Gps::State::Fix ? color::Green : st == Gps::State::Off ? color::Gray : color::Yellow;
  if (gps_->baud())
    snprintf(v.status2, sizeof(v.status2), "Modul: %lu baud, %lu sætninger", (unsigned long)gps_->baud(),
             (unsigned long)gps_->sentences());
  else if (gps_->enabled())
    snprintf(v.status2, sizeof(v.status2), "Modul: leder efter baud og pinde ...");
  char hm[8];
  wallclock::formatHm(wallclock::now(), hm, sizeof(hm));
  if (wallclock::valid()) snprintf(v.status3, sizeof(v.status3), "%s fra %s", hm, wallclock::sourceName(wallclock::source()));

  PanelRow* r = v.add(PanelRow::Item, color::White, "GPS", gps_->enabled() ? color::Green : color::Gray, "%s",
                      gps_->enabled() ? "TIL" : "FRA");
  if (r) r->sel = true;  // eneste valgbare række: klik slår GPS til/fra
  v.info("Satellitter", "%u brugt, %u set, HDOP %u.%u", f.sats, gps_->satsInView(), f.hdop_x10 / 10,
         f.hdop_x10 % 10);
  int32_t lat, lon, alt;
  uint32_t age;
  if (gps_->lastPosition(lat, lon, alt, age)) {
    v.info("Bredde", "%c %ld.%07ld", lat < 0 ? 'S' : 'N', long(std::labs(lat) / 10000000),
           long(std::labs(lat) % 10000000));
    v.info("Længde", "%s %ld.%07ld", lon < 0 ? "V" : "Ø", long(std::labs(lon) / 10000000),
           long(std::labs(lon) % 10000000));
    v.info("Højde og fart", "%ld m, %u.%u km/t", long(alt), f.speed_kmh_x10 / 10, f.speed_kmh_x10 % 10);
    if (age < 5) v.add(PanelRow::Info, color::Gray, "Position", color::Green, "aktuel");
    else v.add(PanelRow::Info, color::Gray, "Position", color::Yellow, "%lu s gammel", (unsigned long)age);
  } else {
    v.add(PanelRow::Info, color::Gray, "Ingen position endnu.");
    v.add(PanelRow::Info, color::Gray, "Første fix: 30 s til flere minutter");
    v.add(PanelRow::Info, color::Gray, "under åben himmel.");
  }
  const char* sos = sos_ ? sos_() : nullptr;
  if (sos && *sos) {
    snprintf(v.note, sizeof(v.note), "%s", sos);
    v.note_fg = color::Red;
  } else {
    snprintf(v.note, sizeof(v.note), "SOS: hold trackball-klik i 5 s");
  }
  snprintf(v.hint, sizeof(v.hint), "Klik: GPS %s   Backspace: tilbage", gps_->enabled() ? "fra" : "til");
  return;
}

void GpsUi::click() {
  if (!gps_) return;
  const bool on = !gps_->enabled();
  gps_->setEnabled(on);
  if (store_) {
    store_->setInt("gps_enabled", on ? 1 : 0);
    store_->save();
  }
}

void GpsUi::key(char c) {
  if (c == '\r' || c == '\n') click();
  else if (c == 0x08 || c == 0x7F) requestClose();
}

}  // namespace dm
