// SPDX-License-Identifier: MIT
#include "storage/sd_log.h"
#include "status_display.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <Arduino.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace dm {

static constexpr int kW = 320, kH = 240;

void StatusDisplay::send(const uint8_t* d, size_t n, bool is_data) {
  gpio_set_level(gpio_num_t(p_.dc), is_data ? 1 : 0);
  // Small chunks: the radio shares this bus and must not wait long.
  constexpr size_t kChunk = 512;  // ~0.1 ms at 40 MHz
  while (n) {
    const size_t len = n < kChunk ? n : kChunk;
    spi_transaction_t t{};
    t.length = len * 8;
    t.tx_buffer = d;
    spi_device_polling_transmit(dev_, &t);
    d += len;
    n -= len;
  }
}

void StatusDisplay::cmd(uint8_t c, const uint8_t* data, size_t n) {
  send(&c, 1, false);
  if (n) send(data, n, true);
}

void StatusDisplay::window(int x0, int y0, int x1, int y1) {
  const uint8_t ca[4] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
  const uint8_t ra[4] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
  cmd(0x2A, ca, 4);
  cmd(0x2B, ra, 4);
  cmd(0x2C);
}

bool StatusDisplay::begin(const Pins& p) {
  p_ = p;
  // gpio_reset_pin selects the plain GPIO function: GPIO42 defaults to JTAG (MTMS).
  gpio_reset_pin(gpio_num_t(p_.dc));
  gpio_reset_pin(gpio_num_t(p_.backlight));
  gpio_set_direction(gpio_num_t(p_.dc), GPIO_MODE_OUTPUT);
  gpio_set_direction(gpio_num_t(p_.backlight), GPIO_MODE_OUTPUT);
  gpio_set_level(gpio_num_t(p_.backlight), 1);

  spi_device_interface_config_t d{};
  d.clock_speed_hz = p_.spi_hz;
  d.mode = 0;
  d.spics_io_num = p_.cs;
  d.queue_size = 1;
  if (spi_bus_add_device(p_.host, &d, &dev_) != ESP_OK) return false;

  cmd(0x01);  // software reset
  vTaskDelay(pdMS_TO_TICKS(150));
  cmd(0x11);  // sleep out
  vTaskDelay(pdMS_TO_TICKS(120));
  const uint8_t colmod = 0x55;  // 16 bit RGB565
  cmd(0x3A, &colmod, 1);
  cmd(0x36, &p_.madctl, 1);
  cmd(p_.invert ? 0x21 : 0x20);
  cmd(0x13);  // normal display
  cmd(0x29);  // display on
  vTaskDelay(pdMS_TO_TICKS(20));
  ok_ = true;
  clear();
  return true;
}

void StatusDisplay::clear(uint16_t bg) {
  if (!ok_) return;
  static uint8_t buf[kW * 2 * 8];
  for (size_t i = 0; i < sizeof(buf); i += 2) {
    buf[i] = uint8_t(bg >> 8);
    buf[i + 1] = uint8_t(bg);
  }
  window(0, 0, kW - 1, kH - 1);
  for (int y = 0; y < kH; y += 8) send(buf, sizeof(buf), true);
}

// Baggrundslyset på T-Deck styres af en LED-driver med 16 trin via én pin:
// høj = tændt (fuld styrke efter start), hver kort lav puls (< 0,5 ms) går ét
// trin ned (fra 1 rundt til 16), og lav i > 2,5 ms slukker den.
void StatusDisplay::applyLevel(uint8_t level) {
  const gpio_num_t pin = gpio_num_t(p_.backlight);
  if (level > kMaxLevel) level = kMaxLevel;
  if (level == hw_level_) return;
  if (level == 0) {
    gpio_set_level(pin, 0);
    delayMicroseconds(3000);
    hw_level_ = 0;
    return;
  }
  if (hw_level_ == 0) {
    gpio_set_level(pin, 1);
    delayMicroseconds(50);
    hw_level_ = kMaxLevel;
  }
  const int pulses = (hw_level_ - level + kMaxLevel) % kMaxLevel;
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  for (int i = 0; i < pulses; ++i) {
    portENTER_CRITICAL(&mux);  // pulsen må ikke blive lang (ellers slukker den)
    gpio_set_level(pin, 0);
    delayMicroseconds(2);
    gpio_set_level(pin, 1);
    portEXIT_CRITICAL(&mux);
    delayMicroseconds(2);
  }
  hw_level_ = level;
}

void StatusDisplay::setBrightness(uint8_t level) {
  user_level_ = level < 1 ? 1 : (level > kMaxLevel ? kMaxLevel : level);
  if (ok_ && on_) applyLevel(dimmed_ && kDimLevel < user_level_ ? kDimLevel : user_level_);
}

void StatusDisplay::setDimmed(bool dim) {
  dimmed_ = dim;
  if (ok_ && on_) applyLevel(dim && kDimLevel < user_level_ ? kDimLevel : user_level_);
}

void StatusDisplay::setPower(bool on) {
  if (!ok_ || on == on_) return;
  on_ = on;
  if (on) {
    cmd(0x29);  // DISPON
    dimmed_ = false;
    applyLevel(user_level_);
  } else {
    applyLevel(0);
    cmd(0x28);  // DISPOFF
  }
}

void StatusDisplay::pushRect565(int x0, int y0, int x1, int y1, const uint16_t* px, size_t count) {
  if (!ok_ || !px) return;
  alignas(4) static uint8_t chunk[512];
  window(x0, y0, x1, y1);
  while (count) {
    const size_t n = count < sizeof(chunk) / 2 ? count : sizeof(chunk) / 2;
    for (size_t i = 0; i < n; ++i) {
      chunk[2 * i] = uint8_t(px[i] >> 8);
      chunk[2 * i + 1] = uint8_t(px[i]);
    }
    send(chunk, n * 2, true);
    px += n;
    count -= n;
  }
}

void StatusDisplay::pushRect(int x0, int y0, int x1, int y1, const uint8_t* data, size_t n) {
  if (!ok_ || !data) return;
  alignas(4) static uint8_t chunk[512];
  window(x0, y0, x1, y1);
  while (n) {
    const size_t len = n < sizeof(chunk) ? n : sizeof(chunk);
    memcpy(chunk, data, len);
    send(chunk, len, true);
    data += len;
    n -= len;
  }
}

}  // namespace dm
