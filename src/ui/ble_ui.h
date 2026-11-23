// SPDX-License-Identifier: MIT
// DualMesh – Bluetooth-status (åbnes fra Indst. > Bluetooth-status). Kun visning.
// Backspace/Enter eller trackball venstre: tilbage.
#pragma once
#include <Arduino.h>

#include "net/ble_serial.h"
#include "net/mc_companion.h"
#include "net/mt_ble.h"
#include "ui/panel.h"
#include "ui/status_display.h"

namespace dm {

class BleUi : public Panel {
 public:
  void begin(BleSerial* ble, McCompanion* comp, bool enabled) {
    ble_ = ble;
    comp_ = comp;
    enabled_ = enabled;
  }
  // Meshtastic-appen (Indst. > Bluetooth > App = Meshtastic): transporten kører, navn og PIN.
  void setMtApp(MtBle* mt, const char* name, uint32_t pin) {
    mt_ = mt;
    snprintf(mt_name_, sizeof(mt_name_), "%s", name ? name : "");
    mt_pin_ = pin;
  }
  // Bluetooth ønsket i Indst., men appens net er slået fra (forklares på skærmen).
  void setWanted(bool wanted, const char* app) {
    wanted_ = wanted;
    snprintf(app_, sizeof(app_), "%s", app ? app : "");
  }
  const char* title() const override { return "Bluetooth"; }
  bool needsRedraw() const override { return millis() - last_ms_ > 1000; }
  void stepY(int) override {}
  void click() override { requestClose(); }
  void key(char c) override {
    if (c == 0x08 || c == 0x7F || c == '\r' || c == '\n') requestClose();
  }
  void view(PanelView& v, int) override {
    last_ms_ = millis();
    v.clear();
    if (mt_) {  // Meshtastic-appen
      const bool con = mt_->connected();
      snprintf(v.status, sizeof(v.status), "%s", con ? "Meshtastic-app forbundet" : "Venter på Meshtastic-appen");
      v.status_fg = con ? color::Green : color::Yellow;
      v.info("Navn", "%.36s", mt_name_);
      v.info("PIN", "%06lu", (unsigned long)mt_pin_);
      v.add(PanelRow::Blank, color::Gray, "");
      v.add(PanelRow::Info, color::Gray, "Parring: vælg enheden i Meshtastic-");
      v.add(PanelRow::Info, color::Gray, "appen og skriv PIN-koden ovenfor.");
    } else if (!enabled_ || !ble_ || !ble_->started()) {
      snprintf(v.status, sizeof(v.status), "Bluetooth: slået fra");
      v.status_fg = color::Gray;
      if (wanted_) {  // slået til, men appens net kører ikke
        v.add(PanelRow::Info, color::Yellow, "Bluetooth til app er slået til, men");
        char t[48];
        snprintf(t, sizeof(t), "%s er slået fra (eller ikke startet).", app_);
        v.add(PanelRow::Info, color::Yellow, t);
      } else {
        v.add(PanelRow::Info, color::White, "Slå \"Bluetooth til app\" til under");
        v.add(PanelRow::Info, color::White, "Bluetooth i Indst., vælg app og Gem.");
        v.add(PanelRow::Info, color::Gray, "T-Deck'en genstarter derefter.");
      }
    } else {
      const bool con = ble_->connected();
      snprintf(v.status, sizeof(v.status), "%s", con ? "MeshCore-app forbundet" : "Venter på MeshCore-appen");
      v.status_fg = con ? color::Green : color::Yellow;
      v.info("Navn", "%.36s", ble_->name());
      v.info("PIN", "%06lu", (unsigned long)ble_->pin());
      v.info("I kø til appen", "%d beskeder", comp_ ? comp_->queued() : 0);
      v.add(PanelRow::Blank, color::Gray, "");
      v.add(PanelRow::Info, color::Gray, "Parring: vælg enheden i MeshCore-");
      v.add(PanelRow::Info, color::Gray, "appen og skriv PIN-koden ovenfor.");
    }
    snprintf(v.hint, sizeof(v.hint), "Backspace: tilbage");
    return;
  }

 private:
  BleSerial* ble_ = nullptr;
  McCompanion* comp_ = nullptr;
  bool enabled_ = false;
  MtBle* mt_ = nullptr;
  char mt_name_[24] = "";
  uint32_t mt_pin_ = 0;
  bool wanted_ = false;
  char app_[16] = "";
  uint32_t last_ms_ = 0;
};

}  // namespace dm
