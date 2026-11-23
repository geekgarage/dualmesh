// SPDX-License-Identifier: MIT
// DualMesh – MeshCores radiogrænseflade (mesh::Radio) oven på vores scheduler.
//
// MeshCore-biblioteket (Dispatcher) poller radioen fra loop()-tasken, mens
// scheduleren og SX1262-driveren kører i radio-tasken. Al trafik mellem dem går
// gennem FreeRTOS-køer, så de to tasks aldrig rører de samme data.
//   modtag:  radio-task -> pushRx() -> kø -> recvRaw()        (loop-task)
//   send:    loop-task  -> startSendRaw() -> kø -> popTx()     (radio-task)
//            radio-task -> onTxDone() -> isSendComplete()      (loop-task)
#pragma once
#include <Dispatcher.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lora_profile.h"
#include "radio_scheduler.h"

namespace dm {

struct McRadioFrame {
  uint8_t len;
  int16_t rssi_dbm;
  int8_t snr_q4;
  uint8_t data[255];
};

class McRadio : public mesh::Radio {
 public:
  // notify_task/bit: radio-tasken vækkes med denne bit, når der er noget at sende.
  bool begin(const LoRaProfile& profile, TaskHandle_t notify_task = nullptr,
             uint32_t notify_bit = 0);
  static constexpr uint32_t kTxNotifyBit = 0x40000000u;
  void setTxEnabled(bool on) { tx_enabled_ = on; }
  bool txEnabled() const { return tx_enabled_; }

  // --- radio-task side ---
  bool pushRx(const RxPacket& p);         // false = kø fuld (pakken tabes)
  bool popTx(McRadioFrame& out);          // næste pakke MeshCore vil sende
  void onTxDone() { tx_done_ = true; }
  void setBusy(bool receiving) { busy_ = receiving; }
  uint32_t rxDropped() const { return rx_dropped_; }

  // --- mesh::Radio (loop-task) ---
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  float packetScore(float snr, int packet_len) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override { return tx_done_; }
  void onSendFinished() override { tx_pending_ = false; tx_done_ = false; }
  bool isInRecvMode() const override { return !tx_pending_; }
  // Optaget: radioen modtager/sender (på et af nettene), eller en pakke venter.
  bool isReceiving() override { return busy_ || tx_pending_; }
  float getLastRSSI() const override { return last_rssi_; }
  float getLastSNR() const override { return last_snr_; }

 private:
  LoRaProfile profile_{};
  QueueHandle_t rx_q_ = nullptr, tx_q_ = nullptr;
  TaskHandle_t notify_task_ = nullptr;
  uint32_t notify_bit_ = 0;
  volatile bool tx_enabled_ = false, tx_pending_ = false, tx_done_ = false, busy_ = false;
  volatile uint32_t rx_dropped_ = 0;
  float last_rssi_ = 0, last_snr_ = 0;
};

}  // namespace dm
