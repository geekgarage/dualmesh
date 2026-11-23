// SPDX-License-Identifier: MIT
#include "net/mt_phone.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "net/channel_scan.h"
#include "net/mt_ble.h"
#include "net/mt_pki.h"
#include "net/mt_tx.h"
#include "storage/sd_log.h"
#include "util/b64.h"
#include "util/wall_clock.h"
#include "version.h"

namespace dm {

namespace {
portMUX_TYPE g_qmux = portMUX_INITIALIZER_UNLOCKED;
constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
constexpr uint32_t kPortRouting = 5, kPortAdmin = 6, kPortTelemetry = 67;

// Lille protobuf-skriver (feltnumre: docs/meshtastic_protokol.md afsnit 12).
struct Pw {
  uint8_t* b;
  size_t cap, n = 0;
  Pw(uint8_t* buf, size_t c) : b(buf), cap(c) {}
  void raw(const void* p, size_t l) {
    if (n + l > cap) {
      n = cap + 1;  // markér overløb
      return;
    }
    std::memcpy(b + n, p, l);
    n += l;
  }
  void var(uint64_t v) {
    uint8_t t[10];
    size_t k = 0;
    do {
      t[k++] = uint8_t((v & 0x7F) | (v > 0x7F ? 0x80 : 0));
      v >>= 7;
    } while (v);
    raw(t, k);
  }
  void key(uint32_t f, uint8_t wt) { var((uint64_t(f) << 3) | wt); }
  void u(uint32_t f, uint64_t v) {
    key(f, 0);
    var(v);
  }
  void i32(uint32_t f, int32_t v) { u(f, uint64_t(int64_t(v))); }
  void f32(uint32_t f, uint32_t v) {
    key(f, 5);
    raw(&v, 4);
  }
  void flt(uint32_t f, float v) {
    uint32_t x;
    std::memcpy(&x, &v, 4);
    f32(f, x);
  }
  void bytes(uint32_t f, const void* p, size_t l) {
    key(f, 2);
    var(l);
    raw(p, l);
  }
  void str(uint32_t f, const char* s) { bytes(f, s, std::strlen(s)); }
  bool ok() const { return n <= cap; }
};

// Læser (felt, wiretype); samme princip som i MtNode.
struct Pr {
  const uint8_t* p;
  size_t n, i = 0;
  Pr(const uint8_t* d, size_t l) : p(d), n(l) {}
  bool var(uint64_t& v) {
    v = 0;
    for (int s = 0; s < 64 && i < n; s += 7) {
      const uint8_t c = p[i++];
      v |= uint64_t(c & 0x7F) << s;
      if (!(c & 0x80)) return true;
    }
    return false;
  }
  bool next(uint32_t& f, uint8_t& wt) {
    uint64_t k;
    if (i >= n || !var(k)) return false;
    f = uint32_t(k >> 3);
    wt = uint8_t(k & 7);
    return f != 0;
  }
  bool f32(uint32_t& v) {
    if (i + 4 > n) return false;
    std::memcpy(&v, p + i, 4);
    i += 4;
    return true;
  }
  bool bytes(const uint8_t*& b, size_t& l) {
    uint64_t L;
    if (!var(L) || i + L > n) return false;
    b = p + i;
    l = size_t(L);
    i += l;
    return true;
  }
  bool skip(uint8_t wt) {
    uint64_t v;
    const uint8_t* b;
    size_t l;
    if (wt == 0) return var(v);
    if (wt == 1) return (i += 8) <= n;
    if (wt == 2) return bytes(b, l);
    if (wt == 5) return (i += 4) <= n;
    return false;
  }
};

void copyStr(char* out, size_t cap, const uint8_t* s, size_t n) {
  if (n >= cap) n = cap - 1;
  std::memcpy(out, s, n);
  out[n] = 0;
}
}  // namespace

void MtPhone::begin(MtNode* node, ChannelScan* chans, MtBle* ble, const Hooks& h) {
  node_ = node;
  chans_ = chans;
  ble_ = ble;
  h_ = h;
  if (!q_) q_ = static_cast<Frame*>(heap_caps_calloc(kQ, sizeof(Frame), MALLOC_CAP_SPIRAM));
  node_->setPhone(this);
}

bool MtPhone::push(const uint8_t* p, size_t n) {
  if (!q_ || n > sizeof(q_[0].buf)) return false;
  bool ok = false;
  portENTER_CRITICAL(&g_qmux);
  const int nx = (head_ + 1) % kQ;
  if (nx != tail_) {
    q_[head_].len = uint16_t(n);
    std::memcpy(q_[head_].buf, p, n);
    head_ = nx;
    ok = true;
  }
  portEXIT_CRITICAL(&g_qmux);
  if (!ok) DLOG.println("[BLE] Meshtastic-app: koe fuld - ramme tabt");
  return ok;
}

size_t MtPhone::pop(uint8_t* buf, size_t cap) {
  if (!q_) return 0;
  size_t n = 0;
  portENTER_CRITICAL(&g_qmux);
  if (tail_ != head_ && q_[tail_].len <= cap) {
    n = q_[tail_].len;
    std::memcpy(buf, q_[tail_].buf, n);
    tail_ = (tail_ + 1) % kQ;
  }
  portEXIT_CRITICAL(&g_qmux);
  return n;
}

void MtPhone::loop() {
  if (!ble_) return;
  static uint8_t buf[MtBle::kMaxFrame];
  for (int k = 0; k < 4; ++k) {
    const size_t n = ble_->poll(buf);
    if (!n) break;
    handleToRadio(buf, n);
  }
}

// ---- Fra appen ---------------------------------------------------------------------------

void MtPhone::handleToRadio(const uint8_t* p, size_t n) {
  Pr r(p, n);
  uint32_t f;
  uint8_t wt;
  while (r.next(f, wt)) {
    uint64_t v;
    const uint8_t* b;
    size_t l;
    if (f == 1 && wt == 2 && r.bytes(b, l)) {
      handlePacket(b, l);
    } else if (f == 3 && wt == 0 && r.var(v)) {
      DLOG.printf("[BLE] Meshtastic-app: want_config %lu\n", (unsigned long)v);
      sendConfig(uint32_t(v));
    } else if (f == 4 && wt == 0 && r.var(v)) {
      ready_ = false;  // appen afbryder
      DLOG.println("[BLE] Meshtastic-app: afbrudt af appen");
    } else if (f == 7) {  // heartbeat: svar med en køstatus, ellers lukker appen efter ~87 s
      if (!r.skip(wt)) break;
      if (ready_) pushQueueStatus(0);
    } else {
      DLOG.printf("[BLE] Meshtastic-app: ToRadio felt %lu (ikke brugt)\n", (unsigned long)f);
      if (!r.skip(wt)) break;
    }
  }
}

void MtPhone::handlePacket(const uint8_t* p, size_t n) {
  uint32_t to = kBroadcast, id = 0, chan = 0, hop = 0;
  bool want_ack = false;
  const uint8_t* data = nullptr;
  size_t dlen = 0;
  Pr r(p, n);
  uint32_t f;
  uint8_t wt;
  while (r.next(f, wt)) {
    uint64_t v;
    uint32_t x;
    if (f == 2 && wt == 5) r.f32(to);
    else if (f == 6 && wt == 5) r.f32(id);
    else if (f == 3 && wt == 0 && r.var(v)) chan = uint32_t(v);
    else if (f == 9 && wt == 0 && r.var(v)) hop = uint32_t(v);
    else if (f == 10 && wt == 0 && r.var(v)) want_ack = v != 0;
    else if (f == 4 && wt == 2) r.bytes(data, dlen);
    else if (f == 1 && wt == 5) r.f32(x);  // from: altid os
    else if (!r.skip(wt)) break;
  }
  if (!data || !dlen) return;
  const uint32_t self = mt_tx::nodeNum();
  if (!id) id = mt_tx::newId();
  pushQueueStatus(id);  // pakken er taget imod
  // Appens indstillinger: AdminMessage til os selv.
  uint32_t port = 0;
  {
    Pr d(data, dlen);
    uint64_t v;
    if (d.next(f, wt) && f == 1 && wt == 0 && d.var(v)) port = uint32_t(v);
  }
  DLOG.printf("[BLE] Meshtastic-app: pakke port %lu til !%08lx (want_ack %d)\n", (unsigned long)port,
              (unsigned long)to, want_ack ? 1 : 0);
  if (to == self || (port == kPortAdmin && to == 0)) {
    if (port == kPortAdmin) {
      Pr d(data, dlen);
      const uint8_t* pl = nullptr;
      size_t pn = 0;
      uint64_t v;
      while (d.next(f, wt)) {
        if (f == 2 && wt == 2) d.bytes(pl, pn);
        else if (!d.skip(wt)) break;
      }
      (void)v;
      handleAdmin(id, pl, pn);
    } else if (port == kPortTelemetry) {  // appen spørger om vores egen telemetri: svar lokalt
      uint8_t tp[64], level;
      float volts;
      const size_t k = node_->telemetryPayload(tp, level, volts);
      DLOG.printf("[BLE] Meshtastic-app: egen telemetri %u%% %.2f V\n", level, volts);
      pushLocal(kPortTelemetry, id, tp, k);
    }
    return;
  }
  if (want_ack && to == kBroadcast) {
    app_ids_[app_i_] = id;
    app_i_ = (app_i_ + 1) % 8;
  }
  if (!node_->sendFromApp(to, int(chan), data, dlen, want_ack, id, uint8_t(hop))) onSendFailed(id, 3);
}

// ---- Til appen ---------------------------------------------------------------------------

// FromRadio.queueStatus (res 1, ledige 2, kølængde 3, pakke-id 4): efter hver pakke fra appen
// og som svar på heartbeat (pakke-id 0).
void MtPhone::pushQueueStatus(uint32_t id) {
  uint8_t qs[24], fr[40];
  Pw q(qs, sizeof(qs));
  q.u(1, 0);
  q.u(2, 16);
  q.u(3, 16);
  q.u(4, id);
  Pw o(fr, sizeof(fr));
  o.u(1, ++from_id_);
  o.bytes(11, qs, q.n);
  if (o.ok() && push(fr, o.n) && ble_) ble_->notify();
}

void MtPhone::pushLocal(uint32_t port, uint32_t request_id, const uint8_t* payload, size_t n) {
  const uint32_t self = mt_tx::nodeNum();
  uint8_t data[360];
  Pw d(data, sizeof(data));
  d.u(1, port);
  d.bytes(2, payload, n);
  if (request_id) d.f32(6, request_id);
  if (!d.ok()) return;
  uint8_t pkt[380];
  Pw m(pkt, sizeof(pkt));
  m.f32(1, self);
  m.f32(2, self);
  m.bytes(4, data, d.n);
  m.f32(6, mt_tx::newId());
  if (const uint32_t t = wallclock::now()) m.f32(7, t);
  uint8_t fr[400];
  Pw o(fr, sizeof(fr));
  o.u(1, ++from_id_);
  o.bytes(2, pkt, m.n);
  if (o.ok() && push(fr, o.n) && ble_) ble_->notify();
}

void MtPhone::onMeshPacket(uint32_t from, uint32_t to, uint32_t id, int chan, bool pki, uint8_t hop_limit,
                           uint8_t hop_start, bool want_ack, int8_t snr_q4, int16_t rssi, const uint8_t* data,
                           size_t len) {
  if (!ready_ || !ble_ || !ble_->connected()) return;
  uint8_t pkt[380];
  Pw m(pkt, sizeof(pkt));
  m.f32(1, from);
  m.f32(2, to);
  m.u(3, uint32_t(chan < 0 ? 0 : chan));
  m.bytes(4, data, len);
  m.f32(6, id);
  if (const uint32_t t = wallclock::now()) m.f32(7, t);
  m.flt(8, snr_q4 / 4.0f);
  m.u(9, hop_limit);
  if (want_ack) m.u(10, 1);
  m.i32(12, rssi);
  m.u(15, hop_start);
  if (pki) m.u(17, 1);
  uint8_t fr[400];
  Pw o(fr, sizeof(fr));
  o.u(1, ++from_id_);
  o.bytes(2, pkt, m.n);
  if (m.ok() && o.ok() && push(fr, o.n)) ble_->notify();
}

void MtPhone::onImplicitAck(uint32_t id) {
  for (uint32_t& a : app_ids_)
    if (a && a == id) {
      a = 0;
      const uint8_t routing[2] = {0x18, 0x00};  // Routing.error_reason = 0 (ACK)
      pushLocal(kPortRouting, id, routing, sizeof(routing));
      return;
    }
}

void MtPhone::onSendFailed(uint32_t id, uint32_t err) {
  uint8_t routing[4];
  Pw r(routing, sizeof(routing));
  r.u(3, err);  // Routing.error_reason
  pushLocal(kPortRouting, id, routing, r.n);
}

// ---- Opstartsforløbet --------------------------------------------------------------------

size_t MtPhone::encodeUser(uint8_t* out, size_t cap, const MtNodeEntry* e) {
  Pw u(out, cap);
  char id[12];
  if (!e) {
    Info in{};
    if (h_.info) h_.info(in);
    const uint32_t num = mt_tx::nodeNum();
    snprintf(id, sizeof(id), "!%08lx", (unsigned long)num);
    u.str(1, id);
    u.str(2, in.long_name[0] ? in.long_name : id);
    char sn[8];
    if (in.short_name[0]) snprintf(sn, sizeof(sn), "%s", in.short_name);
    else snprintf(sn, sizeof(sn), "%04lx", (unsigned long)(num & 0xFFFF));
    u.str(3, sn);
    u.u(5, 50);  // T-Deck
    u.u(7, in.relay ? 0 : 1);
    if (mt_pki::ready()) u.bytes(8, mt_pki::publicKey(), 32);
  } else {
    snprintf(id, sizeof(id), "!%08lx", (unsigned long)e->num);
    u.str(1, id);
    // Uden NodeInfo kender vi ikke navnet: "Meshtastic xxxx" / "xxxx" (sidste 4 hex), som
    // standardnavnet på en ny node, så appen viser den i stedet for at springe den over.
    char ln[24], sn[8];
    snprintf(sn, sizeof(sn), "%04lx", (unsigned long)(e->num & 0xFFFF));
    snprintf(ln, sizeof(ln), "Meshtastic %s", sn);
    u.str(2, e->long_name[0] ? e->long_name : ln);
    u.str(3, e->short_name[0] ? e->short_name : sn);
    if (e->hw) u.u(5, e->hw);
    if (e->role) u.u(7, e->role);
    if (e->has_key) u.bytes(8, e->pubkey, 32);
  }
  return u.ok() ? u.n : 0;
}

size_t MtPhone::encodeMetadata(uint8_t* out, size_t cap) {
  Info in{};
  if (h_.info) h_.info(in);
  Pw m(out, cap);
  char ver[40];
  snprintf(ver, sizeof(ver), "2.8.2.DualMesh-%s", DM_FW_VERSION);  // Meshtastic-kompatibel version
  m.str(1, ver);
  m.u(2, 25);
  m.u(3, 1);
  m.u(4, 1);
  m.u(5, 1);
  m.u(7, in.relay ? 0 : 1);
  m.u(9, 50);
  m.u(11, 1);
  return m.ok() ? m.n : 0;
}

size_t MtPhone::encodeChannel(uint8_t* out, size_t cap, int idx) {
  Pw c(out, cap);
  c.u(1, uint32_t(idx));
  if (chans_ && idx < chans_->mtChannelCount()) {
    const MtChannel& ch = chans_->mtChannel(idx);
    uint8_t s[96];
    Pw st(s, sizeof(s));
    uint8_t psk[32];
    const int pl = b64Decode(ch.psk_b64, psk, sizeof(psk));
    st.bytes(2, psk, pl > 0 ? size_t(pl) : 0);
    st.str(3, ch.name);
    c.bytes(2, s, st.n);
    c.u(3, idx == 0 ? 1 : 2);
  } else {
    c.u(3, 0);  // DISABLED
  }
  return c.ok() ? c.n : 0;
}

size_t MtPhone::encodeConfig(uint8_t* out, size_t cap, int type) {
  Info in{};
  if (h_.info) h_.info(in);
  uint8_t s[120];
  Pw sub(s, sizeof(s));
  uint32_t field = 0;
  switch (type) {
    case 0:  // device
      field = 1;
      sub.u(1, in.relay ? 0 : 1);
      sub.u(7, in.nodeinfo_secs);
      break;
    case 1:  // position
      field = 2;
      sub.u(1, in.position_secs);
      sub.u(4, 1);
      break;
    case 2:
      field = 3;  // power: standard
      break;
    case 3:
      field = 4;  // network: styres på T-Deck'en
      break;
    case 4:
      field = 5;  // display
      break;
    case 5:  // lora
      field = 6;
      sub.u(1, 1);
      sub.u(2, uint32_t(in.preset));
      sub.u(7, uint32_t(in.region));
      sub.u(8, uint32_t(in.hop_limit));
      sub.u(9, in.tx_enabled ? 1 : 0);
      sub.i32(10, in.tx_power);
      sub.u(11, uint32_t(in.slot));
      break;
    case 6:  // bluetooth
      field = 7;
      sub.u(1, 1);
      sub.u(2, 1);  // FIXED_PIN
      sub.u(3, in.ble_pin);
      break;
    case 7:  // security
      field = 8;
      if (mt_pki::ready()) sub.bytes(1, mt_pki::publicKey(), 32);
      sub.u(9, 1);  // signerer (BALANCED)
      break;
    default:
      return 0;
  }
  Pw c(out, cap);
  c.bytes(field, s, sub.n);
  return c.ok() ? c.n : 0;
}

void MtPhone::sendConfig(uint32_t nonce) {
  if (!node_ || !q_) return;
  portENTER_CRITICAL(&g_qmux);
  head_ = tail_ = 0;  // nyt forløb: glem det gamle
  portEXIT_CRITICAL(&g_qmux);
  ready_ = false;
  uint8_t fr[400], sub[360];
  auto emit = [&](uint32_t field, const uint8_t* p, size_t n) {
    Pw o(fr, sizeof(fr));
    o.u(1, ++from_id_);
    o.bytes(field, p, n);
    if (o.ok()) push(fr, o.n);
  };
  const uint32_t self = mt_tx::nodeNum();
  // Appen beder i to runder: 69420 = kun konfiguration (uden de andre noder), 69421 = kun noderne
  // (egen node først). Andre værdier (ældre apps) = det hele i én runde.
  const bool only_config = nonce == 69420u;
  const bool only_nodes = nonce == 69421u;
  if (!only_nodes) {  // my_info
    Pw m(sub, sizeof(sub));
    m.u(1, self);
    m.u(8, 1);
    m.u(11, 30200);
    uint8_t dev[16] = {};
    if (mt_pki::ready()) std::memcpy(dev, mt_pki::publicKey(), 16);
    m.bytes(12, dev, 16);
    m.str(13, "dualmesh-tdeck-plus");
    m.u(15, uint32_t(node_->count() + 1));
    emit(3, sub, m.n);
  }
  {  // egen node
    uint8_t user[200];
    const size_t ul = encodeUser(user, sizeof(user), nullptr);
    Pw ni(sub, sizeof(sub));
    ni.u(1, self);
    ni.bytes(2, user, ul);
    // Egen position kun efter "Del position" (privatliv), med samme præcision som over radioen.
    int32_t la = 0, lo = 0, alt = 0;
    uint32_t prec = 32;
    const auto& cf = node_->config();
    if (cf.position && cf.position(la, lo, alt, prec)) {
      if (prec < 32) {  // omtrent: nederste bits nulstilles, midten af feltet bruges
        const uint32_t mask = ~((1u << (32 - prec)) - 1);
        la = int32_t((uint32_t(la) & mask) + (1u << (31 - prec)));
        lo = int32_t((uint32_t(lo) & mask) + (1u << (31 - prec)));
      }
      uint8_t pos[48];
      Pw ps(pos, sizeof(pos));
      ps.f32(1, uint32_t(la));
      ps.f32(2, uint32_t(lo));
      ps.i32(3, alt);
      if (const uint32_t t = wallclock::now()) ps.f32(4, t);
      ps.u(23, prec);  // precision_bits
      ni.bytes(3, pos, ps.n);
    }
    if (const uint32_t t = wallclock::now()) ni.f32(5, t);
    emit(4, sub, ni.n);
  }
  if (!only_nodes) {
    emit(13, sub, encodeMetadata(sub, sizeof(sub)));
    for (int i = 0; i < 8; ++i) emit(10, sub, encodeChannel(sub, sizeof(sub), i));
    for (int t = 0; t <= 7; ++t) emit(5, sub, encodeConfig(sub, sizeof(sub), t));
  }
  for (int i = 0; !only_config && i < node_->count(); ++i) {  // de andre noder
    const MtNodeEntry& e = node_->at(i);
    uint8_t user[200];
    const size_t ul = encodeUser(user, sizeof(user), &e);
    Pw ni(sub, sizeof(sub));
    ni.u(1, e.num);
    ni.bytes(2, user, ul);
    if (e.has_pos) {
      uint8_t pos[40];
      Pw ps(pos, sizeof(pos));
      ps.f32(1, uint32_t(e.lat_e7));
      ps.f32(2, uint32_t(e.lon_e7));
      ps.i32(3, e.alt_m);
      if (e.pos_ts) ps.f32(4, e.pos_ts);
      ni.bytes(3, pos, ps.n);
    }
    ni.flt(4, e.snr_q4 / 4.0f);
    if (e.last_ts) ni.f32(5, e.last_ts);
    if (e.batt || e.mv) {
      uint8_t dm[20];
      Pw m(dm, sizeof(dm));
      if (e.batt) m.u(1, e.batt);
      if (e.mv) m.flt(2, e.mv / 1000.0f);
      ni.bytes(6, dm, m.n);
    }
    ni.u(9, e.hops);
    // (Fejlsøgningslinjen pr. node er fjernet før udgivelse; kun fejl logges.)
    if (!ni.ok())
      DLOG.printf("[BLE] Meshtastic-app: node !%08lx for stor (%u byte), ikke sendt\n", (unsigned long)e.num,
                  unsigned(ni.n));
    if (ni.ok()) emit(4, sub, ni.n);
  }
  {  // config_complete_id
    Pw o(fr, sizeof(fr));
    o.u(1, ++from_id_);
    o.u(7, nonce);
    push(fr, o.n);
  }
  ready_ = true;
  if (ble_) ble_->notify();
  DLOG.printf("[BLE] Meshtastic-app: %s sendt (%d noder)\n",
              only_config ? "konfiguration" : only_nodes ? "nodeliste" : "konfiguration+noder",
              only_config ? 1 : node_->count() + 1);
}

// ---- Appens indstillinger (AdminMessage) -------------------------------------------------

void MtPhone::handleAdmin(uint32_t req_id, const uint8_t* p, size_t n) {
  if (!p) return;
  Pr r(p, n);
  uint32_t f;
  uint8_t wt;
  uint8_t resp[360];
  bool ack = false;
  while (r.next(f, wt)) {
    uint64_t v = 0;
    const uint8_t* b = nullptr;
    size_t l = 0;
    if (wt == 0) r.var(v);
    else if (wt == 2) r.bytes(b, l);
    else if (wt == 5) {
      uint32_t x;
      r.f32(x);
      v = x;
    } else if (!r.skip(wt)) {
      break;
    }
    Pw a(resp, sizeof(resp));
    uint8_t tmp[300];
    DLOG.printf("[BLE] Meshtastic-app: admin felt %lu (vaerdi %lu)\n", (unsigned long)f, (unsigned long)v);
    switch (f) {
      case 1: {  // get_channel_request (indeks + 1)
        const size_t k = encodeChannel(tmp, sizeof(tmp), int(v) - 1);
        a.bytes(2, tmp, k);
        pushLocal(kPortAdmin, req_id, resp, a.n);
        break;
      }
      case 3: {  // get_owner_request
        const size_t k = encodeUser(tmp, sizeof(tmp), nullptr);
        a.bytes(4, tmp, k);
        pushLocal(kPortAdmin, req_id, resp, a.n);
        break;
      }
      case 5: {  // get_config_request
        const size_t k = encodeConfig(tmp, sizeof(tmp), int(v));
        a.bytes(6, tmp, k);
        pushLocal(kPortAdmin, req_id, resp, a.n);
        break;
      }
      case 7: {  // get_module_config_request: standardværdier (tom)
        a.bytes(8, tmp, 0);
        pushLocal(kPortAdmin, req_id, resp, a.n);
        break;
      }
      case 12: {  // get_device_metadata_request
        const size_t k = encodeMetadata(tmp, sizeof(tmp));
        a.bytes(13, tmp, k);
        pushLocal(kPortAdmin, req_id, resp, a.n);
        break;
      }
      case 32: {  // set_owner (User)
        char ln[40] = "", sn[8] = "";
        Pr u(b, l);
        uint32_t g;
        uint8_t w;
        while (u.next(g, w)) {
          const uint8_t* s;
          size_t sl;
          if (w == 2 && u.bytes(s, sl)) {
            if (g == 2) copyStr(ln, sizeof(ln), s, sl);
            else if (g == 3) copyStr(sn, sizeof(sn), s, sl);
          } else if (!u.skip(w)) {
            break;
          }
        }
        if (h_.set_owner) h_.set_owner(ln, sn);
        ack = true;
        break;
      }
      case 33: {  // set_channel (sekundære kanaler)
        int idx = 0, role = 0;
        char name[24] = "";
        char psk_b64[48] = "";
        Pr c(b, l);
        uint32_t g;
        uint8_t w;
        while (c.next(g, w)) {
          uint64_t x;
          const uint8_t* s;
          size_t sl;
          if (g == 1 && w == 0 && c.var(x)) idx = int(x);
          else if (g == 3 && w == 0 && c.var(x)) role = int(x);
          else if (g == 2 && w == 2 && c.bytes(s, sl)) {
            Pr st(s, sl);
            uint32_t h;
            uint8_t ww;
            while (st.next(h, ww)) {
              const uint8_t* q;
              size_t ql;
              if (h == 3 && ww == 2 && st.bytes(q, ql)) copyStr(name, sizeof(name), q, ql);
              else if (h == 2 && ww == 2 && st.bytes(q, ql)) b64Encode(q, ql, psk_b64, sizeof(psk_b64));
              else if (!st.skip(ww)) break;
            }
          } else if (!c.skip(w)) {
            break;
          }
        }
        if (idx > 0 && chans_) {
          if (idx < chans_->mtChannelCount()) chans_->removeMtChannel(idx);
          const char* note = nullptr;
          if (role != 0 && name[0]) chans_->addMtChannel(name, psk_b64, &note);
          DLOG.printf("[BLE] Meshtastic-app: kanal %d %s\n", idx, role ? name : "slettet");
        } else {
          DLOG.println("[BLE] Meshtastic-app: den primaere kanal aendres i Indst. > Meshtastic");
        }
        ack = true;
        break;
      }
      case 34: {  // set_config: kun lora (region, preset, hop, effekt, sender)
        Pr c(b, l);
        uint32_t g;
        uint8_t w;
        while (c.next(g, w)) {
          const uint8_t* s;
          size_t sl;
          if (g == 6 && w == 2 && c.bytes(s, sl)) {
            int region = -1, preset = -1, hop = -1, pwr = -1, txe = -1;
            Pr lr(s, sl);
            uint32_t h;
            uint8_t ww;
            while (lr.next(h, ww)) {
              uint64_t x;
              if (ww == 0 && lr.var(x)) {
                if (h == 2) preset = int(x);
                else if (h == 7) region = int(x);
                else if (h == 8) hop = int(x);
                else if (h == 9) txe = int(x);
                else if (h == 10) pwr = int(int32_t(x));
              } else if (!lr.skip(ww)) {
                break;
              }
            }
            if (h_.set_lora) h_.set_lora(region, preset, hop, pwr, txe);
          } else if (!c.skip(w)) {
            break;
          }
        }
        ack = true;
        break;
      }
      case 38:  // remove_by_nodenum
        if (node_) node_->removeNode(uint32_t(v));
        ack = true;
        break;
      case 43:  // set_time_only
        if (h_.set_time && v) h_.set_time(uint32_t(v));
        ack = true;
        break;
      case 97:  // reboot_seconds
        if (h_.reboot) h_.reboot(uint32_t(v > 0 ? v : 1) * 1000u);
        ack = true;
        break;
      default:  // begin/commit_edit_settings, favoritter m.m.: OK, men ingen virkning
        ack = true;
        break;
    }
  }
  if (ack) {
    const uint8_t routing[2] = {0x18, 0x00};
    pushLocal(kPortRouting, req_id, routing, sizeof(routing));
  }
}

}  // namespace dm
