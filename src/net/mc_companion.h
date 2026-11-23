// SPDX-License-Identifier: MIT
// DualMesh – MeshCores companion-protokol over BLE, så den officielle MeshCore-app
// kan bruge T-Deck'en som radio. Rammeformater og koder er taget fra MeshCores
// examples/companion_radio (v1.18.0, MIT, Copyright (c) 2025 Scott Powell /
// rippleradios.com); implementeringen her er vores egen og dækker det, appen
// bruger til daglig:
//   DEVICE_QUERY, APP_START, kontakter (hent/tilføj/opdatér/fjern/nulstil sti),
//   DM'er og kanalbeskeder (send/modtag, kø til appen), kanaler (hent/sæt),
//   adverts, tid (hent/sæt), batteri, navn, del-position, PIN, genstart.
// Også: eksport/import af identitetsnøglen (import gemmes krypteret og giver genstart),
// auto-tilføj (vi tilføjer alle kontakter), custom vars (ingen), repeater-frekvenser
// (ingen: T-Deck'en er klient).
// Fra 17. nov 2026 også resten af den officielle companion (handlerne er tilpasset fra
// MeshCores companion_radio/MyMesh.cpp, MIT, Copyright (c) 2025 Scott Powell / rippleradios.com):
// login/logout/status hos repeatere og rumservere, binære og anonyme forespørgsler, telemetri
// fra andre noder, find sti, trace, del/eksportér/importér kontakt, advert-sti, statistik,
// signering med nodens nøgle, kanal-/rå-/kontroldata, rå pakker, tuning og fabriksnulstilling
// (= "Nulstil DualMesh": kun /dualmesh/ på SD).
// Radio: vælger appen samme radio som nu, svares OK; en anden radio gemmes i
// settings.ini (preset eller "egne radioværdier"), og T-Deck'en genstarter, fordi
// scheduleren læser radioen ved opstart. Sendeeffekt fra appen ignoreres (regionens
// grænse gælder). Repeater-tilstand afvises (T-Deck'en er klient).
// Privatliv: appen får kun vores position, hvis "Del position" (MeshCore) er slået til.
#pragma once
#include <cstddef>
#include <cstdint>

#include "net/ble_serial.h"
#include "net/meshcore_node.h"

namespace dm {

class McCompanion : public McListener {
 public:
  struct Hooks {
    uint16_t (*batt_mv)() = nullptr;
    void (*set_name)(const char* name) = nullptr;     // gem som meshcore_name
    void (*set_share)(bool on) = nullptr;             // meshcore_share_position
    bool (*share_on)() = nullptr;
    bool (*position)(double& lat, double& lon) = nullptr;  // delt position (eller false)
    // Aktuel MeshCore-radio: frekvens i kHz, båndbredde i Hz, SF, CR, effekt (dBm).
    void (*radio)(uint32_t& freq_khz, uint32_t& bw_hz, uint8_t& sf, uint8_t& cr, int8_t& dbm) = nullptr;
    void (*set_pin)(uint32_t pin) = nullptr;           // gem som ble_pin
    // Appen vælger radio (frekvens kHz, båndbredde Hz, SF, CR): 0 = som nu,
    // 1 = gemt (T-Deck'en genstarter), -1 = ugyldig.
    int (*set_radio)(uint32_t freq_khz, uint32_t bw_hz, uint8_t sf, uint8_t cr) = nullptr;
    void (*restart)(uint32_t delay_ms) = nullptr;  // fx efter import af identitet
    // Ekstra CLI-kommandoer (fx "get wifi.pwd"): true = kendt, svaret står i reply.
    bool (*run_cli)(const char* cmd, char* reply, size_t n) = nullptr;
    // GPS som MeshCores "custom var" gps (0/1): -1 = ingen GPS.
    int (*gps_get)() = nullptr;
    // Telemetri-tilstande som MeshCore: bit 0-1 svar, 2-3 position, 4-5 miljø
    // (0 = nej, 1 = kontakter med tilladelse, 2 = alle/ja).
    uint8_t (*telem_modes)() = nullptr;
    void (*set_telem_modes)(uint8_t modes) = nullptr;
    // Aktuel GPS-position til appens egen telemetri (false = intet fix).
    bool (*gps_pos)(double& lat, double& lon, double& alt_m) = nullptr;
    void (*gps_set)(bool on) = nullptr;
    // Radiostatistik for MeshCore (modtaget, sendt, fejl, støjgulv i dBm).
    void (*radio_stats)(uint32_t& recv, uint32_t& sent, uint32_t& errors, int16_t& noise) = nullptr;
    // Fabriksnulstilling fra appen: slet /dualmesh/ på SD og genstart. false = fejl.
    bool (*factory_reset)() = nullptr;
  };

  void begin(McNode* node, BleSerial* ble, const Hooks& h);
  void loop();
  bool connected() const { return ble_ && ble_->connected(); }
  int queued() const { return n_off_; }

  // McListener
  void onDmRecv(const ContactInfo& c, mesh::Packet* pkt, uint32_t ts, const char* text) override;
  void onChannelRecv(int idx, mesh::Packet* pkt, uint32_t ts, const char* text) override;
  void onAdvertRecv(const ContactInfo& c, bool is_new) override;
  void onPathUpdated(const ContactInfo& c) override;
  ContactInfo* onAckRecv(const uint8_t* data) override;
  void onResponse(const ContactInfo& c, const uint8_t* data, uint8_t len) override;
  bool onPathRecv(ContactInfo& c, const uint8_t* in_path, uint8_t in_len, const uint8_t* out_path,
                  uint8_t out_len, uint8_t extra_type, const uint8_t* extra, uint8_t extra_len) override;
  void onTrace(mesh::Packet* p, uint32_t tag, uint32_t auth, uint8_t flags, const uint8_t* snrs,
               const uint8_t* hashes, uint8_t path_len) override;
  void onControlData(mesh::Packet* p) override;
  void onRxRaw(float snr, float rssi, const uint8_t* raw, int len) override;
  void onRawData(mesh::Packet* p) override;
  void onChannelData(int idx, mesh::Packet* p, uint16_t type, const uint8_t* data, size_t len) override;

 private:
  static constexpr int kMaxOffline = 32, kAcks = 8;
  struct Frame {
    uint8_t len;
    bool chan;
    uint8_t buf[BleSerial::kMaxFrame];
  };
  struct Ack {
    uint32_t ack, sent_ms;
    ContactInfo* c;
  };
  void handle(size_t len);
  void write(const uint8_t* d, size_t n) { ble_->writeFrame(d, n); }
  void ok();
  void err(uint8_t code);
  void writeContact(uint8_t code, const ContactInfo& c);
  void queueOffline(const uint8_t* d, size_t n, bool chan);
  void tickle();

  McNode* node_ = nullptr;
  BleSerial* ble_ = nullptr;
  Hooks h_{};
  uint8_t cmd_[BleSerial::kMaxFrame + 1] = {};
  uint8_t out_[BleSerial::kMaxFrame] = {};
  Frame* off_ = nullptr;  // PSRAM
  int n_off_ = 0;
  uint8_t app_ver_ = 0;
  bool iter_on_ = false;
  ContactsIterator iter_{0};
  uint32_t iter_since_ = 0, iter_recent_ = 0;
  Ack acks_[kAcks] = {};
  int next_ack_ = 0;
  // Ventende forespørgsler (som MeshCore: én ad gangen; login/status matches på nøglens 4
  // første byte, resten på tag).
  uint32_t pending_login_ = 0, pending_status_ = 0, pending_telemetry_ = 0, pending_discovery_ = 0,
           pending_req_ = 0;
  void clearPending() { pending_login_ = pending_status_ = pending_telemetry_ = pending_discovery_ = pending_req_ = 0; }
  void sent(int result, uint32_t tag, uint32_t est_timeout);  // RESP_SENT
  bool handleExtra(uint8_t c, size_t len);  // de kommandoer, der kom til 17. nov 2026
  uint8_t* sign_ = nullptr;  // data til signering (PSRAM, højst kMaxSign)
  size_t sign_len_ = 0;
  static constexpr size_t kMaxSign = 8 * 1024;
  uint32_t rx_delay_ = 0, airtime_factor_ = 1000;  // tuning (×1000); gemmes kun i RAM
};

}  // namespace dm
