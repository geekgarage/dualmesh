# SPDX-License-Identifier: MIT
# Laver mange kortpakker uden opsyn: et stort område tegnes først til zoom 12, deles med
# plan_regions.py, og hver del tegnes til zoom 0-15. Bliver en pakke alligevel over grænsen, deles
# den i to (langs den længste akse) og tegnes igen. Alt kører lokalt; status i <ud>/build.log.
#
#   python tools/maps/build_regions.py --bbox=4,54.9,31.7,71.3 --prefix Norden --out C:\Claude\tools\maps\norden
#     --cache C:\Claude\tools\maps\src_norden [--max-mb 1850]
# Hver pakke hedder <prefix>-NN.pmtiles; summary.txt får én linje pr. pakke (fil, byte, bbox).
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable


def log(out, *a):
    line = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    print(line, flush=True)
    with open(os.path.join(out, "build.log"), "a", encoding="utf-8") as f:
        f.write(line + "\n")


def make(bbox, zoom, name, path, cache, out):
    cmd = [PY, os.path.join(HERE, "make_map.py"), "--bbox=" + ",".join(f"{v:.4f}" for v in bbox),
           "--zoom", zoom, "--name", name, "--out", path, "--cache", cache]
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0 or not os.path.exists(path):
        log(out, "FEJL", name, r.stderr[-500:])
        return 0
    size = os.path.getsize(path)
    log(out, f"{name}: {size / 1e6:.0f} MB på {time.time() - t0:.0f} s")
    return size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bbox", required=True)
    ap.add_argument("--prefix", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cache", required=True)
    ap.add_argument("--max-mb", type=float, default=1850)
    ap.add_argument("--factor", type=float, default=30.0)
    ap.add_argument("--max-tiles", type=float, default=8e6, help="højst så mange fliser pr. pakke (RAM)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    bbox = [float(v) for v in a.bbox.split(",")]

    # 1. Hele området til zoom 12 og opdeling.
    z12 = os.path.join(a.out, a.prefix + "-z12.pmtiles")
    if not os.path.exists(z12):
        log(a.out, "zoom 12 af hele området ...")
        make(bbox, "0-12", a.prefix + " z12", z12, a.cache, a.out)
    r = subprocess.run([PY, os.path.join(HERE, "plan_regions.py"), z12, "--max-mb", str(a.max_mb),
                        "--factor", str(a.factor), "--max-tiles", str(a.max_tiles)],
                       capture_output=True, text=True, encoding="utf-8")
    log(a.out, "plan:\n" + r.stdout)
    queue = []
    for line in r.stdout.splitlines():
        if line.startswith("--bbox="):
            queue.append([float(v) for v in line.split()[0][7:].split(",")])

    # 2. Hver del til zoom 0-15; for stor = del i to og prøv igen.
    n = 0
    done = []
    while queue:
        b = queue.pop(0)
        n += 1
        name = f"{a.prefix}-{n:02d}"
        path = os.path.join(a.out, name + ".pmtiles")
        size = make(b, "0-15", name, path, a.cache, a.out)
        if size > a.max_mb * 1e6:
            os.remove(path)
            w, h = b[2] - b[0], b[3] - b[1]
            if w * 0.5 >= h:  # længdegrader er kortere mod nord; groft men nok
                m = (b[0] + b[2]) / 2
                queue[:0] = [[b[0], b[1], m, b[3]], [m, b[1], b[2], b[3]]]
            else:
                m = (b[1] + b[3]) / 2
                queue[:0] = [[b[0], b[1], b[2], m], [b[0], m, b[2], b[3]]]
            log(a.out, f"{name} var for stor - deles i to")
            n -= 1
            continue
        if size:
            done.append((name + ".pmtiles", size, b))
    with open(os.path.join(a.out, "summary.txt"), "a", encoding="utf-8") as f:  # flere kørsler i samme mappe
        for fn, size, b in done:
            f.write(f"{fn} {size} {b[0]:.4f},{b[1]:.4f},{b[2]:.4f},{b[3]:.4f}\n")
    log(a.out, f"FÆRDIG: {len(done)} pakker, {sum(s for _, s, _ in done) / 1e9:.1f} GB")


if __name__ == "__main__":
    main()
