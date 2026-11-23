# SPDX-License-Identifier: MIT
# Abonnerer på dualmesh/# og skriver alt med tidsstempel (stdout + mqtt/watch.log).
import sys, time, paho.mqtt.client as mqtt
host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
log = open(__file__.replace("watch.py", "watch.log"), "a", encoding="utf-8")
def out(s):
    line = time.strftime("%H:%M:%S ") + s
    print(line, flush=True); log.write(line + "\n"); log.flush()
def on_connect(c, u, f, rc, p=None):
    out(f"forbundet rc={rc}"); c.subscribe("dualmesh/#", qos=1)
def on_message(c, u, m):
    out(f"{m.topic} {'[retained] ' if m.retain else ''}{m.payload.decode('utf-8', 'replace')}")
c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="dualmesh-watch")
c.on_connect = on_connect; c.on_message = on_message
c.connect(host, 1883, 60)
c.loop_forever()
