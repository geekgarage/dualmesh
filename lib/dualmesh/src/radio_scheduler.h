// SPDX-License-Identifier: MIT
// DualMesh – single-radio scheduler that listens to several LoRa networks
// (e.g. MeshCore and Meshtastic) by hopping between them with CAD.
//
// Principle: a packet can only be caught while its preamble is on air.
// For every receive group we know how long the preamble lasts, so we know
// the latest moment the next CAD must start (its "deadline"). The scheduler
// always serves the group with the earliest deadline (EDF). This works for
// any combination of frequencies / spreading factors / bandwidths.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "lora_profile.h"
#include "phy_math.h"
#include "radio_hal.h"

namespace dm {

constexpr size_t kMaxProfiles = 4;
constexpr size_t kMaxGroups = 4;
constexpr size_t kTxQueueLen = 8;
constexpr size_t kMaxPacket = 255;

struct RxPacket {
  uint8_t data[kMaxPacket];
  uint16_t len = 0;
  uint8_t group = 0;
  uint8_t net_mask = 0;  // which stacks should parse it (bit per NetId)
  int16_t rssi_dbm = 0;
  int8_t snr_q4 = 0;     // SNR in 0.25 dB steps
  uint64_t rx_end_us = 0;
};

class IPacketSink {
 public:
  virtual ~IPacketSink() = default;
  virtual void onPacket(const RxPacket& pkt) = 0;
  virtual void onTxDone(uint8_t profile, uint32_t tag) = 0;
};

struct SchedulerConfig {
  enum class Mode : uint8_t {
    MaxSensitivity,  // CAD back-to-back: several chances per preamble
    LowPower,        // start each CAD as late as still guaranteed, idle between
  };
  Mode mode = Mode::MaxSensitivity;
  uint32_t retune_us = 400;       // profile switch time (measure on HW!)
  uint32_t cad_fixed_us = 150;    // fixed CAD overhead estimate
  uint32_t irq_latency_us = 100;  // IRQ -> task -> next SPI command
  uint8_t lock_margin_syms = 4;   // preamble symbols needed after CAD to lock
  uint32_t guard_us = 300;        // safety margin in LowPower mode
  uint32_t min_idle_us = 1500;    // shorter idle than this: just keep scanning
  float duty_cycle = 0.10f;       // EU 869.4-869.65 MHz: 10 %, shared by both
  // False-alarm handling:
  bool early_abort = true;        // abort RX if no preamble shows up after a CAD hit
  uint8_t verify_syms = 6;        // symbols to wait for PreambleDetected
  bool adapt_cad = true;          // tune det_peak from the measured noise rate
  uint8_t adapt_max_raise = 6;    // never more than this above Semtech's value
  // Auto-fit never shortens a CAD below this many symbols (Semtech's
  // minimum). Measured on T-Deck (SF11): 1 symbol gave 3-8x more false alarms
  // and needed a much higher det_peak, i.e. weak real packets get missed. The
  // simulator shows that a slightly too long gap costs far less than that.
  // When the plan cannot be met at this floor, EDF still runs best effort.
  uint8_t min_cad_syms = 2;
  // Accept gaps up to this much over slack before going below min_cad_syms.
  // T-Deck MeshCore SF8 overshoots ~9 % (stays at 2 symbols); the SF7 preset
  // overshoots ~39 % (then 1 symbol is the better trade). Keep well clear of both,
  // so the plan does not flip between boots on measurement noise.
  float late_tolerance = 0.15f;
  uint32_t adapt_window = 1000;   // CADs per evaluation
  float noise_high = 0.005f;      // raise det_peak above 0.5 % noise hits
  float noise_low = 0.0005f;      // lower it again below 0.05 %
  // Self-calibration: measure real switch and CAD times on the hardware and
  // re-plan with them (high quantile, so occasional slow cycles are covered).
  bool calibrate = true;
  uint16_t calib_samples = 128;   // samples per measurement before use
  float calib_quantile = 0.98f;
  uint32_t lbt_backoff_min_us = 50000;
  uint32_t lbt_backoff_max_us = 400000;
};

// Static plan for one receive group (computed in start()).
struct GroupPlan {
  LoRaProfile rx;          // receive configuration (shortest member preamble)
  uint8_t net_mask = 0;
  CadParams cad;
  uint8_t base_det_peak = 0;        // Semtech starting value
  uint16_t plan_preamble_syms = 0;  // worst-case preamble used for planning
  bool cad_shortened = false;       // auto-fit reduced CAD symbols
  double sym_us = 0, preamble_us = 0, cad_us = 0;
  double slack_us = 0;     // max allowed time between two CAD starts
  uint32_t rx_timeout_us = 0;
  // Measured CAD overhead beyond N symbols (TCXO start, processing, IRQ
  // latency), per case: [0] no switch before the CAD, [1] after a switch.
  // < 0 = not measured yet; the model value is used.
  double cad_over_us[2] = {-1, -1};
  bool detectable = true;  // false: preamble too short for CAD at all
  bool cad_ambiguous = false;  // shares RF channel with another group
};

struct GroupStats {
  uint32_t cads = 0, detections = 0, rx_ok = 0, rx_bad = 0, rx_timeouts = 0;
  uint32_t noise = 0;          // CAD hit, no preamble: pure noise (aborted or timed out)
  uint32_t weak = 0;           // preamble seen, no packet: weak/foreign signal
  uint32_t preambles = 0;      // PreambleDetected events
  uint32_t peak_changes = 0;   // adaptive det_peak adjustments
  uint64_t cad_time_us = 0, rx_time_us = 0;
  // Gap between consecutive CAD starts of this group, split by cause:
  // max_gap_us: nothing but CADs/idle in between (pure scheduling + system
  //   latency) – must stay <= slack for guaranteed detection.
  // max_gap_busy_us: a CAD hit (RX window) or TX occurred in between; such
  //   gaps are expected and are counted as loss in the simulator.
  uint64_t max_gap_us = 0, max_gap_busy_us = 0;
  uint32_t late_gaps = 0;      // clean gaps longer than slack_us
  // Same as max_gap_us, but only since resetWindow() (the log window), plus
  // when the worst gap ended (hal clock) so it can be matched with other events.
  uint64_t win_max_gap_us = 0, win_max_gap_at_us = 0;
  // Start-up only: false until the first noise window that needs no further
  // raise (calm enough, or det_peak at its limit). Later swings are normal
  // operation and must not make the UI say "stabilising" again.
  bool settled = false;
};

// Is reception at full quality yet? For the UI: show "stabiliserer..." while
// not Stable. Measured on T-Deck: calibration ~5 s, CAD threshold 1-2 min.
enum class Settle : uint8_t {
  Calibrating,  // measuring real switch/CAD times; plan not final yet
  AdaptingCad,  // CAD threshold still rising: extra false alarms, less listening
  Stable,
};

// Collects samples and returns a high quantile.
struct QuantileSampler {
  static constexpr size_t kN = 256;
  std::array<uint32_t, kN> v{};
  uint16_t n = 0;
  void add(uint32_t x) { if (n < kN) v[n++] = x; }
  bool full(uint16_t need) const { return n >= need; }
  uint32_t quantile(float q) const;
  void reset() { n = 0; }
};

struct SchedulerStats {
  std::array<GroupStats, kMaxGroups> g{};
  bool early_abort_active = true;  // switched off if the chip never reports preambles
  uint32_t retunes = 0, tx_ok = 0, tx_lbt_busy = 0, tx_duty_waits = 0,
           watchdog = 0;
  uint64_t tx_time_us = 0, idle_us = 0;
  uint32_t calibrations = 0;      // times the plan was re-made from measurements
  uint32_t cad_too_short = 0;     // CAD events faster than N symbols (ignored)
  double retune_cal_us = 0;       // calibrated switch time (0 = model value)
};

class RadioScheduler {
 public:
  RadioScheduler(IRadioHal& hal, IPacketSink& sink, const SchedulerConfig& cfg);

  // Register networks before start(). Returns profile index or -1.
  int addProfile(const LoRaProfile& p);
  bool start();
  void stop();

  // Drive from the radio task:
  void onRadioEvent(RadioEvt e);
  void onTimer();

  bool enqueueTx(uint8_t profile, const uint8_t* data, uint16_t len,
                 uint32_t tag);

  const SchedulerStats& stats() const { return stats_; }
  // Start a new log window for the win_* fields (benign race with the radio task).
  void resetWindow() {
    for (auto& g : stats_.g) g.win_max_gap_us = g.win_max_gap_at_us = 0;
  }
  Settle settleState() const;
  // Receiving, listening before talk or transmitting: a new TX must wait.
  // Read from another task as a hint (single-byte state, no lock).
  bool busy() const {
    const St s = st_;
    return s == St::Rx || s == St::RxContinuous || s == St::CadTx || s == St::Tx;
  }
  size_t txQueued() const { return tx_count_; }
  size_t groupCount() const { return n_groups_; }
  const GroupPlan& plan(size_t g) const { return groups_[g]; }
  int groupOfProfile(uint8_t p) const { return profile_group_[p]; }

  // Restore a CAD threshold learned earlier (e.g. saved across reboots).
  // Clamped to Semtech's value .. + adapt_max_raise. Call after start().
  void setDetPeak(size_t g, uint8_t peak);

  // Dry run on an empty channel: worst-case gap between CAD starts per group.
  // Detection is guaranteed when gap <= slack.
  void estimateGaps(uint64_t* worst_gap_us, uint32_t iterations = 4000) const;

 private:
  enum class St : uint8_t { Stopped, Idle, Cad, CadTx, Rx, RxContinuous, Tx };
  struct Choice { uint8_t group; uint64_t start_us; };
  struct TxItem {
    uint8_t profile; uint16_t len; uint32_t tag; uint8_t data[kMaxPacket];
  };

  static constexpr int kCfgNone = -1;
  static int cfgRx(int g) { return g; }
  static int cfgTx(int p) { return 100 + p; }

  Choice pick(uint64_t now, const uint64_t* last_start, int applied) const;
  double slotUs(int g, int applied) const;
  void computeTiming(size_t g);
  void finishRx(RadioEvt e, uint64_t now);
  void adapt(uint8_t g);
  void next();
  void beginCad(uint8_t group, bool for_tx);
  bool applyCfg(int cfg);  // true if the radio was re-tuned
  void fitPlan();
  void maybeCalibrate();
  double retuneUs() const;
  double cadCostUs(size_t g, bool after_switch) const;  // CAD start -> next decision
  void refillDuty(uint64_t now);
  void finishOp(uint64_t now);
  uint32_t rnd();

  IRadioHal& hal_;
  IPacketSink& sink_;
  SchedulerConfig cfg_;

  std::array<LoRaProfile, kMaxProfiles> profiles_{};
  std::array<int, kMaxProfiles> profile_group_{};
  size_t n_profiles_ = 0;
  std::array<GroupPlan, kMaxGroups> groups_{};
  size_t n_groups_ = 0;

  St st_ = St::Stopped;
  int applied_ = kCfgNone;
  uint8_t cur_group_ = 0;
  uint64_t op_start_us_ = 0;
  uint64_t op_deadline_us_ = 0;
  uint64_t idle_until_us_ = 0;
  uint64_t verify_until_us_ = 0;
  bool rx_preamble_ = false, rx_header_ = false;
  std::array<uint32_t, kMaxGroups> win_cads_{}, win_noise_{};
  std::array<uint64_t, kMaxGroups> last_cad_start_{};
  std::array<bool, kMaxGroups> seen_{};
  std::array<bool, kMaxGroups> busy_since_cad_{};  // RX/TX since last CAD start
  bool cad_after_switch_ = false;
  QuantileSampler cal_retune_;
  std::array<std::array<QuantileSampler, 2>, kMaxGroups> cal_cad_{};
  bool calibrated_ = false;

  std::array<TxItem, kTxQueueLen> txq_{};
  size_t tx_head_ = 0, tx_count_ = 0;
  uint64_t tx_not_before_us_ = 0;
  uint32_t tx_airtime_us_ = 0;

  double duty_bucket_us_ = 0;
  uint64_t duty_last_us_ = 0;
  uint32_t rng_ = 0x1234567u;

  RxPacket rxbuf_{};
  SchedulerStats stats_{};
};

}  // namespace dm
