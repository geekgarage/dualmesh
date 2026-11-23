// SPDX-License-Identifier: MIT
// DualMesh – minimal non-blocking radio interface the scheduler drives.
// Implemented by hal/esp32/sx1262_hal (real hardware) and sim/sim_radio (PC).
#pragma once
#include <cstdint>
#include "lora_profile.h"

namespace dm {

enum class RadioEvt : uint8_t {
  CadClear,     // CAD finished, nothing detected
  CadDetected,  // CAD hit; radio has moved into RX on its own (CAD_RX mode)
  PreambleDetected,  // RX locked onto a real preamble (after a CAD hit)
  HeaderValid,       // LoRa header decoded: a packet is coming
  RxDone,       // valid packet waiting in the buffer
  RxError,      // header or CRC error
  RxTimeout,    // RX gave up (false CAD alarm, or wrong sync word)
  TxDone,
};

class IRadioHal {
 public:
  virtual ~IRadioHal() = default;

  virtual uint64_t nowUs() const = 0;

  // Standby (oscillator kept running) + program frequency, modulation,
  // packet params and sync word. Must be fast: this is the "switch".
  virtual void applyProfile(const LoRaProfile& p) = 0;

  // Start CAD. If rx_on_detect, the chip continues straight into RX on a
  // hit (no MCU round-trip), with the given RX timeout.
  virtual void startCad(const CadParams& cad, bool rx_on_detect,
                        uint32_t rx_timeout_us) = 0;

  // Plain receive. timeout_us = 0 means continuous.
  virtual void startRx(uint32_t timeout_us) = 0;

  virtual void startTx(const uint8_t* data, uint16_t len) = 0;

  // Fetch the packet after RxDone.
  virtual bool readPacket(uint8_t* buf, uint16_t& len, int16_t& rssi_dbm,
                          int8_t& snr_q4) = 0;

  virtual void standby() = 0;

  // One-shot software timer; calls RadioScheduler::onTimer() when it fires.
  // A new call replaces the previous one.
  virtual void armTimer(uint64_t at_us) = 0;
};

}  // namespace dm
