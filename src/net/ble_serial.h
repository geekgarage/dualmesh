// SPDX-License-Identifier: MIT
// DualMesh – BLE-transport til MeshCore-appen (Nordic UART-service, som MeshCores
// companion-firmware). Bygget efter MeshCores SerialBLEInterface (MIT,
// Copyright (c) 2025 Scott Powell / rippleradios.com), med Arduino-ESP32's BLE.
//
//   Service 6E400001-..., RX (app skriver) 6E400002-..., TX (notify) 6E400003-...
//   Parring med fast PIN (standard 123456), krypteret forbindelse krævet.
//   Én ramme pr. BLE-skrivning/notify (højst kMaxFrame byte).
// Modtagne rammer lægges i en kø fra BLE-tasken og hentes i loop()-tasken.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

class BleSerial {
 public:
  static constexpr size_t kMaxFrame = 176;  // MeshCores MAX_FRAME_SIZE
  // name = det navn, appen ser (fx "MeshCore-DualMesh_DK"). pin = 6 cifre.
  bool begin(const char* name, uint32_t pin);
  void end();  // slår BLE helt fra (frigiver ikke stakken; kræver genstart for RAM)
  bool started() const { return started_; }
  bool connected() const;
  // Sæt en ramme i sendekøen (sendes med mindst 60 ms mellemrum). false = kø fuld.
  bool writeFrame(const uint8_t* data, size_t len);
  bool writeBusy() const;
  // Kald ofte: sender næste ramme og returnerer en modtaget ramme (0 = ingen).
  size_t poll(uint8_t* dest);
  uint32_t pin() const { return pin_; }
  const char* name() const { return name_; }

 private:
  bool started_ = false;
  uint32_t pin_ = 123456;
  char name_[48] = "";
};

}  // namespace dm
