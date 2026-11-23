// SPDX-License-Identifier: MIT
#include "storage/sd_log.h"
#include "ui/input.h"

#include <Arduino.h>
#include <Wire.h>

#include <cstring>

namespace dm {

namespace {
constexpr int kRight = 2, kUp = 3, kLeft = 1, kDown = 15, kClick = 0;
volatile int32_t g_right = 0, g_up = 0, g_left = 0, g_down = 0;

void IRAM_ATTR isrRight() { g_right = g_right + 1; }
void IRAM_ATTR isrUp() { g_up = g_up + 1; }
void IRAM_ATTR isrLeft() { g_left = g_left + 1; }
void IRAM_ATTR isrDown() { g_down = g_down + 1; }
}  // namespace

// Tastmatrix fra LilyGos tastaturfirmware (Keyboard_ESP32C3): [kolonne][række].
// 0 = ingen tegn (modifier eller specialtast).
static const char kKeys[5][7] = {
    {'q', 'w', 0, 'a', 0, ' ', 0},      // [0][2] Sym, [0][4] Alt, [0][6] Mic
    {'e', 's', 'd', 'p', 'x', 'z', 0},  // [1][6] venstre Shift
    {'r', 'g', 't', 0, 'v', 'c', 'f'},  // [2][3] højre Shift
    {'u', 'h', 'y', 0, 'b', 'n', 'j'},  // [3][3] Enter
    {'o', 'l', 'i', 0, '$', 'm', 'k'},  // [4][3] Backspace
};
static const char kSymbols[5][7] = {
    {'#', '1', 0, '*', 0, 0, '0'},
    {'2', '4', '5', '@', '8', '7', 0},
    {'3', '/', '(', 0, '?', '9', '6'},
    {'_', ':', ')', 0, '!', ',', ';'},
    {'+', '"', '-', 0, 0, '.', '\''},
};

bool Keyboard::begin(int sda, int scl, uint8_t addr, bool raw) {
  addr_ = addr;
  raw_ = raw;
  Wire.begin(sda, scl, 100000);
  Wire.beginTransmission(addr_);
  present_ = Wire.endTransmission() == 0;
  if (present_) {
    Wire.beginTransmission(addr_);
    Wire.write(raw_ ? 0x03 : 0x04);  // LILYGO_KB_MODE_RAW_CMD / _KEY_CMD
    Wire.endTransmission();
    memset(prev_, 0, sizeof(prev_));
  }
  return present_;
}

char Keyboard::mapKey(int col, int row) const {
  if (col == 3 && row == 3) return 0x0D;  // Enter
  if (col == 4 && row == 3) return 0x08;  // Backspace
  const bool sym = prev_[0] & (1u << 2);
  const bool shift = (prev_[1] & (1u << 6)) || (prev_[2] & (1u << 3));
  if (col == 0 && row == 6 && !sym) return 0x1E;  // Mic (uden Sym): emoji-vælgeren
  char c = sym ? kSymbols[col][row] : kKeys[col][row];
  if (!c && sym) c = kKeys[col][row];  // fx mellemrum med Sym holdt nede
  if (shift && c >= 'a' && c <= 'z') c = char(c - 32);
  return c;
}

char Keyboard::poll() {
  if (!present_) return 0;
  if (!raw_) {
    if (Wire.requestFrom(int(addr_), 1) != 1) return 0;
    const int c = Wire.read();
    if (c > 0) activity_ = true;
    return c > 0 ? char(c) : 0;
  }
  if (n_pending_ == 0) {
    uint8_t cur[5];
    if (Wire.requestFrom(int(addr_), 5) != 5) return 0;
    uint8_t raw_bytes[5];
    for (int i = 0; i < 5; ++i) raw_bytes[i] = uint8_t(Wire.read());
    // Ældre tastaturfirmware uden rå tilstand svarer kun med én byte; resten
    // læses som 0xFF (= alle syv taster i en kolonne trykket, fysisk umuligt).
    for (int i = 1; i < 5; ++i)
      if (raw_bytes[i] == 0xFF) {
        raw_ = false;
        Wire.beginTransmission(addr_);
        Wire.write(0x04);  // tilbage til tegntilstand
        Wire.endTransmission();
        dm::DLOG.println("[UI] tastaturet kan ikke raa tilstand - bruger tegntilstand "
                       "(modifier-taster taender ikke lyset)");
        return 0;
      }
    for (int i = 0; i < 5; ++i) cur[i] = raw_bytes[i] & 0x7F;
    if (memcmp(cur, prev_, sizeof(cur)) != 0) activity_ = true;
    // Nye tryk: bits der er sat nu, men ikke var det før. Modifiers (prev_) skal
    // gælde for trykket, så de opdateres før tegnene findes.
    uint8_t pressed[5];
    for (int i = 0; i < 5; ++i) pressed[i] = cur[i] & uint8_t(~prev_[i]);
    memcpy(prev_, cur, sizeof(prev_));
    for (int col = 0; col < 5 && n_pending_ < sizeof(pending_); ++col)
      for (int row = 0; row < 7 && n_pending_ < sizeof(pending_); ++row)
        if (pressed[col] & (1u << row)) {
          const char c = mapKey(col, row);
          if (c) pending_[n_pending_++] = c;
        }
    // Backspace (kolonne 4, række 3) holdt nede: gentag (kun Backspace).
    const uint32_t now = millis();
    if (pressed[4] & (1u << 3)) {
      bs_next_ms_ = now + kRepeatDelayMs;
    } else if ((cur[4] & (1u << 3)) && int32_t(now - bs_next_ms_) >= 0 && n_pending_ < sizeof(pending_)) {
      pending_[n_pending_++] = kBackspaceRepeat;
      bs_next_ms_ = now + kRepeatMs;
    }
  }
  if (n_pending_ == 0) return 0;
  const char c = pending_[0];
  memmove(pending_, pending_ + 1, --n_pending_);
  return c;
}

void Keyboard::setBacklight(uint8_t level) {
  if (!present_) return;
  Wire.beginTransmission(addr_);
  Wire.write(0x01);  // LILYGO_KB_BRIGHTNESS_CMD
  Wire.write(level);
  Wire.endTransmission();
}

void Trackball::begin() {
  for (int p : {kRight, kUp, kLeft, kDown, kClick}) pinMode(p, INPUT_PULLUP);
  attachInterrupt(kRight, isrRight, CHANGE);
  attachInterrupt(kUp, isrUp, CHANGE);
  attachInterrupt(kLeft, isrLeft, CHANGE);
  attachInterrupt(kDown, isrDown, CHANGE);
}

bool Trackball::held() const { return digitalRead(kClick) == LOW; }

TrackballEvent Trackball::read(uint32_t click_delay_ms) {
  TrackballEvent e;
  noInterrupts();
  const int32_t r = g_right, u = g_up, l = g_left, d = g_down;
  g_right = g_up = g_left = g_down = 0;
  interrupts();
  e.dx = int(r - l);
  e.dy = int(d - u);

  // Klik: aktiv lav. Tæller én gang pr. tryk, og først efter at knappen har
  // været sluppet i click_delay_ms (undgår dobbeltklik ved prel).
  static bool armed = true;
  static uint32_t released_ms = 0;
  const bool down = digitalRead(kClick) == LOW;
  const uint32_t now = millis();
  if (down && armed) {
    e.click = true;
    armed = false;
  } else if (!down) {
    if (released_ms == 0) released_ms = now;
    if (!armed && now - released_ms >= click_delay_ms) armed = true;
  }
  if (down) released_ms = 0;
  return e;
}

bool Touch::readReg(uint16_t reg, uint8_t* buf, int n) {
  Wire.beginTransmission(addr_);
  Wire.write(uint8_t(reg >> 8));
  Wire.write(uint8_t(reg));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(int(addr_), n) != n) return false;
  for (int i = 0; i < n; ++i) buf[i] = uint8_t(Wire.read());
  return true;
}

bool Touch::writeReg(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(addr_);
  Wire.write(uint8_t(reg >> 8));
  Wire.write(uint8_t(reg));
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool Touch::begin(const Orientation& o) {
  o_ = o;
  for (uint8_t a : {uint8_t(0x5D), uint8_t(0x14)}) {
    addr_ = a;
    uint8_t id[4] = {};
    if (readReg(0x8140, id, 4) && id[0] == '9') return true;  // produkt-id "911"
  }
  addr_ = 0;
  return false;
}

bool Touch::tap(int& x, int& y) {
  if (!addr_) return false;
  uint8_t st = 0;
  if (!readReg(0x814E, &st, 1)) return false;
  if (!(st & 0x80)) return false;  // ingen nye data
  const int points = st & 0x0F;
  bool down = false;
  if (points > 0) {
    uint8_t p[4];
    if (readReg(0x8150, p, 4)) {
      int rx = p[0] | (p[1] << 8), ry = p[2] | (p[3] << 8);
      int sx = o_.swap_xy ? ry : rx, sy = o_.swap_xy ? rx : ry;
      if (o_.invert_x) sx = 319 - sx;
      if (o_.invert_y) sy = 239 - sy;
      x = sx < 0 ? 0 : (sx > 319 ? 319 : sx);
      y = sy < 0 ? 0 : (sy > 239 ? 239 : sy);
      down = true;
    }
  }
  writeReg(0x814E, 0);  // kvittér, så GT911 måler videre
  const bool new_tap = down && !was_down_;
  was_down_ = down;
  return new_tap;
}

Touch::Phase Touch::poll(int& x, int& y) {
  const bool before = was_down_;
  int nx = last_x_, ny = last_y_;
  const bool fresh = tap(nx, ny);  // opdaterer was_down_ og koordinaterne, hvis der er nye data
  if (fresh) {
    last_x_ = x = nx;
    last_y_ = y = ny;
    return Phase::Down;
  }
  if (before && !was_down_) {  // sluppet
    x = last_x_;
    y = last_y_;
    return Phase::Up;
  }
  if (was_down_ && (nx != last_x_ || ny != last_y_)) {
    last_x_ = x = nx;
    last_y_ = y = ny;
    return Phase::Move;
  }
  return Phase::None;
}

}  // namespace dm
