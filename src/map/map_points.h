// SPDX-License-Identifier: MIT
// DualMesh – gemte prikker på kortet (delte lokationer): position + hvor den blev delt
// (kanal- eller DM-navn). Gemmes i /sd/dualmesh/maps/points.txt (tab-separeret, atomisk).
#pragma once
#include <cstdint>

namespace dm {

struct MapPoint {
  double lat = 0, lon = 0;
  char label[24] = "";  // kanal/DM, hvor lokationen blev delt (waypoint: navnet)
  uint8_t kind = 0;     // 0 delt/gemt lokation, 1 Meshtastic-waypoint
  uint32_t wp_id = 0;   // waypointets id (kind 1)
};

class MapPoints {
 public:
  static constexpr int kMax = 64;
  void load(bool sd_mounted);
  int count() const { return n_; }
  const MapPoint& at(int i) const { return pts_[i]; }
  // Tilføjer (samme sted + samme etiket = intet nyt); den ældste ryger, når listen er fuld.
  void add(double lat, double lon, const char* label);
  // Meshtastic-waypoint: samme id opdateres; removeWaypoint ved udløbet/slettet.
  void upsertWaypoint(uint32_t id, double lat, double lon, const char* name);
  void removeWaypoint(uint32_t id);
  void remove(int i);
  void clear();  // alle prikker væk (filen tømmes)

 private:
  void save();
  MapPoint pts_[kMax];
  int n_ = 0;
  bool sd_ = false;
};

}  // namespace dm
