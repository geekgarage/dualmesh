// SPDX-License-Identifier: MIT
#include "hal/gps.h"

#include <Arduino.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "storage/sd_log.h"
#include "util/wall_clock.h"

namespace dm {

namespace {
constexpr int kPinA = 44, kPinB = 43;  // ESP RX / TX (LilyGo), prøves også omvendt
constexpr uint32_t kDetectMs = 2500;   // så længe ventes på gyldige data pr. forsøg
HardwareSerial& port() { return Serial1; }

int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// "ddmm.mmmm" + N/S -> grader x 1e7 (heltal, ingen afrunding i float).
bool parseCoord(const char* v, const char* hemi, int deg_digits, int32_t& out) {
  if (!v || !*v || !hemi || !*hemi) return false;
  const char* dot = std::strchr(v, '.');
  const int int_len = dot ? int(dot - v) : int(std::strlen(v));
  if (int_len < deg_digits + 2) return false;
  int deg = 0;
  for (int i = 0; i < deg_digits; ++i) deg = deg * 10 + (v[i] - '0');
  // minutter x 1e7
  int64_t min_e7 = 0;
  for (int i = deg_digits; i < int_len; ++i) min_e7 = min_e7 * 10 + (v[i] - '0');
  min_e7 *= 10000000;
  if (dot) {
    int64_t scale = 1000000;
    for (const char* p = dot + 1; *p && scale; ++p, scale /= 10) min_e7 += int64_t(*p - '0') * scale;
  }
  int64_t e7 = int64_t(deg) * 10000000 + min_e7 / 60;
  if (*hemi == 'S' || *hemi == 'W') e7 = -e7;
  out = int32_t(e7);
  return true;
}

// Fast-komma: "12.34" -> 123 (x10)
int32_t x10(const char* v) {
  if (!v || !*v) return 0;
  return int32_t(std::lround(std::atof(v) * 10.0));
}

// UTC (dato + tid) -> unix uden at bruge TZ (timegm findes ikke altid).
uint32_t unixUtc(int y, int mo, int d, int h, int mi, int s) {
  // Dage siden 1970 (Howard Hinnants algoritme).
  y -= mo <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = unsigned((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = int64_t(era) * 146097 + int64_t(doe) - 719468;
  return uint32_t(days * 86400 + h * 3600 + mi * 60 + s);
}
}  // namespace

constexpr uint32_t Gps::kBauds[3];

void Gps::begin(bool enabled, bool set_time) {
  set_time_ = set_time;
  setEnabled(enabled);
}

void Gps::openPort() {
  port().end();
  const int rx = swap_ ? kPinB : kPinA, tx = swap_ ? kPinA : kPinB;
  port().setRxBufferSize(1024);
  port().begin(kBauds[baud_i_], SERIAL_8N1, rx, tx);
  open_ms_ = millis();
  len_ = 0;
}

void Gps::setEnabled(bool on) {
  if (on == enabled_) return;
  enabled_ = on;
  if (on) {
    found_ = false;
    baud_i_ = 0;
    swap_ = false;
    sleep_ = false;
    openPort();
    wake();  // modulet kan stå i dvale fra en tidligere slukning
    DLOG.println("[GPS] slaaet til, leder efter data (38400/9600/115200)");
  } else {
    if (found_ && !sleep_) sendPmreq();  // modulet i dvale (før kørte det videre og brugte strøm)
    port().end();
    fix_.valid = false;
    sleep_ = false;
    DLOG.println("[GPS] slaaet fra");
  }
}

// UBX-RXM-PMREQ (klasse 0x02, id 0x41, 16 byte): version 0, varighed 0 (til vækning), flag 0x06
// (backup + force), vækkekilde 0x08 (UART RX). Fra u-blox' offentlige protokolbeskrivelse.
void Gps::sendPmreq() {
  uint8_t m[24] = {0xB5, 0x62, 0x02, 0x41, 16, 0};
  uint8_t* p = m + 6;
  p[8] = 0x06;   // flags (U4, little endian)
  p[12] = 0x08;  // wakeupSources: uartrx
  uint8_t a = 0, b = 0;
  for (int i = 2; i < 22; ++i) a = uint8_t(a + m[i]), b = uint8_t(b + a);
  m[22] = a;
  m[23] = b;
  port().write(m, sizeof(m));
  port().flush();
}

void Gps::wake() {
  // Noget på UART RX vækker modulet; de første byte efter vækning kan gå tabt.
  static const uint8_t kWake[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  port().write(kWake, sizeof(kWake));
  port().flush();
}

void Gps::setSleep(bool sleep) {
  if (!enabled_ || sleep == sleep_) return;
  if (sleep && !found_) return;  // modulet er ikke fundet endnu: intet at lægge i dvale
  sleep_ = sleep;
  if (sleep) {
    sendPmreq();
    fix_.valid = false;
    sleep_bytes_ = 0;
    sleep_ms_ = millis();
    DLOG.println("[GPS] dvale (intet bruger GPS'en)");
  } else {
    wake();
    last_ok_ms_ = millis();  // giv modulet tid, før baud-søgningen går i gang
    wake_ms_ = millis();
    DLOG.printf("[GPS] vaekket efter %lu s dvale (%lu byte fra modulet imens; 0 = det sov)\n",
                (unsigned long)((millis() - sleep_ms_) / 1000), (unsigned long)sleep_bytes_);
  }
}

Gps::State Gps::state() const {
  if (!enabled_ || sleep_) return State::Off;
  if (!found_ || millis() - last_ok_ms_ > 5000) return State::Searching;
  return fix_.valid ? State::Fix : State::NoFix;
}

const char* Gps::stateText() const {
  switch (state()) {
    case State::Off: return sleep_ ? "dvale (strømspar)" : "slået fra";
    case State::Searching: return "ingen data fra modulet";
    case State::NoFix: return "søger satellitter";
    case State::Fix: return "fix";
  }
  return "";
}

bool Gps::lastPosition(int32_t& lat_e7, int32_t& lon_e7, int32_t& alt_m, uint32_t& age_s) const {
  if (!fix_.fix_ms) return false;
  lat_e7 = fix_.lat_e7;
  lon_e7 = fix_.lon_e7;
  alt_m = fix_.alt_dm / 10;
  age_s = (millis() - fix_.fix_ms) / 1000;
  return true;
}

void Gps::tick() {
  if (!enabled_) return;
  if (sleep_) {  // i dvale: læs og smid væk (tælles: et sovende modul sender intet)
    while (port().available()) port().read(), ++sleep_bytes_;
    return;
  }
  const uint32_t now = millis();
  int budget = 512;  // højst så mange tegn pr. kald (loop'en skal ikke hænge)
  while (budget-- > 0 && port().available()) {
    const char c = char(port().read());
    if (c == '$') {
      len_ = 0;
      buf_[len_++] = c;
    } else if (c == '\r' || c == '\n') {
      if (len_ > 6) {
        buf_[len_] = 0;
        handle(buf_);
      }
      len_ = 0;
    } else if (len_ > 0 && len_ < int(sizeof(buf_)) - 1) {
      buf_[len_++] = c;
    } else {
      len_ = 0;
    }
  }
  // Automatisk søgning efter baudrate/pinne, indtil der kommer gyldige sætninger.
  if (!found_ && now - open_ms_ > kDetectMs) {
    if (++baud_i_ >= 3) {
      baud_i_ = 0;
      swap_ = !swap_;
    }
    openPort();
  }
}

void Gps::handle(char* s) {
  // Checksum: XOR af alt mellem '$' og '*'.
  char* star = std::strrchr(s, '*');
  if (!star || std::strlen(star) < 3) {
    ++bad_;
    return;
  }
  uint8_t sum = 0;
  for (char* p = s + 1; p < star; ++p) sum ^= uint8_t(*p);
  const int h = hexv(star[1]), l = hexv(star[2]);
  if (h < 0 || l < 0 || sum != uint8_t(h * 16 + l)) {
    ++bad_;
    return;
  }
  *star = 0;
  ++ok_;
  last_ok_ms_ = millis();
  if (!found_) {
    found_ = true;
    DLOG.printf("[GPS] modul fundet: %lu baud, RX %d / TX %d\n", (unsigned long)kBauds[baud_i_],
                swap_ ? kPinB : kPinA, swap_ ? kPinA : kPinB);
  }
  // Del i felter (tomme felter bevares).
  char* f[24];
  int n = 0;
  char* p = s + 1;
  f[n++] = p;
  for (; *p && n < 24; ++p)
    if (*p == ',') {
      *p = 0;
      f[n++] = p + 1;
    }
  const char* id = f[0];
  if (std::strlen(id) < 5) return;
  const char* type = id + 2;  // efter talker (GP, GN, ...)
  if (std::strcmp(type, "RMC") == 0) parseRmc(f, n);
  else if (std::strcmp(type, "GGA") == 0) parseGga(f, n);
  else if (std::strcmp(type, "GSV") == 0 && n > 3 && (id[1] == 'P' || id[1] == 'N'))
    sats_view_ = uint8_t(std::atoi(f[3]));
}

// $xxRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,knob,kurs,ddmmyy,...
void Gps::parseRmc(char** f, int n) {
  if (n < 10) return;
  const bool active = f[2][0] == 'A';
  if (active) {
    int32_t lat, lon;
    if (parseCoord(f[3], f[4], 2, lat) && parseCoord(f[5], f[6], 3, lon)) {
      fix_.lat_e7 = lat;
      fix_.lon_e7 = lon;
      fix_.fix_ms = millis();
    }
    fix_.speed_kmh_x10 = uint16_t(x10(f[7]) * 1852 / 1000);
    fix_.course_x10 = uint16_t(x10(f[8]));
  }
  fix_.valid = active;
  if (active && wake_ms_) {
    DLOG.printf("[GPS] fix %.1f s efter vaekning\n", (millis() - wake_ms_) / 1000.0);
    wake_ms_ = 0;
  }
  // Tid: kun med fix (før fix kan u-blox' tid mangle skudsekunder).
  if (active && std::strlen(f[1]) >= 6 && std::strlen(f[9]) == 6) {
    auto two = [](const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); };
    const uint32_t t = unixUtc(2000 + two(f[9] + 4), two(f[9] + 2), two(f[9]), two(f[1]),
                               two(f[1] + 2), two(f[1] + 4));
    fix_.unix = t;
    const uint32_t now = millis();
    if (set_time_ && (!last_time_set_ms_ || now - last_time_set_ms_ > 600000u)) {
      last_time_set_ms_ = now ? now : 1;
      wallclock::set(t, wallclock::Gps);
    }
  }
}

// $xxGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,kvalitet,sat,hdop,højde,M,...
void Gps::parseGga(char** f, int n) {
  if (n < 10) return;
  const int q = std::atoi(f[6]);
  fix_.sats = uint8_t(std::atoi(f[7]));
  fix_.hdop_x10 = uint16_t(x10(f[8]));
  if (q > 0) {
    int32_t lat, lon;
    if (parseCoord(f[2], f[3], 2, lat) && parseCoord(f[4], f[5], 3, lon)) {
      fix_.lat_e7 = lat;
      fix_.lon_e7 = lon;
      fix_.fix_ms = millis();
      fix_.valid = true;
    }
    fix_.alt_dm = x10(f[9]);
  }
}

}  // namespace dm
