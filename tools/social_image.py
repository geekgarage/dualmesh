# SPDX-License-Identifier: MIT
# Laver billederne til opslag på sociale medier (LinkedIn, Facebook): 1200x630 (1.91:1, link-
# opslag) på engelsk og dansk, plus 1280x640 til GitHubs "Social preview" (Settings > General).
# Kræver Pillow (fx C:\Claude\tools\mapvenv). Brug: python tools/social_image.py
# Skrift: Montserrat og JetBrains Mono (OFL, fonts/). Logo: assets/logo/png.
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "social")
S = 2  # tegn i dobbelt størrelse og skalér ned (pænere kanter)

BG0, BG1 = (10, 16, 20), (14, 24, 30)
FG, MUTED, LINE = (232, 236, 238), (150, 166, 172), (40, 56, 62)
BLUE, GREEN = (56, 189, 248), (74, 222, 128)  # MeshCore, Meshtastic (som opstartsskærmen)

TEXT = {
    "en": {
        "head": ["One handheld.", "Both mesh networks."],
        "sub": "MeshCore and Meshtastic at the same time on one radio",
        "dev": "for the LilyGo T-Deck Plus",
        "chips": ["Offline maps", "SOS on both networks", "Encrypted DMs", "No phone needed"],
        "cta": "Install in your browser",
        "foot": "Open source · MIT",
    },
    "da": {
        "head": ["Én håndholdt.", "Begge mesh-net."],
        "sub": "MeshCore og Meshtastic på samme tid med én radio",
        "dev": "til LilyGo T-Deck Plus",
        "chips": ["Offline-kort", "SOS på begge net", "Krypterede DM'er", "Ingen telefon"],
        "cta": "Installér i browseren",
        "foot": "Open source · MIT",
    },
}


def font(name, size):
    return ImageFont.truetype(os.path.join(ROOT, "fonts", name), size * S)


def gradient(w, h):
    img = Image.new("RGB", (w, h), BG0)
    px = img.load()
    for y in range(h):
        for x in range(0, w):
            t = (x / w) * 0.6 + (y / h) * 0.4
            px[x, y] = tuple(int(BG0[i] + (BG1[i] - BG0[i]) * t) for i in range(3))
    return img


def mesh(draw, w, h, seed=7):
    """Svagt netværk af noder i baggrunden (blå forneden, grøn foroven, som logoet)."""
    rnd = random.Random(seed)
    pts = [(rnd.uniform(0.52, 1.02) * w, rnd.uniform(-0.05, 1.05) * h) for _ in range(26)]
    for i, a in enumerate(pts):
        near = sorted(pts, key=lambda b: (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2)[1:3]
        for b in near:
            col = GREEN if (a[1] + b[1]) / 2 < h / 2 else BLUE
            draw.line([a, b], fill=col + (38,), width=2 * S)
    for (x, y) in pts:
        col = GREEN if y < h / 2 else BLUE
        r = rnd.choice([4, 5, 7]) * S
        draw.ellipse([x - r, y - r, x + r, y + r], fill=col + (70,))


def chip(draw, x, y, text, f, col):
    pad_x, pad_y = 12 * S, 7 * S
    tw = draw.textlength(text, font=f)
    asc, desc = f.getmetrics()
    h = asc + desc + 2 * pad_y
    draw.rounded_rectangle([x, y, x + tw + 2 * pad_x, y + h], radius=h // 2, fill=(20, 32, 38), outline=col + (160,), width=S)
    draw.text((x + pad_x, y + pad_y), text, font=f, fill=FG)
    return x + tw + 2 * pad_x + 8 * S, h


def render(lang, W, H, path):
    w, h = W * S, H * S
    t = TEXT[lang]
    base = gradient(w, h).convert("RGBA")
    over = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    mesh(ImageDraw.Draw(over), w, h)
    over = over.filter(ImageFilter.GaussianBlur(0.6 * S))
    img = Image.alpha_composite(base, over)
    d = ImageDraw.Draw(img)

    # Logo til venstre.
    logo = Image.open(os.path.join(ROOT, "assets", "logo", "png", "dualmesh-logo-dark-1024.png")).convert("RGBA")
    ls = int(h * 0.52)
    logo = logo.resize((ls, ls), Image.LANCZOS)
    lx, ly = int(w * 0.055), int((h - ls) / 2) - int(h * 0.03)
    img.alpha_composite(logo, (lx, ly))

    # Tekst til højre for logoet.
    x = lx + ls + int(w * 0.045)
    f_brand = font("montserrat/Montserrat-SemiBold.ttf", 30)
    f_head = font("montserrat/Montserrat-SemiBold.ttf", 52)
    f_sub = font("montserrat/Montserrat-Medium.ttf", 23)
    f_chip = font("montserrat/Montserrat-Medium.ttf", 16)
    f_cta = font("montserrat/Montserrat-SemiBold.ttf", 20)
    asc, desc = f_chip.getmetrics()
    chip_h = asc + desc + 14 * S
    gap_cta = 44 * S
    # Hele tekstblokken (navn ... adresse) centreres lodret.
    total = (int(f_brand.size * 1.55) + 2 * int(f_head.size * 1.12) + 14 * S + int(f_sub.size * 1.35)
             + int(f_sub.size * 2.1) + chip_h + gap_cta + int(f_cta.size * 1.3))
    y = (h - total) // 2
    d.text((x, y), "DualMesh", font=f_brand, fill=FG)
    f_foot = font("montserrat/Montserrat-Medium.ttf", 16)
    fw = d.textlength(t["foot"], font=f_foot)
    d.text((w - int(w * 0.05) - fw, y + int(f_brand.size * 0.32)), t["foot"], font=f_foot, fill=MUTED)
    y += int(f_brand.size * 1.55)
    for i, line in enumerate(t["head"]):
        d.text((x, y), line, font=f_head, fill=FG if i == 0 else GREEN)
        y += int(f_head.size * 1.12)
    y += int(14 * S)
    # "MeshCore" blå og "Meshtastic" grøn i underteksten.
    cx = x
    for word in t["sub"].split(" "):
        col = BLUE if word == "MeshCore" else GREEN if word == "Meshtastic" else FG
        d.text((cx, y), word, font=f_sub, fill=col)
        cx += d.textlength(word + " ", font=f_sub)
    y += int(f_sub.size * 1.35)
    d.text((x, y), t["dev"], font=f_sub, fill=MUTED)
    y += int(f_sub.size * 2.1)

    # Funktioner som chips (ombrydes, så de holder sig inden for billedet).
    cx, cy = x, y
    for i, c in enumerate(t["chips"]):
        tw = d.textlength(c, font=f_chip) + 24 * S
        if cx + tw > w - int(w * 0.05):
            cx, cy = x, cy + int(f_chip.size * 2.3)
        cx, _ = chip(d, cx, cy, c, f_chip, BLUE if i % 2 == 0 else GREEN)

    # Bund: opfordring + adresse, og licens.
    f_url = font("jetbrainsmono/JetBrainsMono-Medium.ttf", 19)
    by = cy + chip_h + gap_cta
    d.line([(x, by - int(16 * S)), (w - int(w * 0.05), by - int(16 * S))], fill=LINE, width=S)
    d.text((x, by), t["cta"] + ":", font=f_cta, fill=FG)
    ux = x + d.textlength(t["cta"] + ":  ", font=f_cta)
    d.text((ux, by + 1 * S), "geekgarage.github.io/dualmesh", font=f_url, fill=GREEN)

    img = img.convert("RGB").resize((W, H), Image.LANCZOS)
    img.save(path, optimize=True)
    print(path)


def main():
    os.makedirs(OUT, exist_ok=True)
    for lang in ("en", "da"):
        render(lang, 1200, 630, os.path.join(OUT, f"dualmesh-social-{lang}.png"))
    render("en", 1280, 640, os.path.join(OUT, "dualmesh-github-preview.png"))


if __name__ == "__main__":
    main()
