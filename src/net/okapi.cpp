// SPDX-License-Identifier: MIT
#include "net/okapi.h"

#include <WiFi.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform.h"
#include "storage/sd_log.h"

// ESP-IDF's indbyggede rodcertifikater (Arduinos egen esp_crt_bundle.h skygger for IDF's).
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace dm {
namespace okapi {

namespace {
constexpr size_t kMaxResp = 512 * 1024;  // svaret i PSRAM (500 caches ≈ 250 KB)
constexpr const char* kOut = "/sd/dualmesh/caches/opencaching.gpx";
constexpr const char* kTmp = "/sd/dualmesh/caches/opencaching.tmp";

volatile State g_state = State::Idle;
char g_msg[64] = "";
char g_url[1024];

// Alle mbedtls-blokke i PSRAM (som net/dl): intern RAM er knap, og hardware-AES/SHA skal kunne
// få sine DMA-buffere dér.
void* tlsCalloc(size_t n, size_t size) {
  if (void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
  return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
void tlsFree(void* p) { heap_caps_free(p); }

void urlenc(const char* s, char* out, size_t n) {
  size_t w = 0;
  for (; *s && w + 4 < n; ++s) {
    const unsigned char c = (unsigned char)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
      out[w++] = char(c);
    else
      w += size_t(snprintf(out + w, n - w, "%%%02X", c));
  }
  out[w] = 0;
}

// --- en lille JSON-læser til OKAPI's svar: {"KODE": {"felt": værdi, ...}, ...} ---
struct Json {
  const char* p;
  const char* end;
  void ws() {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
  }
  bool lit(char c) {
    ws();
    if (p < end && *p == c) {
      ++p;
      return true;
    }
    return false;
  }
  // Streng til out (UTF-8, \uXXXX afkodet). false ved fejl.
  bool str(char* out, size_t n) {
    ws();
    if (p >= end || *p != '"') return false;
    ++p;
    size_t w = 0;
    auto put = [&](unsigned cp) {
      char b[4];
      int k;
      if (cp < 0x80) b[0] = char(cp), k = 1;
      else if (cp < 0x800) b[0] = char(0xC0 | (cp >> 6)), b[1] = char(0x80 | (cp & 0x3F)), k = 2;
      else b[0] = char(0xE0 | (cp >> 12)), b[1] = char(0x80 | ((cp >> 6) & 0x3F)), b[2] = char(0x80 | (cp & 0x3F)), k = 3;
      for (int i = 0; i < k; ++i)
        if (w + 1 < n) out[w++] = b[i];
    };
    while (p < end && *p != '"') {
      if (*p == '\\' && p + 1 < end) {
        ++p;
        const char e = *p++;
        switch (e) {
          case 'n': put('\n'); break;
          case 't': put(' '); break;
          case 'r': break;
          case 'u':
            if (p + 4 <= end) {
              char h[5] = {p[0], p[1], p[2], p[3], 0};
              put(unsigned(std::strtoul(h, nullptr, 16)));
              p += 4;
            }
            break;
          default: put((unsigned char)e); break;
        }
      } else {
        if (w + 1 < n) out[w++] = *p;
        ++p;
      }
    }
    out[w] = 0;
    if (p >= end) return false;
    ++p;
    return true;
  }
  // En værdi som tekst (streng, tal, true/false/null); objekter/lister springes over.
  bool value(char* out, size_t n) {
    ws();
    if (p >= end) return false;
    if (*p == '"') return str(out, n);
    if (*p == '{' || *p == '[') {
      int depth = 0;
      bool in_s = false;
      for (; p < end; ++p) {
        if (in_s) {
          if (*p == '\\') ++p;
          else if (*p == '"') in_s = false;
        } else if (*p == '"') in_s = true;
        else if (*p == '{' || *p == '[') ++depth;
        else if ((*p == '}' || *p == ']') && --depth == 0) {
          ++p;
          break;
        }
      }
      out[0] = 0;
      return true;
    }
    size_t w = 0;
    while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n')
      if (w + 1 < n) out[w++] = *p++;
      else ++p;
    out[w] = 0;
    return true;
  }
};

void xml(FILE* f, const char* s) {
  for (; *s; ++s) {
    if (*s == '&') fputs("&amp;", f);
    else if (*s == '<') fputs("&lt;", f);
    else if (*s == '>') fputs("&gt;", f);
    else if (*s == '"') fputs("&quot;", f);
    else fputc(*s, f);
  }
}

// OKAPI-svaret -> GPX med Groundspeak-felter (samme som en Pocket Query). Antal caches eller -1.
int toGpx(const char* buf, size_t len, char* err, size_t errn) {
  Json j{buf, buf + len};
  if (!j.lit('{')) return -1;
  mkdir("/sd/dualmesh", 0775);
  mkdir("/sd/dualmesh/caches", 0775);
  FILE* f = fopen(kTmp, "w");
  if (!f) {
    snprintf(err, errn, "Kan ikke skrive på SD");
    return -1;
  }
  fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<gpx version=\"1.0\" creator=\"DualMesh (Opencaching OKAPI)\">\n", f);
  int n = 0;
  static char key[64], fk[32], val[1024];
  static char code[16], name[96], loc[48], type[32], size[16], diff[8], terr[8], hint[1024];
  while (!j.lit('}')) {
    j.lit(',');
    if (!j.str(key, sizeof(key)) || !j.lit(':')) break;
    if (!std::strcmp(key, "error")) {  // {"error": {"developer_message": ...}}
      if (j.lit('{')) {
        while (!j.lit('}')) {
          j.lit(',');
          if (!j.str(fk, sizeof(fk)) || !j.lit(':') || !j.value(val, sizeof(val))) break;
          if (!std::strcmp(fk, "developer_message")) snprintf(err, errn, "%.60s", val);
        }
      }
      fclose(f);
      remove(kTmp);
      return -1;
    }
    if (!j.lit('{')) {  // null (ukendt kode) o.l.
      j.value(val, sizeof(val));
      continue;
    }
    code[0] = name[0] = loc[0] = type[0] = size[0] = diff[0] = terr[0] = hint[0] = 0;
    while (!j.lit('}')) {
      j.lit(',');
      if (!j.str(fk, sizeof(fk)) || !j.lit(':') || !j.value(val, sizeof(val))) break;
      if (!std::strcmp(fk, "code")) snprintf(code, sizeof(code), "%s", val);
      else if (!std::strcmp(fk, "name")) snprintf(name, sizeof(name), "%s", val);
      else if (!std::strcmp(fk, "location")) snprintf(loc, sizeof(loc), "%s", val);
      else if (!std::strcmp(fk, "type")) snprintf(type, sizeof(type), "%s", val);
      else if (!std::strcmp(fk, "size2")) snprintf(size, sizeof(size), "%s", val);
      else if (!std::strcmp(fk, "difficulty")) snprintf(diff, sizeof(diff), "%s", val);
      else if (!std::strcmp(fk, "terrain")) snprintf(terr, sizeof(terr), "%s", val);
      else if (!std::strcmp(fk, "hint2")) snprintf(hint, sizeof(hint), "%s", val);
    }
    const char* bar = std::strchr(loc, '|');
    if (!code[0] || !bar) continue;
    fprintf(f, "<wpt lat=\"%.*s\" lon=\"%s\"><name>", int(bar - loc), loc, bar + 1);
    xml(f, code);
    fputs("</name><sym>Geocache</sym><groundspeak:cache><groundspeak:name>", f);
    xml(f, name);
    fputs("</groundspeak:name><groundspeak:type>", f);
    xml(f, type);
    fputs("</groundspeak:type><groundspeak:container>", f);
    xml(f, size);
    fprintf(f, "</groundspeak:container><groundspeak:difficulty>%s</groundspeak:difficulty>"
               "<groundspeak:terrain>%s</groundspeak:terrain><groundspeak:encoded_hints>",
            diff[0] ? diff : "1", terr[0] ? terr : "1");
    xml(f, hint);
    fputs("</groundspeak:encoded_hints></groundspeak:cache></wpt>\n", f);
    ++n;
  }
  fputs("</gpx>\n", f);
  fclose(f);
  remove(kOut);
  rename(kTmp, kOut);
  return n;
}

void task(void*) {
  esp_http_client_config_t cfg = {};
  cfg.url = g_url;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 30000;
  cfg.buffer_size = 2048;
  cfg.user_agent = "DualMesh";
  char* buf = static_cast<char*>(heap_caps_malloc(kMaxResp, MALLOC_CAP_SPIRAM));
  size_t len = 0;
  int status = 0;
  esp_err_t err = ESP_FAIL;
  esp_http_client_handle_t c = buf ? esp_http_client_init(&cfg) : nullptr;
  if (c && (err = esp_http_client_open(c, 0)) == ESP_OK) {
    esp_http_client_fetch_headers(c);
    status = esp_http_client_get_status_code(c);
    int r;
    while (len < kMaxResp - 1 && (r = esp_http_client_read(c, buf + len, int(kMaxResp - 1 - len))) > 0) len += size_t(r);
    esp_http_client_close(c);
  }
  if (c) esp_http_client_cleanup(c);
  char msg[64] = "";
  State st = State::Failed;
  if (!buf) {
    snprintf(msg, sizeof(msg), "Ikke nok hukommelse");
  } else if (err != ESP_OK) {
    snprintf(msg, sizeof(msg), "Ingen forbindelse til Opencaching");
    DLOG.printf("[OKAPI] forbindelse fejlede: %s\n", esp_err_to_name(err));
  } else {
    buf[len] = 0;
    const int n = toGpx(buf, len, msg, sizeof(msg));
    DLOG.printf("[OKAPI] HTTP %d, %u byte, %d caches\n", status, unsigned(len), n);
    if (n >= 0) {
      snprintf(msg, sizeof(msg), "%d caches hentet fra Opencaching", n);
      st = State::Done;
    } else if (!msg[0]) {
      snprintf(msg, sizeof(msg), "Fejl fra Opencaching (HTTP %d)", status);
    }
  }
  heap_caps_free(buf);
  snprintf(g_msg, sizeof(g_msg), "%s", msg);
  g_state = st;
  vTaskDelete(nullptr);
}
}  // namespace

bool start(const char* site, const char* key, double lat, double lon, int radius_km) {
  if (g_state == State::Busy) return false;
  if (!key || !*key) {
    snprintf(g_msg, sizeof(g_msg), "Mangler Opencaching-nøgle (Indst. > Kort)");
    g_state = State::Failed;
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(g_msg, sizeof(g_msg), "WiFi er ikke forbundet");
    g_state = State::Failed;
    return false;
  }
  if (!site || !*site) site = "www.opencaching.de";
  if (radius_km < 1) radius_km = 1;
  char sp[160], rp[160], spe[400], rpe[400], keye[96];
  snprintf(sp, sizeof(sp), "{\"center\":\"%.6f|%.6f\",\"radius\":\"%d\",\"limit\":\"500\"}", lat, lon, radius_km);
  snprintf(rp, sizeof(rp), "{\"fields\":\"code|name|location|type|size2|difficulty|terrain|hint2\"}");
  urlenc(sp, spe, sizeof(spe));
  urlenc(rp, rpe, sizeof(rpe));
  urlenc(key, keye, sizeof(keye));
  snprintf(g_url, sizeof(g_url),
           "https://%s/okapi/services/caches/shortcuts/search_and_retrieve?search_method=services/caches/search/"
           "nearest&search_params=%s&retr_method=services/caches/geocaches&retr_params=%s&wrap=false&"
           "consumer_key=%s",
           site, spe, rpe, keye);
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  g_state = State::Busy;
  DLOG.printf("[OKAPI] henter caches inden for %d km fra %s\n", radius_km, site);  // (nøglen logges ikke)
  if (xTaskCreatePinnedToCore(task, "okapi", 12288, nullptr, 2, nullptr, 0) != pdPASS) {
    snprintf(g_msg, sizeof(g_msg), "Kunne ikke starte hentningen");
    g_state = State::Failed;
    return false;
  }
  return true;
}

State poll(char* msg, size_t n) {
  const State s = g_state;
  if (s == State::Done || s == State::Failed) {
    if (msg) snprintf(msg, n, "%s", g_msg);
    g_state = State::Idle;
  }
  return s;
}

}  // namespace okapi
}  // namespace dm
