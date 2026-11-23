// SPDX-License-Identifier: MIT
// Test af oversættelsen (lib/dualmesh/src/i18n.*) og lokationsteksten (geo_text.h).
#include <cstdio>
#include <cstring>

#include "geo_text.h"
#include "i18n.h"

static int fails = 0;

static void expect(const char* src, const char* want, const dm::i18n::Catalog& c) {
  char out[256];
  dm::i18n::setActive(&c);
  const char* got = dm::i18n::tr(src, out, sizeof(out));
  if (std::strcmp(got, want) != 0) {
    std::printf("FEJL: \"%s\" -> \"%s\" (ventet \"%s\")\n", src, got, want);
    ++fails;
  }
}

int main() {
  const char* lang =
      "#name: English\n"
      "# kommentar\n"
      "Kanal\tChannel\n"
      "fra\toff\n"
      "til\ton\n"
      "Sendt til %.30s\tSent to %s\n"
      "DM sendt til %.26s\tDM sent to %s\n"
      "Zoom %d  %.5f, %.5f%s\tZoom %d  %.5f, %.5f%s\n"
      "  (følger)\t  (following)\n"
      "Del position: %s\tShare position: %s\n"
      "100 %% sikkert\t100 %% safe\n"
      "To\\nlinjer\tTwo\\nlines\n"
      "Ryd alle gemte prikker (%d)\tClear all saved points (%d)\n";
  dm::i18n::Catalog c;
  const int n = c.load(lang, std::strlen(lang));
  if (n != 11 || std::strcmp(c.name(), "English") != 0) {
    std::printf("FEJL: %d poster, navn \"%s\"\n", n, c.name());
    ++fails;
  }
  expect("Kanal", "Channel", c);
  expect("Ukendt tekst", "Ukendt tekst", c);
  expect("Sendt til Public", "Sent to Public", c);
  expect("DM sendt til Heltec", "DM sent to Heltec", c);  // mere specifik end "Sendt til"
  expect("Del position: fra", "Share position: off", c);  // stykket er selv en nøgle
  expect("Zoom 12  55.67567, 12.56756  (følger)", "Zoom 12  55.67567, 12.56756  (following)", c);
  expect("100 % sikkert", "100 % safe", c);
  expect("To\nlinjer", "Two\nlines", c);
  expect("Ryd alle gemte prikker (7)", "Clear all saved points (7)", c);
  dm::i18n::setActive(nullptr);
  char out[64];
  if (std::strcmp(dm::i18n::tr("Kanal", out, sizeof(out)), "Kanal") != 0) {
    std::printf("FEJL: dansk skal være uændret\n");
    ++fails;
  }

  // Den rigtige engelske sprogfil (hvis testen køres fra repoets rod eller build/).
  // Den indbyggede engelske tekst fra src/ui/lang_builtin.h (som firmwaren bruger den).
  const char* const paths[] = {"src/ui/lang_builtin.h", "../src/ui/lang_builtin.h"};
  for (const char* path : paths) {
    FILE* f = std::fopen(path, "rb");
    if (!f) continue;
    static char buf[300000];
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = 0;
    char* s = std::strstr(buf, "kText_en[] = R\"DMLANG(");
    char* e = s ? std::strstr(s, ")DMLANG\"") : nullptr;
    if (!s || !e) break;
    s += std::strlen("kText_en[] = R\"DMLANG(");
    dm::i18n::Catalog en;
    en.load(s, size_t(e - s));
    expect("Zoom 12  55.67567, 12.56756  (følger)", "Zoom 12  55.67567, 12.56756  (following)", en);
    expect("Beskeder", "Messages", en);
    expect("Brug MeshCore", "Use MeshCore", en);
    expect("Sendt til Public", "Sent to Public", en);
    // USB-drev-skærmen: tal sat ind før oversættelsen, også et tomt %s til sidst.
    expect("Kortet (3781 MB) vises som et drev på PC'en. Radio og net er slukket imens.",
           "The card (3781 MB) shows up as a drive on the PC. Radio and networks are off meanwhile.", en);
    expect("Læst 565 KB, skrevet 0 KB", "Read 565 KB, written 0 KB", en);
    expect(", FEJL ved læsning/skrivning", ", ERROR reading/writing", en);
    expect("NULSTIL", "RESET", en);
    break;
  }

  // Lokation i tekst.
  double la, lo;
  char t[64];
  dm::geo::formatLoc(t, sizeof(t), 55.675671, 12.567561);
  if (!dm::geo::parseLoc(t, la, lo) || la < 55.6756 || la > 55.6757 || lo < 12.5675 || lo > 12.5676) {
    std::printf("FEJL: formatLoc/parseLoc \"%s\"\n", t);
    ++fails;
  }
  if (!dm::geo::parseLoc("Mød mig her: -33.86880,151.20930 ok", la, lo) || la > -33.8 || lo < 151.2) {
    std::printf("FEJL: parseLoc negativ\n");
    ++fails;
  }
  if (dm::geo::parseLoc("Version 1.18.0, build 2.7.14", la, lo) || dm::geo::parseLoc("kl. 12.30, 14.45", la, lo)) {
    std::printf("FEJL: parseLoc fandt en lokation, der ikke er der\n");
    ++fails;
  }
  std::printf(fails ? "test_i18n: %d FEJL\n" : "test_i18n: OK\n", fails);
  return fails ? 1 : 0;
}
