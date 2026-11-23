# SPDX-License-Identifier: MIT
# Lægger en byline ("by GeekGarage.dk") på opstartslogoet i src/ui/logo/dualmesh_logo_tdeck.h.
#
# Splash-billedet (320x240 RGB565) læses ud af headeren, teksten tegnes med Montserrat (samme
# skrift som UI'et, fonts/montserrat, OFL) og arrayet skrives tilbage. Kører idempotent: den
# oprindelige splash gemmes første gang som assets/logo/firmware/splash_original.rgb565 og
# bruges som udgangspunkt hver gang, så teksten ikke tegnes oven i sig selv.
#
# Brug: python tools/splash_byline.py ["by GeekGarage.dk"] [--preview build/splash.png]
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR = os.path.join(ROOT, 'src', 'ui', 'logo', 'dualmesh_logo_tdeck.h')
ORIG = os.path.join(ROOT, 'assets', 'logo', 'firmware', 'splash_original.rgb565')
FONT = os.path.join(ROOT, 'fonts', 'montserrat', 'Montserrat-Medium.ttf')
W, H = 320, 240


def rgb565_to_rgb(v):
    return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def rgb_to_565(r, g, b):
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    text = args[0] if args else 'by GeekGarage.dk'
    preview = None
    if '--preview' in sys.argv:
        preview = sys.argv[sys.argv.index('--preview') + 1]
    src = open(HDR, encoding='utf-8').read()
    m = re.search(r'(static const uint16_t dualmesh_splash\[76800\] = \{)(.*?)(\};)', src, re.S)
    if not m:
        sys.exit('fandt ikke dualmesh_splash i headeren')
    if os.path.exists(ORIG):
        vals = list(memoryview(open(ORIG, 'rb').read()).cast('H'))
    else:
        vals = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', m.group(2))]
        with open(ORIG, 'wb') as f:
            f.write(b''.join(v.to_bytes(2, 'little') for v in vals))
    assert len(vals) == W * H, len(vals)
    img = Image.new('RGB', (W, H))
    img.putdata([rgb565_to_rgb(v) for v in vals])

    # Find den nederste række med logo-indhold (ikke baggrundsfarven i hjørnet).
    bg = img.getpixel((0, 0))
    def differs(p):
        return sum(abs(a - b) for a, b in zip(p, bg)) > 40
    last = 0
    for y in range(H):
        if any(differs(img.getpixel((x, y))) for x in range(0, W, 2)):
            last = y
    font = ImageFont.truetype(FONT, 13)
    d = ImageDraw.Draw(img)
    l, t, r, b = d.textbbox((0, 0), text, font=font)
    tw, th = r - l, b - t
    y = min(H - th - 6, max(last + 6, H - th - 14))
    x = (W - tw) // 2 - l
    d.text((x, y - t), text, font=font, fill=(0x9C, 0xA3, 0xAF))  # dæmpet grå (neutral)
    if preview:
        os.makedirs(os.path.dirname(preview) or '.', exist_ok=True)
        img.save(preview)

    out = [rgb_to_565(*p) for p in img.getdata()]
    lines = []
    for i in range(0, len(out), 12):
        lines.append('  ' + ', '.join('0x%04X' % v for v in out[i:i + 12]) + ',')
    body = '\n' + '\n'.join(lines) + '\n'
    src = src[:m.start(2)] + body + src[m.end(2):]
    open(HDR, 'w', encoding='utf-8', newline='\n').write(src)
    print('byline "%s" tegnet ved y=%d (logoets nederste række %d), bg %s' % (text, y, last, bg))


if __name__ == '__main__':
    main()
