// SPDX-License-Identifier: MIT
#include "ui/settings_ui.h"

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "esp_heap_caps.h"
#include "presets_meshcore.h"
#include "presets_meshtastic.h"
#include "storage/sd_log.h"
#include "storage/sd_store.h"
#include "i18n.h"
#include "ui/lang.h"
#include "ui/status_display.h"

namespace dm {

// Hemmelige indstillinger: gemmes krypteret via on_secret_ (aldrig i settings.ini/loggen).
static bool isSecretKey(const char* k) { return !std::strcmp(k, "mqtt_pass") || !std::strcmp(k, "ble_pin"); }

namespace {
enum : uint8_t { kOptMtRegion, kOptMtPreset, kOptMcPreset, kOptPosShare, kOptTelemReq, kOptTelemData, kOptMtRelay, kOptBleApp, kOptLang };
// Telemetri (som MeshCore-appen): svar til nej/alle/kontakter med tilladelse; position og
// miljø med i svaret: nej/ja/kontakter med tilladelse.
const char* const kTelemReq[] = {"nej", "alle", "kontakter"};
const char* const kTelemData[] = {"nej", "ja", "kontakter"};
// Deling af egen position: fra (standard), præcis eller omtrent (ca. 1 km).
const char* const kPosShare[] = {"fra", "præcis", "omtrent"};
const char* const kMtRelay[] = {"fra", "egne kanaler", "alle"};
const char* const kBleApp[] = {"MeshCore", "Meshtastic"};

// Kopierer højst `cols` tegn (UTF-8-tegn, ikke bytes) og fylder op med mellemrum.
void padCols(char* out, size_t cap, const char* s, int cols) {
  size_t o = 0;
  int n = 0;
  for (const char* p = s; *p && o + 4 < cap;) {
    const unsigned char c = (unsigned char)*p;
    const int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    if (n == cols) break;
    for (int k = 0; k < len && p[k]; ++k) out[o++] = p[k];
    p += len;
    ++n;
  }
  while (n < cols && o + 1 < cap) {
    out[o++] = ' ';
    ++n;
  }
  out[o] = 0;
}
}  // namespace

// Forklaring pr. indstilling (vises i række 9-12 under listen; højst 160 tegn = 4 linjer,
// gerne med et konkret eksempel på en værdi).
struct HelpText {
  const char* key;
  const char* text;
};
const HelpText kHelp[] = {
    {">status", "Viser radio, net, tid, WiFi, GPS, batteri og modtagne pakker. Klik for at åbne; klik igen for at gå tilbage."},
    {"meshcore_name", "Dit navn på nettet: MeshCores navn og Meshtastics lange navn. Eksempel: DualMesh_Anna. Tom = DualMesh-XXXX."},
    {"meshtastic_short_name", "Meshtastics korte navn, højst 4 tegn, vises i lister og på kort. Eksempel: GEEK. Tom = automatisk."},
    {">profile_export", "Gemmer identitet, navne, kanaler, kontakter og WiFi i én fil med adgangskode. Bagefter får denne T-Deck en ny profil."},
    {">profile_import", "Henter en eksporteret profil (.dmp) fra /dualmesh/eksport/ eller SD-kortets rod og erstatter den nuværende."},
    {"meshcore_enabled", "Lyt og send på MeshCore. Fra: radioen bruger kun Meshtastic, og MeshCores indstillinger låses. Kræver genstart."},
    {"meshcore_preset", "MeshCores radio (frekvens, båndbredde, SF), som i MeshCore-appen. Rul for at vælge. Danmark: EU/UK (Narrow)."},
    {"meshcore_custom", "Egne radioværdier i stedet for preset: frekvens Hz, båndbredde Hz, SF, CR adskilt af komma. Eksempel: 869618000,62500,8,8. Tom = preset."},
    {"meshcore_tx", "Må T-Deck'en sende på MeshCore? Fra = kun lytte, fx hvis antennen er taget af. SOS respekterer også dette."},
    {"meshcore_share_position", "Din position i adverts: fra, præcis eller omtrent (afrundet til ca. 1 km). Rul for at vælge. SOS sender altid præcis position."},
    {"meshcore_advert_local_min", "Advert til direkte naboer (nul hop, sendes ikke videre) hvert N. minut. Eksempel: 30. 0 = fra."},
    {"meshcore_advert_flood_h", "Advert til hele nettet via repeatere hver N. time. Eksempel: 12. 0 = fra, ellers mindst 3 for ikke at belaste nettet."},
    {"meshcore_telemetry", "Må andre hente din telemetri (batteri, temperatur)? nej, alle, eller kun kontakter med tilladelse (Telem.-kontakter). Rul for at vælge."},
    {"meshcore_telemetry_pos", "Din GPS-position med i telemetrien: nej, ja eller kun kontakter med tilladelse. Præcision som Del position (omtrent = ca. 1 km)."},
    {"meshcore_telemetry_env", "Miljødata med i telemetrien: nej, ja eller kun kontakter med tilladelse. T-Deck'en har ingen miljøsensorer endnu."},
    {">telem_contacts", "Giv enkelte kontakter lov til telemetri (T), position (P) og miljø (M). Bruges ved valget \"kontakter\" ovenfor."},
    {">advert_local", "Send en advert til direkte naboer nu, så de kan se dig som kontakt. Belaster ikke nettet."},
    {">advert_flood", "Send en advert til hele nettet nu. Den sendes videre af alle repeatere, så brug den sjældent."},
    {"meshcore_tx_power", "Sendeeffekt på MeshCore i dBm, 1-22 (aldrig over regionens grænse). Eksempel: 22. Lavere = kortere rækkevidde, men mindre strøm."},
    {"meshcore_test_text", "Testbeskeden slås til med tandhjulet i en DM eller privat kanal. Højst 120 tegn. Tom = standardtekst."},
    {"meshcore_test_interval_min", "Minutter mellem testbeskeder, 2-1440. Eksempel: 15. Aldrig under 10 x sendetiden (højst 10 % airtime)."},
    {"meshtastic_enabled", "Lyt og send på Meshtastic. Fra: radioen bruger kun MeshCore, og Meshtastics indstillinger låses. Kræver genstart."},
    {"meshtastic_region", "Dit lands båndplan (frekvenser og max effekt). Rul for at vælge. Danmark: EU_868."},
    {"meshtastic_preset", "Modem-preset, skal være det samme som nettet omkring dig. Rul for at vælge. Mest brugt: LONG_FAST."},
    {"meshtastic_channel", "Navnet på den primære kanal, højst 11 tegn. Eksempel: LongFast. Tom = presetnavnet."},
    {"meshtastic_slot", "Frekvens-slot i båndet. 0 = beregnes ud fra kanalnavnet, som Meshtastic selv gør. Kun ændres, hvis nettet bruger et fast slot."},
    {"meshtastic_share_position", "Din position til Meshtastic: fra, præcis eller omtrent (ca. 1 km). Rul for at vælge."},
    {"meshtastic_tx", "Send på Meshtastic: tekst, NodeInfo og (hvis delt) position. Fra: der lyttes kun."},
    {"meshtastic_tx_power", "Sendeeffekt på Meshtastic i dBm, 1-22 (aldrig over regionens grænse). Eksempel: 22. Lavere = kortere rækkevidde, men mindre strøm."},
    {"meshtastic_position_min", "Hvor tit din position sendes, når Del position er slået til. Eksempel: 30. 0 = aldrig."},
    {"meshtastic_nodeinfo_h", "Hvor tit dit navn (NodeInfo) sendes, så andre kan se dig. Eksempel: 3. 0 = kun ved start og når nogen spørger."},
    {"meshtastic_relay", "Send andres pakker videre (rolle CLIENT). egne kanaler: kun dem, du kan læse. alle: også fremmede kanaler. Fra = CLIENT_MUTE."},
    {"meshtastic_telemetry_min", "Send batteri, spænding og oppetid (ingen position) hvert N. minut, og svar når nogen spørger. Eksempel: 30. 0 = fra."},
    {"meshtastic_data_channel", "Kanal for position og telemetri (navn, fx DMtest). Tom = primær. Findes den ikke, sendes intet. NodeInfo og SOS bruger altid den primære."},
    {"msgs_per_channel", "Hvor mange beskeder pr. kanal der gemmes på SD og vises i Chat, 10-1000. Eksempel: 200."},
    {"msgs_per_dm", "Hvor mange beskeder pr. DM der gemmes på SD og vises i Chat, 10-1000. Eksempel: 200."},
    {"msgs_days_channel", "Vis kun kanalbeskeder fra de sidste N dage. Eksempel: 7. 0 = alle. Antal eller dage: det, der rammer først."},
    {"msgs_days_dm", "Vis kun DM'er fra de sidste N dage. Eksempel: 30. 0 = alle. Antal eller dage: det, der rammer først."},
    {">colors", "Vælg farve for hver kontakt i Chat. Farven følger kontaktens nøgle, så en ny identitet får en ny farve."},
    {">about", "Firmwareversion, hvem der står bag, licenser og en QR-kode til projektet på GitHub."},
    {">report", "Seneste nedbrud og en QR-kode til et udfyldt GitHub-issue. Fuld log: sæt i USB og åbn siden, der vises."},
    {"!clear_chat", "Sletter den samtale, der er åben i Chat: fil på SD, hukommelse og ulæste. Klik 2 gange."},
    {"!clear_channels", "Sletter alle kanal-chats for begge net. DM'er bevares. Klik 2 gange."},
    {"sos_interval_min", "Minutter mellem SOS-udsendelser, mens SOS er aktiv, 2-60. Eksempel: 5. SOS startes ved at holde trackballen i 5 s."},
    {"track_interval_s", "Sekunder mellem punkter, mens et spor optages. Eksempel: 5. Kortere = mere præcist spor, større fil."},
    {"track_min_m", "Et punkt gemmes kun, når du har flyttet dig så mange meter. Eksempel: 10. Sparer plads, når du står stille."},
    {"map_sensitivity", "Hvor langt kortet flytter sig pr. ryk på trackballen, 1-10. Højere = hurtigere panorering. Eksempel: 5."},
    {"okapi_site", "Opencaching-sidens adresse. Eksempel: www.opencaching.de (også .pl, .nl, .ro, .us, opencache.uk). Tom = www.opencaching.de."},
    {"okapi_key", "Din gratis API-nøgle (consumer key) fra sidens /okapi/signup.html. Hver Opencaching-side har sin egen nøgle."},
    {"okapi_radius_km", "Caches inden for så mange km fra din position (eller kortets midte) hentes. Eksempel: 10. Højst 500 caches."},
    {"!okapi_fetch", "Henter caches fra Opencaching over WiFi og gemmer dem på SD (opencaching.gpx), så de også virker uden net."},
    {"gps_eco", "GPS'en går i dvale, når intet har brugt den i 1 min (skærm slukket, intet spor, ingen deling eller SOS). Nyt fix på få sekunder."},
    {"cpu_eco", "CPU'en kører langsommere (80 MHz), mens skærmen er slukket. Sparer strøm; radioen og net virker som før."},
    {"language", "Brugerfladens sprog. Rul for at vælge. Flere sprog: /dualmesh/lang/<id>.txt på SD. Kræver genstart."},
    {"screen_brightness", "Skærmens lysstyrke fra 1 (svag) til 16 (fuld). Lavere sparer strøm."},
    {"screen_timeout_s", "Skærmen dæmpes og slukker efter så mange sekunder uden input. Eksempel: 20. 0 = aldrig."},
    {"screen_timeout_usb", "Sluk også skærmen, når T-Deck'en er sluttet til USB. Fra: skærmen er tændt på USB."},
    {"touch_swap_xy", "Touch: byt X og Y, hvis et tryk rammer et helt andet sted end fingeren."},
    {"touch_invert_x", "Touch: spejl vandret, hvis tryk i venstre side rammer højre side."},
    {"touch_invert_y", "Touch: spejl lodret, hvis tryk øverst rammer nederst."},
    {"keyboard_backlight", "Tastaturlysets styrke ved tastetryk, 0-255. Eksempel: 127. 0 = aldrig tændt."},
    {"keyboard_backlight_timeout_s", "Sekunder før tastaturlyset slukker efter sidste tryk. Eksempel: 10."},
    {"keyboard_layouts", "Layouts for specialtegn (fx a -> å æ á), adskilt af komma. Eksempel: da,latin."},
    {">dl_keyboards", "Henter nye tastaturlayouts fra GitHub over WiFi. Et hentet layout tages i brug med det samme."},
    {">dl_maps", "Henter kortpakker fra GitHub over WiFi (store filer: brug WiFi, ikke mobildata). En afbrudt hentning genoptages."},
    {">dl_lang", "Henter flere sprog fra GitHub over WiFi. Vælg dem bagefter under Sprog / Language."},
    {">ota", "Finder den nyeste firmware på GitHub og installerer den over WiFi. Med en launcher gemmes den på SD-kortet."},
    {"keyboard_raw_mode", "Læs tastaturet direkte, så alle taster virker. Slå kun fra ved gammel tastaturfirmware. Kræver genstart."},
    {"trackball_sensitivity", "Hvor følsom trackballen er, 1-10. 10 = et lille ryk flytter, 1 = skal rulles meget. Eksempel: 5."},
    {"trackball_invert_x", "Vend trackballens vandrette retning."},
    {"trackball_invert_y", "Vend trackballens lodrette retning."},
    {"trackball_deadzone", "Hvor tydeligt et sideryk skal være for at skifte fane, 1-10. Højere = ingen hop til siden ved scroll. Eksempel: 3."},
    {"trackball_click_delay_ms", "Pause før et nyt klik tæller, så ét tryk ikke bliver til to. Eksempel: 150."},
    {">wifi", "Status, scan, forbind (også skjulte net) og glem gemte netværk. Koder gemmes krypteret på SD."},
    {"wifi_enabled", "WiFi til/fra. Til: forbinder selv til det stærkeste gemte net (højst 6 forsøg med 10 s imellem)."},
    {"wifi_ntp", "Hent tiden fra internettet (NTP), når WiFi er forbundet. GPS-tiden har forrang."},
    {">mqtt", "Viser broens tilstand, emne-rod og emnerne. MQTT sender modtagne beskeder til din broker og kan sende fra den."},
    {"mqtt_enabled", "Sender modtagne beskeder til en MQTT-broker, når WiFi er forbundet, og kan sende fra den. Kræver Broker."},
    {"mqtt_broker", "Broker-adresse. Eksempel: mqtt://min-pc.lan:1883 eller mqtts://host:8883 (TLS; egen CA i /dualmesh/mqtt_ca.pem)."},
    {"mqtt_user", "Brugernavn til brokeren. Tom = intet login. Eksempel: dualmesh"},
    {"mqtt_pass", "Kode til brokeren; gemmes krypteret, ikke i settings.ini. Tom = behold den gemte. Skriv - for at slette den."},
    {"mqtt_root", "Starten af alle emner. Eksempel: dualmesh/tdeck giver dualmesh/tdeck/rx/meshcore/chan/Public. Tom = dualmesh/<id>."},
    {"mqtt_tx", "Tillad at sende ud på LoRa fra MQTT (<rod>/tx/...). Højst én pr. 10 s og inden for 10 %-grænsen. Standard fra."},
    {"mqtt_status_min", "Hvor tit status (batteri, GPS-fix, net) sendes til <rod>/status. 0 = kun ved forbindelse. Eksempel: 5"},
    {"mqtt_share_position", "Egen position i MQTT-status: fra, præcis eller omtrent (ca. 1 km). Kræver GPS-fix. Standard fra."},
    {"ntp_server", "Tidsserver til NTP. Eksempel: dk.pool.ntp.org. Tom = pool.ntp.org."},
    {">ble", "Viser Bluetooth-navnet, PIN-koden og om MeshCore-appen er forbundet."},
    {"ble_enabled", "Lad en app på telefonen bruge T-Deck'en over Bluetooth (se App). Kræver genstart."},
    {"ble_app", "Hvilken app Bluetooth taler med: MeshCore-appen eller Meshtastic-appen (én ad gangen). Kræver genstart."},
    {"ble_pin", "Seks cifre til parring, fx 482913. Gemmes krypteret; vises under Bluetooth-status. Tom = behold, - = standard 123456. Kræver genstart."},
    {">gps", "Viser fix, satellitter, position, højde, fart og ur fra GPS'en. Klik i skærmen slår GPS til/fra."},
    {"gps_enabled", "GPS til/fra. Fra sparer strøm. SOS tænder den selv ved behov."},
    {"gps_time", "Sæt uret fra GPS. Det er den mest præcise kilde og går forud for NTP og mesh."},
    {">nvs", "Chippens interne lager, som deles med launcher og andre firmwares. Se forbrug og ryd Bluetooth/WiFi-data."},
    {">crypto", "Kryptering af koder og nøgler på SD: fra, chip-ID, kode ved opstart eller eFuse (nøgle i chippen)."},
    {">reset", "Sletter alle DualMesh-data på SD. Eksporterede profiler og kortpakker bevares; du kan eksportere først."},
    {"!clear_log", "Sletter alle logfiler på SD-kortet og starter en ny log. Klik 2 gange."},
    {"!usb_disk", "Genstarter, så SD-kortet vises som et drev på PC'en (radioen er slukket imens). Skub drevet ud for at vende tilbage."},
};

const char* helpFor(const char* key) {
  for (const HelpText& h : kHelp)
    if (!std::strcmp(h.key, key)) return h.text;
  return "";
}

// Grupper: navn og farve (overskriften har farven som baggrund, indstillingerne
// som tekstfarve). Rækkefølge = visning.
struct GroupInfo {
  const char* name;
  uint16_t color;
};
const GroupInfo kGroups[] = {  // samme rækkefølge som Group (MeshCore før Meshtastic)
    {"Status", color::Gold},
    {"WiFi", color::Salmon},  // (Cyan er forbeholdt fane- og valgbjælken)
    {"Bluetooth", color::Violet},
    {"GPS", color::Lime},
    {"Kort", color::Peach},
    {"Profil", color::Yellow},
    {"Beskeder", color::White},
    {"MeshCore", color::LightBlue},
    {"Meshtastic", color::Green},
    {"MQTT", color::Pink},
    {"Skærm", color::Magenta},
    {"Tastatur og trackball", color::Orange},
    {"System", color::Gray},
};

// Sorteret efter gruppe (= visning). Etiketter højst 17 tegn. Brug hver protokols
// egne begreber (Meshtastic: primær kanal, preset; MeshCore: Public, advert ...).
// Strømstyring for én ting ligger i dens gruppe (fx skærmens sluk-tid under Skærm).
const SettingsUi::Def SettingsUi::kDefs[] = {
    // Profil: fælles for begge net. Navn = MeshCores navn og Meshtastics lange navn
    // (nøglen hedder meshcore_name af hensyn til eksisterende settings.ini).
    {">status", "Status", Type::Action, 0, 0, 0, 0, false, "(radio, net, strøm)", GStatus},
    {"meshcore_name", "Navn/Langt Navn", Type::Text, 0, 31, 0, 0, false, "(DualMesh-XXXX)", GProfile},
    {"meshtastic_short_name", "Kort Navn", Type::Text, 0, 4, 0, 0, false, "(auto)", GProfile},
    {">profile_export", "Eksportér profil", Type::Action, 0, 0, 0, 0, false, "(til fil på SD)", GProfile},
    {">profile_import", "Importér profil", Type::Action, 0, 0, 0, 0, false, "(fra fil på SD)", GProfile},
    {"meshcore_enabled", "Brug MeshCore", Type::Bool, 0, 1, 1, 0, true, nullptr, GMeshCore},
    {"meshcore_preset", "Radio-preset", Type::Enum, 0, 0, 0, kOptMcPreset, true, nullptr, GMeshCore},
    {"meshcore_custom", "Egne radioværdier", Type::Text, 0, 40, 0, 0, true, "(fra)", GMeshCore},
    {"meshcore_tx", "Må sende", Type::Bool, 0, 1, 1, 0, false, nullptr, GMeshCore},
    {"meshcore_share_position", "Del position", Type::Enum, 0, 0, 0, kOptPosShare, false, nullptr, GMeshCore},
    {"meshcore_telemetry", "Telemetri-svar", Type::Enum, 0, 0, 0, kOptTelemReq, false, nullptr, GMeshCore},
    {"meshcore_telemetry_pos", "Position i telem.", Type::Enum, 0, 0, 0, kOptTelemData, false, nullptr, GMeshCore},
    {"meshcore_telemetry_env", "Miljø i telem.", Type::Enum, 0, 0, 0, kOptTelemData, false, nullptr, GMeshCore},
    {">telem_contacts", "Telem.-kontakter", Type::Action, 0, 0, 0, 0, false, "(klik for at vælge)", GMeshCore},
    {"meshcore_advert_local_min", "Lokal advert, min", Type::Int, 0, 1440, 10, 0, false, "fra", GMeshCore},
    {"meshcore_advert_flood_h", "Flood-advert, t", Type::Int, 0, 168, 1, 0, false, "fra", GMeshCore},
    {">advert_local", "Send lokal advert", Type::Action, 0, 0, 0, 0, false, "(klik: send nu)", GMeshCore},
    {">advert_flood", "Send flood-advert", Type::Action, 0, 0, 0, 0, false, "(klik: send nu)", GMeshCore},
    {"meshcore_tx_power", "Sendeeffekt, dBm", Type::Int, 1, 22, 1, 0, true, nullptr, GMeshCore},
    {"meshtastic_enabled", "Brug Meshtastic", Type::Bool, 0, 1, 1, 0, true, nullptr, GMeshtastic},
    {"meshtastic_region", "Region", Type::Enum, 0, 0, 0, kOptMtRegion, true, nullptr, GMeshtastic},
    {"meshtastic_preset", "Preset", Type::Enum, 0, 0, 0, kOptMtPreset, true, nullptr, GMeshtastic},
    {"meshtastic_channel", "Primær kanal", Type::Text, 0, 11, 0, 0, true, "(presetnavn)", GMeshtastic},
    {"meshtastic_slot", "Frekvens-slot", Type::Int, 0, 200, 1, 0, true, "auto", GMeshtastic},
    {"meshtastic_tx", "Må sende", Type::Bool, 0, 1, 1, 0, false, nullptr, GMeshtastic},
    {"meshtastic_tx_power", "Sendeeffekt, dBm", Type::Int, 1, 22, 1, 0, true, nullptr, GMeshtastic},
    {"meshtastic_share_position", "Del position", Type::Enum, 0, 0, 0, kOptPosShare, false, nullptr, GMeshtastic},
    {"meshtastic_position_min", "Position, min", Type::Int, 0, 1440, 5, 0, false, "aldrig", GMeshtastic},
    {"meshtastic_nodeinfo_h", "NodeInfo, t", Type::Int, 0, 24, 1, 0, false, "kun ved start", GMeshtastic},
    {"meshtastic_relay", "Videresend", Type::Enum, 0, 0, 0, kOptMtRelay, false, nullptr, GMeshtastic},
    {"meshtastic_telemetry_min", "Telemetri, min", Type::Int, 0, 1440, 5, 0, false, "fra", GMeshtastic},
    {"meshtastic_data_channel", "Kanal for data", Type::Text, 0, 11, 0, 0, false, "(primær)", GMeshtastic},
    {"msgs_per_channel", "Antal pr. kanal", Type::Int, 10, 1000, 10, 0, false, nullptr, GMessages},
    {"msgs_per_dm", "Antal pr. DM", Type::Int, 10, 1000, 10, 0, false, nullptr, GMessages},
    {"msgs_days_channel", "Dage pr. kanal", Type::Int, 0, 365, 1, 0, false, "alle", GMessages},
    {"msgs_days_dm", "Dage pr. DM", Type::Int, 0, 365, 1, 0, false, "alle", GMessages},
    {">colors", "Kontaktfarver", Type::Action, 0, 0, 0, 0, false, "(klik for at vælge)", GMessages},
    {"meshcore_test_text", "Testbesked tekst", Type::Text, 0, 120, 0, 0, false, "(standardtekst)", GMessages},
    {"meshcore_test_interval_min", "Testbesked, min", Type::Int, 2, 1440, 1, 0, false, nullptr, GMessages},
    {"!clear_chat", "Ryd åben chat", Type::Action, 0, 0, 0, 0, false, "(klik 2 gange)", GMessages},
    {"!clear_channels", "Ryd kanal-chats", Type::Action, 0, 0, 0, 0, false, "(klik 2 gange)", GMessages},
    {"sos_interval_min", "SOS-interval, min", Type::Int, 2, 60, 1, 0, false, nullptr, GMessages},
    {"screen_brightness", "Lysstyrke", Type::Int, 1, 16, 1, 0, false, nullptr, GScreen},
    {"screen_timeout_s", "Sluk efter, sek", Type::Int, 0, 600, 5, 0, false, "aldrig", GScreen},
    {"screen_timeout_usb", "Sluk også på USB", Type::Bool, 0, 1, 1, 0, false, nullptr, GScreen},
    {"touch_swap_xy", "Touch byt X/Y", Type::Bool, 0, 1, 1, 0, false, nullptr, GScreen},
    {"touch_invert_x", "Touch vend X", Type::Bool, 0, 1, 1, 0, false, nullptr, GScreen},
    {"touch_invert_y", "Touch vend Y", Type::Bool, 0, 1, 1, 0, false, nullptr, GScreen},
    {"keyboard_backlight", "Tastaturlysstyrke", Type::Int, 0, 255, 16, 0, false, "fra", GInput},
    {"keyboard_backlight_timeout_s", "Tastaturlys, sek", Type::Int, 1, 120, 1, 0, false, nullptr, GInput},
    {"keyboard_layouts", "Tastaturlayouts", Type::Text, 0, 40, 0, 0, false, "(da,latin)", GInput},
    {">dl_keyboards", "Hent layouts", Type::Action, 0, 0, 0, 0, false, "(fra GitHub)", GInput},
    {"keyboard_raw_mode", "Rå tastatur", Type::Bool, 0, 1, 1, 0, true, nullptr, GInput},
    {"trackball_sensitivity", "Trackball følsom", Type::Int, 1, 10, 1, 0, false, nullptr, GInput},
    {"trackball_invert_x", "Trackball vend X", Type::Bool, 0, 1, 1, 0, false, nullptr, GInput},
    {"trackball_invert_y", "Trackball vend Y", Type::Bool, 0, 1, 1, 0, false, nullptr, GInput},
    {"trackball_deadzone", "Trackball dødzone", Type::Int, 1, 10, 1, 0, false, nullptr, GInput},
    {"trackball_click_delay_ms", "Klik-pause, ms", Type::Int, 50, 1000, 50, 0, false, nullptr, GInput},
    {">wifi", "WiFi-netværk", Type::Action, 0, 0, 0, 0, false, "(klik: status/scan)", GWifi},
    {"wifi_enabled", "Brug WiFi", Type::Bool, 0, 1, 1, 0, false, nullptr, GWifi},
    {"wifi_ntp", "Tid fra NTP", Type::Bool, 0, 1, 1, 0, false, nullptr, GWifi},
    {"ntp_server", "NTP-server", Type::Text, 0, 40, 0, 0, false, "(pool.ntp.org)", GWifi},
    {">mqtt", "MQTT-status", Type::Action, 0, 0, 0, 0, false, "(klik for at se)", GMqtt},
    {"mqtt_enabled", "Brug MQTT-bro", Type::Bool, 0, 1, 1, 0, false, nullptr, GMqtt},
    {"mqtt_broker", "Broker", Type::Text, 0, 100, 0, 0, false, "(ingen)", GMqtt},
    {"mqtt_user", "Brugernavn", Type::Text, 0, 60, 0, 0, false, "(intet login)", GMqtt},
    {"mqtt_pass", "Adgangskode", Type::Text, 0, 100, 0, 0, false, "(skjult)", GMqtt},
    {"mqtt_root", "Emne-rod", Type::Text, 0, 60, 0, 0, false, "(dualmesh/<id>)", GMqtt},
    {"mqtt_tx", "Må sende fra MQTT", Type::Bool, 0, 1, 1, 0, false, nullptr, GMqtt},
    {"mqtt_status_min", "Status, min", Type::Int, 0, 1440, 1, 0, false, "kun ved start", GMqtt},
    {"mqtt_share_position", "Position i status", Type::Enum, 0, 0, 0, kOptPosShare, false, nullptr, GMqtt},
    {">ble", "Bluetooth-status", Type::Action, 0, 0, 0, 0, false, "(klik for at se)", GBluetooth},
    {"ble_enabled", "Bluetooth til app", Type::Bool, 0, 1, 1, 0, true, nullptr, GBluetooth},
    {"ble_app", "App", Type::Enum, 0, 0, 0, kOptBleApp, true, nullptr, GBluetooth},
    {"ble_pin", "Bluetooth PIN", Type::Text, 0, 6, 0, 0, true, "(skjult)", GBluetooth},
    {">gps", "GPS-status", Type::Action, 0, 0, 0, 0, false, "(klik for at se)", GGps},
    {"gps_enabled", "Brug GPS", Type::Bool, 0, 1, 1, 0, false, nullptr, GGps},
    {"gps_time", "Tid fra GPS", Type::Bool, 0, 1, 1, 0, false, nullptr, GGps},
    {"gps_eco", "GPS-strømspar", Type::Bool, 0, 1, 1, 0, false, nullptr, GGps},
    {"map_sensitivity", "Kortfølsomhed", Type::Int, 1, 10, 1, 0, false, nullptr, GMap},
    {"track_interval_s", "Spor-interval, s", Type::Int, 1, 60, 1, 0, false, nullptr, GMap},
    {"track_min_m", "Spor-afstand, m", Type::Int, 1, 100, 1, 0, false, nullptr, GMap},
    {"okapi_site", "Opencaching-side", Type::Text, 0, 40, 0, 0, false, "(opencaching.de)", GMap},
    {"okapi_key", "Opencaching-nøgle", Type::Text, 0, 40, 0, 0, false, "(ingen)", GMap},
    {"okapi_radius_km", "Hent radius, km", Type::Int, 1, 50, 1, 0, false, nullptr, GMap},
    {"!okapi_fetch", "Hent caches nu", Type::Action, 0, 0, 0, 0, false, "(kræver WiFi)", GMap},
    {">dl_maps", "Hent kortpakker", Type::Action, 0, 0, 0, 0, false, "(fra GitHub)", GMap},
    {"language", "Sprog / Language", Type::Enum, 0, 0, 0, kOptLang, true, nullptr, GSystem},
    {">dl_lang", "Hent sprog", Type::Action, 0, 0, 0, 0, false, "(fra GitHub)", GSystem},
    {">ota", "Opdatér firmware", Type::Action, 0, 0, 0, 0, false, "(fra GitHub)", GSystem},
    {"cpu_eco", "Strømspar (CPU)", Type::Bool, 0, 1, 1, 0, false, nullptr, GSystem},
    {">nvs", "Intern NVS", Type::Action, 0, 0, 0, 0, false, "(se / ryd)", GSystem},
    {">crypto", "SD-kryptering", Type::Action, 0, 0, 0, 0, false, "(klik for at se)", GSystem},
    {">reset", "Nulstil DualMesh", Type::Action, 0, 0, 0, 0, false, "(Nulstil alt)", GSystem},
    {"!clear_log", "Ryd log", Type::Action, 0, 0, 0, 0, false, "(klik 2 gange)", GSystem},
    {"!usb_disk", "SD som USB-drev", Type::Action, 0, 0, 0, 0, false, "(klik 2 gange)", GSystem},
    {">report", "Fejlrapport", Type::Action, 0, 0, 0, 0, false, "(nedbrud, QR)", GSystem},
    {">about", "Omkring DualMesh", Type::Action, 0, 0, 0, 0, false, "(version, licens)", GSystem},
};
const int SettingsUi::kCount = int(sizeof(kDefs) / sizeof(kDefs[0]));

void SettingsUi::begin(SdStore* store, SavedFn on_saved, ActionFn on_action, CheckFn on_check) {
  store_ = store;
  on_saved_ = on_saved;
  on_action_ = on_action;
  on_check_ = on_check;
  const size_t bytes = size_t(kCount) * kVal;
  buildRows();
  val_ = static_cast<char(*)[kVal]>(heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM));
  orig_ = static_cast<char(*)[kVal]>(heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM));
  load();
}

void SettingsUi::load() {
  if (!val_ || !orig_) return;
  for (int i = 0; i < kCount; ++i) {
    const char* v =
        (store_ && kDefs[i].type != Type::Action) ? store_->get(kDefs[i].key) : nullptr;
    snprintf(orig_[i], kVal, "%s", v ? v : "");
    std::memcpy(val_[i], orig_[i], kVal);
  }
  editing_ = false;
}

// Rækker: for hver gruppe en overskrift, dens indstillinger (eller "ingen endnu")
// og en tom linje før næste gruppe.
void SettingsUi::buildRows() {
  static_assert(sizeof(kDefs) / sizeof(kDefs[0]) + 2 * GCount <= kMaxRows,
                "kMaxRows er for lille: grupperne nederst i Indst. ville blive skaaret af");
  n_rows_ = 0;
  for (int g = 0; g < GCount && n_rows_ < kMaxRows - 3; ++g) {
    if (g && open_[g - 1]) rows_[n_rows_++] = {Row::Blank, uint8_t(g), -1};  // luft efter en åben gruppe
    rows_[n_rows_++] = {Row::Header, uint8_t(g), -1};
    if (!open_[g]) continue;  // sammenfoldet: kun overskriften
    bool any = false;
    for (int i = 0; i < kCount && n_rows_ < kMaxRows; ++i)
      if (kDefs[i].group == g) {
        rows_[n_rows_++] = {Row::Item, uint8_t(g), int16_t(i)};
        any = true;
      }
    if (!any && n_rows_ < kMaxRows) rows_[n_rows_++] = {Row::Empty, uint8_t(g), -1};
  }
}

int SettingsUi::rowOf(int def) const {
  for (int r = 0; r < n_rows_; ++r)
    if (rows_[r].kind == Row::Item && rows_[r].def == def) return r;
  return 0;
}

int SettingsUi::selRow() const {
  if (onHeader()) {
    for (int r = 0; r < n_rows_; ++r)
      if (rows_[r].kind == Row::Header && rows_[r].group == headerGroup()) return r;
    return 0;
  }
  return rowOf(sel_);
}

// Sørg for at den valgte indstilling (og gruppens overskrift lige over) er synlig.
void SettingsUi::scrollTo(int def) {
  if (def >= kCount) {  // knapperne: vis slutningen af listen
    top_ = n_rows_ > view_rows_ ? n_rows_ - view_rows_ : 0;
    return;
  }
  const int r = def < 0 ? selRow() : rowOf(def);
  int want_top = r;
  if (r > 0 && rows_[r - 1].kind == Row::Header) want_top = r - 1;
  if (r > 1 && rows_[r - 1].kind == Row::Header && rows_[r - 2].kind == Row::Blank) want_top = r - 2;
  if (want_top < top_) top_ = want_top;
  if (r >= top_ + view_rows_) top_ = r - view_rows_ + 1;
  if (top_ < 0) top_ = 0;
}

const char* SettingsUi::edited(const char* key) const {
  if (!val_) return "";
  for (int i = 0; i < kCount; ++i)
    if (std::strcmp(kDefs[i].key, key) == 0) return val_[i];
  return "";
}

// Indstillinger for et net, der er slået fra, er låste (brugerens ønske 15. nov 2026).
// Afgøres af den redigerede værdi, så de låses op/ned, så snart nettet slås til/fra.
const char* SettingsUi::lockedBy(int i) const {
  const Def& d = kDefs[i];
  const char* dep = nullptr;
  if (d.group == GMeshCore && std::strcmp(d.key, "meshcore_enabled") != 0) dep = "meshcore_enabled";
  else if (d.group == GMeshtastic && std::strcmp(d.key, "meshtastic_enabled") != 0) dep = "meshtastic_enabled";
  // (ble_enabled er ikke låst af et net: den bruges af enten MeshCore- eller Meshtastic-appen)
  if (!dep) return nullptr;
  const char* v = edited(dep);
  if (!v || !*v || std::atoi(v) != 0) return nullptr;
  return dep[5] == 'o' ? "MeshCore er slået fra" : "Meshtastic er slået fra";
}

bool SettingsUi::isChanged(int i) const { return std::strcmp(val_[i], orig_[i]) != 0; }

bool SettingsUi::changed() const {
  if (!val_) return false;
  for (int i = 0; i < kCount; ++i)
    if (isChanged(i)) return true;
  return false;
}

int SettingsUi::optCount(uint8_t list) const {
  int n = 0;
  if (list == kOptMtRegion) {
    for (size_t i = 0; i < mt::regionCount(); ++i) n += mt::regionAt(i).listed ? 1 : 0;
  } else if (list == kOptMtPreset) {
    for (uint8_t i = 0; i < mt::kPresetCount; ++i) {
      const mt::PresetInfo* p = mt::preset(mt::Preset(i));
      n += (p && !p->deprecated) ? 1 : 0;
    }
  } else if (list == kOptPosShare) {
    n = int(sizeof(kPosShare) / sizeof(kPosShare[0]));
  } else if (list == kOptTelemReq || list == kOptTelemData || list == kOptMtRelay) {
    n = 3;
  } else if (list == kOptBleApp) {
    n = 2;
  } else if (list == kOptLang) {
    n = lang::count();
  } else {
    n = int(mc::presetCount());
  }
  return n;
}

const char* SettingsUi::opt(uint8_t list, int idx) const {
  int n = 0;
  if (list == kOptMtRegion) {
    for (size_t i = 0; i < mt::regionCount(); ++i)
      if (mt::regionAt(i).listed && n++ == idx) return mt::regionAt(i).name;
  } else if (list == kOptMtPreset) {
    for (uint8_t i = 0; i < mt::kPresetCount; ++i) {
      const mt::PresetInfo* p = mt::preset(mt::Preset(i));
      if (p && !p->deprecated && n++ == idx) return mt::presetEnumName(p->id);
    }
  } else if (list == kOptPosShare) {
    if (idx >= 0 && idx < int(sizeof(kPosShare) / sizeof(kPosShare[0]))) return kPosShare[idx];
  } else if (list == kOptTelemReq || list == kOptTelemData) {
    if (idx >= 0 && idx < 3) return list == kOptTelemReq ? kTelemReq[idx] : kTelemData[idx];
  } else if (list == kOptMtRelay) {
    if (idx >= 0 && idx < 3) return kMtRelay[idx];
  } else if (list == kOptBleApp) {
    if (idx >= 0 && idx < 2) return kBleApp[idx];
  } else if (list == kOptLang) {
    return lang::nameAt(idx);
  } else if (idx >= 0 && idx < int(mc::presetCount())) {
    return mc::presetAt(size_t(idx)).title;
  }
  return "";
}

void SettingsUi::change(int i, int dir) {
  const Def& d = kDefs[i];
  if (d.type == Type::Bool) {
    snprintf(val_[i], kVal, "%d", std::atoi(val_[i]) ? 0 : 1);
  } else if (d.type == Type::Int) {
    long v = std::strtol(val_[i], nullptr, 10) + long(dir) * d.step;
    if (v < d.min) v = d.min;
    if (v > d.max) v = d.max;
    snprintf(val_[i], kVal, "%ld", v);
  } else if (d.type == Type::Enum) {
    const int n = optCount(d.opts);
    int cur = -1;
    for (int k = 0; k < n; ++k)
      if (strcasecmp(opt(d.opts, k), val_[i]) == 0) cur = k;
    cur = cur < 0 ? 0 : (cur + dir + n) % n;
    snprintf(val_[i], kVal, "%s", opt(d.opts, cur));
  }
}

void SettingsUi::note(const char* text) {
  snprintf(note_, sizeof(note_), "%s", text);
  note_ms_ = millis();
}

void SettingsUi::save() {
  editing_ = false;
  if (!changed()) {
    note("Ingen ændringer at gemme");
    return;
  }
  // Tjek Meshtastic-kombinationen, før noget gemmes.
  int ir = -1, ip = -1, ic = -1;
  for (int i = 0; i < kCount; ++i) {
    if (!std::strcmp(kDefs[i].key, "meshtastic_region")) ir = i;
    if (!std::strcmp(kDefs[i].key, "meshtastic_preset")) ip = i;
    if (!std::strcmp(kDefs[i].key, "meshcore_custom")) ic = i;
  }
  const mt::RegionInfo* r = mt::regionByName(val_[ir][0] ? val_[ir] : "EU_868");
  const mt::PresetInfo* p = val_[ip][0] ? mt::presetByName(val_[ip]) : nullptr;
  if (!r || !r->listed) {
    note("MT-regionen findes ikke");
    return;
  }
  if (p && (!mt::regionAllowsPreset(r->id, p->id) || !mt::sx1262Supports(r->id, p->id))) {
    note("MT-preset virker ikke i regionen");
    return;
  }
  if (val_[ic][0]) {
    unsigned long f, bw;
    unsigned sf, cr;
    if (sscanf(val_[ic], "%lu,%lu,%u,%u", &f, &bw, &sf, &cr) != 4) {
      note("MC egen radio: Hz,bw,sf,cr");
      return;
    }
  }
  if (on_check_)
    if (const char* err = on_check_(*this)) {
      note(err);
      return;
    }
  bool restart = false;
  for (int i = 0; i < kCount; ++i) {
    if (!isChanged(i)) continue;
    if (isSecretKey(kDefs[i].key)) {  // hemmelig: aldrig i settings.ini eller loggen
      DLOG.printf("[MENU] %s: (skjult)\n", kDefs[i].key);
      if (on_secret_) on_secret_(kDefs[i].key, val_[i]);
      val_[i][0] = 0;
      restart |= kDefs[i].restart;
      continue;
    }
    if (!std::strcmp(kDefs[i].key, "okapi_key"))  // API-nøgle: gemmes, men logges ikke
      DLOG.printf("[MENU] %s: (aendret, vises ikke)\n", kDefs[i].key);
    else
      DLOG.printf("[MENU] %s: '%s' -> '%s'\n", kDefs[i].key, orig_[i], val_[i]);
    store_->set(kDefs[i].key, val_[i]);
    restart |= kDefs[i].restart;
  }
  const bool sd = store_->mounted() && store_->save();
  for (int i = 0; i < kCount; ++i) std::memcpy(orig_[i], val_[i], kVal);
  if (!sd) note(restart ? "IKKE gemt (intet SD) - radio uændret" : "Gælder til genstart (intet SD)");
  else note(restart ? "Gemt - genstarter radioen..." : "Gemt");
  if (on_saved_) on_saved_(restart && sd);
}

void SettingsUi::cancel() {
  const bool had = changed();
  load();
  note(had ? "Ændringer annulleret" : "Ingen ændringer");
}

// Fritekst, hvor specialtegn giver mening (navne og beskeder). Tekniske værdier
// (frekvenser, server, tidszone, PIN, layouts, Meshtastic-kanalnavn) holdes på ASCII.
static bool allowsSpecial(const char* key) {
  return !std::strcmp(key, "meshcore_name") || !std::strcmp(key, "meshtastic_short_name") ||
         !std::strcmp(key, "meshcore_test_text");
}

bool SettingsUi::textField(char*& buf, size_t& cap) {
  if (!editing_ || !onItem()) return false;
  const Def& d = kDefs[sel_];
  if (d.type != Type::Text || !allowsSpecial(d.key)) return false;
  buf = val_[sel_];
  cap = size_t(d.max + 1) < size_t(kVal) ? size_t(d.max + 1) : size_t(kVal);
  return true;
}

void SettingsUi::key(char c) {
  if (!onItem()) {  // knapperne eller en gruppeoverskrift
    if (c == '\r' || c == '\n') click();
    return;
  }
  const Def& d = kDefs[sel_];
  if (editing_ && d.type == Type::Text) {
    size_t n = std::strlen(val_[sel_]);
    if (c == '\r' || c == '\n') {
      editing_ = false;
    } else if (c == 0x08 || c == 0x7F) {  // slet hele tegnet (UTF-8)
      while (n && (uint8_t(val_[sel_][n - 1]) & 0xC0) == 0x80) --n;
      if (n) val_[sel_][n - 1] = 0;
    } else if (c >= 0x20 && c < 0x7F && int(n) < d.max && n + 1 < size_t(kVal)) {
      val_[sel_][n] = c;
      val_[sel_][n + 1] = 0;
    }
    return;
  }
  if (c == '\r' || c == '\n') {
    click();
  } else if (d.type == Type::Text && c >= 0x20 && c < 0x7F && !lockedBy(sel_)) {
    editing_ = true;  // begynd at skrive direkte (erstatter værdien)
    val_[sel_][0] = c;
    val_[sel_][1] = 0;
  }
}

void SettingsUi::stepY(int dir) {
  if (editing_ && onItem() && kDefs[sel_].type != Type::Text) {
    change(sel_, -dir);  // op = større
    return;
  }
  if (editing_) return;
  // Op/ned følger de viste rækker: gruppeoverskrifter og (i åbne grupper) indstillinger.
  // Efter sidste række kommer Gem og derefter Annuller.
  auto selectable = [&](int r) { return rows_[r].kind == Row::Item || rows_[r].kind == Row::Header; };
  auto selOf = [&](int r) { return rows_[r].kind == Row::Header ? -1 - int(rows_[r].group) : int(rows_[r].def); };
  int last = -1;
  for (int r = 0; r < n_rows_; ++r)
    if (selectable(r)) last = r;
  if (last < 0) return;
  if (sel_ >= kCount) {  // på knapperne
    if (dir > 0) sel_ = kCount + 1;
    else if (sel_ == kCount + 1) sel_ = kCount;
    else sel_ = selOf(last);
  } else {
    int r = selRow();
    do {
      r += dir;
    } while (r >= 0 && r < n_rows_ && !selectable(r));
    if (r < 0) sel_ = -1;  // øverste overskrift
    else if (r >= n_rows_) sel_ = kCount;
    else sel_ = selOf(r);
  }
  scrollTo(sel_);
}

bool SettingsUi::wantsX() const { return editing_ || sel_ >= kCount; }

bool SettingsUi::atTop() const { return !editing_ && sel_ == -1; }

void SettingsUi::collapse() {
  if (editing_) return;
  int g = -1;
  if (onHeader()) g = headerGroup();
  else if (onItem()) g = int(kDefs[sel_].group);
  if (g < 0 || g >= int(GCount) || !open_[g]) return;
  open_[g] = false;
  sel_ = -1 - g;  // markøren på gruppens overskrift
  buildRows();
  scrollTo(sel_);
}

size_t SettingsUi::textLen() const {
  if (!editing_ || sel_ < 0 || sel_ >= kCount || kDefs[sel_].type != Type::Text) return 0;
  return std::strlen(val_[sel_]);
}

void SettingsUi::stepX(int dir) {
  if (sel_ >= kCount) {
    sel_ = dir > 0 ? kCount + 1 : kCount;  // Gem <-> Annuller
  } else if (editing_ && kDefs[sel_].type != Type::Text) {
    change(sel_, dir);
  }
}

void SettingsUi::click() {
  if (sel_ == kCount) return save();
  if (sel_ == kCount + 1) return cancel();
  if (onHeader()) {  // åbn/luk gruppen; markøren bliver på overskriften
    open_[headerGroup()] = !open_[headerGroup()];
    editing_ = false;
    buildRows();
    // Åbnet: vis så meget af gruppen som muligt under overskriften.
    const int r = selRow();
    if (open_[headerGroup()]) {
      top_ = r > 0 && rows_[r - 1].kind == Row::Blank ? r - 1 : r;
      if (top_ > n_rows_ - view_rows_) top_ = n_rows_ > view_rows_ ? n_rows_ - view_rows_ : 0;
    }
    scrollTo(sel_);
    return;
  }
  if (const char* why = lockedBy(sel_)) {
    note(why);
    return;
  }
  if (kDefs[sel_].type == Type::Action && kDefs[sel_].key[0] == '>') {  // åbn underskærm
    const char* r = on_action_ ? on_action_(kDefs[sel_].key) : "Ikke muligt";
    if (r && *r) note(r);
    return;
  }
  if (kDefs[sel_].type == Type::Action) {
    if (armed_ == sel_ && millis() - armed_ms_ < 4000) {
      armed_ = -1;
      note(on_action_ ? on_action_(kDefs[sel_].key) : "Ikke muligt");
    } else {
      armed_ = sel_;
      armed_ms_ = millis();
      note("Klik igen inden 4 s for at udføre");
    }
    return;
  }
  if (kDefs[sel_].type == Type::Bool) {
    change(sel_, 1);
    return;
  }
  editing_ = !editing_;
}

void SettingsUi::tapRow(int r) {
  const int ri = top_ + r;
  if (r < 0 || r >= view_rows_ || ri >= n_rows_) return;
  if (rows_[ri].kind == Row::Header) {  // tryk på en overskrift: åbn/luk gruppen
    editing_ = false;
    sel_ = -1 - int(rows_[ri].group);
    click();
    return;
  }
  if (rows_[ri].kind != Row::Item) return;
  const int i = rows_[ri].def;
  if (i != sel_) {
    editing_ = false;
    sel_ = i;
  }
  click();
}

void SettingsUi::tapButton(int b) {
  sel_ = b == 0 ? kCount : kCount + 1;
  click();
}

// Datamodellen: de synlige rækker, forklaringen og hjælpelinjen.
void SettingsUi::view(View& v, int rows) {
  if (rows > kViewRows) rows = kViewRows;
  if (rows != view_rows_) {
    view_rows_ = rows;
    scrollTo(sel_);
  }
  v.nrows = 0;
  for (int r = 0; r < rows; ++r) {
    const int ri = top_ + r;
    if (ri >= n_rows_) break;
    ViewRow& w = v.rows[v.nrows++];
    w = ViewRow{};
    const GroupInfo& gi = kGroups[rows_[ri].group];
    w.color = gi.color;
    w.kind = ViewRow::Kind(rows_[ri].kind);
    if (rows_[ri].kind == Row::Header) {
      const int g = rows_[ri].group;
      bool ch = false;  // ikke-gemte ændringer i gruppen
      for (int i = 0; i < kCount && !ch; ++i) ch = kDefs[i].group == g && isChanged(i);
      char gn[40];  // gruppenavnet oversættes her (overskriften sættes sammen med +/-)
      snprintf(w.label, sizeof(w.label), "%s %s%s", open_[g] ? "-" : "+", i18n::tr(gi.name, gn, sizeof(gn)),
               ch ? " *" : "");
      w.sel = sel_ == -1 - g;
      continue;
    }
    if (rows_[ri].kind != Row::Item) {
      if (rows_[ri].kind == Row::Empty) snprintf(w.label, sizeof(w.label), "(ingen indstillinger endnu)");
      continue;
    }
    const int i = rows_[ri].def;
    const Def& df = kDefs[i];
    const char* val = val_[i];
    if (df.type == Type::Action) {
      val = (armed_ == i && millis() - armed_ms_ < 4000) ? "klik igen = udfør" : df.zero;
    } else if (df.type == Type::Bool) {
      val = std::atoi(val) ? "til" : "fra";
    } else if (df.type == Type::Int && df.zero && std::atoi(val) == 0) {
      val = df.zero;
    } else if (df.type == Type::Text && !*val && !(editing_ && i == sel_)) {
      val = df.zero ? df.zero : "";
    } else if (df.type == Type::Enum && !*val) {
      val = "(standard)";
    }
    char mask[40];
    if (isSecretKey(df.key) && *val && val != df.zero) {  // vis kun stjerner
      size_t k = 0;
      for (const char* q = val; *q && k + 1 < sizeof(mask); ++q)
        if ((uint8_t(*q) & 0xC0) != 0x80) mask[k++] = '*';
      mask[k] = 0;
      val = mask;
    }
    if (editing_ && i == sel_ && df.type == Type::Text) {
      const size_t n = std::strlen(val);
      const char* tail = n > 40 ? val + (n - 40) : val;
      while (tail > val && (uint8_t(*tail) & 0xC0) == 0x80) ++tail;
      snprintf(w.value, sizeof(w.value), "%s_", tail);
    } else {
      snprintf(w.value, sizeof(w.value), "%s", val ? val : "");
    }
    snprintf(w.label, sizeof(w.label), "%s", df.label);
    w.changed = isChanged(i);
    w.locked = lockedBy(i) != nullptr;
    w.sel = i == sel_;
  }
  snprintf(v.help, sizeof(v.help), "%s", onItem() ? helpFor(kDefs[sel_].key) : "");
  v.help_dim = onItem() && lockedBy(sel_);
  v.editing = editing_;
  v.changed = changed();
  v.save_sel = sel_ == kCount;
  v.cancel_sel = sel_ == kCount + 1;
  const uint32_t now = millis();
  v.hint_fg = color::Gray;
  if (note_[0] && now - note_ms_ < 5000) {
    v.hint_fg = color::Green;
    snprintf(v.hint, sizeof(v.hint), "%s", note_);
  } else if (onHeader()) {
    snprintf(v.hint, sizeof(v.hint), "%s", open_[headerGroup()] ? "Klik: luk gruppen" : "Klik: åbn gruppen");
  } else if (sel_ >= kCount) {
    v.hint_fg = changed() ? color::Yellow : color::Gray;
    snprintf(v.hint, sizeof(v.hint), "%s", changed() ? "Ændringer er ikke gemt" : "Ingen ændringer");
  } else {
    const Def& df = kDefs[sel_];
    const char* h = "Klik for at ændre";
    if (editing_) h = df.type == Type::Text ? "Skriv. Enter/klik = færdig" : "Rul for at ændre, klik = færdig";
    else if (df.type == Type::Bool) h = "Klik skifter til/fra";
    else if (df.type == Type::Action && df.key[0] == '>') h = "Klik for at åbne";
    else if (df.type == Type::Action) h = "Klik 2 gange: udføres med det samme";
    snprintf(v.hint, sizeof(v.hint), "%s%s", h, df.restart && !editing_ ? " (genstart)" : "");
  }
}

}  // namespace dm
