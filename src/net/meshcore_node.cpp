// SPDX-License-Identifier: MIT
#include "storage/sd_log.h"
#include "net/meshcore_node.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <cstring>

#include "esp_random.h"
#include "storage/sd_crypt.h"
#include "util/b64.h"
#include "util/wall_clock.h"

namespace dm {

static constexpr const char* kDir = "/sd/dualmesh/meshcore";
static constexpr const char* kIdFile = "/sd/dualmesh/meshcore/identity.txt";
static constexpr const char* kIdTmp = "/sd/dualmesh/meshcore/identity.tmp";
static constexpr const char* kContactsFile = "/sd/dualmesh/meshcore/contacts.txt";
static constexpr const char* kContactsTmp = "/sd/dualmesh/meshcore/contacts.tmp";
static constexpr const char* kChannelsFile = "/sd/dualmesh/meshcore/channels.txt";
static constexpr const char* kChannelsTmp = "/sd/dualmesh/meshcore/channels.tmp";
// MeshCores offentlige kanal (fælles nøgle, som i alle MeshCore-apps).
static constexpr const char* kPublicPsk = "izOH6cXN6mrJ5e26oRXNcg==";

void EspRng::random(uint8_t* dest, size_t sz) { esp_fill_random(dest, sz); }

static const char* advType(uint8_t t) {
  switch (t) {
    case ADV_TYPE_CHAT: return "chat";
    case ADV_TYPE_REPEATER: return "repeater";
    case ADV_TYPE_ROOM: return "rum";
    case ADV_TYPE_SENSOR: return "sensor";
    default: return "?";
  }
}

// Identiteten gemmes som én hex-linje i MeshCores eget binære format
// (LocalIdentity::writeTo/readFrom: privat nøgle efterfulgt af offentlig nøgle).
static constexpr size_t kIdBytes = PRV_KEY_SIZE + PUB_KEY_SIZE;

bool McNode::loadIdentity() {
  FILE* f = sdcrypt::openRead(kIdFile);  // dekrypteret (se storage/sd_crypt.h)
  if (!f) return false;
  char hex[2 * kIdBytes + 8] = "", line[2 * kIdBytes + 16];
  while (std::fgets(line, sizeof(line), f)) {
    line[std::strcspn(line, "\r\n")] = 0;
    if (!std::strncmp(line, "id=", 3)) std::strncpy(hex, line + 3, sizeof(hex) - 1);
  }
  sdcrypt::closeRead(f);
  uint8_t buf[kIdBytes];
  if (std::strlen(hex) != 2 * kIdBytes || !mesh::Utils::fromHex(buf, sizeof(buf), hex)) return false;
  self_id.readFrom(buf, sizeof(buf));
  return true;
}

bool McNode::saveIdentity() {
  uint8_t buf[kIdBytes];
  if (self_id.writeTo(buf, sizeof(buf)) != sizeof(buf)) return false;
  char hex[2 * kIdBytes + 1];
  mesh::Utils::toHex(hex, buf, sizeof(buf));
  mkdir(kDir, 0775);
  FILE* f = sdcrypt::openWrite();
  if (!f) return false;
  std::fprintf(f, "# DualMesh MeshCore-identitet. HEMMELIG: indeholder den private noegle.\nid=%s\n", hex);
  return sdcrypt::commit(f, kIdFile);
}

void McNode::setName(const char* name) {
  if (name && *name)
    std::snprintf(name_, sizeof(name_), "%s", name);
  else
    std::snprintf(name_, sizeof(name_), "DualMesh-%02X%02X", self_id.pub_key[0], self_id.pub_key[1]);
}

void McNode::start(bool sd_ok, const char* name) {
  if (sd_ok && loadIdentity()) {
    id_note_ = "hentet fra SD";
  } else if (sd_ok && sdcrypt::unreadable(kIdFile)) {
    // Krypteret med en anden nøgle (låst/andet kort): brug en midlertidig identitet
    // og overskriv ALDRIG den gemte.
    self_id = mesh::LocalIdentity(&rng_);
    id_note_ = "MIDLERTIDIG (gemt identitet kan ikke dekrypteres)";
  } else {
    self_id = mesh::LocalIdentity(&rng_);  // nyt Ed25519-nøglepar
    id_note_ = !sd_ok ? "midlertidig (intet SD-kort)"
                      : (saveIdentity() ? "ny, gemt paa SD" : "ny, KUNNE IKKE gemmes");
  }
  if (name && *name) {
    std::snprintf(name_, sizeof(name_), "%s", name);
  } else {
    std::snprintf(name_, sizeof(name_), "DualMesh-%02X%02X", self_id.pub_key[0], self_id.pub_key[1]);
  }
  sd_ok_ = sd_ok;
  begin();
  addChannel("Public", kPublicPsk);
  if (sd_ok_) {
    loadChannels();
    const int n = loadContacts();
    if (n > 0) dm::DLOG.printf("[MC] %d kontakter hentet fra SD\n", n);
  }
}

const char* McNode::channelName(const mesh::GroupChannel& ch) {
  static char name[32];
  const int i = findChannelIdx(ch);
  ChannelDetails d;
  if (i < 0 || !getChannel(i, d) || !d.name[0]) return "?";
  snprintf(name, sizeof(name), "%s", d.name);
  return name;
}

int McNode::channelCount() {
  int n = 0;
  ChannelDetails d;
  while (n < MAX_GROUP_CHANNELS && getChannel(n, d) && d.name[0]) ++n;
  return n;
}

// Nøglelængde: 16 byte, medmindre de sidste 16 ikke er nul (som MeshCore selv).
static int keyLen(const mesh::GroupChannel& ch) {
  for (int i = 16; i < 32; ++i)
    if (ch.secret[i]) return 32;
  return 16;
}

void McNode::channelConvId(const mesh::GroupChannel& ch, char* out, size_t n) {
  snprintf(out, n, "mcc_%08lx", (unsigned long)fnv1a(ch.secret, size_t(keyLen(ch))));
}

void McNode::dmConvId(const uint8_t* pub, char* out, size_t n) {
  snprintf(out, n, "mcd_%02x%02x%02x%02x%02x%02x", pub[0], pub[1], pub[2], pub[3], pub[4], pub[5]);
}

void McNode::channelKeyB64(const mesh::GroupChannel& ch, char* out, size_t n) {
  b64Encode(ch.secret, size_t(keyLen(ch)), out, n);
}

int McNode::addUserChannel(const char* name_in, const char* psk_b64, const char** note) {
  static const char* dummy;
  if (!note) note = &dummy;
  char name[32];
  snprintf(name, sizeof(name), "%s", name_in ? name_in : "");
  while (std::strlen(name) && name[std::strlen(name) - 1] == ' ') name[std::strlen(name) - 1] = 0;
  if (!name[0]) {
    *note = "Navnet mangler";
    return -1;
  }
  uint8_t key[32] = {};
  int klen = 0;
  const bool hashtag = name[0] == '#';
  if (psk_b64 && *psk_b64) {
    klen = b64Decode(psk_b64, key, sizeof(key));
    if (klen != 16 && klen != 32) {
      *note = "Nøglen skal være 16/32 byte base64";
      return -1;
    }
  } else if (hashtag) {
    for (char* p = name; *p; ++p) *p = char(std::tolower(uint8_t(*p)));
    if (std::strlen(name) < 2) {
      *note = "Skriv et navn efter #";
      return -1;
    }
    mesh::Utils::sha256(key, 16, reinterpret_cast<const uint8_t*>(name), int(std::strlen(name)));
    klen = 16;
  } else {
    esp_fill_random(key, 16);  // ny privat kanal
    klen = 16;
  }
  // Findes den allerede (samme nøgle)?
  const int n = channelCount();
  for (int i = 0; i < n; ++i) {
    ChannelDetails d;
    if (getChannel(i, d) && std::memcmp(d.channel.secret, key, 32) == 0) {
      *note = "Kanalen findes allerede";
      return i;
    }
  }
  if (n >= MAX_GROUP_CHANNELS) {
    *note = "For mange kanaler";
    return -1;
  }
  char b64[48];
  b64Encode(key, size_t(klen), b64, sizeof(b64));
  if (putChannel(name, b64) < 0) {
    *note = "Kunne ikke tilføje kanalen";
    return -1;
  }
  *note = !sd_ok_ ? "Tilføjet (gemmes ikke: intet SD)"
                  : (saveChannels() ? "Kanal tilføjet og gemt" : "Tilføjet, men IKKE gemt");
  dm::DLOG.printf("[MC] kanal tilfoejet: %s (%s, %d byte noegle)\n", name,
                  psk_b64 && *psk_b64 ? "egen noegle" : (hashtag ? "hashtag" : "ny tilfaeldig"),
                  klen);
  return n;
}

int McNode::putChannel(const char* name, const char* psk_b64) {
  const int idx = channelCount();
  if (idx >= MAX_GROUP_CHANNELS || !name || !*name) return -1;
  ChannelDetails d;
  std::memset(&d, 0, sizeof(d));
  const int len = b64Decode(psk_b64, d.channel.secret, sizeof(d.channel.secret));
  if (len != 16 && len != 32) return -1;
  snprintf(d.name, sizeof(d.name), "%s", name);
  return setChannel(idx, d) ? idx : -1;
}

void McNode::compactChannels() {
  int w = 0;
  for (int r = 0; r < MAX_GROUP_CHANNELS; ++r) {
    ChannelDetails d;
    if (!getChannel(r, d) || !d.name[0]) continue;
    if (w != r) setChannel(w, d);
    ++w;
  }
  ChannelDetails z;
  std::memset(&z, 0, sizeof(z));
  for (int i = w; i < MAX_GROUP_CHANNELS; ++i) {
    ChannelDetails d;
    if (getChannel(i, d) && d.name[0]) setChannel(i, z);
  }
}

bool McNode::removeChannel(int idx) {
  const int n = channelCount();
  if (idx < 1 || idx >= n) return false;  // Public (0) kan ikke slettes
  ChannelDetails gone;
  getChannel(idx, gone);
  for (int i = idx; i + 1 < n; ++i) {
    ChannelDetails d;
    getChannel(i + 1, d);
    setChannel(i, d);
  }
  ChannelDetails z;
  std::memset(&z, 0, sizeof(z));
  setChannel(n - 1, z);
  const bool saved = saveChannels();
  dm::DLOG.printf("[MC] kanal slettet: %s (%s)\n", gone.name, saved ? "gemt" : sd_ok_ ? "IKKE gemt" : "intet SD");
  return true;
}

// Kanaler ud over Public: "navn<TAB>nøgle-base64" pr. linje.
bool McNode::loadChannels() {
  FILE* f = sdcrypt::openRead(kChannelsFile);
  if (!f) return false;
  char line[128];
  int n = 0;
  while (std::fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    line[std::strcspn(line, "\r\n")] = 0;
    char* tab = std::strchr(line, '\t');
    if (!tab) continue;
    *tab = 0;
    if (channelCount() >= MAX_GROUP_CHANNELS) break;
    if (putChannel(line, tab + 1) >= 0) ++n;
  }
  sdcrypt::closeRead(f);
  dm::DLOG.printf("[MC] %d kanaler hentet fra SD\n", n);
  return true;
}

bool McNode::saveChannels() {
  if (!sd_ok_) return false;
  mkdir(kDir, 0775);
  FILE* f = sdcrypt::openWrite();
  if (!f) return false;
  std::fprintf(f, "# DualMesh MeshCore-kanaler (Public er indbygget). navn<TAB>noegle-base64. HEMMELIG.\n");
  const int n = channelCount();
  for (int i = 1; i < n; ++i) {
    ChannelDetails d;
    if (!getChannel(i, d)) continue;
    char b64[48];
    channelKeyB64(d.channel, b64, sizeof(b64));
    std::fprintf(f, "%s\t%s\n", d.name, b64);
  }
  return sdcrypt::commit(f, kChannelsFile);
}

// Ur uden RTC/GPS: saml afsendernes tid (som forskel til vores oppetid) og stol
// først på den, når 3 forskellige afsendere er enige inden for 2 min (én node med
// forkert ur kan ikke flytte os). Medianen af de enige bruges. Kun én gang pr. opstart.
void McNode::noteRemoteTime(uint32_t ts, uint32_t src) {
  if (wallclock::valid() || ts < 1735689600u) return;  // før 1/1-2025 = uret er ikke sat hos afsenderen
  const int64_t up = int64_t(millis() / 1000);
  const int64_t off = int64_t(ts) - up;
  int slot = -1;
  for (int i = 0; i < n_tcand_; ++i)
    if (tcand_[i].src == src) slot = i;
  if (slot < 0) {
    if (n_tcand_ < kTimeCands) slot = n_tcand_++;
    else slot = next_tcand_++ % kTimeCands;
  }
  tcand_[slot] = {off, src};
  for (int i = 0; i < n_tcand_; ++i) {
    int64_t agree[kTimeCands];
    int n = 0;
    for (int j = 0; j < n_tcand_; ++j) {
      const int64_t d = tcand_[j].off - tcand_[i].off;
      if (d >= -120 && d <= 120) agree[n++] = tcand_[j].off;
    }
    if (n < 3) continue;
    std::sort(agree, agree + n);
    const uint32_t now = uint32_t(agree[n / 2] + up);
    DLOG.printf("[UR] %d MeshCore-afsendere er enige om tiden\n", n);
    wallclock::set(now, wallclock::Mesh);
    return;
  }
}

bool McNode::timeValid() const { return wallclock::valid(); }
uint32_t McNode::nowTs() const { return wallclock::now(); }

const McNode::AdvertPathRec* McNode::findAdvertPath(const uint8_t* pub_prefix) const {
  for (const AdvertPathRec& r : advert_paths_)
    if (r.ts && std::memcmp(r.prefix, pub_prefix, sizeof(r.prefix)) == 0) return &r;
  return nullptr;
}

void McNode::onDiscoveredContact(ContactInfo& c, bool is_new, uint8_t path_len, const uint8_t* path) {
  stats_.adverts++;
  // Advertens sti gemmes (nyeste 16, ældste overskrives), så appen kan vise den.
  if (path && mesh::Packet::isValidPathLen(path_len)) {
    AdvertPathRec* p = &advert_paths_[0];
    for (AdvertPathRec& r : advert_paths_) {
      if (std::memcmp(r.prefix, c.id.pub_key, sizeof(r.prefix)) == 0) {
        p = &r;
        break;
      }
      if (r.ts < p->ts) p = &r;
    }
    std::memcpy(p->prefix, c.id.pub_key, sizeof(p->prefix));
    p->ts = getRTCClock()->getCurrentTime();
    if (!p->ts) p->ts = 1;
    p->path_len = mesh::Packet::copyPath(p->path, path, path_len);
  }
  if (!clock_set_ && !wallclock::valid()) {  // intet RTC: fortsæt fra kontakternes tid (monotont)
    bootstrapRTCfromContacts();
    clock_set_ = true;
  }
  noteRemoteTime(c.last_advert_timestamp, fnv1a(c.id.pub_key, 4));
  if (is_new) stats_.new_contacts++;
  if (listener_) listener_->onAdvertRecv(c, is_new);
  contacts_dirty_ = true;
  dm::DLOG.printf("[MC] advert %s: \"%s\" (%s) hop=%u", is_new ? "NY" : "kendt", c.name, advType(c.type),
                unsigned(path_len & 63));
  if (c.gps_lat || c.gps_lon) dm::DLOG.printf(" pos=%.4f,%.4f", c.gps_lat / 1e6, c.gps_lon / 1e6);
  dm::DLOG.printf(" kontakter=%d\n", getNumContacts());
}

void McNode::onMessageRecv(const ContactInfo& c, mesh::Packet* pkt, uint32_t sender_ts, const char* text) {
  stats_.direct_msgs++;
  if (listener_) listener_->onDmRecv(c, pkt, sender_ts, text);
  noteRemoteTime(sender_ts, fnv1a(c.id.pub_key, 4));
  dm::DLOG.printf("[MC] DM fra \"%s\": %s\n", c.name, text);
  if (store_) {
    Msg m;
    m.rx_ms = millis();
    m.ts = nowTs();
    m.kind = Msg::DirectIn;
    if (pkt) {
      m.has_sig = true;
      m.rssi = int16_t(radioRssi());
      m.snr_q4 = pkt->_snr;
    }
    dmConvId(c.id.pub_key, m.conv, sizeof(m.conv));
    snprintf(m.peer, sizeof(m.peer), "%s", c.name);
    snprintf(m.text, sizeof(m.text), "%s", text);
    store_->push(m);
  }
}

void McNode::onSignedMessageRecv(const ContactInfo& c, mesh::Packet*, uint32_t, const uint8_t*,
                                 const char* text) {
  stats_.direct_msgs++;
  dm::DLOG.printf("[MC] signeret DM fra \"%s\": %s\n", c.name, text);
}

void McNode::onChannelMessageRecv(const mesh::GroupChannel& ch, mesh::Packet* pkt, uint32_t sender_ts,
                                  const char* text) {
  stats_.channel_msgs++;
  {  // afsenderen står før ": " i teksten
    const char* colon = std::strstr(text, ": ");
    noteRemoteTime(sender_ts, fnv1a(text, colon ? size_t(colon - text) : std::strlen(text)));
  }
  dm::DLOG.printf("[MC] #%s (hop=%u): %s\n", channelName(ch), unsigned(pkt->getPathHashCount()), text);
  if (listener_) listener_->onChannelRecv(findChannelIdx(ch), pkt, sender_ts, text);
  if (store_) {
    Msg m;
    m.rx_ms = millis();
    m.ts = nowTs();
    m.kind = Msg::ChannelIn;
    m.hops = pkt->getPathHashCount();
    m.has_sig = true;
    m.rssi = int16_t(radioRssi());
    m.snr_q4 = pkt->_snr;
    channelConvId(ch, m.conv, sizeof(m.conv));
    snprintf(m.peer, sizeof(m.peer), "%s", channelName(ch));
    snprintf(m.text, sizeof(m.text), "%s", text);
    store_->push(m);
  }
}

bool McNode::importPrivateKey(const uint8_t prv[64]) {
  if (!mesh::LocalIdentity::validatePrivateKey(prv)) return false;
  mesh::LocalIdentity id;
  id.readFrom(prv, 64);  // den offentlige nøgle udledes
  const mesh::LocalIdentity old = self_id;
  self_id = id;
  if (!sd_ok_ || !saveIdentity()) {
    self_id = old;
    return false;
  }
  dm::DLOG.println("[MC] ny identitet importeret fra MeshCore-appen (gemt) - genstart kræves");
  return true;
}

// Andre noders forespørgsler. Kun telemetri (REQ_TYPE_GET_TELEMETRY_DATA = 3) besvares,
// og kun som brugeren har tilladt (Indst. > MeshCore > Telemetri-svar m.fl.).
uint8_t McNode::onContactRequest(const ContactInfo& c, uint32_t ts, const uint8_t* data, uint8_t len,
                                 uint8_t* reply) {
  if (!len || data[0] != 0x03 || !telem_fn_) return 0;
  const uint8_t n = telem_fn_(c, len > 1 ? data[1] : 0, reply + 4, 100);
  if (!n) {
    dm::DLOG.printf("[MC] telemetri-forespoergsel fra \"%s\" afvist (ikke tilladt)\n", c.name);
    return 0;
  }
  std::memcpy(reply, &ts, 4);  // afsenderens tid spejles tilbage (som MeshCore)
  dm::DLOG.printf("[MC] telemetri sendt til \"%s\" (%u byte)\n", c.name, unsigned(n));
  return uint8_t(4 + n);
}

bool McNode::sendAdvert() { return sendAdvertMode(true); }

int McNode::sendDirectFromApp(const ContactInfo& to, uint32_t ts, uint8_t attempt, const char* text,
                              uint32_t& ack, uint32_t& timeout) {
  const int r = sendMessage(to, ts, attempt, text, ack, timeout);
  if (store_ && attempt == 0) {
    Msg m;
    m.rx_ms = millis();
    m.ts = nowTs();
    m.kind = Msg::DirectOut;
    m.status = r == MSG_SEND_FAILED ? Msg::Failed : Msg::Queued;
    dmConvId(to.id.pub_key, m.conv, sizeof(m.conv));
    snprintf(m.peer, sizeof(m.peer), "%s", to.name);
    snprintf(m.text, sizeof(m.text), "%s", text);
    store_->push(m, true);
  }
  dm::DLOG.printf("[BLE] DM fra appen til \"%s\" %s\n", to.name, r == MSG_SEND_FAILED ? "FEJLEDE" : "sendt");
  return r;
}

bool McNode::sendChannelFromApp(int idx, uint32_t ts, const char* text) {
  ChannelDetails d;
  if (!text || !*text || !getChannel(idx, d)) return false;
  const bool ok = sendGroupMessage(ts, d.channel, name_, text, int(std::strlen(text)));
  if (store_) {
    Msg m;
    m.rx_ms = millis();
    m.ts = nowTs();
    m.kind = Msg::ChannelOut;
    m.status = ok ? Msg::Queued : Msg::Failed;
    channelConvId(d.channel, m.conv, sizeof(m.conv));
    snprintf(m.peer, sizeof(m.peer), "%s", d.name);
    snprintf(m.text, sizeof(m.text), "%s", text);
    store_->push(m);
  }
  dm::DLOG.printf("[BLE] kanalbesked fra appen til %s %s\n", d.name, ok ? "sendt" : "FEJLEDE");
  return ok;
}

bool McNode::sendAdvertMode(bool flood) {
  double lat = 0, lon = 0;
  const bool pos = pos_fn_ && pos_fn_(lat, lon);
  mesh::Packet* p = pos ? createSelfAdvert(name_, lat, lon) : createSelfAdvert(name_);
  dm::DLOG.printf("[MC] advert %s%s\n", pos ? "MED position" : "uden position", flood ? "" : " (nul hop)");
  if (!p) return false;
  if (flood) sendFlood(p);
  else sendZeroHop(p);
  return true;
}

bool McNode::sendChannel(int idx, const char* text, bool record) {
  ChannelDetails d;
  if (!text || !*text || idx < 0 || idx >= channelCount() || !getChannel(idx, d)) return false;
  const bool ok = sendGroupMessage(getRTCClock()->getCurrentTime(), d.channel, name_, text,
                                   int(std::strlen(text)));
  if (store_ && record) {
    Msg m;
    m.rx_ms = millis();
    m.ts = nowTs();
    m.kind = Msg::ChannelOut;
    m.status = ok ? Msg::Queued : Msg::Failed;
    channelConvId(d.channel, m.conv, sizeof(m.conv));
    snprintf(m.peer, sizeof(m.peer), "%s", d.name);
    snprintf(m.text, sizeof(m.text), "%s: %s", name_, text);
    store_->push(m);
  }
  return ok;
}

bool McNode::sendDirect(const ContactInfo& to, const char* text) {
  if (!text || !*text) return false;
  uint32_t ack = 0, timeout = 0;
  const int r = sendMessage(to, getRTCClock()->getCurrentTime(), 0, text, ack, timeout);
  uint32_t seq = 0;
  if (store_) {
    Msg m;
    m.rx_ms = millis();
    m.ts = nowTs();
    m.kind = Msg::DirectOut;
    m.status = r == MSG_SEND_FAILED ? Msg::Failed : Msg::Queued;
    dmConvId(to.id.pub_key, m.conv, sizeof(m.conv));
    snprintf(m.peer, sizeof(m.peer), "%s", to.name);
    snprintf(m.text, sizeof(m.text), "%s", text);
    // Gemmes på SD, når status er endelig (leveret/fejl).
    seq = store_->push(m, r == MSG_SEND_FAILED);
  }
  if (r == MSG_SEND_FAILED) return false;
  if (pending_seq_ && store_) store_->setStatus(pending_seq_, Msg::Failed, true);  // erstattet
  pending_ack_ = ack;
  std::memcpy(pending_pub_, to.id.pub_key, PUB_KEY_SIZE);
  pending_seq_ = seq;
  dm::DLOG.printf("[MC] DM til \"%s\" sendt (%s), venter paa kvittering\n", to.name,
                r == MSG_SEND_SENT_DIRECT ? "direkte" : "flood");
  return true;
}

ContactInfo* McNode::processAck(const uint8_t* data) {
  if (!pending_ack_ || std::memcmp(data, &pending_ack_, 4) != 0)
    return listener_ ? listener_->onAckRecv(data) : nullptr;
  pending_ack_ = 0;
  if (pending_seq_ && store_) store_->setStatus(pending_seq_, Msg::Delivered, true);
  pending_seq_ = 0;
  dm::DLOG.println("[MC] DM kvitteret (leveret)");
  return lookupContactByPubKey(pending_pub_, PUB_KEY_SIZE);
}

void McNode::onSendTimeout() {
  if (pending_seq_) {
    if (store_) store_->setStatus(pending_seq_, Msg::Failed, true);
    dm::DLOG.println("[MC] DM: ingen kvittering (timeout)");
  }
  pending_seq_ = 0;
  pending_ack_ = 0;
}

// Kontakter: én linje pr. kontakt, tab-separeret, navnet sidst:
// pub_hex  type  flags  out_path_len  out_path_hex  last_advert  lastmod  lat  lon  navn
int McNode::loadContacts() {
  FILE* f = std::fopen(kContactsFile, "r");
  if (!f) return 0;
  int n = 0;
  char line[512];
  while (std::fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    line[std::strcspn(line, "\r\n")] = 0;
    char* fld[10] = {};
    int k = 0;
    for (char* p = line; k < 10 && p; ++k) {
      fld[k] = p;
      char* t = (k < 9) ? std::strchr(p, '\t') : nullptr;
      if (t) *t = 0;
      p = t ? t + 1 : nullptr;
    }
    if (k < 10 || std::strlen(fld[0]) != 2 * PUB_KEY_SIZE) continue;
    ContactInfo c;
    std::memset(&c, 0, sizeof(c));
    uint8_t pub[PUB_KEY_SIZE];
    if (!mesh::Utils::fromHex(pub, PUB_KEY_SIZE, fld[0])) continue;
    c.id = mesh::Identity(pub);
    c.type = uint8_t(std::strtoul(fld[1], nullptr, 10));
    c.flags = uint8_t(std::strtoul(fld[2], nullptr, 10));
    c.out_path_len = uint8_t(std::strtoul(fld[3], nullptr, 10));
    if (std::strlen(fld[4]) == 2 * MAX_PATH_SIZE) mesh::Utils::fromHex(c.out_path, MAX_PATH_SIZE, fld[4]);
    c.last_advert_timestamp = std::strtoul(fld[5], nullptr, 10);
    c.lastmod = std::strtoul(fld[6], nullptr, 10);
    c.gps_lat = int32_t(std::strtol(fld[7], nullptr, 10));
    c.gps_lon = int32_t(std::strtol(fld[8], nullptr, 10));
    snprintf(c.name, sizeof(c.name), "%s", fld[9]);
    c.shared_secret_valid = false;
    if (addContact(c)) ++n;
  }
  std::fclose(f);
  contacts_dirty_ = false;
  return n;
}

bool McNode::saveContactsIfDirty(uint32_t min_interval_ms) {
  if (!sd_ok_ || !contacts_dirty_) return true;
  const uint32_t now = millis();
  if (last_contacts_save_ms_ && now - last_contacts_save_ms_ < min_interval_ms) return true;
  last_contacts_save_ms_ = now;
  mkdir(kDir, 0775);
  FILE* f = std::fopen(kContactsTmp, "w");
  if (!f) return false;
  std::fprintf(f, "# DualMesh MeshCore-kontakter (tab-separeret). Navnet er sidste felt.\n");
  ContactsIterator it = startContactsIterator();
  ContactInfo c;
  int n = 0;
  char pub[2 * PUB_KEY_SIZE + 1], path[2 * MAX_PATH_SIZE + 1];
  while (it.hasNext(this, c)) {
    mesh::Utils::toHex(pub, c.id.pub_key, PUB_KEY_SIZE);
    mesh::Utils::toHex(path, c.out_path, MAX_PATH_SIZE);
    std::fprintf(f, "%s\t%u\t%u\t%u\t%s\t%lu\t%lu\t%ld\t%ld\t%s\n", pub, c.type, c.flags,
                 c.out_path_len, path, (unsigned long)c.last_advert_timestamp,
                 (unsigned long)c.lastmod, (long)c.gps_lat, (long)c.gps_lon, c.name);
    ++n;
  }
  std::fflush(f);
  const bool ok = std::ferror(f) == 0;
  std::fclose(f);
  if (!ok) return false;
  std::remove(kContactsFile);
  if (std::rename(kContactsTmp, kContactsFile) != 0) return false;
  contacts_dirty_ = false;
  dm::DLOG.printf("[MC] %d kontakter gemt paa SD\n", n);
  return true;
}

int McNode::contactsSnapshot(ContactInfo* out, int max) {
  int n = 0;
  ContactsIterator it = startContactsIterator();
  ContactInfo c;
  while (n < max && it.hasNext(this, c)) out[n++] = c;
  // nyeste først (sidst ændret efter vores ur)
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && out[j].lastmod > out[j - 1].lastmod; --j) {
      ContactInfo t = out[j];
      out[j] = out[j - 1];
      out[j - 1] = t;
    }
  return n;
}

// Samme tidsgrænser som MeshCores companion-firmware (MIT).
uint32_t McNode::calcFloodTimeoutMillisFor(uint32_t airtime) const { return 500 + 16 * airtime; }
uint32_t McNode::calcDirectTimeoutMillisFor(uint32_t airtime, uint8_t path_len) const {
  return 500 + (airtime * 6 + 250) * ((path_len & 63) + 1);
}

}  // namespace dm
