# SPDX-License-Identifier: MIT
# Holder T-Deck'ens serielle port åben (åbning genstarter den), logger alt til serial.log
# og sender linjer, der skrives i cmd.txt (fil tømmes efter afsendelse; én linje pr.
# kommando, en linje på ét tegn sendes uden linjeskift).
import os, sys, time, serial
here = os.path.dirname(os.path.abspath(__file__))
log = open(os.path.join(here, "serial.log"), "ab")
cmd = os.path.join(here, "cmd.txt")
s = serial.Serial(sys.argv[1] if len(sys.argv) > 1 else "COM5", 115200, timeout=0.2)
while True:
    d = s.read(4096)
    if d:
        log.write(d); log.flush()
    if os.path.exists(cmd) and os.path.getsize(cmd) > 0:
        with open(cmd, "r", encoding="utf-8") as f:
            lines = [l.rstrip("\r\n") for l in f if l.strip("\r\n")]
        open(cmd, "w").close()
        for l in lines:
            s.write(l.encode("utf-8") if len(l) == 1 else l.encode("utf-8") + b"\r\n")
            s.flush()
            time.sleep(0.8)
