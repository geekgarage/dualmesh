// SPDX-License-Identifier: MIT
// Runs the real RadioScheduler against simulated MeshCore + Meshtastic
// traffic and reports how many packets it catches compared with a
// dedicated always-listening radio per network.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "presets_meshcore.h"
#include "presets_meshtastic.h"
#include "radio_scheduler.h"
#include "sim_radio.h"

using namespace dm;

static LoRaProfile meshtasticLongFast() {
  LoRaProfile p;
  p.name = "Meshtastic EU LongFast";
  p.net = NetId::Meshtastic;
  p.freq_hz = 869525000; p.bw_hz = 250000; p.sf = 11; p.cr = 5;
  p.sync_word = 0x2B; p.preamble_syms = 16;
  return p;
}
static LoRaProfile meshcoreNarrow(uint8_t sf, uint8_t cr, uint16_t pre,
                                  uint16_t min_air = 0) {
  LoRaProfile p;
  p.name = "MeshCore EU/UK Narrow";
  p.net = NetId::MeshCore;
  p.freq_hz = 869618000; p.bw_hz = 62500; p.sf = sf; p.cr = cr;
  p.sync_word = 0x12; p.preamble_syms = pre;
  p.min_air_preamble_syms = min_air;
  return p;
}

struct NullSink : IPacketSink {
  uint32_t rx = 0, tx = 0;
  void onPacket(const RxPacket&) override { rx++; }
  void onTxDone(uint8_t, uint32_t) override { tx++; }
};

struct Scenario {
  std::string label;
  LoRaProfile mt, mc;
  double util_mt, util_mc;  // channel occupancy 0..1
  SchedulerConfig::Mode mode;
  double p_detect = 1.0, p_false = 0.0;
  double own_tx_period_s = 0;  // 0 = we never transmit
  // Hardware timing (see SimParams) and the scheduler's view of it.
  double tcxo_us = 0, cad_extra_syms = 0;
  bool calibrate = true;
  uint32_t cfg_cad_fixed_us = 150, cfg_retune_us = 400;
  uint8_t min_cad_syms = 2;
  double p_detect_1sym = -1;
};

struct Result {
  double loss_mt, loss_mc;
  uint32_t base_mt, base_mc;
  double radio_active;  // fraction of time not idle
  double late_pct[2];   // clean CAD gaps longer than allowed, % of CADs
  double max_gap_ms[2];
  uint8_t cad_syms[2];
  double plan_gap_ms[2], slack_ms[2];  // calibrated plan after the run
};

static std::vector<sim::AirPkt> genTraffic(const LoRaProfile& p, double util,
                                           double dur_us, std::mt19937_64& rng) {
  std::vector<sim::AirPkt> v;
  std::uniform_int_distribution<int> len(30, 160);
  double mean_at = 0;
  for (int l = 30; l <= 160; ++l) mean_at += phy::airtimeUs(p, uint16_t(l));
  mean_at /= 131.0;
  const double rate = util / mean_at;  // packets per us
  // Older nodes send the shorter preamble: half the traffic when mixed.
  LoRaProfile short_p = p;
  if (p.min_air_preamble_syms) short_p.preamble_syms = p.min_air_preamble_syms;
  std::bernoulli_distribution is_short(p.min_air_preamble_syms ? 0.5 : 0.0);
  std::exponential_distribution<double> gap(rate);
  double t = gap(rng);
  while (t < dur_us) {
    sim::AirPkt a;
    a.len = uint16_t(len(rng));
    const LoRaProfile& tp = is_short(rng) ? short_p : p;
    a.t0 = uint64_t(t);
    a.pre_end = a.t0 + uint64_t(phy::preambleUs(tp));
    a.end = a.t0 + phy::airtimeUs(tp, a.len);
    v.push_back(a);
    t += gap(rng);
  }
  // Dedicated receiver baseline: it locks to a packet if not already busy.
  uint64_t busy = 0;
  for (auto& a : v) {
    if (a.t0 >= busy) { a.baseline = true; busy = a.end; }
  }
  return v;
}

static Result run(const Scenario& sc, uint64_t seed, double dur_s,
                  bool print_plan) {
  std::mt19937_64 rng(seed);
  const double dur = dur_s * 1e6;
  sim::SimParams sp;
  sp.p_detect = sc.p_detect;
  sp.p_false = sc.p_false;
  sp.tcxo_us = sc.tcxo_us;
  sp.cad_extra_syms = sc.cad_extra_syms;
  sp.p_detect_1sym = sc.p_detect_1sym;
  sim::SimRadio radio(sp, seed ^ 0x9E3779B97F4A7C15ull);
  radio.air_profiles = {sc.mt, sc.mc};
  radio.air = {genTraffic(sc.mt, sc.util_mt, dur, rng),
               genTraffic(sc.mc, sc.util_mc, dur, rng)};

  SchedulerConfig cfg;
  cfg.mode = sc.mode;
  cfg.calibrate = sc.calibrate;
  cfg.cad_fixed_us = sc.cfg_cad_fixed_us;
  cfg.retune_us = sc.cfg_retune_us;
  cfg.min_cad_syms = sc.min_cad_syms;
  NullSink sink;
  RadioScheduler s(radio, sink, cfg);
  s.addProfile(sc.mt);
  s.addProfile(sc.mc);
  s.start();

  if (print_plan) {
    uint64_t gaps[kMaxGroups];
    s.estimateGaps(gaps);
    for (size_t g = 0; g < s.groupCount(); ++g) {
      const GroupPlan& gp = s.plan(g);
      std::printf(
          "  %-24s SF%u BW%.1f: symbol %.2f ms, preamble %.1f ms, CAD %u sym "
          "%.1f ms%s, max tilladt hul %.1f ms, faktisk hul %.1f ms -> %s\n",
          gp.rx.name, gp.rx.sf, gp.rx.bw_hz / 1000.0, gp.sym_us / 1000,
          gp.preamble_us / 1000, gp.cad.symbols, gp.cad_us / 1000,
          gp.cad_shortened ? " (auto-forkortet)" : "", gp.slack_us / 1000,
          gaps[g] / 1000.0,
          (gp.detectable && gaps[g] <= gp.slack_us) ? "GARANTERET" : "IKKE garanteret");
    }
  }

  double next_tx = sc.own_tx_period_s > 0 ? sc.own_tx_period_s * 1e6 : 1e18;
  uint8_t payload[100] = {0};
  uint8_t which = 0;

  while (radio.now < uint64_t(dur)) {
    const bool has_ev = !radio.evq.empty();
    const uint64_t t_ev = has_ev ? radio.evq.front().t : UINT64_MAX;
    const uint64_t t_tm = radio.timer_armed ? radio.timer_at : UINT64_MAX;
    uint64_t t_next = std::min(t_ev, t_tm);
    if (t_next == UINT64_MAX) break;
    if (double(t_next) >= next_tx && double(radio.now) <= next_tx) {
      radio.now = std::max(radio.now, uint64_t(next_tx));
      s.enqueueTx(which, payload, 60, 0);
      which ^= 1;
      next_tx += sc.own_tx_period_s * 1e6 / 2;  // alternate networks
      continue;
    }
    if (t_ev <= t_tm) {
      radio.now = std::max(radio.now, t_ev);
      const RadioEvt e = radio.evq.front().e;
      radio.evq.pop_front();
      s.onRadioEvent(e);
    } else {
      radio.now = std::max(radio.now, t_tm);
      radio.timer_armed = false;
      s.onTimer();
    }
  }

  Result r{};
  uint32_t got[2] = {0, 0}, base[2] = {0, 0};
  for (int n = 0; n < 2; ++n)
    for (auto& a : radio.air[n]) {
      if (a.end > uint64_t(dur)) continue;
      if (a.baseline) base[n]++;
      if (a.baseline && a.delivered) got[n]++;
    }
  r.base_mt = base[0];
  r.base_mc = base[1];
  r.loss_mt = base[0] ? 100.0 * (1.0 - double(got[0]) / base[0]) : 0;
  r.loss_mc = base[1] ? 100.0 * (1.0 - double(got[1]) / base[1]) : 0;
  r.radio_active = 1.0 - double(s.stats().idle_us) / dur;
  for (int g = 0; g < 2; ++g) {
    const GroupStats& gs = s.stats().g[g];
    r.late_pct[g] = gs.cads ? 100.0 * gs.late_gaps / gs.cads : 0;
    r.max_gap_ms[g] = gs.max_gap_us / 1000.0;
    r.cad_syms[g] = s.plan(g).cad.symbols;
  }
  uint64_t gaps[kMaxGroups] = {};
  s.estimateGaps(gaps);
  for (int g = 0; g < 2 && g < int(s.groupCount()); ++g) {
    r.plan_gap_ms[g] = gaps[g] / 1000.0;
    r.slack_ms[g] = s.plan(g).slack_us / 1000.0;
  }
  return r;
}

int main(int argc, char** argv) {
  const double dur_s = argc > 1 ? std::atof(argv[1]) : 7200;

  const LoRaProfile mt = meshtasticLongFast();
  struct McVariant { const char* name; LoRaProfile p; };
  const McVariant mcs[] = {
      {"SF8 kun v1.16+  ", meshcoreNarrow(8, 8, 32)},
      {"SF8 blandet     ", meshcoreNarrow(8, 8, 32, 16)},
      {"SF8 kun aeldre  ", meshcoreNarrow(8, 8, 16)},
      {"SF7 blandet     ", meshcoreNarrow(7, 5, 32, 16)},
  };

  std::printf("Planlaegning (tom kanal):\n");
  for (const auto& m : mcs) {
    std::printf(" %s:\n", m.name);
    Scenario sc{m.name, mt, m.p, 0, 0, SchedulerConfig::Mode::MaxSensitivity};
    run(sc, 1, 1, true);
  }

  std::printf("\nSimuleret %.0f s pr. linje. Tab = andel af pakker en dedikeret radio\n"
              "ville have modtaget, som scheduleren missede.\n\n", dur_s);
  std::printf("%-17s %-8s %-11s %-9s %10s %10s %8s\n", "MeshCore-preset",
              "Belast.", "Mode", "CAD-pd", "Tab MT %", "Tab MC %", "Aktiv %");

  const double utils[] = {0.02, 0.05, 0.15, 0.30};
  const char* ulabel[] = {"2 %", "5 %", "15 %", "30 %"};
  for (const auto& m : mcs) {
    for (int u = 0; u < 4; ++u) {
      for (int mode = 0; mode < 2; ++mode) {
        Scenario sc{m.name, mt, m.p, utils[u], utils[u],
                    mode ? SchedulerConfig::Mode::LowPower
                         : SchedulerConfig::Mode::MaxSensitivity};
        Result r = run(sc, 42 + u, dur_s, false);
        std::printf("%-17s %-8s %-11s %-9s %10.2f %10.2f %8.1f\n", m.name,
                    ulabel[u], mode ? "LowPower" : "MaxSens", "1.00",
                    r.loss_mt, r.loss_mc, 100 * r.radio_active);
      }
    }
  }

  std::printf("\nRobusthed (MeshCore SF8 blandet, 5 %% belastning):\n");
  struct R { const char* what; double pd, pf, tx; SchedulerConfig::Mode m; };
  const R rob[] = {
      {"CAD fanger 90 %, MaxSens", 0.90, 0.0, 0, SchedulerConfig::Mode::MaxSensitivity},
      {"CAD fanger 90 %, LowPower", 0.90, 0.0, 0, SchedulerConfig::Mode::LowPower},
      {"1 % falske alarmer", 1.0, 0.01, 0, SchedulerConfig::Mode::MaxSensitivity},
      {"2 % falske alarmer", 1.0, 0.02, 0, SchedulerConfig::Mode::MaxSensitivity},
      {"Egen TX hvert 30. s", 1.0, 0.0, 30, SchedulerConfig::Mode::MaxSensitivity},
  };
  for (const auto& x : rob) {
    Scenario sc{"", mt, mcs[1].p, 0.05, 0.05, x.m, x.pd, x.pf, x.tx};
    Result r = run(sc, 7, dur_s, false);
    std::printf("  %-28s tab MT %5.2f %%  tab MC %5.2f %%\n", x.what, r.loss_mt,
                r.loss_mc);
  }

  // Real T-Deck timing (measured 14 Nov 2026): TCXO restart 1 ms after every
  // clear CAD, CAD ~0.3 symbol longer than the model. "Uden" = fixed plan
  // with the TCXO added to the CAD time; "med" = plan from measured timings.
  std::printf("\nT-Deck-timing (TCXO 1 ms, CAD +0,4 sym; MeshCore SF8 blandet, 5 %%, MaxSens):\n");
  struct TD { const char* name; bool cal; uint8_t min_syms; double pd1; };
  const TD tds[] = {
      {"uden kalibrering", false, 2, -1},
      {"kalib., min 2 sym", true, 2, -1},
      {"kalib., 1 sym tilladt", true, 1, -1},
      {"kalib., 1 sym, pd 90%", true, 1, 0.90},
  };
  for (const auto& td : tds) {
    Scenario sc{"", mt, mcs[1].p, 0.05, 0.05, SchedulerConfig::Mode::MaxSensitivity};
    sc.tcxo_us = 1000;
    sc.cad_extra_syms = 0.4;
    sc.calibrate = td.cal;
    sc.min_cad_syms = td.min_syms;
    sc.p_detect_1sym = td.pd1;
    sc.cfg_cad_fixed_us = 150 + 1000;
    sc.cfg_retune_us = 324;
    Result r = run(sc, 7, dur_s, false);
    std::printf("  %-22s CAD MT/MC %u/%u sym  sene MT %5.2f %% MC %5.2f %%  maxhul MT %5.1f "
                "MC %5.1f ms  tab MT %5.2f %%  tab MC %5.2f %%\n",
                td.name, r.cad_syms[0], r.cad_syms[1],
                r.late_pct[0], r.late_pct[1], r.max_gap_ms[0], r.max_gap_ms[1], r.loss_mt,
                r.loss_mc);
  }

  // Global matrix: every MeshCore preset against every Meshtastic region whose
  // band contains it (region default preset), T-Deck timing, 5 % load each,
  // half of the MeshCore nodes on the old 16-symbol preamble.
  const double gdur = std::min(dur_s, 1800.0);
  std::printf("\nGlobal matrix (T-Deck-timing, kalibreret, 5 %% belastning, %.0f s pr. linje):\n", gdur);
  std::printf("%-26s %-11s %8s %13s %13s %8s %8s  [MT-regioner]\n", "MeshCore-preset",
              "MT-preset", "CAD sym", "hul/tilladt MT", "hul/tilladt MC", "tab MT", "tab MC");
  int rows = 0, over = 0, same = 0;
  for (size_t i = 0; i < mc::presetCount(); ++i) {
    const mc::PresetInfo& mp = mc::presetAt(i);
    LoRaProfile mcp = mc::makeProfile(mp);
    mcp.name = "MeshCore";
    // Timing depends only on the modulation, so regions with the same default
    // preset give identical rows: run once per preset, list the regions.
    struct Row { mt::Preset preset; std::string regions; bool same_channel; };
    std::vector<Row> todo;
    for (size_t k = 0; k < mt::regionCount(); ++k) {
      const mt::RegionInfo& ri = mt::regionAt(k);
      if (!ri.listed || mt::planLicensedOnly(ri.plan)) continue;
      if (!mt::sx1262Supports(ri.id, ri.default_preset)) continue;
      const double f = mp.freq_hz / 1e6;
      if (f < ri.freq_start_mhz || f > ri.freq_end_mhz) continue;
      LoRaProfile mtp;
      if (!mt::makeProfile(ri.id, ri.default_preset, mtp)) continue;
      const bool sc = sameCadChannel(mtp, mcp);
      bool merged = false;
      for (auto& r : todo)
        if (r.preset == ri.default_preset && r.same_channel == sc) {
          r.regions += std::string(",") + ri.name;
          merged = true;
        }
      if (!merged) todo.push_back({ri.default_preset, ri.name, sc});
    }
    if (todo.empty())
      std::printf("%-26.26s (ingen Meshtastic-region dækker %.3f MHz)\n", mp.title, mp.freq_hz / 1e6);
    for (const auto& row : todo) {
      // Any region of the group gives the same timing; same_channel needs the real one.
      LoRaProfile mtp;
      const std::string first = row.regions.substr(0, row.regions.find(','));
      for (size_t k = 0; k < mt::regionCount(); ++k)
        if (first == mt::regionAt(k).name) mt::makeProfile(mt::regionAt(k).id, row.preset, mtp);
      mtp.name = "Meshtastic";
      Scenario sc{"", mtp, mcp, 0.05, 0.05, SchedulerConfig::Mode::MaxSensitivity};
      sc.tcxo_us = 1000;
      sc.cad_extra_syms = 0.4;
      sc.calibrate = true;
      sc.cfg_cad_fixed_us = 150 + 1000;
      sc.cfg_retune_us = 324;
      const Result r = run(sc, 11, gdur, false);
      const bool late = r.plan_gap_ms[0] > r.slack_ms[0] || r.plan_gap_ms[1] > r.slack_ms[1];
      over += late;
      same += row.same_channel;
      ++rows;
      std::printf("%-26.26s %-11s %4u/%-3u %6.0f/%-6.0f %6.0f/%-6.0f %7.2f%% %7.2f%%%s%s  [%s]\n",
                  mp.title, mt::preset(row.preset)->name, r.cad_syms[0], r.cad_syms[1],
                  r.plan_gap_ms[0], r.slack_ms[0], r.plan_gap_ms[1], r.slack_ms[1], r.loss_mt,
                  r.loss_mc, late ? "  (over)" : "", row.same_channel ? "  SAMME KANAL" : "",
                  row.regions.c_str());
    }
  }
  std::printf("SAMME KANAL: samme frekvens/BW/SF med forskelligt sync word; CAD kan ikke skelne\n"
              "nettene, så op mod halvdelen af pakkerne tabes. Kan ikke løses med én radio: advar brugeren.\n");
  std::printf("%d kombinationer, %d med planlagt hul over tilladt, %d på samme kanal\n", rows, over,
              same);
  return 0;
}
