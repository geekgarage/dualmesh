# SPDX-License-Identifier: MIT
# Lokal MQTT-broker til test af DualMesh (amqtt), lytter på 0.0.0.0:1883 uden login.
import asyncio, logging
from amqtt.broker import Broker

CFG = {
    "listeners": {"default": {"type": "tcp", "bind": "0.0.0.0:1883"}},
    "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True},
                "amqtt.plugins.sys.broker.BrokerSysPlugin": {"sys_interval": 0}},
}

async def main():
    b = Broker(CFG)
    await b.start()
    print("broker koerer paa 0.0.0.0:1883", flush=True)
    await asyncio.Event().wait()

logging.basicConfig(level=logging.WARNING)
asyncio.run(main())
