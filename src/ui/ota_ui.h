// SPDX-License-Identifier: MIT
// DualMesh – "Opdatér firmware" (Indst. > System): finder den seneste udgivelse på GitHub
// (releases/latest/download/version.txt, lavet af .github/workflows/release.yml) og installerer
// den over WiFi i den anden app-partition (SHA-256 kontrolleres, før den vælges), hvorefter
// T-Deck'en genstarter. Kører DualMesh under en launcher, røres partitionerne ikke: firmwaren
// gemmes i stedet som /dualmesh/firmware/<fil> på SD-kortet, så launcheren kan installere den.
// version.txt: linjer "version 0.1.1", "rev abc1234", "file dualmesh-tdeck_plus.bin",
// "size 3683104", "sha256 <64 hex>".
#pragma once
#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net/dl.h"
#include "ui/panel.h"
#include "version.h"

namespace dm {

class OtaUi : public Panel {
 public:
  using RestartFn = void (*)();
  void begin(bool launcher, RestartFn restart) {
    launcher_ = launcher;
    restart_ = restart;
  }
  const char* title() const override { return "Opdatér firmware"; }
  void onOpen() override {
    armed_ = false;
    sel_ = 0;
    if (dl::busy()) {
      if (!mine_) snprintf(msg_, sizeof(msg_), "En anden hentning kører; prøv igen bagefter");
      return;
    }
    check();
  }
  void tick() {
    if (!mine_) return;
    char m[80];
    const dl::State s = dl::poll(m, sizeof(m));
    if (s != dl::State::Done && s != dl::State::Failed) return;
    mine_ = false;
    if (phase_ == Phase::Check) {
      phase_ = Phase::Ready;
      if (s == dl::State::Done && parse(dl::text())) msg_[0] = 0;
      else snprintf(msg_, sizeof(msg_), "Ingen udgivelse fundet: %.50s", s == dl::State::Done ? "version.txt er ugyldig" : m);
      return;
    }
    phase_ = Phase::Ready;
    snprintf(msg_, sizeof(msg_), "%s", m);
    if (s == dl::State::Done && !launcher_) {
      installed_ = true;
      if (restart_) restart_();
    } else if (s == dl::State::Done) {
      snprintf(msg_, sizeof(msg_), "Gemt: installér /dualmesh/firmware/%.30s fra launcheren", file_);
    }
  }

  void view(PanelView& v, int) override {
    v.clear();
    snprintf(v.status, sizeof(v.status), "Denne: %s (%s)", DM_FW_VERSION, DM_GIT_REV);
    if (phase_ == Phase::Check) {
      snprintf(v.status2, sizeof(v.status2), "Søger efter den nyeste udgivelse ...");
      snprintf(v.hint, sizeof(v.hint), "Backspace: tilbage");
      return;
    }
    if (busy()) {
      uint64_t done, total;
      uint32_t bps;
      dl::progress(done, total, bps);
      snprintf(v.status2, sizeof(v.status2), "%s %s: %u %%  %lu KB/s", launcher_ ? "Henter" : "Installerer", ver_,
               total ? unsigned(done * 100 / total) : 0u, (unsigned long)(bps / 1024));
      PanelRow* r = v.add(PanelRow::Item, color::Red, "Afbryd");
      if (r) r->sel = true;
      snprintf(v.note, sizeof(v.note), "Sluk ikke T-Deck'en under opdateringen");
      v.note_fg = color::Yellow;
      snprintf(v.hint, sizeof(v.hint), "Klik: afbryd");
      return;
    }
    if (installed_) {
      snprintf(v.status2, sizeof(v.status2), "Version %s er installeret. Genstarter ...", ver_);
      return;
    }
    if (ver_[0]) {
      snprintf(v.status2, sizeof(v.status2), "På GitHub: %s (%s), %lu KB", ver_, rev_, (unsigned long)(size_ / 1024));
      const int c = cmp(ver_, DM_FW_VERSION);
      char lab[48];
      if (launcher_) snprintf(lab, sizeof(lab), "Hent %s til SD-kortet", ver_);
      else if (c > 0) snprintf(lab, sizeof(lab), "Opdatér til %s", ver_);
      else if (c == 0) snprintf(lab, sizeof(lab), "Installér %s igen", ver_);
      else snprintf(lab, sizeof(lab), "Installér ældre %s", ver_);
      PanelRow* a = v.add(PanelRow::Item, c > 0 ? color::Green : color::White, armed_ ? "Klik igen for at starte" : lab);
      if (a) {
        a->sel = sel_ == 0;
        a->confirm = armed_;
      }
    }
    PanelRow* b = v.add(PanelRow::Item, color::White, "Søg igen");
    if (b) b->sel = ver_[0] ? sel_ == 1 : true;
    if (msg_[0]) {
      snprintf(v.note, sizeof(v.note), "%s", msg_);
    } else if (launcher_) {
      snprintf(v.note, sizeof(v.note), "Launcher fundet: firmwaren gemmes på SD-kortet");
    } else if (ver_[0] && cmp(ver_, DM_FW_VERSION) <= 0) {
      snprintf(v.note, sizeof(v.note), "Du har den nyeste version");
    }
    snprintf(v.hint, sizeof(v.hint), "Klik: vælg   Backspace: tilbage");
  }

  void tapRow(int i) override {
    if (busy()) {
      click();
      return;
    }
    if (!ver_[0]) {
      check();
      return;
    }
    if (sel_ != i) armed_ = false;
    sel_ = i;
    click();
  }
  void key(char c) override {
    if (c == '\r' || c == '\n') click();
    else if ((c == 0x08 || c == 0x7F || c == 0x1B) && !busy()) requestClose();
  }
  void stepY(int dir) override {
    armed_ = false;
    if (ver_[0]) sel_ = dir > 0 ? 1 : 0;
  }
  void click() override {
    if (busy()) {
      dl::cancel();
      return;
    }
    if (phase_ != Phase::Ready || installed_) return;
    if (!ver_[0] || sel_ == 1) {
      check();
      return;
    }
    if (!armed_) {
      armed_ = true;
      return;
    }
    armed_ = false;
    char url[200];
    snprintf(url, sizeof(url), "%s%s", dl::kLatest, file_);
    bool ok;
    if (launcher_) {
      char p[128];
      snprintf(p, sizeof(p), "/sd/dualmesh/firmware/%s", file_);
      ok = dl::startFile(url, p, false, size_, sha_);  // SHA-256 tjekkes, før filen gemmes
    } else {
      ok = dl::startOta(url, uint32_t(size_), sha_);
    }
    if (ok) {
      mine_ = true;
      phase_ = Phase::Install;
      msg_[0] = 0;
    } else {
      char m[80] = "";
      dl::poll(m, sizeof(m));
      snprintf(msg_, sizeof(msg_), "%s", m);
    }
  }
  bool wantsX() const override { return false; }
  bool needsRedraw() const override { return true; }

  // "0.1.10" > "0.1.9" (tal for tal).
  static int cmp(const char* a, const char* b) {
    for (int i = 0; i < 4; ++i) {
      const long x = std::strtol(a, const_cast<char**>(&a), 10), y = std::strtol(b, const_cast<char**>(&b), 10);
      if (x != y) return x > y ? 1 : -1;
      if (*a == '.') ++a;
      if (*b == '.') ++b;
      if (!*a && !*b) break;
    }
    return 0;
  }

 private:
  enum class Phase : uint8_t { Ready, Check, Install };
  bool busy() const { return mine_ && dl::busy(); }

  void check() {
    ver_[0] = rev_[0] = file_[0] = sha_[0] = 0;
    size_ = 0;
    msg_[0] = 0;
    char url[160];
    snprintf(url, sizeof(url), "%sversion.txt", dl::kLatest);
    if (dl::startText(url)) {
      mine_ = true;
      phase_ = Phase::Check;
    } else {
      char m[80] = "";
      dl::poll(m, sizeof(m));
      snprintf(msg_, sizeof(msg_), "%s", m);
      phase_ = Phase::Ready;
    }
  }
  bool parse(const char* t) {
    while (*t) {
      char k[16], val[80];
      if (sscanf(t, "%15s %79s", k, val) == 2) {
        if (!std::strcmp(k, "version")) snprintf(ver_, sizeof(ver_), "%s", val);
        else if (!std::strcmp(k, "rev")) snprintf(rev_, sizeof(rev_), "%s", val);
        else if (!std::strcmp(k, "file")) snprintf(file_, sizeof(file_), "%s", val);
        else if (!std::strcmp(k, "size")) size_ = std::strtoull(val, nullptr, 10);
        else if (!std::strcmp(k, "sha256")) snprintf(sha_, sizeof(sha_), "%s", val);
      }
      const char* nl = std::strchr(t, '\n');
      if (!nl) break;
      t = nl + 1;
    }
    const bool ok = ver_[0] && file_[0] && size_ && std::strlen(sha_) == 64 && !std::strchr(file_, '/');
    if (!ok) ver_[0] = 0;
    return ok;
  }

  bool launcher_ = false;
  RestartFn restart_ = nullptr;
  Phase phase_ = Phase::Ready;
  bool mine_ = false, armed_ = false, installed_ = false;
  int sel_ = 0;
  char ver_[24] = "", rev_[16] = "", file_[64] = "", sha_[72] = "";
  uint64_t size_ = 0;
  char msg_[80] = "";
};

}  // namespace dm
