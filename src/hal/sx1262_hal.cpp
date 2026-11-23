// SPDX-License-Identifier: MIT
// Command set: Semtech SX1261/2 datasheet rev 1.2, chapter 13.
#include "sx1262_hal.h"

#include <cstring>

#include "radio_scheduler.h"

namespace dm {

namespace op {
constexpr uint8_t SetStandby = 0x80, SetPacketType = 0x8A, SetRfFreq = 0x86,
                  SetModParams = 0x8B, SetPktParams = 0x8C, SetCadParams = 0x88,
                  SetCad = 0xC5, SetRx = 0x82, SetTx = 0x83, SetBufBase = 0x8F,
                  WriteBuf = 0x0E, ReadBuf = 0x1E, WriteReg = 0x0D, ReadReg = 0x1D,
                  SetDioIrq = 0x08, GetIrq = 0x12, ClearIrq = 0x02,
                  GetRxBufStatus = 0x13, GetPktStatus = 0x14, SetTcxo = 0x97,
                  SetDio2RfSw = 0x9D, Calibrate = 0x89, CalibrateImage = 0x98,
                  SetRegulator = 0x96, SetPaConfig = 0x95, SetTxParams = 0x8E,
                  StopTimerOnPre = 0x9F, SetFallback = 0x93, GetRssiInst = 0x15;
}
namespace irq {
constexpr uint16_t TxDone = 0x0001, RxDone = 0x0002, PreambleDetected = 0x0004,
                   HeaderValid = 0x0010, HeaderErr = 0x0020,
                   CrcErr = 0x0040, CadDone = 0x0080, CadDetected = 0x0100,
                   Timeout = 0x0200;
constexpr uint16_t All = TxDone | RxDone | PreambleDetected | HeaderValid | HeaderErr |
                         CrcErr | CadDone | CadDetected | Timeout;
}
constexpr uint16_t kRegSyncWord = 0x0740, kRegIqPolarity = 0x0736,
                   kRegOcp = 0x08E7;

// Nearest SX126x bandwidth setting (7.8 kHz = 7812.5 Hz, 15.6 kHz = 15625 Hz, ...),
// so both rounded (15600) and exact (15625) values select the right code.
static uint8_t bwCode(uint32_t bw) {
  static const struct { uint32_t hz; uint8_t code; } k[] = {
      {7813, 0x00},  {10417, 0x08}, {15625, 0x01},  {20833, 0x09},  {31250, 0x02},
      {41667, 0x0A}, {62500, 0x03}, {125000, 0x04}, {250000, 0x05}, {500000, 0x06}};
  uint8_t best = 0x06;
  uint32_t best_d = UINT32_MAX;
  for (const auto& e : k) {
    const uint32_t d = bw > e.hz ? bw - e.hz : e.hz - bw;
    if (d < best_d) { best_d = d; best = e.code; }
  }
  return best;
}
static uint8_t cadSymCode(uint8_t n) {
  switch (n) { case 1: return 0; case 2: return 1; case 4: return 2; case 8: return 3; default: return 4; }
}
static uint32_t toTicks(uint32_t us) {  // 15.625 us units
  return uint32_t((uint64_t(us) * 64) / 1000);
}

// ---------------------------------------------------------------- low level
void Sx1262Hal::waitBusy() {
  // BUSY is normally low within a few us; bounded spin for safety.
  if (!gpio_get_level(gpio_num_t(pins_.busy))) return;
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < 100000 && gpio_get_level(gpio_num_t(pins_.busy)); ++i) {
  }
  const uint32_t dt = uint32_t(esp_timer_get_time() - t0);
  diag_.busy_sum_us += dt;
  if (dt > diag_.busy_max_us) diag_.busy_max_us = dt;
  if (dt > 1000) diag_.busy_waits_long++;
}

void Sx1262Hal::cmd(uint8_t o, const uint8_t* args, size_t n) {
  uint8_t tx[260];
  tx[0] = o;
  if (n) std::memcpy(tx + 1, args, n);
  spi_transaction_t t{};
  t.length = (n + 1) * 8;
  t.tx_buffer = tx;
  waitBusy();
  spi_device_polling_transmit(spi_, &t);
}

void Sx1262Hal::cmdRead(uint8_t o, uint8_t* out, size_t n) {
  uint8_t tx[260] = {o};
  uint8_t rx[260] = {};
  spi_transaction_t t{};
  t.length = (n + 2) * 8;  // opcode + status + data
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  waitBusy();
  spi_device_polling_transmit(spi_, &t);
  std::memcpy(out, rx + 2, n);
}

void Sx1262Hal::writeReg(uint16_t a, const uint8_t* d, size_t n) {
  uint8_t args[16] = {uint8_t(a >> 8), uint8_t(a)};
  std::memcpy(args + 2, d, n);
  cmd(op::WriteReg, args, n + 2);
}

uint8_t Sx1262Hal::readReg(uint16_t a) {
  uint8_t tx[5] = {op::ReadReg, uint8_t(a >> 8), uint8_t(a), 0, 0};
  uint8_t rx[5] = {};
  spi_transaction_t t{};
  t.length = 5 * 8;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  waitBusy();
  spi_device_polling_transmit(spi_, &t);
  return rx[4];
}

void Sx1262Hal::setPacketParams(uint16_t pre, uint8_t len) {
  const uint8_t a[6] = {uint8_t(pre >> 8), uint8_t(pre),
                        uint8_t(cur_.implicit_header ? 1 : 0), len,
                        uint8_t(cur_.crc_on ? 1 : 0), 0 /* standard IQ */};
  cmd(op::SetPktParams, a, 6);
}

// ---------------------------------------------------------------- init
bool Sx1262Hal::begin() {
  for (int p : {pins_.busy, pins_.rst, pins_.dio1}) gpio_reset_pin(gpio_num_t(p));
  if (pins_.board_power >= 0) {
    gpio_set_direction(gpio_num_t(pins_.board_power), GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_num_t(pins_.board_power), 1);
  }
  gpio_set_direction(gpio_num_t(pins_.busy), GPIO_MODE_INPUT);
  gpio_set_direction(gpio_num_t(pins_.rst), GPIO_MODE_OUTPUT);
  gpio_set_level(gpio_num_t(pins_.rst), 0);
  vTaskDelay(pdMS_TO_TICKS(2));
  gpio_set_level(gpio_num_t(pins_.rst), 1);
  vTaskDelay(pdMS_TO_TICKS(10));

  spi_device_interface_config_t dev{};
  dev.clock_speed_hz = pins_.spi_hz;
  dev.mode = 0;
  dev.spics_io_num = pins_.cs;
  dev.queue_size = 1;
  if (spi_bus_add_device(pins_.host, &dev, &spi_) != ESP_OK) return false;

  uint8_t a[8];
  a[0] = 0x00; cmd(op::SetStandby, a, 1);              // STDBY_RC
  if (pins_.tcxo) {
    const uint8_t code = pins_.tcxo_volt >= 3.3f ? 0x07 : pins_.tcxo_volt >= 3.0f ? 0x06
                       : pins_.tcxo_volt >= 2.7f ? 0x05 : pins_.tcxo_volt >= 2.4f ? 0x04
                       : pins_.tcxo_volt >= 2.2f ? 0x03 : pins_.tcxo_volt >= 1.8f ? 0x02
                       : pins_.tcxo_volt >= 1.7f ? 0x01 : 0x00;
    const uint32_t d = toTicks(pins_.tcxo_delay_us);
    const uint8_t t[4] = {code, uint8_t(d >> 16), uint8_t(d >> 8), uint8_t(d)};
    cmd(op::SetTcxo, t, 4);
  }
  a[0] = 0x7F; cmd(op::Calibrate, a, 1);
  vTaskDelay(pdMS_TO_TICKS(5));
  a[0] = 0x01; cmd(op::SetRegulator, a, 1);            // DC-DC
  if (pins_.dio2_rf_switch) { a[0] = 0x01; cmd(op::SetDio2RfSw, a, 1); }
  a[0] = 0x01; cmd(op::SetPacketType, a, 1);           // LoRa
  a[0] = 0xD7; a[1] = 0xDB; cmd(op::CalibrateImage, a, 2);  // 863-870 MHz
  const uint8_t pa[4] = {0x04, 0x07, 0x00, 0x01};      // SX1262 +22 dBm table
  cmd(op::SetPaConfig, pa, 4);
  a[0] = uint8_t(pins_.tx_power_dbm); a[1] = 0x04; cmd(op::SetTxParams, a, 2);
  const uint8_t ocp = 0x38; writeReg(kRegOcp, &ocp, 1);  // 140 mA
  a[0] = 0x00; a[1] = 0x00; cmd(op::SetBufBase, a, 2);
  a[0] = 0x00; cmd(op::StopTimerOnPre, a, 1);          // stop RX timer on sync/header
  a[0] = 0x30; cmd(op::SetFallback, a, 1);             // fall back to STDBY_XOSC
  const uint8_t dio[8] = {uint8_t(irq::All >> 8), uint8_t(irq::All),
                          uint8_t(irq::All >> 8), uint8_t(irq::All), 0, 0, 0, 0};
  cmd(op::SetDioIrq, dio, 8);
  a[0] = 0x01; cmd(op::SetStandby, a, 1);              // STDBY_XOSC: TCXO stays on

  gpio_set_direction(gpio_num_t(pins_.dio1), GPIO_MODE_INPUT);
  gpio_set_intr_type(gpio_num_t(pins_.dio1), GPIO_INTR_POSEDGE);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(gpio_num_t(pins_.dio1), dio1Isr, this);

  esp_timer_create_args_t ta{};
  ta.callback = timerCb;
  ta.arg = this;
  ta.name = "dm_sched";
  esp_timer_create(&ta, &timer_);
  return true;
}

// ---------------------------------------------------------------- fast path
void Sx1262Hal::applyProfile(const LoRaProfile& p) {
  const int64_t t0 = esp_timer_get_time();
  applyProfileNoDiag(p);
  waitBusy();  // include the chip's own settle time in the measurement
  const uint32_t dt = uint32_t(esp_timer_get_time() - t0);
  diag_.retunes++;
  diag_.retune_sum_us += dt;
  if (dt > diag_.retune_max_us) diag_.retune_max_us = dt;
  if (dt > diag_.retune_win_max_us) {
    diag_.retune_win_max_us = dt;
    diag_.retune_win_at_us = uint64_t(esp_timer_get_time());
  }
}

void Sx1262Hal::applyProfileNoDiag(const LoRaProfile& p) {
  cur_ = p;
  uint8_t a[4];
  a[0] = 0x01; cmd(op::SetStandby, a, 1);  // STDBY_XOSC (no TCXO restart)
  const uint32_t f = uint32_t((uint64_t(p.freq_hz) << 25) / 32000000ull);
  const uint8_t fr[4] = {uint8_t(f >> 24), uint8_t(f >> 16), uint8_t(f >> 8), uint8_t(f)};
  cmd(op::SetRfFreq, fr, 4);
  const uint8_t mp[4] = {p.sf, bwCode(p.bw_hz), uint8_t(p.cr - 4),
                         uint8_t(phy::ldro(p) ? 1 : 0)};
  cmd(op::SetModParams, mp, 4);
  setPacketParams(p.preamble_syms, 0xFF);
  // Errata 15.4: standard IQ needs bit 2 of 0x0736 set.
  uint8_t iq = readReg(kRegIqPolarity) | 0x04;
  writeReg(kRegIqPolarity, &iq, 1);
  // 1-byte sync word -> SX126x 2-byte register format.
  const uint8_t sw[2] = {uint8_t((p.sync_word & 0xF0) | 0x04),
                         uint8_t(((p.sync_word & 0x0F) << 4) | 0x04)};
  writeReg(kRegSyncWord, sw, 2);
  // Sendeeffekt pr. net (kun når den skifter, så profilskift ikke bliver dyrere).
  if (p.tx_power_dbm != pins_.tx_power_dbm) setTxPower(p.tx_power_dbm);
}

void Sx1262Hal::startCad(const CadParams& c, bool rx_on_detect, uint32_t to_us) {
  const uint8_t clr[2] = {0xFF, 0xFF};
  cmd(op::ClearIrq, clr, 2);
  const uint32_t t = toTicks(to_us);
  const uint8_t a[7] = {cadSymCode(c.symbols), c.det_peak, c.det_min,
                        uint8_t(rx_on_detect ? 0x01 : 0x00),  // CAD_RX / CAD_ONLY
                        uint8_t(t >> 16), uint8_t(t >> 8), uint8_t(t)};
  cmd(op::SetCadParams, a, 7);
  cmd(op::SetCad, nullptr, 0);
  rx_mode_ = false;
  rx_busy_ = false;
  // Forrige CAD-hit uden præambel (falsk alarm) = en støjprøve.
  if (noise_pending_) noiseSample(noise_pending_freq_, noise_pending_dbm_);
  noise_pending_ = false;
}

// Støjgulv pr. frekvens (som MeshCores RadioLibWrapper): kun prøver under gulv + 14 dB tæller,
// gennemsnit af 32 prøver, aldrig under -120 dBm. Afvises 64 i træk (gulvet sidder fast for lavt,
// fx efter et skift i omgivelserne), starter målingen forfra.
void Sx1262Hal::noiseSample(uint32_t freq, int16_t dbm) {
  NoiseFloor* nf = nullptr;
  for (auto& n : noise_) {
    if (n.freq_hz == freq) { nf = &n; break; }
  }
  if (!nf) {
    for (auto& n : noise_) {
      if (n.freq_hz == 0) { nf = &n; nf->freq_hz = freq; break; }
    }
  }
  if (!nf) return;
  if (nf->floor_dbm != 0 && dbm >= nf->floor_dbm + 14) {
    if (++nf->rejects >= 64) { nf->floor_dbm = 0; nf->n = 0; nf->sum = 0; nf->rejects = 0; }
    return;
  }
  nf->rejects = 0;
  nf->sum += dbm;
  if (++nf->n >= 32) {
    int32_t f = nf->sum / int32_t(nf->n);
    if (f < -120) f = -120;
    nf->floor_dbm = int16_t(f);
    nf->n = 0;
    nf->sum = 0;
    nf->rounds++;
  }
}

int16_t Sx1262Hal::noiseFloorDbm(uint32_t freq_hz, uint32_t* rounds) const {
  for (const auto& n : noise_) {
    if (n.freq_hz == freq_hz) {
      if (rounds) *rounds = n.rounds;
      return n.floor_dbm;
    }
  }
  if (rounds) *rounds = 0;
  return 0;
}

void Sx1262Hal::startRx(uint32_t to_us) {
  const uint8_t clr[2] = {0xFF, 0xFF};
  cmd(op::ClearIrq, clr, 2);
  const uint32_t t = to_us ? toTicks(to_us) : 0xFFFFFF;  // 0xFFFFFF = continuous
  const uint8_t a[3] = {uint8_t(t >> 16), uint8_t(t >> 8), uint8_t(t)};
  cmd(op::SetRx, a, 3);
  rx_mode_ = true;
  rx_cont_ = to_us == 0;
  rx_busy_ = false;
}

// Kun ét net (RxContinuous, ingen CAD): tag en støjprøve, når der ikke er en pakke undervejs.
// Kaldes fra radio-tasken (fx hvert 250. ms).
void Sx1262Hal::sampleNoiseIdle() {
  if (!rx_mode_ || !rx_cont_ || rx_busy_) return;
  uint8_t r = 0;
  cmdRead(op::GetRssiInst, &r, 1);
  noiseSample(cur_.freq_hz, -int16_t(r) / 2);
}

void Sx1262Hal::startTx(const uint8_t* data, uint16_t len) {
  const uint8_t clr[2] = {0xFF, 0xFF};
  cmd(op::ClearIrq, clr, 2);
  uint8_t buf[257];
  buf[0] = 0x00;  // buffer offset
  std::memcpy(buf + 1, data, len);
  cmd(op::WriteBuf, buf, len + 1);
  setPacketParams(cur_.preamble_syms, uint8_t(len));
  const uint8_t a[3] = {0, 0, 0};  // no TX timeout
  cmd(op::SetTx, a, 3);
  rx_mode_ = false;
}

bool Sx1262Hal::readPacket(uint8_t* buf, uint16_t& len, int16_t& rssi,
                           int8_t& snr) {
  uint8_t st[2];
  cmdRead(op::GetRxBufStatus, st, 2);
  len = st[0];
  const uint8_t start = st[1];
  uint8_t tx[3 + 255] = {op::ReadBuf, start, 0};
  uint8_t rx[3 + 255] = {};
  spi_transaction_t t{};
  t.length = (3 + len) * 8;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  waitBusy();
  spi_device_polling_transmit(spi_, &t);
  std::memcpy(buf, rx + 3, len);
  uint8_t ps[3];
  cmdRead(op::GetPktStatus, ps, 3);
  rssi = -int16_t(ps[0]) / 2;
  snr = int8_t(ps[1]);
  return len > 0;
}

void Sx1262Hal::setTxPower(int8_t dbm) {
  if (dbm > 22) dbm = 22;
  if (dbm < -9) dbm = -9;
  pins_.tx_power_dbm = dbm;
  const uint8_t a[2] = {uint8_t(dbm), 0x04};  // ramp 200 us
  cmd(op::SetTxParams, a, 2);
}

void Sx1262Hal::standby() {
  const uint8_t a = 0x01;
  cmd(op::SetStandby, &a, 1);
  rx_mode_ = false;
}

void Sx1262Hal::armTimer(uint64_t at_us) {
  esp_timer_stop(timer_);
  const uint64_t now = nowUs();
  esp_timer_start_once(timer_, at_us > now ? at_us - now : 1);
}

// ---------------------------------------------------------------- events
void IRAM_ATTR Sx1262Hal::dio1Isr(void* arg) {
  auto* self = static_cast<Sx1262Hal*>(arg);
  self->irq_count_++;
  BaseType_t woken = pdFALSE;
  if (self->task_) xTaskNotifyFromISR(self->task_, 0x1u, eSetBits, &woken);
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

void Sx1262Hal::timerCb(void* arg) {
  auto* self = static_cast<Sx1262Hal*>(arg);
  // Runs in the esp_timer task; hand over to the radio task.
  if (self->task_) xTaskNotify(self->task_, 0x80000000u, eSetBits);
}

void Sx1262Hal::serviceTimer() {
  if (sched_) sched_->onTimer();
}

void Sx1262Hal::serviceIrq() {
  // DIO1 is edge-triggered: if a new flag arrived while we were handling the
  // previous one, the line stays high and no new edge comes. Loop until low.
  for (int i = 0; i < 4; ++i) {
    serviceIrqOnce();
    if (!gpio_get_level(gpio_num_t(pins_.dio1))) break;
  }
}

void Sx1262Hal::serviceIrqOnce() {
  uint8_t s[2];
  cmdRead(op::GetIrq, s, 2);
  const uint16_t f = uint16_t(s[0]) << 8 | s[1];
  const uint8_t clr[2] = {s[0], s[1]};
  cmd(op::ClearIrq, clr, 2);
  if (!sched_ || !f) return;

  if (f & irq::TxDone) { sched_->onRadioEvent(RadioEvt::TxDone); return; }
  if (f & irq::CadDone) {
    if (!(f & irq::CadDetected)) {
      const uint8_t mode = (chipStatus() >> 4) & 0x07;  // 2 = STDBY_RC, 3 = STDBY_XOSC
      if (mode == 2) diag_.cad_end_rc++;
      else if (mode == 3) diag_.cad_end_xosc++;
      else diag_.cad_end_other++;
      if ((diag_.cad_end_rc & 7) == 0) {  // sample every 8th CAD: did the TCXO start in time?
        const uint16_t e = deviceErrors();
        diag_.err_checks++;
        diag_.err_bits |= e;
        if (e & 0x0020) diag_.xosc_errs++;
        if (e) clearDeviceErrors();
      }
      sched_->onRadioEvent(RadioEvt::CadClear);
      return;
    }
    // CAD_RX: chippen lytter nu. Mål RSSI med det samme; bliver det ikke til en præambel, var hittet
    // støj, og prøven bruges til støjgulvet (se startCad).
    if (!(f & (irq::PreambleDetected | irq::HeaderValid | irq::RxDone))) {
      uint8_t r = 0;
      cmdRead(op::GetRssiInst, &r, 1);
      noise_pending_dbm_ = -int16_t(r) / 2;
      noise_pending_freq_ = cur_.freq_hz;
      noise_pending_ = true;
    }
    sched_->onRadioEvent(RadioEvt::CadDetected);
    // RX may already have progressed if the task was slow: fall through.
  }
  if (f & (irq::PreambleDetected | irq::HeaderValid | irq::RxDone | irq::HeaderErr | irq::CrcErr))
    noise_pending_ = false;  // en rigtig pakke: ikke støj
  if (f & (irq::PreambleDetected | irq::HeaderValid)) rx_busy_ = true;
  if (f & (irq::RxDone | irq::HeaderErr | irq::CrcErr | irq::Timeout)) rx_busy_ = false;
  if (f & irq::PreambleDetected) sched_->onRadioEvent(RadioEvt::PreambleDetected);
  if (f & irq::HeaderValid) sched_->onRadioEvent(RadioEvt::HeaderValid);
  if (f & (irq::HeaderErr | irq::CrcErr)) {
    if (f & irq::HeaderErr) diag_.hdr_errs++;
    else diag_.crc_errs++;
    uint8_t ps[3];
    cmdRead(op::GetPktStatus, ps, 3);
    diag_.err_rssi = -int16_t(ps[0]) / 2;
    diag_.err_snr_q4 = int8_t(ps[1]);
    diag_.err_freq_hz = cur_.freq_hz;
    if (f & irq::CrcErr) {
      diag_.fe_err_hz = freqErrorHz();
      diag_.fe_err_valid = true;
    }
    sched_->onRadioEvent(RadioEvt::RxError);
  }
  else if (f & irq::RxDone) {
    diag_.fe_ok_hz = freqErrorHz();
    diag_.fe_ok_valid = true;
    sched_->onRadioEvent(RadioEvt::RxDone);
  }
  else if (f & irq::Timeout) sched_->onRadioEvent(RadioEvt::RxTimeout);
}

// Frekvensfejl for den sidst modtagne LoRa-pakke: register 0x076B-0x076D, 20 bit med fortegn.
// Omregning efter SX1262's estimator (som RadioLib, MIT): fejl = 1,55 * værdi / (1600 / BW_kHz) Hz.
int32_t Sx1262Hal::freqErrorHz() {
  uint32_t v = uint32_t(readReg(0x076B)) << 16 | uint32_t(readReg(0x076C)) << 8 | readReg(0x076D);
  v &= 0x0FFFFF;
  if (v & 0x80000) v |= 0xFFF00000u;
  const float bw_khz = cur_.bw_hz / 1000.0f;
  return int32_t(1.55f * float(int32_t(v)) / (1600.0f / bw_khz));
}

uint8_t Sx1262Hal::chipStatus() {
  uint8_t tx[2] = {0xC0, 0x00}, rx[2] = {};
  spi_transaction_t t{};
  t.length = 16;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  waitBusy();
  spi_device_polling_transmit(spi_, &t);
  return rx[1];
}

uint16_t Sx1262Hal::deviceErrors() {
  uint8_t e[2];
  cmdRead(0x17, e, 2);
  return uint16_t(e[0]) << 8 | e[1];
}

void Sx1262Hal::clearDeviceErrors() {
  const uint8_t a[2] = {0, 0};
  cmd(0x07, a, 2);
}

uint16_t Sx1262Hal::syncWordRegister() {
  return uint16_t(readReg(kRegSyncWord)) << 8 | readReg(kRegSyncWord + 1);
}

uint32_t Sx1262Hal::measureRetuneUs(const LoRaProfile& a, const LoRaProfile& b, int n) {
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < n; ++i) applyProfile(i & 1 ? b : a);
  const uint32_t avg = uint32_t((esp_timer_get_time() - t0) / n);
  diag_ = Diag{};  // the operating statistics start after this test
  return avg;
}

}  // namespace dm
