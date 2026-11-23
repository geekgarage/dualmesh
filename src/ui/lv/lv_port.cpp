// SPDX-License-Identifier: MIT
#include "ui/lv/lv_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "storage/sd_log.h"
#include "ui/status_display.h"

namespace dm {
namespace lvport {

static constexpr int kW = 320, kH = 240, kBufRows = 40;
static StatusDisplay* g_d = nullptr;
static bool g_ready = false;
static lv_disp_draw_buf_t g_buf;
static lv_disp_drv_t g_drv;

static void flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  const size_t n = size_t(a->x2 - a->x1 + 1) * size_t(a->y2 - a->y1 + 1) * sizeof(lv_color_t);
  if (g_d) g_d->pushRect(a->x1, a->y1, a->x2, a->y2, reinterpret_cast<const uint8_t*>(px), n);
  lv_disp_flush_ready(drv);
}

bool begin(StatusDisplay* d) {
  if (g_ready) return true;
  g_d = d;
  const uint32_t t0 = millis();
  lv_init();
  void* buf = heap_caps_malloc(kW * kBufRows * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) {
    DLOG.println("[LVGL] ingen PSRAM til tegnebufferen");
    return false;
  }
  lv_disp_draw_buf_init(&g_buf, buf, nullptr, kW * kBufRows);
  lv_disp_drv_init(&g_drv);
  g_drv.hor_res = kW;
  g_drv.ver_res = kH;
  g_drv.flush_cb = flush;
  g_drv.draw_buf = &g_buf;
  lv_disp_t* disp = lv_disp_drv_register(&g_drv);
  lv_theme_t* th = lv_theme_default_init(disp, lv_color_hex(0x00FFFF), lv_color_hex(0x5DB3FF), true,
                                         &dm_font_14);
  lv_disp_set_theme(disp, th);
  g_ready = true;
  DLOG.printf("[LVGL] startet (v%d.%d.%d) paa %lu ms, intern RAM %u KB fri\n", LVGL_VERSION_MAJOR,
              LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, (unsigned long)(millis() - t0),
              unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  return true;
}

bool ready() { return g_ready; }

void handle() {
  if (g_ready) lv_timer_handler();
}

void invalidate() {
  if (g_ready) lv_obj_invalidate(lv_scr_act());
}

}  // namespace lvport
}  // namespace dm
