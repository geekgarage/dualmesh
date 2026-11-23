// SPDX-License-Identifier: MIT
// DualMesh – farve pr. kontakt (brugerens ønske 15. nov 2026), så afsendere er lette
// at skelne i Chat, og så en ny identitet ses: farven afledes af kontaktens offentlige
// nøgle (første 6 byte), ikke af navnet. Brugeren kan vælge en anden farve pr. kontakt
// (Indst. > Beskeder > Kontaktfarver); valget gemmes i /sd/dualmesh/meshcore/colors.txt
// ("nøgle-hex<TAB>farve" pr. linje).
// Kanalbeskeder har kun afsenderens navn: farven findes via kontakten med det navn.
// Har flere kontakter samme navn (fx efter ny identitet), markeres afsenderen med '!'.
#pragma once
#include <cstdint>

namespace dm {

class McNode;

namespace ccolor {

constexpr int kCount = 13;
uint16_t rgb(int idx);
const char* name(int idx);

void load(bool sd);
// Farve for en nøgle (6 byte er nok): brugerens valg eller automatisk.
int forKey(const uint8_t* pub6);
int autoFor(const uint8_t* pub6);
bool custom(const uint8_t* pub6);
void set(const uint8_t* pub6, int idx);  // -1 = automatisk igen
// Farve ud fra navn (kanalbeskeder). dup = flere kontakter med det navn; known = fundet.
int forName(const char* name, bool* dup, bool* known);
// Navneopslag bygges fra kontakterne (kald jævnligt, fx hvert 10. s).
void refresh(McNode* node);
// Tæller, der stiger, når en farve kan have ændret sig (nyt navneopslag eller valg).
uint32_t generation();

}  // namespace ccolor
}  // namespace dm
