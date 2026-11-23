// SPDX-License-Identifier: MIT
// DualMesh – SD-kortet som USB-drev (lager-tilstand, ønske 22. nov 2026).
//
// Man går ind i lager-tilstanden ved en genstart (Indst. > System > "SD-kort som USB-drev" eller
// monitor U + LAGER): et flag i RTC-hukommelsen overlever genstarten, og setup() kalder så run()
// i stedet for at starte radio, net og brugerflade. I lager-tilstanden:
//   - kører kun SPI-bussen, skærmen (LVGL) og SD-kortet; FatFs monteres IKKE, så ingen andre end
//     PC'en skriver på kortet (ellers ødelægges filsystemet),
//   - skifter chippens USB fra Serial-JTAG til TinyUSB og viser kortet som et drev (MSC); den
//     serielle port er væk, indtil T-Deck'en genstarter,
//   - genstarter T-Deck'en i normal tilstand, når drevet skubbes ud på PC'en eller trackballen
//     klikkes (skærmen beder om at skubbe ud først).
// Flaget slettes ved opstart, så et strømsvigt eller et nedbrud altid giver normal tilstand.
#pragma once
#include <cstdint>

#include "driver/spi_common.h"

namespace dm {
class StatusDisplay;

namespace usbdisk {

// Sæt flaget og genstart (vender ikke tilbage).
[[noreturn]] void rebootIntoStorage();
// Var flaget sat ved denne opstart? Sletter det (kald én gang, tidligt i setup()).
bool takeRequest();
// Lager-tilstanden (vender ikke tilbage). SPI-bussen og skærmen skal være startet.
[[noreturn]] void run(spi_host_device_t host, int sd_cs, int max_khz, StatusDisplay* disp, int click_pin);

}  // namespace usbdisk
}  // namespace dm
