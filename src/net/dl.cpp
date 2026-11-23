// SPDX-License-Identifier: MIT
#include "net/dl.h"

#include "net/gh_roots.h"

#include <WiFi.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform.h"
#include "mbedtls/sha256.h"
#include "storage/sd_fastwrite.h"
#include "storage/sd_log.h"
#include "version.h"

// ESP-IDF's indbyggede rodcertifikater (Arduinos egen esp_crt_bundle.h skygger for IDF's).
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace dm {
namespace dl {

namespace {
enum class Sink { Mem, File, Ota };
constexpr size_t kTextMax = 64 * 1024;
constexpr size_t kChunk = 16 * 1024;

volatile State g_state = State::Idle;
volatile bool g_cancel = false;
char g_msg[80] = "";
Sink g_sink = Sink::Mem;
char g_url[512];
char g_path[128];
bool g_resume = false;
uint64_t g_expect = 0;
uint32_t g_ota_size = 0;
uint8_t g_sha[32];
bool g_file_sha = false;  // startFile med SHA-256 (fx firmware til en launcher)
char* g_text = nullptr;
size_t g_text_len = 0;
volatile uint64_t g_done = 0, g_total = 0;
volatile uint32_t g_bps = 0;

// Alle mbedtls-blokke i PSRAM (også de små: rodcertifikaterne parses til mange blokke under 2 KB,
// ca. 10 KB i alt). Intern RAM er knap (35-40 KB fri med BLE+WiFi+MQTT), og hardware-AES/SHA skal
// kunne få sine DMA-buffere dér; ellers fejler TLS ("ctr_drbg_seed -1"), og andre tasks får NULL.
void* tlsCalloc(size_t n, size_t size) {
  if (void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
  return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
void tlsFree(void* p) { heap_caps_free(p); }

bool hexToBytes(const char* hex, uint8_t* out, size_t n) {
  if (!hex || std::strlen(hex) < n * 2) return false;
  for (size_t i = 0; i < n; ++i) {
    char b[3] = {hex[2 * i], hex[2 * i + 1], 0};
    char* end;
    out[i] = uint8_t(std::strtoul(b, &end, 16));
    if (*end) return false;
  }
  return true;
}

// Opret mapperne i en sti (/sd/dualmesh/maps/x.pmtiles -> /sd/dualmesh, /sd/dualmesh/maps).
void mkdirs(const char* path) {
  char p[128];
  snprintf(p, sizeof(p), "%s", path);
  for (char* s = p + 4; (s = std::strchr(s, '/')) != nullptr; ++s) {
    *s = 0;
    mkdir(p, 0775);
    *s = '/';
  }
}

void finish(State st, const char* msg) {
  snprintf(g_msg, sizeof(g_msg), "%s", msg);
  g_state = st;
}

void task(void*) {
  char msg[80] = "";
  bool ok = false;
  uint64_t offset = 0;
  char part_path[136] = "";
  FILE* f = nullptr;
  esp_ota_handle_t oh = 0;
  const esp_partition_t* part = nullptr;
  mbedtls_sha256_context sha;
  bool sha_on = false;

  if (g_sink == Sink::File) {
    snprintf(part_path, sizeof(part_path), "%s.part", g_path);
    struct stat st;
    if (g_resume && stat(part_path, &st) == 0) offset = uint64_t(st.st_size);
    else remove(part_path);
    mkdirs(g_path);
  }
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kChunk, MALLOC_CAP_SPIRAM));

  esp_http_client_config_t cfg = {};
  cfg.url = g_url;
  // GitHubs egne rødder med mbedtls' almindelige kædetjek: IDF 4.4's certifikatbundt fejler på
  // krydssignerede kæder (Let's Encrypt "Root YR" -> ISRG Root X1), se tools/gen_gh_roots.py.
  // Andre værter (ikke GitHub) bruger bundtet.
  if (std::strstr(g_url, "github.com/") || std::strstr(g_url, "githubusercontent.com/")) cfg.cert_pem = kGithubRoots;
  else cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 20000;
  // Over 4 KB, så malloc lægger bufferne i PSRAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL = 4096).
  // tx skal også rumme GitHubs viderestillinger med lange, signerede adresser.
  cfg.buffer_size = 8192;
  cfg.buffer_size_tx = 4352;
  cfg.user_agent = "DualMesh/" DM_FW_VERSION;
  cfg.disable_auto_redirect = true;  // vi følger selv (open/read gør det ikke automatisk)
  esp_http_client_handle_t c = buf ? esp_http_client_init(&cfg) : nullptr;
  int status = 0;
  int64_t clen = -1;
  esp_err_t err = ESP_FAIL;
  if (c) {
    for (int hop = 0; hop < 6; ++hop) {
      if (offset) {
        char r[40];
        snprintf(r, sizeof(r), "bytes=%llu-", (unsigned long long)offset);
        esp_http_client_set_header(c, "Range", r);
      }
      err = esp_http_client_open(c, 0);
      if (err != ESP_OK) break;
      clen = esp_http_client_fetch_headers(c);
      status = esp_http_client_get_status_code(c);
      if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        esp_http_client_set_redirection(c);
        esp_http_client_close(c);
        continue;
      }
      break;
    }
  }

  if (!buf || !c) {
    snprintf(msg, sizeof(msg), "Ikke nok hukommelse");
  } else if (err != ESP_OK) {
    snprintf(msg, sizeof(msg), "Ingen forbindelse");
    DLOG.printf("[HENT] forbindelse fejlede: %s\n", esp_err_to_name(err));
  } else if (status == 416 && offset && g_expect && offset == g_expect) {
    ok = true;  // .part var allerede hentet helt
  } else if (status != 200 && status != 206) {
    snprintf(msg, sizeof(msg), status == 404 ? "Findes ikke (HTTP 404)" : "Fejl fra serveren (HTTP %d)", status);
  } else {
    if (status == 200 && offset) offset = 0;  // serveren genoptager ikke: forfra
    g_done = offset;
    g_total = clen > 0 ? offset + uint64_t(clen) : g_expect;
    bool setup = true;
    if (g_sink == Sink::File) {
      const uint64_t need = g_total > offset ? g_total - offset : 0;
      const uint64_t fri = sdFree();
      if (need && fri && fri < need + 1024 * 1024) {
        snprintf(msg, sizeof(msg), "For lidt plads på SD (%lu MB fri, %lu MB mangler)",
                 (unsigned long)(fri >> 20), (unsigned long)(need >> 20));
        setup = false;
      } else if (!(f = fopen(part_path, offset ? "ab" : "wb"))) {
        snprintf(msg, sizeof(msg), "Kan ikke skrive på SD-kortet");
        setup = false;
      } else if (g_file_sha) {
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts_ret(&sha, 0);
        sha_on = true;
      }
    } else if (g_sink == Sink::Mem) {
      if (!g_text) g_text = static_cast<char*>(heap_caps_malloc(kTextMax, MALLOC_CAP_SPIRAM));
      g_text_len = 0;
      if (!g_text || g_total >= kTextMax) {
        snprintf(msg, sizeof(msg), "Filen er for stor");
        setup = false;
      }
    } else {  // OTA
      part = esp_ota_get_next_update_partition(nullptr);
      if (!part || part == esp_ota_get_running_partition() || g_ota_size > part->size) {
        snprintf(msg, sizeof(msg), "Ingen ledig app-partition til opdateringen");
        setup = false;
      } else if (g_total && g_total != g_ota_size) {
        snprintf(msg, sizeof(msg), "Forkert størrelse (%lu i stedet for %lu)", (unsigned long)g_total,
                 (unsigned long)g_ota_size);
        setup = false;
      } else if ((err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &oh)) != ESP_OK) {
        snprintf(msg, sizeof(msg), "OTA kunne ikke starte (%s)", esp_err_to_name(err));
        setup = false;
      } else {
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts_ret(&sha, 0);
        sha_on = true;
        DLOG.printf("[OTA] skriver %lu byte til %s\n", (unsigned long)g_ota_size, part->label);
      }
    }

    // Selve hentningen.
    uint64_t synced = g_done;
    uint32_t t_sec = millis(), b_sec = 0;
    bool io_err = false;
    while (setup && !g_cancel) {
      const int r = esp_http_client_read(c, reinterpret_cast<char*>(buf), int(kChunk));
      if (r < 0) {
        io_err = true;
        break;
      }
      if (r == 0) {
        if (esp_http_client_is_complete_data_received(c)) break;
        // Intet nyt endnu (eller forbindelsen er tabt): read venter op til timeout_ms.
        if (g_total && g_done < g_total) {
          io_err = true;
          break;
        }
        break;
      }
      bool wrote = true;
      if (g_sink == Sink::File) {
        wrote = fwrite(buf, 1, size_t(r), f) == size_t(r);
        if (wrote && sha_on) mbedtls_sha256_update_ret(&sha, buf, size_t(r));
        if (wrote && g_done + r - synced >= 1024 * 1024) {  // FatFs opdaterer først størrelsen ved sync
          fflush(f);
          fsync(fileno(f));
          synced = g_done + r;
        }
      } else if (g_sink == Sink::Mem) {
        if (g_text_len + size_t(r) >= kTextMax) wrote = false;
        else std::memcpy(g_text + g_text_len, buf, size_t(r)), g_text_len += size_t(r);
      } else {
        wrote = esp_ota_write(oh, buf, size_t(r)) == ESP_OK;
        if (wrote) mbedtls_sha256_update_ret(&sha, buf, size_t(r));
      }
      if (!wrote) {
        snprintf(msg, sizeof(msg), g_sink == Sink::Ota ? "Fejl ved skrivning til flash" : "Fejl ved skrivning");
        setup = false;
        break;
      }
      g_done += uint64_t(r);
      b_sec += uint32_t(r);
      const uint32_t now = millis();
      if (now - t_sec >= 1000) {
        g_bps = uint32_t(uint64_t(b_sec) * 1000 / (now - t_sec));
        t_sec = now;
        b_sec = 0;
      }
      vTaskDelay(1);  // giv radioen og SD-bussen luft
    }

    if (setup) {
      if (g_cancel) {
        snprintf(msg, sizeof(msg), g_sink == Sink::File && g_resume ? "Afbrudt (kan genoptages)" : "Afbrudt");
      } else if (io_err || (g_total && g_done != g_total)) {
        snprintf(msg, sizeof(msg), g_sink == Sink::File && g_resume ? "Forbindelsen røg (kan genoptages)"
                                                                     : "Forbindelsen røg");
      } else {
        ok = true;
      }
    }
  }

  // Afslut og ryd op.
  if (f) {
    fclose(f);
    if (ok && g_file_sha && sha_on) {  // firmware til en launcher: kun gemt, hvis SHA-256 passer
      uint8_t got[32];
      mbedtls_sha256_finish_ret(&sha, got);
      if (std::memcmp(got, g_sha, 32) != 0) {
        ok = false;
        snprintf(msg, sizeof(msg), "Kontrolsummen passer ikke: filen er slettet");
      }
    }
    if (!ok && !g_resume) remove(part_path);
  }
  if (g_sink == Sink::File && ok) {
    remove(g_path);
    if (rename(part_path, g_path) != 0) {
      ok = false;
      snprintf(msg, sizeof(msg), "Kunne ikke omdøbe filen");
    } else {
      snprintf(msg, sizeof(msg), "Hentet (%lu KB)", (unsigned long)((g_done + 1023) / 1024));
    }
  }
  if (g_sink == Sink::Mem) {
    if (g_text) g_text[ok ? g_text_len : 0] = 0;
    if (ok) snprintf(msg, sizeof(msg), "Hentet");
  }
  if (g_sink == Sink::Ota && oh) {
    uint8_t got[32];
    if (sha_on) mbedtls_sha256_finish_ret(&sha, got);
    if (ok && (g_done != g_ota_size || std::memcmp(got, g_sha, 32) != 0)) {
      ok = false;
      snprintf(msg, sizeof(msg), "Kontrolsummen passer ikke: opdateringen er ikke installeret");
    }
    if (!ok) {
      esp_ota_abort(oh);
    } else if ((err = esp_ota_end(oh)) != ESP_OK) {  // tjekker også billedets eget format
      ok = false;
      snprintf(msg, sizeof(msg), "Firmwaren er ugyldig (%s)", esp_err_to_name(err));
    } else if ((err = esp_ota_set_boot_partition(part)) != ESP_OK) {
      ok = false;
      snprintf(msg, sizeof(msg), "Kunne ikke vælge den nye firmware (%s)", esp_err_to_name(err));
    } else {
      snprintf(msg, sizeof(msg), "Firmwaren er installeret; genstarter");
    }
  }
  if (sha_on) mbedtls_sha256_free(&sha);
  if (c) {
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
  }
  heap_caps_free(buf);
  DLOG.printf("[HENT] %s: HTTP %d, %llu af %llu byte, %s\n", g_sink == Sink::Ota ? "OTA" : g_sink == Sink::File ? "fil" : "tekst",
              status, (unsigned long long)g_done, (unsigned long long)g_total, ok ? "OK" : msg);
  g_bps = 0;
  finish(ok ? State::Done : State::Failed, msg);
  vTaskDelete(nullptr);
}

bool launch(Sink s, const char* url) {
  if (g_state == State::Busy) return false;
  if (WiFi.status() != WL_CONNECTED) {
    finish(State::Failed, "WiFi er ikke forbundet");
    return false;
  }
  g_sink = s;
  snprintf(g_url, sizeof(g_url), "%s", url);
  g_cancel = false;
  g_done = g_total = 0;
  g_bps = 0;
  g_msg[0] = 0;
  // Mindst 24 KB intern RAM: stakken (10 KB) + hardware-AES/SHA's DMA-buffere + resten af systemet.
  const size_t fri = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (fri < 24 * 1024) {
    DLOG.printf("[HENT] for lidt intern RAM (%u KB fri)\n", unsigned(fri / 1024));
    finish(State::Failed, "For lidt hukommelse lige nu; prøv igen");
    return false;
  }
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  g_state = State::Busy;
  DLOG.printf("[HENT] %s\n", url);
  if (xTaskCreatePinnedToCore(task, "dl", 10240, nullptr, 2, nullptr, 0) != pdPASS) {
    finish(State::Failed, "Kunne ikke starte hentningen");
    return false;
  }
  return true;
}
}  // namespace

bool startText(const char* url) {
  if (g_state == State::Busy) return false;
  g_file_sha = false;
  return launch(Sink::Mem, url);
}

bool startFile(const char* url, const char* path, bool resume, uint64_t expect, const char* sha256_hex) {
  if (g_state == State::Busy) return false;
  snprintf(g_path, sizeof(g_path), "%s", path);
  g_resume = resume;
  g_expect = expect;
  g_file_sha = false;
  if (sha256_hex && !resume) {
    if (!hexToBytes(sha256_hex, g_sha, 32)) {
      finish(State::Failed, "Ugyldig version.txt (størrelse/SHA-256)");
      return false;
    }
    g_file_sha = true;
  }
  return launch(Sink::File, url);
}

bool startOta(const char* url, uint32_t size, const char* sha256_hex) {
  if (g_state == State::Busy) return false;
  if (!hexToBytes(sha256_hex, g_sha, 32) || !size) {
    finish(State::Failed, "Ugyldig version.txt (størrelse/SHA-256)");
    return false;
  }
  g_ota_size = size;
  g_expect = size;
  g_resume = false;
  g_file_sha = false;
  return launch(Sink::Ota, url);
}

State poll(char* msg, size_t n) {
  const State s = g_state;
  if (s == State::Done || s == State::Failed) {
    if (msg) snprintf(msg, n, "%s", g_msg);
    g_state = State::Idle;
  }
  return s;
}

bool busy() { return g_state == State::Busy; }
void cancel() { g_cancel = true; }

void progress(uint64_t& done, uint64_t& total, uint32_t& bps) {
  done = g_done;
  total = g_total;
  bps = g_bps;
}

const char* text() { return g_text ? g_text : ""; }

uint64_t sdFree() {
  const int pd = sdfw::pdrv();
  if (pd < 0) return 0;
  char drv[4];
  snprintf(drv, sizeof(drv), "%d:", pd);
  FATFS* fs = nullptr;
  DWORD clusters = 0;
  if (f_getfree(drv, &clusters, &fs) != FR_OK || !fs) return 0;
#if FF_MAX_SS != FF_MIN_SS
  const uint32_t ss = fs->ssize;
#else
  const uint32_t ss = FF_MAX_SS;
#endif
  return uint64_t(clusters) * fs->csize * ss;
}

}  // namespace dl
}  // namespace dm
