# Tredjeparts-notitser

*[English](THIRD_PARTY_NOTICES.md) | Dansk*

DualMesh er MIT (se `LICENSE`). Firmwaren (og repoet) indeholder eller bygger på følgende værker.
Lægges firmwaren ud som binær fil (fx en GitHub-release), skal denne fil og licensteksterne følge
med.

## Kode i repoet

| Værk | Licens | Hvor | Licenstekst |
| --- | --- | --- | --- |
| MeshCore (udvalgte filer, companion-v1.18.0) | MIT | `lib/meshcore/` | `lib/meshcore/license.txt` |
| ed25519 af Orson Peters (via MeshCore) | zlib | `lib/ed25519/` | `lib/ed25519/license.txt` |

## Biblioteker, der hentes ved bygning (lib_deps / platform)

| Værk | Licens | Bemærkning |
| --- | --- | --- |
| LVGL 8.4 | MIT | Brugerfladen; inkl. qrcodegen (MIT) og LVGL's Montserrat-fallback (OFL 1.1) |
| rweather/Crypto | MIT | AES, SHA-256, Curve25519, Ed25519 |
| densaugeo/base64 | MIT | |
| Arduino-ESP32 2.0.14 | LGPL-2.1 (kerne) / Apache-2.0 | Framework; linkes statisk (LGPL: kildekoden er offentlig hos Espressif, og DualMesh' kildekode er åben, så firmwaren kan genbygges med en ændret kerne) |
| ESP-IDF 4.4.6 (FreeRTOS, lwIP, mbedtls, esp-mqtt, esp_http_client, TinyUSB, FatFs, Bluedroid) | Apache-2.0, MIT, BSD-3, FatFs-licens (BSD-agtig) | Via Arduino-ESP32; Apache-2.0-notitser i Espressifs kildekode |

## Fonte (genereret ind i firmwaren)

| Font | Licens | Licenstekst |
| --- | --- | --- |
| Montserrat (Medium, SemiBold) | SIL OFL 1.1 | `fonts/montserrat/OFL.txt` |
| JetBrains Mono | SIL OFL 1.1 | `fonts/jetbrainsmono/OFL.txt` |
| Noto Emoji | SIL OFL 1.1 | `fonts/notoemoji/OFL.txt` |
| Noto Color Emoji | SIL OFL 1.1 | `fonts/notocoloremoji/OFL.txt` |

Fontene er omsat til bitmap-fonte (`src/ui/lv/fonts/`) med `tools/gen_lvgl_font.py` og
`tools/gen_emoji_color.py`; de har deres oprindelige navne og licens.

## Kortdata (ikke i repoet; kortpakker laves med tools/maps/make_map.py)

| Kilde | Licens | Krav |
| --- | --- | --- |
| OpenStreetMap (via Protomaps' daglige builds) | ODbL 1.0 | Kildeangivelse "© OpenStreetMap-bidragydere" (vises på kortet); fliserne er et "produced work" |
| Natural Earth (lave zoomniveauer) | Public domain | Ingen |
| Protomaps' `landcover`-lag (ESA WorldCover) | CC-BY 4.0 | Bruges IKKE (tegnes ikke) |

## Protokoller og formater (ingen kode overtaget)

- Meshtastic: skrevet ud fra den offentlige protokoldokumentation (clean room,
  `docs/meshtastic_protokol.md`); ingen kode, `.proto`-filer eller genereret kode fra
  Meshtastic-projektet (GPL-3.0). Se `docs/gpl_revision.md`. "Meshtastic" bruges kun til at
  beskrive kompatibilitet.
- PMTiles v3 (Protomaps, BSD-3-specifikation), PNG, GPX, Groundspeak-udvidelsen, u-blox UBX,
  Opencaching OKAPI: egne implementeringer ud fra de offentlige beskrivelser.
- MeshCores companion-protokol over Bluetooth: egen implementering efter MeshCore (MIT).
