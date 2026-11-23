// SPDX-License-Identifier: MIT
// Test af rensningen (scrub.js). Kør i node:  node docs/rapport/scrub_test.js
// eller åbn docs/rapport/test.html i en browser (uden PC-værktøjer).
//
// 1. Korpus (corpus.js, lavet af tools/gen_scrub_vocab.py): ALLE log-formater fra firmwaren med
//    kanariefugle i hver %s. Ingen kanariefugl må overleve.
// 2. Håndskrevne linjer med rigtige typer data (navne, beskeder, positioner, IP, nøgler ...).
// 3. Linjer, der IKKE må ændres (statistik, plan, nedbrud), så rapporten stadig kan bruges.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.DMScrubTest = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  // [linje, ord der IKKE må stå i resultatet, ord der SKAL stå i resultatet]
  var CASES = [
    ['[MC] #Public (hop=2): hej Jens, ring 12345678 i morgen', ['Jens', '12345678', 'hej', 'morgen'], ['[MC] #Public (hop=2): ‹tekst, 32 tegn›']],
    ['[MC] #test (hop=1): hej', ['#test', 'hej'], ['[MC] #‹…› (hop=1): ‹tekst, 3 tegn›']],
    ['[MT] tekst til LongFast: hej (sat i koe)', ['hej'], ['[MT] tekst til LongFast: ‹tekst, 3 tegn› (sat i koe)']],
    ['[MC] DM fra "Bjarne": kommer du til mødet test', ['Bjarne', 'kommer', 'mødet', 'test'], ['[MC] DM fra "‹…›": ‹tekst']],
    ['[MT] DM fra Ole Hansen (PKI): ses kl 5 ved stationen', ['Ole', 'Hansen', 'ses', 'stationen'], ['(PKI): ‹tekst']],
    ['[MT] DM til !9a8b7c6d (PKI): Mød mig ved kirken (sat i koe)', ['9a8b7c6d', 'kirken', 'Mød'], ['!‹node', '(sat i koe)']],
    ['[MT] tekst til DMtest: hej fra Lise (FEJL)', ['DMtest', 'Lise', 'hej'], ['(FEJL)']],
    ['[MT] kanal Familie: Mor: aftensmad kl 18', ['Familie', 'Mor', 'aftensmad'], ['[MT] kanal ‹…›: ‹tekst']],
    ['[MT] kanal Hemmelig: Mor: hej', ['Hemmelig', 'Mor', 'hej'], []],
    ['[MT] Familie: Mor: aftensmad kl 18', ['Familie', 'Mor', 'aftensmad'], []],
    ['[UI] DM til Karen Jensen: vi ses (sat i koe)', ['Karen', 'Jensen', 'ses'], ['(sat i koe)']],
    ['[WIFI] forbundet til "Hjemmenet 5G", IP 192.168.1.20, -54 dBm', ['Hjemmenet', '192.168', '5G'], ['‹ip›', '-54 dBm']],
    ['[WIFI] forbinder til "Sommerhuset"', ['Sommerhuset'], ['[WIFI] forbinder til "‹…›"']],
    ['[GPS] fix 9/12 sat, HDOP 1.2, pos 55.67891,12.56789, hoejde 12 m, fart 0.0 km/t', ['55.67891', '12.56789', '55.6'], ['‹position›', 'HDOP 1.2']],
    ['[MT] position fra !9a8b7c6d: 55.12345,12.12345', ['55.12', '9a8b7c6d'], ['‹position›']],
    // Svar fra tandhjulsmenuen: navnet kan bestå af almindelige ord (som firmwaren selv bruger).
    ['[MT] NodeInfo: Hemmelig Node (HN42), PKI-nøgle', ['Hemmelig', 'Node (', 'HN42'], ['[MT] NodeInfo: ‹…› (']],
    ['[MT] Position: 55.67891, 12.56789, 14 m', ['55.67', '12.56'], ['‹position›']],
    ['{"lat":55.6761,"lon":12.5683,"alt":14}', ['55.67', '12.56'], []],
    ['[MQTT] forbinder til mqtts://bruger:hemmeligt@broker.example.com:8883 som dualmesh-f8e3a1 (rod dualmesh/f8e3a1, TLS ja)', ['bruger', 'hemmeligt', 'example', 'f8e3a1'], ['mqtts://‹vært›']],
    ['[MQTT] tx dualmesh/f8e3a1/tx/meshcore/chan/Hemmelig: sat i koe', ['f8e3a1', 'Hemmelig'], ['sat i koe']],
    ['[BLE] klar som "MeshCore-DualMesh_Anna" (PIN 123456)', ['DualMesh_Anna', '123456'], ['‹hemmelig›']],
    ['[BLE] klar som "MeshCore-Navn" (PIN skjult)', ['Navn'], ['(PIN skjult)']],
    ['[MC] kanal tilfoejet: Jagtholdet (16 byte noegle) psk=AQIDBAUGBwgJCgsMDQ4PEA==', ['Jagtholdet', 'AQIDBAUG'], ['16 byte noegle']],
    ['noegle 3q2+7wABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhs=', ['3q2+7wAB'], ['‹nøgle›']],
    ['[MC] advert NY: "Peters Repeater" (repeater) hop=1 pubkey a1b2c3d4e5f60718', ['Peters', 'a1b2c3d4e5f6'], ['‹hex›']],
    ['[BESKED] mcd_a1b2c3d4e5f6: 12 beskeder laest fra SD (3 ms), viser 12', ['a1b2c3d4e5f6'], ['mcd_‹samtale1›']],
    ['[MT] routing fra !9a8b7c6d: ACK (0) for pakke 1a2b3c4d', ['9a8b7c6d', '1a2b3c4d'], ['‹id', 'ACK (0)']],
    ['kontakt mig på jens@example.dk eller 20 30 40 50', ['jens@', 'example'], ['‹e-mail›']],
    ['[MT] nodeinfo !0a0b0c0d: Lars 🐻 (LRS)', ['Lars', '🐻', 'LRS'], []],
    ['MAC aa:bb:cc:dd:ee:ff set', ['aa:bb'], ['‹mac›']],
    ['[MC] #Public: "Hej alle sammen" sat i koe', ['Hej', 'sammen'], []],
    ['[MENU] meshcore_name: \'Gammelt Navn\' -> \'Nyt Navn\'', ['Gammelt', 'Nyt'], []],
    ['[UR] tid sat fra GPS: 2026-11-17 19:30:00 UTC (spring +3 s)', [], ['2026-11-17 19:30:00 UTC']],
    ['[MQTT] tx dualmesh/f8e3a1/tx/mt/dm/!9a8b7c6d: sat i koe', ['f8e3a1', '9a8b7c6d'], ['/tx/mt/dm/!‹node1›']],
    ['[RX MT]   12.5s fra=!9a8b7c6d til=!ffffffff id=1a2b3c4d hop=3/3 kanal=0x08 len=40 rssi=-110 snr=-9.0', ['9a8b7c6d', '1a2b3c4d'], ['fra=!‹node1› til=!ffffffff id=‹id1› hop=3/3']],
    ['ring til 22334455667 nu', ['22334455667'], ['‹tal›']],
    ['wifi pass=Sommer2026! og key: abc123', ['Sommer2026', 'abc123'], ['pass=‹hemmelig›', ': ‹hemmelig›']]
  ];

  // Linjer, der skal stå uændret efter rensningen.
  var UNCHANGED = [
    '00:10:00.123 [STAT] 600s retunes=12 watchdog=0 tidlig-afbrydelse=til irq=3456 tilstand=stabil tx=2 (lbt-optaget 0, duty-vent 0, sendetid 1166 ms)',
    '00:10:00.124 [STAT]   MeshCore   cad=5000 hits=40 preamb=12 ok=10 fejl=1 stoej=29 (0.6%) svage=0 timeout=1 peak=22 maxhul=39.5ms cad-tid=12% rx-tid=3%',
    '[PLAN] MeshCore   SF8 BW62.5 preamble 66.6 ms, CAD 2 sym 8.3 ms, tilladt hul 36.7 ms, planlagt hul 40.0 ms -> IKKE GARANTERET',
    '[RADIO] Meshtastic: EU_868 LongFast  869.5250 MHz BW 250.0 kHz SF11 CR4/5 preamble 16',
    '[CRASH] forrige koersel gik ned (PANIC): LoadProhibited i loopTask, PC 0x4200abcd, adresse 0x00000000',
    '[CRASH] Backtrace: 0x4200abcd:0x00000000 0x42001111:0x00000000',
    '[CRASH] ELF-SHA256 1a2b3c4d5e6f7a8b (samme build)',
    '[LOG] DualMesh 0.1.0 (c48036d+)',
    '[STROEM] 4.12 V, batteri',
    '[MEM] intern RAM fri 62 KB (stoerste blok 31 KB), PSRAM fri 7012 KB',
    '[LOG] logger til SD: /sd/dualmesh/logs/0012.log (opstart: NEDBRUD (panic))',
    '00:00:04.344 [LOG] logger til SD: /sd/dualmesh/logs/0000.log (opstart: kode 12)',
    '[STAT] 600s retunes=12345678 watchdog=0 tidlig-afbrydelse=til irq=98765432 tilstand=stabil',
    '[TX] afsendelse faerdig (tag 1234567), i alt 3, samlet sendetid 2100 ms',
    '[UI] tastaturlayouts: da(indbygget) latin(indbygget); touch fundet (GT911)',
    '[PROFIL] selvtest: eksport OK, laes OK, forkert kode afvist OK',
    "[MENU] trackball_sensitivity: '5' -> '7'"
  ];

  function run(DMScrub, vocab, corpus) {
    var fails = [];
    var passed = 0;
    var sc = DMScrub.makeScrubber(vocab);
    // 1. Korpus med kanariefugle.
    (corpus || []).forEach(function (c) {
      var out = sc.scrubLine(c.line);
      if (/Zqxv|Bjørkvæld/.test(out)) fails.push('KORPUS ' + c.src + ': ' + c.line + '\n   -> ' + out);
      else passed++;
    });
    // 2. Håndskrevne linjer.
    CASES.forEach(function (c) {
      var out = DMScrub.makeScrubber(vocab).scrubLine(c[0]);
      var bad = c[1].filter(function (w) { return out.indexOf(w) >= 0; });
      var miss = c[2].filter(function (w) { return out.indexOf(w) < 0; });
      if (bad.length || miss.length)
        fails.push('LINJE: ' + c[0] + '\n   -> ' + out +
                   (bad.length ? '\n   slap igennem: ' + bad.join(', ') : '') +
                   (miss.length ? '\n   mangler: ' + miss.join(', ') : ''));
      else passed++;
    });
    // 3. Uændrede linjer.
    UNCHANGED.forEach(function (l) {
      var out = DMScrub.makeScrubber(vocab).scrubLine(l);
      if (out !== l) fails.push('ÆNDRET: ' + l + '\n   -> ' + out);
      else passed++;
    });
    // 4. Samme node får samme alias i hele rapporten.
    var s2 = DMScrub.makeScrubber(vocab);
    var a = s2.scrub('[MT] DM til !9a8b7c6d: x (sat i koe)\n[MT] routing fra !9a8b7c6d: ACK\n[MT] routing fra !11223344: ACK');
    if (a.split('!‹node1›').length !== 3 || a.indexOf('!‹node2›') < 0) fails.push('ALIAS: ' + a);
    else passed++;
    return { passed: passed, fails: fails };
  }

  if (typeof require !== 'undefined' && typeof module !== 'undefined' && require.main === module) {
    var path = require('path');
    var dir = __dirname;
    var res = run(require(path.join(dir, 'scrub.js')), require(path.join(dir, 'vocab.js')),
                  require(path.join(dir, 'corpus.js')));
    res.fails.forEach(function (f) { console.log('FEJL ' + f); });
    console.log(res.passed + ' bestået, ' + res.fails.length + ' fejlet');
    process.exit(res.fails.length ? 1 : 0);
  }
  return { run: run, CASES: CASES, UNCHANGED: UNCHANGED };
});
