# SPDX-License-Identifier: MIT
"""Logger T-Deck'ens serielle output til en fil med tidsstempler.

Brug:  python tools/serlog.py <logfil> <sekunder> [port] [start-taster] [slut-taster]
       start-taster sendes ~20 s efter start (efter kalibrering), slut-taster 5 s
       før slut (standard = start-taster ved lange logs), fx "a" og "m".
       Bemærk: at åbne porten kan genstarte T-Deck'en.
Kræver pyserial (findes i PlatformIO's Python: D:/Claude/tools/platformio/penv).
"""
import datetime
import sys
import time

import serial


def main():
    out, secs = sys.argv[1], float(sys.argv[2])
    port = sys.argv[3] if len(sys.argv) > 3 else 'COM5'
    keys = sys.argv[4].encode() if len(sys.argv) > 4 else b''
    end_keys = sys.argv[5].encode() if len(sys.argv) > 5 else keys
    f = open(out, 'w', encoding='utf-8')
    t0 = time.time()
    while True:
        try:
            s = serial.Serial(port, 115200, timeout=0.3)
            break
        except Exception:
            if time.time() - t0 > 10:
                raise
            time.sleep(0.3)
    buf, sent, sent_end = b'', not keys, not end_keys
    while time.time() - t0 < secs:
        buf += s.read(4096)
        if not sent and time.time() - t0 > 20:
            s.write(keys)
            sent = True
        if not sent_end and secs > 40 and time.time() - t0 > secs - 5:
            s.write(end_keys)  # lige før slut, fx for at se status efter en lang test
            sent_end = True
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            f.write(datetime.datetime.now().strftime('%H:%M:%S.%f')[:-3] + ' > ' +
                    line.decode('utf-8', 'replace').rstrip('\r') + '\n')
            f.flush()


if __name__ == '__main__':
    main()
