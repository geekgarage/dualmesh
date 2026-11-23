// SPDX-License-Identifier: MIT
// DualMesh – geocaches (ønske 22. nov 2026). Kilde 1: GPX-filer, som brugeren selv lægger i
// /sd/dualmesh/caches/*.gpx (fx en Pocket Query fra geocaching.com eller en download fra
// opencaching.de; begge bruger Groundspeak-udvidelsen). Kilde 2 (senere): Opencaching over WiFi.
// Caches holdes i PSRAM; fund gemmes i /sd/dualmesh/caches/found.txt
// ("kode<TAB>unixtid<TAB>status<TAB>note", status F = fundet, N = ikke fundet).
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

struct Geocache {
  enum Type : uint8_t { Traditional, Multi, Mystery, Letterbox, Wherigo, Event, Earth, Virtual, Webcam, Other };
  char code[12];   // fx GC12345 / OC1234
  char name[48];
  double lat, lon;
  float mx, my;      // Mercator 0..1 (kortet regner ikke log/tan pr. cache ved hver tegning)
  uint8_t type;
  uint8_t size;      // 0 ukendt, 1 mikro, 2 lille, 3 mellem, 4 stor, 5 andet, 6 virtuel
  uint8_t diff2;     // sværhed x 2 (1..10)
  uint8_t terr2;     // terræn x 2
  uint8_t found;     // 0 nej, 1 fundet, 2 ikke fundet (DNF), 3 slettet (skjult i liste og på kort)
  uint32_t hint;     // offset i hint-puljen (0 = intet)
};

class Caches {
 public:
  static constexpr int kMax = 3000;
  void load(bool sd_mounted);  // læser alle GPX-filer + found.txt
  int count() const { return n_; }
  const Geocache& at(int i) const { return c_[i]; }
  const char* hint(const Geocache& g) const { return g.hint && pool_ ? pool_ + g.hint : ""; }
  int find(const char* code) const;
  // Fundet (1) / ikke fundet (2) / slettet (3) / nulstil (0); gemmes på SD.
  void mark(int i, uint8_t status);
  // Field notes (geocache_visits.txt, UTF-16LE: "KODE,2026-11-22T20:15Z,Found it,\"\"") ud fra
  // found.txt; kan uploades på geocaching.com og Opencaching. Antal linjer eller -1.
  int exportFieldNotes() const;
  static const char* typeName(uint8_t t);
  static const char* sizeName(uint8_t s);
  static uint16_t typeColor(uint8_t t);  // RGB565
  // Nærmeste caches (indeks) sorteret efter afstand fra (lat, lon).
  // filter: 0 alle (ikke slettede), 1 ikke fundet, 2 kun slettede.
  int nearest(double lat, double lon, int filter, int* out, int max) const;
  // Valgt mål for navigation (-1 = intet).
  int target() const { return target_; }
  void setTarget(int i) {
    target_ = i;
    ++version_;
  }
  uint32_t version() const { return version_; }

 private:
  void parseFile(const char* path);
  void loadFound();
  uint32_t addHint(const char* s, size_t n);
  Geocache* c_ = nullptr;
  int n_ = 0;
  char* pool_ = nullptr;
  uint32_t pool_n_ = 0;
  bool sd_ = false;
  int target_ = -1;
  uint32_t version_ = 0;
};

// Afstand (m) og kurs (grader, 0 = nord) mellem to punkter.
double geoDistance(double la0, double lo0, double la1, double lo1);
double geoBearing(double la0, double lo0, double la1, double lo1);
const char* compass(double deg);  // "N", "NØ", ...

}  // namespace dm
