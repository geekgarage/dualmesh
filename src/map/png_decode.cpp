// SPDX-License-Identifier: MIT
#include "map/png_decode.h"

#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "map/inflate.h"

namespace dm {
namespace png {

namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

inline uint16_t rgb565swap(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t v = uint16_t((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
  return uint16_t(v << 8 | v >> 8);
}

inline uint8_t paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
  return uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}
}  // namespace

bool decode565(const uint8_t* d, size_t len, uint16_t* out, int w, int h) {
  static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (len < 33 || memcmp(d, kSig, 8) != 0) return false;
  size_t pos = 8;
  int width = 0, height = 0, depth = 0, ctype = -1;
  uint16_t pal[256] = {};
  // IDAT-data samles i én buffer (kortfliser er små: typisk 10-40 KB).
  uint8_t* z = static_cast<uint8_t*>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!z) return false;
  size_t zn = 0;
  while (pos + 12 <= len) {
    const uint32_t n = be32(d + pos);
    const uint8_t* type = d + pos + 4;
    const uint8_t* c = d + pos + 8;
    if (pos + 12 + n > len) break;
    if (!memcmp(type, "IHDR", 4) && n >= 13) {
      width = int(be32(c));
      height = int(be32(c + 4));
      depth = c[8];
      ctype = c[9];
      if (c[12] != 0) {  // interlace understøttes ikke
        free(z);
        return false;
      }
    } else if (!memcmp(type, "PLTE", 4)) {
      for (uint32_t i = 0; i + 2 < n && i / 3 < 256; i += 3) pal[i / 3] = rgb565swap(c[i], c[i + 1], c[i + 2]);
    } else if (!memcmp(type, "IDAT", 4)) {
      memcpy(z + zn, c, n);
      zn += n;
    } else if (!memcmp(type, "IEND", 4)) {
      break;
    }
    pos += 12 + n;
  }
  int bpp;  // bit pr. pixel
  if (ctype == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) bpp = depth;
  else if (ctype == 0 && depth == 8) bpp = 8;
  else if (ctype == 2 && depth == 8) bpp = 24;
  else if (ctype == 6 && depth == 8) bpp = 32;
  else bpp = 0;
  if (!bpp || width != w || height != h || !zn) {
    free(z);
    return false;
  }
  const size_t stride = (size_t(width) * bpp + 7) / 8;
  const size_t raw_len = (stride + 1) * size_t(height);
  uint8_t* raw = static_cast<uint8_t*>(heap_caps_malloc(raw_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!raw) {
    free(z);
    return false;
  }
  const size_t got = inflateTo(raw, raw_len, z, zn, true);
  free(z);
  if (got != raw_len) {
    free(raw);
    return false;
  }
  // Fjern linjefiltrene (PNG-spec: None, Sub, Up, Average, Paeth) på stedet.
  const int bpx = bpp >= 8 ? bpp / 8 : 1;  // byte pr. pixel (mindst 1) til filtrene
  uint8_t* prev = nullptr;
  for (int y = 0; y < height; ++y) {
    uint8_t* line = raw + size_t(y) * (stride + 1);
    const uint8_t f = line[0];
    uint8_t* s = line + 1;
    for (size_t i = 0; i < stride; ++i) {
      const int a = i >= size_t(bpx) ? s[i - bpx] : 0;
      const int b = prev ? prev[i] : 0;
      const int c = (prev && i >= size_t(bpx)) ? prev[i - bpx] : 0;
      switch (f) {
        case 1: s[i] = uint8_t(s[i] + a); break;
        case 2: s[i] = uint8_t(s[i] + b); break;
        case 3: s[i] = uint8_t(s[i] + ((a + b) >> 1)); break;
        case 4: s[i] = uint8_t(s[i] + paeth(a, b, c)); break;
        default: break;
      }
    }
    prev = s;
    uint16_t* o = out + size_t(y) * width;
    if (ctype == 3) {
      if (depth == 8) {
        for (int x = 0; x < width; ++x) o[x] = pal[s[x]];
      } else {
        const int per = 8 / depth, mask = (1 << depth) - 1;
        for (int x = 0; x < width; ++x) {
          const int sh = 8 - depth * (x % per + 1);
          o[x] = pal[(s[x / per] >> sh) & mask];
        }
      }
    } else if (ctype == 0) {
      for (int x = 0; x < width; ++x) o[x] = rgb565swap(s[x], s[x], s[x]);
    } else {
      const int step = bpp / 8;
      for (int x = 0; x < width; ++x) o[x] = rgb565swap(s[x * step], s[x * step + 1], s[x * step + 2]);
    }
  }
  free(raw);
  return true;
}

}  // namespace png
}  // namespace dm
