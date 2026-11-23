// SPDX-License-Identifier: MIT
#include "diag_report.h"

#include <cstdio>
#include <cstring>

namespace dm {
namespace diag {

const char* causeName(uint32_t cause) {
  // Xtensa ISA: EXCCAUSE-værdier (offentlig arkitekturdokumentation).
  static const char* const kNames[] = {
      "IllegalInstruction", "Syscall", "InstructionFetchError", "LoadStoreError",
      "Level1Interrupt", "Alloca", "IntegerDivideByZero", "PCValue",
      "Privileged", "LoadStoreAlignment", nullptr, nullptr,
      "InstrPDAddrError", "LoadStorePIFDataError", "InstrPIFAddrError", "LoadStorePIFAddrError",
      "InstTLBMiss", "InstTLBMultiHit", "InstFetchPrivilege", nullptr,
      "InstrFetchProhibited", nullptr, nullptr, nullptr,
      "LoadStoreTLBMiss", "LoadStoreTLBMultihit", "LoadStorePrivilege", nullptr,
      "LoadProhibited", "StoreProhibited"};
  if (cause < sizeof(kNames) / sizeof(kNames[0]) && kNames[cause]) return kNames[cause];
  if (cause >= 32 && cause <= 39) return "CoprocessorDisabled";
  return "Panic";
}

size_t formatCrashLine(const CrashSummary& c, char* out, size_t n) {
  if (!n) return 0;
  if (!c.present) return size_t(std::snprintf(out, n, "intet gemt nedbrud"));
  int k = std::snprintf(out, n, "%s i %s, PC 0x%08lx, adresse 0x%08lx", causeName(c.cause),
                        c.task[0] ? c.task : "?", (unsigned long)c.pc, (unsigned long)c.vaddr);
  return k < 0 ? 0 : size_t(k) < n ? size_t(k) : n - 1;
}

size_t formatBacktrace(const CrashSummary& c, char* out, size_t n, bool decoder, int max_frames) {
  if (!n) return 0;
  out[0] = '\0';
  size_t len = 0;
  int frames = c.depth < max_frames ? c.depth : max_frames;
  if (frames > 16) frames = 16;
  for (int i = 0; i < frames; ++i) {
    char f[24];
    const int k = decoder ? std::snprintf(f, sizeof(f), "%s0x%08lx:0x00000000", i ? " " : "",
                                          (unsigned long)c.bt[i])
                          : std::snprintf(f, sizeof(f), "%s0x%08lx", i ? " " : "",
                                          (unsigned long)c.bt[i]);
    if (k <= 0 || len + size_t(k) + 1 > n) break;
    std::memcpy(out + len, f, size_t(k) + 1);
    len += size_t(k);
  }
  if (c.corrupted && len + 4 < n) {
    std::memcpy(out + len, " |<", 4);  // ESP-IDF's markering for en ødelagt backtrace
    len += 3;
  }
  return len;
}

size_t urlEncode(const char* in, char* out, size_t n) {
  static const char kHex[] = "0123456789ABCDEF";
  size_t len = 0;
  if (!n) return 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p; ++p) {
    const unsigned char ch = *p;
    const bool plain = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                       (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
                       // lovlige i en forespørgsel og sparer plads i QR-koden
                       ch == ':' || ch == ',' || ch == '/' || ch == '(' || ch == ')';
    const size_t need = plain ? 1 : 3;
    if (len + need + 1 > n) break;
    if (plain) {
      out[len++] = char(ch);
    } else {
      out[len++] = '%';
      out[len++] = kHex[ch >> 4];
      out[len++] = kHex[ch & 15];
    }
  }
  out[len] = '\0';
  return len;
}

namespace {

// Tilføjer "&<key>=<kodet værdi>". false = ikke plads.
bool addParam(char* out, size_t n, size_t& len, const char* key, const char* value) {
  char enc[512];
  const size_t vl = urlEncode(value, enc, sizeof(enc));
  if (std::strlen(value) && !vl) return false;
  const int k = std::snprintf(out + len, n - len, "&%s=%s", key, enc);
  if (k < 0 || len + size_t(k) >= n) return false;
  len += size_t(k);
  return true;
}

}  // namespace

size_t buildIssueUrl(const char* repo_url, const ReportInfo& info, char* out, size_t n,
                     size_t max_len) {
  if (!n) return 0;
  char version[64];
  std::snprintf(version, sizeof(version), "%s (%s) %s", info.fw_version, info.git_rev, info.board);
  const CrashSummary* c = info.crash;
  int frames = (c && c->present) ? c->depth : 0;
  for (;;) {
    // Kompakt (hvert tegn tæller i QR-koden): adresser uden 0x, rammer adskilt af komma.
    char crash[320] = "";
    if (c && c->present) {
      int k = std::snprintf(crash, sizeof(crash), "%s i %s pc:%08lx va:%08lx\nbt:", causeName(c->cause),
                            c->task[0] ? c->task : "?", (unsigned long)c->pc, (unsigned long)c->vaddr);
      for (int i = 0; i < frames && i < 16 && k > 0 && size_t(k) < sizeof(crash); ++i)
        k += std::snprintf(crash + k, sizeof(crash) - size_t(k), "%s%08lx", i ? "," : "",
                           (unsigned long)c->bt[i]);
      if (k > 0 && size_t(k) < sizeof(crash))
        std::snprintf(crash + k, sizeof(crash) - size_t(k), "%s\nelf:%.8s",
                      c->corrupted && frames == c->depth ? ",|<" : "", c->elf);
    } else {
      std::snprintf(crash, sizeof(crash), "intet gemt nedbrud");
    }
    size_t len = 0;
    const int k = std::snprintf(out, n, "%s/issues/new?template=fejlrapport.yml", repo_url);
    if (k < 0 || size_t(k) >= n) {
      out[0] = '\0';
      return 0;
    }
    len = size_t(k);
    const bool ok = addParam(out, n, len, "version", version) &&
                    addParam(out, n, len, "opstart", info.reset_reason) &&
                    addParam(out, n, len, "nedbrud", crash);
    if (ok && len <= max_len) return len;
    if (frames <= 0) break;  // passer ikke engang uden backtrace
    --frames;
  }
  out[0] = '\0';
  return 0;
}

}  // namespace diag
}  // namespace dm
