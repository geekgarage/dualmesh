"""Outlined, mask-free vector versions of the DualMesh logo.

Every stroke is converted to a filled outline, overlaps are removed with boolean
path operations and cut-outs are real holes. Result: one plain <path> per colour,
no masks, no strokes, no fonts. Exports SVG and PDF (true vectors).
"""
import os, sys
import skia
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen

sys.path.insert(0, os.path.dirname(__file__))
from logo import PALETTES, VB, NODES_A, NODES_B, FONT

OUT = os.path.join(os.path.dirname(__file__), "..", "vector")


def stroke(build, width):
    p = skia.Path()
    build(p)
    paint = skia.Paint(Style=skia.Paint.kStroke_Style, StrokeWidth=width,
                       StrokeCap=skia.Paint.kRound_Cap, StrokeJoin=skia.Paint.kRound_Join)
    out = skia.Path()
    paint.getFillPath(p, out)
    return simplify(out)


def simplify(p):
    return skia.Op(p, p, skia.PathOp.kUnion_PathOp)


def op(a, b, kind):
    return skia.Op(a, b, kind)


def union(*ps):
    acc = skia.Path()
    for p in ps:
        acc = op(acc, p, skia.PathOp.kUnion_PathOp)
    return acc


def circle(x, y, r):
    p = skia.Path(); p.addCircle(x, y, r); return p


def rrect(x, y, w, h, r):
    p = skia.Path(); p.addRRect(skia.RRect.MakeRectXY(skia.Rect.MakeXYWH(x, y, w, h), r, r)); return p


def lines(segs):
    def b(p):
        for (x0, y0), (x1, y1) in segs:
            p.moveTo(x0, y0); p.lineTo(x1, y1)
    return b


def arc_top(p):
    p.moveTo(48, 30); p.lineTo(62, 30)
    p.arcTo(35, 35, 0, skia.Path.ArcSize.kSmall_ArcSize, skia.PathDirection.kCW, 97, 65)


def arc_bot(p):
    p.moveTo(97, 65)
    p.arcTo(35, 35, 0, skia.Path.ArcSize.kSmall_ArcSize, skia.PathDirection.kCW, 62, 100)
    p.lineTo(48, 100)


def layers(mono):
    """Ordered (colour-key, path) layers, painter's order, plus cut-out path."""
    ha, hb = (68, 48), (68, 82)
    la = stroke(lines([(ha, (62, 30)), (ha, (86.7, 40.3)), (ha, (97, 65)), (ha, (48, 65))]), 3)
    lb = stroke(lines([(hb, (62, 100)), (hb, (86.7, 89.7)), (hb, (97, 65)), (hb, (48, 65))]), 3)
    L = [("a", la), ("b", lb), ("a", stroke(arc_top, 6)), ("b", stroke(arc_bot, 6)),
         ("n", union(rrect(36, 27, 12, 62, 6), rrect(34, 86, 16, 17, 2)))]
    L.append(("a", union(*[circle(x, y, r) for x, y, r in NODES_A])))
    L.append(("b", union(*[circle(x, y, r) for x, y, r in NODES_B])))
    L.append(("n", circle(97, 65, 6)))
    cut = union(stroke(lines([((37, 76), (47, 76)), ((37, 81), (47, 81)), ((35, 92), (49, 92)), ((35, 97), (49, 97))]), 2),
                op(circle(42, 58, 5.75), circle(42, 58, 3.25), skia.PathOp.kDifference_PathOp))
    if mono:
        cut = union(cut, *[circle(x, y, r - 2.5) for x, y, r in NODES_B])
    return L, cut


def flatten(mono, extra=()):
    """Return {colour-key: path} with no overlaps between colours and holes cut."""
    L, cut = layers(mono)
    L = list(L) + list(extra)
    if mono:
        return {"n": op(union(*[p for _, p in L]), cut, skia.PathOp.kDifference_PathOp)}
    acc = {}
    for key, p in L:
        for k in acc:  # later layer covers earlier ones
            acc[k] = op(acc[k], p, skia.PathOp.kDifference_PathOp)
        acc[key] = op(acc.get(key, skia.Path()), p, skia.PathOp.kUnion_PathOp)
    return {k: op(v, cut, skia.PathOp.kDifference_PathOp) for k, v in acc.items()}


def to_d(path):
    it = skia.Path.Iter(path, False)
    d = []
    f = lambda v: f"{v:.3f}".rstrip("0").rstrip(".")
    while True:
        verb, pts = it.next()
        if verb == skia.Path.Verb.kDone_Verb:
            break
        if verb == skia.Path.Verb.kMove_Verb:
            d.append(f"M{f(pts[0].x())} {f(pts[0].y())}")
        elif verb == skia.Path.Verb.kLine_Verb:
            d.append(f"L{f(pts[1].x())} {f(pts[1].y())}")
        elif verb == skia.Path.Verb.kQuad_Verb:
            d.append(f"Q{f(pts[1].x())} {f(pts[1].y())} {f(pts[2].x())} {f(pts[2].y())}")
        elif verb == skia.Path.Verb.kConic_Verb:
            # convert conic to quads
            w = it.conicWeight()
            quads = skia.Path.ConvertConicToQuads(pts[0], pts[1], pts[2], w, 2)
            for i in range(1, len(quads) - 1, 2):
                d.append(f"Q{f(quads[i].x())} {f(quads[i].y())} {f(quads[i+1].x())} {f(quads[i+1].y())}")
        elif verb == skia.Path.Verb.kCubic_Verb:
            d.append(f"C{f(pts[1].x())} {f(pts[1].y())} {f(pts[2].x())} {f(pts[2].y())} {f(pts[3].x())} {f(pts[3].y())}")
        elif verb == skia.Path.Verb.kClose_Verb:
            d.append("Z")
    return "".join(d)


class SkiaPen(BasePen):
    def __init__(self, gs, path, s, dx, dy):
        super().__init__(gs); self.p, self.s, self.dx, self.dy = path, s, dx, dy
    def t(self, pt): return (self.dx + pt[0] * self.s, self.dy - pt[1] * self.s)
    def _moveTo(self, pt): self.p.moveTo(*self.t(pt))
    def _lineTo(self, pt): self.p.lineTo(*self.t(pt))
    def _qCurveToOne(self, a, b): self.p.quadTo(*self.t(a), *self.t(b))
    def _curveToOne(self, a, b, c): self.p.cubicTo(*self.t(a), *self.t(b), *self.t(c))
    def _closePath(self): self.p.close()


def text_outline(text, size, x, base):
    f = TTFont(FONT); gs = f.getGlyphSet(); cmap = f.getBestCmap(); hm = f["hmtx"]
    s = size / f["head"].unitsPerEm
    p = skia.Path(); xpos = x
    for ch in text:
        g = cmap[ord(ch)]
        gs[g].draw(SkiaPen(gs, p, s, xpos, base))
        xpos += hm[g][0] * s
    p.setFillType(skia.PathFillType.kWinding)
    return simplify(p), xpos


COLORS_HEX = {"a": "a", "b": "b", "n": "n"}


def write(name, paths, pal, box, bg=None):
    x, y, w, h = box
    pc = PALETTES[pal]
    ids = {"a": "meshtastic-net", "b": "meshcore-net", "n": "logo" if len(paths) == 1 else "antenne-og-tekst"}
    body = "".join(f'<path id="{ids[k]}" fill="{pc[k]}"{rule(p)} d="{to_d(p)}"/>' for k, p in paths.items())
    bgr = f'<rect x="{x}" y="{y}" width="{w:.2f}" height="{h}" fill="{bg}"/>' if bg else ""
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x} {y} {w:.2f} {h}" width="{w*8:.0f}" height="{h*8:.0f}">'
           f'<title>DualMesh</title>{bgr}{body}</svg>')
    open(os.path.join(OUT, name + ".svg"), "w").write(svg)
    # PDF (vector) at 1 unit = 1 pt * 4 for a sensible physical size
    sc = 4
    stream = skia.FILEWStream(os.path.join(OUT, name + ".pdf"))
    with skia.PDF.MakeDocument(stream) as doc:
        with doc.page(w * sc, h * sc) as c:
            c.scale(sc, sc); c.translate(-x, -y)
            if bg:
                c.drawRect(skia.Rect.MakeXYWH(x, y, w, h), skia.Paint(Color=skia.Color(*hexrgb(bg))))
            for k, p in paths.items():
                c.drawPath(p, skia.Paint(AntiAlias=True, Color=skia.Color(*hexrgb(pc[k]))))
    stream.flush()


def rule(p):
    ft = p.getFillType()
    return ' fill-rule="evenodd"' if ft in (skia.PathFillType.kEvenOdd, skia.PathFillType.kInverseEvenOdd) else ""


def hexrgb(h):
    h = h.lstrip("#"); return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for pal, mono in [("dark", False), ("light", False), ("mono", True)]:
        write(f"dualmesh-logo-{pal}-outlined", flatten(mono), pal, VB)
        x, y, w, h = VB
        size = 44
        tx = x + w + 6
        base = 65 + size * 0.727 / 2
        tp, xend = text_outline("DualMesh", size, tx, base)
        total = (xend - x) + 8
        write(f"dualmesh-wordmark-{pal}-outlined", flatten(mono, [("n", tp)]), pal, (x, y, total, h))
    # mono on black, ready for an OLED mockup / print
    write("dualmesh-logo-mono-on-black-outlined", flatten(True), "mono", VB, bg="#000000")
    print(sorted(os.listdir(OUT)))
