// SPDX-License-Identifier: MIT
// Tests for the crash summary formatting and the GitHub issue link shown as a QR code.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "diag_report.h"

using namespace dm::diag;

static CrashSummary sample(int depth) {
  CrashSummary c;
  c.present = true;
  std::snprintf(c.task, sizeof(c.task), "loopTask");
  c.pc = 0x4200abcd;
  c.cause = 28;  // LoadProhibited
  c.vaddr = 0;
  c.depth = uint8_t(depth);
  for (int i = 0; i < depth; ++i) c.bt[i] = 0x42001000u + uint32_t(i) * 0x111u;
  std::snprintf(c.elf, sizeof(c.elf), "1a2b3c4d5e6f7a8b");
  return c;
}

int main() {
  assert(std::strcmp(causeName(28), "LoadProhibited") == 0);
  assert(std::strcmp(causeName(29), "StoreProhibited") == 0);
  assert(std::strcmp(causeName(6), "IntegerDivideByZero") == 0);
  assert(std::strcmp(causeName(10), "Panic") == 0);  // reserveret værdi
  assert(std::strcmp(causeName(33), "CoprocessorDisabled") == 0);

  char buf[512];
  CrashSummary c = sample(3);
  formatCrashLine(c, buf, sizeof(buf));
  assert(std::strcmp(buf, "LoadProhibited i loopTask, PC 0x4200abcd, adresse 0x00000000") == 0);
  formatBacktrace(c, buf, sizeof(buf), true);
  assert(std::strcmp(buf, "0x42001000:0x00000000 0x42001111:0x00000000 0x42001222:0x00000000") == 0);
  formatBacktrace(c, buf, sizeof(buf), false, 2);
  assert(std::strcmp(buf, "0x42001000 0x42001111") == 0);
  c.corrupted = true;
  formatBacktrace(c, buf, sizeof(buf), false, 1);
  assert(std::strcmp(buf, "0x42001000 |<") == 0);

  urlEncode("a b/æ&x=1+2;3", buf, sizeof(buf));
  assert(std::strcmp(buf, "a%20b/%C3%A6%26x%3D1%2B2%3B3") == 0);
  // For lille buffer: stopper før et halvt %-escape.
  char small[6];
  urlEncode("ab cd", small, sizeof(small));
  assert(std::strcmp(small, "ab%20") == 0);

  const char* repo = "https://github.com/geekgarage/dualmesh";
  ReportInfo info;
  info.fw_version = "0.1.0";
  info.git_rev = "c48036d";
  info.board = "T-Deck Plus";
  info.reset_reason = "PANIC";

  // Uden nedbrud.
  size_t n = buildIssueUrl(repo, info, buf, sizeof(buf));
  assert(n > 0 && n <= 287);
  assert(std::strncmp(buf, "https://github.com/geekgarage/dualmesh/issues/new?template=fejlrapport.yml&version=0.1.0%20(c48036d)%20T-Deck%20Plus&opstart=PANIC&nedbrud=intet", 136) == 0);

  // Med et dybt nedbrud: backtracen beskæres, så linket passer i en QR-kode version 12.
  CrashSummary deep = sample(16);
  info.crash = &deep;
  n = buildIssueUrl(repo, info, buf, sizeof(buf));
  assert(n > 0 && n <= 287 && std::strlen(buf) == n);
  assert(std::strstr(buf, "LoadProhibited%20i%20loopTask%20pc:4200abcd%20va:00000000%0Abt:42001000,42001111,"));
  assert(!std::strstr(buf, "42001fff"));           // ramme 16 er skåret fra
  assert(std::strstr(buf, "%0Aelf:1a2b3c4d") && !std::strstr(buf, "1a2b3c4d5e"));
  std::printf("rapport-link %zu tegn: %s\n", n, buf);

  // Større grænse: alle 16 rammer med.
  n = buildIssueUrl(repo, info, buf, sizeof(buf), 1000);
  assert(n > 287 && std::strstr(buf, ",42001fff%0Aelf"));

  // For lille grænse: 0 og tom streng.
  n = buildIssueUrl(repo, info, buf, sizeof(buf), 50);
  assert(n == 0 && buf[0] == '\0');

  std::printf("rapport OK\n");
  return 0;
}
