// SPDX-License-Identifier: MIT
#include "diag/crash_info.h"

#include <cstdio>
#include <cstring>

#include "esp_core_dump.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

namespace dm {
namespace crashinfo {

bool read(diag::CrashSummary& out) {
  out = diag::CrashSummary{};
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0) return false;
  static esp_core_dump_summary_t s;  // ~200 byte; statisk for at skåne loop-stakken
  std::memset(&s, 0, sizeof(s));
  if (esp_core_dump_get_summary(&s) != ESP_OK) return false;
  out.present = true;
  std::snprintf(out.task, sizeof(out.task), "%.15s", s.exc_task);
  out.pc = s.exc_pc;
  out.cause = s.ex_info.exc_cause;
  out.vaddr = s.ex_info.exc_vaddr;
  uint32_t depth = s.exc_bt_info.depth;
  if (depth > 16) depth = 16;
  out.depth = uint8_t(depth);
  for (uint32_t i = 0; i < depth; ++i) out.bt[i] = s.exc_bt_info.bt[i];
  out.corrupted = s.exc_bt_info.corrupted;
  std::snprintf(out.elf, sizeof(out.elf), "%.16s", reinterpret_cast<const char*>(s.app_elf_sha256));
  return true;
}

bool fromOtherBuild(const diag::CrashSummary& c) {
  if (!c.present || !c.elf[0]) return false;
  char now[17] = "";
  esp_ota_get_app_elf_sha256(now, sizeof(now));
  return std::strncmp(now, c.elf, std::strlen(c.elf)) != 0;
}

const char* resetCode() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

void logAtBoot(Print& log) {
  const esp_reset_reason_t r = esp_reset_reason();
  if (r != ESP_RST_PANIC && r != ESP_RST_INT_WDT) return;
  diag::CrashSummary c;
  if (!read(c)) {
    log.printf("[CRASH] forrige koersel gik ned (%s), men der er intet core dump i flash\n",
               resetCode());
    return;
  }
  char line[96], bt[400];
  diag::formatCrashLine(c, line, sizeof(line));
  diag::formatBacktrace(c, bt, sizeof(bt), true);
  log.printf("[CRASH] forrige koersel gik ned (%s): %s\n", resetCode(), line);
  // Samme form som ESP-IDF's egen udskrift, så esp32_exception_decoder kan oversætte den.
  log.printf("[CRASH] Backtrace: %s\n", bt);
  log.printf("[CRASH] ELF-SHA256 %s (%s build)\n", c.elf,
             fromOtherBuild(c) ? "ANDET" : "samme");
}

bool erase() { return esp_core_dump_image_erase() == ESP_OK; }

}  // namespace crashinfo
}  // namespace dm
