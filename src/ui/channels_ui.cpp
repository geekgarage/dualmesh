// SPDX-License-Identifier: MIT
#include "ui/channels_ui.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "net/channel_scan.h"
#include "net/meshcore_node.h"
#include "net/msg_store.h"
#include "ui/status_display.h"

namespace dm {

namespace {

const char* filterName(ChannelsUi::Filter f) {
  return f == ChannelsUi::OnlyMc ? "MeshCore" : f == ChannelsUi::OnlyMt ? "Meshtastic" : "Alle";
}
}  // namespace

void ChannelsUi::begin(ChannelScan* scan, McNode* node, MsgStore* store, OpenFn open, void* ctx) {
  scan_ = scan;
  node_ = node;
  store_ = store;
  open_ = open;
  ctx_ = ctx;
}

void ChannelsUi::onEnter() {
  sel_ = 0;
  top_ = 0;
  filter_edit_ = false;
  confirm_ = -1;
  dirty_ = true;
}

void ChannelsUi::note(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(note_, sizeof(note_), fmt, ap);
  va_end(ap);
  note_ms_ = millis();
  dirty_ = true;
}

int ChannelsUi::collect(Item* out, int cap) const {
  int n = 0;
  const bool mc = mc_on_ && filter_ != OnlyMt, mt = mt_on_ && filter_ != OnlyMc;
  if (mc && node_)
    for (int i = 0; i < node_->channelCount() && n < cap; ++i) out[n++] = {McJoined, i};
  if (mt && scan_)
    for (int i = 0; i < scan_->mtChannelCount() && n < cap; ++i) out[n++] = {MtJoined, i};
  if (mc && n < cap) out[n++] = {AddMc, 0};
  if (mt && n < cap) out[n++] = {AddMt, 0};
  return n;
}

bool ChannelsUi::needsRedraw() const {
  return dirty_ || (scan_ && scan_->version() != drawn_version_) ||
         (store_ && store_->version() != drawn_store_);
}

void ChannelsUi::view(View& v, int rows) {
  if (rows > kViewRows) rows = kViewRows;
  view_rows_ = rows;
  dirty_ = false;
  drawn_version_ = scan_ ? scan_->version() : 0;
  drawn_store_ = store_ ? store_->version() : 0;
  static Item items[kMaxItems];
  const int n = scan_ ? collect(items, kMaxItems) : 0;
  if (sel_ >= n) sel_ = n - 1;
  if (sel_ >= 0 && sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
  if (top_ > 0 && top_ > n - rows) top_ = n > rows ? n - rows : 0;

  v.single_net = !bothNets();
  if (v.single_net)  // kun ét net slået til: intet filter
    snprintf(v.filter, sizeof(v.filter), "Kun %s (%s er slået fra)", mc_on_ ? "MeshCore" : "Meshtastic",
             mc_on_ ? "Meshtastic" : "MeshCore");
  else if (filter_edit_) snprintf(v.filter, sizeof(v.filter), "Vis: < %s >", filterName(filter_));
  else snprintf(v.filter, sizeof(v.filter), "Vis: %s  (Enter for filter)", filterName(filter_));
  v.filter_sel = sel_ < 0;
  v.filter_edit = filter_edit_;

  const uint32_t now = millis();
  v.nrows = 0;
  for (int r = 0; r < rows; ++r) {
    const int k = top_ + r;
    if (k >= n) break;
    const Item& it = items[k];
    ViewRow& w = v.rows[v.nrows++];
    w = ViewRow{};
    w.sel = k == sel_;
    w.confirm = k == confirm_;
    w.fg = color::Green;
    snprintf(w.net, sizeof(w.net), "MC");
    if (it.type == McJoined) {
      ChannelDetails cd;
      node_->channelAt(it.idx, cd);
      snprintf(w.name, sizeof(w.name), "%s", cd.name);
      char conv[20];
      McNode::channelConvId(cd.channel, conv, sizeof(conv));
      const uint32_t u = store_ ? store_->unread(conv) : 0;
      if (u) {
        snprintf(w.unread, sizeof(w.unread), "%lu", (unsigned long)(u > 999 ? 999 : u));
        w.fg = color::Yellow;
      }
    } else if (it.type == MtJoined) {
      const MtChannel& mc = scan_->mtChannel(it.idx);
      snprintf(w.net, sizeof(w.net), "MT");
      snprintf(w.name, sizeof(w.name), "%s%s", mc.name, it.idx == 0 ? " (primær)" : "");
      const uint32_t u = store_ ? store_->unread(mc.conv) : 0;
      if (u) {
        snprintf(w.unread, sizeof(w.unread), "%lu", (unsigned long)(u > 999 ? 999 : u));
        w.fg = color::Yellow;
      }
    } else {
      w.add = true;
      w.net[0] = 0;
      snprintf(w.name, sizeof(w.name), "+ Tilføj %s-kanal", it.type == AddMc ? "MeshCore" : "Meshtastic");
      w.fg = color::Gold;
      continue;
    }
  }

  // Detaljer: spørgsmål, note eller om den valgte.
  v.detail_fg = color::Gray;
  if (input_step_) {
    v.detail_fg = color::White;
    if (input_step_ == 1)
      snprintf(v.detail, sizeof(v.detail), "%s",
               input_net_ == 0 ? "MC-kanal: #navn (hashtag) eller navn" : "MT-kanalnavn (1-11 tegn):");
    else
      snprintf(v.detail, sizeof(v.detail), "%s",
               input_net_ == 0 ? "Nøgle base64 (tom = ny tilfældig):" : "PSK base64 (tom = standard AQ==):");
  } else if (note_[0] && now - note_ms_ < 6000) {
    v.detail_fg = color::Green;
    snprintf(v.detail, sizeof(v.detail), "%s", note_);
  } else if (sel_ >= 0 && sel_ < n && confirm_ == sel_) {
    v.detail_fg = color::Red;
    snprintf(v.detail, sizeof(v.detail), "Slet kanalen? Backspace igen = ja");
  } else if (sel_ >= 0 && sel_ < n && (items[sel_].type == McJoined || items[sel_].type == MtJoined)) {
    if (items[sel_].idx == 0)
      snprintf(v.detail, sizeof(v.detail), "%s",
               items[sel_].type == McJoined ? "Klik/Enter: åbn (Public kan ikke slettes)"
                                            : "Klik/Enter: åbn (primær: se Indst.)");
    else
      snprintf(v.detail, sizeof(v.detail), "Klik/Enter: åbn  Backspace 2x: slet");
  } else {
    snprintf(v.detail, sizeof(v.detail), "Grøn = vores, [n] = nye beskeder");
  }

  // Indtastning eller hjælp.
  v.input = input_step_ != 0;
  if (input_step_) {
    const char* s = input_step_ == 1 ? in_name_ : in_key_;
    const size_t len = strlen(s);
    const char* tail = len > 36 ? s + (len - 36) : s;
    while (tail > s && (uint8_t(*tail) & 0xC0) == 0x80) ++tail;
    snprintf(v.input_text, sizeof(v.input_text), "%s", tail);
  } else if (filter_edit_) {
    snprintf(v.input_text, sizeof(v.input_text), "Venstre/højre: vælg  Klik/Enter: færdig");
  } else {
    snprintf(v.input_text, sizeof(v.input_text), "Op/ned: vælg  Enter: åbn  Bksp 2x: slet");
  }
}

void ChannelsUi::cycle(int dir) {
  filter_ = Filter((filter_ + FilterCount + dir) % FilterCount);
  top_ = 0;
  dirty_ = true;
}

void ChannelsUi::finishInput() {
  const char* res = nullptr;
  if (input_net_ == 0) {
    const int idx = node_ ? node_->addUserChannel(in_name_, in_key_, &res) : -1;
    if (idx > 0) {
      ChannelDetails cd;
      node_->channelAt(idx, cd);
      // Ny tilfældig nøgle: vis den, så den kan deles med de andre på kanalen.
      if (!in_key_[0] && cd.name[0] != '#') {
        char b64[48];
        McNode::channelKeyB64(cd.channel, b64, sizeof(b64));
        note("Nøgle: %s", b64);
      } else {
        note("%s", res);
      }
    } else {
      note("%s", res ? res : "Fejl");
    }
  } else {
    scan_->addMtChannel(in_name_, in_key_, &res);
    note("%s", res ? res : "Fejl");
  }
  input_step_ = 0;
  dirty_ = true;
}

void ChannelsUi::key(char c) {
  dirty_ = true;
  if (input_step_) {
    char* buf = input_step_ == 1 ? in_name_ : in_key_;
    const size_t cap = input_step_ == 1 ? sizeof(in_name_) : sizeof(in_key_);
    size_t n = strlen(buf);
    if (c == '\r' || c == '\n') {
      if (input_step_ == 1) {
        if (!n) return;
        // Hashtag-kanal (MC) eller tom nøgle mulig: spring nøglen over for "#navn".
        if (input_net_ == 0 && buf[0] == '#') return finishInput();
        input_step_ = 2;
        in_key_[0] = 0;
      } else {
        finishInput();
      }
    } else if (c == 0x08 || c == 0x7F) {
      while (n && (uint8_t(buf[n - 1]) & 0xC0) == 0x80) --n;  // hele tegnet (UTF-8)
      if (n) buf[n - 1] = 0;
      else if (input_step_ == 2) input_step_ = 1;  // tilbage til navnet
      else input_step_ = 0;                        // fortryd
    } else if (c >= 0x20 && c < 0x7F && n + 1 < cap && (input_step_ == 2 || n < 31)) {
      buf[n] = c;
      buf[n + 1] = 0;
    }
    return;
  }
  if (c == '\r' || c == '\n') click();
  else if (c == 0x08 || c == 0x7F) removeSelected();
}

// Backspace på en kanal: første gang spørges, anden gang slettes (Public og Meshtastics
// primære kanal kan ikke slettes). Beskederne i samtalen bliver liggende på SD.
void ChannelsUi::removeSelected() {
  static Item items[kMaxItems];
  const int n = collect(items, kMaxItems);
  if (sel_ < 0 || sel_ >= n) return;
  const Item it = items[sel_];
  if ((it.type != McJoined && it.type != MtJoined) || it.idx == 0) {
    note("%s", it.idx == 0 && it.type == McJoined   ? "Public kan ikke slettes"
               : it.idx == 0 && it.type == MtJoined ? "Primær kanal: skift i Indst."
                                                    : "Vælg en kanal");
    return;
  }
  if (confirm_ != sel_) {
    confirm_ = sel_;
    return;
  }
  confirm_ = -1;
  char name[40] = "";
  bool ok = false;
  if (it.type == McJoined && node_) {
    ChannelDetails cd;
    if (node_->channelAt(it.idx, cd)) snprintf(name, sizeof(name), "%s", cd.name);
    ok = node_->removeChannel(it.idx);
  } else if (it.type == MtJoined && scan_) {
    snprintf(name, sizeof(name), "%s", scan_->mtChannel(it.idx).name);
    ok = scan_->removeMtChannel(it.idx);
  }
  if (ok && removed_) removed_(ctx_, it.type == McJoined ? 0 : 1, it.idx);
  note(ok ? "Slettet: %s" : "Kunne ikke slette %s", name);
  if (sel_ > 0) --sel_;
}

void ChannelsUi::stepY(int dir) {
  if (input_step_ || filter_edit_) return;
  static Item items[kMaxItems];
  const int n = collect(items, kMaxItems);
  sel_ += dir;
  if (sel_ < -1) sel_ = -1;
  if (sel_ >= n) sel_ = n - 1;
  confirm_ = -1;
  dirty_ = true;
}

void ChannelsUi::stepX(int dir) {
  if (filter_edit_) cycle(dir);
}

void ChannelsUi::activate(const Item& it) {
  if (it.type == McJoined || it.type == MtJoined) {
    if (open_) open_(ctx_, it.type == McJoined ? 0 : 1, it.idx);
  } else if (it.type == AddMc || it.type == AddMt) {
    input_step_ = 1;
    input_net_ = it.type == AddMc ? 0 : 1;
    in_name_[0] = in_key_[0] = 0;
  }
}

void ChannelsUi::click() {
  dirty_ = true;
  if (input_step_) return key('\n');
  if (sel_ < 0) {
    filter_edit_ = bothNets() && !filter_edit_;
    return;
  }
  static Item items[kMaxItems];
  const int n = collect(items, kMaxItems);
  if (sel_ < n) activate(items[sel_]);
}

void ChannelsUi::tapFilter() {
  dirty_ = true;
  if (input_step_) return;
  sel_ = -1;
  if (filter_edit_) cycle(1);
  else filter_edit_ = bothNets();
}

void ChannelsUi::tapRow(int r) {
  dirty_ = true;
  if (input_step_ || r < 0 || r >= view_rows_) return;
  static Item items[kMaxItems];
  const int n = collect(items, kMaxItems);
  const int k = top_ + r;
  if (k >= n) return;
  filter_edit_ = false;
  if (k != sel_) {
    sel_ = k;
    confirm_ = -1;
    return;
  }
  activate(items[k]);
}

}  // namespace dm
