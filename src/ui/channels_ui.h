// SPDX-License-Identifier: MIT
// DualMesh – fanen "Kanal": vores kanaler (MeshCore og Meshtastic) med ulæste
// beskeder, tilføjelse og sletning. (Hørte kanaler/scanning fjernet 17. nov 2026.)
//
//   Række 1: filter. Står der "(Enter for filter)", vælges det med klik/Enter;
//            derefter skifter venstre/højre mellem < Alle > / < MeshCore > /
//            < Meshtastic >, og klik/Enter afslutter. Ellers skifter venstre/højre
//            fane som overalt.
//   Række 2-12: vores kanaler (grøn, gul ved ulæste [n]) og
//            "+ Tilføj MeshCore-/Meshtastic-kanal".
//   Række 13: detaljer / spørgsmål.  Række 14: hjælp eller indtastning.
//   Klik/Enter: vores kanal = åbn i Chat; "+ Tilføj" = skriv navn (og nøgle). Tom
//   Backspace fortryder. Backspace to gange på en kanal = slet (ikke Public/primær).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dm {

class ChannelScan;
class McNode;
class MsgStore;
class StatusDisplay;

class ChannelsUi {
 public:
  enum Filter : uint8_t { All, OnlyMc, OnlyMt, FilterCount };
  // net 0 = MeshCore (idx = kanalindeks i McNode), 1 = Meshtastic (idx i ChannelScan).
  using OpenFn = void (*)(void* ctx, int net, int idx);
  // Kaldes efter en sletning, så Chat kan rette sit kanalindeks (net, slettet indeks).
  using RemovedFn = void (*)(void* ctx, int net, int idx);
  void setRemovedFn(RemovedFn f) { removed_ = f; }

  void begin(ChannelScan* scan, McNode* node, MsgStore* store, OpenFn open, void* ctx);
  void onEnter();
  // Hvilke net er slået til (Indst.). Et slået-fra net vises ikke (kanaler, hørte
  // kanaler og "+ Tilføj"); med kun ét net er der intet filter at vælge.
  void setNets(bool mc, bool mt) {
    mc_on_ = mc;
    mt_on_ = mt;
  }
  bool bothNets() const { return mc_on_ && mt_on_; }
  bool mtOn() const { return mt_on_; }  // fanen vælges: markøren på første kanal, intet filter i gang

  // Det, der skal vises (fælles for tekstvisningen og LVGL): filter, de synlige
  // rækker, detaljelinjen og hjælp/indtastning. rows = antal synlige rækker.
  static constexpr int kViewRows = 12;
  struct ViewRow {
    char net[3];        // "MC"/"MT" ("" for "+ Tilføj")
    char name[48];      // kanalnavn eller "+ Tilføj ...-kanal"
    char unread[8];     // "3" eller ""
    char pkts[8];       // antal hørte pakker eller ""
    char age[8];        // tid siden sidst hørt eller ""
    uint16_t fg;        // tekstfarve (RGB565)
    bool add, sel, confirm;
  };
  struct View {
    char filter[64];
    bool filter_sel, filter_edit, single_net;
    int nrows;
    ViewRow rows[kViewRows];
    char detail[96];
    uint16_t detail_fg;
    bool input;          // der skrives (navn eller nøgle)
    char input_text[64]; // det skrevne (eller hjælpeteksten)
  };
  void view(View& v, int rows);
  // Tryk fra LVGL: filteret eller række nr. r af de synlige.
  void tapFilter();
  void tapRow(int r);
  void key(char c);
  // Navnefeltet under "+ Tilføj" (specialtegn tilladt); nøglen er base64 og har ingen.
  bool textField(char*& buf, size_t& cap) {
    if (input_step_ != 1) return false;
    buf = in_name_;
    cap = sizeof(in_name_);
    return true;
  }
  void stepY(int dir);
  // Navigation (22. nov 2026): op fra filteret går til fanelinjen; inputLen > 0 = en holdt
  // Backspace må slette i navne-/nøglefeltet.
  bool atTop() const { return sel_ < 0 && !filter_edit_ && !input_step_; }
  bool inputActive() const { return input_step_ != 0; }
  size_t inputLen() const { return !input_step_ ? 0 : strlen(input_step_ == 1 ? in_name_ : in_key_); }
  bool wantsX() const { return filter_edit_; }
  void stepX(int dir);
  void click();
  bool needsRedraw() const;

 private:
  enum ItemType : uint8_t { McJoined, MtJoined, AddMc, AddMt };
  struct Item {
    ItemType type;
    int idx;  // McJoined: kanalindeks, MtJoined: MT-indeks
  };
  static constexpr int kMaxItems = 40 + 8 + 2;
  void removeSelected();
  RemovedFn removed_ = nullptr;
  int collect(Item* out, int cap) const;
  void cycle(int dir);
  void note(const char* fmt, ...);
  void activate(const Item& it);
  void finishInput();

  ChannelScan* scan_ = nullptr;
  McNode* node_ = nullptr;
  MsgStore* store_ = nullptr;
  OpenFn open_ = nullptr;
  void* ctx_ = nullptr;

  Filter filter_ = All;
  bool mc_on_ = true, mt_on_ = true;
  bool filter_edit_ = false;
  int sel_ = 0;  // -1 = filteret, ellers position i listen
  int top_ = 0;
  int view_rows_ = 11;  // synlige rækker i den sidste visning (til tryk)
  uint32_t drawn_version_ = 0, drawn_store_ = 0;
  bool dirty_ = true;

  // Bekræft sletning (indeks i listen, -1 = ingen).
  int confirm_ = -1;
  // Indtastning af ny kanal: 0 = ingen, 1 = navn, 2 = nøgle.
  uint8_t input_step_ = 0;
  uint8_t input_net_ = 0;
  char in_name_[32] = "";
  char in_key_[48] = "";
  char note_[48] = "";
  uint32_t note_ms_ = 0;
};

}  // namespace dm
