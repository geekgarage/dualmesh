// SPDX-License-Identifier: MIT
// DualMesh – WiFi (station): status, scanning, gemte netværk, skjulte netværk og
// tid fra NTP.
//
// Gemte netværk ligger på SD i /sd/dualmesh/wifi.txt (hemmelig: "ssid<TAB>kode<TAB>skjult"
// pr. linje, det sidst forbundne øverst), ikke i den interne NVS (WiFi.persistent(false)).
// Er WiFi slået til (ved opstart, når WiFi tændes, eller når forbindelsen tabes), prøves
// først det sidst forbundne netværk direkte, derefter det stærkeste gemte netværk i
// nærheden (skjulte prøves direkte): højst 6 forsøg med 10 s imellem, så stoppes der
// (brugerens krav 15. og 17. nov 2026). Fejler et forsøg, står årsagen på dansk i
// lastError() (forkert kode, net ikke fundet, ingen IP ...). Scan/forbind i WiFi-skærmen
// virker altid.
// Tid: når der er forbindelse, hentes tiden med SNTP (configTzTime) og sættes i det
// fælles ur (wallclock, kilde NTP).
//
// Bemærk: et WiFi-beacon har kun routerens oppetid (TSF i µs), ikke et klokkeslæt,
// så tid kan ikke læses fra netværk, vi ikke er forbundet til.
// Kører i loop()-tasken.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

class SdStore;

class WifiMgr {
 public:
  enum class State : uint8_t { Off, Idle, Connecting, Connected, Failed };
  struct Net {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    bool secure;
    bool known;
  };
  struct Known {
    char ssid[33];
    char pass[65];
    bool hidden;
  };

  void begin(SdStore* store);
  void tick();
  // Slå til/fra; persist = gem i settings.ini (wifi_enabled).
  void setEnabled(bool on, bool persist);
  bool enabled() const { return enabled_; }
  void applySettings();  // wifi_ntp / ntp_server / timezone fra settings.ini

  void startScan();
  bool scanning() const { return scanning_; }
  int netCount() const { return n_nets_; }
  const Net& net(int i) const { return nets_[i]; }
  uint32_t version() const { return version_; }  // ændres ved nyt scan/status

  // Forbind (og husk netværket, hvis remember - men først når forbindelsen er lykkedes).
  // hidden = netværket sender ikke SSID.
  void connect(const char* ssid, const char* pass, bool hidden, bool remember);
  void disconnect();
  bool forget(const char* ssid);
  int knownCount() const { return n_known_; }
  const Known& known(int i) const { return known_[i]; }
  int findKnown(const char* ssid) const;

  State state() const { return state_; }
  const char* stateText() const;
  const char* ssid() const { return cur_ssid_; }
  int rssi() const;
  void ipText(char* out, int n) const;
  uint32_t lastNtpMs() const { return ntp_ms_; }  // millis() ved seneste NTP-sync (0 = aldrig)
  // Hvorfor seneste forsøg fejlede, på dansk (højst 40 tegn; tom = ingen fejl).
  const char* lastError() const { return err_; }

 private:
  void rememberLast(const char* ssid);  // flyt det forbundne net øverst i wifi.txt
  void failReason(char* out, size_t n) const;
  char err_[48] = "";
  void storePending();
  Known pending_ = {};        // net, der gemmes, når forbindelsen lykkes
  bool pending_save_ = false;
  bool try_last_ = false;  // næste auto-forsøg: det sidst forbundne net direkte
  void load();
  bool save();
  bool autoConnect();  // false = intet gemt netværk at prøve
  void startNtp();

  SdStore* store_ = nullptr;
  bool enabled_ = false, ntp_on_ = true, ntp_started_ = false;
  char ntp_server_[48] = "pool.ntp.org";
  State state_ = State::Off;
  bool scanning_ = false, scan_for_auto_ = false;
  static constexpr int kMaxNets = 24, kMaxKnown = 8;
  Net nets_[kMaxNets] = {};
  int n_nets_ = 0;
  Known known_[kMaxKnown] = {};
  int n_known_ = 0;
  char cur_ssid_[33] = "";
  uint32_t scan_start_ms_ = 0;
  uint32_t connect_ms_ = 0, next_auto_ms_ = 0, ntp_ms_ = 0, version_ = 1;
  static constexpr int kAutoTries = 6;
  static constexpr uint32_t kAutoGapMs = 10000;
  int auto_left_ = 0;  // automatiske forsøg tilbage
  bool auto_conn_ = false;  // den igangværende forbindelse er et auto-forsøg
  void autoFailed(const char* why);
  int hidden_try_ = 0;  // næste skjulte netværk at prøve ved auto-forbind
};

}  // namespace dm
