// SPDX-License-Identifier: MIT
#include "net/mt_node.h"

#include <AES.h>
#include <Arduino.h>
#include <CTR.h>
#include <esp_heap_caps.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net/channel_scan.h"
#include "net/msg_store.h"
#include "net/mt_pki.h"
#include "net/mt_tx.h"
#include "storage/sd_log.h"
#include "util/wall_clock.h"

namespace dm {

namespace {

constexpr const char* kFile = "/sd/dualmesh/meshtastic/nodes.txt";
constexpr const char* kTmp = "/sd/dualmesh/meshtastic/nodes.tmp";
constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
constexpr uint32_t kSaveEveryMs = 60000;

// Portnumre (PortNum).
enum : uint32_t {
  kPortText = 1,
  kPortPosition = 3,
  kPortNodeInfo = 4,
  kPortRouting = 5,
  kPortWaypoint = 8,
  kPortTelemetry = 67,
  kPortTraceroute = 70,
};

uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Minimal protobuf-læser: felter i rækkefølge; ukendte springes over.
struct Pb {
  const uint8_t* p;
  size_t n, i = 0;
  Pb(const uint8_t* d, size_t len) : p(d), n(len) {}
  bool varint(uint64_t& v) {
    v = 0;
    for (int s = 0; s < 64 && i < n; s += 7) {
      const uint8_t b = p[i++];
      v |= uint64_t(b & 0x7F) << s;
      if (!(b & 0x80)) return true;
    }
    return false;
  }
  // Næste felt; false ved slut eller fejl.
  bool next(uint32_t& field, uint8_t& wt) {
    if (i >= n) return false;
    uint64_t key;
    if (!varint(key)) return false;
    field = uint32_t(key >> 3);
    wt = uint8_t(key & 7);
    return field != 0;
  }
  bool fixed32(uint32_t& v) {
    if (i + 4 > n) return false;
    v = le32(p + i);
    i += 4;
    return true;
  }
  bool bytes(const uint8_t*& b, size_t& l) {
    uint64_t len;
    if (!varint(len) || i + len > n) return false;
    b = p + i;
    l = size_t(len);
    i += l;
    return true;
  }
  bool skip(uint8_t wt) {
    uint64_t v;
    const uint8_t* b;
    size_t l;
    switch (wt) {
      case 0: return varint(v);
      case 1: i += 8; return i <= n;
      case 2: return bytes(b, l);
      case 5: i += 4; return i <= n;
      default: return false;
    }
  }
};

void copyStr(char* out, size_t cap, const uint8_t* s, size_t n) {
  size_t k = 0;
  for (size_t j = 0; j < n && k + 1 < cap; ++j) {
    const char c = char(s[j]);
    out[k++] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;  // ikke i vores filformat
  }
  // Skær et halvt UTF-8-tegn af.
  while (k > 0 && (uint8_t(out[k - 1]) & 0xC0) == 0x80) {
    size_t s = k - 1;
    while (s > 0 && (uint8_t(out[s]) & 0xC0) == 0x80) --s;
    const uint8_t lead = uint8_t(out[s]);
    const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (k - s >= need) break;
    k = s;
  }
  out[k] = 0;
}

void toHex(const uint8_t* b, size_t n, char* out) {
  static const char* h = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = h[b[i] >> 4];
    out[2 * i + 1] = h[b[i] & 15];
  }
  out[2 * n] = 0;
}

bool fromHex(const char* s, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    char t[3] = {s[2 * i], s[2 * i + 1], 0};
    if (!t[0] || !t[1]) return false;
    char* e;
    out[i] = uint8_t(strtoul(t, &e, 16));
    if (*e) return false;
  }
  return true;
}

}  // namespace

void MtNode::dmConvId(uint32_t num, char* out, size_t n) {
  snprintf(out, n, "mtd_%08lx", (unsigned long)num);
}

const char* MtNode::hwName(uint8_t hw) {
  switch (hw) {
    case 0: return "ukendt";
    case 4: return "T-Beam";
    case 9: return "RAK4631";
    case 43: return "Heltec V3";
    case 44: return "Heltec WSL V3";
    case 50: return "T-Deck";
    case 255: return "privat";
    default: return "andet";
  }
}

const char* MtNode::roleName(uint8_t role) {
  static const char* n[] = {"client", "mute", "router", "router-client", "repeater", "tracker",
                            "sensor", "tak", "hidden", "lost&found", "tak-tracker", "router-late"};
  return role < sizeof(n) / sizeof(n[0]) ? n[role] : "?";
}

void MtNode::begin(ChannelScan* scan, MsgStore* store, bool sd) {
  scan_ = scan;
  store_ = store;
  sd_ = sd;
  if (!nodes_)
    nodes_ = static_cast<MtNodeEntry*>(heap_caps_calloc(kMax, sizeof(MtNodeEntry), MALLOC_CAP_SPIRAM));
  if (sd_) load();
  if (mt_pki::begin(sd_)) mt_tx::setNodeNum(mt_pki::nodeNum());
  DLOG.printf("[MT] node !%08lx klar, %d kendte noder fra SD\n", (unsigned long)mt_tx::nodeNum(), n_);
}

const MtNodeEntry* MtNode::find(uint32_t num) const {
  for (int i = 0; i < n_; ++i)
    if (nodes_[i].num == num) return &nodes_[i];
  return nullptr;
}

const char* MtNode::nameOf(uint32_t num, char* buf, size_t n) const {
  const MtNodeEntry* e = find(num);
  if (e && e->long_name[0]) snprintf(buf, n, "%s", e->long_name);
  else if (e && e->short_name[0]) snprintf(buf, n, "%s", e->short_name);
  else snprintf(buf, n, "!%08lx", (unsigned long)num);
  return buf;
}

// Find/opret noden og flyt den forrest (nyeste først).
MtNodeEntry* MtNode::touch(uint32_t num, int16_t rssi, int8_t snr_q4, uint8_t hops, uint32_t t_ms) {
  if (!nodes_) return nullptr;
  int i = 0;
  while (i < n_ && nodes_[i].num != num) ++i;
  MtNodeEntry e{};
  if (i < n_) {
    e = nodes_[i];
  } else {
    e.num = num;
    if (n_ < kMax) ++n_;
    i = n_ - 1;  // den ældste overskrives, hvis databasen er fuld
  }
  memmove(&nodes_[1], &nodes_[0], sizeof(MtNodeEntry) * size_t(i));
  e.rssi = rssi;
  e.snr_q4 = snr_q4;
  e.hops = hops;
  e.last_ms = t_ms ? t_ms : 1;
  e.last_ts = wallclock::now();
  ++e.pkts;
  nodes_[0] = e;
  dirty_ = true;
  ++version_;
  return &nodes_[0];
}

bool MtNode::seen(uint32_t from, uint32_t id) {
  for (auto& s : seen_)
    if (s[0] == from && s[1] == id) return true;
  seen_[seen_i_][0] = from;
  seen_[seen_i_][1] = id;
  seen_i_ = (seen_i_ + 1) % 64;
  return false;
}

bool MtNode::parseData(const uint8_t* p, size_t n, DataMsg& d) {
  if (!n || p[0] != 0x08) return false;  // gyldig Data starter med portnum (felt 1, varint)
  Pb pb(p, n);
  uint32_t field;
  uint8_t wt;
  bool ok = true;
  while (ok && pb.next(field, wt)) {
    uint64_t v;
    size_t l;
    if (field == 1 && wt == 0) ok = pb.varint(v), d.port = uint32_t(v);
    else if (field == 2 && wt == 2) ok = pb.bytes(d.payload, d.plen);
    else if (field == 3 && wt == 0) ok = pb.varint(v), d.want_response = v != 0;
    else if (field == 4 && wt == 5) ok = pb.fixed32(d.dest);
    else if (field == 6 && wt == 5) ok = pb.fixed32(d.request_id);
    else if (field == 7 && wt == 5) ok = pb.fixed32(d.reply_id);
    else if (field == 8 && wt == 5) ok = pb.fixed32(d.emoji);
    else if (field == 9 && wt == 0) ok = pb.varint(v), d.bitfield = uint32_t(v), d.has_bitfield = true;
    else if (field == 10 && wt == 2) {
      const uint8_t* b;
      ok = pb.bytes(b, l);
      if (ok && l == 64) d.sig = b;
      else if (ok && l != 0) return false;  // misdannet signatur
    } else {
      ok = pb.skip(wt);
    }
  }
  return ok && d.port && d.port <= 1024;
}

// XEdDSA (docs/meshtastic_protokol.md afsnit 6): en forkert signatur forkaster pakken; en
// NodeInfo ved første kontakt verificeres med nøglen i den selv (CRC-32 = afsender).
bool MtNode::checkSignature(const DataMsg& d, uint32_t from, uint32_t to, uint32_t id, bool& signed_ok) {
  signed_ok = false;
  if (!d.sig || d.port == kPortTraceroute) return true;
  mt_pki::SignFields f{};
  f.from = from;
  f.id = id;
  f.to = to;
  f.portnum = d.port;
  f.request_id = d.request_id;
  f.reply_id = d.reply_id;
  f.emoji = d.emoji;
  f.bitfield = d.bitfield;
  f.has_bitfield = d.has_bitfield;
  f.want_response = d.want_response;
  f.payload = d.payload;
  f.payload_len = d.plen;
  const MtNodeEntry* e = find(from);
  if (e && e->has_key) {
    signed_ok = mt_pki::verify(e->pubkey, f, d.sig);
  } else if (d.port == kPortNodeInfo && d.payload) {
    // Nøglen fra selve NodeInfo (User felt 8).
    Pb pb(d.payload, d.plen);
    uint32_t field;
    uint8_t wt;
    const uint8_t* key = nullptr;
    bool ok = true;
    while (ok && pb.next(field, wt)) {
      const uint8_t* b;
      size_t l;
      if (field == 8 && wt == 2) {
        ok = pb.bytes(b, l);
        if (ok && l == 32) key = b;
      } else {
        ok = pb.skip(wt);
      }
    }
    if (!key || mt_pki::crc32(key, 32) != from) return false;
    signed_ok = mt_pki::verify(key, f, d.sig);
  } else {
    return true;  // ukendt nøgle: kan ikke tjekkes (som Meshtastics standardpolitik)
  }
  if (!signed_ok) DLOG.printf("[MT] forkert XEdDSA-signatur fra !%08lx - pakken forkastes\n", (unsigned long)from);
  return signed_ok;
}

void MtNode::onPacket(const uint8_t* raw, size_t len, int16_t rssi, int8_t snr_q4, uint32_t t_ms) {
  if (len < 16 + 2 || !scan_) return;
  ++st_.pkts;
  const uint32_t to = le32(raw), from = le32(raw + 4), id = le32(raw + 8);
  const uint8_t flags = raw[12], hash = raw[13];
  const uint32_t self = mt_tx::nodeNum();
  rx_next_hop_ = raw[14];
  rx_hop_limit_ = flags & 7;
  rx_rssi_ = rssi;
  rx_snr_q4_ = snr_q4;
  // Sendt til os via next-hop-routing: afsenderen gensender, til vi kvitterer (også uden want_ack).
  const bool next_hop_us = to == self && raw[14] == uint8_t(self & 0xFF) && (flags & 7) > 0;
  if (from == self) {  // vores egen pakke, sendt videre af en anden
    onOwnEcho(id);
    if (phone_) phone_->onImplicitAck(id);
    return;
  }
  if (seen(from, id)) {  // samme pakke gentaget af en anden node
    ++st_.dupes;
    const uint8_t hl = flags & 7;
    const bool repeated = (flags >> 5) != 0 && (flags >> 5) == hl;  // afsenderen gentager selv (0 hop)
    if (Relay* r = findRelay(from, id)) {
      if (hl > (r->pkt[12] & 7) + 1) {  // kopi med flere hop tilbage: brug den
        r->pkt[12] = uint8_t((raw[12] & ~7) | ((hl - 1) & 7));
      } else if (!repeated) {
        cancelRelay(from, id, "hoert sendt af en anden");
      }
    } else if (repeated && !testing_) {
      if (to == self && ((flags & 0x08) || next_hop_us)) {  // vores ACK gik tabt: send den igen
        for (const AckMemo& a : acks_)
          if (a.from == from && a.id == id) sendRouting(from, id, 0, a.chan, a.hop);
      } else {
        maybeRelay(raw, len, snr_q4, true);
      }
    }
    return;
  }
  const bool want_ack = flags & 0x08;
  const int hops_i = int((flags >> 5) & 7) - int(flags & 7);
  const uint8_t hops = uint8_t(hops_i > 0 ? hops_i : 0);
  const size_t n = len - 16;
  static uint8_t plain[256];

  // PKI-krypteret DM til os (kanal-hash 0): X25519 + AES-256-CCM med afsenderens nøgle.
  if (hash == 0 && to == self && n > 12) {
    const MtNodeEntry* e = find(from);
    const int m = e && e->has_key ? mt_pki::decrypt(e->pubkey, from, id, raw + 16, n, plain) : -1;
    DataMsg d;
    if (m > 0 && parseData(plain, size_t(m), d)) {
      ++st_.decoded;
      dispatch(d, from, to, id, -1, true, want_ack, hops, rssi, snr_q4, t_ms);
      if (phone_ && !testing_)
        phone_->onMeshPacket(from, to, id, 0, true, flags & 7, flags >> 5, want_ack, snr_q4, rssi, plain, size_t(m));
      return;
    }
    ++st_.undecoded;
    touch(from, rssi, snr_q4, hops, t_ms);
    DLOG.printf("[MT] PKI-DM fra !%08lx kunne ikke laeses (%s)\n", (unsigned long)from,
                e && e->has_key ? "forkert noegle?" : "afsenderens noegle ukendt");
    if (want_ack && !testing_) sendRouting(from, id, 35, 0);  // PKI_UNKNOWN_PUBKEY
    // Kender vi ikke afsenderens nøgle, beder vi om dens NodeInfo (med nøglen), så næste
    // forsøg kan læses. Højst én gang pr. node hvert 5. minut.
    static uint32_t asked_num = 0, asked_ms = 0;
    if (!testing_ && !(e && e->has_key) && (asked_num != from || t_ms - asked_ms > 300000u)) {
      asked_num = from;
      asked_ms = t_ms;
      sendNodeInfo(from, 0, true);
    }
    return;
  }
  // Kanalpakke: AES-CTR med kanalens nøgle. Nonce: pakke-id (64 bit), afsender (32 bit), tæller.
  uint8_t nonce[16] = {};
  memcpy(nonce, raw + 8, 4);
  memcpy(nonce + 8, raw + 4, 4);
  for (int c = 0; c < scan_->mtChannelCount(); ++c) {
    const MtChannel& ch = scan_->mtChannel(c);
    if (ch.hash != hash) continue;
    if (ch.klen == 0) {
      memcpy(plain, raw + 16, n);
    } else if (ch.klen == 16) {
      CTR<AES128> ctr;
      ctr.setKey(ch.key, 16);
      ctr.setIV(nonce, sizeof(nonce));
      ctr.setCounterSize(4);
      ctr.decrypt(plain, raw + 16, n);
    } else {
      CTR<AES256> ctr;
      ctr.setKey(ch.key, 32);
      ctr.setIV(nonce, sizeof(nonce));
      ctr.setCounterSize(4);
      ctr.decrypt(plain, raw + 16, n);
    }
    DataMsg d;
    if (!parseData(plain, n, d)) continue;
    ++st_.decoded;
    dispatch(d, from, to, id, c, false, want_ack, hops, rssi, snr_q4, t_ms);
    if (phone_ && !testing_)
      phone_->onMeshPacket(from, to, id, c, false, flags & 7, flags >> 5, want_ack, snr_q4, rssi, plain, n);
    maybeRelay(raw, len, snr_q4, true);
    return;
  }
  ++st_.undecoded;  // ingen af vores kanaler passede (kanalscanneren viser den som hørt)
  if (want_ack && to == self && !testing_) sendRouting(from, id, 6, 0);  // NO_CHANNEL
  // PKI-DM mellem to andre noder: "læselig" i tilstand 1, når vi kender en af dem.
  maybeRelay(raw, len, snr_q4, hash == 0 && to != kBroadcast && (find(from) || find(to)));
}

void MtNode::dispatch(const DataMsg& d, uint32_t from, uint32_t to, uint32_t id, int chan, bool pki, bool want_ack,
                      uint8_t hops, int16_t rssi, int8_t snr_q4, uint32_t t_ms) {
  bool signed_ok = false;
  if (!checkSignature(d, from, to, id, signed_ok)) return;
  MtNodeEntry* e = touch(from, rssi, snr_q4, hops, t_ms);
  const uint32_t self = mt_tx::nodeNum();
  // ACK til afsenderen, når den beder om det (på kanalen; Routing sendes aldrig med PKI). Også
  // for Routing: en NAK (fx 37 på en admin-forespørgsel) kommer med want_ack og gentages, til den
  // kvitteres. En almindelig ACK har aldrig want_ack, så det giver ingen løkke.
  if (want_ack && to == self && !testing_) {
    const uint8_t hop = uint8_t(hops + 2 < cfg_.hop_limit ? hops + 2 : cfg_.hop_limit);
    acks_[ack_i_] = AckMemo{from, id, uint8_t(chan < 0 ? 0 : chan), hop};
    ack_i_ = (ack_i_ + 1) % 8;
    sendRouting(from, id, 0, chan < 0 ? 0 : chan, hop);
  } else if (to == self && !testing_ && rx_next_hop_ == uint8_t(self & 0xFF) && rx_hop_limit_ > 0) {
    // Next-hop-routing uden want_ack (fx svaret på vores telemetri-forespørgsel): afsenderen
    // gensender, til den hører en ACK. Kvittér med hop_limit 0 (docs/meshtastic_protokol.md afsnit 7).
    acks_[ack_i_] = AckMemo{from, id, uint8_t(chan < 0 ? 0 : chan), 0};
    ack_i_ = (ack_i_ + 1) % 8;
    sendRouting(from, id, 0, chan < 0 ? 0 : chan, 0);
  }
  // Svar/ACK på en DM mellem to andre: den oprindelige DM skal ikke sendes videre længere.
  if ((d.request_id || d.reply_id) && to != self && to != kBroadcast)
    cancelRelay(to, d.request_id ? d.request_id : d.reply_id, "svar hoert");
  // Forespørgsler til os (want_response): NodeInfo besvares altid (indeholder ingen
  // position); Position kun hvis "Del position" for Meshtastic tillader det.
  if (d.want_response && to == self && !testing_) {
    const int rc = chan < 0 ? 0 : chan;  // svar på samme kanal (PKI: den primære)
    if (d.port == kPortNodeInfo) sendNodeInfo(from, id, false, rc);
    else if (d.port == kPortPosition && !sendPosition(from, id, rc))
      DLOG.printf("[MT] !%08lx bad om vores position - ikke delt (privatliv)\n", (unsigned long)from);
    else if (d.port == kPortTelemetry && !(cfg_.telemetry_min && sendTelemetry(from, id, rc)))
      DLOG.printf("[MT] !%08lx bad om telemetri - ikke slaaet til\n", (unsigned long)from);
  }
  // Ukendt afsender (intet navn eller nøgle endnu): send vores NodeInfo med want_response, så
  // vi får dens User og nøgle nu i stedet for om op til 3 timer. Højst én gang pr. node pr. 12 t
  // og én forespørgsel pr. 5 min i alt (sendetid); på den kanal pakken kom fra.
  if (e && !e->long_name[0] && !e->has_key && d.port != kPortNodeInfo && from != self && !testing_)
    askNodeInfo(from, chan < 0 ? 0 : chan, t_ms);
  switch (d.port) {
    case kPortText:
      ++st_.text;
      handleText(from, to, chan, d.payload, d.plen, hops, t_ms, d.emoji);
      if (pki) DLOG.printf("[MT] (PKI-krypteret%s)\n", signed_ok ? ", signeret" : "");
      break;
    case kPortPosition:
      ++st_.position;
      if (e && d.payload) handlePosition(e, d.payload, d.plen);
      break;
    case kPortNodeInfo:
      ++st_.nodeinfo;
      if (e && d.payload) handleUser(e, d.payload, d.plen, signed_ok);
      break;
    case kPortRouting:
      ++st_.routing;
      handleRouting(from, d.request_id, d.payload, d.plen);
      break;
    case kPortTelemetry:
      ++st_.telemetry;
      if (e && d.payload) handleTelemetry(e, d.payload, d.plen);
      break;
    case kPortTraceroute:
      handleTraceroute(d.payload, d.plen, d.want_response, d.request_id, from, to, id, chan, hops, snr_q4);
      break;
    case kPortWaypoint:
      ++st_.other;
      if (d.payload && chan >= 0) handleWaypoint(from, chan, d.payload, d.plen);
      break;
    default:
      ++st_.other;
      DLOG.printf("[MT] port %lu fra !%08lx (%u byte, ikke brugt endnu)\n", (unsigned long)d.port,
                  (unsigned long)from, unsigned(d.plen));
      break;
  }
  // Svar på en forespørgsel fra tandhjulsmenuen (requestInfo): resultatet står i DM'en.
  if (req_to_ && from == req_to_ && to == self && d.port == req_port_ && t_ms - req_ms_ < kReqWindowMs)
    requestResult(e);
}

void MtNode::handleText(uint32_t from, uint32_t to, int chan, const uint8_t* p, size_t n, uint8_t hops,
                        uint32_t t_ms, uint32_t emoji) {
  if (!p || !store_) return;
  if (testing_) {
    copyStr(test_text_, sizeof(test_text_), p, n);
    return;
  }
  char name[40], text[150];
  nameOf(from, name, sizeof(name));
  copyStr(text, sizeof(text), p, n);
  if (emoji && n <= 8) {  // reaktion (tapback) på en anden besked
    char t[160];
    snprintf(t, sizeof(t), "reagerede med %s", text);
    snprintf(text, sizeof(text), "%s", t);
  }
  Msg m;
  m.rx_ms = t_ms;
  m.ts = wallclock::now();  // 0 = uret er ikke sat endnu (vises så ud fra oppetiden)
  m.net = 2;
  m.hops = hops;
  if (!testing_) {  // signalet fra pakken, der er ved at blive behandlet (onPacket)
    m.has_sig = true;
    m.rssi = rx_rssi_;
    m.snr_q4 = rx_snr_q4_;
  }
  if (to == kBroadcast && chan >= 0) {
    const MtChannel& ch = scan_->mtChannel(chan);
    m.kind = Msg::ChannelIn;
    snprintf(m.conv, sizeof(m.conv), "%s", ch.conv);
    snprintf(m.peer, sizeof(m.peer), "%s", ch.name);
    snprintf(m.text, sizeof(m.text), "%s: %s", name, text);
    DLOG.printf("[MT] kanal %s: %s\n", ch.name, m.text);  // "kanal" gør linjen genkendelig for fejlrapportens rensning
  } else if (to == mt_tx::nodeNum()) {
    m.kind = Msg::DirectIn;  // DM (PKI, eller kanalnøglen fra ældre klienter)
    dmConvId(from, m.conv, sizeof(m.conv));
    snprintf(m.peer, sizeof(m.peer), "%s", name);
    snprintf(m.text, sizeof(m.text), "%s", text);
    DLOG.printf("[MT] DM fra %s (%s): %s\n", name, chan < 0 ? "PKI" : "kanalnoegle", text);
  } else {
    return;  // DM mellem andre
  }
  store_->push(m);
}

// Waypoint (afsnit 13): 1 id, 2/3 bredde/længde (sfixed32, x 1e7), 4 expire, 6 name, 7 description.
void MtNode::handleWaypoint(uint32_t from, int chan, const uint8_t* p, size_t n) {
  Pb pb(p, n);
  uint32_t field, v32 = 0;
  uint8_t wt;
  bool ok = true, has_lat = false;
  Waypoint w{};
  w.from = from;
  w.chan = chan;
  int32_t la = 0, lo = 0;
  while (ok && pb.next(field, wt)) {
    uint64_t v;
    const uint8_t* s;
    size_t sl;
    if (field == 1 && wt == 0) ok = pb.varint(v), w.id = uint32_t(v);
    else if (field == 2 && wt == 5) ok = pb.fixed32(v32), la = int32_t(v32), has_lat = true;
    else if (field == 3 && wt == 5) ok = pb.fixed32(v32), lo = int32_t(v32);
    else if (field == 4 && wt == 0) ok = pb.varint(v), w.expire = uint32_t(v);
    else if ((field == 6 || field == 7) && wt == 2 && (ok = pb.bytes(s, sl)))
      copyStr(field == 6 ? w.name : w.desc, field == 6 ? sizeof(w.name) : sizeof(w.desc), s, sl);
    else ok = pb.skip(wt);
  }
  if (!ok || !w.id) return;
  w.lat = la / 1e7;
  w.lon = lo / 1e7;
  char nm[40];
  nameOf(from, nm, sizeof(nm));
  DLOG.printf("[MT] waypoint %lu \"%s\" fra %s%s\n", (unsigned long)w.id, w.name, nm,
              w.expire && w.expire < wallclock::now() ? " (slettet)" : "");
  if (!has_lat && !(w.expire && w.expire < wallclock::now())) return;
  if (waypoint_fn_) waypoint_fn_(w);
}

void MtNode::handlePosition(MtNodeEntry* e, const uint8_t* p, size_t n) {
  Pb pb(p, n);
  uint32_t field, v32 = 0;
  uint8_t wt;
  bool ok = true, got = false;
  while (ok && pb.next(field, wt)) {
    uint64_t v;
    if (field == 1 && wt == 5) ok = pb.fixed32(v32), e->lat_e7 = int32_t(v32), got = true;
    else if (field == 2 && wt == 5) ok = pb.fixed32(v32), e->lon_e7 = int32_t(v32);
    else if (field == 3 && wt == 0) ok = pb.varint(v), e->alt_m = int32_t(int64_t(v));
    else if (field == 4 && wt == 5) ok = pb.fixed32(e->pos_ts);
    else ok = pb.skip(wt);
  }
  if (got && (e->lat_e7 || e->lon_e7)) {
    e->has_pos = true;
    DLOG.printf("[MT] position fra !%08lx: %.5f,%.5f\n", (unsigned long)e->num, e->lat_e7 / 1e7,
                e->lon_e7 / 1e7);
  }
}

void MtNode::handleUser(MtNodeEntry* e, const uint8_t* p, size_t n, bool signed_ok) {
  Pb pb(p, n);
  uint32_t field;
  uint8_t wt;
  bool ok = true;
  while (ok && pb.next(field, wt)) {
    const uint8_t* b;
    size_t l;
    uint64_t v;
    if (field == 2 && wt == 2) ok = pb.bytes(b, l), copyStr(e->long_name, sizeof(e->long_name), b, l);
    else if (field == 3 && wt == 2) ok = pb.bytes(b, l), copyStr(e->short_name, sizeof(e->short_name), b, l);
    else if (field == 5 && wt == 0) ok = pb.varint(v), e->hw = uint8_t(v > 255 ? 255 : v);
    else if (field == 7 && wt == 0) ok = pb.varint(v), e->role = uint8_t(v);
    else if (field == 8 && wt == 2) {
      ok = pb.bytes(b, l);
      if (ok && l == 32) {
        // En kendt node, der pludselig har en anden nøgle, gemmes ikke (som Meshtastic).
        if (e->has_key && memcmp(e->pubkey, b, 32) != 0) {
          DLOG.printf("[MT] !%08lx har en ny offentlig noegle - ignoreret\n", (unsigned long)e->num);
        } else {
          memcpy(e->pubkey, b, 32);
          e->has_key = true;
        }
      }
    } else {
      ok = pb.skip(wt);
    }
  }
  DLOG.printf("[MT] nodeinfo !%08lx: \"%s\" (%s) %s, %s%s%s\n", (unsigned long)e->num, e->long_name,
              e->short_name, hwName(e->hw), roleName(e->role), e->has_key ? ", PKI-noegle" : "",
              signed_ok ? ", signeret" : "");
}

void MtNode::handleTelemetry(MtNodeEntry* e, const uint8_t* p, size_t n) {
  Pb pb(p, n);
  uint32_t field;
  uint8_t wt;
  bool ok = true;
  while (ok && pb.next(field, wt)) {
    const uint8_t* b;
    size_t l;
    if (field == 2 && wt == 2) {  // DeviceMetrics
      ok = pb.bytes(b, l);
      Pb dm(b, l);
      uint32_t f2, u;
      uint8_t w2;
      bool ok2 = true;
      while (ok2 && dm.next(f2, w2)) {
        uint64_t v;
        if (f2 == 1 && w2 == 0) ok2 = dm.varint(v), e->batt = uint8_t(v > 101 ? 101 : v);
        else if (f2 == 2 && w2 == 5) {
          ok2 = dm.fixed32(u);
          float f;
          memcpy(&f, &u, 4);
          e->mv = uint16_t(f > 0 && f < 60 ? f * 1000 : 0);
        } else {
          ok2 = dm.skip(w2);
        }
      }
    } else {
      ok = pb.skip(wt);
    }
  }
}

void MtNode::handleRouting(uint32_t from, uint32_t request_id, const uint8_t* p, size_t n) {
  uint32_t err = 0;
  if (p) {
    Pb pb(p, n);
    uint32_t field;
    uint8_t wt;
    bool ok = true;
    while (ok && pb.next(field, wt)) {
      uint64_t v;
      if (field == 3 && wt == 0) ok = pb.varint(v), err = uint32_t(v);
      else ok = pb.skip(wt);
    }
  }
  DLOG.printf("[MT] routing fra !%08lx: %s (%lu) for pakke %08lx\n", (unsigned long)from,
              err ? "NAK" : "ACK", (unsigned long)err, (unsigned long)request_id);
  if (err && req_to_ && from == req_to_ && request_id == req_id_) {
    char t[80];
    snprintf(t, sizeof(t), "Intet svar på %s (fejl %lu: %s)", reqName(req_port_), (unsigned long)err,
             err == 8 ? "noden har det ikke eller deler det ikke" : "afvist");
    noteInDm(from, t);
    req_to_ = 0;
  }
  for (PendingDm& p : dms_)
    if (p.id && p.id == request_id) {
      if (!err && from == p.to) dmFinal(p, true, "ACK fra modtageren");
      else if (err) {
        char why[40];
        snprintf(why, sizeof(why), "NAK %lu fra !%08lx", (unsigned long)err, (unsigned long)from);
        dmFinal(p, false, why);
      }
    }
}

namespace {
size_t pbVarint(uint8_t* p, uint64_t v) {
  size_t n = 0;
  do {
    uint8_t b = uint8_t(v & 0x7F);
    v >>= 7;
    if (v) b |= 0x80;
    p[n++] = b;
  } while (v);
  return n;
}
size_t pbKey(uint8_t* p, uint32_t field, uint8_t wt) { return pbVarint(p, (uint64_t(field) << 3) | wt); }
size_t pbBytes(uint8_t* p, uint32_t field, const void* d, size_t n) {
  size_t k = pbKey(p, field, 2);
  k += pbVarint(p + k, n);
  memcpy(p + k, d, n);
  return k + n;
}
size_t pbFixed32(uint8_t* p, uint32_t field, uint32_t v) {
  size_t k = pbKey(p, field, 5);
  memcpy(p + k, &v, 4);
  return k + 4;
}
size_t pbU(uint8_t* p, uint32_t field, uint64_t v) {
  size_t k = pbKey(p, field, 0);
  return k + pbVarint(p + k, v);
}

}  // namespace

// ---- Afsendelse (trin 2) --------------------------------------------------------------

bool MtNode::sendWaypoint(int chan, double lat, double lon, const char* name, const char* desc, uint32_t& id,
                          uint32_t expire) {
  if (!scan_ || chan < 0 || chan >= scan_->mtChannelCount() || !cfg_.send) return false;
  if (!id) id = mt_tx::newId();
  uint8_t pl[200];
  size_t k = pbU(pl, 1, id);
  k += pbFixed32(pl + k, 2, uint32_t(int32_t(lat * 1e7)));
  k += pbFixed32(pl + k, 3, uint32_t(int32_t(lon * 1e7)));
  if (expire) k += pbU(pl + k, 4, expire);
  if (name && *name) k += pbBytes(pl + k, 6, name, strnlen(name, 30));
  if (desc && *desc) k += pbBytes(pl + k, 7, desc, strnlen(desc, 100));
  uint8_t pkt[256];
  mt_tx::Opts o;
  o.to = kBroadcast;
  o.hop_limit = cfg_.hop_limit;
  o.id = mt_tx::newId();
  uint8_t sig[64];
  if (signOwn(kPortWaypoint, o, pl, k, sig)) o.signature = sig;
  const size_t n = mt_tx::buildData(scan_->mtChannel(chan), kPortWaypoint, pl, k, o, nullptr, pkt, sizeof(pkt));
  const bool ok = n && sendRaw(pkt, n);
  DLOG.printf("[MT] waypoint %lu \"%s\" paa %s: %s\n", (unsigned long)id, name ? name : "",
              scan_->mtChannel(chan).name, ok ? "sat i koe" : "IKKE sendt");
  return ok;
}

bool MtNode::sendRaw(const uint8_t* pkt, size_t len) {
  if (!cfg_.tx || !cfg_.send || !len) return false;
  return cfg_.send(pkt, len);
}

void MtNode::onOwnEcho(uint32_t id) {
  for (PendingDm& p : dms_)  // vores DM sendt videre af en anden: stop gensendelser, vent på ACK
    if (p.id == id && !p.relayed) {
      p.relayed = true;
      p.next_ms = uint32_t(millis()) + 30000;
      DLOG.printf("[MT] DM %08lx hoert sendt videre - venter paa ACK fra modtageren\n", (unsigned long)id);
      return;
    }
  for (Pending& p : pending_)
    if (p.id == id && p.seq) {
      if (store_) store_->setStatus(p.seq, Msg::Delivered, true);
      DLOG.printf("[MT] egen besked %08lx hoert sendt videre - leveret\n", (unsigned long)id);
      p = Pending{};
      return;
    }
}

bool MtNode::sendText(int chan, const char* text) {
  if (!scan_ || chan < 0 || chan >= scan_->mtChannelCount() || !text || !*text) return false;
  const MtChannel& ch = scan_->mtChannel(chan);
  uint8_t pkt[256];
  uint32_t id = 0;
  mt_tx::Opts o;
  o.hop_limit = cfg_.hop_limit;
  size_t tl = strlen(text);
  if (tl > 160) tl = 160;
  uint8_t sig[64];
  o.id = mt_tx::newId();
  if (signOwn(kPortText, o, reinterpret_cast<const uint8_t*>(text), tl, sig)) o.signature = sig;
  const size_t n = mt_tx::buildData(ch, kPortText, reinterpret_cast<const uint8_t*>(text), tl, o, &id, pkt,
                                    sizeof(pkt));
  const bool ok = n && sendRaw(pkt, n);
  if (store_) {
    Msg m;
    m.rx_ms = millis();
    m.ts = wallclock::now();
    m.net = 2;
    m.kind = Msg::ChannelOut;
    m.status = ok ? Msg::Queued : Msg::Failed;
    snprintf(m.conv, sizeof(m.conv), "%s", ch.conv);
    snprintf(m.peer, sizeof(m.peer), "%s", ch.name);
    snprintf(m.text, sizeof(m.text), "%s", text);
    const uint32_t seq = store_->push(m);
    if (ok) {
      pending_[pending_i_] = Pending{id, seq, uint32_t(millis())};
      pending_i_ = (pending_i_ + 1) % 16;
    }
  }
  if (ok) ccPhone(kBroadcast, id, chan, false, text, tl, false);
  DLOG.printf("[MT] tekst til %s: %s (%s)\n", ch.name, text, ok ? "sat i koe" : cfg_.tx ? "FEJL" : "afsendelse er FRA");
  return ok;
}

// Tekst skrevet på T-Deck'en sendes også til Meshtastic-appen (som afsendt af os), så samtalerne
// er ens begge steder. Appen får status via de ACK'er, der sendes videre til den.
void MtNode::ccPhone(uint32_t to, uint32_t id, int chan, bool pki, const char* text, size_t tl, bool want_ack) {
  if (!phone_) return;
  mt_tx::Opts o;
  o.to = to;
  o.id = id;
  uint8_t plain[240];
  const size_t k =
      mt_tx::encodeData(kPortText, reinterpret_cast<const uint8_t*>(text), tl, o, plain, sizeof(plain));
  if (k)
    phone_->onMeshPacket(mt_tx::nodeNum(), to, id, chan, pki, cfg_.hop_limit, cfg_.hop_limit, want_ack, 0, 0, plain,
                         k);
}

// ---- Forespørgsler fra tandhjulsmenuen ---------------------------------------------------
// Position, telemetri eller NodeInfo fra én node (want_response). Svaret (eller NAK) skrives
// som en linje i DM'en med noden. Position: vi sender en tom Position (vores egen deles kun
// efter "Del position", privatlivsreglen).

const char* MtNode::reqName(uint32_t port) {
  return port == kPortPosition ? "position" : port == kPortTelemetry ? "telemetri" : "NodeInfo";
}

bool MtNode::requestInfo(uint32_t to, uint32_t port) {
  if (!scan_ || scan_->mtChannelCount() == 0 || to == kBroadcast || to == mt_tx::nodeNum()) return false;
  const uint32_t id = mt_tx::newId();
  bool ok = false;
  if (port == kPortNodeInfo) {
    ok = sendNodeInfo(to, 0, true, 0);
  } else if (port == kPortPosition || port == kPortTelemetry) {
    uint8_t pl[4];
    size_t pn = 0;
    if (port == kPortTelemetry) pn = pbBytes(pl, 2, pl, 0);  // device_metrics (tom) = bed om enhedsdata
    mt_tx::Opts o;
    o.to = to;
    o.id = id;
    o.want_response = true;
    uint8_t data[160];  // encodeData kræver plads til payload + 100 (signatur m.m.)
    const size_t k = mt_tx::encodeData(port, pl, pn, o, data, sizeof(data));
    ok = k && sendFromApp(to, 0, data, k, false, id, 0);
  } else {
    return false;
  }
  if (ok) {
    req_to_ = to;
    req_port_ = port;
    req_id_ = id;
    req_ms_ = uint32_t(millis());
  }
  DLOG.printf("[MT] beder !%08lx om %s: %s\n", (unsigned long)to, reqName(port), ok ? "sat i koe" : "IKKE sendt");
  return ok;
}

void MtNode::requestResult(const MtNodeEntry* e) {
  char t[120];
  if (!e) return;
  if (req_port_ == kPortPosition && e->has_pos)
    snprintf(t, sizeof(t), "Position: %.5f, %.5f, %ld m", e->lat_e7 / 1e7, e->lon_e7 / 1e7, (long)e->alt_m);
  else if (req_port_ == kPortTelemetry && e->batt > 100)
    snprintf(t, sizeof(t), "Telemetri: på strøm, %.2f V", e->mv / 1000.0);
  else if (req_port_ == kPortTelemetry)
    snprintf(t, sizeof(t), "Telemetri: batteri %u %%, %.2f V", unsigned(e->batt), e->mv / 1000.0);
  else if (req_port_ == kPortNodeInfo)
    snprintf(t, sizeof(t), "NodeInfo: %s (%s)%s", e->long_name, e->short_name, e->has_key ? ", PKI-nøgle" : "");
  else
    snprintf(t, sizeof(t), "Svar på %s modtaget", reqName(req_port_));
  noteInDm(e->num, t);
  req_to_ = 0;
}

// En linje fra os selv i DM'en med en node (resultat af traceroute/forespørgsel).
void MtNode::noteInDm(uint32_t num, const char* text) {
  DLOG.printf("[MT] %s\n", text);
  if (!store_) return;
  Msg msg;
  msg.rx_ms = millis();
  msg.ts = wallclock::now();
  msg.net = 2;
  msg.kind = Msg::DirectIn;
  dmConvId(num, msg.conv, sizeof(msg.conv));
  nameOf(num, msg.peer, sizeof(msg.peer));
  snprintf(msg.text, sizeof(msg.text), "%s", text);
  store_->push(msg);
}

void MtNode::askNodeInfo(uint32_t num, int chan, uint32_t t_ms) {
  constexpr uint32_t kPerNodeMs = 12u * 3600u * 1000u, kGlobalMs = 5u * 60u * 1000u;
  if (ask_any_ms_ && t_ms - ask_any_ms_ < kGlobalMs) return;
  for (const AskMemo& a : asks_)
    if (a.num == num && a.ms && t_ms - a.ms < kPerNodeMs) return;
  ask_any_ms_ = t_ms ? t_ms : 1;
  asks_[ask_i_] = AskMemo{num, ask_any_ms_};
  ask_i_ = (ask_i_ + 1) % kAsks;
  DLOG.printf("[MT] ukendt node !%08lx hoert - beder om dens NodeInfo\n", (unsigned long)num);
  sendNodeInfo(num, 0, true, chan);
}

int MtNode::dataChannel() const {
  if (!cfg_.data_channel[0] || !scan_) return 0;
  for (int i = 0; i < scan_->mtChannelCount(); ++i)
    if (!strcasecmp(scan_->mtChannel(i).name, cfg_.data_channel)) return i;
  return -1;
}

int MtNode::chanOr0(int c) const { return scan_ && c >= 0 && c < scan_->mtChannelCount() ? c : 0; }

bool MtNode::sendNodeInfo(uint32_t to, uint32_t request_id, bool want_response, int chan) {
  if (!scan_ || scan_->mtChannelCount() == 0) return false;
  uint8_t pl[120];
  char id[12], sn[8];
  snprintf(id, sizeof(id), "!%08lx", (unsigned long)mt_tx::nodeNum());
  if (cfg_.short_name[0]) snprintf(sn, sizeof(sn), "%s", cfg_.short_name);
  else snprintf(sn, sizeof(sn), "%04lx", (unsigned long)(mt_tx::nodeNum() & 0xFFFF));
  size_t k = pbBytes(pl, 1, id, strlen(id));
  const char* ln = cfg_.long_name[0] ? cfg_.long_name : id;
  k += pbBytes(pl + k, 2, ln, strlen(ln));
  k += pbBytes(pl + k, 3, sn, strlen(sn));
  k += pbU(pl + k, 5, 50);  // hw_model: T-Deck
  if (!cfg_.relay) k += pbU(pl + k, 7, 1);  // rolle: CLIENT_MUTE; CLIENT (0) er standard og udelades
  if (mt_pki::ready()) k += pbBytes(pl + k, 8, mt_pki::publicKey(), 32);  // til PKI-DM'er
  uint8_t pkt[256];
  mt_tx::Opts o;
  o.to = to;
  o.hop_limit = cfg_.hop_limit;
  o.request_id = request_id;
  o.want_response = want_response;
  o.id = mt_tx::newId();
  uint8_t sig[64];
  if (signOwn(kPortNodeInfo, o, pl, k, sig)) o.signature = sig;
  const size_t n = mt_tx::buildData(scan_->mtChannel(chanOr0(chan)), kPortNodeInfo, pl, k, o, nullptr, pkt, sizeof(pkt));
  const bool ok = n && sendRaw(pkt, n);
  char dst[12] = "";
  if (to != kBroadcast) snprintf(dst, sizeof(dst), "!%08lx", (unsigned long)to);
  DLOG.printf("[MT] nodeinfo \"%s\" (%s) %s%s%s: %s\n", ln, sn,
              to == kBroadcast ? "til alle" : want_response ? "til " : "som svar til ", dst,
              want_response ? " (beder om dens)" : "", ok ? "sat i koe" : "IKKE sendt");
  return ok;
}

bool MtNode::sendPosition(uint32_t to, uint32_t request_id, int chan) {
  int32_t la = 0, lo = 0, alt = 0;
  uint32_t prec = 32;
  if (!scan_ || scan_->mtChannelCount() == 0 || !cfg_.position || !cfg_.position(la, lo, alt, prec)) return false;
  if (prec < 32) {  // omtrentlig: nederste bits nulstilles og midten af feltet bruges
    const uint32_t mask = ~((1u << (32 - prec)) - 1);
    la = int32_t((uint32_t(la) & mask) + (1u << (31 - prec)));
    lo = int32_t((uint32_t(lo) & mask) + (1u << (31 - prec)));
  }
  uint8_t pl[64];
  size_t k = pbFixed32(pl, 1, uint32_t(la));
  k += pbFixed32(pl + k, 2, uint32_t(lo));
  k += pbU(pl + k, 3, uint64_t(int64_t(alt)));
  if (const uint32_t t = wallclock::now()) k += pbFixed32(pl + k, 4, t);
  k += pbU(pl + k, 23, prec);  // precision_bits
  uint8_t pkt[256];
  mt_tx::Opts o;
  o.to = to;
  o.hop_limit = cfg_.hop_limit;
  o.request_id = request_id;
  o.id = mt_tx::newId();
  uint8_t sig[64];
  if (signOwn(kPortPosition, o, pl, k, sig)) o.signature = sig;
  const size_t n = mt_tx::buildData(scan_->mtChannel(chanOr0(chan)), kPortPosition, pl, k, o, nullptr, pkt, sizeof(pkt));
  const bool ok = n && sendRaw(pkt, n);
  DLOG.printf("[MT] position (%s) %s: %s\n", prec >= 32 ? "praecis" : "omtrent",
              to == kBroadcast ? "til alle" : "som svar", ok ? "sat i koe" : "IKKE sendt");
  return ok;
}

// XEdDSA-signatur af en egen pakke (docs/meshtastic_protokol.md afsnit 6).
bool MtNode::signOwn(uint32_t port, const mt_tx::Opts& o, const uint8_t* payload, size_t n, uint8_t sig[64]) {
  if (!mt_pki::ready()) return false;
  mt_pki::SignFields f{};
  f.from = mt_tx::nodeNum();
  f.id = o.id;
  f.to = o.to;
  f.portnum = port;
  f.request_id = o.request_id;
  f.want_response = o.want_response;
  f.has_bitfield = true;  // encodeData sender altid bitfield
  f.bitfield = mt_tx::bitfieldOf(o);
  f.payload = payload;
  f.payload_len = n;
  return mt_pki::sign(f, sig);
}

// ACK (err 0) eller NAK til `to` for pakken request_id, på kanal nr. chan.
void MtNode::sendRouting(uint32_t to, uint32_t request_id, uint32_t err, int chan, uint8_t hop) {
  if (!scan_ || chan < 0 || chan >= scan_->mtChannelCount()) return;
  uint8_t pl[8];
  size_t k = 0;
  if (err) k = pbU(pl, 3, err);  // Routing.error_reason; ACK = tom Routing
  uint8_t pkt[256];
  mt_tx::Opts o;
  o.to = to;
  o.hop_limit = hop == kDefaultHop ? cfg_.hop_limit : hop;
  o.request_id = request_id;
  const size_t n = mt_tx::buildData(scan_->mtChannel(chan), kPortRouting, pl, k, o, nullptr, pkt, sizeof(pkt));
  const bool ok = n && sendRaw(pkt, n);
  DLOG.printf("[MT] %s %lu til !%08lx for %08lx: %s\n", err ? "NAK" : "ACK", (unsigned long)err, (unsigned long)to,
              (unsigned long)request_id, ok ? "sat i koe" : "IKKE sendt");
}

size_t MtNode::telemetryPayload(uint8_t* pl, uint8_t& level, float& volts) {
  uint8_t dmx[40];
  size_t m = 0;
  level = 0;
  volts = 0;
  if (cfg_.battery && cfg_.battery(level, volts)) {
    m += pbU(dmx + m, 1, level);
    uint32_t u;
    memcpy(&u, &volts, 4);
    m += pbFixed32(dmx + m, 2, u);
  }
  m += pbU(dmx + m, 5, uint32_t(millis() / 1000));  // oppetid i sekunder
  size_t k = 0;
  if (const uint32_t t = wallclock::now()) k += pbFixed32(pl, 1, t);
  k += pbBytes(pl + k, 2, dmx, m);  // device_metrics
  return k;
}

bool MtNode::sendTelemetry(uint32_t to, uint32_t request_id, int chan) {
  if (!scan_ || scan_->mtChannelCount() == 0) return false;
  uint8_t pl[64], level;
  float volts;
  const size_t k = telemetryPayload(pl, level, volts);
  uint8_t pkt[256];
  mt_tx::Opts o;
  o.to = to;
  o.hop_limit = cfg_.hop_limit;
  o.request_id = request_id;
  o.id = mt_tx::newId();
  uint8_t sig[64];
  if (signOwn(kPortTelemetry, o, pl, k, sig)) o.signature = sig;
  const size_t n = mt_tx::buildData(scan_->mtChannel(chanOr0(chan)), kPortTelemetry, pl, k, o, nullptr, pkt, sizeof(pkt));
  const bool ok = n && sendRaw(pkt, n);
  DLOG.printf("[MT] telemetri (batteri %u%%, %.2f V) %s: %s\n", level, volts,
              to == kBroadcast ? "til alle" : "som svar", ok ? "sat i koe" : "IKKE sendt");
  return ok;
}

// ---- Videresendelse (trin 4, docs/meshtastic_protokol.md afsnit 9) ---------------------

MtNode::Relay* MtNode::findRelay(uint32_t from, uint32_t id) {
  for (Relay& r : relay_)
    if (r.id && r.id == id && r.from == from) return &r;
  return nullptr;
}

void MtNode::cancelRelay(uint32_t from, uint32_t id, const char* why) {
  Relay* r = findRelay(from, id);
  if (!r) return;
  ++st_.relay_canceled;
  DLOG.printf("[MT] videresendelse af !%08lx %08lx aflyst (%s)\n", (unsigned long)from, (unsigned long)id, why);
  *r = Relay{};
}

// Ventetid som en CLIENT: et fast stykke (2 x CWmax slots), så ROUTER-noder kommer først, plus
// et tilfældigt antal slots i et vindue, der vokser med SNR (svagt signal = sender tidligt).
uint32_t MtNode::relayDelayMs(int8_t snr_q4) const {
  constexpr int kCwMin = 3, kCwMax = 8, kSnrMin = -20, kSnrMax = 10;
  int snr = snr_q4 / 4;
  if (snr < kSnrMin) snr = kSnrMin;
  if (snr > kSnrMax) snr = kSnrMax;
  const int cw = kCwMin + (snr - kSnrMin) * (kCwMax - kCwMin) / (kSnrMax - kSnrMin);
  const uint32_t slot = cfg_.slot_ms ? cfg_.slot_ms : 28;
  return 2u * kCwMax * slot + (esp_random() % (1u << cw)) * slot;
}

void MtNode::maybeRelay(const uint8_t* raw, size_t len, int8_t snr_q4, bool readable) {
  if (!cfg_.relay || !cfg_.tx || testing_ || len < 16 || len > 255) return;
  const uint32_t to = le32(raw), from = le32(raw + 4), id = le32(raw + 8);
  const uint32_t self = mt_tx::nodeNum();
  const uint8_t hl = raw[12] & 7, next_hop = raw[14];
  if (to == self || from == self || !id || hl == 0) return;
  if (next_hop && next_hop != uint8_t(self & 0xFF)) return;  // en anden node er valgt som næste hop
  if (cfg_.relay == 1 && !readable) return;
  if (findRelay(from, id)) return;
  Relay* slot = nullptr;
  for (Relay& r : relay_)
    if (!r.id) slot = &r;
  if (!slot) {
    DLOG.printf("[MT] videresendelse af !%08lx %08lx sprunget over (koeen er fuld)\n", (unsigned long)from,
                (unsigned long)id);
    return;
  }
  slot->from = from;
  slot->id = id;
  slot->len = uint8_t(len);
  memcpy(slot->pkt, raw, len);
  slot->pkt[12] = uint8_t((raw[12] & ~7) | (hl - 1));  // hop_limit - 1; hop_start, want_ack, via_mqtt uændret
  slot->pkt[14] = 0;                                    // næste hop: ingen præference (flood)
  slot->pkt[15] = uint8_t(self & 0xFF);                 // relay_node = os
  const uint32_t wait = relayDelayMs(snr_q4);
  slot->due_ms = uint32_t(millis()) + wait;
  DLOG.printf("[MT] sender !%08lx %08lx videre om %lu ms (hop_limit %u)\n", (unsigned long)from, (unsigned long)id,
              (unsigned long)wait, hl - 1);
}

// ---- Traceroute (docs/meshtastic_protokol.md afsnit 11) ----------------------------------

namespace {
struct RouteList {
  uint32_t node[8];
  int8_t snr[8];
  int nn = 0, ns = 0;
};
struct RouteDisc {
  RouteList towards, back;
};

// Gentagne tal: enten "packed" (wt 2) eller enkeltvis (wt 0 / wt 5).
void readNodes(Pb& pb, uint8_t wt, RouteList& l) {
  if (wt == 5) {
    uint32_t v;
    if (pb.fixed32(v) && l.nn < 8) l.node[l.nn++] = v;
  } else if (wt == 2) {
    const uint8_t* b;
    size_t n;
    if (!pb.bytes(b, n)) return;
    for (size_t i = 0; i + 4 <= n && l.nn < 8; i += 4) l.node[l.nn++] = le32(b + i);
  } else {
    pb.skip(wt);
  }
}
void readSnrs(Pb& pb, uint8_t wt, RouteList& l) {
  uint64_t v;
  if (wt == 0) {
    if (pb.varint(v) && l.ns < 8) l.snr[l.ns++] = int8_t(int32_t(v));
  } else if (wt == 2) {
    const uint8_t* b;
    size_t n;
    if (!pb.bytes(b, n)) return;
    Pb in(b, n);
    while (in.i < in.n && l.ns < 8 && in.varint(v)) l.snr[l.ns++] = int8_t(int32_t(v));
  } else {
    pb.skip(wt);
  }
}
RouteDisc parseRoute(const uint8_t* p, size_t n) {
  RouteDisc r;
  if (!p) return r;
  Pb pb(p, n);
  uint32_t f;
  uint8_t wt;
  while (pb.next(f, wt)) {
    if (f == 1) readNodes(pb, wt, r.towards);
    else if (f == 2) readSnrs(pb, wt, r.towards);
    else if (f == 3) readNodes(pb, wt, r.back);
    else if (f == 4) readSnrs(pb, wt, r.back);
    else if (!pb.skip(wt)) break;
  }
  return r;
}
size_t packNodes(uint8_t* p, uint32_t field, const RouteList& l) {
  if (!l.nn) return 0;
  uint8_t tmp[32];
  for (int i = 0; i < l.nn; ++i) memcpy(tmp + 4 * i, &l.node[i], 4);
  return pbBytes(p, field, tmp, size_t(4 * l.nn));
}
size_t packSnrs(uint8_t* p, uint32_t field, const RouteList& l) {
  if (!l.ns) return 0;
  uint8_t tmp[8 * 10];
  size_t k = 0;
  for (int i = 0; i < l.ns; ++i) k += pbVarint(tmp + k, uint64_t(int64_t(l.snr[i])));  // negativ = 10 byte
  return pbBytes(p, field, tmp, k);
}
size_t encodeRoute(uint8_t* p, const RouteDisc& r) {
  size_t k = packNodes(p, 1, r.towards);
  k += packSnrs(p + k, 2, r.towards);
  k += packNodes(p + k, 3, r.back);
  k += packSnrs(p + k, 4, r.back);
  return k;
}
// Udfyld "ukendte hop", så listen har én post pr. hop pakken har taget, og tilføj vores SNR
// (og vores nummer, medmindre vi er målet/afsenderen).
void appendHop(RouteList& l, uint8_t hops, int8_t snr_q4, uint32_t self, bool snr_only) {
  while (l.nn < hops && l.nn < 8) l.node[l.nn++] = 0xFFFFFFFFu;
  while (l.ns < l.nn && l.ns < 8) l.snr[l.ns++] = -128;
  if (l.ns < 8) l.snr[l.ns++] = snr_q4 < -127 ? -127 : snr_q4;
  if (!snr_only && l.nn < 8) l.node[l.nn++] = self;
}
}  // namespace

bool MtNode::sendTraceroute(uint32_t to) {
  if (!scan_ || scan_->mtChannelCount() == 0 || !to || to == kBroadcast) return false;
  uint8_t pkt[256];
  mt_tx::Opts o;
  o.to = to;
  o.hop_limit = cfg_.hop_limit;
  o.want_response = true;
  o.id = mt_tx::newId();
  const size_t n = mt_tx::buildData(scan_->mtChannel(0), kPortTraceroute, nullptr, 0, o, nullptr, pkt, sizeof(pkt));
  const bool ok = n && sendRaw(pkt, n);
  if (ok) {
    trace_id_ = o.id;
    trace_to_ = to;
  }
  char name[40];
  nameOf(to, name, sizeof(name));
  DLOG.printf("[MT] traceroute til %s: %s\n", name, ok ? "sendt" : "IKKE sendt");
  return ok;
}

void MtNode::handleTraceroute(const uint8_t* p, size_t n, bool want_response, uint32_t request_id, uint32_t from,
                              uint32_t to, uint32_t id, int chan, uint8_t hops, int8_t snr_q4) {
  const uint32_t self = mt_tx::nodeNum();
  if (to != self || testing_) return;  // (videresendte traceroutes ændres ikke: vi vises som "?")
  RouteDisc r = parseRoute(p, n);
  char from_name[40];
  nameOf(from, from_name, sizeof(from_name));
  if (!request_id) {
    if (!want_response) return;
    // Forespørgsel til os: tilføj vores SNR mod målet og svar med ruten.
    appendHop(r.towards, hops, snr_q4, self, true);
    uint8_t pl[160];
    const size_t k = encodeRoute(pl, r);
    uint8_t pkt[256];
    mt_tx::Opts o;
    o.to = from;
    o.hop_limit = cfg_.hop_limit;
    o.request_id = id;
    o.id = mt_tx::newId();
    const int c = chan < 0 ? 0 : chan;
    const size_t m = scan_ ? mt_tx::buildData(scan_->mtChannel(c), kPortTraceroute, pl, k, o, nullptr, pkt, sizeof(pkt)) : 0;
    const bool ok = m && sendRaw(pkt, m);
    DLOG.printf("[MT] traceroute fra %s besvaret (%d hop): %s\n", from_name, int(hops), ok ? "sat i koe" : "IKKE sendt");
    return;
  }
  if (request_id != trace_id_ || from != trace_to_) return;
  trace_id_ = 0;
  appendHop(r.back, hops, snr_q4, self, true);
  // "Os → A (snr) → B (snr) → mål (snr)  tilbage: mål → ... → os (snr)"
  char out[150];
  size_t k = 0;
  auto add = [&](const char* fmt, auto... a) {
    if (k < sizeof(out)) k += size_t(snprintf(out + k, sizeof(out) - k, fmt, a...));
  };
  auto hopName = [&](uint32_t num, char* buf, size_t sz) {
    if (num == 0xFFFFFFFFu) snprintf(buf, sz, "?");
    else nameOf(num, buf, sz);
  };
  auto snrTxt = [](int8_t q4, char* buf, size_t sz) {
    if (q4 == -128) snprintf(buf, sz, "?");
    else snprintf(buf, sz, "%.1f", q4 / 4.0f);
  };
  char nm[40], sn[12];
  add("Traceroute: os");
  for (int i = 0; i < r.towards.nn; ++i) {
    hopName(r.towards.node[i], nm, sizeof(nm));
    snrTxt(i < r.towards.ns ? r.towards.snr[i] : -128, sn, sizeof(sn));
    add(" > %.12s (%s)", nm, sn);
  }
  snrTxt(r.towards.ns > r.towards.nn ? r.towards.snr[r.towards.nn] : -128, sn, sizeof(sn));
  add(" > %.14s (%s dB)", from_name, sn);
  if (r.back.nn || r.back.ns) {
    add(" | tilbage:");
    for (int i = 0; i < r.back.nn; ++i) {
      hopName(r.back.node[i], nm, sizeof(nm));
      add(" %.12s", nm);
    }
    snrTxt(r.back.ns ? r.back.snr[r.back.ns - 1] : -128, sn, sizeof(sn));
    add(" > os (%s dB)", sn);
  }
  DLOG.printf("[MT] %s\n", out);
  if (store_) {
    Msg msg;
    msg.rx_ms = millis();
    msg.ts = wallclock::now();
    msg.net = 2;
    msg.kind = Msg::DirectIn;
    dmConvId(from, msg.conv, sizeof(msg.conv));
    snprintf(msg.peer, sizeof(msg.peer), "%s", from_name);
    snprintf(msg.text, sizeof(msg.text), "%s", out);
    store_->push(msg);
  }
}

bool MtNode::sendFromApp(uint32_t to, int chan, const uint8_t* data, size_t len, bool want_ack, uint32_t id,
                         uint8_t hop_limit) {
  if (!scan_ || !len || len > 233) return false;
  DataMsg d;
  if (!parseData(data, len, d)) return false;
  // Som Meshtastic-firmwaren: lokale pakker har altid bitfield (bit 1 = want_response).
  uint8_t withbf[240];
  if (!d.has_bitfield && len + 2 <= sizeof(withbf)) {
    memcpy(withbf, data, len);
    withbf[len] = 0x48;  // felt 9 (bitfield), varint
    withbf[len + 1] = d.want_response ? 2 : 0;
    data = withbf;
    len += 2;
  }
  if (!id) id = mt_tx::newId();
  if (!hop_limit || hop_limit > 7) hop_limit = cfg_.hop_limit;
  const uint32_t self = mt_tx::nodeNum();
  const MtNodeEntry* e = (to != kBroadcast && to != self) ? find(to) : nullptr;
  const bool pki = e && e->has_key && d.port != kPortNodeInfo && d.port != kPortPosition && d.port != kPortRouting &&
                   d.port != kPortTraceroute && mt_pki::ready();
  uint8_t pkt[256];
  size_t n = 0;
  if (pki) {
    mt_tx::header(pkt, to, id, hop_limit, want_ack, 0);
    const size_t c = mt_pki::encrypt(e->pubkey, self, id, data, len, pkt + 16);
    n = c ? 16 + c : 0;
  } else {
    const int c = chanOr0(chan);
    const MtChannel& ch = scan_->mtChannel(c);
    mt_tx::header(pkt, to, id, hop_limit, want_ack, ch.hash);
    uint8_t nonce[16] = {};
    memcpy(nonce, pkt + 8, 4);
    memcpy(nonce + 8, pkt + 4, 4);
    if (ch.klen == 0) {
      memcpy(pkt + 16, data, len);
    } else if (ch.klen == 16) {
      CTR<AES128> ctr;
      ctr.setKey(ch.key, 16);
      ctr.setIV(nonce, sizeof(nonce));
      ctr.setCounterSize(4);
      ctr.encrypt(pkt + 16, data, len);
    } else {
      CTR<AES256> ctr;
      ctr.setKey(ch.key, 32);
      ctr.setIV(nonce, sizeof(nonce));
      ctr.setCounterSize(4);
      ctr.encrypt(pkt + 16, data, len);
    }
    n = 16 + len;
  }
  if (!n || n > 255 || !sendRaw(pkt, n)) return false;
  seen(self, id);
  // DM med want_ack: gensend som vores egne (3 forsøg); fejl meldes til appen i dmFinal.
  PendingDm* slot = nullptr;
  if (want_ack && to != kBroadcast) {
    for (PendingDm& p : dms_)
      if (!p.id) {
        p = PendingDm{};
        p.id = id;
        p.to = to;
        p.tries = 1;
        p.len = uint8_t(n);
        memcpy(p.pkt, pkt, n);
        p.next_ms = uint32_t(millis()) + kDmRetryMs;
        slot = &p;
        break;
      }
  }
  // Tekst fra appen vises også i T-Deck'ens Chat, og status (leveret/FEJL) følger med: DM'er via
  // dmFinal (gemmes, når status er endelig), kanalbeskeder, når vi hører dem sendt videre.
  if (d.port == kPortText && d.payload && store_) {
    Msg m;
    m.rx_ms = millis();
    m.ts = wallclock::now();
    m.net = 2;
    m.kind = to == kBroadcast ? Msg::ChannelOut : Msg::DirectOut;
    m.status = Msg::Queued;
    if (to == kBroadcast) {
      const MtChannel& ch = scan_->mtChannel(chanOr0(chan));
      snprintf(m.conv, sizeof(m.conv), "%s", ch.conv);
      snprintf(m.peer, sizeof(m.peer), "%s", ch.name);
    } else {
      dmConvId(to, m.conv, sizeof(m.conv));
      nameOf(to, m.peer, sizeof(m.peer));
    }
    copyStr(m.text, sizeof(m.text), d.payload, d.plen);
    const uint32_t seq = store_->push(m, !slot);
    if (slot) {
      slot->seq = seq;
    } else if (to == kBroadcast) {
      pending_[pending_i_] = Pending{id, seq, uint32_t(millis())};
      pending_i_ = (pending_i_ + 1) % 16;
    }
  }
  DLOG.printf("[MT] fra appen: port %lu til !%08lx (%s, %u byte)\n", (unsigned long)d.port, (unsigned long)to,
              pki ? "PKI" : "kanal", unsigned(n));
  return true;
}

int MtNode::sendDm(uint32_t to, const char* text) {
  if (!text || !*text || !mt_pki::ready()) return 0;
  const MtNodeEntry* e = find(to);
  // Ældre firmware (før PKI) sender NodeInfo uden offentlig nøgle. Kender vi nodens NodeInfo,
  // men ingen nøgle, krypteres DM'en med den primære kanals nøgle (som Meshtastic selv gør).
  const bool legacy = e && !e->has_key && e->long_name[0];
  if (!e || (!e->has_key && !legacy)) {
    // Uden modtagerens nøgle kan der ikke krypteres: bed om dens NodeInfo (med nøglen).
    sendNodeInfo(to, 0, true);
    DLOG.printf("[MT] DM til !%08lx: noeglen kendes ikke - har bedt om NodeInfo\n", (unsigned long)to);
    return -1;
  }
  PendingDm* slot = nullptr;
  for (PendingDm& p : dms_)
    if (!p.id) slot = &p;
  if (!slot) return 0;  // for mange DM'er i luften
  size_t tl = strlen(text);
  if (tl > 160) tl = 160;
  mt_tx::Opts o;
  o.to = to;
  o.hop_limit = cfg_.hop_limit;
  o.want_ack = true;
  o.id = mt_tx::newId();
  uint8_t pkt[256];
  size_t c = 0;
  if (legacy) {
    uint8_t sig[64];
    if (signOwn(kPortText, o, reinterpret_cast<const uint8_t*>(text), tl, sig)) o.signature = sig;
    const size_t n = scan_ && scan_->mtChannelCount()
                         ? mt_tx::buildData(scan_->mtChannel(0), kPortText, reinterpret_cast<const uint8_t*>(text), tl,
                                            o, nullptr, pkt, sizeof(pkt))
                         : 0;
    c = n > 16 ? n - 16 : 0;
  } else {
    uint8_t plain[240];
    const size_t k =
        mt_tx::encodeData(kPortText, reinterpret_cast<const uint8_t*>(text), tl, o, plain, sizeof(plain));
    mt_tx::header(pkt, to, o.id, o.hop_limit, true, 0);  // kanal-hash 0 = PKI
    c = k ? mt_pki::encrypt(e->pubkey, mt_tx::nodeNum(), o.id, plain, k, pkt + 16) : 0;
  }
  const bool ok = c && 16 + c <= 255 && sendRaw(pkt, 16 + c);
  Msg m;
  m.rx_ms = millis();
  m.ts = wallclock::now();
  m.net = 2;
  m.kind = Msg::DirectOut;
  m.status = ok ? Msg::Queued : Msg::Failed;
  dmConvId(to, m.conv, sizeof(m.conv));
  nameOf(to, m.peer, sizeof(m.peer));
  snprintf(m.text, sizeof(m.text), "%s", text);
  const uint32_t seq = store_ ? store_->push(m, !ok) : 0;  // gemmes, når status er endelig
  if (ok) {
    *slot = PendingDm{};
    slot->id = o.id;
    slot->to = to;
    slot->seq = seq;
    slot->tries = 1;
    slot->len = uint8_t(16 + c);
    memcpy(slot->pkt, pkt, 16 + c);
    slot->next_ms = uint32_t(millis()) + kDmRetryMs;
    ccPhone(to, o.id, 0, !legacy, text, tl, true);
  }
  DLOG.printf("[MT] DM til %s (%s): %s (%s)\n", m.peer, legacy ? "kanalnoegle, aeldre node uden PKI" : "PKI", text, ok ? "sat i koe" : cfg_.tx ? "FEJL" : "afsendelse er FRA");
  return ok ? 1 : 0;
}

void MtNode::dmFinal(PendingDm& p, bool delivered, const char* why) {
  if (store_ && p.seq) store_->setStatus(p.seq, delivered ? Msg::Delivered : Msg::Failed, true);
  if (!delivered && phone_) phone_->onSendFailed(p.id, 5);  // MAX_RETRANSMIT
  DLOG.printf("[MT] DM %08lx til !%08lx: %s (%s)\n", (unsigned long)p.id, (unsigned long)p.to,
              delivered ? "LEVERET" : "FEJLEDE", why);
  p = PendingDm{};
}

// ---- Selvtest -------------------------------------------------------------------------

namespace {
// Krypteret pakke fra `from` på kanal ch med Data{port, payload}.
size_t testPacket(const MtChannel& ch, uint32_t from, uint32_t id, uint32_t port, const uint8_t* pl, size_t pn,
                  uint8_t* out) {
  uint8_t data[220];
  size_t k = pbU(data, 1, port);
  k += pbBytes(data + k, 2, pl, pn);
  const uint32_t to = 0xFFFFFFFFu;
  memcpy(out, &to, 4);
  memcpy(out + 4, &from, 4);
  memcpy(out + 8, &id, 4);
  out[12] = uint8_t(3 | (3 << 5));
  out[13] = ch.hash;
  out[14] = 0;
  out[15] = uint8_t(from);
  uint8_t nonce[16] = {};
  memcpy(nonce, &id, 4);
  memcpy(nonce + 8, &from, 4);
  if (ch.klen == 16) {
    CTR<AES128> c;
    c.setKey(ch.key, 16);
    c.setIV(nonce, 16);
    c.setCounterSize(4);
    c.encrypt(out + 16, data, k);
  } else if (ch.klen == 32) {
    CTR<AES256> c;
    c.setKey(ch.key, 32);
    c.setIV(nonce, 16);
    c.setCounterSize(4);
    c.encrypt(out + 16, data, k);
  } else {
    memcpy(out + 16, data, k);
  }
  return 16 + k;
}
}  // namespace

bool MtNode::selfTest() {
  if (!scan_ || scan_->mtChannelCount() == 0 || !nodes_) return false;
  const MtChannel& ch = scan_->mtChannel(0);
  const uint32_t from = 0x7E57D00Du;
  testing_ = true;
  test_text_[0] = 0;
  const Stats before = st_;
  uint8_t pl[200], pkt[256];
  // NodeInfo (User): id, langt navn, kort navn, hw_model, rolle, offentlig nøgle.
  // Den opdigtede nodes nøgle = vores egen (X25519 er symmetrisk), så en PKI-DM "fra" den kan
  // dekrypteres her.
  const uint8_t* key = mt_pki::publicKey();
  size_t k = pbBytes(pl, 1, "!7e57d00d", 9);
  k += pbBytes(pl + k, 2, "Selvtest Æøå", strlen("Selvtest Æøå"));
  k += pbBytes(pl + k, 3, "TST", 3);
  k += pbU(pl + k, 5, 43);
  k += pbU(pl + k, 7, 0);
  k += pbBytes(pl + k, 8, key, 32);
  k += pbU(pl + k, 99, 12345);  // ukendt felt: skal springes over
  onPacket(pkt, testPacket(ch, from, 0x1001, kPortNodeInfo, pl, k, pkt), -80, 20, 1);
  // Position
  k = pbFixed32(pl, 1, uint32_t(int32_t(556761234)));
  k += pbFixed32(pl + k, 2, uint32_t(int32_t(125683210)));
  k += pbU(pl + k, 3, 42);
  k += pbFixed32(pl + k, 4, 1763300000u);
  onPacket(pkt, testPacket(ch, from, 0x1002, kPortPosition, pl, k, pkt), -80, 20, 2);
  // Telemetri: DeviceMetrics { battery_level=87, voltage=3.95 }
  uint8_t dmx[16];
  float v = 3.95f;
  uint32_t vb;
  memcpy(&vb, &v, 4);
  size_t d = pbU(dmx, 1, 87);
  d += pbFixed32(dmx + d, 2, vb);
  k = pbFixed32(pl, 1, 1763300000u);
  k += pbBytes(pl + k, 2, dmx, d);
  onPacket(pkt, testPacket(ch, from, 0x1003, kPortTelemetry, pl, k, pkt), -80, 20, 3);
  // Tekst (to gange: anden gang er en gentagelse fra en anden node)
  const char* txt = "Hej fra selvtesten 😀";
  const size_t tn = testPacket(ch, from, 0x1004, kPortText, reinterpret_cast<const uint8_t*>(txt), strlen(txt), pkt);
  onPacket(pkt, tn, -80, 20, 4);
  onPacket(pkt, tn, -90, 8, 5);
  const bool txt_seen = !strcmp(test_text_, txt);
  // PKI-DM (X25519 + AES-256-CCM) fra den opdigtede node til os.
  test_text_[0] = 0;
  const char* dm_txt = "Hemmelig PKI-DM æøå";
  {
    mt_tx::Opts o;
    o.to = mt_tx::nodeNum();
    o.id = 0x1005;
    uint8_t plainx[200];
    const size_t k2 = mt_tx::encodeData(kPortText, reinterpret_cast<const uint8_t*>(dm_txt), strlen(dm_txt), o,
                                        plainx, sizeof(plainx));
    const uint32_t to2 = mt_tx::nodeNum(), id2 = 0x1005;
    memcpy(pkt, &to2, 4);
    memcpy(pkt + 4, &from, 4);
    memcpy(pkt + 8, &id2, 4);
    pkt[12] = uint8_t(3 | (3 << 5));
    pkt[13] = 0;
    pkt[14] = 0;
    pkt[15] = uint8_t(from);
    const size_t c2 = mt_pki::encrypt(key, from, id2, plainx, k2, pkt + 16);
    if (c2) onPacket(pkt, 16 + c2, -80, 20, 6);
  }
  const bool ok_pki = !strcmp(test_text_, dm_txt) && mt_pki::selfTest();
  snprintf(test_text_, sizeof(test_text_), "%s", txt_seen ? txt : "");
  testing_ = false;

  const MtNodeEntry* e = find(from);
  const bool ok_info = e && !strcmp(e->long_name, "Selvtest Æøå") && !strcmp(e->short_name, "TST") &&
                       e->hw == 43 && e->has_key && memcmp(e->pubkey, key, 32) == 0;
  const bool ok_pos = e && e->has_pos && e->lat_e7 == 556761234 && e->lon_e7 == 125683210 && e->alt_m == 42;
  const bool ok_tel = e && e->batt == 87 && e->mv >= 3940 && e->mv <= 3960;
  const bool ok_txt = !strcmp(test_text_, txt);
  const bool ok_dup = st_.dupes == before.dupes + 1;
  // Videresendelse: en pakke fra den opdigtede node lægges i køen med hop_limit - 1 og os som
  // relay_node; en kopi hørt fra en anden node aflyser den igen (intet sendes).
  bool ok_relay = false;
  {
    const Config saved = cfg_;
    cfg_.relay = 2;
    cfg_.tx = true;
    const uint32_t bc = kBroadcast, rid = 0x1006;
    memcpy(pkt, &bc, 4);
    memcpy(pkt + 4, &from, 4);
    memcpy(pkt + 8, &rid, 4);
    pkt[12] = uint8_t(3 | (3 << 5));
    pkt[13] = 0x55;
    pkt[14] = 0;
    pkt[15] = uint8_t(from);
    memset(pkt + 16, 0xA5, 20);
    maybeRelay(pkt, 36, 20, false);
    const Relay* r = findRelay(from, rid);
    const uint32_t d_lo = relayDelayMs(-120), d_hi = relayDelayMs(60);
    const uint32_t slot = cfg_.slot_ms;
    ok_relay = r && (r->pkt[12] & 7) == 2 && (r->pkt[12] >> 5) == 3 && r->pkt[15] == uint8_t(mt_tx::nodeNum()) &&
               r->len == 36 && memcmp(r->pkt + 16, pkt + 16, 20) == 0 && d_lo >= 16 * slot && d_lo < 24 * slot &&
               d_hi >= 16 * slot && d_hi < (16 + 256) * slot;
    pkt[12] = uint8_t(2 | (3 << 5));  // samme pakke, sendt videre af en anden
    seen(from, rid);
    const uint32_t c0 = st_.relay_canceled;
    onPacket(pkt, 36, -90, 0, 7);
    ok_relay = ok_relay && !findRelay(from, rid) && st_.relay_canceled == c0 + 1;
    cfg_ = saved;
  }
  // Traceroute: rute med ukendte hop og negative SNR'er kodes og læses tilbage uændret.
  bool ok_trace = false;
  {
    RouteDisc r;
    appendHop(r.towards, 2, -50, 0x11223344u, false);  // 1 ukendt hop + os
    appendHop(r.back, 0, 13, 0, true);
    uint8_t buf[160];
    const size_t k = encodeRoute(buf, r);
    const RouteDisc q = parseRoute(buf, k);
    ok_trace = q.towards.nn == 3 && q.towards.node[0] == 0xFFFFFFFFu && q.towards.node[2] == 0x11223344u &&
               q.towards.ns == 3 && q.towards.snr[0] == -128 && q.towards.snr[2] == -50 && q.back.nn == 0 &&
               q.back.ns == 1 && q.back.snr[0] == 13;
  }
  const bool ok = ok_info && ok_pos && ok_tel && ok_txt && ok_dup && ok_pki && ok_relay && ok_trace;
  DLOG.printf("[MT] selvtest paa %s: nodeinfo %s, position %s, telemetri %s, tekst %s, gentagelse %s, PKI-DM %s, "
              "videresendelse %s, traceroute %s -> %s\n",
              ch.name, ok_info ? "OK" : "FEJL", ok_pos ? "OK" : "FEJL", ok_tel ? "OK" : "FEJL",
              ok_txt ? "OK" : "FEJL", ok_dup ? "OK" : "FEJL", ok_pki ? "OK" : "FEJL", ok_relay ? "OK" : "FEJL",
              ok_trace ? "OK" : "FEJL", ok ? "BESTAAET" : "FEJLEDE");
  // Fjern den opdigtede node igen (uden at gemme: den har aldrig været på SD).
  const bool was_dirty = dirty_;
  removeNode(from);
  dirty_ = was_dirty;
  st_ = before;
  return ok;
}

bool MtNode::removeNode(uint32_t num) {
  for (int i = 0; i < n_; ++i)
    if (nodes_[i].num == num) {
      memmove(&nodes_[i], &nodes_[i + 1], sizeof(MtNodeEntry) * size_t(n_ - i - 1));
      --n_;
      ++version_;
      dirty_ = true;  // gemmes ved næste tick
      return true;
    }
  return false;
}

// ---- SD -----------------------------------------------------------------------------

void MtNode::load() {
  FILE* f = fopen(kFile, "r");
  if (!f) return;
  char line[256];
  while (fgets(line, sizeof(line), f) && n_ < kMax) {
    if (line[0] == ';' || line[0] == '\n') continue;
    line[strcspn(line, "\r\n")] = 0;
    char* col[17] = {};
    int k = 0;
    for (char* s = line; k < 17;) {
      col[k++] = s;
      char* t = strchr(s, '\t');
      if (!t) break;
      *t = 0;
      s = t + 1;
    }
    if (k < 16) continue;
    MtNodeEntry& e = nodes_[n_];
    e = MtNodeEntry{};
    e.num = uint32_t(strtoul(col[0], nullptr, 16));
    if (!e.num) continue;
    snprintf(e.long_name, sizeof(e.long_name), "%s", col[1]);
    snprintf(e.short_name, sizeof(e.short_name), "%s", col[2]);
    e.hw = uint8_t(atoi(col[3]));
    e.role = uint8_t(atoi(col[4]));
    e.has_key = strlen(col[5]) == 64 && fromHex(col[5], e.pubkey, 32);
    e.lat_e7 = int32_t(atol(col[6]));
    e.lon_e7 = int32_t(atol(col[7]));
    e.alt_m = int32_t(atol(col[8]));
    e.has_pos = e.lat_e7 || e.lon_e7;
    e.pos_ts = uint32_t(strtoul(col[9], nullptr, 10));
    e.last_ts = uint32_t(strtoul(col[10], nullptr, 10));
    e.rssi = int16_t(atoi(col[11]));
    e.snr_q4 = int8_t(atoi(col[12]));
    e.hops = uint8_t(atoi(col[13]));
    e.batt = uint8_t(atoi(col[14]));
    e.mv = uint16_t(atoi(col[15]));
    e.pkts = k > 16 ? uint32_t(strtoul(col[16], nullptr, 10)) : 0;
    ++n_;
  }
  fclose(f);
}

void MtNode::save() {
  mkdir("/sd/dualmesh/meshtastic", 0775);
  FILE* f = fopen(kTmp, "w");
  if (!f) return;
  fprintf(f, "; DualMesh Meshtastic-noder: id navn kort hw rolle noegle lat lon hoejde postid sidst rssi snr hop batt mV pakker\n");
  char hex[65];
  for (int i = 0; i < n_; ++i) {
    const MtNodeEntry& e = nodes_[i];
    if (e.has_key) toHex(e.pubkey, 32, hex);
    else snprintf(hex, sizeof(hex), "-");
    fprintf(f, "%08lx\t%s\t%s\t%u\t%u\t%s\t%ld\t%ld\t%ld\t%lu\t%lu\t%d\t%d\t%u\t%u\t%u\t%lu\n",
            (unsigned long)e.num, e.long_name, e.short_name, e.hw, e.role, hex, long(e.lat_e7),
            long(e.lon_e7), long(e.alt_m), (unsigned long)e.pos_ts, (unsigned long)e.last_ts, e.rssi,
            e.snr_q4, e.hops, e.batt, e.mv, (unsigned long)e.pkts);
  }
  const bool ok = fflush(f) == 0 && !ferror(f);
  fclose(f);
  if (!ok) return;
  remove(kFile);
  rename(kTmp, kFile);
}

void MtNode::tick(uint32_t now_ms) {
  // Gensend DM'er uden ACK (3 forsøg i alt); hørt sendt videre = vent kun på ACK.
  for (PendingDm& p : dms_) {
    if (!p.id || int32_t(now_ms - p.next_ms) < 0) continue;
    if (p.relayed || p.tries >= 3) {
      dmFinal(p, false, p.relayed ? "sendt videre, men ingen ACK" : "ingen ACK efter 3 forsoeg");
      continue;
    }
    ++p.tries;
    p.next_ms = now_ms + kDmRetryMs;
    const bool ok = sendRaw(p.pkt, p.len);
    DLOG.printf("[MT] gensender DM %08lx (forsoeg %u): %s\n", (unsigned long)p.id, p.tries, ok ? "OK" : "FEJL");
  }
  // Ventende videresendelser.
  for (Relay& r : relay_) {
    if (!r.id || int32_t(now_ms - r.due_ms) < 0) continue;
    const bool ok = cfg_.relay && sendRaw(r.pkt, r.len);
    if (ok) ++st_.relayed;
    DLOG.printf("[MT] videresendt !%08lx %08lx (hop_limit %u): %s\n", (unsigned long)r.from, (unsigned long)r.id,
                r.pkt[12] & 7, ok ? "sat i koe" : "IKKE sendt");
    r = Relay{};
  }
  if (cfg_.tx && cfg_.telemetry_min && int32_t(now_ms - next_telemetry_ms_) >= 0) {
    const int dc = dataChannel();
    if (dc >= 0) sendTelemetry(kBroadcast, 0, dc);
    else DLOG.printf("[MT] telemetri ikke sendt: kanalen \"%s\" findes ikke\n", cfg_.data_channel);
    next_telemetry_ms_ = now_ms + uint32_t(cfg_.telemetry_min) * 60000u;
  }
  // Egen NodeInfo og Position med jævne mellemrum (første efter 1 og 2 min).
  if (cfg_.tx && int32_t(now_ms - next_nodeinfo_ms_) >= 0) {
    sendNodeInfo();
    next_nodeinfo_ms_ = now_ms + (cfg_.nodeinfo_h ? uint32_t(cfg_.nodeinfo_h) * 3600000u : 0x7FFFFFFFu);
  }
  if (cfg_.tx && cfg_.position_min && int32_t(now_ms - next_position_ms_) >= 0) {
    const int dc = dataChannel();  // sender kun, hvis "Del position" tillader det
    const bool sent = dc >= 0 && sendPosition(kBroadcast, 0, dc);
    if (dc < 0) DLOG.printf("[MT] position ikke sendt: kanalen \"%s\" findes ikke\n", cfg_.data_channel);
    // Ikke sendt (deling fra, intet fix, ...): tjek igen om 1 min, så en ny "Del position"
    // virker med det samme i stedet for efter et helt interval.
    next_position_ms_ = now_ms + (sent ? uint32_t(cfg_.position_min) * 60000u : 60000u);
  }
  if (!dirty_ || !sd_ || now_ms - saved_ms_ < kSaveEveryMs) return;
  saved_ms_ = now_ms;
  dirty_ = false;
  save();
}

}  // namespace dm
