// SPDX-License-Identifier: MIT
// DualMesh – kortets model (fase 7): kortpakker i /sd/dualmesh/maps/*.pmtiles, centrum og zoom,
// og en flisecache i PSRAM (afkodede RGB565-fliser). Tegnes af AppUi (app_ui_lv.cpp).
// Fliser indlæses højst nogle få pr. tegning (pump), så brugerfladen ikke hakker; SD-bussen deles
// med radioen.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

class PmTiles;

// En prik på kortet: egen position (kind 0) eller en node (1 = MeshCore, 2 = Meshtastic).
struct MapMarker {
  double lat = 0, lon = 0;
  uint16_t color = 0xFFFF;  // RGB565 (kontaktfarven)
  uint8_t kind = 0;
  int16_t heading = -1;  // egen kurs i grader (0 = nord), -1 = ukendt/stillestående
  char label[20] = "";
};
// Fylder out (højst max) og returnerer antallet; egen position først, hvis den kendes.
using MapMarkersFn = int (*)(MapMarker* out, int max);

class MapView {
 public:
  static constexpr int kTile = 256;
  static constexpr int kMaxPacks = 8;
  static constexpr int kCache = 20;  // fliser i PSRAM (20 x 128 KB = 2,5 MB)

  // Finder pakker på SD-kortet (kald igen efter at nye er lagt på).
  void scan(bool sd_mounted);
  int packCount() const { return n_packs_; }
  const PmTiles* pack(int i) const { return packs_[i]; }
  void goToPack(int i);       // centrér på pakken (dens centrum og zoom)
  bool removePack(int i);     // slet filen fra SD og find pakkerne igen
  bool hasMaps() const { return n_packs_ > 0; }
  int minZoom() const { return min_z_; }
  int maxZoom() const { return max_z_; }

  // Udsnittet: centrum (grader) og zoom.
  void setCenter(double lat, double lon);
  double centerLat() const;
  double centerLon() const;
  int zoom() const { return z_; }
  void setZoom(int z);
  void panPx(int dx, int dy);  // flyt centrum med skærmpixel
  bool centered() const { return centered_; }  // centrum sat (fra pakke eller GPS)

  // Verdenspixel (ved aktuel zoom) for en position.
  double worldX(double lon) const;
  double worldY(double lat) const;
  double cx() const { return cx_; }  // centrum i verdenspixel
  double cy() const { return cy_; }

  // Afkodet flise (RGB565, byte-byttet), eller nullptr. state: 0 = indlæses, 1 = klar, 2 = mangler.
  const uint16_t* tile(int z, int32_t x, int32_t y, int& state);
  // Indlæs højst max fliser fra køen; true, hvis noget blev indlæst (tegn igen).
  bool pump(int max);
  uint32_t version() const { return version_; }

 private:
  struct Slot {
    int8_t z = -1;
    int32_t x = 0, y = 0;
    uint8_t state = 0;  // 0 tom/køet, 1 klar, 2 mangler
    uint16_t* px = nullptr;
    uint32_t used = 0;
  };
  Slot* find(int z, int32_t x, int32_t y);
  Slot* claim(int z, int32_t x, int32_t y);
  bool load(Slot& s);

  PmTiles* packs_[kMaxPacks] = {};
  int n_packs_ = 0;
  int min_z_ = 0, max_z_ = 0;
  int z_ = 12;
  double cx_ = 0, cy_ = 0;  // centrum i verdenspixel ved z_
  bool centered_ = false;
  Slot cache_[kCache];
  uint32_t tick_ = 0, version_ = 0;
};

}  // namespace dm
