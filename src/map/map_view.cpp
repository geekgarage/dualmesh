// SPDX-License-Identifier: MIT
#include "map/map_view.h"

#include <dirent.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "esp_heap_caps.h"
#include "map/png_decode.h"
#include "map/pmtiles.h"
#include "storage/sd_log.h"

namespace dm {

namespace {
constexpr const char* kDir = "/sd/dualmesh/maps";
constexpr double kPi = 3.14159265358979323846;

double worldSize(int z) { return double(MapView::kTile) * double(1u << z); }
}  // namespace

void MapView::scan(bool sd) {
  for (int i = 0; i < n_packs_; ++i) delete packs_[i];
  n_packs_ = 0;
  for (Slot& s : cache_) s.z = -1;
  if (!sd) {
    DLOG.println("[KORT] intet SD-kort: ingen kort");
    return;
  }
  DIR* d = opendir(kDir);
  if (!d) {
    DLOG.printf("[KORT] ingen kortmappe (%s)\n", kDir);
    return;
  }
  min_z_ = 99;
  max_z_ = 0;
  while (struct dirent* e = readdir(d)) {
    const size_t n = strlen(e->d_name);
    if (n < 9 || strcasecmp(e->d_name + n - 8, ".pmtiles") != 0 || n_packs_ >= kMaxPacks) continue;
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", kDir, e->d_name);
    PmTiles* p = new (std::nothrow) PmTiles;
    if (!p) break;
    if (!p->open(path) || p->tileType() != 2) {  // kun PNG-rasterfliser
      if (p->isOpen()) DLOG.printf("[KORT] %s: kun PNG-fliser understoettes\n", path);
      delete p;
      continue;
    }
    packs_[n_packs_++] = p;
    if (p->minZoom() < min_z_) min_z_ = p->minZoom();
    if (p->maxZoom() > max_z_) max_z_ = p->maxZoom();
    DLOG.printf("[KORT] pakke \"%s\": zoom %u-%u, %lu KB\n", p->name(), p->minZoom(), p->maxZoom(),
                (unsigned long)(p->fileSize() / 1024));
  }
  closedir(d);
  DLOG.printf("[KORT] %d kortpakke(r) i %s\n", n_packs_, kDir);
  if (!n_packs_) {
    min_z_ = max_z_ = 0;
    return;
  }
  if (!centered_) {  // start i den første pakkes centrum
    z_ = packs_[0]->centerZoom();
    if (z_ < min_z_ || z_ > max_z_) z_ = (min_z_ + max_z_) / 2;
    setCenter(packs_[0]->centerLat(), packs_[0]->centerLon());
  }
  ++version_;
}

void MapView::goToPack(int i) {
  if (i < 0 || i >= n_packs_) return;
  const PmTiles* p = packs_[i];
  int z = p->centerZoom();
  if (z < p->minZoom() || z > p->maxZoom()) z = (p->minZoom() + p->maxZoom()) / 2;
  if (z > 13) z = 13;  // et overblik over området
  setZoom(z);
  setCenter(p->centerLat(), p->centerLon());
}

bool MapView::removePack(int i) {
  if (i < 0 || i >= n_packs_) return false;
  char path[96];
  snprintf(path, sizeof(path), "%s", packs_[i]->path());
  delete packs_[i];  // lukker filen først
  packs_[i] = nullptr;
  const bool ok = ::remove(path) == 0;
  DLOG.printf("[KORT] %s %s\n", ok ? "slettet" : "kunne ikke slette", path);
  const bool was_centered = centered_;
  for (int k = i; k + 1 < n_packs_; ++k) packs_[k] = packs_[k + 1];
  --n_packs_;
  scan(true);  // (lukker og åbner de øvrige igen; flisecachen tømmes)
  centered_ = was_centered;
  return ok;
}

double MapView::worldX(double lon) const { return (lon + 180.0) / 360.0 * worldSize(z_); }

double MapView::worldY(double lat) const {
  if (lat > 85.0511) lat = 85.0511;
  if (lat < -85.0511) lat = -85.0511;
  const double r = lat * kPi / 180.0;
  return (1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / kPi) / 2.0 * worldSize(z_);
}

void MapView::setCenter(double lat, double lon) {
  cx_ = worldX(lon);
  cy_ = worldY(lat);
  centered_ = true;
  ++version_;
}

double MapView::centerLon() const { return cx_ / worldSize(z_) * 360.0 - 180.0; }

double MapView::centerLat() const {
  const double n = kPi - 2.0 * kPi * cy_ / worldSize(z_);
  return 180.0 / kPi * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

void MapView::setZoom(int z) {
  if (n_packs_) {
    if (z < min_z_) z = min_z_;
    if (z > max_z_) z = max_z_;
  }
  if (z == z_) return;
  const double lat = centerLat(), lon = centerLon();
  z_ = z;
  cx_ = worldX(lon);
  cy_ = worldY(lat);
  ++version_;
}

void MapView::panPx(int dx, int dy) {
  const double ws = worldSize(z_);
  cx_ += dx;
  cy_ += dy;
  if (cx_ < 0) cx_ += ws;
  if (cx_ >= ws) cx_ -= ws;
  if (cy_ < 0) cy_ = 0;
  if (cy_ > ws) cy_ = ws;
  ++version_;
}

MapView::Slot* MapView::find(int z, int32_t x, int32_t y) {
  for (Slot& s : cache_)
    if (s.z == z && s.x == x && s.y == y) return &s;
  return nullptr;
}

MapView::Slot* MapView::claim(int z, int32_t x, int32_t y) {
  Slot* best = &cache_[0];
  for (Slot& s : cache_) {
    if (s.z < 0) {
      best = &s;
      break;
    }
    if (s.used < best->used) best = &s;
  }
  best->z = int8_t(z);
  best->x = x;
  best->y = y;
  best->state = 0;
  best->used = ++tick_;
  return best;
}

const uint16_t* MapView::tile(int z, int32_t x, int32_t y, int& state) {
  const int32_t n = int32_t(1) << z;
  if (y < 0 || y >= n) {
    state = 2;
    return nullptr;
  }
  x = ((x % n) + n) % n;
  Slot* s = find(z, x, y);
  if (!s) s = claim(z, x, y);
  s->used = ++tick_;
  state = s->state;
  return s->state == 1 ? s->px : nullptr;
}

bool MapView::load(Slot& s) {
  for (int i = 0; i < n_packs_; ++i) {
    PmTiles* p = packs_[i];
    if (s.z < p->minZoom() || s.z > p->maxZoom()) continue;
    size_t len = 0;
    uint8_t* data = p->get(s.z, uint32_t(s.x), uint32_t(s.y), len);
    if (!data) continue;
    if (!s.px) s.px = static_cast<uint16_t*>(heap_caps_malloc(kTile * kTile * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const bool ok = s.px && png::decode565(data, len, s.px, kTile, kTile);
    free(data);
    if (ok) return true;
    DLOG.printf("[KORT] flise %d/%ld/%ld kunne ikke afkodes\n", s.z, (long)s.x, (long)s.y);
  }
  return false;
}

bool MapView::pump(int max) {
  bool any = false;
  while (max-- > 0) {
    // Nyest brugte tomme plads først (det, der er på skærmen nu).
    Slot* pick = nullptr;
    for (Slot& s : cache_)
      if (s.z >= 0 && s.state == 0 && (!pick || s.used > pick->used)) pick = &s;
    if (!pick) break;
    pick->state = load(*pick) ? 1 : 2;
    any = true;
    ++version_;
  }
  return any;
}

}  // namespace dm
