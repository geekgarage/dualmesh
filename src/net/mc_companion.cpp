// SPDX-License-Identifier: MIT
// Rammeformater efter MeshCores examples/companion_radio/MyMesh.cpp (v1.18.0, MIT,
// Copyright (c) 2025 Scott Powell / rippleradios.com).
#include "net/mc_companion.h"

#include <Arduino.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>

#include <cstring>

#include "esp_heap_caps.h"
#include "storage/sd_log.h"
#include "util/wall_clock.h"

namespace dm {

namespace {
// Kommandoer (app -> radio)
enum : uint8_t {
  CMD_APP_START = 1, CMD_SEND_TXT_MSG = 2, CMD_SEND_CHANNEL_TXT_MSG = 3, CMD_GET_CONTACTS = 4,
  CMD_GET_DEVICE_TIME = 5, CMD_SET_DEVICE_TIME = 6, CMD_SEND_SELF_ADVERT = 7,
  CMD_SET_ADVERT_NAME = 8, CMD_ADD_UPDATE_CONTACT = 9, CMD_SYNC_NEXT_MESSAGE = 10,
  CMD_SET_RADIO_PARAMS = 11, CMD_SET_RADIO_TX_POWER = 12, CMD_RESET_PATH = 13,
  CMD_SET_ADVERT_LATLON = 14, CMD_REMOVE_CONTACT = 15, CMD_REBOOT = 19,
  CMD_GET_BATT_AND_STORAGE = 20, CMD_DEVICE_QUERY = 22, CMD_GET_CONTACT_BY_KEY = 30,
  CMD_GET_CHANNEL = 31, CMD_SET_CHANNEL = 32, CMD_SET_DEVICE_PIN = 37, CMD_SET_OTHER_PARAMS = 38,
  CMD_SET_FLOOD_SCOPE_KEY = 54, CMD_SET_PATH_HASH_MODE = 61, CMD_SET_DEFAULT_FLOOD_SCOPE = 63,
  CMD_GET_DEFAULT_FLOOD_SCOPE = 64, CMD_EXPORT_PRIVATE_KEY = 23, CMD_IMPORT_PRIVATE_KEY = 24,
  CMD_GET_CUSTOM_VARS = 40, CMD_SET_CUSTOM_VAR = 41, CMD_SET_AUTOADD_CONFIG = 58, CMD_GET_AUTOADD_CONFIG = 59,
  CMD_GET_ALLOWED_REPEAT_FREQ = 60, CMD_SEND_TELEMETRY_REQ = 39, CMD_RUN_CLI_COMMAND = 66,
};
// Svar og push (radio -> app)
enum : uint8_t {
  RESP_OK = 0, RESP_ERR = 1, RESP_CONTACTS_START = 2, RESP_CONTACT = 3, RESP_END_OF_CONTACTS = 4,
  RESP_SELF_INFO = 5, RESP_SENT = 6, RESP_CONTACT_MSG_RECV = 7, RESP_CHANNEL_MSG_RECV = 8,
  RESP_CURR_TIME = 9, RESP_NO_MORE_MESSAGES = 10, RESP_BATT_AND_STORAGE = 12,
  RESP_DEVICE_INFO = 13, RESP_CONTACT_MSG_RECV_V3 = 16, RESP_CHANNEL_MSG_RECV_V3 = 17,
  RESP_CHANNEL_INFO = 18, RESP_DEFAULT_FLOOD_SCOPE = 28, RESP_PRIVATE_KEY = 14, RESP_CUSTOM_VARS = 21,
  RESP_AUTOADD_CONFIG = 25, RESP_ALLOWED_REPEAT_FREQ = 26, RESP_CLI_REPLY = 29,
  PUSH_TELEMETRY_RESPONSE = 0x8B, RESP_EXPORT_CONTACT = 11, RESP_SIGN_START = 19, RESP_SIGNATURE = 20,
  RESP_ADVERT_PATH = 22, RESP_TUNING_PARAMS = 23, RESP_STATS = 24, RESP_CHANNEL_DATA_RECV = 27,
  PUSH_RAW_DATA = 0x84, PUSH_LOGIN_SUCCESS = 0x85, PUSH_LOGIN_FAIL = 0x86, PUSH_STATUS_RESPONSE = 0x87,
  PUSH_TRACE_DATA = 0x89, PUSH_BINARY_RESPONSE = 0x8C, PUSH_PATH_DISCOVERY_RESPONSE = 0x8D,
  PUSH_CONTROL_DATA = 0x8E, PUSH_LOG_RX_DATA = 0x88,
  PUSH_ADVERT = 0x80, PUSH_PATH_UPDATED = 0x81, PUSH_SEND_CONFIRMED = 0x82,
  PUSH_MSG_WAITING = 0x83, PUSH_NEW_ADVERT = 0x8A,
};
enum : uint8_t { ERR_UNSUPPORTED = 1, ERR_NOT_FOUND = 2, ERR_TABLE_FULL = 3, ERR_BAD_STATE = 4,
                 ERR_FILE_IO = 5, ERR_ILLEGAL_ARG = 6 };
// Kommandoer, der kom til 17. nov 2026 (koder fra MeshCores companion_radio).
enum : uint8_t {
  CMD_SHARE_CONTACT = 16, CMD_EXPORT_CONTACT = 17, CMD_IMPORT_CONTACT = 18, CMD_SET_TUNING_PARAMS = 21,
  CMD_SEND_RAW_DATA = 25, CMD_SEND_LOGIN = 26, CMD_SEND_STATUS_REQ = 27, CMD_HAS_CONNECTION = 28,
  CMD_LOGOUT = 29, CMD_SIGN_START = 33, CMD_SIGN_DATA = 34, CMD_SIGN_FINISH = 35, CMD_SEND_TRACE_PATH = 36,
  CMD_GET_ADVERT_PATH = 42, CMD_GET_TUNING_PARAMS = 43, CMD_SEND_BINARY_REQ = 50, CMD_FACTORY_RESET = 51,
  CMD_SEND_PATH_DISCOVERY_REQ = 52, CMD_SEND_CONTROL_DATA = 55, CMD_GET_STATS = 56, CMD_SEND_ANON_REQ = 57,
  CMD_SEND_CHANNEL_DATA = 62, CMD_SEND_RAW_PACKET = 65,
};
enum : uint8_t { STATS_CORE = 0, STATS_RADIO = 1, STATS_PACKETS = 2 };
constexpr uint8_t kTelemPermBase = 0x01;
constexpr uint8_t kReqTelemetry = 0x03;  // REQ_TYPE_GET_TELEMETRY_DATA
constexpr uint8_t kFirmwareVer = 14;  // MeshCore companion v1.18.0
constexpr const char* kBuildDate = "15 Nov 2026";
// Versionen skal være ren "vX.Y.Z": appen læser den og slår funktioner (fx eksport af
// identitetsnøglen) fra ved et ukendt format. DualMesh står i producentfeltet.
constexpr const char* kVersion = "v1.18.0";
constexpr const char* kManufacturer = "LilyGo T-Deck Plus (DualMesh)";

void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
uint32_t get32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
}  // namespace

void McCompanion::begin(McNode* node, BleSerial* ble, const Hooks& h) {
  node_ = node;
  ble_ = ble;
  h_ = h;
  if (!off_)
    off_ = static_cast<Frame*>(heap_caps_calloc(kMaxOffline, sizeof(Frame), MALLOC_CAP_SPIRAM));
  node_->setListener(this);
}

void McCompanion::ok() {
  const uint8_t b = RESP_OK;
  write(&b, 1);
}

void McCompanion::err(uint8_t code) {
  const uint8_t b[2] = {RESP_ERR, code};
  write(b, 2);
}

void McCompanion::writeContact(uint8_t code, const ContactInfo& c) {
  size_t i = 0;
  out_[i++] = code;
  std::memcpy(out_ + i, c.id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  out_[i++] = c.type;
  out_[i++] = c.flags;
  out_[i++] = c.out_path_len;
  std::memcpy(out_ + i, c.out_path, MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  std::memset(out_ + i, 0, 32);
  std::strncpy(reinterpret_cast<char*>(out_ + i), c.name, 31);
  i += 32;
  put32(out_ + i, c.last_advert_timestamp);
  i += 4;
  std::memcpy(out_ + i, &c.gps_lat, 4);
  i += 4;
  std::memcpy(out_ + i, &c.gps_lon, 4);
  i += 4;
  put32(out_ + i, c.lastmod);
  i += 4;
  write(out_, i);
}

// Kø til appen (også mens den ikke er forbundet). Fuld: den ældste kanalbesked
// smides ud (som MeshCore), DM'er bevares.
void McCompanion::queueOffline(const uint8_t* d, size_t n, bool chan) {
  if (!off_) return;
  if (n_off_ >= kMaxOffline) {
    int drop = -1;
    for (int i = 0; i < n_off_ && drop < 0; ++i)
      if (off_[i].chan) drop = i;
    if (drop < 0) return;  // kun DM'er i køen: den nye tabes
    for (int i = drop; i + 1 < n_off_; ++i) off_[i] = off_[i + 1];
    --n_off_;
  }
  Frame& f = off_[n_off_++];
  f.len = uint8_t(n);
  f.chan = chan;
  std::memcpy(f.buf, d, n);
}

void McCompanion::tickle() {
  if (!connected()) return;
  const uint8_t b = PUSH_MSG_WAITING;
  write(&b, 1);
}

// --- hændelser fra MeshCore ----------------------------------------------
void McCompanion::onDmRecv(const ContactInfo& c, mesh::Packet* pkt, uint32_t ts, const char* text) {
  uint8_t f[BleSerial::kMaxFrame];
  size_t i = 0;
  if (app_ver_ >= 3) {
    f[i++] = RESP_CONTACT_MSG_RECV_V3;
    f[i++] = uint8_t(int8_t(pkt->getSNR() * 4));
    f[i++] = 0;
    f[i++] = 0;
  } else {
    f[i++] = RESP_CONTACT_MSG_RECV;
  }
  std::memcpy(f + i, c.id.pub_key, 6);
  i += 6;
  f[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  f[i++] = TXT_TYPE_PLAIN;
  put32(f + i, ts);
  i += 4;
  size_t tl = std::strlen(text);
  if (i + tl > sizeof(f)) tl = sizeof(f) - i;
  std::memcpy(f + i, text, tl);
  i += tl;
  queueOffline(f, i, false);
  tickle();
}

void McCompanion::onChannelRecv(int idx, mesh::Packet* pkt, uint32_t ts, const char* text) {
  if (idx < 0) return;
  uint8_t f[BleSerial::kMaxFrame];
  size_t i = 0;
  if (app_ver_ >= 3) {
    f[i++] = RESP_CHANNEL_MSG_RECV_V3;
    f[i++] = uint8_t(int8_t(pkt->getSNR() * 4));
    f[i++] = 0;
    f[i++] = 0;
  } else {
    f[i++] = RESP_CHANNEL_MSG_RECV;
  }
  f[i++] = uint8_t(idx);
  f[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  f[i++] = TXT_TYPE_PLAIN;
  put32(f + i, ts);
  i += 4;
  size_t tl = std::strlen(text);
  if (i + tl > sizeof(f)) tl = sizeof(f) - i;
  std::memcpy(f + i, text, tl);
  i += tl;
  queueOffline(f, i, true);
  tickle();
}

void McCompanion::onAdvertRecv(const ContactInfo& c, bool is_new) {
  if (!connected()) return;
  if (is_new) {
    writeContact(PUSH_NEW_ADVERT, c);
  } else {
    out_[0] = PUSH_ADVERT;
    std::memcpy(out_ + 1, c.id.pub_key, PUB_KEY_SIZE);
    write(out_, 1 + PUB_KEY_SIZE);
  }
}

void McCompanion::onPathUpdated(const ContactInfo& c) {
  if (!connected()) return;
  out_[0] = PUSH_PATH_UPDATED;
  std::memcpy(out_ + 1, c.id.pub_key, PUB_KEY_SIZE);
  write(out_, 1 + PUB_KEY_SIZE);
}

ContactInfo* McCompanion::onAckRecv(const uint8_t* data) {
  for (Ack& a : acks_) {
    if (!a.ack || std::memcmp(data, &a.ack, 4) != 0) continue;
    uint8_t f[9];
    f[0] = PUSH_SEND_CONFIRMED;
    std::memcpy(f + 1, data, 4);
    put32(f + 5, millis() - a.sent_ms);
    if (connected()) write(f, 9);
    a.ack = 0;  // samme kvittering kan komme flere gange
    return a.c;
  }
  return nullptr;
}

// --- kommandoer fra appen -------------------------------------------------
void McCompanion::loop() {
  if (!ble_ || !node_) return;
  const size_t len = ble_->poll(cmd_);
  if (len) {
    handle(len);
  } else if (iter_on_ && !ble_->writeBusy()) {  // kontaktlisten sendes én ad gangen
    ContactInfo c;
    if (iter_.hasNext(node_, c)) {
      if (c.lastmod > iter_since_) {
        writeContact(RESP_CONTACT, c);
        if (c.lastmod > iter_recent_) iter_recent_ = c.lastmod;
      }
    } else {
      out_[0] = RESP_END_OF_CONTACTS;
      put32(out_ + 1, iter_recent_);
      write(out_, 5);
      iter_on_ = false;
    }
  }
}

void McCompanion::handle(size_t len) {
  cmd_[len] = 0;
  const uint8_t c = cmd_[0];
  if (c == CMD_DEVICE_QUERY && len >= 2) {
    app_ver_ = cmd_[1];
    size_t i = 0;
    out_[i++] = RESP_DEVICE_INFO;
    out_[i++] = kFirmwareVer;
    out_[i++] = MAX_CONTACTS / 2;
    out_[i++] = MAX_GROUP_CHANNELS;
    put32(out_ + i, ble_->pin());
    i += 4;
    std::memset(out_ + i, 0, 12);
    std::strncpy(reinterpret_cast<char*>(out_ + i), kBuildDate, 11);
    i += 12;
    std::memset(out_ + i, 0, 40);
    std::strncpy(reinterpret_cast<char*>(out_ + i), kManufacturer, 39);
    i += 40;
    std::memset(out_ + i, 0, 20);
    std::strncpy(reinterpret_cast<char*>(out_ + i), kVersion, 19);
    i += 20;
    out_[i++] = 0;  // repeater: fra (T-Deck'en er klient)
    out_[i++] = 0;  // path_hash_mode
    write(out_, i);
    DLOG.printf("[BLE] app forespoerger enheden (protokol v%u)\n", unsigned(app_ver_));
  } else if (c == CMD_APP_START && len >= 8) {
    DLOG.printf("[BLE] app startet: %.40s\n", reinterpret_cast<const char*>(cmd_ + 8));
    iter_on_ = false;
    uint32_t freq = 0, bw = 0;
    uint8_t sf = 0, cr = 0;
    int8_t dbm = 0;
    if (h_.radio) h_.radio(freq, bw, sf, cr, dbm);
    double lat = 0, lon = 0;
    const bool pos = h_.position && h_.position(lat, lon);
    size_t i = 0;
    out_[i++] = RESP_SELF_INFO;
    out_[i++] = ADV_TYPE_CHAT;
    out_[i++] = uint8_t(dbm);
    out_[i++] = 22;  // SX1262 max
    std::memcpy(out_ + i, node_->selfPubKey(), PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;
    put32(out_ + i, uint32_t(int32_t(pos ? lat * 1e6 : 0)));
    i += 4;
    put32(out_ + i, uint32_t(int32_t(pos ? lon * 1e6 : 0)));
    i += 4;
    out_[i++] = 0;                                         // multi_acks
    out_[i++] = (h_.share_on && h_.share_on()) ? 1 : 0;   // advert_loc_policy
    out_[i++] = h_.telem_modes ? h_.telem_modes() : 0;     // telemetri-tilstande
    out_[i++] = 0;                                         // manual_add_contacts
    put32(out_ + i, freq);
    i += 4;
    put32(out_ + i, bw);
    i += 4;
    out_[i++] = sf;
    out_[i++] = cr;
    const size_t nl = std::strlen(node_->nodeName());
    std::memcpy(out_ + i, node_->nodeName(), nl);
    i += nl;
    write(out_, i);
  } else if (c == CMD_SEND_TXT_MSG && len >= 14) {
    const uint8_t type = cmd_[1], attempt = cmd_[2];
    const uint32_t ts = get32(cmd_ + 3);
    ContactInfo* to = node_->lookupContactByPubKey(cmd_ + 7, 6);
    if (!to) return err(ERR_NOT_FOUND);
    if (type != TXT_TYPE_PLAIN) return err(ERR_UNSUPPORTED);
    uint32_t ack = 0, timeout = 0;
    const int r = node_->sendDirectFromApp(*to, ts, attempt, reinterpret_cast<const char*>(cmd_ + 13), ack,
                                           timeout);
    if (r == MSG_SEND_FAILED) return err(ERR_TABLE_FULL);
    if (ack) {
      acks_[next_ack_] = {ack, millis(), to};
      next_ack_ = (next_ack_ + 1) % kAcks;
    }
    out_[0] = RESP_SENT;
    out_[1] = r == MSG_SEND_SENT_FLOOD ? 1 : 0;
    put32(out_ + 2, ack);
    put32(out_ + 6, timeout);
    write(out_, 10);
  } else if (c == CMD_SEND_CHANNEL_TXT_MSG && len >= 7) {
    if (cmd_[1] != TXT_TYPE_PLAIN) return err(ERR_UNSUPPORTED);
    if (node_->sendChannelFromApp(cmd_[2], get32(cmd_ + 3), reinterpret_cast<const char*>(cmd_ + 7))) ok();
    else err(ERR_NOT_FOUND);
  } else if (c == CMD_GET_CONTACTS) {
    if (iter_on_) return err(ERR_BAD_STATE);
    iter_since_ = len >= 5 ? get32(cmd_ + 1) : 0;
    out_[0] = RESP_CONTACTS_START;
    put32(out_ + 1, uint32_t(node_->getNumContacts()));
    write(out_, 5);
    iter_ = node_->startContactsIterator();
    iter_on_ = true;
    iter_recent_ = 0;
  } else if (c == CMD_GET_CONTACT_BY_KEY && len >= 1 + PUB_KEY_SIZE) {
    ContactInfo* ct = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    if (ct) writeContact(RESP_CONTACT, *ct);
    else err(ERR_NOT_FOUND);
  } else if (c == CMD_SET_ADVERT_NAME && len >= 2) {
    char name[32];
    const size_t n = len - 1 < sizeof(name) - 1 ? len - 1 : sizeof(name) - 1;
    std::memcpy(name, cmd_ + 1, n);
    name[n] = 0;
    node_->setName(name);
    if (h_.set_name) h_.set_name(name);
    ok();
  } else if (c == CMD_SET_ADVERT_LATLON) {
    ok();  // vi bruger GPS'en; appens position ignoreres (privatliv styres i T-Deck'en)
  } else if (c == CMD_GET_DEVICE_TIME) {
    out_[0] = RESP_CURR_TIME;
    put32(out_ + 1, node_->getRTCClock()->getCurrentTime());
    write(out_, 5);
  } else if (c == CMD_SET_DEVICE_TIME && len >= 5) {
    const uint32_t t = get32(cmd_ + 1);
    if (t < 1735689600u) return err(ERR_ILLEGAL_ARG);
    wallclock::set(t, wallclock::App);  // en bedre kilde (NTP/GPS) har forrang
    ok();
  } else if (c == CMD_SEND_SELF_ADVERT) {
    if (node_->sendAdvertMode(len >= 2 && cmd_[1] == 1)) ok();
    else err(ERR_TABLE_FULL);
  } else if (c == CMD_RESET_PATH && len >= 1 + PUB_KEY_SIZE) {
    ContactInfo* ct = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    if (!ct) return err(ERR_NOT_FOUND);
    ct->out_path_len = OUT_PATH_UNKNOWN;
    node_->markContactsDirty();
    ok();
  } else if (c == CMD_ADD_UPDATE_CONTACT && len >= 1 + PUB_KEY_SIZE + 3 + MAX_PATH_SIZE + 32 + 4) {
    ContactInfo tmp{};
    ContactInfo* ct = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    ContactInfo& d = ct ? *ct : tmp;
    size_t i = 1;
    std::memcpy(d.id.pub_key, cmd_ + i, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;
    d.type = cmd_[i++];
    d.flags = cmd_[i++];
    d.out_path_len = cmd_[i++];
    std::memcpy(d.out_path, cmd_ + i, MAX_PATH_SIZE);
    i += MAX_PATH_SIZE;
    std::memcpy(d.name, cmd_ + i, 32);
    d.name[31] = 0;
    i += 32;
    d.last_advert_timestamp = get32(cmd_ + i);
    i += 4;
    uint32_t lastmod = node_->getRTCClock()->getCurrentTime();
    if (len >= i + 8) {
      std::memcpy(&d.gps_lat, cmd_ + i, 4);
      std::memcpy(&d.gps_lon, cmd_ + i + 4, 4);
      i += 8;
      if (len >= i + 4) lastmod = get32(cmd_ + i);
    }
    d.lastmod = lastmod;
    if (!ct && !node_->addContact(d)) return err(ERR_TABLE_FULL);
    node_->markContactsDirty();
    ok();
  } else if (c == CMD_REMOVE_CONTACT && len >= 1 + PUB_KEY_SIZE) {
    ContactInfo* ct = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    if (ct && node_->removeContact(*ct)) {
      node_->markContactsDirty();
      ok();
    } else {
      err(ERR_NOT_FOUND);
    }
  } else if (c == CMD_SYNC_NEXT_MESSAGE) {
    if (n_off_ > 0) {
      write(off_[0].buf, off_[0].len);
      --n_off_;
      for (int i = 0; i < n_off_; ++i) off_[i] = off_[i + 1];
    } else {
      out_[0] = RESP_NO_MORE_MESSAGES;
      write(out_, 1);
    }
  } else if (c == CMD_SET_RADIO_PARAMS && len >= 11) {
    const uint32_t freq = get32(cmd_ + 1), bw = get32(cmd_ + 5);
    const uint8_t sf = cmd_[9], cr = cmd_[10];
    const bool repeat = len > 11 && cmd_[11];
    DLOG.printf("[BLE] appen vaelger radio: %lu kHz, BW %lu Hz, SF%u, CR%u%s\n", (unsigned long)freq,
                (unsigned long)bw, unsigned(sf), unsigned(cr), repeat ? ", repeater" : "");
    if (repeat) return err(ERR_ILLEGAL_ARG);  // T-Deck'en er klient, ikke repeater
    const int r = h_.set_radio ? h_.set_radio(freq, bw, sf, cr) : -1;
    if (r < 0) err(ERR_ILLEGAL_ARG);
    else ok();
  } else if (c == CMD_SET_RADIO_TX_POWER) {
    DLOG.printf("[BLE] appen vil saette sendeeffekt %d dBm - ignoreret (regionens graense gaelder)\n",
                int(int8_t(cmd_[1])));
    ok();
  } else if (c == CMD_SET_OTHER_PARAMS && len >= 2) {
    if (len >= 3 && h_.set_telem_modes) h_.set_telem_modes(cmd_[2]);  // telemetri
    if (len >= 4 && h_.set_share) h_.set_share(cmd_[3] != 0);  // advert_loc_policy
    ok();
  } else if (c == CMD_REBOOT && len >= 7 && std::memcmp(cmd_ + 1, "reboot", 6) == 0) {
    DLOG.println("[BLE] genstart bedt om af appen");
    DLOG.flush(true);
    delay(50);
    esp_restart();
  } else if (c == CMD_GET_BATT_AND_STORAGE) {
    const uint16_t mv = h_.batt_mv ? h_.batt_mv() : 0;
    out_[0] = RESP_BATT_AND_STORAGE;
    std::memcpy(out_ + 1, &mv, 2);
    put32(out_ + 3, 0);
    put32(out_ + 7, 0);
    write(out_, 11);
  } else if (c == CMD_GET_CHANNEL && len >= 2) {
    ChannelDetails d;
    if (!node_->getChannel(cmd_[1], d)) return err(ERR_NOT_FOUND);
    size_t i = 0;
    out_[i++] = RESP_CHANNEL_INFO;
    out_[i++] = cmd_[1];
    std::memset(out_ + i, 0, 32);
    std::strncpy(reinterpret_cast<char*>(out_ + i), d.name, 31);
    i += 32;
    std::memcpy(out_ + i, d.channel.secret, 16);
    i += 16;
    write(out_, i);
  } else if (c == CMD_SET_CHANNEL && len >= 2 + 32 + 32) {
    err(ERR_UNSUPPORTED);  // 256-bit nøgler (som MeshCore)
  } else if (c == CMD_SET_CHANNEL && len >= 2 + 32 + 16) {
    ChannelDetails d;
    std::memset(&d, 0, sizeof(d));
    std::memcpy(d.name, cmd_ + 2, 32);
    d.name[31] = 0;
    std::memcpy(d.channel.secret, cmd_ + 34, 16);
    if (node_->setChannel(cmd_[1], d)) {
      if (!d.name[0]) node_->compactChannels();  // appen sletter med et tomt navn
      node_->saveChannelsNow();
      DLOG.printf("[BLE] kanal %u sat af appen: %s\n", unsigned(cmd_[1]), d.name);
      ok();
    } else {
      err(ERR_NOT_FOUND);
    }
  } else if (c == CMD_SET_PATH_HASH_MODE && len >= 3 && cmd_[1] == 0) {
    // Appens opsætning sender den fra presettet. Vi bruger MeshCores standard (1 byte)
    // indtil videre; svaret er OK, så opsætningen kan gennemføres.
    if (cmd_[2] >= 3) return err(ERR_ILLEGAL_ARG);
    DLOG.printf("[BLE] appen saetter path hash mode %u (bruges ikke endnu, standard 1 byte)\n", unsigned(cmd_[2]));
    ok();
  } else if (c == CMD_SET_FLOOD_SCOPE_KEY || c == CMD_SET_DEFAULT_FLOOD_SCOPE) {
    // Region-scope (transport-nøgler) understøttes ikke endnu: vi sender uden scope.
    DLOG.printf("[BLE] appen saetter flood scope (kommando %u) - ignoreret, sender uden scope\n", unsigned(c));
    ok();
  } else if (c == CMD_EXPORT_PRIVATE_KEY) {
    uint8_t r[65];
    r[0] = RESP_PRIVATE_KEY;
    node_->exportPrivateKey(r + 1);
    write(r, 65);
    for (uint8_t& b : r) b = 0;
    DLOG.println("[BLE] identitetsnoeglen er eksporteret til appen");
  } else if (c == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
    if (!node_->importPrivateKey(cmd_ + 1)) return err(ERR_ILLEGAL_ARG);
    ok();
    if (h_.restart) h_.restart(2000);  // kontakternes nøgler skal beregnes på ny
  } else if (c == CMD_SEND_TELEMETRY_REQ && len == 4) {
    // Egen telemetri til appen (Cayenne LPP, kanal 1 = os selv): batteri, chiptemperatur
    // og position ved GPS-fix. Andre noders telemetri-forespørgsler besvares ikke
    // (privatliv); appens telemetri-indstillinger ændrer ikke på det.
    size_t i = 0;
    out_[i++] = PUSH_TELEMETRY_RESPONSE;
    out_[i++] = 0;
    std::memcpy(out_ + i, node_->selfPubKey(), 6);
    i += 6;
    constexpr uint8_t kSelf = 1;
    const uint16_t mv = h_.batt_mv ? h_.batt_mv() : 0;
    const uint16_t v100 = uint16_t(mv / 10);  // 0,01 V
    out_[i++] = kSelf;
    out_[i++] = 116;  // LPP_VOLTAGE
    out_[i++] = uint8_t(v100 >> 8);
    out_[i++] = uint8_t(v100);
    const int16_t t10 = int16_t(temperatureRead() * 10.0f);  // 0,1 °C
    out_[i++] = kSelf;
    out_[i++] = 103;  // LPP_TEMPERATURE
    out_[i++] = uint8_t(uint16_t(t10) >> 8);
    out_[i++] = uint8_t(t10);
    double lat, lon, alt;
    if (h_.gps_pos && h_.gps_pos(lat, lon, alt)) {
      const int32_t la = int32_t(lat * 10000), lo = int32_t(lon * 10000), al = int32_t(alt * 100);
      out_[i++] = kSelf;
      out_[i++] = 136;  // LPP_GPS: 3 byte bredde, 3 byte længde (0,0001°), 3 byte højde (0,01 m)
      for (int32_t v : {la, lo, al}) {
        out_[i++] = uint8_t(v >> 16);
        out_[i++] = uint8_t(v >> 8);
        out_[i++] = uint8_t(v);
      }
    }
    write(out_, i);
  } else if (c == CMD_RUN_CLI_COMMAND && len >= 3) {
    // MeshCores CLI (v14). Vi kender få kommandoer; ukendte besvares som i MeshCore
    // med "Unknown command" (ikke som fejl). Teksten logges, så vi kan se, hvad appen vil.
    const char* text = reinterpret_cast<const char*>(cmd_ + 1);
    {  // log kommandoen, men aldrig en kode ("set wifi.pwd ...")
      const char* pw = std::strstr(text, "wifi.pwd ");
      if (pw) DLOG.printf("[BLE] CLI fra appen: %.*s(skjult)\n", int(pw - text + 9), text);
      else DLOG.printf("[BLE] CLI fra appen: %.60s\n", text);
    }
    char reply[BleSerial::kMaxFrame] = "";
    size_t rl = 0;
    if (std::strlen(text) > 4 && text[2] == '|') {  // præfiks spejles tilbage
      std::memcpy(reply, text, 3);
      rl = 3;
      text += 3;
    }
    while (*text == ' ') ++text;
    if (!std::strcmp(text, "reboot")) {
      DLOG.println("[BLE] genstart via CLI");
      if (h_.restart) h_.restart(500);
      rl += snprintf(reply + rl, sizeof(reply) - rl, "OK");
    } else if (!std::strcmp(text, "ver") || !std::strcmp(text, "version")) {
      rl += snprintf(reply + rl, sizeof(reply) - rl, "%s (DualMesh, LilyGo T-Deck Plus)", kVersion);
    } else if (!std::strcmp(text, "clock")) {
      rl += snprintf(reply + rl, sizeof(reply) - rl, "%lu", (unsigned long)node_->getRTCClock()->getCurrentTime());
    } else if (h_.run_cli && h_.run_cli(text, reply + rl, sizeof(reply) - rl)) {
      rl += std::strlen(reply + rl);
    } else {
      rl += snprintf(reply + rl, sizeof(reply) - rl, "Unknown command");
    }
    out_[0] = RESP_CLI_REPLY;
    if (rl > sizeof(out_) - 1) rl = sizeof(out_) - 1;
    std::memcpy(out_ + 1, reply, rl);
    write(out_, 1 + rl);
  } else if (c == CMD_GET_CUSTOM_VARS) {
    // Som MeshCores companion med GPS: "gps:1" / "gps:0" (appens Position Settings).
    out_[0] = RESP_CUSTOM_VARS;
    const int g = h_.gps_get ? h_.gps_get() : -1;
    size_t n = 1;
    if (g >= 0) n += snprintf(reinterpret_cast<char*>(out_ + 1), sizeof(out_) - 1, "gps:%d", g);
    write(out_, n);
  } else if (c == CMD_SET_CUSTOM_VAR && len >= 4) {
    char* name = reinterpret_cast<char*>(cmd_ + 1);
    char* val = std::strchr(name, ':');
    if (!val || !h_.gps_set || std::strncmp(name, "gps:", 4) != 0) return err(ERR_ILLEGAL_ARG);
    h_.gps_set(val[1] != '0');
    ok();
  } else if (c == CMD_GET_AUTOADD_CONFIG) {
    out_[0] = RESP_AUTOADD_CONFIG;
    out_[1] = 0x1E;  // alle typer tilføjes automatisk (chat, repeater, rum, sensor)
    out_[2] = 0;     // ingen grænse for hop
    write(out_, 3);
  } else if (c == CMD_SET_AUTOADD_CONFIG) {
    DLOG.println("[BLE] appens auto-tilfoej-indstilling ignoreret (alle kontakter tilfoejes)");
    ok();
  } else if (c == CMD_GET_ALLOWED_REPEAT_FREQ) {
    out_[0] = RESP_ALLOWED_REPEAT_FREQ;  // ingen: T-Deck'en er klient, ikke repeater
    write(out_, 1);
  } else if (c == CMD_GET_DEFAULT_FLOOD_SCOPE) {
    out_[0] = RESP_DEFAULT_FLOOD_SCOPE;  // intet navn/nøgle = ingen scope
    write(out_, 1);
  } else if (c == CMD_SET_DEVICE_PIN && len >= 5) {
    const uint32_t pin = get32(cmd_ + 1);
    if (pin != 0 && (pin < 100000 || pin > 999999)) return err(ERR_ILLEGAL_ARG);
    if (h_.set_pin) h_.set_pin(pin);
    ok();
  } else if (!handleExtra(c, len)) {
    DLOG.printf("[BLE] kommando %u ikke understoettet\n", unsigned(c));
    err(ERR_UNSUPPORTED);
  }
}

// ---- Resten af den officielle companion (17. nov 2026) -----------------------------------
// Rammeformater og forløb som MeshCores companion_radio/MyMesh.cpp (MIT, Copyright (c) 2025
// Scott Powell / rippleradios.com), så MeshCore-appen virker uændret.

void McCompanion::sent(int result, uint32_t tag, uint32_t est_timeout) {
  out_[0] = RESP_SENT;
  out_[1] = result == MSG_SEND_SENT_FLOOD ? 1 : 0;
  put32(out_ + 2, tag);
  put32(out_ + 6, est_timeout);
  write(out_, 10);
}

bool McCompanion::handleExtra(uint8_t c, size_t len) {
  if (c == CMD_SEND_LOGIN && len >= 1 + PUB_KEY_SIZE) {
    ContactInfo* to = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    if (!to) return err(ERR_NOT_FOUND), true;
    cmd_[len] = 0;  // kodeordet slutter her
    uint32_t est = 0;
    const int r = node_->sendLogin(*to, reinterpret_cast<const char*>(cmd_ + 1 + PUB_KEY_SIZE), est);
    if (r == MSG_SEND_FAILED) return err(ERR_TABLE_FULL), true;
    clearPending();
    std::memcpy(&pending_login_, to->id.pub_key, 4);
    DLOG.printf("[BLE] login hos \"%s\" (kode skjult)\n", to->name);
    sent(r, pending_login_, est);
  } else if (c == CMD_SEND_STATUS_REQ && len >= 1 + PUB_KEY_SIZE) {
    ContactInfo* to = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    if (!to) return err(ERR_NOT_FOUND), true;
    uint32_t tag = 0, est = 0;
    const int r = node_->sendRequest(*to, REQ_TYPE_GET_STATUS, tag, est);
    if (r == MSG_SEND_FAILED) return err(ERR_TABLE_FULL), true;
    clearPending();
    std::memcpy(&pending_status_, to->id.pub_key, 4);  // MeshCores matching (nøgle-præfiks)
    sent(r, tag, est);
  } else if (c == CMD_SEND_TELEMETRY_REQ && len >= 4 + PUB_KEY_SIZE) {
    ContactInfo* to = node_->lookupContactByPubKey(cmd_ + 4, PUB_KEY_SIZE);
    if (!to) return err(ERR_NOT_FOUND), true;
    uint32_t tag = 0, est = 0;
    const int r = node_->sendRequest(*to, kReqTelemetry, tag, est);
    if (r == MSG_SEND_FAILED) return err(ERR_TABLE_FULL), true;
    clearPending();
    pending_telemetry_ = tag;
    sent(r, tag, est);
  } else if (c == CMD_SEND_BINARY_REQ && len >= 2 + PUB_KEY_SIZE) {
    ContactInfo* to = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    if (!to) return err(ERR_NOT_FOUND), true;
    uint32_t tag = 0, est = 0;
    const int r = node_->sendRequest(*to, cmd_ + 1 + PUB_KEY_SIZE, uint8_t(len - (1 + PUB_KEY_SIZE)), tag, est);
    if (r == MSG_SEND_FAILED) return err(ERR_TABLE_FULL), true;
    clearPending();
    pending_req_ = tag;
    sent(r, tag, est);
  } else if (c == CMD_SEND_ANON_REQ && len > 1 + PUB_KEY_SIZE) {
    ContactInfo* to = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    ContactInfo anon;
    if (!to) {  // også noder, der ikke er kontakter (direkte, nul hop)
      std::memset(&anon, 0, sizeof(anon));
      std::memcpy(anon.id.pub_key, cmd_ + 1, PUB_KEY_SIZE);
      anon.out_path_len = 0;
      anon.type = ADV_TYPE_NONE;
      anon.lastmod = node_->getRTCClock()->getCurrentTime();
      if (node_->addContact(anon)) to = &anon;
    }
    if (!to) return err(ERR_TABLE_FULL), true;
    uint32_t tag = 0, est = 0;
    const int r = node_->sendAnonReq(*to, cmd_ + 1 + PUB_KEY_SIZE, uint8_t(len - (1 + PUB_KEY_SIZE)), tag, est);
    if (r == MSG_SEND_FAILED) return err(ERR_TABLE_FULL), true;
    clearPending();
    pending_req_ = tag;
    sent(r, tag, est);
  } else if (c == CMD_HAS_CONNECTION && len >= 1 + PUB_KEY_SIZE) {
    if (node_->connHas(cmd_ + 1)) ok();
    else err(ERR_NOT_FOUND);
  } else if (c == CMD_LOGOUT && len >= 1 + PUB_KEY_SIZE) {
    node_->connStop(cmd_ + 1);
    ok();
  } else if (c == CMD_SEND_PATH_DISCOVERY_REQ && len >= 2 + PUB_KEY_SIZE && cmd_[1] == 0) {
    ContactInfo* to = node_->lookupContactByPubKey(cmd_ + 2, PUB_KEY_SIZE);
    if (!to) return err(ERR_NOT_FOUND), true;
    // "Find sti" = telemetri-forespørgsel (kun grunddata) sendt som flood.
    uint8_t req[9];
    req[0] = kReqTelemetry;
    req[1] = uint8_t(~kTelemPermBase);
    std::memset(req + 2, 0, 3);
    node_->getRNG()->random(req + 5, 4);
    const uint8_t save = to->out_path_len;
    to->out_path_len = OUT_PATH_UNKNOWN;
    uint32_t tag = 0, est = 0;
    const int r = node_->sendRequest(*to, req, sizeof(req), tag, est);
    to->out_path_len = save;
    if (r == MSG_SEND_FAILED) return err(ERR_TABLE_FULL), true;
    clearPending();
    pending_discovery_ = tag;
    sent(r, tag, est);
  } else if (c == CMD_SEND_TRACE_PATH && len > 10 && len - 10 < MAX_PACKET_PAYLOAD - 5) {
    const uint8_t path_len = uint8_t(len - 10), flags = cmd_[9], sz = flags & 0x03;
    if ((path_len >> sz) > MAX_PATH_SIZE || (path_len % (1 << sz)) != 0) return err(ERR_ILLEGAL_ARG), true;
    mesh::Packet* pkt = node_->createTrace(get32(cmd_ + 1), get32(cmd_ + 5), flags);
    if (!pkt) return err(ERR_TABLE_FULL), true;
    static_cast<mesh::Mesh*>(node_)->sendDirect(pkt, cmd_ + 10, path_len);
    out_[0] = RESP_SENT;
    out_[1] = 0;
    std::memcpy(out_ + 2, cmd_ + 1, 4);
    put32(out_ + 6, 5000 + 2000u * (path_len >> sz));  // anslået ventetid
    write(out_, 10);
  } else if (c == CMD_SHARE_CONTACT && len >= 1 + PUB_KEY_SIZE) {
    ContactInfo* ct = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
    if (!ct) return err(ERR_NOT_FOUND), true;
    if (node_->shareContactZeroHop(*ct)) ok();
    else err(ERR_TABLE_FULL);
  } else if (c == CMD_EXPORT_CONTACT) {
    if (len < 1 + PUB_KEY_SIZE) {  // os selv: vores advert (position kun efter "Del position")
      double lat = 0, lon = 0;
      const bool pos = h_.position && h_.position(lat, lon);
      mesh::Packet* pkt = pos ? node_->createSelfAdvert(node_->nodeName(), lat, lon)
                              : node_->createSelfAdvert(node_->nodeName());
      if (!pkt) return err(ERR_TABLE_FULL), true;
      pkt->header |= ROUTE_TYPE_FLOOD;
      out_[0] = RESP_EXPORT_CONTACT;
      const uint8_t n = pkt->writeTo(out_ + 1);
      node_->releasePacket(pkt);
      write(out_, 1 + n);
    } else {
      ContactInfo* ct = node_->lookupContactByPubKey(cmd_ + 1, PUB_KEY_SIZE);
      uint8_t n = 0;
      if (!ct || (n = node_->exportContact(*ct, out_ + 1)) == 0) return err(ERR_NOT_FOUND), true;
      out_[0] = RESP_EXPORT_CONTACT;
      write(out_, 1 + n);
    }
  } else if (c == CMD_IMPORT_CONTACT && len > 2 + 32 + 64) {
    if (node_->importContact(cmd_ + 1, uint8_t(len - 1))) {
      node_->markContactsDirty();
      ok();
    } else {
      err(ERR_ILLEGAL_ARG);
    }
  } else if (c == CMD_GET_ADVERT_PATH && len >= PUB_KEY_SIZE + 2) {
    const McNode::AdvertPathRec* p = node_->findAdvertPath(cmd_ + 2);
    if (!p) return err(ERR_NOT_FOUND), true;
    size_t i = 0;
    out_[i++] = RESP_ADVERT_PATH;
    put32(out_ + i, p->ts);
    i += 4;
    out_[i++] = p->path_len;
    i += mesh::Packet::writePath(out_ + i, p->path, p->path_len);
    write(out_, i);
  } else if (c == CMD_GET_STATS && len >= 2) {
    size_t i = 0;
    out_[i++] = RESP_STATS;
    out_[i++] = cmd_[1];
    uint32_t recv = 0, sentn = 0, errs = 0;
    int16_t noise = 0;
    if (h_.radio_stats) h_.radio_stats(recv, sentn, errs, noise);
    if (cmd_[1] == STATS_CORE) {
      const uint16_t mv = h_.batt_mv ? h_.batt_mv() : 0;
      const uint32_t up = millis() / 1000;
      const uint16_t errflags = 0;
      std::memcpy(out_ + i, &mv, 2), i += 2;
      put32(out_ + i, up), i += 4;
      std::memcpy(out_ + i, &errflags, 2), i += 2;
      out_[i++] = uint8_t(node_->outboundTotal());
    } else if (cmd_[1] == STATS_RADIO) {
      const int8_t rssi = int8_t(node_->radioRssi()), snr = int8_t(node_->radioSnr() * 4);
      std::memcpy(out_ + i, &noise, 2), i += 2;
      out_[i++] = uint8_t(rssi);
      out_[i++] = uint8_t(snr);
      put32(out_ + i, uint32_t(node_->getTotalAirTime() / 1000)), i += 4;
      put32(out_ + i, uint32_t(node_->getReceiveAirTime() / 1000)), i += 4;
    } else if (cmd_[1] == STATS_PACKETS) {
      const uint32_t v[7] = {recv, sentn, node_->getNumSentFlood(), node_->getNumSentDirect(),
                             node_->getNumRecvFlood(), node_->getNumRecvDirect(), errs};
      for (uint32_t x : v) put32(out_ + i, x), i += 4;
    } else {
      return err(ERR_ILLEGAL_ARG), true;
    }
    write(out_, i);
  } else if (c == CMD_SIGN_START) {
    if (!sign_) sign_ = static_cast<uint8_t*>(heap_caps_malloc(kMaxSign, MALLOC_CAP_SPIRAM));
    sign_len_ = 0;
    if (!sign_) return err(ERR_TABLE_FULL), true;
    out_[0] = RESP_SIGN_START;
    out_[1] = 0;
    put32(out_ + 2, uint32_t(kMaxSign));
    write(out_, 6);
  } else if (c == CMD_SIGN_DATA && len > 1) {
    if (!sign_ || sign_len_ + (len - 1) > kMaxSign) return err(sign_ ? ERR_TABLE_FULL : ERR_BAD_STATE), true;
    std::memcpy(sign_ + sign_len_, cmd_ + 1, len - 1);
    sign_len_ += len - 1;
    ok();
  } else if (c == CMD_SIGN_FINISH) {
    if (!sign_) return err(ERR_BAD_STATE), true;
    node_->signData(out_ + 1, sign_, sign_len_);
    heap_caps_free(sign_);
    sign_ = nullptr;
    out_[0] = RESP_SIGNATURE;
    write(out_, 1 + SIGNATURE_SIZE);
  } else if (c == CMD_SEND_CHANNEL_DATA) {
    if (len < 4) return err(ERR_ILLEGAL_ARG), true;
    size_t i = 1;
    const uint8_t idx = cmd_[i++], path_len = cmd_[i++];
    if (!mesh::Packet::isValidPathLen(path_len) && path_len != OUT_PATH_UNKNOWN) return err(ERR_ILLEGAL_ARG), true;
    uint8_t path[MAX_PATH_SIZE];
    if (path_len != OUT_PATH_UNKNOWN) i += mesh::Packet::writePath(path, cmd_ + i, path_len);
    if (i + 2 > len) return err(ERR_ILLEGAL_ARG), true;
    const uint16_t type = uint16_t(cmd_[i] | (cmd_[i + 1] << 8));
    i += 2;
    const int plen = len > i ? int(len - i) : 0;
    ChannelDetails ch;
    if (!node_->channelAt(idx, ch) || !ch.name[0]) return err(ERR_NOT_FOUND), true;
    if (type == DATA_TYPE_RESERVED || plen > MAX_GROUP_DATA_LENGTH) return err(ERR_ILLEGAL_ARG), true;
    if (node_->sendGroupData(ch.channel, path, path_len, type, cmd_ + i, plen)) ok();
    else err(ERR_TABLE_FULL);
  } else if (c == CMD_SEND_CONTROL_DATA && len >= 2 && (cmd_[1] & 0x80)) {
    mesh::Packet* pkt = node_->createControlData(cmd_ + 1, len - 1);
    if (!pkt) return err(ERR_TABLE_FULL), true;
    node_->sendZeroHop(pkt);
    ok();
  } else if (c == CMD_SEND_RAW_DATA && len >= 6) {
    size_t i = 1;
    const uint8_t path_len = cmd_[i++];
    if (!mesh::Packet::isValidPathLen(path_len)) return err(ERR_UNSUPPORTED), true;  // flood: ikke endnu (som MeshCore)
    uint8_t path[MAX_PATH_SIZE];
    i += mesh::Packet::writePath(path, cmd_ + i, path_len);
    if (i + 4 > len) return err(ERR_ILLEGAL_ARG), true;
    mesh::Packet* pkt = node_->createRawData(cmd_ + i, len - i);
    if (!pkt) return err(ERR_TABLE_FULL), true;
    static_cast<mesh::Mesh*>(node_)->sendDirect(pkt, path, path_len);
    ok();
  } else if (c == CMD_SEND_RAW_PACKET && len >= 4) {
    mesh::Packet* pkt = node_->obtainNewPacket();
    if (!pkt) return err(ERR_TABLE_FULL), true;
    if (node_->tryParsePacket(pkt, cmd_ + 2, int(len - 2))) {
      node_->sendPacket(pkt, cmd_[1], 0);
      ok();
    } else {
      node_->releasePacket(pkt);
      err(ERR_ILLEGAL_ARG);
    }
  } else if (c == CMD_SET_TUNING_PARAMS && len >= 9) {
    rx_delay_ = get32(cmd_ + 1);  // vi bruger schedulerens egen timing; værdierne huskes kun
    airtime_factor_ = get32(cmd_ + 5);
    ok();
  } else if (c == CMD_GET_TUNING_PARAMS) {
    out_[0] = RESP_TUNING_PARAMS;
    put32(out_ + 1, rx_delay_);
    put32(out_ + 5, airtime_factor_);
    write(out_, 9);
  } else if (c == CMD_FACTORY_RESET && len >= 6 && std::memcmp(cmd_ + 1, "reset", 5) == 0) {
    DLOG.println("[BLE] fabriksnulstilling fra appen (= Nulstil DualMesh)");
    if (h_.factory_reset && h_.factory_reset()) ok();
    else err(ERR_FILE_IO);
  } else {
    return false;
  }
  return true;
}

// Svar på forespørgsler: login, status, telemetri, binær (ellers videre til appen som binær).
void McCompanion::onResponse(const ContactInfo& c, const uint8_t* data, uint8_t len) {
  uint32_t tag;
  std::memcpy(&tag, data, 4);
  size_t i = 0;
  if (pending_login_ && std::memcmp(&pending_login_, c.id.pub_key, 4) == 0) {
    pending_login_ = 0;
    if (len >= 6 && std::memcmp(data + 4, "OK", 2) == 0) {  // ældre repeatere
      out_[i++] = PUSH_LOGIN_SUCCESS;
      out_[i++] = 0;
      std::memcpy(out_ + i, c.id.pub_key, 6), i += 6;
    } else if (len >= 13 && data[4] == RESP_SERVER_LOGIN_OK) {
      const uint16_t keep_alive = uint16_t(data[5]) * 16;
      if (keep_alive) node_->connStart(c, keep_alive);
      out_[i++] = PUSH_LOGIN_SUCCESS;
      out_[i++] = data[6];  // rettigheder (fx admin)
      std::memcpy(out_ + i, c.id.pub_key, 6), i += 6;
      put32(out_ + i, tag), i += 4;
      out_[i++] = data[7];   // ACL-rettigheder
      out_[i++] = data[12];  // firmwareniveau
    } else {
      out_[i++] = PUSH_LOGIN_FAIL;
      out_[i++] = 0;
      std::memcpy(out_ + i, c.id.pub_key, 6), i += 6;
    }
    DLOG.printf("[MC] login hos \"%s\": %s\n", c.name, out_[0] == PUSH_LOGIN_SUCCESS ? "OK" : "afvist");
  } else if (len > 4 && pending_status_ && std::memcmp(&pending_status_, c.id.pub_key, 4) == 0) {
    pending_status_ = 0;
    out_[i++] = PUSH_STATUS_RESPONSE;
    out_[i++] = 0;
    std::memcpy(out_ + i, c.id.pub_key, 6), i += 6;
    std::memcpy(out_ + i, data + 4, len - 4), i += len - 4;
  } else if (len > 4 && pending_telemetry_ && tag == pending_telemetry_) {
    pending_telemetry_ = 0;
    out_[i++] = PUSH_TELEMETRY_RESPONSE;
    out_[i++] = 0;
    std::memcpy(out_ + i, c.id.pub_key, 6), i += 6;
    std::memcpy(out_ + i, data + 4, len - 4), i += len - 4;
  } else if (len > 4 && pending_req_ && tag == pending_req_) {
    pending_req_ = 0;
    out_[i++] = PUSH_BINARY_RESPONSE;
    out_[i++] = 0;
    put32(out_ + i, tag), i += 4;
    std::memcpy(out_ + i, data + 4, len - 4), i += len - 4;
  } else {  // ukendt/uopfordret svar (fx skubbet telemetri): videre til appen
    out_[i++] = PUSH_BINARY_RESPONSE;
    out_[i++] = 0;
    std::memcpy(out_ + i, data, len), i += len;
  }
  if (connected() && i <= sizeof(out_)) write(out_, i);
}

bool McCompanion::onPathRecv(ContactInfo& c, const uint8_t* in_path, uint8_t in_len, const uint8_t* out_path,
                             uint8_t out_len, uint8_t extra_type, const uint8_t* extra, uint8_t extra_len) {
  if (extra_type != PAYLOAD_TYPE_RESPONSE || extra_len <= 4 || !pending_discovery_) return false;
  uint32_t tag;
  std::memcpy(&tag, extra, 4);
  if (tag != pending_discovery_) return false;
  pending_discovery_ = 0;
  if (mesh::Packet::isValidPathLen(in_len) && mesh::Packet::isValidPathLen(out_len) && connected()) {
    size_t i = 0;
    out_[i++] = PUSH_PATH_DISCOVERY_RESPONSE;
    out_[i++] = 0;
    std::memcpy(out_ + i, c.id.pub_key, 6), i += 6;
    out_[i++] = out_len;
    i += mesh::Packet::writePath(out_ + i, out_path, out_len);
    out_[i++] = in_len;
    i += mesh::Packet::writePath(out_ + i, in_path, in_len);
    write(out_, i);
  }
  return true;  // send ikke vores sti tilbage
}

void McCompanion::onTrace(mesh::Packet* p, uint32_t tag, uint32_t auth, uint8_t flags, const uint8_t* snrs,
                          const uint8_t* hashes, uint8_t path_len) {
  const uint8_t sz = flags & 0x03;
  if (!connected() || 12 + path_len + (path_len >> sz) + 1 > sizeof(out_)) return;
  size_t i = 0;
  out_[i++] = PUSH_TRACE_DATA;
  out_[i++] = 0;
  out_[i++] = path_len;
  out_[i++] = flags;
  put32(out_ + i, tag), i += 4;
  put32(out_ + i, auth), i += 4;
  std::memcpy(out_ + i, hashes, path_len), i += path_len;
  std::memcpy(out_ + i, snrs, path_len >> sz), i += path_len >> sz;
  out_[i++] = uint8_t(int8_t(p->getSNR() * 4));
  write(out_, i);
}

void McCompanion::onControlData(mesh::Packet* p) {
  if (!connected() || p->payload_len + 4 > sizeof(out_)) return;
  size_t i = 0;
  out_[i++] = PUSH_CONTROL_DATA;
  out_[i++] = uint8_t(int8_t(node_->radioSnr() * 4));
  out_[i++] = uint8_t(int8_t(node_->radioRssi()));
  out_[i++] = p->path_len;
  std::memcpy(out_ + i, p->payload, p->payload_len), i += p->payload_len;
  write(out_, i);
}

// Appens "Rx Log": hver modtagen pakke som 0x88, SNR x 4, RSSI og de rå bytes (som MeshCores companion).
void McCompanion::onRxRaw(float snr, float rssi, const uint8_t* raw, int len) {
  if (!connected() || len <= 0 || size_t(len) + 3 > sizeof(out_)) return;
  size_t i = 0;
  out_[i++] = PUSH_LOG_RX_DATA;
  out_[i++] = uint8_t(int8_t(snr * 4));
  out_[i++] = uint8_t(int8_t(rssi));
  std::memcpy(out_ + i, raw, size_t(len)), i += size_t(len);
  write(out_, i);
}

void McCompanion::onRawData(mesh::Packet* p) {
  if (!connected() || p->payload_len + 4 > sizeof(out_)) return;
  size_t i = 0;
  out_[i++] = PUSH_RAW_DATA;
  out_[i++] = uint8_t(int8_t(node_->radioSnr() * 4));
  out_[i++] = uint8_t(int8_t(node_->radioRssi()));
  out_[i++] = 0xFF;
  std::memcpy(out_ + i, p->payload, p->payload_len), i += p->payload_len;
  write(out_, i);
}

void McCompanion::onChannelData(int idx, mesh::Packet* p, uint16_t type, const uint8_t* data, size_t len) {
  if (idx < 0 || len > BleSerial::kMaxFrame - 9) return;
  uint8_t f[BleSerial::kMaxFrame];
  size_t i = 0;
  f[i++] = RESP_CHANNEL_DATA_RECV;
  f[i++] = uint8_t(int8_t(p->getSNR() * 4));
  f[i++] = 0;
  f[i++] = 0;
  f[i++] = uint8_t(idx);
  f[i++] = p->isRouteFlood() ? p->path_len : 0xFF;
  f[i++] = uint8_t(type & 0xFF);
  f[i++] = uint8_t(type >> 8);
  f[i++] = uint8_t(len);
  std::memcpy(f + i, data, len), i += len;
  queueOffline(f, i, true);
  tickle();
}

}  // namespace dm
