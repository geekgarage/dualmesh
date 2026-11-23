# SPDX-License-Identifier: MIT
# Laver kortjobs for alle lande ud fra Natural Earths landegrænser (public domain,
# ne_50m_admin_0_countries.geojson fra github.com/nvkelso/natural-earth-vector).
#
#   python tools/maps/world_jobs.py <countries.geojson> <jobmappe> [--start 100]
#
# Hvert land deles i sammenhængende landområder (polygoner, hvis omsluttende rektangler ligger
# højst 1,5° fra hinanden, samles); hvert område bliver ét job til build_regions.py. Områder, der
# allerede er lavet (Danmark, Island, Færøerne, Grønland, Svalbard og Norge/Sverige/Finland),
# springes over. Jobbene nummereres efter afstanden fra København, så de nærmeste lande laves først.
# Desuden skrives countries.json (præfiks -> [[verdensdel, land]]), som bruges til katalogerne.
import argparse
import json
import math
import os
import re
import unicodedata

HOME = (55.68, 12.57)  # København
DONE_NAMES = {"Denmark", "Iceland", "Faroe Islands", "Greenland"}
DONE_BBOXES = [(4.0, 54.9, 31.7, 71.3), (10.0, 76.3, 34.0, 80.9), (-24.6, 63.2, -13.4, 66.6), (-7.7, 61.3, -6.2, 62.4)]
CONTINENT = {"Seven seas (open ocean)": "Remote islands"}
MARGIN = 1.5
PAD = 0.05


def slug(name):
    s = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode()
    s = re.sub(r"[^A-Za-z0-9]+", "-", s).strip("-")
    return s


def rings(geom):
    polys = geom["coordinates"] if geom["type"] == "MultiPolygon" else [geom["coordinates"]]
    for p in polys:
        xs = [c[0] for c in p[0]]
        ys = [c[1] for c in p[0]]
        yield [min(xs), min(ys), max(xs), max(ys)]


def near(a, b, m):
    return not (a[2] + m < b[0] or b[2] + m < a[0] or a[3] + m < b[1] or b[3] + m < a[1])


def clusters(boxes):
    groups = [list(b) for b in boxes]
    changed = True
    while changed:
        changed = False
        out = []
        for g in groups:
            for o in out:
                if near(g, o, MARGIN):
                    o[0], o[1], o[2], o[3] = min(o[0], g[0]), min(o[1], g[1]), max(o[2], g[2]), max(o[3], g[3])
                    changed = True
                    break
            else:
                out.append(g)
        groups = out
    return groups


def inside(b, d):
    return b[0] >= d[0] and b[1] >= d[1] and b[2] <= d[2] and b[3] <= d[3]


def dist_km(b):
    lat = min(max(HOME[0], b[1]), b[3])
    lon = min(max(HOME[1], b[0]), b[2])
    p1, p2 = math.radians(HOME[0]), math.radians(lat)
    dl = math.radians(lon - HOME[1])
    c = math.sin(p1) * math.sin(p2) + math.cos(p1) * math.cos(p2) * math.cos(dl)
    return 6371 * math.acos(max(-1.0, min(1.0, c)))


def area(b):
    return (b[2] - b[0]) * (b[3] - b[1]) * math.cos(math.radians((b[1] + b[3]) / 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("geojson")
    ap.add_argument("jobs")
    ap.add_argument("--start", type=int, default=100, help="første jobnummer (køen tager laveste først)")
    a = ap.parse_args()
    feats = json.load(open(a.geojson, encoding="utf-8"))["features"]
    jobs, cmap = [], {}
    for f in feats:
        p = f["properties"]
        name = p.get("NAME_EN") or p["ADMIN"]
        if p["ADMIN"] in DONE_NAMES or not f.get("geometry"):
            continue
        cont = CONTINENT.get(p["CONTINENT"], p["CONTINENT"])
        groups = sorted(clusters(list(rings(f["geometry"]))), key=area, reverse=True)
        k = 0
        for g in groups:
            g = [max(-180.0, g[0] - PAD), max(-85.0, g[1] - PAD), min(180.0, g[2] + PAD), min(85.0, g[3] + PAD)]
            if any(inside(g, d) for d in DONE_BBOXES):
                continue
            k += 1
            prefix = slug(name) if k == 1 else f"{slug(name)}-Part-{k}"
            job = {"type": "region", "prefix": prefix, "bbox": ",".join(f"{v:.3f}" for v in g),
                   "out": "/work/data/world", "cache": f"/work/data/src_{prefix}", "max_mb": 1850,
                   "max_tiles": 3e7, "cleanup": True}
            if cont == "Antarctica":  # is og hav: kun oversigt
                job.update(type="single", name=prefix, title=prefix, zoom="0-10")
            jobs.append((dist_km(g), prefix, job))
            cmap[prefix] = [[cont, name]]
    jobs.sort(key=lambda j: (j[0], j[1]))
    os.makedirs(a.jobs, exist_ok=True)
    for i, (d, prefix, job) in enumerate(jobs):
        with open(os.path.join(a.jobs, f"{a.start + i:04d}-{prefix}.job"), "w", encoding="utf-8", newline="\n") as fh:
            json.dump(job, fh)
    # Pakker lavet før dette script (filnavnene efter uploaderens engelske navne).
    cmap.update({"Denmark": [["Europe", "Denmark"]], "Iceland": [["Europe", "Iceland"]],
                 "Faroe-Islands": [["Europe", "Faroe Islands"]], "Svalbard": [["Europe", "Norway"]],
                 "Nordics": [["Europe", "Norway"], ["Europe", "Sweden"], ["Europe", "Finland"]],
                 "Greenland": [["North America", "Greenland"]]})
    with open(os.path.join(a.jobs, "countries.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(cmap, fh, ensure_ascii=False, indent=0)
    print(f"{len(jobs)} jobs; de første: " + ", ".join(p for _, p, _ in jobs[:12]))
    print("de sidste: " + ", ".join(p for _, p, _ in jobs[-5:]))


if __name__ == "__main__":
    main()
