// SPDX-License-Identifier: MIT
// DualMesh – Meshtastic-identitet og PKI (fase 3, trin 3). Skrevet ud fra
// docs/meshtastic_protokol.md (afsnit 3, 4 og 6); ingen kode fra Meshtastic.
//
//   - X25519-nøglepar, gemt krypteret i /sd/dualmesh/meshtastic/identity.txt (hemmelig fil).
//   - Nodenummer = CRC-32 af den offentlige nøgle.
//   - PKI-DM'er: fælles nøgle = SHA-256(X25519), AES-256-CCM (L=2, 8 byte tag), 4 byte ekstra
//     nonce efter tagget.
//   - XEdDSA: signatur af NodeInfo (og andre pakker) med X25519-nøglen omsat til Ed25519.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {
namespace mt_pki {

// Hent nøglen fra SD (eller lav en ny og gem den). Kræver, at SD-krypteringen er låst op.
// false = ingen nøgle (uden SD: midlertidig nøgle til denne opstart).
bool begin(bool sd);
bool ready();
const uint8_t* publicKey();  // 32 byte
uint32_t nodeNum();          // CRC-32 af den offentlige nøgle

uint32_t crc32(const uint8_t* d, size_t n);

// PKI-kryptering af en Data-protobuf til `peer` (32 byte offentlig nøgle).
// out får ciffertekst || tag(8) || ekstra nonce(4); returnerer længden (n + 12) eller 0.
size_t encrypt(const uint8_t* peer, uint32_t from, uint32_t id, const uint8_t* plain, size_t n, uint8_t* out);
// Omvendt: in = ciffertekst || tag || ekstra nonce (n byte i alt). Returnerer klartekstens
// længde (n - 12), eller -1 hvis tagget ikke passer.
int decrypt(const uint8_t* peer, uint32_t from, uint32_t id, const uint8_t* in, size_t n, uint8_t* out);

// XEdDSA-signatur (64 byte) af den faste signaturbuffer for en Data-besked.
struct SignFields {
  uint32_t from, id, to, portnum, request_id, reply_id, emoji, bitfield;
  bool has_bitfield, want_response;
  const uint8_t* payload;
  size_t payload_len;
};
bool sign(const SignFields& f, uint8_t sig[64]);
bool verify(const uint8_t* peer_x25519, const SignFields& f, const uint8_t sig[64]);

// Selvtest: CCM frem og tilbage, signér/verificér, CRC-32-kendt værdi.
bool selfTest();

}  // namespace mt_pki
}  // namespace dm
