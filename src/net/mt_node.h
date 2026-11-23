// SPDX-License-Identifier: MIT
// DualMesh – Meshtastic-node (fase 3, trin 1: modtagelse). Skrevet ud fra den offentlige
// protokolbeskrivelse med en håndskrevet protobuf-læser; ingen .proto-filer eller genereret
// kode (licensreglerne i CLAUDE.md).
//
// Hver modtagen Meshtastic-pakke (16 byte header + krypteret Data) dekrypteres med vores
// kanaler (AES-CTR, kanalens PSK), og Data afkodes:
//   port 1  TEXT_MESSAGE_APP  -> Chat (kanal) eller DM (dest = os)
//   port 3  POSITION_APP      -> nodens position
//   port 4  NODEINFO_APP      -> navn, kort navn, hardware, rolle, offentlig nøgle
//   port 5  ROUTING_APP       -> ACK/NAK (logges; bruges til DM-status i trin 3)
//   port 67 TELEMETRY_APP     -> batteri/spænding (DeviceMetrics)
// Nodedatabasen (højst kMax noder, PSRAM) gemmes på SD i /sd/dualmesh/meshtastic/nodes.txt
// (tab-separeret; ikke hemmelig - offentlige nøgler og navne), højst hvert minut ved ændring.
// Ukendte felter springes over, så nye Meshtastic-versioner ikke ødelægger noget.
#pragma once
#include <cstddef>
#include <cstdint>

#include "net/mt_tx.h"

namespace dm {

class ChannelScan;
class MsgStore;

struct MtNodeEntry {
  uint32_t num;          // nodenummer (!xxxxxxxx)
  char long_name[40];
  char short_name[8];
  uint8_t hw;            // HardwareModel
  uint8_t role;          // Config.DeviceConfig.Role
  bool has_key;
  uint8_t pubkey[32];    // X25519 (til PKI-DM'er i trin 3)
  bool has_pos;
  int32_t lat_e7, lon_e7, alt_m;
  uint32_t pos_ts;       // positionens tid (unix, 0 = ukendt)
  uint32_t last_ts;      // sidst hørt (unix, 0 = uret var ikke sat)
  uint32_t last_ms;      // sidst hørt (millis, 0 = hentet fra SD)
  int16_t rssi;
  int8_t snr_q4;
  uint8_t hops;          // hop_start - hop_limit ved seneste pakke
  uint8_t batt;          // batteri % (0 = ukendt, 101 = strøm)
  uint16_t mv;           // spænding i mV (0 = ukendt)
  uint32_t pkts;
};

// Meshtastic-appen (telefon-API, net/mt_phone.*) får de pakker, vi modtager, og besked om
// leveringsstatus for de pakker, den selv har sendt.
class MtPhoneSink {
 public:
  virtual ~MtPhoneSink() = default;
  // En afkodet pakke: data = Data-protobuf'en (klartekst).
  virtual void onMeshPacket(uint32_t from, uint32_t to, uint32_t id, int chan, bool pki, uint8_t hop_limit,
                            uint8_t hop_start, bool want_ack, int8_t snr_q4, int16_t rssi, const uint8_t* data,
                            size_t len) = 0;
  virtual void onImplicitAck(uint32_t id) = 0;               // vores pakke hørt sendt videre
  virtual void onSendFailed(uint32_t id, uint32_t err) = 0;  // opgivet (fx 5 = MAX_RETRANSMIT)
};

class MtNode {
 public:
  static constexpr int kMax = 200;

  // Afsendelse (trin 2). send = læg en færdig pakke i radioens kø; position = egen position
  // efter "Del position" for Meshtastic (false = deles ikke; precision 32 = præcis).
  struct Config {
    bool tx = true;                 // "Må sende"
    char long_name[40] = "";
    char short_name[8] = "";
    uint8_t hop_limit = 3;
    uint16_t nodeinfo_h = 3;        // NodeInfo-interval (timer, 0 = kun ved start/forespørgsel)
    uint16_t position_min = 30;     // positions-interval, når deling er slået til (0 = aldrig)
    // Videresendelse (trin 4): 0 = fra (rolle CLIENT_MUTE), 1 = kun pakker på vores kanaler og
    // PKI-DM'er mellem kendte noder, 2 = alle (rolle CLIENT i begge tilfælde).
    uint8_t relay = 0;
    uint16_t slot_ms = 28;          // konkurrencevinduets slot (afhænger af SF/BW)
    uint16_t telemetry_min = 0;     // egen telemetri (batteri/spænding/oppetid); 0 = fra
    // Kanal for de faste udsendelser af position og telemetri (navn; tom = den primære).
    // Findes kanalen ikke, sendes intet (aldrig i stilhed på den offentlige primære).
    char data_channel[24] = "";
    bool (*send)(const uint8_t* pkt, size_t len) = nullptr;
    // Batteri: level 0-100 (101 = strømforsyning) og spænding i volt; false = ukendt.
    bool (*battery)(uint8_t& level, float& volts) = nullptr;
    bool (*position)(int32_t& lat_e7, int32_t& lon_e7, int32_t& alt_m, uint32_t& precision) = nullptr;
  };
  void configure(const Config& c) { cfg_ = c; }
  // Tekst på vores kanal nr. chan (0 = primær). Lægges i Chat som "sendt" og bliver
  // "leveret", når en anden node sender den videre (som i Meshtastic).
  bool sendText(int chan, const char* text);
  // chan: vores kanal (indeks), svaret sendes på; standard = den primære. Svar på en forespørgsel
  // går tilbage på den kanal, den kom fra (som Meshtastic).
  bool sendNodeInfo(uint32_t to = 0xFFFFFFFFu, uint32_t request_id = 0, bool want_response = false, int chan = 0);
  bool sendPosition(uint32_t to = 0xFFFFFFFFu, uint32_t request_id = 0, int chan = 0);
  bool sendTelemetry(uint32_t to = 0xFFFFFFFFu, uint32_t request_id = 0, int chan = 0);
  // Telemetry-payload (tid + DeviceMetrics) i pl (mindst 64 byte); bruges også til appen lokalt.
  size_t telemetryPayload(uint8_t* pl, uint8_t& level, float& volts);
  // PKI-krypteret DM (trin 3) med want_ack og op til 3 forsøg. Status i Chat: sendt ->
  // leveret (ACK fra modtageren) eller FEJL. Returnerer 1 = sendt, 0 = fejl, -1 = modtagerens
  // nøgle kendes ikke endnu (vi har bedt om dens NodeInfo).
  int sendDm(uint32_t to, const char* text);
  void setPhone(MtPhoneSink* p) { phone_ = p; }
  // En pakke fra appen: data = Data-protobuf'en. chan = kanalindeks. PKI bruges til DM'er, når
  // modtagerens nøgle kendes (ikke NodeInfo/Position/Routing/Traceroute). want_ack på en DM giver
  // gensendelse som vores egne. Returnerer false, hvis den ikke kunne sendes.
  bool sendFromApp(uint32_t to, int chan, const uint8_t* data, size_t len, bool want_ack, uint32_t id,
                   uint8_t hop_limit);
  const Config& config() const { return cfg_; }
  // Traceroute til en node (docs/meshtastic_protokol.md afsnit 11). Resultatet lægges i
  // DM-samtalen med noden og logges. false = kunne ikke sendes.
  bool sendTraceroute(uint32_t to);
  // Bed én node om position (3), telemetri (67) eller NodeInfo (4); svaret skrives i DM'en.
  bool requestInfo(uint32_t to, uint32_t port);
  // Waypoint (port 8, docs/meshtastic_protokol.md afsnit 13): send et punkt til alle på kanal nr.
  // chan (id 0 = nyt tilfældigt id; returneres i id). Modtagne waypoints gives til WaypointFn
  // (expire i fortiden = slet).
  // expire: unix-tid, hvor punktet udløber (0 = aldrig, 1 = slet det med dette id hos andre).
  bool sendWaypoint(int chan, double lat, double lon, const char* name, const char* desc, uint32_t& id,
                    uint32_t expire = 0);
  struct Waypoint {
    uint32_t id, expire, from;
    double lat, lon;
    char name[32], desc[104];
    int chan;
  };
  using WaypointFn = void (*)(const Waypoint& w);
  void setWaypointFn(WaypointFn f) { waypoint_fn_ = f; }
  // Kanalen for faste udsendelser ("Kanal for data"; 0 = primær, -1 = findes ikke).
  int dataChannelIndex() const { return dataChannel(); }

  void begin(ChannelScan* scan, MsgStore* store, bool sd);
  // En modtagen Meshtastic-pakke (rå, inkl. header).
  void onPacket(const uint8_t* raw, size_t len, int16_t rssi, int8_t snr_q4, uint32_t t_ms);
  void tick(uint32_t now_ms);  // gem nodedatabasen på SD, når den er ændret (højst hvert minut)

  int count() const { return n_; }
  // Slet en node fra nodedatabasen (Kontakt-fanen). Den kommer igen, hvis vi hører den.
  bool removeNode(uint32_t num);
  const MtNodeEntry& at(int i) const { return nodes_[i]; }  // nyeste først
  const MtNodeEntry* find(uint32_t num) const;
  // Langt navn, ellers kort navn, ellers "!xxxxxxxx".
  const char* nameOf(uint32_t num, char* buf, size_t n) const;
  uint32_t version() const { return version_; }

  static void dmConvId(uint32_t num, char* out, size_t n);  // "mtd_xxxxxxxx"
  static const char* hwName(uint8_t hw);
  static const char* roleName(uint8_t role);

  struct Stats {
    uint32_t pkts, decoded, undecoded, text, position, nodeinfo, routing, telemetry, other, dupes;
    uint32_t relayed, relay_canceled;
  };
  const Stats& stats() const { return st_; }
  // Selvtest (monitor 'X'): konstruerede pakker fra en opdigtet node på den primære kanal
  // (NodeInfo, Position, Telemetri, tekst, gentagelse) gennem modtagelsen. Skriver ikke i
  // samtalerne, og den opdigtede node fjernes bagefter.
  bool selfTest();

 private:
  MtNodeEntry* touch(uint32_t num, int16_t rssi, int8_t snr_q4, uint8_t hops, uint32_t t_ms);
  bool seen(uint32_t from, uint32_t id);
  void load();
  void save();
  void handleText(uint32_t from, uint32_t to, int chan, const uint8_t* p, size_t n, uint8_t hops,
                  uint32_t t_ms, uint32_t emoji);
  void handlePosition(MtNodeEntry* e, const uint8_t* p, size_t n);
  void handleUser(MtNodeEntry* e, const uint8_t* p, size_t n, bool signed_ok);
  void handleTelemetry(MtNodeEntry* e, const uint8_t* p, size_t n);
  void handleRouting(uint32_t from, uint32_t request_id, const uint8_t* p, size_t n);
  void handleTraceroute(const uint8_t* p, size_t n, bool want_response, uint32_t request_id, uint32_t from,
                        uint32_t to, uint32_t id, int chan, uint8_t hops, int8_t snr_q4);
  uint32_t trace_id_ = 0, trace_to_ = 0;  // vores igangværende traceroute
  // Afkodet Data-besked.
  struct DataMsg {
    uint32_t port = 0, dest = 0, request_id = 0, reply_id = 0, emoji = 0, bitfield = 0;
    bool want_response = false, has_bitfield = false;
    const uint8_t* payload = nullptr;
    size_t plen = 0;
    const uint8_t* sig = nullptr;  // 64 byte, hvis signeret
  };
  static bool parseData(const uint8_t* p, size_t n, DataMsg& d);
  // Behandl en afkodet pakke. chan = vores kanal (indeks) eller -1 for PKI.
  void dispatch(const DataMsg& d, uint32_t from, uint32_t to, uint32_t id, int chan, bool pki, bool want_ack,
                uint8_t hops, int16_t rssi, int8_t snr_q4, uint32_t t_ms);
  bool checkSignature(const DataMsg& d, uint32_t from, uint32_t to, uint32_t id, bool& signed_ok);
  // hop = kDefaultHop: vores normale hop_limit; 0 = kun direkte nabo (ACK til next-hop-afsender).
  static constexpr uint8_t kDefaultHop = 0xFF;
  void sendRouting(uint32_t to, uint32_t request_id, uint32_t err, int chan, uint8_t hop = kDefaultHop);
  // Den aktuelle pakkes header (sat i onPacket før dispatch): next_hop (byte 14) og hop_limit.
  uint8_t rx_next_hop_ = 0, rx_hop_limit_ = 0;
  int16_t rx_rssi_ = 0;
  int8_t rx_snr_q4_ = 0;
  // Videresendelse (docs/meshtastic_protokol.md afsnit 9).
  struct Relay {
    uint32_t from, id, due_ms;
    uint8_t len;
    uint8_t pkt[255];
  };
  Relay relay_[6] = {};
  Relay* findRelay(uint32_t from, uint32_t id);
  void maybeRelay(const uint8_t* raw, size_t len, int8_t snr_q4, bool readable);
  void cancelRelay(uint32_t from, uint32_t id, const char* why);
  uint32_t relayDelayMs(int8_t snr_q4) const;
  // Senest sendte ACK'er, så en gentaget want_ack-pakke (ACK'en gik tabt) kan få en ny.
  struct AckMemo {
    uint32_t from, id;
    uint8_t chan, hop;
  };
  AckMemo acks_[8] = {};
  int ack_i_ = 0;
  // NodeInfo-forespørgsler til ukendte afsendere (askNodeInfo): hvem og hvornår.
  void askNodeInfo(uint32_t num, int chan, uint32_t t_ms);
  struct AskMemo {
    uint32_t num, ms;
  };
  static constexpr int kAsks = 32;
  AskMemo asks_[kAsks] = {};
  int ask_i_ = 0;
  uint32_t ask_any_ms_ = 0;
  uint32_t next_telemetry_ms_ = 300000;

  ChannelScan* scan_ = nullptr;
  MsgStore* store_ = nullptr;
  bool sd_ = false;
  MtNodeEntry* nodes_ = nullptr;  // PSRAM
  int n_ = 0;
  bool dirty_ = false;
  uint32_t saved_ms_ = 0;
  uint32_t version_ = 0;
  uint32_t seen_[64][2] = {};
  int seen_i_ = 0;
  Stats st_{};
  bool testing_ = false;
  Config cfg_{};
  uint32_t next_nodeinfo_ms_ = 60000, next_position_ms_ = 120000;
  struct Pending {
    uint32_t id, seq, ms;
  };
  Pending pending_[16] = {};  // egne kanalbeskeder, der venter på at blive hørt sendt videre
  int pending_i_ = 0;
  bool sendRaw(const uint8_t* pkt, size_t len);
  int chanOr0(int c) const;
  int dataChannel() const;  // indeks for cfg_.data_channel, 0 = primær, -1 = findes ikke
  WaypointFn waypoint_fn_ = nullptr;
  void handleWaypoint(uint32_t from, int chan, const uint8_t* p, size_t n);
  bool signOwn(uint32_t port, const mt_tx::Opts& o, const uint8_t* payload, size_t n, uint8_t sig[64]);
  static constexpr uint32_t kDmRetryMs = 20000;
  struct PendingDm {
    uint32_t id, to, seq, next_ms;
    uint8_t tries, len;
    bool relayed;
    uint8_t pkt[255];
  };
  PendingDm dms_[4] = {};
  MtPhoneSink* phone_ = nullptr;
  void dmFinal(PendingDm& p, bool delivered, const char* why);
  void ccPhone(uint32_t to, uint32_t id, int chan, bool pki, const char* text, size_t tl, bool want_ack);
  // Forespørgsel fra tandhjulsmenuen, der venter på svar.
  static constexpr uint32_t kReqWindowMs = 60000;
  uint32_t req_to_ = 0, req_port_ = 0, req_id_ = 0, req_ms_ = 0;
  static const char* reqName(uint32_t port);
  void requestResult(const MtNodeEntry* e);
  void noteInDm(uint32_t num, const char* text);
  void onOwnEcho(uint32_t id);
  char test_text_[64] = "";
};

}  // namespace dm
