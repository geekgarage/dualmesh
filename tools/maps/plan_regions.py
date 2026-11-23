# SPDX-License-Identifier: MIT
# Deler et stort område i kortpakker under en størrelsesgrænse (GitHub-releases: højst 2 GiB pr.
# fil; T-Deck'ens fseek er 32-bit, så hold pakker under ~1,9 GB).
#
# Fremgangsmåde: tegn først området til kun zoom 12 med make_map.py (hurtigt, < 2 % af arbejdet):
#   python tools/maps/make_map.py --bbox=4,54.9,31.7,71.3 --zoom 0-12 --name Norden-z12 --cache cache_nordic
# Så måler dette script bytes pr. zoom 12-flise (havfliser er ens og tæller ikke) og deler området
# (k-d: længste akse, ved vægtet median), indtil hver del ganget med faktoren (zoom 0-15 ≈ 30 x
# zoom 12, målt på Danmark 22. nov 2026) er under grænsen:
#   python tools/maps/plan_regions.py Norden-z12.pmtiles --max-mb 1850 --factor 30
# Udskriver én linje pr. del: bbox, anslået størrelse og antal landfliser.
import argparse
import math
from collections import Counter

from pmtiles.reader import MmapSource, Reader, all_tiles


def tile_lon(x, z):
    return x / 2 ** z * 360.0 - 180.0


def tile_lat(y, z):
    n = math.pi - 2.0 * math.pi * y / 2 ** z
    return math.degrees(math.atan(math.sinh(n)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pmtiles")
    ap.add_argument("--zoom", type=int, default=12)
    ap.add_argument("--max-mb", type=float, default=1850)
    ap.add_argument("--factor", type=float, default=30.0, help="størrelse zoom 0-15 / størrelse zoom 12")
    ap.add_argument("--max-tiles", type=float, default=8e6,
                    help="højst så mange fliser (zoom 0-15) pr. del: make_map.py holder dem i RAM "
                         "(vigtigt mod polerne, hvor isen fylder lidt men giver mange fliser)")
    a = ap.parse_args()

    cells = {}
    with open(a.pmtiles, "r+b") as f:
        r = Reader(MmapSource(f))
        datas = {}
        for (z, x, y), data in all_tiles(r.get_bytes):
            if z == a.zoom:
                datas[(x, y)] = data
    common = Counter(datas.values()).most_common(1)[0][0] if datas else b""
    for k, d in datas.items():
        if d != common:  # havfliser (den hyppigste, ens flise) koster intet i den færdige pakke
            cells[k] = len(d)
    limit = a.max_mb * 1e6 / a.factor  # bytes ved zoom 12 pr. del

    def split(box):
        x0, y0, x1, y1 = box
        inside = [(k, v) for k, v in cells.items() if x0 <= k[0] <= x1 and y0 <= k[1] <= y1]
        total = sum(v for _, v in inside)
        # Fliser i alt ved zoom 0-15 ≈ zoom 12-fliserne i rektanglet x 85 (1 + 4 + 16 + 64).
        ntiles = (x1 - x0 + 1) * (y1 - y0 + 1) * 85
        if (total <= limit and ntiles <= a.max_tiles) or (x1 - x0 < 2 and y1 - y0 < 2):
            return [(box, total, len(inside))]
        axis = 0 if (x1 - x0) * math.cos(math.radians(tile_lat((y0 + y1) / 2, a.zoom))) >= (y1 - y0) else 1
        acc, cut = 0, None
        if total <= limit:  # kun for mange fliser: del midt over
            cut = (x0 + x1) // 2 if axis == 0 else (y0 + y1) // 2
        for k, v in sorted(inside, key=lambda kv: kv[0][axis]):
            if cut is not None:
                break
            acc += v
            if acc >= total / 2:
                cut = k[axis]
                break
        lo, hi = (x0, x1) if axis == 0 else (y0, y1)
        if cut is None or cut < lo or cut >= hi:  # altid to ikke-tomme halvdele (ingen endeløs deling)
            cut = (lo + hi) // 2
        if axis == 0:
            return split((x0, y0, cut, y1)) + split((cut + 1, y0, x1, y1))
        return split((x0, y0, x1, cut)) + split((x0, cut + 1, x1, y1))

    xs = [k[0] for k in cells]
    ys = [k[1] for k in cells]
    parts = split((min(xs), min(ys), max(xs), max(ys)))
    print(f"landfliser ved zoom {a.zoom}: {len(cells)}, i alt anslået {sum(p[1] for p in parts) * a.factor / 1e9:.1f} GB "
          f"i {len(parts)} dele")
    for (x0, y0, x1, y1), total, n in sorted(parts, key=lambda p: (-p[0][1], p[0][0])):
        z = a.zoom
        bbox = (tile_lon(x0, z), tile_lat(y1 + 1, z), tile_lon(x1 + 1, z), tile_lat(y0, z))
        print(f"--bbox={bbox[0]:.3f},{bbox[1]:.3f},{bbox[2]:.3f},{bbox[3]:.3f}  "
              f"~{total * a.factor / 1e6:.0f} MB  ({n} landfliser)")


if __name__ == "__main__":
    main()
