"""Export PNGs and firmware headers (RGB565 for T-Deck, XBM 1-bit for Heltec)."""
import os, subprocess, io, sys
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
import logo
import pixel

OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SVG = os.path.join(OUT, "svg")
PNG = os.path.join(OUT, "png")
FW = os.path.join(OUT, "firmware")
for d in (PNG, FW):
    os.makedirs(d, exist_ok=True)


def render(svg_text_or_path, w=None, h=None):
    args = ["rsvg-convert"]
    if w: args += ["-w", str(w)]
    if h: args += ["-h", str(h)]
    if os.path.exists(svg_text_or_path):
        args.append(svg_text_or_path)
        data = subprocess.run(args, capture_output=True, check=True).stdout
    else:
        data = subprocess.run(args, input=svg_text_or_path.encode(), capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(data)).convert("RGBA")


# ---------- PNG exports ----------
for name in ["dualmesh-logo-dark", "dualmesh-logo-light", "dualmesh-logo-mono"]:
    for s in (1024, 512, 256, 128, 64):
        render(os.path.join(SVG, name + ".svg"), s, s).save(os.path.join(PNG, f"{name}-{s}.png"))
for name in ["dualmesh-logo-small-dark", "dualmesh-logo-small-mono"]:
    for s in (48, 32, 24):
        render(os.path.join(SVG, name + ".svg"), s, s).save(os.path.join(PNG, f"{name}-{s}.png"))
for name in ["dualmesh-wordmark-dark", "dualmesh-wordmark-light", "dualmesh-wordmark-mono"]:
    render(os.path.join(SVG, name + ".svg"), h=256).save(os.path.join(PNG, f"{name}-256h.png"))

FONT_B = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"
FONT_M = "/usr/share/fonts/opentype/inter/Inter-Medium.otf"


def on_bg(img, bg=(0, 0, 0)):
    base = Image.new("RGBA", img.size, bg + (255,))
    base.alpha_composite(img)
    return base.convert("RGB")


# ---------- T-Deck (ST7789 320x240, colour) ----------
def tdeck_splash():
    W, H = 320, 240
    im = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    mark = render(os.path.join(SVG, "dualmesh-logo-dark.svg"), 132, 132)
    im.alpha_composite(mark, ((W - 132) // 2, 22))
    d = ImageDraw.Draw(im)
    f1 = ImageFont.truetype(FONT_B, 30)
    f2 = ImageFont.truetype(FONT_M, 13)
    t = "DualMesh"
    tw = d.textlength(t, font=f1)
    d.text(((W - tw) / 2, 160), t, font=f1, fill=(229, 231, 235))
    # subtitle in the two network colours
    a, sep, b = "MeshCore", "  +  ", "Meshtastic"
    wa, ws, wb = (d.textlength(s, font=f2) for s in (a, sep, b))
    x = (W - (wa + ws + wb)) / 2
    d.text((x, 200), a, font=f2, fill=(56, 189, 248))
    d.text((x + wa, 200), sep, font=f2, fill=(156, 163, 175))
    d.text((x + wa + ws, 200), b, font=f2, fill=(74, 222, 128))
    return im.convert("RGB")


def rgb565(img):
    px = img.convert("RGB").load()
    w, h = img.size
    out = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            out.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    return out


def c_u16(name, vals, w, h, per=12):
    lines = [f"// {w}x{h} RGB565 (native uint16_t order)", f"static const uint16_t {name}[{w*h}] = {{"]
    for i in range(0, len(vals), per):
        lines.append("  " + ", ".join(f"0x{v:04X}" for v in vals[i:i + per]) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_u8(name, vals, comment, per=16):
    lines = [f"// {comment}", f"static const uint8_t {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        lines.append("  " + ", ".join(f"0x{v:02X}" for v in vals[i:i + per]) + ",")
    lines.append("};")
    return "\n".join(lines)


splash = tdeck_splash()
splash.save(os.path.join(PNG, "tdeck-splash-320x240.png"))

icons = {
    "dualmesh_icon64": render(os.path.join(SVG, "dualmesh-logo-dark.svg"), 64, 64),
    "dualmesh_icon24": (lambda im: (lambda a: (im.putalpha(a), im)[1])(im.convert("L").point(lambda v: 255 if v else 0)))(pixel.icon(24).convert("RGBA")),
}
for k, v in icons.items():
    v.save(os.path.join(PNG, f"tdeck-{k}.png"))

parts = ["""// DualMesh logo assets for LilyGo T-Deck / T-Deck Plus (ST7789, 320x240)
// Generated - do not edit by hand. Source: assets/logo/svg/
//
// Pixel data is RGB565 as native uint16_t values (on a black background).
//   LovyanGFX : lcd.pushImage(x, y, w, h, dualmesh_splash);
//   TFT_eSPI  : tft.setSwapBytes(true); tft.pushImage(x, y, w, h, dualmesh_splash);
//   Arduino_GFX: gfx->draw16bitRGBBitmap(x, y, (uint16_t*)dualmesh_splash, w, h);
// Icons also ship an 8-bit alpha plane (*_alpha) for drawing on non-black backgrounds.
// For LVGL 9 define DUALMESH_LOGO_LVGL before including to get lv_image_dsc_t descriptors.
#pragma once
#include <stdint.h>
""",
         "#define DUALMESH_SPLASH_W 320\n#define DUALMESH_SPLASH_H 240",
         c_u16("dualmesh_splash", rgb565(splash), 320, 240)]
for k, im in icons.items():
    s = im.size[0]
    parts.append(f"#define {k.upper()}_W {s}\n#define {k.upper()}_H {s}")
    parts.append(c_u16(k, rgb565(on_bg(im)), s, s))
    parts.append(c_u8(k + "_alpha", list(im.getchannel("A").tobytes()), f"{s}x{s} alpha, 0=transparent 255=opaque"))

lv = ["#ifdef DUALMESH_LOGO_LVGL", '#include "lvgl.h"']
for k, w, h in [("dualmesh_splash", 320, 240)] + [(k, im.size[0], im.size[1]) for k, im in icons.items()]:
    lv.append(f"""static const lv_image_dsc_t {k}_img = {{
  .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .flags = 0, .w = {w}, .h = {h}, .stride = {w*2}, .reserved_2 = 0 }},
  .data_size = {w*h*2},
  .data = (const uint8_t *){k},
  .reserved = NULL,
}};""")
lv.append("#endif // DUALMESH_LOGO_LVGL")
parts.append("\n".join(lv))
open(os.path.join(FW, "dualmesh_logo_tdeck.h"), "w").write("\n\n".join(parts) + "\n")


# ---------- Heltec LoRa32 V3.x (SSD1306 128x64, 1-bit) ----------
def to_1bit(img, thr=110):
    g = on_bg(img).convert("L")
    return g.point(lambda v: 255 if v >= thr else 0, "1")


def heltec_splash():
    W, H = 128, 64
    im = Image.new("1", (W, H), 0)
    mark = to_1bit(render(logo.svg_mark("mono", mono=False, simple=True), 60, 60))
    im.paste(mark, (2, 2))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(FONT_B, 22)
    d.text((64, 6), "Dual", font=f, fill=1)
    d.text((64, 32), "Mesh", font=f, fill=1)
    return im


ICON16 = """
................
.#######..#.....
.##.....#...#...
.##....#.#...#..
.##...#...#..#..
.##..#.....#..#.
.#####......###.
.##..#.....#..#.
.##...#...#..#..
.##....#.#...#..
.##.....#...#...
.##......#.#....
.##.......#.....
.#########......
................
................
"""


def icon16():
    rows = [r for r in ICON16.strip("\n").split("\n")]
    im = Image.new("1", (16, 16), 0)
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c == "#":
                im.putpixel((x, y), 1)
    return im


def xbm_bytes(im):
    w, h = im.size
    out = []
    for y in range(h):
        for bx in range(0, w, 8):
            b = 0
            for i in range(8):
                x = bx + i
                if x < w and im.getpixel((x, y)):
                    b |= 1 << i
            out.append(b)
    return out


hsplash = heltec_splash()
h32 = pixel.icon(32, False)
h16 = pixel.icon(16, False)
for n, im in [("heltec-splash-128x64", hsplash), ("heltec-icon32", h32), ("heltec-icon16", h16)]:
    im.save(os.path.join(PNG, n + ".png"))

hp = ["""// DualMesh logo assets for Heltec WiFi LoRa 32 V3.x (SSD1306 128x64, 1-bit)
// Generated - do not edit by hand. Source: assets/logo/svg/
//
// XBM format (LSB first, rows padded to whole bytes), 1 = pixel on.
//   U8g2           : u8g2.drawXBMP(x, y, w, h, dualmesh_oled_splash);
//   Adafruit_SSD1306: display.drawXBitmap(x, y, dualmesh_oled_splash, w, h, SSD1306_WHITE);
//   ThingPulse SSD1306: display.drawXbm(x, y, w, h, dualmesh_oled_splash);
#pragma once
#include <stdint.h>
"""]
for name, im in [("dualmesh_oled_splash", hsplash), ("dualmesh_oled_icon32", h32), ("dualmesh_oled_icon16", h16)]:
    w, h = im.size
    hp.append(f"#define {name.upper()}_W {w}\n#define {name.upper()}_H {h}")
    hp.append(c_u8(name, xbm_bytes(im), f"{w}x{h} XBM"))
open(os.path.join(FW, "dualmesh_logo_oled.h"), "w").write("\n\n".join(hp) + "\n")
print("done")
