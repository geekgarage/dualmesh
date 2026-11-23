// SPDX-License-Identifier: MIT
#include "map/caches.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

#include "esp_heap_caps.h"
#include "storage/sd_log.h"

namespace dm {

namespace {
constexpr const char* kDir = "/sd/dualmesh/caches";
constexpr const char* kFound = "/sd/dualmesh/caches/found.txt";
constexpr uint32_t kPool = 512 * 1024;  // hints i PSRAM
constexpr double kPi = 3.14159265358979323846;

// Buffret læsning tegn for tegn.
struct Reader {
  FILE* f;
  char buf[4096];
  size_t n = 0, i = 0;
  int get() {
    if (i == n) {
      n = fread(buf, 1, sizeof(buf), f);
      i = 0;
      if (!n) return -1;
    }
    return uint8_t(buf[i++]);
  }
};

// Navnet uden navnerum ("groundspeak:name" -> "name").
const char* local(const char* tag) {
  const char* c = std::strrchr(tag, ':');
  return c ? c + 1 : tag;
}

// XML-entiteter -> UTF-8 (på stedet).
void unentity(char* s) {
  char* w = s;
  for (char* r = s; *r;) {
    if (*r == '&') {
      char* e = std::strchr(r, ';');
      if (e && e - r < 10) {
        *e = 0;
        const char* n = r + 1;
        unsigned long cp = 0;
        if (!std::strcmp(n, "amp")) cp = '&';
        else if (!std::strcmp(n, "lt")) cp = '<';
        else if (!std::strcmp(n, "gt")) cp = '>';
        else if (!std::strcmp(n, "quot")) cp = '"';
        else if (!std::strcmp(n, "apos")) cp = '\'';
        else if (n[0] == '#') cp = n[1] == 'x' ? std::strtoul(n + 2, nullptr, 16) : std::strtoul(n + 1, nullptr, 10);
        if (cp) {
          if (cp < 0x80) {
            *w++ = char(cp);
          } else if (cp < 0x800) {
            *w++ = char(0xC0 | (cp >> 6)), *w++ = char(0x80 | (cp & 0x3F));
          } else if (cp < 0x10000) {
            *w++ = char(0xE0 | (cp >> 12)), *w++ = char(0x80 | ((cp >> 6) & 0x3F)), *w++ = char(0x80 | (cp & 0x3F));
          } else {
            *w++ = char(0xF0 | (cp >> 18)), *w++ = char(0x80 | ((cp >> 12) & 0x3F));
            *w++ = char(0x80 | ((cp >> 6) & 0x3F)), *w++ = char(0x80 | (cp & 0x3F));
          }
          r = e + 1;
          continue;
        }
        *e = ';';
      }
    }
    *w++ = *r++;
  }
  *w = 0;
}

double attr(const char* tag, const char* name) {
  char key[16];
  snprintf(key, sizeof(key), "%s=", name);
  const char* p = std::strstr(tag, key);
  while (p && p > tag && p[-1] != ' ' && p[-1] != '\t' && p[-1] != '\n') p = std::strstr(p + 1, key);
  if (!p) return NAN;
  p += std::strlen(key);
  if (*p == '"' || *p == '\'') ++p;
  return std::strtod(p, nullptr);
}

uint8_t typeOf(const char* t) {
  if (strcasestr(t, "Traditional")) return Geocache::Traditional;
  if (strcasestr(t, "Multi")) return Geocache::Multi;
  if (strcasestr(t, "Unknown") || strcasestr(t, "Mystery") || strcasestr(t, "Quiz")) return Geocache::Mystery;
  if (strcasestr(t, "Letterbox")) return Geocache::Letterbox;
  if (strcasestr(t, "Wherigo")) return Geocache::Wherigo;
  if (strcasestr(t, "Event") || strcasestr(t, "CITO")) return Geocache::Event;
  if (strcasestr(t, "Earth")) return Geocache::Earth;
  if (strcasestr(t, "Virtual")) return Geocache::Virtual;
  if (strcasestr(t, "Webcam")) return Geocache::Webcam;
  return Geocache::Other;
}

uint8_t sizeOf(const char* s) {
  if (strcasestr(s, "Micro") || strcasestr(s, "Nano")) return 1;
  if (strcasestr(s, "Small")) return 2;
  if (strcasestr(s, "Regular") || strcasestr(s, "Medium")) return 3;
  if (strcasestr(s, "Large") || strcasestr(s, "Very large")) return 4;
  if (strcasestr(s, "Virtual") || strcasestr(s, "None") || strcasestr(s, "No container")) return 6;
  if (strcasestr(s, "Other")) return 5;
  return 0;
}
}  // namespace

double geoDistance(double la0, double lo0, double la1, double lo1) {
  const double r = 6371000.0, d2r = kPi / 180.0;
  const double dla = (la1 - la0) * d2r, dlo = (lo1 - lo0) * d2r;
  const double a = std::sin(dla / 2) * std::sin(dla / 2) +
                   std::cos(la0 * d2r) * std::cos(la1 * d2r) * std::sin(dlo / 2) * std::sin(dlo / 2);
  return 2 * r * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

double geoBearing(double la0, double lo0, double la1, double lo1) {
  const double d2r = kPi / 180.0;
  const double y = std::sin((lo1 - lo0) * d2r) * std::cos(la1 * d2r);
  const double x = std::cos(la0 * d2r) * std::sin(la1 * d2r) -
                   std::sin(la0 * d2r) * std::cos(la1 * d2r) * std::cos((lo1 - lo0) * d2r);
  double b = std::atan2(y, x) / d2r;
  return b < 0 ? b + 360 : b;
}

const char* compass(double d) {
  static const char* const k[] = {"N", "NØ", "Ø", "SØ", "S", "SV", "V", "NV"};
  return k[int((d + 22.5) / 45.0) & 7];
}

const char* Caches::typeName(uint8_t t) {
  static const char* const k[] = {"Traditionel", "Multi", "Mystery", "Letterbox", "Wherigo",
                                  "Event", "EarthCache", "Virtuel", "Webcam", "Anden"};
  return t < 10 ? k[t] : k[9];
}

const char* Caches::sizeName(uint8_t s) {
  static const char* const k[] = {"ukendt", "mikro", "lille", "mellem", "stor", "anden", "ingen"};
  return s < 7 ? k[s] : k[0];
}

uint16_t Caches::typeColor(uint8_t t) {
  switch (t) {
    case Geocache::Traditional: return 0x07E0;  // grøn
    case Geocache::Multi: return 0xFE00;        // gul/orange
    case Geocache::Mystery: return 0x041F;      // blå
    case Geocache::Event: return 0xF81F;        // magenta
    case Geocache::Earth: return 0xA145;        // brun
    default: return 0xC618;                     // grå
  }
}

uint32_t Caches::addHint(const char* s, size_t n) {
  while (n && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) ++s, --n;
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t')) --n;
  if (!n) return 0;
  if (!pool_) {
    pool_ = static_cast<char*>(heap_caps_malloc(kPool, MALLOC_CAP_SPIRAM));
    if (!pool_) return 0;
    pool_[0] = 0;
    pool_n_ = 1;
  }
  if (pool_n_ + n + 1 > kPool) return 0;
  const uint32_t off = pool_n_;
  std::memcpy(pool_ + off, s, n);
  pool_[off + n] = 0;
  pool_n_ += uint32_t(n + 1);
  return off;
}

void Caches::parseFile(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return;
  Reader* rd = static_cast<Reader*>(heap_caps_malloc(sizeof(Reader), MALLOC_CAP_SPIRAM));
  if (!rd) {
    fclose(f);
    return;
  }
  new (rd) Reader{f};
  static char tag[512], text[600];
  size_t tn = 0, xn = 0;
  bool in_wpt = false, have_code = false;
  Geocache cur{};
  const int before = n_;
  int c;
  while ((c = rd->get()) >= 0) {
    if (c != '<') {
      if (xn + 1 < sizeof(text)) text[xn++] = char(c);
      continue;
    }
    // Et mærke: læs til '>'.
    tn = 0;
    while ((c = rd->get()) >= 0 && c != '>')
      if (tn + 1 < sizeof(tag)) tag[tn++] = char(c);
    tag[tn] = 0;
    if (tag[0] == '!' || tag[0] == '?') {
      xn = 0;
      continue;
    }
    const bool close = tag[0] == '/';
    char name[48];
    {
      const char* t = close ? tag + 1 : tag;
      size_t k = 0;
      while (t[k] && t[k] != ' ' && t[k] != '/' && t[k] != '\t' && t[k] != '\n' && k + 1 < sizeof(name)) {
        name[k] = t[k];
        ++k;
      }
      name[k] = 0;
    }
    const char* ln = local(name);
    if (!close) {
      if (!std::strcmp(name, "wpt")) {
        in_wpt = true;
        have_code = false;
        cur = Geocache{};
        cur.lat = attr(tag, "lat");
        cur.lon = attr(tag, "lon");
      }
      xn = 0;
      continue;
    }
    text[xn] = 0;
    if (in_wpt) {
      unentity(text);
      if (!std::strcmp(name, "name") && !have_code) {
        snprintf(cur.code, sizeof(cur.code), "%s", text);
        have_code = true;
      } else if (!std::strcmp(ln, "name") && std::strchr(name, ':')) {
        snprintf(cur.name, sizeof(cur.name), "%s", text);  // groundspeak:name
      } else if (!std::strcmp(name, "urlname") && !cur.name[0]) {
        snprintf(cur.name, sizeof(cur.name), "%s", text);
      } else if (!std::strcmp(ln, "type")) {
        // wpt <type>Geocache|Traditional Cache</type> eller groundspeak:type
        const char* bar = std::strchr(text, '|');
        cur.type = typeOf(bar ? bar + 1 : text);
      } else if (!std::strcmp(ln, "container")) {
        cur.size = sizeOf(text);
      } else if (!std::strcmp(ln, "difficulty")) {
        cur.diff2 = uint8_t(std::strtod(text, nullptr) * 2 + 0.5);
      } else if (!std::strcmp(ln, "terrain")) {
        cur.terr2 = uint8_t(std::strtod(text, nullptr) * 2 + 0.5);
      } else if (!std::strcmp(ln, "encoded_hints") || !std::strcmp(ln, "hints")) {
        cur.hint = addHint(text, std::strlen(text));
      } else if (!std::strcmp(name, "sym") && strcasestr(text, "Found")) {
        cur.found = 1;
      } else if (!std::strcmp(name, "wpt")) {
        in_wpt = false;
        // Kun caches (ikke ekstra waypoints som parkering): koden starter med GC/OC/..., og
        // der er en type eller sværhed.
        if (!std::isnan(cur.lat) && !std::isnan(cur.lon) && cur.code[0] && (cur.diff2 || cur.terr2) &&
            n_ < kMax && find(cur.code) < 0) {
          if (!cur.name[0]) snprintf(cur.name, sizeof(cur.name), "%s", cur.code);
          const double la = cur.lat > 85.05 ? 85.05 : (cur.lat < -85.05 ? -85.05 : cur.lat);
          const double r = la * kPi / 180.0;
          cur.mx = float((cur.lon + 180.0) / 360.0);
          cur.my = float((1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / kPi) / 2.0);
          c_[n_++] = cur;
        }
      }
    }
    xn = 0;
  }
  heap_caps_free(rd);
  fclose(f);
  DLOG.printf("[CACHE] %s: %d caches\n", path, n_ - before);
}

void Caches::loadFound() {
  FILE* f = fopen(kFound, "r");
  if (!f) return;
  char line[160];
  while (fgets(line, sizeof(line), f)) {
    char code[16];
    unsigned long ts;
    char st;
    if (sscanf(line, "%15[^\t]\t%lu\t%c", code, &ts, &st) == 3) {
      const int i = find(code);
      if (i >= 0) c_[i].found = st == 'F' ? 1 : st == 'N' ? 2 : st == 'H' ? 3 : 0;
    }
  }
  fclose(f);
}

void Caches::load(bool sd) {
  sd_ = sd;
  n_ = 0;
  pool_n_ = 1;
  target_ = -1;
  if (!sd) return;
  if (!c_) c_ = static_cast<Geocache*>(heap_caps_calloc(kMax, sizeof(Geocache), MALLOC_CAP_SPIRAM));
  if (!c_) return;
  DIR* d = opendir(kDir);
  if (!d) return;
  while (struct dirent* e = readdir(d)) {
    const size_t l = std::strlen(e->d_name);
    if (l < 5 || strcasecmp(e->d_name + l - 4, ".gpx") != 0) continue;
    char p[300];
    snprintf(p, sizeof(p), "%s/%s", kDir, e->d_name);
    parseFile(p);
  }
  closedir(d);
  loadFound();
  ++version_;
  DLOG.printf("[CACHE] %d caches i alt\n", n_);
}

int Caches::find(const char* code) const {
  for (int i = 0; i < n_; ++i)
    if (!strcasecmp(c_[i].code, code)) return i;
  return -1;
}

void Caches::mark(int i, uint8_t st) {
  if (i < 0 || i >= n_) return;
  c_[i].found = st;
  ++version_;
  if (!sd_) return;
  mkdir(kDir, 0775);
  if (FILE* f = fopen(kFound, "a")) {  // nyeste linje gælder (læses i rækkefølge)
    fprintf(f, "%s\t%lu\t%c\t\n", c_[i].code, (unsigned long)time(nullptr),
            st == 1 ? 'F' : st == 2 ? 'N' : st == 3 ? 'H' : '-');
    fclose(f);
  }
  if (st == 3 && target_ == i) target_ = -1;  // en slettet cache er ikke længere et mål
  DLOG.printf("[CACHE] %s markeret %s\n", c_[i].code,
              st == 1 ? "fundet" : st == 2 ? "ikke fundet" : st == 3 ? "slettet" : "nulstillet");
}

int Caches::exportFieldNotes() const {
  if (!sd_) return -1;
  FILE* in = fopen(kFound, "r");
  if (!in) return 0;
  FILE* out = fopen("/sd/dualmesh/caches/geocache_visits.txt", "wb");
  if (!out) {
    fclose(in);
    return -1;
  }
  // UTF-16LE med BOM (som Garmin-enheder skriver den); koderne er ASCII.
  auto put = [&](const char* s) {
    for (; *s; ++s) {
      fputc(*s, out);
      fputc(0, out);
    }
  };
  fputc(0xFF, out);
  fputc(0xFE, out);
  int n = 0;
  char line[160];
  while (fgets(line, sizeof(line), in)) {
    char code[16];
    unsigned long ts;
    char st;
    if (sscanf(line, "%15[^\t]\t%lu\t%c", code, &ts, &st) != 3 || (st != 'F' && st != 'N')) continue;
    // Kun den nyeste status pr. cache (found.txt skrives løbende); en fortrudt markering springes over.
    const int i = find(code);
    if (i >= 0 && c_[i].found != (st == 'F' ? 1 : 2)) continue;
    const time_t t = time_t(ts);
    struct tm tm;
    gmtime_r(&t, &tm);
    char iso[24], row[96];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%MZ", &tm);
    snprintf(row, sizeof(row), "%s,%s,%s,\"\"\r\n", code, iso, st == 'F' ? "Found it" : "Didn't find it");
    put(row);
    ++n;
  }
  fclose(in);
  fclose(out);
  DLOG.printf("[CACHE] field notes: %d linjer i geocache_visits.txt\n", n);
  return n;
}

int Caches::nearest(double lat, double lon, int filter, int* out, int max) const {
  // Udvælg de max nærmeste (indsættelsessortering; max er lille).
  static double dist[64];
  if (max > 64) max = 64;
  int n = 0;
  for (int i = 0; i < n_; ++i) {
    const uint8_t f = c_[i].found;
    if (filter == 2 ? f != 3 : (f == 3 || (filter == 1 && f == 1))) continue;
    const double d = geoDistance(lat, lon, c_[i].lat, c_[i].lon);
    if (n == max && d >= dist[n - 1]) continue;
    int k = n < max ? n++ : max - 1;
    while (k > 0 && dist[k - 1] > d) {
      dist[k] = dist[k - 1];
      out[k] = out[k - 1];
      --k;
    }
    dist[k] = d;
    out[k] = i;
  }
  return n;
}

}  // namespace dm
