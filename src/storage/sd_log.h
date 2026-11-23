// SPDX-License-Identifier: MIT
// DualMesh – log til både USB (Serial) og SD-kortet.
//
// Alt hvad firmwaren skriver med DLOG.printf/println, går til Serial som før og
// samles samtidig i en ringbuffer i PSRAM. loop() kalder flush(), som højst hvert
// 30. s (eller når bufferen er ved at være fyldt) skriver bufferen i ét stykke til
// /sd/dualmesh/logs/NNNN.log. Én fil pr. opstart (NNNN tæller op), ny fil efter
// 4 MB, og de ældste slettes, når der er flere end kMaxFiles. Hver linje i filen
// starter med oppetiden (TT:MM:SS.mmm), så loggen kan læses uden PC tilkoblet.
//
// Sjældne, samlede skrivninger: SD-kortet deler SPI-bus med radioen.
// Uden SD-kort virker alt; bufferen løber blot rundt.
// Strømmen slukket: højst de sidste ~30 s mangler i filen.
//
// Trådsikkerhed: write() må kaldes fra alle tasks (kort kritisk sektion);
// begin()/flush() kun fra loop()-tasken.
#pragma once
#include <Print.h>

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"

namespace dm {

class SdLog : public Print {
 public:
  SdLog();
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t n) override;

  // Kaldes når SD-kortet er monteret: finder næste filnummer og rydder op.
  void begin(bool sd_ok);
  // Skriver bufferen til SD, hvis der er gået interval_ms, eller force.
  void flush(bool force = false);
  bool active() const { return sd_ok_; }
  // Sender logfilerne råt over Serial (til tools/fetch_logs.py og fejlrapport-siden):
  // "=== DMLOG-FIL <navn> <bytes> ===" + indhold for hver, til sidst "=== DMLOG-SLUT ===".
  // newest > 0: kun de nyeste filer (fx 3: denne opstart og de to før).
  void dumpToSerial(int newest = 0);
  const char* fileName() const { return path_; }
  int fileNo() const { return file_no_; }
  uint32_t droppedBytes() const { return dropped_; }
  uint32_t writtenBytes() const { return written_; }
  // Længste enkelte SD-skridt (åbn/bid/fsync) siden sidste kald (us) og hvornår det
  // startede (millis).
  // Nulstiller. Bruges til at finde årsagen til lange radiopauser (delt SPI-bus).
  uint32_t takeSlowestFlush(uint32_t& at_ms);
  // Sletter alle logfiler (også den aktuelle) og starter forfra med 0000.log.
  // Returnerer antal slettede filer (-1 uden SD).
  int clearAll();

 private:
  static constexpr size_t kBuf = 64 * 1024;
  static constexpr size_t kFlushAt = 16 * 1024;
  static constexpr uint32_t kIntervalMs = 30000;
  static constexpr uint32_t kMaxFileBytes = 4u * 1024 * 1024;
  static constexpr int kMaxFiles = 100;

  void put(uint8_t c);  // kræver lås
  void openNext();
  void cleanup();

  uint8_t* buf_ = nullptr;
  size_t head_ = 0, len_ = 0;  // ringbuffer
  bool line_start_ = true;
  bool sd_ok_ = false;
  volatile bool mute_serial_ = false;  // under dumpToSerial()
  int file_no_ = 0;
  uint32_t file_bytes_ = 0;
  uint32_t last_flush_ms_ = 0;
  uint32_t dropped_ = 0, written_ = 0;
  uint32_t slow_us_ = 0, slow_at_ms_ = 0;
  char path_[48] = "";
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

extern SdLog DLOG;

}  // namespace dm
