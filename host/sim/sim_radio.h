// SPDX-License-Identifier: MIT
// Discrete-event simulation of an SX1262 + the air around it.
// The real RadioScheduler runs unmodified against this.
#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "phy_math.h"
#include "radio_hal.h"

namespace dm {
namespace sim {

struct AirPkt {
  uint64_t t0 = 0, pre_end = 0, end = 0;
  uint16_t len = 0;
  bool delivered = false;
  bool baseline = false;  // a dedicated always-on receiver would get it
};

struct SimParams {
  double retune_us = 400;       // time spent in applyProfile()
  double cad_fixed_us = 150;
  double irq_latency_us = 100;
  double lock_margin_syms = 4;  // preamble symbols needed after CAD
  double p_detect = 1.0;        // CAD detection probability (per CAD)
  double p_detect_1sym = -1;    // if >= 0: detection probability of a 1-symbol CAD
  double p_false = 0.0;         // CAD false alarm probability (per CAD)
  // Real SX1262 with TCXO (measured on T-Deck Plus): a CAD always ends in
  // STDBY_RC, so the TCXO must start again before the next switch or CAD.
  double tcxo_us = 0;           // 0 = ideal radio (oscillator always on)
  double cad_extra_syms = 0;    // CAD processing beyond the model's N+0.5 symbols
};

class SimRadio : public IRadioHal {
 public:
  SimRadio(const SimParams& sp, uint64_t seed) : sp_(sp), rng_(seed) {}

  // One entry per network on the air.
  std::vector<LoRaProfile> air_profiles;
  std::vector<std::vector<AirPkt>> air;

  uint64_t now = 0;
  bool timer_armed = false;
  uint64_t timer_at = 0;
  uint64_t tx_busy_us = 0;

  struct Ev { uint64_t t; RadioEvt e; };
  std::deque<Ev> evq;

  // ---- IRadioHal ----
  uint64_t nowUs() const override { return now; }

  void applyProfile(const LoRaProfile& p) override {
    now += uint64_t(sp_.retune_us + (xosc_on_ ? 0.0 : sp_.tcxo_us));
    xosc_on_ = true;
    applied_ = p;
    evq.clear();
  }

  void startCad(const CadParams& cad, bool rx_on_detect,
                uint32_t rx_timeout_us) override {
    evq.clear();
    const double sym = phy::symbolUs(applied_);
    const uint64_t s = now + uint64_t(xosc_on_ ? 0.0 : sp_.tcxo_us);  // listening starts
    xosc_on_ = true;
    const uint64_t cad_end = s + uint64_t(phy::cadUs(applied_, cad, sp_.cad_fixed_us) +
                                          sp_.cad_extra_syms * sym);
    const uint64_t lat = uint64_t(sp_.irq_latency_us);

    int hit_net = -1, hit_idx = -1;
    for (size_t n = 0; n < air.size() && hit_net < 0; ++n) {
      if (!sameCadChannel(applied_, air_profiles[n])) continue;
      const auto& v = air[n];
      // last packet with t0 <= s
      auto it = std::upper_bound(v.begin(), v.end(), s,
                                 [](uint64_t x, const AirPkt& a) { return x < a.t0; });
      // check a few candidates backwards (overlapping packets are possible)
      for (int k = 0; k < 4 && it != v.begin(); ++k) {
        --it;
        if (it->t0 <= s && cad_end <= it->pre_end) {
          const double pd = (cad.symbols == 1 && sp_.p_detect_1sym >= 0)
                                ? sp_.p_detect_1sym : sp_.p_detect;
          if (uni_(rng_) < pd) {
            hit_net = int(n);
            hit_idx = int(it - v.begin());
          }
          break;
        }
      }
    }

    if (hit_net >= 0) {
      evq.push_back({cad_end + lat, RadioEvt::CadDetected});
      const AirPkt& pk = air[hit_net][hit_idx];
      const bool sync_ok = air_profiles[hit_net].sync_word == applied_.sync_word;
      const bool lock_ok = double(pk.pre_end - cad_end) >= sp_.lock_margin_syms * sym;
      if (rx_on_detect && lock_ok) {
        // Real preamble: the chip reports it a couple of symbols into RX.
        evq.push_back({cad_end + uint64_t(2 * sym) + lat, RadioEvt::PreambleDetected});
      }
      if (rx_on_detect && sync_ok && lock_ok) {
        rx_net_ = hit_net;
        rx_idx_ = hit_idx;
        evq.push_back({pk.end + lat, RadioEvt::RxDone});
      } else if (rx_on_detect) {
        evq.push_back({cad_end + rx_timeout_us + lat, RadioEvt::RxTimeout});
      }
    } else if (uni_(rng_) < sp_.p_false) {
      evq.push_back({cad_end + lat, RadioEvt::CadDetected});
      if (rx_on_detect)
        evq.push_back({cad_end + rx_timeout_us + lat, RadioEvt::RxTimeout});
    } else {
      evq.push_back({cad_end + lat, RadioEvt::CadClear});
      xosc_on_ = false;  // CAD done -> STDBY_RC
    }
  }

  void startRx(uint32_t /*timeout_us*/) override {
    // Continuous RX (single-network case): next packet we can lock onto.
    evq.clear();
    const double sym = phy::symbolUs(applied_);
    uint64_t best = UINT64_MAX;
    for (size_t n = 0; n < air.size(); ++n) {
      if (!sameRxChannel(applied_, air_profiles[n])) continue;
      for (size_t i = 0; i < air[n].size(); ++i) {
        const AirPkt& pk = air[n][i];
        if (double(pk.pre_end) - double(now) < sp_.lock_margin_syms * sym) continue;
        if (pk.end < best) { best = pk.end; rx_net_ = int(n); rx_idx_ = int(i); }
        break;
      }
    }
    if (best != UINT64_MAX)
      evq.push_back({best + uint64_t(sp_.irq_latency_us), RadioEvt::RxDone});
  }

  void startTx(const uint8_t*, uint16_t len) override {
    evq.clear();
    const uint32_t at = phy::airtimeUs(applied_, len);
    tx_busy_us += at;
    evq.push_back({now + at + uint64_t(sp_.irq_latency_us), RadioEvt::TxDone});
  }

  bool readPacket(uint8_t* buf, uint16_t& len, int16_t& rssi,
                  int8_t& snr) override {
    if (rx_net_ < 0) return false;
    AirPkt& pk = air[rx_net_][rx_idx_];
    pk.delivered = true;
    len = pk.len;
    buf[0] = uint8_t(rx_net_);
    rssi = -100;
    snr = 20;
    rx_net_ = -1;
    return true;
  }

  void standby() override { evq.clear(); }

  void armTimer(uint64_t at) override {
    timer_armed = true;
    timer_at = at;
  }

 private:
  SimParams sp_;
  std::mt19937_64 rng_;
  std::uniform_real_distribution<double> uni_{0.0, 1.0};
  LoRaProfile applied_{};
  bool xosc_on_ = true;  // TCXO running (STDBY_XOSC/RX/TX); false after a clear CAD
  int rx_net_ = -1, rx_idx_ = -1;
};

}  // namespace sim
}  // namespace dm
