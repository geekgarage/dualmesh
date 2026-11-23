# DualMesh – firmware til LilyGo T-Deck Plus

Ny firmware skrevet fra bunden, der taler **MeshCore og Meshtastic med én SX1262-radio** via en
tidsdelende scheduler. Første prioritet er de to protokoller; derefter BLE, MQTT-bro, kort og UI.
Fuld kravspecifikation: `docs/spec.md` (læs den før større designbeslutninger).

Projektets sprog er dansk: svar og skriv kommentarer/logtekster på dansk; kode-identifiers på engelsk.

## Licensregler (vigtigt)

- Projektet er **MIT** (besluttet 15. nov 2026, se `LICENSE`). Alle kildefiler starter
  med `// SPDX-License-Identifier: MIT`.
- **Kopiér aldrig kode fra Trail Mate (AGPL) eller Meshtastic-firmwaren (GPL-3.0).**
- **Brug ikke Meshtastics `.proto`-filer eller genereret protobuf-kode** (GPL-3.0). Meshtastic-delen
  skrives ud fra den offentlige protokoldokumentation med en håndskrevet protobuf-encoder/decoder
  til de få beskeder, vi bruger.
- **Clean room for Meshtastic (besluttet 17. nov 2026):** Meshtastic-firmwarens kode
  må LÆSES for at forstå protokollen, men kun som analyse: fakta (byteformater, nonces,
  konstanter, feltnumre, algoritmetrin) skrives i `docs/meshtastic_protokol.md` uden kode,
  kopierede kommentarer eller deres struktur, og vores kode skrives ud fra det dokument i vores
  egen stil. **Når Meshtastic-delen er færdig, skal vores kode sammenlignes med Meshtastics**
  for at sikre, at GPL-licensen ikke er brudt (revision; krav).
- MeshCore-biblioteket (MIT) og RadioLib (MIT) må bruges; behold deres copyright-notits.
- "Meshtastic" må kun bruges til at beskrive kompatibilitet, ikke som produktnavn.
- Grundlag for valget (målt i meshtastic/protobufs, nov 2025 → nov 2026, v2.7.14 → v2.8.2): de
  beskeder, vi sender over radioen, fik kun nye valgfrie felter, ingen blev fjernet eller
  omnummereret (`User`, `Position` uændret; `Data.xeddsa_signature`, `Routing.ack_proof`,
  `ChannelSettings.use_aead`; 9 nye portnumre). Ukendte felter skal ignoreres af vores decoder, så
  ældre DualMesh stadig virker. Hold øje med nye releases; sikkerhedsfunktionerne (XEdDSA-
  signaturer, ack-bevis, AES-CCM) skal skrives selv ud fra den offentlige beskrivelse.

## Hardware

ESP32-S3FN16R8 (16 MB flash, 8 MB OPI PSRAM), SX1262, ST7789 320×240, keyboard via I²C (ESP32-C3),
trackball, microSD, u-blox MIA-M10Q GPS på Grove-pinnene.

- `POWER_ON` = GPIO10 tænder radio, skærm og keyboard.
- Radio (CS 9), skærm (CS 12) og SD (CS 39) deler SPI-bus (SCK 40, MISO 38, MOSI 41). Ikke-aktive
  CS skal holdes høje, også radioens før dens driver starter (ellers fejler SD-init tilfældigt). Lange skærmoverførsler må ikke blokere radioen (tegn i små bidder).
- Radioen har TCXO: `XOSC_START_ERR` ved opstart er forventet og ryddes. Stå i STDBY_XOSC mellem
  skift, men bemærk: en CAD ender altid i STDBY_RC (TCXO slukket), så hver CAD betaler TCXO-opstarten.

## Byg, flash og test

PlatformIO, env `tdeck_plus` (Arduino-framework, men SX1262-driveren bruger ESP-IDF's
`spi_master` direkte). Platformen er låst til `espressif32@6.5.0` (Arduino-ESP32 2.0.14 / IDF
4.4.6); uden låsning henter en ny PC 8.x (Arduino 3), som ikke bygger.
Repoet ligger i Dokumenter (OneDrive), værktøjerne i **`C:\Claude\tools`** (uden for OneDrive):
`PLATFORMIO_CORE_DIR=C:\Claude\tools\platformio` (brugermiljøvariabel); er `pio` ikke på PATH, brug
`C:/Claude/tools/platformio/penv/Scripts/pio.exe`. Gamle logs ligger i `logs/` (ignoreres af git),
Heltec-firmware (Meshtastic) i `C:\Claude\tools\mtfw_*`.
**Værktøjsstien skal være kort:** med Arduino 2.0.14 får GCC flere hundrede `-I`-stier med
PlatformIO-mappen foran; med PlatformIO i OneDrive (72 tegn) blev kommandolinjen over Windows'
32 767 tegn ("xtensa-esp32s3-elf-g++: error: CreateProcess: No such file or directory").
`python` skal være på PATH under bygget (`tools/git_rev.py`; ellers "exited 9009").
`tools/dev/setup_pc.ps1` (standard `-Root` = Dokumenter\Claude, `-Tools` = C:\Claude\tools) gør en
Windows-PC klar (Git, GitHub CLI, Python 3.11, CMake, Ninja, WinLibs GCC,
Node.js, PlatformIO 6.2.0, venvs `mqtt-venv`/`mtvenv`/`fontvenv` med faste versioner, MQTT-scripts
fra `tools/dev/mqtt/` til `<Tools>\mqtt`), bygger og kører hosttestene.
`serial_bridge.py [COMx]` tager porten som argument (standard COM5).

```bash
pio run -e tdeck_plus                 # byg
pio run -e tdeck_plus -t upload       # flash (T-Deck i USB)
pio device monitor -e tdeck_plus      # seriel log, 115200; kører til den stoppes
```

- Monitoren er interaktiv og stopper ikke af sig selv: kør den som baggrundsopgave og læs outputtet,
  eller læs de gemte logs (`platformio-device-monitor-*.log` i roden; ældre logs ligger i `logs/`).
- Taster i monitoren: `s` = statistik nu, `p` = plan, `h` = hjælp (alle taster), `k`/`K` = kanaler/selvtest, `M` = beskedlager-selvtest, `P` = profil-selvtest,
  `I` = rapport-hoved (version, opstart, nedbrud), `d` = send de 3 nyeste SD-logs (fejlrapport-siden),
  `=` + `nøgle=værdi` + Enter = sæt en indstilling (gemmes og anvendes som Gem i menuen; `mqtt_pass` går
  til den krypterede fil). **Radioindstillinger (fx `*_tx_power`) genstarter IKKE selv** via `=`:
  genstart bagefter (RTS-puls på porten som esptool, eller åbn porten igen).
- At åbne COM-porten genstarter T-Deck'en. Til længere test: `C:\Claude\tools\mqtt\serial_bridge.py`
  holder porten åben, logger til `serial.log` og sender linjer fra `cmd.txt`.
- Upload fejler: bed brugeren holde trackballen nede, trykke reset og slippe (boot-tilstand).
- Hostsimulator og test (CMake + Ninja + GCC fra WinLibs (14 på den gamle PC, 16.2 på den nye; samme
  simuleringsresultat), installeret via winget i
  `C:\Claude\tools\winlibs`; samme pakke
  har clang, clang-tidy, clang-format og gdb). Visual Studio har ingen C++-del, så angiv altid
  `-G Ninja`. Står værktøjerne ikke på PATH i en gammel shell, hent PATH igen fra registreringen.
  Simuleringen kører på ca. 1 s og er deterministisk; diff mod `host/sim_resultat.txt`:
  ```bash
  cmake -S host -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ && cmake --build build && ./build/sim > build/sim_ny.txt && ./build/test_phy && ./build/test_report
  ```
- **Send aldrig med radioen uden antenne monteret.** Bekræftet 15. nov 2026: 
  antennen er monteret, og at afsendelse må aktiveres, når koden er klar. Overhold altid regionens
  duty cycle og effektgrænse (fx EU 869,4–869,65 MHz: 10 %, 27 dBm), og start forsigtigt
  (få, korte pakker), før automatisk afsendelse slås til. Bliver antennen taget af, gælder
  forbuddet igen.
- Du kan ikke se skærmen eller mærke trackballen: spørg brugeren, hvordan UI ser ud og føles.

## Kodestruktur

| Sti | Indhold |
| --- | --- |
| `src/main.cpp` | Fase 1 testfirmware: radio, scheduler, pakkelog, statistik hvert 10. s |
| `src/hal/sx1262_hal.*` | SX1262-driver, direkte registre via ESP-IDF SPI |
| `src/ui/` | Brugerfladen (LVGL); `status_display` = skærmdriveren (init, pixels, lys, farver) |
| `src/net/meshcore_radio.*` | MeshCores `mesh::Radio` over scheduleren (FreeRTOS-køer mellem radio- og loop-task) |
| `src/net/meshcore_node.*` | MeshCore-klient (`BaseChatMesh`): adverts/kontakter, kanaler, DM'er; identitet på SD |
| `lib/meshcore/` | Låst kopi af de MeshCore-filer vi bruger (MIT); opdateres kun med `tools/sync_meshcore.py` |
| `lib/ed25519/` | MeshCores ed25519 (orlp, zlib-licens), hentes med samme script |
| `src/net/msg_store.*` | Beskeder pr. samtale (kanal/DM, begge net): ringbuffer i PSRAM + én fil pr. samtale på SD, ulæste |
| `src/util/b64.h` | Lille base64 (densaugeo/base64 kan kun inkluderes ét sted) |
| `src/ui/input.*` | Tastatur (I²C 0x55, SDA 18/SCL 8) og trackball (GPIO 2 højre, 3 op, 1 venstre, 15 ned, 0 klik) |
| `src/ui/keymap.*`, `keyboards/` | Specialtegn pr. tast fra layouts (`*.kbd`); indbygget via `tools/gen_keyboards.py`, installerede på SD |
| `src/ui/font8x16.h` | GENERERET af `tools/gen_font.py`; bygges ikke længere ind, men er tegnsættet for LVGL-fontene og tjekket i `gen_keyboards.py` |
| `src/ui/app_ui.*` | UI-logikken (input, faner, skrivning, skærmslukning, SOS); tegnes i `app_ui_lv.cpp` |
| `src/net/channel_scan.*`, `src/ui/channels_ui.*` | Vores Meshtastic-kanaler (primær + tilføjede) og fanen "Kanal" (tilføj/slet). Scanning af hørte kanaler fjernet 17. nov 2026 |
| `src/storage/sd_store.*` | Indstillinger på SD-kortet (`/sd/dualmesh/settings.ini`, ESP-IDF sdspi på den delte bus) |
| `lib/dualmesh/src/` | Hardware-uafhængig kerne: `radio_scheduler`, `lora_profile.h`, `phy_math.h`, `radio_hal.h` |
| `lib/dualmesh/src/presets_*` | Alle Meshtastic-regioner/-presets og MeshCore-presets (globalt), test i `host/tests/test_presets.cpp` |
| `src/util/wall_clock.*` | Fælles ur: GPS > NTP > app > mesh; sætter systemur + MeshCore-RTC |
| `src/net/wifi_mgr.*`, `src/ui/wifi_ui.*` | WiFi: status, scan, gemte/skjulte net (`/sd/dualmesh/wifi.txt`), NTP |
| `src/hal/gps.*`, `src/ui/gps_ui.*` | GPS (egen NMEA-parser, auto-baud/pinne) og GPS-status |
| `src/net/mt_tx.*` | Minimal Meshtastic-afsendelse (tekst + Position), bruges til SOS |
| `src/net/mt_node.*` | Meshtastic-node (fase 3): afkodning af Data/porte, nodedatabase (`/sd/dualmesh/meshtastic/nodes.txt`), DM'er med ACK, selvtest `X` |
| `src/net/mt_pki.*` | Meshtastic-identitet (X25519), PKI (AES-256-CCM) og XEdDSA-signaturer |
| `docs/meshtastic_protokol.md` | Clean room-fakta om Meshtastic-protokollen (ingen kode); vores kode skrives herfra |
| `src/net/ble_serial.*`, `src/net/mc_companion.*`, `src/ui/ble_ui.h` | BLE + MeshCores companion-protokol til MeshCore-appen |
| `src/storage/sd_crypt.*`, `src/ui/crypto_ui.*` | Kryptering af hemmelige SD-filer (fra/chip-ID/kode/eFuse) |
| `src/storage/nvs_tool.*`, `src/ui/nvs_ui.*` | Intern NVS: vis/ryd navnerum, launcher-genkendelse |
| `src/storage/profile_io.*`, `src/ui/profile_ui.*` | Eksport/import af profilen (adgangskode-krypteret fil) |
| `src/ui/contact_colors.*`, `src/ui/colors_ui.*` | Farve pr. kontakt (fra nøglen, kan vælges) |
| `src/ui/perms_ui.*` | Telemetri-tilladelser pr. kontakt (T/P/M i kontaktflag) |
| `src/net/mqtt_bridge.*`, `src/ui/mqtt_ui.h` | MQTT-bro (esp-mqtt): rx/tx/status/result-emner, Indst. > MQTT |
| `src/ui/panel.h` | Underskærme åbnet fra Indst. og deres fælles datamodel (`PanelView`) |
| `src/ui/lv/lv_port.*`, `src/ui/app_ui_lv.cpp` | LVGL (fase 4): skærmdriver over delt SPI og al tegning (faner, Chat, lister, underskærme) |
| `src/ui/lv/fonts/`, `fonts/montserrat/` | GENERERET Montserrat (OFL) med æøå af `tools/gen_lvgl_font.py` (FreeType, `C:\Claude\tools\fontvenv`) |
| `assets/logo/` | DualMesh-logoet (SVG-master, PNG, firmware-headere, scripts; README) |
| `src/diag/crash_info.*` | Seneste nedbrud fra ESP-IDF's core dump i flash; `[CRASH]`-linjer ved opstart efter et nedbrud |
| `lib/dualmesh/src/diag_report.*` | Nedbrudsresumé og GitHub-issue-linket til QR-koden (hardware-uafhængig, test i `host/tests/test_report.cpp`) |
| `src/ui/report_ui.h` | Indst. > System > "Fejlrapport": seneste nedbrud + stor QR-kode til et udfyldt issue |
| `docs/rapport/` | Fejlrapport-siden (GitHub Pages): henter log over WebSerial eller fra filer, renser den i browseren (`scrub.js`), opretter issue |
| `tools/gen_scrub_vocab.py` | Laver `docs/rapport/vocab.js` (firmwarens egne ord) og `corpus.js` (testkorpus) ud fra kildekoden |
| `.github/ISSUE_TEMPLATE/fejlrapport.yml` | Issue-skabelonen; felt-id'erne `version`, `opstart`, `nedbrud` udfyldes fra QR-koden og siden |
| `src/ui/logo/dualmesh_logo_tdeck.h` | Kopi af logo-headeren (RGB565): opstartslogo i fuld skærm (`pushRect565` lige efter skærm-init, før ventetiden på USB-terminalen), mindst 5 s (`AppUi::holdSplashUntil`; ønske: reklame i videoer om firmwaren, men må ikke genere). Vises ved 0,84 s |
| `host/` | Simulator (`sim/`) og test (`tests/`); `sim_resultat.txt` er seneste simulering |

**Lagring (besluttet 15. nov 2026):** alt der skal overleve en genstart, ligger på
SD-kortet, ikke i intern NVS/flash: indstillinger, lærte radioværdier og senere beskeder, kontakter,
nøgler og kortfliser. Kode og data, der bruges løbende, bliver i flash/RAM (SD er langsommere og
deler SPI-bus med radioen). Brug `SdStore` (ikke Arduinos `SD.h`, som vil initialisere bussen
igen). Skriv sjældent og kun ved ændring; filer skal være læsbare og skrives atomisk (tmp + .bak).
Uden SD-kort skal alt virke, blot uden at gemme. Den interne NVS (0x9000) har data fra
tidligere firmwares på T-Deck'en (målt 15. nov 2026: 504 af 630 poster brugt, 35 navnerum);
ryd den aldrig automatisk, og læg ikke egne data dér. Undtagelse (tilladelse 15. nov
2026): ESP-IDF's egne Bluetooth-parringsnøgler og WiFi-PHY-kalibrering må ligge i NVS.
NVS kan ryddes i Indst. > System > "Intern NVS" (`storage/nvs_tool.*`,
`ui/nvs_ui.*`): forbrug, launcher-genkendelse (anden app i factory, eller "launch" i navnet i en
anden app-partition), "Ryd Bluetooth-parringer", "Ryd WiFi- og radiodata" (nvs.net80211 + phy),
ét navnerum ad gangen (to klik; andres markeres "anden fw"), og "Ryd HELE NVS" (skriv SLET ALT;
advarer om launcherens indstillinger). Genstart efter rydning af Bluetooth/WiFi/hele NVS.

**SD-kort og launcher-kompatibilitet:** alle DualMesh-filer ligger i `/dualmesh/` på kortet.
"Nulstil DualMesh" (`SdStore::resetDualMesh`) sletter kun den mappe, men bevarer `eksport/` og
kortpakkerne `maps/*.pmtiles` + `*.pmtiles.part` (ønske 23. nov 2026; prikker i maps/ slettes).
Bekræftelsesordene (NULSTIL, SLET ALT, BRUG EFUSE) oversættes (en: RESET, ERASE ALL, USE EFUSE);
det danske ord virker altid. En launchers firmwarefiler og
andre mapper røres aldrig, og intern NVS/flash røres heller ikke. Brug ikke en egen
partitionstabel til data: under en launcher bestemmer den layoutet. Formatering
(`SdStore::formatCard`) sletter hele kortet: et helt tomt kort (ingen boot-signatur) formateres
automatisk; alt andet (exFAT, NTFS, FAT32 som FatFs afviser, ...) kun efter bekræftelse.
ESP-IDF's FatFs har ikke exFAT (`FF_FS_EXFAT 0`) og kræver ≥ 65 525 klynger for FAT32.
I menuen: Indst. > System > "Nulstil DualMesh" (`ui/reset_ui.*`): vælg "Eksportér profil og
nulstil" (adgangskode, filen kontrolleres) eller "Nulstil uden eksport", bekræft med NULSTIL +
Enter, genstart straks. Seriel monitor: `R` + `JA` = nulstil DualMesh,
`F` + `FORMATER` = formatér kortet. GUI'en skal kalde de samme funktioner med en bekræftelsesdialog.

**Opstartsstabilitet → GUI:** `RadioScheduler::settleState()` giver `Calibrating` (≈ 5 s: måler
reelle tider), `AdaptingCad` (CAD-tærsklen stiger stadig; flere falske alarmer og mindre lyttetid,
typisk 1–2 min uden gemt tærskel) eller `Stable`. Brugerfladen (fase 4) skal vise "stabiliserer…"
indtil `Stable`. Alt nyt, der er ustabilt lige efter opstart, skal meldes på samme måde.

**Lokation og privatliv (krav 15. nov 2026):** egen position sendes kun, hvis brugeren
har slået det til (pr. net og gerne pr. kanal/kontakt), og standard er fra. Andre noder må aldrig
kunne hente positionen uden tilladelse: Meshtastic-positionsforespørgsler (Position med
`want_response`, NodeInfo-svar) og MeshCore-adverts/telemetri-forespørgsler skal respektere
indstillingen, og vi svarer ikke med position, når den er slået fra. Gælder også BLE-apps og
MQTT-broen. Overvej en reduceret præcision som mellemvej.

**MeshCore-integration (fase 2):** MeshCore v1.18.0 (`companion-v1.18.0`, 81dbae5) ligger som
udvalgte filer i `lib/meshcore`; MeshCores egen biblioteksbuild bygger alle deres board- og
radiodrivere og kræver deres `target.h`, så den bruges ikke. Ret aldrig i `lib/meshcore`; opdatér
med `python tools/sync_meshcore.py <tag>` og tilføj filer til listen i scriptet. Afhængigheder:
rweather/Crypto og densaugeo/base64. Adapteren melder `isReceiving()` = optaget, mens radioen
lytter på det andet net, så MeshCores Dispatcher venter i stedet for at time ud (den giver op
efter 1,5 × sendetid). Identitet: `/sd/dualmesh/meshcore/identity.txt` (hemmelig, MeshCores
binære format som hex). Navn: `meshcore_name` i settings.ini (tom = `DualMesh-XXXX`).
Kontakter (350) ligger i intern RAM (+73 KB); flyt til PSRAM, hvis RAM bliver knap.

Kernen i `lib/dualmesh` må ikke afhænge af Arduino/ESP-IDF, så den kan bygges og testes på PC.
Ændringer i scheduleren skal køre gennem simulatoren og sammenlignes med `host/sim_resultat.txt`.

## Scheduler (kort)

EDF-scheduler: for hvert net beregnes, hvor længe den korteste preamble varer, og dermed en deadline
for næste CAD. Netværket med tidligste deadline får radioen. CAD kører i CAD_RX-tilstand. Er planen
ikke mulig, forkortes den længste CAD automatisk. Profiler med samme kanal slås sammen.
Tilstande: `MaxSensitivity` og `LowPower`. Afsendelse bruger LBT og en fælles 10 % duty cycle
(869,4–869,65 MHz).

**Radioindstillinger (globalt, indsamlet 15. nov 2026):**
- Meshtastic (`presets_meshtastic.*`): 34 regioner + 3 ikke-listede enum-værdier (UA_868, EU_874,
  EU_917; ingen kendt båndplan, giver aldrig en profil) og 16 modem-presets (+ forældet
  VERY_LONG_SLOW). Kilde: meshtastic.org's LoRa-dokumentation og frekvensberegner samt
  protobuf-enum-værdierne. Kanalfrekvens = båndstart + BW/2 + padding + slot·(spacing + 2·padding
  + BW); slot = regionens faste slot eller djb2(kanalnavn) % antal slots (unavngiven kanal =
  presetnavnet, fx "LongFast"). Sync 0x2B, preamble 16 (12 på 2,4 GHz). EU_868/EU_866/EU_N_868 har
  hver deres presets (Lite = EU_866, Narrow = EU_N_868; ingen Turbo i EU). Testet mod kendte
  standarder: EU 869,525, US 906,875, ANZ 919,875 MHz.
- SX1262 kan ikke LORA_24 (2,4 GHz) eller 2 m-båndene (144 MHz); ITU-regionerne kræver licens.
  30 regioner virker på T-Deck'en.
- MeshCore (`presets_meshcore.*`): 27 presets fra den officielle app-API
  `https://api.meshcore.nz/api/v1/config` (`suggested_radio_settings`, se MeshCore
  `docs/radio_presets.md`); nye kan komme til: `/dualmesh/meshcore/presets.txt` på SD
  (`titel;frekvens_hz;bw_hz;sf;cr[;path_hash_size[;forældet]]`, `mc::loadExtraPresets`, test i
  `test_presets.cpp`; samme titel erstatter, nye lægges til; 22. nov 2026, indlæsning af en rigtig
  fil på hardware ikke afprøvet). Sync
  0x12, preamble 32 ved SF ≤ 8 ellers 16 (MeshCore ≥ 1.16); scheduleren planlægger efter 16 for
  ældre noder. Nogle presets har `path_hash_size` (netværksindstilling til fase 2).
- Valg i `/dualmesh/settings.ini`: `meshtastic_region`, `meshtastic_preset`, `meshtastic_channel`,
  `meshtastic_slot`, `meshcore_preset` (titel som i appen), `meshcore_custom`
  (`frekvens_hz,bw_hz,sf,cr`). Standard: EU_868/LONG_FAST + "EU/UK (Narrow)". `l` i monitoren
  lister alt. Ugyldige værdier giver en log-linje og falder tilbage til EU.
- Global simulering (`sim_resultat.txt`, sidste afsnit): alle 27 MeshCore-presets har mindst én
  Meshtastic-region på samme bånd; 36 forskellige kombinationer, tab 2–10 % ved 5 % belastning
  hver. SF7-presets (USA, Canada, NL, HU m.fl.) får 1-symbols-CAD pga. 16-symbols-antagelsen.
  **Samme kanal:** MeshCore "EU/UK (Deprecated)" = Meshtastic EU LongFast med andet sync word;
  CAD kan ikke skelne dem (~55 % tab). Firmwaren advarer; GUI'en skal advare tydeligt.

## Status (fase 1: radio på hardware)

Målt på T-Deck Plus 14. nov 2026:
- SPI, skærm og sync-register virker for begge profiler. Profilskift ≈ 270 µs fra STDBY_XOSC.
- Scheduleren kører stabilt (watchdog=0). Tidlig afbrydelse ved falske CAD-hits er slået til;
  støj-hits ligger på ca. 0–4 %.
- **Første pakke modtaget:** `[RX MC] TRANSPORT_FLOOD RESPONSE len=36 rssi=-103 snr=-12.8`.
  Der er altså MeshCore-trafik inden for rækkevidde. Meshtastic: endnu ingen pakker.
  Der er også en Heltec LoRa32 v3.1 (mangler LoRa-antennepigtail), der kan bruges som modpart.
- Hul-statistik: `maxhul` = rene huller (kun CAD/idle imellem, skal være ≤ tilladt hul),
  `sene` = antal rene huller over tilladt hul i vinduet, `maxhul-rx` = huller med RX/TX imellem
  (forventet store: et RX-vindue efter en preamble er ≈ 150 ms). `[DIAG]`-linjen viser chippens
  tilstand efter CAD, profilskiftetid, BUSY-ventetid og TCXO-fejl.
- **TCXO-fund:** en CAD slutter altid i STDBY_RC (målt 100 %), så TCXO'en slukker efter hver CAD
  og skal starte igen før næste operation. Med den gamle opstartstid på 5 ms kostede det ~17 % af
  tiden, og MeshCore havde ~50 % sene huller (maxhul 53 ms). Opstartstiden er nu 1000 µs
  (`Sx1262Pins::tcxo_delay_us`, kan sættes med `-DDM_TCXO_DELAY_US=`); 250–5000 µs er testet uden
  `XOSC_START_ERR`. TCXO-tiden lægges til `cad_fixed_us` i planen.
- **Selvkalibrering** (`SchedulerConfig::calibrate`): de første ~128 CAD'er og profilskift pr. net
  måles; derefter laves planen om med p98-værdierne (profilskift R, CAD-overhead O ud over N
  symboler, separat med/uden forudgående skift). Målt: SX1262-CAD tager ≈ (N+0,9)·Tsym, ikke
  N+0,5; skift p98 ≈ 2,0 ms (snit 1,35 ms inkl. TCXO). Med de rigtige tal forkorter planen
  Meshtastic-CAD'en til 1 symbol, *hvis* den må (se næste punkt).
- **Valg: aldrig under 2 CAD-symboler, medmindre hullet ellers er > 15 % for langt**
  (`min_cad_syms`, `late_tolerance`). Begrundelse: 1-symbols-CAD på Meshtastic SF11 gav 3–8× flere
  falske alarmer og krævede `det_peak` 30 i stedet for 25, så svage rigtige pakker overses.
  Simulatoren: fanger en 1-symbols-CAD kun 90 %, stiger MT-tabet fra 6 % til 16 %, mens 2 symboler
  kun koster MeshCore ~0,6–0,9 procentpoint (kun noder med 16 symbolers preamble). T-Deck/SF8
  ligger ~9 % over → beholder 2/2 symboler; SF7-presettet ligger 39 % over → forkorter stadig.
  Målt: MeshCore planlagt hul ≈ 40 ms mod tilladt ≈ 36,7 ms (`sene` ≈ 160/10 s er derfor forventet
  med denne plan og forklares i `[PLAN]`), Meshtastic 63 ms mod 75 ms, MT-støj 0–3 %.
- Skærmen sender nu i bidder på 512 B (før 2048), det tog toppen af skiftetiden (3,7 → 2,2 ms).
- Lært CAD-tærskel gemmes på SD (`settings.ini`, nøgle `cad_peak_<frekvens>_sf<SF>_bw<BW>`) og
  hentes ved opstart (første gang efter 3 min, derefter højst hvert 10. min, kun ved ændring).
  Afprøvet 15. nov 2026 på SD-kortet (≈ 4 GB, før FAT32 fra `mkdosfs` med for få klynger,
  formateret med tilladelse via `F` + `FORMATER`: 2,7 s). Gem: 22 ms. Efter genstart
  hentes tærsklerne (MT 27, MC 22), og tilstanden er `stabil` efter ~20 s i stedet for 1–2 min.
  `R` + `JA` sletter `/dualmesh/` (185 ms); næste opstart starter med standardværdier.
  Under formateringen faldt MeshCore-CAD'erne ~20 % i det 10-s-vindue (forventet).
- `settleState()` er rettet til kun at beskrive opstarten (`settled` sættes ved første rolige
  støjvindue og nulstilles ikke); før skiftede den hele tiden mellem stabil og "tilpasser CAD".
- Simulatoren har en T-Deck-timing-model (`SimParams::tcxo_us`, `cad_extra_syms`, `p_detect_1sym`)
  og et afsnit, der sammenligner kalibrering og symbolgulv. `sim_resultat.txt` er regenereret
  14. nov 2026 (GCC 14).

## Status (fase 2: MeshCore)

- Trin 1+2 (15. nov 2026): MeshCore-klient modtager og sender via scheduleren. Én advert uden
  position sendt (1166 ms sendetid). Rå MeshCore-pakker modtaget (2 DM'er mellem andre på 30 min).
  **Ikke set endnu:** afkodning af andres adverts/kanalbeskeder, og om andre hører os. En pigtail
  skaffes til Heltec'en (MeshCore companion + app) til at teste begge veje.
- Trin 3 (15. nov 2026): tekst-UI med tastatur og trackball. Trackball venstre/højre skifter
  skærm (Status/Chat/Kontakter), op/ned ruller/vælger, klik på kontakt = DM; man skriver bare, Enter
  sender, tom Enter fortryder. Trackball-indstillinger i settings.ini (`trackball_sensitivity` 1–10,
  `trackball_invert_x/y`, `trackball_click_delay_ms`). Kontakter gemmes på SD (`contacts.txt`).
  Skærmen sender kun ændrede linjer (linjecache). Verificeret: tastatur fundet, ingen nedbrud,
  radioen uændret. **Ikke verificeret: hvordan UI'et ser ud og føles – spørg brugeren.**
  ESP-IDF logger én harmløs linje "GPIO isr service already installed" ved opstart.
- **Tastatur med specialtegn (ønske 15. nov 2026):** skriver man en tast med varianter,
  vises de i en bjælke over indtastningen (fx a → å æ á à ä …; det valgte tegn gror op forstørret); trackball bladrer (tegnet skiftes
  straks), tryk på skærmen vælger, næste tast/klik bekræfter. Layouts er filer i `keyboards/`
  (`<tast>=<varianter>`), bygget ind med `tools/gen_keyboards.py` (tjekker at fonten har tegnene);
  aktive layouts i `keyboard_layouts` (standard `da,latin`); installerede layouts ligger i
  `/sd/dualmesh/keyboards/<id>.kbd` og går forud for indbyggede. Overlayet virker også i Indst.
  (Navn/Langt Navn, Kort Navn, Testbesked tekst) og i kanalnavnet under "+ Tilføj"
  (`SettingsUi::textField`, `ChannelsUi::textField`); tekniske felter forbliver ASCII. Overlayets
  række skjules for andre tegninger (`StatusDisplay::setHiddenRow`/`lineOver`, ingen blink).
  **Mangler:** at hente/installere
  layouts fra GitHub via appens indstillinger (kræver WiFi og indstillings-UI; `keyboards/index.txt`
  er kataloget). Repoet er privat, så rå GitHub-URL'er virker ikke fra enheden: kataloget skal
  ligge et offentligt sted (offentligt repo eller GitHub Pages) – spørg brugeren.
- Touch (GT911) fundet; orientering efter LilyGo (swap XY, spejlet Y), kan rettes med
  `touch_swap_xy/invert_x/invert_y`. Tryk på fanerne skifter skærm. Ikke kalibreret mod virkeligheden.
- **Tastatur (afprøvet 16. nov 2026):** rå tilstand (5x7-matrix, LilyGo-kommando 0x03);
  vi omsætter selv taster til tegn med LilyGos tabeller (Sym = tal/symboler, Shift = store bogstaver).
  Alle taster inkl. Shift/Alt/Sym/Mic, trackball og touch tænder tastaturlyset; slukker efter 10 s
  (`keyboard_backlight`, `keyboard_backlight_timeout_s`). Ældre tastaturfirmware uden rå tilstand
  opdages (0xFF-svar) og falder tilbage til tegntilstand; `keyboard_raw_mode=0` tvinger det.
  Lærdom: læs klokken FRISK efter I²C-input, før tider sammenlignes (uint32-overløb gav "blink").
- **Skærmtekst:** alt bruger proportional Montserrat (14 px, sekundært 12 px); lange tekster
  klippes med "…", så hold etiketter korte og brug `%.Ns` på navne. Skærmtekster må bruge æøå.
- Testbesked med fast interval, **kun i en DM eller privat kanal** (17. nov 2026; aldrig
  Public/primær, #hashtag-kanaler eller Meshtastic-kanaler med standardnøgle; `convIsPrivate`).
  Slås til/fra med tandhjulet i samtalen (samtalen i `test_conv`, til/fra i `meshcore_hourly_test`);
  `T` i monitoren skifter i den sidst valgte samtale; en gammel opsætning uden samtale slås fra ved
  opstart. Gemmes i samtalen (krav 16. nov 2026: alt, man
  selv sender, skal stå i Chat, ellers kan man ikke se, hvad der svares på). Tekst og interval
  står under Beskeder: tekst `meshcore_test_text` (tom =
  "Test afsendelse fra DualMesh firmware, er der hul igennem?", højst 120 tegn) og interval
  `meshcore_test_interval_min` (2–1440 i trin på 1, standard 15; minimum 2). **Højst 10 % airtime (krav 15.
  nov 2026):** Gem afvises, hvis intervallet er under 10 × beskedens sendetid med den aktuelle
  MeshCore-radio (`testAirtimeMs`: header, path, hash, MAC og AES-blokke; beregnet 1034 ms mod
  målt 1035 ms), og ved afsendelse forlænges perioden til mindst 10 × sendetid. Schedulerens
  fælles 10 %-duty cycle gælder desuden for al afsendelse. Første efter genstart sendes efter
  1 min. Stopper (og gemmer FRA), når en anden skriver i testsamtalen.
- **Navigation i tre lag (ønske 22. nov 2026, `AppUi::tab_focus_`, `chat_parent_`, `goBack`):**
  Chat er ikke længere en fane, men en underside af Kontakt og Kanal. Fanelinjen har en markør
  (aktiv fane vendt om: sort med fanelinjens farve): venstre/højre vælger fane, ned/klik/Enter går
  ind i listen. I listen: kun op over øverste række (Kanal: over filteret; Indst.: over første
  overskrift) går til fanelinjen - venstre hopper ikke selv dertil; i Indst. folder venstre den
  åbne gruppe sammen (`SettingsUi::collapse`, markøren på overskriften). Højre/klik/Enter går ind
  (Kanal/Kontakt: chatten; Indst.: som klik). Faneorden Kanal | Kontakt | Indst. (`kFirstTab`). I chatten: venstre eller Backspace i et tomt felt = tilbage til listen (Kanal:
  markøren bliver på kanalen); fanen, chatten kom fra, står som aktiv. Venstre med skrevet tekst
  gemmer den som **kladde pr. samtale** (6 i RAM, `draftSave/draftLoad`; også ved samtaleskift
  og tryk på en fane) og henter den, når samtalen åbnes igen. Touch: tryk på en fane = ind i
  dens liste. **Holdt Backspace** gentages (450 ms, derefter hvert 60. ms) med en egen kode
  (`Keyboard::kBackspaceRepeat`), der kun sletter tekst (chat, tekstfelter i Indst., kanalnavn/
  -nøgle) og aldrig bekræfter slet kontakt/kanal eller går tilbage; ikke i underskærmene.
  Set og godkendt 22. nov 2026, også sammenfoldning med venstre og faneordenen.
- **Tandhjulsmenuer (17.-22. nov 2026, `AppUi::openUserMenu/openChanMenu/menuRun`, tegnes i
  `lvDrawMenu`):** tandhjul til højre for andres bobler i en kanal (afsenderen: Nævn @, Skriv DM;
  Meshtastic-node også Traceroute og Bed om position/telemetri/NodeInfo - svaret skrives i DM'en,
  `MtNode::requestInfo`) og foran titlen (kanal: Vis nøgle, Testbesked, Ryd chatten; DM: de samme
  node-forespørgsler, Testbesked, Ryd samtalen; Ryd kræver to klik). Kun punkter, der kan bruges,
  vises. Touch: tryk på tandhjul/titel; trackball: klik i Chat, når man står ved de nyeste
  beskeder (første klik hopper dertil), op/ned vælger, klik/Enter udfører, sidelæns/Backspace lukker.
  Set og godkendt på skærmen 22. nov 2026. **Afprøvet mod Heltec'en (Meshtastic 2.8.2) 22. nov
  2026:** telemetri besvaret (efter rettelse: bufferen til `encodeData` var for lille), position
  korrekt afvist (NAK 8), NodeInfo ikke besvaret (Heltec'en havde lige sendt sin); testbesked i
  DM sendt, kvitteret og stoppet ved svar; SOS med 1 dBm modtaget på Meshtastic (tekst med BEL,
  position, stop). Heltec'ens 3 telemetri-svar (rettet 22. nov 2026): next-hop-svar skal kvitteres
  med en ACK med hop_limit 0, og 2.8 smider pakker med hop_start 0 uden `Data.bitfield` væk; vi
  sender nu altid bitfield. (DMtest manglede på T-Deck'en og blev tilføjet igen med `C`.)
  **MeshCore-siden (22. nov 2026, Heltec med MeshCore companion 1.18.0):** testbesked i den private
  kanal MCtest modtaget, SOS-advert med præcis position modtaget (SOS-teksten på Public ikke set).
  Stop ved svar ikke afprøvet: Heltec'ens MeshCore-pakker når T-Deck'en med RSSI −77, SNR −15 og
  CRC-fejl uanset dens effekt (2/10/22 dBm), CR og ±10 kHz, mens T-Deck→Heltec giver SNR +12 og
  Meshtastic på samme Heltec −36 dBm. Fejlen ligger i Heltec'ens sendevej under MeshCore, ikke hos os
  (17. nov: +7,2 dB). `[RXFEJL]` i loggen viser header-/CRC-fejl med RSSI/SNR/frekvens.
  **Årsag fundet 23. nov 2026 (Heltec'ens sender driver under lange pakker):** SX1262's
  FreqError-register (`Sx1262Hal::freqErrorHz`, `[FREKV]` for gode pakker, `frekvensfejl=` i
  `[RXFEJL]`) viser +560-610 Hz ved både 250 og 62,5 kHz, altså ingen fast forskydning. Ved
  62,5 kHz kommer korte DM'er (22 byte, ~0,4 s) fejlfrit (RSSI -42, SNR +12), mens adverts
  (112 byte, 1,1 s) får RSSI -77/SNR -15 og CRC-fejl: frekvensen glider ud af det smalle filter
  undervejs. Ved 250 kHz er der plads nok (advert RSSI -42, SNR +12). Samme fejl med MeshCore
  1.17.1 og 1.18.0, og også med Meshtastic slået fra (ikke tidsdelingen). T-Deck→Heltec virker
  (også lange pakker). Ikke vores fejl; test MeshCore mod Heltec'en med korte beskeder eller
  250 kHz.
  **App- og MQTT-test ved 250 kHz (23. nov 2026, MeshCore-appen over BLE via Telefonlink):**
  kanal (privat MCtest2) og DM fra appen modtaget af Heltec'en ("Delivered"), telemetri-
  forespørgsel (Heltec'en skal have `telemetry_mode_base 2`), favorit, adverts (nul hop/flood),
  Discover (ingen repeatere = 0), MeshCore-rx over MQTT (DM + kanal). **Rx Log** i appen virker nu
  (`McNode::logRxRaw` -> `McCompanion::onRxRaw`, PUSH 0x88: SNR x 4, RSSI, rå pakke). **Støjgulv
  (23. nov 2026, `Sx1262Hal::noiseFloorDbm`):** ved et CAD-hit (CAD_RX, chippen lytter) læses
  GetRssiInst (0x15); kommer der ingen præambel, var hittet støj, og prøven tæller (som MeshCore:
  kun under gulv + 14 dB, snit af 32, mindst -120, nulstilles efter 64 afviste i træk). Pr.
  frekvens; ca. én runde i minuttet; `[STOEJ]` hvert minut i loggen; appens Noise Floor viser
  MeshCores. Målt ved skrivebordet: MeshCore -105 dBm (62,5 kHz), Meshtastic -100 dBm (250 kHz),
  radiohuller uændrede. Prøverne kommer fra falske alarmer og ligger måske lidt højt; med kun
  ét net (RxContinuous, ingen CAD) måles intet (0). Tabsmåling 35/40 ved 250 kHz
  (planen siger selv "IKKE GARANTERET": tilladt hul 8,1 ms mod planlagt 27 ms), ikke
  repræsentativt for EU/UK (Narrow), som Heltec'en ikke kan sende stabilt. En besked lige før
  MQTT er forbundet går tabt (ingen kø).
- **Log på SD (ønske 15. nov 2026, så T-Deck'en kan tages med uden PC):** alt der
  logges med `dm::DLOG.printf/println` (`src/storage/sd_log.*`; brug den, ikke `Serial.print`)
  går både til USB og til `/sd/dualmesh/logs/NNNN.log` (én fil pr. opstart, ny ved 4 MB, højst
  100 filer, ældste slettes; hver linje har oppetid TT:MM:SS.mmm og første linjer har
  opstartsårsagen). Skrives hvert 30. s / ved 16 KB i bidder på 512 B med 1 tick pause imellem,
  så radioen kommer til (delt SPI-bus); filen oprettes og første data skrives i `begin()`, før
  radioen starter. Målt 15. nov 2026: første skrivning til en ny fil 30 s efter opstart tog
  37–149 ms i ét stræk og gav profilskift på 122 ms og radiohuller på 164 ms; efter rettelsen
  højst 8,5 ms skift og 68 ms MT-hul (tilladt 75) på 3½ min;
  strømsvigt mister højst ~30 s. Opstartsårsag BROWNOUT ses, når T-Deck'en slukkes og tændes med
  afbryderen (spændingen falder langsomt; bekræftet 15. nov 2026) – ikke en fejl i sig selv. `L` = skriv nu, `D` = send alle logfiler over USB. Hent dem med
  `python tools/fetch_logs.py [COM5] [logs/sd]` (genstarter T-Deck'en; hentede filer springes over).
  Ca. 250 KB pr. time. `[STROEM]` i hver statistik og række 1 på skærmen viser batterispænding
  (GPIO4 ×2) og USB/batteri; med USB målt 4,54 V (ikke kalibreret mod et voltmeter).
- **Skærmslukning (ønske 15. nov 2026):** på batteri slukker skærmen (DISPOFF +
  baggrundslys GPIO42) efter `screen_timeout_s` (standard 20, 0 = aldrig) uden input. Tast,
  trackball, touch, en modtaget kanalbesked/DM eller USB til en PC tænder den. USB genkendes på USB-controllerens SOF-rammetæller (tæller hvert ms, når en vært er tilsluttet, også uden åben terminal; `bool(Serial)` hang fast i opstartstilstanden), en ren oplader på spænding > 4,35 V; skift logges som `[STROEM] strømkilde nu: …`. Når USB tages ud, starter nedtællingen forfra (dæmp efter 15 s, sluk efter 20 s); "lader" kræver to høje målinger i træk og regnes ikke de første 60 s efter en PC er koblet fra (afprøvet og godkendt 15. nov 2026). Det vækkende input
  bruges kun til at tænde (intet vælges i blinde). Med USB slukker den ikke
  (`screen_timeout_usb=1` ændrer det). Slukket skærm tegnes ikke (sparer SPI-tid).
  `[UI] skaerm slukket/taendt` står i loggen. 
  5 s før den slukker, dæmpes den til trin 2 af 16 (`StatusDisplay::setDimmed`); input lysner
  den igen (dæmpning, slukning og opvågning afprøvet og godkendt 15. nov 2026).
  Lysstyrke `screen_brightness` 1–16: baggrundslysets LED-driver tæller ét trin ned
  pr. kort lav puls på GPIO42 (fra 1 rundt til 16), lav > 2,5 ms = slukket.
- **Indstillingsmenu (fanen "Indst.", ønske 15. nov 2026):** `src/ui/settings_ui.*`.
  Viser alle brugerindstillinger; ændringer gælder først efter **Gem** (række 14, sammen med
  **Annuller**). Gem tjekker Meshtastic-region/preset og `meshcore_custom`, skriver til SD og
  kalder `onMenuSaved` i main.cpp: UI/tastatur/touch/navn/afsendelse/testbesked anvendes med det
  samme; radioindstillinger (markeret "genstart") giver genstart efter 2 s. Nye indstillinger
  skal tilføjes til `SettingsUi::kDefs` (etiket ≤ 17 tegn, værdi ≤ 21 tegn) **og have en
  forklaring i `kHelp` (≤ 160 tegn og højst 4 linjer à 40 efter ombrydning ved mellemrum, vises i række
  9-12 under listen; gerne med et konkret eksempel som "Eksempel: 869618000,62500,8,8"; ønske
  15. nov 2026)**.
  **Grupper (krav 15. nov 2026, gælder al fremtidig udvikling), i denne rækkefølge:**
  Status (statusskærmen, tidligere en fane), WiFi, Bluetooth, GPS, Profil, Beskeder, MeshCore,
  Meshtastic, MQTT, Skærm, Tastatur og trackball, System ("Strømstyring" fjernet efter
  ønske; Beskeder flyttet op over MeshCore og MQTT ned under Meshtastic 16. nov 2026).
  **Lyseblå (`color::Cyan`/`color::Select`) bruges KUN til fanelinjen og valgbjælken** i hele
  UI'et (krav 15. nov 2026); aldrig som tekst- eller gruppefarve.
  **Kort (ønske 16. nov 2026, `StatusDisplay::setCard`):** i specialtegn-bjælken gror
  det valgte tegn op som et forstørret kort (godkendt); grundtasten vises ikke i
  bjælken. Prøvet og rullet tilbage: listevalg som boks over 3 rækker (skjulte naborækkerne) og
  den aktive fane som kort, der popper ned ("ser dumt ud"). Fanelinje og listevalg er bjælker. Fanerne er nu Kanal |
  Kontakt | Indst. (Kanal-listen er startskærmen; se "Navigation" nedenfor); Status åbnes som underskærm fra toppen af Indst. – hver
  med egen farve (overskrift med farvet baggrund, indstillinger i farven) og en tom linje
  imellem. En ny indstilling lægges i gruppen for den ting, den hører til, også strømstyring,
  der kun gælder én ting (skærmens sluk-tid under Skærm, tastaturlysets tid under Tastatur). Touch ligger under Skærm. Grupper uden indstillinger
  viser "(ingen indstillinger endnu)". Nye funktioner kan få deres egen gruppe (`Group`-enum +
  `kGroups`). Etiketter bruger hver protokols egne begreber (fx "Primær kanal", "Preset"). Set og godkendt 15. nov 2026. **MeshCore står altid før Meshtastic** i menuer, lister og på
  statusskærmen (princip). Profil (fælles for begge net): "Navn/Langt Navn" (nøgle
  `meshcore_name`: MeshCores navn og Meshtastics long name) og "Kort Navn"
  (`meshtastic_short_name`, højst 4 tegn, tom = automatisk; bruges først fra fase 3).
- **Langsomme pauser (`[TRAEG]`):** hver statistik har pr. 10-s-vindue `vindue: maxhul=… kl.
  TT:MM:SS.mmm` (pr. net) og `vindue-max` for profilskift; ved et rent hul > 1,5 × tilladt eller
  et skift > 10 ms skrives én `[TRAEG]`-linje med tidspunkter for hul, skift og længste SD-skridt,
  så årsagen kan matches med andre linjer i loggen. `maxhul` (siden opstart) er uændret.
- **Kanaler i nærheden: FJERNET 17. nov 2026 (ønske).** Scanneren (hørte kanaler ud fra
  pakker, ordliste med navnegæt, `channel_names.txt`, monitor `K`) er væk: hverken MeshCore eller
  Meshtastic har det officielt; der er Public/primær og de kanaler, man selv tilføjer.
  `ChannelScan` (navnet bevaret) holder kun vores Meshtastic-kanaler; `k` lister dem.
- **Fanen "Kanal" (ønsker 15. nov 2026):** række 1 = filter; når fanen vælges, står
  markøren på første kanal (ikke filteret); venstre/højre følger navigationen i tre lag. Filteret
  viser "Vis: Alle  (Enter for filter)"; klik/Enter starter valget ("< Alle >", venstre/højre
  skifter), klik/Enter afslutter. Listen: vores MeshCore-kanaler, vores Meshtastic-kanaler
  (primær + tilføjede) og "+ Tilføj MeshCore-/Meshtastic-kanal". `[n]` = ulæste,
  gul. Klik på vores kanal = åbn i Chat; "+ Tilføj": navn, derefter nøgle (MC: "#navn" springer nøglen over; tom
  nøgle = ny tilfældig, vises som "Nøgle: …" så den kan deles. MT: tom PSK = standard AQ==;
  base64 af 1/16/32 byte). Tom Backspace går tilbage/fortryder. **Slet (17. nov 2026): Backspace to
  gange** på en kanal (ikke Public/primær); Chat skifter til Public/primær, hvis den slettede
  stod åben, og retter ellers sit kanalindeks. MeshCore-kanaler lægges med `setChannel` på første
  ledige plads (MeshCores `addChannel` har en privat tæller, der ikke kender sletninger), og
  listen samles, når appen sletter med et tomt navn. Beskederne bliver på SD. MeshCore-kanaler: `/sd/dualmesh/meshcore/channels.txt` (højst 40 inkl.
  Public); Meshtastic: `/sd/dualmesh/meshtastic/channels.txt` (højst 8 inkl. primær). Begge
  filer er hemmelige (nøgler). Meshtastic-beskeder afkodes af `MtNode`.
- **Slet kontakt (17. nov 2026):** Backspace to gange (inden 4 s) i Kontakt-fanen; spørgsmålet
  står i titellinjen. MeshCore: `removeContact` + contacts.txt gemmes straks; Meshtastic:
  `MtNode::removeNode`. Begge kommer igen ved næste advert/NodeInfo. DM-historikken bliver.
  Sletning af kanaler og kontakter afprøvet og godkendt 17. nov 2026.
- **Samtaler og beskedlager (ønske 15. nov 2026):** Chat viser én samtale (start:
  MeshCore #Public); den vælges i Kanal- eller Kontakt-fanen (klik/Enter på en kontakt åbner
  DM'en), og skrevet tekst sendes dertil. Hver samtale gemmes på SD i
  `/sd/dualmesh/msgs/<id>.txt` (`mcc_<fnv32 af nøgle>`, `mcd_<6 byte offentlig nøgle>`,
  `mtc_<fnv32 af navn+nøgle>`), én tab-separeret linje pr. besked (unix-tid, art, status, hop,
  afsender, tekst). Ved skift indlæses de seneste `msgs_per_channel` / `msgs_per_dm` (standard
  200, 10–1000, i menuen); filen beskæres ved åbning og når den bliver for stor. Sendte DM'er
  gemmes, når status er endelig (leveret/fejl). Uden SD: kun ringbufferen (100). Ulæste tælles
  i RAM (nulstilles ved genstart): `[n]` foran kontakter og kanaler, `*` efter fanenavnet; ulæste
  vækker skærmen (også Meshtastic). Monitor `M` = selvtest (605
  beskeder skrives, 200 vises, filen beskæres, ulæste nulstilles; bestået 15. nov 2026; burst-
  skrivningen gav et radiohul på 89 ms, normal brug er én skrivning pr. besked).
- **Ur og beskedtider (ønske 15. nov 2026):** Chat viser klokkeslæt ("TT:MM", "--:--"
  ukendt) først på hver linje. Som i Messenger står en centreret datolinje ("I dag, 15. november",
  "I går, …", "lørdag 8. november", årstal hvis ikke i år) over dagens første besked, og række 1
  er en fastfrosset datobjælke med dagen for den øverste synlige linje (skifter ved scroll). Der er intet RTC/GPS endnu:
  `McNode::noteRemoteTime` samler afsendertid fra adverts, DM'er og kanalbeskeder og sætter uret,
  når 3 forskellige afsendere er enige inden for 2 min (median; tider før 2025 ignoreres; én gang
  pr. opstart, logges som `[UR]`). Indtil da gemmes beskeder med ts=0 og vises ud fra oppetiden,
  når uret bliver sat. Tidszone: `timezone` i settings.ini (POSIX, standard
  `CET-1CEST,M3.5.0,M10.5.0/3`). **Ikke set med rigtig trafik endnu.** GPS-tid kommer i fase 7.
- **WiFi (ønske 15. nov 2026):** Indst. > WiFi > "WiFi-netværk" åbner en skærm med
  status, til/fra, scan, "skjult netværk" og listen (* = gemt, > = forbundet; klik = forbind,
  kode spørges om; klik to gange på det forbundne = afbryd; Backspace to gange = glem).
  Gemte net ligger i `/sd/dualmesh/wifi.txt` (HEMMELIG), `WiFi.persistent(false)`. Med WiFi til
  forbindes automatisk til det stærkeste gemte net (skjulte prøves direkte), nyt forsøg hvert
  minut. Tid hentes med SNTP (`wifi_ntp`, `ntp_server`, standard pool.ntp.org). Scannet tager
  ≈ 6,8 s her; Arduinos timeout er 20 × ms pr. kanal, så vi bruger 500 ms (300 gav altid -2).
  Afprøvet 15. nov 2026: forbinder til et kendt net, NTP ok, radiohuller uændrede.
  **Beacon-tid:** et beacon har kun routerens oppetid (TSF), ikke klokkeslæt; tid uden
  forbindelse kan ikke fås fra WiFi (forklaret).
- **Ur:** `wallclock` (GPS > NTP > app > mesh; en dårligere kilde overskriver aldrig en bedre).
  GPS sætter tiden ved første fix og derefter højst hvert 10. min.
- **GPS (fase 7 fremrykket, 15. nov 2026):** MIA-M10Q fundet på 38400 baud, RX 44 / TX 43 (auto-
  søgning af baud og pinne-retning). Fik fix og tid indendørs. Indst. > GPS > "GPS-status".
  `gps_enabled`, `gps_time`. **Del position** pr. net (`meshcore_share_position`,
  `meshtastic_share_position`: fra/præcis/omtrent ≈ 1 km, standard fra) bruges i MeshCore-
  adverts (`McNode::setPositionFn`); Meshtastic-positionsudsendelse kommer med fase 3.
- **SOS (valgt 15. nov 2026):** hold trackball-klik 5 s (også med slukket skærm) →
  bekræftelsesskærm (Enter = send, anden tast/15 s = fortryd). Aktiv SOS sender hvert
  `sos_interval_min` (standard 5): MeshCore Public "SOS! Har brug for hjælp. Pos lat,lon kl TT:MM"
  + advert med præcis position, og på Meshtastics primære kanal tekst (med BEL \a for alarm)
  + Position (port 3, hop_limit 7). Overstyrer "Del position" mens den er aktiv. Rød fanelinje og
  række 4 viser SOS. "SOS er stoppet" sendes kun, hvis der er sendt mindst én SOS-runde
  (rettet 16. nov 2026: en SOS stoppet under ventetiden på GPS-fix sendte stop-beskeden). Stop: hold 5 s igen + Enter (sender "SOS er stoppet"). `sos_active` i
  settings.ini, så SOS fortsætter efter genstart. Respekterer `meshcore_tx`. Skærmen siger
  "Ikke en erstatning for 112". Aktivering, bekræftelse og venten på fix er set i loggen; selve SOS-beskeden er **ikke sendt
  på hardware endnu** (ville alarmere andre). Første Meshtastic-pakke (stop-beskeden) blev sendt
  15. nov 2026 (684 ms sendetid).
- **Bluetooth / MeshCore-app (valgt 15. nov 2026: MeshCore-appen nu, Meshtastic efter
  fase 3):** Arduino-BLE (Bluedroid, som MeshCore), Nordic UART-service, PIN (standard 123456,
  krypteret i `ble.txt`), navn "MeshCore-<navn>". Protokollen er vores egen implementering efter MeshCores
  companion_radio (MIT): enhedsinfo, kontakter, DM'er/kanaler (kø på 32 i PSRAM), kanaler,
  adverts, tid, batteri, navn, del-position, PIN, genstart. Radio-/effektændringer fra appen
  afvises. Til/fra kræver genstart. McNode ligger nu i PSRAM (intern RAM fri 78 → 147 KB).
  BLE gemmer parringsnøgler i den interne NVS (ESP-IDF/Bluedroid) – tilladt
  15. nov 2026. Afprøvet med appen (meshcore-flutter) 15. nov 2026: parring, opsætning og
  navn virker. Appen bruger også: radiovalg (samme = OK, andet = gem + genstart), path hash
  mode (61) og flood scope (54/63/64) (accepteres, bruges ikke endnu), eksport/import af
  identitetsnøglen (23/24; import gemmes krypteret + genstart), custom vars (40/41: "gps:0/1"
  = T-Deck'ens GPS), auto-tilføj (58/59), repeater-frekvenser (60: ingen). Versionsstrengen skal
  være ren "v1.18.0" (appen slår funktioner fra ved andet format). App-over-WiFi understøttes ikke.
  Egen telemetri (39, len 4): batteri, chiptemperatur og GPS-position (Cayenne LPP, kanal 1);
  andre noders telemetri besvares efter indstillingerne nedenfor. Appens WiFi-skærm bruger CLI'en
  (`bleCli` i main.cpp, tekster som MeshCores companion med WiFi): get/set wifi.ssid, wifi.pwd,
  wifi.enabled, get wifi.status/wifi.ip, set wifi.clear (glemmer alle gemte net), get name, board.
  **Appen må læse WiFi-koden** (besluttet 16. nov 2026: T-Deck'en skal aktivt parres
  med PIN først). "set wifi.pwd" forbinder og husker nettet med det samme (ingen genstart).
  Koder logges aldrig ("set wifi.pwd (skjult)", "appen laeste WiFi-koden"). CLI (66): "reboot", "ver", "clock"; resten svarer
  "Unknown command" som MeshCore, og teksten logges ("[BLE] CLI fra appen: …").
- **SD-kryptering (ønske 15. nov 2026, `storage/sd_crypt.*`, `ui/crypto_ui.*`):**
  wifi.txt, meshcore/identity.txt, meshcore/channels.txt og meshtastic/channels.txt krypteres
  med AES-256-GCM ("DMC1"-header). Nøglekilden vælges i Indst. > System >
  SD-kryptering (`sd_crypto`): fra / chip-ID (standard; eFuse OPTIONAL_UNIQUE_ID + MAC) /
  kode (skrives ved opstart; SHA-256 x 10000 med salt i crypto.txt; MeshCore, kanaler og WiFi
  starter først efter oplåsning, `startNets()`) / eFuse (HMAC_UP-nøgle; en eksisterende tilbydes
  først, ellers kan en ny brændes – UOMVENDELIGT, kræver at skrive BRUG EFUSE – ingen specialtegn, ønske). Skift re-krypterer alle
  filer. Gamle tekstfiler krypteres ved opstart og kontrolleres ved tilbagelæsning. En fil, der
  ikke kan dekrypteres, overskrives aldrig (MeshCore kører så med midlertidig identitet).
  Afprøvet med chip-ID 15. nov 2026 (identitet og WiFi-kode læst fra krypterede filer).
  **eFuse-tilstand afprøvet og virker** (16. nov 2026; loggen viser nøgle brændt i
  blok 0 og filerne krypteret). **Kode-tilstand er ikke afprøvet på hardware.**
- **Sikkerhedsgennemgang (ønske 16. nov 2026), rettet samme dag:** Bluetooth-PIN'en
  ligger nu krypteret i `/sd/dualmesh/ble.txt` (`blePin`/`bleSavePin`, hemmelig indstilling som
  `mqtt_pass`; gammel `ble_pin` i settings.ini flyttes ved opstart og settings.bak overskrives);
  PIN'en logges ikke længere (opstart, Indst., monitor `=`); `logs/` og `*.log` i .gitignore;
  `sdcrypt::shred` overskriver den gamle udgave af en hemmelig fil med nuller før sletning;
  kode-tilstand og profil-eksport bruger PBKDF2-HMAC-SHA256 30.000 runder (`kdf=2` i
  crypto.txt, eksport "DMX2"; gamle "DMX1"/kdf 1 læses stadig, kode-tilstand opgraderes ved
  oplåsning). Egen PBKDF2 med forudberegnede HMAC-tilstande (1,86 s; mbedtls brugte 9,9 s
  for 100.000), tjekket mod mbedtls i `P`-selvtesten. USB-monitoren kan kun `s`/`p`/`h`,
  indtil koden er skrevet (kode-tilstand). Ikke afprøvet: kode-tilstand og PIN-flytning fra en
  gammel settings.ini på hardware.
- **RAM:** WiFi bruger ca. 100 KB intern RAM (DMA-buffere; resten ligger allerede i PSRAM via
  `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`). Med WiFi forbundet er ca. 84 KB intern RAM fri.
  ESP32-S3 har ingen swap/sidefil til SD: kode kører fra flash (XIP via cache), store data
  lægges i PSRAM eller på SD efter behov.
- **WiFi auto-forbind:** ved opstart/WiFi til/tabt forbindelse højst 6 forsøg med 10 s imellem.
  **17. nov 2026 (krav):** det sidst forbundne net prøves først (står øverst i
  wifi.txt), derefter scan + stærkeste gemte net. Et net gemmes først, når forbindelsen er
  lykkedes (forkert kode gemmes aldrig, en gammel rigtig kode bevares). Fejlårsagen fra
  ESP-IDF (`wifi_err_reason_t`) vises på dansk på WiFi-skærmen og i loggen (forkert kode, net
  ikke fundet, ingen IP/DHCP, router fuld, 802.1X ...). **Ikke afprøvet:** forkert kode og
  skift mellem to gemte net.
- **SOS + GPS:** er GPS'en slået fra, tændes den ved SOS (og slukkes igen ved stop); første
  udsendelse venter på et aktuelt fix (højst 3 min, derefter sendes uden aktuel position).
- **Profil-eksport/-import (ønske 15. nov 2026, `storage/profile_io.*`, `ui/profile_ui.*`):**
  Indst. > Profil > "Eksportér profil" / "Importér profil". Én fil med MeshCore-identitet, navn,
  kort navn, MeshCore-kanaler og -kontakter, Meshtastic-kanaler og WiFi-net, krypteret med en
  adgangskode (mindst 6 tegn; AES-256-GCM, "DMX1"), i `/dualmesh/eksport/<navn>-<dato>.dmp`.
  Import finder *.dmp dér og i kortets rod, viser indholdet, erstatter filerne (med den aktuelle
  SD-kryptering) og genstarter straks. Monitor `P` = selvtest (eksport, læs, forkert kode afvist;
  bestået 15. nov 2026). Import afprøvet på hardware 16. nov 2026. "Nulstil DualMesh" bevarer
  eksportmappen. **Efter eksport får den gamle enhed en NY profil** (krav): skærmen
  advarer først; filen kontrolleres (læses tilbage med koden), så slettes identitet og navne
  (`profile_io::retireIdentity`), og T-Deck'en genstarter efter 8 s med nyt nøglepar og
  standardnavn. Kanaler, kontakter og WiFi bliver. (Selvtesten `P` sletter intet.)
- **Adverts (ønske 15. nov 2026), gruppe MeshCore:** "Lokal advert, min" (nul hop =
  kun direkte naboer; 0 = fra, standard fra) og "Flood-advert, t" (hele nettet; 0 = fra, ellers
  mindst 3 t som MeshCores repeatere; standard 12) samt "Send lokal advert"/"Send flood-advert"
  (ét klik). Første lokale 1 min efter opstart, første flood efter 10 min. Position efter "Del
  position"/SOS. Meshtastics modstykke (NodeInfo hver 3. t) kommer med fase 3.
- **Chatgrænser og rydning (Beskeder):** "Dage pr. kanal"/"Dage pr. DM" (0 = alle) – Chat viser
  det antal (Antal pr. kanal/DM) eller den tid, der rammer først; filerne beskæres kun efter antal.
  "Ryd åben chat" og "Ryd alle kanaler" (to klik) sletter filer, RAM og ulæste (`MsgStore::clear`,
  `clearPrefix`).
- **Kontaktfarver (ønske 15. nov 2026, `ui/contact_colors.*`, `ui/colors_ui.*`):** hver
  kontakt har en farve (13 lyse farver, ikke grøn/cyan som vores egne beskeder), afledt af de 6
  første byte af den offentlige nøgle – ny identitet = ny farve. Chat: kanalbeskeder farves via
  afsenderens navn (nyeste kontakt med navnet), '!' foran = flere kontakter med samme navn;
  DM'er via samtalens nøgle. Kontakt-fanen viser navnene i farven. Indst. > Beskeder >
  "Kontaktfarver": vælg farve med venstre/højre, Backspace = automatisk;
  `/sd/dualmesh/meshcore/colors.txt`.
- **Net til/fra (ønske 15. nov 2026):** "MeshCore" og "Meshtastic" øverst i hver gruppe
  (`meshcore_enabled`, `meshtastic_enabled`, genstart; mindst ét skal være til). Et slået-fra
  net får ingen profil i scheduleren (MeshCore-noden startes ikke), og dets indstillinger (+
  "MeshCore-app" under Bluetooth) vises nedtonet (`color::Dim`) og er låst
  (`SettingsUi::lockedBy`, følger den redigerede værdi). Med ét net lytter radioen konstant
  (RxContinuous, ingen CAD) og er `Stable` med det samme (rettet i `settleState`, simulator
  uændret). Afprøvet 15. nov 2026 med kun MeshCore (midlertidig testbuild).
  Kanal-fanen viser ikke et slået-fra nets kanaler, hørte kanaler eller "+ Tilføj"; med ét net
  er der intet filter ("Kun MeshCore (Meshtastic er slået fra)"). Chat åbner Meshtastics primære
  kanal, når MeshCore er fra.
- **Telemetri til andre noder (ønske 16. nov 2026, som MeshCore-appen):** MeshCore-gruppen
  har "Telemetri-svar" (nej/alle/kontakter), "Position i telem." og "Miljø i telem." (nej/ja/
  kontakter), alle standard "nej" (privatlivskravet). Tilstande som MeshCore (0 nej, 1 flag, 2 alle)
  og synkroniseret med appen (SELF_INFO-byten og SET_OTHER_PARAMS). Tilladelse pr. kontakt i
  kontaktens flag (bit 1 T, 2 P, 3 M; bit 0 favorit) via "Telem.-kontakter" (`ui/perms_ui.*`) eller
  appen. `McNode::onContactRequest` besvarer kun REQ_TYPE_GET_TELEMETRY_DATA (3) via `mcTelemetry`:
  batteri + chiptemperatur, position kun med tilladelse og fix, præcision som "Del position".
  Ikke afprøvet med en anden node.
- **Ryd log (menuen, "Indst." nederst):** handling, der kræver to klik inden 4 s; sletter alle
  logfiler og starter forfra med 0000.log (`SdLog::clearAll`).
- Nodenavn: `N` + navn + Enter i monitoren gemmer `meshcore_name` og sender en advert.

- **MQTT-bro (fase 6, ønske 16. nov 2026, fremrykket):** `net/mqtt_bridge.*` bruger
  ESP-IDF's esp-mqtt (egen task; indgående tx lægges i en kø til loop). Gruppen "MQTT" ligger efter
  Meshtastic i Indst.: Brug MQTT-bro, Broker (`mqtt://host:1883`, `mqtts://` med
  `/dualmesh/mqtt_ca.pem` eller IDF's rod-CA'er), Brugernavn, Adgangskode (gemmes KRYPTERET i
  `/sd/dualmesh/mqtt.txt` via `SettingsUi::setSecretFn`, aldrig i settings.ini/loggen; "-" sletter),
  Emne-rod (tom = `dualmesh/<3 byte MAC>`), Må sende fra MQTT (standard fra, højst én pr. 10 s +
  schedulerens 10 %), Status-interval, Position i status (fra/præcis/omtrent). Emner som i
  spec'en: `<rod>/rx/<net>/chan|dm/<url-kodet>`, `<rod>/tx/...` (tekst eller `{"text":...}`;
  MC-DM: navn eller hex-præfiks af nøglen; MT-DM afvises til fase 3), `<rod>/result`,
  `<rod>/status` (retained, LWT `{"online":false}`). Alle nye indgående beskeder kommer via
  `MsgStore::setPushHook`. JSON mangler endnu rssi/snr (Msg har dem ikke). mqtt.txt er med i
  SD-krypteringen og profil-eksporten.
  **Afprøvet 16. nov 2026** mod en lokal broker på PC'en (amqtt i `C:\Claude\tools\mqtt`:
  `broker.py` på 0.0.0.0:1883 uden login, `watch.py` logger `dualmesh/#` til `watch.log`,
  `pub.py <emne> <tekst>`; PC <PC-IP>, rod `dualmesh/<3 byte MAC>`): forbindelse, status hvert
  minut, LWT/offline ved genstart, afvisning (tx fra, ukendt kanal/kontakt/net, MT-DM, for
  hurtigt) og afsendelse på MeshCore Public og Meshtastic LongFast (begge sendt over radioen).
  MQTT bruger ca. 13 KB intern RAM (59 KB fri med BLE). **rx afprøvet 17. nov 2026** med Heltec'en
  (Meshtastic 2.8.2): kanaltekst -> `rx/meshtastic/chan/LongFast`, PKI-DM ->
  `rx/meshtastic/dm/!<nodeid>` med `"from":"!<nodeid>"` (før `mtd_...`; nu samme id som tx bruger,
  `!` url-kodes ikke), og svar via `tx/meshtastic/dm/!<nodeid>` blev LEVERET. Broker
  `mqtt://<PC-IP>:1883`. TLS ikke afprøvet (kræver brokerens CA
  i `/dualmesh/mqtt_ca.pem` på SD); MeshCore-rx over MQTT kræver en MeshCore-modpart.
  **Ny PC (22. nov 2026):** testen virker igen (status/LWT, rx med `rssi`/`snr`, tx-DM leveret).
  Lærdom: Windows Firewall skal tillade indgående TCP 1883 (Python), og WiFi-nettet må ikke have
  klientisolering (gæstenet/hotspot: PC'en ser ikke T-Deck'en, ARP "Unreachable"). Sæt
  T-Deck'ens broker med monitor `=` + `mqtt_broker=mqtt://<PC-IP>:1883` (forbinder straks igen).
  **TLS afprøvet 22. nov 2026** (`mqtts://<PC-IP>:8883`, egen CA i `/dualmesh/mqtt_ca.pem`, lagt på
  med USB-drevet; DM via TLS LEVERET). Tre lærdomme: (1) mbedtls i IDF 4.4 matcher ikke IP-SAN, så
  certifikatet skal have IP'en som `DNS:` også; (2) brug EC-nøgler (P-256), RSA-2048 fik ikke RAM;
  (3) Arduinos mbedtls allokerer kun i intern RAM, så `MqttBridge::start` omdirigerer med
  `mbedtls_platform_set_calloc_free` (blokke ≥ 2 KB i PSRAM; før `ssl_setup -0x7F00`). Intern RAM
  40 KB fri under TLS. Testbroker: `C:\Claude\tools\mqtt\broker_tls.py` + certifikater i `tls\`.
- **SD-kortet som USB-drev (22. nov 2026, `storage/usb_disk.*`):** Indst. > System > "SD som
  USB-drev" (to klik) eller monitor `U` + `LAGER`: et RTC_NOINIT-flag + genstart; `setup()` kører
  så kun skærm og SD (ingen FatFs-montering) og viser kortet via TinyUSB MSC (`USB.begin()`
  skifter PHY fra Serial-JTAG til OTG under kørsel; ingen ændring i platformio.ini). Skub ud/
  trackball-klik afslutter med en RTC-watchdog-reset (`rwdt_ll_*`, `RESET_RTC`): en almindelig
  `esp_restart` beholder PHY-valget i RTC-domænet, og T-Deck'en kom tilbage UDEN USB (kræver så
  reset-knappen). Afprøvet: drev D:, filer kopieret, COM3 tilbage 2 s efter Skub ud. "Skub ud" lige
  efter en kopiering kan fejle (Windows skriver stadig): prøv igen.

## Status (fase 4: LVGL, startet 16. nov 2026)

- LVGL 8.4.0 (`lib_deps`, konfiguration i `include/lv_conf.h`): objekter og tegnebuffer
  (320 x 40) i PSRAM; `StatusDisplay::pushRect` sender i bidder på 512 B via en intern buffer.
  Startes først, når den bruges (`lvport::begin`): 4 ms, intern RAM 55 KB fri med BLE+MQTT.
- Fonte: Montserrat Medium 14/16 og SemiBold 20 (wadamesh bruger også Montserrat; den er nem at læse), samme tegnsæt som 8x16-fonten + typografiske tegn; LVGL's
  egen Montserrat 14 er fallback til `LV_SYMBOL_*`. Node.js/lv_font_conv er ikke installeret,
  derfor egen generator.
- **Alt tegnes med LVGL** (krav 16. nov 2026: den gamle metode udfases). AppUi
  beholder al logik (input, faner, skrivning, scroll, skærmslukning, SOS); `app_ui_lv.cpp`
  tegner. Den gamle 8x16-tekstvisning, rækkecache, kort og "Ny brugerflade" (`ui_lvgl`) er
  fjernet; `StatusDisplay` er kun init, `pushRect`, tænd/sluk og baggrundslys.
- Flyttet: **Chat**, **Kontakt**, **Kanal** og **Indst.-menuen** (Indst.: `SettingsUi::view` er datamodellen; 7 rækker à 18 px i 14 px skrift (16 px var for stort: forklaring og knapper faldt ud af skærmen), tomme rækker 6 px, gruppeoverskrift som farvet bjælke, etiket venstre/værdi højre, forklaring og hjælpelinje i 12 px (dm_font_12), knapperne Gem/Annuller med runde hjørner; `tapRow`/`tapButton`). Set og godkendt 16. nov 2026. Skriftstørrelse (valgt): 14 px overalt
  (fanelinje 22 px), sekundær tekst (tid, datolinjer, type/hop/alder, detaljer) 12 px; Kontakt
  og Kanal viser 8 rækker. **Lærdom:** LVGL's tema giver flex-containere et mellemrum mellem
  børn (`pad_row`/`pad_column`); `noStyle` nulstiller det (`pad_gap`), ellers bliver lister for
  høje, og bunden (hjælp, Gem/Annuller) skubbes ud af skærmen.
- **Underskærme, Status og SOS-bekræftelsen** brugte først et tekstgitter i monospace; det er
  fjernet 22. nov 2026 (se nedenfor). (Kanal: `ChannelsUi::view` er en fælles datamodel for tekstvisningen og LVGL; filteret står i titellinjen, 7 rækker, detaljelinje over indtastningen; `tapFilter`/`tapRow`) (Kontakt: 7 faste rækker fyldt med de synlige kontakter, valgt række = lyseblå bjælke, tryk = vælg, tryk igen = DM; Chat: fanelinje, titel, beskeder med tid/afsender i kontaktfarve/tekst og
  ombrydning, datolinjer, indtastning, specialtegn-bjælke med forstørret valgt tegn.) Listen
  bygges kun igen ved ændring (signatur over samtale, beskeder, dato og kontaktfarver).
  Målt 16. nov 2026: radiohuller uændrede (MT 62-65 ms, MC 39-47 ms, skift højst 4,9 ms).
  Chat set og godkendt 16. nov 2026 (kolon efter afsender, eget navn skjult). Kontakt set og godkendt samme dag. **DM'er (ønske):** kontaktens navn står kun i titellinjen; beskederne er bobler (runde hjørner, lidt lysere baggrund 0x2124, ramme i kontaktfarven / grøn for mine), deres til venstre, mine til højre, tid og status småt under teksten. **Kanaler har også bobler (ønske 17. nov 2026):** andres med afsenderens navn (kontaktfarve, 12 px) øverst i boblen, mine til højre uden navn; @-omtaler farves med LVGL-recolor (grøn = os, lyseblå = andre; '#' i teksten skrives '##'); tryk på en andens boble = @-omtale. DM-bobler, kanalbobler (med @-omtaler) og touch-scroll set og godkendt 17. nov 2026. LVGL bryder kun ved mellemrum/tegnsætning (`LV_TXT_BREAK_CHARS` i lv_conf.h), så navne ikke deles.- **Underskærme i LVGL (22. nov 2026, fælles model valgt af brugeren):** `PanelView`/`PanelRow`
  i `ui/panel.h` (status 1-3, rækker Header/Item/Info/Blank med etiket og værdi, note, hjælp eller
  gul indtastning; `add`/`info`, `clear()` i stedet for kopi på stakken). Et panel med
  `view()` = true tegnes af `AppUi::lvDrawPanel` i Indst.-stil (titelbjælke med tid, 7 rækker
  à 18 px, valgbjælke lyseblå/gul ved andet klik, QR til højre ved `qrText`), tryk via `tapRow`.
  Alle 13 underskærme er flyttet; Status ruller (`StatusViewFn`/`statusView` i main.cpp).
  **Krav 22. nov 2026: ingen menuer eller underskærme må være tekstgitter; alt kører LVGL.**
  Tekstgitteret (`text_grid.h`, panelernes `draw()`, `drawScreen`) er fjernet, og `Panel::view`
  er obligatorisk. Fejlrapportens QR-side: stor kode til venstre, `side`/`note`/`hint` til
  højre (ingen LVGL-farvekoder: de brød ombrydningen). SOS-bekræftelsen er en PanelView
  (`sosConfirmView`); tryk bekræfter aldrig SOS. Intern RAM 53 -> 62 KB fri.
  Mens fanelinjen har markøren, viser listerne intet valg. Set og godkendt 22. nov 2026.

## Status (fase 3: Meshtastic, startet 17. nov 2026)

Plan ("lav Meshtastic-delen der mangler"; Heltec LoRa32 bliver modpart, når der er en
pigtail): trin 1 modtag/afkod + nodedatabase, trin 2 send (kanaltekst, NodeInfo, Position efter
privatlivsreglerne), trin 3 egen X25519-nøgle, nodenummer fra nøglen og PKI-DM'er med ACK,
trin 4 videresendelse (CLIENT, kan slås til; standard CLIENT_MUTE) og telemetri.
- **Trin 1 (17. nov 2026):** `MtNode` dekrypterer alle pakker på vores kanaler og afkoder Data
  (portnum 1, payload 2, want_response 3, dest 4, request_id 6, emoji 8; ukendte felter
  springes over) og portene tekst (1), Position (3: lat 1, lon 2, alt 3, time 4), NodeInfo/User
  (4: id 1, long 2, short 3, hw 5, rolle 7, public_key 8), Routing (5: error_reason 3) og
  Telemetri (67: DeviceMetrics 2 -> batteri 1, spænding 2). Feltnumrene er protokolfakta
  (dokumentationen linker kun til GPL-schemaet); **bekræft dem mod rigtige pakker fra
  Heltec'en.** Nodedatabase (200, PSRAM, SD hvert minut ved ændring), navne i Chat i stedet
  for !nodeid, reaktioner ("reagerede med 👍"), DM'er med kanalnøgle (mtd_<nodenr>),
  PKI-pakker (hash 0) tælles og logges. Kontakt-fanen viser Meshtastic-noder efter MeshCore
  (MC/MT foran info). `[MT] i alt ...` hvert minut i loggen. Selvtest `X` (konstruerede
  pakker) bestået på hardware.
- **Trin 2 (17. nov 2026):** `MtNode::sendText/sendNodeInfo/sendPosition` via `mt_tx::buildData`
  (to, want_ack, want_response 3, request_id 6). Chat og MQTT sender på Meshtastic-kanaler;
  beskeden bliver "leveret", når vi hører en anden node sende den videre (egen id ->
  `setStatus`). NodeInfo (User: id, long = profilnavnet, short = Kort Navn eller sidste 4 hex,
  hw_model 50 T-Deck, rolle 1 CLIENT_MUTE) 1 min efter start, hver `meshtastic_nodeinfo_h`
  time (3) og som svar på forespørgsler til os. Position kun efter "Del position" (præcis =
  precision_bits 32, omtrent = 15 med midten af feltet), hver `meshtastic_position_min` (30);
  positionsforespørgsler besvares ikke, når den ikke deles (logges). "Må sende"
  (`meshtastic_tx`, standard til). Første NodeInfo sendt på hardware (684 ms). Meshtastic-DM'er
  venter på trin 3 (PKI). **Ikke set:** at andre noder viser vores navn (kræver modpart).
- **Trin 3 (17. nov 2026, clean room ud fra `docs/meshtastic_protokol.md`):** `net/mt_pki.*`:
  egen X25519-nøgle i `/sd/dualmesh/meshtastic/identity.txt` (hemmelig, krypteret, med i
  profil-eksporten); **nodenummer = CRC-32 af den offentlige nøgle** (skiftede fra MAC ved
  første opstart). PKI-DM'er: delt nøgle SHA-256(X25519), AES-256-CCM (egen RFC 3610, tag 8,
  extraNonce), kanal-hash 0; want_ack, ACK/NAK via Routing (35 = ukendt nøgle → vi sender
  NodeInfo), 3 forsøg à 20 s, "leveret"/"FEJL" i Chat. Ukendt nøgle: Chat siger "Nøgle ukendt -
  spurgt efter NodeInfo". XEdDSA-signatur (Data felt 10) på vores tekst/NodeInfo/Position;
  modtagne signaturer tjekkes, og en ny nodes nøgle godtages kun, hvis CRC-32 = afsender.
  Chat (Kontakt → MT-node) og MQTT (`<rod>/tx/mt/dm/<!nodeid|navn>`) sender DM'er.
  Selvtest `X` (inkl. CCM, manipulation, XEdDSA) bestået på hardware. **Ikke set:** DM'er og
  signaturer med en rigtig Meshtastic-node (Heltec).
- **Trin 4 (17. nov 2026):** videresendelse (`meshtastic_relay`: fra = CLIENT_MUTE (standard) /
  egne kanaler / alle = CLIENT) efter afsnit 9 i protokoldokumentet: ventetid 2 x CWmax slots +
  tilfældigt vindue efter SNR (slot fra SF/BW, LongFast ≈ 28 ms, 0,45-7,6 s), aflyses når en
  anden sender pakken, opgraderes ved flere hop, aflyses ved svar på en DM mellem andre; højst 6
  i kø; schedulerens 10 % gælder. ACK'er bruger brugte hop + 2 og sendes igen, når afsenderen
  gentager (ACK tabt). Egen telemetri (`meshtastic_telemetry_min`, standard 0 = fra): batteri
  (101 = USB), spænding, oppetid, ingen position; svar på forespørgsler kun når slået til.
  `[MT] i alt` viser videresendt/aflyst. Selvtest `X` dækker kø, header og aflysning (bestået).
  **Ikke set:** videresendelse og telemetri med rigtige noder.
- **Afprøvet mod en rigtig node (17. nov 2026):** Heltec Wireless Tracker V1.1 med Meshtastic
  2.8.2. Virker: NodeInfo begge veje (Heltec viser
  vores navn, T_DECK, CLIENT_MUTE og vores PKI-nøgle), kanaltekst begge veje (vores bliver
  "leveret", når Heltec sender den videre), PKI-DM begge veje med ACK ("LEVERET"), XEdDSA-
  signaturer begge veje (Heltec med politik BALANCED, signerede pakker godkendt; Meshtastics
  standard COMPATIBLE signerer ikke), telemetri-forespørgsel (PKI) besvaret. Fundet og rettet:
  RX-timeout efter CAD manglede headerens 8 symboler (korte pakker tabtes med tidsdeling).
  **Ikke afprøvet:** videresendelse (kræver 3 noder), NodeInfo-navneskift fra Heltec (sendes
  kun hver 3. time).
- **MeshCore afprøvet mod en rigtig node (17. nov 2026):** Heltec Wireless Tracker med MeshCore
  companion v1.18.0 (USB, `meshcore-cli` i `C:\Claude\tools\mtvenv`), EU/UK Narrow. Virker:
  adverts begge veje (kontakter oprettet), DM begge veje med kvittering (leveret / "Msg acked"),
  #Public begge veje. Heltec'en på 22 dBm få cm væk gav RSSI -11 dBm og CRC-fejl (overstyring); på
  2 dBm fejlfrit. Rettet: MQTT-DM til en kontakt efter navn fandt ikke de sidste 8 kontakter
  (`getNumContacts()` vs. `getContactByIdx()`, MeshCores 8 anonyme pladser).
- **Sendeeffekt pr. net (ønske 17. nov 2026):** `meshcore_tx_power` og
  `meshtastic_tx_power` (1-22 dBm, standard 22, aldrig over regionens grænse; genstart) i hver
  sin gruppe. `LoRaProfile::tx_power_dbm`; HAL sætter effekten ved profilskift, kun når den skifter.
- **Kanal for data (ønske 17. nov 2026):** `meshtastic_data_channel` (navn, tom = primær):
  de faste udsendelser af position og telemetri går på den kanal; findes den ikke, sendes intet.
  NodeInfo og SOS bruger altid den primære; svar på forespørgsler går på den kanal, forespørgslen
  kom fra. Position tjekkes hvert minut, mens den ikke kan sendes (deling fra/intet fix), så en ny
  "Del position" virker straks. Afprøvet mod Heltec 17. nov 2026: position og telemetri på DMtest.
  (Under en tidligere test gik én positionsudsendelse ud på LongFast, før denne indstilling fandtes.)
- **Testrunde 17. nov 2026 på private kanaler (DMtest, begge net) mod Heltec:** MC kanal begge
  veje, MQTT-rx, position i advert, telemetri (afvist som standard / tilladt med batteri, temp,
  GPS), direkte routing; MT kanal begge veje, position/telemetri på forespørgsel, traceroute
  Heltec->T-Deck. Monitor `C` = tilføj kanal (`mc,navn,nøgle` / `mt,navn,psk`). Mangler:
  MeshCore-app-kommandoerne (kræver telefon), /trace fra T-Deck, SOS/testbesked (kun Public),
  videresendelse (3. node).
- **Meshtastic-appen over BLE (17. nov 2026, `net/mt_ble.*`, `net/mt_phone.*`):** forbinder og
  viser node, firmware, radio og nodeliste (afprøvet). Nøglen var want_config i to
  runder (69420 = kun konfiguration, 69421 = kun noder) + QueueStatus efter hver pakke fra appen.
  Egen telemetri besvares lokalt. Noder uden NodeInfo får navnet "Meshtastic xxxx". Afprøvet:
  kanaltekst fra appen sendt og hørt af Heltec, Request User Info sendt, traceroute fra appen
  ("Direct", 1,9 s). Batterimålingen på USB viser ~3,1 V på PC-porten (porten giver for
  lidt strøm); på batteri korrekt (3,85 V, 64 %). **Ikke afprøvet:** DM fra appen, admin-ændringer.
- **GPL-revision (17. nov 2026, krav):** `docs/gpl_revision.md`, script
  `tools/gpl_compare.py` (token-/struktur-/kommentarsammenligning mod firmware + protobufs).
  Intet kopieret; kun idiomer, protokolkonstanter og den matematisk bundne X25519→Ed25519-
  omregning. Gentag ved større ændringer i Meshtastic-koden og før udgivelse.

## Feltlog (16. nov 2026, før første tur med LVGL-firmwaren)

Hvert minut i SD-loggen (`logFieldMinute` i main.cpp): `[GPS]` fix/satellitter/HDOP/position
(5 decimaler)/højde/fart - kun lokalt, så modtagne pakker (`[RX MC]`/`[RX MT]` med RSSI/SNR) kan
sammenholdes med stedet; `[MEM]` intern RAM (fri, største blok, laveste) og PSRAM; `[UI]` skærm
tændt/slukket, aktiv fane/underskærm, længste `AppUi::draw` og `lv_timer_handler` siden sidst.
Desuden som før: `[STAT]`/`[STROEM]`/`[DIAG]` hvert 10. s, `[TRAEG]`, `[KANAL]`, `[MC]` (adverts,
DM'er, kanalbeskeder med tekst), `[MT]`, `[UR]`, `[UI] skaerm slukket/taendt`. Hent med
`python tools/fetch_logs.py COM5 logs/sd` (pyserial: brug PlatformIO's python).
Målt ved skrivebordet: Chat-tegning 55 ms (første 210 ms med opbygning), LVGL-opdatering 0,1 ms,
intern RAM 62 KB fri. **Tjek efter turen:** modtagelse på begge net (regressionen fra
0013-0081), kanalscanner med rigtig trafik, ur fra mesh, batteriforbrug pr. time, `[TRAEG]`
og om tegnetiden eller hukommelsen vokser.

**Resultat af turen 16. nov 2026 (0006.log, 2 t 47 min på batteri, kørsel op til 93 km/t):**
- Modtagelsen virker (regressionsmistanken var bare manglende trafik ved skrivebordet):
  104 MeshCore-pakker (48 TXT_MSG, 35 RESPONSE, 8 REQ, 6 GRP_TXT, 3 PATH, 2 ADVERT, 1 ACK),
  RSSI -88 til -112 (snit -107), 2 repeater-adverts afkodet, kanalscanneren genkendte #Public og
  #ping med rigtig trafik, og **andre hører os**: en anden bruger svarede på testbeskeden på #Public
  (testbeskeden stoppede derefter som planlagt). Meshtastic: kun 1 pakke (gættet "deutschland").
- MeshCore: 657 præambler, 104 ok, 27 CRC-fejl, 526 timeout (præambel uden header, ~80 %).
  **Rettet 17. nov 2026 (fundet med Heltec'en):** RX-timeout efter et CAD-hit manglede LoRa-
  headerens 8 symboler (SX1262 stopper timeren først ved headeren, `StopTimerOnPre = 0`), så et
  CAD-hit tidligt i præamblen timede ud lige før headeren. Med Meshtastic 2.8: 0 af 4 korte
  tekster modtaget med tidsdeling (alle med kun Meshtastic); efter rettelsen 3 af 3. Den første
  forklaring ("svage signaler") var forkert; svage signaler bidrager måske stadig - se næste
  feltlog, om `timeout` på MeshCore er faldet.
- Batteri 3,99 → 3,90 V på 2 t 40 min (skærmen mest slukket). Intern RAM stabil 177 KB (uden
  WiFi/BLE), PSRAM stabil: ingen læk. Tegning typisk 55-65 ms, op til ~100 ms ved opvågning.
- **Fund: SD-kortet holder den delte SPI-bus.** Gem af CAD-tærskler tog 776 ms (normalt 22-37)
  → radiohul 477 ms og profilskift 144 ms; SD-log-skrivninger op til 166 ms under kørsel →
  huller 177-292 ms. sdspi venter på kortets "busy" med bussen låst. **Løst 16. nov 2026:**
  `storage/sd_fastwrite.*` overtager FatFs-drevet efter montering (`ff_diskio_register`):
  skrivning med egne kommandoer (CMD24 + data med CRC7/CRC16) på en ekstra SPI-enhed (manuel
  CS 39), bussen slippes straks, og kortets busy afventes med korte forespørgsler + 1 ms
  pause; læsning via `sdmmc_read_sectors` efter klar-tjek. Init/format/probe bruger stadig
  sdspi. Beskedlager-selvtest (605 skrivninger) bestået uden fejl; `[SD] skrevet N sektorer,
  laengste busy X ms` hvert minut i loggen. **Ikke set med et langsomt kort-øjeblik endnu**
  (busy var højst 5 ms ved testen) - tjek i næste feltlog, at lange busy ikke giver `[TRAEG]`.
- Før turen: "Nulstil DualMesh" med profil-eksport og **import afprøvet på hardware** (16. nov
  2026: identitet og navn tilbage). Indstillingerne følger ikke med profilen, så SD-kryptering
  er nu chip-ID (før eFuse), og WiFi/Bluetooth/MQTT stod på standard (fra).
- Rettet efter turen: selv sendte beskeder logges (`[UI] kanalbesked/DM til ...`), testbeskeden
  vises i Chat, emojis (Noto Emoji, OFL, `fonts/notoemoji/`, fallback for alle fonte) og
  "@[navn]" vises som "@navn" (grøn = os, ellers lyseblå).
- **Farve-emojis og emoji-vælger (ønske 16. nov 2026):** Noto Color Emoji (CBDT,
  OFL, `fonts/notocoloremoji/`) → `tools/gen_emoji_color.py` (fontTools + Pillow) →
  `src/ui/lv/fonts/dm_emoji_color.c`: 1318 emojis à 16x15 px RGB565+alfa (ca. 950 KB flash)
  vist som billed-tegn (`LV_USE_IMGFONT`, bpp = `LV_IMGFONT_BPP`), fallback-kæde: Montserrat →
  farve-emoji → sort/hvid Noto Emoji → LVGL-symboler. Vælgeren: Mic-tasten (0x1E) eller
  😀-ikonet foran indtastningen åbner 4x10 over feltet (smileys/hænder først); trackball
  flytter, klik/tryk indsætter (forbliver åben), Enter sender. Tryk på en andens besked i en
  kanal indsætter "@[navn] " (MeshCores omtaleformat). **Ikke set på skærmen endnu.**
- **Egne beskeder i kanaler (ønske 16. nov 2026, "mere Messenger, mindre IRC"):**
  højrestillet, intet "Mig:" - tid, status (sendt/leveret/FEJL, 12 px) og så den grønne tekst; andres
  beskeder er uændrede og venstrestillede (tid, afsender i kontaktfarve, tekst).

## Strømspar (22. nov 2026)

`cpu_eco` (Indst. > System > "Strømspar (CPU)", standard til): CPU 80 MHz, mens skærmen er
slukket, 240 MHz når den tænder (`AppUi::setPowerFn`, `[STROEM] CPU … MHz` i loggen). Målt med
skærmen slukket: radiohuller uændrede (MT 59-61 ms, MC 27-30 ms mod 58-61/27-28 ved 240 MHz),
ingen `[TRAEG]`. ESP-IDF's automatiske light sleep (`CONFIG_PM_ENABLE`) er ikke slået til i
Arduino-ESP32 2.0.14's færdigbyggede biblioteker og kræver et egenbygget framework. Radio-tasken
er hændelsesstyret (DIO1 + esp_timer), loop har `delay(5)`. Strømmen er ikke målt (ingen
strømmåler): se batterispændingen i `[STROEM]` på en tur.
**GPS-strømspar** (`gps_eco`, Indst. > GPS, standard til): u-blox UBX-RXM-PMREQ (backup,
vækkes af UART RX: 8 x 0xFF), når intet har brugt GPS'en i 60 s: skærm slukket, intet spor,
ingen "Del position" (MC/MT/MQTT), ingen SOS, uret sat. **SOS har prioritet over alt** (krav):
ingen GPS-dvale og CPU 240 MHz under SOS. Spor: GPS'en bliver vågen; kun skærm/CPU sparer.
`setEnabled(false)` sender nu også dvalekommandoen (før lukkede den kun porten, og modulet kørte
videre). Målt 22. nov 2026: 34 byte fra modulet på 62 s dvale (vågen ≈ 30.000), fix 10,4 s efter
vækning indendørs. Bemærk: med USB til en PC slukker skærmen ikke (medmindre "Sluk også på USB"),
så dvalen ses kun på batteri.

## Sprog (ønske 22. nov 2026)

Dansk er kildesproget: de danske tekster i koden ER nøglerne. `lang/<id>.txt` (fx `en.txt`,
"#name: English", linjer `dansk<TAB>oversættelse`, `\n` = linjeskift) oversættes, når teksten
tegnes: `setText` i `app_ui_lv.cpp` kalder `i18n::tr` (`lib/dualmesh/src/i18n.*`, test
`host/tests/test_i18n.cpp`). printf-felter i nøglen matcher hvad som helst ("Sendt til %.30s" /
"Sent to %s"), talfelter kun tal; et fundet stykke, der selv er en nøgle ("fra"), oversættes også.
Brugerindhold (beskedtekst, afsendernavne) sættes med `lv_label_set_text` og oversættes ikke; logs
oversættes aldrig. **Nye UI-tekster:** kør `python tools/gen_lang.py` (finder strengene i src/ui m.fl.,
fletter dem ind i lang/*.txt med tom oversættelse, bygger `src/ui/lang_builtin.h`), oversæt de tomme
linjer og kør igen; `--check` fejler ved manglende. Indstillingen "Sprog / Language" (`language`,
System, genstart, **standard English**) vælger sproget (`ui/lang.*`); ekstra sprog som
`/dualmesh/lang/<id>.txt` på SD. Uoversatte mønstre indlæses som sig selv (så "(følger)" i
"Zoom … %s" oversættes); engelske etiketter højst 17 tegn, værdier højst ~21 (ellers skæres de).
Engelsk: 686 af 909 nøgler (resten er tekniske strenge, filindhold og radiobeskeder). Afprøvet
22. nov 2026: sproget indlæses ved opstart; skærmen ikke set endnu.

## Fejlrapport (ønske 17. nov 2026: løsning 1 + 2)

Brugere skal kunne sende logs til GitHub uden følsomme data. Der er INGEN token i firmwaren og
ingen server: brugeren opretter selv issuet med sin egen GitHub-konto (også af hensyn til GDPR).
- **Løsning 1, QR-kode (`report_ui.h`):** Indst. > System > Fejlrapport viser denne opstarts
  årsag og seneste gemte nedbrud; Enter/klik viser en QR-kode (210 px, øverst til venstre,
  `Panel::qrSize`) med `.../issues/new?template=fejlrapport.yml&version=&opstart=&nedbrud=`.
  Linket har KUN version, opstartsårsag og nedbrudsadresser (højst 287 tegn = QR version 12 med
  3 px pr. modul; backtracen beskæres bagfra, i dag 7 rammer). `x` to gange sletter det gemte
  nedbrud (`esp_core_dump_image_erase`, 64 KB flash ≈ 0,5-1 s med cachen slået fra).
- **Nedbrud:** core dump til flash er slået til i Arduino-ESP32 2.0.14's sdkconfig, og
  `default_16MB.csv` har partitionen `coredump` (64 KB @ 0xFF0000). `crashinfo::logAtBoot` skriver
  ved opstart efter PANIC/INT_WDT `[CRASH]`-linjer (fejltype, opgave, PC, backtrace i
  exception-decoder-format, ELF-SHA256 + om det er samme build). Task-watchdog giver ikke dump.
- **Løsning 2, websiden `docs/rapport/index.html`:** WebSerial (Chrome/Edge; Espressif VID
  0x303a) sender `I` hvert 3. s, til `=== DMRAPPORT ===`-blokken kommer (porten genstarter
  T-Deck'en), derefter `d` (de 3 nyeste logfiler i DMLOG-formatet). Alternativt vælges filer fra
  SD-kortet; så findes version/opstart/nedbrud i `[LOG] DualMesh …`, `[LOG] logger til SD …`
  og `[CRASH]`. Rensningen sker i browseren; brugeren gennemser (markeringer `‹…›`), kan rette,
  downloader .txt og åbner et udfyldt issue, hvor filen trækkes ind.
- **Rensningen (`scrub.js`):** kun ord, som firmwaren selv skriver, overlever (`vocab.js`, fra
  DLOG-formater + literaler ≤ 40 tegn; navne fra hjælpeteksternes eksempler er spærret i
  `DENY`). Før ordtjekket: beskedtekster (hele teksten + kanal/personnavn, undtagen offentlige
  standardkanaler), citerede værdier, positioner, IP/MAC/e-mail/URL-værter, PIN/psk/pass=,
  base64-nøgler, lange hex, lange tal (ikke tællere `x=…`); node-, samtale- og pakke-id'er får
  faste aliaser (`!‹node1›`). **Nye log-linjer:** kør `python tools/gen_scrub_vocab.py` og testen
  (`node docs/rapport/scrub_test.js` eller åbn `docs/rapport/test.html`); korpusset fylder alle
  `%s` med kanariefugle og kræver, at ingen overlever. Nye linjer med beskedtekst skal have en
  regel i `MESSAGE_RULES`. Log aldrig hemmeligheder (koder, nøgler) – rensningen er et
  sikkerhedsnet, ikke en tilladelse. Afprøvet 17. nov 2026 på alle logs (129.616
  linjer): ingen SSID, nodenavn, IP, position, node-id eller lange tal tilbage.
- **Afventer ("vent med resten", repoet er privat):** GitHub Pages (Settings > Pages >
  branch main, mappe `/docs`) og labelen `fejlrapport` slås til, når repoet bliver offentligt;
  før det virker hverken siden på `DM_REPORT_URL` eller issue-linket for andre end brugeren.
  Siden kan bruges lokalt ved at åbne `docs/rapport/index.html` i Chrome/Edge.
- **Afprøvet 22. nov 2026:** bygger, `I`/`d` i monitoren virker, og en midlertidig `abort()`
  gav ved næste opstart `[CRASH]`-linjer (PANIC, backtrace med samme PC'er, "samme build") og
  nedbruddet i rapport-hovedet. Menupunktet og QR-koden set og godkendt samme dag: GitHub-appen
  viser skabelonvalget, og "Fejlrapport" åbner browseren med version, opstart og nedbrud udfyldt
  (derfor siger QR-siden "Åbner GitHub-appen: tryk på Bug report"; skabelonen hedder "Bug report"
  og er på engelsk siden 23. nov 2026, filnavn, felt-id'er og labelen `fejlrapport` er uændrede).

## Udgivelser og hent fra GitHub (23. nov 2026)

- **Builds til brugerne (krav: ingen skal bygge selv):** tag `v<version>` (= `DM_FW_VERSION` i
  `src/version.h`) → `.github/workflows/release.yml` bygger på GitHub Actions, `tools/release.py`
  pakker (`dualmesh-tdeck_plus.bin` = app til OTA/launcher, bootloader/partitions/boot_app0,
  `version.txt` med size+sha256, `manifest.json`, `notes.md`) og laver releasen; derefter startes
  `pages.yml` på main (Pages-miljøet tillader kun main), der lægger `docs/` + seneste release på
  GitHub Pages (build_type = workflow). Intet samlet billede fra 0x0: det ville overskrive NVS.
  **v0.1.0 udgivet 23. nov 2026** (rev 273b5df; kan ikke selv hente – flash v0.1.1 over USB),
  **v0.1.1** samme dag (a07ffda, hentning rettet), **v0.1.3** (3d8aaae: bootloop med Bluetooth og et
  nodenavn over 17 tegn rettet, BLE-navnet højst 26 byte; `dualmesh-licenses.zip` følger med).
  Opdatering til v0.1.3 på T-Deck'en afprøvet af brugeren 23. nov 2026: virker. **v0.1.4**
  (866d492): kortpakkernes navne fra filnavnet (set korrekt på T-Deck'en).
- **Flash-siden** `docs/flash/` (https://geekgarage.github.io/dualmesh/flash/): ESP Web Tools 10
  fra unpkg (Apache-2.0), engelsk/dansk; ikke afprøvet med en T-Deck endnu.
- **`net/dl.*`:** én HTTPS-hentning ad gangen i egen task (følger viderestillinger selv,
  `buffer_size_tx` 2048 til GitHubs lange signerede adresser, Range-genoptagelse til `<sti>.part`,
  fsync hver MB, ledig plads via `f_getfree` på `sdfw::pdrv()`), til hukommelse (katalog, 64 KB),
  SD eller OTA (`OTA_WITH_SEQUENTIAL_WRITES`, SHA-256 skal passe før `esp_ota_set_boot_partition`).
- **`ui/downloads_ui.h`:** kataloger i repoet (`keyboards/index.txt`, `lang/index.txt` fra
  `gen_lang.py`, `maps/index.txt`), Indst. > Tastatur "Hent layouts", System "Hent sprog", Kort
  "Hent kortpakker"; `installed.txt` pr. mappe; hentede layouts lægges i `keyboard_layouts`.
- **`ui/ota_ui.h`:** System > "Opdatér firmware" (`releases/latest/download/version.txt`); under
  en launcher (`nvstool::launcher().found`) gemmes .bin i `/dualmesh/firmware/` i stedet. `[OTA]
  koerer fra <partition>` ved opstart.
- **Kortpakker:** release `kort-1` (`--latest=false`, ellers finder OTA ikke firmwaren):
  Danmark.pmtiles 1,58 GB (Accept-Ranges virker). Udgiv aldrig pakker med brugerens hjemområde.
  Fra 23. nov 2026 release `kort-2` (lægges op automatisk fra en byggeserver): **fil- og katalognavne
  er engelske** (Denmark, Iceland, Faroe-Islands, Greenland-…, Svalbard-NN, Nordics-NN; krav: engelsk
  som standard). T-Deck'en viser filnavnet med '-' som mellemrum (`PmTiles::open`, efter v0.1.3);
  metadataens `"name"` bruges ikke (JSON-escapes: "Færøerne" blev vist som "FÃ¦r…", og
  ældre pakker har danske navne). kort-1 (dansk filnavn) er slettet 23. nov 2026.
- **Afprøvet 23. nov 2026:** OTA (0.1.0 skrevet i app1, SHA-256 OK, genstart), flash-siden i
  browseren (v0.1.1) og OTA igen til v0.1.1 (a07ffda); monitor `G` henter katalog + version.txt.
  **Lærdom:** IDF 4.4's certifikatbundt fejler på Let's Encrypts krydssignerede kæde ("PK verify
  failed") → egne rødder (`src/net/gh_roots.h`, `tools/gen_gh_roots.py`); rødderne parset i intern
  RAM tømte den (laveste 1 KB, "ctr_drbg_seed -1", nedbrud i radio-tasken) → alle mbedtls-blokke og
  HTTP-buffere (> 4 KB, `SPIRAM_MALLOC_ALWAYSINTERNAL` = 4096) i PSRAM, start kræver 24 KB fri.
- **Panelerne afprøvet samme dag:** Hent layouts (da.kbd installeret og aktiv), Hent sprog (katalog),
  Hent kortpakker (Danmark startet og afbrudt, `.part` bevares). **Ikke afprøvet:** en hel
  kortpakke-hentning og genoptagelse efter afbrydelse (kræver rigtigt WiFi, ikke mobildata).
- **Senere (ønske):** DualMesh i T-Deck Launcherens firmwarekatalog (testliste b18). Kataloget
  (LauncherHub, bmorcelli/M5Stack-json-fw) får man ved et issue på bmorcelli/Launcher ("Add new app
  to the Catalog"): kategori `t-deck`, app-billedet `dualmesh-tdeck_plus.bin` fra releases.
- **Under bmorcelli's Launcher (afprøvet 23. nov 2026):** Launcher ligger i `app0` (subtype
  **test**, 1,5 MB), DualMesh i `dualme` (ota_0, størrelsen tilpasset appen: 3648 KB til 3,7 MB),
  `coredump` findes. DualMesh kører, finder SD-data og kortpakken. Launcher genkendes nu
  (`nvstool::launcher`: factory/test-partition eller kørsel fra en partition ≠ app0/app1;
  projektnavnet er "arduino-lib-builder" for alle Arduino-apps og kan ikke bruges). **Lærdom:**
  efter Launchers USB-drev-tilstand starter appen uden seriel USB (PHY-valget overlever en
  software-genstart) – reset-knappen hjælper. Udvikling under Launcher: `esptool write_flash
  0x1a0000 firmware.bin` bevarer layoutet (tjek at appen passer i partitionen).

## Faseplan

1. Radio på hardware: mål skiftetid, finjustér CAD, løs SPI-deling med skærmen. Kriterium: målt tab
   ligner simuleringen. (Mangler: tabsmåling med Heltec som modpart.)
2. MeshCore-biblioteket koblet på scheduleren: kanaler, DM'er, kontakter, simpelt UI.
   (Fremrykket 15. nov 2026: WiFi, GPS, SOS og MeshCore-appen over BLE, se ovenfor.)
3. Meshtastic: modtag/send LongFast og egne kanaler, NodeInfo, Position, PKI-DM'er. **Færdig
   22. nov 2026** (afprøvet mod Heltec, GPL-revision gentaget); mangler kun videresendelse med 3 noder.
4. Fælles UI (LVGL) og beskedlager for begge net, inkl. trackball-indstillinger. **Færdig 22. nov
   2026:** alt tegnes med LVGL, intet tekstgitter.
5. Bluetooth: MeshCore companion-protokol først, derefter Meshtastic telefon-API.
6. MQTT-bro (emner `<rod>/rx|tx/<net>/chan|dm/...`, se spec).
7. Kort med GPS og offline-download af fliser til SD (ikke fra OSM's egen server). ← **i gang**
   (22. nov 2026): `tools/maps/make_map.py` (mapvenv: pmtiles, mapbox-vector-tile, Pillow) henter
   OSM-vektordata for et område fra den nyeste Protomaps-build (kun HTTP range) og tegner selv
   rasterfliser i vores stil (metafliser 4x4, så navne ikke skæres; PNG med 32 farver; alle
   CPU-kerner) til én PMTiles v3-fil. `--cache <mappe>` henter først områdets kildedata i store
   sammenlagte bidder (Danmark z0-15: 1 GB på ~4 min) og genbruger dem; rent hav tegnes ikke.
   Licens: kortdata ODbL, fliserne er "produced work" og må deles (fx som GitHub-release,
   ≤ 2 GiB pr. fil; T-Deck'ens `fseek` er 32-bit, så hold pakker under 2 GB) med
   "© OpenStreetMap-bidragydere" ved kortet; aldrig fra OSM's egne flise-servere. Protomaps'
   `landcover`-lag (z0-7) er ESA WorldCover under CC-BY 4.0 (deres LICENSE_DATA.md) og tegnes
   IKKE (kontrolleret 22. nov 2026), så pakkerne kun er OSM (ODbL) + Natural Earth (public domain). T-Deck: fanen **Kort** (Kanal | Kontakt | Kort | Indst.),
   `src/map/` (PMTiles-læser, PNG-afkoder til byte-byttet RGB565, ROM-inflate med dekompressoren i
   heap, 20 fliser i PSRAM, én indlæses pr. tegning). Pakker i `/dualmesh/maps/*.pmtiles` (læg på
   med USB-drevet). Trackball panorerer, klik/`c` centrerer og følger GPS, `+`/`o` zoomer
   ind og `-`/`i` ud (tasternes påtryk), træk med fingeren panorerer, knapperne +/-/GPS/menu til
   højre, Backspace = fanelinjen; et tyndt rødt sigtekorn viser punktet, når kortet ikke følger
   GPS (koordinaterne i infolinjen). **Delte lokationer (22. nov 2026):** Enter/`m`/menuknappen =
   kortets menu: "Del i en samtale", "Del og gem på kortet", "Gem på kortet", "Fjern prik" (prik
   ≤ 15 px fra sigtekornet), "Ryd alle gemte prikker" (to klik). Del: vælg samtale i Kanal/Kontakt, teksten "📍 bredde, længde"
   (`lib/dualmesh/src/geo_text.h`, virker på begge net og i apps) lægges i indtastningen; "gem"
   gemmer prikken med samtalens navn, når den sendes. Chattens tandhjulsmenu har "Del min
   position" (GPS). Modtagne gemmes kun, når man vælger det: tryk på en boble med en lokation =
   "Vis på kortet"/"Gem på kortet" (kun touch). Prikker: orange firkanter med etiket,
   `/sd/dualmesh/maps/points.txt` (`map/map_points.*`, højst 64). **Spor (22. nov 2026,
   `map/track.*`, `ui/tracks_ui.h`):** kortets menu "Start spor"/"Stop og gem spor"/"Skjul vist
   spor"; GPS-punkt hvert `track_interval_s` (5) s, når man har flyttet sig `track_min_m` (10) m;
   skrives i bidder hvert 30. s til `/sd/dualmesh/tracks/<ÅÅÅÅMMDD-TTMMSS>.csv` (bredde,længde,
   unixtid,højde), ved stop også `.gpx`. Optagelsen fortsætter efter genstart (`track_rec` i
   settings.ini). kortets menu > "Gemte spor" (panel over kortet; paneler kan åbnes på Indst. og Kort,
   `AppUi::panelOn`): klik = vis på kortet (violet; optagelsen er rød),
   Backspace 2x = slet. Punkterne gemmes som Mercator 0..1 (float) i PSRAM (højst 20.000, tyndes
   ud), tegnes som lv_line-stykker (6 à 160 punkter). Gemte spor virker (22. nov 2026); fejlen
   med tom liste var, at panelets data kun blev hentet i Indst. Genvej på kortet: `s`.
   **Geocaching (22. nov 2026, `map/caches.*`, `ui/caches_ui.h`), kilde 1 af 2:** GPX-filer i
   `/sd/dualmesh/caches/*.gpx` (Pocket Query fra geocaching.com, download fra opencaching.de;
   Groundspeak-udvidelsen; egen streaming-parser, ekstra waypoints uden sværhed/terræn springes
   over; højst 3000 caches + 512 KB hints i PSRAM). Kortets menu/`g` = "Geocaches": nærmeste 30
   (fra GPS, ellers kortets midte), venstre/højre = alle/ikke fundet; detaljer med type,
   størrelse, D/T, afstand/kurs, hint (klik/h), Navigér hertil, Vis på kortet, Fundet/Ikke fundet
   og "Slet fra listen" (to klik; skjuler cachen i liste og på kort; filteret "slettede" kan
   gendanne) (`/sd/dualmesh/caches/found.txt` med F/N/H, nyeste linje gælder; `<sym>Geocache Found</sym>` i GPX
   tæller også). Kortet (fra zoom 10): firkanter i typens farve (fundne grå), kode fra zoom 14,
   orange stiplet linje til målet og "-> KODE afstand retning" i infolinjen. Geocaching.com's API
   kræver partneraftale og deres vilkår forbyder automatisk hentning: brug kun GPX, som brugeren
   selv henter. **Kilde 2: Opencaching over WiFi** (`net/okapi.*`): Indst. > Kort
   `okapi_site` (standard www.opencaching.de), `okapi_key` (brugerens gratis consumer key fra
   `/okapi/signup.html`, logges aldrig), `okapi_radius_km` (10), "Hent caches nu"; eller `o` i
   Geocaches-listen. OKAPI `services/caches/shortcuts/search_and_retrieve` (nearest, højst 500,
   felter code|name|location|type|size2|difficulty|terrain|hint2) i en egen task over HTTPS
   (esp_http_client + IDF's rod-CA'er, mbedtls-blokke i PSRAM); svaret (egen lille JSON-læser)
   gemmes som `/sd/dualmesh/caches/opencaching.gpx` og indlæses med GPX-læseren (virker offline
   bagefter). Endpoint og fejlsvar ({"error":{"developer_message":...}}) tjekket fra PC'en med
   ugyldig nøgle 22. nov 2026; ikke afprøvet med rigtig nøgle på T-Deck'en.
   Afprøvet med en testfil (3 caches, 29 ms); ikke set på skærmen eller med rigtige caches. Danmark z0-15 = 1,65 GB, 477.637
   fliser, tegnet på 16 min; første åbning starter ved egen position. Prikker: egen
   (blå) og noder fra begge net med position (kontaktfarve + navn). Set og virker 22. nov 2026
   (to forskellige områder). **Kortet færdigt 22. nov 2026:** blå pil i kørselsretningen på
   egen prik (GPS-kurs ved over 2 km/t, `MapMarker::heading`), kortets menu/`k` = "Kortpakker"
   (`ui/maps_ui.h`: navn, zoom, MB; Gå til / Slet pakken med to klik, `MapView::goToPack/
   removePack`), Indst. > Kort > "Kortfølsomhed" (`map_sensitivity` 1-10, 4·n+4 px pr. skridt,
   standard 5 = 24 px). Kortmenuen ruller ved mange punkter (`kMenuMax` 12).
   **Meshtastic-waypoints (22. nov 2026, port 8, afsnit 13 i protokoldokumentet):** modtagne på
   vores kanaler bliver magenta prikker (`MapPoint::kind` 1, samme id = opdatering, udløbet =
   slettet; `MtNode::setWaypointFn`); kortmenuen "Send som waypoint (<Kanal for data>)" sender
   sigtekornets punkt (navn = gemt prik under sigtekornet, ellers "DualMesh TT:MM"), signeret.
   Afprøvet begge veje mod Heltec'en (2.8.2): modtagelse (id, navn, position), afsendelse på
   DMtest (alle felter afkodet af Meshtastic-biblioteket) og sletning begge veje (expire 1).
   Monitor `W`/`w` = send/slet testwaypoint, KUN på DMtest. **Lærdom:** "Kanal for data" tom =
   primær (LongFast): et testwaypoint gik ved en fejl ud offentligt og blev slettet igen; test
   altid med en eksplicit privat kanal. GPL-revision gentaget (aften): intet kopieret. **Field notes:** `e` i Geocaches-listen skriver
   `/dualmesh/caches/geocache_visits.txt` (UTF-16LE, "KODE,ÅÅÅÅ-MM-DDTTT:MMZ,Found it,\"\"") ud fra
   found.txt (kun nyeste status pr. cache); upload på geocaching.com/Opencaching. **Mangler:**
   download af kortpakker på T-Deck'en (kræver offentligt repo eller anden kilde).

Trackball (lavet 15. nov 2026): akselås – et skridt på den ene akse spærrer den anden i 0,5 s, og
spredte sidepulser glemmes efter 0,4 s, så scroll ikke hopper til siden; fanebytte kræver
`trackball_deadzone` (1–10, standard 3) skridt sidelæns. Afprøvet og godkendt 15. nov 2026 ("virker perfekt"). Spec: Trackball: følsomhed 1–10 (standard 5), acceleration, separat kortfølsomhed, dødzone,
klik-forsinkelse 150 ms, vend akser; gemmes og virker med det samme. Se spec.

## Åbne beslutninger (spørg brugeren, gæt ikke)

- ~~Bluetooth: begge apps eller én først?~~ Besluttet 15. nov 2026: MeshCore-appen nu. **Meshtastic-
  appen over BLE først, når fase 3 er HELT færdig** (16. nov 2026: "stop mig, hvis jeg
  beder om det før").
- ~~Kortkilde?~~ Besluttet 16. nov 2026: brug de kilder, der er tilgængelige (også selv om de ikke
  tillader offline-download); offline-download ses på i en senere opdatering.
- ~~Videresendelse?~~ Besluttet 16. nov 2026: repeater-funktion skal kunne slås til (standard fra).
- ~~Tastaturlayouts?~~ Besluttet 16. nov 2026: ligger på GitHub i projektets repo; når projektet
  bliver offentligt, henter firmwaren dem derfra over WiFi.

## Arbejdsgang

- Git (regel 15. nov 2026): **commit altid efter ændringer** (små, sammenhængende commits
  med dansk besked), og **push ved større ændringer** som nye funktioner, uden at spørge. Commit
  aldrig ikke-fungerende kode uden at sige det i beskeden. `logs/` committes ikke.
- Lav små, testbare skridt: byg → flash → læs log → justér.
- **Offentlige sider er på engelsk med dansk som valg (krav 23. nov 2026):** `README.md` (engelsk)
  og `README.da.md` (dansk) holdes ens; fejlrapport-siden (`docs/rapport/index.html`) har begge
  sprog i `TEXT` (standard engelsk, `?lang=da` eller sprogknappen). Nye tekster skal med i begge.
  Repoets beskrivelse på GitHub (vises, når linket deles) er på engelsk; hjemmeside =
  https://geekgarage.github.io/dualmesh/ (forsiden `docs/index.html`); social preview =
  `assets/social/dualmesh-github-preview.png` (`tools/social_image.py`).
- Repoet er offentligt (23. nov 2026, ny historik): commit aldrig navne, node-id'er, positioner,
  WiFi-navne, IP'er eller nøgler fra rigtige enheder; brug opdigtede testværdier. Forfatter-e-mail
  er GitHubs noreply-adresse (repoets git-config).
