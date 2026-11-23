# SPDX-License-Identifier: MIT
"""DualMesh - lav en offline-kortpakke (rasterfliser) til T-Deck'en.

Henter OpenStreetMap-vektorfliser for et område fra en Protomaps-build (kun de byte, der skal
bruges, via HTTP range) og tegner dem selv som 256 px PNG-fliser i DualMesh' egen enkle stil.
Resultatet er ÉN PMTiles-fil (v3), som lægges i /dualmesh/maps/ på SD-kortet (fx med
"SD som USB-drev").

Licens: kortdata (c) OpenStreetMap-bidragydere, ODbL. Fliserne er et "produced work", som må
deles, når der står "Kortdata fra OpenStreetMap" ved kortet (T-Deck'en viser det). Vi henter
aldrig fra OpenStreetMaps egne flise-servere.

Brug (mapvenv: pmtiles, mapbox-vector-tile, Pillow):
  python make_map.py --bbox 10.0,57.1,10.3,57.3 --zoom 8-14 --name Testomraade
  --bbox er min_lon,min_lat,max_lon,max_lat. --out kan angive filen (standard <name>.pmtiles).
"""
import argparse
import gzip
import io
import json
import math
import os
import sys
import time
import urllib.request

import mapbox_vector_tile as mvt
from PIL import Image, ImageDraw, ImageFont
from pmtiles.reader import Reader
from pmtiles.tile import Compression, TileType, zxy_to_tileid
from pmtiles.writer import Writer

UA = {"User-Agent": "DualMesh-kortscript (github.com/geekgarage/dualmesh)"}
BUILDS = "https://build-metadata.protomaps.dev/builds.json"
SRC_MAX_Z = 15          # Protomaps-builds har vektorfliser til og med z15 (over det: forstørres)
TILE = 256
SS = 2                  # tegnes i dobbelt størrelse og skaleres ned (glatte kanter)
META = 4                # metaflise: 4 x 4 fliser tegnes samlet (navne skæres ikke ved flisekanten)
COLORS = 32             # farver pr. PNG-flise (palet: små filer, hurtig afkodning på T-Deck'en)
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "..", "..", "fonts", "montserrat", "Montserrat-SemiBold.ttf")

# ---------------------------------------------------------------- stil (vores egen, lys)
BG_WATER = (170, 211, 223)
EARTH = (242, 239, 233)
LANDUSE = {
    "park": (200, 250, 204), "garden": (205, 235, 176), "grass": (205, 235, 176),
    "wood": (173, 209, 158), "forest": (173, 209, 158), "nature_reserve": (200, 240, 200),
    "farmland": (238, 240, 213), "meadow": (205, 235, 176), "scrub": (200, 215, 171),
    "residential": (226, 224, 222), "industrial": (235, 219, 232), "commercial": (242, 218, 217),
    "retail": (255, 214, 209), "cemetery": (170, 203, 175), "pitch": (170, 224, 203),
    "beach": (255, 241, 186), "sand": (245, 233, 198), "wetland": (210, 230, 215),
    "glacier": (240, 248, 255), "military": (243, 227, 221), "university": (255, 255, 229),
    "school": (255, 255, 229), "hospital": (255, 255, 229), "aerodrome": (233, 231, 226),
    "railway": (235, 219, 232), "pedestrian": (237, 237, 237), "playground": (223, 252, 226),
}
WATER = (170, 211, 223)
BUILDING = (217, 208, 201)
BUILDING_EDGE = (196, 182, 171)
# vej: (farve, kant, bredde ved z14 i px, mindste zoom)
ROADS = {
    "highway": ((232, 146, 162), (196, 106, 125), 5.0, 5),
    "major_road": ((252, 214, 164), (200, 155, 100), 4.0, 8),
    "minor_road": ((255, 255, 255), (190, 190, 190), 2.6, 12),
    "other": ((255, 255, 255), (200, 200, 200), 1.6, 14),
    "path": ((250, 128, 114), None, 1.0, 14),
}
RAIL = ((150, 150, 150), 1.2, 10)
BOUNDARY = ((172, 70, 172), 1.2)
LABEL = (40, 40, 40)
HALO = (255, 255, 255)


def log(*a):
    print(*a, flush=True)


# ---------------------------------------------------------------- kilde (HTTP range + cache)
def http_range(url, offset, length):
    for attempt in range(6):
        try:
            req = urllib.request.Request(url, headers={**UA, "Range": f"bytes={offset}-{offset + length - 1}"})
            data = urllib.request.urlopen(req, timeout=120).read()
            if len(data) == length:
                return data
            raise IOError(f"fik {len(data)} af {length} byte")
        except Exception as e:  # netværksfejl: prøv igen lidt senere
            if attempt == 5:
                raise
            log(f"  (forsøger igen: {e})")
            time.sleep(2 + attempt * 3)


class LocalTiles:
    """Lokal kopi af kildefliserne (lavet af prefetch): data.bin + index.pkl {tile_id: (pos, len)}."""

    def __init__(self, d):
        import mmap
        import pickle
        with open(os.path.join(d, "index.pkl"), "rb") as f:
            self.idx = pickle.load(f)
        self.f = open(os.path.join(d, "data.bin"), "rb")
        self.mm = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)

    def raw(self, z, x, y):
        e = self.idx.get(zxy_to_tileid(z, x, y))
        return self.mm[e[0]:e[0] + e[1]] if e else None


class Source:
    def __init__(self, url, local=None):
        self.url = url
        self.cache = {}
        self.calls = 0
        self.bytes = 0
        self.reader = Reader(self.get_bytes)
        self.local = LocalTiles(local) if local else None
        self.tiles = {}

    def get_bytes(self, offset, length):
        key = (offset, length)
        if key in self.cache:
            return self.cache[key]
        data = http_range(self.url, offset, length)
        self.calls += 1
        self.bytes += len(data)
        if length <= 65536:  # mapper (små) gemmes; fliser hentes én gang alligevel
            self.cache[key] = data
        return data

    def tile(self, z, x, y):
        key = (z, x, y)
        if key not in self.tiles:
            raw = self.local.raw(z, x, y) if self.local else self.reader.get(z, x, y)
            if raw:
                if raw[:2] == b"\x1f\x8b":
                    raw = gzip.decompress(raw)
                self.tiles[key] = mvt.decode(raw, default_options={"y_coord_down": True})
            else:
                self.tiles[key] = None
            if len(self.tiles) > 400:  # begræns hukommelsen
                self.tiles.pop(next(iter(self.tiles)))
        return self.tiles[key]


def source_ranges(bbox, z0, z1):
    """Kildefliser (sz, x0, y0, x1, y1) som tegningen af zoom z0-z1 bruger (metafliser + ramme)."""
    out = []
    for z in range(z0, z1 + 1):
        x0, y0, x1, y1 = tile_range(bbox, z)
        sz = min(z, SRC_MAX_Z)
        f = 2 ** (z - sz)
        n = 2 ** sz
        out.append((sz, max(0, (x0 - 1) // f), max(0, (y0 - 1) // f),
                    min(n - 1, (x1 + META) // f), min(n - 1, (y1 + META) // f)))
    return out


def prefetch(url, bbox, z0, z1, d, threads=16):
    """Henter kildefliserne for området til en lokal mappe, i store sammenlagte HTTP-range-
    bidder (fliserne ligger i Hilbert-orden, så et område ligger tæt i filen). Findes mappen
    allerede (fx fra en kørsel til zoom 12), hentes kun de fliser, der mangler, og de lægges til.
    Fliser, kilden ikke har (hav uden data), huskes i absent.pkl, så de ikke slås op igen.
    (Fejl rettet 23. nov 2026: før blev en eksisterende cache brugt, som den var, og zoom 13-15
    blev tegnet tomme, når cachen kun dækkede zoom 0-12.)"""
    import pickle
    import threading
    from concurrent.futures import ThreadPoolExecutor
    from pmtiles.tile import deserialize_directory, deserialize_header, find_tile

    os.makedirs(d, exist_ok=True)
    idx_path, absent_path = os.path.join(d, "index.pkl"), os.path.join(d, "absent.pkl")
    old_idx, absent = {}, set()
    if os.path.exists(idx_path):
        with open(idx_path, "rb") as f:
            old_idx = pickle.load(f)
    if os.path.exists(absent_path):
        with open(absent_path, "rb") as f:
            absent = pickle.load(f)
    t0 = time.time()
    ids = set()
    for sz, x0, y0, x1, y1 in source_ranges(bbox, z0, z1):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                ids.add(zxy_to_tileid(sz, x, y))
    ids = sorted(ids - old_idx.keys() - absent)
    if not ids:
        log(f"Kildedata findes allerede lokalt: {d}")
        return
    log(f"Kildedata: {len(ids)} vektorfliser skal slås op" + (f" (cachen har {len(old_idx)})" if old_idx else ""))
    h = deserialize_header(http_range(url, 0, 127))
    root = deserialize_directory(http_range(url, h["root_offset"], h["root_length"]))

    leaves, lock = {}, threading.Lock()

    def lookup(tid):
        e = find_tile(root, tid)
        while e and e.run_length == 0:
            key = (e.offset, e.length)
            dd = leaves.get(key)
            if dd is None:
                dd = deserialize_directory(http_range(url, h["leaf_directory_offset"] + e.offset, e.length))
                with lock:
                    leaves[key] = dd
            e = find_tile(dd, tid)
        return e

    def lookup_chunk(chunk):
        return [(tid, lookup(tid)) for tid in chunk]

    found = []
    step = max(1, len(ids) // (threads * 8))
    chunks = [ids[i:i + step] for i in range(0, len(ids), step)]
    with ThreadPoolExecutor(threads) as ex:
        for res in ex.map(lookup_chunk, chunks):
            found.extend((tid, e.offset, e.length) for tid, e in res if e)
    log(f"  {len(found)} fliser fundet ({len(leaves)} undermapper), {time.time() - t0:.0f} s")

    # Sammenlæg til store bidder (mellemrum < 256 KB, højst 16 MB pr. bid).
    uniq = sorted({(o, l) for _, o, l in found})
    groups, cur = [], []
    for o, l in uniq:
        if cur and (o - (cur[-1][0] + cur[-1][1]) > 262144 or o + l - cur[0][0] > 16 << 20):
            groups.append(cur)
            cur = []
        cur.append((o, l))
    if cur:
        groups.append(cur)
    total = sum(g[-1][0] + g[-1][1] - g[0][0] for g in groups)
    log(f"  henter {total / 1e6:.0f} MB i {len(groups)} bidder")

    pos_of = {}
    data_path = os.path.join(d, "data.bin")
    data = open(data_path, "ab")  # nye fliser lægges efter de gamle
    wpos = [os.path.getsize(data_path)]
    done = [0, 0]

    def fetch(g):
        start = g[0][0]
        blob = http_range(url, h["tile_data_offset"] + start, g[-1][0] + g[-1][1] - start)
        with lock:
            for o, l in g:
                data.write(blob[o - start:o - start + l])
                pos_of[(o, l)] = (wpos[0], l)
                wpos[0] += l
            done[0] += 1
            done[1] += len(blob)
            if done[0] % max(1, len(groups) // 20) == 0 or done[0] == len(groups):
                log(f"  {done[0]}/{len(groups)} bidder, {done[1] / 1e6:.0f} MB, {time.time() - t0:.0f} s")

    with ThreadPoolExecutor(threads) as ex:
        list(ex.map(fetch, groups))
    data.close()
    idx = dict(old_idx)
    idx.update({tid: pos_of[(o, l)] for tid, o, l in found})
    absent |= set(ids) - {tid for tid, _, _ in found}
    for path, obj in ((idx_path, idx), (absent_path, absent)):  # atomisk: tmp + omdøb
        with open(path + ".tmp", "wb") as f:
            pickle.dump(obj, f)
        os.replace(path + ".tmp", path)
    log(f"Kildedata klar: {wpos[0] / 1e6:.0f} MB lokalt ({len(found)} nye fliser), {time.time() - t0:.0f} s")


def latest_build():
    req = urllib.request.Request(BUILDS, headers=UA)
    builds = json.load(urllib.request.urlopen(req, timeout=30))
    key = builds[-1]["key"]
    return f"https://build.protomaps.com/{key}", key


# ---------------------------------------------------------------- geometri
def lonlat_to_tile(lon, lat, z):
    n = 2 ** z
    x = (lon + 180.0) / 360.0 * n
    lat = max(min(lat, 85.0511), -85.0511)
    y = (1 - math.log(math.tan(math.radians(lat)) + 1 / math.cos(math.radians(lat))) / math.pi) / 2 * n
    return x, y


def tile_range(bbox, z):
    x0, y1 = lonlat_to_tile(bbox[0], bbox[1], z)
    x1, y0 = lonlat_to_tile(bbox[2], bbox[3], z)
    return int(x0), int(y0), int(x1), int(y1)


def width_at(w14, z):
    return max(0.6, w14 * (1.6 ** (z - 14)) if z < 14 else w14 * (1.4 ** (z - 14)))


class Canvas:
    """En metaflise: (META+2)^2 fliser med en ramme på én flise, så kanterne bliver ens."""

    def __init__(self, z, mx, my):
        self.z = z
        self.ox, self.oy = mx - 1, my - 1  # øverste venstre flise i lærredet
        self.n = META + 2
        size = self.n * TILE * SS
        self.img = Image.new("RGB", (size, size), BG_WATER)
        self.d = ImageDraw.Draw(self.img)
        self.labels = []

    def proj(self, sz, sx, sy, extent):
        f = 2 ** (self.z - sz)
        s = TILE * f * SS / extent
        bx = (sx * TILE * f - self.ox * TILE) * SS
        by = (sy * TILE * f - self.oy * TILE) * SS
        return lambda p: (bx + p[0] * s, by + p[1] * s)


def rings(geom):
    t = geom["type"]
    c = geom["coordinates"]
    if t == "Polygon":
        return [c]
    if t == "MultiPolygon":
        return c
    return []


def lines(geom):
    t = geom["type"]
    c = geom["coordinates"]
    if t == "LineString":
        return [c]
    if t == "MultiLineString":
        return c
    if t in ("Polygon", "MultiPolygon"):
        return [r for poly in rings(geom) for r in poly]
    return []


def points(geom):
    t = geom["type"]
    c = geom["coordinates"]
    if t == "Point":
        return [c]
    if t == "MultiPoint":
        return c
    return []


def fill_polys(cv, pr, geom, color, outline=None):
    for poly in rings(geom):
        if not poly or len(poly[0]) < 3:
            continue
        outer = [pr(p) for p in poly[0]]
        cv.d.polygon(outer, fill=color, outline=outline)
        for hole in poly[1:]:  # huller (øer i søer osv.) males med baggrunden under
            if len(hole) >= 3:
                cv.d.polygon([pr(p) for p in hole], fill=EARTH)


def stroke(cv, pr, geom, color, width):
    w = max(1, int(round(width * SS)))
    for ln in lines(geom):
        if len(ln) >= 2:
            cv.d.line([pr(p) for p in ln], fill=color, width=w, joint="curve")


def render_meta(src, z, mx, my, fonts):
    cv = Canvas(z, mx, my)
    sz = min(z, SRC_MAX_Z)
    f = 2 ** (z - sz)
    # Kildefliser, der dækker lærredet.
    sx0, sy0 = cv.ox // f, cv.oy // f
    sx1, sy1 = (cv.ox + cv.n - 1) // f, (cv.oy + cv.n - 1) // f
    n = 2 ** sz
    layers = []
    for sx in range(sx0, sx1 + 1):
        for sy in range(sy0, sy1 + 1):
            if not (0 <= sx < n and 0 <= sy < n):
                continue
            t = src.tile(sz, sx, sy)
            if t:
                layers.append((sx, sy, t))

    # Rent hav (intet land, ingen veje/navne/grænser): samme vandflise overalt, intet at tegne.
    land = ("earth", "landuse", "roads", "buildings", "places", "boundaries")
    if not any(t.get(k, {}).get("features") for _, _, t in layers for k in land):
        sea = fonts.get("_sea")
        if sea is None:
            buf = io.BytesIO()
            Image.new("RGB", (TILE, TILE), BG_WATER).quantize(colors=COLORS).save(buf, format="PNG", optimize=True)
            sea = fonts["_sea"] = buf.getvalue()
        return {(cv.ox + i, cv.oy + j): sea for i in range(1, cv.n - 1) for j in range(1, cv.n - 1)}

    def each(layer_name):
        for sx, sy, t in layers:
            L = t.get(layer_name)
            if not L:
                continue
            pr = cv.proj(sz, sx, sy, L.get("extent", 4096))
            for feat in L["features"]:
                yield pr, feat["properties"], feat["geometry"]

    for pr, p, g in each("earth"):
        fill_polys(cv, pr, g, EARTH)
    # Kun "landuse" (OSM, ODbL). Protomaps' "landcover" (z0-7) er ESA WorldCover under CC-BY 4.0
    # (protomaps/basemaps LICENSE_DATA.md) og bruges ikke, så pakken kun er OSM + Natural Earth.
    for name in ("landuse",):
        for pr, p, g in each(name):
            col = LANDUSE.get(p.get("kind"))
            if col:
                fill_polys(cv, pr, g, col)
    for pr, p, g in each("water"):
        if g["type"] in ("Polygon", "MultiPolygon"):
            fill_polys(cv, pr, g, WATER)
        elif z >= 12:
            stroke(cv, pr, g, WATER, 1.5 if p.get("kind") == "river" else 0.8)
    if z >= 14:
        for pr, p, g in each("buildings"):
            fill_polys(cv, pr, g, BUILDING, BUILDING_EDGE if z >= 15 else None)
    for pr, p, g in each("boundaries"):
        stroke(cv, pr, g, BOUNDARY[0], BOUNDARY[1])
    # Veje: først kanter, så fyld, mindre veje først.
    road_feats = [(pr, p, g) for pr, p, g in each("roads")]
    order = ["path", "other", "minor_road", "major_road", "highway"]
    for pr, p, g in road_feats:
        k = p.get("kind")
        if k == "rail" and z >= RAIL[2]:
            stroke(cv, pr, g, RAIL[0], RAIL[1])
    for casing in (True, False):
        for kind in order:
            col, edge, w14, minz = ROADS[kind]
            if z < minz:
                continue
            w = width_at(w14, z)
            for pr, p, g in road_feats:
                if p.get("kind") != kind:
                    continue
                if casing:
                    if edge:
                        stroke(cv, pr, g, edge, w + 1.2)
                else:
                    stroke(cv, pr, g, col, w)
    # Stednavne (byer, landsbyer): størst først, ingen overlap.
    places = []
    for pr, p, g in each("places"):
        name = p.get("name:da") or p.get("name")
        if not name:
            continue
        kind = p.get("kind")
        if kind not in ("locality", "region", "country"):
            continue
        rank = p.get("population_rank", 0) or 0
        for pt in points(g):
            places.append((-(rank), kind, name, pr(pt)))
    places.sort(key=lambda t: t[0])
    placed = []
    for _, kind, name, (x, y) in places:
        size = 15 if kind == "country" else (13 if kind == "region" else 12)
        font = fonts.setdefault(size, ImageFont.truetype(FONT, size * SS))
        box = cv.d.textbbox((x, y), name, font=font, anchor="mm", stroke_width=2 * SS)
        if any(not (box[2] < b[0] or box[0] > b[2] or box[3] < b[1] or box[1] > b[3]) for b in placed):
            continue
        placed.append(box)
        cv.d.text((x, y), name, font=font, fill=LABEL, anchor="mm", stroke_width=2 * SS, stroke_fill=HALO)

    small = cv.img.resize((cv.n * TILE, cv.n * TILE), Image.LANCZOS)
    out = {}
    for i in range(1, cv.n - 1):
        for j in range(1, cv.n - 1):
            tx, ty = cv.ox + i, cv.oy + j
            tile = small.crop((i * TILE, j * TILE, (i + 1) * TILE, (j + 1) * TILE))
            q = tile.quantize(colors=COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            buf = io.BytesIO()
            q.save(buf, format="PNG", optimize=True)
            out[(tx, ty)] = buf.getvalue()
    return out


# Parallel tegning: hver proces har sin egen kilde (HTTP-cache) og sine fonte.
_W = {}


def _init_worker(url, local):
    _W["src"] = Source(url, local)
    _W["fonts"] = {}


def _work(job):
    z, mx, my = job
    return z, render_meta(_W["src"], z, mx, my, _W["fonts"]), _W["src"].calls, _W["src"].bytes


def main():
    ap = argparse.ArgumentParser(description="Lav en DualMesh-kortpakke (PMTiles med PNG-fliser).")
    ap.add_argument("--bbox", required=True, help="min_lon,min_lat,max_lon,max_lat")
    ap.add_argument("--zoom", default="8-14", help="fx 8-14 (højst 16)")
    ap.add_argument("--name", required=True, help="områdets navn (vises på T-Deck'en)")
    ap.add_argument("--out", help="udfil (standard <name>.pmtiles)")
    ap.add_argument("--source", help="PMTiles-URL (standard: nyeste Protomaps-build)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count(), help="processer (standard: alle kerner)")
    ap.add_argument("--cache", help="mappe til en lokal kopi af kildedataene (hentes først i store bidder; "
                    "meget hurtigere ved store områder, og kan genbruges)")
    a = ap.parse_args()
    bbox = [float(v) for v in a.bbox.split(",")]
    z0, z1 = (int(v) for v in a.zoom.split("-"))
    if not (0 <= z0 <= z1 <= 16):
        sys.exit("zoom skal være mellem 0 og 16")
    url, key = (a.source, os.path.basename(a.source)) if a.source else latest_build()
    out = a.out or f"{a.name}.pmtiles"

    total = 0
    for z in range(z0, z1 + 1):
        x0, y0, x1, y1 = tile_range(bbox, z)
        total += (x1 - x0 + 1) * (y1 - y0 + 1)
    log(f"Kilde: {url}\nOmråde: {a.name}, zoom {z0}-{z1}, {total} fliser, {a.jobs} processer")
    if a.cache:
        prefetch(url, bbox, z0, z1, a.cache)

    # Alle metafliser tegnes parallelt på PC'ens kerner (--jobs, standard alle).
    jobs, ranges = [], {}
    for z in range(z0, z1 + 1):
        x0, y0, x1, y1 = tile_range(bbox, z)
        ranges[z] = (x0, y0, x1, y1)
        for mx in range(x0, x1 + 1, META):
            for my in range(y0, y1 + 1, META):
                jobs.append((z, mx, my))
    from concurrent.futures import ProcessPoolExecutor, as_completed
    tiles = {}
    t0 = time.time()
    done = 0
    with ProcessPoolExecutor(max_workers=a.jobs, initializer=_init_worker, initargs=(url, a.cache)) as ex:
        futs = [ex.submit(_work, j) for j in jobs]
        for fut in as_completed(futs):
            z, result, _, _ = fut.result()
            x0, y0, x1, y1 = ranges[z]
            for (tx, ty), png in result.items():
                if x0 <= tx <= x1 and y0 <= ty <= y1:
                    tiles[zxy_to_tileid(z, tx, ty)] = png
            done += 1
            if done % max(1, len(jobs) // 20) == 0 or done == len(jobs):
                log(f"  {done}/{len(jobs)} metafliser, {len(tiles)} fliser, {time.time() - t0:.0f} s")

    with open(out, "wb") as f:
        w = Writer(f)
        for tid in sorted(tiles):
            w.write_tile(tid, tiles[tid])
        cx, cy = (bbox[0] + bbox[2]) / 2, (bbox[1] + bbox[3]) / 2
        w.finalize(
            {
                "tile_type": TileType.PNG,
                "tile_compression": Compression.NONE,
                "min_lon_e7": int(bbox[0] * 1e7), "min_lat_e7": int(bbox[1] * 1e7),
                "max_lon_e7": int(bbox[2] * 1e7), "max_lat_e7": int(bbox[3] * 1e7),
                "center_zoom": min(max(z0, 12), z1),
                "center_lon_e7": int(cx * 1e7), "center_lat_e7": int(cy * 1e7),
            },
            {
                "name": a.name,
                "attribution": "© OpenStreetMap-bidragydere (ODbL, openstreetmap.org/copyright); "
                               "Natural Earth (public domain)",
                "description": f"DualMesh-kortpakke, zoom {z0}-{z1}, tegnet ud fra Protomaps-build {key}",
                "format": "png",
                "generator": "DualMesh tools/maps/make_map.py",
            },
        )
    size = os.path.getsize(out)
    log(f"Færdig: {out} ({size / 1e6:.1f} MB, {len(tiles)} fliser, snit {size / max(1, len(tiles)) / 1024:.1f} KB)")


if __name__ == "__main__":
    main()
