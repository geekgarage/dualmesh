// SPDX-License-Identifier: MIT
#include "net/mt_tx.h"

#include <AES.h>
#include <CTR.h>
#include <esp_mac.h>
#include <esp_random.h>

#include <cstring>

#include "net/channel_scan.h"

namespace dm {
namespace mt_tx {

static uint32_t g_num_override = 0;
uint32_t newId();
void header(uint8_t* out, uint32_t to, uint32_t id, uint8_t hop_limit, bool want_ack, uint8_t hash);

namespace {
void put32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}

size_t varint(uint8_t* p, uint64_t v) {
  size_t n = 0;
  do {
    uint8_t b = uint8_t(v & 0x7F);
    v >>= 7;
    if (v) b |= 0x80;
    p[n++] = b;
  } while (v);
  return n;
}

// Header + kryptering af Data (plain, pl byte) -> out.
size_t finish(const MtChannel& ch, const uint8_t* plain, size_t pl, uint8_t hop_limit, uint8_t* out,
              size_t cap, uint32_t to = 0xFFFFFFFFu, bool want_ack = false, uint32_t* id_out = nullptr,
              uint32_t id = 0) {
  if (16 + pl > cap || 16 + pl > 255) return 0;
  if (!id) id = newId();
  if (id_out) *id_out = id;
  header(out, to, id, hop_limit, want_ack, ch.hash);
  uint8_t nonce[16] = {};
  std::memcpy(nonce, out + 8, 4);
  std::memcpy(nonce + 8, out + 4, 4);
  if (ch.klen == 0) {
    std::memcpy(out + 16, plain, pl);
  } else if (ch.klen == 16) {
    CTR<AES128> ctr;
    ctr.setKey(ch.key, 16);
    ctr.setIV(nonce, sizeof(nonce));
    ctr.setCounterSize(4);
    ctr.encrypt(out + 16, plain, pl);
  } else {
    CTR<AES256> ctr;
    ctr.setKey(ch.key, 32);
    ctr.setIV(nonce, sizeof(nonce));
    ctr.setCounterSize(4);
    ctr.encrypt(out + 16, plain, pl);
  }
  return 16 + pl;
}

// Data { portnum = port; payload = bytes }
size_t data(uint8_t* p, uint32_t port, const uint8_t* payload, size_t n) {
  size_t k = 0;
  p[k++] = 0x08;  // felt 1, varint
  k += varint(p + k, port);
  p[k++] = 0x12;  // felt 2, længde
  k += varint(p + k, n);
  std::memcpy(p + k, payload, n);
  return k + n;
}

// Data.bitfield (felt 9, varint).
size_t bitfield(uint8_t* p, uint32_t v) {
  p[0] = 0x48;
  return 1 + varint(p + 1, v);
}
}  // namespace

uint32_t newId() {
  uint32_t id = esp_random();
  return id ? id : 1;
}

void header(uint8_t* out, uint32_t to, uint32_t id, uint8_t hop_limit, bool want_ack, uint8_t hash) {
  const uint32_t from = nodeNum();
  if (hop_limit > 7) hop_limit = 7;
  put32(out, to);
  put32(out + 4, from);
  put32(out + 8, id);
  // Flag: bit 0-2 hop_limit, bit 3 want_ack, bit 5-7 hop_start.
  out[12] = uint8_t((hop_limit & 7) | (want_ack ? 0x08 : 0) | ((hop_limit & 7) << 5));
  out[13] = hash;
  out[14] = 0;                     // next_hop: ingen (flood)
  out[15] = uint8_t(from & 0xFF);  // relay_node: os
}

size_t encodeData(uint32_t port, const uint8_t* payload, size_t n, const Opts& o, uint8_t* plain, size_t cap) {
  if (n > 200 || cap < n + 100) return 0;
  size_t k = data(plain, port, payload, n);
  if (o.want_response) {
    plain[k++] = 0x18;  // felt 3 (want_response), varint
    plain[k++] = 1;
  }
  if (o.request_id) {
    plain[k++] = 0x35;  // felt 6 (request_id), fixed32
    put32(plain + k, o.request_id);
    k += 4;
  }
  k += bitfield(plain + k, bitfieldOf(o));
  if (o.signature) {
    plain[k++] = 0x52;  // felt 10 (xeddsa_signature), bytes
    plain[k++] = 64;
    std::memcpy(plain + k, o.signature, 64);
    k += 64;
  }
  return k;
}

size_t buildData(const MtChannel& ch, uint32_t port, const uint8_t* payload, size_t n, const Opts& o,
                 uint32_t* id_out, uint8_t* out, size_t cap) {
  uint8_t plain[300];
  const size_t k = encodeData(port, payload, n, o, plain, sizeof(plain));
  if (!k) return 0;
  return finish(ch, plain, k, o.hop_limit, out, cap, o.to, o.want_ack, id_out, o.id);
}

void setNodeNum(uint32_t n) { g_num_override = n; }

uint32_t nodeNum() {
  if (g_num_override) return g_num_override;
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  return (uint32_t(mac[2]) << 24) | (uint32_t(mac[3]) << 16) | (uint32_t(mac[4]) << 8) | mac[5];
}

size_t buildText(const MtChannel& ch, const char* text, uint8_t hop_limit, uint8_t* out, size_t cap) {
  size_t tl = std::strlen(text);
  if (tl > 200) tl = 200;
  uint8_t plain[240];
  size_t pl = data(plain, 1, reinterpret_cast<const uint8_t*>(text), tl);
  pl += bitfield(plain + pl, 0);
  return finish(ch, plain, pl, hop_limit, out, cap);
}

size_t buildPosition(const MtChannel& ch, int32_t lat_e7, int32_t lon_e7, int32_t alt_m, uint32_t unix,
                     uint32_t precision_bits, uint8_t hop_limit, uint8_t* out, size_t cap) {
  uint8_t pos[48];
  size_t k = 0;
  pos[k++] = 0x0D;  // latitude_i (felt 1, sfixed32)
  put32(pos + k, uint32_t(lat_e7));
  k += 4;
  pos[k++] = 0x15;  // longitude_i (felt 2, sfixed32)
  put32(pos + k, uint32_t(lon_e7));
  k += 4;
  pos[k++] = 0x18;  // altitude (felt 3, int32 varint; negativ = 10 byte)
  k += varint(pos + k, uint64_t(int64_t(alt_m)));
  if (unix) {
    pos[k++] = 0x25;  // time (felt 4, fixed32)
    put32(pos + k, unix);
    k += 4;
  }
  pos[k++] = 0xB8;  // precision_bits (felt 23, varint): tag = 23 << 3 = 184
  pos[k++] = 0x01;
  k += varint(pos + k, precision_bits);
  uint8_t plain[80];
  size_t pl = data(plain, 3, pos, k);
  pl += bitfield(plain + pl, 0);
  return finish(ch, plain, pl, hop_limit, out, cap);
}

}  // namespace mt_tx
}  // namespace dm
