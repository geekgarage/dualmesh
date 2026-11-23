// SPDX-License-Identifier: MIT
// DualMesh – "Omkring DualMesh" (Indst. > System): version, hvem der står bag, licenser og en
// QR-kode til GitHub-projektet (brugerens ønske 17. nov 2026). Teksten står til venstre i
// tekstgitteret; QR-koden tegnes af LVGL til højre (Panel::qrText).
#pragma once
#include <cstdint>
#include <cstdio>

#include "ui/panel.h"
#include "ui/status_display.h"
#include "version.h"

namespace dm {

class AboutUi : public Panel {
 public:
  // Node-id'er til visning (kan være tomme, hvis nettet er slået fra).
  void setIds(const char* mc_id, const char* mt_id) {
    snprintf(mc_id_, sizeof(mc_id_), "%s", mc_id ? mc_id : "");
    snprintf(mt_id_, sizeof(mt_id_), "%s", mt_id ? mt_id : "");
  }
  const char* title() const override { return "Omkring DualMesh"; }
  const char* qrText() const override { return DM_PROJECT_URL; }
  // LVGL: rækkerne står til venstre for QR-koden (AppUi gør dem smallere).
  void view(PanelView& v, int) override {
    v.clear();
    snprintf(v.status, sizeof(v.status), "DualMesh %s", DM_FW_VERSION);
    snprintf(v.status2, sizeof(v.status2), "MeshCore v1.18.0 + Meshtastic 2.x (komp.)");
    snprintf(v.status3, sizeof(v.status3), "LilyGo T-Deck Plus");
    v.info("Build", "%.16s", DM_GIT_REV);
    v.info("Bygget", "%s", __DATE__);
    v.add(PanelRow::Info, color::Gray, "Lavet af", color::Green, "%s", DM_AUTHOR);
    v.info("Licens", "%s, open source", DM_LICENSE);
    if (mc_id_[0]) v.info("MeshCore", "%.20s", mc_id_);
    if (mt_id_[0]) v.info("Meshtastic", "%.12s", mt_id_);
    snprintf(v.note, sizeof(v.note), "%s", DM_PROJECT_URL_SHORT);
    v.note_fg = color::LightBlue;
    snprintf(v.hint, sizeof(v.hint), "Bygger på MeshCore, LVGL m.fl. (se README)");
    return;
  }
  void key(char c) override {
    if (c == 0x08 || c == 0x7F || c == 0x1B || c == '\r' || c == '\n') requestClose();
  }
  void stepY(int) override {}
  void click() override { requestClose(); }

 private:
  char mc_id_[32] = "";
  char mt_id_[16] = "";
};

}  // namespace dm
