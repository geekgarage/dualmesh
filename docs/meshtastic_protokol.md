# Meshtastic-protokollen – fakta til DualMesh

Dette dokument beskriver **fakta** om Meshtastics radioprotokol, som DualMesh skal kende for at
kunne tale med Meshtastic-noder: byteformater, konstanter, feltnumre og trinene i algoritmerne.
Det er skrevet med egne ord ud fra den offentlige dokumentation og en analyse af Meshtastic-
firmwaren (v2.8.2, GPL-3.0) efter beslutningen 17. nov 2026 om clean room (se
`CLAUDE.md`). Dokumentet indeholder ingen kode, kommentarer eller struktur fra Meshtastic.
DualMesh' kode skrives ud fra dette dokument. Når Meshtastic-delen er færdig, sammenlignes vores
kode med Meshtastics for at sikre, at GPL-licensen ikke er brudt.

Alle heltal er little-endian, medmindre andet står.

## 1. Radiopakke

16 byte header i klartekst, derefter den krypterede payload (højst 239 byte, i alt 255).

| Byte | Felt |
| --- | --- |
| 0-3 | `to`: modtagerens nodenummer; `0xFFFFFFFF` = alle |
| 4-7 | `from`: afsenderens nodenummer |
| 8-11 | `id`: pakke-id (tilfældigt, ikke 0) |
| 12 | flag: bit 0-2 hop_limit, bit 3 want_ack, bit 4 via_mqtt, bit 5-7 hop_start |
| 13 | kanal-hash; 0 ved PKI-krypterede DM'er |
| 14 | next_hop (sidste byte af næste hop; 0 = flood) |
| 15 | relay_node (sidste byte af den node, der sendte pakken sidst) |

Hops = hop_start - hop_limit.

## 2. Kanalkryptering (AES-CTR)

- Nøgle: kanalens PSK (16 byte = AES-128, 32 byte = AES-256, 0 = ingen kryptering). PSK på 1 byte:
  0 = ingen, 1 = standardnøglen, 2-255 = standardnøglen med sidste byte øget med (værdi - 1).
- Nonce (16 byte): byte 0-7 pakke-id som 64-bit tal, byte 8-11 afsenderens nodenummer, byte 12-15
  bloktæller (starter i 0, 4 byte tæller).
- Kanal-hash: XOR af navnets bytes XOR XOR af nøglens bytes. Tomt navn = presetnavnet.
- (2.8.1+, eksperimentelt: AES-CCM-kanal med 12 byte tag, AAD = from og to; ikke brugt endnu.)

## 3. Nodeidentitet og nodenummer

- Hver node har et X25519-nøglepar. Den private nøgle "clampes" før brug: byte 0 `&= 0xF8`,
  byte 31 `= (byte 31 & 0x7F) | 0x40`. Den offentlige nøgle = X25519(privat, basispunktet 9).
- **Fra 2.8: nodenummer = CRC-32 af den 32 byte offentlige nøgle** (standard CRC-32 som i
  zlib/IEEE 802.3: polynomium 0xEDB88320 reflekteret, start 0xFFFFFFFF, slut-XOR 0xFFFFFFFF).
  Før der findes en nøgle, og i ældre versioner (målt: 2.7.26), er nodenummeret de sidste 4 byte
  af MAC-adressen (fx MAC ..:..:aa:bb:cc:dd = !aabbccdd). Nodenummeret er vilkårligt for andre
  noder: 2.7.26 godtog vores CRC-baserede nummer uden videre (målt 17. nov 2026).
- Nodens id-tekst er `!` + nodenummeret som 8 hex-cifre med små bogstaver.

## 4. PKI-krypterede DM'er (fra 2.5)

- Bruges kun til pakker, vi selv sender til én bestemt node, når vi kender dens offentlige nøgle,
  og ikke til portene TRACEROUTE (70), NODEINFO (4), ROUTING (5) og POSITION (3).
- Fælles nøgle = SHA-256( X25519(vores private nøgle, modtagerens offentlige nøgle) ), 32 byte.
- Kryptering: **AES-256-CCM** (RFC 3610) med L = 2 (13 byte nonce), tag M = 8 byte, ingen AAD.
- Nonce: start med den 16 byte kanal-nonce (pakke-id 64 bit, afsender 32 bit); et tilfældigt
  4 byte "ekstra nonce" skrives over byte 4-7. CCM bruger de første 13 byte.
- Payload på radioen: ciffertekst || tag (8 byte) || ekstra nonce (4 byte). Overhead 12 byte.
- Kendes modtagerens nøgle ikke (fx ældre firmware før 2.5, hvis NodeInfo ikke har public_key),
  sendes DM'en som en almindelig kanalpakke (AES-CTR med kanalens nøgle, `to` = modtageren);
  alle på kanalen kan da læse den.
- Headerens kanal-hash er 0. Modtageren prøver PKI, når hash = 0, `to` = os, og pakken er
  længere end 12 byte payload; den bruger afsenderens kendte offentlige nøgle. Lykkes tag-tjekket
  ikke, er pakken ikke til os/ikke læsbar.

## 5. Data (protobuf inde i den dekrypterede payload)

| Felt | Navn | Type |
| --- | --- | --- |
| 1 | portnum | varint |
| 2 | payload | bytes |
| 3 | want_response | varint (bool) |
| 4 | dest | fixed32 |
| 5 | source | fixed32 |
| 6 | request_id | fixed32 |
| 7 | reply_id | fixed32 |
| 8 | emoji | fixed32 (reaktion, når ≠ 0) |
| 9 | bitfield | varint (bit 0 ok_to_mqtt, bit 1 want_response) |
| 10 | xeddsa_signature | bytes (64) |

**bitfield er påkrævet i praksis (2.8, set 22. nov 2026):** firmware fra 2.5 sætter altid
bitfield i egne pakker. En modtaget pakke med hop_start = 0 og uden bitfield regnes for at komme
fra firmware før 2.3 (som ikke kendte hop_start) og smides væk efter dekodning ("hop_start
invalid/missing"). Det rammer især ACK'er med hop_limit 0. Send derfor altid felt 9 (også med
værdien 0); i signaturen tæller det som "bitfield findes" (afsnit 6).

Porte: 1 tekst, 3 Position, 4 NodeInfo (User), 5 Routing, 67 Telemetri, 70 Traceroute.

User (port 4): 1 id (tekst), 2 long_name, 3 short_name, 4 macaddr, 5 hw_model, 6 is_licensed,
7 role, 8 public_key (32 byte), 9 is_unmessagable. hw_model 50 = T-Deck, 43 = Heltec V3.
Roller: 0 CLIENT, 1 CLIENT_MUTE, 2 ROUTER, 4 REPEATER, 5 TRACKER, 6 SENSOR.

Position (port 3): 1 latitude_i (sfixed32, grader × 1e7), 2 longitude_i, 3 altitude (int32),
4 time (fixed32), 23 precision_bits (32 = præcis).

Routing (port 5): 1 route_request, 2 route_reply, 3 error_reason (varint). Fejlkoder: 0 NONE (=
ACK), 1 NO_ROUTE, 2 GOT_NAK, 3 TIMEOUT, 5 MAX_RETRANSMIT, 6 NO_CHANNEL, 7 TOO_LARGE, 8
NO_RESPONSE, 9 DUTY_CYCLE_LIMIT, 32 BAD_REQUEST, 33 NOT_AUTHORIZED, 34 PKI_FAILED, 35
PKI_UNKNOWN_PUBKEY, 36 ADMIN_BAD_SESSION_KEY, 37 ADMIN_PUBLIC_KEY_UNAUTHORIZED (admin fra en
nøgle, noden ikke har som admin-nøgle). Routing har også et nyere valgfrit `ack_proof`-felt (se 8).

Telemetri (port 67): 1 time, 2 device_metrics (1 battery_level, 2 voltage float, 3
channel_utilization, 4 air_util_tx, 5 uptime_seconds).

## 6. XEdDSA-signatur (fra 2.8)

- Signaturen ligger i Data felt 10 (64 byte). Den dækker en fast opbygget buffer (ikke selve
  protobuf'en): version (1 byte, 0x01) | from | id | to | portnum | request_id | reply_id |
  emoji | bitfield (0 hvis ikke sat) (alle 4 byte LE) | flag (1 byte: bit 0 want_response, bit 1
  bitfield findes) | Data.payload. Headerlængde 34 byte.
- Nøglen er nodens X25519-nøgle omsat til Ed25519 efter XEdDSA-specifikationen (Signal): skalar a
  = den clampede private nøgle, A = a·B; har A fortegnsbit 1, bruges -a (mod gruppeordenen), så A
  altid har fortegnsbit 0. Signering: r = SHA-512(0xFE || 31 × 0xFF || a || M || Z) mod q (Z =
  64 tilfældige byte), R = r·B, h = SHA-512(R || A || M) mod q, s = r + h·a mod q, signatur = R || s.
- Verifikation = almindelig Ed25519-verifikation med A, hvor A udledes af den offentlige
  X25519-nøgle u som y = (u - 1)/(u + 1) mod p med fortegnsbit 0.
- **Første kontakt:** en node, der endnu ikke kender vores nøgle, accepterer den fra en signeret
  NodeInfo, når signaturen passer, og CRC-32 af nøglen = afsenderens nodenummer. Usignerede
  NodeInfo accepteres stadig i standardpolitikken (TOFU), medmindre noden tidligere har signeret
  ("Balanced") eller politikken er "Strict".
- En kendt node, der pludselig har en anden nøgle, ignoreres.
- Traceroute signeres ikke (hvert hop ændrer den).

## 7. ACK, NAK og gensendelse

- En pakke med want_ack til os besvares med Routing (port 5) til afsenderen: error_reason 0 (ACK)
  og `request_id` = den modtagne pakkes id. Routing sendes aldrig PKI-krypteret, men på kanalen
  (primær kanal ved fejl som NO_CHANNEL og PKI_UNKNOWN_PUBKEY).
- Observeret med 2.8.2 (17. nov 2026): en NAK (fx 37 på en admin-forespørgsel) kommer med
  want_ack og gensendes, til den selv kvitteres med en ACK. En ACK (fejl 0) har aldrig want_ack.
- Kan en want_ack-pakke til os ikke dekrypteres med PKI, svares med NAK PKI_UNKNOWN_PUBKEY (35);
  kan den slet ikke afkodes, NO_CHANNEL (6), begge på den primære kanal.
- Afsenderen gensender en want_ack-pakke, til der kommer ACK/NAK med request_id = pakkens id
  (unicast: 3 forsøg i alt). Hører afsenderen sin egen pakke blive sendt videre af en anden node,
  er det en "implicit ACK" for broadcast og stopper gensendelser (for DM: kun leveret til nettet,
  ikke til modtageren).
- **Next-hop-routing (2.6+, set med 2.8.2 22. nov 2026):** en DM med `next_hop` sat (byte 14 ≠ 0)
  gensendes af afsenderen (3 forsøg i alt), også når want_ack er 0, fx et svar på en
  forespørgsel. Er pakken til os, og er `next_hop` = sidste byte af vores nodenummer og
  hop_limit > 0, svarer modtageren med en ACK (Routing, fejl 0, request_id = pakkens id) med
  hop_limit 0, på samme kanal som pakken (ved PKI: den primære). Ellers gentages pakken, og
  afsenderen melder til sidst MAX_RETRANSMIT til sig selv.
- Ventetid før gensendelse ≈ 2 × pakkens sendetid + konkurrencevinduets slots + behandlingstid
  (for LongFast typisk nogle sekunder).

## 8. ACK-bevis (Routing.ack_proof, 2.8, kun vejledende)

- bevis = de første N byte af HMAC-SHA256(fælles nøgle, "ack" || ackFrom || ackTo || request_id
  || Routing-bytes uden ack_proof). Kun til PKI-pakker. I 2.8.2 accepteres en ACK uden bevis, så
  feltet er valgfrit for os indtil videre.

## 9. Videresendelse (flood)

- Roller: CLIENT (0) sender andres pakker videre; CLIENT_MUTE (1) aldrig. Desuden en
  "rebroadcast mode": ALL (alt, også pakker på kanaler, noden ikke kan læse), LOCAL_ONLY (kun
  pakker, noden kan afkode på egne kanaler, samt PKI-DM'er mellem kendte noder), NONE.
- Betingelser for at sende videre: pakken er hverken til os eller fra os, hop_limit > 0, id ≠ 0,
  den er ikke set før (historik over (from, id)), og next_hop er 0 (flood) eller den sidste byte
  af vores nodenummer.
- Den videresendte pakke er den samme (samme id, samme krypterede payload); kun headeren ændres:
  hop_limit - 1, hop_start uændret, want_ack/via_mqtt uændret, relay_node = sidste byte af vores
  nodenummer. Sender vi efter next_hop, sættes next_hop til vores kendte næste hop eller 0 (flood).
- Ventetid før videresendelse (CLIENT): 2 × CWmax × slot + tilfældig(0 .. 2^CW - 1) × slot, hvor
  CW = lineær omregning af modtaget SNR fra [-20, 10] dB til [CWmin = 3, CWmax = 8] (svagt
  signal = kort ventetid). slot = (NUM_SYM_CAD + 0,5) × Tsym + 7,6 ms med NUM_SYM_CAD = 2
  (mindst 2,25 symboler); Tsym = 2^SF / BW (ms, BW i kHz). LongFast: Tsym 8,19 ms, slot ≈ 28 ms,
  ventetid ≈ 0,45-7,6 s. ROUTER venter kun tilfældig(0 .. 2 × CW - 1) × slot.
- Hører en CLIENT pakken sendt af en anden, mens dens egen kopi venter, aflyses kopien. Hører den
  en kopi med flere hop tilbage end den ventende, erstattes den ventende med den nye (stadig
  hop_limit - 1).
- Hører noden et svar/ACK (Data med request_id) på en DM, der ikke er til den, aflyses en ventende
  videresendelse af den oprindelige DM (from = svarets to, id = request_id).
- Gentager den oprindelige afsender selv pakken (hop_start = hop_limit, dvs. 0 hop), og er vores
  kopi allerede sendt, sendes den videre igen; er pakken til os med want_ack, sendes ACK igen.
- Svar (fx ACK) bruger hop_limit = brugte hop + 2, dog højst den normale hop_limit (3).
- Ventetid før gensendelse af egne pakker: 2 × sendetid + (2^CW + 2 × CWmax + 2^5) × slot +
  4500 ms, CW efter kanalens udnyttelse (3-8).

## 10. Egen telemetri (port 67)

- Telemetry: 1 time (fixed32, unix-tid), 2 device_metrics: 1 battery_level (varint, 0-100; 101 =
  strømforsyning/lader), 2 voltage (float, V), 3 channel_utilization (float, %), 4 air_util_tx
  (float, %), 5 uptime_seconds (varint).
- Sendes som broadcast uden want_response, standard hvert 30. minut (1800 s). Ingen position.
- En forespørgsel (Telemetry med want_response) besvares med vores DeviceMetrics til spørgeren
  (request_id = forespørgslens id); en multi-hop broadcast-forespørgsel besvares kun af
  ROUTER/SENSOR.

## 11. Traceroute (port 70)

- Payload er RouteDiscovery: 1 route (gentaget fixed32, nodenumre mod målet), 2 snr_towards
  (gentaget int32, SNR × 4 pr. hop mod målet), 3 route_back (gentaget fixed32, tilbagevejen),
  4 snr_back (gentaget int32). Gentagne tal sendes normalt "packed" (felt med længde), men en
  modtager skal også kunne læse dem enkeltvis. Negative int32 er 10 byte varint. Højst 8 pr. liste.
- Forespørgsel: Data port 70 med want_response, request_id 0, sendt direkte til målet (aldrig
  PKI-krypteret, aldrig signeret). Hver node, der sender den videre, ændrer payload: først
  indsættes "ukendte hop", så antallet af ruteposter = antal hop pakken har taget (ukendt node =
  0xFFFFFFFF, ukendt SNR = -128), derefter tilføjes nodens egen SNR (modtaget SNR × 4, afrundet,
  -127..127) og dens nodenummer.
- Målet gør det samme, men tilføjer kun sin SNR (ikke sit nummer), og svarer til afsenderen med
  RouteDiscovery i en Data port 70 med request_id = forespørgslens id. På tilbagevejen bruges
  route_back/snr_back på samme måde; afsenderen tilføjer til sidst sin egen SNR til snr_back.
- En broadcast-forespørgsel, der allerede har taget hop (hop_limit < hop_start), besvares ikke.

## 12. Telefon-API over Bluetooth (appen)

**GATT-tjeneste** `6ba1b218-15a8-461f-9fa8-5dcae273eafd` med tre karakteristikker:
ToRadio `f75c76d2-129e-4dad-a1dd-7866124401e7` (skriv: én ToRadio-protobuf pr. skrivning),
FromRadio `2c55e69e-4993-11ed-b878-0242ac120002` (læs: én FromRadio pr. læsning; tom = intet nyt;
under opstartsforløbet må en læsning ikke være tom), FromNum `ed9da18c-a800-4f66-a670-aa7547e34453`
(læs/notify: uint32 tæller, notificeres når der er nyt i FromRadio). Parring med PIN.

**ToRadio:** 1 packet (MeshPacket), 3 want_config_id (uint32), 4 disconnect (bool), 7 heartbeat.
**FromRadio:** 1 id, 2 packet, 3 my_info, 4 node_info, 5 config, 7 config_complete_id, 8 rebooted,
9 moduleConfig, 10 channel, 11 queueStatus, 13 metadata.

**Opstartsforløb** efter want_config_id = n: my_info, egen node_info, metadata, channel for hver
af de 8 pladser (også tomme, role DISABLED), config (én pr. type), moduleConfig, de andre noders
node_info, config_complete_id = n. Derefter sendes modtagne pakker som FromRadio.packet.
Nyere apps beder i to runder: n = 69420 betyder kun konfigurationen (alt ovenfor undtagen de
andre noders node_info), n = 69421 kun noderne (egen node_info, de andre noders, så complete).
Andre værdier: det hele i én runde. Heartbeat (ToRadio 7): nonce 1 = noden udsender sin
NodeInfo; ellers svarer noden med en queueStatus ved appens næste læsning (den går forud for
alt andet). Uden det svar lukker Meshtastic-appen (Android) forbindelsen efter ca. 87 s
(observeret: HCI-årsag 0x13 fra telefonen) og forbinder igen.

**QueueStatus** (FromRadio 11), sendes efter hver pakke fra appen: 1 res (int32, 0 = ok),
2 free (ledige pladser), 3 maxlen, 4 mesh_packet_id (pakkens id).

**MeshPacket:** 1 from (fixed32), 2 to (fixed32), 3 channel (indeks), 4 decoded (Data), 5 encrypted,
6 id (fixed32), 7 rx_time (fixed32), 8 rx_snr (float), 9 hop_limit, 10 want_ack, 11 priority,
12 rx_rssi (int32), 14 via_mqtt, 15 hop_start, 16 public_key, 17 pki_encrypted, 18 next_hop,
19 relay_node.
**MyNodeInfo:** 1 my_node_num, 8 reboot_count, 11 min_app_version (30200), 12 device_id (bytes),
13 pio_env, 14 firmware_edition, 15 nodedb_count.
**NodeInfo:** 1 num, 2 user (User), 3 position, 4 snr (float), 5 last_heard (fixed32), 6
device_metrics (DeviceMetrics), 7 channel, 9 hops_away, 10 is_favorite.
**DeviceMetadata:** 1 firmware_version (tekst), 2 device_state_version, 3 canShutdown, 4 hasWifi,
5 hasBluetooth, 7 role, 8 position_flags, 9 hw_model, 11 hasPKC, 12 excluded_modules.
**Channel:** 1 index, 2 settings (ChannelSettings: 2 psk, 3 name, 4 id, 5 uplink, 6 downlink), 3
role (0 DISABLED, 1 PRIMARY, 2 SECONDARY).
**Config:** 1 device (1 role, 6 rebroadcast_mode, 7 node_info_broadcast_secs), 2 position (1
position_broadcast_secs, 4 gps_enabled), 6 lora (1 use_preset, 2 modem_preset, 7 region, 8
hop_limit, 9 tx_enabled, 10 tx_power, 11 channel_num), 7 bluetooth (1 enabled, 2 mode, 3 fixed_pin),
8 security (1 public_key, 9 packet_signature_policy). ConfigType: 0 device, 1 position, 2 power,
3 network, 4 display, 5 lora, 6 bluetooth, 7 security.
**Appens egne indstillinger** sendes som AdminMessage (port 6) til vores eget nodenummer med
want_response; svaret er en AdminMessage fra os med request_id: 1 get_channel_request (indeks + 1),
2 get_channel_response, 3 get_owner_request, 4 get_owner_response (User), 5 get_config_request
(ConfigType), 6 get_config_response, 12 get_device_metadata_request, 13 ..._response, 32 set_owner,
33 set_channel, 34 set_config, 38 remove_by_nodenum, 39 set_favorite_node, 43 set_time_only,
64 begin_edit_settings, 65 commit_edit_settings, 97 reboot_seconds.
**Leveringsstatus til appen:** for en pakke med want_ack sendes en Routing (port 5, request_id =
pakkens id) fra modtageren (ACK/NAK), eller, for broadcast, en lokalt lavet Routing med fejl 0, når
vi hører pakken sendt videre ("implicit ACK"); opgives den efter gensendelser: fejl 5
(MAX_RETRANSMIT).

## 13. Waypoint (port 8)

1 id (uint32), 2 latitude_i (sfixed32, grader × 1e7), 3 longitude_i, 4 expire (unix-tid, 0 =
aldrig), 5 locked_to (nodenummer, 0 = alle må ændre), 6 name (≤ 30), 7 description (≤ 100), 8 icon
(fixed32, Unicode-tegn, fx en emoji). Sendes normalt som broadcast på en kanal. Samme id = opdatering;
expire i fortiden = slet.
