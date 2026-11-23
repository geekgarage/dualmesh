"""Pixel-snapped small icons (no antialiasing) for 16/24/32 px."""
from PIL import Image, ImageDraw

DARK = dict(a=(74, 222, 128), b=(56, 189, 248), n=(229, 231, 235), cut=(0, 0, 0))
MONO = dict(a=1, b=1, n=1, cut=0)

# per-size hand-tuned grid geometry
SPEC = {
    16: dict(stem=(1, 1, 3, 14), base=None, cy=7.5, top=1, bot=14, rx=13, lw=1, ow=2,
             hubA=(8, 5), hubB=(8, 10), mid=(4, 7), mid2=(4, 8), right=(13, 7), rr=1, hr=1),
    24: dict(stem=(2, 2, 5, 21), base=(2, 18, 5, 21), cy=11.5, top=2, bot=21, rx=20, lw=1, ow=2,
             hubA=(12, 7), hubB=(12, 16), mid=(6, 11), mid2=(6, 12), right=(19, 11), rr=1, hr=1),
    32: dict(stem=(3, 3, 7, 28), base=(2, 24, 7, 28), cy=15.5, top=3, bot=28, rx=27, lw=2, ow=3,
             hubA=(16, 10), hubB=(16, 21), mid=(8, 15), mid2=(8, 16), right=(26, 15), rr=2, hr=2),
}


def draw(size, pal, mode):
    s = SPEC[size]
    im = Image.new(mode, (size, size), 0 if mode == "1" else (0, 0, 0))
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = s["stem"]
    top, bot, ow = s["top"], s["bot"], s["ow"]
    # bowl: bounding box of an ellipse whose left edge sits on the stem
    r = (bot - top) / 2
    cx = s["rx"] - r
    box = [cx - r, top, s["rx"], bot]
    d.arc(box, 270, 360, fill=pal["a"], width=ow)
    d.arc(box, 0, 90, fill=pal["b"], width=ow)
    # top / bottom bars between stem and arc
    d.rectangle([x1, top, cx, top + ow - 1], fill=pal["a"])
    d.rectangle([x1, bot - ow + 1, cx, bot], fill=pal["b"])
    # mesh links
    lw = s["lw"]
    ha, hb, m, m2, rt = s["hubA"], s["hubB"], s["mid"], s["mid2"], s["right"]
    k = 0.7071 * (r - ow / 2)
    cyy = (top + bot) / 2
    topmid = (round(cx + k), round(cyy - k))
    botmid = (round(cx + k), round(cyy + k))
    for p in (m, rt):
        d.line([ha, p], fill=pal["a"], width=lw)
    for p in (m2, rt):
        d.line([hb, p], fill=pal["b"], width=lw)
    # hubs
    hr = s["hr"]
    d.ellipse([ha[0] - hr, ha[1] - hr, ha[0] + hr, ha[1] + hr], fill=pal["a"])
    d.ellipse([hb[0] - hr, hb[1] - hr, hb[0] + hr, hb[1] + hr], fill=pal["b"])
    # antenna stem drawn last so it sits on top
    d.rectangle([x0, y0 + 1, x1 - 1, y1], fill=pal["n"])
    if x1 - x0 >= 3:
        d.rectangle([x0 + 1, y0, x1 - 2, y0], fill=pal["n"])  # rounded tip
    else:
        d.rectangle([x0, y0, x1 - 1, y0], fill=pal["n"])
    if s["base"]:
        bx0, by0, bx1, by1 = s["base"]
        d.rectangle([bx0, by0, bx1 - 1, by1], fill=pal["n"])
        d.line([bx0, by0 + (by1 - by0) // 2, bx1 - 1, by0 + (by1 - by0) // 2], fill=pal["cut"])
    # shared node on the right where both nets meet
    rr = s["rr"]
    d.rectangle([rt[0] - rr + 1, rt[1] - rr + 1, rt[0] + rr, rt[1] + rr], fill=pal["n"])
    return im


def icon(size, color=True):
    if color:
        return draw(size, DARK, "RGB")
    return draw(size, MONO, "1")


if __name__ == "__main__":
    tiles = [icon(16, False), icon(32, False), icon(16), icon(24), icon(32)]
    sc = 10
    W = sum(t.width * sc + 20 for t in tiles)
    H = 32 * sc
    sheet = Image.new("RGB", (W, H), (70, 70, 70))
    x = 0
    for t in tiles:
        t = t.convert("RGB").resize((t.width * sc, t.height * sc), Image.NEAREST)
        sheet.paste(t, (x, 0)); x += t.width + 20
    sheet.save("pixel-preview.png")
