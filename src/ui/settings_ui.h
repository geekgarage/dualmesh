// SPDX-License-Identifier: MIT
// DualMesh – indstillingsmenu i tekst-UI'et (fanen "Indst.").
//
// Viser alle brugerindstillinger fra settings.ini som en liste (række 1-12),
// en hjælpelinje (række 13) og knapperne Gem / Annuller (række 14).
// Indstillingerne er grupperet (Status, WiFi, Bluetooth, GPS, Profil, MeshCore, Meshtastic,
// Beskeder, Skærm, Tastatur og trackball, System – brugerens rækkefølge); hver gruppe har sin farve og en
// tom linje imellem. PRINCIP (brugerens krav): en ny indstilling lægges i gruppen
// for den ting, den hører til – også strømstyring, der kun gælder én ting (fx
// skærmens sluk-tid ligger under Skærm). Gruppen "Strømstyring" er fjernet (brugerens
// ønske 15. nov 2026); kommer der generelle strømindstillinger, aftales gruppen igen.
// PRINCIP: MeshCore står altid før Meshtastic i menuer og lister.
//   Trackball op/ned: vælg.  Klik: til/fra (ja/nej) eller begynd at ændre.
//   Under ændring: trackball venstre/højre (eller op/ned) skifter værdi,
//   tekst skrives på tastaturet; klik eller Enter = færdig.
//   Gem skriver de ændrede værdier til SD og anvender dem (radioindstillinger
//   kræver genstart, som main.cpp så foretager). Annuller fortryder alle ændringer.
// Ændringer gælder først efter Gem.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

class SdStore;
class StatusDisplay;

class SettingsUi {
 public:
  // Kaldes efter Gem. restart = en radioindstilling er ændret.
  using SavedFn = void (*)(bool restart);
  // Handling (fx "Ryd log"): udføres straks efter to klik; returnerer en kort note.
  // Nøgler, der starter med '>', åbner en underskærm og udføres efter ét klik.
  using ActionFn = const char* (*)(const char* key);
  // Ekstra tjek før Gem (fx airtime); returnerer en fejlnote eller nullptr.
  using CheckFn = const char* (*)(const SettingsUi& ui);
  // Hemmelige værdier (mqtt_pass) gemmes ikke i settings.ini, men gives til denne
  // (krypteret fil); feltet står tomt bagefter. Sættes efter begin().
  using SecretFn = void (*)(const char* key, const char* value);
  void setSecretFn(SecretFn f) { on_secret_ = f; }

  void begin(SdStore* store, SavedFn on_saved, ActionFn on_action = nullptr,
             CheckFn on_check = nullptr);
  // Den redigerede (endnu ikke gemte) værdi; "" hvis nøglen ikke findes.
  const char* edited(const char* key) const;
  void load();  // henter værdierne fra SdStore (fortryder ændringer)

  // Det, der skal vises (fælles for tekstvisningen og LVGL). rows = synlige rækker.
  static constexpr int kViewRows = 10;
  struct ViewRow {
    enum Kind : uint8_t { Header, Item, Empty, Blank } kind;
    char label[48];
    char value[96];
    uint16_t color;     // gruppens farve
    bool changed, locked, sel;
  };
  struct View {
    int nrows;
    ViewRow rows[kViewRows];
    char help[200];
    bool help_dim;
    char hint[64];
    uint16_t hint_fg;
    bool save_sel, cancel_sel, changed, editing;
  };
  void view(View& v, int rows);
  // Tryk fra LVGL: række nr. r af de synlige, eller knap 0 = Gem, 1 = Annuller.
  void tapRow(int r);
  void tapButton(int b);

  void key(char c);       // tastatur
  // Tekstfeltet, der redigeres nu, hvis det må have specialtegn (æøå ...); ellers false.
  bool textField(char*& buf, size_t& cap);
  void stepY(int dir);    // trackball op/ned
  bool atTop() const;     // øverste række og intet redigeres: op går til fanelinjen
  void collapse();        // trackball venstre: fold den åbne gruppe sammen (markøren på overskriften)
  size_t textLen() const; // længden af tekstfeltet, der redigeres (0 = intet)
  bool wantsX() const;    // true: venstre/højre bruges her (ellers skifter fanen)
  void stepX(int dir);    // trackball venstre/højre
  void click();
  bool changed() const;   // ændringer, der ikke er gemt

 private:
  enum class Type : uint8_t { Bool, Int, Enum, Text, Action };
  enum Group : uint8_t {
    GStatus, GWifi, GBluetooth, GGps, GMap, GProfile, GMessages, GMeshCore, GMeshtastic, GMqtt, GScreen, GInput,
    GSystem,
    GCount
  };
  struct Def {
    const char* key;
    const char* label;
    Type type;
    int min, max, step;   // Int; Text: max = længde
    uint8_t opts;         // Enum: hvilken liste
    bool restart;         // kræver genstart (radio)
    const char* zero;     // Int: tekst for 0 / Text: tekst for tom værdi
    Group group;
  };
  // Visningsrækker: gruppeoverskrift, indstilling, "ingen endnu" eller tom linje.
  struct Row {
    enum Kind : uint8_t { Header, Item, Empty, Blank } kind;
    uint8_t group;
    int16_t def;  // Item: indeks i kDefs
  };
  static constexpr int kMaxRows = 160;  // indstillinger + 2 pr. gruppe (overskrift, tom linje); se static_assert i .cpp
  void buildRows();
  int rowOf(int def) const;
  void scrollTo(int def);
  static const Def kDefs[];
  static const int kCount;
  // Liste i række 1-8, forklaring i 9-12, hjælp i 13, Gem/Annuller i 14.
  static constexpr int kVal = 128, kListRows = 8, kFirstRow = 1, kHelpRows = 4;

  int optCount(uint8_t list) const;
  const char* opt(uint8_t list, int i) const;
  void change(int i, int dir);
  void save();
  void cancel();
  void note(const char* text);
  bool isChanged(int i) const;
  // Nettet er slået fra (MeshCore/Meshtastic): indstillingen er nedtonet og låst.
  // Returnerer en forklaring eller nullptr.
  const char* lockedBy(int i) const;

  SdStore* store_ = nullptr;
  SavedFn on_saved_ = nullptr;
  ActionFn on_action_ = nullptr;
  CheckFn on_check_ = nullptr;
  SecretFn on_secret_ = nullptr;
  int armed_ = -1;          // handling, der venter på andet klik
  uint32_t armed_ms_ = 0;
  char (*val_)[kVal] = nullptr;   // PSRAM: redigerede værdier
  char (*orig_)[kVal] = nullptr;  // værdierne i settings.ini
  // sel_: indstilling nr.; kCount = Gem, kCount + 1 = Annuller; negativ = gruppeoverskrift
  // (-1 - gruppe). top_ = øverste viste række.
  int sel_ = -1, top_ = 0;
  // Grupperne er foldet sammen fra opstart (brugerens ønske 17. nov 2026); klik åbner/lukker.
  bool open_[GCount] = {};
  bool onHeader() const { return sel_ < 0; }
  int headerGroup() const { return -1 - sel_; }
  bool onItem() const { return sel_ >= 0 && sel_ < kCount; }
  int selRow() const;  // rækken for sel_ (overskrift eller indstilling)
  int view_rows_ = kListRows;     // synlige rækker i den aktuelle visning
  Row rows_[kMaxRows] = {};
  int n_rows_ = 0;
  bool editing_ = false;
  char note_[48] = "";
  uint32_t note_ms_ = 0;
};

}  // namespace dm
