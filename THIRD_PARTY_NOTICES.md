# Third-party notices

*English | [Dansk](THIRD_PARTY_NOTICES.da.md)*

DualMesh is MIT licensed (see `LICENSE`). The firmware (and the repository) contains or builds on
the following works. When the firmware is distributed as a binary (e.g. a GitHub release), this
file and the license texts must accompany it.

## Code in the repository

| Work | License | Where | License text |
| --- | --- | --- | --- |
| MeshCore (selected files, companion-v1.18.0) | MIT | `lib/meshcore/` | `lib/meshcore/license.txt` |
| ed25519 by Orson Peters (via MeshCore) | zlib | `lib/ed25519/` | `lib/ed25519/license.txt` |

## Libraries fetched at build time (lib_deps / platform)

| Work | License | Notes |
| --- | --- | --- |
| LVGL 8.4 | MIT | User interface; includes qrcodegen (MIT) and LVGL's Montserrat fallback (OFL 1.1) |
| rweather/Crypto | MIT | AES, SHA-256, Curve25519, Ed25519 |
| densaugeo/base64 | MIT | |
| Arduino-ESP32 2.0.14 | LGPL-2.1 (core) / Apache-2.0 | Framework; statically linked (LGPL: the source code is public at Espressif, and DualMesh's source code is open, so the firmware can be rebuilt with a modified core) |
| ESP-IDF 4.4.6 (FreeRTOS, lwIP, mbedtls, esp-mqtt, esp_http_client, TinyUSB, FatFs, Bluedroid) | Apache-2.0, MIT, BSD-3, FatFs license (BSD-style) | Via Arduino-ESP32; Apache-2.0 notices in Espressif's source code |

## Fonts (generated into the firmware)

| Font | License | License text |
| --- | --- | --- |
| Montserrat (Medium, SemiBold) | SIL OFL 1.1 | `fonts/montserrat/OFL.txt` |
| JetBrains Mono | SIL OFL 1.1 | `fonts/jetbrainsmono/OFL.txt` |
| Noto Emoji | SIL OFL 1.1 | `fonts/notoemoji/OFL.txt` |
| Noto Color Emoji | SIL OFL 1.1 | `fonts/notocoloremoji/OFL.txt` |

The fonts are converted to bitmap fonts (`src/ui/lv/fonts/`) with `tools/gen_lvgl_font.py` and
`tools/gen_emoji_color.py`; they keep their original names and licenses.

## Map data (not in the repository; map packs are made with tools/maps/make_map.py)

| Source | License | Requirements |
| --- | --- | --- |
| OpenStreetMap (via Protomaps' daily builds) | ODbL 1.0 | Attribution "© OpenStreetMap contributors" (shown on the map); the tiles are a "produced work" |
| Natural Earth (low zoom levels) | Public domain | None |
| Protomaps' `landcover` layer (ESA WorldCover) | CC-BY 4.0 | NOT used (not drawn) |

## Protocols and formats (no code taken over)

- Meshtastic: written from the public protocol documentation (clean room,
  `docs/meshtastic_protokol.md`); no code, `.proto` files or generated code from the Meshtastic
  project (GPL-3.0). See `docs/gpl_revision.md`. "Meshtastic" is only used to describe
  compatibility.
- PMTiles v3 (Protomaps, BSD-3 specification), PNG, GPX, the Groundspeak extension, u-blox UBX,
  Opencaching OKAPI: our own implementations based on the public descriptions.
- MeshCore's companion protocol over Bluetooth: our own implementation following MeshCore (MIT).
