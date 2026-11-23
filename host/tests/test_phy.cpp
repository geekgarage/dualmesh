// SPDX-License-Identifier: MIT
#include <cassert>
#include <cmath>
#include <cstdio>
#include "phy_math.h"

using namespace dm;

int main() {
  LoRaProfile lf;  // defaults = Meshtastic LongFast
  // Reference: SF11/250 kHz/4-5, 16-byte payload = 354.304 ms on air.
  const uint32_t at = phy::airtimeUs(lf, 16);
  std::printf("LongFast 16 B: %u us\n", at);
  assert(at == 354304);
  assert(std::fabs(phy::symbolUs(lf) - 8192.0) < 1e-6);
  assert(!phy::ldro(lf));

  LoRaProfile mc;
  mc.bw_hz = 62500; mc.sf = 8; mc.cr = 8; mc.preamble_syms = 16;
  assert(std::fabs(phy::symbolUs(mc) - 4096.0) < 1e-6);

  LoRaProfile slow = lf;
  slow.bw_hz = 125000; slow.sf = 12;  // 32.77 ms symbols -> LDRO on
  assert(phy::ldro(slow));
  (void)slow;
  std::puts("phy tests OK");
  return 0;
}
