// SPDX-License-Identifier: MIT
// DualMesh – oversættelse af brugerfladen. Dansk er kildesproget: de danske tekster i koden er
// nøglerne, og en sprogfil (én pr. sprog) har linjer "dansk<TAB>oversættelse". Oversættelsen sker,
// når teksten tegnes, så koden ikke skal ændres for hvert nyt sprog.
//
// Pladsholdere: printf-felter i nøglen (%s, %d, %.30s, %lu, ...) matcher hvad som helst, og de
// samme felter i oversættelsen (i samme rækkefølge) får det fundne indsat. "Sendt til %.30s" /
// "Sent to %s" oversætter altså "Sendt til Public" til "Sent to Public". Et fundet stykke, der selv
// er en hel nøgle (fx "fra"), oversættes også. Uden match vises den danske tekst.
//
// Filformat: UTF-8, én post pr. linje, TAB mellem dansk og oversættelse, "\n" = linjeskift,
// "\t" = tabulator, linjer der starter med '#' er kommentarer. Første linje må være
// "#name: English" (navnet i sprogmenuen). Hardware-uafhængig (testes på PC).
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {
namespace i18n {

class Catalog {
 public:
  ~Catalog() { reset(); }
  // Indlæser en sprogfil (teksten kopieres). Returnerer antal poster.
  int load(const char* text, size_t len);
  void reset();
  int size() const { return n_; }
  const char* name() const { return name_; }
  // Oversætter src til out. false = ingen oversættelse (out = src).
  bool translate(const char* src, char* out, size_t n) const;

 private:
  struct Entry {
    const char* key;   // dansk (med printf-felter)
    const char* val;   // oversættelse
    uint32_t hash;     // kun for nøgler uden felter
    uint16_t lit;      // antal faste tegn i nøglen (mere = mere specifik)
    bool pattern;      // har printf-felter
  };
  const Entry* exact(const char* s) const;
  bool matchEntry(const Entry& e, const char* s, const char** cap, size_t* caplen, int& ncap) const;
  char* buf_ = nullptr;
  Entry* e_ = nullptr;
  int n_ = 0;
  uint32_t* idx_ = nullptr;  // åben hashtabel over nøgler uden felter (indeks + 1)
  int idx_cap_ = 0;
  char name_[32] = "";
};

// Det aktive sprog (nullptr = dansk). tr() bruges af tegnekoden.
void setActive(const Catalog* c);
const Catalog* active();
// Oversætter til out (højst n byte inkl. 0) og returnerer out, eller src selv, hvis dansk/ukendt.
const char* tr(const char* src, char* out, size_t n);

}  // namespace i18n
}  // namespace dm
