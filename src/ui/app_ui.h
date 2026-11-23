// SPDX-License-Identifier: MIT
// DualMesh – simpelt tekst-UI (fase 2 trin 3), erstattes af LVGL i fase 4.
//
// Faner: Kanal | Kontakt | Indst. Status ligger øverst i Indst. og åbnes som underskærm.
// Navigation i tre lag (22. nov 2026): fanelinjen -> listen -> chatten.
//   Fanelinjen (markør på fanen): venstre/højre vælger fane, ned/klik/Enter går ind i listen.
//   Listen: op/ned vælger; op over øverste række = fanelinjen (venstre hopper ikke dertil;
//   i Indst. folder venstre den åbne gruppe sammen); højre/klik/Enter = ind (Kontakt/Kanal:
//   åbn chatten; Indst.: som klik).
//   Chatten (én kanal eller DM, åbnet fra Kontakt eller Kanal): venstre eller Backspace i et
//   tomt felt = tilbage til listen; skrevet, ikke sendt tekst gemmes som kladde pr. samtale.
//   Klik i chatten: hop til nyeste (står man der: tandhjulsmenuen).
//   Ulæste: [n] foran kontakter/kanaler, '*' efter fanenavnet.
//   Tastatur: begynd at skrive. Enter sender, Backspace sletter, tom Enter annullerer.
//   Specialtegn: efter en tast med varianter (fx a -> å æ á ...) vises de over
//   indtastningen; trackball venstre/højre bladrer (tegnet skiftes med det samme),
//   tryk på et tegn vælger det, næste tast/klik bekræfter.
//   Touch: tryk på fanerne øverst skifter skærm.
// Kører i loop()-tasken.
#pragma once
#include <cstdint>

#include "net/meshcore_node.h"
#include "net/meshcore_radio.h"
#include "net/msg_store.h"
#include "net/mt_node.h"
#include "ui/channels_ui.h"
#include "ui/input.h"
#include "ui/keymap.h"
#include "map/caches.h"
#include "map/map_points.h"
#include "map/track.h"
#include "map/map_view.h"
#include "ui/panel.h"
#include "ui/settings_ui.h"
#include "ui/status_display.h"

namespace dm {

class AppUi {
 public:
  using StatusViewFn = void (*)(PanelView& v);  // Status som LVGL-rækker (alle; panelet ruller)
  struct Settings {
    uint8_t sensitivity = 5;  // 1..10 (spec: standard 5)
    bool invert_x = false, invert_y = false;
    uint16_t click_delay_ms = 150;
    uint8_t deadzone = 3;  // fanebytte kræver så mange skridt sidelæns (1..10)
    uint8_t map_sensitivity = 5;  // kortet: pixel pr. trackball-skridt = 4 x følsomhed + 4 (1..10)
    uint8_t kb_backlight = 127;           // tastaturlys ved aktivitet (0 = aldrig)
    uint16_t kb_backlight_timeout_s = 10; // slukker efter så lang tids ro
    uint16_t screen_timeout_s = 20;       // skærmen slukker efter så lang tids ro (0 = aldrig)
    uint8_t screen_brightness = 16;       // 1..16
    bool screen_timeout_on_usb = false;   // også når USB er tilsluttet en PC
    bool (*usb_connected)() = nullptr;    // nullptr = regnes som batteri
  };

  void begin(StatusDisplay* d, McNode* node, McRadio* radio, MsgStore* store, Keyboard* kb,
             Trackball* tb, StatusViewFn status_view, const Settings& s, KeyMap* km = nullptr,
             Touch* touch = nullptr);
  void tick();  // kald ofte fra loop()
  // Nye værdier fra indstillingsmenuen (virker med det samme).
  void applySettings(const Settings& s);
  const Settings& settings() const { return set_; }
  void setSettingsUi(SettingsUi* s) { menu_ = s; }
  void setChannelsUi(ChannelsUi* c) { chan_ = c; }
  void setChannelScan(ChannelScan* s) { scan_ = s; }
  void setMtNode(MtNode* m) { mt_ = m; }
  // Kortet: fliserne og markørerne (egen position først, derefter noder med position).
  // points = gemte prikker (delte lokationer), som markers også viser.
  void setMap(MapView* m, MapMarkersFn markers, MapPoints* points = nullptr, TrackLog* track = nullptr) {
    map_ = m;
    markers_ = markers;
    points_ = points;
    track_ = track;
  }
  void setTracksPanel(Panel* p) { tracks_panel_ = p; }
  // Kaldes, når skærmen tændes (true) eller slukkes (false); main.cpp skifter CPU-fart.
  void setPowerFn(void (*f)(bool screen_on)) { power_fn_ = f; }
  bool screenIsOff() const { return screen_off_; }
  // Kort besked (som efter afsendelse), fx når en hentning i baggrunden er færdig.
  void notify(const char* msg);
  void setMapsPanel(Panel* p) { maps_panel_ = p; }
  void setCaches(Caches* c, Panel* list) {
    caches_ = c;
    caches_panel_ = list;
  }
  // Luk evt. underskærm og vis kortet centreret her (kortet følger ikke GPS bagefter).
  void showOnMap(double lat, double lon);
  void openMtDm(uint32_t num, bool show = true);
  // Åbn en samtale i Chat. net 0 = MeshCore-kanal idx, 1 = Meshtastic-kanal idx.
  void openChannel(int net, int idx, bool show = true);
  void openDm(const ContactInfo& c, bool show = true);
  static void openChannelCb(void* ctx, int net, int idx) {
    static_cast<AppUi*>(ctx)->openChannel(net, idx);
  }
  // En kanal er slettet: står Chat på den, skiftes til Public/primær; ellers rettes indekset,
  // så beskeder ikke sendes til en anden kanal, når listen rykker op.
  static void channelRemovedCb(void* ctx, int net, int idx) { static_cast<AppUi*>(ctx)->channelRemoved(net, idx); }
  void channelRemoved(int net, int idx);
  // En kontakt er slettet: står Chat på DM'en med den, skiftes til Public/primær.
  void contactRemoved();
  void deleteContact(int i);
  int del_sel_ = -1;       // kontakt, der venter på bekræftelse (anden Backspace)
  uint32_t del_ms_ = 0;
  bool typing() const { return in_len_ > 0 || typing_; }
  // Åbn en underskærm (fra Indst.); nullptr lukker den.
  void openPanel(Panel* p);
  void openStatus() { openPanel(&status_panel_); }
  // Ny firmware fundet (tjekket ved WiFi-forbindelse): popup over alt med "Opdatér nu" (kalder
  // open, fx åbn Opdatér firmware) og "Senere". Vises én gang pr. version pr. opstart.
  using UpdateOpenFn = void (*)();
  void showUpdateNotice(const char* ver, UpdateOpenFn open);
  // SOS: kaldes med trackball-klikkets rå tilstand (holdes nede); se sos_*.
  using SosFn = bool (*)();          // er SOS aktiv?
  using SosSetFn = void (*)(bool on);
  void setSos(SosFn active, SosSetFn set) {
    sos_active_ = active;
    sos_set_ = set;
  }
  bool screenOn() const { return !screen_off_; }
  // Opstartslogoet står på skærmen, til denne tid (millis) er nået; brugerfladen tegner først derefter.
  void holdSplashUntil(uint32_t ms) { splash_until_ = ms ? ms : 1; }
  // Testbeskeden (main.cpp): kun i en DM eller privat kanal, slås til fra tandhjulsmenuen.
  struct TestFns {
    bool (*on)(const char* conv) = nullptr;      // kører den i denne samtale?
    int (*toggle)(const char* conv) = nullptr;   // 1 til, 0 fra, -1 ikke tilladt
    bool (*priv)(const char* conv) = nullptr;    // DM eller privat kanal?
  };
  void setTestFns(const TestFns& f) { test_ = f; }
  // Til feltloggen: længste tegning (AppUi::draw) og LVGL-opdatering (lv_timer_handler)
  // siden sidst, i µs; nulstilles. screen = aktiv fane/underskærm.
  void takeUiStats(uint32_t& draw_us, uint32_t& lv_us, const char*& screen);

 private:
  // Fanerne er ScrChannels..ScrSettings i den rækkefølge (Kanal | Kontakt | Kort | Indst.).
  enum Screen : uint8_t { ScrStatus, ScrChat, ScrChannels, ScrContacts, ScrMap, ScrSettings, ScrCount };
  // Kortet (fase 7).
  MapView* map_ = nullptr;
  MapMarkersFn markers_ = nullptr;
  bool map_follow_ = false;  // centrér på egen position, når den flytter sig
  // Delte lokationer (ønske 22. nov 2026): kortets menu deler sigtekornets punkt i en samtale
  // (tekst "📍 bredde, længde" i indtastningen), evt. også som prik; modtagne gemmes kun, når
  // man vælger det (tryk på boblen).
  MapPoints* points_ = nullptr;
  TrackLog* track_ = nullptr;  // spor: optagelse og et vist, gemt spor
  uint32_t track_seen_ = 0;
  bool share_pending_ = false;  // vælg samtale: teksten lægges i indtastningen, når Chat åbnes
  bool share_save_ = false;     // gem prikken, når beskeden sendes
  double share_lat_ = 0, share_lon_ = 0;
  double loc_lat_ = 0, loc_lon_ = 0;  // lokationen i den boble, man trykkede på
  void openMapMenu();
  // Et panel er åbent på den aktuelle fane (Indst. eller Kort).
  bool panelOn() const { return panel_ && (scr_ == ScrSettings || scr_ == ScrMap); }
  Panel* tracks_panel_ = nullptr;  // "Gemte spor" (åbnes fra kortets menu)
  Caches* caches_ = nullptr;       // geocaches (prikker og navigation på kortet)
  Panel* caches_panel_ = nullptr;  // "Geocaches" (kortets menu)
  Panel* maps_panel_ = nullptr;    // "Kortpakker" (kortets menu)
  void (*power_fn_)(bool screen_on) = nullptr;
  uint32_t caches_seen_ = 0;
  bool mapTarget(double& lat, double& lon);  // sigtekornet (eller GPS, når kortet følger)
  int pointNearCenter();                      // gemt prik under sigtekornet (-1 = ingen)
  void openLocMenu(double lat, double lon);   // tryk på en boble med en lokation
  void handleMapKey(char c);
  void mapCenterSelf();
  void lvDrawMap();
  static constexpr Screen kFirstTab = ScrChannels;
  void switchTo(Screen s);

  void handleKey(char c);
  void handleTrackball(const TrackballEvent& e);
  void startTyping(bool dm);
  void send();
  void draw(bool force);
  void refreshContacts();
  void handleTap(int x, int y);
  void showVariants(char key, bool ext = false);
  bool extField(char*& buf, size_t& cap);  // tekstfelt i Indst./Kanal med specialtegn
  void applyVariant(int idx);  // -1 = grundtegnet
  void activity();  // tast/trackball/touch: tænd tastaturlyset
  void screenWake(const char* why);
  void screenSleep();
  bool overlayOn();
  // Ny brugerflade (LVGL, app_ui_lv.cpp). lv_owner_ = LVGL ejer skærmen lige nu.
  struct LvChat;
  LvChat* lvc_ = nullptr;
  uint32_t splash_until_ = 0;  // 0 = intet logo at vente på
  uint32_t max_draw_us_ = 0, max_lv_us_ = 0;  // feltloggen (takeUiStats)
  static constexpr int kPanelRows = 7;  // listerækker i en underskærm med datamodel
  bool lv_panel_ = false;  // LVGL viser panelets datamodel (pview_)
  PanelView pview_;
  // Emoji-vælger (brugerens ønske 16. nov 2026): Mic-tasten eller ikonet foran
  // indtastningen; trackball vælger, klik indsætter (vælgeren bliver åben).
  static constexpr char kKeyEmoji = 0x1E;
  static constexpr int kEmoCols = 10, kEmoRows = 4;
  bool emo_open_ = false;
  int emo_sel_ = 0;
  void emojiToggle();
  void emojiInsert(int i);   // i = plads i vælgerens rækkefølge
  int emojiCount() const;
  void mentionInsert(const char* name);  // "@[navn] " i indtastningen (MeshCore-omtale)
  // Tandhjulsmenuer (17. nov 2026): afsenderens funktioner (tandhjul i en andens boble, eller
  // foran navnet i en DM) og kanalens funktioner (tandhjul foran kanalnavnet). Kun de punkter,
  // der kan bruges for netop den afsender/kanal, vises.
  enum MenuAct : uint8_t {
    MaDm, MaMention, MaTrace, MaReqPos, MaReqTelem, MaReqNode, MaTest, MaKey, MaClear, MaClearYes,
    MaLocShare, MaLocShareSave, MaLocSave, MaLocRemove, MaLocView, MaLocKeep, MaLocMyPos,
    MaLocClearAll, MaLocClearAllYes, MaTrackStart, MaTrackStop, MaTrackHide, MaTrackList, MaCaches, MaMaps, MaWaypoint
  };
  struct MenuItem {
    char label[40];
    MenuAct act;
  };
  static constexpr int kMenuMax = 12;
  bool menu_open_ = false;
  char menu_title_[44] = "";
  char menu_info_[120] = "";   // fx kanalens nøgle (vises under titlen)
  MenuItem menu_items_[kMenuMax] = {};
  int menu_n_ = 0, menu_sel_ = 0;
  char menu_name_[33] = "";    // afsenderens navn (til @-omtale)
  int menu_net_ = 0;           // 1 = MeshCore, 2 = Meshtastic
  uint32_t menu_mt_ = 0;       // Meshtastic-node (0 = ukendt)
  bool menu_mc_ok_ = false;    // MeshCore-kontakt fundet
  uint8_t menu_mc_pub_[PUB_KEY_SIZE] = {};
  bool menu_in_dm_ = false;
  TestFns test_{};
  void openUserMenu(const char* name, bool in_channel);
  void openChanMenu();
  void openTitleMenu();  // tandhjulet i titlen: kanalens menu eller DM-partnerens
  void menuAdd(const char* label, MenuAct a);
  void menuRun();
  void menuClose() {
    menu_open_ = false;
    dirty_ = true;
  }
  void lvDrawMenu();
  bool lvMenuTap(int x, int y);
  bool gearAllowed() const;  // tandhjul foran titlen (en samtale er åben i Chat)
  int lvPanelTop() const;  // y, hvor underskærmens indhold starter (under fanelinjen)
  void lvDrawPanel();
  int lvPanelRowAt(int y) const;  // listerække under y (-1 = ingen)
  void lvBuildChat();
  void lvRebuildMsgs();
  void lvDrawChat(bool force);  // Chat og Kontakt (samme ramme)
  void lvDrawMsgs(bool force);
  void lvDrawContacts();
  void lvDrawBottom();
  void lvDrawChannels(const ChannelsUi::View& v);
  void lvDrawSettings();
  bool lvTap(int x, int y);

  StatusDisplay* d_ = nullptr;
  McNode* node_ = nullptr;
  McRadio* radio_ = nullptr;
  MsgStore* store_ = nullptr;
  Keyboard* kb_ = nullptr;
  Trackball* tb_ = nullptr;
  Settings set_{};
  SettingsUi* menu_ = nullptr;  // fanen "Indst."
  ChannelsUi* chan_ = nullptr;  // fanen "Kanal"
  ChannelScan* scan_ = nullptr;
  MtNode* mt_ = nullptr;  // Meshtastic-noder (vises i Kontakt efter MeshCore)
  uint32_t tgt_mt_ = 0;   // Meshtastic-DM: nodenummer
  uint32_t seen_mt_version_ = 0;
  // Kontakt-fanen: MeshCore-kontakter først, derefter Meshtastic-noder.
  int contactRows() const { return n_contacts_ + (mt_ ? mt_->count() : 0); }
  void openContact(int i);
  Panel* panel_ = nullptr;  // åben underskærm (vises under fanen Indst.)
  // Statusskærmen som underskærm i Indst. (rækkerne kommer fra main.cpp's statusView).
  class StatusPanel : public Panel {
   public:
    StatusViewFn vfn = nullptr;
    const char* title() const override { return "Status"; }
    void onOpen() override { top_ = 0; }
    // main.cpp fylder alle rækker; her vises `rows` fra top_ (op/ned ruller).
    void view(PanelView& v, int rows) override {
      last_ = millis();
      v.clear();
      if (vfn) vfn(v);
      const int n = v.nrows;
      if (top_ > n - rows) top_ = n > rows ? n - rows : 0;
      if (top_ < 0) top_ = 0;
      for (int i = 0; i < rows && top_ + i < n; ++i) v.rows[i] = v.rows[top_ + i];
      v.nrows = n - top_ < rows ? n - top_ : rows;
      v.scroll_up = top_ > 0;
      v.scroll_down = top_ + rows < n;
    }
    void key(char c) override {
      if (c == 0x08 || c == 0x7F || c == '\r' || c == '\n') requestClose();
    }
    void stepY(int dir) override { top_ += dir; }  // grænserne tjekkes i view()
    void click() override { requestClose(); }
    bool needsRedraw() const override { return millis() - last_ >= 1000; }

   private:
    uint32_t last_ = 0;
    int top_ = 0;
  };
  StatusPanel status_panel_;
  // SOS: hold trackball-klik i 5 s -> bekræft med Enter (enhver anden tast fortryder).
  SosFn sos_active_ = nullptr;
  SosSetFn sos_set_ = nullptr;
  uint32_t hold_ms_ = 0;    // klikket holdt nede siden (0 = ikke nede)
  bool hold_fired_ = false;
  bool sos_confirm_ = false;
  uint32_t sos_confirm_ms_ = 0;
  bool upd_notice_ = false;      // popup: ny firmware
  int upd_sel_ = 0;              // 0 = Opdatér nu, 1 = Senere
  char upd_ver_[24] = "";
  UpdateOpenFn upd_open_ = nullptr;
  void updNoticeView(PanelView& v);
  void updNoticeChoose(int sel);
  void sosHold(bool down);
  void sosConfirmView(PanelView& v);
  // Den åbne samtale (hvor skrevet tekst sendes hen).
  enum TargetKind : uint8_t { TgtNone, TgtMcChannel, TgtMcDm, TgtMtChannel, TgtMtDm };
  TargetKind tgt_ = TgtNone;
  int tgt_idx_ = 0;
  uint8_t tgt_pub_[PUB_KEY_SIZE] = {};
  char tgt_name_[32] = "";
  uint32_t last_unread_ = 0;  // samlet antal ulæste (vækker skærmen, når det stiger)

  Screen scr_ = ScrChannels;  // start i Kanal-listen (22. nov 2026); ScrChat er en underside
  // Navigation (22. nov 2026): fanelinjen -> liste -> chat.
  static constexpr int kTabs = 4;      // Kanal | Kontakt | Kort | Indst. (kFirstTab..ScrSettings)
  bool tab_focus_ = false;             // markøren står i fanelinjen
  Screen chat_parent_ = ScrChannels;   // listen, chatten blev åbnet fra (tilbage går dertil)
  void goBack();                       // chat -> listen
  // Kladder pr. samtale: tekst, der ikke er sendt, når man går ud af chatten.
  struct Draft {
    char conv[20];
    char text[168];
  };
  static constexpr int kDrafts = 6;
  Draft drafts_[kDrafts] = {};
  int draft_next_ = 0;
  void draftSave();  // gem feltet for den åbne samtale (tomt felt sletter kladden) og tøm det
  void draftLoad();  // hent den åbne samtales kladde (kun hvis feltet er tomt)
  int acc_x_ = 0, acc_y_ = 0;  // trackball-pulser mod næste skridt
  uint8_t lock_axis_ = 0;      // 0 = ingen, 1 = vandret, 2 = lodret
  uint32_t lock_ms_ = 0, last_x_ms_ = 0;
  void lockAxis(uint8_t axis);
  int chat_scroll_ = 0;        // linjer op fra bunden
  // Touch: start, sidste punkt, om det er et træk, opsamlede pixels og om berøringen vækkede
  // skærmen. Et "trin" i chatten er 20 px (samme som trackballen, kScrollStepPx i app_ui_lv).
  static constexpr int kDragStartPx = 10, kChatStepPx = 20;
  int td_x_ = 0, td_y_ = 0, td_last_y_ = 0, td_last_x_ = 0, td_acc_ = 0;
  bool td_drag_ = false, td_wake_ = false;
  int sel_ = 0;                // valgt kontakt
  uint32_t seen_version_ = 0, drawn_version_ = 0, unread_ = 0;
  uint32_t last_draw_ms_ = 0, last_kb_ms_ = 0, last_contacts_ms_ = 0;
  bool dirty_ = true;

  // Indtastning
  bool typing_ = false, dm_ = false;
  char in_[168] = "";
  uint16_t in_len_ = 0;
  char dm_name_[32] = "";
  uint8_t dm_pub_[PUB_KEY_SIZE] = {};
  char note_[48] = "";  // kort besked i bunden (fx "sendt")
  uint32_t note_ms_ = 0;

  KeyMap* km_ = nullptr;
  Touch* touch_ = nullptr;
  uint32_t last_touch_ms_ = 0;
  uint32_t last_activity_ms_ = 0;
  bool kb_lit_ = false;
  bool screen_off_ = false, dimmed_ = false;
  uint32_t last_wake_ms_ = 0;  // seneste input eller modtagne besked
  bool may_sleep_ = false;     // forrige tick: måtte skærmen slukke?
  uint32_t heard_ = 0;         // modtagne kanalbeskeder + DM'er
  char ov_key_ = 0;                          // fysisk tast, varianterne hører til
  const char* ov_vars_[KeyMap::kMaxVariants] = {};
  bool ov_ext_ = false;                     // overlayet hører til et felt i Indst./Kanal
  int ov_n_ = 0, ov_sel_ = -1;               // -1 = grundtegnet

  static constexpr int kMaxContacts = 64;  // vises i UI'et (nyeste først)
  ContactInfo* contacts_ = nullptr;        // PSRAM
  int n_contacts_ = 0;
};

}  // namespace dm
