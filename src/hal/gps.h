// SPDX-License-Identifier: MIT
// DualMesh – GPS (T-Deck Plus: u-blox MIA-M10Q eller L76K på UART, GPIO 43/44).
//
// Læser NMEA (RMC + GGA fra alle systemer: GP/GN/GL/GA/GB) med vores egen lille
// parser. Baudrate og pinnenes retning findes automatisk (38400, 9600, 115200;
// RX 44/TX 43 og omvendt), indtil der kommer gyldige sætninger med checksum.
// Tiden fra RMC (status A) sættes i det fælles ur (wallclock, kilde GPS).
// Positionen bruges kun til det, brugeren har slået til (deling er fra som
// standard) og til SOS.
// Kører i loop()-tasken.
#pragma once
#include <cstdint>

namespace dm {

struct GpsFix {
  bool valid = false;       // seneste RMC/GGA havde fix
  int32_t lat_e7 = 0, lon_e7 = 0;  // grader x 1e7
  int32_t alt_dm = 0;       // højde over havet, decimeter
  uint8_t sats = 0;
  uint16_t hdop_x10 = 0;
  uint16_t speed_kmh_x10 = 0;
  uint16_t course_x10 = 0;
  uint32_t fix_ms = 0;      // millis() for sidste gyldige position (0 = aldrig)
  uint32_t unix = 0;        // UTC fra seneste gyldige RMC
};

class Gps {
 public:
  enum class State : uint8_t { Off, Searching, NoFix, Fix };
  void begin(bool enabled, bool set_time);
  void setEnabled(bool on);
  void setTimeSync(bool on) { set_time_ = on; }
  bool enabled() const { return enabled_; }
  // Strømspar: modulet i dvale (u-blox UBX-RXM-PMREQ, backup; satellitdata bevares) og vækkes
  // igen ved aktivitet på UART. Den seneste position bevares (lastPosition bliver ældre).
  void setSleep(bool sleep);
  bool sleeping() const { return sleep_; }
  // Slukket med setEnabled(false) sendes også dvalekommandoen (før lukkede vi kun porten).
  void tick();
  State state() const;
  const char* stateText() const;
  const GpsFix& fix() const { return fix_; }
  // Seneste kendte position (også gammel). age_s = sekunder siden. false = ingen.
  bool lastPosition(int32_t& lat_e7, int32_t& lon_e7, int32_t& alt_m, uint32_t& age_s) const;
  uint32_t sentences() const { return ok_; }
  uint32_t badChecksums() const { return bad_; }
  uint32_t baud() const { return found_ ? kBauds[baud_i_] : 0; }
  uint8_t satsInView() const { return sats_view_; }

 private:
  static constexpr uint32_t kBauds[3] = {38400, 9600, 115200};
  void openPort();
  void handle(char* s);
  void parseRmc(char** f, int n);
  void parseGga(char** f, int n);

  void sendPmreq();  // UBX-RXM-PMREQ: backup til der kommer noget på UART
  void wake();
  bool enabled_ = false, set_time_ = true, found_ = false, sleep_ = false;
  uint32_t sleep_bytes_ = 0, sleep_ms_ = 0, wake_ms_ = 0;  // dvale-statistik; wake_ms_ = 0: intet vækket
  int baud_i_ = 0;
  bool swap_ = false;
  uint32_t open_ms_ = 0, last_ok_ms_ = 0, last_time_set_ms_ = 0;
  char buf_[100];
  int len_ = 0;
  uint32_t ok_ = 0, bad_ = 0;
  uint8_t sats_view_ = 0;
  GpsFix fix_;
};

}  // namespace dm
