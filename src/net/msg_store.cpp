// SPDX-License-Identifier: MIT
#include "net/msg_store.h"

#include <Arduino.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <new>

#include "esp_heap_caps.h"
#include "storage/sd_log.h"

namespace dm {

static constexpr const char* kDir = "/sd/dualmesh/msgs";

uint32_t fnv1a(const void* data, size_t n, uint32_t h) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

static Msg* allocMsgs(size_t n) {
  // PSRAM: beskeder er store og bruges sjældent, så intern RAM skånes.
  void* p = heap_caps_calloc(n, sizeof(Msg), MALLOC_CAP_SPIRAM);
  if (!p) p = heap_caps_calloc(n, sizeof(Msg), MALLOC_CAP_8BIT);
  if (!p) return nullptr;
  Msg* m = static_cast<Msg*>(p);
  for (size_t i = 0; i < n; ++i) new (&m[i]) Msg();
  return m;
}

bool MsgStore::begin(size_t capacity) {
  buf_ = allocMsgs(capacity);
  view_ = allocMsgs(kMaxLimit);
  if (!buf_ || !view_) return false;
  cap_ = capacity;
  head_ = count_ = 0;
  v_cap_ = size_t(per_channel_);
  return true;
}

void MsgStore::setLimits(int per_channel, int per_dm) {
  auto clamp = [](int v) { return v < 10 ? 10 : (v > kMaxLimit ? kMaxLimit : v); };
  per_channel_ = clamp(per_channel);
  per_dm_ = clamp(per_dm);
}

int MsgStore::limitFor(const char* id) const {
  return std::strncmp(id, "mcd_", 4) == 0 ? per_dm_ : per_channel_;
}

const Msg& MsgStore::at(size_t i) const { return buf_[(head_ + i) % cap_]; }
const Msg& MsgStore::viewAt(size_t i) const { return view_[(v_head_ + i) % v_cap_]; }

Msg& MsgStore::viewAdd() {
  size_t idx;
  if (v_count_ < v_cap_) {
    idx = (v_head_ + v_count_) % v_cap_;
    ++v_count_;
  } else {
    idx = v_head_;
    v_head_ = (v_head_ + 1) % v_cap_;
  }
  return view_[idx];
}

MsgStore::Conv* MsgStore::conv(const char* id, bool create) {
  for (int i = 0; i < n_convs_; ++i)
    if (std::strcmp(convs_[i].id, id) == 0) return &convs_[i];
  if (!create || !*id) return nullptr;
  if (n_convs_ >= kMaxConvs) return nullptr;
  Conv& c = convs_[n_convs_++];
  snprintf(c.id, sizeof(c.id), "%s", id);
  c.unread = 0;
  return &c;
}

uint32_t MsgStore::unread(const char* id) const {
  for (int i = 0; i < n_convs_; ++i)
    if (std::strcmp(convs_[i].id, id) == 0) return convs_[i].unread;
  return 0;
}

uint32_t MsgStore::unreadPrefix(const char* prefix) const {
  uint32_t n = 0;
  const size_t l = std::strlen(prefix);
  for (int i = 0; i < n_convs_; ++i)
    if (std::strncmp(convs_[i].id, prefix, l) == 0) n += convs_[i].unread;
  return n;
}

uint32_t MsgStore::totalUnread() const {
  uint32_t n = 0;
  for (int i = 0; i < n_convs_; ++i) n += convs_[i].unread;
  return n;
}

void MsgStore::setViewActive(bool on) {
  view_active_ = on;
  if (on)
    if (Conv* c = conv(open_id_, false)) {
      if (c->unread) ++version_;
      c->unread = 0;
    }
}

uint32_t MsgStore::push(const Msg& in, bool persist) {
  if (!buf_) return 0;
  size_t idx;
  if (count_ < cap_) {
    idx = (head_ + count_) % cap_;
    ++count_;
  } else {
    idx = head_;
    head_ = (head_ + 1) % cap_;
  }
  Msg& m = buf_[idx];
  m = in;
  if (!m.seq) m.seq = ++seq_;
  if (open_id_[0] && std::strcmp(m.conv, open_id_) == 0) viewAdd() = m;
  const bool incoming = m.kind == Msg::ChannelIn || m.kind == Msg::DirectIn;
  const bool seen = view_active_ && std::strcmp(m.conv, open_id_) == 0;
  if (incoming && !seen)
    if (Conv* c = conv(m.conv, true)) c->unread++;
  if (persist) appendFile(m);
  ++version_;
  if (hook_) hook_(m);
  return m.seq;
}

void MsgStore::setStatus(uint32_t seq, Msg::Status st, bool write) {
  if (!seq) return;
  for (size_t i = 0; i < count_; ++i) {
    Msg& m = buf_[(head_ + i) % cap_];
    if (m.seq != seq) continue;
    m.status = st;
    if (write) appendFile(m);
  }
  for (size_t i = 0; i < v_count_; ++i) {
    Msg& m = view_[(v_head_ + i) % v_cap_];
    if (m.seq == seq) m.status = st;
  }
  ++version_;
}

void MsgStore::path(const char* id, char* out, size_t n, const char* ext) const {
  snprintf(out, n, "%s/%s.%s", kDir, id, ext);
}

// Tekst uden tabulator og linjeskift (filformatet er én linje pr. besked).
static void clean(char* dst, size_t cap, const char* src) {
  size_t o = 0;
  for (; *src && o + 1 < cap; ++src) dst[o++] = (*src == '\t' || *src == '\n' || *src == '\r') ? ' ' : *src;
  dst[o] = 0;
}

static void writeLine(FILE* f, const Msg& m) {
  char peer[sizeof(m.peer)], text[sizeof(m.text)];
  clean(peer, sizeof(peer), m.peer);
  clean(text, sizeof(text), m.text);
  std::fprintf(f, "%lu\t%u\t%u\t%u\t%s\t%s\n", (unsigned long)m.ts, unsigned(m.kind),
               unsigned(m.status), unsigned(m.hops), peer, text);
}

void MsgStore::appendFile(const Msg& m) {
  if (!sd_ || !m.conv[0]) return;
  mkdir(kDir, 0775);
  char p[64];
  path(m.conv, p, sizeof(p));
  FILE* f = std::fopen(p, "a");
  if (!f) {
    DLOG.printf("[BESKED] kunne ikke skrive %s\n", p);
    return;
  }
  writeLine(f, m);
  const long size = std::ftell(f);
  std::fclose(f);
  // For stor: beskær (linjer er typisk < 200 B; grænsen giver god margen).
  const int limit = limitFor(m.conv);
  if (size > long(limit) * 300 + 8192) trimFile(m.conv, limit);
}

static bool parseLine(char* line, Msg& m) {
  line[std::strcspn(line, "\r\n")] = 0;
  char* f[6] = {};
  int k = 0;
  for (char* p = line; k < 6 && p; ++k) {
    f[k] = p;
    char* t = k < 5 ? std::strchr(p, '\t') : nullptr;
    if (t) *t = 0;
    p = t ? t + 1 : nullptr;
  }
  if (k < 6) return false;
  m.ts = std::strtoul(f[0], nullptr, 10);
  m.kind = Msg::Kind(std::atoi(f[1]) & 3);
  m.status = Msg::Status(std::atoi(f[2]) & 3);
  m.hops = uint8_t(std::atoi(f[3]));
  snprintf(m.peer, sizeof(m.peer), "%s", f[4]);
  snprintf(m.text, sizeof(m.text), "%s", f[5]);
  return true;
}

void MsgStore::loadFile(const char* id) {
  char p[64];
  path(id, p, sizeof(p));
  FILE* f = std::fopen(p, "r");
  if (!f) return;
  const uint32_t t0 = millis();
  static char line[sizeof(Msg::peer) + sizeof(Msg::text) + 48];
  int lines = 0;
  const uint8_t net = std::strncmp(id, "mt", 2) == 0 ? 2 : 1;
  while (std::fgets(line, sizeof(line), f)) {
    Msg m;
    if (!parseLine(line, m)) continue;
    m.net = net;
    snprintf(m.conv, sizeof(m.conv), "%s", id);
    viewAdd() = m;
    if (++lines % 32 == 0) vTaskDelay(1);  // delt SPI-bus: lad radioen komme til
  }
  std::fclose(f);
  DLOG.printf("[BESKED] %s: %d beskeder laest fra SD (%lu ms), viser %u\n", id, lines,
              (unsigned long)(millis() - t0), unsigned(v_count_));
  if (lines > int(v_cap_) + int(v_cap_) / 4) trimFile(id, int(v_cap_));
}

void MsgStore::trimFile(const char* id, int keep) {
  char p[64], tmp[64], line[sizeof(Msg::peer) + sizeof(Msg::text) + 48];
  path(id, p, sizeof(p));
  path(id, tmp, sizeof(tmp), "tmp");
  FILE* in = std::fopen(p, "r");
  if (!in) return;
  int total = 0;
  while (std::fgets(line, sizeof(line), in))
    if (++total % 32 == 0) vTaskDelay(1);  // delt SPI-bus: lad radioen komme til
  if (total <= keep) {
    std::fclose(in);
    return;
  }
  std::rewind(in);
  FILE* out = std::fopen(tmp, "w");
  if (!out) {
    std::fclose(in);
    return;
  }
  int n = 0;
  while (std::fgets(line, sizeof(line), in)) {
    if (n++ >= total - keep) std::fputs(line, out);
    if (n % 32 == 0) vTaskDelay(1);
  }
  std::fclose(in);
  std::fflush(out);
  const bool ok = std::ferror(out) == 0;
  std::fclose(out);
  if (!ok) return;
  std::remove(p);
  std::rename(tmp, p);
  DLOG.printf("[BESKED] %s beskaaret til %d beskeder (var %d)\n", id, keep, total);
}

bool MsgStore::selfTest() {
  static const char* kId = "zz_selvtest";
  char prev_id[20], prev_title[40];
  snprintf(prev_id, sizeof(prev_id), "%s", open_id_);
  snprintf(prev_title, sizeof(prev_title), "%s", open_title_);
  const bool prev_active = view_active_;
  view_active_ = false;
  open_id_[0] = 0;  // ingen samtale åben: alt tæller som ulæst
  char p[64];
  path(kId, p, sizeof(p));
  std::remove(p);
  const int limit = limitFor(kId);
  const int n = 3 * limit + 5;
  const uint32_t t0 = millis();
  const uint32_t unread0 = unread(kId);
  for (int i = 1; i <= n; ++i) {
    Msg m;
    m.kind = Msg::ChannelIn;
    snprintf(m.conv, sizeof(m.conv), "%s", kId);
    snprintf(m.peer, sizeof(m.peer), "selvtest");
    snprintf(m.text, sizeof(m.text), "test\t%d", i);  // tab skal blive til mellemrum
    push(m);
  }
  const uint32_t t_write = millis() - t0;
  bool ok = unread(kId) - unread0 == uint32_t(n);
  open(kId, "selvtest");
  char want[16];
  snprintf(want, sizeof(want), "test %d", n);
  const bool sd_ok = !sd_ || (int(v_count_) == limit && std::strcmp(viewAt(v_count_ - 1).text, want) == 0);
  // Uden SD kommer visningen fra ringbufferen (højst dens størrelse).
  ok = ok && sd_ok && v_count_ > 0 && std::strcmp(viewAt(v_count_ - 1).text, want) == 0;
  // Efter open() (beskæring) må filen højst have `limit` linjer.
  int lines = 0;
  if (sd_) {
    if (FILE* f = std::fopen(p, "r")) {
      char line[256];
      while (std::fgets(line, sizeof(line), f)) ++lines;
      std::fclose(f);
    }
    ok = ok && lines <= limit;
  }
  setViewActive(true);
  ok = ok && unread(kId) == 0;
  DLOG.printf("[BESKED] selvtest: %d skrevet (%lu ms), vist %u (graense %d), fil %d linjer, "
              "sidste '%s' -> %s\n",
              n, (unsigned long)t_write, unsigned(v_count_), limit, lines,
              v_count_ ? viewAt(v_count_ - 1).text : "-", ok ? "BESTAAET" : "FEJLEDE");
  std::remove(p);
  view_active_ = prev_active;
  if (prev_id[0]) open(prev_id, prev_title);
  else open_id_[0] = 0, v_count_ = 0;
  return ok;
}

void MsgStore::setDayLimits(int channel_days, int dm_days) {
  days_channel_ = channel_days < 0 ? 0 : channel_days;
  days_dm_ = dm_days < 0 ? 0 : dm_days;
  ++version_;
}

uint32_t MsgStore::cutoffFor(const char* id, uint32_t now_ts) const {
  const int days = std::strncmp(id, "mcd_", 4) == 0 ? days_dm_ : days_channel_;
  if (!days || !now_ts) return 0;
  const uint32_t span = uint32_t(days) * 86400u;
  return now_ts > span ? now_ts - span : 0;
}

// Glem beskeder i RAM (ringbuffer, visning) og ulæste for en samtale eller et præfiks.
void MsgStore::forget(const char* key, bool prefix) {
  const size_t kl = std::strlen(key);
  auto match = [&](const char* id) { return prefix ? std::strncmp(id, key, kl) == 0 : std::strcmp(id, key) == 0; };
  for (size_t i = 0; i < count_; ++i) {
    Msg& m = buf_[(head_ + i) % cap_];
    if (match(m.conv)) m.conv[0] = 0;
  }
  if (match(open_id_)) v_head_ = v_count_ = 0;
  for (int i = 0; i < n_convs_; ++i)
    if (match(convs_[i].id)) convs_[i].unread = 0;
  ++version_;
}

int MsgStore::clear(const char* id) {
  if (!id || !*id) return 0;
  if (sd_) {
    char p[64];
    path(id, p, sizeof(p));
    std::remove(p);
  }
  forget(id, false);
  DLOG.printf("[BESKED] samtale %s ryddet\n", id);
  return 1;
}

int MsgStore::clearPrefix(const char* prefix) {
  int n = 0;
  if (sd_) {
    if (DIR* d = opendir(kDir)) {
      const size_t pl = std::strlen(prefix);
      char names[64][24];
      int k = 0;
      while (dirent* e = readdir(d))
        if (std::strncmp(e->d_name, prefix, pl) == 0 && k < 64)
          snprintf(names[k++], sizeof(names[0]), "%s", e->d_name);
      closedir(d);
      for (int i = 0; i < k; ++i) {
        char p[64];
        snprintf(p, sizeof(p), "%s/%s", kDir, names[i]);
        if (std::remove(p) == 0) ++n;
        if (i % 8 == 7) vTaskDelay(1);  // delt SPI-bus: lad radioen komme til
      }
    }
  }
  forget(prefix, true);
  DLOG.printf("[BESKED] alle %s-samtaler ryddet (%d filer)\n", prefix, n);
  return n;
}

void MsgStore::open(const char* id, const char* title) {
  snprintf(open_id_, sizeof(open_id_), "%s", id ? id : "");
  snprintf(open_title_, sizeof(open_title_), "%s", title ? title : "");
  v_head_ = v_count_ = 0;
  v_cap_ = size_t(limitFor(open_id_));
  if (sd_) {
    loadFile(open_id_);
  } else {  // uden SD: det, der er i ringbufferen
    for (size_t i = 0; i < count_; ++i)
      if (std::strcmp(at(i).conv, open_id_) == 0) viewAdd() = at(i);
  }
  if (view_active_)
    if (Conv* c = conv(open_id_, false)) c->unread = 0;
  ++version_;
}

}  // namespace dm
