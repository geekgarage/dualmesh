// SPDX-License-Identifier: MIT
#include "storage/usb_disk.h"

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <lvgl.h>

#include <cstdio>

#include "driver/sdspi_host.h"
#include "driver/periph_ctrl.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "hal/misc.h"     // HAL_FORCE_MODIFY_U32_REG_FIELD, som rwdt_ll.h bruger
#include "hal/rwdt_ll.h"  // RTC-watchdoggen direkte (rtc_wdt_* er ikke med i S3-bibliotekerne)
#include "soc/usb_serial_jtag_reg.h"
#include "sdmmc_cmd.h"
#include "ui/lv/lv_port.h"
#include "ui/status_display.h"

namespace dm {
namespace usbdisk {

namespace {
constexpr uint32_t kMagic = 0x55534244;  // "USBD"
RTC_NOINIT_ATTR uint32_t g_flag;         // overlever esp_restart(), ikke strømsvigt

USBMSC g_msc;
sdmmc_card_t g_card;
volatile bool g_ok = false;      // kortet svarer
volatile bool g_ejected = false;  // PC'en har skubbet drevet ud
volatile uint32_t g_reads = 0, g_writes = 0, g_errors = 0;  // sektorer

int32_t onRead(uint32_t lba, uint32_t offset, void* buf, uint32_t size) {
  // TinyUSB beder om hele sektorer (offset 0, size et multiplum af 512).
  if (!g_ok || offset || size % 512) return -1;
  if (sdmmc_read_sectors(&g_card, buf, lba, size / 512) != ESP_OK) {
    ++g_errors;
    return -1;
  }
  g_reads += size / 512;
  return int32_t(size);
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t size) {
  if (!g_ok || offset || size % 512) return -1;
  if (sdmmc_write_sectors(&g_card, buf, lba, size / 512) != ESP_OK) {
    ++g_errors;
    return -1;
  }
  g_writes += size / 512;
  return int32_t(size);
}

bool onStartStop(uint8_t, bool start, bool load_eject) {
  if (load_eject && !start) g_ejected = true;  // "Skub ud" på PC'en
  return true;
}

// Tilbage til Serial-JTAG før genstarten. Valget af USB-PHY ligger i et RTC-register, som en
// softwaregenstart (esp_restart) ikke nulstiller: uden dette kører T-Deck'en normalt bagefter,
// men uden USB (hverken drev eller seriel port), indtil den slukkes eller resettes.
void backToSerialJtag() {
  periph_module_reset(PERIPH_USB_MODULE);
  periph_module_disable(PERIPH_USB_MODULE);
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                      RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
  // Træk D-/D+ lavt et øjeblik, så PC'en ser enheden forsvinde og kommer igen (ny opremsning).
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  pinMode(19, OUTPUT_OPEN_DRAIN);  // USB D-
  pinMode(20, OUTPUT_OPEN_DRAIN);  // USB D+
  digitalWrite(19, LOW);
  digitalWrite(20, LOW);
  delay(100);
  SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t rgb, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_width(l, 300);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l, text);
  return l;
}
}  // namespace

void rebootIntoStorage() {
  g_flag = kMagic;
  delay(200);  // lad loggen nå ud
  esp_restart();
}

bool takeRequest() {
  const bool on = g_flag == kMagic;
  g_flag = 0;
  return on;
}

void run(spi_host_device_t host, int sd_cs, int max_khz, StatusDisplay* disp, int click_pin) {
  // SD-kortet uden filsystem: kun sektorer.
  sdmmc_host_t h = SDSPI_HOST_DEFAULT();
  h.max_freq_khz = max_khz;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.host_id = host;
  dev.gpio_cs = gpio_num_t(sd_cs);
  sdspi_host_init();
  sdspi_dev_handle_t handle;
  uint64_t mb = 0;
  if (sdspi_host_init_device(&dev, &handle) == ESP_OK) {
    h.slot = handle;
    if (sdmmc_card_init(&h, &g_card) == ESP_OK && g_card.csd.sector_size == 512) {
      g_ok = true;
      mb = uint64_t(g_card.csd.capacity) * 512 / (1024 * 1024);
    }
  }

  if (g_ok) {
    g_msc.vendorID("DualMesh");
    g_msc.productID("T-Deck SD-kort");
    g_msc.productRevision("1.0");
    g_msc.onRead(onRead);
    g_msc.onWrite(onWrite);
    g_msc.onStartStop(onStartStop);
    g_msc.mediaPresent(true);
    g_msc.begin(uint32_t(g_card.csd.capacity), 512);
    USB.productName("DualMesh SD-kort");
    USB.begin();  // skifter USB fra Serial-JTAG til TinyUSB (den serielle port forsvinder)
  }

  // Skærmen (LVGL): hvad der sker, og hvordan man kommer ud.
  lv_obj_t* st = nullptr;
  if (disp && lvport::begin(disp)) {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_pad_all(scr, 10, 0);
    lv_obj_set_style_pad_row(scr, 8, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    label(scr, &dm_font_20, 0xFFE000, "SD-kort som USB-drev");
    char t[160];
    if (g_ok)
      snprintf(t, sizeof(t), "Kortet (%llu MB) vises som et drev på PC'en. Radio og net er slukket imens.",
               (unsigned long long)mb);
    else
      snprintf(t, sizeof(t), "SD-kortet svarer ikke. Klik på trackballen for at genstarte.");
    label(scr, &dm_font_14, 0xFFFFFF, t);
    label(scr, &dm_font_14, 0x40FF40,
          "Afslut: skub drevet ud på PC'en, så genstarter T-Deck'en selv. Eller klik på trackballen "
          "(skub ud først, ellers kan filer gå tabt).");
    st = label(scr, &dm_font_12, 0xA0A0A0, "");
    lv_scr_load(scr);
  }

  pinMode(click_pin, INPUT_PULLUP);
  uint32_t last = 0, held = 0;
  for (;;) {
    lvport::handle();
    const uint32_t now = millis();
    if (st && now - last > 1000) {
      last = now;
      char t[96];
      snprintf(t, sizeof(t), "Læst %lu KB, skrevet %lu KB%s", (unsigned long)(g_reads / 2),
               (unsigned long)(g_writes / 2), g_errors ? ", FEJL ved læsning/skrivning" : "");
      lv_label_set_text(st, t);
    }
    // Klik: kræv 100 ms nede (mod prel), og genstart først, når det slippes.
    if (digitalRead(click_pin) == LOW) {
      if (!held) held = now ? now : 1;
    } else if (held) {
      if (now - held > 100) break;
      held = 0;
    }
    if (g_ejected) {
      delay(500);  // lad PC'en afslutte
      break;
    }
    delay(20);
  }
  if (g_ok) backToSerialJtag();
  // Fuld genstart som ved reset-knappen: RTC-watchdoggen nulstiller også RTC-domænet, så
  // USB-PHY-valget er væk (en softwaregenstart alene gav en T-Deck uden USB, målt 22. nov 2026).
  rwdt_ll_write_protect_disable(&RTCCNTL);
  rwdt_ll_disable(&RTCCNTL);
  rwdt_ll_set_sys_reset_length(&RTCCNTL, WDT_RESET_SIG_LENGTH_3_2us);
  rwdt_ll_config_stage(&RTCCNTL, WDT_STAGE0, 20000 /* RTC-takter, ca. 0,15 s */, WDT_STAGE_ACTION_RESET_RTC);
  rwdt_ll_enable(&RTCCNTL);
  rwdt_ll_write_protect_enable(&RTCCNTL);
  delay(1000);
  esp_restart();  // reserve, hvis watchdoggen ikke nåede det
  for (;;) {
  }
}

}  // namespace usbdisk
}  // namespace dm
