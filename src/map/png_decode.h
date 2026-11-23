// SPDX-License-Identifier: MIT
// DualMesh – lille PNG-afkoder til kortfliser (fase 7). Understøtter de PNG'er, kortpakkerne
// bruger: palet (1/2/4/8 bit, fra tools/maps/make_map.py), gråtone 8 bit og RGB/RGBA 8 bit
// (gennemsigtighed ignoreres), uden interlace. Inflate kommer fra ESP32-S3'ens ROM (miniz).
// Resultatet er RGB565 med byterne byttet (som LVGL med LV_COLOR_16_SWAP = 1).
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {
namespace png {

// Afkod til out (w x h pixel). Billedet skal have præcis den størrelse. false ved fejl.
bool decode565(const uint8_t* data, size_t len, uint16_t* out, int w, int h);

}  // namespace png
}  // namespace dm
