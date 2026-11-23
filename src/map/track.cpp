// SPDX-License-Identifier: MIT
#include "map/track.h"

#include <Arduino.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esp_heap_caps.h"
#include "storage/sd_log.h"

namespace dm {

namespace {
constexpr const char* kDir = "/sd/dualmesh/tracks";
constexpr size_t kPend = 4096;
constexpr double kPi = 3.14159265358979323846;

void path(char* out, size_t n, const char* name, const char* ext) {
  snprintf(out, n, "%s/%s.%s", kDir, name, ext);
}

// Afstand i meter (fladjordsformel; fint til korte skridt).
double meters(double la0, double lo0, double la1, double lo1) {
  const double k = 111320.0;
  const double dx = (lo1 - lo0) * k * std::cos((la0 + la1) * 0.5 * kPi / 180.0);
  const double dy = (la1 - la0) * k;
  return std::sqrt(dx * dx + dy * dy);
}
}  // namespace

void Track::add(double lat, double lon) {
  if (!pts_) pts_ = static_cast<TrackPt*>(heap_caps_malloc(sizeof(TrackPt) * kMax, MALLOC_CAP_SPIRAM));
  if (!pts_) return;
  if (n_ == kMax) {  // fuld: behold hvert andet punkt (kortet; filen har dem alle)
    for (int i = 0; i < kMax / 2; ++i) pts_[i] = pts_[2 * i];
    n_ = kMax / 2;
  }
  if (lat > 85.0511) lat = 85.0511;
  if (lat < -85.0511) lat = -85.0511;
  const double r = lat * kPi / 180.0;
  pts_[n_].mx = float((lon + 180.0) / 360.0);
  pts_[n_].my = float((1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / kPi) / 2.0);
  ++n_;
}

bool Track::bounds(double& lat0, double& lon0, double& lat1, double& lon1) const {
  if (!n_) return false;
  float x0 = 1, y0 = 1, x1 = 0, y1 = 0;
  for (int i = 0; i < n_; ++i) {
    x0 = std::fmin(x0, pts_[i].mx), x1 = std::fmax(x1, pts_[i].mx);
    y0 = std::fmin(y0, pts_[i].my), y1 = std::fmax(y1, pts_[i].my);
  }
  auto lat = [](double my) {
    const double n = kPi - 2.0 * kPi * my;
    return 180.0 / kPi * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
  };
  lon0 = x0 * 360.0 - 180.0;
  lon1 = x1 * 360.0 - 180.0;
  lat0 = lat(y1);
  lat1 = lat(y0);
  return true;
}

void TrackLog::begin(bool sd, const char* resume, PersistFn persist) {
  sd_ = sd;
  persist_ = persist;
  if (!sd || !resume || !*resume) return;
  // En optagelse var i gang før genstarten: læs punkterne ind igen og fortsæt i samme fil.
  snprintf(name_, sizeof(name_), "%s", resume);
  char p[64];
  path(p, sizeof(p), name_, "csv");
  if (FILE* f = fopen(p, "r")) {
    char line[96];
    while (fgets(line, sizeof(line), f)) {
      double la, lo;
      if (sscanf(line, "%lf,%lf", &la, &lo) == 2) {
        live_.add(la, lo);
        last_lat_ = la, last_lon_ = lo, have_last_ = true;
      }
    }
    fclose(f);
  }
  rec_ = true;
  ++version_;
  DLOG.printf("[SPOR] fortsaetter optagelsen %s (%d punkter)\n", name_, live_.count());
}

bool TrackLog::start() {
  if (rec_) return true;
  if (!sd_) return false;
  mkdir("/sd/dualmesh", 0775);
  mkdir(kDir, 0775);
  const time_t t = time(nullptr);
  if (t > 1735689600) {  // klokken kendes (efter 2025)
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(name_, sizeof(name_), "%Y%m%d-%H%M%S", &tm);
  } else {
    snprintf(name_, sizeof(name_), "spor-%lu", (unsigned long)(millis() / 1000));
  }
  live_.clear();
  have_last_ = false;
  pend_n_ = 0;
  last_pt_ms_ = 0;
  rec_ = true;
  ++version_;
  if (persist_) persist_(name_);
  DLOG.printf("[SPOR] optagelse startet: %s\n", name_);
  return true;
}

void TrackLog::stop() {
  if (!rec_) return;
  flush();
  writeGpx();
  rec_ = false;
  if (persist_) persist_("");
  DLOG.printf("[SPOR] optagelse stoppet: %s (%d punkter)\n", name_, live_.count());
  live_.clear();
  ++version_;
}

void TrackLog::tick(bool fix, double lat, double lon, int32_t alt_m, uint32_t unix_time, uint32_t now) {
  if (!rec_) return;
  if (fix && (!last_pt_ms_ || now - last_pt_ms_ >= uint32_t(interval_s_) * 1000u)) {
    if (!have_last_ || meters(last_lat_, last_lon_, lat, lon) >= min_m_) {
      last_pt_ms_ = now;
      live_.add(lat, lon);
      last_lat_ = lat, last_lon_ = lon, have_last_ = true;
      ++version_;
      if (!pend_) pend_ = static_cast<char*>(heap_caps_malloc(kPend, MALLOC_CAP_SPIRAM));
      if (pend_) {
        if (pend_n_ + 64 > kPend) flush();
        pend_n_ += size_t(snprintf(pend_ + pend_n_, kPend - pend_n_, "%.6f,%.6f,%lu,%ld\n", lat, lon,
                                   (unsigned long)unix_time, (long)alt_m));
      }
    }
  }
  if (pend_n_ && now - last_flush_ms_ >= 30000) flush();
}

void TrackLog::flush() {
  last_flush_ms_ = millis();
  if (!pend_n_ || !sd_) return;
  char p[64];
  path(p, sizeof(p), name_, "csv");
  if (FILE* f = fopen(p, "a")) {
    fwrite(pend_, 1, pend_n_, f);
    fclose(f);
  } else {
    DLOG.printf("[SPOR] kunne ikke skrive %s\n", p);
  }
  pend_n_ = 0;
}

void TrackLog::writeGpx() {
  char in[64], out[64];
  path(in, sizeof(in), name_, "csv");
  path(out, sizeof(out), name_, "gpx");
  FILE* f = fopen(in, "r");
  if (!f) return;
  FILE* g = fopen(out, "w");
  if (!g) {
    fclose(f);
    return;
  }
  fprintf(g,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<gpx version=\"1.1\" creator=\"DualMesh\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
          "<trk><name>%s</name><trkseg>\n",
          name_);
  char line[96];
  while (fgets(line, sizeof(line), f)) {
    double la, lo;
    unsigned long ts = 0;
    long alt = 0;
    const int k = sscanf(line, "%lf,%lf,%lu,%ld", &la, &lo, &ts, &alt);
    if (k < 2) continue;
    fprintf(g, "<trkpt lat=\"%.6f\" lon=\"%.6f\">", la, lo);
    if (k >= 4) fprintf(g, "<ele>%ld</ele>", alt);
    if (k >= 3 && ts > 1735689600) {
      const time_t t = time_t(ts);
      struct tm tm;
      gmtime_r(&t, &tm);
      char iso[24];
      strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm);
      fprintf(g, "<time>%s</time>", iso);
    }
    fputs("</trkpt>\n", g);
  }
  fputs("</trkseg></trk>\n</gpx>\n", g);
  fclose(g);
  fclose(f);
}

int TrackLog::list(char names[][28], int max) const {
  if (!sd_) {
    DLOG.println("[SPOR] liste: intet SD-kort");
    return 0;
  }
  DIR* d = opendir(kDir);
  if (!d) {
    DLOG.printf("[SPOR] liste: kan ikke aabne %s\n", kDir);
    return 0;
  }
  int n = 0, seen = 0;
  while (struct dirent* e = readdir(d)) {
    ++seen;
    const size_t l = std::strlen(e->d_name);
    if (l < 5 || l > 30 || strcasecmp(e->d_name + l - 4, ".csv") != 0) continue;
    char nm[28];
    snprintf(nm, sizeof(nm), "%.*s", int(l - 4), e->d_name);
    if (rec_ && !std::strcmp(nm, name_)) continue;  // den aktive optagelse
    // Indsæt sorteret, nyeste (største navn) først.
    int pos = n < max ? n : max - 1;
    if (n >= max && std::strcmp(nm, names[max - 1]) < 0) continue;
    while (pos > 0 && std::strcmp(nm, names[pos - 1]) > 0) {
      std::memcpy(names[pos], names[pos - 1], sizeof(names[0]));
      --pos;
    }
    std::memcpy(names[pos], nm, sizeof(names[0]));
    if (n < max) ++n;
  }
  closedir(d);
  DLOG.printf("[SPOR] liste: %d spor (%d filer i mappen)\n", n, seen);
  return n;
}

bool TrackLog::show(const char* name) {
  char p[64];
  path(p, sizeof(p), name, "csv");
  FILE* f = fopen(p, "r");
  if (!f) return false;
  shown_.clear();
  char line[96];
  while (fgets(line, sizeof(line), f)) {
    double la, lo;
    if (sscanf(line, "%lf,%lf", &la, &lo) == 2) shown_.add(la, lo);
  }
  fclose(f);
  snprintf(shown_name_, sizeof(shown_name_), "%s", name);
  ++version_;
  DLOG.printf("[SPOR] viser %s (%d punkter)\n", name, shown_.count());
  return shown_.count() > 0;
}

bool TrackLog::remove(const char* name) {
  char p[64];
  path(p, sizeof(p), name, "csv");
  const bool ok = ::remove(p) == 0;
  path(p, sizeof(p), name, "gpx");
  ::remove(p);
  if (!std::strcmp(shown_name_, name)) hide();
  ++version_;
  DLOG.printf("[SPOR] slettet %s\n", name);
  return ok;
}

}  // namespace dm
