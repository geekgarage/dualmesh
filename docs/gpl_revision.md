# GPL-revision af DualMesh' Meshtastic-kode

Krav (17. nov 2026): når Meshtastic-delen er færdig, sammenlignes vores kode med
Meshtastics, så vi sikrer, at GPL-3.0-licensen ikke er brudt. Denne revision er lavet 17. nov
2026 efter fase 3, trin 1-4. Den skal gentages, når Meshtastic-koden ændres væsentligt (fx før
en udgivelse).

## Sammenlignet

- **Meshtastic-firmwaren**, `master` 968692d (17. nov 2026): `src/mesh`, `src/modules`,
  `src/xeddsa`, `src/crypto`. Det er 464 filer og 458 630 tokens, inklusive den genererede protobuf-kode.
- **meshtastic/protobufs**, `master` 8649b79 (16. nov 2026): alle `.proto`-filer.
- **Vores filer:** `mt_node.*`, `mt_tx.*`, `mt_pki.*`, `channel_scan.*`, `presets_meshtastic.*`,
  `main.cpp`, `ui/app_ui.cpp` og `mqtt_bridge.cpp`.

## Metode

Scriptet `tools/gpl_compare.py` (vores eget) sammenligner på tre måder:

1. **Rå match:** 12 ens tokens i træk (kommentarer og præprocessor fjernet).
2. **Strukturmatch:** 40 tokens i træk, hvor navne, tal og strenge er normaliseret. Det fanger
   kopieret kode med omdøbte navne.
3. **Tekst:** 6 ord i træk i vores kommentarer og strenge mod Meshtastics kommentarer og mod
   `.proto`-filerne.

```bash
python -I tools/gpl_compare.py <firmware>/src <protobufs> .
```

## Resultat

| | Fund |
| --- | --- |
| Kommentarer/strenge mod firmwaren | **0** |
| Tekst mod `.proto`-filerne | **0** |
| Rå match (12 tokens) | 40, alle trivielle (se nedenfor) |
| Strukturmatch (40 tokens) | 12, alle trivielle eller faktuelle (se nedenfor) |

Vores Meshtastic-filer bruger ikke Meshtastics `.proto`-filer eller genereret kode (`*.pb.h`).
Protobuf-encoderen og -decoderen er håndskrevne.

Gennemgang af hvert fund:

- **Standardidiomer (rå match):** `for (int i = 0; i < n; ++i) {`, `memset(&x, 0, sizeof(x))`,
  `ftell/fseek`, `(unsigned long)(millis() / 1000)`, `mac[3], mac[4], mac[5]` og include-lister
  (`freertos/queue.h` m.fl.). Det er almindelig C/C++, ikke beskyttet udtryk.
- **Meshtastics standardnøgle** `d4 f1 bb 3a …` (`channel_scan.cpp`). Det er en offentlig
  protokolkonstant, som skal kendes for at læse LongFast; den står også i Meshtastics
  dokumentation. En fakta, nødvendig for kompatibilitet.
- **Region- og preset-enums** (`presets_meshtastic.h`): navne og numre (`EU_868 = 3`,
  `LONG_FAST = 0`, …) svarer til protokollens enum-værdier. Det er fakta, der er nødvendige for
  kompatibilitet, og navnene er dem, brugerne kender fra appen. Skrevet i hånden, ikke kopieret
  fra den genererede kode.
- **Omregning af X25519-nøgle til Ed25519** (`mt_pki.cpp`, `verify`): de syv kald
  `fe_frombytes, fe_1, fe_sub, fe_add, fe_invert, fe_mul, fe_tobytes` ligner Meshtastics
  `curve_to_ed_pub`. Begge implementerer formlen y = (u − 1)/(u + 1) fra XEdDSA-specifikationen
  med de samme ref10-funktioner (vi bruger dem fra orlp/ed25519 via MeshCore), og det kan
  praktisk talt kun skrives på én måde ("merger"). Navne, kommentarer og den omgivende kode er
  forskellige.
- **Øvrige strukturmatch:** en tabel med 32 byte (`kLminus1`, gruppeordenen L − 1, matematisk
  konstant), feltvise tildelinger `f.x = d.x; …` og include-blokke. Det er generisk.

Andre punkter:

- **Kryptografien er vores egen.** AES-CCM er skrevet selv efter RFC 3610 (Meshtastic bruger en
  anden implementering). XEdDSA er skrevet efter Signals XEdDSA-specifikation. CRC-32 er
  standardalgoritmen.
- **Videresendelsen** (trin 4) følger de tal og regler, der står i
  `docs/meshtastic_protokol.md` (CWmin/CWmax, SNR-interval, slot-formlen). Koden er vores egen
  struktur: én kø i `MtNode`, ingen router-klassehierarki.

## Gentaget 22. nov 2026

Efter tandhjulsmenuens forespørgsler (`MtNode::requestInfo`), Meshtastic-appen over BLE
(`mt_ble.*`, `mt_phone.*`) og LVGL-chatten. Sammenlignet med firmwaren `master` f584291 (465
filer, 459 070 tokens) og protobufs `master` 40e5fec. Nye filer i scriptet: `mt_ble.*`,
`mt_phone.*`, `ui/app_ui_lv.cpp`.

| | Fund |
| --- | --- |
| Kommentarer/strenge mod firmwaren | **0** |
| Tekst mod `.proto`-filerne | **0** |
| Rå match (12 tokens) | 42, alle standardidiomer som før (`for (int i = 0; i < n; ++i)`, `memset`, include-lister) |
| Strukturmatch (40 tokens) | 13: de 11 kendte fra 17. nov plus to nye, begge generiske |

De to nye strukturmatch:

- `mt_phone.cpp` `encodeMetadata`: en række `m.u(felt, værdi);`-kald. Normaliseret er det bare
  "skriv tal i felt N" gentaget, som enhver protobuf-encoder ser ud. Feltnumrene er protokolfakta
  (DeviceMetadata), og encoderen `Pw` er vores egen.
- `app_ui_lv.cpp` `kOrder`: en tabel med Unicode-intervaller til emoji-vælgerens rækkefølge.
  Normaliseret matcher den enhver tabel med hex-par. Intervallerne er Unicode-blokke.

Konklusionen nedenfor gælder uændret.

## Gentaget 22. nov 2026 (aften): waypoints

Efter Meshtastic-waypoints (port 8: `MtNode::handleWaypoint`, `MtNode::sendWaypoint`, skrevet ud
fra afsnit 13 i `docs/meshtastic_protokol.md`), kort, spor og geocaching. Samme referencer
(firmware f584291, protobufs 40e5fec).

| | Fund |
| --- | --- |
| Kommentarer/strenge mod firmwaren | **0** |
| Tekst mod `.proto`-filerne | **0** |
| Rå match (12 tokens) | 49 (42 før): de 7 nye er alle `for (int i = 0; i < n; ++i)`-løkker i ny kode |
| Strukturmatch (40 tokens) | 13, de samme som før |

Waypoint-funktionerne har ingen match. Konklusionen nedenfor gælder uændret.

## Konklusion

Ingen kopieret kode, kommentar eller protobuf-tekst er fundet. Lighederne er protokolfakta,
matematisk bundne formler og almindelige idiomer. Ud fra denne revision bryder DualMesh' kode
efter vores vurdering ikke Meshtastics GPL-3.0-licens.

**Forbehold:**

- Vi er ikke jurister, og dette er ikke juridisk rådgivning.
- "Clean room" blev her gennemført af samme part, der læste koden: analysen og
  implementeringen blev adskilt via `docs/meshtastic_protokol.md`, men ikke af to forskellige
  personer.
- Skal projektet bruges kommercielt, eller bliver licensen bestridt, bør en jurist se på det.
- Navnet "Meshtastic" bruges kun til at beskrive kompatibilitet (se `CLAUDE.md`).
