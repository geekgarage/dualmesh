# SPDX-License-Identifier: MIT
"""Henter DualMesh' logfiler fra SD-kortet over USB (T-Deck'ens 'D'-kommando).

Brug:  python tools/fetch_logs.py [port] [mappe]
       standard: COM5 og logs/sd
Filerne gemmes som <mappe>/NNNN.log (samme navn som på kortet). En fil, der
allerede er hentet, overskrives kun, hvis den på kortet er blevet større.
Bemærk: at åbne porten genstarter T-Deck'en; den starter så en ny logfil.
Kræver pyserial (findes i PlatformIO's Python: D:/Claude/tools/platformio/penv).
"""
import os
import re
import sys
import time

import serial

HEAD = re.compile(rb'\n=== DMLOG-FIL (\d{4}\.log) (\d+) ===\n')
END = re.compile(rb'\n=== DMLOG-SLUT (\d+) filer ===\n')


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join('logs', 'sd')
    os.makedirs(out_dir, exist_ok=True)
    s = serial.Serial(port, 115200, timeout=0.5)
    # Vent til opstarten er færdig (SD monteret, loop kører).
    print('venter paa opstart (25 s)...')
    t0 = time.time()
    while time.time() - t0 < 25:
        s.read(4096)
    s.write(b'D')
    buf = b''
    t0 = time.time()
    last = time.time()
    while True:
        chunk = s.read(65536)
        if chunk:
            buf += chunk
            last = time.time()
        if END.search(buf):
            break
        if time.time() - last > 20:
            sys.exit('FEJL: intet svar fra T-Deck (20 s). Er firmwaren med D-kommandoen flashet?')
    s.close()

    start = HEAD.search(buf)
    if not start:
        print('Ingen logfiler paa kortet.')
        return
    pos = start.start()
    total = 0
    while True:
        m = HEAD.match(buf, pos)
        if not m:
            break
        name, size = m.group(1).decode(), int(m.group(2))
        data = buf[m.end():m.end() + size]
        pos = m.end() + size
        if len(data) != size:
            print(f'{name}: ufuldstaendig ({len(data)} af {size} bytes) - springes over')
            continue
        path = os.path.join(out_dir, name)
        # Samme fil = den lokale er begyndelsen af den nye (filen er kun vokset).
        # Ellers er det en anden opstart med samme nummer (fx efter "Ryd log"):
        # gem den som NNNN-2.log osv., så den gamle ikke overskrives.
        stem, ext = os.path.splitext(name)
        k = 1
        while os.path.exists(path):
            with open(path, 'rb') as f:
                old = f.read()
            if data[:len(old)] == old or old[:len(data)] == data:
                break
            k += 1
            path = os.path.join(out_dir, f'{stem}-{k}{ext}')
        if os.path.exists(path) and os.path.getsize(path) >= size:
            print(f'{os.path.basename(path)}: allerede hentet')
            continue
        name = os.path.basename(path)
        with open(path, 'wb') as f:
            f.write(data)
        total += size
        print(f'{name}: {size} bytes gemt')
    print(f'Faerdig: {total} bytes nye data i {out_dir} ({time.time() - t0:.0f} s)')


if __name__ == '__main__':
    main()
