# SPDX-License-Identifier: MIT
# Laver farve-emojis til LVGL (brugerens ønske 16. nov 2026): hver emoji som et lille billede
# (RGB565 + alfa, LV_IMG_CF_TRUE_COLOR_ALPHA, byteombyttet som LV_COLOR_16_SWAP) og en
# "font", der viser billederne som tegn (LVGL's imgfont-mekanisme, LV_USE_IMGFONT).
# Kilde: Noto Color Emoji (CBDT-bitmaps, SIL Open Font License 1.1,
# fonts/notocoloremoji/OFL.txt). PNG'erne tages ud med fontTools og skaleres med Pillow.
#
# Brug (fontTools + Pillow, fx i D:\Claude\tools\fontvenv):
#   python tools/gen_emoji_color.py   -> src/ui/lv/fonts/dm_emoji_color.c
import io
import os

from fontTools.ttLib import TTFont
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "fonts", "notocoloremoji", "NotoColorEmoji.ttf")
OUT = os.path.join(ROOT, "src", "ui", "lv", "fonts", "dm_emoji_color.c")
W, H = 16, 15          # billedstørrelse (passer til 14 px tekst)
OFS_Y = -2             # sænket lidt under grundlinjen
RANGES = [(0x2600, 0x27C0), (0x1F300, 0x1FB00)]
SKIP = set(range(0x1F3FB, 0x1F400))  # hudfarve-modifikatorer (vises ikke alene)


def main():
    f = TTFont(SRC)
    cmap = f.getBestCmap()
    strike = f["CBDT"].strikeData[0]
    cps = sorted(cp for cp in cmap if any(a <= cp < b for a, b in RANGES) and cp not in SKIP)
    data = bytearray()
    entries = []
    for cp in cps:
        g = strike.get(cmap[cp])
        if g is None:
            continue
        if hasattr(g, "decompile") and not hasattr(g, "imageData"):
            g.decompile()
        im = Image.open(io.BytesIO(g.imageData)).convert("RGBA").resize((W, H), Image.LANCZOS)
        off = len(data)
        for r, gg, b, a in im.getdata():
            if a == 0:
                r = gg = b = 0
            v = ((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3)
            data += bytes((v >> 8, v & 0xFF, a))  # stor endian (LV_COLOR_16_SWAP) + alfa
        entries.append((cp, off))

    L = []
    L.append("// SPDX-License-Identifier: MIT")
    L.append("// GENERERET af tools/gen_emoji_color.py - ret ikke i hånden.")
    L.append(f"// {len(entries)} farve-emojis, {W}x{H} px, RGB565 + alfa. Kilde: Noto Color Emoji,")
    L.append("// Copyright 2013 Google LLC, SIL Open Font License 1.1 (fonts/notocoloremoji/OFL.txt).")
    L.append('#include "lvgl.h"')
    L.append("")
    L.append("LV_FONT_DECLARE(dm_emoji_14)")
    L.append("")
    L.append(f"#define EMOJI_N {len(entries)}")
    L.append(f"#define EMOJI_W {W}")
    L.append(f"#define EMOJI_H {H}")
    L.append("")
    L.append("static LV_ATTRIBUTE_LARGE_CONST const uint8_t emoji_px[] = {")
    for i in range(0, len(data), 30):
        L.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 30]) + ",")
    L.append("};")
    L.append("")
    L.append("static const uint32_t emoji_cp[EMOJI_N] = {")
    for i in range(0, len(entries), 10):
        L.append("    " + ", ".join(f"0x{cp:X}" for cp, _ in entries[i:i + 10]) + ",")
    L.append("};")
    L.append("")
    L.append("#define E(off) {.header = {.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, "
             ".reserved = 0, .w = EMOJI_W, .h = EMOJI_H}, .data_size = EMOJI_W * EMOJI_H * 3, "
             ".data = emoji_px + (off)}")
    L.append("// Ét billede pr. emoji (LVGL's billedcache bruger adressen som nøgle).")
    L.append("static const lv_img_dsc_t emoji_img[EMOJI_N] = {")
    for i in range(0, len(entries), 6):
        L.append("    " + ", ".join(f"E({off})" for _, off in entries[i:i + 6]) + ",")
    L.append("};")
    L.append("")
    L.append(r'''static int emoji_find(uint32_t cp) {
  int lo = 0, hi = EMOJI_N - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (emoji_cp[mid] == cp) return mid;
    if (emoji_cp[mid] < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

static bool emoji_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, uint32_t cp, uint32_t next) {
  (void)font;
  (void)next;
  if (emoji_find(cp) < 0) return false;
  dsc->adv_w = EMOJI_W + 1;
  dsc->box_w = EMOJI_W;
  dsc->box_h = EMOJI_H;
  dsc->ofs_x = 0;
  dsc->ofs_y = ''' + str(OFS_Y) + r''';
  dsc->bpp = LV_IMGFONT_BPP;  // billede, ikke en almindelig glyf
  dsc->is_placeholder = 0;
  return true;
}

static const uint8_t* emoji_glyph_bitmap(const lv_font_t* font, uint32_t cp) {
  (void)font;
  const int i = emoji_find(cp);
  return i < 0 ? NULL : (const uint8_t*)&emoji_img[i];
}

/* Antal og kodepunkter (til emoji-vælgeren). */
int dm_emoji_count(void) { return EMOJI_N; }
uint32_t dm_emoji_at(int i) { return (i >= 0 && i < EMOJI_N) ? emoji_cp[i] : 0; }

/* Farve-emojis; findes en emoji ikke her, bruges den sort/hvide (dm_emoji_14). */
const lv_font_t dm_emoji_color = {
    .get_glyph_dsc = emoji_glyph_dsc,
    .get_glyph_bitmap = emoji_glyph_bitmap,
    .line_height = EMOJI_H,
    .base_line = 0,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = 0,
    .underline_thickness = 0,
    .dsc = NULL,
    .fallback = &dm_emoji_14,
};''')
    open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(L) + "\n")
    print(f"{OUT}: {len(entries)} emojis, {len(data)} byte billeddata")


if __name__ == "__main__":
    main()
