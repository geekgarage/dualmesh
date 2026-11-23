// SPDX-License-Identifier: MIT
// DualMesh – Bluetooth-tjenesten til Meshtastic-appen (docs/meshtastic_protokol.md afsnit 12):
// ToRadio (skriv), FromRadio (læs) og FromNum (læs/notify). Kun transporten: protokollen ligger i
// net/mt_phone.*. Samme parring som MeshCore-appen (fast PIN, krypteret og bundet forbindelse).
// Kun én af de to apps kan bruges ad gangen (Indst. > Bluetooth > "App").
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

class MtBle {
 public:
  static constexpr size_t kMaxFrame = 512;
  // pop: næste FromRadio til appen (0 = intet). Kaldes fra Bluetooth-tasken.
  using PopFn = size_t (*)(uint8_t* buf, size_t cap);
  bool begin(const char* name, uint32_t pin, PopFn pop);
  bool connected() const;
  // Én ToRadio fra appen (kopieret), eller 0. Kaldes fra loop().
  size_t poll(uint8_t* dest);
  // Der er nyt i FromRadio: notificér FromNum.
  void notify();
  void loop();  // genstart annoncering efter afbrydelse

 private:
  bool started_ = false;
};

}  // namespace dm
