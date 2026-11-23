// SPDX-License-Identifier: MIT
// DualMesh – LVGL v8.4-konfiguration (fase 4). Alt, der ikke står her, har LVGL's
// standardværdi (lv_conf_internal.h).
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// ST7789 over SPI: RGB565, byteombyttet (stor endian på bussen).
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1
#define LV_COLOR_SCREEN_TRANSP 0

// Hukommelse: LVGL's objekter ligger i PSRAM (intern RAM er knap: WiFi, BLE, MQTT).
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
#define LV_MEM_CUSTOM_ALLOC(s) heap_caps_malloc((s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE heap_caps_free
#define LV_MEM_CUSTOM_REALLOC(p, s) heap_caps_realloc((p), (s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

// Tid fra Arduino.
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_DPI_DEF 130

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_PERF_MONITOR 0

// Fonte: Montserrat med æøå og europæiske accenter (tools/gen_lvgl_font.py); LVGL's
// egen Montserrat 14 bruges kun som fallback til symbolerne (LV_SYMBOL_*).
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(dm_font_12) LV_FONT_DECLARE(dm_font_14) LV_FONT_DECLARE(dm_font_16) LV_FONT_DECLARE(dm_font_20) LV_FONT_DECLARE(dm_mono_13) LV_FONT_DECLARE(dm_emoji_14) LV_FONT_DECLARE(dm_emoji_color)
#define LV_FONT_DEFAULT &dm_font_14
#define LV_TXT_ENC LV_TXT_ENC_UTF8
// Ombryd kun ved mellemrum og tegnsætning, ikke ved '_' og '-': navne som "DualMesh_Anna" og
// "Heltec-test" (fx i @-omtaler) skal ikke deles midt i (brugerens ønske 17. nov 2026). Ord, der
// er længere end en linje, brydes stadig.
#define LV_TXT_BREAK_CHARS " ,.;:"

#define LV_USE_SPAN 1
// Farve-emojis vises som billeder i teksten (tools/gen_emoji_color.py).
#define LV_USE_IMGFONT 1
// QR-kode på "Omkring"-siden (link til GitHub-projektet).
#define LV_USE_QRCODE 1
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// Ikke brugt endnu.
#define LV_USE_DEMO_WIDGETS 0
#define LV_BUILD_EXAMPLES 0

#endif  // LV_CONF_H
#endif
