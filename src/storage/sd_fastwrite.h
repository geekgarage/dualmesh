// SPDX-License-Identifier: MIT
// DualMesh – SD-skrivning, der ikke holder den delte SPI-bus, mens kortet arbejder.
//
// Problem (målt på turen 16. nov 2026): ESP-IDF's sdspi-driver venter på kortets "busy"
// efter hver skrevet sektor med bussen låst. Et billigt kort kan være optaget i 100-800 ms
// (intern oprydning), og imens kan radioen hverken lave CAD eller modtage (huller på op til
// 477 ms og profilskift på 144 ms).
//
// Løsning: efter monteringen erstattes FatFs-drevets diskdriver med vores: skrivning sker
// med egne SD-kommandoer (CMD24 + data, med CRC) på en ekstra SPI-enhed, og bussen slippes
// straks bagefter. Før næste kommando ventes der på, at kortet er klar, med korte
// forespørgsler (få µs på bussen) og 1 ms pause imellem, så radioen kommer til. Opstart af
// kortet og læsning bruger stadig ESP-IDF's driver (læsning har ingen lang ventetid).
#pragma once
#include "driver/spi_master.h"

namespace dm {
namespace sdfw {

// Efter esp_vfs_fat_sdspi_mount: overtag drevet for `card` (sdmmc_card_t*).
bool install(void* card, spi_host_device_t host, int cs_pin, int khz);
// Før afmontering.
void uninstall();
// Statistik til loggen: længste busy-ventetid (ms) og antal skrevne sektorer siden sidst.
void takeStats(uint32_t& max_busy_ms, uint32_t& sectors, uint32_t& errors);

}  // namespace sdfw
}  // namespace dm
