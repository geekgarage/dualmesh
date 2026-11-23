// SPDX-License-Identifier: MIT
#include "map/map_points.h"

#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "storage/sd_log.h"

namespace dm {

namespace {
constexpr const char* kDir = "/sd/dualmesh/maps";
constexpr const char* kFile = "/sd/dualmesh/maps/points.txt";
constexpr const char* kTmp = "/sd/dualmesh/maps/points.tmp";
constexpr const char* kBak = "/sd/dualmesh/maps/points.bak";
}  // namespace

void MapPoints::load(bool sd) {
  sd_ = sd;
  n_ = 0;
  if (!sd) return;
  FILE* f = fopen(kFile, "r");
  if (!f) f = fopen(kBak, "r");
  if (!f) return;
  char line[96];
  while (n_ < kMax && fgets(line, sizeof(line), f)) {
    // bredde<TAB>længde<TAB>etiket
    char* t1 = strchr(line, '\t');
    if (!t1) continue;
    char* t2 = strchr(t1 + 1, '\t');
    if (!t2) continue;
    MapPoint& p = pts_[n_];
    p.lat = strtod(line, nullptr);
    p.lon = strtod(t1 + 1, nullptr);
    char* lab = t2 + 1;
    lab[strcspn(lab, "\r\n")] = 0;
    p.kind = 0;
    p.wp_id = 0;
    if (char* t3 = strchr(lab, '\t')) {  // nyere linjer: <TAB>art<TAB>waypoint-id
      *t3 = 0;
      p.kind = uint8_t(atoi(t3 + 1));
      if (char* t4 = strchr(t3 + 1, '\t')) p.wp_id = uint32_t(strtoul(t4 + 1, nullptr, 10));
    }
    snprintf(p.label, sizeof(p.label), "%s", lab);
    if (p.lat >= -90 && p.lat <= 90 && p.lon >= -180 && p.lon <= 180) ++n_;
  }
  fclose(f);
  DLOG.printf("[KORT] %d gemte prikker\n", n_);
}

void MapPoints::save() {
  if (!sd_) return;
  mkdir(kDir, 0775);
  FILE* f = fopen(kTmp, "w");
  if (!f) {
    DLOG.println("[KORT] kunne ikke gemme prikkerne");
    return;
  }
  for (int i = 0; i < n_; ++i)
    fprintf(f, "%.6f\t%.6f\t%s\t%u\t%lu\n", pts_[i].lat, pts_[i].lon, pts_[i].label, pts_[i].kind,
            (unsigned long)pts_[i].wp_id);
  fclose(f);
  ::remove(kBak);
  rename(kFile, kBak);
  if (rename(kTmp, kFile) != 0) DLOG.println("[KORT] kunne ikke omdoebe points.tmp");
}

void MapPoints::add(double lat, double lon, const char* label) {
  for (int i = 0; i < n_; ++i)
    if (std::fabs(pts_[i].lat - lat) < 1e-6 && std::fabs(pts_[i].lon - lon) < 1e-6 &&
        !strcmp(pts_[i].label, label ? label : ""))
      return;
  if (n_ == kMax) {  // den ældste ryger (gemmes nedenfor)
    for (int k = 0; k + 1 < n_; ++k) pts_[k] = pts_[k + 1];
    --n_;
  }
  MapPoint& p = pts_[n_++];
  p = MapPoint{};
  p.lat = lat;
  p.lon = lon;
  snprintf(p.label, sizeof(p.label), "%s", label ? label : "");
  save();
}

void MapPoints::upsertWaypoint(uint32_t id, double lat, double lon, const char* name) {
  for (int i = 0; i < n_; ++i)
    if (pts_[i].kind == 1 && pts_[i].wp_id == id) {
      pts_[i].lat = lat;
      pts_[i].lon = lon;
      snprintf(pts_[i].label, sizeof(pts_[i].label), "%s", name && *name ? name : "Waypoint");
      save();
      return;
    }
  if (n_ == kMax) {  // den ældste ryger
    for (int k = 0; k + 1 < n_; ++k) pts_[k] = pts_[k + 1];
    --n_;
  }
  MapPoint& p = pts_[n_++];
  p = MapPoint{};
  p.lat = lat;
  p.lon = lon;
  p.kind = 1;
  p.wp_id = id;
  snprintf(p.label, sizeof(p.label), "%s", name && *name ? name : "Waypoint");
  save();
}

void MapPoints::removeWaypoint(uint32_t id) {
  for (int i = 0; i < n_; ++i)
    if (pts_[i].kind == 1 && pts_[i].wp_id == id) {
      remove(i);
      return;
    }
}

void MapPoints::clear() {
  n_ = 0;
  save();
  DLOG.println("[KORT] alle gemte prikker ryddet");
}

void MapPoints::remove(int i) {
  if (i < 0 || i >= n_) return;
  for (int k = i; k + 1 < n_; ++k) pts_[k] = pts_[k + 1];
  --n_;
  save();
}

}  // namespace dm
