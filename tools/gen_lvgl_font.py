# SPDX-License-Identifier: MIT
# Laver LVGL-fonte (lv_font_fmt_txt, 4 bpp, LVGL v8) ud fra en TTF-fil med FreeType.
# Erstatter lv_font_conv (kræver Node.js). Tegnsættet er det samme som vores 8x16-font
# (src/ui/font8x16.h: æøå, europæiske accenter) + et par typografiske tegn.
#
# Brug (FreeType via freetype-py, fx i D:\Claude\tools\fontvenv):
#   python tools/gen_lvgl_font.py            -> alle fonte i src/ui/lv/fonts/
# Fontene er Montserrat og JetBrains Mono (begge SIL Open Font License 1.1, se OFL.txt i
# fonts/montserrat/ og fonts/jetbrainsmono/). JetBrains Mono (monospace) bruges til
# underskærmene, der er bygget som et tekstgitter med kolonner.
import os
import re
import sys

import freetype

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "ui", "lv", "fonts")
MS = ("montserrat", "Montserrat, Copyright 2011 The Montserrat Project Authors")
JB = ("jetbrainsmono", "JetBrains Mono, Copyright 2020 The JetBrains Mono Project Authors")
NE = ("notoemoji", "Noto Emoji, Copyright 2013 Google LLC")
# Emojis (sort/hvid, farves som teksten): symboler, piktogrammer, smileys, transport og
# supplerende symboler + variationsvælger/ZWJ (usynlige). Bruges som fallback for alle fonte.
EMOJI = (list(range(0x2600, 0x27C0)) + list(range(0x1F300, 0x1F650)) +
         list(range(0x1F680, 0x1F700)) + list(range(0x1F900, 0x1FA00)) + [0x200D, 0xFE0F])
FONTS = [  # (navn, (mappe, ophav), fil, størrelse i px)
    ("dm_font_12", MS, "Montserrat-Medium.ttf", 12),
    ("dm_font_14", MS, "Montserrat-Medium.ttf", 14),
    ("dm_font_16", MS, "Montserrat-Medium.ttf", 16),
    ("dm_font_20", MS, "Montserrat-SemiBold.ttf", 20),
    ("dm_mono_13", JB, "JetBrainsMono-Medium.ttf", 13),
    ("dm_emoji_14", NE, "NotoEmoji.ttf", 14),
]
EXTRA = "–—‘’‚“”„•…€°±×÷·«»©"  # © til kortets kildeangivelse


def codepoints():
    src = open(os.path.join(ROOT, "src", "ui", "font8x16.h"), encoding="utf-8").read()
    m = re.search(r"kCodepoints\[[^\]]*\]\s*=\s*\{([^}]*)\}", src)
    cps = {int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(1))}
    cps |= {ord(c) for c in EXTRA}
    return sorted(c for c in cps if c >= 32)


def gen(name, fam, ttf, size, cps, fallback="&dm_emoji_color"):
    cps = sorted(set(cps))
    face = freetype.Face(os.path.join(ROOT, "fonts", fam[0], ttf))
    face.set_pixel_sizes(0, size)
    asc = (face.size.ascender + 63) // 64
    desc = (-face.size.descender + 63) // 64
    bitmap = bytearray()
    glyphs = [(0, 0, 0, 0, 0, 0)]  # id 0 er reserveret
    used = []
    for cp in cps:
        if face.get_char_index(cp) == 0:
            continue  # findes ikke i fonten (LVGL bruger fallback-fonten)
        face.load_char(cp, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        g = face.glyph
        bm = g.bitmap
        w, h = bm.width, bm.rows
        idx = len(bitmap)
        # 4 bpp, rækkerne pakket i forlængelse af hinanden (LVGL "plain" format).
        nib = []
        for y in range(h):
            for x in range(w):
                nib.append(bm.buffer[y * bm.pitch + x] >> 4)
        if len(nib) % 2:
            nib.append(0)
        for i in range(0, len(nib), 2):
            bitmap.append((nib[i] << 4) | nib[i + 1])
        adv = round(g.advance.x * 16 / 64)  # 1/16 px
        glyphs.append((idx, adv, w, h, g.bitmap_left, g.bitmap_top - h))
        used.append(cp)

    lines = []
    lines.append("// SPDX-License-Identifier: MIT")
    lines.append(f"// GENERERET af tools/gen_lvgl_font.py - ret ikke i hånden.")
    lines.append(f"// {ttf} {size} px, 4 bpp, {len(used)} tegn. Font: {fam[1]},")
    lines.append(f"// SIL Open Font License 1.1 (fonts/{fam[0]}/OFL.txt).")
    lines.append('#include "lvgl.h"')
    if fallback.startswith("&dm_"):
        lines.append(f"LV_FONT_DECLARE({fallback[1:]})")
    lines.append("")
    lines.append(f"static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}_bitmap[] = {{")
    for i in range(0, len(bitmap), 24):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in bitmap[i:i + 24]) + ",")
    lines.append("};")
    lines.append("")
    lines.append(f"static const lv_font_fmt_txt_glyph_dsc_t {name}_glyphs[] = {{")
    for (idx, adv, w, h, ox, oy) in glyphs:
        lines.append(f"    {{.bitmap_index = {idx}, .adv_w = {adv}, .box_w = {w}, .box_h = {h}, "
                     f".ofs_x = {ox}, .ofs_y = {oy}}},")
    lines.append("};")
    lines.append("")
    # Tegnområder: én cmap pr. gruppe, hvor forskydningen fra starten kan stå i uint16.
    groups = []
    for i, cp in enumerate(used):
        if not groups or cp - groups[-1][0] > 0xFFFF:
            groups.append([cp, i, []])
        groups[-1][2].append(cp)
    for gi, (start, first, cps_g) in enumerate(groups):
        lines.append(f"static const uint16_t {name}_unicode{gi}[] = {{")
        offs = [cp - start for cp in cps_g]
        for i in range(0, len(offs), 16):
            lines.append("    " + ", ".join(str(o) for o in offs[i:i + 16]) + ",")
        lines.append("};")
    lines.append("")
    lines.append(f"static const lv_font_fmt_txt_cmap_t {name}_cmaps[] = {{")
    for gi, (start, first, cps_g) in enumerate(groups):
        lines.append(f"    {{.range_start = {start}, .range_length = {cps_g[-1] - start + 1}, "
                     f".glyph_id_start = {first + 1}, .unicode_list = {name}_unicode{gi}, "
                     f".glyph_id_ofs_list = NULL, .list_length = {len(cps_g)}, "
                     f".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}},")
    lines.append("};")
    lines.append("")
    lines.append("#if LVGL_VERSION_MAJOR == 8")
    lines.append(f"static lv_font_fmt_txt_glyph_cache_t {name}_cache;")
    lines.append("#endif")
    lines.append(f"static const lv_font_fmt_txt_dsc_t {name}_dsc = {{")
    lines.append(f"    .glyph_bitmap = {name}_bitmap,")
    lines.append(f"    .glyph_dsc = {name}_glyphs,")
    lines.append(f"    .cmaps = {name}_cmaps,")
    lines.append("    .kern_dsc = NULL,")
    lines.append("    .kern_scale = 0,")
    lines.append(f"    .cmap_num = {len(groups)},")
    lines.append("    .bpp = 4,")
    lines.append("    .kern_classes = 0,")
    lines.append("    .bitmap_format = 0,")
    lines.append("#if LVGL_VERSION_MAJOR == 8")
    lines.append(f"    .cache = &{name}_cache,")
    lines.append("#endif")
    lines.append("};")
    lines.append("")
    lines.append(f"const lv_font_t {name} = {{")
    lines.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    lines.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    lines.append(f"    .line_height = {asc + desc},")
    lines.append(f"    .base_line = {desc},")
    lines.append("    .subpx = LV_FONT_SUBPX_NONE,")
    lines.append("    .underline_position = -1,")
    lines.append("    .underline_thickness = 1,")
    lines.append(f"    .dsc = &{name}_dsc,")
    lines.append(f"    .fallback = {fallback},  // emojis -> LVGL's symboler (LV_SYMBOL_*)")
    lines.append("};")
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".c")
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print(f"{path}: {len(used)} tegn, {len(bitmap)} byte bitmap")


def main():
    cps = codepoints()
    for name, fam, ttf, size in FONTS:
        if fam is NE:
            gen(name, fam, ttf, size, EMOJI, "&lv_font_montserrat_14")
        else:
            gen(name, fam, ttf, size, cps)


if __name__ == "__main__":
    sys.exit(main())
