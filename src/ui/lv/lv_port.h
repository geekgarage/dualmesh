// SPDX-License-Identifier: MIT
// DualMesh – LVGL koblet på skærmen (fase 4). LVGL tegner i en buffer i PSRAM (320 x 40
// pixel ad gangen), og StatusDisplay::pushRect sender den i små bidder over den delte
// SPI-bus. Startes først, når den nye brugerflade bruges (sparer RAM ellers).
#pragma once

namespace dm {
class StatusDisplay;

namespace lvport {
bool begin(StatusDisplay* d);  // kan kaldes flere gange; true = klar
bool ready();
void handle();      // lv_timer_handler(): kald ofte, når LVGL ejer skærmen
void invalidate();  // tegn hele skærmen igen (fx efter den gamle tekstvisning)
}  // namespace lvport
}  // namespace dm
