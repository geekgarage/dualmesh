// SPDX-License-Identifier: MIT
#include "ui/lang.h"

#include <dirent.h>
#include <strings.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "i18n.h"
#include "storage/sd_log.h"
#include "ui/lang_builtin.h"

namespace dm {
namespace lang {

namespace {
constexpr const char* kDir = "/sd/dualmesh/lang";
constexpr int kMax = 12;
struct Lang {
  char id[12];
  char name[32];
  const char* builtin;  // nullptr = fil på SD
};
Lang g_langs[kMax];
int g_n = 0;
i18n::Catalog g_cat;

// "#name: English" fra starten af en sprogtekst.
void nameFrom(const char* text, char* out, size_t n, const char* fallback) {
  snprintf(out, n, "%s", fallback);
  if (std::strncmp(text, "\xEF\xBB\xBF", 3) == 0) text += 3;
  if (std::strncmp(text, "#name:", 6) != 0) return;
  text += 6;
  while (*text == ' ') ++text;
  size_t k = 0;
  while (text[k] && text[k] != '\n' && text[k] != '\r' && k + 1 < n) out[k] = text[k], ++k;
  out[k] = 0;
}

Lang* add(const char* id) {
  for (int i = 1; i < g_n; ++i)
    if (!strcasecmp(g_langs[i].id, id)) return &g_langs[i];
  if (g_n >= kMax) return nullptr;
  Lang& l = g_langs[g_n++];
  snprintf(l.id, sizeof(l.id), "%s", id);
  l.builtin = nullptr;
  return &l;
}
}  // namespace

void begin(bool sd) {
  g_n = 0;
  Lang& da = g_langs[g_n++];
  snprintf(da.id, sizeof(da.id), "da");
  snprintf(da.name, sizeof(da.name), "Dansk");
  da.builtin = "";
  for (int i = 0; i < lang::kBuiltinCount; ++i) {
    Lang* l = add(lang::kBuiltin[i].id);
    if (!l) break;
    l->builtin = lang::kBuiltin[i].text;
    nameFrom(l->builtin, l->name, sizeof(l->name), l->id);
  }
  if (!sd) return;
  DIR* d = opendir(kDir);
  if (!d) return;
  while (struct dirent* e = readdir(d)) {
    const size_t n = std::strlen(e->d_name);
    if (n < 5 || n > 15 || strcasecmp(e->d_name + n - 4, ".txt") != 0) continue;
    char id[12];
    snprintf(id, sizeof(id), "%.*s", int(n - 4), e->d_name);
    Lang* l = add(id);
    if (!l) break;
    l->builtin = nullptr;  // SD går forud for det indbyggede
    char path[64], head[64] = "";
    snprintf(path, sizeof(path), "%s/%s", kDir, e->d_name);
    if (FILE* f = fopen(path, "r")) {
      if (!fgets(head, sizeof(head), f)) head[0] = 0;
      fclose(f);
    }
    nameFrom(head, l->name, sizeof(l->name), l->id);
  }
  closedir(d);
}

int count() { return g_n ? g_n : 1; }

const char* nameAt(int i) { return (i >= 0 && i < g_n) ? g_langs[i].name : "Dansk"; }

void apply(const char* key) {
  i18n::setActive(nullptr);
  g_cat.reset();
  if (!key || !*key) return;
  const Lang* l = nullptr;
  for (int i = 0; i < g_n && !l; ++i)
    if (!strcasecmp(g_langs[i].id, key) || !strcasecmp(g_langs[i].name, key)) l = &g_langs[i];
  if (!l || !std::strcmp(l->id, "da")) return;
  int n = 0;
  if (l->builtin) {
    n = g_cat.load(l->builtin, std::strlen(l->builtin));
  } else {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.txt", kDir, l->id);
    if (FILE* f = fopen(path, "rb")) {
      fseek(f, 0, SEEK_END);
      const long len = ftell(f);
      fseek(f, 0, SEEK_SET);
      char* buf = len > 0 && len < 512 * 1024 ? static_cast<char*>(std::malloc(size_t(len))) : nullptr;
      if (buf && fread(buf, 1, size_t(len), f) == size_t(len)) n = g_cat.load(buf, size_t(len));
      std::free(buf);
      fclose(f);
    }
  }
  if (n > 0) i18n::setActive(&g_cat);
  DLOG.printf("[UI] sprog %s: %d tekster\n", l->name, n);
}

}  // namespace lang
}  // namespace dm
