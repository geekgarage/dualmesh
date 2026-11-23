// SPDX-License-Identifier: MIT
#include "ui/perms_ui.h"

#include <cstdio>

#include "esp_heap_caps.h"
#include "net/meshcore_node.h"
#include "storage/sd_log.h"
#include "ui/contact_colors.h"
#include "ui/status_display.h"

namespace dm {

namespace {
// Tilladelserne ligger i flag-bit 1-3 (bit 0 = favorit). Trin: -, T, TP, TPM.
uint8_t level(uint8_t flags) {
  const uint8_t p = (flags >> 1) & 7;
  if (!(p & 1)) return 0;
  if (!(p & 2)) return 1;
  return (p & 4) ? 3 : 2;
}
const char* levelText(uint8_t l) {
  static const char* t[] = {"-", "T", "TP", "TPM"};
  return t[l & 3];
}
uint8_t flagsFor(uint8_t flags, uint8_t l) {
  static const uint8_t bits[] = {0, 1, 3, 7};
  return uint8_t((flags & ~0x0E) | (bits[l & 3] << 1));
}
}  // namespace

void PermsUi::onOpen() {
  if (!list_) list_ = static_cast<ContactInfo*>(heap_caps_calloc(kMax, sizeof(ContactInfo), MALLOC_CAP_SPIRAM));
  n_ = (list_ && node_) ? node_->contactsSnapshot(list_, kMax) : 0;
  sel_ = top_ = 0;
}

void PermsUi::view(PanelView& v, int rows) {
  last_ms_ = millis();
  v.clear();
  snprintf(v.status, sizeof(v.status), "Telemetri-tilladelser pr. kontakt");
  snprintf(v.status2, sizeof(v.status2), "Bruges, når et valg i MeshCore står på \"kontakter\".");
  if (!n_) v.add(PanelRow::Info, color::Gray, "Ingen kontakter endnu.");
  if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
  v.scroll_up = top_ > 0;
  v.scroll_down = top_ + rows < n_;
  for (int r = 0; r < rows && top_ + r < n_; ++r) {
    const int i = top_ + r;
    const ContactInfo& c = list_[i];
    const uint8_t l = level(c.flags);
    PanelRow* w = v.add(PanelRow::Item, ccolor::rgb(ccolor::forKey(c.id.pub_key)), c.name,
                        l ? color::White : color::Gray, i == sel_ ? "< %s >" : "%s", levelText(l));
    if (w) w->sel = i == sel_;
  }
  snprintf(v.note, sizeof(v.note), "T = telemetri (batteri, temp.)   P = position   M = miljø");
  snprintf(v.hint, sizeof(v.hint), "Venstre/højre: skift   Klik: tilbage");
  return;
}

void PermsUi::tapRow(int i) {
  if (i < 0 || top_ + i >= n_) return;
  if (sel_ == top_ + i) stepX(1);  // tryk igen på den valgte = næste trin
  sel_ = top_ + i;
}

void PermsUi::stepY(int dir) {
  sel_ += dir;
  if (sel_ < 0) sel_ = 0;
  if (sel_ >= n_) sel_ = n_ ? n_ - 1 : 0;
}

void PermsUi::stepX(int dir) {
  if (sel_ >= n_ || !node_) return;
  ContactInfo& snap = list_[sel_];
  ContactInfo* c = node_->lookupContactByPubKey(snap.id.pub_key, PUB_KEY_SIZE);
  if (!c) return;
  const uint8_t l = uint8_t((level(c->flags) + 4 + dir) % 4);
  c->flags = flagsFor(c->flags, l);
  snap.flags = c->flags;
  node_->markContactsDirty();
  DLOG.printf("[MC] telemetri-tilladelse for \"%s\": %s\n", c->name, levelText(l));
}

void PermsUi::key(char c) {
  if (c == 0x08 || c == 0x7F || c == '\r' || c == '\n') requestClose();
}

}  // namespace dm
