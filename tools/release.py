# SPDX-License-Identifier: MIT
# DualMesh – pakker en udgivelse (bruges af .github/workflows/release.yml, kan også køres lokalt).
#
#   python tools/release.py check v0.1.1     # tagget skal passe med DM_FW_VERSION i src/version.h
#   python tools/release.py package dist      # efter "pio run -e tdeck_plus": filer til releasen
#
# package laver i <ud>:
#   dualmesh-tdeck_plus.bin             app-billedet: OTA på T-Deck'en og launchere (fx på SD-kortet)
#   dualmesh-tdeck_plus-bootloader.bin  0x0      \
#   dualmesh-tdeck_plus-partitions.bin  0x8000    > første installation (flash-siden eller esptool)
#   dualmesh-tdeck_plus-boot_app0.bin   0xe000   /
#   version.txt                         version, rev, file, size, sha256 (læses af "Opdatér firmware")
#   manifest.json                       ESP Web Tools (flash-siden på GitHub Pages)
#   notes.md                            udgivelsesnoter
#   dualmesh-licenses.zip               LICENSE, THIRD_PARTY_NOTICES(.da).md og licensteksterne for alt,
#                                       der er bygget ind (skal følge den binære fil)
# Bemærk: der laves bevidst intet samlet billede fra 0x0: det ville overskrive NVS (0x9000) med 0xFF,
# og den interne NVS må aldrig ryddes automatisk.
import hashlib
import json
import os
import re
import shutil
import subprocess
import zipfile
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, ".pio", "build", "tdeck_plus")
NAME = "dualmesh-tdeck_plus"
REPO = "https://github.com/geekgarage/dualmesh"
PAGES = "https://geekgarage.github.io/dualmesh"


def fw_version():
    text = open(os.path.join(ROOT, "src", "version.h"), encoding="utf-8").read()
    m = re.search(r'#define DM_FW_VERSION "([^"]+)"', text)
    if not m:
        sys.exit("DM_FW_VERSION mangler i src/version.h")
    return m.group(1)


def git_rev():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT).decode().strip()
    except Exception:
        return "ukendt"


def boot_app0():
    core = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.join(os.path.expanduser("~"), ".platformio")
    p = os.path.join(core, "packages", "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
    if not os.path.exists(p):
        sys.exit(f"boot_app0.bin findes ikke: {p}")
    return p


def licenses(out):
    """dualmesh-licenses.zip: notitserne + licenstekster for alt, der er bygget ind i firmwaren."""
    libdeps = os.path.join(ROOT, ".pio", "libdeps", "tdeck_plus")
    files = [("LICENSE", "LICENSE"), ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
             ("THIRD_PARTY_NOTICES.da.md", "THIRD_PARTY_NOTICES.da.md"),
             ("lib/meshcore/license.txt", "licenses/MeshCore.txt"),
             ("lib/ed25519/license.txt", "licenses/ed25519.txt"),
             ("fonts/montserrat/OFL.txt", "licenses/Montserrat-OFL.txt"),
             ("fonts/jetbrainsmono/OFL.txt", "licenses/JetBrainsMono-OFL.txt"),
             ("fonts/notoemoji/OFL.txt", "licenses/NotoEmoji-OFL.txt"),
             ("fonts/notocoloremoji/OFL.txt", "licenses/NotoColorEmoji-OFL.txt"),
             (os.path.join(libdeps, "lvgl", "LICENCE.txt"), "licenses/LVGL.txt"),
             (os.path.join(libdeps, "base64", "LICENSE"), "licenses/base64.txt"),
             ("licenses/LGPL-2.1.txt", "licenses/Arduino-ESP32-LGPL-2.1.txt"),
             ("licenses/Apache-2.0.txt", "licenses/ESP-IDF-Apache-2.0.txt")]
    path = os.path.join(out, "dualmesh-licenses.zip")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, dst in files:
            p = src if os.path.isabs(src) else os.path.join(ROOT, src)
            if not os.path.exists(p):
                sys.exit(f"Mangler licensfil {p} (kør pio run -e tdeck_plus først)")
            z.write(p, dst)
        # rweather/Crypto har ingen licensfil; MIT-teksten står øverst i hver kildefil.
        crypto_h = os.path.join(libdeps, "Crypto", "Crypto.h")
        if not os.path.exists(crypto_h):
            sys.exit(f"Mangler {crypto_h}")
        head = open(crypto_h, encoding="utf-8").read().split("*/", 1)[0]
        text = "\n".join(l.strip().lstrip("/*").lstrip("*").strip() for l in head.splitlines()).strip()
        z.writestr("licenses/rweather-Crypto.txt", "rweather/Crypto (MIT), from Crypto.h:\n\n" + text + "\n")
        z.writestr("licenses/README.txt",
                   "License texts for the works built into the DualMesh firmware; see THIRD_PARTY_NOTICES.md.\n"
                   "Arduino-ESP32 core: https://github.com/espressif/arduino-esp32 (2.0.14)\n"
                   "ESP-IDF: https://github.com/espressif/esp-idf (v4.4.6); per-component licenses and\n"
                   "notices are in its source code.\n")
    print(f"licenser -> {path}")


def check(tag):
    ver = fw_version()
    if tag.lstrip("v") != ver:
        sys.exit(f"Tagget {tag} passer ikke med DM_FW_VERSION {ver} i src/version.h")
    print(f"OK: {tag} = {ver}")


def package(out):
    ver, rev = fw_version(), git_rev()
    os.makedirs(out, exist_ok=True)
    parts = [("bootloader.bin", "bootloader", 0x0), ("partitions.bin", "partitions", 0x8000),
             (None, "boot_app0", 0xE000), ("firmware.bin", None, 0x10000)]
    manifest_parts = []
    for src, suffix, off in parts:
        path = boot_app0() if src is None else os.path.join(BUILD, src)
        if not os.path.exists(path):
            sys.exit(f"Mangler {path} (kør pio run -e tdeck_plus først)")
        dst = f"{NAME}-{suffix}.bin" if suffix else f"{NAME}.bin"
        shutil.copyfile(path, os.path.join(out, dst))
        manifest_parts.append({"path": dst, "offset": off})
    app = os.path.join(out, f"{NAME}.bin")
    data = open(app, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    with open(os.path.join(out, "version.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"version {ver}\nrev {rev}\nfile {NAME}.bin\nsize {len(data)}\nsha256 {sha}\nboard tdeck_plus\n")
    manifest = {
        "name": "DualMesh",
        "version": ver,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [{"chipFamily": "ESP32-S3", "parts": manifest_parts}],
    }
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    with open(os.path.join(out, "notes.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"""DualMesh {ver} ({rev}) for the LilyGo T-Deck Plus – MeshCore and Meshtastic on one radio.

**Install**
- **In the browser (easiest):** open the [flash page]({PAGES}/flash/) in Chrome or Edge, connect the T-Deck by USB and press *Install*.
- **Update an existing DualMesh:** Settings > System > *Update firmware* (over WiFi).
- **With a launcher:** put `{NAME}.bin` on the SD card and install it from the launcher.
- **With esptool:**
  ```
  esptool.py --chip esp32s3 write_flash 0x0 {NAME}-bootloader.bin 0x8000 {NAME}-partitions.bin 0xe000 {NAME}-boot_app0.bin 0x10000 {NAME}.bin
  ```

`{NAME}.bin`: {len(data)} bytes, SHA-256 `{sha}`.

**Dansk:** installér i browseren via [flash-siden]({PAGES}/flash/?lang=da), opdatér under Indst. > System > *Opdatér firmware*, eller læg `{NAME}.bin` på SD-kortet til en launcher.

Map data © OpenStreetMap contributors (ODbL). License: MIT; third-party licenses in `dualmesh-licenses.zip`. See {REPO}#readme.
""")
    licenses(out)
    print(f"{ver} ({rev}): {len(data)} byte, sha256 {sha} -> {out}")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "check":
        check(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "package":
        package(sys.argv[2])
    else:
        sys.exit(__doc__ if __doc__ else "brug: release.py check <tag> | package <mappe>")
