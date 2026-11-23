// SPDX-License-Identifier: MIT
#include "radio_scheduler.h"

#include <algorithm>
#include <cstring>

namespace dm {

RadioScheduler::RadioScheduler(IRadioHal& hal, IPacketSink& sink,
                               const SchedulerConfig& cfg)
    : hal_(hal), sink_(sink), cfg_(cfg) {}

int RadioScheduler::addProfile(const LoRaProfile& p) {
  if (st_ != St::Stopped || n_profiles_ >= kMaxProfiles) return -1;
  profiles_[n_profiles_] = p;
  return int(n_profiles_++);
}

bool RadioScheduler::start() {
  if (n_profiles_ == 0) return false;
  n_groups_ = 0;

  // 1) Merge profiles the receiver cannot tell apart into one RX group.
  for (size_t p = 0; p < n_profiles_; ++p) {
    const LoRaProfile& pr = profiles_[p];
    int found = -1;
    for (size_t g = 0; g < n_groups_; ++g)
      if (sameRxChannel(groups_[g].rx, pr)) { found = int(g); break; }
    const uint16_t plan_pre = phy::planPreambleSyms(pr);
    if (found < 0) {
      if (n_groups_ >= kMaxGroups) return false;
      found = int(n_groups_++);
      groups_[found] = GroupPlan{};
      groups_[found].rx = pr;
      groups_[found].plan_preamble_syms = plan_pre;
    } else {
      GroupPlan& gp = groups_[found];
      // Receiver: program the longest preamble (SX126x datasheet advice).
      gp.rx.preamble_syms = std::max(gp.rx.preamble_syms, pr.preamble_syms);
      // Planning: assume the shortest preamble anyone may send.
      gp.plan_preamble_syms = std::min(gp.plan_preamble_syms, plan_pre);
    }
    groups_[found].net_mask |= netBit(pr.net);
    profile_group_[p] = found;
  }

  // 2) Timing plan per group, starting from Semtech's CAD recommendations.
  for (size_t g = 0; g < n_groups_; ++g) {
    groups_[g].cad = phy::defaultCad(groups_[g].rx);
    groups_[g].base_det_peak = groups_[g].cad.det_peak;
    computeTiming(g);
    for (size_t h = 0; h < n_groups_; ++h)
      if (h != g && sameCadChannel(groups_[g].rx, groups_[h].rx))
        groups_[g].cad_ambiguous = true;
  }

  // 3) Auto-fit (see fitPlan). Re-run once real timings are measured.
  calibrated_ = false;
  stats_.retune_cal_us = 0;
  cal_retune_.reset();
  for (auto& gc : cal_cad_)
    for (auto& c : gc) c.reset();
  fitPlan();

  const uint64_t now = hal_.nowUs();
  stats_.early_abort_active = cfg_.early_abort;
  win_cads_.fill(0);
  win_noise_.fill(0);
  seen_.fill(false);
  busy_since_cad_.fill(false);
  last_cad_start_.fill(0);
  duty_bucket_us_ = cfg_.duty_cycle * 3600e6;  // start with a full hour
  duty_last_us_ = now;
  applied_ = kCfgNone;
  st_ = St::Idle;
  op_start_us_ = now;
  next();
  return true;
}

// If any group cannot be revisited within its preamble, shorten the longest
// CAD (fewer symbols) until the plan works or nothing more can be shortened.
// Always starts from Semtech's symbol count, so a re-plan with new timings
// can also undo an earlier shortening. Works for any combination of settings.
void RadioScheduler::fitPlan() {
  for (size_t g = 0; g < n_groups_; ++g) {
    groups_[g].cad.symbols = phy::defaultCad(groups_[g].rx).symbols;
    groups_[g].cad_shortened = false;
    computeTiming(g);
  }
  // Phase 1 keeps every CAD at >= min_cad_syms. Phase 2 (below that floor) is
  // only used when the worst gap would otherwise overshoot its slack by more
  // than late_tolerance: a few % too late costs less than an unreliable CAD.
  uint8_t floor = std::max<uint8_t>(cfg_.min_cad_syms, 1);
  for (int iter = 0; iter < 12 && n_groups_ > 1; ++iter) {
    uint64_t gaps[kMaxGroups];
    estimateGaps(gaps, 2000);
    double worst = 0;  // largest gap / slack
    for (size_t g = 0; g < n_groups_; ++g)
      if (groups_[g].detectable && groups_[g].slack_us > 0)
        worst = std::max(worst, double(gaps[g]) / groups_[g].slack_us);
    if (worst <= 1.0) break;
    int longest = -1;
    for (size_t g = 0; g < n_groups_; ++g) {
      if (groups_[g].cad.symbols / 2 >= floor &&
          (longest < 0 || groups_[g].cad_us > groups_[longest].cad_us))
        longest = int(g);
    }
    if (longest < 0) {
      if (floor > 1 && worst > 1.0 + cfg_.late_tolerance) {
        floor = 1;  // too late by too much: allow shorter CADs after all
        continue;
      }
      break;  // best effort: EDF still serves everyone, slightly late
    }
    groups_[longest].cad.symbols /= 2;
    groups_[longest].cad_shortened = true;
    computeTiming(size_t(longest));
  }
}

uint32_t QuantileSampler::quantile(float q) const {
  if (n == 0) return 0;
  std::array<uint32_t, kN> c = v;
  const size_t k = std::min<size_t>(n - 1, size_t(q * float(n - 1) + 0.5f));
  std::nth_element(c.begin(), c.begin() + k, c.begin() + n);
  return c[k];
}

double RadioScheduler::retuneUs() const {
  return stats_.retune_cal_us > 0 ? stats_.retune_cal_us : double(cfg_.retune_us);
}

double RadioScheduler::cadCostUs(size_t g, bool after_switch) const {
  const GroupPlan& gp = groups_[g];
  double o = gp.cad_over_us[after_switch ? 1 : 0];
  if (o < 0) o = gp.cad_over_us[after_switch ? 0 : 1];
  if (o < 0) return gp.cad_us + cfg_.irq_latency_us;  // model
  return gp.cad.symbols * gp.sym_us + o;
}

// Once enough real timings are collected, re-plan with them.
void RadioScheduler::maybeCalibrate() {
  if (calibrated_ || !cfg_.calibrate || n_groups_ < 2) return;
  const uint16_t need = cfg_.calib_samples;
  if (!cal_retune_.full(need)) return;
  for (size_t g = 0; g < n_groups_; ++g)
    if (!cal_cad_[g][0].full(need) && !cal_cad_[g][1].full(need)) return;
  stats_.retune_cal_us = cal_retune_.quantile(cfg_.calib_quantile);
  for (size_t g = 0; g < n_groups_; ++g) {
    GroupPlan& gp = groups_[g];
    const double listen = gp.cad.symbols * gp.sym_us;  // symbols used while sampling
    for (int k = 0; k < 2; ++k) {
      const QuantileSampler& c = cal_cad_[g][k];
      gp.cad_over_us[k] = c.full(need / 4)
                              ? std::max(0.0, double(c.quantile(cfg_.calib_quantile)) - listen)
                              : -1.0;
    }
  }
  calibrated_ = true;
  fitPlan();
  // Judge the new plan only. Re-planning itself took CPU time (one-off, at
  // boot), so the gaps spanning it count as busy, not as scheduling gaps.
  for (auto& g : stats_.g) g.max_gap_us = g.max_gap_busy_us = g.win_max_gap_us = 0;
  busy_since_cad_.fill(true);
  stats_.calibrations++;  // last: readers polling this see the finished plan
}

void RadioScheduler::stop() {
  hal_.standby();
  st_ = St::Stopped;
}

void RadioScheduler::computeTiming(size_t g) {
  GroupPlan& gp = groups_[g];
  gp.sym_us = phy::symbolUs(gp.rx);
  gp.preamble_us = gp.plan_preamble_syms * gp.sym_us;
  gp.cad_us = phy::cadUs(gp.rx, gp.cad, cfg_.cad_fixed_us);
  // Latest CAD start that still leaves lock_margin symbols of preamble after
  // the CAD has listened. Uses the slowest measured CAD case (e.g. one that
  // must restart the TCXO first); IRQ latency comes after the CAD.
  const double cad_span = std::max(cadCostUs(g, false), cadCostUs(g, true)) -
                          cfg_.irq_latency_us;
  gp.slack_us = gp.preamble_us - cad_span - cfg_.lock_margin_syms * gp.sym_us;
  gp.detectable = gp.slack_us > 0;
  if (!gp.detectable) {
    // Still scan it every round (best effort) so it is never starved.
    gp.slack_us = cadCostUs(g, true) + retuneUs();
  }
  // After a CAD hit the header must arrive within the rest of the longest
  // possible preamble + sync word + the 8-symbol explicit header: the SX1262
  // stops its RX timer only when the header is received (StopTimerOnPreamble
  // off). Without the header symbols a CAD hit early in the preamble timed out
  // just before the header (found 17 Nov 2026 with a Meshtastic 2.8 node: short
  // packets lost, "preamble seen, timeout"). False alarms stay cheap through
  // the early abort, which does not depend on this timeout.
  const double longest_pre = gp.rx.preamble_syms * gp.sym_us;
  gp.rx_timeout_us = uint32_t(std::max(0.0, longest_pre - gp.cad_us) +
                              (phy::syncSyms(gp.rx) + phy::headerSyms(gp.rx) + 2.0) * gp.sym_us + 1000);
}

double RadioScheduler::slotUs(int g, int applied) const {
  const bool sw = applied != cfgRx(g);
  return (sw ? retuneUs() : 0.0) + cadCostUs(size_t(g), sw);
}

// Earliest-deadline-first choice of the next group to scan.
RadioScheduler::Choice RadioScheduler::pick(uint64_t now,
                                            const uint64_t* last_start,
                                            int applied) const {
  auto deadline = [&](size_t g) -> double {
    return seen_[g] ? double(last_start[g]) + groups_[g].slack_us : 0.0;
  };
  size_t best = 0;
  for (size_t g = 1; g < n_groups_; ++g)
    if (deadline(g) < deadline(best)) best = g;

  Choice c{uint8_t(best), now};
  if (cfg_.mode == SchedulerConfig::Mode::LowPower) {
    const double lead = (applied == cfgRx(int(best))) ? 0.0 : retuneUs();
    double start = deadline(best) - lead - cfg_.guard_us;
    // ...but not so late that another group's deadline becomes unreachable.
    for (size_t j = 0; j < n_groups_; ++j) {
      if (j == best) continue;
      const double s = deadline(j) - slotUs(int(best), applied) -
                       retuneUs() - cfg_.guard_us;
      start = std::min(start, s);
    }
    if (start > double(now)) c.start_us = uint64_t(start);
  }
  return c;
}

bool RadioScheduler::applyCfg(int cfg) {
  if (applied_ == cfg) return false;
  const uint64_t t0 = hal_.nowUs();
  if (cfg >= 100) hal_.applyProfile(profiles_[cfg - 100]);
  else hal_.applyProfile(groups_[cfg].rx);
  if (applied_ != kCfgNone) cal_retune_.add(uint32_t(hal_.nowUs() - t0));
  applied_ = cfg;
  stats_.retunes++;
  return true;
}

void RadioScheduler::refillDuty(uint64_t now) {
  const double cap = cfg_.duty_cycle * 3600e6;
  duty_bucket_us_ = std::min(cap, duty_bucket_us_ +
                                      double(now - duty_last_us_) * cfg_.duty_cycle);
  duty_last_us_ = now;
}

uint32_t RadioScheduler::rnd() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return rng_ >> 8;
}

void RadioScheduler::next() {
  if (st_ == St::Stopped || n_groups_ == 0) return;
  const uint64_t now = hal_.nowUs();
  refillDuty(now);

  // Pending transmission?
  if (tx_count_ > 0 && now >= tx_not_before_us_) {
    const TxItem& it = txq_[tx_head_];
    const uint32_t at = phy::airtimeUs(profiles_[it.profile], it.len);
    if (duty_bucket_us_ >= at) {
      tx_airtime_us_ = at;
      beginCad(uint8_t(profile_group_[it.profile]), true);  // listen before talk
      return;
    }
    stats_.tx_duty_waits++;
    tx_not_before_us_ =
        now + uint64_t((at - duty_bucket_us_) / cfg_.duty_cycle) + 1000;
  }

  // Only one channel to listen to: plain continuous RX is strictly better.
  if (n_groups_ == 1) {
    applyCfg(cfgRx(0));
    cur_group_ = 0;
    st_ = St::RxContinuous;
    op_start_us_ = hal_.nowUs();
    hal_.startRx(0);
    if (tx_count_ > 0) hal_.armTimer(tx_not_before_us_);
    return;
  }

  const Choice c = pick(now, last_cad_start_.data(), applied_);
  uint64_t wake = c.start_us;
  if (tx_count_ > 0) wake = std::min(wake, std::max(now, tx_not_before_us_));
  if (wake > now + cfg_.min_idle_us) {
    st_ = St::Idle;
    idle_until_us_ = wake;
    op_start_us_ = now;
    hal_.standby();
    hal_.armTimer(wake);
    return;
  }
  beginCad(c.group, false);
}

void RadioScheduler::beginCad(uint8_t g, bool for_tx) {
  cad_after_switch_ = applyCfg(for_tx ? cfgTx(txq_[tx_head_].profile) : cfgRx(g));
  const uint64_t t = hal_.nowUs();
  if (seen_[g]) {
    const uint64_t gap = t - last_cad_start_[g];
    GroupStats& s = stats_.g[g];
    if (busy_since_cad_[g]) {
      s.max_gap_busy_us = std::max(s.max_gap_busy_us, gap);
    } else {
      s.max_gap_us = std::max(s.max_gap_us, gap);
      if (gap > s.win_max_gap_us) {
        s.win_max_gap_us = gap;
        s.win_max_gap_at_us = t;
      }
      if (double(gap) > groups_[g].slack_us) s.late_gaps++;
    }
  }
  last_cad_start_[g] = t;
  seen_[g] = true;
  busy_since_cad_[g] = false;
  cur_group_ = g;
  st_ = for_tx ? St::CadTx : St::Cad;
  op_start_us_ = t;
  stats_.g[g].cads++;
  win_cads_[g]++;
  adapt(g);
  const GroupPlan& gp = groups_[g];
  // Watchdog: CAD + worst-case RX of a maximum-length packet + margin.
  op_deadline_us_ = t + uint64_t(gp.cad_us) + gp.rx_timeout_us +
                    phy::airtimeUs(gp.rx, kMaxPacket) + 50000;
  hal_.armTimer(op_deadline_us_);
  hal_.startCad(gp.cad, true, gp.rx_timeout_us);
}

void RadioScheduler::onRadioEvent(RadioEvt e) {
  const uint64_t now = hal_.nowUs();
  GroupStats& gs = stats_.g[cur_group_];

  switch (st_) {
    case St::Cad:
    case St::CadTx: {
      gs.cad_time_us += now - op_start_us_;
      if (st_ == St::Cad && !calibrated_) {
        const GroupPlan& gp = groups_[cur_group_];
        const uint64_t d = now - op_start_us_;
        // A CAD cannot finish before it has listened N symbols: such an event
        // is stale or spurious and must not shrink the timing estimate.
        if (double(d) >= gp.cad.symbols * gp.sym_us)
          cal_cad_[cur_group_][cad_after_switch_ ? 1 : 0].add(uint32_t(d));
        else
          stats_.cad_too_short++;
        maybeCalibrate();
      }
      if (e == RadioEvt::CadDetected) {
        gs.detections++;
        if (st_ == St::CadTx) {  // channel busy: receive it, retry later
          stats_.tx_lbt_busy++;
          const uint32_t span = cfg_.lbt_backoff_max_us - cfg_.lbt_backoff_min_us;
          tx_not_before_us_ = now + cfg_.lbt_backoff_min_us + rnd() % (span + 1);
        }
        busy_since_cad_.fill(true);
        st_ = St::Rx;  // the chip is already receiving (CAD_RX exit mode)
        op_start_us_ = now;
        rx_preamble_ = false;
        rx_header_ = false;
        verify_until_us_ = now + uint64_t(cfg_.verify_syms * groups_[cur_group_].sym_us);
        if (stats_.early_abort_active) hal_.armTimer(verify_until_us_);
        return;
      }
      if (e == RadioEvt::CadClear && st_ == St::CadTx) {
        const TxItem& it = txq_[tx_head_];
        duty_bucket_us_ -= tx_airtime_us_;
        busy_since_cad_.fill(true);
        st_ = St::Tx;
        op_start_us_ = now;
        op_deadline_us_ = now + 2ull * tx_airtime_us_ + 50000;
        hal_.armTimer(op_deadline_us_);
        hal_.startTx(it.data, it.len);
        return;
      }
      next();
      return;
    }

    case St::Rx:
    case St::RxContinuous: {
      if (e == RadioEvt::PreambleDetected) {
        if (!rx_preamble_) {
          rx_preamble_ = true;
          gs.preambles++;
          if (st_ == St::Rx) hal_.armTimer(op_deadline_us_);
        }
        return;
      }
      if (e == RadioEvt::HeaderValid) {
        // A header without a preamble report means this chip does not deliver
        // PreambleDetected: early abort would then drop real packets.
        if (!rx_preamble_) stats_.early_abort_active = false;
        rx_preamble_ = rx_header_ = true;
        if (st_ == St::Rx) hal_.armTimer(op_deadline_us_);
        return;
      }
      if (e == RadioEvt::RxDone || e == RadioEvt::RxError || e == RadioEvt::RxTimeout)
        finishRx(e, now);
      return;
    }

    case St::Tx: {
      if (e == RadioEvt::TxDone) {
        stats_.tx_ok++;
        stats_.tx_time_us += now - op_start_us_;
        const TxItem& it = txq_[tx_head_];
        const uint8_t prof = it.profile;
        const uint32_t tag = it.tag;
        tx_head_ = (tx_head_ + 1) % kTxQueueLen;
        tx_count_--;
        sink_.onTxDone(prof, tag);
      }
      next();
      return;
    }

    default:
      return;  // stray IRQ while idle/stopped
  }
}

void RadioScheduler::finishRx(RadioEvt e, uint64_t now) {
  GroupStats& gs = stats_.g[cur_group_];
  gs.rx_time_us += now - op_start_us_;
  if (e == RadioEvt::RxDone) {
    uint16_t len = 0;
    if (hal_.readPacket(rxbuf_.data, len, rxbuf_.rssi_dbm, rxbuf_.snr_q4)) {
      rxbuf_.len = len;
      rxbuf_.group = cur_group_;
      rxbuf_.net_mask = groups_[cur_group_].net_mask;
      rxbuf_.rx_end_us = now;
      gs.rx_ok++;
      sink_.onPacket(rxbuf_);
    } else {
      gs.rx_bad++;
    }
  } else if (e == RadioEvt::RxError) {
    gs.rx_bad++;
  } else {  // RxTimeout
    gs.rx_timeouts++;
    if (st_ == St::Rx) {
      if (rx_preamble_) {
        gs.weak++;
      } else {
        gs.noise++;
        win_noise_[cur_group_]++;
      }
    }
  }
  next();
}

Settle RadioScheduler::settleState() const {
  if (cfg_.calibrate && n_groups_ > 1 && !calibrated_) return Settle::Calibrating;
  // One channel: continuous RX, no CAD to adapt -> stable from the start.
  if (n_groups_ == 1) return Settle::Stable;
  for (size_t g = 0; g < n_groups_; ++g)
    if (cfg_.adapt_cad && !stats_.g[g].settled) return Settle::AdaptingCad;
  return Settle::Stable;
}

void RadioScheduler::setDetPeak(size_t g, uint8_t peak) {
  if (g >= n_groups_) return;
  GroupPlan& gp = groups_[g];
  const uint8_t hi = uint8_t(gp.base_det_peak + cfg_.adapt_max_raise);
  gp.cad.det_peak = std::min(std::max(peak, gp.base_det_peak), hi);
}

void RadioScheduler::adapt(uint8_t g) {
  if (!cfg_.adapt_cad || win_cads_[g] < cfg_.adapt_window) return;
  GroupPlan& gp = groups_[g];
  const float rate = float(win_noise_[g]) / float(win_cads_[g]);
  const bool can_raise = gp.cad.det_peak < gp.base_det_peak + cfg_.adapt_max_raise;
  if (rate <= cfg_.noise_high || !can_raise) stats_.g[g].settled = true;
  if (rate > cfg_.noise_high && can_raise) {
    gp.cad.det_peak++;
    stats_.g[g].peak_changes++;
  } else if (rate < cfg_.noise_low && gp.cad.det_peak > gp.base_det_peak) {
    gp.cad.det_peak--;
    stats_.g[g].peak_changes++;
  }
  win_cads_[g] = 0;
  win_noise_[g] = 0;
}

void RadioScheduler::onTimer() {
  const uint64_t now = hal_.nowUs();
  switch (st_) {
    case St::Idle:
      if (now + 50 >= idle_until_us_) {
        stats_.idle_us += now - op_start_us_;
        next();
      } else {
        hal_.armTimer(idle_until_us_);
      }
      return;
    case St::RxContinuous:
      if (tx_count_ > 0 && now >= tx_not_before_us_) {
        stats_.g[0].rx_time_us += now - op_start_us_;
        hal_.standby();
        next();
      }
      return;
    case St::Rx:
      if (stats_.early_abort_active && !rx_preamble_ && !rx_header_ &&
          now + 50 >= verify_until_us_ && now < op_deadline_us_) {
        // CAD hit but no preamble followed: noise. Stop listening right away.
        hal_.standby();
        GroupStats& gs = stats_.g[cur_group_];
        gs.rx_time_us += now - op_start_us_;
        gs.noise++;
        win_noise_[cur_group_]++;
        next();
        return;
      }
      [[fallthrough]];
    case St::Cad:
    case St::CadTx:
    case St::Tx:
      if (now >= op_deadline_us_) {  // lost IRQ: recover
        stats_.watchdog++;
        hal_.standby();
        applied_ = kCfgNone;
        next();
      }
      return;
    default:
      return;
  }
}

bool RadioScheduler::enqueueTx(uint8_t profile, const uint8_t* data,
                               uint16_t len, uint32_t tag) {
  if (profile >= n_profiles_ || len == 0 || len > kMaxPacket ||
      tx_count_ >= kTxQueueLen)
    return false;
  TxItem& it = txq_[(tx_head_ + tx_count_) % kTxQueueLen];
  it.profile = profile;
  it.len = len;
  it.tag = tag;
  std::memcpy(it.data, data, len);
  tx_count_++;
  if (st_ == St::Idle) {
    stats_.idle_us += hal_.nowUs() - op_start_us_;
    next();
  } else if (st_ == St::RxContinuous) {
    stats_.g[0].rx_time_us += hal_.nowUs() - op_start_us_;
    hal_.standby();
    next();
  }
  return true;
}

void RadioScheduler::estimateGaps(uint64_t* worst, uint32_t iterations) const {
  uint64_t last[kMaxGroups] = {};
  bool seen[kMaxGroups] = {};
  for (size_t g = 0; g < n_groups_; ++g) worst[g] = 0;
  if (n_groups_ < 2) return;
  // pick() reads seen_; emulate with a local copy by temporarily using a
  // const_cast-free approach: replicate the EDF logic here.
  double t = 0;
  int applied = kCfgNone;
  for (uint32_t i = 0; i < iterations; ++i) {
    size_t best = 0;
    auto dl = [&](size_t g) {
      return seen[g] ? double(last[g]) + groups_[g].slack_us : 0.0;
    };
    for (size_t g = 1; g < n_groups_; ++g)
      if (dl(g) < dl(best)) best = g;
    double start = t;
    if (cfg_.mode == SchedulerConfig::Mode::LowPower) {
      const double lead = (applied == cfgRx(int(best))) ? 0.0 : retuneUs();
      double s = dl(best) - lead - cfg_.guard_us;
      for (size_t j = 0; j < n_groups_; ++j) {
        if (j == best) continue;
        s = std::min(s, dl(j) - slotUs(int(best), applied) - retuneUs() -
                            cfg_.guard_us);
      }
      start = std::max(t, s);
    }
    const bool sw = applied != cfgRx(int(best));
    t = start + (sw ? retuneUs() : 0.0);
    if (seen[best] && i > n_groups_)
      worst[best] = std::max<uint64_t>(worst[best], uint64_t(t - double(last[best])));
    last[best] = uint64_t(t);
    seen[best] = true;
    applied = cfgRx(int(best));
    t += cadCostUs(best, sw);
  }
}

}  // namespace dm
