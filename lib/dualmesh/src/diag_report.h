// SPDX-License-Identifier: MIT
// DualMesh – fejlrapport: nedbrudsresumé og et udfyldt GitHub-issue-link (vist som QR-kode).
//
// Hardware-uafhængig (ingen Arduino/ESP-IDF), så den kan testes på PC (host/tests/test_report.cpp).
// Selve nedbrudsdataene hentes af src/diag/crash_info.* fra ESP-IDF's core dump i flash.
//
// Privatliv: linket indeholder KUN firmwareversion, opstartsårsag og nedbrudsadresser (PC,
// backtrace, ELF-hash). Ingen navne, nøgler, positioner eller beskeder. Den fulde log går via
// websiden docs/rapport/, der renser den på brugerens egen PC.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {
namespace diag {

struct CrashSummary {
  bool present = false;
  char task[16] = "";       // opgaven, der gik ned (fx loopTask)
  uint32_t pc = 0;          // programtæller ved nedbruddet
  uint32_t cause = 0;       // Xtensa EXCCAUSE
  uint32_t vaddr = 0;       // adressen, der blev tilgået (EXCVADDR)
  uint32_t bt[16] = {};     // backtrace (PC'er), nyeste først
  uint8_t depth = 0;
  bool corrupted = false;   // backtracen stoppede ved en ødelagt stak
  char elf[17] = "";        // de første 16 hex-tegn af det nedbrudte builds ELF-SHA256
};

// Navnet på en Xtensa-undtagelse, som ESP-IDF skriver det (fx "LoadProhibited").
const char* causeName(uint32_t cause);

// "LoadProhibited i loopTask, PC 0x4200abcd, adresse 0x00000000"
size_t formatCrashLine(const CrashSummary& c, char* out, size_t n);

// Backtrace. decoder = true: "0x4200abcd:0x00000000 ..." (esp32_exception_decoder i
// PlatformIO-monitoren forstår formatet); ellers kun adresserne "0x4200abcd 0x42001234 ...".
size_t formatBacktrace(const CrashSummary& c, char* out, size_t n, bool decoder, int max_frames = 16);

struct ReportInfo {
  const char* fw_version = "";   // DM_FW_VERSION
  const char* git_rev = "";      // DM_GIT_REV
  const char* board = "";        // fx "T-Deck Plus"
  const char* reset_reason = ""; // kort ASCII-kode for denne opstart, fx "PANIC"
  const CrashSummary* crash = nullptr;
};

// Bygger <repo_url>/issues/new?template=fejlrapport.yml&version=...&opstart=...&nedbrud=...
// Felternes id'er svarer til .github/ISSUE_TEMPLATE/fejlrapport.yml. Er linket for langt til
// max_len, fjernes backtrace-adresser bagfra, indtil det passer. Returnerer længden (0 = passede
// ikke). max_len 287 = QR-version 12 med middel fejlkorrektion (LVGL's lv_qrcode), som med 3 px pr.
// modul giver en kode på 195 px, der kan scannes fra T-Deck'ens skærm.
size_t buildIssueUrl(const char* repo_url, const ReportInfo& info, char* out, size_t n,
                     size_t max_len = 287);

// Procent-kodning til en URL-forespørgsel (alt undtagen A-Z a-z 0-9 - _ . ~ kodes).
size_t urlEncode(const char* in, char* out, size_t n);

}  // namespace diag
}  // namespace dm
