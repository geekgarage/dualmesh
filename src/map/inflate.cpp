// SPDX-License-Identifier: MIT
#include "map/inflate.h"

#include <cstdint>
#include <cstdlib>

#include "esp32s3/rom/miniz.h"
#include "esp_heap_caps.h"

namespace dm {

size_t inflateTo(uint8_t* out, size_t out_len, const uint8_t* in, size_t in_len, bool zlib) {
  tinfl_decompressor* d =
      static_cast<tinfl_decompressor*>(heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_8BIT));
  if (!d) return SIZE_MAX;
  tinfl_init(d);
  size_t in_n = in_len, out_n = out_len;
  const int flags = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | (zlib ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0);
  const tinfl_status st = tinfl_decompress(d, in, &in_n, out, out, &out_n, flags);
  free(d);
  return st == TINFL_STATUS_DONE ? out_n : SIZE_MAX;
}

}  // namespace dm
