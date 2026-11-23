// SPDX-License-Identifier: MIT
#include "ui/keymap.h"

#include <cstdio>
#include <cstring>

#include "ui/keyboards_builtin.h"

namespace dm {

bool KeyMap::readSd(const char* id, char* buf, int size) {
  char path[96];
  snprintf(path, sizeof(path), "/sd/dualmesh/keyboards/%s.kbd", id);
  FILE* f = fopen(path, "r");
  if (!f) return false;
  const size_t n = fread(buf, 1, size_t(size - 1), f);
  fclose(f);
  buf[n] = 0;
  return n > 0;
}

bool KeyMap::loadText(const char* text) {
  const char* p = text;
  bool any = false;
  while (*p) {
    const char* eol = strchr(p, '\n');
    const size_t len = eol ? size_t(eol - p) : strlen(p);
    // "<tast>=<varianter>": tasten er præcis ét ASCII-tegn.
    if (len >= 3 && p[1] == '=' && p[0] != '#' && uint8_t(p[0]) >= 32 && uint8_t(p[0]) < 127) {
      const int k = p[0] - 32;
      const char* v = p + 2;
      const char* end = p + len;
      while (v < end) {
        while (v < end && (*v == ' ' || *v == '\r' || *v == '\t')) ++v;
        const char* s = v;
        while (v < end && *v != ' ' && *v != '\r' && *v != '\t') ++v;
        const size_t n = size_t(v - s);
        if (n == 0 || n > 4) continue;
        bool dup = false;
        for (int i = 0; i < count_[k]; ++i)
          if (strlen(var_[k][i]) == n && !memcmp(var_[k][i], s, n)) dup = true;
        if (!dup && count_[k] < kMaxVariants) {
          memcpy(var_[k][count_[k]], s, n);
          var_[k][count_[k]][n] = 0;
          ++count_[k];
          any = true;
        }
      }
    }
    p += len + (eol ? 1 : 0);
  }
  return any;
}

int KeyMap::begin(const char* active_csv, bool sd_ok) {
  memset(count_, 0, sizeof(count_));
  summary_[0] = 0;
  int found = 0;
  char ids[96];
  snprintf(ids, sizeof(ids), "%s", active_csv && *active_csv ? active_csv : "da,latin");
  static char buf[4096];
  for (char* id = strtok(ids, ", "); id; id = strtok(nullptr, ", ")) {
    bool ok = false;
    const char* src = "";
    if (sd_ok && readSd(id, buf, sizeof(buf))) {
      ok = loadText(buf);
      src = "SD";
    } else {
      for (int i = 0; i < kbd::kBuiltinCount; ++i)
        if (!strcmp(kbd::kBuiltin[i].id, id)) {
          ok = loadText(kbd::kBuiltin[i].text);
          src = "indbygget";
        }
    }
    const size_t n = strlen(summary_);
    snprintf(summary_ + n, sizeof(summary_) - n, "%s%s(%s)", n ? " " : "", id, ok ? src : "MANGLER");
    found += ok;
  }
  return found;
}

int KeyMap::variants(char key, const char* out[], int max) const {
  if (uint8_t(key) < 32 || uint8_t(key) >= 127) return 0;
  const int k = key - 32;
  int n = 0;
  for (; n < count_[k] && n < max; ++n) out[n] = var_[k][n];
  return n;
}

}  // namespace dm
