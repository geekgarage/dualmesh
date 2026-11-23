// SPDX-License-Identifier: MIT
// DualMesh – inflate (deflate/zlib) med ESP32-S3'ens ROM-miniz, men med dekompressoren i heap:
// tinfl_decompress_mem_to_mem lægger den (ca. 11 KB) på stakken, og loop-tasken har kun 8 KB.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {
// zlib = true: data har zlib-header (PNG). Returnerer antal byte skrevet, eller SIZE_MAX ved fejl.
size_t inflateTo(uint8_t* out, size_t out_len, const uint8_t* in, size_t in_len, bool zlib);
}  // namespace dm
