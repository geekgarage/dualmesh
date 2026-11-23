// SPDX-License-Identifier: MIT
// DualMesh – Meshtastics telefon-API (ToRadio/FromRadio), så Meshtastic-appen kan bruge T-Deck'en
// som radio. Skrevet ud fra docs/meshtastic_protokol.md afsnit 12 (clean room) med håndskrevet
// protobuf; ingen .proto-filer eller genereret kode.
//
//   want_config_id -> my_info, egen node, metadata, 8 kanaler, config (device, position, power,
//   network, display, lora, bluetooth, security), de andre noder, config_complete_id. Derefter
//   modtagne pakker. Pakker fra appen sendes via MtNode::sendFromApp; AdminMessage til vores eget
//   nodenummer (port 6) besvares her: hent ejer/kanal/config/metadata, sæt ejer, kanal (sekundære),
//   radio (region/preset/hop/effekt -> genstart), slet node, tid og genstart.
//
// Kun Meshtastic-delen: MeshCore-appen har sin egen protokol (net/mc_companion.*).
#pragma once
#include <cstddef>
#include <cstdint>

#include "net/mt_node.h"

namespace dm {

class ChannelScan;
class MtBle;

class MtPhone : public MtPhoneSink {
 public:
  struct Info {
    char long_name[40], short_name[8];
    int region, preset, slot, hop_limit, tx_power;  // protokollens enum-værdier
    bool tx_enabled, relay;
    uint32_t position_secs, nodeinfo_secs, ble_pin;
  };
  struct Hooks {
    void (*info)(Info& out) = nullptr;
    void (*set_owner)(const char* long_name, const char* short_name) = nullptr;
    // Radio fra appen: gemmes, og T-Deck'en genstarter (scheduleren læser radioen ved opstart).
    // -1 = uændret. false = ugyldig.
    bool (*set_lora)(int region, int preset, int hop_limit, int tx_power, int tx_enabled) = nullptr;
    void (*set_time)(uint32_t unix) = nullptr;
    void (*reboot)(uint32_t delay_ms) = nullptr;
    uint16_t (*batt_mv)() = nullptr;
  };
  void begin(MtNode* node, ChannelScan* chans, MtBle* ble, const Hooks& h);
  void loop();  // ToRadio fra appen
  // Næste FromRadio til appen (Bluetooth-tasken). 0 = intet.
  size_t pop(uint8_t* buf, size_t cap);

  // MtPhoneSink
  void onMeshPacket(uint32_t from, uint32_t to, uint32_t id, int chan, bool pki, uint8_t hop_limit, uint8_t hop_start,
                    bool want_ack, int8_t snr_q4, int16_t rssi, const uint8_t* data, size_t len) override;
  void onImplicitAck(uint32_t id) override;
  void onSendFailed(uint32_t id, uint32_t err) override;

 private:
  void handleToRadio(const uint8_t* p, size_t n);
  void handlePacket(const uint8_t* p, size_t n);
  void handleAdmin(uint32_t req_id, const uint8_t* p, size_t n);
  void sendConfig(uint32_t nonce);
  bool push(const uint8_t* p, size_t n);  // FromRadio i køen
  void pushQueueStatus(uint32_t packet_id);
  // FromRadio.packet fra os selv (til appen), fx et admin-svar eller en lokal Routing.
  void pushLocal(uint32_t port, uint32_t request_id, const uint8_t* payload, size_t n);
  size_t encodeChannel(uint8_t* out, size_t cap, int idx);
  size_t encodeConfig(uint8_t* out, size_t cap, int type);
  size_t encodeUser(uint8_t* out, size_t cap, const MtNodeEntry* e);
  size_t encodeMetadata(uint8_t* out, size_t cap);

  MtNode* node_ = nullptr;
  ChannelScan* chans_ = nullptr;
  MtBle* ble_ = nullptr;
  Hooks h_{};
  struct Frame {
    uint16_t len;
    uint8_t buf[400];
  };
  static constexpr int kQ = 260;  // nok til hele opstartsforløbet med 200 noder
  Frame* q_ = nullptr;            // PSRAM
  volatile int head_ = 0, tail_ = 0;
  uint32_t from_id_ = 0;  // FromRadio.id
  bool ready_ = false;     // appen har bedt om config (pakker sendes derefter)
  uint32_t app_ids_[8] = {};  // pakker fra appen med want_ack (broadcast: implicit ACK)
  int app_i_ = 0;
};

}  // namespace dm
