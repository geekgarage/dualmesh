// SPDX-License-Identifier: MIT
// GENERERET af tools/gen_lang.py ud fra lang/*.txt - ret ikke her.
#pragma once

namespace dm {
namespace lang {
struct Builtin {
  const char* id;
  const char* text;
};
static const char kText_en[] = R"DMLANG(#name: English
Omkring DualMesh	About DualMesh
DualMesh %s	DualMesh %s
MeshCore v1.18.0 + Meshtastic 2.x (komp.)	MeshCore v1.18.0 + Meshtastic 2.x (compat.)
Bygget	Built
Lavet af	Made by
Licens	License
%s, open source	%s, open source
Bygger på MeshCore, LVGL m.fl. (se README)	Built on MeshCore, LVGL and more (see README)
%lus	%lus
%lum	%lum
%lut	%lut
%lud	%lud
Enter sender (prikken gemmes)	Enter sends (the point is saved)
Enter sender lokationen	Enter sends the location
SOS stoppet	SOS stopped
SOS AKTIVERET	SOS ACTIVATED
SOS fortrudt	SOS cancelled
Ingen position: flyt kortet eller vent på GPS	No position: move the map or wait for GPS
Del i en samtale	Share in a conversation
Send som waypoint (%.20s)	Send as waypoint (%s)
Del og gem på kortet	Share and save on map
Gem på kortet	Save on map
Fjern prik (%.24s)	Remove point (%s)
Ryd alle gemte prikker (%d)	Clear all saved points (%d)
Stop og gem spor	Stop and save track
Start spor	Start track
Gemte spor	Saved tracks
Kortpakker	Map packs
Skjul vist spor	Hide shown track
Vis på kortet	Show on map
Ingen GPS-position endnu	No GPS position yet
Slet %.24s? Backspace igen = ja	Delete %s? Backspace again = yes
Slettet: %.30s	Deleted: %s
Kunne ikke slette %.20s	Could not delete %s
%s (MC)	%s (MC)
%s (MT)	%s (MT)
DM %s (MT)	DM %s (MT)
Sendt til %.30s	Sent to %s
Ikke sendt (Må sende er fra?)	Not sent (Allow send is off?)
Traceroute sendt, venter på svar	Traceroute sent, waiting for reply
Traceroute ikke sendt	Traceroute not sent
Nøgle ukendt - spurgt efter NodeInfo	Key unknown - asked for NodeInfo
DM sendt til %.26s	DM sent to %s
DM fejlede (%.24s)	DM failed (%s)
"Må sende" er fra (Indst. > MeshCore)	"Allow send" is off (Settings > MeshCore)
Kunne ikke sende	Could not send
Kanal	Channel
Kontakt	Contact
Kort	Map
Indst.	Settings
SOS-bekræftelse	SOS confirmation
SOS sendes nu hvert 5. minut.	SOS is now sent every 5 minutes.
Enter = stop SOS	Enter = stop SOS
Anden tast = fortsæt SOS	Other key = continue SOS
Sender SOS med din position på MeshCore	Sends SOS with your position on MeshCore
Public og Meshtastics primære kanal	Public and Meshtastic's primary channel
hvert 5. min, til du stopper den (hold	every 5 min until you stop it (hold the
trackball 5 s igen). Position sendes,	trackball 5 s again). Position is sent
også hvis deling er slået fra.	even if sharing is turned off.
Enter = SEND SOS     Anden tast = fortryd	Enter = SEND SOS     Other key = cancel
Ikke en erstatning for 112: ring 112!	Not a replacement for 112/911: call them!
søndag	Sunday
mandag	Monday
tirsdag	Tuesday
onsdag	Wednesday
torsdag	Thursday
fredag	Friday
lørdag	Saturday
januar	January
februar	February
marts	March
april	April
maj	May
juni	June
juli	July
august	August
september	September
oktober	October
november	November
december	December
Ukendt dato	Unknown date
I dag, %d. %s	Today, %d %s
I går, %d. %s	Yesterday, %d %s
Nævn @%.30s	Mention @%s
Skriv DM	Write DM
Bed om position	Request position
Bed om telemetri	Request telemetry
Bed om NodeInfo (navn, nøgle)	Request NodeInfo (name, key)
Del min position	Share my position
Testbesked: slå fra	Test message: turn off
Testbesked: slå til	Test message: turn on
Ryd samtalen	Clear conversation
Vis nøgle (til at dele kanalen)	Show key (to share the channel)
Ryd chatten	Clear chat
Telemetri	Telemetry
%s: spurgt, svaret kommer her	%s: requested, the reply appears here
%s: ikke sendt	%s: not sent
Testbesked slået til her	Test message turned on here
Testbesked slået fra	Test message turned off
Testbesked: kun DM/privat kanal	Test message: DM/private channel only
Nøgle (hex): %s	Key (hex): %s
PSK (base64): %s	PSK (base64): %s
Bekræft: ryd (kan ikke fortrydes)	Confirm: clear (cannot be undone)
Samtalen er ryddet	Conversation cleared
Vælg kanal eller kontakt at dele i	Choose a channel or contact to share in
Gemt	Saved
Gemt på kortet	Saved on map
Prikken er fjernet	Point removed
Bekræft: ryd alle prikker	Confirm: clear all points
Alle gemte prikker er ryddet	All saved points cleared
Spor optages	Recording track
Spor kræver SD-kort	Tracks need an SD card
Spor gemt (se Gemte spor)	Track saved (see Saved tracks)
Waypoint sendt: %.30s	Waypoint sent: %s
Waypoint ikke sendt	Waypoint not sent
Gemt på kortet (%.24s)	Saved on map (%s)
Ingen kontakter endnu.\nVenter på adverts fra andre noder.	No contacts yet.\nWaiting for adverts from other nodes.
Gem	Save
Annuller	Cancel
© OpenStreetMap-bidragydere	© OpenStreetMap contributors
Ingen kort på SD-kortet.\n\nLæg en .pmtiles-fil i /dualmesh/maps/ (fx med Indst. > System > SD som USB-drev). Kortpakker laves med tools/maps/make_map.py.	No maps on the SD card.\n\nPut a .pmtiles file in /dualmesh/maps/ (e.g. with Settings > System > SD as USB drive). Map packs are made with tools/maps/make_map.py.
@[%s] 	@[%s] 
Ingen beskeder endnu i denne samtale	No messages in this conversation yet
  leveret	  delivered
  FEJL	  FAILED
  sendt	  sent
%.*s	%.*s
!%s	!%s
Mig	Me
%s%.*s	%s%.*s
Kontakter (%d MeshCore, %d Meshtastic)	Contacts (%d MeshCore, %d Meshtastic)
Kontakter (%d)	Contacts (%d)
Ingen samtale valgt	No conversation selected
> %s_	> %s_
MC %s  flood  %s	MC %s  flood  %s
MC %s  %u hop  %s	MC %s  %u hop  %s
MT %s  %u hop  %s	MT %s  %u hop  %s
> %s	> %s
-> %s  %d m %s (%.0f°)	-> %s  %d m %s (%.0f°)
-> %s  %.1f km %s (%.0f°)	-> %s  %.1f km %s (%.0f°)
Zoom %d  %.5f, %.5f%s	Zoom %d  %.5f, %.5f%s
  (følger)	  (following)
%s%s: %s_	%s%s: %s_
Klik/Enter på en kontakt: skriv DM	Click/Enter on a contact: write DM
Skriv for at sende til %s	Type to send to %s
Meshtastic-app forbundet	Meshtastic app connected
Venter på Meshtastic-appen	Waiting for the Meshtastic app
Navn	Name
Parring: vælg enheden i Meshtastic-	Pairing: choose the device in the Meshtastic
appen og skriv PIN-koden ovenfor.	app and enter the PIN above.
Bluetooth: slået fra	Bluetooth: turned off
Bluetooth til app er slået til, men	Bluetooth for app is turned on, but
%s er slået fra (eller ikke startet).	%s is turned off (or not started).
Slå "Bluetooth til app" til under	Turn on "Bluetooth for app" under
Bluetooth i Indst., vælg app og Gem.	Bluetooth in Settings, choose app and Save.
T-Deck'en genstarter derefter.	The T-Deck then restarts.
MeshCore-app forbundet	MeshCore app connected
Venter på MeshCore-appen	Waiting for the MeshCore app
I kø til appen	Queued for the app
%d beskeder	%d messages
Parring: vælg enheden i MeshCore-	Pairing: choose the device in the MeshCore
Backspace: tilbage	Backspace: back
alle	all
ikke fundet	not found
slettede	deleted
Vis: < %s >   (%d i alt)	Show: < %s >   (%d in total)
Ingen caches indlæst.	No caches loaded.
GPX-filer: /dualmesh/caches/ på SD	GPX files: /dualmesh/caches/ on SD
Eller o: hent fra Opencaching	Or o: fetch from Opencaching
o: hent fra Opencaching   Backspace: tilbage	o: fetch from Opencaching   Backspace: back
Ingen position: afstand fra kortets midte	No position: distance from the map centre
Ingen slettede caches.	No deleted caches.
Ingen caches her.	No caches here.
Navigerer til %s	Navigating to %s
Klik: detaljer  V/H: filter  o: hent  e: field notes	Click: details  L/R: filter  o: fetch  e: field notes
Field notes: kan ikke skrive på SD	Field notes: cannot write to SD
%d field notes i caches/geocache_visits.txt	%d field notes in caches/geocache_visits.txt
%s %s (%.0f°)%s	%s %s (%.0f°)%s
 fra kortets midte	 from the map centre
%s  D%d%s/T%d%s	%s  D%d%s/T%d%s
intet	none
(klik/h for at se)	(click/h to show)
Stop navigation	Stop navigation
Navigér hertil	Navigate here
Fortryd fundet	Undo found
Fundet	Found
Fortryd ikke fundet	Undo not found
Ikke fundet	Not found
Gendan i listen	Restore to list
Bekræft: slet fra listen	Confirm: delete from list
Slet fra listen	Delete from list
Klik: vælg   h: hint   Backspace: tilbage	Click: choose   h: hint   Backspace: back
Alle	All
Kun %s (%s er slået fra)	Only %s (%s is turned off)
Vis: < %s >	Show: < %s >
Vis: %s  (Enter for filter)	Show: %s  (Enter for filter)
 (primær)	 (primary)
+ Tilføj %s-kanal	+ Add %s channel
MC-kanal: #navn (hashtag) eller navn	MC channel: #name (hashtag) or name
MT-kanalnavn (1-11 tegn):	MT channel name (1-11 chars):
Nøgle base64 (tom = ny tilfældig):	Key base64 (empty = new random):
PSK base64 (tom = standard AQ==):	PSK base64 (empty = default AQ==):
Slet kanalen? Backspace igen = ja	Delete the channel? Backspace again = yes
Klik/Enter: åbn (Public kan ikke slettes)	Click/Enter: open (Public cannot be deleted)
Klik/Enter: åbn (primær: se Indst.)	Click/Enter: open (primary: see Settings)
Klik/Enter: åbn  Backspace 2x: slet	Click/Enter: open  Backspace 2x: delete
Grøn = vores, [n] = nye beskeder	Green = ours, [n] = new messages
Venstre/højre: vælg  Klik/Enter: færdig	Left/right: choose  Click/Enter: done
Op/ned: vælg  Enter: åbn  Bksp 2x: slet	Up/down: choose  Enter: open  Bksp 2x: delete
Nøgle: %s	Key: %s
Fejl	Error
Public kan ikke slettes	Public cannot be deleted
Primær kanal: skift i Indst.	Primary channel: change in Settings
Vælg en kanal	Choose a channel
Slettet: %s	Deleted: %s
Kunne ikke slette %s	Could not delete %s
Kontaktfarver	Contact colors
Nyeste kontakter først. Farven følger nøglen.	Newest contacts first. The color follows the key.
Ingen kontakter endnu.	No contacts yet.
 >  valgt	 >  chosen
  valgt	  chosen
Ny identitet = ny farve. '!' i Chat = samme navn 2 gange.	New identity = new color. '!' in Chat = same name twice.
Venstre/højre: farve   Backspace: auto   Klik: tilbage	Left/right: color   Backspace: auto   Click: back
gul	yellow
laks	salmon
violet	violet
lyseblå	light blue
guld	gold
mint	mint
fersken	peach
himmelblå	sky blue
hvid	white
; DualMesh kontaktfarver: noegle (6 byte hex)<TAB>farvenummer 0-%d\n	; DualMesh kontaktfarver: noegle (6 byte hex)<TAB>farvenummer 0-%d\n
%02x%02x%02x%02x%02x%02x\t%d\n	%02x%02x%02x%02x%02x%02x\t%d\n
BRUG EFUSE	USE EFUSE
fra      almindelig tekst	off      plain text
Filerne gemmes ukrypteret.	The files are stored unencrypted.
Alle med SD-kortet kan læse koderne.	Anyone with the SD card can read the passwords.
chip-ID  låst til denne T-Deck	chip ID  locked to this T-Deck
Beskytter et tabt SD-kort. Svag: ID'et	Protects a lost SD card. Weak: the ID
kan læses af den, der har T-Deck'en.	can be read by whoever has the T-Deck.
kode     kode ved hver opstart	password password at every start
Stærk. Skriv koden ved opstart. Glemt	Strong. Enter the password at start. Forgotten
kode = WiFi-koder/nøgler/identitet tabt.	password = WiFi passwords/keys/identity lost.
eFuse    nøgle i chippen	eFuse    key in the chip
Stærkest: nøglen kan ikke læses ud.	Strongest: the key cannot be read out.
Kun denne T-Deck kan læse filerne.	Only this T-Deck can read the files.
SD-kryptering: %s	SD encryption: %s
WiFi-koder, kanalnøgler og MeshCore-	WiFi passwords, channel keys and the MeshCore
identiteten er krypteret med en kode.	identity are encrypted with a password.
Skriv koden og tryk Enter.	Type the password and press Enter.
Radioen kører imens; MeshCore og WiFi	The radio runs meanwhile; MeshCore and WiFi
starter, når koden er skrevet.	start when the password is entered.
Ingen eFuse-nøgle i denne chip:	No eFuse key in this chip:
de hemmelige filer kan ikke læses her.	the secret files cannot be read here.
Brug SD-kortet i den rigtige T-Deck,	Use the SD card in the right T-Deck,
eller nulstil DualMesh (R + JA, USB).	or reset DualMesh (R + JA, USB).
eFuse: nøgle findes (blok %d) og kan bruges	eFuse: key exists (block %d) and can be used
eFuse: ingen nøgle, %d frie pladser	eFuse: no key, %d free slots
Fra	Off
Chip-ID	Chip ID
Kode	Password
almindelig tekst	plain text
låst til denne T-Deck	locked to this T-Deck
kode ved hver opstart	password at every start
nøgle i chippen	key in the chip
Klik igen: brug eFuse-nøglen	Click again: use the eFuse key
Klik igen for at skifte	Click again to switch
Kode:	Password:
Ny kode (mindst 4 tegn):	New password (at least 4 chars):
Gentag koden:	Repeat the password:
UOMVENDELIGT! Skriv BRUG EFUSE og tryk Enter:	IRREVERSIBLE! Type USE EFUSE and press Enter:
 = nuværende	 = current
%s_	%s_
Op/ned: vælg   Klik: skift   Backspace: tilbage	Up/down: choose   Click: switch   Backspace: back
Skiftet til %s	Switched to %s
Er allerede valgt	Already chosen
Ingen fri eFuse-plads	No free eFuse slot
Låst op	Unlocked
Forkert kode	Wrong password
Mindst 4 tegn	At least 4 characters
Fortrudt (ikke BRUG EFUSE)	Cancelled (not USE EFUSE)
eFuse-brænding fejlede	eFuse burning failed
SD-kryptering	SD encryption
Hent tastaturlayouts	Get keyboard layouts
Hent sprog	Get languages
Hent kortpakker	Get map packs
En anden hentning kører; prøv igen bagefter	Another download is running; try again later
Kataloget kunne ikke hentes: %.40s	Could not get the catalogue: %s
%.30s: %.40s	%.30s: %.40s
Henter kataloget fra GitHub ...	Getting the catalogue from GitHub ...
Henter %.40s	Downloading %s
%u %%  %lu KB/s  ca. %lu min tilbage	%u %%  %lu KB/s  about %lu min left
%u %%  %lu KB/s	%u %%  %lu KB/s
Afbryd hentningen	Cancel the download
Klik: afbryd   Backspace: tilbage (hentningen fortsætter)	Click: cancel   Backspace: back (keeps downloading)
Hent igen	Download again
Bekræft: slet	Confirm: delete
Slet fra SD-kortet	Delete from SD card
Klik: vælg   Backspace: tilbage	Click: choose   Backspace: back
%d i kataloget på GitHub	%d in the catalogue on GitHub
%lu MB fri på SD-kortet	%lu MB free on the SD card
Kataloget er tomt.	The catalogue is empty.
© OpenStreetMap-bidragydere (ODbL)	© OpenStreetMap contributors (ODbL)
Klik igen: hent %lu MB (brug WiFi, ikke mobildata)	Click again: get %lu MB (use WiFi, not mobile data)
Klik: hent   Backspace: tilbage	Click: download   Backspace: back
En anden hentning kører allerede	Another download is already running
%s%s/index.txt	%s%s/index.txt
Kataloget kunne ikke hentes	Could not get the catalogue
%31s %llu %7s %199s %n	%31s %llu %7s %199s %n
%s%s/%s%s	%s%s/%s%s
%s.part	%s.part
installeret, %lu MB	installed, %lu MB
%u %% hentet	%u %% downloaded
installeret	installed
indbygget	built-in
kan hentes	available
Kunne ikke starte hentningen	Could not start the fetch
%.40s er slettet	%s is deleted
%s.tmp	%s.tmp
%s %s\n	%s %s\n
GPS: %s	GPS: %s
Modul: %lu baud, %lu sætninger	Module: %lu baud, %lu sentences
Modul: leder efter baud og pinde ...	Module: searching for baud and pins ...
%s fra %s	%s from %s
TIL	ON
FRA	OFF
Satellitter	Satellites
%u brugt, %u set, HDOP %u.%u	%u used, %u seen, HDOP %u.%u
Bredde	Latitude
Længde	Longitude
V	W
Ø	E
Højde og fart	Altitude and speed
%ld m, %u.%u km/t	%ld m, %u.%u km/h
aktuel	current
%lu s gammel	%lu s old
Ingen position endnu.	No position yet.
Første fix: 30 s til flere minutter	First fix: 30 s to several minutes
under åben himmel.	under open sky.
SOS: hold trackball-klik i 5 s	SOS: hold trackball click for 5 s
Klik: GPS %s   Backspace: tilbage	Click: GPS %s   Backspace: back
fra	off
til	on
%s%s(%s)	%s%s(%s)
MANGLER	MISSING
%s/%s	%s/%s
%s/%s.txt	%s/%s.txt
zoom %u-%u, %lu MB	zoom %u-%u, %lu MB
Gå til	Go to
Bekræft: slet pakken	Confirm: delete the pack
Slet pakken	Delete the pack
%d kortpakke(r) i /dualmesh/maps/	%d map pack(s) in /dualmesh/maps/
Ingen kortpakker.	No map packs.
Læg .pmtiles-filer på via USB-drevet	Add .pmtiles files via the USB drive
Klik: gå til eller slet   Backspace: tilbage	Click: go to or delete   Backspace: back
MQTT-bro: %s	MQTT bridge: %s
Rod: %.55s	Root: %s
Sendt til broker	Sent to broker
Emner (<net> = meshcore/meshtastic)	Topics (<net> = meshcore/meshtastic)
Modtaget	Received
Svar	Reply
Kanalnavne URL-kodes: #test = %%23test	Channel names are URL-encoded: #test = %%23test
Egen CA: /dualmesh/mqtt_ca.pem    Backspace: tilbage	Own CA: /dualmesh/mqtt_ca.pem    Backspace: back
SLET ALT	ERASE ALL
NVS: %lu af %lu poster brugt, %lu fri	NVS: %lu of %lu entries used, %lu free
Startet fra launcher: %.30s	Started from launcher: %s
Ingen launcher (kører fra %.8s). DualMesh bruger SD.	No launcher (running from %s). DualMesh uses SD.
Ryd Bluetooth-parringer	Clear Bluetooth pairings
Ryd WiFi- og radiodata (ESP-IDF)	Clear WiFi and radio data (ESP-IDF)
Ryd HELE NVS ...	Clear ALL of NVS ...
%u poster, %s	%u entries, %s
Klik igen for at rydde	Click again to clear
Skriv SLET ALT og tryk Enter:	Type ERASE ALL and press Enter:
Telefoner skal parres igen. Genstarter.	Phones must pair again. Restarting.
WiFi-kalibrering laves igen. Genstarter.	WiFi calibration is redone. Restarting.
Sletter OGSÅ launcherens indstillinger!	ALSO deletes the launcher's settings!
Sletter alle firmwares' NVS-data.	Deletes all firmwares' NVS data.
Tilhører launcher eller anden firmware!	Belongs to the launcher or another firmware!
Tilhører en anden firmware.	Belongs to another firmware.
Data fra ESP-IDF (Bluetooth/WiFi).	Data from ESP-IDF (Bluetooth/WiFi).
Klik 2 gange: ryd   Backspace: tilbage	Click twice: clear   Backspace: back
Intet at rydde	Nothing to clear
Bluetooth-parringer ryddet	Bluetooth pairings cleared
WiFi-/radiodata ryddet	WiFi/radio data cleared
Ryddet: %s	Cleared: %s
Kunne ikke rydde	Could not clear
hele NVS ryddet	all of NVS cleared
Rydning fejlede	Clearing failed
Fortrudt (ikke SLET ALT)	Cancelled (not ERASE ALL)
Intern NVS	Internal NVS
Opdatér firmware	Update firmware
Ingen udgivelse fundet: %.50s	No release found: %s
version.txt er ugyldig	version.txt is invalid
Denne: %s (%s)	This one: %s (%s)
Søger efter den nyeste udgivelse ...	Looking for the newest release ...
%s %s: %u %%  %lu KB/s	%s %s: %u %%  %lu KB/s
Henter	Downloading
Installerer	Installing
Afbryd	Cancel
Sluk ikke T-Deck'en under opdateringen	Do not switch off the T-Deck during the update
Klik: afbryd	Click: cancel
Version %s er installeret. Genstarter ...	Version %s is installed. Restarting ...
Version %s er gemt på SD-kortet	Version %s is saved on the SD card
Installér den sådan i launcheren:	Install it from the launcher like this:
1. Genstart, og åbn launcheren	1. Restart and open the launcher
2. Vælg SD > dualmesh > firmware	2. Choose SD > dualmesh > firmware
3. Vælg filen, og installér den:	3. Choose this file and install it:
Genstart nu	Restart now
Åbn launcheren, som da du installerede DualMesh	Open the launcher the way you did to install DualMesh
Klik: genstart   Backspace: tilbage	Click: restart   Backspace: back
På GitHub: %s (%s), %lu KB	On GitHub: %s (%s), %lu KB
Hent %s til SD-kortet	Save %s to the SD card
Opdatér til %s	Update to %s
Installér %s igen	Install %s again
Installér ældre %s	Install older %s
Klik igen for at starte	Click again to start
Søg igen	Check again
Launcher fundet: firmwaren gemmes på SD-kortet	Launcher found: the firmware is saved to the SD card
Du har den nyeste version	You have the newest version
%sversion.txt	%sversion.txt
Telemetri-tilladelser pr. kontakt	Telemetry permissions per contact
Bruges, når et valg i MeshCore står på "kontakter".	Used when a MeshCore choice is set to "contacts".
< %s >	< %s >
T = telemetri (batteri, temp.)   P = position   M = miljø	T = telemetry (battery, temp.)   P = position   M = environment
Venstre/højre: skift   Klik: tilbage	Left/right: change   Click: back
Telemetri-kontakter	Telemetry contacts
Eksportér profil	Export profile
Identitet, navne, kanaler, kontakter og WiFi-net	Identity, names, channels, contacts and WiFi networks
krypteret fil	encrypted file
DENNE T-DECK FÅR EN NY PROFIL	THIS T-DECK GETS A NEW PROFILE
Efter eksporten slettes identiteten og	After the export the identity and the
navnet her; en ny laves ved genstart.	name are deleted here; a new one is made at restart.
Så kører samme profil aldrig på to	So the same profile never runs on two
enheder. Kanaler, kontakter og WiFi	devices. Channels, contacts and WiFi
bliver. Gem filen godt!	stay. Keep the file safe!
Flyt filen til den nye enhed og vælg Importér dér.	Move the file to the new device and choose Import there.
Mappe	Folder
Denne T-Deck har fået en ny profil	This T-Deck has got a new profile
og genstarter om få sekunder.	and restarts in a few seconds.
Kopiér filen væk, før du nulstiller.	Copy the file away before you reset.
Denne T-Deck får en ny profil bagefter.	This T-Deck gets a new profile afterwards.
Importér profil: vælg fil	Import profile: choose file
Fra /dualmesh/eksport/ eller SD-kortets rod	From /dualmesh/eksport/ or the SD card root
Ingen .dmp-filer fundet.	No .dmp files found.
Læg filen på kortet fra en PC.	Put the file on the card from a PC.
Importér: %.50s	Import: %s
(standard)	(default)
Kort navn	Short name
MeshCore-identitet	MeshCore identity
ja	yes
NEJ	NO
Filer / WiFi-net	Files / WiFi networks
%d / %d	%d / %d
Den nuværende profil ERSTATTES	The current profile is REPLACED
Eksportér den først, hvis du vil beholde	Export it first if you want to keep
den. T-Deck'en genstarter.	it. The T-Deck restarts.
Skriv adgangskoden til filen.	Enter the password for the file.
Adgangskode:	Password:
Ny adgangskode (mindst 6 tegn):	New password (at least 6 chars):
Gentag adgangskoden:	Repeat the password:
Enter = erstat profilen og genstart	Enter = replace the profile and restart
Enter = fortsæt   Backspace = fortryd	Enter = continue   Backspace = cancel
Mindst 6 tegn	At least 6 characters
Adgangskoderne er forskellige	The passwords differ
Kontrol fejlede - intet slettet	Check failed - nothing deleted
Eksporteret og kontrolleret	Exported and checked
Importér profil	Import profile
Fejlrapport	Error report
Scan med telefonen. Åbner et udfyldt GitHub-issue.	Scan with your phone. Opens a filled-in GitHub issue.
Åbner GitHub-appen: tryk på Bug report	Opens the GitHub app: tap Bug report
DualMesh %s (%.16s)	DualMesh %s (%.16s)
Denne opstart: %.45s	This start: %s
Seneste gemte nedbrud	Latest saved crash
PC / adresse	PC / address
0x%08lx / 0x%08lx	0x%08lx / 0x%08lx
et ANDET build end dette	a DIFFERENT build than this
dette build	this build
Intet gemt.	Nothing saved.
Vis QR-kode til GitHub-issue	Show QR code for GitHub issue
Fuld log: T-Deck i USB, åbn	Full log: T-Deck on USB, open
Tryk x igen for at slette nedbruddet	Press x again to delete the crash
Loggen renses på din PC, før noget sendes.	The log is cleaned on your PC before anything is sent.
x x: slet gemt nedbrud   Backspace: tilbage	x x: delete saved crash   Backspace: back
Gemt nedbrud slettet	Saved crash deleted
Kunne ikke slette nedbruddet	Could not delete the crash
NULSTIL	RESET
Nulstil DualMesh	Reset DualMesh
Bevares: eksporterede profiler, kortpakker, launcher og NVS	Kept: exported profiles, map packs, launcher and NVS
Sletter indstillinger, identitet,	Deletes settings, identity,
kontakter, kanaler, beskeder, WiFi-net,	contacts, channels, messages, WiFi networks,
logs og lærte radioværdier.	logs and learned radio values.
Eksportér profil og nulstil	Export profile and reset
Nulstil uden eksport	Reset without export
Eksporteret	Exported
Ingen eksport: identiteten forsvinder.	No export: the identity disappears.
Skriv NULSTIL og tryk Enter	Type RESET and press Enter
Adgangskode til profil-filen	Password for the profile file
(mindst 6 tegn; bruges ved import).	(at least 6 chars; used for import).
Profilen kan importeres igen bagefter.	The profile can be imported again afterwards.
Identiteten kan IKKE gendannes!	The identity can NOT be restored!
Ny adgangskode:	New password:
Bekræft:	Confirm:
Op/ned: vælg   Klik: OK   Backspace: tilbage	Up/down: choose   Click: OK   Backspace: back
Eksport fejlede - intet slettet	Export failed - nothing deleted
Fortrudt (ikke NULSTIL)	Cancelled (not RESET)
Nulstillet - genstarter	Reset - restarting
Nulstilling fejlede delvist	Reset partly failed
nej	no
kontakter	contacts
præcis	exact
omtrent	approx.
egne kanaler	own channels
Viser radio, net, tid, WiFi, GPS, batteri og modtagne pakker. Klik for at åbne; klik igen for at gå tilbage.	Shows radio, networks, time, WiFi, GPS, battery and received packets. Click to open; click again to go back.
Dit navn på nettet: MeshCores navn og Meshtastics lange navn. Eksempel: DualMesh_Anna. Tom = DualMesh-XXXX.	Your name on the network: MeshCore's name and Meshtastic's long name. Example: DualMesh_Anna. Empty = DualMesh-XXXX.
Meshtastics korte navn, højst 4 tegn, vises i lister og på kort. Eksempel: GEEK. Tom = automatisk.	Meshtastic's short name, at most 4 chars, shown in lists and on maps. Example: GEEK. Empty = automatic.
Gemmer identitet, navne, kanaler, kontakter og WiFi i én fil med adgangskode. Bagefter får denne T-Deck en ny profil.	Saves identity, names, channels, contacts and WiFi in one password-protected file. Afterwards this T-Deck gets a new profile.
Henter en eksporteret profil (.dmp) fra /dualmesh/eksport/ eller SD-kortets rod og erstatter den nuværende.	Loads an exported profile (.dmp) from /dualmesh/eksport/ or the SD card root and replaces the current one.
Lyt og send på MeshCore. Fra: radioen bruger kun Meshtastic, og MeshCores indstillinger låses. Kræver genstart.	Listen and send on MeshCore. Off: the radio only uses Meshtastic, and MeshCore's settings are locked. Needs restart.
MeshCores radio (frekvens, båndbredde, SF), som i MeshCore-appen. Rul for at vælge. Danmark: EU/UK (Narrow).	MeshCore's radio (frequency, bandwidth, SF), as in the MeshCore app. Scroll to choose. Denmark: EU/UK (Narrow).
Egne radioværdier i stedet for preset: frekvens Hz, båndbredde Hz, SF, CR adskilt af komma. Eksempel: 869618000,62500,8,8. Tom = preset.	Own radio values instead of a preset: frequency Hz, bandwidth Hz, SF, CR separated by commas. Example: 869618000,62500,8,8. Empty = preset.
Må T-Deck'en sende på MeshCore? Fra = kun lytte, fx hvis antennen er taget af. SOS respekterer også dette.	May the T-Deck send on MeshCore? Off = listen only, e.g. if the antenna is removed. SOS respects this too.
Din position i adverts: fra, præcis eller omtrent (afrundet til ca. 1 km). Rul for at vælge. SOS sender altid præcis position.	Your position in adverts: off, exact or approx. (rounded to about 1 km). Scroll to choose. SOS always sends the exact position.
Advert til direkte naboer (nul hop, sendes ikke videre) hvert N. minut. Eksempel: 30. 0 = fra.	Advert to direct neighbours (zero hop, not forwarded) every N minutes. Example: 30. 0 = off.
Advert til hele nettet via repeatere hver N. time. Eksempel: 12. 0 = fra, ellers mindst 3 for ikke at belaste nettet.	Advert to the whole network via repeaters every N hours. Example: 12. 0 = off, otherwise at least 3 to spare the network.
Må andre hente din telemetri (batteri, temperatur)? nej, alle, eller kun kontakter med tilladelse (Telem.-kontakter). Rul for at vælge.	May others fetch your telemetry (battery, temperature)? no, all, or only contacts with permission (Telem. contacts). Scroll to choose.
Din GPS-position med i telemetrien: nej, ja eller kun kontakter med tilladelse. Præcision som Del position (omtrent = ca. 1 km).	Your GPS position in telemetry: no, yes or only contacts with permission. Precision as Share position (approx. = about 1 km).
Miljødata med i telemetrien: nej, ja eller kun kontakter med tilladelse. T-Deck'en har ingen miljøsensorer endnu.	Environment data in telemetry: no, yes or only contacts with permission. The T-Deck has no environment sensors yet.
Giv enkelte kontakter lov til telemetri (T), position (P) og miljø (M). Bruges ved valget "kontakter" ovenfor.	Allow individual contacts telemetry (T), position (P) and environment (M). Used with the choice "contacts" above.
Send en advert til direkte naboer nu, så de kan se dig som kontakt. Belaster ikke nettet.	Send an advert to direct neighbours now so they can see you as a contact. Does not load the network.
Send en advert til hele nettet nu. Den sendes videre af alle repeatere, så brug den sjældent.	Send an advert to the whole network now. All repeaters forward it, so use it rarely.
Sendeeffekt på MeshCore i dBm, 1-22 (aldrig over regionens grænse). Eksempel: 22. Lavere = kortere rækkevidde, men mindre strøm.	Transmit power on MeshCore in dBm, 1-22 (never above the region's limit). Example: 22. Lower = shorter range but less power.
Testbeskeden slås til med tandhjulet i en DM eller privat kanal. Højst 120 tegn. Tom = standardtekst.	The test message is turned on with the gear in a DM or private channel. At most 120 chars. Empty = default text.
Minutter mellem testbeskeder, 2-1440. Eksempel: 15. Aldrig under 10 x sendetiden (højst 10 % airtime).	Minutes between test messages, 2-1440. Example: 15. Never below 10 x the airtime (at most 10 % airtime).
Lyt og send på Meshtastic. Fra: radioen bruger kun MeshCore, og Meshtastics indstillinger låses. Kræver genstart.	Listen and send on Meshtastic. Off: the radio only uses MeshCore, and Meshtastic's settings are locked. Needs restart.
Dit lands båndplan (frekvenser og max effekt). Rul for at vælge. Danmark: EU_868.	Your country's band plan (frequencies and max power). Scroll to choose. Denmark: EU_868.
Modem-preset, skal være det samme som nettet omkring dig. Rul for at vælge. Mest brugt: LONG_FAST.	Modem preset, must match the network around you. Scroll to choose. Most used: LONG_FAST.
Navnet på den primære kanal, højst 11 tegn. Eksempel: LongFast. Tom = presetnavnet.	Name of the primary channel, at most 11 chars. Example: LongFast. Empty = the preset name.
Frekvens-slot i båndet. 0 = beregnes ud fra kanalnavnet, som Meshtastic selv gør. Kun ændres, hvis nettet bruger et fast slot.	Frequency slot in the band. 0 = computed from the channel name, as Meshtastic does. Only change it if the network uses a fixed slot.
Din position til Meshtastic: fra, præcis eller omtrent (ca. 1 km). Rul for at vælge.	Your position for Meshtastic: off, exact or approx. (about 1 km). Scroll to choose.
Send på Meshtastic: tekst, NodeInfo og (hvis delt) position. Fra: der lyttes kun.	Send on Meshtastic: text, NodeInfo and (if shared) position. Off: listen only.
Sendeeffekt på Meshtastic i dBm, 1-22 (aldrig over regionens grænse). Eksempel: 22. Lavere = kortere rækkevidde, men mindre strøm.	Transmit power on Meshtastic in dBm, 1-22 (never above the region's limit). Example: 22. Lower = shorter range but less power.
Hvor tit din position sendes, når Del position er slået til. Eksempel: 30. 0 = aldrig.	How often your position is sent when Share position is on. Example: 30. 0 = never.
Hvor tit dit navn (NodeInfo) sendes, så andre kan se dig. Eksempel: 3. 0 = kun ved start og når nogen spørger.	How often your name (NodeInfo) is sent so others can see you. Example: 3. 0 = only at start and when someone asks.
Send andres pakker videre (rolle CLIENT). egne kanaler: kun dem, du kan læse. alle: også fremmede kanaler. Fra = CLIENT_MUTE.	Forward other nodes' packets (role CLIENT). own channels: only those you can read. all: also foreign channels. Off = CLIENT_MUTE.
Send batteri, spænding og oppetid (ingen position) hvert N. minut, og svar når nogen spørger. Eksempel: 30. 0 = fra.	Send battery, voltage and uptime (no position) every N minutes, and reply when someone asks. Example: 30. 0 = off.
Kanal for position og telemetri (navn, fx DMtest). Tom = primær. Findes den ikke, sendes intet. NodeInfo og SOS bruger altid den primære.	Channel for position and telemetry (name, e.g. DMtest). Empty = primary. If it does not exist, nothing is sent. NodeInfo and SOS always use the primary.
Hvor mange beskeder pr. kanal der gemmes på SD og vises i Chat, 10-1000. Eksempel: 200.	How many messages per channel are kept on SD and shown in Chat, 10-1000. Example: 200.
Hvor mange beskeder pr. DM der gemmes på SD og vises i Chat, 10-1000. Eksempel: 200.	How many messages per DM are kept on SD and shown in Chat, 10-1000. Example: 200.
Vis kun kanalbeskeder fra de sidste N dage. Eksempel: 7. 0 = alle. Antal eller dage: det, der rammer først.	Only show channel messages from the last N days. Example: 7. 0 = all. Count or days: whichever comes first.
Vis kun DM'er fra de sidste N dage. Eksempel: 30. 0 = alle. Antal eller dage: det, der rammer først.	Only show DMs from the last N days. Example: 30. 0 = all. Count or days: whichever comes first.
Vælg farve for hver kontakt i Chat. Farven følger kontaktens nøgle, så en ny identitet får en ny farve.	Choose a color for each contact in Chat. The color follows the contact's key, so a new identity gets a new color.
Firmwareversion, hvem der står bag, licenser og en QR-kode til projektet på GitHub.	Firmware version, who is behind it, licenses and a QR code to the project on GitHub.
Seneste nedbrud og en QR-kode til et udfyldt GitHub-issue. Fuld log: sæt i USB og åbn siden, der vises.	Latest crash and a QR code for a filled-in GitHub issue. Full log: connect USB and open the page shown.
Sletter den samtale, der er åben i Chat: fil på SD, hukommelse og ulæste. Klik 2 gange.	Deletes the conversation open in Chat: file on SD, memory and unread. Click twice.
Sletter alle kanal-chats for begge net. DM'er bevares. Klik 2 gange.	Deletes all channel chats for both networks. DMs are kept. Click twice.
Minutter mellem SOS-udsendelser, mens SOS er aktiv, 2-60. Eksempel: 5. SOS startes ved at holde trackballen i 5 s.	Minutes between SOS broadcasts while SOS is active, 2-60. Example: 5. SOS is started by holding the trackball for 5 s.
Sekunder mellem punkter, mens et spor optages. Eksempel: 5. Kortere = mere præcist spor, større fil.	Seconds between points while a track is recorded. Example: 5. Shorter = more precise track, bigger file.
Et punkt gemmes kun, når du har flyttet dig så mange meter. Eksempel: 10. Sparer plads, når du står stille.	A point is only saved when you have moved this many metres. Example: 10. Saves space when standing still.
Hvor langt kortet flytter sig pr. ryk på trackballen, 1-10. Højere = hurtigere panorering. Eksempel: 5.	How far the map moves per trackball nudge, 1-10. Higher = faster panning. Example: 5.
Opencaching-sidens adresse. Eksempel: www.opencaching.de (også .pl, .nl, .ro, .us, opencache.uk). Tom = www.opencaching.de.	Address of the Opencaching site. Example: www.opencaching.de (also .pl, .nl, .ro, .us, opencache.uk). Empty = www.opencaching.de.
Din gratis API-nøgle (consumer key) fra sidens /okapi/signup.html. Hver Opencaching-side har sin egen nøgle.	Your free API key (consumer key) from the site's /okapi/signup.html. Each Opencaching site has its own key.
Caches inden for så mange km fra din position (eller kortets midte) hentes. Eksempel: 10. Højst 500 caches.	Caches within this many km of your position (or the map centre) are fetched. Example: 10. At most 500 caches.
Henter caches fra Opencaching over WiFi og gemmer dem på SD (opencaching.gpx), så de også virker uden net.	Fetches caches from Opencaching over WiFi and saves them on SD (opencaching.gpx), so they also work offline.
GPS'en går i dvale, når intet har brugt den i 1 min (skærm slukket, intet spor, ingen deling eller SOS). Nyt fix på få sekunder.	The GPS sleeps when nothing has used it for 1 min (screen off, no track, no sharing or SOS). New fix within seconds.
CPU'en kører langsommere (80 MHz), mens skærmen er slukket. Sparer strøm; radioen og net virker som før.	The CPU runs slower (80 MHz) while the screen is off. Saves power; radio and networks work as before.
Brugerfladens sprog. Rul for at vælge. Flere sprog: /dualmesh/lang/<id>.txt på SD. Kræver genstart.	Language of the user interface. Scroll to choose. More languages: /dualmesh/lang/<id>.txt on SD. Needs restart.
Skærmens lysstyrke fra 1 (svag) til 16 (fuld). Lavere sparer strøm.	Screen brightness from 1 (dim) to 16 (full). Lower saves power.
Skærmen dæmpes og slukker efter så mange sekunder uden input. Eksempel: 20. 0 = aldrig.	The screen dims and turns off after this many seconds without input. Example: 20. 0 = never.
Sluk også skærmen, når T-Deck'en er sluttet til USB. Fra: skærmen er tændt på USB.	Also turn off the screen when the T-Deck is on USB. Off: the screen stays on with USB.
Touch: byt X og Y, hvis et tryk rammer et helt andet sted end fingeren.	Touch: swap X and Y if a tap lands somewhere else entirely than your finger.
Touch: spejl vandret, hvis tryk i venstre side rammer højre side.	Touch: mirror horizontally if taps on the left land on the right.
Touch: spejl lodret, hvis tryk øverst rammer nederst.	Touch: mirror vertically if taps at the top land at the bottom.
Tastaturlysets styrke ved tastetryk, 0-255. Eksempel: 127. 0 = aldrig tændt.	Keyboard light strength on key press, 0-255. Example: 127. 0 = never on.
Sekunder før tastaturlyset slukker efter sidste tryk. Eksempel: 10.	Seconds before the keyboard light turns off after the last press. Example: 10.
Layouts for specialtegn (fx a -> å æ á), adskilt af komma. Eksempel: da,latin.	Layouts for special characters (e.g. a -> å æ á), separated by commas. Example: da,latin.
Henter nye tastaturlayouts fra GitHub over WiFi. Et hentet layout tages i brug med det samme.	Gets new keyboard layouts from GitHub over WiFi. A downloaded layout is used right away.
Henter kortpakker fra GitHub over WiFi (store filer: brug WiFi, ikke mobildata). En afbrudt hentning genoptages.	Gets map packs from GitHub over WiFi (large files: use WiFi, not mobile data). An interrupted download resumes.
Henter flere sprog fra GitHub over WiFi. Vælg dem bagefter under Sprog / Language.	Gets more languages from GitHub over WiFi. Choose them afterwards under Sprog / Language.
Finder den nyeste firmware på GitHub og installerer den over WiFi. Med en launcher gemmes den på SD-kortet.	Finds the newest firmware on GitHub and installs it over WiFi. With a launcher it is saved to the SD card.
Læs tastaturet direkte, så alle taster virker. Slå kun fra ved gammel tastaturfirmware. Kræver genstart.	Read the keyboard directly so all keys work. Only turn off with old keyboard firmware. Needs restart.
Hvor følsom trackballen er, 1-10. 10 = et lille ryk flytter, 1 = skal rulles meget. Eksempel: 5.	How sensitive the trackball is, 1-10. 10 = a small nudge moves, 1 = needs a lot of rolling. Example: 5.
Vend trackballens vandrette retning.	Reverse the trackball's horizontal direction.
Vend trackballens lodrette retning.	Reverse the trackball's vertical direction.
Hvor tydeligt et sideryk skal være for at skifte fane, 1-10. Højere = ingen hop til siden ved scroll. Eksempel: 3.	How clear a sideways nudge must be to switch tab, 1-10. Higher = no sideways jumps when scrolling. Example: 3.
Pause før et nyt klik tæller, så ét tryk ikke bliver til to. Eksempel: 150.	Pause before a new click counts, so one press does not become two. Example: 150.
Status, scan, forbind (også skjulte net) og glem gemte netværk. Koder gemmes krypteret på SD.	Status, scan, connect (also hidden networks) and forget saved networks. Passwords are stored encrypted on SD.
WiFi til/fra. Til: forbinder selv til det stærkeste gemte net (højst 6 forsøg med 10 s imellem).	WiFi on/off. On: connects by itself to the strongest saved network (at most 6 tries 10 s apart).
Hent tiden fra internettet (NTP), når WiFi er forbundet. GPS-tiden har forrang.	Get the time from the internet (NTP) when WiFi is connected. GPS time takes precedence.
Viser broens tilstand, emne-rod og emnerne. MQTT sender modtagne beskeder til din broker og kan sende fra den.	Shows the bridge state, topic root and the topics. MQTT sends received messages to your broker and can send from it.
Sender modtagne beskeder til en MQTT-broker, når WiFi er forbundet, og kan sende fra den. Kræver Broker.	Sends received messages to an MQTT broker when WiFi is connected, and can send from it. Needs Broker.
Broker-adresse. Eksempel: mqtt://min-pc.lan:1883 eller mqtts://host:8883 (TLS; egen CA i /dualmesh/mqtt_ca.pem).	Broker address. Example: mqtt://my-pc.lan:1883 or mqtts://host:8883 (TLS; own CA in /dualmesh/mqtt_ca.pem).
Brugernavn til brokeren. Tom = intet login. Eksempel: dualmesh	User name for the broker. Empty = no login. Example: dualmesh
Kode til brokeren; gemmes krypteret, ikke i settings.ini. Tom = behold den gemte. Skriv - for at slette den.	Password for the broker; stored encrypted, not in settings.ini. Empty = keep the saved one. Type - to delete it.
Starten af alle emner. Eksempel: dualmesh/tdeck giver dualmesh/tdeck/rx/meshcore/chan/Public. Tom = dualmesh/<id>.	The start of all topics. Example: dualmesh/tdeck gives dualmesh/tdeck/rx/meshcore/chan/Public. Empty = dualmesh/<id>.
Tillad at sende ud på LoRa fra MQTT (<rod>/tx/...). Højst én pr. 10 s og inden for 10 %-grænsen. Standard fra.	Allow sending on LoRa from MQTT (<root>/tx/...). At most one per 10 s and within the 10 % limit. Default off.
Hvor tit status (batteri, GPS-fix, net) sendes til <rod>/status. 0 = kun ved forbindelse. Eksempel: 5	How often status (battery, GPS fix, networks) is sent to <root>/status. 0 = only on connect. Example: 5
Egen position i MQTT-status: fra, præcis eller omtrent (ca. 1 km). Kræver GPS-fix. Standard fra.	Own position in MQTT status: off, exact or approx. (about 1 km). Needs a GPS fix. Default off.
Tidsserver til NTP. Eksempel: dk.pool.ntp.org. Tom = pool.ntp.org.	Time server for NTP. Example: dk.pool.ntp.org. Empty = pool.ntp.org.
Viser Bluetooth-navnet, PIN-koden og om MeshCore-appen er forbundet.	Shows the Bluetooth name, the PIN and whether the MeshCore app is connected.
Lad en app på telefonen bruge T-Deck'en over Bluetooth (se App). Kræver genstart.	Let an app on the phone use the T-Deck over Bluetooth (see App). Needs restart.
Hvilken app Bluetooth taler med: MeshCore-appen eller Meshtastic-appen (én ad gangen). Kræver genstart.	Which app Bluetooth talks to: the MeshCore app or the Meshtastic app (one at a time). Needs restart.
Seks cifre til parring, fx 482913. Gemmes krypteret; vises under Bluetooth-status. Tom = behold, - = standard 123456. Kræver genstart.	Six digits for pairing, e.g. 482913. Stored encrypted; shown under Bluetooth status. Empty = keep, - = default 123456. Needs restart.
Viser fix, satellitter, position, højde, fart og ur fra GPS'en. Klik i skærmen slår GPS til/fra.	Shows fix, satellites, position, altitude, speed and clock from the GPS. Click in the screen turns GPS on/off.
GPS til/fra. Fra sparer strøm. SOS tænder den selv ved behov.	GPS on/off. Off saves power. SOS turns it on when needed.
Sæt uret fra GPS. Det er den mest præcise kilde og går forud for NTP og mesh.	Set the clock from GPS. It is the most precise source and takes precedence over NTP and mesh.
Chippens interne lager, som deles med launcher og andre firmwares. Se forbrug og ryd Bluetooth/WiFi-data.	The chip's internal storage, shared with the launcher and other firmwares. See usage and clear Bluetooth/WiFi data.
Kryptering af koder og nøgler på SD: fra, chip-ID, kode ved opstart eller eFuse (nøgle i chippen).	Encryption of passwords and keys on SD: off, chip ID, password at start or eFuse (key in the chip).
Sletter alle DualMesh-data på SD. Eksporterede profiler og kortpakker bevares; du kan eksportere først.	Deletes all DualMesh data on SD. Exported profiles and map packs are kept; you can export first.
Sletter alle logfiler på SD-kortet og starter en ny log. Klik 2 gange.	Deletes all log files on the SD card and starts a new log. Click twice.
Genstarter, så SD-kortet vises som et drev på PC'en (radioen er slukket imens). Skub drevet ud for at vende tilbage.	Restarts so the SD card shows up as a drive on the PC (the radio is off meanwhile). Eject the drive to return.
Profil	Profile
Beskeder	Messages
Skærm	Screen
Tastatur og trackball	Keyboard and trackball
(radio, net, strøm)	(radio, nets, power)
Navn/Langt Navn	Name/Long Name
Kort Navn	Short Name
(til fil på SD)	(to file on SD)
(fra fil på SD)	(from file on SD)
Brug MeshCore	Use MeshCore
Egne radioværdier	Own radio values
(fra)	(off)
Må sende	Allow send
Del position	Share position
Telemetri-svar	Telemetry replies
Position i telem.	Pos. in telem.
Miljø i telem.	Env. in telem.
Telem.-kontakter	Telem. contacts
(klik for at vælge)	(click to choose)
Lokal advert, min	Local advert, min
Flood-advert, t	Flood advert, h
Send lokal advert	Send local advert
(klik: send nu)	(click: send now)
Send flood-advert	Send flood advert
Sendeeffekt, dBm	TX power, dBm
Brug Meshtastic	Use Meshtastic
Primær kanal	Primary channel
(presetnavn)	(preset name)
Frekvens-slot	Frequency slot
aldrig	never
NodeInfo, t	NodeInfo, h
kun ved start	only at start
Videresend	Forward
Telemetri, min	Telemetry, min
Kanal for data	Channel for data
(primær)	(primary)
Antal pr. kanal	Count per channel
Antal pr. DM	Count per DM
Dage pr. kanal	Days per channel
Dage pr. DM	Days per DM
Testbesked tekst	Test message text
(standardtekst)	(default text)
Testbesked, min	Test message, min
Ryd åben chat	Clear open chat
(klik 2 gange)	(click twice)
Ryd kanal-chats	Clear chan. chats
SOS-interval, min	SOS interval, min
Lysstyrke	Brightness
Sluk efter, sek	Off after, sec
Sluk også på USB	Off on USB too
Touch byt X/Y	Touch swap X/Y
Touch vend X	Touch flip X
Touch vend Y	Touch flip Y
Tastaturlysstyrke	Keyboard light
Tastaturlys, sek	Kbd light, sec
Tastaturlayouts	Keyboard layouts
Hent layouts	Get layouts
(fra GitHub)	(from GitHub)
Rå tastatur	Raw keyboard
Trackball følsom	Trackball sens.
Trackball vend X	Trackball flip X
Trackball vend Y	Trackball flip Y
Trackball dødzone	Ball deadzone
Klik-pause, ms	Click pause, ms
WiFi-netværk	WiFi networks
(klik: status/scan)	(click: status/scan)
Brug WiFi	Use WiFi
Tid fra NTP	Time from NTP
(klik for at se)	(click to view)
Brug MQTT-bro	Use MQTT bridge
(ingen)	(none)
Brugernavn	User name
(intet login)	(no login)
Adgangskode	Password
(skjult)	(hidden)
Emne-rod	Topic root
Må sende fra MQTT	Send from MQTT
Status, min	Status, min
Position i status	Pos. in status
Bluetooth til app	Bluetooth for app
Bluetooth PIN	Bluetooth PIN
Brug GPS	Use GPS
Tid fra GPS	Time from GPS
GPS-strømspar	GPS power save
Kortfølsomhed	Map sensitivity
Spor-interval, s	Track interval, s
Spor-afstand, m	Track distance, m
Opencaching-side	Opencaching site
(opencaching.de)	(opencaching.de)
Opencaching-nøgle	Opencaching key
Hent radius, km	Fetch radius, km
Hent caches nu	Fetch caches now
(kræver WiFi)	(needs WiFi)
Strømspar (CPU)	Power save (CPU)
(se / ryd)	(view / clear)
(Nulstil alt)	(Reset all)
Ryd log	Clear log
SD som USB-drev	SD as USB drive
(nedbrud, QR)	(crash, QR)
(version, licens)	(version, license)
MeshCore er slået fra	MeshCore is turned off
Meshtastic er slået fra	Meshtastic is turned off
Ingen ændringer at gemme	No changes to save
MT-regionen findes ikke	The MT region does not exist
MT-preset virker ikke i regionen	The MT preset does not work in the region
%lu,%lu,%u,%u	%lu,%lu,%u,%u
MC egen radio: Hz,bw,sf,cr	MC own radio: Hz,bw,sf,cr
IKKE gemt (intet SD) - radio uændret	NOT saved (no SD) - radio unchanged
Gælder til genstart (intet SD)	Applies until restart (no SD)
Gemt - genstarter radioen...	Saved - restarting the radio...
Ændringer annulleret	Changes cancelled
Ingen ændringer	No changes
Ikke muligt	Not possible
Klik igen inden 4 s for at udføre	Click again within 4 s to run
(ingen indstillinger endnu)	(no settings yet)
klik igen = udfør	click again = run
Klik: luk gruppen	Click: close the group
Klik: åbn gruppen	Click: open the group
Ændringer er ikke gemt	Changes are not saved
Klik for at ændre	Click to change
Skriv. Enter/klik = færdig	Type. Enter/click = done
Rul for at ændre, klik = færdig	Scroll to change, click = done
Klik skifter til/fra	Click toggles on/off
Klik for at åbne	Click to open
Klik 2 gange: udføres med det samme	Click twice: runs immediately
 (genstart)	 (restart)
Bekræft: slet sporet	Confirm: delete the track
Slet sporet	Delete the track
Optager: %s (%d punkter)	Recording: %s (%d points)
Ingen optagelse i gang	No recording in progress
Vises: %s	Shown: %s
Ingen gemte spor endnu.	No saved tracks yet.
vises	shown
Filerne: /dualmesh/tracks/ (csv og gpx)	Files: /dualmesh/tracks/ (csv and gpx)
Klik: vis eller slet   Backspace: tilbage	Click: show or delete   Backspace: back
%.2s-%.2s-%.4s %.2s:%.2s	%.2s-%.2s-%.4s %.2s:%.2s
Forbundet til %.40s	Connected to %s
IP %s   signal %d dBm	IP %s   signal %d dBm
Forbinder til %.40s ...	Connecting to %s ...
Fejl: %.50s	Error: %s
WiFi: %s	WiFi: %s
Scanner ...	Scanning ...
Tid: ukendt (venter på NTP, GPS eller mesh)	Time: unknown (waiting for NTP, GPS or mesh)
Tid: %s fra %s%s	Time: %s from %s%s
Scan efter netværk	Scan for networks
scanner ...	scanning ...
Forbind til skjult netværk ...	Connect to hidden network ...
gemt  	saved
låst	locked
åben	open
gemt  %s	saved  %s
skjult	hidden
ikke set	not seen
Slå WiFi til for at scanne	Turn WiFi on to scan
Klik igen for at afbryde forbindelsen	Click again to disconnect
Backspace igen for at glemme netværket	Backspace again to forget the network
Navn (SSID) på det skjulte netværk:	Name (SSID) of the hidden network:
Kode til %.40s:	Password for %s:
 = forbundet   gul = gemt	 = connected   yellow = saved
Klik: vælg/forbind   Backspace: glem eller tilbage	Click: choose/connect   Backspace: forget or back
WiFi slået til	WiFi turned on
WiFi slået fra	WiFi turned off
Afbrudt	Disconnected
Forbinder til %.28s	Connecting to %s
Glemt: %.32s	Forgotten: %s
egen %.3f MHz SF%u	egen %.3f MHz SF%u
%02lu:%02lu:%02lu.%03lu	%02lu:%02lu:%02lu.%03lu
 %s-hul %.0f ms kl. %s,	 %s-hul %.0f ms kl. %s,
 skift %.1f ms kl. %s,	 skift %.1f ms kl. %s,
 SD-log %.1f ms kl. %s	 SD-log %.1f ms kl. %s
 frekvensfejl=%+ld Hz	 frekvensfejl=%+ld Hz
cad_peak_%lu_sf%u_bw%lu	cad_peak_%lu_sf%u_bw%lu
strøm tilsluttet	power connected
ukendt (fx USB-porten åbnet)	unknown (e.g. USB port opened)
software-genstart	software restart
NEDBRUD (panic)	CRASH (panic)
BROWNOUT (lav spænding - normalt efter sluk/tænd med afbryderen)	BROWNOUT (low voltage - normal after off/on with the switch)
vågnet fra dyb søvn	woke from deep sleep
reset-knap	reset button
kode %d	code %d
kalibrerer	calibrating
tilpasser CAD	adapting CAD
stabil	stable
lader	charging
batteri	battery
> wifi.ssid is now %s (set wifi.pwd too)	> wifi.ssid is now %s (set wifi.pwd too)
> wifi.enabled is now %d	> wifi.enabled is now %d
> %d	> %d
MeshCore-%s	MeshCore-%s
DualMesh_%04lx	DualMesh_%04lx
Sidst kendte pos %s,%s (%lu min gammel)	Last known pos %s,%s (%lu min old)
Pos %s,%s	Pos %s,%s
Position ukendt (ingen GPS-fix)	Position unknown (no GPS fix)
SOS fra %s er stoppet.	SOS fra %s er stoppet.
SOS! Har brug for hjælp. %s kl %s	SOS! Har brug for hjælp. %s kl %s
\\aSOS fra %s! Har brug for hjælp. %s kl %s	\\aSOS fra %s! Har brug for hjælp. %s kl %s
SOS: venter på GPS-fix (%lu:%02lu)	SOS: waiting for GPS fix (%lu:%02lu)
SOS AKTIV: %lu runder, næste om %ld:%02ld	SOS ACTIVE: %lu rounds, next in %ld:%02ld
SPI-FEJL	SPI ERROR
Oppe %lu:%02lu:%02lu   %s   %.2f V %s	Up %lu:%02lu:%02lu   %s   %.2f V %s
Radio: %s	Radio: %s
FEJL ved start	ERROR at start
svarer ikke	not responding
starter ...	starting ...
Radio %s   sync %s   skift %lu us   %d dBm	Radio %s   sync %s   switch %lu us   %d dBm
FEJL	ERROR
SOS: venter på GPS-fix, sender derefter	SOS: waiting for GPS fix, then sending
SOS AKTIV: hold trackball 5 s for stop	SOS ACTIVE: hold trackball 5 s to stop
kører	running
ikke forbundet	not connected
%u satellitter	%u satellites
søger	searching
%s: slået fra (Indst.)	%s: turned off (Settings)
%.3f MHz, BW %.1f, SF%u, CR4/%u	%.3f MHz, BW %.1f, SF%u, CR4/%u
Aktivitet	Activity
CAD %lu, pakker %lu, støj %.1f%%	CAD %lu, packets %lu, noise %.1f%%
%lu min	%lu min
Sidst hørt	Last heard
%d dBm, %.1f dB, for %s	%d dBm, %.1f dB, %s ago
ingen pakker endnu	no packets yet
Node og afsendelse	Node and sending
Kontakter / beskeder	Contacts / messages
%d / kanal %lu, DM %lu	%d / channel %lu, DM %lu
Sendt / watchdog	Sent / watchdog
%lu / %lu%s	%lu / %lu%s
 (testbesked til)	 (test message on)
Største hul MT/MC	Largest gap MT/MC
%.0f / %.0f ms	%.0f / %.0f ms
Test: mindst %lu min (10%% airtime)	Test: at least %lu min (10%% airtime)
Flood-advert: fra eller mindst 3 t	Flood advert: off or at least 3 h
MeshCore kører ikke	MeshCore is not running
"Må sende" er fra (MeshCore)	"Allow send" is off (MeshCore)
Advert kunne ikke sendes	Advert could not be sent
%s advert sendt (%s position)	%s advert sent (%s position)
Flood-	Flood
Lokal	Local
med	with
uden	without
Ingen åben chat	No open chat
Ryddet: %.30s	Cleared: %s
Alle kanal-chats ryddet (%d filer)	All channel chats cleared (%d files)
!%08lx	!%08lx
Kræver WiFi (Indst. > WiFi)	Needs WiFi (Settings > WiFi)
Intet SD-kort	No SD card
Henter caches fra Opencaching ...	Fetching caches from Opencaching ...
Intet SD-kort: ingen log at rydde	No SD card: no log to clear
Log ryddet: %d filer slettet	Log cleared: %d files deleted
Ukendt handling	Unknown action
pin=%s\n	pin=%s\n
pass=%s\n	pass=%s\n
"name":"%s","batt_v":%.2f,"usb":%s,"gps_fix":%s,"meshcore":%s,"meshtastic":%s	"name":"%s","batt_v":%.2f,"usb":%s,"gps_fix":%s,"meshcore":%s,"meshtastic":%s
,"lat":%.5f,"lon":%.5f	,"lat":%.5f,"lon":%.5f
fundet (GT911)	found (GT911)
IKKE fundet	NOT found
ingen	none
%skeyboards/index.txt	%skeyboards/index.txt
%s\t%s\t%d\n	%s\t%s\t%d\n
Forkert kode (godkendelse fejlede)	Wrong password (authentication failed)
Netværket blev ikke fundet	The network was not found
Routeren afviste tilknytning	The router refused association
Forbindelsen mislykkedes	The connection failed
Routeren afviste (sikkerhedsindst.)	The router refused (security settings)
Kræver 802.1X-login (ikke understøttet)	Needs 802.1X login (not supported)
Routeren er fuld (for mange enheder)	The router is full (too many devices)
Intet svar fra routeren	No reply from the router
Ingen IP-adresse fra routeren (DHCP)	No IP address from the router (DHCP)
Intet svar (tidsgrænse)	No reply (timeout)
Afvist af routeren (kode %u)	Refused by the router (code %u)
scan fejlede	scan failed
kunne ikke forbinde	could not connect
scan kunne ikke starte	scan could not start
slået fra	turned off
forbinder	connecting
forbundet	connected
Kan ikke åbne filen	Cannot open the file
Ikke en DualMesh-profil	Not a DualMesh profile
Forkert adgangskode	Wrong password
Profilen er beskadiget	The profile is damaged
Nogle filer kunne ikke skrives	Some files could not be written
Adgangskoden skal have mindst 6 tegn	The password must have at least 6 characters
Ikke nok hukommelse	Not enough memory
DUALMESH-PROFIL 1\nnavn=%s\nkortnavn=%s\n	DUALMESH-PROFIL 1\nnavn=%s\nkortnavn=%s\n
Profilen er for stor	The profile is too large
%s/%s%s.dmp	%s/%s%s.dmp
Kan ikke skrive filen	Cannot write the file
; DualMesh SD-kryptering (kode-tilstand): salt og kontroltal, INGEN noegle\nsalt=%s\ncheck=%s\nkdf=%d\n	; DualMesh SD-kryptering (kode-tilstand): salt og kontroltal, INGEN noegle\nsalt=%s\ncheck=%s\nkdf=%d\n
%s, LÅST	%s, LOCKED
Lås op først	Unlock first
Kan ikke læse %s	Cannot read %s
Ingen eFuse-nøgle	No eFuse key
Koden skal have mindst 4 tegn	The password must have at least 4 characters
Kan ikke skrive crypto.txt	Cannot write crypto.txt
Kan ikke skrive %s	Cannot write %s
anden fw	other fw
T-Deck SD-kort	T-Deck SD card
DualMesh SD-kort	DualMesh SD card
SD-kort som USB-drev	SD card as USB drive
Kortet (%lu MB) vises som et drev på PC'en. Radio og net er slukket imens.	The card (%lu MB) shows up as a drive on the PC. Radio and networks are off meanwhile.
SD-kortet svarer ikke. Klik på trackballen for at genstarte.	The SD card does not respond. Click the trackball to restart.
Afslut: skub drevet ud på PC'en, så genstarter T-Deck'en selv. Eller klik på trackballen (skub ud først, ellers kan filer gå tabt).	Finish: eject the drive on the PC, and the T-Deck restarts by itself. Or click the trackball (eject first, or files may be lost).
Læst %lu KB, skrevet %lu KB	Read %lu KB, written %lu KB
, FEJL ved læsning/skrivning	, ERROR reading/writing
%s=	%s=
NØ	NE
SØ	SE
SV	SW
NV	NW
Traditionel	Traditional
Virtuel	Virtual
Anden	Other
ukendt	unknown
mikro	micro
lille	small
mellem	regular
stor	large
anden	other
%15[^\t]\t%lu\t%c	%15[^\t]\t%lu\t%c
%s\t%lu\t%c\t\n	%s\t%lu\t%c\t\n
%Y-%m-%dT%H:%MZ	%Y-%m-%dT%H:%MZ
%s,%s,%s,""\\r\n	%s,%s,%s,""\\r\n
Kan ikke skrive på SD	Cannot write to SD
<wpt lat="%.*s" lon="%s"><name>	<wpt lat="%.*s" lon="%s"><name>
</groundspeak:container><groundspeak:difficulty>%s</groundspeak:difficulty><groundspeak:terrain>%s</groundspeak:terrain><groundspeak:encoded_hints>	</groundspeak:container><groundspeak:difficulty>%s</groundspeak:difficulty><groundspeak:terrain>%s</groundspeak:terrain><groundspeak:encoded_hints>
Ingen forbindelse til Opencaching	No connection to Opencaching
%d caches hentet fra Opencaching	%d caches fetched from Opencaching
Fejl fra Opencaching (HTTP %d)	Error from Opencaching (HTTP %d)
Mangler Opencaching-nøgle (Indst. > Kort)	Missing Opencaching key (Settings > Map)
WiFi er ikke forbundet	WiFi is not connected
{"center":"%.6f|%.6f","radius":"%d","limit":"500"}	{"center":"%.6f|%.6f","radius":"%d","limit":"500"}
dvale (strømspar)	asleep (power save)
ingen data fra modulet	no data from the module
søger satellitter	searching for satellites
bytes=%llu-	bytes=%llu-
Ingen forbindelse	No connection
Findes ikke (HTTP 404)	Not found (HTTP 404)
Fejl fra serveren (HTTP %d)	Error from the server (HTTP %d)
For lidt plads på SD (%lu MB fri, %lu MB mangler)	Not enough space on SD (%lu MB free, %lu MB needed)
Kan ikke skrive på SD-kortet	Cannot write to the SD card
Filen er for stor	The file is too large
Ingen ledig app-partition til opdateringen	No free app partition for the update
Forkert størrelse (%lu i stedet for %lu)	Wrong size (%lu instead of %lu)
OTA kunne ikke starte (%s)	OTA could not start (%s)
Fejl ved skrivning til flash	Error writing to flash
Fejl ved skrivning	Write error
Afbrudt (kan genoptages)	Cancelled (can be resumed)
Forbindelsen røg (kan genoptages)	Connection lost (can be resumed)
Forbindelsen røg	Connection lost
Kontrolsummen passer ikke: filen er slettet	Checksum mismatch: the file is deleted
Kunne ikke omdøbe filen	Could not rename the file
Hentet (%lu KB)	Downloaded (%lu KB)
Hentet	Downloaded
Kontrolsummen passer ikke: opdateringen er ikke installeret	Checksum mismatch: the update is not installed
Firmwaren er ugyldig (%s)	The firmware is invalid (%s)
Kunne ikke vælge den nye firmware (%s)	Could not select the new firmware (%s)
Firmwaren er installeret; genstarter	The firmware is installed; restarting
Ugyldig version.txt (størrelse/SHA-256)	Invalid version.txt (size/SHA-256)
%d:	%d:
)DMLANG";
static const Builtin kBuiltin[] = {
    {"en", kText_en},
};
constexpr int kBuiltinCount = 1;
}  // namespace lang
}  // namespace dm
