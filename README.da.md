# DualMesh – firmware til LilyGo T-Deck Plus

[English](README.md) | **Dansk**

![DualMesh](assets/logo/png/tdeck-splash-320x240.png)

DualMesh er en ny firmware, skrevet fra bunden, der taler **MeshCore og Meshtastic med én
SX1262-radio**. En tidsdelende scheduler skifter mellem de to net så hurtigt, at begge kan
høres. Firmwaren er kompatibel med begge net, men er ikke et produkt fra MeshCore eller
Meshtastic. Licens: MIT (se `LICENSE`).

## Status

| Område | Status |
| --- | --- |
| Radio og scheduler (CAD, selvkalibrering, LBT, fælles 10 % duty cycle) | Kører på hardware |
| MeshCore: adverts, kontakter, kanaler, DM'er, telemetri | Modtagelse og afsendelse afprøvet i felten |
| Meshtastic: kanaler, NodeInfo, Position, PKI-DM'er, XEdDSA, telemetri, traceroute, waypoints | Afprøvet mod Meshtastic 2.8.2; videresendelse mangler test med 3 noder |
| Brugerflade (LVGL), dansk og engelsk | Alt tegnes med LVGL; sprog vælges i Indst. > System |
| MeshCore-appen og Meshtastic-appen over Bluetooth | Virker (én app ad gangen, parring med PIN) |
| WiFi, NTP, GPS, SOS, SD-kryptering, profil-eksport/-import | Virker |
| MQTT-bro (også TLS) | Afprøvet begge veje |
| Kort (offline), spor, delte lokationer, geocaching | Virker; kortpakker kan hentes over WiFi |
| Firmwareopdatering (OTA), layouts og sprog fra GitHub | Udgivelser bygges af GitHub Actions |

Den fulde kravspecifikation står i `docs/spec.md`, og den detaljerede status i `CLAUDE.md`.

## Hardware

LilyGo T-Deck Plus: ESP32-S3 (16 MB flash, 8 MB PSRAM), SX1262, ST7789 320×240 med touch,
tastatur, trackball, microSD og u-blox GPS. Radio, skærm og SD-kort deler én SPI-bus;
firmwaren sender til skærmen og SD-kortet i små bidder, så radioen aldrig venter længe.

**Send aldrig med radioen uden antenne monteret.** Overhold regionens effektgrænse og duty
cycle (fx EU 869,4–869,65 MHz: 10 %, 27 dBm); firmwaren håndhæver selv 10 %.

## Installér

Ingen byggeværktøjer nødvendige:

- **I browseren:** åbn [flash-siden](https://geekgarage.github.io/dualmesh/flash/?lang=da) i Chrome
  eller Edge, forbind T-Deck Plus med USB, og tryk *Installér DualMesh*.
- **Opdatér:** Indst. > System > *Opdatér firmware* installerer den nyeste udgivelse over WiFi
  (SHA-256 tjekkes, før den nye firmware vælges). Under en launcher gemmes firmwaren i stedet i
  `/dualmesh/firmware/` på SD-kortet, så launcheren kan installere den.
- **Launcher eller esptool:** hent filerne fra den
  [seneste udgivelse](https://github.com/geekgarage/dualmesh/releases/latest); udgivelsesnoterne har
  esptool-kommandoen.

Over WiFi kan T-Deck'en også hente tastaturlayouts (Indst. > Tastatur og trackball > *Hent layouts*),
sprog (Indst. > System > *Hent sprog*) og kortpakker (Indst. > Kort > *Hent kortpakker*; Danmark fylder
1,58 GB, så brug WiFi frem for mobildata). En afbrudt korthentning fortsætter, hvor den slap.

## Byg og flash

Kræver PlatformIO (env `tdeck_plus`, Arduino-framework med ESP-IDF-drivere).

```bash
pio run -e tdeck_plus                 # byg
pio run -e tdeck_plus -t upload       # flash (T-Deck i USB)
pio device monitor -e tdeck_plus      # seriel log, 115200
```

Fejler upload: hold trackballen nede, tryk reset, slip trackballen (boot-tilstand), og prøv
igen. At åbne COM-porten genstarter T-Deck'en.

**Udgivelser:** hæv `DM_FW_VERSION` i `src/version.h`, commit, og push et tag med samme version
(`git tag v0.1.1 && git push origin v0.1.1`). GitHub Actions (`.github/workflows/release.yml`) bygger
firmwaren, laver udgivelsen med `tools/release.py` og lægger den nye firmware på flash-siden.
Kortpakker ligger i egne udgivelser (`kort-N`, oprettet med `--latest=false`) og står i
`maps/index.txt`.

## Brug

- **Faner:** Kanal | Kontakt | Kort | Indst. Markøren starter i listen: op over øverste række går til
  fanelinjen, hvor venstre/højre vælger fane og ned/klik går ind. I listen åbner højre/klik/Enter
  (en kanal eller kontakt åbner Chat); venstre eller Backspace går tilbage. Tryk på skærmen
  virker også.
- **Chat:** begynd bare at skrive, Enter sender. Går du tilbage med tekst i feltet, gemmes den
  som kladde til samtalen. Dine beskeder står til højre i grønt, andres
  til venstre med afsenderen i kontaktens farve. DM'er vises som bobler. Tryk på en andens
  besked i en kanal for at nævne afsenderen (@-omtale).
- **Emojis:** tryk på 😀 foran indtastningen eller på Mic-tasten; vælg med trackball eller
  tryk. Farve-emojis vises også i modtagne beskeder.
- **Specialtegn:** skriv en tast med varianter (fx `a`), og vælg å, æ, á … i bjælken med
  trackballen. Layouts ligger i `keyboards/`.
- **Kort:** offline-kort fra SD-kortet (se "Kort" nedenfor). Kuglen eller fingeren flytter
  kortet; klik på kuglen centrerer og følger din GPS-position. `o` (+) zoomer ind og `i` (-)
  ud, eller brug knapperne til højre. Når kortet ikke følger dig, viser et tyndt rødt sigtekorn
  punktet, og koordinaterne står øverst. Enter eller ≡ åbner kortets menu.
- **Kortets menu:** del sigtekornets punkt (eller din position) i en kanal eller DM, gem det som
  prik på kortet, send det som Meshtastic-waypoint, optag spor, se gemte spor (`s`),
  geocaches (`g`) og kortpakker (`k`).
- **Delte lokationer:** en lokation sendes som tekst ("📍 55.67567, 12.56756"), så den virker på
  begge net og i alle apps. Tryk på en modtaget lokation i chatten for at se eller gemme den på
  kortet. Chattens tandhjulsmenu har "Del min position".
- **Meshtastic-waypoints:** waypoints fra andre på dine kanaler bliver magenta prikker; dine egne
  sendes på "Kanal for data" (Indst. > Meshtastic).
- **Spor:** "Start spor" i kortets menu gemmer, hvor du har været (rød linje), også hen over
  en genstart. "Gemte spor" viser et spor på kortet eller sletter det. Filerne er CSV og GPX.
- **Geocaching:** caches fra GPX-filer (Pocket Query fra geocaching.com, opencaching.de) eller
  hentet fra Opencaching over WiFi (`o` i listen, kræver din egen gratis API-nøgle). Listen viser
  de nærmeste med afstand og retning; detaljer, hint, "Navigér hertil" (stiplet linje og afstand
  på kortet), fundet/ikke fundet og "Slet fra listen". `e` laver field notes
  (`geocache_visits.txt`) til upload.
- **SOS:** hold trackball-klik i 5 s og bekræft med Enter. Sender position på begge net, til
  den stoppes igen. Ikke en erstatning for 112.
- **Indstillinger:** alt i Indst., grupperet pr. ting (WiFi, Bluetooth, GPS, Kort, MeshCore,
  Meshtastic, MQTT, Skærm …). Ændringer gælder efter **Gem**.
- **Sprog:** engelsk som standard; dansk vælges under Indst. > System > "Sprog / Language".
  Flere sprog kan lægges på SD-kortet som `/dualmesh/lang/<id>.txt` (samme format som
  `lang/en.txt`).
- **Privatliv:** egen position deles aldrig, medmindre det er slået til (pr. net), og
  telemetri-svar er slået fra som standard.

## Tekstkommandoer

Der er tre steder, man kan styre T-Deck'en med tekst. Alle andre funktioner ligger i menuerne.

### Seriel monitor (USB, 115200 baud)

`pio device monitor -e tdeck_plus` eller et andet terminalprogram. Bemærk: at åbne porten
genstarter T-Deck'en. Ét tegn = én kommando; kommandoer med tekst afsluttes med Enter, og en
tom linje fortryder. I kode-tilstand (SD-kryptering) virker kun `s`, `p` og `h`, indtil koden
er skrevet på T-Deck'en.

| Tast | Gør |
| --- | --- |
| `h` | Hjælp: viser alle taster |
| `s` / `p` | Statistik nu / radioplanen |
| `l` | Alle regioner og presets (Meshtastic og MeshCore) |
| `m` | MeshCore-status (node, kontakter, beskeder) |
| `k` | Vores Meshtastic-kanaler |
| `a` | Send en MeshCore-advert |
| `c` + tekst | Skriv til MeshCore #Public |
| `C` + `mc,navn,nøgle` / `mt,navn,psk` | Tilføj en kanal (nøgle i base64; tom = ny/standard) |
| `N` + navn | Skift nodenavn (og send en advert) |
| `=` + `nøgle=værdi` | Sæt en indstilling som i menuen. Radioindstillinger kræver genstart bagefter |
| `T` | Testbesked til/fra i den sidst valgte samtale (kun DM eller privat kanal) |
| `J` | List de gemte spor |
| `G` | Test hentning fra GitHub (tastaturkatalog og seneste `version.txt`) |
| `W` / `w` | Send / slet et testwaypoint (kun på kanalen DMtest) |
| `I` | Rapport-hoved: version, opstartsårsag og seneste nedbrud |
| `d` / `D` | Send de 3 nyeste / alle SD-logs over USB (`tools/fetch_logs.py`) |
| `L` | Skriv loggen til SD nu |
| `M` / `P` / `X` | Selvtest: beskedlager / profil-eksport / Meshtastic |
| `R` + `JA` | Nulstil DualMesh (sletter kun `/dualmesh/` på SD-kortet) |
| `U` + `LAGER` | Genstart med SD-kortet som USB-drev (se "SD-kort") |
| `F` + `FORMATER` | Formatér SD-kortet (sletter alt på kortet) |

### MeshCore-appen (Bluetooth, appens kommandolinje)

Virker, når T-Deck'en er parret med appen (Indst. > Bluetooth, App = MeshCore). Svarene følger
MeshCores egen companion-firmware, så appens WiFi-skærm virker.

| Kommando | Gør |
| --- | --- |
| `get name` / `board` / `ver` / `clock` | Nodenavn / enhed / firmwareversion / ur |
| `reboot` | Genstart |
| `get wifi.status` / `get wifi.ip` | Forbundet eller ej / IP-adresse |
| `get wifi.enabled` / `set wifi.enabled 0\|1` | WiFi fra/til |
| `get wifi.ssid` / `set wifi.ssid <navn>` | Netværket (sæt koden bagefter) |
| `get wifi.pwd` / `set wifi.pwd <kode>` | Læs koden / sæt koden, forbind og husk nettet |
| `set wifi.clear` | Glem alle gemte WiFi-net |

Andre kommandoer svarer "Unknown command" som i MeshCore. Koder skrives aldrig i loggen.

### MQTT-broen

Slås til under Indst. > MQTT. `<rod>` er Emne-rod (standard `dualmesh/<3 byte af MAC>`),
`<net>` er `meshcore` eller `meshtastic`, og kanalnavne URL-kodes (`#test` = `%23test`).

| Emne | Retning | Indhold |
| --- | --- | --- |
| `<rod>/rx/<net>/chan/<kanal>` | ud | Modtaget kanalbesked (JSON med afsender, tekst, hop, tid, rssi, snr) |
| `<rod>/rx/<net>/dm/<afsender>` | ud | Modtaget DM (MeshCore: nøglens første 6 byte i hex, Meshtastic: `!nodeid`) |
| `<rod>/tx/<net>/chan/<kanal>` | ind | Send tekst på kanalen (kun med "Må sende fra MQTT"; ren tekst eller `{"text":"..."}`) |
| `<rod>/tx/<net>/dm/<modtager>` | ind | Send DM (navn, nøglepræfiks i hex eller `!nodeid`) |
| `<rod>/result` | ud | Svar på hver tx: sendt eller afvist med årsag |
| `<rod>/status` | ud | Status (retained); `{"online":false}`, når T-Deck'en forsvinder |

Beskeder, der modtages, før broen er forbundet (fx lige efter opstart), gemmes (højst 16) og
sendes, når forbindelsen er oppe. Afsendelse fra MQTT er højst én besked pr. 10 s, og radioens
fælles 10 %-grænse gælder.

## SD-kort

Alle DualMesh-filer ligger i `/dualmesh/` på kortet: `settings.ini`, beskeder pr. samtale,
kontakter, kanaler, logfiler og eksporterede profiler. Uden SD-kort virker alt, blot uden at
gemme. Intern NVS bruges kun af ESP-IDF selv (Bluetooth-parring, WiFi-kalibrering).

**SD-kortet som USB-drev:** Indst. > System > "SD som USB-drev" (klik to gange) eller monitor
`U` + `LAGER`. T-Deck'en genstarter uden radio og net, og kortet vises som et drev på PC'en, så
filer kan lægges på uden at tage kortet ud (fx `/dualmesh/mqtt_ca.pem` eller `presets.txt`).
Skub drevet ud på PC'en, så genstarter T-Deck'en normalt (trackball-klik virker også, men skub
ud først, ellers kan filer gå tabt). Den serielle port er væk, mens drevet er tilsluttet.

**Kort, spor og geocaches på kortet:**

| Mappe/fil | Indhold |
| --- | --- |
| `/dualmesh/maps/*.pmtiles` | Kortpakker (rasterfliser), lavet med `tools/maps/make_map.py` |
| `/dualmesh/maps/points.txt` | Gemte prikker og Meshtastic-waypoints |
| `/dualmesh/tracks/` | Spor som `.csv` (under optagelse) og `.gpx` |
| `/dualmesh/caches/*.gpx` | Geocaches (egne GPX-filer og `opencaching.gpx`) |
| `/dualmesh/caches/found.txt` | Fundet/ikke fundet/slettet; `geocache_visits.txt` = field notes |

En kortpakke laves på en PC ud fra OpenStreetMap-data (Protomaps-builds, kun de byte der skal
bruges), fx:

```bash
python tools/maps/make_map.py --bbox 8.0,54.5,15.25,57.8 --zoom 0-15 --name Danmark --cache cache_dk
```

Danmark i zoom 0-15 fylder ca. 1,65 GB (hold pakker under 2 GB). Læg den på med USB-drevet.

**Nye MeshCore-presets uden ny firmware:** læg en tekstfil `/dualmesh/meshcore/presets.txt` på
kortet med én preset pr. linje (`#` = kommentar):

```
# titel;frekvens_hz;bw_hz;sf;cr[;path_hash_size[;forældet 0/1]]
Mit område;869618000;62500;8;8;2
```

Samme titel som en indbygget preset erstatter den; nye titler kommer nederst i listen under
Indst. > MeshCore > Preset. Filen læses ved opstart (`[RADIO] presets.txt: …` i loggen; højst 32
linjer og 8 KB). Ugyldige linjer springes over.

## Kryptering

### Hemmelige filer på SD-kortet

Disse filer krypteres med **AES-256-GCM** (krypteret og beskyttet mod ændringer):

| Fil | Indhold |
| --- | --- |
| `meshcore/identity.txt` | MeshCore-identiteten (den private nøgle) |
| `meshcore/channels.txt` | MeshCore-kanalernes nøgler |
| `meshtastic/channels.txt` | Meshtastic-kanalernes nøgler (PSK) |
| `wifi.txt` | Gemte WiFi-net og deres koder |
| `mqtt.txt` | Koden til MQTT-brokeren |
| `ble.txt` | Bluetooth-PIN'en til parring med MeshCore-appen |

Nøglen vælges under **Indst. → System → SD-kryptering**:

| Tilstand | Nøglen kommer fra | Beskytter mod |
| --- | --- | --- |
| **fra** | – (almindelig tekst) | Intet |
| **chip-ID** (standard) | Chippens unikke ID og MAC-adresse (SHA-256) | Et tabt eller kopieret SD-kort. Ikke mod den, der har selve T-Deck'en |
| **kode** | En kode, du skriver ved hver opstart (PBKDF2-HMAC-SHA256, 30.000 runder, tilfældig salt) | Både et tabt kort og en tabt T-Deck. Indtil koden er skrevet, starter MeshCore, kanaler, WiFi og Bluetooth ikke, og USB-monitoren kan kun vise statistik |
| **eFuse** | En hemmelig nøgle brændt ind i chippen; firmwaren kan kun bruge den, ikke læse den (HMAC) | Et tabt kort; nøglen kan ikke kopieres ud af chippen. At brænde en ny nøgle er **uomvendeligt** og kræver, at du skriver BRUG EFUSE |

- Ingen nøgle gemmes på SD-kortet eller i den interne NVS. `crypto.txt` indeholder kun salt
  og et kontroltal til kode-tilstanden.
- Skifter du tilstand, læses alle hemmelige filer med den gamle nøgle og skrives med den nye.
- En fil, der ikke kan dekrypteres (forkert kode, andet kort eller anden T-Deck), bliver
  **aldrig overskrevet**. MeshCore kører så med en midlertidig identitet, indtil problemet er
  løst.
- Gamle ukrypterede filer krypteres automatisk ved opstart, og den gamle udgave af en
  hemmelig fil overskrives med nuller, før den slettes. SD-kortets egen slidudjævning kan dog
  have gemt en kopi et andet sted: har en hemmelighed ligget ukrypteret på kortet, er det
  sikreste at skifte den (ny identitet, nye koder).
- Koden er kun så stærk som den er lang: en angriber med SD-kortet kan prøve koder offline.
  Brug mindst 10–12 tegn.

**Ikke krypteret:** `settings.ini`, beskederne (`msgs/`), kontakter, kontaktfarver og
logfilerne. Logfilerne indeholder blandt andet GPS-positioner og modtagne beskeder, så pas
på, hvem der får et SD-kort med logs.

### Profil-eksport

**Indst. → Profil → Eksportér profil** samler identitet, navne, kanaler, kontakter og
WiFi-net i én fil (`/dualmesh/eksport/<navn>-<dato>.dmp`), krypteret med **AES-256-GCM** og
en adgangskode på mindst 6 tegn (nøglen afledes med PBKDF2-HMAC-SHA256, 30.000 runder og
tilfældig salt; ældre eksportfiler kan stadig importeres).
Filen kontrolleres, før den gamle enhed får en ny identitet. Ved import skrives indholdet
med den SD-kryptering, der er valgt på den nye enhed. Indstillingerne (`settings.ini`)
følger ikke med.

### Over radioen og Bluetooth

- **MeshCore:** kanalbeskeder er krypteret med kanalens nøgle; DM'er med en nøgle, som kun
  du og modtageren kender (ud fra jeres offentlige nøgler). #Public og #hashtag-kanaler har
  kendte nøgler og er reelt offentlige.
- **Meshtastic:** krypteret med kanalens PSK. Standardnøglen (`AQ==`, fx på LongFast) er
  offentlig, så alle kan læse med.
- **Bluetooth (MeshCore-appen):** forbindelsen kræver parring med en PIN-kode (Indst. →
  Bluetooth). PIN'en gemmes krypteret og vises kun på skærmen under Bluetooth-status.
  Appen kan læse WiFi-koderne, men kun når T-Deck'en er parret. Parringsnøglerne gemmer
  ESP-IDF i chippens interne NVS (ikke på SD-kortet).
- Koder og PIN skrives aldrig i loggen, og logfiler committes aldrig til git
  (`.gitignore`).

Logfiler (`/dualmesh/logs/NNNN.log`, én pr. opstart) kan hentes over USB:

```bash
python tools/fetch_logs.py COM5 logs/sd
```

## Fejlrapporter

Indst. > System > "Fejlrapport" viser en QR-kode, der åbner et udfyldt GitHub-issue med kun
version, opstartsårsag og nedbrudsadresser. Vil du vedlægge en log, så brug
[fejlrapport-siden](https://geekgarage.github.io/dualmesh/rapport/?lang=da): den henter loggen over
USB (Chrome/Edge) eller fra SD-kortet og fjerner navne, beskeder, positioner, nøgler og adresser
i din browser, før noget deles.

## Mapper

| Mappe | Indhold |
| --- | --- |
| `src/main.cpp` | Opstart, radio-task, statistik og den serielle monitor |
| `src/hal/` | SX1262-driver (direkte registre) og GPS |
| `src/net/` | MeshCore-node, Meshtastic-node, beskedlager, WiFi, BLE, MQTT, Opencaching |
| `src/map/` | Kort: PMTiles-læser, PNG-afkoder, prikker, spor og geocaches |
| `lang/` | Sprogfiler (`en.txt`); dansk er kildesproget (`tools/gen_lang.py`) |
| `src/storage/` | SD-kort (indstillinger, log, kryptering, profil, egen skrivedriver), NVS-værktøj |
| `src/ui/` | Brugerfladen (LVGL), input, skærmdriver; `src/ui/lv/fonts/` er genererede fonte |
| `lib/dualmesh/src/` | Hardware-uafhængig kerne: scheduler, LoRa-tidsberegning, presets for begge net |
| `lib/meshcore/`, `lib/ed25519/` | Låst kopi af de MeshCore-filer, der bruges (opdateres med `tools/sync_meshcore.py`) |
| `host/` | Simulator og test, der kører på PC (CMake + Ninja) |
| `tools/` | Generatorer (fonte, emojis, tastaturlayouts, sprog), kortpakker (`tools/maps/`) og loghentning |
| `assets/logo/` | DualMesh-logoet (SVG, PNG, firmware-headere) |
| `fonts/` | Kildefonte med licenser |
| `docs/rapport/`, `docs/flash/` | Fejlrapport-siden og flash-siden (GitHub Pages) |
| `.github/workflows/` | Udgivelsesbyg (tag `v*`) og GitHub Pages |
| `keyboards/`, `lang/`, `maps/` | Kataloger (`index.txt`), som T-Deck'en henter fra |

Simulator og test på PC:

```bash
cmake -S host -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ && cmake --build build && ./build/sim && ./build/test_phy && ./build/test_i18n
```

## Licenser og tak

DualMesh er MIT. Den fulde liste med licenser står i `THIRD_PARTY_NOTICES.md`. Den bygger på:

- [MeshCore](https://github.com/meshcore-dev/MeshCore) (MIT) – udvalgte filer i `lib/meshcore/`
- ed25519 af Orson Peters (zlib) – `lib/ed25519/`
- [LVGL](https://lvgl.io) 8.4 (MIT)
- rweather/Crypto (MIT) og densaugeo/base64 (MIT)
- Fonte under SIL Open Font License 1.1: Montserrat, JetBrains Mono, Noto Emoji og
  Noto Color Emoji (licenserne ligger i `fonts/*/OFL.txt`)

Kortdata: © OpenStreetMap-bidragydere (ODbL, openstreetmap.org/copyright) og Natural Earth
(public domain), via Protomaps' builds; fliserne tegnes af DualMesh selv og hentes aldrig fra
OpenStreetMaps egne flise-servere. Geocaches fra Opencaching via deres åbne API (OKAPI) med
brugerens egen nøgle.

Meshtastic-delen er skrevet ud fra den offentlige protokoldokumentation; der bruges ingen kode,
`.proto`-filer eller genereret kode fra Meshtastic-projektet (GPL-3.0).
