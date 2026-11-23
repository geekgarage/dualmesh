// SPDX-License-Identifier: MIT
// DualMesh – fase 1 testfirmware til LilyGo T-Deck Plus.
// Kun radio: måler profilskift, kører scheduleren på MeshCore + Meshtastic,
// logger hver modtaget pakke og skriver statistik hvert 10. sekund.
// Status vises på T-Deck'ens skærm og i seriel monitor.
// Taster i seriel monitor:  s = statistik nu,  p = plan,  h = hjælp
#include <Arduino.h>
#include <new>

#include "storage/sd_log.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nvs.h"
#include "hal/gps.h"
#include "hal/sx1262_hal.h"
#include "net/channel_scan.h"
#include "net/meshcore_node.h"
#include "net/meshcore_radio.h"
#include "net/ble_serial.h"
#include "net/mc_companion.h"
#include "net/mt_ble.h"
#include "net/mt_phone.h"
#include "net/mt_node.h"
#include "net/mt_tx.h"
#include "net/mqtt_bridge.h"
#include "net/wifi_mgr.h"
#include "presets_meshcore.h"
#include "presets_meshtastic.h"
#include "soc/usb_serial_jtag_struct.h"
#include "radio_scheduler.h"
#include "storage/sd_crypt.h"
#include "storage/sd_store.h"
#include "ui/app_ui.h"
#include "ui/input.h"
#include "ui/keymap.h"
#include "ui/channels_ui.h"
#include "ui/settings_ui.h"
#include "ui/status_display.h"
#include "ui/ble_ui.h"
#include "ui/crypto_ui.h"
#include "ui/colors_ui.h"
#include "ui/contact_colors.h"
#include "ui/nvs_ui.h"
#include "ui/perms_ui.h"
#include "ui/reset_ui.h"
#include "ui/profile_ui.h"
#include "ui/gps_ui.h"
#include "ui/mqtt_ui.h"
#include "ui/about_ui.h"
#include "ui/report_ui.h"
#include "diag/crash_info.h"
#include "ui/wifi_ui.h"
#include "storage/sd_fastwrite.h"
#include "storage/usb_disk.h"
#include "map/map_view.h"
#include "ui/lang.h"
#include "ui/tracks_ui.h"
#include "ui/caches_ui.h"
#include "ui/maps_ui.h"
#include "ui/downloads_ui.h"
#include "ui/ota_ui.h"
#include "net/dl.h"
#include "esp_ota_ops.h"
#include <WiFi.h>
#include "net/okapi.h"
#include "ui/logo/dualmesh_logo_tdeck.h"  // kun her (static const-data)
#include "util/wall_clock.h"

using namespace dm;

namespace pin {
constexpr int POWER_ON = 10;  // tænder periferi (radio, skærm, keyboard)
constexpr int TFT_CS = 12;    // skærm og SD deler SPI-bus med radioen:
constexpr int SD_CS = 39;     // deres CS skal holdes høj
constexpr int RADIO_CS = 9;   // også radioens, indtil dens driver tager over
constexpr int SCK = 40, MISO = 38, MOSI = 41;
}  // namespace pin

static SdStore g_store;

// MeshCore (fase 2): radio-adapter over scheduleren og selve MeshCore-klienten.
static McRadio g_mc_radio;
static ArduinoMillis g_mc_ms;
static EspRng g_mc_rng;
static VolatileRTCClock g_mc_rtc;
static StaticPoolPacketManager g_mc_pool(16);
static SimpleMeshTables g_mc_tables;
// McNode (med 350 kontakter, ca. 80 KB) ligger i PSRAM, så der er intern RAM til
// WiFi og Bluetooth. PSRAM er klar, før globale konstruktører køres.
static McNode& g_mc_node = *new (heap_caps_malloc(sizeof(McNode), MALLOC_CAP_SPIRAM))
    McNode(g_mc_radio, g_mc_ms, g_mc_rng, g_mc_rtc, g_mc_pool, g_mc_tables);
static bool g_mc_started = false;
static MsgStore g_msgs;   // beskeder til UI'et (PSRAM)
static Keyboard g_kb;
static Trackball g_tb;
static AppUi g_ui;
static KeyMap g_km;      // specialtegn pr. tast (tastaturlayouts)
static Touch g_touch;
static SettingsUi g_menu;  // fanen "Indst."
static ChannelScan g_chan_scan;  // kanaler i nærheden (fra pakkeloggen)
static dm::MtNode g_mt_node;     // Meshtastic: afkodning og nodedatabase (fase 3)
static ChannelsUi g_chan_ui;     // fanen "Kanal"
static uint32_t g_restart_at = 0;  // genstart efter Gem af radioindstillinger
static BleSerial g_ble;           // Bluetooth til MeshCore-appen
static McCompanion g_companion;   // MeshCores companion-protokol over BLE
static dm::MtBle g_mt_ble;         // Bluetooth til Meshtastic-appen
static dm::MtPhone g_mt_phone;     // Meshtastics telefon-API
static bool g_mt_app = false;      // Bluetooth bruges af Meshtastic-appen
static BleUi g_ble_ui;
static ProfileUi g_profile_ui;    // eksport/import af profilen
static ColorsUi g_colors_ui;      // kontaktfarver
static NvsUi g_nvs_ui;
static ResetUi g_reset_ui;
static PermsUi g_perms_ui;        // telemetri-tilladelser pr. kontakt        // Nulstil DualMesh (med eksport først)            // Intern NVS (se/ryd)
static CryptoUi g_crypto_ui;      // SD-kryptering / oplåsning
static WifiMgr g_wifi;
static dm::MqttBridge g_mqtt;
static dm::MqttUi g_mqtt_ui;
static dm::AboutUi g_about_ui;
static dm::ReportUi g_report_ui;  // Indst. > System > Fejlrapport
static WifiUi g_wifi_ui;
static Gps g_gps;
static GpsUi g_gps_ui;
static dm::MapView g_map;  // kortet (fase 7)
static dm::MapPoints g_map_points;  // delte lokationer på kortet
static dm::TrackLog g_track;        // spor (optagelse + vist spor)
static dm::TracksUi g_tracks_ui;    // kortets menu > Gemte spor
static dm::Caches g_caches;         // geocaches fra /dualmesh/caches/*.gpx
static dm::CachesUi g_caches_ui;    // kortets menu > Geocaches
static dm::MapsUi g_maps_ui;        // kortets menu > Kortpakker
// Hent fra GitHub (Indst.): tastaturlayouts, sprog, kortpakker og firmwareopdateringer.
static dm::DownloadsUi g_dl_kbd, g_dl_lang, g_dl_maps;
static dm::OtaUi g_ota_ui;
static int g_dl_test = 0;  // monitor G: 1 = katalog, 2 = version.txt
// Meshtastic-afsendelse (indtil videre kun SOS): loop -> kø -> radio-task.
struct MtTxFrame {
  uint8_t len;
  uint8_t data[255];
};
static QueueHandle_t g_mt_txq = nullptr;
static TaskHandle_t g_radio_task = nullptr;
static constexpr uint32_t kMtTxNotifyBit = 0x20000000u;
static volatile int g_mt_profile = -1;
static uint32_t g_mc_freq_hz = 0, g_mt_freq_hz = 0;  // til støjgulvet (Sx1262Hal::noiseFloorDbm)
// Net slået til/fra i Indst. (meshcore_enabled / meshtastic_enabled; mindst ét; genstart).
static bool g_mc_on = true, g_mt_on = true;
static volatile uint32_t g_mt_tx_done = 0;
// SOS (brugerens ønske 15. nov 2026): hold trackball 5 s + Enter. Sender "SOS" med
// position på MeshCore Public og Meshtastics primære kanal hvert sos_interval_min
// (standard 5), til den stoppes. Overlever genstart (sos_active i settings.ini).
static bool g_sos = false;
static uint32_t g_sos_next_ms = 0, g_sos_rounds = 0;
// SOS venter på GPS-fix før første runde (brugerens krav). GPS'en tændes, hvis den
// er slået fra, og slukkes igen, når SOS stoppes. Uden fix efter kSosFixWaitMs sendes
// alligevel (uden/med sidst kendte position), og positionen kommer med, når der er fix.
static bool g_sos_wait_fix = false, g_sos_gps_auto = false;
static uint32_t g_sos_start_ms = 0;
static constexpr uint32_t kSosFixWaitMs = 3 * 60 * 1000;
// Testbesked med fast interval (meshcore_test_interval_min, standard 15) til én samtale: en DM
// eller en privat kanal, aldrig Public/primær eller #hashtag (beslutning 17. nov 2026). Slås til
// fra tandhjulet i samtalen; samtalen gemmes i test_conv, til/fra i meshcore_hourly_test.
// Stopper, når der kommer en besked fra en anden i samtalen.
static bool g_hourly_test = false;
static uint32_t g_next_test_ms = 0;
static char g_test_conv[20] = "";             // samtale-id (mcc_/mcd_/mtc_/mtd_)
static volatile bool g_test_reply = false;    // besked fra en anden i testsamtalen
static constexpr const char* kTestText =
    "Test afsendelse fra DualMesh firmware, er der hul igennem?";
static char g_test_text[128] = "";        // meshcore_test_text (tom = kTestText)
static uint32_t g_test_period_ms = 15 * 60 * 1000;  // meshcore_test_interval_min
static volatile int g_mc_profile = -1;     // MeshCores profilindeks i scheduleren
static volatile uint32_t g_tx_done_tag = 0;
static volatile uint32_t g_tx_done_count = 0;
static int8_t g_region_power_dbm = 22;     // regionens effektgrænse

// ------------------------------------------------------------ profiler
// Radioindstillingerne vælges i /dualmesh/settings.ini på SD-kortet (standard: EU).
// Tabellerne over alle Meshtastic-regioner/-presets og MeshCore-presets ligger i
// lib/dualmesh (presets_meshtastic / presets_meshcore). Nøgler:
//   meshtastic_region  = EU_868            (regionskode, se 'l' i monitoren)
//   meshtastic_preset  = LONG_FAST         (tom = regionens standard)
//   meshtastic_channel =                   (navn på primær kanal; tom = standardkanal)
//   meshtastic_slot    = 0                 (0 = automatisk, ellers 1-baseret frekvensslot)
//   meshcore_preset    = EU/UK (Narrow)    (titel som i MeshCore-appen)
//   meshcore_custom    =                   (frekvens_hz,bw_hz,sf,cr – overstyrer preset)
static char g_radio_desc[2][64];

static void defaultSetting(const char* key, const char* value) {
  if (g_store.mounted() && !g_store.get(key)) g_store.set(key, value);
}

static const char* setting(const char* key, const char* def) {
  const char* v = g_store.mounted() ? g_store.get(key) : nullptr;
  return (v && *v) ? v : def;
}

static LoRaProfile meshtasticFromSettings() {
  const mt::RegionInfo* r = mt::regionByName(setting("meshtastic_region", "EU_868"));
  if (!r || !r->listed) {
    dm::DLOG.printf("[RADIO] ukendt Meshtastic-region '%s' - bruger EU_868\n",
                  setting("meshtastic_region", ""));
    r = mt::region(mt::Region::EU_868);
  }
  const mt::PresetInfo* p = mt::presetByName(setting("meshtastic_preset", ""));
  if (!p || !mt::regionAllowsPreset(r->id, p->id)) {
    if (p) dm::DLOG.printf("[RADIO] %s tillader ikke %s - bruger regionens standard\n", r->name, p->name);
    p = mt::preset(r->default_preset);
  }
  if (!mt::sx1262Supports(r->id, p->id)) {
    dm::DLOG.printf("[RADIO] %s/%s kan ikke koeres paa T-Deck'ens SX1262 - bruger EU_868\n", r->name,
                  p->name);
    r = mt::region(mt::Region::EU_868);
    p = mt::preset(r->default_preset);
  }
  if (mt::planLicensedOnly(r->plan))
    dm::DLOG.printf("[RADIO] ADVARSEL: %s kraever radioamatoerlicens\n", r->name);
  g_region_power_dbm = r->power_limit_dbm;
  const char* ch = setting("meshtastic_channel", "");
  const uint32_t slot = uint32_t(g_store.getInt("meshtastic_slot", 0));
  LoRaProfile x;
  if (!mt::makeProfile(r->id, p->id, x, ch, slot)) mt::makeProfile(r->id, p->id, x, ch, 0);
  snprintf(g_radio_desc[0], sizeof(g_radio_desc[0]), "%s %s", r->name, p->name);
  return x;
}

// Ekstra MeshCore-presets fra SD (/dualmesh/meshcore/presets.txt), så nye presets fra MeshCore
// kan bruges uden ny firmware: "titel;frekvens_hz;bw_hz;sf;cr[;path_hash_size[;forældet 0/1]]".
// Samme titel som en indbygget erstatter den; nye titler lægges til. Læses én gang ved opstart.
static void loadMcPresetsFromSd() {
  static bool done = false;
  if (done) return;
  done = true;
  FILE* f = fopen("/sd/dualmesh/meshcore/presets.txt", "r");
  if (!f) return;
  constexpr size_t kMax = 8192;
  char* buf = static_cast<char*>(malloc(kMax + 1));
  if (!buf) {
    fclose(f);
    return;
  }
  const size_t n = fread(buf, 1, kMax, f);
  fclose(f);
  buf[n] = 0;
  int bad = 0;
  const int ok = mc::loadExtraPresets(buf, &bad);
  free(buf);
  dm::DLOG.printf("[RADIO] presets.txt: %d MeshCore-presets indlaest%s, %d linjer afvist%s\n", ok,
                  n == kMax ? " (filen er over 8 KB, resten ignoreres)" : "", bad,
                  bad ? " (se formatet i README)" : "");
}

static LoRaProfile meshcoreFromSettings() {
  loadMcPresetsFromSd();
  const char* custom = setting("meshcore_custom", "");
  unsigned long f = 0, bw = 0;
  unsigned sf = 0, cr = 0;
  if (*custom && sscanf(custom, "%lu,%lu,%u,%u", &f, &bw, &sf, &cr) == 4 && f >= 150000000 &&
      f <= 960000000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8) {
    snprintf(g_radio_desc[1], sizeof(g_radio_desc[1]), "egen %.3f MHz SF%u", f / 1e6, sf);
    return mc::makeProfile(f, bw, uint8_t(sf), uint8_t(cr));
  }
  if (*custom) dm::DLOG.printf("[RADIO] meshcore_custom '%s' er ugyldig - ignoreres\n", custom);
  const mc::PresetInfo* p = mc::findPreset(setting("meshcore_preset", "EU/UK (Narrow)"));
  if (!p) {
    dm::DLOG.printf("[RADIO] ukendt MeshCore-preset '%s' - bruger EU/UK (Narrow)\n",
                  setting("meshcore_preset", ""));
    p = mc::findPreset("EU/UK (Narrow)");
  }
  if (p->deprecated) dm::DLOG.printf("[RADIO] ADVARSEL: MeshCore-preset '%s' er forældet\n", p->title);
  snprintf(g_radio_desc[1], sizeof(g_radio_desc[1]), "%s", p->title);
  return mc::makeProfile(*p);
}

static void listPresets() {
  dm::DLOG.println("[LISTE] Meshtastic-regioner (meshtastic_region), * = kraever licens, - = ikke paa SX1262:");
  for (size_t i = 0; i < mt::regionCount(); ++i) {
    const mt::RegionInfo& r = mt::regionAt(i);
    if (!r.listed) continue;
    dm::DLOG.printf("  %-11s %8.3f-%8.3f MHz  %5.1f%% %2d dBm  standard %s%s%s\n", r.name,
                  r.freq_start_mhz, r.freq_end_mhz, r.duty_cycle_pct, r.power_limit_dbm,
                  mt::presetEnumName(r.default_preset), mt::planLicensedOnly(r.plan) ? " *" : "",
                  mt::sx1262Supports(r.id, r.default_preset) ? "" : " -");
  }
  dm::DLOG.println("[LISTE] Meshtastic-presets (meshtastic_preset):");
  for (uint8_t i = 0; i < mt::kPresetCount; ++i) {
    const mt::PresetInfo* p = mt::preset(mt::Preset(i));
    if (p && !p->deprecated)
      dm::DLOG.printf("  %-14s BW %6.1f kHz SF%-2u CR4/%u\n", mt::presetEnumName(p->id),
                    p->bw_hz / 1000.0, p->sf, p->cr);
  }
  dm::DLOG.println("[LISTE] MeshCore-presets (meshcore_preset):");
  for (size_t i = 0; i < mc::presetCount(); ++i) {
    const mc::PresetInfo& p = mc::presetAt(i);
    dm::DLOG.printf("  %-26s %8.3f MHz BW %5.1f SF%-2u CR4/%u%s\n", p.title, p.freq_hz / 1e6,
                  p.bw_hz / 1000.0, p.sf, p.cr, p.deprecated ? " (foraeldet)" : "");
  }
}

static uint16_t expectedSyncReg(uint8_t sw) {
  return uint16_t(((sw & 0xF0) | 0x04) << 8 | ((sw & 0x0F) << 4 | 0x04));
}

// ------------------------------------------------------------ pakkelog
struct LogPkt {
  uint8_t net_mask, group;
  uint16_t len;
  int16_t rssi;
  int8_t snr_q4;
  uint32_t t_ms;
  uint8_t data[kMaxPacket];  // hele pakken (kanalscanneren skal bruge payloaden)
};
static QueueHandle_t g_log = nullptr;

// Tilstand som radio-tasken udfylder og skærmen viser (læses fra loop()).
struct BootInfo {
  volatile int stage = 0;  // 0 starter, 1 radio OK, 2 radio fejl, 3 kører, 4 scheduler fejl
  uint8_t status = 0;
  uint16_t errors = 0, errors_after = 0;
  uint16_t sync_mc = 0, sync_mt = 0;
  uint32_t retune_us = 0;
};
static BootInfo g_boot;

struct LastRx {
  volatile uint32_t t_ms = 0;
  volatile int16_t rssi = 0;
  volatile int8_t snr_q4 = 0;
};
static LastRx g_last[2];

static StatusDisplay g_disp;
static bool g_disp_ok = false;
static uint32_t g_splash_ms = 0;  // hvornår opstartslogoet blev vist (0 = ikke vist)
static bool g_spi_ok = false;

struct Router : IPacketSink {
  void onPacket(const RxPacket& p) override {
    if (p.net_mask & netBit(NetId::MeshCore)) g_mc_radio.pushRx(p);
    LogPkt l{};
    l.net_mask = p.net_mask;
    l.group = p.group;
    l.len = p.len;
    l.rssi = p.rssi_dbm;
    l.snr_q4 = p.snr_q4;
    l.t_ms = millis();
    memcpy(l.data, p.data, p.len < sizeof(l.data) ? p.len : sizeof(l.data));
    for (int n = 0; n < 2; ++n)
      if (p.net_mask & (1u << n)) {
        g_last[n].t_ms = l.t_ms;
        g_last[n].rssi = p.rssi_dbm;
        g_last[n].snr_q4 = p.snr_q4;
      }
    xQueueSend(g_log, &l, 0);  // drop hvis loggen er bagud – aldrig blokere radioen
  }
  void onTxDone(uint8_t profile, uint32_t tag) override {
    if (int(profile) == g_mc_profile) g_mc_radio.onTxDone();
    if (int(profile) == g_mt_profile) g_mt_tx_done = g_mt_tx_done + 1;
    g_tx_done_tag = tag;
    g_tx_done_count = g_tx_done_count + 1;  // loop() logger det (ikke print i radio-tasken)
  }
};

static Router g_router;
static Sx1262Pins g_pins;
static Sx1262Hal* g_hal = nullptr;
static RadioScheduler* g_sched = nullptr;
static SchedulerConfig g_cfg;
static volatile bool g_ready = false;

static const char* mcType(uint8_t t) {
  static const char* n[] = {"REQ", "RESPONSE", "TXT_MSG", "ACK", "ADVERT", "GRP_TXT",
                            "GRP_DATA", "ANON_REQ", "PATH", "TRACE", "MULTIPART"};
  return t < 11 ? n[t] : (t == 0x0F ? "RAW_CUSTOM" : "?");
}
static const char* mcRoute(uint8_t r) {
  static const char* n[] = {"TRANSPORT_FLOOD", "FLOOD", "DIRECT", "TRANSPORT_DIRECT"};
  return n[r & 3];
}

static void printPacket(const LogPkt& l) {
  const float snr = l.snr_q4 / 4.0f;
  const size_t n = l.len < sizeof(l.data) ? l.len : sizeof(l.data);
  if (l.net_mask == netBit(NetId::Meshtastic)) g_mt_node.onPacket(l.data, n, l.rssi, l.snr_q4, l.t_ms);
  if (l.net_mask == netBit(NetId::Meshtastic) && l.len >= 16) {
    auto u32 = [&](int o) {
      return uint32_t(l.data[o]) | uint32_t(l.data[o + 1]) << 8 |
             uint32_t(l.data[o + 2]) << 16 | uint32_t(l.data[o + 3]) << 24;
    };
    const uint8_t fl = l.data[12];
    dm::DLOG.printf("[RX MT] %7.1fs fra=!%08lx til=!%08lx id=%08lx hop=%u/%u kanal=0x%02X "
                  "len=%u rssi=%d snr=%.1f\n",
                  l.t_ms / 1000.0f, (unsigned long)u32(4), (unsigned long)u32(0),
                  (unsigned long)u32(8), fl & 7, (fl >> 5) & 7, l.data[13], l.len, l.rssi,
                  snr);
  } else if (l.net_mask == netBit(NetId::MeshCore) && l.len >= 1) {
    const uint8_t h = l.data[0];
    dm::DLOG.printf("[RX MC] %7.1fs %s %s v%u len=%u rssi=%d snr=%.1f\n", l.t_ms / 1000.0f,
                  mcRoute(h), mcType((h >> 2) & 0x0F), h >> 6, l.len, l.rssi, snr);
  } else {
    dm::DLOG.printf("[RX ??] %7.1fs net_mask=%u len=%u rssi=%d snr=%.1f\n", l.t_ms / 1000.0f,
                  l.net_mask, l.len, l.rssi, snr);
  }
}

static void printPlan() {
  if (!g_sched) return;
  uint64_t gaps[kMaxGroups];
  g_sched->estimateGaps(gaps);
  const SchedulerStats& st = g_sched->stats();
  if (st.calibrations)
    dm::DLOG.printf("[PLAN] mode=%s KALIBRERET (maalt p98): skift %.0f us\n",
                  g_cfg.mode == SchedulerConfig::Mode::LowPower ? "LowPower" : "MaxSensitivity",
                  st.retune_cal_us);
  else
    dm::DLOG.printf("[PLAN] mode=%s model: skift %u us (kalibrerer efter de foerste CAD'er)\n",
                  g_cfg.mode == SchedulerConfig::Mode::LowPower ? "LowPower" : "MaxSensitivity",
                  (unsigned)g_cfg.retune_us);
  for (size_t g = 0; g < g_sched->groupCount(); ++g) {
    const GroupPlan& p = g_sched->plan(g);
    dm::DLOG.printf("[PLAN] %-10s SF%u BW%.1f preamble %.1f ms, CAD %u sym %.1f ms%s, "
                  "tilladt hul %.1f ms, planlagt hul %.1f ms -> %s\n",
                  p.rx.name, p.rx.sf, p.rx.bw_hz / 1000.0, p.preamble_us / 1000,
                  p.cad.symbols, p.cad_us / 1000, p.cad_shortened ? " (auto)" : "",
                  p.slack_us / 1000, gaps[g] / 1000.0,
                  (p.detectable && gaps[g] <= p.slack_us) ? "GARANTERET" : "IKKE GARANTERET");
    if (p.detectable && gaps[g] > p.slack_us)
      dm::DLOG.printf("[PLAN]   %-10s hul %.0f %% over tilladt: valgt frem for CAD under %u symboler "
                    "(kun noder med kort preamble rammes; se late_tolerance)\n",
                    p.rx.name, 100.0 * (gaps[g] / p.slack_us - 1.0), (unsigned)g_cfg.min_cad_syms);
    if (st.calibrations && g == 0)
      dm::DLOG.printf("[PLAN]   (kasserede for korte CAD-maalinger: %lu)\n",
                    (unsigned long)st.cad_too_short);
    if (st.calibrations)
      dm::DLOG.printf("[PLAN]   %-10s CAD-overhead ud over N symboler: uden skift %.1f ms, efter skift %.1f ms\n",
                    p.rx.name, p.cad_over_us[0] / 1000, p.cad_over_us[1] / 1000);
  }
}

static const char* settleText(Settle st);

static float batteryVolts();
static const char* usbText(int src);
static int usbSource();
static bool usbConnected();

// Oppetid som i logfilens tidsstempel (TT:MM:SS.mmm), så hændelser kan matches.
static const char* upTime(uint32_t ms, char* out, size_t n) {
  if (!ms) {
    snprintf(out, n, "-");
    return out;
  }
  snprintf(out, n, "%02lu:%02lu:%02lu.%03lu", (unsigned long)(ms / 3600000),
           (unsigned long)(ms / 60000 % 60), (unsigned long)(ms / 1000 % 60),
           (unsigned long)(ms % 1000));
  return out;
}


static void fmtDeg(int32_t e7, char* out, size_t n);

// Feltlog (brugerens ønske 16. nov 2026: T-Deck'en tages med uden PC). Hvert minut:
// GPS (fix og position, så modtagne pakker kan sammenholdes med stedet - positionen
// bliver i SD-loggen og sendes ingen steder), hukommelse (lækager over timer) og UI
// (skærm, fane, længste tegning og LVGL-opdatering).
static void logFieldMinute(uint32_t now) {
  static uint32_t last = 0;
  if (last && now - last < 60000) return;
  last = now ? now : 1;
  if (g_gps.enabled()) {
    const dm::GpsFix& f = g_gps.fix();
    if (f.valid) {
      char la[16], lo[16];
      fmtDeg(f.lat_e7, la, sizeof(la));
      fmtDeg(f.lon_e7, lo, sizeof(lo));
      dm::DLOG.printf("[GPS] fix %u/%u sat, HDOP %u.%u, pos %s,%s, hoejde %ld m, fart %u.%u km/t\n",
                      f.sats, g_gps.satsInView(), f.hdop_x10 / 10, f.hdop_x10 % 10, la, lo,
                      long(f.alt_dm / 10), f.speed_kmh_x10 / 10, f.speed_kmh_x10 % 10);
    } else {
      dm::DLOG.printf("[GPS] intet fix (%s, %u sat set)\n", g_gps.stateText(), g_gps.satsInView());
    }
  }
  {
    uint32_t busy = 0, secs = 0, errs = 0;
    dm::sdfw::takeStats(busy, secs, errs);
    if (secs || errs)
      dm::DLOG.printf("[SD] skrevet %lu sektorer, laengste busy %lu ms (bussen fri imens), fejl %lu\n",
                      (unsigned long)secs, (unsigned long)busy, (unsigned long)errs);
  }
  {
    const dm::MtNode::Stats& s = g_mt_node.stats();
    if (g_mt_on)
      dm::DLOG.printf("[MT] os: !%08lx; i alt %lu pakker:%lu afkodet (tekst %lu, position %lu, nodeinfo %lu, "
                      "routing %lu, telemetri %lu, andet %lu), %lu ikke laest, %lu gentagelser, "
                      "%lu videresendt, %lu aflyst; %d noder\n",
                      (unsigned long)dm::mt_tx::nodeNum(), (unsigned long)s.pkts, (unsigned long)s.decoded, (unsigned long)s.text,
                      (unsigned long)s.position, (unsigned long)s.nodeinfo, (unsigned long)s.routing,
                      (unsigned long)s.telemetry, (unsigned long)s.other, (unsigned long)s.undecoded,
                      (unsigned long)s.dupes, (unsigned long)s.relayed, (unsigned long)s.relay_canceled,
                      g_mt_node.count());
  }
  if (g_hal) {
    uint32_t rc = 0, rt = 0;
    const int nc = g_mc_on ? g_hal->noiseFloorDbm(g_mc_freq_hz, &rc) : 0;
    const int nt = g_mt_on ? g_hal->noiseFloorDbm(g_mt_freq_hz, &rt) : 0;
    dm::DLOG.printf("[STOEJ] stoejgulv MeshCore %d dBm (%lu runder), Meshtastic %d dBm (%lu runder); 0 = ikke maalt\n",
                    nc, (unsigned long)rc, nt, (unsigned long)rt);
  }
  dm::DLOG.printf("[MEM] intern %u KB fri (stoerste blok %u KB, laveste %u KB), PSRAM %u KB fri\n",
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                  unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  if (g_disp_ok) {
    uint32_t draw_us = 0, lv_us = 0;
    const char* scr = "";
    g_ui.takeUiStats(draw_us, lv_us, scr);
    dm::DLOG.printf("[UI] skaerm %s, viser %s, laengste tegning %.1f ms, LVGL %.1f ms\n",
                    g_ui.screenOn() ? "taendt" : "slukket", scr, draw_us / 1000.0, lv_us / 1000.0);
  }
}

static void printStats() {
  if (!g_sched) return;
  static SchedulerStats prev{};
  static uint32_t prev_ms = 0;
  const SchedulerStats s = g_sched->stats();
  const uint32_t now = millis();
  const double win_us = (now - prev_ms) * 1000.0;
  char tbuf[16];
  dm::DLOG.printf("[STAT] %lus retunes=%lu watchdog=%lu tidlig-afbrydelse=%s irq=%lu tilstand=%s "
                "tx=%lu (lbt-optaget %lu, duty-vent %lu, sendetid %.0f ms)\n",
                (unsigned long)(now / 1000), (unsigned long)s.retunes, (unsigned long)s.watchdog,
                s.early_abort_active ? "til" : "FRA",
                (unsigned long)(g_hal ? g_hal->irqCount() : 0), settleText(g_sched->settleState()),
                (unsigned long)s.tx_ok, (unsigned long)s.tx_lbt_busy, (unsigned long)s.tx_duty_waits,
                s.tx_time_us / 1000.0);
  dm::DLOG.printf("[STROEM] %.2f V, %s\n", batteryVolts(), usbText(usbSource()));
  logFieldMinute(now);
  for (size_t g = 0; g < g_sched->groupCount(); ++g) {
    const GroupStats& a = s.g[g];
    const GroupStats& b = prev.g[g];
    const uint32_t cads = a.cads - b.cads;
    const uint32_t fa = a.rx_timeouts - b.rx_timeouts;
    const uint32_t noise = a.noise - b.noise;
    dm::DLOG.printf("[STAT]   %-10s cad=%lu hits=%lu preamb=%lu ok=%lu fejl=%lu stoej=%lu (%.2f%%) "
                  "svage=%lu timeout=%lu peak=%u maxhul=%.1fms (sene=%lu) maxhul-rx=%.1fms "
                  "cad-tid=%.0f%% rx-tid=%.0f%% | vindue: maxhul=%.1fms kl. %s\n",
                  g_sched->plan(g).rx.name, (unsigned long)cads,
                  (unsigned long)(a.detections - b.detections),
                  (unsigned long)(a.preambles - b.preambles),
                  (unsigned long)(a.rx_ok - b.rx_ok), (unsigned long)(a.rx_bad - b.rx_bad),
                  (unsigned long)noise, cads ? 100.0 * noise / cads : 0.0,
                  (unsigned long)(a.weak - b.weak), (unsigned long)fa,
                  g_sched->plan(g).cad.det_peak, a.max_gap_us / 1000.0,
                  (unsigned long)(a.late_gaps - b.late_gaps), a.max_gap_busy_us / 1000.0,
                  win_us > 0 ? 100.0 * (a.cad_time_us - b.cad_time_us) / win_us : 0.0,
                  win_us > 0 ? 100.0 * (a.rx_time_us - b.rx_time_us) / win_us : 0.0,
                  a.win_max_gap_us / 1000.0,
                  upTime(uint32_t(a.win_max_gap_at_us / 1000), tbuf, sizeof(tbuf)));
  }
  // Lange pauser i dette vindue: skrives samlet på én [TRAEG]-linje med tidspunkter,
  // så årsagen kan findes (SD-skrivning, skærm, menu-Gem ...). Grænse: et rent hul
  // over 1,5 x tilladt, eller et profilskift over 10 ms.
  {
    uint32_t sd_at = 0;
    const uint32_t sd_us = dm::DLOG.takeSlowestFlush(sd_at);
    const uint32_t sw_us = g_hal ? g_hal->diag().retune_win_max_us : 0;
    const uint32_t sw_at = g_hal ? uint32_t(g_hal->diag().retune_win_at_us / 1000) : 0;
    bool slow = sw_us > 10000;
    for (size_t g = 0; g < g_sched->groupCount(); ++g)
      if (s.g[g].win_max_gap_us > 1.5 * g_sched->plan(g).slack_us) slow = true;
    if (slow) {
      char line[200];
      int n = snprintf(line, sizeof(line), "[TRAEG]");
      for (size_t g = 0; g < g_sched->groupCount(); ++g)
        n += snprintf(line + n, sizeof(line) - n, " %s-hul %.0f ms kl. %s,", g_sched->plan(g).rx.name,
                      s.g[g].win_max_gap_us / 1000.0,
                      upTime(uint32_t(s.g[g].win_max_gap_at_us / 1000), tbuf, sizeof(tbuf)));
      n += snprintf(line + n, sizeof(line) - n, " skift %.1f ms kl. %s,", sw_us / 1000.0,
                    upTime(sw_at, tbuf, sizeof(tbuf)));
      snprintf(line + n, sizeof(line) - n, " SD-log %.1f ms kl. %s", sd_us / 1000.0,
               upTime(sd_us ? sd_at : 0, tbuf, sizeof(tbuf)));
      dm::DLOG.println(line);
    } else if (sd_us > 20000) {
      dm::DLOG.printf("[TRAEG] SD-log skrivning %.1f ms kl. %s (radioen klarede det)\n",
                      sd_us / 1000.0, upTime(sd_at, tbuf, sizeof(tbuf)));
    }
  }
  if (g_hal) {
    static Sx1262Hal::Diag pd{};
    const Sx1262Hal::Diag d = g_hal->diag();
    const uint32_t nr = d.retunes - pd.retunes;
    dm::DLOG.printf("[DIAG] cad-slut: RC=%lu XOSC=%lu andet=%lu | skift=%lu snit=%.0fus max=%luus "
                  "vindue-max=%luus | "
                  "busy: total=%.1f%% max=%luus >1ms=%lu | tcxo=%luus fejltjek=%lu xosc-fejl=%lu "
                  "bits=0x%04X\n",
                  (unsigned long)(d.cad_end_rc - pd.cad_end_rc),
                  (unsigned long)(d.cad_end_xosc - pd.cad_end_xosc),
                  (unsigned long)(d.cad_end_other - pd.cad_end_other), (unsigned long)nr,
                  nr ? double(d.retune_sum_us - pd.retune_sum_us) / nr : 0.0,
                  (unsigned long)d.retune_max_us, (unsigned long)d.retune_win_max_us,
                  win_us > 0 ? 100.0 * double(d.busy_sum_us - pd.busy_sum_us) / win_us : 0.0,
                  (unsigned long)d.busy_max_us, (unsigned long)(d.busy_waits_long - pd.busy_waits_long),
                  (unsigned long)g_pins.tcxo_delay_us, (unsigned long)(d.err_checks - pd.err_checks),
                  (unsigned long)(d.xosc_errs - pd.xosc_errs), d.err_bits);
    if (d.hdr_errs != pd.hdr_errs || d.crc_errs != pd.crc_errs) {
      char fe[48] = "";
      if (d.fe_err_valid) snprintf(fe, sizeof(fe), " frekvensfejl=%+ld Hz", (long)d.fe_err_hz);
      dm::DLOG.printf("[RXFEJL] header=%lu crc=%lu (vinduet) | seneste: %.3f MHz rssi=%d snr=%.1f%s\n",
                      (unsigned long)(d.hdr_errs - pd.hdr_errs), (unsigned long)(d.crc_errs - pd.crc_errs),
                      d.err_freq_hz / 1e6, int(d.err_rssi), d.err_snr_q4 / 4.0, fe);
    }
    static int32_t fe_logged = INT32_MIN;
    if (d.fe_ok_valid && d.fe_ok_hz != fe_logged) {  // frekvensfejl for den seneste gode pakke
      fe_logged = d.fe_ok_hz;
      dm::DLOG.printf("[FREKV] seneste modtagne pakke: frekvensfejl %+ld Hz\n", (long)d.fe_ok_hz);
    }
    pd = d;
    g_hal->resetWindow();
  }
  g_sched->resetWindow();
  prev = s;
  prev_ms = now;
}

// ------------------------------------------------------------ lært CAD-tærskel
// Scheduleren hæver/sænker CAD-tærsklen (det_peak) ud fra den målte støj. Den
// lærte værdi gemmes på SD-kortet pr. radioprofil, så næste opstart ikke
// starter med en bølge af falske alarmer. Gemmes kun ved ændring: første gang
// efter 3 min, derefter højst hvert 10. min (SD-skrivning deler bus med radioen).
static void peakKey(const GroupPlan& p, char* out, size_t n) {
  snprintf(out, n, "cad_peak_%lu_sf%u_bw%lu", (unsigned long)p.rx.freq_hz, p.rx.sf,
           (unsigned long)p.rx.bw_hz);
}

static uint8_t g_saved_peak[kMaxGroups] = {};

// Kaldes i radio-tasken lige efter sched.start(); g_store er kun læst her.
static void loadDetPeaks() {
  for (size_t g = 0; g < g_sched->groupCount(); ++g) {
    char k[48];
    peakKey(g_sched->plan(g), k, sizeof(k));
    const long v = g_store.mounted() ? g_store.getInt(k, 0) : 0;
    if (v > 0 && v < 256) {
      g_sched->setDetPeak(g, uint8_t(v));
      dm::DLOG.printf("[SD] %s: CAD-taerskel %u fra sidste koersel\n", g_sched->plan(g).rx.name,
                    g_sched->plan(g).cad.det_peak);
    }
    g_saved_peak[g] = g_sched->plan(g).cad.det_peak;
  }
}

static void saveDetPeaksIfChanged() {
  static uint32_t next_ms = 180000;  // første gang efter 3 min (tærsklen har tilpasset sig)
  if (!g_sched || !g_store.mounted() || millis() < next_ms) return;
  bool changed = false;
  for (size_t g = 0; g < g_sched->groupCount(); ++g)
    if (g_sched->plan(g).cad.det_peak != g_saved_peak[g]) changed = true;
  if (!changed) return;
  next_ms = millis() + 600000;
  for (size_t g = 0; g < g_sched->groupCount(); ++g) {
    char k[48];
    peakKey(g_sched->plan(g), k, sizeof(k));
    g_saved_peak[g] = g_sched->plan(g).cad.det_peak;
    g_store.setInt(k, g_saved_peak[g]);
  }
  const uint32_t t0 = millis();
  const bool ok = g_store.save();
  dm::DLOG.printf("[SD] CAD-taerskler gemt: %s (%lu ms)\n", ok ? "OK" : "FEJL",
                (unsigned long)(millis() - t0));
}

// ------------------------------------------------------------ SD: nulstil / formatér
// Midlertidigt via seriel monitor; flyttes til GUI'en i fase 4 (samme funktioner).
// Nulstil sletter KUN /dualmesh/ på kortet (en launchers filer m.m. røres ikke).
// Formatér sletter hele kortet og kræver, at man skriver FORMATER.
enum class Confirm : uint8_t { None, ResetDualMesh, FormatSd, PublicText, NodeName, SetSetting, AddChannel, UsbDisk };

// SD-kortet som USB-drev: gem alt, skriv loggen og genstart i lager-tilstanden (storage/usb_disk.h).
static void enterUsbDisk(const char* from) {
  dm::DLOG.printf("[SD] genstarter som USB-drev (%s); den serielle port forsvinder, indtil drevet skubbes ud\n", from);
  g_store.save();
  dm::DLOG.flush(true);
  dm::usbdisk::rebootIntoStorage(setting("language", "English"));  // skærmen dér følger sprogvalget
}
static void onMenuSaved(bool restart);
static void mqttSecret(const char* key, const char* value);
static void menuReload();
static void mtConfigure();
static Confirm g_confirm = Confirm::None;
static char g_confirm_buf[MAX_TEXT_LEN + 1];
static uint8_t g_confirm_len = 0;
static uint32_t g_confirm_ms = 0;

static void startSdLog();

// Hvorfor enheden startede (står i logfilen: viser fx nedbrud eller brownout).
static const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "strøm tilsluttet";
    case ESP_RST_UNKNOWN: return "ukendt (fx USB-porten åbnet)";
    case ESP_RST_SW: return "software-genstart";
    case ESP_RST_PANIC: return "NEDBRUD (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT (lav spænding - normalt efter sluk/tænd med afbryderen)";
    case ESP_RST_DEEPSLEEP: return "vågnet fra dyb søvn";
    case ESP_RST_EXT: return "reset-knap";
    default: {
      static char b[24];  // fx USB-seriel reset ved åbning af porten
      snprintf(b, sizeof(b), "kode %d", int(esp_reset_reason()));
      return b;
    }
  }
}

static void askConfirm(Confirm what) {
  if (what == Confirm::FormatSd && g_store.state() == SdState::NoCard) {
    dm::DLOG.println("[SD] intet kort at formatere");
    return;
  }
  g_confirm = what;
  g_confirm_len = 0;
  g_confirm_ms = millis();
  if (what == Confirm::NodeName)
    dm::DLOG.println("[MC] Skriv nyt nodenavn + Enter (hoejst 31 tegn, tom linje annullerer):");
  else if (what == Confirm::PublicText)
    dm::DLOG.println("[MC] Skriv besked til #Public + Enter (tom linje annullerer):");
  else if (what == Confirm::SetSetting)
    dm::DLOG.println("[MENU] Skriv noegle=vaerdi + Enter (fx mqtt_enabled=1; tom linje annullerer):");
  else if (what == Confirm::AddChannel)
    dm::DLOG.println("[KANAL] Skriv mc,navn,noegle eller mt,navn,psk + Enter (noegle base64; tom = "
                     "ny tilfaeldig/standard; tom linje annullerer):");
  else if (what == Confirm::UsbDisk)
    dm::DLOG.println("[SD] SD-kort som USB-drev: T-Deck'en genstarter uden radio, og kortet vises som et drev "
                     "paa PC'en. Skriv LAGER + Enter for at fortsaette.");
  else if (what == Confirm::ResetDualMesh)
    dm::DLOG.println("[SD] Nulstil DualMesh: sletter KUN mappen /dualmesh/ paa SD-kortet "
                   "(undtagen eksporterede profiler i /dualmesh/eksport/). Skriv JA + Enter for at fortsaette.");
  else
    dm::DLOG.println("[SD] Formater SD-kort: SLETTER ALT paa kortet, ogsaa filer fra andre "
                   "firmwares/launcher. Skriv FORMATER + Enter for at fortsaette.");
}

static bool handleConfirm(int c) {
  if (g_confirm == Confirm::None) return false;
  if (millis() - g_confirm_ms > (g_confirm == Confirm::PublicText ? 120000u : 30000u)) {
    g_confirm = Confirm::None;
    return false;
  }
  if (c != '\r' && c != '\n') {
    if (g_confirm_len < sizeof(g_confirm_buf) - 1) g_confirm_buf[g_confirm_len++] = char(c);
    return true;
  }
  if (g_confirm_len == 0) {  // \r\n: vent på resten; en tom besked annullerer
    if ((g_confirm == Confirm::PublicText || g_confirm == Confirm::NodeName ||
         g_confirm == Confirm::SetSetting || g_confirm == Confirm::AddChannel) && c == '\r') {
      g_confirm = Confirm::None;
      dm::DLOG.println("[MC] annulleret");
    }
    return true;
  }
  g_confirm_buf[g_confirm_len] = 0;
  const Confirm what = g_confirm;
  g_confirm = Confirm::None;
  if (what == Confirm::NodeName) {
    g_confirm_buf[31] = 0;
    g_store.set("meshcore_name", g_confirm_buf);
    const bool saved = g_store.save();
    g_mc_node.setName(g_confirm_buf);
    // Advert, så andre lærer det nye navn (uden position).
    const bool adv = g_mc_radio.txEnabled() && g_mc_node.sendAdvert();
    dm::DLOG.printf("[MC] nodenavn nu \"%s\" (%s), advert %s\n", g_mc_node.nodeName(),
                    saved ? "gemt paa SD" : "IKKE gemt", adv ? "sat i koe" : "ikke sendt");
    return true;
  }
  if (what == Confirm::AddChannel) {
    // Udvikler-genvej (test): tilføj en kanal som "+ Tilføj" i Kanal-fanen. Nøglen logges ikke.
    char* net = g_confirm_buf;
    char* name = strchr(net, ',');
    if (!name) {
      dm::DLOG.println("[KANAL] forventede mc,navn,noegle eller mt,navn,psk");
      return true;
    }
    *name++ = 0;
    char* key = strchr(name, ',');
    if (key) *key++ = 0;
    const char* note = nullptr;
    int idx = -1;
    if (!strcmp(net, "mc") && g_mc_started) idx = g_mc_node.addUserChannel(name, key ? key : "", &note);
    else if (!strcmp(net, "mt")) idx = g_chan_scan.addMtChannel(name, key ? key : "", &note);
    else note = "net skal vaere mc eller mt (og MeshCore skal koere)";
    dm::DLOG.printf("[KANAL] %s-kanal \"%s\": %s (indeks %d)\n", net, name, note ? note : "?", idx);
    return true;
  }
  if (what == Confirm::SetSetting) {
    // Udvikler-genvej: sæt en indstilling som i menuen (gælder straks; radio kræver genstart).
    char* eq = strchr(g_confirm_buf, '=');
    if (!eq || eq == g_confirm_buf) {
      dm::DLOG.println("[MENU] forventede noegle=vaerdi");
      return true;
    }
    *eq = 0;
    if (!strcmp(g_confirm_buf, "mqtt_pass") || !strcmp(g_confirm_buf, "ble_pin")) {
      mqttSecret(g_confirm_buf, eq + 1);  // aldrig i settings.ini eller loggen
    } else {
      g_store.set(g_confirm_buf, eq + 1);
      dm::DLOG.printf("[MENU] %s = '%s' (monitor, %s)\n", g_confirm_buf, eq + 1,
                      g_store.save() ? "gemt" : "IKKE gemt");
    }
    menuReload();
    onMenuSaved(false);
    return true;
  }
  if (what == Confirm::PublicText) {
    const bool ok = g_mc_node.sendPublic(g_confirm_buf);
    dm::DLOG.printf("[MC] #Public: \"%s\" %s\n", g_confirm_buf, ok ? "sat i koe" : "FEJL");
    return true;
  }
  const char* need = what == Confirm::ResetDualMesh ? "JA" : what == Confirm::UsbDisk ? "LAGER" : "FORMATER";
  dm::DLOG.flush(true);
  if (strcmp(g_confirm_buf, need) != 0) {
    dm::DLOG.println("[SD] annulleret");
    return true;
  }
  if (what == Confirm::UsbDisk) enterUsbDisk("monitor");
  const uint32_t t0 = millis();
  if (what == Confirm::ResetDualMesh) {
    const bool ok = g_store.resetDualMesh();
    for (auto& p : g_saved_peak) p = 0;  // så de lærte værdier gemmes igen senere
    dm::DLOG.printf("[SD] DualMesh-data nulstillet: %s (%lu ms)\n", ok ? "OK" : "FEJL",
                  (unsigned long)(millis() - t0));
  } else {
    dm::DLOG.println("[SD] formaterer... (radioen kan miste pakker imens)");
    const bool ok = g_store.formatCard();
    for (auto& p : g_saved_peak) p = 0;
    dm::DLOG.printf("[SD] formatering: %s (%lu ms) - SD: %s\n", ok ? "OK" : "FEJL",
                  (unsigned long)(millis() - t0), g_store.status());
  }
  startSdLog();  // mappen er slettet: start en ny logfil
  return true;
}

static void printMeshCore() {
  if (!g_mc_started) {
    dm::DLOG.println("[MC] node ikke startet");
    return;
  }
  const McNodeStats& ms = g_mc_node.nodeStats();
  dm::DLOG.printf("[MC] node \"%s\" identitet %s, sender %s, kontakter %d, adverts %lu (nye %lu), "
                "kanalbeskeder %lu, DM %lu, tabt i koe %lu\n",
                g_mc_node.nodeName(), g_mc_node.identityNote(),
                g_mc_radio.txEnabled() ? "TIL" : "FRA", g_mc_node.contactCount(),
                (unsigned long)ms.adverts, (unsigned long)ms.new_contacts,
                (unsigned long)ms.channel_msgs, (unsigned long)ms.direct_msgs,
                (unsigned long)g_mc_radio.rxDropped());
}

static const char* settleText(Settle st) {
  switch (st) {
    case Settle::Calibrating: return "kalibrerer";
    case Settle::AdaptingCad: return "tilpasser CAD";
    default: return "stabil";
  }
}

// ------------------------------------------------------------ radio-task
static void radioTask(void*) {
  static Sx1262Hal hal(g_pins);
  g_hal = &hal;
  *hal.taskHandle() = xTaskGetCurrentTaskHandle();
  g_radio_task = xTaskGetCurrentTaskHandle();
  if (!hal.begin()) {
    dm::DLOG.println("[FEJL] SX1262 kunne ikke startes (SPI-device)");
    g_boot.stage = 2;
    vTaskDelete(nullptr);
  }
  dm::DLOG.printf("[HW] status=0x%02X fejl=0x%04X sync-register=0x%04X (forventet 0x1424 efter reset)\n",
                hal.chipStatus(), hal.deviceErrors(), hal.syncWordRegister());
  g_boot.status = hal.chipStatus();
  g_boot.errors = hal.deviceErrors();
  if (hal.deviceErrors() & 0x0020)
    dm::DLOG.println("[INFO] XOSC_START_ERR ved opstart (normalt foer TCXO er sat op) - ryddes nu");
  hal.clearDeviceErrors();

  LoRaProfile mt = meshtasticFromSettings();
  LoRaProfile mc = meshcoreFromSettings();
  // Sendeeffekt pr. net (brugerens ønske 17. nov 2026): indstillingen, men aldrig over
  // regionens grænse (fx EU 869,4-869,65: 27 dBm, EU_433: 10) eller chippens 22 dBm.
  auto txPower = [](const char* key) {
    const int lim = g_region_power_dbm < 22 ? g_region_power_dbm : 22;
    int v = g_store.getInt(key, lim);
    if (v > lim) v = lim;
    if (v < 1) v = 1;
    return int8_t(v);
  };
  mt.tx_power_dbm = txPower("meshtastic_tx_power");
  mc.tx_power_dbm = txPower("meshcore_tx_power");
  dm::DLOG.printf("[RADIO] Meshtastic: %s  %.4f MHz BW %.1f kHz SF%u CR4/%u preamble %u\n",
                g_radio_desc[0], mt.freq_hz / 1e6, mt.bw_hz / 1000.0, mt.sf, mt.cr, mt.preamble_syms);
  dm::DLOG.printf("[RADIO] MeshCore:   %s  %.4f MHz BW %.1f kHz SF%u CR4/%u preamble %u\n",
                g_radio_desc[1], mc.freq_hz / 1e6, mc.bw_hz / 1000.0, mc.sf, mc.cr, mc.preamble_syms);
  if (g_mc_on && g_mt_on && sameCadChannel(mt, mc))
    dm::DLOG.println("[RADIO] ADVARSEL: begge net paa samme kanal og modulation - CAD kan ikke skelne "
                   "dem, og op mod halvdelen af pakkerne tabes. Vaelg et andet preset.");
  hal.applyProfile(mc);
  g_boot.sync_mc = hal.syncWordRegister();
  dm::DLOG.printf("[HW] sync efter MeshCore-profil: 0x%04X (forventet 0x%04X)\n", hal.syncWordRegister(),
                expectedSyncReg(mc.sync_word));
  hal.applyProfile(mt);
  g_boot.sync_mt = hal.syncWordRegister();
  dm::DLOG.printf("[HW] sync efter Meshtastic-profil: 0x%04X (forventet 0x%04X)\n",
                hal.syncWordRegister(), expectedSyncReg(mt.sync_word));
  const uint32_t rt = hal.measureRetuneUs(mt, mc, 200);
  dm::DLOG.printf("[HW] profilskift: %lu us i gennemsnit (200 skift)\n", (unsigned long)rt);
  g_boot.errors_after = hal.deviceErrors();
  dm::DLOG.printf("[HW] fejl efter test=0x%04X status=0x%02X\n", g_boot.errors_after, hal.chipStatus());

  g_boot.retune_us = rt;
  g_boot.stage = 1;
  g_cfg.retune_us = rt + 50;
  // Tilpas CAD-taerskel hurtigere: Meshtastic laver kun ~15 CAD/s, saa et vindue
  // paa 1000 CAD'er ville tage over et minut pr. trin.
  g_cfg.adapt_window = 300;
  // En CAD slutter altid i STDBY_RC (TCXO slukket), så hver CAD uden forudgående
  // profilskift betaler TCXO-opstarten. Planlæg efter det.
  if (g_pins.tcxo) g_cfg.cad_fixed_us += g_pins.tcxo_delay_us;
  g_mc_radio.begin(mc, xTaskGetCurrentTaskHandle(), McRadio::kTxNotifyBit);
  g_mc_radio.setTxEnabled(g_store.getInt("meshcore_tx", 1) != 0);
  dm::DLOG.printf("[RADIO] sendeeffekt MeshCore %d dBm, Meshtastic %d dBm (regionens graense %d dBm), "
                  "MeshCore-afsendelse %s\n",
                  mc.tx_power_dbm, mt.tx_power_dbm, g_region_power_dbm, g_mc_radio.txEnabled() ? "TIL" : "FRA");
  static RadioScheduler sched(hal, g_router, g_cfg);
  g_sched = &sched;
  hal.attach(&sched);
  if (g_mt_on) g_mt_profile = sched.addProfile(mt);
  if (g_mc_on) g_mc_profile = sched.addProfile(mc);
  g_mc_freq_hz = mc.freq_hz;
  g_mt_freq_hz = mt.freq_hz;
  dm::DLOG.printf("[RADIO] net: MeshCore %s, Meshtastic %s\n", g_mc_on ? "TIL" : "FRA", g_mt_on ? "TIL" : "FRA");
  if (!sched.start()) {
    dm::DLOG.println("[FEJL] scheduler start fejlede");
    g_boot.stage = 4;
  } else {
    g_boot.stage = 3;
  }
  loadDetPeaks();
  g_ready = true;

  for (;;) {
    uint32_t bits = 0;
    // Kun ét net: radioen lytter konstant, så vågn hvert 250. ms og tag en støjprøve.
    const bool one_net = g_mc_on != g_mt_on;
    xTaskNotifyWait(0, 0xFFFFFFFFu, &bits, one_net ? pdMS_TO_TICKS(250) : portMAX_DELAY);
    if (one_net) {
      static uint32_t last_ms = 0;
      if (millis() - last_ms >= 250) {
        last_ms = millis();
        hal.sampleNoiseIdle();
      }
    }
    if (bits & 0x80000000u) hal.serviceTimer();
    if (bits & 0x1u) hal.serviceIrq();
    if (bits & McRadio::kTxNotifyBit) {  // MeshCore vil sende: giv pakken til scheduleren
      static uint32_t tag = 0;
      McRadioFrame f;
      while (g_mc_radio.popTx(f)) {
        if (g_mc_profile < 0 || !sched.enqueueTx(uint8_t(g_mc_profile), f.data, f.len, ++tag)) {
          dm::DLOG.println("[MC] TX-koe fuld - pakken droppes");
          g_mc_radio.onTxDone();  // lad MeshCore komme videre
        }
      }
    }
    if (bits & kMtTxNotifyBit) {  // Meshtastic-pakke fra loop() (SOS)
      static uint32_t mt_tag = 0x80000000u;
      MtTxFrame f;
      while (g_mt_txq && xQueueReceive(g_mt_txq, &f, 0) == pdTRUE)
        if (g_mt_profile < 0 || !sched.enqueueTx(uint8_t(g_mt_profile), f.data, f.len, ++mt_tag))
          g_mt_tx_done = g_mt_tx_done + 1;  // tabt; loggen viser det via koe-tælleren
    }
    g_mc_radio.setBusy(sched.busy());
  }
}

// ------------------------------------------------------------ strøm
// Batterispænding: GPIO4 bag en 1:2-spændingsdeler (LilyGo BOARD_BAT_ADC).
// Snit over 8 målinger; med USB tilsluttet viser den ladespændingen (~4,2 V).
static float batteryVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; ++i) mv += analogReadMilliVolts(4);
  return mv / 8 * 2 / 1000.0f;
}
// Strømkilde: 0 = batteri, 1 = USB til en PC, 2 = USB-oplader.
// PC: USB-controlleren modtager en SOF-ramme hvert ms fra værten, også når intet
// terminalprogram har porten åben (bool(Serial) er kun sand med en åben terminal og
// gav "batteri" ved 4,48 V, så skærmen slukkede på USB trods indstillingen).
// Oplader uden data: ingen SOF; genkendes på spændingen (> 4,35 V kommer kun fra
// ladekredsen, et fuldt batteri giver ~4,2 V). Er batteriet fuldt og ladningen
// stoppet, ses en ren oplader som batteri.
static const char* usbText(int src) {
  return src == 1 ? "USB" : (src == 2 ? "lader" : "batteri");
}
// Lige efter opladning er batteriet over 4,35 V et stykke tid: derfor kræves to
// målinger i træk (5 s imellem), og de første 60 s efter at en PC er koblet fra,
// regnes der ikke med oplader (det gav en falsk "lader" ved frakobling).
static int usbSource() {
  static uint32_t last_check = 0, last_frame = 0, last_v = 0, host_lost_ms = 0;
  static bool host = false, charger = false, high_before = false;
  const uint32_t now = millis();
  if (now - last_check >= 50) {  // tælleren (11 bit, 1 kHz) skal have flyttet sig
    const uint32_t f = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    const bool h = f != last_frame;
    if (host && !h) host_lost_ms = now ? now : 1;
    host = h;
    last_frame = f;
    last_check = now;
  }
  if (!last_v || now - last_v >= 5000) {
    const bool high = batteryVolts() > 4.35f;
    const bool after_pc = host_lost_ms && now - host_lost_ms < 60000;
    charger = high && high_before && !after_pc;
    high_before = high;
    last_v = now;
  }
  if (host) charger = false;
  const int src = host ? 1 : (charger ? 2 : 0);
  static int logged = -1;
  if (src != logged) {  // skift logges (vises også på statusskærmen)
    if (logged >= 0) dm::DLOG.printf("[STROEM] strømkilde nu: %s\n", usbText(src));
    logged = src;
  }
  return src;
}
static bool usbConnected() { return usbSource() != 0; }

// ------------------------------------------------------------ WiFi, GPS, SOS
// Grader x 1e7 -> "55.67612" (5 decimaler, ca. 1 m).
static void fmtDeg(int32_t e7, char* out, size_t n) {
  const long a = e7 < 0 ? -long(e7) : long(e7);
  snprintf(out, n, "%s%ld.%05ld", e7 < 0 ? "-" : "", a / 10000000, (a % 10000000) / 100);
}

// Egen position til en advert: kun hvis delingen er slået til (eller SOS er aktiv).
// "omtrent" = afrundet til 0,01 grad (ca. 1 km).
static bool positionFor(const char* share_key, double& lat, double& lon) {
  int32_t la, lo, alt;
  uint32_t age;
  if (!g_gps.lastPosition(la, lo, alt, age)) return false;
  const char* mode = g_sos ? "præcis" : setting(share_key, "fra");
  if (strcmp(mode, "præcis") == 0) {
    lat = la / 1e7;
    lon = lo / 1e7;
    return true;
  }
  if (strcmp(mode, "omtrent") == 0) {
    lat = double(int32_t(la / 100000)) / 100.0;
    lon = double(int32_t(lo / 100000)) / 100.0;
    return true;
  }
  return false;
}
static bool mcAdvertPosition(double& lat, double& lon) {
  return positionFor("meshcore_share_position", lat, lon);
}

// --- Automatiske MeshCore-adverts (brugerens ønske 15. nov 2026) ---
// Lokal (nul hop: kun direkte naboer, ingen videresendelse) hvert
// meshcore_advert_local_min (0 = fra), og flood (hele nettet) hvert
// meshcore_advert_flood_h (0 = fra, ellers mindst 3 t). Første lokale 1 min efter
// opstart, første flood efter 10 min (så hyppige genstarter ikke fylder nettet).
// Position kun efter "Del position" (eller SOS).
static uint32_t g_next_adv_local = 0, g_next_adv_flood = 0;
static void advertsReschedule() {
  const uint32_t now = millis();
  const long lm = g_store.getInt("meshcore_advert_local_min", 0);
  const long fh = g_store.getInt("meshcore_advert_flood_h", 12);
  g_next_adv_local = lm > 0 ? now + (now < 60000 ? 60000 : uint32_t(lm) * 60000u) : 0;
  g_next_adv_flood = fh >= 3 ? now + (now < 600000 ? 600000 : uint32_t(fh) * 3600000u) : 0;
}
static void advertsTick() {
  if (!g_mc_started || !g_ready || !g_mc_radio.txEnabled()) return;
  const uint32_t now = millis();
  if (g_next_adv_local && int32_t(now - g_next_adv_local) >= 0) {
    g_next_adv_local = now + uint32_t(g_store.getInt("meshcore_advert_local_min", 0)) * 60000u;
    dm::DLOG.println("[MC] automatisk lokal advert");
    g_mc_node.sendAdvertMode(false);
  }
  if (g_next_adv_flood && int32_t(now - g_next_adv_flood) >= 0) {
    g_next_adv_flood = now + uint32_t(g_store.getInt("meshcore_advert_flood_h", 12)) * 3600000u;
    dm::DLOG.println("[MC] automatisk flood-advert");
    g_mc_node.sendAdvertMode(true);
  }
}

// --- Telemetri til andre noder (brugerens ønske 16. nov 2026; som MeshCore-appen) ---
// Tilstand 0 = nej, 1 = kun kontakter med tilladelse (flag-bit), 2 = alle/ja.
static uint8_t telemMode(const char* key) {
  const char* v = setting(key, "nej");
  if (!strcmp(v, "alle") || !strcmp(v, "ja")) return 2;
  if (!strcmp(v, "kontakter")) return 1;
  return 0;
}
static uint8_t telemModesByte() {
  return uint8_t(telemMode("meshcore_telemetry") | (telemMode("meshcore_telemetry_pos") << 2) |
                 (telemMode("meshcore_telemetry_env") << 4));
}
static void setTelemModesByte(uint8_t m) {
  static const char* req[] = {"nej", "kontakter", "alle"};
  static const char* dat[] = {"nej", "kontakter", "ja"};
  const uint8_t b = m & 3, l = (m >> 2) & 3, e = (m >> 4) & 3;
  g_store.set("meshcore_telemetry", req[b > 2 ? 0 : b]);
  g_store.set("meshcore_telemetry_pos", dat[l > 2 ? 0 : l]);
  g_store.set("meshcore_telemetry_env", dat[e > 2 ? 0 : e]);
  g_store.save();
  dm::DLOG.printf("[BLE] appen satte telemetri: svar %s, position %s, miljoe %s%c", req[b > 2 ? 0 : b],
                  dat[l > 2 ? 0 : l], dat[e > 2 ? 0 : e], 10);
}
// Cayenne LPP (kanal 1 = os selv): batteri og chiptemperatur; position ved tilladelse og
// GPS-fix, med præcision som "Del position" (omtrent = afrundet til ca. 1 km).
static uint8_t mcTelemetry(const ContactInfo& c, uint8_t inv_mask, uint8_t* out, uint8_t max) {
  const uint8_t cp = c.flags >> 1;  // bit 0 = favorit
  uint8_t perm = 0;
  const uint8_t mb = telemMode("meshcore_telemetry"), ml = telemMode("meshcore_telemetry_pos"),
                me = telemMode("meshcore_telemetry_env");
  if (mb == 2 || (mb == 1 && (cp & 1))) perm |= 1;
  if (ml == 2 || (ml == 1 && (cp & 2))) perm |= 2;
  if (me == 2 || (me == 1 && (cp & 4))) perm |= 4;
  perm &= uint8_t(~inv_mask);
  if (!(perm & 1) || max < 20) return 0;
  uint8_t i = 0;
  const uint16_t v100 = uint16_t(batteryVolts() * 100.0f);
  out[i++] = 1;
  out[i++] = 116;  // spænding, 0,01 V
  out[i++] = uint8_t(v100 >> 8);
  out[i++] = uint8_t(v100);
  const int16_t t10 = int16_t(temperatureRead() * 10.0f);
  out[i++] = 1;
  out[i++] = 103;  // temperatur, 0,1 °C
  out[i++] = uint8_t(uint16_t(t10) >> 8);
  out[i++] = uint8_t(t10);
  int32_t la, lo, al;
  uint32_t age;
  if ((perm & 2) && g_gps.state() == Gps::State::Fix && g_gps.lastPosition(la, lo, al, age)) {
    const bool rough = !strcmp(setting("meshcore_share_position", "fra"), "omtrent");
    int32_t lat4 = la / 1000, lon4 = lo / 1000;  // 0,0001 grad
    if (rough) {
      lat4 = lat4 / 100 * 100;  // 0,01 grad ≈ 1 km
      lon4 = lon4 / 100 * 100;
    }
    const int32_t alt = rough ? 0 : al * 100;
    out[i++] = 1;
    out[i++] = 136;  // GPS
    for (int32_t v : {lat4, lon4, alt}) {
      out[i++] = uint8_t(v >> 16);
      out[i++] = uint8_t(v >> 8);
      out[i++] = uint8_t(v);
    }
  }
  return i;
}

// --- Hooks til MeshCore-appen (BLE) ---
static uint16_t bleBattMv() { return uint16_t(batteryVolts() * 1000.0f); }
static void bleSetName(const char* name) {
  g_store.set("meshcore_name", name);
  g_store.save();
  dm::DLOG.printf("[BLE] navn sat af appen: %s\n", name);
}
static void bleSetShare(bool on) {
  g_store.set("meshcore_share_position", on ? "præcis" : "fra");
  g_store.save();
  dm::DLOG.printf("[BLE] appen satte Del position (MeshCore): %s\n", on ? "præcis" : "fra");
}
static bool bleShareOn() { return strcmp(setting("meshcore_share_position", "fra"), "fra") != 0; }
static void bleRadio(uint32_t& freq_khz, uint32_t& bw_hz, uint8_t& sf, uint8_t& cr, int8_t& dbm) {
  const LoRaProfile p = meshcoreFromSettings();
  freq_khz = p.freq_hz / 1000;
  bw_hz = p.bw_hz;
  sf = p.sf;
  cr = p.cr;
  dbm = g_hal ? g_hal->txPower() : 0;
}
// Appen vælger radio. Samme som nu: intet. Ellers: et preset med de værdier, eller
// "egne radioværdier", gemmes, og T-Deck'en genstarter (scheduleren læser radioen ved start).
static int bleSetRadio(uint32_t freq_khz, uint32_t bw_hz, uint8_t sf, uint8_t cr) {
  if (freq_khz < 150000 || freq_khz > 960000 || sf < 5 || sf > 12 || cr < 5 || cr > 8 || bw_hz < 7000 ||
      bw_hz > 500000)
    return -1;
  auto near = [](uint32_t a, uint32_t b, uint32_t tol) { return (a > b ? a - b : b - a) <= tol; };
  const LoRaProfile cur = meshcoreFromSettings();
  if (near(cur.freq_hz / 1000, freq_khz, 1) && near(cur.bw_hz, bw_hz, 100) && cur.sf == sf && cur.cr == cr) {
    dm::DLOG.println("[BLE] appens radio er den samme som T-Deck'ens - OK");
    return 0;
  }
  const mc::PresetInfo* match = nullptr;
  for (size_t i = 0; i < mc::presetCount() && !match; ++i) {
    const mc::PresetInfo& p = mc::presetAt(i);
    if (near(p.freq_hz / 1000, freq_khz, 1) && near(p.bw_hz, bw_hz, 100) && p.sf == sf && p.cr == cr) match = &p;
  }
  if (match) {
    g_store.set("meshcore_preset", match->title);
    g_store.set("meshcore_custom", "");
  } else {
    char v[48];
    snprintf(v, sizeof(v), "%lu,%lu,%u,%u", (unsigned long)freq_khz * 1000ul, (unsigned long)bw_hz,
             unsigned(sf), unsigned(cr));
    g_store.set("meshcore_custom", v);
  }
  g_store.save();
  dm::DLOG.printf("[BLE] ny MeshCore-radio fra appen gemt (%s) - genstarter om 3 s\n",
                  match ? match->title : "egne radioværdier");
  g_restart_at = millis() + 3000;
  return 1;
}

// MeshCore-appens CLI-kommandoer for WiFi (samme tekster som MeshCores companion med WiFi).
// Appen må læse koden: T-Deck'en skal aktivt parres med telefonen (PIN) først
// (brugerens beslutning 16. nov 2026). Vi kan huske flere net: "wifi.ssid" er det
// forbundne net, ellers det første gemte; "set wifi.pwd" forbinder og husker straks.
static char g_cli_ssid[33] = "";  // fra "set wifi.ssid", indtil koden kommer

static int cliWifiIdx() {
  if (g_wifi.state() == WifiMgr::State::Connected) {
    const int i = g_wifi.findKnown(g_wifi.ssid());
    if (i >= 0) return i;
  }
  return g_wifi.knownCount() > 0 ? 0 : -1;
}

static bool bleCli(const char* cmd, char* reply, size_t n) {
  if (!strncmp(cmd, "set wifi.ssid ", 14)) {
    snprintf(g_cli_ssid, sizeof(g_cli_ssid), "%s", cmd + 14);
    snprintf(reply, n, "> wifi.ssid is now %s (set wifi.pwd too)", g_cli_ssid);
    dm::DLOG.printf("[BLE] appen satte WiFi-net \"%s\"\n", g_cli_ssid);
    return true;
  }
  if (!strncmp(cmd, "set wifi.pwd ", 13)) {
    char ssid[33];
    const int i = cliWifiIdx();
    snprintf(ssid, sizeof(ssid), "%s", g_cli_ssid[0] ? g_cli_ssid : (i >= 0 ? g_wifi.known(i).ssid : ""));
    if (!ssid[0]) {
      snprintf(reply, n, "> set wifi.ssid first");
      return true;
    }
    const int k = g_wifi.findKnown(ssid);
    const bool hidden = k >= 0 && g_wifi.known(k).hidden;
    if (!g_wifi.enabled()) g_wifi.setEnabled(true, true);
    g_wifi.connect(ssid, cmd + 13, hidden, true);  // husker nettet krypteret på SD
    g_cli_ssid[0] = 0;
    snprintf(reply, n, "> wifi.pwd updated");
    dm::DLOG.printf("[BLE] appen satte koden til \"%s\" - forbinder\n", ssid);
    return true;
  }
  if (!strcmp(cmd, "get wifi.pwd")) {
    const int i = cliWifiIdx();
    snprintf(reply, n, "> %s", i >= 0 ? g_wifi.known(i).pass : "");
    dm::DLOG.println("[BLE] appen laeste WiFi-koden");
    return true;
  }
  if (!strcmp(cmd, "get wifi.ssid")) {
    const int i = cliWifiIdx();
    snprintf(reply, n, "> %s", i >= 0 ? g_wifi.known(i).ssid : "(not set)");
    return true;
  }
  if (!strcmp(cmd, "set wifi.clear")) {
    // Som MeshCore: glem WiFi-opsætningen (alle gemte net hos os).
    while (g_wifi.knownCount() > 0) {
      char s[33];
      snprintf(s, sizeof(s), "%s", g_wifi.known(0).ssid);
      if (!g_wifi.forget(s)) break;
    }
    g_cli_ssid[0] = 0;
    snprintf(reply, n, "> wifi config cleared");
    dm::DLOG.println("[BLE] appen glemte alle WiFi-net");
    return true;
  }
  if (!strncmp(cmd, "set wifi.enabled ", 17)) {
    const bool on = atoi(cmd + 17) != 0;
    g_wifi.setEnabled(on, true);
    snprintf(reply, n, "> wifi.enabled is now %d", on ? 1 : 0);
    return true;
  }
  if (!strcmp(cmd, "get wifi.enabled")) {
    snprintf(reply, n, "> %d", g_wifi.enabled() ? 1 : 0);
    return true;
  }
  if (!strcmp(cmd, "get wifi.status")) {
    snprintf(reply, n, "%s", g_wifi.state() == WifiMgr::State::Connected ? "> connected" : "> disconnected");
    return true;
  }
  if (!strcmp(cmd, "get wifi.ip")) {
    if (g_wifi.state() == WifiMgr::State::Connected) {
      char ip[24];
      g_wifi.ipText(ip, sizeof(ip));
      snprintf(reply, n, "> %s", ip);
    } else {
      snprintf(reply, n, "> (not connected)");
    }
    return true;
  }
  if (!strcmp(cmd, "get name")) {
    snprintf(reply, n, "> %s", g_mc_node.nodeName());
    return true;
  }
  if (!strcmp(cmd, "board")) {
    snprintf(reply, n, "LilyGo T-Deck Plus (DualMesh)");
    return true;
  }
  return false;
}

// GPS fra appens Position Settings (gemmes som gps_enabled, som i T-Deck'ens menu).
static int bleGpsGet() { return g_gps.enabled() ? 1 : 0; }
static bool bleGpsPos(double& lat, double& lon, double& alt) {
  int32_t la, lo, al;
  uint32_t age;
  if (g_gps.state() != Gps::State::Fix || !g_gps.lastPosition(la, lo, al, age)) return false;
  lat = la / 1e7;
  lon = lo / 1e7;
  alt = al;
  return true;
}
static void bleGpsSet(bool on) {
  g_gps.setEnabled(on);
  g_store.setInt("gps_enabled", on ? 1 : 0);
  g_store.save();
  dm::DLOG.printf("[BLE] appen satte GPS %s%c", on ? "til" : "fra", 10);
}

static void bleSavePin(const char* v);
static uint32_t blePin();
static void bleSetPin(uint32_t pin) {  // fra MeshCore-appen
  char v[12];
  snprintf(v, sizeof(v), "%06lu", (unsigned long)pin);
  bleSavePin(pin ? v : "-");
}

static void restartAfterImport(uint32_t delay_ms);
static bool resetFromMenu();
// MeshCore-appens statistik: vores schedulers tal for MeshCore-gruppen.
static void bleRadioStats(uint32_t& recv, uint32_t& sent, uint32_t& errors, int16_t& noise) {
  recv = sent = errors = 0;
  noise = g_hal ? g_hal->noiseFloorDbm(g_mc_freq_hz) : 0;  // appens "Noise Floor"
  if (!g_sched || g_mc_profile < 0) return;
  const int g = g_sched->groupOfProfile(uint8_t(g_mc_profile));
  if (g < 0) return;
  const GroupStats& gs = g_sched->stats().g[g];
  recv = gs.rx_ok;
  errors = gs.rx_bad;
  sent = g_mc_node.getNumSentFlood() + g_mc_node.getNumSentDirect();
}
// Fabriksnulstilling fra appen = "Nulstil DualMesh" (kun /dualmesh/ på SD) + genstart.
static bool bleFactoryReset() {
  if (!resetFromMenu()) return false;
  restartAfterImport(1000);
  return true;
}
static void startBle() {
  McCompanion::Hooks h;
  h.batt_mv = bleBattMv;
  h.set_name = bleSetName;
  h.set_share = bleSetShare;
  h.share_on = bleShareOn;
  h.position = mcAdvertPosition;
  h.radio = bleRadio;
  h.set_pin = bleSetPin;
  h.set_radio = bleSetRadio;
  h.restart = restartAfterImport;
  h.run_cli = bleCli;
  h.gps_get = bleGpsGet;
  h.gps_set = bleGpsSet;
  h.gps_pos = bleGpsPos;
  h.radio_stats = bleRadioStats;
  h.factory_reset = bleFactoryReset;
  h.telem_modes = telemModesByte;
  h.set_telem_modes = setTelemModesByte;
  g_companion.begin(&g_mc_node, &g_ble, h);
  const uint32_t pin = blePin();
  char name[48];
  snprintf(name, sizeof(name), "MeshCore-%s", g_mc_node.nodeName());
  const uint32_t heap0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  g_ble.begin(name, pin);
  dm::DLOG.printf("[BLE] intern RAM: %lu KB fri foer, %lu KB efter\n", (unsigned long)(heap0 / 1024),
                  (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}

// ---- Meshtastic-appen over Bluetooth (net/mt_phone.*) ----------------------------------
static void mtAppInfo(dm::MtPhone::Info& in) {
  snprintf(in.long_name, sizeof(in.long_name), "%s", g_mc_started ? g_mc_node.nodeName() : setting("meshcore_name", ""));
  snprintf(in.short_name, sizeof(in.short_name), "%s", setting("meshtastic_short_name", ""));
  const mt::RegionInfo* r = mt::regionByName(setting("meshtastic_region", "EU_868"));
  const mt::PresetInfo* p = mt::presetByName(setting("meshtastic_preset", ""));
  if (!r) r = mt::region(mt::Region::EU_868);
  if (!p) p = mt::preset(r->default_preset);
  in.region = int(r->id);
  in.preset = int(p->id);
  in.slot = g_store.getInt("meshtastic_slot", 0);
  in.hop_limit = 3;
  in.tx_power = g_store.getInt("meshtastic_tx_power", 22);
  in.tx_enabled = g_store.getInt("meshtastic_tx", 1) != 0;
  in.relay = strcmp(setting("meshtastic_relay", "fra"), "fra") != 0;
  in.position_secs = uint32_t(g_store.getInt("meshtastic_position_min", 30)) * 60u;
  in.nodeinfo_secs = uint32_t(g_store.getInt("meshtastic_nodeinfo_h", 3)) * 3600u;
  in.ble_pin = blePin();
}
static void mtAppSetOwner(const char* long_name, const char* short_name) {
  if (long_name && *long_name) {
    g_store.set("meshcore_name", long_name);
    if (g_mc_started) g_mc_node.setName(long_name);
  }
  if (short_name && *short_name) g_store.set("meshtastic_short_name", short_name);
  g_store.save();
  mtConfigure();
  menuReload();
  dm::DLOG.printf("[BLE] Meshtastic-app: navn \"%s\" (%s)\n", long_name, short_name);
}
static bool mtAppSetLora(int region, int preset, int hop, int pwr, int txe) {
  bool restart = false;
  if (region >= 0) {
    const mt::RegionInfo* r = mt::region(mt::Region(region));
    if (!r || !r->listed) return false;
    if (strcmp(setting("meshtastic_region", ""), r->name)) g_store.set("meshtastic_region", r->name), restart = true;
  }
  if (preset >= 0) {
    const mt::PresetInfo* p = mt::preset(mt::Preset(preset));
    if (!p) return false;
    const char* pn = mt::presetEnumName(p->id);
    if (strcmp(setting("meshtastic_preset", ""), pn)) g_store.set("meshtastic_preset", pn), restart = true;
  }
  if (pwr > 0 && pwr != g_store.getInt("meshtastic_tx_power", 22)) {
    g_store.setInt("meshtastic_tx_power", pwr > 22 ? 22 : pwr);
    restart = true;
  }
  if (txe >= 0) g_store.setInt("meshtastic_tx", txe ? 1 : 0);
  (void)hop;  // hop_limit er fast 3 (som Meshtastics standard)
  g_store.save();
  mtConfigure();
  menuReload();
  dm::DLOG.printf("[BLE] Meshtastic-app: radio%s\n", restart ? " aendret - genstarter" : " uaendret");
  if (restart) restartAfterImport(2000);
  return true;
}
static void mtAppSetTime(uint32_t t) { wallclock::set(t, wallclock::App); }
static size_t mtAppPop(uint8_t* buf, size_t cap) { return g_mt_phone.pop(buf, cap); }
static void startMtApp() {
  dm::MtPhone::Hooks h;
  h.info = mtAppInfo;
  h.set_owner = mtAppSetOwner;
  h.set_lora = mtAppSetLora;
  h.set_time = mtAppSetTime;
  h.reboot = restartAfterImport;
  h.batt_mv = bleBattMv;
  g_mt_phone.begin(&g_mt_node, &g_chan_scan, &g_mt_ble, h);
  char name[24];
  snprintf(name, sizeof(name), "DualMesh_%04lx", (unsigned long)(dm::mt_tx::nodeNum() & 0xFFFF));
  g_mt_ble.begin(name, blePin(), mtAppPop);
  g_ble_ui.setMtApp(&g_mt_ble, name, blePin());  // Bluetooth-skærmen viser Meshtastic-appen
  g_mt_app = true;
}

static bool mtSend(const uint8_t* data, size_t len) {
  if (!g_mt_txq || !g_radio_task || g_mt_profile < 0 || len == 0 || len > 255) return false;
  MtTxFrame f;
  f.len = uint8_t(len);
  memcpy(f.data, data, len);
  if (xQueueSend(g_mt_txq, &f, 0) != pdTRUE) return false;
  xTaskNotify(g_radio_task, kMtTxNotifyBit, eSetBits);
  return true;
}

// Én SOS-runde (eller afmelding): tekst + position på begge net.
static void sosSend(bool cancel) {
  int32_t la = 0, lo = 0, alt = 0;
  uint32_t age = 0;
  const bool have = g_gps.lastPosition(la, lo, alt, age);
  char pos[80];
  if (have) {
    char a[16], b[16];
    fmtDeg(la, a, sizeof(a));
    fmtDeg(lo, b, sizeof(b));
    if (age > 120)
      snprintf(pos, sizeof(pos), "Sidst kendte pos %s,%s (%lu min gammel)", a, b, (unsigned long)(age / 60));
    else
      snprintf(pos, sizeof(pos), "Pos %s,%s", a, b);
  } else {
    snprintf(pos, sizeof(pos), "Position ukendt (ingen GPS-fix)");
  }
  char hm[8];
  wallclock::formatHm(wallclock::now(), hm, sizeof(hm));
  char mc_text[150], mt_text[180];
  if (cancel) {
    snprintf(mc_text, sizeof(mc_text), "SOS er stoppet.");
    snprintf(mt_text, sizeof(mt_text), "SOS fra %s er stoppet.", g_mc_node.nodeName());
  } else {
    snprintf(mc_text, sizeof(mc_text), "SOS! Har brug for hjælp. %s kl %s", pos, hm);
    // \a (BEL) får Meshtastic-apps/-noder med alarm-notifikation til at give lyd.
    snprintf(mt_text, sizeof(mt_text), "\aSOS fra %s! Har brug for hjælp. %s kl %s",
             g_mc_node.nodeName(), pos, hm);
  }
  // MeshCore: Public + advert med position.
  if (g_mc_started && g_mc_radio.txEnabled()) {
    const bool ok = g_mc_node.sendPublic(mc_text, true);
    if (!cancel && have) g_mc_node.sendAdvert();
    dm::DLOG.printf("[SOS] MeshCore Public: %s (%s)\n", mc_text, ok ? "sat i koe" : "FEJL");
  } else {
    dm::DLOG.println("[SOS] MeshCore: afsendelse er FRA (meshcore_tx) - ikke sendt");
  }
  // Meshtastic: primær kanal (tekst + Position), hop_limit 7.
  if (g_chan_scan.mtChannelCount() > 0) {
    const MtChannel& ch = g_chan_scan.mtChannel(0);
    uint8_t buf[255];
    size_t n = mt_tx::buildText(ch, mt_text, 7, buf, sizeof(buf));
    const bool ok = n && mtSend(buf, n);
    if (!cancel && have) {
      n = mt_tx::buildPosition(ch, la, lo, alt, wallclock::now(), 32, 7, buf, sizeof(buf));
      if (n) mtSend(buf, n);
    }
    dm::DLOG.printf("[SOS] Meshtastic %s (!%08lx): %s (%s)\n", ch.name, (unsigned long)mt_tx::nodeNum(),
                    mt_text + (cancel ? 0 : 1), ok ? "sat i koe" : "FEJL");
    Msg m;  // vis den sendte tekst i Meshtastic-samtalen
    m.rx_ms = millis();
    m.ts = wallclock::now();
    m.net = 2;
    m.kind = Msg::ChannelOut;
    m.status = ok ? Msg::Queued : Msg::Failed;
    snprintf(m.conv, sizeof(m.conv), "%s", ch.conv);
    snprintf(m.peer, sizeof(m.peer), "%s", ch.name);
    snprintf(m.text, sizeof(m.text), "%s", mt_text + (cancel ? 0 : 1));
    g_msgs.push(m);
  }
}

static bool sosActive() { return g_sos; }

// Aktuel GPS-position (fix inden for 10 s)?
static bool sosFreshFix() {
  int32_t la, lo, alt;
  uint32_t age;
  return g_gps.state() == Gps::State::Fix && g_gps.lastPosition(la, lo, alt, age) && age < 10;
}

// SOS startet (af brugeren eller efter genstart): tænd GPS og vent på fix.
static void sosBegin() {
  g_sos_start_ms = millis();
  if (!g_gps.enabled()) {
    g_sos_gps_auto = true;
    g_gps.setEnabled(true);
    dm::DLOG.println("[SOS] GPS var slaaet fra - taendt for SOS");
  }
  g_sos_wait_fix = !sosFreshFix();
  if (g_sos_wait_fix) dm::DLOG.println("[SOS] venter paa GPS-fix foer foerste udsendelse (hoejst 3 min)");
}

static void sosSet(bool on) {
  if (on == g_sos) return;
  g_sos = on;
  g_store.setInt("sos_active", on ? 1 : 0);
  g_store.save();
  if (on) {
    g_sos_next_ms = millis();
    g_sos_rounds = 0;
    dm::DLOG.println("[SOS] AKTIVERET af brugeren");
    sosBegin();
  } else {
    dm::DLOG.printf("[SOS] stoppet af brugeren efter %lu runder\n", (unsigned long)g_sos_rounds);
    g_sos_wait_fix = false;
    // "SOS er stoppet" kun, hvis der faktisk er sendt et SOS (ellers forvirrer det andre;
    // set 15. nov 2026: stoppet under ventetiden på GPS-fix, men stop-beskeden blev sendt).
    if (g_sos_rounds > 0) sosSend(true);
    else dm::DLOG.println("[SOS] intet SOS var sendt endnu - ingen stop-besked");
    if (g_sos_gps_auto) {  // GPS'en var slået fra før SOS: sluk igen
      g_sos_gps_auto = false;
      g_gps.setEnabled(g_store.getInt("gps_enabled", 1) != 0);
    }
  }
}

static uint32_t sosPeriodMs() {
  long m = g_store.getInt("sos_interval_min", 5);
  if (m < 2) m = 2;
  if (m > 60) m = 60;
  return uint32_t(m) * 60000u;
}

static void sosTick() {
  if (!g_sos || !g_ready) return;
  if (g_sos_wait_fix) {
    if (sosFreshFix()) {
      dm::DLOG.printf("[SOS] GPS-fix efter %lu s - sender\n", (unsigned long)((millis() - g_sos_start_ms) / 1000));
    } else if (millis() - g_sos_start_ms >= kSosFixWaitMs) {
      dm::DLOG.println("[SOS] intet GPS-fix efter 3 min - sender uden aktuel position");
    } else {
      return;
    }
    g_sos_wait_fix = false;  // første runde: nu (eller efter kalibreringen ved genstart)
  }
  if (int32_t(millis() - g_sos_next_ms) < 0) return;
  g_sos_next_ms = millis() + sosPeriodMs();
  ++g_sos_rounds;
  dm::DLOG.printf("[SOS] runde %lu\n", (unsigned long)g_sos_rounds);
  sosSend(false);
}

static const char* sosStatusText() {
  static char t[48];
  if (!g_sos) return nullptr;
  if (g_sos_wait_fix) {
    const uint32_t s = (millis() - g_sos_start_ms) / 1000;
    snprintf(t, sizeof(t), "SOS: venter på GPS-fix (%lu:%02lu)", (unsigned long)(s / 60),
             (unsigned long)(s % 60));
    return t;
  }
  const int32_t left = int32_t(g_sos_next_ms - millis()) / 1000;
  snprintf(t, sizeof(t), "SOS AKTIV: %lu runder, næste om %ld:%02ld", (unsigned long)g_sos_rounds,
           long(left > 0 ? left / 60 : 0), long(left > 0 ? left % 60 : 0));
  return t;
}

// ------------------------------------------------------------ skærm
// Status som LVGL-rækker (Indst. > Status): oppetid/strøm og radio øverst, derefter
// scheduler, WiFi, GPS, et afsnit pr. net (MeshCore først) og noden. Panelet ruller listen.
static void statusView(dm::PanelView& v) {
  using dm::PanelRow;
  const uint32_t now = millis();
  const uint32_t up = now / 1000;
  char logtxt[16];
  if (dm::DLOG.active()) snprintf(logtxt, sizeof(logtxt), "log %04d", dm::DLOG.fileNo());
  else snprintf(logtxt, sizeof(logtxt), "SD %.8s", g_spi_ok ? g_store.status() : "SPI-FEJL");
  const float bat = batteryVolts();
  snprintf(v.status, sizeof(v.status), "Oppe %lu:%02lu:%02lu   %s   %.2f V %s", (unsigned long)(up / 3600),
           (unsigned long)(up / 60 % 60), (unsigned long)(up % 60), logtxt, bat, usbText(usbSource()));
  v.status_fg = (g_spi_ok && dm::DLOG.active() && bat >= 3.5f) ? color::White : color::Yellow;
  char hm[8];
  wallclock::formatHm(wallclock::now(), hm, sizeof(hm));
  if (wallclock::valid()) snprintf(v.status3, sizeof(v.status3), "%s", hm);

  const int st = g_boot.stage;
  if (st == 0 || st == 2) {
    snprintf(v.status2, sizeof(v.status2), "Radio: %s",
             st == 2 ? "FEJL ved start" : (now > 8000 ? "svarer ikke" : "starter ..."));
    return;
  }
  const bool sync_ok = g_boot.sync_mc == expectedSyncReg(mc::kSyncWord) &&
                       g_boot.sync_mt == expectedSyncReg(mt::kSyncWord);
  snprintf(v.status2, sizeof(v.status2), "Radio %s   sync %s   skift %lu us   %d dBm",
           g_boot.errors_after ? "FEJL" : "OK", sync_ok ? "OK" : "FEJL", (unsigned long)g_boot.retune_us,
           g_hal ? g_hal->txPower() : 0);

  if (g_sos) v.add(PanelRow::Header, color::Red, g_sos_wait_fix ? "SOS: venter på GPS-fix, sender derefter"
                                                                 : "SOS AKTIV: hold trackball 5 s for stop");
  const Settle settle = (g_sched && st == 3) ? g_sched->settleState() : Settle::Calibrating;
  {
    char t[40];
    snprintf(t, sizeof(t), "%s%s%s", st == 3 ? "kører" : (st == 4 ? "FEJL" : "starter ..."),
             st == 3 && settle != Settle::Stable ? ", " : "", st == 3 && settle != Settle::Stable ? settleText(settle) : "");
    v.add(PanelRow::Info, color::Gray, "Scheduler",
          st == 3 ? (settle == Settle::Stable ? color::Green : color::Yellow) : (st == 4 ? color::Red : color::Yellow),
          "%s", t);
  }
  if (g_wifi.state() == WifiMgr::State::Connected) v.info("WiFi", "%.30s", g_wifi.ssid());
  else v.add(PanelRow::Info, color::Gray, "WiFi", color::Gray, "%s", g_wifi.enabled() ? "ikke forbundet" : "fra");
  const Gps::State gs = g_gps.state();
  if (gs == Gps::State::Fix) v.info("GPS", "%u satellitter", g_gps.fix().sats);
  else v.add(PanelRow::Info, color::Gray, "GPS", color::Gray, "%s",
             gs == Gps::State::Off ? "fra" : (gs == Gps::State::NoFix ? "søger" : "?"));
  if (!g_sched || st != 3) return;

  const SchedulerStats s = g_sched->stats();
  for (int n = 1; n >= 0; --n) {  // MeshCore altid før Meshtastic (brugerens princip)
    const char* name = n ? "MeshCore" : "Meshtastic";
    if (!(n ? g_mc_on : g_mt_on)) {
      char t[48];
      snprintf(t, sizeof(t), "%s: slået fra (Indst.)", name);
      v.add(PanelRow::Header, color::Dim, t);
      continue;
    }
    for (size_t g = 0; g < g_sched->groupCount() && g < 2; ++g) {
      const GroupPlan& p = g_sched->plan(g);
      if (((p.net_mask & netBit(NetId::MeshCore)) ? 1 : 0) != n) continue;
      const GroupStats& a = s.g[g];
      const LoRaProfile& r = p.rx;
      char t[48];
      snprintf(t, sizeof(t), "%s   %s", name, g_radio_desc[n]);
      v.add(PanelRow::Header, n ? color::LightBlue : color::Green, t);
      v.info("Radio", "%.3f MHz, BW %.1f, SF%u, CR4/%u", r.freq_hz / 1e6, r.bw_hz / 1000.0, r.sf, r.cr);
      const double noise = a.cads ? 100.0 * a.noise / a.cads : 0.0;
      v.add(PanelRow::Info, color::Gray, "Aktivitet", noise > 0.5 ? color::Yellow : color::White,
            "CAD %lu, pakker %lu, støj %.1f%%", (unsigned long)a.cads, (unsigned long)a.rx_ok, noise);
      if (g_last[n].t_ms) {
        const uint32_t ago = (now - g_last[n].t_ms) / 1000;
        char ta[12];
        if (ago < 120) snprintf(ta, sizeof(ta), "%lu s", (unsigned long)ago);
        else if (ago < 7200) snprintf(ta, sizeof(ta), "%lu min", (unsigned long)(ago / 60));
        else snprintf(ta, sizeof(ta), "%lu t", (unsigned long)(ago / 3600));
        v.info("Sidst hørt", "%d dBm, %.1f dB, for %s", g_last[n].rssi, g_last[n].snr_q4 / 4.0, ta);
      } else {
        v.add(PanelRow::Info, color::Gray, "Sidst hørt", color::Gray, "ingen pakker endnu");
      }
    }
  }
  v.add(PanelRow::Header, color::Gold, "Node og afsendelse");
  if (g_mc_started) {
    const McNodeStats& ms = g_mc_node.nodeStats();
    v.info("MeshCore-node", "%.20s", g_mc_node.nodeName());
    v.info("Kontakter / beskeder", "%d / kanal %lu, DM %lu", g_mc_node.contactCount(),
           (unsigned long)ms.channel_msgs, (unsigned long)ms.direct_msgs);
  }
  v.add(PanelRow::Info, color::Gray, "Sendt / watchdog", s.watchdog ? color::Yellow : color::White, "%lu / %lu%s",
        (unsigned long)s.tx_ok, (unsigned long)s.watchdog, g_hourly_test ? " (testbesked til)" : "");
  v.add(PanelRow::Info, color::Gray, "Største hul MT/MC", s.early_abort_active ? color::White : color::Yellow,
        "%.0f / %.0f ms", s.g[0].max_gap_us / 1000.0, s.g[1].max_gap_us / 1000.0);
}

// Fejlrapport-siden (docs/rapport/) sender 'I' og læser blokken mellem markørerne.
// Kun version, opstart, oppetid og nedbrudsadresser: intet, der skal renses.
static void printReportHeader() {
  dm::diag::CrashSummary c;
  const bool crash = dm::crashinfo::read(c);
  char line[96], bt[400];
  dm::diag::formatCrashLine(c, line, sizeof(line));
  dm::diag::formatBacktrace(c, bt, sizeof(bt), true);
  dm::DLOG.printf("\n=== DMRAPPORT ===\nversion %s (%s)\nboard T-Deck Plus\nopstart %s (%s)\n"
                  "oppetid %lu s\nnedbrud %s\n",
                  DM_FW_VERSION, DM_GIT_REV, dm::crashinfo::resetCode(), resetReasonText(),
                  (unsigned long)(millis() / 1000), line);
  if (crash) {
    dm::DLOG.printf("backtrace %s\nelf %s (%s build)\n", bt, c.elf,
                    dm::crashinfo::fromOtherBuild(c) ? "ANDET" : "samme");
  }
  dm::DLOG.printf("sd %s, logfil %s\n=== DMRAPPORT-SLUT ===\n", dm::DLOG.active() ? "ja" : "nej",
                  dm::DLOG.active() ? dm::DLOG.fileName() : "-");
}

// ------------------------------------------------------------ Arduino
static void startSdLog() {
  dm::DLOG.begin(g_store.mounted());
  // Versionen står i hver logfil, så fejlrapport-siden kan finde den i en log fra SD-kortet.
  dm::DLOG.printf("[LOG] DualMesh %s (%s)\n", DM_FW_VERSION, DM_GIT_REV);
  if (dm::DLOG.active())
    dm::DLOG.printf("[LOG] logger til SD: %s (opstart: %s)\n", dm::DLOG.fileName(),
                    resetReasonText());
  else
    dm::DLOG.println("[LOG] intet SD-kort - logger kun til USB");
}

// Indstillinger, der virker med det samme (uden genstart).
static AppUi::Settings uiSettingsFromStore() {
  AppUi::Settings us;
  us.sensitivity = uint8_t(g_store.getInt("trackball_sensitivity", 5));
  us.invert_x = g_store.getInt("trackball_invert_x", 0) != 0;
  us.invert_y = g_store.getInt("trackball_invert_y", 0) != 0;
  us.click_delay_ms = uint16_t(g_store.getInt("trackball_click_delay_ms", 150));
  us.deadzone = uint8_t(g_store.getInt("trackball_deadzone", 3));
  us.map_sensitivity = uint8_t(g_store.getInt("map_sensitivity", 5));
  us.kb_backlight = uint8_t(g_store.getInt("keyboard_backlight", 127));
  us.kb_backlight_timeout_s = uint16_t(g_store.getInt("keyboard_backlight_timeout_s", 10));
  us.screen_timeout_s = uint16_t(g_store.getInt("screen_timeout_s", 20));
  us.screen_timeout_on_usb = g_store.getInt("screen_timeout_usb", 0) != 0;
  us.screen_brightness = uint8_t(g_store.getInt("screen_brightness", 16));
  us.usb_connected = usbConnected;
  return us;
}

static Touch::Orientation touchFromStore() {
  Touch::Orientation to;
  to.swap_xy = g_store.getInt("touch_swap_xy", 1) != 0;
  to.invert_x = g_store.getInt("touch_invert_x", 0) != 0;
  to.invert_y = g_store.getInt("touch_invert_y", 1) != 0;
  return to;
}

// Kaldes af menuen efter Gem: anvend alt, der kan anvendes uden genstart.
// (g_store.get bruges direkte, så det også virker uden SD-kort indtil genstart.)
// Navnet på vores Meshtastic-primærkanal: kanalnavnet, ellers presetnavnet (fx "LongFast").
static const char* mtPrimaryName() {
  const char* ch = setting("meshtastic_channel", "");
  if (ch && *ch) return ch;
  const mt::PresetInfo* p = mt::presetByName(setting("meshtastic_preset", "LONG_FAST"));
  return p ? p->name : "LongFast";
}

// Testbeskeden: tekst og interval fra settings.ini.
static void loadTestSettings() {
  const char* t = g_store.get("meshcore_test_text");
  snprintf(g_test_text, sizeof(g_test_text), "%s", t && *t ? t : kTestText);
  long min = g_store.getInt("meshcore_test_interval_min", 15);
  if (min < 2) min = 2;
  if (min > 1440) min = 1440;
  g_test_period_ms = uint32_t(min) * 60000u;
}

// Sendetid (ms) for en MeshCore-kanalbesked "navn: tekst" med den aktuelle radio:
// header + path_len + kanal-hash + 2 byte MAC + krypteret (tid 4 + flag 1 + tekst),
// rundet op til hele AES-blokke (16 byte).
static uint32_t testAirtimeMs(const char* text, const char* name) {
  const size_t plain = 5 + strlen(name) + 2 + strlen(text);
  const size_t len = 2 + 1 + 2 + (plain + 15) / 16 * 16;
  const LoRaProfile p = meshcoreFromSettings();
  return (phy::airtimeUs(p, uint16_t(len > 255 ? 255 : len)) + 999) / 1000;
}

// MeshCore-kanalens indeks for et samtale-id (mcc_...), -1 = ingen.
static int mcChannelForConv(const char* conv) {
  if (!g_mc_started || strncmp(conv, "mcc_", 4)) return -1;
  for (int i = 0; i < 40; ++i) {
    ChannelDetails d;
    if (!g_mc_node.channelAt(i, d) || !d.name[0]) continue;
    char id[20];
    McNode::channelConvId(d.channel, id, sizeof(id));
    if (!strcmp(id, conv)) return i;
  }
  return -1;
}

// Meshtastic-kanalens indeks for et samtale-id (mtc_...), -1 = ingen.
static int mtChannelForConv(const char* conv) {
  if (strncmp(conv, "mtc_", 4)) return -1;
  for (int i = 0; i < g_chan_scan.mtChannelCount(); ++i)
    if (!strcmp(g_chan_scan.mtChannel(i).conv, conv)) return i;
  return -1;
}

// Privat samtale: en DM, eller en kanal med egen hemmelig nøgle (ikke MeshCores Public eller
// #hashtag-kanaler, ikke en Meshtastic-kanal med standardnøglen eller en 1-byte-nøgle).
static bool convIsPrivate(const char* conv) {
  if (!conv || !conv[0]) return false;
  if (!strncmp(conv, "mcd_", 4) || !strncmp(conv, "mtd_", 4)) return true;
  const int mc = mcChannelForConv(conv);
  if (mc > 0) {
    ChannelDetails d;
    return g_mc_node.channelAt(mc, d) && d.name[0] != '#';
  }
  const int mt = mtChannelForConv(conv);
  if (mt >= 0) {
    const dm::MtChannel& ch = g_chan_scan.mtChannel(mt);
    return ch.klen > 0 && strlen(ch.psk_b64) > 4;
  }
  return false;
}

// Send en tekst til en samtale ud fra dens id (bruges af testbeskeden).
static bool sendToConv(const char* conv, const char* text) {
  const int mc = mcChannelForConv(conv);
  if (mc >= 0) return g_mc_radio.txEnabled() && g_mc_node.sendChannel(mc, text, true);
  if (!strncmp(conv, "mcd_", 4) && g_mc_started && strlen(conv) >= 16) {
    uint8_t pre[6];
    for (int b = 0; b < 6; ++b) {
      char hx[3] = {conv[4 + 2 * b], conv[5 + 2 * b], 0};
      pre[b] = uint8_t(strtoul(hx, nullptr, 16));
    }
    ContactInfo* c = g_mc_node.lookupContactByPubKey(pre, 6);
    return c && g_mc_radio.txEnabled() && g_mc_node.sendDirect(*c, text);
  }
  const int mt = mtChannelForConv(conv);
  if (mt >= 0) return g_mt_node.sendText(mt, text);
  if (!strncmp(conv, "mtd_", 4)) return g_mt_node.sendDm(uint32_t(strtoul(conv + 4, nullptr, 16)), text) > 0;
  return false;
}

// Tandhjulsmenuen: er testbeskeden slået til i denne samtale?
static bool testOnConv(const char* conv) { return g_hourly_test && conv && !strcmp(conv, g_test_conv); }

// Slå testbeskeden til/fra i en samtale. 1 = til, 0 = fra, -1 = ikke tilladt (ikke privat).
static int testToggleConv(const char* conv) {
  if (testOnConv(conv)) {
    g_hourly_test = false;
    g_store.setInt("meshcore_hourly_test", 0);
    g_store.save();
    dm::DLOG.printf("[TEST] testbesked FRA (%s)\n", conv);
    return 0;
  }
  if (!convIsPrivate(conv)) {
    dm::DLOG.printf("[TEST] testbesked afvist i %s: kun DM eller privat kanal\n", conv ? conv : "-");
    return -1;
  }
  snprintf(g_test_conv, sizeof(g_test_conv), "%s", conv);
  g_hourly_test = true;
  g_test_reply = false;
  g_next_test_ms = millis();  // første med det samme
  g_store.set("test_conv", g_test_conv);
  g_store.setInt("meshcore_hourly_test", 1);  // overlever genstart
  g_store.save();
  dm::DLOG.printf("[TEST] testbesked TIL til %s\n", g_test_conv);
  return 1;
}

// Tjek før Gem: testbeskeden må højst bruge 10 % airtime (EU 869,4-869,65 MHz).
static const char* onMenuCheck(const SettingsUi& ui) {
  static char note[48];
  const char* t = ui.edited("meshcore_test_text");
  const char* n = ui.edited("meshcore_name");
  const uint32_t air = testAirtimeMs(t && *t ? t : kTestText, n && *n ? n : g_mc_node.nodeName());
  const long min = strtol(ui.edited("meshcore_test_interval_min"), nullptr, 10);
  const uint32_t need_s = (10 * air + 999) / 1000;  // 10 % => periode >= 10 x sendetid
  if (min * 60 < long(need_s)) {
    snprintf(note, sizeof(note), "Test: mindst %lu min (10%% airtime)",
             (unsigned long)((need_s + 59) / 60));
    return note;
  }
  // MeshCore anbefaler flood-adverts sjældent (repeatere: mindst 3 t): de fylder hele nettet.
  if (!strcmp(ui.edited("meshcore_enabled"), "0") && !strcmp(ui.edited("meshtastic_enabled"), "0"))
    return "Mindst ét net skal være slået til";
  const long fh = strtol(ui.edited("meshcore_advert_flood_h"), nullptr, 10);
  if (fh > 0 && fh < 3) return "Flood-advert: fra eller mindst 3 t";
  return nullptr;
}

// Handlinger fra indstillingsmenuen (udføres straks efter to klik).
static const char* onMenuAction(const char* key) {
  static char note[48];
  if (strcmp(key, ">advert_local") == 0 || strcmp(key, ">advert_flood") == 0) {
    if (!g_mc_started) return "MeshCore kører ikke";
    if (!g_mc_radio.txEnabled()) return "\"Må sende\" er fra (MeshCore)";
    const bool flood = key[8] == 'f';
    double la, lo;
    const bool pos = mcAdvertPosition(la, lo);
    if (!g_mc_node.sendAdvertMode(flood)) return "Advert kunne ikke sendes";
    snprintf(note, sizeof(note), "%s advert sendt (%s position)", flood ? "Flood-" : "Lokal",
             pos ? "med" : "uden");
    return note;
  }
  if (strcmp(key, "!clear_chat") == 0) {
    if (!g_msgs.openId()[0]) return "Ingen åben chat";
    g_msgs.clear(g_msgs.openId());
    snprintf(note, sizeof(note), "Ryddet: %.30s", g_msgs.openTitle());
    return note;
  }
  if (strcmp(key, "!clear_channels") == 0) {
    const int n = g_msgs.clearPrefix("mcc_") + g_msgs.clearPrefix("mtc_");
    snprintf(note, sizeof(note), "Alle kanal-chats ryddet (%d filer)", n);
    return note;
  }
  if (strcmp(key, ">status") == 0) {
    g_ui.openStatus();
    return nullptr;
  }
  if (strcmp(key, ">wifi") == 0) {
    g_ui.openPanel(&g_wifi_ui);
    return nullptr;
  }
  if (strcmp(key, ">profile_export") == 0 || strcmp(key, ">profile_import") == 0) {
    if (key[9] == 'i') g_profile_ui.openImport();
    else g_profile_ui.openExport();
    g_ui.openPanel(&g_profile_ui);
    return nullptr;
  }
  if (strcmp(key, ">colors") == 0) {
    g_ui.openPanel(&g_colors_ui);
    return nullptr;
  }
  if (strcmp(key, ">telem_contacts") == 0) {
    g_ui.openPanel(&g_perms_ui);
    return nullptr;
  }
  if (strcmp(key, ">reset") == 0) {
    g_ui.openPanel(&g_reset_ui);
    return nullptr;
  }
  if (strcmp(key, ">nvs") == 0) {
    g_ui.openPanel(&g_nvs_ui);
    return nullptr;
  }
  if (strcmp(key, ">crypto") == 0) {
    g_ui.openPanel(&g_crypto_ui);
    return nullptr;
  }
  if (strcmp(key, ">gps") == 0) {
    g_ui.openPanel(&g_gps_ui);
    return nullptr;
  }
  if (strcmp(key, ">mqtt") == 0) {
    g_ui.openPanel(&g_mqtt_ui);
    return nullptr;
  }
  if (strcmp(key, ">about") == 0) {
    char mc[32] = "", mt[16] = "";
    if (g_mc_started) {
      const uint8_t* k = g_mc_node.selfPubKey();
      snprintf(mc, sizeof(mc), "%02x%02x%02x%02x%02x%02x...", k[0], k[1], k[2], k[3], k[4], k[5]);
    }
    if (g_mt_on) snprintf(mt, sizeof(mt), "!%08lx", (unsigned long)dm::mt_tx::nodeNum());
    g_about_ui.setIds(mc, mt);
    g_ui.openPanel(&g_about_ui);
    return nullptr;
  }
  if (strcmp(key, ">report") == 0) {
    g_ui.openPanel(&g_report_ui);
    return nullptr;
  }
  if (strcmp(key, ">dl_keyboards") == 0 || strcmp(key, ">dl_lang") == 0 || strcmp(key, ">dl_maps") == 0 ||
      strcmp(key, ">ota") == 0) {
    if (WiFi.status() != WL_CONNECTED) return "Kræver WiFi (Indst. > WiFi)";
    if (key[1] == 'o') g_ui.openPanel(&g_ota_ui);
    else if (key[4] == 'k') g_ui.openPanel(&g_dl_kbd);
    else if (key[4] == 'l') g_ui.openPanel(&g_dl_lang);
    else g_ui.openPanel(&g_dl_maps);
    return nullptr;
  }
  if (strcmp(key, ">ble") == 0) {
    g_ui.openPanel(&g_ble_ui);
    return nullptr;
  }
  if (strcmp(key, "!usb_disk") == 0) {
    if (g_store.state() == SdState::NoCard) return "Intet SD-kort";
    enterUsbDisk("menuen");  // vender ikke tilbage
  }
  if (strcmp(key, "!okapi_fetch") == 0) {  // geocaches fra Opencaching omkring os
    double lat = g_map.centerLat(), lon = g_map.centerLon();
    int32_t la, lo, al;
    uint32_t age;
    if (g_gps.lastPosition(la, lo, al, age) && age < 600) lat = la / 1e7, lon = lo / 1e7;
    const int radius = int(g_store.getInt("okapi_radius_km", 10));
    if (dm::okapi::start(setting("okapi_site", "www.opencaching.de"), setting("okapi_key", ""), lat, lon, radius)) {
      return "Henter caches fra Opencaching ...";
    }
    dm::okapi::poll(note, sizeof(note));  // fejlen (mangler nøgle/WiFi)
    return note;
  }
  if (strcmp(key, "!clear_log") == 0) {
    const int n = dm::DLOG.clearAll();
    if (n < 0) return "Intet SD-kort: ingen log at rydde";
    dm::DLOG.printf("[LOG] loggen er ryddet fra menuen (%d filer slettet), ny fil %s\n", n,
                    dm::DLOG.fileName());
    snprintf(note, sizeof(note), "Log ryddet: %d filer slettet", n);
    return note;
  }
  return "Ukendt handling";
}

// ---------- MQTT-bro (fase 6) ----------
static constexpr const char* kMqttFile = "/sd/dualmesh/mqtt.txt";  // HEMMELIG: brokerens kode

static void mqttLoadPass(char* out, size_t n) {
  out[0] = 0;
  FILE* f = dm::sdcrypt::openRead(kMqttFile);
  if (!f) return;
  char line[200];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "pass=", 5)) snprintf(out, n, "%s", line + 5);
  }
  dm::sdcrypt::closeRead(f);
}

// ---------- Bluetooth-PIN (hemmelig, /sd/dualmesh/ble.txt) ----------
static constexpr const char* kBleFile = "/sd/dualmesh/ble.txt";

static uint32_t blePin() {
  uint32_t pin = 123456;
  if (FILE* f = dm::sdcrypt::openRead(kBleFile)) {
    char line[48];
    while (fgets(line, sizeof(line), f))
      if (!strncmp(line, "pin=", 4) && atol(line + 4) > 0) pin = uint32_t(atol(line + 4));
    dm::sdcrypt::closeRead(f);
  }
  return pin;
}

// "" = behold, "-" = standard (slet), ellers præcis 6 cifre.
static void bleSavePin(const char* v) {
  if (!v[0]) return;
  const bool del = !strcmp(v, "-");
  if (!del && (strlen(v) != 6 || strspn(v, "0123456789") != 6)) {
    dm::DLOG.println("[BLE] PIN ikke gemt: skal vaere 6 cifre");
    return;
  }
  FILE* f = dm::sdcrypt::openWrite();
  if (!f) return;
  fprintf(f, "; DualMesh Bluetooth (HEMMELIG)\n");
  if (!del) fprintf(f, "pin=%s\n", v);
  const bool ok = dm::sdcrypt::commit(f, kBleFile);
  dm::DLOG.printf("[BLE] PIN %s (%s, gaelder efter genstart)\n", del ? "nulstillet" : "gemt krypteret",
                  ok ? "OK" : "FEJL");
}

// Ældre firmware havde PIN'en i settings.ini: flyt den til den krypterede fil, og
// overskriv den gamle kopi (settings.bak), så den ikke kan læses på kortet.
static void bleMigratePin() {
  const char* old = setting("ble_pin", "");
  if (!old[0] || !g_store.mounted() || dm::sdcrypt::locked()) return;
  char v[12];
  snprintf(v, sizeof(v), "%s", old);
  bleSavePin(v);
  g_store.set("ble_pin", "");
  g_store.save();
  dm::sdcrypt::shred("/sd/dualmesh/settings.bak");
  dm::DLOG.println("[BLE] PIN flyttet fra settings.ini til krypteret fil");
}

// Hemmelige indstillinger fra Indst./monitoren: gemmes krypteret (aldrig i settings.ini).
static void mqttSecret(const char* key, const char* value) {
  if (!strcmp(key, "ble_pin")) {
    bleSavePin(value);
    return;
  }
  if (strcmp(key, "mqtt_pass") != 0) return;
  if (!g_store.mounted()) {
    dm::DLOG.println("[MQTT] intet SD-kort: adgangskoden kan ikke gemmes");
    return;
  }
  const bool del = !strcmp(value, "-");
  FILE* f = dm::sdcrypt::openWrite();
  if (!f) return;
  fprintf(f, "; DualMesh MQTT (HEMMELIG)\n");
  if (!del) fprintf(f, "pass=%s\n", value);
  const bool ok = dm::sdcrypt::commit(f, kMqttFile);
  dm::DLOG.printf("[MQTT] adgangskode %s (%s)\n", del ? "slettet" : "gemt krypteret", ok ? "OK" : "FEJL");
}

static void mqttApply() {
  dm::MqttBridge::Config c;
  c.enabled = g_store.getInt("mqtt_enabled", 0) != 0;
  snprintf(c.uri, sizeof(c.uri), "%s", setting("mqtt_broker", ""));
  snprintf(c.user, sizeof(c.user), "%s", setting("mqtt_user", ""));
  mqttLoadPass(c.pass, sizeof(c.pass));
  snprintf(c.root, sizeof(c.root), "%s", setting("mqtt_root", ""));
  c.tx_allowed = g_store.getInt("mqtt_tx", 0) != 0;
  long sm = g_store.getInt("mqtt_status_min", 5);
  c.status_min = uint16_t(sm < 0 ? 0 : (sm > 1440 ? 1440 : sm));
  g_mqtt.configure(c);
}

// <rod>/status: navn, strøm, GPS-fix, net og (kun hvis tilladt) position.
static void mqttStatus(char* out, size_t n) {
  char name[96];
  dm::MqttBridge::jsonEscape(g_mc_started ? g_mc_node.nodeName() : setting("meshcore_name", ""), name,
                             sizeof(name));
  int k = snprintf(out, n,
                   "\"name\":\"%s\",\"batt_v\":%.2f,\"usb\":%s,\"gps_fix\":%s,\"meshcore\":%s,"
                   "\"meshtastic\":%s",
                   name, batteryVolts(), usbConnected() ? "true" : "false",
                   g_gps.state() == Gps::State::Fix ? "true" : "false", g_mc_on ? "true" : "false",
                   g_mt_on ? "true" : "false");
  double lat, lon;
  if (k > 0 && size_t(k) < n && g_gps.state() == Gps::State::Fix && positionFor("mqtt_share_position", lat, lon))
    snprintf(out + k, n - k, ",\"lat\":%.5f,\"lon\":%.5f", lat, lon);
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Afsendelse fra MQTT (kun når "Må sende fra MQTT" er til; broen begrænser tempoet).
static bool mqttTx(const char* net, bool dm, const char* target, const char* text, char* err, size_t en) {
  auto fail = [&](const char* m) {
    snprintf(err, en, "%s", m);
    return false;
  };
  if (!strcmp(net, "meshcore")) {
    if (!g_mc_started) return fail("MeshCore er slået fra");
    if (!g_mc_radio.txEnabled()) return fail("MeshCore: Må sende er fra");
    if (!dm) {
      const int n = g_mc_node.channelCount();
      for (int i = 0; i < n; ++i) {
        ChannelDetails cd;
        if (!g_mc_node.channelAt(i, cd) || !cd.name[0]) continue;
        if (!strcasecmp(cd.name, target) || (cd.name[0] == '#' && !strcasecmp(cd.name + 1, target)))
          return g_mc_node.sendChannel(i, text) ? true : fail("kunne ikke sættes i kø");
      }
      return fail("ukendt MeshCore-kanal");
    }
    // Modtager: hex-præfiks af den offentlige nøgle (mindst 4 hex) eller kontaktens navn.
    ContactInfo* c = nullptr;
    const size_t hl = strlen(target);
    bool hex = hl >= 4 && hl <= 64 && hl % 2 == 0;
    for (size_t i = 0; hex && i < hl; ++i) hex = hexVal(target[i]) >= 0;
    if (hex) {
      uint8_t pk[32];
      for (size_t i = 0; i < hl / 2; ++i) pk[i] = uint8_t(hexVal(target[2 * i]) * 16 + hexVal(target[2 * i + 1]));
      c = g_mc_node.lookupContactByPubKey(pk, int(hl / 2));
    }
    if (!c) {
      ContactInfo ci;
      // Alle pladser: getNumContacts() trækker MeshCores 8 anonyme pladser fra, men
      // getContactByIdx() tæller dem med (de sidste 8 kontakter blev ellers aldrig fundet).
      for (int i = 0; i < g_mc_node.getTotalContactSlots(); ++i)
        if (g_mc_node.getContactByIdx(uint32_t(i), ci) && ci.name[0] && !strcasecmp(ci.name, target)) {
          c = g_mc_node.lookupContactByPubKey(ci.id.pub_key, PUB_KEY_SIZE);
          break;
        }
    }
    if (!c) return fail("ukendt MeshCore-kontakt");
    return g_mc_node.sendDirect(*c, text) ? true : fail("kunne ikke sættes i kø");
  }
  if (!g_mt_on) return fail("Meshtastic er slået fra");
  if (dm) {  // Meshtastic-DM: !nodeid (hex) eller nodens navn (langt/kort)
    uint32_t num = 0;
    const char* t = target[0] == '!' ? target + 1 : target;
    char* e = nullptr;
    if (strlen(t) == 8) num = uint32_t(strtoul(t, &e, 16));
    if (!num || (e && *e)) {
      num = 0;
      for (int i = 0; i < g_mt_node.count() && !num; ++i) {
        const dm::MtNodeEntry& n = g_mt_node.at(i);
        if (!strcasecmp(n.long_name, target) || !strcasecmp(n.short_name, target)) num = n.num;
      }
    }
    if (!num) return fail("ukendt Meshtastic-node");
    const int r = g_mt_node.sendDm(num, text);
    if (r < 0) return fail("nodens noegle kendes ikke endnu (har bedt om NodeInfo)");
    return r > 0 ? true : fail("kunne ikke sættes i kø (eller Må sende er fra)");
  }
  for (int i = 0; i < g_chan_scan.mtChannelCount(); ++i) {
    const MtChannel& ch = g_chan_scan.mtChannel(i);
    if (strcasecmp(ch.name, target) != 0) continue;
    (void)ch;
    return g_mt_node.sendText(i, text) ? true : fail("kunne ikke sættes i kø (eller Må sende er fra)");
  }
  return fail("ukendt Meshtastic-kanal");
}

// Meshtastic: egen position efter "Del position" (SOS = præcis). precision 32 = præcis,
// 15 = omtrent (ca. 1 km, som MeshCores "omtrent").
static bool mtPosition(int32_t& la, int32_t& lo, int32_t& alt, uint32_t& prec) {
  uint32_t age;
  if (!g_gps.lastPosition(la, lo, alt, age)) return false;
  const char* mode = g_sos ? "præcis" : setting("meshtastic_share_position", "fra");
  if (!strcmp(mode, "præcis")) prec = 32;
  else if (!strcmp(mode, "omtrent")) prec = 15;
  else return false;
  return true;
}

// Meshtastic-telemetri: 101 = strømforsyning; ellers en simpel LiPo-kurve 3,3-4,2 V.
static bool mtBattery(uint8_t& level, float& volts) {
  volts = batteryVolts();
  if (usbConnected()) {
    level = 101;
  } else {
    const float p = (volts - 3.3f) / (4.2f - 3.3f) * 100.0f;
    level = uint8_t(p < 0 ? 0 : p > 100 ? 100 : p);
  }
  return volts > 1.0f;
}

static void mtConfigure() {
  dm::MtNode::Config c;
  c.tx = g_mt_on && g_store.getInt("meshtastic_tx", 1) != 0;
  snprintf(c.long_name, sizeof(c.long_name), "%s",
           g_mc_started ? g_mc_node.nodeName() : setting("meshcore_name", ""));
  snprintf(c.short_name, sizeof(c.short_name), "%s", setting("meshtastic_short_name", ""));
  c.nodeinfo_h = uint16_t(g_store.getInt("meshtastic_nodeinfo_h", 3));
  c.position_min = uint16_t(g_store.getInt("meshtastic_position_min", 30));
  const char* rl = setting("meshtastic_relay", "fra");
  c.relay = !strcmp(rl, "alle") ? 2 : !strcmp(rl, "egne kanaler") ? 1 : 0;
  c.telemetry_min = uint16_t(g_store.getInt("meshtastic_telemetry_min", 0));
  snprintf(c.data_channel, sizeof(c.data_channel), "%s", setting("meshtastic_data_channel", ""));
  // Slot = (2 + 0,5) symboler CAD + 7,6 ms udbredelse/omstilling (docs/meshtastic_protokol.md 9).
  const LoRaProfile mtp = meshtasticFromSettings();
  const float tsym_ms = float(1u << mtp.sf) * 1000.0f / float(mtp.bw_hz ? mtp.bw_hz : 250000);
  c.slot_ms = uint16_t(2.5f * tsym_ms + 7.6f + 0.5f);
  c.battery = mtBattery;
  c.send = mtSend;
  c.position = mtPosition;
  g_mt_node.configure(c);
}

static void menuReload() { g_menu.load(); }

static void onMenuSaved(bool restart) {
  mtConfigure();
  g_ui.applySettings(uiSettingsFromStore());
  g_msgs.setDayLimits(g_store.getInt("msgs_days_channel", 0), g_store.getInt("msgs_days_dm", 0));
  g_track.setFilter(int(g_store.getInt("track_interval_s", 5)), int(g_store.getInt("track_min_m", 10)));
  advertsReschedule();
  g_wifi.applySettings();
  g_wifi.setEnabled(g_store.getInt("wifi_enabled", 0) != 0, false);
  mqttApply();
  g_gps.setTimeSync(g_store.getInt("gps_time", 1) != 0);
  g_gps.setEnabled(g_store.getInt("gps_enabled", 1) != 0);
  loadTestSettings();
  g_msgs.setLimits(g_store.getInt("msgs_per_channel", 200), g_store.getInt("msgs_per_dm", 200));
  g_msgs.setDayLimits(g_store.getInt("msgs_days_channel", 0), g_store.getInt("msgs_days_dm", 0));
  const char* layouts = g_store.get("keyboard_layouts");
  g_km.begin(layouts && *layouts ? layouts : "da,latin", g_store.mounted());
  if (g_touch.present()) g_touch.begin(touchFromStore());
  if (g_mc_started) {
    g_mc_radio.setTxEnabled(g_store.getInt("meshcore_tx", 1) != 0);
    char before[32];
    snprintf(before, sizeof(before), "%s", g_mc_node.nodeName());
    const char* nm = g_store.get("meshcore_name");
    g_mc_node.setName(nm ? nm : "");
    if (strcmp(before, g_mc_node.nodeName()) != 0) {
      const bool adv = g_mc_radio.txEnabled() && g_mc_node.sendAdvert();
      dm::DLOG.printf("[MC] nodenavn nu \"%s\", advert %s\n", g_mc_node.nodeName(),
                      adv ? "sat i koe" : "ikke sendt");
    }
  }
  if (restart) g_restart_at = millis() + 2000;  // radioindstillinger læses ved opstart
}

// Alt, der bruger de hemmelige SD-filer (MeshCore-identitet og -kanaler, Meshtastic-
// kanaler, WiFi-koder). Køres i setup(), eller efter at koden er skrevet, når SD-
// krypteringen er i kode-tilstand (se storage/sd_crypt.h).
static bool g_nets_started = false;
// Kortets prikker: egen GPS-position først (kun på vores egen skærm), derefter MeshCore-kontakter
// og Meshtastic-noder med kendt position, i kontaktfarven.
static int mapMarkers(dm::MapMarker* out, int max) {
  int n = 0;
  int32_t la, lo, al;
  uint32_t age;
  if (n < max && g_gps.lastPosition(la, lo, al, age)) {
    out[n] = dm::MapMarker{};
    out[n].lat = la / 1e7;
    out[n].lon = lo / 1e7;
    out[n].kind = 0;
    const dm::GpsFix& fx = g_gps.fix();  // kurs kun i bevægelse (over 2 km/t) og med et friskt fix
    if (fx.valid && age <= 5 && fx.speed_kmh_x10 >= 20) out[n].heading = int16_t(fx.course_x10 / 10);
    ++n;
  }
  for (int i = 0; i < g_map_points.count() && n < max; ++i) {  // delte lokationer (orange)
    const dm::MapPoint& p = g_map_points.at(i);
    out[n] = dm::MapMarker{};
    out[n].lat = p.lat;
    out[n].lon = p.lon;
    out[n].kind = 3;
    out[n].color = p.kind == 1 ? 0xF81F : 0xFD20;  // waypoint magenta, delt lokation orange
    snprintf(out[n].label, sizeof(out[n].label), "%.19s", p.label);
    ++n;
  }
  if (g_mc_started) {
    static ContactInfo* list = nullptr;
    constexpr int kMax = 64;
    if (!list) list = static_cast<ContactInfo*>(heap_caps_calloc(kMax, sizeof(ContactInfo), MALLOC_CAP_SPIRAM));
    const int k = list ? g_mc_node.contactsSnapshot(list, kMax) : 0;
    for (int i = 0; i < k && n < max; ++i) {
      const ContactInfo& c = list[i];
      if (!c.gps_lat && !c.gps_lon) continue;
      out[n] = dm::MapMarker{};
      out[n].lat = c.gps_lat / 1e6;
      out[n].lon = c.gps_lon / 1e6;
      out[n].kind = 1;
      out[n].color = dm::ccolor::rgb(dm::ccolor::forKey(c.id.pub_key));
      snprintf(out[n].label, sizeof(out[n].label), "%.19s", c.name);
      ++n;
    }
  }
  for (int i = 0; i < g_mt_node.count() && n < max; ++i) {
    const dm::MtNodeEntry& e = g_mt_node.at(i);
    if (!e.has_pos || (!e.lat_e7 && !e.lon_e7)) continue;
    out[n] = dm::MapMarker{};
    out[n].lat = e.lat_e7 / 1e7;
    out[n].lon = e.lon_e7 / 1e7;
    out[n].kind = 2;
    uint8_t k6[6];  // samme farve som i Kontakt-fanen
    for (int b = 0; b < 6; ++b) k6[b] = uint8_t(e.num >> (8 * (b % 4))) ^ uint8_t(b * 37);
    out[n].color = dm::ccolor::rgb(dm::ccolor::autoFor(k6));
    char nm[40];
    g_mt_node.nameOf(e.num, nm, sizeof(nm));
    snprintf(out[n].label, sizeof(out[n].label), "%.19s", nm);
    ++n;
  }
  return n;
}

static void startNets() {
  bleMigratePin();
  if (g_nets_started) return;
  g_nets_started = true;
  if (g_ready && g_mc_on) {
    g_mc_node.start(g_store.mounted(), setting("meshcore_name", ""));
    g_mc_started = true;
    dm::DLOG.printf("[MC] node \"%s\", identitet %s, sender %s\n", g_mc_node.nodeName(),
                  g_mc_node.identityNote(), g_mc_radio.txEnabled() ? "TIL" : "FRA (kun modtagelse)");
  }
  g_chan_scan.begin(g_store.mounted(), mtPrimaryName(), &g_msgs);
  g_mt_node.begin(&g_chan_scan, &g_msgs, g_store.mounted());
  // Testbeskeden huskes på SD (så den kører videre efter sluk/tænd uden PC), men kun til en DM
  // eller privat kanal; en gammel opsætning uden samtale (før: #Public) slås fra.
  loadTestSettings();
  snprintf(g_test_conv, sizeof(g_test_conv), "%s", setting("test_conv", ""));
  if (g_store.getInt("meshcore_hourly_test", 0) != 0) {
    if (g_test_conv[0] && convIsPrivate(g_test_conv)) {
      g_hourly_test = true;
      g_next_test_ms = millis() + 60000;  // først når kalibreringen er færdig
      dm::DLOG.printf("[TEST] testbesked: TIL til %s (fra settings.ini), foerste om 1 min\n", g_test_conv);
    } else {
      g_store.setInt("meshcore_hourly_test", 0);
      g_store.save();
      dm::DLOG.println("[TEST] testbesked slaaet FRA: kun i en DM eller privat kanal (vaelg med tandhjulet)");
    }
  }
  g_ui.setMtNode(&g_mt_node);
  g_map.scan(g_store.mounted());  // kortpakker i /dualmesh/maps/
  g_map_points.load(g_store.mounted());
  // Meshtastic-waypoints på vores kanaler bliver prikker på kortet (udløbet/slettet = væk).
  g_mt_node.setWaypointFn([](const dm::MtNode::Waypoint& w) {
    const uint32_t now = dm::wallclock::now();
    if (w.expire && now && w.expire < now) g_map_points.removeWaypoint(w.id);
    else g_map_points.upsertWaypoint(w.id, w.lat, w.lon, w.name);
  });
  // Spor: en optagelse, der var i gang før genstarten, fortsætter (navnet ligger i track_rec).
  g_track.setFilter(int(g_store.getInt("track_interval_s", 5)), int(g_store.getInt("track_min_m", 10)));
  g_track.begin(g_store.mounted(), setting("track_rec", ""), [](const char* name) {
    g_store.set("track_rec", name);
    g_store.save();
  });
  g_tracks_ui.begin(&g_track, [](const char* name) {
    if (!g_track.show(name)) return;
    double la0, lo0, la1, lo1;
    if (g_track.shown().bounds(la0, lo0, la1, lo1)) g_ui.showOnMap((la0 + la1) / 2, (lo0 + lo1) / 2);
  });
  g_ui.setMap(&g_map, mapMarkers, &g_map_points, &g_track);
  g_ui.setTracksPanel(&g_tracks_ui);
  g_maps_ui.begin(&g_map, [](int i) {
    g_map.goToPack(i);
    g_ui.showOnMap(g_map.centerLat(), g_map.centerLon());
  });
  g_ui.setMapsPanel(&g_maps_ui);
  // Hent fra GitHub: efter installation/sletning genindlæses layouts, sprog eller kortpakker.
  auto dlChanged = [](dm::DownloadsUi::Kind k, const char* id, bool installed) {
    if (k == dm::DownloadsUi::Kind::Keyboards) {
      if (installed) {  // et nyt layout tages i brug med det samme (sidst i listen)
        char cur[96];
        snprintf(cur, sizeof(cur), "%s", setting("keyboard_layouts", "da,latin"));
        bool have = false;
        char tmp[96];
        snprintf(tmp, sizeof(tmp), "%s", cur);
        for (char* t = strtok(tmp, ", "); t; t = strtok(nullptr, ", ")) have |= !strcmp(t, id);
        if (!have && strlen(cur) + strlen(id) + 2 < sizeof(cur)) {
          strcat(cur, ",");
          strcat(cur, id);
          g_store.set("keyboard_layouts", cur);
          g_store.save();
        }
      }
      g_km.begin(setting("keyboard_layouts", "da,latin"), g_store.mounted());
      dm::DLOG.printf("[HENT] tastaturlayout %s %s; aktive: %s\n", id, installed ? "installeret" : "slettet",
                      g_km.loadedSummary());
    } else if (k == dm::DownloadsUi::Kind::Languages) {
      dm::lang::begin(g_store.mounted());  // vælges under Indst. > System > Sprog
      dm::DLOG.printf("[HENT] sprog %s %s\n", id, installed ? "installeret" : "slettet");
    } else {
      g_map.scan(g_store.mounted());
      dm::DLOG.printf("[HENT] kortpakke %s %s\n", id, installed ? "installeret" : "slettet");
    }
  };
  g_dl_kbd.begin(dm::DownloadsUi::Kind::Keyboards, dlChanged, dm::KeyMap::hasBuiltin);
  g_dl_lang.begin(dm::DownloadsUi::Kind::Languages, dlChanged, dm::lang::hasBuiltin);
  g_dl_maps.begin(dm::DownloadsUi::Kind::Maps, dlChanged);
  g_ota_ui.begin(dm::nvstool::launcher().found, []() { g_restart_at = millis() + 3000; });
  // Strømspar: CPU'en kører 80 MHz, mens skærmen er slukket (radio, SPI og WiFi klarer sig fint
  // med det; APB er 80 MHz uanset), og 240 MHz, når brugerfladen tegner.
  g_ui.setPowerFn([](bool screen_on) {
    if (!g_store.getInt("cpu_eco", 1)) return;
    const uint32_t mhz = (screen_on || g_sos) ? 240 : 80;  // SOS: altid fuld fart
    if (getCpuFrequencyMhz() == mhz) return;
    setCpuFrequencyMhz(mhz);
    dm::DLOG.printf("[STROEM] CPU %lu MHz (skaerm %s)\n", (unsigned long)getCpuFrequencyMhz(),
                    screen_on ? "taendt" : "slukket");
  });
  {  // geocaches (GPX-filer på SD)
    const uint32_t t0 = millis();
    g_caches.load(g_store.mounted());
    dm::DLOG.printf("[CACHE] indlaest paa %lu ms\n", (unsigned long)(millis() - t0));
  }
  g_caches_ui.begin(
      &g_caches,
      [](double& lat, double& lon) {  // GPS-position, ellers kortets midte
        int32_t la, lo, al;
        uint32_t age;
        if (g_gps.lastPosition(la, lo, al, age) && age < 120) {
          lat = la / 1e7, lon = lo / 1e7;
          return true;
        }
        lat = g_map.centerLat(), lon = g_map.centerLon();
        return false;
      },
      [](double lat, double lon) { g_ui.showOnMap(lat, lon); },
      []() { return onMenuAction("!okapi_fetch"); });
  g_ui.setCaches(&g_caches, &g_caches_ui);
  mtConfigure();
  g_chan_ui.begin(&g_chan_scan, g_mc_started ? &g_mc_node : nullptr, &g_msgs,
                  AppUi::openChannelCb, &g_ui);
  g_chan_ui.setNets(g_mc_on, g_mt_on);
  g_chan_ui.setRemovedFn(AppUi::channelRemovedCb);
  g_ui.setChannelsUi(&g_chan_ui);
  g_ui.setChannelScan(&g_chan_scan);
  g_mc_node.setPositionFn(mcAdvertPosition);
  g_mc_node.setTelemetryFn(mcTelemetry);
  g_wifi.begin(&g_store);
  g_mqtt.begin(mqttTx, mqttStatus);
  g_mqtt_ui.begin(&g_mqtt);
  mqttApply();
  g_msgs.setPushHook([](const dm::Msg& m) {
    g_mqtt.onMessage(m);
    // Testbeskeden stopper, når en anden skriver i testsamtalen.
    if (g_hourly_test && (m.kind == dm::Msg::ChannelIn || m.kind == dm::Msg::DirectIn) && !strcmp(m.conv, g_test_conv))
      g_test_reply = true;
  });
  // Bluetooth til én app ad gangen: MeshCore-appen eller Meshtastic-appen (Indst. > Bluetooth).
  const bool ble_wanted = g_store.getInt("ble_enabled", 0) != 0;
  const bool mt_app = !strcmp(setting("ble_app", "MeshCore"), "Meshtastic");
  const bool ble_on = ble_wanted && !mt_app && g_mc_started;
  if (ble_on) startBle();
  if (ble_wanted && mt_app && g_mt_on) startMtApp();
  dm::DLOG.printf("[BLE] bluetooth %s, app %s\n", ble_wanted ? "til" : "fra", setting("ble_app", "MeshCore"));
  g_ble_ui.begin(&g_ble, &g_companion, ble_on);
  g_ble_ui.setWanted(ble_wanted, mt_app ? "Meshtastic" : "MeshCore");
  advertsReschedule();
}

static void onCryptoUnlocked() {
  startNets();
  mqttApply();  // kodens fil kunne først læses nu
}
static bool resetFromMenu() {
  dm::DLOG.flush(true);
  const bool ok = g_store.resetDualMesh();
  for (auto& p : g_saved_peak) p = 0;
  return ok;
}
// Efter profil-import: genstart straks (så intet i RAM overskriver de nye filer).
// Efter eksport (delay > 0) laves den nye identitet ved opstarten.
static void restartAfterImport(uint32_t delay_ms) {
  if (delay_ms) {
    g_restart_at = millis() + delay_ms;
    return;
  }
  dm::DLOG.println("[PROFIL] profil skiftet - genstarter");
  dm::DLOG.flush(true);
  delay(100);
  esp_restart();
}
static void restartSoon(const char* why) {
  dm::DLOG.printf("[NVS] %s - genstarter om 2 s\n", why);
  g_restart_at = millis() + 2000;
}
static void onCryptoMode(sdcrypt::Mode m) {
  g_store.set("sd_crypto", sdcrypt::modeName(m));
  g_store.save();
}

void setup() {
  Serial.begin(115200);
  pinMode(pin::POWER_ON, OUTPUT);
  digitalWrite(pin::POWER_ON, HIGH);
  pinMode(pin::TFT_CS, OUTPUT);
  digitalWrite(pin::TFT_CS, HIGH);
  pinMode(pin::SD_CS, OUTPUT);
  digitalWrite(pin::SD_CS, HIGH);
  pinMode(pin::RADIO_CS, OUTPUT);  // ellers kan radioen svare på MISO under SD-init
  digitalWrite(pin::RADIO_CS, HIGH);
  delay(50);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = pin::MOSI;
  bus.miso_io_num = pin::MISO;
  bus.sclk_io_num = pin::SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 16384;
  const esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  g_spi_ok = err == ESP_OK;

  // Skærmen og opstartslogoet først (brugerens DualMesh-logo i fuld skærm, indtil
  // brugerfladen tegner) - før ventetiden på USB-terminalen, så skærmen ikke er sort.
  if (g_spi_ok) {
    g_disp_ok = g_disp.begin();
    if (g_disp_ok) {
      g_disp.pushRect565(0, 0, DUALMESH_SPLASH_W - 1, DUALMESH_SPLASH_H - 1, dualmesh_splash,
                         size_t(DUALMESH_SPLASH_W) * DUALMESH_SPLASH_H);
      g_splash_ms = millis();
    }
  }

  // Lager-tilstand (SD-kort som USB-drev): bestilt før genstarten. Intet andet startes, så kun
  // PC'en skriver på kortet; T-Deck'en genstarter normalt, når drevet skubbes ud.
  if (dm::usbdisk::takeRequest() && g_spi_ok)
    dm::usbdisk::run(SPI2_HOST, pin::SD_CS, 20000, g_disp_ok ? &g_disp : nullptr, 0 /* trackball-klik */);

  // Giv en USB-terminal op til 3 s til at åbne, så opstartsloggen kan læses.
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(200);
  dm::DLOG.println("\n=== DualMesh fase 1 radiotest ===");
  dm::DLOG.printf("[HW] SPI-bus init: %s\n", g_spi_ok ? "OK" : esp_err_to_name(err));
  dm::DLOG.printf("[HW] Skaerm init: %s\n", g_disp_ok ? "OK" : "FEJL");
  if (g_splash_ms) dm::DLOG.printf("[UI] opstartslogo vist ved %lu ms oppetid\n", (unsigned long)g_splash_ms);

  // SD-kortet før radio-tasken: indstillinger skal være læst, inden radioen starter.
  if (g_spi_ok) {
    const uint32_t t0 = millis();
    const SdState sd = g_store.begin(SPI2_HOST, pin::SD_CS);
    if (sd == SdState::Ready) {
      const bool had = g_store.load();
      // Standard = det mest udbredte i Danmark (15. nov 2026):
      //  Meshtastic: EU_868 + LongFast (Meshtastics regionstabel: Danmark = EU_868;
      //    dansk vejledning zyberdata.dk/meshtastic-denmark peger på LongFast).
      //  MeshCore: "EU/UK (Narrow)" 869,618 MHz / 62,5 kHz / SF8 / CR8 (meshguide.dk,
      //    meshview.dk, brugtgrej.dk/meshcore).
      defaultSetting("meshtastic_region", "EU_868");
      defaultSetting("meshtastic_preset", "LONG_FAST");
      defaultSetting("meshtastic_channel", "");
      defaultSetting("meshtastic_slot", "0");
      defaultSetting("meshcore_preset", "EU/UK (Narrow)");
      defaultSetting("meshcore_custom", "");
      defaultSetting("meshcore_name", "");
      defaultSetting("meshtastic_short_name", "");
      defaultSetting("trackball_deadzone", "3");
      defaultSetting("meshcore_test_text", "");
      defaultSetting("meshcore_test_interval_min", "15");
      defaultSetting("meshcore_tx", "1");
      defaultSetting("trackball_sensitivity", "5");
      defaultSetting("trackball_invert_x", "0");
      defaultSetting("trackball_invert_y", "0");
      defaultSetting("trackball_click_delay_ms", "150");
      defaultSetting("keyboard_layouts", "da,latin");
      defaultSetting("keyboard_raw_mode", "1");
      defaultSetting("keyboard_backlight", "127");
      defaultSetting("keyboard_backlight_timeout_s", "10");
      defaultSetting("screen_timeout_s", "20");
      defaultSetting("screen_timeout_usb", "0");
      defaultSetting("screen_brightness", "16");
      defaultSetting("msgs_per_channel", "200");
      defaultSetting("msgs_per_dm", "200");
      defaultSetting("touch_swap_xy", "1");
      defaultSetting("touch_invert_x", "0");
      defaultSetting("touch_invert_y", "1");
      // POSIX-tidszone til visning af beskedtider (standard: Danmark, CET/CEST).
      defaultSetting("timezone", "CET-1CEST,M3.5.0,M10.5.0/3");
      defaultSetting("wifi_enabled", "0");
      defaultSetting("wifi_ntp", "1");
      defaultSetting("ntp_server", "");
      defaultSetting("gps_enabled", "1");
      defaultSetting("gps_time", "1");
      defaultSetting("meshcore_share_position", "fra");
      defaultSetting("meshtastic_share_position", "fra");
      defaultSetting("meshtastic_tx", "1");
      defaultSetting("meshtastic_position_min", "30");
      defaultSetting("meshtastic_nodeinfo_h", "3");
      defaultSetting("meshtastic_relay", "fra");
      defaultSetting("meshtastic_tx_power", "22");
      defaultSetting("meshcore_tx_power", "22");
      defaultSetting("meshtastic_telemetry_min", "0");
      defaultSetting("sos_interval_min", "5");
      defaultSetting("language", "English");  // standard: engelsk (ønske 22. nov 2026)
      defaultSetting("track_interval_s", "5");
      defaultSetting("map_sensitivity", "5");
      defaultSetting("cpu_eco", "1");
      defaultSetting("gps_eco", "1");
      defaultSetting("track_min_m", "10");
      defaultSetting("ble_enabled", "0");
      defaultSetting("sd_crypto", "chip-ID");
      defaultSetting("meshcore_advert_local_min", "0");
      defaultSetting("meshcore_enabled", "1");
      defaultSetting("meshcore_telemetry", "nej");
      defaultSetting("meshcore_telemetry_pos", "nej");
      defaultSetting("meshcore_telemetry_env", "nej");
      defaultSetting("meshtastic_enabled", "1");
      defaultSetting("meshcore_advert_flood_h", "12");
      defaultSetting("msgs_days_channel", "0");
      defaultSetting("msgs_days_dm", "0");
      g_store.save();
      dm::DLOG.printf("[SD] monteret%s, indstillinger %s (%lu ms)\n",
                    g_store.autoFormatted() ? " (tomt kort blev formateret som FAT)" : "",
                    had ? "indlaest" : "ikke fundet - starter med standard",
                    (unsigned long)(millis() - t0));
    } else if (sd == SdState::Unsupported) {
      dm::DLOG.printf("[SD] kortet har filsystemet '%s', som DualMesh ikke kan bruge. Indstillinger "
                    "gemmes ikke.\n[SD] Tast F i monitoren for at formatere kortet som FAT "
                    "(SLETTER ALT paa kortet).\n", g_store.foundFs());
      if (*g_store.diag()) dm::DLOG.printf("[SD] diagnose: %s\n", g_store.diag());
    } else {
      dm::DLOG.printf("[SD] %s (%s) - indstillinger gemmes ikke\n", g_store.status(),
                    esp_err_to_name(g_store.lastError()));
    }
  }
  startSdLog();
  dm::crashinfo::logAtBoot(dm::DLOG);  // [CRASH]-resumé, hvis forrige kørsel gik ned
  {  // Hvilken app-partition kører vi fra (OTA skriver i den anden)? Godkend den (rollback, hvis slået til).
    const esp_partition_t* run = esp_ota_get_running_partition();
    esp_ota_mark_app_valid_cancel_rollback();
    dm::DLOG.printf("[OTA] koerer fra %s%s\n", run ? run->label : "?",
                    dm::nvstool::launcher().found ? " (launcher fundet: opdateringer gemmes paa SD)" : "");
  }
  {
    const char* tz = setting("timezone", "");
    setenv("TZ", *tz ? tz : "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
  }

  g_mc_on = g_store.getInt("meshcore_enabled", 1) != 0;
  g_mt_on = g_store.getInt("meshtastic_enabled", 1) != 0;
  if (!g_mc_on && !g_mt_on) {  // mindst ét net (menuen tillader det ikke, men settings.ini kan rettes i hånden)
    g_mc_on = g_mt_on = true;
    dm::DLOG.println("[RADIO] begge net var slaaet fra i settings.ini - slaar begge til");
  }
  g_log = xQueueCreate(16, sizeof(LogPkt));
  g_mt_txq = xQueueCreate(4, sizeof(MtTxFrame));
  wallclock::setRtc(&g_mc_rtc);
  xTaskCreatePinnedToCore(radioTask, "radio", 8192, nullptr, configMAX_PRIORITIES - 2,
                          nullptr, 0);
  const uint32_t t1 = millis();
  while (!g_ready && millis() - t1 < 5000) delay(10);
  printPlan();
  {
    sdcrypt::Mode cm = sdcrypt::Chip;  // standard: chip-ID (intet uomvendeligt, ingen kode)
    sdcrypt::modeFromName(setting("sd_crypto", "chip-ID"), cm);
    if (g_store.mounted()) sdcrypt::init(cm);
  }
  if (g_store.mounted()) loadMcPresetsFromSd();  // før radioen og menuen slår presets op
  g_msgs.begin(100);
  g_msgs.setSd(g_store.mounted());
  g_msgs.setLimits(g_store.getInt("msgs_per_channel", 200), g_store.getInt("msgs_per_dm", 200));
  g_msgs.setDayLimits(g_store.getInt("msgs_days_channel", 0), g_store.getInt("msgs_days_dm", 0));
  g_mc_node.setStore(&g_msgs);
  dm::DLOG.println("Taster: s = statistik, p = plan, h = hjaelp");

  // UI (fase 2 trin 3): tastatur + trackball + tekstskærme.
  g_tb.begin();
  const bool kb = g_kb.begin(18, 8, 0x55, g_store.getInt("keyboard_raw_mode", 1) != 0);
  dm::DLOG.printf("[UI] tastatur %s, trackball klar\n", kb ? "fundet (I2C 0x55)" : "IKKE fundet endnu");
  const AppUi::Settings us = uiSettingsFromStore();
  g_kb.setBacklight(0);  // slukket, til der er aktivitet
  g_km.begin(setting("keyboard_layouts", "da,latin"), g_store.mounted());
  dm::lang::begin(g_store.mounted());  // brugerfladens sprog (før noget tegnes)
  dm::lang::apply(setting("language", "English"));
  const bool touch = g_touch.begin(touchFromStore());
  dm::DLOG.printf("[UI] tastaturlayouts: %s; touch %s\n", g_km.loadedSummary(),
                touch ? "fundet (GT911)" : "IKKE fundet");
  if (g_disp_ok)
    g_ui.begin(&g_disp, &g_mc_node, &g_mc_radio, &g_msgs, &g_kb, &g_tb, statusView, us, &g_km,
               touch ? &g_touch : nullptr);
  // Logoet i mindst 5 s: synligt i videoer om firmwaren, men ikke længere end at det
  // ikke generer (brugerens ønske 16. nov 2026).
  if (g_splash_ms) g_ui.holdSplashUntil(g_splash_ms + 5000);
  g_menu.begin(&g_store, onMenuSaved, onMenuAction, onMenuCheck);
  g_menu.setSecretFn(mqttSecret);
  g_ui.setSettingsUi(&g_menu);
  g_gps.begin(g_store.getInt("gps_enabled", 1) != 0, g_store.getInt("gps_time", 1) != 0);
  g_gps_ui.begin(&g_gps, &g_store, sosStatusText);
  g_ui.setSos(sosActive, sosSet);
  {
    dm::AppUi::TestFns tf;  // testbeskeden slås til/fra fra tandhjulsmenuen
    tf.on = testOnConv;
    tf.toggle = testToggleConv;
    tf.priv = convIsPrivate;
    g_ui.setTestFns(tf);
  }
  g_wifi_ui.begin(&g_wifi);
  g_crypto_ui.begin(onCryptoUnlocked, onCryptoMode);
  g_nvs_ui.begin(restartSoon);
  g_colors_ui.begin(&g_mc_node);
  g_reset_ui.begin(&g_store, resetFromMenu, restartAfterImport);
  g_perms_ui.begin(&g_mc_node);
  g_report_ui.begin("T-Deck Plus", resetReasonText());
  dm::ccolor::load(g_store.mounted());
  g_profile_ui.begin(&g_store, restartAfterImport);
  {
    const dm::nvstool::Launcher& l = dm::nvstool::launcher();
    dm::DLOG.printf("[NVS] koerer fra partition %s; launcher: %s\n", l.running, l.found ? l.name : "ingen");
  }
  if (g_store.mounted() && sdcrypt::locked() && sdcrypt::mode() == sdcrypt::Code) {
    dm::DLOG.println("[KRYPTO] SD-data er laast med kode - MeshCore og WiFi starter efter oplaasning");
    if (g_disp_ok) g_ui.openPanel(&g_crypto_ui);
  } else {
    startNets();
  }
  dm::DLOG.printf("[MEM] intern RAM fri %lu KB (stoerste blok %lu KB), PSRAM fri %lu KB%c",
                  (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), 10);
  {  // Intern NVS: kun læsning (Bluetooth gemmer parringsnøgler dér)
    nvs_stats_t st;
    if (nvs_get_stats(nullptr, &st) == ESP_OK)
      dm::DLOG.printf("[NVS] intern NVS: %u af %u poster brugt, %u fri, %u navnerum%c",
                      unsigned(st.used_entries), unsigned(st.total_entries), unsigned(st.free_entries),
                      unsigned(st.namespace_count), 10);
  }
  if (g_store.getInt("sos_active", 0) != 0) {  // SOS var aktiv før genstart: fortsæt
    g_sos = true;
    g_sos_next_ms = millis() + 60000;  // efter kalibreringen
    dm::DLOG.println("[SOS] var aktiv foer genstart - fortsaetter om 1 min");
    sosBegin();
  }
}

void loop() {
  static uint32_t last = millis();
  {  // Opencaching: færdig hentning -> indlæs caches igen og vis beskeden
    char m[64];
    const dm::okapi::State st = dm::okapi::poll(m, sizeof(m));
    if (st == dm::okapi::State::Done || st == dm::okapi::State::Failed) {
      if (st == dm::okapi::State::Done) g_caches.load(g_store.mounted());
      g_ui.notify(m);
      dm::DLOG.printf("[OKAPI] %s\n", m);
    }
  }
  {  // GPS-strømspar: dvale, når intet har brugt GPS'en i 60 s (skærm slukket, intet spor,
     // ingen positionsdeling, ingen SOS, uret sat); vækkes straks, når noget skal bruge den.
    static uint32_t gps_ms = 0, unused_since = 0;
    const uint32_t now = millis();
    // SOS har prioritet over alt (krav 22. nov 2026): ingen GPS-dvale og fuld CPU-fart under SOS.
    if (g_sos && getCpuFrequencyMhz() != 240) {
      setCpuFrequencyMhz(240);
      dm::DLOG.println("[STROEM] CPU 240 MHz (SOS)");
    }
    if (g_sos && g_gps.sleeping()) g_gps.setSleep(false);
    if (now - gps_ms >= 1000 && g_gps.enabled()) {
      gps_ms = now;
      auto shared = [](const char* k) { return strcmp(setting(k, "fra"), "fra") != 0; };
      const bool wanted = g_sos || !g_store.getInt("gps_eco", 1) || !g_ui.screenIsOff() || g_track.recording() ||
                          shared("meshcore_share_position") || shared("meshtastic_share_position") ||
                          shared("mqtt_share_position") || (g_store.getInt("gps_time", 1) && !dm::wallclock::valid());
      if (wanted) {
        unused_since = 0;
        if (g_gps.sleeping()) g_gps.setSleep(false);
      } else if (!unused_since) {
        unused_since = now ? now : 1;
      } else if (!g_gps.sleeping() && now - unused_since >= 60000) {
        g_gps.setSleep(true);
      }
    }
  }
  {  // spor: en GPS-position pr. sekund til optagelsen (den filtrerer selv på tid og afstand)
    static uint32_t trk_ms = 0;
    const uint32_t now = millis();
    if (g_track.recording() && now - trk_ms >= 1000) {
      trk_ms = now;
      int32_t la, lo, al;
      uint32_t age;
      const bool fix = g_gps.lastPosition(la, lo, al, age) && age <= 3;
      g_track.tick(fix, la / 1e7, lo / 1e7, al, uint32_t(time(nullptr)), now);
    }
  }
  LogPkt l;
  while (g_log && xQueueReceive(g_log, &l, 0) == pdTRUE) printPacket(l);
  if (Serial.available()) {
    const int c = Serial.read();
    // Kode-tilstand: indtil koden er skrevet på T-Deck'en, kan USB kun vise statistik og
    // hjælp (ingen logfiler, indstillinger, nulstilling eller formatering).
    const bool usb_locked = dm::sdcrypt::mode() == dm::sdcrypt::Code && dm::sdcrypt::locked() &&
                            c != 's' && c != 'p' && c != 'h' && c != '\r' && c != '\n';
    if (usb_locked) {
      static uint32_t told = 0;
      if (!told || millis() - told > 5000) {
        told = millis() ? millis() : 1;
        dm::DLOG.println("[KRYPTO] monitoren er laast, indtil koden er skrevet paa T-Deck'en");
      }
    } else if (handleConfirm(c)) {
      // del af en bekræftelse (JA/FORMATER)
    } else if (c == 'W' || c == 'w') {
      // Testwaypoint (opdigtet punkt: Rådhuspladsen i København) KUN på den private kanal DMtest
      // (aldrig den offentlige primære); w sletter det igen (samme id, udløbet).
      static uint32_t last_id = 0;
      int ch = -1;
      for (int i = 0; i < g_chan_scan.mtChannelCount(); ++i)
        if (!strcasecmp(g_chan_scan.mtChannel(i).name, "DMtest")) ch = i;
      if (c == 'w' && !last_id) ch = -1;  // intet at slette
      uint32_t id = c == 'w' ? last_id : 0;
      const bool ok = ch >= 0 && g_mt_node.sendWaypoint(ch, 55.67567, 12.56756, "DualMesh test-wp", "test fra monitoren",
                                                        id, c == 'w' ? 1u : 0u);
      if (ok && c == 'W') last_id = id;
      if (ok && c == 'w') last_id = 0;
      dm::DLOG.printf("[MT] testwaypoint %s %s (id %lu)\n", c == 'w' ? "sletning" : "", ok ? "sendt" : "IKKE sendt (DMtest?)",
                      (unsigned long)id);
    } else if (c == 'G') {  // test af hentning fra GitHub (katalog + version.txt), resultat i loggen
      g_dl_test = 1;
      char url[160];
      snprintf(url, sizeof(url), "%skeyboards/index.txt", dm::dl::kRaw);
      if (!dm::dl::startText(url)) {
        g_dl_test = 0;
        dm::DLOG.println("[HENT] test: kunne ikke starte (WiFi? anden hentning?)");
      }
    } else if (c == 'J') {  // gemte spor (som listen i kortets menu)
      static char names[40][28];
      const int n = g_track.list(names, 40);
      for (int i = 0; i < n; ++i) dm::DLOG.printf("[SPOR]   %s\n", names[i]);
    } else if (c == 's') printStats();
    else if (c == 'p') printPlan();
    else if (c == 'l') listPresets();
    else if (c == 'm') printMeshCore();
    else if (c == 'a' && g_mc_started) {
      const bool ok = g_mc_radio.txEnabled() && g_mc_node.sendAdvert();
      dm::DLOG.printf("[MC] advert (position efter \"Del position\") %s\n", ok ? "sat i koe" : "IKKE sendt (afsendelse FRA?)");
    } else if (c == 'c' && g_mc_started) {
      if (g_mc_radio.txEnabled()) askConfirm(Confirm::PublicText);
      else dm::DLOG.println("[MC] afsendelse er FRA (meshcore_tx=0)");
    }
    else if (c == 'T') {
      if (g_hourly_test || g_test_conv[0]) testToggleConv(g_test_conv);
      else dm::DLOG.println("[TEST] ingen samtale valgt: slaa testbeskeden til med tandhjulet i en DM eller privat kanal");
    }
    else if (c == 'D') dm::DLOG.dumpToSerial();
    else if (c == 'd') dm::DLOG.dumpToSerial(3);  // kun de 3 nyeste (fejlrapport-siden)
    else if (c == 'I') printReportHeader();       // fejlrapport-siden: version, opstart, nedbrud
    else if (c == 'k') g_chan_scan.dump();
    else if (c == 'M') g_msgs.selfTest();
    else if (c == 'X') g_mt_node.selfTest();  // Meshtastic-afkodning (konstruerede pakker)
    else if (c == 'P') {  // profil-selvtest: eksportér, læs tilbage, slet testfilen (importerer ikke)
      char path[96], err[48];
      dm::profile_io::Info info;
      const bool e = dm::profile_io::exportTo(&g_store, "selvtest123", path, sizeof(path), err, sizeof(err));
      const bool r = e && dm::profile_io::peek(path, "selvtest123", info, err, sizeof(err));
      const bool w = e && !dm::profile_io::peek(path, "forkert-kode", info, err, sizeof(err));
      if (e && r) dm::profile_io::peek(path, "selvtest123", info, err, sizeof(err));
      dm::sdcrypt::pbkdfSelfTest();
      dm::DLOG.printf("[PROFIL] selvtest: eksport %s, laes %s, forkert kode afvist %s; navn \"%s\", "
                      "identitet %s, %d filer, %d WiFi\n", e ? "OK" : "FEJL", r ? "OK" : "FEJL",
                      w ? "OK" : "FEJL", info.name, info.identity ? "ja" : "nej", info.files, info.wifi);
      if (e) remove(path);
    }
    else if (c == 'N' && g_mc_started) askConfirm(Confirm::NodeName);
    else if (c == 'C') askConfirm(Confirm::AddChannel);
    else if (c == '=') askConfirm(Confirm::SetSetting);
    else if (c == 'L') {
      dm::DLOG.flush(true);
      dm::DLOG.printf("[LOG] %s: %lu bytes skrevet, %lu tabt\n",
                    dm::DLOG.active() ? dm::DLOG.fileName() : "(ingen SD)",
                    (unsigned long)dm::DLOG.writtenBytes(), (unsigned long)dm::DLOG.droppedBytes());
    }
    else if (c == 'R') askConfirm(Confirm::ResetDualMesh);
    else if (c == 'F') askConfirm(Confirm::FormatSd);
    else if (c == 'U' && g_store.state() != SdState::NoCard) askConfirm(Confirm::UsbDisk);
    else if (c == 'h')
      dm::DLOG.println("s = statistik, p = plan, l = regioner/presets, m = MeshCore-status, "
                     "a = send MeshCore-advert, N = skift nodenavn, = = saet indstilling (noegle=vaerdi), k = vores Meshtastic-kanaler, "
                     "M = beskedlager-selvtest, c = skriv til #Public, T = testbesked til/fra i den sidst valgte samtale (vaelges med tandhjulet i en DM/privat kanal), "
                     "L = skriv log til SD nu, D = send SD-logs (tools/fetch_logs.py), R =nulstil DualMesh-data, "
                     "F = formater SD-kort, U = SD-kort som USB-drev (genstart), I = rapport-hoved, d = send de 3 nyeste SD-logs (fejlrapport-siden), "
                     "G = test hentning fra GitHub (katalog + version.txt), J = gemte spor, W/w = testwaypoint paa DMtest");
  }
  static uint32_t tx_logged = 0;
  if (g_tx_done_count != tx_logged) {
    tx_logged = g_tx_done_count;
    const SchedulerStats& st = g_sched->stats();
    dm::DLOG.printf("[TX] afsendelse faerdig (tag %lu), i alt %lu, samlet sendetid %.0f ms\n",
                  (unsigned long)g_tx_done_tag, (unsigned long)st.tx_ok, st.tx_time_us / 1000.0);
  }
  if (g_mc_started) {
    g_mc_rtc.tick();
    g_mc_node.loop();
    if (g_ble.started()) g_companion.loop();
    g_mc_node.saveContactsIfDirty();
  }
  // Testbeskeden: stop, så snart en anden har skrevet i samtalen; ellers send med fast interval.
  if (g_test_reply) {
    g_test_reply = false;
    if (g_hourly_test) {
      g_hourly_test = false;
      g_store.setInt("meshcore_hourly_test", 0);
      g_store.save();
      dm::DLOG.printf("[TEST] svar modtaget i %s - testbesked er STOPPET\n", g_test_conv);
    }
  }
  if (g_hourly_test && g_test_conv[0] && int32_t(millis() - g_next_test_ms) >= 0) {
    // Aldrig over 10 % airtime: perioden er mindst 10 x beskedens sendetid (og mindst 2 min).
    const uint32_t air = testAirtimeMs(g_test_text, g_mc_started ? g_mc_node.nodeName() : "DualMesh");
    const uint32_t period = g_test_period_ms > 10 * air ? g_test_period_ms : 10 * air;
    g_next_test_ms = millis() + period;
    const bool ok = sendToConv(g_test_conv, g_test_text);  // vises i Chat (svar giver mening)
    dm::DLOG.printf("[TEST] testbesked til %s \"%s\" %s (naeste om %lu s)\n", g_test_conv, g_test_text,
                    ok ? "sat i koe" : "FEJL", (unsigned long)(period / 1000));
  }
  // Tastaturets ESP32-C3 starter langsommere end os: prøv igen de første 15 s.
  static uint32_t kb_retry = 0;
  if (!g_kb.present() && millis() < 15000 && millis() - kb_retry > 2000) {
    kb_retry = millis();
    if (g_kb.begin(18, 8, 0x55, g_store.getInt("keyboard_raw_mode", 1) != 0))
      dm::DLOG.println("[UI] tastatur fundet (I2C 0x55)");
  }
  saveDetPeaksIfChanged();
  static uint32_t seen_cal = 0;
  if (g_sched && g_sched->stats().calibrations != seen_cal) {
    seen_cal = g_sched->stats().calibrations;
    printPlan();
  }
  if (millis() - last >= 10000) {
    last = millis();
    printStats();
  }
  g_wifi.tick();
  g_mqtt.tick(g_wifi.state() == WifiMgr::State::Connected);
  g_mt_node.tick(millis());
  if (g_mt_app) {
    g_mt_phone.loop();
    g_mt_ble.loop();
  }
  g_gps.tick();
  advertsTick();
  sosTick();
  static uint32_t mt_tx_logged = 0;
  if (g_mt_tx_done != mt_tx_logged) {
    mt_tx_logged = g_mt_tx_done;
    dm::DLOG.printf("[MT] pakke sendt (%lu i alt)\n", (unsigned long)mt_tx_logged);
  }
  if (g_disp_ok) g_ui.tick();  // tegner kun ændrede linjer
  dm::DLOG.flush();             // højst hvert 30. s (eller ved 16 KB)
  if (g_dl_test) {  // monitor G
    char m[80];
    const dm::dl::State s = dm::dl::poll(m, sizeof(m));
    if (s == dm::dl::State::Done || s == dm::dl::State::Failed) {
      const char* t = dm::dl::text();
      dm::DLOG.printf("[HENT] test %d: %s, %u tegn: %.120s\n", g_dl_test, m, unsigned(strlen(t)), t);
      if (g_dl_test == 1) {
        char url[160];
        snprintf(url, sizeof(url), "%sversion.txt", dm::dl::kLatest);
        g_dl_test = dm::dl::startText(url) ? 2 : 0;
      } else {
        g_dl_test = 0;
      }
    }
  }
  g_dl_kbd.tick();
  g_dl_lang.tick();
  g_dl_maps.tick();
  g_ota_ui.tick();
  if (g_restart_at && int32_t(millis() - g_restart_at) >= 0) {
    dm::DLOG.println("[MENU] genstarter (radioindstillinger, import eller firmwareopdatering)");
    dm::DLOG.flush(true);
    delay(50);
    esp_restart();
  }
  delay(5);
}
