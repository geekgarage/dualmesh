// SPDX-License-Identifier: MIT
#include "storage/sd_log.h"

#include <Arduino.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace dm {

SdLog DLOG;

namespace {
constexpr const char* kDir = "/sd/dualmesh/logs";
uint8_t* g_out = nullptr;  // kopi af bufferen, der skrives uden lås

int fileNumber(const char* name) {
  // "NNNN.log" -> NNNN, ellers -1
  char* end = nullptr;
  const long n = std::strtol(name, &end, 10);
  return (end != name && std::strcmp(end, ".log") == 0 && n >= 0) ? int(n) : -1;
}
}  // namespace

SdLog::SdLog() {}

void SdLog::put(uint8_t c) {
  const size_t pos = (head_ + len_) % kBuf;
  buf_[pos] = c;
  if (len_ < kBuf) {
    ++len_;
  } else {  // fuld: ældste byte overskrives
    head_ = (head_ + 1) % kBuf;
    ++dropped_;
  }
}

size_t SdLog::write(uint8_t c) { return write(&c, 1); }

size_t SdLog::write(const uint8_t* data, size_t n) {
  if (!mute_serial_) Serial.write(data, n);
  if (!buf_) {
    // Første kald (statisk konstruktion er for tidligt til PSRAM-allokering).
    void* p = heap_caps_malloc(kBuf, MALLOC_CAP_SPIRAM);
    if (!p) return n;
    buf_ = static_cast<uint8_t*>(p);
  }
  const uint32_t ms = millis();
  char stamp[16];
  const uint32_t s = ms / 1000;
  const int sl = std::snprintf(stamp, sizeof(stamp), "%02lu:%02lu:%02lu.%03lu ",
                               (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                               (unsigned long)(s % 60), (unsigned long)(ms % 1000));
  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < n; ++i) {
    const uint8_t c = data[i];
    if (c == '\r') continue;  // filen bruger kun \n
    if (line_start_) {
      if (c == '\n') {
        put(c);
        continue;  // tomme linjer uden tidsstempel
      }
      for (int k = 0; k < sl; ++k) put(uint8_t(stamp[k]));
      line_start_ = false;
    }
    put(c);
    if (c == '\n') line_start_ = true;
  }
  portEXIT_CRITICAL(&mux_);
  return n;
}

void SdLog::cleanup() {
  DIR* d = opendir(kDir);
  if (!d) return;
  int nums[kMaxFiles + 64];
  int count = 0, total = 0;
  while (dirent* e = readdir(d)) {
    const int n = fileNumber(e->d_name);
    if (n < 0) continue;
    ++total;
    if (count < int(sizeof(nums) / sizeof(nums[0]))) nums[count++] = n;
  }
  closedir(d);
  // Slet de ældste (laveste numre), så der er plads til én ny fil.
  while (total >= kMaxFiles && count > 0) {
    int lo = 0;
    for (int i = 1; i < count; ++i)
      if (nums[i] < nums[lo]) lo = i;
    char p[48];
    std::snprintf(p, sizeof(p), "%s/%04d.log", kDir, nums[lo]);
    std::remove(p);
    nums[lo] = nums[--count];
    --total;
  }
}

void SdLog::openNext() {
  cleanup();
  std::snprintf(path_, sizeof(path_), "%s/%04d.log", kDir, file_no_);
  file_bytes_ = 0;
}

void SdLog::begin(bool sd_ok) {
  sd_ok_ = false;
  if (!sd_ok) return;
  mkdir("/sd/dualmesh", 0775);
  mkdir(kDir, 0775);
  DIR* d = opendir(kDir);
  if (!d) return;
  int max_no = -1;
  while (dirent* e = readdir(d)) {
    const int n = fileNumber(e->d_name);
    if (n > max_no) max_no = n;
  }
  closedir(d);
  if (!g_out) g_out = static_cast<uint8_t*>(heap_caps_malloc(kBuf, MALLOC_CAP_SPIRAM));
  if (!g_out) return;
  file_no_ = (max_no + 1) % 10000;
  openNext();
  sd_ok_ = true;
  // Opret filen og skriv opstartslinjerne nu, før radioen kører: den første
  // skrivning til en ny fil (mappeindgang + første klynge) målte 37-149 ms og gav
  // lige så lange huller i radiolytningen, når den skete 30 s efter opstart.
  flush(true);
}

int SdLog::clearAll() {
  if (!sd_ok_) return -1;
  flush(true);  // tøm bufferen først, så intet gammelt lander i den nye fil
  DIR* d = opendir(kDir);
  if (!d) return -1;
  char names[kMaxFiles + 8][16];
  int n = 0;
  while (dirent* e = readdir(d))
    if (fileNumber(e->d_name) >= 0 && n < kMaxFiles + 8)
      std::snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
  closedir(d);
  int removed = 0;
  char p[64];
  for (int i = 0; i < n; ++i) {
    std::snprintf(p, sizeof(p), "%s/%s", kDir, names[i]);
    if (std::remove(p) == 0) ++removed;
    vTaskDelay(1);  // delt SPI-bus
  }
  file_no_ = 0;
  openNext();
  return removed;
}

uint32_t SdLog::takeSlowestFlush(uint32_t& at_ms) {
  const uint32_t us = slow_us_;
  at_ms = slow_at_ms_;
  slow_us_ = 0;
  return us;
}

void SdLog::flush(bool force) {
  if (!sd_ok_ || !buf_) return;
  const uint32_t now = millis();
  if (!force && len_ < kFlushAt && now - last_flush_ms_ < kIntervalMs) return;
  last_flush_ms_ = now;
  // Byt buffere under låsen (O(1)); skriv den gamle uden lås.
  portENTER_CRITICAL(&mux_);
  uint8_t* full = buf_;
  const size_t head = head_, n = len_;
  buf_ = g_out;
  head_ = 0;
  len_ = 0;
  g_out = full;
  portEXIT_CRITICAL(&mux_);
  if (n == 0) return;
  if (file_bytes_ >= kMaxFileBytes) {
    file_no_ = (file_no_ + 1) % 10000;
    openNext();
  }
  // Måles: det længste enkelte skridt (åbn / én bid / fsync+luk), dvs. hvor længe
  // bussen højst var optaget i ét stræk – ikke pauserne mellem bidderne.
  uint32_t step_max = 0, step_at = now;
  int64_t t0 = esp_timer_get_time();
  auto step = [&]() {
    const int64_t t1 = esp_timer_get_time();
    const uint32_t dt = uint32_t(t1 - t0);
    if (dt > step_max) {
      step_max = dt;
      step_at = uint32_t(t0 / 1000);
    }
    t0 = t1;
  };
  FILE* f = std::fopen(path_, "a");
  step();
  if (!f) {
    dropped_ += n;
    return;
  }
  // Skriv i bidder på 512 B med en kort pause imellem: SD-kortet deler SPI-bus med
  // radioen, og én lang skrivning holder bussen, så et profilskift kan vente
  // > 100 ms. Med små bidder kommer radio-tasken til mellem hver.
  setvbuf(f, nullptr, _IONBF, 0);
  constexpr size_t kChunk = 512;
  size_t w = 0;
  for (size_t done = 0; done < n;) {
    const size_t pos = (head + done) % kBuf;
    size_t c = n - done;
    if (c > kChunk) c = kChunk;
    if (pos + c > kBuf) c = kBuf - pos;  // ringbufferen løber rundt
    const size_t k = std::fwrite(g_out + pos, 1, c, f);
    step();
    w += k;
    done += c;
    if (k != c) break;
    if (done < n) {
      vTaskDelay(1);
      t0 = esp_timer_get_time();
    }
  }
  fsync(fileno(f));
  std::fclose(f);
  step();
  if (step_max > slow_us_) {
    slow_us_ = step_max;
    slow_at_ms_ = step_at;
  }
  file_bytes_ += w;
  written_ += w;
  if (w != n) dropped_ += n - w;
}

}  // namespace dm

namespace dm {

void SdLog::dumpToSerial(int newest) {
  flush(true);
  DIR* d = sd_ok_ ? opendir(kDir) : nullptr;
  int nums[kMaxFiles + 64];
  int count = 0;
  if (d) {
    while (dirent* e = readdir(d)) {
      const int n = fileNumber(e->d_name);
      if (n >= 0 && count < int(sizeof(nums) / sizeof(nums[0]))) nums[count++] = n;
    }
    closedir(d);
  }
  // Ældste først.
  for (int i = 1; i < count; ++i)
    for (int j = i; j > 0 && nums[j] < nums[j - 1]; --j) {
      const int t = nums[j];
      nums[j] = nums[j - 1];
      nums[j - 1] = t;
    }
  mute_serial_ = true;  // andre logudskrifter må ikke blande sig i dumpet
  static uint8_t chunk[2048];
  const int first = (newest > 0 && count > newest) ? count - newest : 0;
  for (int i = first; i < count; ++i) {
    char p[48];
    std::snprintf(p, sizeof(p), "%s/%04d.log", kDir, nums[i]);
    struct stat st;
    if (stat(p, &st) != 0) continue;
    FILE* f = std::fopen(p, "r");
    if (!f) continue;
    Serial.printf("\n=== DMLOG-FIL %04d.log %ld ===\n", nums[i], long(st.st_size));
    size_t r;
    while ((r = std::fread(chunk, 1, sizeof(chunk), f)) > 0) Serial.write(chunk, r);
    std::fclose(f);
  }
  Serial.printf("\n=== DMLOG-SLUT %d filer ===\n", count - first);
  mute_serial_ = false;
}

}  // namespace dm
