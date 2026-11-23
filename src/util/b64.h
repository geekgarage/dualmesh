// SPDX-License-Identifier: MIT
// DualMesh – lille base64-koder/-afkoder (standardalfabet, med '=' til sidst).
// densaugeo/base64 kan kun inkluderes i én oversættelsesenhed (definitioner i .hpp).
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

inline size_t b64Encode(const uint8_t* in, size_t n, char* out, size_t cap) {
  static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < n ? uint32_t(in[i + 1]) << 8 : 0) |
                       (i + 2 < n ? in[i + 2] : 0);
    if (o + 5 > cap) break;
    out[o++] = k[(v >> 18) & 63];
    out[o++] = k[(v >> 12) & 63];
    out[o++] = i + 1 < n ? k[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? k[v & 63] : '=';
  }
  if (cap) out[o < cap ? o : cap - 1] = 0;
  return o;
}

// Returnerer antal bytes, eller -1 ved ugyldige tegn / for lille buffer.
inline int b64Decode(const char* in, uint8_t* out, size_t cap) {
  uint32_t v = 0;
  int bits = 0;
  size_t o = 0;
  for (; *in && *in != '='; ++in) {
    const char c = *in;
    int d;
    if (c >= 'A' && c <= 'Z') d = c - 'A';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
    else if (c >= '0' && c <= '9') d = c - '0' + 52;
    else if (c == '+' || c == '-') d = 62;
    else if (c == '/' || c == '_') d = 63;
    else if (c == ' ') continue;
    else return -1;
    v = (v << 6) | uint32_t(d);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= cap) return -1;
      out[o++] = uint8_t(v >> bits);
    }
  }
  return int(o);
}

}  // namespace dm
