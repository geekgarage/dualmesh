// SPDX-License-Identifier: MIT
#include "storage/nvs_tool.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstdio>
#include <cctype>
#include <cstring>

#include "storage/sd_log.h"

namespace dm {
namespace nvstool {

namespace {
Kind kindOf(const char* ns) {
  if (!strcmp(ns, "bt_config.conf") || !strcmp(ns, "nimble_bond")) return Bluetooth;
  if (!strcmp(ns, "nvs.net80211")) return Wifi;
  if (!strcmp(ns, "phy")) return Phy;
  return Other;
}
}  // namespace

bool stats(Stats& out) {
  nvs_stats_t st;
  if (nvs_get_stats(nullptr, &st) != ESP_OK) return false;
  out.used = st.used_entries;
  out.free = st.free_entries;
  out.total = st.total_entries;
  out.namespaces = st.namespace_count;
  return true;
}

int list(Ns* out, int max) {
  int n = 0;
  nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY);
  while (it) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    int k = 0;
    while (k < n && strcmp(out[k].name, info.namespace_name) != 0) ++k;
    if (k == n && n < max) {
      snprintf(out[n].name, sizeof(out[n].name), "%s", info.namespace_name);
      out[n].entries = 0;
      out[n].kind = kindOf(info.namespace_name);
      ++n;
    }
    if (k < n) ++out[k].entries;
    it = nvs_entry_next(it);
  }
  return n;
}

const char* kindText(Kind k) {
  switch (k) {
    case Bluetooth: return "Bluetooth";
    case Wifi: return "WiFi";
    case Phy: return "radio-kal.";
    default: return "anden fw";
  }
}

bool eraseNamespace(const char* ns) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  DLOG.printf("[NVS] navnerum \"%s\" ryddet af brugeren: %s\n", ns, ok ? "OK" : "FEJL");
  return ok;
}

bool eraseAll() {
  nvs_flash_deinit();
  const esp_err_t e = nvs_flash_erase();
  nvs_flash_init();
  DLOG.printf("[NVS] HELE den interne NVS ryddet af brugeren: %s\n", e == ESP_OK ? "OK" : esp_err_to_name(e));
  return e == ESP_OK;
}

const Launcher& launcher() {
  static Launcher l;
  static bool done = false;
  if (done) return l;
  done = true;
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (run) snprintf(l.running, sizeof(l.running), "%s", run->label);
  esp_app_desc_t self{};
  if (run) esp_ota_get_partition_description(run, &self);
  // En anden app i factory-partitionen, eller en app med "launch" i navnet i en af
  // de andre app-partitioner (nogle launchere bruger OTA-partitioner), regnes for en
  // launcher. En gammel DualMesh/anden firmware efter en OTA tæller ikke.
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  for (; it && !l.found; it = esp_partition_next(it)) {
    const esp_partition_t* p = esp_partition_get(it);
    esp_app_desc_t d{};
    if (p == run || esp_ota_get_partition_description(p, &d) != ESP_OK) continue;
    if (!strcmp(d.project_name, self.project_name)) continue;
    char low[33];
    snprintf(low, sizeof(low), "%s", d.project_name);
    for (char* c = low; *c; ++c) *c = char(tolower(*c));
    if (p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY || strstr(low, "launch")) {
      l.found = true;
      snprintf(l.name, sizeof(l.name), "%s", d.project_name);
    }
  }
  if (it) esp_partition_iterator_release(it);
  return l;
}

}  // namespace nvstool
}  // namespace dm
