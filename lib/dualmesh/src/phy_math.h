// SPDX-License-Identifier: MIT
// DualMesh – LoRa timing math (Semtech SX126x datasheet / AN1200.13).
#pragma once
#include <cstdint>
#include "lora_profile.h"

namespace dm {
namespace phy {

inline double symbolUs(const LoRaProfile& p) {
  return double(1u << p.sf) * 1e6 / double(p.bw_hz);
}

// Low data rate optimisation is mandatory when a symbol exceeds 16.38 ms.
inline bool ldro(const LoRaProfile& p) { return symbolUs(p) >= 16380.0; }

// Programmed preamble only (excludes the 4.25 / 6.25 sync symbols).
inline double preambleUs(const LoRaProfile& p) {
  return p.preamble_syms * symbolUs(p);
}

// Worst-case preamble actually seen on air (used for scheduling).
inline uint16_t planPreambleSyms(const LoRaProfile& p) {
  return p.min_air_preamble_syms ? p.min_air_preamble_syms : p.preamble_syms;
}
inline double planPreambleUs(const LoRaProfile& p) {
  return planPreambleSyms(p) * symbolUs(p);
}

inline double syncSyms(const LoRaProfile& p) { return p.sf < 7 ? 6.25 : 4.25; }
// Explicit LoRa header: always the first 8 symbols after the sync word (coded at CR 4/8).
inline double headerSyms(const LoRaProfile&) { return 8.0; }

// Time on air for a packet with `payload_len` bytes.
inline uint32_t airtimeUs(const LoRaProfile& p, uint16_t payload_len) {
  const double ts = symbolUs(p);
  const int sf = p.sf, crc = p.crc_on ? 1 : 0, ih = p.implicit_header ? 1 : 0;
  const int de = ldro(p) ? 1 : 0, cr = p.cr - 4;
  const int pl = payload_len;
  int num, den;
  if (sf >= 7) {
    num = 8 * pl - 4 * sf + 28 + 16 * crc - 20 * ih;
    den = 4 * (sf - 2 * de);
  } else {
    num = 8 * pl - 4 * sf + 8 + 16 * crc - 20 * ih;
    den = 4 * sf;
  }
  const int blocks = num > 0 ? (num + den - 1) / den : 0;
  const double n_payload = 8.0 + blocks * (cr + 4);
  const double n_pre = p.preamble_syms + syncSyms(p);
  return uint32_t((n_pre + n_payload) * ts + 0.5);
}

// Semtech's recommended starting points for SX126x CAD (AN1200.48).
// They MUST be fine-tuned on the real hardware (false alarms vs. misses).
inline CadParams defaultCad(const LoRaProfile& p) {
  switch (p.sf) {
    case 5: case 6: case 7: case 8: return {2, 22, 10};
    case 9:  return {4, 23, 10};
    case 10: return {4, 24, 10};
    case 11: return {4, 25, 10};
    default: return {8, 28, 10};
  }
}

// Estimated CAD duration: N symbols plus roughly half a symbol of
// processing, plus a fixed overhead. Replace with measured values.
inline double cadUs(const LoRaProfile& p, const CadParams& c, double fixed_us) {
  return (c.symbols + 0.5) * symbolUs(p) + fixed_us;
}

}  // namespace phy
}  // namespace dm
