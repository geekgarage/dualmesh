// SPDX-License-Identifier: MIT
#include "map/pmtiles.h"

#include <cstdlib>
#include <cstring>

#include <cstdint>

#include "esp_heap_caps.h"
#include "map/inflate.h"
#include "storage/sd_log.h"

namespace dm {

namespace {
uint64_t rd64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}
int32_t rd32s(const uint8_t* p) { return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24); }

bool varint(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
  v = 0;
  for (int s = 0; s < 64 && p < end; s += 7) {
    const uint8_t b = *p++;
    v |= uint64_t(b & 0x7F) << s;
    if (!(b & 0x80)) return true;
  }
  return false;
}

void* psram(size_t n) { return heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

// Pakker gzip (eller zlib) ud til en ny PSRAM-buffer; max = højeste udpakkede størrelse.
uint8_t* inflate(const uint8_t* in, size_t n, size_t max, size_t& out) {
  size_t skip = 0;
  bool zlib = false;
  if (n >= 10 && in[0] == 0x1F && in[1] == 0x8B) {  // gzip-header (RFC 1952)
    const uint8_t fl = in[3];
    skip = 10;
    if (fl & 4) skip += 2 + (in[skip] | in[skip + 1] << 8);  // FEXTRA
    if (fl & 8) while (skip < n && in[skip++]) {}            // FNAME
    if (fl & 16) while (skip < n && in[skip++]) {}           // FCOMMENT
    if (fl & 2) skip += 2;                                   // FHCRC
  } else {
    zlib = true;
  }
  if (skip >= n) return nullptr;
  uint8_t* buf = static_cast<uint8_t*>(psram(max));
  if (!buf) return nullptr;
  const size_t r = inflateTo(buf, max, in + skip, n - skip, zlib);
  if (r == SIZE_MAX) {
    free(buf);
    return nullptr;
  }
  out = r;
  return buf;
}
}  // namespace

PmTiles::~PmTiles() { close(); }

void PmTiles::close() {
  if (f_) fclose(f_);
  f_ = nullptr;
  free(root_.e);
  root_ = Dir{};
  for (Dir& d : leaves_) {
    free(d.e);
    d = Dir{};
  }
}

uint64_t PmTiles::tileId(int z, uint32_t x, uint32_t y) {
  // Hilbert-kurve inden for zoomniveauet + antal fliser i alle lavere niveauer (spec v3).
  uint64_t acc = 0;
  for (int t = 0; t < z; ++t) acc += uint64_t(1) << (2 * t);
  const uint64_t n = uint64_t(1) << z;
  uint64_t d = 0;
  uint64_t xx = x, yy = y;
  for (uint64_t s = n / 2; s > 0; s /= 2) {
    const uint64_t rx = (xx & s) > 0, ry = (yy & s) > 0;
    d += s * s * ((3 * rx) ^ ry);
    if (ry == 0) {
      if (rx == 1) {
        xx = s - 1 - xx;
        yy = s - 1 - yy;
      }
      const uint64_t t = xx;
      xx = yy;
      yy = t;
    }
  }
  return acc + d;
}

bool PmTiles::readAt(uint64_t off, void* buf, size_t n) {
  if (!f_ || fseek(f_, long(off), SEEK_SET) != 0) return false;
  return fread(buf, 1, n, f_) == n;
}

uint8_t* PmTiles::readBlock(uint64_t off, uint32_t len, size_t& out_len) {
  uint8_t* raw = static_cast<uint8_t*>(psram(len));
  if (!raw || !readAt(off, raw, len)) {
    free(raw);
    return nullptr;
  }
  if (internal_comp_ == 1) {  // ingen komprimering
    out_len = len;
    return raw;
  }
  // gzip: mapper er højst ~10 x komprimeret; prøv med plads nok.
  uint8_t* out = inflate(raw, len, size_t(len) * 16 + 4096, out_len);
  free(raw);
  return out;
}

bool PmTiles::parseDir(const uint8_t* p, size_t n, Dir& d) {
  const uint8_t* end = p + n;
  uint64_t count;
  if (!varint(p, end, count) || count > 200000) return false;
  Entry* e = static_cast<Entry*>(psram(sizeof(Entry) * count));
  if (!e) return false;
  uint64_t v, last = 0;
  for (uint64_t i = 0; i < count; ++i) {
    if (!varint(p, end, v)) goto bad;
    last += v;
    e[i].id = last;
  }
  for (uint64_t i = 0; i < count; ++i) {
    if (!varint(p, end, v)) goto bad;
    e[i].run = uint32_t(v);
  }
  for (uint64_t i = 0; i < count; ++i) {
    if (!varint(p, end, v)) goto bad;
    e[i].length = uint32_t(v);
  }
  for (uint64_t i = 0; i < count; ++i) {
    if (!varint(p, end, v)) goto bad;
    // 0 = lige efter den forrige (offset + længde), ellers offset + 1.
    e[i].offset = (v == 0 && i > 0) ? e[i - 1].offset + e[i - 1].length : v - 1;
  }
  free(d.e);
  d.e = e;
  d.n = uint32_t(count);
  return true;
bad:
  free(e);
  return false;
}

bool PmTiles::open(const char* path) {
  close();
  f_ = fopen(path, "rb");
  if (!f_) return false;
  snprintf(path_, sizeof(path_), "%s", path);
  uint8_t h[127];
  if (!readAt(0, h, sizeof(h)) || memcmp(h, "PMTiles", 7) != 0 || h[7] != 3) {
    DLOG.printf("[KORT] %s er ikke en PMTiles v3-fil\n", path);
    close();
    return false;
  }
  root_off_ = rd64(h + 8);
  root_len_ = rd64(h + 16);
  leaf_off_ = rd64(h + 40);
  data_off_ = rd64(h + 56);
  internal_comp_ = h[97];
  tile_comp_ = h[98];
  tile_type_ = h[99];
  min_z_ = h[100];
  max_z_ = h[101];
  min_lon_e7_ = rd32s(h + 102);
  min_lat_e7_ = rd32s(h + 106);
  max_lon_e7_ = rd32s(h + 110);
  max_lat_e7_ = rd32s(h + 114);
  c_z_ = h[118];
  c_lon_e7_ = rd32s(h + 119);
  c_lat_e7_ = rd32s(h + 123);
  fseek(f_, 0, SEEK_END);
  file_size_ = uint32_t(ftell(f_));
  if ((internal_comp_ != 1 && internal_comp_ != 2) || (tile_comp_ != 1 && tile_comp_ != 2)) {
    DLOG.printf("[KORT] %s: ukendt komprimering (%u/%u)\n", path, internal_comp_, tile_comp_);
    close();
    return false;
  }
  size_t n = 0;
  uint8_t* rd = readBlock(root_off_, uint32_t(root_len_), n);
  const bool ok = rd && parseDir(rd, n, root_);
  free(rd);
  if (!ok) {
    DLOG.printf("[KORT] %s: rodmappen kunne ikke laeses\n", path);
    close();
    return false;
  }
  // Navnet er filnavnet uden ".pmtiles" og med '-' som mellemrum ("Faroe-Islands" -> "Faroe
  // Islands"), så det passer med kataloget (engelske filnavne). Metadataens "name" bruges ikke:
  // den er JSON med \u-escapes (æøå blev vist som "Ã¦") og var dansk i ældre pakker.
  const char* base = strrchr(path, '/');
  snprintf(name_, sizeof(name_), "%s", base ? base + 1 : path);
  if (char* dot = strrchr(name_, '.')) *dot = 0;
  for (char* c = name_; *c; ++c) {
    if (*c == '-' || *c == '_') *c = ' ';
  }
  return true;
}

const PmTiles::Entry* PmTiles::find(const Dir& d, uint64_t id) {
  // Sidste post med id <= søgt.
  int32_t lo = 0, hi = int32_t(d.n) - 1, best = -1;
  while (lo <= hi) {
    const int32_t mid = (lo + hi) / 2;
    if (d.e[mid].id <= id) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (best < 0) return nullptr;
  const Entry& e = d.e[best];
  if (e.run == 0) return &e;                    // bladmappe: skal undersøges
  if (id < e.id + e.run) return &e;             // flise (eller gentagelse af den)
  return nullptr;
}

PmTiles::Dir* PmTiles::leaf(uint64_t off, uint32_t len) {
  Dir* oldest = &leaves_[0];
  for (Dir& d : leaves_) {
    if (d.e && d.offset == off) {
      d.used = ++tick_;
      return &d;
    }
    if (d.used < oldest->used) oldest = &d;
  }
  size_t n = 0;
  uint8_t* raw = readBlock(leaf_off_ + off, len, n);
  if (!raw) return nullptr;
  const bool ok = parseDir(raw, n, *oldest);
  free(raw);
  if (!ok) return nullptr;
  oldest->offset = off;
  oldest->used = ++tick_;
  return oldest;
}

uint8_t* PmTiles::get(int z, uint32_t x, uint32_t y, size_t& len) {
  if (!f_ || z < min_z_ || z > max_z_) return nullptr;
  const uint64_t id = tileId(z, x, y);
  const Dir* d = &root_;
  for (int depth = 0; depth < 4 && d; ++depth) {
    const Entry* e = find(*d, id);
    if (!e) return nullptr;
    if (e->run > 0) {
      uint8_t* raw = static_cast<uint8_t*>(psram(e->length));
      if (!raw || !readAt(data_off_ + e->offset, raw, e->length)) {
        free(raw);
        return nullptr;
      }
      if (tile_comp_ == 2) {  // gzip-komprimerede fliser (sjældent for PNG)
        size_t n = 0;
        uint8_t* out = inflate(raw, e->length, 512 * 1024, n);
        free(raw);
        len = n;
        return out;
      }
      len = e->length;
      return raw;
    }
    d = leaf(e->offset, e->length);
  }
  return nullptr;
}

bool PmTiles::contains(double lat, double lon) const {
  const int32_t la = int32_t(lat * 1e7), lo = int32_t(lon * 1e7);
  return la >= min_lat_e7_ && la <= max_lat_e7_ && lo >= min_lon_e7_ && lo <= max_lon_e7_;
}

}  // namespace dm
