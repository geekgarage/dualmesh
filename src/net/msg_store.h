// SPDX-License-Identifier: MIT
// DualMesh – beskeder til UI'et, opdelt i samtaler (en kanal eller en DM på ét net).
//
// - Seneste beskeder fra alle samtaler i en ringbuffer i PSRAM (bruges uden SD-kort).
// - Hver samtale gemmes på SD i /sd/dualmesh/msgs/<id>.txt (én linje pr. besked,
//   tab-separeret: unix-tid, art, status, hop, afsender/kanal, tekst). Filen
//   beskæres til grænsen (msgs_per_channel / msgs_per_dm), når den åbnes og når
//   den er blevet for stor.
// - "Visningen" er den åbne samtale (Chat-fanen): indlæses fra SD ved skift,
//   ellers fra ringbufferen. Nye beskeder i den åbne samtale lægges også i den.
// - Ulæste tælles pr. samtale (i RAM; nulstilles ved genstart).
// Samtale-id: "mcc_xxxxxxxx" (MeshCore-kanal), "mcd_xxxxxxxxxxxx" (MeshCore-DM,
// offentlig nøgles første 6 byte), "mtc_xxxxxxxx" (Meshtastic-kanal).
// Bruges kun fra loop()-tasken.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

struct Msg {
  enum Kind : uint8_t { ChannelIn, ChannelOut, DirectIn, DirectOut };
  enum Status : uint8_t { None, Queued, Delivered, Failed };
  uint32_t rx_ms = 0;     // millis() da den kom ind / blev sendt (0 = hentet fra SD)
  uint32_t ts = 0;        // unix-tid (0 = uret var ikke sat)
  uint32_t seq = 0;       // løbenummer (til statusopdatering af sendte DM'er)
  uint8_t net = 1;        // bit i NetId (MeshCore = 1, Meshtastic = 2)
  Kind kind = ChannelIn;
  Status status = None;
  uint8_t hops = 0;
  // Modtaget signal (kun for nye indgående beskeder; gemmes ikke på SD). Bruges af MQTT-broen.
  bool has_sig = false;
  int16_t rssi = 0;       // dBm
  int8_t snr_q4 = 0;      // SNR x 4 (dB)
  char conv[20] = "";     // samtale-id
  char peer[32] = "";     // kanalnavn, eller kontaktens navn ved DM
  char text[168] = "";    // som modtaget (kanal: "afsender: tekst")
};

// 32-bit FNV-1a (til samtale-id'er ud fra en kanalnøgle; ikke kryptografisk).
uint32_t fnv1a(const void* data, size_t n, uint32_t h = 2166136261u);

class MsgStore {
 public:
  static constexpr int kMaxLimit = 1000;  // højeste grænse pr. samtale
  static constexpr int kMaxConvs = 64;    // samtaler med ulæst-tælling

  bool begin(size_t capacity = 100);
  void setSd(bool mounted) { sd_ = mounted; }
  void setLimits(int per_channel, int per_dm);
  int limitFor(const char* conv) const;
  // Tidsgrænse for visning (dage, 0 = ingen); det antal eller den tid, der rammer
  // først, bestemmer hvad Chat viser (brugerens ønske 15. nov 2026).
  void setDayLimits(int channel_days, int dm_days);
  // Ældste unix-tid, der vises i samtalen (0 = alle).
  uint32_t cutoffFor(const char* conv, uint32_t now_ts) const;
  // Ryd én samtale / alle samtaler med et præfiks ("mcc_", "mtc_", "mcd_"):
  // filen på SD, beskederne i RAM og de ulæste. Returnerer antal samtaler.
  int clear(const char* conv);
  int clearPrefix(const char* prefix);

  // Ny besked: i ringbufferen, i visningen (hvis samtalen er åben), på SD (persist)
  // og som ulæst (hvis indgående og samtalen ikke er åben). Returnerer seq.
  uint32_t push(const Msg& m, bool persist = true);
  // Kaldes for hver ny besked (fx MQTT-broen).
  using PushHook = void (*)(const Msg& m);
  void setPushHook(PushHook h) { hook_ = h; }
  // Ny status for en sendt besked (fx DM kvitteret); write = gem den nu på SD.
  void setStatus(uint32_t seq, Msg::Status st, bool write);

  // Åben samtale (Chat-fanen).
  void open(const char* conv, const char* title);
  const char* openId() const { return open_id_; }
  const char* openTitle() const { return open_title_; }
  size_t viewSize() const { return v_count_; }
  const Msg& viewAt(size_t i) const;  // 0 = ældste
  bool viewActive() const { return view_active_; }  // Chat-fanen vises (læses straks)
  void setViewActive(bool on);

  uint32_t unread(const char* conv) const;
  uint32_t totalUnread() const;
  uint32_t unreadPrefix(const char* prefix) const;  // fx "mcd_" = alle DM'er
  // Skriver 3 x grænsen + 5 testbeskeder til samtalen "zz_selvtest" (SD), åbner den
  // igen og tjekker antal, rækkefølge, beskæring og ulæste; sletter filen bagefter
  // og genåbner den samtale, der var åben. Monitor: M.
  bool selfTest();
  uint32_t version() const { return version_; }  // ændres ved hver ny/ændret besked
  void touch() { ++version_; }
  // Antal ældre beskeder i ringbufferen (bruges ikke af UI'et længere).
  size_t size() const { return count_; }
  const Msg& at(size_t i) const;

 private:
  struct Conv {
    char id[20];
    uint32_t unread;
  };
  Conv* conv(const char* id, bool create);
  void appendFile(const Msg& m);
  void loadFile(const char* id);
  void trimFile(const char* id, int keep);
  void path(const char* id, char* out, size_t n, const char* ext = "txt") const;
  Msg& viewAdd();

  Msg* buf_ = nullptr;  // ringbuffer, alle samtaler
  size_t cap_ = 0, head_ = 0, count_ = 0;
  Msg* view_ = nullptr;  // åben samtale (kMaxLimit)
  size_t v_head_ = 0, v_count_ = 0, v_cap_ = 0;
  char open_id_[20] = "";
  char open_title_[40] = "";
  bool view_active_ = false;
  Conv convs_[kMaxConvs] = {};
  int n_convs_ = 0;
  int per_channel_ = 200, per_dm_ = 200;
  int days_channel_ = 0, days_dm_ = 0;
  void forget(const char* id_or_prefix, bool prefix);
  bool sd_ = false;
  uint32_t seq_ = 0;
  uint32_t version_ = 0;
  PushHook hook_ = nullptr;
};

}  // namespace dm
