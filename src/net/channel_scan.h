// SPDX-License-Identifier: MIT
// DualMesh – vores Meshtastic-kanaler: den primære (fra settings.ini, standardnøglen) og
// dem, brugeren selv har tilføjet (/sd/dualmesh/meshtastic/channels.txt, "navn<TAB>psk-base64",
// hemmelig). MeshCores kanaler ligger i McNode.
//
// (Tidligere fandt modulet også "kanaler i nærheden" ud fra hørte pakker og en ordliste. Det
// er fjernet efter brugerens ønske 17. nov 2026: hverken MeshCore eller Meshtastic har det
// officielt; der er Public/primær og de kanaler, man selv tilføjer. Klassenavnet er bevaret.)
//
// Meshtastic: byte 13 i headeren er kanal-hash = XOR af navnets bytes XOR XOR af nøglens bytes.
// Kører i loop()-tasken.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

class MsgStore;

struct MtChannel {
  char name[24];
  char psk_b64[48];     // som indtastet/gemt ("AQ==" = standardnøglen)
  uint8_t key[32];
  uint8_t klen;         // 0 = ingen kryptering, 16 eller 32
  uint8_t hash;
  char conv[20];        // samtale-id ("mtc_xxxxxxxx")
};

class ChannelScan {
 public:
  static constexpr int kMaxMt = 8;

  void begin(bool sd_mounted, const char* mt_primary, MsgStore* store);

  // Vores Meshtastic-kanaler: 0 = primær, derefter de tilføjede.
  int mtChannelCount() const { return n_mtc_; }
  const MtChannel& mtChannel(int i) const { return mtc_[i]; }
  // Tilføj (navn 1-11 tegn, PSK base64 af 1/16/32 byte, tom = "AQ=="). Returnerer indeks eller -1.
  int addMtChannel(const char* name, const char* psk_b64, const char** note);
  // Slet kanal nr. i (ikke den primære, som styres i Indst.) og gem.
  bool removeMtChannel(int i);
  uint32_t version() const { return version_; }

  void dump() const;  // vores kanaler i loggen ([KANAL], monitor 'k')

 private:
  bool parseMt(const char* name, const char* psk_b64, MtChannel& out, const char** note) const;
  bool saveMt() const;

  MtChannel mtc_[kMaxMt] = {};
  int n_mtc_ = 0;
  char mt_primary_[24] = "";
  bool sd_ = false;
  MsgStore* store_ = nullptr;
  uint32_t version_ = 0;
};

}  // namespace dm
