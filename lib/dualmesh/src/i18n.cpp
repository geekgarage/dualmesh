// SPDX-License-Identifier: MIT
#include "i18n.h"

#include <cstdlib>
#include <cstring>

namespace dm {
namespace i18n {

namespace {

constexpr int kMaxCaps = 8;

uint32_t fnv(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ uint8_t(*s++)) * 16777619u;
  return h;
}

// Et printf-felt fra p ('%' er p[0]); returnerer tegnet efter feltet, eller nullptr ("%%" og
// ugyldige felter er ikke felter).
const char* spec(const char* p) {
  if (p[0] != '%' || p[1] == '%' || !p[1]) return nullptr;
  const char* q = p + 1;
  while (*q && std::strchr("-+ #0", *q)) ++q;
  while ((*q >= '0' && *q <= '9') || *q == '*') ++q;
  if (*q == '.') {
    ++q;
    while ((*q >= '0' && *q <= '9') || *q == '*') ++q;
  }
  while (*q && std::strchr("hlLzjt", *q)) ++q;
  if (!*q || !std::strchr("diouxXeEfgGcsp", *q)) return nullptr;
  return q + 1;
}

// Tegn, et talfelt kan bestå af (nullptr = tekstfelt: alt).
const char* numChars(char conv) {
  switch (conv) {
    case 'd': case 'i': case 'u': return "-+0123456789";
    case 'x': case 'X': case 'p': return "0123456789abcdefABCDEFx";
    case 'o': return "01234567";
    case 'e': case 'E': case 'f': case 'g': case 'G': return "-+0123456789.eEinfINFa";
    default: return nullptr;
  }
}

// Rekursiv match af nøglen k mod teksten s. Tekstfelter fanger mindst ét tegn (korteste først);
// talfelter kun tal (længste først), så "%.5f%s" deles rigtigt.
bool match(const char* k, const char* s, const char** cap, size_t* caplen, int& ncap) {
  while (*k) {
    if (const char* e = spec(k)) {
      if (ncap >= kMaxCaps || !*s) return false;
      const int me = ncap++;
      cap[me] = s;
      const char* nums = numChars(e[-1]);
      if (nums) {
        size_t run = 0;
        while (s[run] && std::strchr(nums, s[run])) ++run;
        for (size_t l = run; l >= 1; --l) {
          caplen[me] = l;
          int save = ncap;
          if (match(e, s + l, cap, caplen, ncap)) return true;
          ncap = save;
        }
        --ncap;
        return false;
      }
      if (!*e) {  // sidste felt: resten
        caplen[me] = std::strlen(s);
        return true;
      }
      for (const char* t = s + 1; *t; ++t) {
        caplen[me] = size_t(t - s);
        int save = ncap;
        if (match(e, t, cap, caplen, ncap)) return true;
        ncap = save;
      }
      --ncap;
      return false;
    }
    const char want = (k[0] == '%' && k[1] == '%') ? '%' : k[0];
    if (*s != want) return false;
    k += (k[0] == '%' && k[1] == '%') ? 2 : 1;
    ++s;
  }
  return *s == 0;
}

const Catalog* g_active = nullptr;

}  // namespace

void Catalog::reset() {
  std::free(buf_);
  std::free(e_);
  std::free(idx_);
  buf_ = nullptr;
  e_ = nullptr;
  idx_ = nullptr;
  n_ = idx_cap_ = 0;
  name_[0] = 0;
}

int Catalog::load(const char* text, size_t len) {
  reset();
  buf_ = static_cast<char*>(std::malloc(len + 1));
  if (!buf_) return 0;
  std::memcpy(buf_, text, len);
  buf_[len] = 0;
  int lines = 1;
  for (size_t i = 0; i < len; ++i) lines += buf_[i] == '\n';
  e_ = static_cast<Entry*>(std::calloc(size_t(lines), sizeof(Entry)));
  if (!e_) return 0;
  // Linjerne skæres ud på stedet; "\n" og "\t" afkodes (bliver kortere, så det kan ske i bufferen).
  auto unescape = [](char* s) {
    char* w = s;
    for (char* r = s; *r; ++r) {
      if (r[0] == '\\' && r[1] == 'n') *w++ = '\n', ++r;
      else if (r[0] == '\\' && r[1] == 't') *w++ = '\t', ++r;
      else if (r[0] == '\\' && r[1] == '\\') *w++ = '\\', ++r;
      else *w++ = *r;
    }
    *w = 0;
  };
  char* p = buf_;
  if (len >= 3 && uint8_t(p[0]) == 0xEF && uint8_t(p[1]) == 0xBB && uint8_t(p[2]) == 0xBF) p += 3;
  while (*p) {
    char* nl = std::strchr(p, '\n');
    char* next = nl ? nl + 1 : p + std::strlen(p);
    if (nl) *nl = 0;
    if (nl && nl > p && nl[-1] == '\r') nl[-1] = 0;
    if (p[0] == '#') {
      if (!std::strncmp(p, "#name:", 6)) {
        const char* v = p + 6;
        while (*v == ' ') ++v;
        std::strncpy(name_, v, sizeof(name_) - 1);
      }
    } else if (char* tab = std::strchr(p, '\t')) {
      *tab = 0;
      char* val = tab + 1;
      unescape(p);
      unescape(val);
      if (*p && *val) {
        Entry& e = e_[n_++];
        e.key = p;
        e.val = val;
        e.pattern = false;
        uint16_t lit = 0;
        for (const char* k = p; *k;) {
          if (const char* x = spec(k)) {
            e.pattern = true;
            k = x;
          } else {
            k += (k[0] == '%' && k[1] == '%') ? 2 : 1;
            ++lit;
          }
        }
        e.lit = lit;
        if (!e.pattern) {  // fast tekst: "%%" er bare "%" (i både nøgle og oversættelse)
          char* both[2] = {p, val};
          for (char* s : both) {
            char* w = s;
            for (char* r = s; *r; ++r) {
              *w++ = *r;
              if (r[0] == '%' && r[1] == '%') ++r;
            }
            *w = 0;
          }
        }
        e.hash = fnv(p);
      }
    }
    p = next;
  }
  // Hashtabel over de faste nøgler.
  idx_cap_ = 16;
  while (idx_cap_ < 2 * n_ + 1) idx_cap_ *= 2;
  idx_ = static_cast<uint32_t*>(std::calloc(size_t(idx_cap_), sizeof(uint32_t)));
  if (!idx_) {
    idx_cap_ = 0;
    return n_;
  }
  for (int i = 0; i < n_; ++i) {
    if (e_[i].pattern) continue;
    uint32_t h = e_[i].hash & uint32_t(idx_cap_ - 1);
    while (idx_[h]) h = (h + 1) & uint32_t(idx_cap_ - 1);
    idx_[h] = uint32_t(i + 1);
  }
  return n_;
}

const Catalog::Entry* Catalog::exact(const char* s) const {
  if (!idx_cap_) return nullptr;
  const uint32_t hs = fnv(s);
  uint32_t h = hs & uint32_t(idx_cap_ - 1);
  while (idx_[h]) {
    const Entry& e = e_[idx_[h] - 1];
    if (e.hash == hs && !std::strcmp(e.key, s)) return &e;
    h = (h + 1) & uint32_t(idx_cap_ - 1);
  }
  return nullptr;
}

bool Catalog::matchEntry(const Entry& e, const char* s, const char** cap, size_t* caplen, int& ncap) const {
  ncap = 0;
  // Hurtig afvisning: første faste tegn.
  if (!spec(e.key) && e.key[0] != '%' && e.key[0] != s[0]) return false;
  return match(e.key, s, cap, caplen, ncap);
}

bool Catalog::translate(const char* src, char* out, size_t n) const {
  if (!src || !out || !n) return false;
  if (const Entry* e = exact(src)) {
    std::strncpy(out, e->val, n - 1);
    out[n - 1] = 0;
    return true;
  }
  const Entry* best = nullptr;
  const char* bcap[kMaxCaps];
  size_t blen[kMaxCaps];
  int bn = 0;
  for (int i = 0; i < n_; ++i) {
    const Entry& e = e_[i];
    if (!e.pattern || (best && e.lit <= best->lit)) continue;
    const char* cap[kMaxCaps];
    size_t len[kMaxCaps];
    int nc = 0;
    if (!matchEntry(e, src, cap, len, nc)) continue;
    best = &e;
    bn = nc;
    std::memcpy(bcap, cap, sizeof(cap));
    std::memcpy(blen, len, sizeof(len));
  }
  if (!best) return false;
  // Oversættelsen med de fundne stykker indsat (et stykke, der selv er en nøgle, oversættes).
  size_t w = 0;
  int ci = 0;
  auto put = [&](const char* t, size_t l) {
    for (size_t i = 0; i < l && w + 1 < n; ++i) out[w++] = t[i];
  };
  for (const char* v = best->val; *v;) {
    if (const char* x = spec(v)) {
      if (ci < bn) {
        char piece[160];
        const size_t pl = blen[ci] < sizeof(piece) - 1 ? blen[ci] : sizeof(piece) - 1;
        std::memcpy(piece, bcap[ci], pl);
        piece[pl] = 0;
        const Entry* pe = exact(piece);
        if (pe) put(pe->val, std::strlen(pe->val));
        else put(piece, pl);
      }
      ++ci;
      v = x;
    } else if (v[0] == '%' && v[1] == '%') {
      put("%", 1);
      v += 2;
    } else {
      put(v, 1);
      ++v;
    }
  }
  out[w] = 0;
  return true;
}

void setActive(const Catalog* c) { g_active = c; }
const Catalog* active() { return g_active; }

const char* tr(const char* src, char* out, size_t n) {
  if (!g_active || !src || !*src) return src;
  return g_active->translate(src, out, n) ? out : src;
}

}  // namespace i18n
}  // namespace dm
