// SPDX-License-Identifier: MIT
// DualMesh – seneste nedbrud fra ESP-IDF's core dump i flash (partitionen "coredump" i
// default_16MB.csv; core dump til flash er slået til i Arduino-ESP32 2.0.x's sdkconfig).
//
// Ved et nedbrud (panic, interrupt-watchdog) skriver ESP-IDF selv et core dump, før chippen
// genstarter. Ved næste opstart skriver logAtBoot() et resumé med [CRASH] i loggen (og dermed
// på SD), så nedbruddet kan ses, selvom ingen PC var tilsluttet. Dumpet bliver liggende, indtil
// brugeren sletter det (Indst. > System > Fejlrapport) eller et nyt nedbrud overskriver det.
#pragma once
#include <Print.h>

#include "diag_report.h"

namespace dm {
namespace crashinfo {

// Læser resuméet af det gemte core dump. false = intet (gyldigt) dump.
bool read(diag::CrashSummary& out);

// true, hvis nedbruddet kom fra et andet build end det, der kører nu (ELF-hash).
bool fromOtherBuild(const diag::CrashSummary& c);

// Kaldes én gang ved opstart, når loggen kører: skriver [CRASH]-linjer, hvis denne opstart
// skyldes et nedbrud (ESP_RST_PANIC / ESP_RST_INT_WDT), og et dump findes.
void logAtBoot(Print& log);

// Sletter det gemte dump (64 KB flash: ca. 0,5-1 s, hvor flash-cachen er slået fra – radioen
// kan miste en pakke). true = slettet.
bool erase();

// Kort ASCII-kode for denne opstarts årsag (til rapport-linket), fx "PANIC", "TASK_WDT".
const char* resetCode();

}  // namespace crashinfo
}  // namespace dm
