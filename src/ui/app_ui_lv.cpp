// SPDX-License-Identifier: MIT
// DualMesh – den nye brugerflade (fase 4, LVGL). AppUi beholder al logik (input,
// faner, skrivning, afsendelse, scroll, skærmslukning); her tegnes skærmene med LVGL.
// Skærme, der endnu ikke er flyttet, tegnes stadig af den gamle tekstvisning.
#include <cstdio>
#include <cmath>
#include <cstring>

#include <lvgl.h>

#include "geo_text.h"
#include "i18n.h"
#include "ui/app_ui.h"
#include "ui/contact_colors.h"
#include "ui/lv/lv_port.h"
#include "util/wall_clock.h"

namespace dm {

// Fra app_ui.cpp (fælles for begge visninger).
uint32_t chatMsgTs(const Msg& m, uint32_t now_ts);
int chatDayKey(uint32_t ts);
void chatDayText(uint32_t ts, uint32_t now_ts, char* out, size_t n);
const char* contactTypeName(uint8_t t);
void contactAge(char* out, size_t n, uint32_t secs);

namespace {

constexpr int kW = 320;
constexpr int kTabH = 22;
constexpr int kScrollStepPx = 20;  // én trackball-"linje" i chatten
constexpr int kMaxMsgs = 60;       // beskeder bygget som objekter (resten: gamle)
constexpr int kCRows = 8, kCRowH = 20;  // synlige kontaktrækker (genbruges ved scroll)
constexpr int kKRows = 8, kKRowH = 19;  // synlige kanalrækker
constexpr int kSRows = 7, kSRowH = 18;  // synlige rækker i Indst. (14 px skrift)
constexpr int kPTitleH = 18;             // underskærmens titelbjælke

lv_color_t c565(uint16_t v) {
  return lv_color_make(uint8_t(((v >> 11) & 0x1F) * 255 / 31), uint8_t(((v >> 5) & 0x3F) * 255 / 63),
                       uint8_t((v & 0x1F) * 255 / 31));
}

// Sæt kun teksten, hvis den er ændret (ellers tegner LVGL objektet igen).
// Brugerfladens tekster oversættes her, når de tegnes (dansk = nøglen, se lib/dualmesh/src/i18n.h).
// Brugerindhold (beskedtekst, afsendernavne) sættes direkte med lv_label_set_text og oversættes ikke.
void setText(lv_obj_t* l, const char* t) {
  char buf[400];
  t = i18n::tr(t, buf, sizeof(buf));
  const char* cur = lv_label_get_text(l);
  if (!cur || std::strcmp(cur, t) != 0) lv_label_set_text(l, t);
}

void noStyle(lv_obj_t* o) {
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_pad_gap(o, 0, 0);  // temaets mellemrum mellem børn (gjorde listerne for høje)
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

uint32_t fnv(uint32_t h, const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}

}  // namespace

struct AppUi::LvChat {
  lv_obj_t* scr = nullptr;
  lv_obj_t* tabs = nullptr;
  lv_obj_t* tab[4] = {};
  lv_obj_t* title = nullptr;
  lv_obj_t* list = nullptr;
  lv_obj_t* varbar = nullptr;
  lv_obj_t* var[KeyMap::kMaxVariants] = {};
  int nvar = 0;
  lv_obj_t* input = nullptr;
  lv_obj_t* inrow = nullptr;   // ikon + indtastning
  lv_obj_t* emobtn = nullptr;  // 😀 foran indtastningen
  lv_obj_t* emo = nullptr;     // emoji-vælgeren over indtastningen
  lv_obj_t* emocell[AppUi::kEmoCols * AppUi::kEmoRows] = {};
  int emo_top = 0;             // første viste række
  char mnames[kMaxMsgs + 1][33] = {};  // afsendere i kanalen (tryk = @-omtale)
  lv_obj_t* gear[kMaxMsgs + 1] = {};   // tandhjulet i en andens boble (samme indeks som mnames)
  // Tandhjulsmenuen (svæver over chatten).
  lv_obj_t* menu = nullptr;
  lv_obj_t* mtitle = nullptr;
  lv_obj_t* minfo = nullptr;
  lv_obj_t* mitem[AppUi::kMenuMax] = {};
  uint32_t sig = 0;
  int scroll_shown = -1;
  // Kontakt-fanen: faste rækker, der fyldes med de synlige kontakter.
  lv_obj_t* clist = nullptr;
  lv_obj_t* crow[kCRows] = {};
  lv_obj_t* cbadge[kCRows] = {};
  lv_obj_t* cname[kCRows] = {};
  lv_obj_t* cinfo[kCRows] = {};
  lv_obj_t* cempty = nullptr;
  int ctop = 0;
  // Kanal-fanen (filteret står i titellinjen).
  lv_obj_t* klist = nullptr;
  lv_obj_t* krow[kKRows] = {};
  lv_obj_t* knet[kKRows] = {};
  lv_obj_t* kname[kKRows] = {};
  lv_obj_t* kunread[kKRows] = {};
  lv_obj_t* kinfo[kKRows] = {};
  lv_obj_t* kdetail = nullptr;
  // Indst. (menuen; underskærmene bruger endnu tekstvisningen).
  lv_obj_t* sp = nullptr;
  lv_obj_t* srow[kSRows] = {};
  lv_obj_t* slab[kSRows] = {};
  lv_obj_t* sval[kSRows] = {};
  lv_obj_t* shelp = nullptr;
  lv_obj_t* shint = nullptr;
  lv_obj_t* sbtn[2] = {};
  // Underskærm med datamodel (PanelView): titelbjælke, status, liste, note og hjælp/indtastning.
  lv_obj_t* pp = nullptr;
  lv_obj_t* ptb = nullptr;      // titelbjælken
  lv_obj_t* pside[3] = {};      // ved siden af en stor QR-kode: side, note og hint
  lv_obj_t* ptitle = nullptr;
  lv_obj_t* ptime = nullptr;
  lv_obj_t* pstat = nullptr;
  lv_obj_t* pstat2 = nullptr;
  lv_obj_t* plist = nullptr;
  lv_obj_t* prow[AppUi::kPanelRows] = {};
  lv_obj_t* plab[AppUi::kPanelRows] = {};
  lv_obj_t* pval[AppUi::kPanelRows] = {};
  lv_obj_t* pnote = nullptr;
  lv_obj_t* pbottom = nullptr;
  // Kortet (fase 7): op til 3 x 2 fliser, markører og to tekstlinjer.
  static constexpr int kMapImgs = 6, kMapMarks = 40;
  static constexpr int kTrkSeg = 6, kTrkPts = 160;  // spor: stykker à højst 160 punkter pr. spor
  lv_obj_t* tline[2][kTrkSeg] = {};
  lv_point_t tpts[2][kTrkSeg][kTrkPts] = {};
  static constexpr int kCacheMarks = 30;  // geocaches i udsnittet (målet altid med)
  lv_obj_t* cdot[kCacheMarks] = {};
  lv_obj_t* clab[kCacheMarks] = {};
  lv_obj_t* cnav = nullptr;  // linje fra egen position til målet
  lv_point_t cnav_pts[2] = {};
  lv_obj_t* harrow = nullptr;  // pil i kørselsretningen på egen position
  lv_point_t harrow_pts[4] = {};
  lv_obj_t* mp = nullptr;
  lv_obj_t* mimg[kMapImgs] = {};
  lv_img_dsc_t mdsc[kMapImgs] = {};
  lv_obj_t* mdot[kMapMarks] = {};
  lv_obj_t* mlab[kMapMarks] = {};
  lv_obj_t* mapinfo = nullptr;  // zoom og koordinater
  lv_obj_t* mattr = nullptr;   // "Kortdata © OpenStreetMap"
  lv_obj_t* mbtn[4] = {};      // touch-knapper: zoom ind, zoom ud, centrér på GPS, menu
  static constexpr int kLocs = 16;  // bobler med en lokation (tryk = vis/gem)
  lv_obj_t* locobj[kLocs] = {};
  double loclat[kLocs] = {}, loclon[kLocs] = {};
  int nlocs = 0;
  lv_obj_t* mcross[4] = {};    // sigtekorn midt på kortet, når det ikke følger GPS (4 streger)
  lv_obj_t* mempty = nullptr;  // ingen kort på SD
  lv_obj_t* pqr = nullptr;      // QR-kode (Panel::qrText): lille til højre for listen, stor til venstre
  int pqr_size = 0;
  char pqr_text[320] = "";      // Fejlrapportens issue-link er op til 287 tegn
};

void AppUi::lvBuildChat() {
  LvChat* c = new LvChat;
  lvc_ = c;
  c->scr = lv_obj_create(nullptr);
  noStyle(c->scr);
  lv_obj_set_style_bg_color(c->scr, lv_color_black(), 0);
  lv_obj_set_flex_flow(c->scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c->scr, 0, 0);

  // Fanelinjen (lyseblå er forbeholdt fanelinjen og valget). Aktiv fane: sort streg under.
  c->tabs = lv_obj_create(c->scr);
  noStyle(c->tabs);
  lv_obj_set_size(c->tabs, kW, kTabH);
  lv_obj_set_style_bg_color(c->tabs, c565(color::Cyan), 0);
  lv_obj_set_flex_flow(c->tabs, LV_FLEX_FLOW_ROW);
  for (int i = 0; i < AppUi::kTabs; ++i) {
    lv_obj_t* t = lv_label_create(c->tabs);
    lv_obj_set_flex_grow(t, 1);
    lv_obj_set_height(t, kTabH);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(t, lv_color_black(), 0);
    lv_obj_set_style_text_font(t, &dm_font_14, 0);
    lv_obj_set_style_pad_top(t, 0, 0);
    lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(t, lv_color_black(), 0);
    lv_obj_set_style_border_width(t, 3, 0);  // alle faner: samme tekstplacering
    c->tab[i] = t;
  }

  // Samtalens navn (eller en kort besked som "sendt").
  c->title = lv_label_create(c->scr);
  lv_obj_set_width(c->title, kW);
  lv_obj_set_style_bg_color(c->title, c565(0x2945), 0);
  lv_obj_set_style_bg_opa(c->title, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(c->title, 6, 0);
  lv_obj_set_style_pad_ver(c->title, 2, 0);
  lv_obj_set_style_text_font(c->title, &dm_font_14, 0);
  lv_label_set_long_mode(c->title, LV_LABEL_LONG_DOT);

  // Beskederne.
  c->list = lv_obj_create(c->scr);
  noStyle(c->list);
  lv_obj_add_flag(c->list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(c->list, kW);
  lv_obj_set_flex_grow(c->list, 1);
  lv_obj_set_style_bg_color(c->list, lv_color_black(), 0);
  lv_obj_set_style_pad_hor(c->list, 5, 0);
  lv_obj_set_style_pad_ver(c->list, 4, 0);
  lv_obj_set_style_pad_row(c->list, 5, 0);
  lv_obj_set_flex_flow(c->list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(c->list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(c->list, LV_SCROLLBAR_MODE_AUTO);

  // Kontakter (skjult, når Chat vises).
  c->clist = lv_obj_create(c->scr);
  noStyle(c->clist);
  lv_obj_set_width(c->clist, kW);
  lv_obj_set_flex_grow(c->clist, 1);
  lv_obj_set_style_bg_color(c->clist, lv_color_black(), 0);
  lv_obj_set_style_pad_ver(c->clist, 3, 0);
  lv_obj_set_flex_flow(c->clist, LV_FLEX_FLOW_COLUMN);
  for (int r = 0; r < kCRows; ++r) {
    lv_obj_t* row = lv_obj_create(c->clist);
    noStyle(row);
    lv_obj_set_size(row, kW, kCRowH);
    lv_obj_set_style_pad_hor(row, 5, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_style_bg_color(row, c565(color::Select), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c->cbadge[r] = lv_label_create(row);
    lv_obj_set_width(c->cbadge[r], 22);
    lv_obj_set_style_text_font(c->cbadge[r], &dm_font_12, 0);
    c->cname[r] = lv_label_create(row);
    lv_obj_set_flex_grow(c->cname[r], 1);
    lv_label_set_long_mode(c->cname[r], LV_LABEL_LONG_DOT);
    c->cinfo[r] = lv_label_create(row);
    lv_obj_set_style_text_font(c->cinfo[r], &dm_font_12, 0);
    lv_obj_set_style_text_align(c->cinfo[r], LV_TEXT_ALIGN_RIGHT, 0);
    c->crow[r] = row;
  }
  c->cempty = lv_label_create(c->clist);
  lv_obj_set_width(c->cempty, LV_PCT(100));
  lv_obj_set_style_text_color(c->cempty, c565(color::Gray), 0);
  lv_obj_set_style_text_align(c->cempty, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(c->cempty, 30, 0);
  setText(c->cempty, "Ingen kontakter endnu.\nVenter på adverts fra andre noder.");
  lv_obj_add_flag(c->clist, LV_OBJ_FLAG_HIDDEN);

  // Kanaler (skjult, når Chat/Kontakt vises).
  c->klist = lv_obj_create(c->scr);
  noStyle(c->klist);
  lv_obj_set_width(c->klist, kW);
  lv_obj_set_flex_grow(c->klist, 1);
  lv_obj_set_style_bg_color(c->klist, lv_color_black(), 0);
  lv_obj_set_style_pad_top(c->klist, 2, 0);
  lv_obj_set_flex_flow(c->klist, LV_FLEX_FLOW_COLUMN);
  for (int r = 0; r < kKRows; ++r) {
    lv_obj_t* row = lv_obj_create(c->klist);
    noStyle(row);
    lv_obj_set_size(row, kW, kKRowH);
    lv_obj_set_style_pad_hor(row, 5, 0);
    lv_obj_set_style_pad_column(row, 5, 0);
    lv_obj_set_style_bg_color(row, c565(color::Select), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c->knet[r] = lv_label_create(row);
    lv_obj_set_width(c->knet[r], 24);
    lv_obj_set_style_text_font(c->knet[r], &dm_font_12, 0);
    c->kname[r] = lv_label_create(row);
    lv_obj_set_flex_grow(c->kname[r], 1);
    lv_label_set_long_mode(c->kname[r], LV_LABEL_LONG_DOT);
    c->kunread[r] = lv_label_create(row);
    lv_obj_set_style_text_font(c->kunread[r], &dm_font_12, 0);
    c->kinfo[r] = lv_label_create(row);
    lv_obj_set_style_text_font(c->kinfo[r], &dm_font_12, 0);
    lv_obj_set_style_text_align(c->kinfo[r], LV_TEXT_ALIGN_RIGHT, 0);
    c->krow[r] = row;
  }
  c->kdetail = lv_label_create(c->scr);
  lv_obj_set_width(c->kdetail, kW);
  lv_obj_set_style_pad_hor(c->kdetail, 6, 0);
  lv_obj_set_style_pad_ver(c->kdetail, 1, 0);
  lv_obj_set_style_text_font(c->kdetail, &dm_font_12, 0);
  lv_label_set_long_mode(c->kdetail, LV_LABEL_LONG_DOT);
  lv_obj_add_flag(c->klist, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(c->kdetail, LV_OBJ_FLAG_HIDDEN);

  // Indst.: rækker, forklaring, hjælpelinje og knapperne Gem / Annuller.
  c->sp = lv_obj_create(c->scr);
  noStyle(c->sp);
  lv_obj_set_width(c->sp, kW);
  lv_obj_set_flex_grow(c->sp, 1);
  lv_obj_set_style_bg_color(c->sp, lv_color_black(), 0);
  lv_obj_set_flex_flow(c->sp, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_top(c->sp, 2, 0);
  // Rækkerne i en beholder med fast højde, så forklaring og knapper altid står samme
  // sted (tomme rækker er kun 6 px; uden beholderen hoppede bunden op og ned).
  lv_obj_t* rows = lv_obj_create(c->sp);
  noStyle(rows);
  lv_obj_set_size(rows, kW, kSRows * kSRowH);
  lv_obj_set_style_bg_opa(rows, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
  for (int r = 0; r < kSRows; ++r) {
    lv_obj_t* row = lv_obj_create(rows);
    noStyle(row);
    lv_obj_set_size(row, kW, kSRowH);
    lv_obj_set_style_pad_hor(row, 5, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c->slab[r] = lv_label_create(row);
    lv_obj_set_style_text_font(c->slab[r], &dm_font_14, 0);
    lv_obj_set_flex_grow(c->slab[r], 1);
    lv_label_set_long_mode(c->slab[r], LV_LABEL_LONG_DOT);
    c->sval[r] = lv_label_create(row);
    lv_obj_set_style_text_font(c->sval[r], &dm_font_14, 0);
    lv_obj_set_style_max_width(c->sval[r], 170, 0);
    lv_label_set_long_mode(c->sval[r], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(c->sval[r], LV_TEXT_ALIGN_RIGHT, 0);
    c->srow[r] = row;
  }
  c->shelp = lv_label_create(c->sp);
  lv_obj_set_size(c->shelp, kW, 50);  // 3 linjer + luft til hjælpelinjen
  lv_obj_set_style_pad_hor(c->shelp, 6, 0);
  lv_obj_set_style_pad_top(c->shelp, 3, 0);
  lv_obj_set_style_text_font(c->shelp, &dm_font_12, 0);
  lv_label_set_long_mode(c->shelp, LV_LABEL_LONG_WRAP);
  c->shint = lv_label_create(c->sp);
  lv_obj_set_width(c->shint, kW);
  lv_obj_set_style_pad_hor(c->shint, 6, 0);
  lv_obj_set_style_text_font(c->shint, &dm_font_12, 0);
  lv_label_set_long_mode(c->shint, LV_LABEL_LONG_DOT);
  lv_obj_t* bt = lv_obj_create(c->sp);
  noStyle(bt);
  lv_obj_set_size(bt, kW, 24);
  lv_obj_set_style_bg_opa(bt, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(bt, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bt, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int b = 0; b < 2; ++b) {
    lv_obj_t* l = lv_label_create(bt);
    lv_obj_set_width(l, 120);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, &dm_font_14, 0);
    lv_obj_set_style_radius(l, 6, 0);
    lv_obj_set_style_border_width(l, 1, 0);
    lv_obj_set_style_pad_ver(l, 1, 0);  // knappen (ca. 20 px) skal kunne være i rækken
    lv_obj_set_style_bg_color(l, c565(color::Select), 0);
    setText(l, b == 0 ? "Gem" : "Annuller");
    c->sbtn[b] = l;
  }
  lv_obj_add_flag(c->sp, LV_OBJ_FLAG_HIDDEN);

  // Underskærm med datamodel, i samme stil som Indst.
  c->pp = lv_obj_create(c->scr);
  noStyle(c->pp);
  lv_obj_set_width(c->pp, kW);
  lv_obj_set_flex_grow(c->pp, 1);
  lv_obj_set_style_bg_color(c->pp, lv_color_black(), 0);
  lv_obj_set_flex_flow(c->pp, LV_FLEX_FLOW_COLUMN);
  {
    lv_obj_t* tb = lv_obj_create(c->pp);  // titel til venstre, tid til højre
    c->ptb = tb;
    noStyle(tb);
    lv_obj_set_size(tb, kW, kPTitleH);
    lv_obj_set_style_bg_color(tb, c565(0x2945), 0);
    lv_obj_set_style_pad_hor(tb, 6, 0);
    lv_obj_set_flex_flow(tb, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tb, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c->ptitle = lv_label_create(tb);
    lv_obj_set_style_text_font(c->ptitle, &dm_font_14, 0);
    lv_obj_set_style_text_color(c->ptitle, lv_color_white(), 0);
    lv_obj_set_flex_grow(c->ptitle, 1);
    c->ptime = lv_label_create(tb);
    lv_obj_set_style_text_font(c->ptime, &dm_font_12, 0);
    lv_obj_set_style_text_color(c->ptime, c565(color::Gray), 0);
  }
  c->pstat = lv_label_create(c->pp);
  lv_obj_set_size(c->pstat, kW, 17);
  lv_obj_set_style_pad_hor(c->pstat, 6, 0);
  lv_obj_set_style_pad_top(c->pstat, 1, 0);
  lv_obj_set_style_text_font(c->pstat, &dm_font_14, 0);
  lv_label_set_long_mode(c->pstat, LV_LABEL_LONG_DOT);
  c->pstat2 = lv_label_create(c->pp);
  lv_obj_set_size(c->pstat2, kW, 16);
  lv_obj_set_style_pad_hor(c->pstat2, 6, 0);
  lv_obj_set_style_text_font(c->pstat2, &dm_font_12, 0);
  lv_obj_set_style_text_color(c->pstat2, c565(color::Gray), 0);
  lv_label_set_long_mode(c->pstat2, LV_LABEL_LONG_DOT);
  c->plist = lv_obj_create(c->pp);  // fast højde, så note og hjælp står samme sted
  noStyle(c->plist);
  lv_obj_set_size(c->plist, kW, AppUi::kPanelRows * kSRowH);
  lv_obj_set_style_bg_opa(c->plist, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(c->plist, LV_FLEX_FLOW_COLUMN);
  for (int r = 0; r < AppUi::kPanelRows; ++r) {
    lv_obj_t* row = lv_obj_create(c->plist);
    noStyle(row);
    lv_obj_set_size(row, kW, kSRowH);
    lv_obj_set_style_pad_hor(row, 5, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c->plab[r] = lv_label_create(row);
    lv_obj_set_style_text_font(c->plab[r], &dm_font_14, 0);
    lv_obj_set_flex_grow(c->plab[r], 1);
    lv_label_set_long_mode(c->plab[r], LV_LABEL_LONG_DOT);
    c->pval[r] = lv_label_create(row);
    lv_obj_set_style_text_font(c->pval[r], &dm_font_12, 0);
    lv_obj_set_style_max_width(c->pval[r], 150, 0);
    lv_label_set_long_mode(c->pval[r], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(c->pval[r], LV_TEXT_ALIGN_RIGHT, 0);
    c->prow[r] = row;
  }
  c->pnote = lv_label_create(c->pp);
  lv_obj_set_width(c->pnote, kW);
  lv_obj_set_style_pad_hor(c->pnote, 6, 0);
  lv_obj_set_style_pad_top(c->pnote, 2, 0);
  lv_obj_set_style_text_font(c->pnote, &dm_font_12, 0);
  lv_label_set_long_mode(c->pnote, LV_LABEL_LONG_DOT);
  c->pbottom = lv_label_create(c->pp);  // hjælp eller indtastning (gul)
  lv_obj_set_width(c->pbottom, kW);
  lv_obj_set_style_pad_hor(c->pbottom, 6, 0);
  lv_obj_set_style_pad_ver(c->pbottom, 3, 0);
  lv_obj_set_style_text_font(c->pbottom, &dm_font_12, 0);
  lv_label_set_long_mode(c->pbottom, LV_LABEL_LONG_DOT);
  lv_obj_set_style_bg_color(c->pbottom, c565(color::Yellow), 0);
  // Tekst til højre for en stor QR-kode (svæver; placeres i lvDrawPanel).
  for (lv_obj_t*& l : c->pside) {
    l = lv_label_create(c->pp);
    lv_obj_add_flag(l, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(l, &dm_font_12, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_add_flag(c->pp, LV_OBJ_FLAG_HIDDEN);

  // Kortet: et lærred under fanelinjen; fliser og markører placeres frit (ingen flex).
  c->mp = lv_obj_create(c->scr);
  noStyle(c->mp);
  lv_obj_set_width(c->mp, kW);
  lv_obj_set_flex_grow(c->mp, 1);
  lv_obj_set_style_bg_color(c->mp, c565(0xC618), 0);  // grå, hvor der ingen flise er
  lv_obj_set_style_bg_opa(c->mp, LV_OPA_COVER, 0);
  for (int i = 0; i < LvChat::kMapImgs; ++i) {
    lv_img_dsc_t& ds = c->mdsc[i];
    ds.header.cf = LV_IMG_CF_TRUE_COLOR;
    ds.header.always_zero = 0;
    ds.header.w = MapView::kTile;
    ds.header.h = MapView::kTile;
    ds.data_size = MapView::kTile * MapView::kTile * 2;
    ds.data = nullptr;
    lv_obj_t* im = lv_img_create(c->mp);
    lv_obj_add_flag(im, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(im, LV_OBJ_FLAG_HIDDEN);
    c->mimg[i] = im;
  }
  // Spor: linjer over fliserne og under prikkerne (0 = optagelsen, rød; 1 = vist spor, violet).
  for (int k = 0; k < 2; ++k)
    for (int s = 0; s < LvChat::kTrkSeg; ++s) {
      lv_obj_t* l = lv_line_create(c->mp);
      lv_obj_add_flag(l, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_set_pos(l, 0, 0);
      lv_obj_set_style_line_width(l, 3, 0);
      lv_obj_set_style_line_rounded(l, true, 0);
      lv_obj_set_style_line_color(l, lv_color_hex(k == 0 ? 0xE00000 : 0x8020C0), 0);
      lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
      c->tline[k][s] = l;
    }
  // Geocaches: navigationslinje og firkanter i typens farve (med kode ved høj zoom).
  c->cnav = lv_line_create(c->mp);
  lv_obj_add_flag(c->cnav, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_pos(c->cnav, 0, 0);
  lv_obj_set_style_line_width(c->cnav, 2, 0);
  lv_obj_set_style_line_color(c->cnav, lv_color_hex(0xFFB000), 0);
  lv_obj_set_style_line_dash_width(c->cnav, 6, 0);
  lv_obj_set_style_line_dash_gap(c->cnav, 4, 0);
  lv_obj_add_flag(c->cnav, LV_OBJ_FLAG_HIDDEN);
  c->harrow = lv_line_create(c->mp);
  lv_obj_add_flag(c->harrow, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_pos(c->harrow, 0, 0);
  lv_obj_set_style_line_width(c->harrow, 3, 0);
  lv_obj_set_style_line_rounded(c->harrow, true, 0);
  lv_obj_set_style_line_color(c->harrow, lv_color_hex(0x0060FF), 0);
  lv_obj_add_flag(c->harrow, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < LvChat::kCacheMarks; ++i) {
    lv_obj_t* d = lv_obj_create(c->mp);
    noStyle(d);
    lv_obj_add_flag(d, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(d, 11, 11);
    lv_obj_set_style_radius(d, 2, 0);
    lv_obj_set_style_border_width(d, 2, 0);
    lv_obj_set_style_border_color(d, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    c->cdot[i] = d;
    lv_obj_t* l = lv_label_create(c->mp);
    lv_obj_add_flag(l, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(l, &dm_font_12, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_obj_set_style_bg_color(l, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(l, 2, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    c->clab[i] = l;
  }
  for (int i = 0; i < LvChat::kMapMarks; ++i) {
    lv_obj_t* d = lv_obj_create(c->mp);
    noStyle(d);
    lv_obj_add_flag(d, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(d, 10, 10);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d, 2, 0);
    lv_obj_set_style_border_color(d, lv_color_white(), 0);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    c->mdot[i] = d;
    lv_obj_t* l = lv_label_create(c->mp);
    lv_obj_add_flag(l, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(l, &dm_font_12, 0);
    lv_obj_set_style_bg_color(l, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(l, 2, 0);
    lv_obj_set_style_radius(l, 3, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    c->mlab[i] = l;
  }
  auto bar = [&](lv_align_t al) {
    lv_obj_t* l = lv_label_create(c->mp);
    lv_obj_add_flag(l, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(l, &dm_font_12, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_bg_color(l, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(l, 4, 0);
    lv_obj_set_style_pad_ver(l, 1, 0);
    lv_obj_align(l, al, 0, 0);
    return l;
  };
  c->mapinfo = bar(LV_ALIGN_TOP_LEFT);
  c->mattr = bar(LV_ALIGN_BOTTOM_RIGHT);
  setText(c->mattr, "© OpenStreetMap-bidragydere");  // ODbL-kildeangivelse
  {
    static const char* const kBtn[4] = {"+", "-", LV_SYMBOL_GPS, LV_SYMBOL_LIST};
    for (int i = 0; i < 4; ++i) {
      lv_obj_t* b = lv_label_create(c->mp);
      lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_set_size(b, 36, 36);
      lv_obj_set_style_text_font(b, &dm_font_20, 0);
      lv_obj_set_style_text_color(b, lv_color_white(), 0);
      lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_pad_top(b, 7, 0);
      lv_obj_set_style_bg_color(b, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_60, 0);
      lv_obj_set_style_radius(b, 8, 0);
      lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -6, 22 + i * 44);
      lv_label_set_text(b, kBtn[i]);
      c->mbtn[i] = b;
    }
    // Sigtekorn (ønske 22. nov 2026: tyndt, så man kan sigte præcist): fire streger à 1 px med
    // et hul på 5 px om centrum; centrum = koordinaterne i infolinjen.
    {
      constexpr int kLen = 12, kGap = 2;
      const int cx = kW / 2, cy = (240 - kTabH) / 2;
      const int xs[4] = {cx - kGap - kLen, cx + kGap + 1, cx, cx};
      const int ys[4] = {cy, cy, cy - kGap - kLen, cy + kGap + 1};
      for (int i = 0; i < 4; ++i) {
        lv_obj_t* r = lv_obj_create(c->mp);
        noStyle(r);
        lv_obj_add_flag(r, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(r, i < 2 ? kLen : 1, i < 2 ? 1 : kLen);
        lv_obj_set_style_bg_color(r, lv_color_hex(0xD00000), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_pos(r, xs[i], ys[i]);
        lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
        c->mcross[i] = r;
      }
    }
  }
  c->mempty = lv_label_create(c->mp);
  lv_obj_add_flag(c->mempty, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_width(c->mempty, kW - 30);
  lv_obj_align(c->mempty, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_font(c->mempty, &dm_font_14, 0);
  lv_obj_set_style_text_color(c->mempty, lv_color_black(), 0);
  lv_label_set_long_mode(c->mempty, LV_LABEL_LONG_WRAP);
  setText(c->mempty,
                    "Ingen kort på SD-kortet.\n\nLæg en .pmtiles-fil i /dualmesh/maps/ (fx med Indst. > System > "
                    "SD som USB-drev). Kortpakker laves med tools/maps/make_map.py.");
  lv_obj_add_flag(c->mp, LV_OBJ_FLAG_HIDDEN);

  // Specialtegn: bjælke med varianterne; det valgte tegn er forstørret.
  c->varbar = lv_obj_create(c->scr);
  noStyle(c->varbar);
  lv_obj_set_size(c->varbar, kW, 34);
  lv_obj_set_style_bg_color(c->varbar, c565(color::Cyan), 0);
  lv_obj_set_flex_flow(c->varbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c->varbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_hor(c->varbar, 6, 0);
  lv_obj_set_style_pad_column(c->varbar, 2, 0);
  for (int i = 0; i < KeyMap::kMaxVariants; ++i) {
    lv_obj_t* v = lv_label_create(c->varbar);
    lv_obj_set_style_text_color(v, lv_color_black(), 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(v, 18);
    c->var[i] = v;
  }
  lv_obj_add_flag(c->varbar, LV_OBJ_FLAG_HIDDEN);

  // Emoji-vælgeren: 4 rækker à 10, lige over indtastningen (teksten kan stadig ses).
  c->emo = lv_obj_create(c->scr);
  noStyle(c->emo);
  lv_obj_set_size(c->emo, kW, AppUi::kEmoRows * 24 + 6);
  lv_obj_set_style_bg_color(c->emo, c565(0x2124), 0);
  lv_obj_set_style_bg_opa(c->emo, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(c->emo, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(c->emo, 1, 0);
  lv_obj_set_style_border_color(c->emo, c565(color::Gray), 0);
  lv_obj_set_style_pad_all(c->emo, 3, 0);
  lv_obj_set_flex_flow(c->emo, LV_FLEX_FLOW_ROW_WRAP);
  for (int i = 0; i < AppUi::kEmoCols * AppUi::kEmoRows; ++i) {
    lv_obj_t* l = lv_label_create(c->emo);
    lv_obj_set_size(l, 31, 24);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(l, 4, 0);
    lv_obj_set_style_radius(l, 5, 0);
    lv_obj_set_style_bg_color(l, c565(color::Select), 0);
    lv_label_set_text(l, "");
    c->emocell[i] = l;
  }
  lv_obj_add_flag(c->emo, LV_OBJ_FLAG_HIDDEN);

  // Indtastning (eller en grå hjælpelinje), med emoji-ikonet foran.
  c->inrow = lv_obj_create(c->scr);
  noStyle(c->inrow);
  lv_obj_set_size(c->inrow, kW, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(c->inrow, c565(0x18C3), 0);
  lv_obj_set_style_bg_opa(c->inrow, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(c->inrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c->inrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  c->emobtn = lv_label_create(c->inrow);
  lv_obj_set_size(c->emobtn, 26, 22);
  lv_obj_set_style_text_align(c->emobtn, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(c->emobtn, 3, 0);
  lv_obj_set_style_radius(c->emobtn, 5, 0);
  lv_obj_set_style_bg_color(c->emobtn, c565(color::Select), 0);
  lv_obj_set_style_pad_left(c->inrow, 3, 0);
  lv_label_set_text(c->emobtn, "\xF0\x9F\x98\x80");  // 😀
  c->input = lv_label_create(c->inrow);
  lv_obj_set_flex_grow(c->input, 1);
  lv_obj_set_style_pad_hor(c->input, 6, 0);
  lv_obj_set_style_pad_ver(c->input, 5, 0);
  lv_label_set_long_mode(c->input, LV_LABEL_LONG_WRAP);

  // Tandhjulsmenuen: en boks midt på skærmen over chatten (svæver uden for layoutet).
  c->menu = lv_obj_create(c->scr);
  noStyle(c->menu);
  lv_obj_add_flag(c->menu, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(c->menu, 280, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(c->menu, 240 - kTabH - 10, 0);  // mange punkter: menuen ruller
  lv_obj_align(c->menu, LV_ALIGN_TOP_MID, 0, kTabH + 6);
  lv_obj_set_style_bg_color(c->menu, c565(0x2124), 0);
  lv_obj_set_style_bg_opa(c->menu, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c->menu, 8, 0);
  lv_obj_set_style_border_width(c->menu, 1, 0);
  lv_obj_set_style_border_color(c->menu, c565(color::Gray), 0);
  lv_obj_set_style_pad_all(c->menu, 4, 0);
  lv_obj_set_style_pad_row(c->menu, 1, 0);
  lv_obj_set_flex_flow(c->menu, LV_FLEX_FLOW_COLUMN);
  c->mtitle = lv_label_create(c->menu);
  lv_obj_set_width(c->mtitle, LV_PCT(100));
  lv_obj_set_style_text_font(c->mtitle, &dm_font_14, 0);
  lv_obj_set_style_text_color(c->mtitle, c565(color::Yellow), 0);
  lv_obj_set_style_pad_hor(c->mtitle, 4, 0);
  lv_label_set_long_mode(c->mtitle, LV_LABEL_LONG_DOT);
  c->minfo = lv_label_create(c->menu);
  lv_obj_set_width(c->minfo, LV_PCT(100));
  lv_obj_set_style_text_font(c->minfo, &dm_font_12, 0);
  lv_obj_set_style_text_color(c->minfo, lv_color_white(), 0);
  lv_obj_set_style_pad_hor(c->minfo, 4, 0);
  lv_label_set_long_mode(c->minfo, LV_LABEL_LONG_WRAP);
  for (int i = 0; i < AppUi::kMenuMax; ++i) {
    lv_obj_t* l = lv_label_create(c->menu);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_font(l, &dm_font_14, 0);
    lv_obj_set_style_pad_hor(l, 6, 0);
    lv_obj_set_style_pad_ver(l, 2, 0);
    lv_obj_set_style_radius(l, 5, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    c->mitem[i] = l;
  }
  lv_obj_add_flag(c->menu, LV_OBJ_FLAG_HIDDEN);
}

void AppUi::lvDrawMenu() {
  LvChat* c = lvc_;
  if (!c || !c->menu) return;
  const bool on = menu_open_ && (scr_ == ScrChat || scr_ == ScrMap) && !lv_panel_;
  if (!on) {
    lv_obj_add_flag(c->menu, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(c->menu, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(c->menu);
  setText(c->mtitle, menu_title_);
  if (menu_info_[0]) {
    setText(c->minfo, menu_info_);
    lv_obj_clear_flag(c->minfo, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(c->minfo, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < kMenuMax; ++i) {
    lv_obj_t* l = c->mitem[i];
    if (i >= menu_n_) {
      lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN);
    setText(l, menu_items_[i].label);
    const bool sel = i == menu_sel_;  // valgbjælken er lyseblå (som overalt)
    lv_obj_set_style_bg_color(l, c565(color::Select), 0);
    lv_obj_set_style_bg_opa(l, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(l, sel ? lv_color_black() : lv_color_white(), 0);
  }
  if (menu_sel_ >= 0 && menu_sel_ < menu_n_) lv_obj_scroll_to_view(c->mitem[menu_sel_], LV_ANIM_OFF);
}

// Tryk mens menuen er åben: på et punkt = vælg og udfør, uden for menuen = luk.
bool AppUi::lvMenuTap(int x, int y) {
  LvChat* c = lvc_;
  lv_area_t a;
  lv_obj_get_coords(c->menu, &a);
  if (x < a.x1 || x > a.x2 || y < a.y1 || y > a.y2) {
    menuClose();
    return true;
  }
  for (int i = 0; i < menu_n_; ++i) {
    lv_obj_get_coords(c->mitem[i], &a);
    if (y >= a.y1 && y <= a.y2) {
      menu_sel_ = i;
      menuRun();
      break;
    }
  }
  dirty_ = true;
  return true;
}

// ---- Emoji-vælger ---------------------------------------------------------------
extern "C" int dm_emoji_count(void);
extern "C" uint32_t dm_emoji_at(int i);

namespace {
// Rækkefølge i vælgeren: smileys og hænder først, derefter resten.
const uint32_t kOrder[][2] = {{0x1F600, 0x1F650}, {0x1F910, 0x1F930}, {0x1F970, 0x1F980},
                              {0x1F440, 0x1F500}, {0x1F90C, 0x1F910}, {0x1F930, 0x1F970},
                              {0x1F980, 0x1FA00}, {0x2600, 0x27C0},   {0x1F300, 0x1F440},
                              {0x1F500, 0x1F600}, {0x1F680, 0x1F700}, {0x1FA00, 0x1FB00}};
int16_t* g_order = nullptr;
int g_order_n = 0;

void buildOrder() {
  if (g_order) return;
  const int n = dm_emoji_count();
  g_order = static_cast<int16_t*>(lv_mem_alloc(sizeof(int16_t) * n));
  if (!g_order) return;
  for (const auto& r : kOrder)
    for (int i = 0; i < n; ++i) {
      const uint32_t cp = dm_emoji_at(i);
      if (cp >= r[0] && cp < r[1]) g_order[g_order_n++] = int16_t(i);
    }
}

int utf8(uint32_t cp, char* o) {
  if (cp < 0x80) { o[0] = char(cp); return 1; }
  if (cp < 0x800) { o[0] = char(0xC0 | (cp >> 6)); o[1] = char(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) {
    o[0] = char(0xE0 | (cp >> 12)); o[1] = char(0x80 | ((cp >> 6) & 0x3F));
    o[2] = char(0x80 | (cp & 0x3F)); return 3;
  }
  o[0] = char(0xF0 | (cp >> 18)); o[1] = char(0x80 | ((cp >> 12) & 0x3F));
  o[2] = char(0x80 | ((cp >> 6) & 0x3F)); o[3] = char(0x80 | (cp & 0x3F)); return 4;
}
}  // namespace

int AppUi::emojiCount() const {
  buildOrder();
  return g_order_n;
}

void AppUi::emojiToggle() {
  if (!typing_) {  // som når man begynder at skrive
    if (scr_ == ScrContacts && contactRows() > 0) openContact(sel_);
    else if (scr_ != ScrChat) switchTo(ScrChat);
    startTyping(false);
  }
  emo_open_ = !emo_open_;
  ov_n_ = 0;
  dirty_ = true;
}

void AppUi::emojiInsert(int i) {
  if (i < 0 || i >= emojiCount()) return;
  char u[5];
  const int n = utf8(dm_emoji_at(g_order[i]), u);
  if (in_len_ + n >= sizeof(in_) - 1) return;
  std::memcpy(in_ + in_len_, u, n);
  in_len_ = uint16_t(in_len_ + n);
  in_[in_len_] = 0;
  dirty_ = true;
}

void AppUi::mentionInsert(const char* name) {
  if (!name || !*name) return;
  if (!typing_) startTyping(false);
  char t[48];
  const int n = snprintf(t, sizeof(t), "@[%s] ", name);
  if (n <= 0 || in_len_ + n >= int(sizeof(in_)) - 1) return;
  std::memcpy(in_ + in_len_, t, n);
  in_len_ = uint16_t(in_len_ + n);
  in_[in_len_] = 0;
  dirty_ = true;
}

// Byg beskedlisten igen (kun når samtalen eller beskederne har ændret sig).
void AppUi::lvRebuildMsgs() {
  LvChat* c = lvc_;
  lv_obj_clean(c->list);
  std::memset(c->gear, 0, sizeof(c->gear));  // de gamle tandhjul er slettet med listen
  const uint32_t now_ts = node_ ? node_->nowTs() : 0;
  const int count = int(store_ ? store_->viewSize() : 0);
  const uint32_t cutoff = store_ ? store_->cutoffFor(store_->openId(), now_ts) : 0;
  int oldest = count > kMaxMsgs ? count - kMaxMsgs : 0;
  if (cutoff)
    while (oldest < count) {
      const uint32_t t = chatMsgTs(store_->viewAt(size_t(oldest)), now_ts);
      if (!t || t >= cutoff) break;
      ++oldest;
    }
  if (oldest >= count) {
    lv_obj_t* l = lv_label_create(c->list);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_color(l, c565(color::Gray), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(l, 40, 0);
    setText(l, "Ingen beskeder endnu i denne samtale");
    return;
  }
  int last_day = -1;
  int nnames = 0;
  c->nlocs = 0;
  // DM-visning (bobler, intet navn pr. besked) for begge net: MeshCore "mcd_", Meshtastic "mtd_".
  const bool dm = std::strncmp(store_->openId(), "mcd_", 4) == 0 || std::strncmp(store_->openId(), "mtd_", 4) == 0;
  for (int mi = oldest; mi < count; ++mi) {
    const Msg& m = store_->viewAt(size_t(mi));
    const uint32_t ts = chatMsgTs(m, now_ts);
    const int day = chatDayKey(ts);
    if (day != last_day) {  // datolinje over dagens første besked
      last_day = day;
      char dt[48];
      chatDayText(ts, now_ts, dt, sizeof(dt));
      lv_obj_t* l = lv_label_create(c->list);
      lv_obj_set_width(l, LV_PCT(100));
      lv_obj_set_style_text_font(l, &dm_font_12, 0);
      lv_obj_set_style_text_color(l, c565(color::LightBlue), 0);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      setText(l, dt);
    }
    char when[8];
    wallclock::formatHm(ts, when, sizeof(when));
    char sender[40] = "";
    const char* body = m.text;
    uint16_t scol = color::White;
    char stb[24];
    const char* st = i18n::tr(m.status == Msg::Delivered ? "  leveret"
                              : m.status == Msg::Failed  ? "  FEJL"
                              : m.status == Msg::Queued  ? "  sendt"
                                                         : "",
                              stb, sizeof(stb));
    switch (m.kind) {
      case Msg::ChannelIn: {
        const char* colon = std::strstr(m.text, ": ");
        if (colon && colon - m.text < int(sizeof(sender) - 1)) {
          bool dup = false;
          snprintf(sender, sizeof(sender), "%.*s", int(colon - m.text), m.text);
          scol = ccolor::rgb(ccolor::forName(sender, &dup, nullptr));
          if (dup) {
            char t[40];
            snprintf(t, sizeof(t), "!%s", sender);
            snprintf(sender, sizeof(sender), "%s", t);
          }
          body = colon + 2;
        }
        break;
      }
      case Msg::DirectIn: {
        snprintf(sender, sizeof(sender), "%s", m.peer);
        uint8_t k[6] = {};
        const char* id = m.conv;
        bool ok = std::strncmp(id, "mcd_", 4) == 0 && std::strlen(id) >= 16;
        for (int b = 0; ok && b < 6; ++b) {
          char hx[3] = {id[4 + 2 * b], id[5 + 2 * b], 0};
          char* e;
          k[b] = uint8_t(std::strtoul(hx, &e, 16));
          ok = !*e;
        }
        if (!ok && std::strncmp(id, "mtd_", 4) == 0 && std::strlen(id) >= 12) {
          // Meshtastic: farven ud fra nodenummeret (4 byte), så den er fast for noden.
          const uint32_t num = uint32_t(std::strtoul(id + 4, nullptr, 16));
          for (int b = 0; b < 4; ++b) k[b] = uint8_t(num >> (24 - 8 * b));
          k[4] = 0x4D;  // 'M' - adskiller fra MeshCore-nøgler
          k[5] = 0x54;  // 'T'
          ok = true;
        }
        scol = ok ? ccolor::rgb(ccolor::forKey(k)) : color::Yellow;
        break;
      }
      case Msg::ChannelOut:
      case Msg::DirectOut: {
        // MeshCore lægger afsendernavnet foran kanalteksten ("Navn: tekst"): vis kun "Mig".
        const char* me = node_ ? node_->nodeName() : "";
        const size_t ml = std::strlen(me);
        if (ml && std::strncmp(body, me, ml) == 0 && body[ml] == ':' && body[ml + 1] == ' ')
          body += ml + 2;
        snprintf(sender, sizeof(sender), "Mig");
        scol = color::Green;  // egne beskeder er grønne
        break;
      }
    }
    // Bobler for både DM'er og kanaler (brugerens ønsker 16. og 17. nov 2026): andres til
    // venstre, mine til højre; runde hjørner, lidt lysere baggrund, ramme i afsenderens
    // kontaktfarve (mine: grøn). I en kanal står afsenderens navn øverst i andres bobler (i en
    // DM står navnet kun i titellinjen). Tid og status småt under teksten.
    const bool mine = m.kind == Msg::DirectOut || m.kind == Msg::ChannelOut;
    lv_obj_t* row = lv_obj_create(c->list);
    noStyle(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    if (!dm && m.kind == Msg::ChannelIn && sender[0] && nnames < kMaxMsgs) {
      ++nnames;  // tryk på boblen = @-omtale af afsenderen (uden '!' for dubletter)
      snprintf(c->mnames[nnames], sizeof(c->mnames[0]), "%s", sender[0] == '!' ? sender + 1 : sender);
      lv_obj_set_user_data(row, reinterpret_cast<void*>(intptr_t(nnames)));
    }
    lv_obj_t* b = lv_obj_create(row);
    noStyle(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(b, 250, 0);
    lv_obj_set_style_radius(b, 9, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, c565(scol), 0);
    lv_obj_set_style_bg_color(b, c565(0x2124), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(b, 7, 0);
    lv_obj_set_style_pad_ver(b, 4, 0);
    lv_obj_set_style_pad_row(b, 1, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    {  // en lokation i teksten: tryk på boblen = vis/gem på kortet
      double la, lo;
      if (c->nlocs < LvChat::kLocs && geo::parseLoc(body, la, lo)) {
        c->locobj[c->nlocs] = b;
        c->loclat[c->nlocs] = la;
        c->loclon[c->nlocs] = lo;
        ++c->nlocs;
      }
    }
    if (!dm && !mine && sender[0]) {  // afsenderen i en kanal
      lv_obj_t* nl = lv_label_create(b);
      lv_obj_set_style_text_font(nl, &dm_font_12, 0);
      lv_obj_set_style_text_color(nl, c565(scol), 0);
      lv_label_set_long_mode(nl, LV_LABEL_LONG_DOT);
      lv_obj_set_style_max_width(nl, 236, 0);
      lv_label_set_text(nl, sender);
    }
    // Teksten som spans i én skriftstørrelse (14 px): MeshCores omtaler "@[navn]" vises som
    // "@navn", i grønt (vores farve), hvis det er os, så beskeder til os er lette at finde,
    // ellers i lyseblå (ikke Cyan, som er forbeholdt bjælkerne). (LVGL's recolor-koder blev
    // prøvet 17. nov 2026 og slugte tegnet efter en omtale.)
    struct Part {
      const char* p;
      size_t n;
      uint16_t col;
      bool at;
    };
    Part parts[12];
    int np = 0;
    char plain[240];  // samme tekst uden "[ ]", til at måle bredden
    size_t pk = 0;
    {
      const char* me = node_ ? node_->nodeName() : "";
      auto addPart = [&](const char* t, size_t n, uint16_t col, bool at) {
        if (!n || np >= int(sizeof(parts) / sizeof(parts[0]))) return;
        parts[np++] = {t, n, col, at};
        if (at && pk + 1 < sizeof(plain)) plain[pk++] = '@';
        for (size_t i = 0; i < n && pk + 1 < sizeof(plain); ++i) plain[pk++] = t[i];
      };
      for (const char* p = body; *p;) {
        const char* at = std::strstr(p, "@[");
        const char* end = at ? std::strchr(at + 2, ']') : nullptr;
        if (!at || !end || np >= 10) {
          addPart(p, std::strlen(p), color::White, false);
          break;
        }
        addPart(p, size_t(at - p), color::White, false);
        const size_t nl = size_t(end - at - 2);
        const bool us = me[0] && std::strlen(me) == nl && std::strncmp(at + 2, me, nl) == 0;
        addPart(at + 2, nl, us ? color::Green : color::LightBlue, true);
        p = end + 1;
      }
      plain[pk] = 0;
    }
    lv_point_t sz;  // bredeste linje efter ombrydning ved højst 236 px
    lv_txt_get_size(&sz, plain, &dm_font_14, 0, 0, 236, LV_TEXT_FLAG_NONE);
    lv_obj_t* tl = lv_spangroup_create(b);
    lv_obj_set_style_text_font(tl, &dm_font_14, 0);
    lv_spangroup_set_mode(tl, LV_SPAN_MODE_BREAK);
    lv_obj_set_width(tl, sz.x + 2);
    lv_obj_set_height(tl, LV_SIZE_CONTENT);
    for (int i = 0; i < np; ++i) {
      char seg[224];
      snprintf(seg, sizeof(seg), "%s%.*s", parts[i].at ? "@" : "", int(parts[i].n), parts[i].p);
      lv_span_t* sp = lv_spangroup_new_span(tl);
      lv_span_set_text(sp, seg);
      lv_style_set_text_color(&sp->style, c565(parts[i].col));
    }
    lv_spangroup_refr_mode(tl);
    lv_obj_t* meta = lv_label_create(b);
    lv_obj_set_style_text_font(meta, &dm_font_12, 0);
    lv_obj_set_style_text_color(meta, c565(m.status == Msg::Failed ? color::Red : color::Gray), 0);
    char mt[24];
    snprintf(mt, sizeof(mt), "%s%s", when, st);
    lv_label_set_text(meta, mt);
    // Tandhjulet til højre for en andens boble i en kanal: afsenderens funktioner (tryk).
    const intptr_t uid = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
    if (uid > 0) {
      lv_obj_t* g = lv_label_create(row);
      lv_obj_set_style_text_font(g, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(g, c565(color::Gray), 0);
      lv_obj_set_style_pad_left(g, 6, 0);
      lv_obj_set_style_pad_top(g, 4, 0);
      lv_label_set_text(g, LV_SYMBOL_SETTINGS);
      c->gear[uid] = g;
    }
  }
}

void AppUi::lvDrawChat(bool force) {
  if (!lvc_) lvBuildChat();
  LvChat* c = lvc_;
  if (lv_scr_act() != c->scr) lv_scr_load(c->scr);

  // Faner med '*' for ulæste. Chatten er en underside: dens fane er den, den blev åbnet fra.
  static const char* names[kTabs] = {"Kanal", "Kontakt", "Kort", "Indst."};  // som Screen fra kFirstTab
  bool mark[kTabs] = {};
  if (store_) {
    mark[ScrChannels - kFirstTab] = store_->unreadPrefix("mcc_") + store_->unreadPrefix("mtc_") > 0;
    mark[ScrContacts - kFirstTab] = store_->unreadPrefix("mcd_") + store_->unreadPrefix("mtd_") > 0;
  }
  const bool sos = sos_active_ && sos_active_();
  const uint16_t bar = sos ? color::Red : color::Cyan;
  lv_obj_set_style_bg_color(c->tabs, c565(bar), 0);
  const Screen tab_scr = scr_ == ScrChat ? chat_parent_ : scr_;
  for (int i = 0; i < kTabs; ++i) {
    char t[16];
    snprintf(t, sizeof(t), "%s%s", names[i], mark[i] ? "*" : "");
    setText(c->tab[i], t);
    const bool act = tab_scr == Screen(kFirstTab + i);
    // Stregen under den aktive fane: kun den er synlig (alle har samme bredde, så
    // teksten står ens - LVGL trækker kanten fra tekstområdet).
    lv_obj_set_style_border_opa(c->tab[i], act ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    // Markøren i fanelinjen: den aktive fane vendes om (sort med fanelinjens farve).
    const bool focus = act && tab_focus_;
    lv_obj_set_style_bg_color(c->tab[i], lv_color_black(), 0);
    lv_obj_set_style_bg_opa(c->tab[i], focus ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(c->tab[i], focus ? c565(bar) : sos ? lv_color_white() : lv_color_black(), 0);
  }

  // Titel / kort besked.
  const bool contacts = scr_ == ScrContacts;
  const bool chans = scr_ == ScrChannels && chan_;
  if (contacts && millis() - last_contacts_ms_ > 5000) refreshContacts();
  static ChannelsUi::View kv;
  if (chans) chan_->view(kv, kKRows);
  lv_obj_set_style_bg_color(c->title, c565(chans && kv.filter_sel && !tab_focus_ ? (kv.filter_edit ? color::Yellow : color::Select) : 0x2945), 0);
  if (chans) {
    setText(c->title, kv.filter);
    lv_obj_set_style_text_color(c->title, kv.filter_sel && !tab_focus_ ? lv_color_black() : lv_color_white(), 0);
  } else if (note_[0]) {
    setText(c->title, note_);
    lv_obj_set_style_text_color(c->title, c565(color::Yellow), 0);
  } else if (contacts) {
    char t[40];
    if (mt_ && mt_->count())
      snprintf(t, sizeof(t), "Kontakter (%d MeshCore, %d Meshtastic)", n_contacts_, mt_->count());
    else
      snprintf(t, sizeof(t), "Kontakter (%d)", n_contacts_);
    setText(c->title, t);
    lv_obj_set_style_text_color(c->title, lv_color_white(), 0);
  } else {
    const char* t = store_ && store_->openTitle()[0] ? store_->openTitle() : "Ingen samtale valgt";
    if (std::strncmp(t, "DM ", 3) == 0) t += 3;  // DM: kun navnet på den, man skriver med
    char tt[64];
    if (gearAllowed())  // tandhjulet foran titlen: kanalens/DM-partnerens funktioner
      snprintf(tt, sizeof(tt), LV_SYMBOL_SETTINGS "  %s", t);
    else
      snprintf(tt, sizeof(tt), "%s", t);
    setText(c->title, tt);
    lv_obj_set_style_text_color(c->title, lv_color_white(), 0);
  }

  // Indholdet: kanaler, kontakter eller beskeder.
  auto show = [](lv_obj_t* o, bool on) {
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  };
  const bool pnl = lv_panel_;  // underskærm/Status/SOS erstatter listerne og indtastningen
  const bool sets = !pnl && scr_ == ScrSettings && menu_;
  const bool mapv = !pnl && scr_ == ScrMap;
  show(c->list, !pnl && !contacts && !chans && !sets && !mapv);
  show(c->clist, !pnl && contacts);
  show(c->klist, !pnl && chans);
  show(c->kdetail, !pnl && chans);
  show(c->sp, sets);
  show(c->pp, pnl);
  show(c->mp, mapv);
  show(c->title, !sets && !pnl && !mapv);
  show(c->inrow, !sets && !pnl && !mapv);
  if (pnl) lvDrawPanel();
  else if (mapv) lvDrawMap();
  else if (contacts) lvDrawContacts();
  else if (chans) lvDrawChannels(kv);
  else if (sets) lvDrawSettings();
  else lvDrawMsgs(force);
  lvDrawBottom();
  lvDrawMenu();  // tandhjulsmenuen svæver over chatten (skjules uden for Chat)
  if (chans) {  // nederst: indtastning (navn/nøgle) eller hjælp
    char in[96];
    if (kv.input) snprintf(in, sizeof(in), "> %s_", kv.input_text);
    else snprintf(in, sizeof(in), "%s", kv.input_text);
    setText(c->input, in);
    lv_obj_set_style_text_color(c->input, kv.input ? lv_color_white() : c565(color::Gray), 0);
  }
  if (force) lv_obj_invalidate(c->scr);
}

void AppUi::lvDrawMsgs(bool force) {
  LvChat* c = lvc_;
  // Beskeder: byg kun igen ved ændring (samtale, antal, status, dato).
  const uint32_t now_ts = node_ ? node_->nowTs() : 0;
  uint32_t sig = 2166136261u;
  if (store_) {
    sig = fnv(sig, store_->openId(), std::strlen(store_->openId()));
    const size_t n = store_->viewSize();
    sig = fnv(sig, &n, sizeof(n));
    const uint32_t cut = store_->cutoffFor(store_->openId(), now_ts) / 3600;
    sig = fnv(sig, &cut, sizeof(cut));
    const int today = chatDayKey(now_ts);
    sig = fnv(sig, &today, sizeof(today));
    for (size_t i = n > kMaxMsgs ? n - kMaxMsgs : 0; i < n; ++i) {
      const Msg& m = store_->viewAt(i);
      const uint32_t t = chatMsgTs(m, now_ts) / 60;  // minuttet (ts=0 regnes fra oppetiden)
      sig = fnv(sig, &t, sizeof(t));
      sig = fnv(sig, &m.status, sizeof(m.status));
      sig = fnv(sig, m.text, std::strlen(m.text));
    }
    const uint32_t gen = ccolor::generation();
    sig = fnv(sig, &gen, sizeof(gen));
  }
  const bool rebuilt = force || sig != c->sig;
  if (rebuilt) {
    c->sig = sig;
    lvRebuildMsgs();
  }
  if (rebuilt || c->scroll_shown != chat_scroll_) {
    lv_obj_update_layout(c->list);
    const lv_coord_t max = lv_obj_get_scroll_y(c->list) + lv_obj_get_scroll_bottom(c->list);
    lv_coord_t y = max - chat_scroll_ * kScrollStepPx;
    if (y < 0) {
      y = 0;
      chat_scroll_ = (max + kScrollStepPx - 1) / kScrollStepPx;  // ikke længere op end toppen
    }
    lv_obj_scroll_to_y(c->list, y, LV_ANIM_OFF);
    c->scroll_shown = chat_scroll_;
  }
}

// Kontaktlisten: de synlige rækker fyldes med kontakterne fra `ctop` (valget holdes synligt).
void AppUi::lvDrawContacts() {
  LvChat* c = lvc_;
  const int rows = contactRows();
  if (sel_ < c->ctop) c->ctop = sel_;
  if (sel_ >= c->ctop + kCRows) c->ctop = sel_ - kCRows + 1;
  if (c->ctop > rows - kCRows) c->ctop = rows > kCRows ? rows - kCRows : 0;
  if (c->ctop < 0) c->ctop = 0;
  const uint32_t now = node_ ? node_->getRTCClock()->getCurrentTime() : 0;
  if (rows == 0) lv_obj_clear_flag(c->cempty, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(c->cempty, LV_OBJ_FLAG_HIDDEN);
  for (int r = 0; r < kCRows; ++r) {
    const int i = c->ctop + r;
    if (i >= rows) {
      lv_obj_add_flag(c->crow[r], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(c->crow[r], LV_OBJ_FLAG_HIDDEN);
    const bool sel = i == sel_ && !tab_focus_;  // fanelinjen har markøren: intet valg i listen
    char a[8], info[48], conv[20], un[8] = "", name[40];
    uint16_t ncol = color::Gray;
    if (i < n_contacts_) {  // MeshCore-kontakt
      const ContactInfo& ct = contacts_[i];
      contactAge(a, sizeof(a), now > ct.lastmod ? now - ct.lastmod : 0);
      if (ct.out_path_len == OUT_PATH_UNKNOWN)
        snprintf(info, sizeof(info), "MC %s  flood  %s", contactTypeName(ct.type), a);
      else
        snprintf(info, sizeof(info), "MC %s  %u hop  %s", contactTypeName(ct.type),
                 unsigned(ct.out_path_len & 63), a);
      McNode::dmConvId(ct.id.pub_key, conv, sizeof(conv));
      snprintf(name, sizeof(name), "%s", ct.name);
      if (ct.type == ADV_TYPE_CHAT) ncol = ccolor::rgb(ccolor::forKey(ct.id.pub_key));
    } else {  // Meshtastic-node
      const MtNodeEntry& e = mt_->at(i - n_contacts_);
      const uint32_t secs = e.last_ts && now > e.last_ts ? now - e.last_ts
                            : e.last_ms                  ? (millis() - e.last_ms) / 1000
                                                         : 0;
      contactAge(a, sizeof(a), secs);
      snprintf(info, sizeof(info), "MT %s  %u hop  %s", e.short_name[0] ? e.short_name : "-", e.hops, a);
      MtNode::dmConvId(e.num, conv, sizeof(conv));
      mt_->nameOf(e.num, name, sizeof(name));
      uint8_t k6[6];  // farve ud fra nodenummeret (som MeshCore ud fra nøglen)
      for (int b = 0; b < 6; ++b) k6[b] = uint8_t(e.num >> (8 * (b % 4))) ^ uint8_t(b * 37);
      ncol = ccolor::rgb(ccolor::autoFor(k6));
    }
    const uint32_t u = store_ ? store_->unread(conv) : 0;
    if (u) snprintf(un, sizeof(un), "%lu", (unsigned long)(u > 99 ? 99 : u));
    setText(c->cbadge[r], un);
    setText(c->cname[r], name);
    setText(c->cinfo[r], info);
    lv_obj_set_style_bg_opa(c->crow[r], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(c->cname[r], sel ? lv_color_black() : c565(ncol), 0);
    lv_obj_set_style_text_color(c->cinfo[r], sel ? lv_color_black() : c565(color::Gray), 0);
    lv_obj_set_style_text_color(c->cbadge[r], sel ? lv_color_black() : c565(color::Yellow), 0);
  }
}

// Kanallisten ud fra ChannelsUi's datamodel (samme logik som tekstvisningen).
void AppUi::lvDrawChannels(const ChannelsUi::View& v) {
  LvChat* c = lvc_;
  for (int r = 0; r < kKRows; ++r) {
    if (r >= v.nrows) {
      lv_obj_add_flag(c->krow[r], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(c->krow[r], LV_OBJ_FLAG_HIDDEN);
    const ChannelsUi::ViewRow& w = v.rows[r];
    char info[20] = "";
    if (w.pkts[0]) snprintf(info, sizeof(info), "%s  %s", w.pkts, w.age);
    setText(c->knet[r], w.net);
    setText(c->kname[r], w.name);
    setText(c->kunread[r], w.unread);
    setText(c->kinfo[r], info);
    const bool sel = w.sel && !tab_focus_;
    const uint16_t bg = w.confirm ? color::Yellow : color::Select;
    lv_obj_set_style_bg_color(c->krow[r], c565(bg), 0);
    lv_obj_set_style_bg_opa(c->krow[r], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(c->krow[r], w.add ? 29 : 5, 0);
    lv_obj_set_style_text_color(c->knet[r], sel ? lv_color_black() : c565(color::Gray), 0);
    lv_obj_set_style_text_color(c->kname[r], sel ? lv_color_black() : c565(w.fg), 0);
    lv_obj_set_style_text_color(c->kunread[r], sel ? lv_color_black() : c565(color::Yellow), 0);
    lv_obj_set_style_text_color(c->kinfo[r], sel ? lv_color_black() : c565(color::Gray), 0);
  }
  setText(c->kdetail, v.detail);
  lv_obj_set_style_text_color(c->kdetail, c565(v.detail_fg), 0);
}

// Indst.: gruppeoverskrifter i gruppens farve, etiket til venstre og værdi til højre,
// valgt række = lyseblå bjælke (gul under ændring, grå når låst).
void AppUi::lvDrawSettings() {
  LvChat* c = lvc_;
  static SettingsUi::View v;
  menu_->view(v, kSRows);
  for (int r = 0; r < kSRows; ++r) {
    lv_obj_t* row = c->srow[r];
    if (r >= v.nrows) {
      lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    const SettingsUi::ViewRow& w = v.rows[r];
    uint16_t bg = 0, fl = w.color, fv = color::White;
    bool fill = false;
    char lab[56];
    switch (w.kind) {
      case SettingsUi::ViewRow::Header:
        fill = true;
        bg = w.sel && !tab_focus_ ? color::Select : w.color;  // valgt overskrift = valgbjælken
        fl = color::Black;
        snprintf(lab, sizeof(lab), "%s", w.label);
        break;
      case SettingsUi::ViewRow::Item: {
        char tl[56];
        snprintf(lab, sizeof(lab), "%s%s", w.changed ? "* " : "", i18n::tr(w.label, tl, sizeof(tl)));
        if (w.locked) fl = fv = color::Dim;
        if (w.sel && !tab_focus_) {
          fill = true;
          bg = v.editing ? color::Yellow : (w.locked ? color::Gray : color::Select);
          fl = fv = color::Black;
        }
        break;
      }
      case SettingsUi::ViewRow::Empty: {
        char tl[56];
        snprintf(lab, sizeof(lab), "  %s", i18n::tr(w.label, tl, sizeof(tl)));
        fl = color::Gray;
        break;
      }
      default:
        lab[0] = 0;
        break;
    }
    setText(c->slab[r], lab);
    setText(c->sval[r], w.kind == SettingsUi::ViewRow::Item ? w.value : "");
    lv_obj_set_style_bg_color(row, c565(bg), 0);
    lv_obj_set_style_bg_opa(row, fill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(row, w.kind == SettingsUi::ViewRow::Header ? 4 : 0, 0);
    lv_obj_set_height(row, w.kind == SettingsUi::ViewRow::Blank ? 6 : kSRowH);
    lv_obj_set_style_text_color(c->slab[r], c565(fl), 0);
    lv_obj_set_style_text_color(c->sval[r], c565(fv), 0);
  }
  setText(c->shelp, v.help);
  lv_obj_set_style_text_color(c->shelp, c565(v.help_dim ? color::Dim : color::White), 0);
  setText(c->shint, v.hint);
  lv_obj_set_style_text_color(c->shint, c565(v.hint_fg), 0);
  for (int b = 0; b < 2; ++b) {
    const bool sel = (b == 0 ? v.save_sel : v.cancel_sel) && !tab_focus_;
    lv_obj_set_style_bg_opa(c->sbtn[b], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(c->sbtn[b], c565(sel ? color::Select : (v.changed ? color::White : color::Gray)), 0);
    lv_obj_set_style_text_color(c->sbtn[b], sel ? lv_color_black() : c565(v.changed ? color::White : color::Gray), 0);
  }
}

// Underskærm med datamodel: rækkerne i Indst.-stil (etiket til venstre, værdi til højre,
// valgt = lyseblå bjælke, gul når den venter på andet klik). En stor QR-kode (qrSize > 104,
// fx Fejlrapport) fylder venstre side, og kun PanelView::side vises til højre.
void AppUi::lvDrawPanel() {
  LvChat* c = lvc_;
  const PanelView& v = pview_;
  // Hvilket panel tegnes: SOS-bekræftelsen har intet, Status er AppUi's eget.
  const Panel* p = sos_confirm_ ? nullptr : scr_ == ScrStatus ? &status_panel_ : panel_;
  const char* q = p ? p->qrText() : nullptr;
  const int qs = q ? p->qrSize() : 0;
  const bool big = q && qs > 104;
  auto show = [](lv_obj_t* o, bool on) {
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  };
  for (lv_obj_t* o : {c->ptb, c->pstat, c->pstat2, c->plist, c->pnote, c->pbottom}) show(o, !big);
  for (lv_obj_t* l : c->pside) show(l, big);

  char t[64];
  snprintf(t, sizeof(t), "%s%s%s", v.title[0] ? v.title : (p ? p->title() : ""),
           v.scroll_up ? "  " LV_SYMBOL_UP : "", v.scroll_down ? "  " LV_SYMBOL_DOWN : "");
  setText(c->ptitle, t);
  setText(c->ptime, v.status3);
  setText(c->pstat, v.status);
  lv_obj_set_style_text_color(c->pstat, c565(v.status_fg), 0);
  setText(c->pstat2, v.status2);
  // QR-kode: lille (fx "Omkring") til højre for listen, stor øverst til venstre.
  if (c->pqr && (!q || qs != c->pqr_size)) {
    lv_obj_del(c->pqr);
    c->pqr = nullptr;
  }
  if (q && !c->pqr) {
    c->pqr = lv_qrcode_create(c->pp, qs, lv_color_black(), lv_color_white());
    c->pqr_size = qs;
    c->pqr_text[0] = '\0';
    lv_obj_add_flag(c->pqr, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_border_color(c->pqr, lv_color_white(), 0);
    lv_obj_set_style_border_width(c->pqr, big ? 4 : 3, 0);
    if (big) lv_obj_align(c->pqr, LV_ALIGN_TOP_LEFT, 0, 1);
    else lv_obj_align(c->pqr, LV_ALIGN_TOP_RIGHT, -5, kPTitleH + 17 + 16 + 2);  // ud for listen
  }
  if (q && std::strcmp(q, c->pqr_text) != 0) {
    snprintf(c->pqr_text, sizeof(c->pqr_text), "%s", q);
    lv_qrcode_update(c->pqr, q, uint32_t(std::strlen(q)));
  }
  if (big) {  // teksten til højre for koden, med luft: forklaring, advarsel, hjælp nederst
    const int x = qs + 8 + 12;
    const char* txt[3] = {v.side, v.note, v.hint};
    const uint16_t col[3] = {color::White, v.note_fg, color::Gray};
    for (int i = 0; i < 3; ++i) {
      lv_obj_t* l = c->pside[i];
      lv_obj_set_width(l, kW - x - 2);
      setText(l, txt[i]);
      lv_obj_set_style_text_color(l, c565(col[i]), 0);
      if (i == 0) {
        lv_obj_set_pos(l, x, 4);
        lv_obj_update_layout(l);  // højden efter ombrydning, før advarslen placeres under
      } else if (i == 1) lv_obj_align_to(l, c->pside[0], LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);
      else lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, x, -4);
    }
    return;
  }
  const int roww = q ? kW - qs - 14 : kW;
  for (int r = 0; r < kPanelRows; ++r) {
    lv_obj_t* row = c->prow[r];
    lv_obj_set_width(row, roww);
    if (r >= v.nrows) {
      lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    const PanelRow& w = v.rows[r];
    uint16_t bg = 0, fl = w.fg, fv = w.vfg;
    bool fill = false;
    if (w.kind == PanelRow::Header) {
      fill = true;
      bg = w.sel ? color::Select : w.fg;
      fl = color::Black;
    } else if (w.sel) {
      fill = true;
      bg = w.confirm ? color::Yellow : color::Select;
      fl = fv = color::Black;
    }
    setText(c->plab[r], w.label);
    setText(c->pval[r], w.value);
    lv_obj_set_style_bg_color(row, c565(bg), 0);
    lv_obj_set_style_bg_opa(row, fill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(row, w.kind == PanelRow::Header ? 4 : 0, 0);
    lv_obj_set_height(row, w.kind == PanelRow::Blank ? 6 : kSRowH);
    lv_obj_set_style_text_color(c->plab[r], c565(fl), 0);
    lv_obj_set_style_text_color(c->pval[r], c565(fv), 0);
  }
  setText(c->pnote, v.note);
  lv_obj_set_style_text_color(c->pnote, c565(v.note_fg), 0);
  if (v.input) {
    snprintf(t, sizeof(t), "> %s", v.input_text);
    setText(c->pbottom, t);
    lv_obj_set_style_bg_opa(c->pbottom, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(c->pbottom, lv_color_black(), 0);
  } else {
    setText(c->pbottom, v.hint);
    lv_obj_set_style_bg_opa(c->pbottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(c->pbottom, c565(color::Gray), 0);
  }
}

int AppUi::lvPanelRowAt(int y) const {
  const LvChat* c = lvc_;
  if (!c) return -1;
  for (int r = 0; r < pview_.nrows && r < kPanelRows; ++r) {
    if (lv_obj_has_flag(c->prow[r], LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t a;
    lv_obj_get_coords(c->prow[r], &a);
    if (y >= a.y1 && y <= a.y2) return pview_.rows[r].kind == PanelRow::Item ? r : -1;
  }
  return -1;
}

int AppUi::lvPanelTop() const { return kTabH; }

// Kortet: fliserne omkring centrum (op til 3 x 2), derefter markører og tekstlinjerne.
void AppUi::lvDrawMap() {
  LvChat* c = lvc_;
  const int W = kW, H = 240 - kTabH;
  const bool have = map_ && map_->hasMaps();
  if (have) lv_obj_add_flag(c->mempty, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(c->mempty, LV_OBJ_FLAG_HIDDEN);
  int used = 0;
  if (have) {
    const int z = map_->zoom();
    const double ox = map_->cx() - W / 2.0, oy = map_->cy() - H / 2.0;  // skærmens øverste venstre
    const int tx0 = int(std::floor(ox / MapView::kTile)), ty0 = int(std::floor(oy / MapView::kTile));
    for (int ty = ty0; ty * MapView::kTile < oy + H && used < LvChat::kMapImgs; ++ty) {
      for (int tx = tx0; tx * MapView::kTile < ox + W && used < LvChat::kMapImgs; ++tx) {
        int st = 0;
        const uint16_t* px = map_->tile(z, tx, ty, st);
        lv_obj_t* im = c->mimg[used];
        lv_img_dsc_t& ds = c->mdsc[used];
        ++used;
        if (!px) {
          lv_obj_add_flag(im, LV_OBJ_FLAG_HIDDEN);
          continue;
        }
        if (ds.data != reinterpret_cast<const uint8_t*>(px)) {
          lv_img_cache_invalidate_src(&ds);  // samme beskrivelse, nyt indhold
          ds.data = reinterpret_cast<const uint8_t*>(px);
          lv_img_set_src(im, &ds);
          lv_obj_invalidate(im);
        }
        lv_obj_set_pos(im, int(std::lround(tx * double(MapView::kTile) - ox)),
                       int(std::lround(ty * double(MapView::kTile) - oy)));
        lv_obj_clear_flag(im, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  for (int i = used; i < LvChat::kMapImgs; ++i) lv_obj_add_flag(c->mimg[i], LV_OBJ_FLAG_HIDDEN);

  // Spor: punkterne (Mercator 0..1) omregnes til skærmen; kun stykker i/nær udsnittet, og
  // punkter under 2 px fra det forrige springes over (så et langt spor koster lidt).
  for (int k = 0; k < 2; ++k) {
    int seg = 0;
    const Track* tr = nullptr;
    if (have && track_) tr = k == 0 ? &track_->live() : &track_->shown();
    if (tr && tr->count() > 1) {
      const double ws = double(MapView::kTile) * double(1u << map_->zoom());
      const double ox = map_->cx() - W / 2.0, oy = map_->cy() - H / 2.0;
      int n = 0;
      int lx = 0, ly = 0;
      auto finish = [&]() {
        if (n >= 2 && seg < LvChat::kTrkSeg) {
          lv_line_set_points(c->tline[k][seg], c->tpts[k][seg], uint16_t(n));
          lv_obj_clear_flag(c->tline[k][seg], LV_OBJ_FLAG_HIDDEN);
          ++seg;
        }
        n = 0;
      };
      const TrackPt* p = tr->points();
      for (int i = 0; i < tr->count() && seg < LvChat::kTrkSeg; ++i) {
        const double x = p[i].mx * ws - ox, y = p[i].my * ws - oy;
        if (x < -W || x > 2 * W || y < -H || y > 2 * H) {  // langt uden for: afbryd stykket
          finish();
          continue;
        }
        const int ix = int(x), iy = int(y);
        if (n && std::abs(ix - lx) < 2 && std::abs(iy - ly) < 2 && i + 1 < tr->count()) continue;
        c->tpts[k][seg][n++] = {lv_coord_t(ix), lv_coord_t(iy)};
        lx = ix, ly = iy;
        if (n == LvChat::kTrkPts) {  // fuldt stykke: næste begynder i samme punkt
          finish();
          if (seg < LvChat::kTrkSeg) c->tpts[k][seg][n++] = {lv_coord_t(ix), lv_coord_t(iy)};
        }
      }
      finish();
    }
    for (int s = seg; s < LvChat::kTrkSeg; ++s) lv_obj_add_flag(c->tline[k][s], LV_OBJ_FLAG_HIDDEN);
  }

  // Geocaches (fra zoom 10): målet først, derefter dem i udsnittet.
  {
    int shown_c = 0;
    const int tgt = caches_ ? caches_->target() : -1;
    if (have && caches_ && map_->zoom() >= 10) {
      const double ws = double(MapView::kTile) * double(1u << map_->zoom());
      const double ox = map_->cx() - W / 2.0, oy = map_->cy() - H / 2.0;
      auto place = [&](int i) {
        const Geocache& g = caches_->at(i);
        if (g.found == 3) return;  // slettet fra listen
        const double x = g.mx * ws - ox, y = g.my * ws - oy;
        if (x < -6 || x > W + 6 || y < -6 || y > H + 6 || shown_c >= LvChat::kCacheMarks) return;
        lv_obj_t* d = c->cdot[shown_c];
        lv_obj_t* l = c->clab[shown_c];
        ++shown_c;
        const uint16_t col = g.found == 1 ? color::Gray : Caches::typeColor(g.type);
        lv_obj_set_style_bg_color(d, c565(col), 0);
        lv_obj_set_style_border_color(d, i == tgt ? lv_color_hex(0xFFB000) : lv_color_black(), 0);
        lv_obj_set_pos(d, int(x) - 5, int(y) - 5);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_HIDDEN);
        if (map_->zoom() >= 14 || i == tgt) {
          lv_label_set_text(l, g.code);
          lv_obj_set_pos(l, int(x) + 7, int(y) - 7);
          lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        }
      };
      if (tgt >= 0) place(tgt);
      for (int i = 0; i < caches_->count() && shown_c < LvChat::kCacheMarks; ++i)
        if (i != tgt) place(i);
    }
    for (int i = shown_c; i < LvChat::kCacheMarks; ++i) {
      lv_obj_add_flag(c->cdot[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(c->clab[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Navigationslinjen fra egen position til målet.
    MapMarker self[1];
    bool nav = false;
    if (have && tgt >= 0 && markers_ && markers_(self, 1) > 0 && self[0].kind == 0) {
      const Geocache& g = caches_->at(tgt);
      const double ox = map_->cx() - W / 2.0, oy = map_->cy() - H / 2.0;
      const double ws = double(MapView::kTile) * double(1u << map_->zoom());
      auto clamp = [](double v) { return lv_coord_t(v < -30000 ? -30000 : v > 30000 ? 30000 : v); };
      c->cnav_pts[0] = {clamp(map_->worldX(self[0].lon) - ox), clamp(map_->worldY(self[0].lat) - oy)};
      c->cnav_pts[1] = {clamp(g.mx * ws - ox), clamp(g.my * ws - oy)};
      lv_line_set_points(c->cnav, c->cnav_pts, 2);
      nav = true;
    }
    if (nav) lv_obj_clear_flag(c->cnav, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c->cnav, LV_OBJ_FLAG_HIDDEN);
  }

  // Markører: egen position (blå) og noder i kontaktfarven, med navn.
  static MapMarker mk[LvChat::kMapMarks];
  const int nm = (have && markers_) ? markers_(mk, LvChat::kMapMarks) : 0;
  int shown = 0;
  for (int i = 0; i < nm; ++i) {
    const double x = map_->worldX(mk[i].lon) - (map_->cx() - W / 2.0);
    const double y = map_->worldY(mk[i].lat) - (map_->cy() - H / 2.0);
    if (x < -20 || x > W + 20 || y < -20 || y > H + 20) continue;
    lv_obj_t* d = c->mdot[shown];
    lv_obj_t* l = c->mlab[shown];
    ++shown;
    const bool self = mk[i].kind == 0;
    const int sz = self ? 14 : 10;
    lv_obj_set_size(d, sz, sz);
    lv_obj_set_style_radius(d, mk[i].kind == 3 ? 2 : LV_RADIUS_CIRCLE, 0);  // delt lokation: firkant
    lv_obj_set_pos(d, int(x) - sz / 2, int(y) - sz / 2);
    lv_obj_set_style_bg_color(d, self ? c565(0x041F) : c565(mk[i].color), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_HIDDEN);
    if (!self && mk[i].label[0]) {
      setText(l, mk[i].label);
      lv_obj_set_style_text_color(l, c565(mk[i].color), 0);
      lv_obj_set_pos(l, int(x) + 7, int(y) - 8);
      lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
  }
  for (int i = shown; i < LvChat::kMapMarks; ++i) {
    lv_obj_add_flag(c->mdot[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->mlab[i], LV_OBJ_FLAG_HIDDEN);
  }
  // Kørselsretning: en pil (spids 18 px fra prikken) på egen position, når GPS'en giver en kurs.
  {
    bool arrow = false;
    if (nm > 0 && mk[0].kind == 0 && mk[0].heading >= 0) {
      const double x = map_->worldX(mk[0].lon) - (map_->cx() - W / 2.0);
      const double y = map_->worldY(mk[0].lat) - (map_->cy() - H / 2.0);
      const double a = mk[0].heading * 3.14159265358979 / 180.0;
      auto pt = [&](double r, double da) {
        return lv_point_t{lv_coord_t(x + r * std::sin(a + da)), lv_coord_t(y - r * std::cos(a + da))};
      };
      c->harrow_pts[0] = pt(10, -0.55);
      c->harrow_pts[1] = pt(19, 0);
      c->harrow_pts[2] = pt(10, 0.55);
      lv_line_set_points(c->harrow, c->harrow_pts, 3);
      lv_obj_move_foreground(c->harrow);
      arrow = true;
    }
    if (arrow) lv_obj_clear_flag(c->harrow, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c->harrow, LV_OBJ_FLAG_HIDDEN);
  }

  if (have) {
    char t[64];
    MapMarker self[1];
    const int tgt = caches_ ? caches_->target() : -1;
    if (note_[0]) {
      snprintf(t, sizeof(t), "%s", note_);
    } else if (tgt >= 0 && markers_ && markers_(self, 1) > 0 && self[0].kind == 0) {
      // Navigation: afstand og retning til målet.
      const Geocache& g = caches_->at(tgt);
      const double d = geoDistance(self[0].lat, self[0].lon, g.lat, g.lon);
      const double b = geoBearing(self[0].lat, self[0].lon, g.lat, g.lon);
      if (d < 1000) snprintf(t, sizeof(t), "-> %s  %d m %s (%.0f°)", g.code, int(d + 0.5), compass(b), b);
      else snprintf(t, sizeof(t), "-> %s  %.1f km %s (%.0f°)", g.code, d / 1000, compass(b), b);
    } else {
      snprintf(t, sizeof(t), "Zoom %d  %.5f, %.5f%s", map_->zoom(), map_->centerLat(), map_->centerLon(),
               map_follow_ ? "  (følger)" : "");
    }
    setText(c->mapinfo, t);
    lv_obj_clear_flag(c->mapinfo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c->mattr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(c->mapinfo);
    lv_obj_move_foreground(c->mattr);
    for (lv_obj_t* b : c->mbtn) {
      lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(b);
    }
    for (lv_obj_t* r : c->mcross) {
      if (map_follow_) {
        lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_clear_flag(r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(r);
      }
    }
  } else {
    for (lv_obj_t* r : c->mcross) lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->mapinfo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->mattr, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t* b : c->mbtn) lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
  }
}

void AppUi::lvDrawBottom() {
  LvChat* c = lvc_;

  // Specialtegn-bjælken.
  if (overlayOn()) {
    lv_obj_clear_flag(c->varbar, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < KeyMap::kMaxVariants; ++i) {
      lv_obj_t* v = c->var[i];
      if (i >= ov_n_) {
        lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_clear_flag(v, LV_OBJ_FLAG_HIDDEN);
      setText(v, ov_vars_[i]);
      const bool sel = i == ov_sel_;
      lv_obj_set_style_text_font(v, sel ? &dm_font_20 : &dm_font_14, 0);
      lv_obj_set_width(v, sel ? 26 : 18);
      lv_obj_set_style_bg_color(v, c565(color::Cyan), 0);
      lv_obj_set_style_bg_opa(v, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      lv_obj_set_style_radius(v, sel ? 6 : 0, 0);
      lv_obj_set_style_border_width(v, sel ? 2 : 0, 0);
      lv_obj_set_style_border_color(v, lv_color_black(), 0);
      lv_obj_set_style_pad_ver(v, sel ? 2 : 0, 0);
    }
    c->nvar = ov_n_;
  } else {
    lv_obj_add_flag(c->varbar, LV_OBJ_FLAG_HIDDEN);
  }

  // Emoji-ikonet (kun i Chat) og vælgeren.
  const bool chat = scr_ == ScrChat && !lv_panel_;
  if (!typing_) emo_open_ = false;
  const bool emo = emo_open_ && chat;
  if (chat) lv_obj_clear_flag(c->emobtn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(c->emobtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(c->emobtn, emo ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  if (emo) {
    lv_obj_clear_flag(c->emo, LV_OBJ_FLAG_HIDDEN);
    const int n = emojiCount();
    const int row = emo_sel_ / kEmoCols;
    if (row < c->emo_top) c->emo_top = row;
    if (row >= c->emo_top + kEmoRows) c->emo_top = row - kEmoRows + 1;
    for (int k = 0; k < kEmoCols * kEmoRows; ++k) {
      const int i = c->emo_top * kEmoCols + k;
      char u[5] = "";
      if (i < n) u[utf8(dm_emoji_at(g_order[i]), u)] = 0;
      setText(c->emocell[k], u);
      lv_obj_set_style_bg_opa(c->emocell[k], i == emo_sel_ ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
  } else {
    lv_obj_add_flag(c->emo, LV_OBJ_FLAG_HIDDEN);
  }

  // Indtastning.
  char in[sizeof(in_) + 48];
  if (typing_) {
    snprintf(in, sizeof(in), "%s%s: %s_", tgt_ == TgtMcDm ? "DM " : "", tgt_name_, in_);
    lv_obj_set_style_text_color(c->input, lv_color_white(), 0);
  } else if (scr_ == ScrContacts) {
    snprintf(in, sizeof(in), "Klik/Enter på en kontakt: skriv DM");
    lv_obj_set_style_text_color(c->input, c565(color::Gray), 0);
  } else {
    snprintf(in, sizeof(in), "Skriv for at sende til %s", tgt_name_[0] ? tgt_name_ : "...");
    lv_obj_set_style_text_color(c->input, c565(color::Gray), 0);
  }
  setText(c->input, in);
}

bool AppUi::lvTap(int x, int y) {
  LvChat* c = lvc_;
  if (!c) return false;
  if (menu_open_) {  // tandhjulsmenuen fanger tryk; på fanerne lukkes den, og fanen skiftes
    if (y >= kTabH) return lvMenuTap(x, y);
    menuClose();
  }
  if (y < kTabH) {  // fanerne: tre lige brede felter; tryk = ind i fanens liste
    const int i = x * kTabs / kW;
    const Screen s = Screen(kFirstTab + (i < 0 ? 0 : i >= kTabs ? kTabs - 1 : i));
    tab_focus_ = false;
    if (s != scr_) switchTo(s);  // også fra chatten: tilbage til listen (kladden gemmes)
    else if ((s == ScrSettings || s == ScrMap) && panel_) openPanel(nullptr);
    return true;
  }
  if (scr_ == ScrMap) {  // zoom-/GPS-knapperne (lidt større felt end knappen)
    for (int i = 0; i < 4; ++i) {
      if (!c->mbtn[i] || lv_obj_has_flag(c->mbtn[i], LV_OBJ_FLAG_HIDDEN)) continue;
      lv_area_t a;
      lv_obj_get_coords(c->mbtn[i], &a);
      if (x >= a.x1 - 6 && x <= a.x2 + 6 && y >= a.y1 - 4 && y <= a.y2 + 4) {
        handleMapKey(i == 0 ? '+' : i == 1 ? '-' : i == 2 ? 'c' : 'm');
        return true;
      }
    }
    return true;
  }
  if (scr_ == ScrChat) {
    lv_area_t a;
    lv_obj_get_coords(c->emobtn, &a);
    if (!lv_obj_has_flag(c->emobtn, LV_OBJ_FLAG_HIDDEN) && x <= a.x2 + 6 && y >= a.y1 - 4) {
      emojiToggle();
      return true;
    }
    if (!lv_obj_has_flag(c->emo, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_get_coords(c->emo, &a);
      if (y >= a.y1 && y <= a.y2) {
        for (int k = 0; k < kEmoCols * kEmoRows; ++k) {
          lv_obj_get_coords(c->emocell[k], &a);
          if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) {
            emo_sel_ = c->emo_top * kEmoCols + k;
            emojiInsert(emo_sel_);
            break;
          }
        }
        dirty_ = true;
        return true;
      }
    }
    // Tryk på titellinjen (med tandhjulet): kanalens eller DM-partnerens menu.
    lv_obj_get_coords(c->title, &a);
    if (y >= a.y1 && y <= a.y2 && gearAllowed()) {
      openTitleMenu();
      return true;
    }
    // Tryk på en boble med en lokation: vis/gem på kortet (tandhjulet virker stadig).
    for (int k = 0; k < c->nlocs; ++k) {
      lv_obj_get_coords(c->locobj[k], &a);
      if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) {
        openLocMenu(c->loclat[k], c->loclon[k]);
        return true;
      }
    }
    // Tryk på en andens besked i en kanal: tandhjulet = afsenderens menu, ellers
    // "@[navn] " i indtastningen.
    const uint32_t n = lv_obj_get_child_cnt(c->list);
    for (uint32_t k = 0; k < n; ++k) {
      lv_obj_t* ch = lv_obj_get_child(c->list, k);
      const intptr_t id = reinterpret_cast<intptr_t>(lv_obj_get_user_data(ch));
      if (id <= 0 || id > kMaxMsgs) continue;
      lv_obj_get_coords(ch, &a);
      if (y >= a.y1 && y <= a.y2) {
        lv_area_t g;
        if (c->gear[id]) lv_obj_get_coords(c->gear[id], &g);
        if (c->gear[id] && x >= g.x1 - 12) openUserMenu(c->mnames[id], true);  // fingervenlig margen
        else mentionInsert(c->mnames[id]);
        return true;
      }
    }
  }
  if (scr_ == ScrSettings && menu_) {
    lv_area_t a;
    for (int b = 0; b < 2; ++b) {
      lv_obj_get_coords(c->sbtn[b], &a);
      if (y >= a.y1 - 4 && y <= a.y2 + 4 && x >= a.x1 - 10 && x <= a.x2 + 10) {
        menu_->tapButton(b);
        dirty_ = true;
        return true;
      }
    }
    for (int r = 0; r < kSRows; ++r) {
      if (lv_obj_has_flag(c->srow[r], LV_OBJ_FLAG_HIDDEN)) continue;
      lv_obj_get_coords(c->srow[r], &a);
      if (y >= a.y1 && y <= a.y2) {
        menu_->tapRow(r);
        dirty_ = true;
        return true;
      }
    }
    return true;
  }
  if (scr_ == ScrChannels && chan_) {
    lv_area_t a;
    lv_obj_get_coords(c->title, &a);
    if (y >= a.y1 && y <= a.y2) {
      chan_->tapFilter();
      dirty_ = true;
      return true;
    }
    for (int r = 0; r < kKRows; ++r) {
      if (lv_obj_has_flag(c->krow[r], LV_OBJ_FLAG_HIDDEN)) continue;
      lv_obj_get_coords(c->krow[r], &a);
      if (y >= a.y1 && y <= a.y2) {
        chan_->tapRow(r);
        dirty_ = true;
        return true;
      }
    }
  }
  if (scr_ == ScrContacts && !typing_) {  // tryk = vælg; tryk på den valgte = skriv DM
    for (int r = 0; r < kCRows; ++r) {
      if (lv_obj_has_flag(c->crow[r], LV_OBJ_FLAG_HIDDEN)) continue;
      lv_area_t a;
      lv_obj_get_coords(c->crow[r], &a);
      if (y >= a.y1 && y <= a.y2) {
        const int i = c->ctop + r;
        if (i == sel_ && i < contactRows()) openContact(i);
        else sel_ = i;
        dirty_ = true;
        return true;
      }
    }
  }
  if (overlayOn() && !lv_obj_has_flag(c->varbar, LV_OBJ_FLAG_HIDDEN)) {
    for (int i = 0; i < c->nvar; ++i) {
      lv_area_t a;
      lv_obj_get_coords(c->var[i], &a);
      if (x >= a.x1 - 2 && x <= a.x2 + 2 && y >= a.y1 - 8 && y <= a.y2 + 8) {
        applyVariant(i);
        ov_n_ = 0;
        dirty_ = true;
        return true;
      }
    }
  }
  return true;
}

}  // namespace dm
