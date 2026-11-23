# DualMesh – kravspecifikation for T-Deck Plus

Nov 14, 2026

## Formål og licens

DualMesh er en ny firmware til LilyGo T-Deck Plus, der taler MeshCore og Meshtastic med én radio, plus Bluetooth, MQTT-bro og offline-kort. Vi skriver alt selv; ingen kode fra Trail Mate (AGPL) genbruges.

| Komponent | Licens | Konsekvens for os |
| --- | --- | --- |
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | MIT | Må bruges direkte som bibliotek, også i lukket eller MIT-licenseret kode. Kun copyright-notits skal med. |
| Meshtastic-firmware | GPL-3.0 | Ingen kode kopieres. |
| [Meshtastic protobufs](https://jsr.io/@meshtastic/protobufs) | GPL-3.0-only | Bruger vi deres .proto-filer eller genereret kode, bliver firmwaren GPL, når den distribueres. |
| Navnet Meshtastic | Varemærke | Må kun bruges til at beskrive kompatibilitet, ikke som produktnavn. |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | Kan bruges, men scheduleren har sin egen hurtigere SX1262-driver. |

**Beslutning (15. nov 2026):** DualMesh udgives under MIT. Meshtastic-delen skrives ud fra den offentlige protokoldokumentation med en håndskrevet protobuf-encoder til de få beskeder vi bruger, i stedet for at inkludere deres .proto-filer. Om det er nok til at holde koden fri af GPL er et juridisk spørgsmål; jeg er ikke jurist. Det enkle alternativ er at udgive hele projektet som GPL-3.0, så forsvinder spørgsmålet.

## Hardware: T-Deck Plus

Alt kører på én ESP32-S3 med 8 MB PSRAM og én SX1262-radio, så radiotid og SPI-bus er de knappe ressourcer ([LilyGo wiki](https://wiki.lilygo.cc/products/t-deck-series/t-deck-plus/)).

| Del | Specifikation | Betydning for designet |
| --- | --- | --- |
| MCU | ESP32-S3FN16R8, 2 kerner, 240 MHz | Radio-task på én kerne, UI/kort/WiFi på den anden |
| Hukommelse | 16 MB flash, 8 MB PSRAM | Firmware + to protokolstakke + kortcache kan være der |
| Radio | SX1262, op til +22 dBm | Én radio: kræver scheduleren |
| GPS | u-blox MIA-M10Q på Grove-pinnene | Grove kan ikke bruges til et ekstra radiomodul |
| Skærm | 2,8" ST7789, 320×240, GT911 touch | Deler SPI-bus med radio og SD-kort |
| Input | QWERTY via I²C (ESP32-C3), trackball | – |
| Lager | microSD | Kortfliser og beskedarkiv |
| Lyd | ES7210, mikrofon, højttaler | Senere funktioner |
| Strøm | 2000 mAh | LowPower-tilstand i scheduleren betyder noget |
| Trådløst | WiFi 4 + Bluetooth 5 LE | WiFi og BLE deler 2,4 GHz-radioen |

To hardware-risici skal afklares på enheden:

- **Delt SPI-bus:** Lange skærmoverførsler må ikke holde radioen hen. Skærmen skal tegne i små bidder (LVGL partial buffers), så radiokommandoer højst venter ca. 1 ms.
- **TCXO:** Har LoRa-modulet en TCXO, skal radioen stå i STDBY\_XOSC mellem skift, ellers koster hver opstart flere millisekunder. Spænding og pins verificeres mod LilyGos diagram.

## MeshCore: hvad kræves

MeshCore er den billige halvdel: biblioteket er MIT-licenseret, så vi bruger det direkte og lader det sende og modtage gennem vores scheduler i stedet for at tale med radioen selv. Hvor biblioteket kobles på radioen (dets Radio-abstraktion) skal bekræftes i kildekoden.

**Protokollen** ([pakkeformat](https://docs.meshcore.io/packet_format/), [kanaler og nøgler](https://docsearch.algolia.com/mcp/docs/repo/meshcore-dev/meshcore))

- Pakke: 1 byte header (rutetype, payloadtype, version), valgfri 4 byte transport-koder, sti op til 64 byte, payload op til 184 byte.
- Payloadtyper: REQ, RESPONSE, TXT\_MSG, ACK, ADVERT, GRP\_TXT, GRP\_DATA, ANON\_REQ, PATH, TRACE, MULTIPART, CONTROL, RAW\_CUSTOM.
- Identitet: Ed25519-nøglepar. Direkte beskeder: X25519-ECDH, AES-128-ECB og 2 byte HMAC-SHA256.
- Kanaler: offentlig kanal med kendt nøgle, hashtag-kanaler med nøgle = første 16 byte af SHA-256("#navn"), private kanaler med tilfældig nøgle.
- Routing: første besked flooder og lærer stien, derefter sendes direkte ad stien. Kanalbeskeder flooder altid.
- Radio i EU/UK Narrow: 869,618 MHz, 62,5 kHz, SF8, CR 4/8, sync word 0x12. Fra version 1.16.0 er preamblen 32 symboler under SF9; ældre noder sender 16 ([MeshCore 1.16.0](https://meshcore.at/en/news/fw-1-16-0)).

**Funktioner som i den originale companion-firmware**

- [ ] Kontakter fra adverts, egne adverts (zero-hop og flood) med navn og GPS-position
- [ ] Direkte beskeder med ACK, gensendelse og stilæring
- [ ] Offentlig kanal, hashtag-kanaler og private kanaler
- [ ] Rum-servere: login og hent gemte beskeder
- [ ] Fjernadministration af repeatere: login, status, telemetri, CLI
- [ ] Trace, så man kan se stien og SNR pr. hop
- [ ] Regioner via transport-koder
- [ ] Companion-protokollen over BLE og USB, så MeshCore-appen virker (se Bluetooth)
- [ ] Indbygget UI på T-Deck: samtaler, kanaler, kontakter, kort

Repeater-rollen slås ikke til på en håndholdt enhed som standard: den ville bruge sendetid fra den fælles 10 %-kvote.

## Meshtastic: hvad kræves

Meshtastic er den dyre halvdel: der findes ingen genbrugelig kode under en fri licens, så hele stakken skrives selv ud fra dokumentationen.

**Protokollen** ([oversigt](https://meshtastic.org/docs/overview/), [kryptering](https://meshtastic.org/docs/development/reference/encryption-technical/), [routing](https://meshtastic.org/docs/overview/mesh-algo/))

- Radio-header på 16 byte i klartekst: modtager, afsender, pakke-ID, flag (hop limit, want ack, via MQTT, hop start), kanal-hash, next hop, relay node. Payload højst 239 byte.
- Indhold: protobuf `Data` med portnummer (tekst, position, nodeinfo, telemetri, routing, traceroute, admin m.fl.).
- Kanaler: AES-CTR med kanalens PSK (16 byte = AES-128, 32 byte = AES-256). Nonce = pakke-ID (8 byte) + afsender (4 byte) + bloktæller. Standardkanalen bruger den kendte nøgle `AQ==`. Fra 2.8.1 kan en kanal bruge AES-CCM med 12 byte tag.
- Direkte beskeder (PKI, fra 2.5): X25519-nøgleudveksling, AES-256-CCM med 8 byte tag og 4 byte ekstra nonce. Pakker kan signeres med XEdDSA (fra 2.8).
- Routing: styret flooding med tilfældig forsinkelse vægtet efter SNR, hop limit tælles ned. Direkte beskeder bruger next-hop, når ruten er lært.
- Radio i EU\_868 LongFast: 869,525 MHz, 250 kHz, SF11, CR 4/5, sync word 0x2B, preamble 16 symboler. EU har 10 % duty cycle over en rullende time.

**Funktioner som i den originale firmware (klient-rolle)**

- [ ] Tekst på kanaler og som direkte besked, med ACK/NAK og gensendelse
- [ ] PKI: egen nøgle, lær andres nøgler fra NodeInfo, krypterede DM'er
- [ ] NodeInfo, Position og enhedstelemetri ud og ind
- [ ] Nodedatabase med SNR, hops, sidst hørt, position
- [ ] Viderebroadcast efter Meshtastic-reglerne (rollerne CLIENT og CLIENT\_MUTE)
- [ ] Traceroute
- [ ] Op til 8 kanaler med egne nøgler
- [ ] Telefon-API over BLE, så Meshtastic-appen virker (se Bluetooth)
- [ ] Fjernadministration (admin-beskeder), waypoints, Store & Forward, range test: senere faser

Telefon-API'et er den største enkeltopgave: appen forventer hele konfigurationsudvekslingen (MyNodeInfo, nodeliste, Config, ModuleConfig, kanaler) før den virker.

## Radio-scheduler

Scheduleren er skrevet og simuleret. Den fanger alle pakker, den overhovedet kan fange: i simuleringen svarer tabet til den andel af tiden, hvor radioen allerede er optaget af en pakke på det andet net. Det tab kan ingen løsning med én radio undgå.

**Sådan virker den**

1. For hvert net beregnes, hvor længe preamblen varer i værste fald (Meshtastic 131 ms, MeshCore SF8 med ældre noder 65,5 ms).
2. Heraf fås en deadline: seneste tidspunkt for næste CAD-lyt, hvis en pakke ikke må slippe forbi.
3. Scheduleren lytter altid på det net, hvis deadline kommer først (Earliest Deadline First). Det virker for alle kombinationer af frekvens, båndbredde, SF og preamble.
4. CAD kører i CAD\_RX-tilstand: ved et hit går radiochippen selv i modtagelse uden at vente på processoren.
5. Hvis planen ikke går op, forkorter scheduleren automatisk den længste CAD (færre symboler), indtil den gør. Med Meshtastic SF11 og MeshCore SF8 går Meshtastic-CAD'en fra 4 til 2 symboler.
6. Profiler med identisk kanal slås sammen, og har vi kun ét net, kører radioen i almindelig kontinuerlig modtagelse.
7. Afsendelse lytter først (listen before talk) og respekterer en fælles 10 %-kvote, fordi 869,525 og 869,618 MHz ligger i samme EU-delbånd.

To tilstande: **MaxSensitivity** lytter uafbrudt og giver flere chancer pr. preamble. **LowPower** starter hver CAD så sent som muligt og lader radioen hvile imellem; den bruger ca. 30–40 % mindre radiotid uden mærkbart større tab.

**Simuleringsresultat** (2 timer pr. linje, MeshCore SF8 med blandet 16/32-preamble, tab målt mod en dedikeret radio pr. net)

| Kanalbelastning pr. net | Tab Meshtastic | Tab MeshCore | Radio aktiv i LowPower |
| --- | --- | --- | --- |
| 2 % | 2,0 % | 0,7 % | 57 % |
| 5 % | 5,2 % | 4,8 % | 59 % |
| 15 % | 11,4 % | 9,5 % | 66 % |
| 30 % | 21,7 % | 18,9 % | 72 % |

Med kun nye MeshCore-noder (32 symboler), kun ældre noder (16) eller MeshCore på SF7 ligger tallene inden for 1–2 procentpoint af tabellen. Mesh-netværk sender typisk beskeder videre via flere noder, så det tab, brugeren mærker, er lavere.

**Følsomhed, der skal måles på hardware** (5 % belastning)

- Fanger CAD kun 90 % af preamblerne, stiger tabet til 12,5 % for Meshtastic og 5,3 % for MeshCore. CAD-indstillingerne skal derfor finjusteres på enheden.
- 1 % falske CAD-alarmer giver 13,8 % og 10,6 % tab. Næste optimering er at afbryde modtagelsen straks, når chippen ikke ser en preamble.
- Egen afsendelse hvert 30. sekund giver 7,0 % og 5,6 % tab.

Koden ligger i projektpakken: kerne (`radio_scheduler`), SX1262-driver til ESP-IDF og simulatoren. Kernen og simulatoren er kompileret og kørt; driveren er kun syntakstjekket og er ikke testet på en T-Deck.

## Bluetooth til mobil

T-Deck'en kan tilbyde begge officielle apps på én gang, fordi de bruger hver sin BLE-tjeneste. Hver app ser kun sit eget net.

|  | MeshCore-appen | Meshtastic-appen |
| --- | --- | --- |
| Tjeneste | Nordic UART, `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | `6ba1b218-15a8-461f-9fa8-5dcae273eafd` |
| Format | Binære rammer: 1 byte type + data, højst 172 byte | Protobuf ToRadio / FromRadio / FromNum |
| Opstart | CMD\_APP\_START, derefter enhedsinfo og kontakter | want\_config: MyNodeInfo, noder, Config, kanaler, config\_complete |
| Sikkerhed | Bonding med PIN (MITM-beskyttelse) | Bonding med PIN |
| Arbejde | Moderat: protokollen er dokumenteret, og MeshCore-biblioteket har kontakter og beskeder | Stort: hele konfigurationsmodellen skal efterlignes |

Kilder: [MeshCore companion-protokol](https://docs.meshcore.io/companion_protocol/), [Meshtastic klient-API](https://meshtastic.org/docs/development/device/client-api/).

ESP32-S3 kan håndtere to samtidige BLE-forbindelser med NimBLE, men BLE og WiFi deler 2,4 GHz-radioen. Under en stor kortdownload over WiFi kan BLE blive langsom. Et alternativ er en egen DualMesh-app, der viser begge net i én indbakke; det er et større projekt og hører til en senere fase.

## MQTT-bro

Broen bruger ét fælles emneformat for begge net. Net, beskedtype og kanal står både i emnet og i JSON-indholdet, så man kan filtrere i fx Home Assistant eller Node-RED.

**Emner** (`<rod>` = fx `dualmesh/<enheds-id>`)

| Retning | Emne | Indhold |
| --- | --- | --- |
| Modtaget | `<rod>/rx/<net>/chan/<kanal>` | Kanalbesked |
| Modtaget | `<rod>/rx/<net>/dm/<afsender>` | Direkte besked til os |
| Send | `<rod>/tx/<net>/chan/<kanal>` | Tekst ud på en kanal |
| Send | `<rod>/tx/<net>/dm/<modtager>` | Direkte besked ud |
| Status | `<rod>/status` | Online, batteri, GPS-fix, kø-længde |

`<net>` er `meshcore` eller `meshtastic`. Kanalnavne URL-kodes, så `#test` bliver `%23test`.

**Eksempel på modtaget besked**

```json
{"net":"meshcore","type":"chan","channel":"Public","from":"a1b2c3d4","from_name":"Jens T-Deck","text":"Hej","rssi":-97,"snr":6.5,"hops":2,"ts":1794665954}
```

Regler:

- Kun beskeder, vi selv kan dekryptere, sendes ud. Rå krypterede pakker kan slås til som debug.
- Broen må ikke sende MQTT-beskeder ud på LoRa uden at de tæller med i 10 %-kvoten. Ellers kan man fylde begge net fra internettet.
- Valgfrit: Meshtastics eget format (`msh/EU_868/2/e/<kanal>/!<node-id>`, protobuf `ServiceEnvelope`) for kompatibilitet med eksisterende Meshtastic-værktøjer ([Meshtastic MQTT](https://meshtastic.org/docs/software/mqtt/)). MeshCore har ikke et officielt MQTT-format i firmwaren.
- Broker: din egen, fx Mosquitto i en container på Unraid, med TLS og brugernavn/kodeord. Broen kører kun, når T-Deck'en er på WiFi.

## Kort

Kortet viser egen GPS-position og noder fra begge net. Hjemmefra vælger man et område og et zoom-interval på T-Deck'en, og fliserne hentes over WiFi til SD-kortet. Kildeserveren må dog ikke være OpenStreetMaps egen.

**Tilladt kilde.** OpenStreetMaps flise-server forbyder forhåndsdownload til offline brug og blokerer klienter, der gør det ([OSM tile policy](https://operations.osmfoundation.org/policies/tiles/)). Muligheder:

| Kilde | Fordel | Ulempe |
| --- | --- | --- |
| Egen fliseserver på Unraid (fx Protomaps PMTiles eller tileserver-gl med OSM-data) | Ingen grænser, virker også for venner | Skal sættes op og opdateres |
| Kommerciel udbyder, der tillader offline (fx med API-nøgle) | Ingen drift | Pris og vilkår skal tjekkes |
| Fliser forberedt på PC og kopieret til SD | Simpelt | Ikke "direkte på T-Deck" |

**Opløsning = zoom-niveau.** Brugeren vælger område og højeste zoom; T-Deck'en viser antal fliser og forventet størrelse før download. Beregnede tal for 256-pixel-fliser:

| Område | Zoom | Antal fliser | Ca. størrelse ved 20 KB/flise |
| --- | --- | --- | --- |
| 20 × 20 km (fx Odense) | 10–14 | 350 | 7 MB |
| 20 × 20 km | 10–15 | 1.250 | 25 MB |
| 20 × 20 km | 10–16 | 4.731 | 95 MB |
| Hele Danmark | 6–12 | 7.680 | 150 MB |
| Hele Danmark | 6–13 | 29.984 | 600 MB |

Fliseantallet er beregnet; 20 KB pr. flise er et skøn, der afhænger af kortstil og format. Fliser gemmes i en enkelt pakkefil pr. område (fx PMTiles eller MBTiles) frem for tusindvis af små filer, fordi FAT på SD-kort er langsomt med mange filer.

Krav:

- [ ] Download kan pauses og genoptages og kører i baggrunden, mens LoRa kører videre
- [ ] Liste over hentede områder med størrelse og dato; slet område
- [ ] Kortet virker helt uden net, med position, retning og noder fra begge net
- [ ] Tastaturlayouts med specialtegn pr. fysisk tast (overlay over indtastningen, vælg med
      trackball eller touch); flere layouts kan installeres fra appens indstillinger og hentes fra
      GitHub (`keyboards/index.txt` + `<id>.kbd`), gemmes på SD
- [ ] Egen position deles kun, når brugeren har slået det til (standard fra), og andre noder kan
      ikke hente den via forespørgsler (Meshtastic Position/NodeInfo, MeshCore adverts/telemetri),
      BLE-apps eller MQTT, når den er slået fra
- [ ] Rasterfliser i første version; vektorfliser kan komme senere, hvis ydelsen tillader det

## Trackball

Trackballens følsomhed skal kunne indstilles, fordi den kan føles vild. Den giver korte pulser for hver retning, og firmwaren bestemmer selv, hvor mange pulser der skal til et skridt på skærmen.

| Indstilling | Hvad den gør | Standard |
| --- | --- | --- |
| Følsomhed (1–10) | Antal pulser pr. skridt i lister og menuer. Lav værdi = flere pulser kræves, roligere bevægelse | 5 |
| Acceleration (fra, lav, høj) | Hurtige rul flytter flere skridt, langsomme rul præcist ét | Lav |
| Kort-følsomhed | Separat indstilling for panorering på kortet | 5 |
| Dødzone | Ignorerer enkelte løse pulser, så et let tryk ikke flytter markøren | Til |
| Klik-forsinkelse | Undertrykker bevægelse lige efter klik, så man ikke rammer forkert punkt | 150 ms |
| Vend akser | Byt op/ned og venstre/højre | Fra |

Krav:

- [ ] Indstillingerne findes under Indstillinger → Input og gemmes, så de overlever genstart
- [ ] Ændringer virker med det samme, med et testfelt hvor man kan prøve trackballen
- [ ] Pulserne tælles i interrupts og filtreres i input-laget, så UI'et ikke mærker forskel på indstillingerne

## Arkitektur og faseplan

Firmwaren bygges med ESP-IDF og FreeRTOS. Radioen ejes af én task med høj prioritet på kerne 0; alt andet kører på kerne 1 og taler med radioen gennem køer.

**Komponenter**

- **Radio-task (kerne 0):** SX1262-driver og scheduler. Leverer pakker mærket med net.
- **MeshCore-stak:** MeshCore-biblioteket (MIT) med kontakter, kanaler og routing.
- **Meshtastic-stak:** egen implementering af header, kryptering, routing og de nødvendige protobuf-beskeder.
- **Fælles beskedlager:** én indbakke med net, kanal eller DM, tidsstempel og status, gemt på SD.
- **UI (kerne 1):** LVGL med samtaler, kanaler, noder, kort og indstillinger.
- **Forbindelser:** BLE (begge app-protokoller), WiFi, MQTT-bro, kortdownload.
- **GPS:** position til kort, MeshCore-adverts og Meshtastic-positionsbeskeder.

**Faser**

1. **Radio på hardware:** driver og scheduler på T-Deck Plus. Mål skiftetid, finjuster CAD, løs SPI-deling med skærmen. Kriterium: målt tab ligner simuleringen.
2. **MeshCore:** biblioteket koblet på scheduleren, kanaler, DM'er og kontakter i et simpelt UI.
3. **Meshtastic:** modtag og send på LongFast og egne kanaler, NodeInfo, Position, PKI-DM'er.
4. **Fælles UI og beskedlager** for begge net, inklusive indstillinger for trackballen.
5. **Bluetooth:** MeshCore companion-protokol først (mindst arbejde), derefter Meshtastic telefon-API.
6. **MQTT-bro.**
7. **Kort** med GPS og offline-download.

Fase 1 er den, der afgør, om resten giver mening, så den kommer først.

## Åbne beslutninger

- [x] **Licens:** MIT med håndskrevet Meshtastic-encoding (besluttet 15. nov 2026).
- [ ] **Bluetooth:** begge officielle apps (anbefalet), eller kun én af dem i første omgang?
- [x] **Radioindstillinger:** alle Meshtastic-regioner/-presets og alle MeshCore-presets understøttes globalt og vælges i `settings.ini` (besluttet 15. nov 2026; standard EU).
- [ ] **Kortkilde:** egen fliseserver på Unraid, eller en kommerciel udbyder der tillader offline?
- [ ] **Videresendelse:** skal T-Deck'en nogensinde fungere som MeshCore-repeater eller Meshtastic-router, eller altid kun som klient?

## Kilder

- [LilyGo T-Deck Plus wiki](https://wiki.lilygo.cc/products/t-deck-series/t-deck-plus/)
- [MeshCore pakkeformat](https://docs.meshcore.io/packet_format/) · [companion-protokol](https://docs.meshcore.io/companion_protocol/) · [1.16.0 preamble](https://meshcore.at/en/news/fw-1-16-0)
- [Meshtastic oversigt](https://meshtastic.org/docs/overview/) · [kryptering](https://meshtastic.org/docs/development/reference/encryption-technical/) · [klient-API](https://meshtastic.org/docs/development/device/client-api/) · [MQTT](https://meshtastic.org/docs/software/mqtt/)
- [Meshtastic protobufs (GPL-3.0)](https://jsr.io/@meshtastic/protobufs)
- [OSM tile usage policy](https://operations.osmfoundation.org/policies/tiles/)
