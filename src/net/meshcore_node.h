// SPDX-License-Identifier: MIT
// DualMesh – MeshCore-klient bygget på MeshCores BaseChatMesh (MIT).
//
// Fase 2, trin 1: modtager adverts (kontakter), beskeder på den offentlige
// kanal og DM'er. Identiteten (Ed25519-nøglepar) gemmes på SD i
// /sd/dualmesh/meshcore/identity.txt. Kører i loop()-tasken.
#pragma once
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>

#include "net/meshcore_radio.h"
#include "net/msg_store.h"

namespace dm {

// Kryptografisk tilfældighed fra ESP32's hardware-RNG (nøgler må ikke komme fra random()).
class EspRng : public mesh::RNG {
 public:
  void random(uint8_t* dest, size_t sz) override;
};

// Lytter på MeshCore-hændelser (bruges af companion-protokollen til appen over BLE).
class McListener {
 public:
  virtual ~McListener() = default;
  virtual void onDmRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) {}
  virtual void onChannelRecv(int, mesh::Packet*, uint32_t, const char*) {}
  virtual void onAdvertRecv(const ContactInfo&, bool) {}
  virtual void onPathUpdated(const ContactInfo&) {}
  // Kvittering for en DM, appen har sendt: returnér kontakten (ellers nullptr).
  virtual ContactInfo* onAckRecv(const uint8_t*) { return nullptr; }
  // Svar på en forespørgsel (login, status, telemetri, binær): data starter med tag (4 byte).
  virtual void onResponse(const ContactInfo&, const uint8_t*, uint8_t) {}
  // Sti modtaget (fx svar på "find sti"): true = håndteret, svar IKKE med vores sti.
  virtual bool onPathRecv(ContactInfo&, const uint8_t*, uint8_t, const uint8_t*, uint8_t, uint8_t,
                          const uint8_t*, uint8_t) { return false; }
  virtual void onTrace(mesh::Packet*, uint32_t, uint32_t, uint8_t, const uint8_t*, const uint8_t*, uint8_t) {}
  virtual void onControlData(mesh::Packet*) {}
  virtual void onRawData(mesh::Packet*) {}
  // Hver modtagen pakke, rå (appens "Rx Log").
  virtual void onRxRaw(float, float, const uint8_t*, int) {}
  virtual void onChannelData(int, mesh::Packet*, uint16_t, const uint8_t*, size_t) {}
};

struct McNodeStats {
  uint32_t adverts = 0, new_contacts = 0, channel_msgs = 0, direct_msgs = 0;
};

class McNode : public BaseChatMesh {
 public:
  McNode(McRadio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
         mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : BaseChatMesh(radio, ms, rng, rtc, mgr, tables), rng_(rng) {}

  // sd_ok: identiteten kan gemmes/hentes. name: tom = "DualMesh-XXXX".
  void start(bool sd_ok, const char* name);
  const char* nodeName() const { return name_; }
  // Nyt navn (bruges i næste advert og i kanalbeskeder). Tom = "DualMesh-XXXX".
  void setName(const char* name);
  const char* identityNote() const { return id_note_; }
  const McNodeStats& nodeStats() const { return stats_; }
  int contactCount() const { return getNumContacts(); }

  // Afsendelse (fase 2 trin 2). Adverts har kun position, hvis PosFn giver en
  // (privatlivskrav: main.cpp giver den kun, når brugeren har slået deling til,
  // eller når SOS er aktiv).
  using PosFn = bool (*)(double& lat, double& lon);
  void setPositionFn(PosFn f) { pos_fn_ = f; }
  // Telemetri til en anden node: skriver Cayenne LPP i out og returnerer længden, eller 0
  // = intet svar (ikke tilladt). inv_mask = forespørgslens "vil ikke have"-bits.
  using TelemFn = uint8_t (*)(const ContactInfo& c, uint8_t inv_mask, uint8_t* out, uint8_t max);
  void setTelemetryFn(TelemFn f) { telem_fn_ = f; }
  bool sendAdvert();
  bool sendAdvertMode(bool flood);  // flood = som sendAdvert, ellers kun direkte naboer
  void setListener(McListener* l) { listener_ = l; }
  const uint8_t* selfPubKey() const { return self_id.pub_key; }
  bool saveChannelsNow() { return saveChannels(); }
  // Identitetsnøglen til/fra MeshCore-appen (64 byte privat nøgle, MeshCores format).
  void exportPrivateKey(uint8_t out[64]) { self_id.writeTo(out, 64); }
  // Gemmer en ny identitet (krypteret på SD). Kræver genstart bagefter. false = ugyldig/ikke gemt.
  bool importPrivateKey(const uint8_t prv[64]);
  void markContactsDirty() { contacts_dirty_ = true; }
  // Fra appen (vises også i T-Deck'ens samtaler).
  int sendDirectFromApp(const ContactInfo& to, uint32_t ts, uint8_t attempt, const char* text,
                        uint32_t& ack, uint32_t& timeout);
  bool sendChannelFromApp(int idx, uint32_t ts, const char* text);
  // record = vis/gem den i samtalen (testbeskeden gemmes ikke).
  bool sendPublic(const char* text, bool record = true) { return sendChannel(0, text, record); }
  bool sendChannel(int idx, const char* text, bool record = true);

  // Kanaler: 0 = Public (fast); de øvrige gemmes på SD (/sd/dualmesh/meshcore/channels.txt,
  // "navn<TAB>nøgle-base64"). Navn "#navn" = hashtag-kanal (nøgle = SHA-256("#navn")[0:16]).
  int channelCount();
  bool channelAt(int idx, ChannelDetails& out) { return getChannel(idx, out); }
  // Tilføj: psk_b64 tom = hashtag (ved "#navn") eller ny tilfældig nøgle. Returnerer
  // indeks (også hvis kanalen findes i forvejen) eller -1; *note = kort forklaring.
  int addUserChannel(const char* name, const char* psk_b64, const char** note);
  // Slet kanal nr. idx (ikke Public) og gem. De efterfølgende rykker én plads op.
  bool removeChannel(int idx);
  // Saml kanalerne uden huller (appen sletter ved at give en plads et tomt navn).
  void compactChannels();
  static void channelConvId(const mesh::GroupChannel& ch, char* out, size_t n);
  static void dmConvId(const uint8_t* pub, char* out, size_t n);
  // Nøglen som base64 (16 byte, eller 32 hvis den er så lang), til deling.
  static void channelKeyB64(const mesh::GroupChannel& ch, char* out, size_t n);
  // DM til en kontakt; følges til kvittering (Msg::Delivered/Failed i lageret).
  bool sendDirect(const ContactInfo& to, const char* text);

  void setStore(MsgStore* s) { store_ = s; }
  // Kontakter på SD (/sd/dualmesh/meshcore/contacts.txt). Gem kaldes jævnligt
  // fra loop(); skriver kun, når noget er ændret.
  int loadContacts();
  bool saveContactsIfDirty(uint32_t min_interval_ms = 60000);
  // Senest modtagne advert-stier (som MeshCores companion, til appens "advert path").
  struct AdvertPathRec {
    uint8_t prefix[7];
    uint8_t path_len;
    uint32_t ts;
    uint8_t path[MAX_PATH_SIZE];
  };
  const AdvertPathRec* findAdvertPath(const uint8_t* pub_prefix) const;
  // Forbindelser til rumservere/repeatere efter login (BaseChatMesh har dem som protected).
  bool connStart(const ContactInfo& c, uint16_t keep_alive_secs) { return startConnection(c, keep_alive_secs); }
  void connStop(const uint8_t* pub_key) { stopConnection(pub_key); }
  bool connHas(const uint8_t* pub_key) { return hasConnectionTo(pub_key); }
  int outboundTotal() const { return _mgr->getOutboundTotal(); }
  float radioRssi() const { return _radio->getLastRSSI(); }
  float radioSnr() const { return _radio->getLastSNR(); }
  // Signér data med nodens Ed25519-nøgle (appens "sign"), 64 byte signatur.
  void signData(uint8_t* sig, const uint8_t* data, size_t len) { self_id.sign(sig, data, int(len)); }
  // Kopi af kontakterne til UI'et, nyeste først (sidst hørt). Returnerer antal.
  int contactsSnapshot(ContactInfo* out, int max);
  bool clockSet() const { return clock_set_; }
  // Rigtig tid kendt (fælles ur, se util/wall_clock.h)? Fra mesh sættes den, når mindst
  // 3 forskellige afsendere (adverts, DM'er, kanalbeskeder) er enige inden for 2 min.
  bool timeValid() const;
  // Unix-tid nu, eller 0 hvis den rigtige tid ikke kendes endnu.
  uint32_t nowTs() const;

 protected:
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len,
                           const uint8_t* path) override;
  ContactInfo* processAck(const uint8_t* data) override;
  void onContactPathUpdated(const ContactInfo& c) override {
    contacts_dirty_ = true;
    if (listener_) listener_->onPathUpdated(c);
  }
  void onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp,
                     const char* text) override;
  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCLICommandRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*, char*) override {}
  void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp,
                           const uint8_t* sender_prefix, const char* text) override;
  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;
  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp,
                            const char* text) override;
  uint8_t onContactRequest(const ContactInfo& c, uint32_t ts, const uint8_t* data, uint8_t len,
                           uint8_t* reply) override;
  void onContactResponse(const ContactInfo& c, const uint8_t* data, uint8_t len) override {
    if (listener_ && len >= 4) listener_->onResponse(c, data, len);
  }
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path,
                         uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override {
    if (listener_ && listener_->onPathRecv(from, in_path, in_path_len, out_path, out_path_len, extra_type, extra,
                                            extra_len))
      return false;  // svar på "find sti": send ikke vores sti tilbage
    return BaseChatMesh::onContactPathRecv(from, in_path, in_path_len, out_path, out_path_len, extra_type, extra,
                                           extra_len);
  }
  void onTraceRecv(mesh::Packet* p, uint32_t tag, uint32_t auth, uint8_t flags, const uint8_t* snrs,
                   const uint8_t* hashes, uint8_t path_len) override {
    if (listener_) listener_->onTrace(p, tag, auth, flags, snrs, hashes, path_len);
  }
  void onControlDataRecv(mesh::Packet* p) override {
    if (listener_) listener_->onControlData(p);
  }
  void onRawDataRecv(mesh::Packet* p) override {
    if (listener_) listener_->onRawData(p);
  }
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override {
    if (listener_) listener_->onRxRaw(snr, rssi, raw, len);
  }
  void onChannelDataRecv(const mesh::GroupChannel& ch, mesh::Packet* p, uint16_t type, const uint8_t* data,
                         size_t len) override {
    if (listener_) listener_->onChannelData(findChannelIdx(ch), p, type, data, len);
  }

 private:
  bool loadIdentity();
  bool saveIdentity();
  const char* channelName(const mesh::GroupChannel& ch);

  mesh::RNG& rng_;
  McNodeStats stats_{};
  char name_[32] = "";
  const char* id_note_ = "";
  bool clock_set_ = false;
  PosFn pos_fn_ = nullptr;
  TelemFn telem_fn_ = nullptr;
  McListener* listener_ = nullptr;
  struct TimeCand {
    int64_t off;   // afsenderens tid - vores oppetid (s)
    uint32_t src;  // hvem (hash), så én node ikke tæller flere gange
  };
  static constexpr int kTimeCands = 8;
  TimeCand tcand_[kTimeCands] = {};
  int n_tcand_ = 0, next_tcand_ = 0;
  void noteRemoteTime(uint32_t ts, uint32_t src);
  MsgStore* store_ = nullptr;
  bool contacts_dirty_ = false, sd_ok_ = false;
  AdvertPathRec advert_paths_[16] = {};
  uint32_t last_contacts_save_ms_ = 0;
  bool loadChannels();
  // Læg en kanal på første ledige plads (setChannel, ikke MeshCores addChannel, hvis
  // interne tæller ikke kender til sletninger). -1 = fuld eller forkert nøgle.
  int putChannel(const char* name, const char* psk_b64);
  bool saveChannels();
  // Ventende DM-kvittering (én ad gangen i fase 2).
  uint32_t pending_ack_ = 0;
  uint8_t pending_pub_[PUB_KEY_SIZE] = {};
  uint32_t pending_seq_ = 0;  // beskedens løbenummer i MsgStore
};

}  // namespace dm
