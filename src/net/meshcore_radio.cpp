// SPDX-License-Identifier: MIT
#include "net/meshcore_radio.h"

#include <cstring>

#include "phy_math.h"

namespace dm {

bool McRadio::begin(const LoRaProfile& profile, TaskHandle_t notify_task, uint32_t notify_bit) {
  profile_ = profile;
  notify_task_ = notify_task;
  notify_bit_ = notify_bit;
  if (!rx_q_) rx_q_ = xQueueCreate(8, sizeof(McRadioFrame));
  if (!tx_q_) tx_q_ = xQueueCreate(2, sizeof(McRadioFrame));
  return rx_q_ && tx_q_;
}

bool McRadio::pushRx(const RxPacket& p) {
  if (!rx_q_ || p.len == 0 || p.len > sizeof(McRadioFrame::data)) return false;
  McRadioFrame f;
  f.len = uint8_t(p.len);
  f.rssi_dbm = p.rssi_dbm;
  f.snr_q4 = p.snr_q4;
  std::memcpy(f.data, p.data, p.len);
  if (xQueueSend(rx_q_, &f, 0) != pdTRUE) {  // aldrig blokere radio-tasken
    rx_dropped_ = rx_dropped_ + 1;
    return false;
  }
  return true;
}

bool McRadio::popTx(McRadioFrame& out) {
  return tx_q_ && xQueueReceive(tx_q_, &out, 0) == pdTRUE;
}

int McRadio::recvRaw(uint8_t* bytes, int sz) {
  McRadioFrame f;
  if (!rx_q_ || xQueueReceive(rx_q_, &f, 0) != pdTRUE) return 0;
  if (f.len > sz) return 0;
  std::memcpy(bytes, f.data, f.len);
  last_rssi_ = f.rssi_dbm;
  last_snr_ = f.snr_q4 / 4.0f;
  return f.len;
}

uint32_t McRadio::getEstAirtimeFor(int len_bytes) {
  return phy::airtimeUs(profile_, uint16_t(len_bytes)) / 1000 + 1;
}

// Samme vurdering som MeshCores RadioLibWrapper (MIT): hvor sikkert en pakke med
// denne SNR og længde kan modtages; bruges til at vælge videresendelsesforsinkelse.
float McRadio::packetScore(float snr, int packet_len) {
  static const float kSnrThreshold[] = {-7.5f, -10.f, -12.5f, -15.f, -17.5f, -20.f};  // SF7..SF12
  const int sf = profile_.sf;
  if (sf < 7 || sf > 12) return 0.0f;
  const float th = kSnrThreshold[sf - 7];
  if (snr < th) return 0.0f;
  const float s = ((snr - th) / 10.0f) * (1.0f - packet_len / 256.0f);
  return s < 0 ? 0.0f : (s > 1 ? 1.0f : s);
}

bool McRadio::startSendRaw(const uint8_t* bytes, int len) {
  if (!tx_enabled_ || !tx_q_ || len <= 0 || len > 255) return false;
  McRadioFrame f;
  f.len = uint8_t(len);
  f.rssi_dbm = 0;
  f.snr_q4 = 0;
  std::memcpy(f.data, bytes, len);
  tx_done_ = false;
  if (xQueueSend(tx_q_, &f, 0) != pdTRUE) return false;
  tx_pending_ = true;
  if (notify_task_) xTaskNotify(notify_task_, notify_bit_, eSetBits);
  return true;
}

}  // namespace dm
