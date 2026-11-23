// SPDX-License-Identifier: MIT
// DualMesh – minimal Meshtastic-afsendelse (bygget ud fra den offentlige
// protokolbeskrivelse; ingen .proto-filer eller genereret kode).
//
// Pakke: 16 byte header (dest, afsender, pakke-id, flag, kanal-hash, next_hop,
// relay_node) + Data-protobuf krypteret med AES-CTR (nonce = pakke-id 64 bit LE +
// afsender 32 bit LE). Data: portnum (felt 1) + payload (felt 2).
// Bruges indtil videre kun til SOS (tekst + Position på den primære kanal);
// resten af Meshtastic-afsendelsen kommer i fase 3.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

struct MtChannel;

namespace mt_tx {

// Vores nodenummer: CRC-32 af den offentlige nøgle, når identiteten er klar (setNodeNum),
// ellers de sidste 4 byte af MAC-adressen.
uint32_t nodeNum();
void setNodeNum(uint32_t n);

static constexpr uint32_t kBroadcast = 0xFFFFFFFFu;

// Generel pakke: Data{portnum, payload, want_response, request_id} til `to` (kBroadcast =
// alle), krypteret med kanalens nøgle. id_out = pakkens id. Returnerer længden (0 = fejl).
struct Opts {
  uint32_t to = kBroadcast;
  uint8_t hop_limit = 3;
  bool want_ack = false;
  bool want_response = false;
  uint32_t request_id = 0;
  uint32_t id = 0;                    // 0 = tilfældigt
  const uint8_t* signature = nullptr; // 64 byte XEdDSA (Data felt 10)
};
// Data.bitfield (felt 9), som vi altid sender: bit 1 = want_response, bit 0 (ok_to_mqtt) = 0.
// Uden feltet smider Meshtastic 2.8 pakker med hop_start 0 væk (docs/meshtastic_protokol.md 5).
inline uint32_t bitfieldOf(const Opts& o) { return o.want_response ? 2u : 0u; }
// Data-protobuf i klartekst (til PKI-kryptering eller signatur). Returnerer længden.
size_t encodeData(uint32_t port, const uint8_t* payload, size_t n, const Opts& o, uint8_t* out, size_t cap);
// 16 byte header.
void header(uint8_t* out, uint32_t to, uint32_t id, uint8_t hop_limit, bool want_ack, uint8_t hash);
uint32_t newId();
size_t buildData(const MtChannel& ch, uint32_t port, const uint8_t* payload, size_t n, const Opts& o,
                 uint32_t* id_out, uint8_t* out, size_t cap);

// Broadcast-tekst (TEXT_MESSAGE_APP, port 1). Returnerer pakkens længde (0 = fejl).
size_t buildText(const MtChannel& ch, const char* text, uint8_t hop_limit, uint8_t* out,
                 size_t cap);
// Position (POSITION_APP, port 3): bredde/længde i grader x 1e7, højde i m, unix-tid
// (0 = ukendt), precision_bits (32 = fuld præcision).
size_t buildPosition(const MtChannel& ch, int32_t lat_e7, int32_t lon_e7, int32_t alt_m,
                     uint32_t unix, uint32_t precision_bits, uint8_t hop_limit, uint8_t* out,
                     size_t cap);

}  // namespace mt_tx
}  // namespace dm
