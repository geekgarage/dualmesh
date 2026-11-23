// SPDX-License-Identifier: MIT
// ST7789-skærmen på T-Deck'en (320x240, liggende): init, pixels fra LVGL, tænd/sluk og
// baggrundslys. Al tegning sker med LVGL (fase 4; den gamle 8x16-tekstvisning er fjernet).
// Skærmen deler SPI-bus med radioen, så pixels sendes i små bidder.
#pragma once
#include <cstdint>
#include "driver/spi_master.h"

namespace dm {

namespace color {
constexpr uint16_t Black = 0x0000, White = 0xFFFF, Gray = 0x8410, Green = 0x07E0,
                   Red = 0xF800, Yellow = 0xFFE0, Cyan = 0x07FF;
// Cyan (lyseblå) bruges KUN til fanelinjen og bjælken, der viser valget (brugerens krav
// 15. nov 2026). Brug ikke color::Cyan til tekst. Valgbjælke: color::Select.
constexpr uint16_t Select = Cyan;
constexpr uint16_t Dim = 0x4208;  // nedtonet (låst indstilling)
// Ekstra farver (RGB565), bl.a. til grupperne i indstillingsmenuen.
constexpr uint16_t Orange = 0xFD20, Magenta = 0xF81F, LightBlue = 0x5D9F, Lime = 0xAFE5,
                   Salmon = 0xFB2C, Violet = 0xA45F, Gold = 0xFEA0, Pink = 0xFC18, Peach = 0xFDB8;
}

class StatusDisplay {
 public:
  struct Pins {
    int cs = 12, dc = 11, backlight = 42;
    spi_host_device_t host = SPI2_HOST;
    int spi_hz = 40 * 1000 * 1000;
    uint8_t madctl = 0x60;  // landscape; 0xA0 if the picture is upside down
    bool invert = true;     // IPS panel: colours need inversion
  };

  bool begin(const Pins& p);
  bool begin() { return begin(Pins{}); }
  void clear(uint16_t bg = color::Black);  // hele skærmen i én farve (ved opstart)
  // Rå pixels (RGB565, stor endian) til et rektangel - til LVGL. Kopieres via en lille
  // intern buffer og sendes i bidder på 512 B, så radioen aldrig venter længe på bussen
  // (data kan ligge i PSRAM).
  void pushRect(int x0, int y0, int x1, int y1, const uint8_t* data, size_t n);
  // Som pushRect, men pixels som uint16_t RGB565 i CPU'ens rækkefølge (fx opstartslogoet);
  // byteordenen vendes undervejs.
  void pushRect565(int x0, int y0, int x1, int y1, const uint16_t* px, size_t count);
  // Skærm og baggrundslys til/fra (DISPON/DISPOFF + GPIO42). Panelets RAM bevares.
  void setPower(bool on);
  bool powered() const { return on_; }
  // Lysstyrke 1..16 (brugerens indstilling) og dæmpning (før skærmen slukker).
  void setBrightness(uint8_t level);
  void setDimmed(bool dim);
  static constexpr uint8_t kMaxLevel = 16, kDimLevel = 2;

 private:
  void cmd(uint8_t c, const uint8_t* data = nullptr, size_t n = 0);
  void send(const uint8_t* d, size_t n, bool is_data);
  void window(int x0, int y0, int x1, int y1);

  Pins p_{};
  spi_device_handle_t dev_ = nullptr;
  bool ok_ = false;
  bool on_ = true;
  bool dimmed_ = false;
  uint8_t user_level_ = kMaxLevel;
  uint8_t hw_level_ = kMaxLevel;  // baggrundslysets aktuelle trin (0 = slukket)
  void applyLevel(uint8_t level);
};

}  // namespace dm
