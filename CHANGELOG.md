# Changelog

What is added, changed and fixed in each DualMesh firmware release. The newest version is at the
top. A release is only built when its version has a section here (`tools/release.py check`).

## [0.1.5] - unreleased

### Added
- Map packs for the whole world: *Get map packs* now opens a catalog in layers
  (continent > country > packs). New countries appear as they are made, starting with the
  countries closest to Denmark.
- Partially downloaded map packs can be deleted: click a pack marked "% downloaded" in
  *Get map packs* and choose *Resume download* or *Delete from SD card*. *Map packs* on the
  map also lists partial files, so you can delete those that are no longer in the catalog.

### Changed
- The map catalog is read from `maps/catalog.txt`. Firmware before 0.1.5 keeps using the old
  single list (`maps/index.txt`), which is no longer extended.

## [0.1.4] - 2026-11-23

### Fixed
- Map pack names: a pack could be shown with escape codes instead of letters (for example
  "FÃ¦r..."). The name now comes from the file name, so it matches the catalog.

## [0.1.3] - 2026-11-23

### Fixed
- Boot loop when Bluetooth was turned on with a long node name (more than 17 characters). The
  Bluetooth name is now shortened to fit the advertisement.

### Added
- The release includes `dualmesh-licenses.zip` with the license texts of everything built into
  the firmware.

## [0.1.2] - 2026-11-23

### Added
- Runs under bmorcelli's T-Deck Launcher, which is now detected. *Update firmware* then saves the
  new firmware on the SD card (checked with SHA-256), shows how to install it from the launcher and
  has a *Restart now* button.

### Changed
- The confirmation words follow the language setting (RESET, ERASE ALL, USE EFUSE); the Danish
  words still work.
- *Reset DualMesh* keeps your map packs.
- The *SD as USB drive* screen follows the language setting.

### Fixed
- Values on the right of sub-screens were cut off (for example the satellite line in GPS status).
- Numbers on the *SD as USB drive* screen were shown in Danish.

## [0.1.1] - 2026-11-23

### Fixed
- Downloads from GitHub (firmware updates, keyboard layouts, languages, map packs) failed because
  of the certificate chain and too little internal RAM. 0.1.0 cannot update itself: install 0.1.1
  or newer with the flash page.

## [0.1.0] - 2026-11-23

### Added
- First release: MeshCore and Meshtastic on one SX1262 radio with a time-sharing scheduler,
  channels, direct messages, contacts, maps with GPS, the MeshCore and Meshtastic apps over
  Bluetooth, MQTT bridge, SOS, encrypted storage on the SD card, firmware update over WiFi and
  downloads from GitHub.
