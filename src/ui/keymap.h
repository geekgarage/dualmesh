// SPDX-License-Identifier: MIT
// DualMesh – specialtegn pr. fysisk tast fra tastaturlayouts.
//
// Layouts (keyboards/*.kbd i repoet) er bygget ind i firmwaren. Installerede
// layouts ligger på SD i /sd/dualmesh/keyboards/<id>.kbd og går forud for de
// indbyggede med samme id. settings.ini: keyboard_layouts = da,latin (rækkefølge
// = rækkefølgen i overlayet). Hentning fra GitHub kommer, når WiFi findes.
#pragma once
#include <cstdint>

namespace dm {

class KeyMap {
 public:
  static constexpr int kMaxVariants = 16;

  // active_csv: fx "da,latin". Returnerer antal layouts, der blev fundet.
  int begin(const char* active_csv, bool sd_ok);
  // Varianter for en fysisk tast (fx 'a' -> "å","æ",...). Returnerer antal.
  int variants(char key, const char* out[], int max) const;
  const char* loadedSummary() const { return summary_; }

 private:
  bool loadText(const char* text);
  static bool readSd(const char* id, char* buf, int size);

  char var_[95][kMaxVariants][5] = {};  // ASCII 32..126
  uint8_t count_[95] = {};
  char summary_[96] = "";
};

}  // namespace dm
