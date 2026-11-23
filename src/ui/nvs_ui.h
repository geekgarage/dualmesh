// SPDX-License-Identifier: MIT
// DualMesh – Intern NVS (Indst. > System > Intern NVS). Se storage/nvs_tool.h.
//   Øverst: forbrug og om DualMesh kører fra en launcher.
//   Handlinger (klik to gange): ryd Bluetooth-parringer, ryd WiFi-/radiodata,
//   ryd ét navnerum fra listen. "Ryd HELE NVS" kræver, at man skriver SLET ALT
//   (sletter også launcherens og andre firmwares indstillinger). Genstart bagefter,
//   når det berører Bluetooth/WiFi eller hele NVS.
#pragma once
#include <cstdint>

#include "storage/nvs_tool.h"
#include "ui/panel.h"

namespace dm {

class NvsUi : public Panel {
 public:
  using RestartFn = void (*)(const char* why);
  void begin(RestartFn restart) { restart_ = restart; }
  const char* title() const override { return "Intern NVS"; }
  void onOpen() override;
  void view(PanelView& v, int rows) override;
  void tapRow(int i) override;
  void key(char c) override;
  void stepY(int dir) override;
  void click() override;
  bool needsRedraw() const override { return true; }

 private:
  static constexpr int kMaxNs = 48, kActions = 3, kTop = 4, kRows = 8;
  void refresh();
  void note(const char* t);
  int count() const { return kActions + n_ns_; }
  bool eraseKind(nvstool::Kind a, nvstool::Kind b);

  RestartFn restart_ = nullptr;
  nvstool::Ns ns_[kMaxNs] = {};
  int n_ns_ = 0;
  nvstool::Stats st_;
  int sel_ = 0, top_ = 0;
  int armed_ = -1;
  uint32_t armed_ms_ = 0;
  bool typing_ = false;
  char buf_[16] = "";
  char note_[48] = "";
  uint32_t note_ms_ = 0;
};

}  // namespace dm
