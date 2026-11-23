// SPDX-License-Identifier: MIT
// DualMesh – spor (ønske 22. nov 2026): gemmer, hvor man har været, og tegner ruten på kortet.
// Optagelsen skrives løbende til /sd/dualmesh/tracks/<navn>.csv ("bredde,længde,unixtid,højde"
// pr. linje, i bidder hvert 30. s, så SD-bussen ikke blokeres), og når sporet stoppes, laves også
// <navn>.gpx (kan åbnes i andre kortapps). En optagelse fortsætter efter en genstart. Et gemt
// spor kan vises på kortet (ét ad gangen) og slettes.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

// Et punkt i Mercator-koordinater 0..1 (verdenspixel = mx * 256 * 2^zoom), så kortet ikke skal
// regne log/tan for hvert punkt ved hver tegning.
struct TrackPt {
  float mx, my;
};

class Track {
 public:
  static constexpr int kMax = 20000;  // i PSRAM (160 KB); ved fuld tyndes ud (filen har alle)
  void clear() { n_ = 0; }
  void add(double lat, double lon);
  int count() const { return n_; }
  const TrackPt* points() const { return pts_; }
  bool bounds(double& lat0, double& lon0, double& lat1, double& lon1) const;

 private:
  TrackPt* pts_ = nullptr;
  int n_ = 0;
};

class TrackLog {
 public:
  using PersistFn = void (*)(const char* file);  // gemmer navnet på den aktive optagelse ("" = ingen)
  void begin(bool sd_mounted, const char* resume, PersistFn persist);
  void setFilter(int interval_s, int min_m) {
    interval_s_ = interval_s < 1 ? 1 : interval_s;
    min_m_ = min_m < 1 ? 1 : min_m;
  }

  bool recording() const { return rec_; }
  bool start();  // ny optagelse (navn ud fra klokken)
  void stop();   // gem (csv + gpx)
  const char* name() const { return name_; }
  const Track& live() const { return live_; }
  // Kaldes ofte fra loop: fix = aktuel GPS-position.
  void tick(bool fix, double lat, double lon, int32_t alt_m, uint32_t unix_time, uint32_t now_ms);

  // Gemte spor: liste (nyeste først), vis ét, slet.
  int list(char names[][28], int max) const;
  bool show(const char* name);
  void hide() { shown_.clear(), shown_name_[0] = 0; }
  const Track& shown() const { return shown_; }
  const char* shownName() const { return shown_name_; }
  bool remove(const char* name);
  uint32_t version() const { return version_; }  // ændres, når et spor skal tegnes igen

 private:
  void flush();
  void writeGpx();
  bool sd_ = false, rec_ = false;
  PersistFn persist_ = nullptr;
  char name_[28] = "";
  Track live_, shown_;
  char shown_name_[28] = "";
  char* pend_ = nullptr;  // linjer, der venter på at blive skrevet
  size_t pend_n_ = 0;
  uint32_t last_flush_ms_ = 0, last_pt_ms_ = 0;
  double last_lat_ = 0, last_lon_ = 0;
  bool have_last_ = false;
  int interval_s_ = 5, min_m_ = 10;
  uint32_t version_ = 0;
};

}  // namespace dm
