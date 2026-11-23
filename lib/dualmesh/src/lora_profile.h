// SPDX-License-Identifier: MIT
// DualMesh – radio profile definitions (no hardware dependencies).
#pragma once
#include <cstdint>

namespace dm {

// Which mesh stack a profile belongs to. Used as a bit index in net masks.
enum class NetId : uint8_t { Meshtastic = 0, MeshCore = 1 };

inline uint8_t netBit(NetId n) { return uint8_t(1u << uint8_t(n)); }

// Complete LoRa PHY description of one network. Everything the scheduler
// needs is in here, so any combination of settings can be scheduled.
struct LoRaProfile {
  const char* name = "";
  NetId net = NetId::Meshtastic;
  uint32_t freq_hz = 869525000;
  uint32_t bw_hz = 250000;      // 7800 ... 500000
  uint8_t sf = 11;              // 5..12
  uint8_t cr = 5;               // 5..8  => 4/5 .. 4/8
  uint8_t sync_word = 0x2B;     // 1-byte LoRa sync word (0x12, 0x2B, ...)
  uint16_t preamble_syms = 16;  // what we transmit and program for RX
  // Shortest preamble other nodes may still send (older firmware).
  // 0 = same as preamble_syms. The scheduler plans for this worst case.
  uint16_t min_air_preamble_syms = 0;
  bool crc_on = true;
  bool implicit_header = false;
  int8_t tx_power_dbm = 22;     // sendeeffekt for dette net (begrænset af region og chip)
};

// SX126x Channel Activity Detection parameters.
struct CadParams {
  uint8_t symbols = 2;    // 1, 2, 4, 8 or 16
  uint8_t det_peak = 22;
  uint8_t det_min = 10;
};

// Two profiles can share one receive configuration when the receiver
// cannot tell them apart anyway (same channel, same modulation, same sync).
inline bool sameRxChannel(const LoRaProfile& a, const LoRaProfile& b) {
  return a.freq_hz == b.freq_hz && a.bw_hz == b.bw_hz && a.sf == b.sf &&
         a.sync_word == b.sync_word && a.crc_on == b.crc_on &&
         a.implicit_header == b.implicit_header;
}

// Same RF channel and modulation, so CAD cannot distinguish them
// (CAD does not look at the sync word).
inline bool sameCadChannel(const LoRaProfile& a, const LoRaProfile& b) {
  return a.freq_hz == b.freq_hz && a.bw_hz == b.bw_hz && a.sf == b.sf;
}

}  // namespace dm
