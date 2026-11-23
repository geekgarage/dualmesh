// SPDX-License-Identifier: MIT
#include "util/wall_clock.h"

#include <Arduino.h>
#include <Mesh.h>
#include <sys/time.h>

#include <cstdio>
#include <ctime>

#include "storage/sd_log.h"

namespace dm {
namespace wallclock {

namespace {
mesh::RTCClock* g_rtc = nullptr;
Source g_src = None;
uint32_t g_set_ms = 0;
constexpr uint32_t kMinValid = 1735689600u;  // 1/1-2025
}  // namespace

void setRtc(mesh::RTCClock* rtc) { g_rtc = rtc; }

bool set(uint32_t unix, Source src) {
  if (unix < kMinValid || src == None) return false;
  if (src < g_src) return false;
  const uint32_t before = now();
  timeval tv{time_t(unix), 0};
  settimeofday(&tv, nullptr);
  if (g_rtc) g_rtc->setCurrentTime(unix);
  const bool first = g_src == None || src != g_src;
  g_src = src;
  g_set_ms = millis();
  // Log kun første gang pr. kilde eller ved større spring (GPS sætter jævnligt).
  const long jump = before ? long(int64_t(unix) - int64_t(before)) : 0;
  if (first || jump > 2 || jump < -2) {
    time_t t = unix;
    struct tm tm;
    gmtime_r(&t, &tm);
    DLOG.printf("[UR] tid sat fra %s: %04d-%02d-%02d %02d:%02d:%02d UTC (spring %+ld s)\n",
                sourceName(src), tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, jump);
  }
  return true;
}

bool valid() { return g_src != None; }

uint32_t now() { return g_src != None ? uint32_t(time(nullptr)) : 0; }

Source source() { return g_src; }

const char* sourceName(Source s) {
  switch (s) {
    case Mesh: return "mesh";
    case App: return "app";
    case Ntp: return "NTP";
    case Gps: return "GPS";
    default: return "ukendt";
  }
}

uint32_t setAtMs() { return g_set_ms; }

void formatHm(uint32_t unix, char* out, int n) {
  if (!unix) {
    snprintf(out, n, "--:--");
    return;
  }
  time_t t = unix;
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(out, n, "%H:%M", &tm);
}

}  // namespace wallclock
}  // namespace dm
