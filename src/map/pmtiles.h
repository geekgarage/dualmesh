// SPDX-License-Identifier: MIT
// DualMesh – læser PMTiles v3-kortpakker fra SD-kortet (fase 7). Skrevet ud fra den offentlige
// PMTiles-specifikation (github.com/protomaps/PMTiles, spec/v3): 127 byte header, rodmappe,
// bladmapper og fliser; mapper er varint-kodede og normalt gzip-komprimerede (pakkes ud med
// ESP32-S3'ens ROM-inflate). Pakkerne laves med tools/maps/make_map.py.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace dm {

class PmTiles {
 public:
  ~PmTiles();
  // Åbn en pakke; false ved fejl (forkert format, ukendt komprimering).
  bool open(const char* path);
  void close();
  bool isOpen() const { return f_ != nullptr; }

  // Hent en flise (z/x/y, XYZ-skema). Data lægges i PSRAM og skal frigives med free(). nullptr =
  // findes ikke i pakken.
  uint8_t* get(int z, uint32_t x, uint32_t y, size_t& len);

  uint8_t minZoom() const { return min_z_; }
  uint8_t maxZoom() const { return max_z_; }
  bool contains(double lat, double lon) const;
  double centerLat() const { return c_lat_e7_ / 1e7; }
  double centerLon() const { return c_lon_e7_ / 1e7; }
  uint8_t centerZoom() const { return c_z_; }
  uint8_t tileType() const { return tile_type_; }  // 2 = PNG
  const char* name() const { return name_; }
  const char* path() const { return path_; }
  uint32_t fileSize() const { return file_size_; }

  static uint64_t tileId(int z, uint32_t x, uint32_t y);

 private:
  struct Entry {
    uint64_t id;
    uint64_t offset;
    uint32_t length;
    uint32_t run;
  };
  // Indlæst mappe (rod eller blad) i PSRAM.
  struct Dir {
    uint64_t offset = 0;  // hvor den lå i filen (nøgle i cachen)
    Entry* e = nullptr;
    uint32_t n = 0;
    uint32_t used = 0;    // LRU-tæller
  };
  bool readAt(uint64_t off, void* buf, size_t n);
  uint8_t* readBlock(uint64_t off, uint32_t len, size_t& out_len);  // pakker ud efter behov
  bool parseDir(const uint8_t* p, size_t n, Dir& d);
  Dir* leaf(uint64_t off, uint32_t len);
  static const Entry* find(const Dir& d, uint64_t id);

  FILE* f_ = nullptr;
  char path_[96] = "";
  char name_[48] = "";
  uint32_t file_size_ = 0;
  uint64_t root_off_ = 0, root_len_ = 0, leaf_off_ = 0, data_off_ = 0;
  uint8_t internal_comp_ = 0, tile_comp_ = 0, tile_type_ = 0;
  uint8_t min_z_ = 0, max_z_ = 0, c_z_ = 0;
  int32_t min_lon_e7_ = 0, min_lat_e7_ = 0, max_lon_e7_ = 0, max_lat_e7_ = 0;
  int32_t c_lon_e7_ = 0, c_lat_e7_ = 0;
  Dir root_;
  static constexpr int kLeaves = 4;
  Dir leaves_[kLeaves];
  uint32_t tick_ = 0;
};

}  // namespace dm
