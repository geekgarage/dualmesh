"""DualMesh logo generator: SVG masters + wordmark (text as paths)."""
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.boundsPen import BoundsPen

OUT = os.path.join(os.path.dirname(__file__), "..")

PALETTES = {
    "dark":  dict(a="#4ADE80", b="#38BDF8", n="#E5E7EB"),   # for dark backgrounds
    "light": dict(a="#16A34A", b="#0284C7", n="#1F2937"),   # for light backgrounds
    "mono":  dict(a="#FFFFFF", b="#FFFFFF", n="#FFFFFF"),   # 1-bit OLED, white on black
}

# Tight square view box around the mark (content ~x34..103, y25..105)
VB = (23.5, 20, 90, 90)

LINKS_A = "M68,48 L62,30 M68,48 L86.7,40.3 M68,48 L97,65 M68,48 L48,65"
LINKS_B = "M68,82 L62,100 M68,82 L86.7,89.7 M68,82 L97,65 M68,82 L48,65"
NODES_A = [(68, 48, 6), (62, 30, 5), (86.7, 40.3, 5)]
NODES_B = [(68, 82, 6), (62, 100, 5), (86.7, 89.7, 5)]


def mark(pal, mono=False, simple=False, mid="m"):
    """Return (defs, body) SVG fragments. Cut-outs are real transparency via a mask."""
    p = PALETTES[pal]
    lw = 3.6 if simple else 3          # link width
    dw = 7 if simple else 6            # D outline width
    ring = 2.0 if simple else 2.5      # hollow-node ring width
    cut = []                           # shapes punched out (black in mask)
    body = ['<g fill="none" stroke-linecap="round" stroke-linejoin="round">']
    body.append(f'<path d="{LINKS_A}" stroke="{p["a"]}" stroke-width="{lw}"/>')
    body.append(f'<path d="{LINKS_B}" stroke="{p["b"]}" stroke-width="{lw}"/>')
    body.append(f'<path d="M48,30 L62,30 A35,35 0 0 1 97,65" stroke="{p["a"]}" stroke-width="{dw}"/>')
    body.append(f'<path d="M97,65 A35,35 0 0 1 62,100 L48,100" stroke="{p["b"]}" stroke-width="{dw}"/>')
    body.append('</g>')
    # antenna: whip + SMA base
    body.append(f'<rect x="36" y="27" width="12" height="62" rx="6" fill="{p["n"]}"/>')
    body.append(f'<rect x="34" y="86" width="16" height="17" rx="2" fill="{p["n"]}"/>')
    if not simple:
        cut.append('<path d="M37,76 L47,76 M37,81 L47,81 M35,92 L49,92 M35,97 L49,97" '
                   'stroke="#000" stroke-width="2" stroke-linecap="round" fill="none"/>')
        cut.append('<circle cx="42" cy="58" r="5.75" fill="#000"/>')
        cut.append('<circle cx="42" cy="58" r="3.25" fill="#fff"/>')
    else:
        cut.append('<path d="M36,92.5 L48,92.5" stroke="#000" stroke-width="2.6" fill="none"/>')
    # nodes
    for x, y, r in NODES_A:
        body.append(f'<circle cx="{x}" cy="{y}" r="{r + (0.6 if simple else 0)}" fill="{p["a"]}"/>')
    for x, y, r in NODES_B:
        rr = r + (0.6 if simple else 0)
        body.append(f'<circle cx="{x}" cy="{y}" r="{rr}" fill="{p["b"]}"/>')
        if mono:  # hollow in mono so the two networks differ without colour
            cut.append(f'<circle cx="{x}" cy="{y}" r="{rr - ring}" fill="#000"/>')
    body.append(f'<circle cx="97" cy="65" r="{6.6 if simple else 6}" fill="{p["n"]}"/>')
    x, y, w, h = VB
    defs = (f'<mask id="{mid}" maskUnits="userSpaceOnUse" x="{x}" y="{y}" width="{w}" height="{h}">'
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#fff"/>' + "".join(cut) + '</mask>')
    return defs, f'<g mask="url(#{mid})">' + "".join(body) + '</g>'


def svg_mark(pal, mono=False, simple=False, bg=None):
    x, y, w, h = VB
    defs, body = mark(pal, mono, simple)
    bgr = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x} {y} {w} {h}" width="{w*4:g}" height="{h*4:g}">'
            f'<title>DualMesh</title><defs>{defs}</defs>{bgr}{body}</svg>')


def text_path(text, font_path, size):
    f = TTFont(font_path)
    gs = f.getGlyphSet()
    cmap = f.getBestCmap()
    upm = f["head"].unitsPerEm
    s = size / upm
    paths, xpos = [], 0
    hmtx = f["hmtx"]
    for ch in text:
        g = cmap[ord(ch)]
        pen = SVGPathPen(gs)
        gs[g].draw(pen)
        d = pen.getCommands()
        if d:
            paths.append(f'<path transform="translate({xpos*s:.3f},0) scale({s:.5f},{-s:.5f})" d="{d}"/>')
        xpos += hmtx[g][0]
    return "".join(paths), xpos * s


FONT = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"


def svg_wordmark(pal, mono=False, bg=None):
    """Mark + 'DualMesh' to the right. 'Dual' in network-A colour edge? Keep neutral; 'Mesh' neutral."""
    p = PALETTES[pal]
    x, y, w, h = VB
    defs, body = mark(pal, mono, False, mid="mw")
    size = 44
    tp, tw = text_path("DualMesh", FONT, size)
    tx = x + w + 6
    total_w = (tx - x) + tw + 8
    # cap height of Inter ~0.727em -> centre caps on mark centre (y=65)
    base = 65 + size * 0.727 / 2
    bgr = f'<rect x="{x}" y="{y}" width="{total_w}" height="{h}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x} {y} {total_w:.2f} {h}" '
            f'width="{total_w*4:.0f}" height="{h*4:g}"><title>DualMesh</title><defs>{defs}</defs>{bgr}{body}'
            f'<g fill="{p["n"]}" transform="translate({tx:.2f},{base:.2f})">{tp}</g></svg>')


if __name__ == "__main__":
    d = os.path.join(OUT, "svg")
    os.makedirs(d, exist_ok=True)
    files = {
        "dualmesh-logo-dark.svg": svg_mark("dark"),
        "dualmesh-logo-light.svg": svg_mark("light"),
        "dualmesh-logo-mono.svg": svg_mark("mono", mono=True),
        "dualmesh-logo-small-dark.svg": svg_mark("dark", simple=True),
        "dualmesh-logo-small-mono.svg": svg_mark("mono", mono=True, simple=True),
        "dualmesh-wordmark-dark.svg": svg_wordmark("dark"),
        "dualmesh-wordmark-light.svg": svg_wordmark("light"),
        "dualmesh-wordmark-mono.svg": svg_wordmark("mono", mono=True),
    }
    for n, s in files.items():
        open(os.path.join(d, n), "w").write(s)
    print("\n".join(files))
