// SPDX-License-Identifier: MIT
#include "net/wifi_mgr.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "storage/sd_crypt.h"
#include "storage/sd_log.h"
#include "storage/sd_store.h"
#include "util/wall_clock.h"

namespace dm {

namespace {
constexpr const char* kFile = "/sd/dualmesh/wifi.txt";
constexpr const char* kTmp = "/sd/dualmesh/wifi.tmp";
constexpr uint32_t kConnectTimeoutMs = 20000;
volatile bool g_ntp_synced = false;
// Fra WiFi-hændelser (kører i WiFi-tasken): seneste afbrydelsesårsag og om vi nåede at blive
// tilknyttet (så en manglende IP kan skelnes fra en afvisning).
volatile uint16_t g_reason = 0;
volatile bool g_associated = false;

void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info) {
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) g_reason = info.wifi_sta_disconnected.reason;
  else if (ev == ARDUINO_EVENT_WIFI_STA_CONNECTED) g_associated = true;
}

void onNtpSync(struct timeval*) { g_ntp_synced = true; }

void copyStr(char* out, size_t n, const char* s) { snprintf(out, n, "%s", s ? s : ""); }

void wipe(char* p, size_t n) {  // overskriv en kode i RAM (volatile, så det ikke optimeres væk)
  volatile char* v = p;
  while (n--) *v++ = 0;
}
}  // namespace

void WifiMgr::begin(SdStore* store) {
  store_ = store;
  // Gem aldrig noget i den interne NVS (brugerens regel): kun RAM, filerne er på SD.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);  // vi styrer selv genforbindelse
  WiFi.onEvent(onWifiEvent);
  load();
  applySettings();
  setEnabled(store_ && store_->getInt("wifi_enabled", 0) != 0, false);
}

void WifiMgr::applySettings() {
  if (!store_) return;
  ntp_on_ = store_->getInt("wifi_ntp", 1) != 0;
  const char* s = store_->get("ntp_server");
  copyStr(ntp_server_, sizeof(ntp_server_), s && *s ? s : "pool.ntp.org");
  if (state_ == State::Connected && ntp_on_ && !ntp_started_) startNtp();
}

void WifiMgr::setEnabled(bool on, bool persist) {
  if (persist && store_) {
    store_->setInt("wifi_enabled", on ? 1 : 0);
    store_->save();
  }
  if (on == enabled_ && (on ? state_ != State::Off : state_ == State::Off)) return;
  enabled_ = on;
  if (on) {
    WiFi.mode(WIFI_STA);
    state_ = State::Idle;
    // Første auto-forsøg om 3 s (lige efter WIFI_STA fejler et scan med -2).
    auto_left_ = kAutoTries;
    try_last_ = true;
    next_auto_ms_ = millis() + 3000;
    DLOG.println("[WIFI] slaaet til");
  } else {
    if (ntp_started_) {
      sntp_stop();
      ntp_started_ = false;
    }
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    state_ = State::Off;
    scanning_ = false;
    n_nets_ = 0;
    cur_ssid_[0] = 0;
    DLOG.println("[WIFI] slaaet fra");
  }
  ++version_;
}

// --- gemte netværk (SD) ---------------------------------------------------
void WifiMgr::load() {
  n_known_ = 0;
  FILE* f = sdcrypt::openRead(kFile);  // dekrypteret (se storage/sd_crypt.h)
  if (!f) return;
  char line[160];
  while (n_known_ < kMaxKnown && std::fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0] || line[0] == ';') continue;
    char* pass = std::strchr(line, '\t');
    char* hid = nullptr;
    if (pass) {
      *pass++ = 0;
      hid = std::strchr(pass, '\t');
      if (hid) *hid++ = 0;
    }
    Known& k = known_[n_known_++];
    copyStr(k.ssid, sizeof(k.ssid), line);
    copyStr(k.pass, sizeof(k.pass), pass);
    k.hidden = hid && *hid == '1';
  }
  sdcrypt::closeRead(f);
  DLOG.printf("[WIFI] %d gemte netvaerk\n", n_known_);
}

bool WifiMgr::save() {
  FILE* f = sdcrypt::openWrite();
  if (!f) return false;
  std::fprintf(f, "; DualMesh WiFi-netvaerk (HEMMELIG): ssid<TAB>kode<TAB>skjult\n");
  for (int i = 0; i < n_known_; ++i)
    std::fprintf(f, "%s\t%s\t%d\n", known_[i].ssid, known_[i].pass, known_[i].hidden ? 1 : 0);
  return sdcrypt::commit(f, kFile);
}

int WifiMgr::findKnown(const char* ssid) const {
  for (int i = 0; i < n_known_; ++i)
    if (std::strcmp(known_[i].ssid, ssid) == 0) return i;
  return -1;
}

bool WifiMgr::forget(const char* ssid) {
  const int i = findKnown(ssid);
  if (i < 0) return false;
  for (int k = i; k + 1 < n_known_; ++k) known_[k] = known_[k + 1];
  --n_known_;
  for (int k = 0; k < n_nets_; ++k)
    if (std::strcmp(nets_[k].ssid, ssid) == 0) nets_[k].known = false;
  if (std::strcmp(cur_ssid_, ssid) == 0) disconnect();
  save();
  ++version_;
  DLOG.printf("[WIFI] glemt: \"%s\"\n", ssid);
  return true;
}

// --- scan / forbind -------------------------------------------------------
void WifiMgr::startScan() {
  if (!enabled_ || scanning_) return;
  // Asynkront, med skjulte netværk (vises uden navn og udelades).
  // 500 ms pr. kanal giver Arduino-timeout 10 s (300 ms -> 6 s var for kort her).
  if (WiFi.scanNetworks(true, false, false, 500) == WIFI_SCAN_FAILED) {
    DLOG.println("[WIFI] scan kunne ikke starte");
    return;
  }
  scanning_ = true;
  scan_start_ms_ = millis();
  ++version_;
}

void WifiMgr::connect(const char* ssid, const char* pass, bool hidden, bool remember) {
  if (!ssid || !*ssid) return;
  if (!enabled_) setEnabled(true, true);
  // Nettet gemmes først, når forbindelsen er lykkedes (brugerens krav 17. nov 2026): en forkert
  // kode må ikke gemmes, og en tidligere gemt, rigtig kode bevares.
  pending_save_ = remember;
  if (remember) {
    copyStr(pending_.ssid, sizeof(pending_.ssid), ssid);
    copyStr(pending_.pass, sizeof(pending_.pass), pass);
    pending_.hidden = hidden;
  }
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false);
  copyStr(cur_ssid_, sizeof(cur_ssid_), ssid);
  auto_conn_ = false;
  err_[0] = 0;
  g_reason = 0;
  g_associated = false;
  WiFi.begin(ssid, pass && *pass ? pass : nullptr);
  state_ = State::Connecting;
  connect_ms_ = millis();
  ++version_;
  DLOG.printf("[WIFI] forbinder til \"%s\"%s\n", ssid, hidden ? " (skjult)" : "");
}

void WifiMgr::disconnect() {
  if (ntp_started_) {
    sntp_stop();
    ntp_started_ = false;
  }
  WiFi.disconnect(false);
  if (enabled_) state_ = State::Idle;
  cur_ssid_[0] = 0;
  auto_left_ = 0;  // brugeren afbrød: ingen automatiske forsøg
  ++version_;
  DLOG.println("[WIFI] afbrudt");
}

void WifiMgr::autoFailed(const char* why) {
  next_auto_ms_ = millis() + kAutoGapMs;
  if (auto_left_ > 0)
    DLOG.printf("[WIFI] auto-forbind: %s (forsoeg %d af %d, naeste om 10 s)\n", why,
                kAutoTries - auto_left_, kAutoTries);
  else
    DLOG.printf("[WIFI] auto-forbind: %s - stopper efter %d forsoeg (brug WiFi-skaermen)\n", why,
                kAutoTries);
}

bool WifiMgr::autoConnect() {
  if (!n_known_) return false;
  // Stærkeste gemte netværk fra seneste scan.
  int best = -1;
  for (int i = 0; i < n_nets_; ++i)
    if (nets_[i].known && (best < 0 || nets_[i].rssi > nets_[best].rssi)) best = i;
  if (best >= 0) {
    const int k = findKnown(nets_[best].ssid);
    connect(known_[k].ssid, known_[k].pass, known_[k].hidden, false);
    auto_conn_ = true;
    return true;
  }
  // Skjulte netværk ses ikke i et scan: prøv dem på skift.
  for (int n = 0; n < n_known_; ++n) {
    const int k = (hidden_try_ + n) % n_known_;
    if (known_[k].hidden) {
      hidden_try_ = k + 1;
      connect(known_[k].ssid, known_[k].pass, true, false);
      auto_conn_ = true;
      return true;
    }
  }
  return false;
}

void WifiMgr::storePending() {
  pending_save_ = false;
  int i = findKnown(pending_.ssid);
  if (i < 0 && n_known_ < kMaxKnown) i = n_known_++;
  if (i < 0) i = kMaxKnown - 1;  // fuld: erstat den sidste
  known_[i] = pending_;
  wipe(pending_.pass, sizeof(pending_.pass));
  for (int k = 0; k < n_nets_; ++k)
    if (std::strcmp(nets_[k].ssid, known_[i].ssid) == 0) nets_[k].known = true;
  save();  // rememberLast flytter det øverst bagefter (og gemmer igen, hvis nødvendigt)
  DLOG.printf("[WIFI] \"%s\" gemt (forbindelsen lykkedes)\n", known_[i].ssid);
}

void WifiMgr::rememberLast(const char* ssid) {
  const int i = findKnown(ssid);
  if (i <= 0) return;  // allerede øverst (eller ikke gemt)
  const Known k = known_[i];
  for (int j = i; j > 0; --j) known_[j] = known_[j - 1];
  known_[0] = k;
  save();
}

// ESP-IDF's årsagskoder (wifi_err_reason_t) som dansk tekst, højst 40 tegn.
void WifiMgr::failReason(char* out, size_t n) const {
  const uint16_t r = g_reason;
  const char* t = nullptr;
  switch (r) {
    case 2: case 15: case 202: case 204: t = "Forkert kode (godkendelse fejlede)"; break;
    case 201: t = "Netværket blev ikke fundet"; break;
    case 203: t = "Routeren afviste tilknytning"; break;
    case 205: t = "Forbindelsen mislykkedes"; break;
    case 17: t = "Routeren afviste (sikkerhedsindst.)"; break;
    case 23: t = "Kræver 802.1X-login (ikke understøttet)"; break;
    case 5: t = "Routeren er fuld (for mange enheder)"; break;
    case 4: case 200: t = "Intet svar fra routeren"; break;
    case 0: t = g_associated ? "Ingen IP-adresse fra routeren (DHCP)" : "Intet svar (tidsgrænse)"; break;
    default: break;
  }
  if (t) snprintf(out, n, "%s", t);
  else snprintf(out, n, "Afvist af routeren (kode %u)", unsigned(r));
}

void WifiMgr::startNtp() {
  const char* tz = store_ ? store_->get("timezone") : nullptr;
  sntp_set_time_sync_notification_cb(onNtpSync);
  configTzTime(tz && *tz ? tz : "CET-1CEST,M3.5.0,M10.5.0/3", ntp_server_);
  ntp_started_ = true;
  DLOG.printf("[WIFI] henter tid fra %s\n", ntp_server_);
}

void WifiMgr::tick() {
  if (!enabled_) return;
  const uint32_t now = millis();
  if (scanning_) {
    const int n = WiFi.scanComplete();
    if (n >= 0 || n == WIFI_SCAN_FAILED) {
      scanning_ = false;
      n_nets_ = 0;
      for (int i = 0; i < n && n_nets_ < kMaxNets; ++i) {
        const String s = WiFi.SSID(i);
        if (!s.length()) continue;
        // Samme navn fra flere access points: behold det stærkeste.
        int dup = -1;
        for (int k = 0; k < n_nets_; ++k)
          if (std::strcmp(nets_[k].ssid, s.c_str()) == 0) dup = k;
        Net& e = dup >= 0 ? nets_[dup] : nets_[n_nets_];
        if (dup >= 0 && e.rssi >= WiFi.RSSI(i)) continue;
        copyStr(e.ssid, sizeof(e.ssid), s.c_str());
        e.rssi = int8_t(WiFi.RSSI(i));
        e.channel = uint8_t(WiFi.channel(i));
        e.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        e.known = findKnown(e.ssid) >= 0;
        if (dup < 0) ++n_nets_;
      }
      // Stærkeste først.
      for (int a = 0; a < n_nets_; ++a)
        for (int b = a + 1; b < n_nets_; ++b)
          if (nets_[b].rssi > nets_[a].rssi) {
            const Net t = nets_[a];
            nets_[a] = nets_[b];
            nets_[b] = t;
          }
      WiFi.scanDelete();
      ++version_;
      DLOG.printf("[WIFI] scan: %d netvaerk (%s, %lu ms)\n", n_nets_, n < 0 ? "FEJL/timeout" : "ok",
                  (unsigned long)(millis() - scan_start_ms_));
      if (scan_for_auto_) {
        scan_for_auto_ = false;
        if (n == WIFI_SCAN_FAILED) autoFailed("scan fejlede");
        else if ((state_ == State::Idle || state_ == State::Failed) && !autoConnect())
          autoFailed("intet gemt netvaerk i naerheden");
      }
    }
  }
  const wl_status_t st = WiFi.status();
  if (state_ == State::Connecting) {
    if (st == WL_CONNECTED) {
      state_ = State::Connected;
      auto_left_ = 0;
      try_last_ = false;
      err_[0] = 0;
      if (pending_save_) storePending();
      rememberLast(cur_ssid_);
      ++version_;
      DLOG.printf("[WIFI] forbundet til \"%s\", IP %s, %d dBm\n", cur_ssid_,
                  WiFi.localIP().toString().c_str(), int(WiFi.RSSI()));
      if (ntp_on_) startNtp();
    } else if (int32_t(millis() - connect_ms_) > int32_t(kConnectTimeoutMs) || st == WL_CONNECT_FAILED) {
      // (frisk klokke: connect() kan have stemplet connect_ms_ efter `now` i dette kald)
      failReason(err_, sizeof(err_));
      DLOG.printf("[WIFI] kunne ikke forbinde til \"%s\": %s (aarsag %u, status %d)\n", cur_ssid_, err_,
                  unsigned(g_reason), int(st));
      if (pending_save_) DLOG.printf("[WIFI] \"%s\" er IKKE gemt (forbindelsen lykkedes ikke)\n", cur_ssid_);
      pending_save_ = false;
      wipe(pending_.pass, sizeof(pending_.pass));
      WiFi.disconnect(false);
      state_ = State::Failed;
      ++version_;
      if (auto_conn_) autoFailed("kunne ikke forbinde");
    }
  } else if (state_ == State::Connected && st != WL_CONNECTED) {
    DLOG.printf("[WIFI] forbindelsen til \"%s\" er tabt (aarsag %u)\n", cur_ssid_, unsigned(g_reason));
    if (ntp_started_) {
      sntp_stop();
      ntp_started_ = false;
    }
    state_ = State::Idle;
    auto_left_ = kAutoTries;  // forbindelsen tabt: nye 6 forsøg, det samme net først
    try_last_ = true;
    next_auto_ms_ = now + 5000;
    ++version_;
  }
  // Auto-forbind: scan først, så vælges det stærkeste gemte netværk.
  if ((state_ == State::Idle || state_ == State::Failed) && n_known_ && !scanning_ &&
      auto_left_ > 0 && int32_t(millis() - next_auto_ms_) >= 0) {
    --auto_left_;
    // Først det sidst forbundne net direkte (det står øverst i wifi.txt), derefter scan og
    // det stærkeste gemte net.
    if (try_last_) {
      try_last_ = false;
      DLOG.printf("[WIFI] auto-forbind: proever sidst forbundne net \"%s\" foerst\n", known_[0].ssid);
      connect(known_[0].ssid, known_[0].pass, known_[0].hidden, false);
      auto_conn_ = true;
      return;
    }
    scan_for_auto_ = true;
    startScan();
    if (!scanning_) {
      scan_for_auto_ = false;
      autoFailed("scan kunne ikke starte");
    }
  }
  if (g_ntp_synced) {
    g_ntp_synced = false;
    ntp_ms_ = now ? now : 1;
    const bool used = wallclock::set(uint32_t(time(nullptr)), wallclock::Ntp);
    DLOG.printf("[WIFI] NTP-tid modtaget%s\n", used ? "" : " (GPS-tiden har forrang)");
    ++version_;
  }
}

const char* WifiMgr::stateText() const {
  switch (state_) {
    case State::Off: return "slået fra";
    case State::Idle: return "ikke forbundet";
    case State::Connecting: return "forbinder";
    case State::Connected: return "forbundet";
    case State::Failed: return "kunne ikke forbinde";
  }
  return "";
}

int WifiMgr::rssi() const { return state_ == State::Connected ? int(WiFi.RSSI()) : 0; }

void WifiMgr::ipText(char* out, int n) const {
  if (state_ != State::Connected) {
    snprintf(out, n, "-");
    return;
  }
  snprintf(out, n, "%s", WiFi.localIP().toString().c_str());
}

}  // namespace dm
