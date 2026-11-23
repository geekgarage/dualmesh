// SPDX-License-Identifier: MIT
// DualMesh – lokation som tekst i en besked ("📍 55.67567, 12.56756"), så den virker på begge net
// og i alle apps. parseLoc finder det første "bredde, længde"-par (mindst 3 decimaler hver) i en
// tekst, også uden 📍, fx fra en anden app. Hardware-uafhængig (bygges også på PC).
#pragma once
#include <cstdio>
#include <cstdlib>

namespace dm {
namespace geo {

// "📍 55.67567, 12.56756" (5 decimaler ≈ 1 m).
inline int formatLoc(char* out, size_t n, double lat, double lon) {
  return std::snprintf(out, n, "\xF0\x9F\x93\x8D %.5f, %.5f", lat, lon);
}

namespace detail {
// Et decimaltal med mindst 3 decimaler fra p; returnerer slutningen eller nullptr.
inline const char* number(const char* p, double& v) {
  const char* s = p;
  if (*p == '-') ++p;
  if (*p < '0' || *p > '9') return nullptr;
  while (*p >= '0' && *p <= '9') ++p;
  if (*p != '.') return nullptr;
  ++p;
  int dec = 0;
  while (*p >= '0' && *p <= '9') ++p, ++dec;
  if (dec < 3) return nullptr;
  v = std::strtod(s, nullptr);
  return p;
}
}  // namespace detail

inline bool parseLoc(const char* t, double& lat, double& lon) {
  if (!t) return false;
  for (const char* p = t; *p; ++p) {
    if (p > t && ((p[-1] >= '0' && p[-1] <= '9') || p[-1] == '.')) continue;  // midt i et tal
    double a, b;
    const char* q = detail::number(p, a);
    if (!q) continue;
    if (*q != ',') continue;
    ++q;
    while (*q == ' ') ++q;
    const char* r = detail::number(q, b);
    if (!r || (*r >= '0' && *r <= '9')) continue;
    if (a < -90 || a > 90 || b < -180 || b > 180 || (a == 0 && b == 0)) continue;
    lat = a;
    lon = b;
    return true;
  }
  return false;
}

}  // namespace geo
}  // namespace dm
