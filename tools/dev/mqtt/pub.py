# SPDX-License-Identifier: MIT
import sys, paho.mqtt.publish as p
p.single(sys.argv[1], sys.argv[2], hostname="127.0.0.1", qos=1)
