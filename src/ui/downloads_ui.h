// SPDX-License-Identifier: MIT
// DualMesh – "Hent fra GitHub": tastaturlayouts (Indst. > Tastatur), sprog (Indst. > System) og
// kortpakker (Indst. > Kort). Kataloget hentes, når panelet åbnes (<mappe>/index.txt i repoet);
// hver linje er én fil:
//   keyboards/index.txt, lang/index.txt:  <id> <version> <navn ...>
//   maps/index.txt:                       <fil> <byte> <zoom> <url> <navn ...>
// Klik = hent (kortpakker: to klik, de er store; en afbrudt hentning genoptages). Klik på en
// installeret = "Hent igen" eller "Slet" (to klik). Hentningen fortsætter, hvis panelet lukkes.
// Installerede versioner står i <mappe på SD>/installed.txt ("<id> <version>").
#pragma once
#include <Arduino.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "net/dl.h"
#include "ui/panel.h"

namespace dm {

class DownloadsUi : public Panel {
 public:
  enum class Kind : uint8_t { Keyboards, Languages, Maps };
  // Kaldes efter en installation eller sletning (fx genindlæs layouts/sprog, find kortpakker).
  using ChangedFn = void (*)(Kind k, const char* id, bool installed);
  using BuiltinFn = bool (*)(const char* id);

  void begin(Kind k, ChangedFn changed, BuiltinFn builtin = nullptr) {
    kind_ = k;
    changed_ = changed;
    builtin_ = builtin;
  }
  const char* title() const override {
    return kind_ == Kind::Keyboards ? "Hent tastaturlayouts" : kind_ == Kind::Languages ? "Hent sprog" : "Hent kortpakker";
  }

  void onOpen() override {
    sel_ = top_ = 0;
    act_ = -1;
    armed_ = false;
    if (dl::busy()) {  // en hentning kører stadig (vores vises i listen)
      if (owner() != this) snprintf(msg_, sizeof(msg_), "En anden hentning kører; prøv igen bagefter");
      return;
    }
    loadIndex();
  }

  // Fra loop() hele tiden (også når panelet er lukket): fang slutningen på vores hentning.
  void tick() {
    if (owner() != this) return;
    char msg[80];
    const dl::State s = dl::poll(msg, sizeof(msg));
    if (s != dl::State::Done && s != dl::State::Failed) return;
    owner() = nullptr;
    if (phase_ == Phase::Index) {
      if (s == dl::State::Done) parse(dl::text());
      else snprintf(msg_, sizeof(msg_), "Kataloget kunne ikke hentes: %.40s", msg);
      phase_ = Phase::List;
      return;
    }
    phase_ = Phase::List;
    snprintf(msg_, sizeof(msg_), "%.30s: %.40s", job_name_, msg);
    if (s == dl::State::Done && job_ >= 0 && job_ < n_) {
      setInstalled(items_[job_].id, items_[job_].ver);
      if (changed_) changed_(kind_, items_[job_].id, true);
    }
    job_ = -1;
  }

  void view(PanelView& v, int rows) override {
    v.clear();
    if (phase_ == Phase::Index) {
      snprintf(v.status, sizeof(v.status), "Henter kataloget fra GitHub ...");
      snprintf(v.hint, sizeof(v.hint), "Backspace: tilbage");
      return;
    }
    // Igangværende hentning.
    const bool running = dl::busy() && owner() == this && job_ >= 0;
    if (running) {
      uint64_t done, total;
      uint32_t bps;
      dl::progress(done, total, bps);
      snprintf(v.status, sizeof(v.status), "Henter %.40s", job_name_);
      if (total) {
        const unsigned pct = unsigned(done * 100 / total);
        const unsigned long left = bps ? (unsigned long)((total - done) / bps) : 0;
        if (left >= 120)
          snprintf(v.status2, sizeof(v.status2), "%u %%  %lu KB/s  ca. %lu min tilbage", pct, (unsigned long)(bps / 1024), left / 60);
        else
          snprintf(v.status2, sizeof(v.status2), "%u %%  %lu KB/s", pct, (unsigned long)(bps / 1024));
      } else {
        snprintf(v.status2, sizeof(v.status2), "%lu KB", (unsigned long)(done / 1024));
      }
      PanelRow* r = v.add(PanelRow::Item, color::Red, "Afbryd hentningen");
      if (r) r->sel = true;
      snprintf(v.hint, sizeof(v.hint), "Klik: afbryd   Backspace: tilbage (hentningen fortsætter)");
      return;
    }
    if (act_ >= 0 && act_ < n_) {  // valg for en installeret
      const Item& it = items_[act_];
      snprintf(v.status, sizeof(v.status), "%.60s", it.name);
      snprintf(v.status2, sizeof(v.status2), "%s", stateText(act_));
      PanelRow* a = v.add(PanelRow::Item, color::White, "Hent igen");
      if (a) a->sel = asel_ == 0;
      PanelRow* b = v.add(PanelRow::Item, color::Red, armed_ ? "Bekræft: slet" : "Slet fra SD-kortet");
      if (b) {
        b->sel = asel_ == 1;
        b->confirm = armed_;
      }
      snprintf(v.hint, sizeof(v.hint), "Klik: vælg   Backspace: tilbage");
      return;
    }
    if (msg_[0]) snprintf(v.status, sizeof(v.status), "%s", msg_);
    else snprintf(v.status, sizeof(v.status), "%d i kataloget på GitHub", n_);
    if (kind_ == Kind::Maps) {
      const uint64_t fri = dl::sdFree();
      if (fri) snprintf(v.status2, sizeof(v.status2), "%lu MB fri på SD-kortet", (unsigned long)(fri >> 20));
    }
    if (!n_) {
      v.add(PanelRow::Info, color::Gray, "Kataloget er tomt.");
    }
    if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
    v.scroll_up = top_ > 0;
    v.scroll_down = top_ + rows < n_;
    for (int r = 0; r < rows && top_ + r < n_; ++r) {
      const int i = top_ + r;
      PanelRow* w = v.add(PanelRow::Item, color::White, items_[i].name, stateColor(i), "%s", stateText(i));
      if (w) {
        w->sel = i == sel_;
        w->confirm = armed_ && i == sel_;
      }
    }
    if (kind_ == Kind::Maps) snprintf(v.note, sizeof(v.note), "© OpenStreetMap-bidragydere (ODbL)");
    if (armed_ && sel_ < n_)
      snprintf(v.note, sizeof(v.note), "Klik igen: hent %lu MB (brug WiFi, ikke mobildata)",
               (unsigned long)((items_[sel_].bytes + 524288u) >> 20));
    snprintf(v.hint, sizeof(v.hint), "Klik: hent   Backspace: tilbage");
  }

  void tapRow(int i) override {
    if (dl::busy() && owner() == this) {
      if (i == 0) click();
      return;
    }
    if (act_ >= 0) {
      if (i == 0 || i == 1) {
        asel_ = i;
        click();
      }
      return;
    }
    if (i < 0 || top_ + i >= n_) return;
    if (sel_ != top_ + i) armed_ = false;
    sel_ = top_ + i;
    click();
  }
  void key(char c) override {
    if (c == '\r' || c == '\n') {
      click();
    } else if (c == 0x08 || c == 0x7F || c == 0x1B) {
      if (act_ >= 0) act_ = -1;
      else requestClose();
      armed_ = false;
    }
  }
  void stepY(int dir) override {
    armed_ = false;
    if (dl::busy() && owner() == this) return;
    if (act_ >= 0) {
      asel_ = dir > 0 ? 1 : 0;
      return;
    }
    sel_ += dir;
    if (sel_ < 0) sel_ = 0;
    if (sel_ >= n_) sel_ = n_ ? n_ - 1 : 0;
  }
  void click() override {
    if (dl::busy()) {
      if (owner() == this) dl::cancel();
      else snprintf(msg_, sizeof(msg_), "En anden hentning kører allerede");
      return;
    }
    if (phase_ != Phase::List || !n_) return;
    if (act_ >= 0) {
      if (asel_ == 0) {
        start(act_);
        act_ = -1;
      } else if (!armed_) {
        armed_ = true;
      } else {
        remove(act_);
        act_ = -1;
        armed_ = false;
      }
      return;
    }
    if (sel_ >= n_) return;
    if (onSd(sel_) == 2) {  // installeret: valg
      act_ = sel_;
      asel_ = 0;
      armed_ = false;
      return;
    }
    // Store filer (kortpakker) kræver et ekstra klik.
    if (items_[sel_].bytes > 20u * 1024 * 1024 && !armed_) {
      armed_ = true;
      return;
    }
    armed_ = false;
    start(sel_);
  }
  bool needsRedraw() const override { return true; }

 private:
  enum class Phase : uint8_t { Index, List };
  struct Item {
    char id[32];      // id (layouts/sprog) eller filnavn (kortpakker)
    char ver[16];
    char name[48];
    char zoom[8];
    char url[200];
    uint64_t bytes;
  };
  static constexpr int kMax = 32;

  static DownloadsUi*& owner() {
    static DownloadsUi* o = nullptr;
    return o;
  }
  const char* dir() const {
    return kind_ == Kind::Keyboards ? "keyboards" : kind_ == Kind::Languages ? "lang" : "maps";
  }
  const char* ext() const { return kind_ == Kind::Keyboards ? ".kbd" : kind_ == Kind::Languages ? ".txt" : ""; }
  void sdPath(int i, char* out, size_t n) const {
    snprintf(out, n, "/sd/dualmesh/%s/%s%s", dir(), items_[i].id, ext());
  }

  void loadIndex() {
    msg_[0] = 0;
    n_ = 0;
    char url[160];
    snprintf(url, sizeof(url), "%s%s/index.txt", dl::kRaw, dir());
    phase_ = Phase::Index;
    if (dl::startText(url)) {
      owner() = this;
    } else {
      char m[80] = "";
      dl::poll(m, sizeof(m));
      snprintf(msg_, sizeof(msg_), "%s", m[0] ? m : "Kataloget kunne ikke hentes");
      phase_ = Phase::List;
    }
  }

  void parse(const char* t) {
    if (!items_) items_ = static_cast<Item*>(heap_caps_calloc(kMax, sizeof(Item), MALLOC_CAP_SPIRAM));
    n_ = 0;
    if (!items_) return;
    while (*t && n_ < kMax) {
      const char* eol = std::strchr(t, '\n');
      const size_t len = eol ? size_t(eol - t) : std::strlen(t);
      char line[320];
      snprintf(line, sizeof(line), "%.*s", int(len < sizeof(line) - 1 ? len : sizeof(line) - 1), t);
      t += len + (eol ? 1 : 0);
      char* cr = std::strchr(line, '\r');
      if (cr) *cr = 0;
      if (!line[0] || line[0] == '#') continue;
      Item& it = items_[n_];
      it = Item{};
      int pos = 0;
      if (kind_ == Kind::Maps) {
        unsigned long long b = 0;
        if (sscanf(line, "%31s %llu %7s %199s %n", it.id, &b, it.zoom, it.url, &pos) < 4) continue;
        it.bytes = b;
      } else {
        if (sscanf(line, "%31s %15s %n", it.id, it.ver, &pos) < 2) continue;
        snprintf(it.url, sizeof(it.url), "%s%s/%s%s", dl::kRaw, dir(), it.id, ext());
      }
      snprintf(it.name, sizeof(it.name), "%s", pos ? line + pos : it.id);
      if (!it.name[0]) snprintf(it.name, sizeof(it.name), "%s", it.id);
      if (std::strchr(it.id, '/') || std::strstr(it.id, "..")) continue;  // kun filnavne
      ++n_;
    }
  }

  // 0 = ikke på SD, 1 = delvis hentet (.part), 2 = installeret.
  int onSd(int i) const {
    char p[128];
    sdPath(i, p, sizeof(p));
    struct stat st;
    if (stat(p, &st) == 0) return (kind_ != Kind::Maps || items_[i].bytes == 0 || uint64_t(st.st_size) == items_[i].bytes) ? 2 : 1;
    char pp[136];
    snprintf(pp, sizeof(pp), "%s.part", p);
    return stat(pp, &st) == 0 ? 1 : 0;
  }
  uint64_t partBytes(int i) const {
    char p[136];
    sdPath(i, p, sizeof(p) - 6);
    std::strcat(p, ".part");
    struct stat st;
    return stat(p, &st) == 0 ? uint64_t(st.st_size) : 0;
  }

  const char* stateText(int i) {
    static char s[40];
    const int sd = onSd(i);
    if (kind_ == Kind::Maps) {
      const unsigned long mb = (unsigned long)((items_[i].bytes + 524288u) >> 20);
      if (sd == 2) snprintf(s, sizeof(s), "installeret, %lu MB", mb);
      else if (sd == 1 && items_[i].bytes)
        snprintf(s, sizeof(s), "%u %% hentet", unsigned(partBytes(i) * 100 / items_[i].bytes));
      else snprintf(s, sizeof(s), "z%s  %lu MB", items_[i].zoom, mb);
      return s;
    }
    char have[16] = "";
    if (sd == 2) {
      installedVer(items_[i].id, have, sizeof(have));
      if (have[0] && std::strcmp(have, items_[i].ver) != 0) snprintf(s, sizeof(s), "ny version");
      else snprintf(s, sizeof(s), "installeret");
    } else if (builtin_ && builtin_(items_[i].id)) {
      snprintf(s, sizeof(s), "indbygget");
    } else {
      snprintf(s, sizeof(s), "kan hentes");
    }
    return s;
  }
  uint16_t stateColor(int i) {
    const int sd = onSd(i);
    if (sd == 2) return color::Green;
    if (sd == 1) return color::Yellow;
    return color::Gray;
  }

  void start(int i) {
    char p[128];
    sdPath(i, p, sizeof(p));
    snprintf(job_name_, sizeof(job_name_), "%s", items_[i].name);
    const bool big = kind_ == Kind::Maps;
    if (dl::startFile(items_[i].url, p, big, items_[i].bytes)) {
      owner() = this;
      job_ = i;
      msg_[0] = 0;
    } else {
      char m[80] = "";
      dl::poll(m, sizeof(m));
      snprintf(msg_, sizeof(msg_), "%s", m[0] ? m : "Kunne ikke starte hentningen");
    }
  }

  void remove(int i) {
    char p[136];
    sdPath(i, p, sizeof(p));
    ::remove(p);
    std::strcat(p, ".part");
    ::remove(p);
    setInstalled(items_[i].id, nullptr);
    snprintf(msg_, sizeof(msg_), "%.40s er slettet", items_[i].name);
    if (changed_) changed_(kind_, items_[i].id, false);
  }

  // installed.txt: "<id> <version>" pr. linje.
  void installedVer(const char* id, char* out, size_t n) const {
    out[0] = 0;
    char p[96], line[96], vid[40], ver[24];
    snprintf(p, sizeof(p), "/sd/dualmesh/%s/installed.txt", dir());
    FILE* f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f))
      if (sscanf(line, "%39s %23s", vid, ver) == 2 && !std::strcmp(vid, id)) snprintf(out, n, "%s", ver);
    fclose(f);
  }
  void setInstalled(const char* id, const char* ver) {
    char p[96], tmp[100], line[96], vid[40];
    snprintf(p, sizeof(p), "/sd/dualmesh/%s/installed.txt", dir());
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE* in = fopen(p, "r");
    FILE* out = fopen(tmp, "w");
    if (!out) {
      if (in) fclose(in);
      return;
    }
    if (in) {
      while (fgets(line, sizeof(line), in))
        if (sscanf(line, "%39s", vid) == 1 && std::strcmp(vid, id) != 0) fputs(line, out);
      fclose(in);
    }
    if (ver && ver[0]) fprintf(out, "%s %s\n", id, ver);
    fclose(out);
    ::remove(p);
    rename(tmp, p);
  }

  Kind kind_ = Kind::Keyboards;
  ChangedFn changed_ = nullptr;
  BuiltinFn builtin_ = nullptr;
  Phase phase_ = Phase::List;
  Item* items_ = nullptr;
  int n_ = 0, sel_ = 0, top_ = 0, act_ = -1, asel_ = 0, job_ = -1;
  bool armed_ = false;
  char msg_[80] = "";
  char job_name_[48] = "";
};

}  // namespace dm
