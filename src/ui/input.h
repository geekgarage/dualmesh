// SPDX-License-Identifier: MIT
// DualMesh – T-Deck-tastatur (ESP32-C3 på I²C 0x55) og trackball (GPIO-pulser).
//
// Fakta fra LilyGos T-Deck-eksempler: tastaturet svarer med ét tegn pr.
// I²C-læsning (0 = ingen tast) på SDA 18 / SCL 8. Trackballen giver en puls
// (kant) pr. skridt: GPIO 2 = højre, 3 = op, 1 = venstre, 15 = ned; klik er
// GPIO 0 (BOOT). Pulserne tælles i interrupts, så intet går tabt mellem polls.
#pragma once
#include <cstdint>

namespace dm {

// Rå tilstand (standard): tastaturet sender hele matricen (5 kolonner x 7
// rækker), så vi også ser Shift, Alt, Sym og Mic (fx til tastaturlyset), og selv
// omsætter taster til tegn med samme tabeller som LilyGos tastaturfirmware.
// keyboard_raw_mode=0 i settings.ini bruger tastaturets egen tegntilstand.
class Keyboard {
 public:
  bool begin(int sda = 18, int scl = 8, uint8_t addr = 0x55, bool raw = true);
  bool present() const { return present_; }
  bool rawMode() const { return raw_; }
  // Næste tegn, eller 0. Kald ca. hvert 10. ms.
  char poll();
  // true hvis en tast (også en modifier) er trykket eller sluppet siden sidst.
  bool takeActivity() {
    const bool a = activity_;
    activity_ = false;
    return a;
  }
  void setBacklight(uint8_t level);  // 0..255
  // Holdes Backspace nede (rå tilstand), kommer denne kode efter kRepeatDelayMs og derefter
  // hvert kRepeatMs. Den er adskilt fra 0x08, så en holdt tast kun sletter tekst og aldrig
  // bekræfter "slet kontakt/kanal" eller går tilbage.
  static constexpr char kBackspaceRepeat = 0x1F;
  static constexpr uint32_t kRepeatDelayMs = 450, kRepeatMs = 60;

 private:
  char mapKey(int col, int row) const;
  uint8_t addr_ = 0x55;
  bool present_ = false, raw_ = true, activity_ = false;
  uint32_t bs_next_ms_ = 0;  // næste gentagelse af Backspace
  uint8_t prev_[5] = {};
  char pending_[8] = {};
  uint8_t n_pending_ = 0;
};

struct TrackballEvent {
  int dx = 0, dy = 0;  // skridt siden sidst (højre/ned positiv)
  bool click = false;  // et klik (efter klik-forsinkelse)
};

class Trackball {
 public:
  void begin();
  // Samler skridt og klik siden sidste kald. click_delay_ms: et nyt klik
  // tæller først, når knappen har været sluppet (brugerens indstilling, 150 ms).
  TrackballEvent read(uint32_t click_delay_ms = 150);
  // Klikket holdes nede lige nu (rå tilstand, til "hold i 5 s" = SOS).
  bool held() const;
};

// Kapacitiv touch (GT911 på samme I²C-bus, adresse 0x5D eller 0x14).
// LilyGo: koordinater 320x240 efter "swap XY" og spejlet Y i forhold til panelet.
class Touch {
 public:
  struct Orientation {
    bool swap_xy = true, invert_x = false, invert_y = true;
  };
  bool begin(const Orientation& o);
  bool present() const { return addr_ != 0; }
  // true én gang pr. berøring (ved tryk), med skærmkoordinater 0..319 / 0..239.
  bool tap(int& x, int& y);
  // Berøringens forløb (til træk/scroll): Down ved første kontakt, Move mens fingeren er på
  // (x/y = nuværende punkt), Up når den slippes (x/y = sidste punkt), None = intet nyt.
  enum class Phase : uint8_t { None, Down, Move, Up };
  Phase poll(int& x, int& y);

 private:
  bool readReg(uint16_t reg, uint8_t* buf, int n);
  bool writeReg(uint16_t reg, uint8_t v);
  uint8_t addr_ = 0;
  Orientation o_{};
  bool was_down_ = false;
  int last_x_ = 0, last_y_ = 0;
};

}  // namespace dm
