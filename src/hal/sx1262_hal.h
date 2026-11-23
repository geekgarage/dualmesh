// SPDX-License-Identifier: MIT
// DualMesh – direct-register SX1262 driver for ESP-IDF, built for speed:
// every profile switch is a handful of short SPI commands, no RadioLib
// round-trips. NOT YET TESTED ON HARDWARE – see README "Status".
#pragma once
#include <cstdint>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "radio_hal.h"

namespace dm {

class RadioScheduler;

struct Sx1262Pins {
  // LilyGo T-Deck / T-Deck Plus defaults – verify against LilyGo's
  // schematic / utilities.h before first power-up.
  int sck = 40, miso = 38, mosi = 41;
  int cs = 9, rst = 17, busy = 13, dio1 = 45;
  int board_power = 10;  // peripheral power enable on T-Deck
  spi_host_device_t host = SPI2_HOST;
  int spi_hz = 10 * 1000 * 1000;
  bool tcxo = true;           // module uses a TCXO on DIO3 (verify!)
  float tcxo_volt = 1.8f;     // verify for the T-Deck LoRa module
  // TCXO start-up time. Paid on EVERY CAD: a CAD always ends in STDBY_RC,
  // which switches the TCXO off (measured on T-Deck Plus 14 Nov 2026).
#ifdef DM_TCXO_DELAY_US
  uint32_t tcxo_delay_us = DM_TCXO_DELAY_US;
#else
  uint32_t tcxo_delay_us = 1000;  // 250 us testet uden XOSC_START_ERR; 4x margin
#endif
  bool dio2_rf_switch = true;
  int8_t tx_power_dbm = 22;
};

class Sx1262Hal : public IRadioHal {
 public:
  explicit Sx1262Hal(const Sx1262Pins& pins) : pins_(pins) {}

  // Bus must already be initialised (shared with display + SD on T-Deck).
  bool begin();
  void attach(RadioScheduler* s) { sched_ = s; }

  // Call from the radio task when notified (DIO1 ISR or timer).
  void serviceIrq();
  void serviceTimer();
  TaskHandle_t* taskHandle() { return &task_; }
  // TX power in dBm (SX1262: -9..22), e.g. clamped to the region's limit.
  void setTxPower(int8_t dbm);
  int8_t txPower() const { return pins_.tx_power_dbm; }

  // IRadioHal
  uint64_t nowUs() const override { return uint64_t(esp_timer_get_time()); }
  void applyProfile(const LoRaProfile& p) override;
  void startCad(const CadParams& cad, bool rx_on_detect,
                uint32_t rx_timeout_us) override;
  void startRx(uint32_t timeout_us) override;
  void startTx(const uint8_t* data, uint16_t len) override;
  bool readPacket(uint8_t* buf, uint16_t& len, int16_t& rssi_dbm,
                  int8_t& snr_q4) override;
  void standby() override;
  void armTimer(uint64_t at_us) override;

  // Diagnostics for bring-up.
  uint8_t chipStatus();          // GetStatus: bits 6:4 mode, 3:1 cmd status
  int32_t freqErrorHz();         // målt frekvensfejl for den sidst modtagne pakke (Hz)
  // Målt støjgulv (dBm) på en frekvens; 0 = ikke målt endnu. rounds = antal færdige målerunder.
  int16_t noiseFloorDbm(uint32_t freq_hz, uint32_t* rounds = nullptr) const;
  void sampleNoiseIdle();  // kun ét net: støjprøve i RxContinuous (radio-tasken)
  uint16_t deviceErrors();       // GetDeviceErrors (bit 5 = XOSC/TCXO start error)
  void clearDeviceErrors();
  uint16_t syncWordRegister();   // 0x1424 after reset, 0x3444 public LoRa
  uint32_t irqCount() const { return irq_count_; }

  // Timing diagnostics (radio task only; read approximately from loop()).
  struct Diag {
    uint32_t cad_end_rc = 0, cad_end_xosc = 0, cad_end_other = 0;  // chip mode after CadDone
    uint32_t retunes = 0, busy_waits_long = 0;                       // busy > 1 ms
    uint64_t retune_sum_us = 0, busy_sum_us = 0;
    uint32_t retune_max_us = 0, busy_max_us = 0;
    uint32_t retune_win_max_us = 0;   // siden resetWindow() (logvinduet)
    uint64_t retune_win_at_us = 0;    // hvornår (esp_timer) det længste skift sluttede
    uint32_t err_checks = 0, xosc_errs = 0;  // GetDeviceErrors sampled after CAD
    uint16_t err_bits = 0;                   // OR of all error bits seen
    // Modtagefejl (22. nov 2026): header- og CRC-fejl hver for sig, og pakkestatus ved den
    // seneste (RSSI dBm, SNR i kvarte dB, frekvens), så årsagen kan ses i [DIAG].
    uint32_t hdr_errs = 0, crc_errs = 0;
    int16_t err_rssi = 0;
    int8_t err_snr_q4 = 0;
    uint32_t err_freq_hz = 0;
    // Målt frekvensfejl (Hz, senderens frekvens minus vores) for den seneste modtagne pakke og den
    // seneste fejlede (SX1262 FreqError-registret 0x076B, gyldigt efter headeren).
    int32_t fe_ok_hz = 0, fe_err_hz = 0;
    bool fe_ok_valid = false, fe_err_valid = false;
  };
  const Diag& diag() const { return diag_; }
  void resetWindow() { diag_.retune_win_max_us = 0; diag_.retune_win_at_us = 0; }

  // Measure the real profile switch time (for SchedulerConfig::retune_us).
  uint32_t measureRetuneUs(const LoRaProfile& a, const LoRaProfile& b, int n);

 private:
  void serviceIrqOnce();
  void waitBusy();
  void cmd(uint8_t op, const uint8_t* args, size_t n);
  void cmdRead(uint8_t op, uint8_t* out, size_t n);  // status skipped
  void writeReg(uint16_t addr, const uint8_t* data, size_t n);
  uint8_t readReg(uint16_t addr);
  void setPacketParams(uint16_t preamble, uint8_t payload_len);
  void applyProfileNoDiag(const LoRaProfile& p);
  void noiseSample(uint32_t freq, int16_t dbm);

  // Støjgulv: RSSI målt lige efter falske CAD-hit (radio-tasken skriver, loop læser floor_dbm).
  struct NoiseFloor {
    uint32_t freq_hz = 0;
    int32_t sum = 0;
    uint16_t n = 0, rejects = 0;
    volatile int16_t floor_dbm = 0;
    volatile uint32_t rounds = 0;
  };
  NoiseFloor noise_[4];
  bool noise_pending_ = false;
  int16_t noise_pending_dbm_ = 0;
  uint32_t noise_pending_freq_ = 0;

  static void IRAM_ATTR dio1Isr(void* arg);
  static void timerCb(void* arg);

  Sx1262Pins pins_;
  spi_device_handle_t spi_ = nullptr;
  esp_timer_handle_t timer_ = nullptr;
  TaskHandle_t task_ = nullptr;
  RadioScheduler* sched_ = nullptr;
  LoRaProfile cur_{};
  bool rx_mode_ = false;
  bool rx_cont_ = false;  // RxContinuous (kun ét net)
  bool rx_busy_ = false;  // præambel/header set, pakken er ikke færdig
  volatile uint32_t irq_count_ = 0;
  Diag diag_{};
};

}  // namespace dm
