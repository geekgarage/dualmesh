// SPDX-License-Identifier: MIT
// DualMesh – MQTT-status (åbnes fra Indst. > MQTT > MQTT-status). Kun visning.
// Backspace/Enter/klik: tilbage.
#pragma once
#include <Arduino.h>

#include "net/mqtt_bridge.h"
#include "ui/panel.h"
#include "ui/status_display.h"

namespace dm {

class MqttUi : public Panel {
 public:
  void begin(MqttBridge* b) { b_ = b; }
  const char* title() const override { return "MQTT"; }
  bool needsRedraw() const override { return millis() - last_ms_ > 1000; }
  void stepY(int) override {}
  void click() override { requestClose(); }
  void key(char c) override {
    if (c == 0x08 || c == 0x7F || c == '\r' || c == '\n') requestClose();
  }
  void view(PanelView& v, int) override {
    last_ms_ = millis();
    v.clear();
    const bool con = b_ && b_->connected();
    snprintf(v.status, sizeof(v.status), "MQTT-bro: %s", b_ ? b_->stateText() : "-");
    v.status_fg = con ? color::Green : color::Yellow;
    snprintf(v.status2, sizeof(v.status2), "Rod: %.55s", b_ ? b_->root() : "");
    v.info("Sendt til broker", "%lu", (unsigned long)(b_ ? b_->published() : 0));
    v.add(PanelRow::Header, color::Gold, "Emner (<net> = meshcore/meshtastic)");
    v.info("Modtaget", "<rod>/rx/<net>/chan|dm/..");
    v.info("Send", "<rod>/tx/<net>/chan|dm/..");
    v.info("Svar", "<rod>/result");
    v.info("Status", "<rod>/status");
    snprintf(v.note, sizeof(v.note), "Kanalnavne URL-kodes: #test = %%23test");
    snprintf(v.hint, sizeof(v.hint), "Egen CA: /dualmesh/mqtt_ca.pem    Backspace: tilbage");
    return;
  }

 private:
  MqttBridge* b_ = nullptr;
  uint32_t last_ms_ = 0;
};

}  // namespace dm
