// SPDX-License-Identifier: MIT
#include "ui/colors_ui.h"

#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "net/meshcore_node.h"
#include "ui/contact_colors.h"
#include "ui/status_display.h"

namespace dm {

void ColorsUi::onOpen() {
  if (!list_) list_ = static_cast<ContactInfo*>(heap_caps_calloc(kMax, sizeof(ContactInfo), MALLOC_CAP_SPIRAM));
  n_ = (list_ && node_) ? node_->contactsSnapshot(list_, kMax) : 0;
  sel_ = top_ = 0;
}

void ColorsUi::view(PanelView& v, int rows) {
  v.clear();
  snprintf(v.status, sizeof(v.status), "Kontaktfarver");
  snprintf(v.status2, sizeof(v.status2), "Nyeste kontakter først. Farven følger nøglen.");
  if (!n_) v.add(PanelRow::Info, color::Gray, "Ingen kontakter endnu.");
  if (rows > PanelView::kMaxRows) rows = PanelView::kMaxRows;
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + rows) top_ = sel_ - rows + 1;
  v.scroll_up = top_ > 0;
  v.scroll_down = top_ + rows < n_;
  for (int r = 0; r < rows && top_ + r < n_; ++r) {
    const int i = top_ + r;
    const ContactInfo& c = list_[i];
    const int idx = ccolor::forKey(c.id.pub_key);
    PanelRow* w = v.add(PanelRow::Item, ccolor::rgb(idx), c.name, ccolor::rgb(idx), "%s%s",
                        i == sel_ ? "< " : "", ccolor::name(idx));
    if (!w) break;
    if (ccolor::custom(c.id.pub_key)) snprintf(w->value + strlen(w->value), sizeof(w->value) - strlen(w->value), "%s",
                                               i == sel_ ? " >  valgt" : "  valgt");
    else if (i == sel_) snprintf(w->value + strlen(w->value), sizeof(w->value) - strlen(w->value), " >");
    w->sel = i == sel_;
  }
  snprintf(v.note, sizeof(v.note), "Ny identitet = ny farve. '!' i Chat = samme navn 2 gange.");
  snprintf(v.hint, sizeof(v.hint), "Venstre/højre: farve   Backspace: auto   Klik: tilbage");
  return;
}

void ColorsUi::tapRow(int i) {
  if (i < 0 || top_ + i >= n_) return;
  sel_ = top_ + i;  // tryk vælger; farven skiftes med trackballen
}

void ColorsUi::stepY(int dir) {
  sel_ += dir;
  if (sel_ < 0) sel_ = 0;
  if (sel_ >= n_) sel_ = n_ ? n_ - 1 : 0;
}

void ColorsUi::stepX(int dir) {
  if (sel_ >= n_) return;
  const uint8_t* k = list_[sel_].id.pub_key;
  ccolor::set(k, (ccolor::forKey(k) + dir + ccolor::kCount) % ccolor::kCount);
}

void ColorsUi::key(char c) {
  if (c == 0x08 || c == 0x7F) {
    if (sel_ < n_ && ccolor::custom(list_[sel_].id.pub_key)) ccolor::set(list_[sel_].id.pub_key, -1);
    else requestClose();
  } else if (c == '\r' || c == '\n') {
    requestClose();
  }
}

}  // namespace dm
