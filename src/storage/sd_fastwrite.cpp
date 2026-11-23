// SPDX-License-Identifier: MIT
#include "storage/sd_fastwrite.h"

#include <cstring>

#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/gpio.h"
#include "driver/sdmmc_defs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "storage/sd_log.h"

namespace dm {
namespace sdfw {

namespace {

sdmmc_card_t* g_card = nullptr;
spi_device_handle_t g_dev = nullptr;
int g_cs = -1;
int g_pdrv = -1;
// Statistik.
uint32_t g_max_busy_ms = 0, g_sectors = 0, g_errors = 0;

// SPI-buffere i intern, DMA-egnet RAM (FatFs' buffer kan ligge i PSRAM).
alignas(4) uint8_t g_tx[520];
alignas(4) uint8_t g_rx[16];

uint8_t crc7(const uint8_t* d, int n) {
  uint8_t crc = 0;
  for (int i = 0; i < n; ++i) {
    uint8_t b = d[i];
    for (int k = 0; k < 8; ++k) {
      crc <<= 1;
      if ((b ^ crc) & 0x80) crc ^= 0x09;
      b <<= 1;
    }
  }
  return crc & 0x7F;
}

uint16_t crc16(const uint8_t* d, int n) {  // CRC-16-CCITT (XMODEM), som SD-kort bruger
  uint16_t crc = 0;
  for (int i = 0; i < n; ++i) {
    crc ^= uint16_t(d[i]) << 8;
    for (int k = 0; k < 8; ++k) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}

// Én SPI-overførsel (bussen skal være taget). rx må være nullptr.
void xfer(const uint8_t* tx, uint8_t* rx, size_t n) {
  spi_transaction_t t{};
  t.length = n * 8;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  spi_device_polling_transmit(g_dev, &t);
}

uint8_t xferByte(uint8_t b = 0xFF) {
  alignas(4) static uint8_t tx[4];
  tx[0] = b;
  xfer(tx, g_rx, 1);
  return g_rx[0];
}

void csLow() { gpio_set_level(gpio_num_t(g_cs), 0); }
void csHigh() {
  gpio_set_level(gpio_num_t(g_cs), 1);
  xferByte();  // 8 ekstra clocks, så kortet slipper MISO
}

// Vent til kortet ikke er optaget (MISO = 0xFF). Bussen holdes kun i få µs ad gangen.
bool waitReady(uint32_t timeout_ms) {
  const int64_t t0 = esp_timer_get_time();
  for (;;) {
    spi_device_acquire_bus(g_dev, portMAX_DELAY);
    csLow();
    const uint8_t b = xferByte();
    csHigh();
    spi_device_release_bus(g_dev);
    if (b == 0xFF) {
      const uint32_t ms = uint32_t((esp_timer_get_time() - t0) / 1000);
      if (ms > g_max_busy_ms) g_max_busy_ms = ms;
      return true;
    }
    if (esp_timer_get_time() - t0 > int64_t(timeout_ms) * 1000) return false;
    vTaskDelay(1);  // radioen og skærmen kommer til
  }
}

// Skriv én sektor (CMD24). Kortet er bagefter optaget; det ventes der på før næste kommando.
bool writeSector(const uint8_t* data, uint32_t sector) {
  if (!waitReady(1000)) return false;
  const uint32_t arg = (g_card->ocr & SD_OCR_SDHC_CAP) ? sector : sector * 512;
  uint8_t cmd[7];
  cmd[0] = 0xFF;  // en tom byte først
  cmd[1] = 0x40 | 24;
  cmd[2] = uint8_t(arg >> 24);
  cmd[3] = uint8_t(arg >> 16);
  cmd[4] = uint8_t(arg >> 8);
  cmd[5] = uint8_t(arg);
  cmd[6] = uint8_t((crc7(cmd + 1, 5) << 1) | 1);
  std::memcpy(g_tx, cmd, sizeof(cmd));
  spi_device_acquire_bus(g_dev, portMAX_DELAY);
  csLow();
  xfer(g_tx, nullptr, sizeof(cmd));
  uint8_t r1 = 0xFF;
  for (int i = 0; i < 10 && (r1 & 0x80); ++i) r1 = xferByte();
  bool ok = r1 == 0;
  if (ok) {
    // Pause (Nwr), start-token, data og CRC16.
    g_tx[0] = 0xFF;
    g_tx[1] = 0xFE;
    std::memcpy(g_tx + 2, data, 512);
    const uint16_t crc = crc16(data, 512);
    g_tx[514] = uint8_t(crc >> 8);
    g_tx[515] = uint8_t(crc);
    xfer(g_tx, nullptr, 516);
    uint8_t resp = 0xFF;
    for (int i = 0; i < 10 && resp == 0xFF; ++i) resp = xferByte();
    ok = (resp & 0x1F) == 0x05;  // data accepteret
    if (!ok) DLOG.printf("[SD] skrivning afvist: sektor %lu, svar 0x%02X\n", (unsigned long)sector, resp);
  } else {
    DLOG.printf("[SD] CMD24 afvist: sektor %lu, R1 0x%02X\n", (unsigned long)sector, r1);
  }
  csHigh();
  spi_device_release_bus(g_dev);
  if (ok) ++g_sectors;
  else ++g_errors;
  return ok;
}

DSTATUS fwInit(BYTE) { return 0; }
DSTATUS fwStatus(BYTE) { return 0; }

DRESULT fwRead(BYTE, BYTE* buff, uint32_t sector, unsigned count) {
  if (!waitReady(1000)) return RES_ERROR;
  return sdmmc_read_sectors(g_card, buff, sector, count) == ESP_OK ? RES_OK : RES_ERROR;
}

DRESULT fwWrite(BYTE, const BYTE* buff, uint32_t sector, unsigned count) {
  for (unsigned i = 0; i < count; ++i)
    if (!writeSector(buff + i * 512, sector + i)) return RES_ERROR;
  return RES_OK;
}

DRESULT fwIoctl(BYTE, BYTE cmd, void* buff) {
  switch (cmd) {
    case CTRL_SYNC:
      return waitReady(2000) ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
      *static_cast<DWORD*>(buff) = DWORD(g_card->csd.capacity);
      return RES_OK;
    case GET_SECTOR_SIZE:
      *static_cast<uint16_t*>(buff) = uint16_t(g_card->csd.sector_size);
      return RES_OK;
    case GET_BLOCK_SIZE:
      *static_cast<DWORD*>(buff) = 1;
      return RES_OK;
    default:
      return RES_ERROR;
  }
}

const ff_diskio_impl_t kImpl = {fwInit, fwStatus, fwRead, fwWrite, fwIoctl};

}  // namespace

bool install(void* card, spi_host_device_t host, int cs_pin, int khz) {
  uninstall();
  g_card = static_cast<sdmmc_card_t*>(card);
  if (!g_card || g_card->csd.sector_size != 512) return false;
  const BYTE pdrv = ff_diskio_get_pdrv_card(g_card);
  if (pdrv == 0xFF) return false;
  spi_device_interface_config_t d{};
  d.clock_speed_hz = khz * 1000;
  d.mode = 0;
  d.spics_io_num = -1;  // CS styres her (som sdspi gør), så kommandoen kan gå over flere overførsler
  d.queue_size = 1;
  if (spi_bus_add_device(host, &d, &g_dev) != ESP_OK) {
    g_dev = nullptr;
    return false;
  }
  g_cs = cs_pin;
  g_pdrv = pdrv;
  ff_diskio_register(pdrv, &kImpl);
  DLOG.printf("[SD] egen skrivedriver aktiv (drev %u, %d kHz): bussen slippes, mens kortet skriver\n",
              unsigned(pdrv), khz);
  return true;
}

void uninstall() {
  if (g_dev) {
    spi_bus_remove_device(g_dev);
    g_dev = nullptr;
  }
  g_card = nullptr;
  g_pdrv = -1;
}

void takeStats(uint32_t& max_busy_ms, uint32_t& sectors, uint32_t& errors) {
  max_busy_ms = g_max_busy_ms;
  sectors = g_sectors;
  errors = g_errors;
  g_max_busy_ms = g_sectors = g_errors = 0;
}

int pdrv() { return g_pdrv; }

}  // namespace sdfw
}  // namespace dm
