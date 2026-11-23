// SPDX-License-Identifier: MIT
// DualMesh – rensning af logfiler før de deles (fejlrapport-siden, docs/rapport/).
//
// Princip: i en renset linje overlever KUN ord, som firmwaren selv skriver (vocab.js, lavet af
// tools/gen_scrub_vocab.py ud fra kildekoden), samt tal, tider og adresser. Alt, der kommer fra
// brugeren eller radioen – navne, kanaler, SSID'er, beskeder, nøgler – står ikke i kildekoden og
// bliver til ‹…›. Først fjernes ting, der ellers ville slippe igennem som "tal": positioner,
// IP-/MAC-adresser, lange tal og nøgler. Beskedtekster fjernes helt (også almindelige ord).
// Node-id'er og pakke-id'er erstattes af faste aliaser (!‹node1›), så man stadig kan se, hvem
// der svarer hvem, uden at de rigtige id'er står i rapporten.
//
// Alt sker lokalt i browseren; intet sendes nogen steder hen.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.DMScrub = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  var RED = '‹…›';
  var STATUS = '(?:sat i koe|FEJL|afsendelse er FRA|sendt|FEJLEDE)';
  // Linjer med beskedtekst. Grupper: (før)(navn)(midt)(tekst)(efter). Teksten fjernes helt (også
  // ord, der står i ordforrådet); navnet også, medmindre det er en offentlig standardkanal.
  var MESSAGE_RULES = [
    /^(\[MC\] #)(.*?)( \(hop=\d+\): )(.*)()$/,
    /^(\[MC\] DM fra ")(.*?)(": )(.*)()$/,
    /^(\[MT\] DM fra )(.*?)( \((?:PKI|kanalnoegle)\): )(.*)()$/,
    new RegExp('^(\\[MT\\] DM til )(.*?)( \\((?:PKI|kanalnoegle[^)]*)\\): )(.*)( \\(' + STATUS + '\\))$'),
    new RegExp('^(\\[MT\\] tekst til )(.*?)(: )(.*)( \\(' + STATUS + '\\))$'),
    /^(\[MT\] kanal )(.*?)(: )(.*)()$/,
    new RegExp('^(\\[UI\\] (?:DM|kanalbesked) til )(.*?)(: )(.*)( \\(' + STATUS + '\\))$'),
    /^(\[MT\] traceroute (?:fra|til) )(.*?)(: )(.*)()$/,
    // Svar på "Bed om NodeInfo" fra tandhjulsmenuen: langt og kort navn.
    /^(\[MT\] NodeInfo: )(.*?)( \()(.*?)(\)(?:, PKI-nøgle)?)$/
  ];
  // Offentlige standardkanaler (MeshCore #Public og Meshtastics presetnavne) må stå.
  var PUBLIC_CHANNELS = ['public', 'longfast', 'longslow', 'longmoderate', 'longturbo', 'mediumfast',
                         'mediumslow', 'shortfast', 'shortslow', 'shortturbo', 'verylongslow',
                         'narrowfast', 'narrowslow', 'litefast', 'liteslow'];

  function makeScrubber(vocabList) {
    var vocab = new Set();
    (vocabList || []).forEach(function (w) { vocab.add(String(w).toLowerCase()); });
    var aliases = { node: new Map(), conv: new Map(), id: new Map() };
    var stats = {};

    function count(k) { stats[k] = (stats[k] || 0) + 1; }
    function alias(kind, key, prefix) {
      var m = aliases[kind];
      key = key.toLowerCase();
      if (!m.has(key)) m.set(key, prefix + (m.size + 1));
      return m.get(key);
    }

    function scrubLine(line) {
      // Pladsholdere (tegn i Unicodes private område) beskytter allerede rensede dele mod
      // de næste trin; de er hverken bogstaver eller tal, så ordtjekket springer dem over.
      var held = [];
      function hold(s) {
        held.push(s);
        return '' + String.fromCharCode(0xE100 + held.length - 1) + '';
      }
      function mark(kind, label) { count(kind); return hold('‹' + label + '›'); }

      line = String(line).replace(/‹/g, '<').replace(/›/g, '>');
      var m = /^(\d{2}:\d{2}:\d{2}\.\d{3} )?([\s\S]*)$/.exec(line);
      var stamp = m[1] || '';
      var s = m[2];

      // 1. Beskedtekster (og navnet på kanalen/personen).
      for (var i = 0; i < MESSAGE_RULES.length; i++) {
        var r = MESSAGE_RULES[i].exec(s);
        if (r) {
          var name = r[2];
          if (PUBLIC_CHANNELS.indexOf(name.toLowerCase()) < 0 && !/^![0-9a-fA-F]{8}$/.test(name))
            name = mark('navne', '…');
          s = r[1] + name + r[3] + mark('beskeder', 'tekst, ' + Array.from(r[4]).length + ' tegn') + r[5];
          break;
        }
      }
      // Ældre logs: "[MT] <kanal>: <tekst>" (før "kanal" kom med i linjen).
      var old = /^(\[MT\] )([^\s:]{1,12})(: )(.+)$/.exec(s);
      if (old && !vocab.has(old[2].toLowerCase())) {
        s = old[1] + mark('navne', 'kanal') + old[3] +
            mark('beskeder', 'tekst, ' + Array.from(old[4]).length + ' tegn');
      }

      // 2. Hemmeligheder efter et nøgleord (ældre logs skrev fx Bluetooth-PIN'en).
      s = s.replace(/\b(PIN)(\s*[:=]?\s*)\d{4,8}\b/g,
                    function (_, k, sep) { return k + sep + mark('hemmeligheder', 'hemmelig'); });
      s = s.replace(/\b(password|passwd|pass|pwd|psk|key|secret)(\s*[:=]\s*)([^\s,()]+)/gi,
                    function (_, k, sep) { return k + sep + mark('hemmeligheder', 'hemmelig'); });
      // 3. Positioner: "lat": 55.1, lat=55.1 og talpar med mange decimaler (55.67891,12.56789).
      s = s.replace(/\b(lat|lon|lng|latitude|longitude|alt|altitude)("?\s*[:=]\s*)-?\d+(?:\.\d+)?/gi,
                    function (_, k, sep) { return k + sep + mark('positioner', 'position'); });
      s = s.replace(/-?\d{1,3}\.\d{3,}\s*,\s*-?\d{1,3}\.\d{3,}/g, function () { return mark('positioner', 'position'); });
      s = s.replace(/-?\b\d{1,3}\.\d{5,}\b/g, function () { return mark('positioner', 'position'); });
      // 4. Adresser.
      s = s.replace(/\b([a-z][a-z0-9+.-]*):\/\/[^\s"'<>]+/gi,
                    function (_, scheme) { return scheme + '://' + mark('adresser', 'vært'); });
      s = s.replace(/[\w.+-]+@[\w-]+\.[\w.-]+/g, function () { return mark('adresser', 'e-mail'); });
      s = s.replace(/\b(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b/g, function () { return mark('adresser', 'mac'); });
      s = s.replace(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g, function () { return mark('adresser', 'ip'); });
      // 5. Citerede værdier (navne, SSID'er, tekster): "..." og '...'.
      // Rene tal (fx indstillingsværdier '15') må blive.
      s = s.replace(/"([^"]*)"/g, function (all, v) {
        return !v || /^[\d\s.,:+-]+$/.test(v) ? all : '"' + mark('navne', '…') + '"';
      });
      s = s.replace(/(^|[\s(=:])'([^'\n]{1,60})'(?=$|[\s),.:;])/g, function (all, pre, v) {
        return /^[\d\s.,:+-]+$/.test(v) ? all : pre + "'" + mark('navne', '…') + "'";
      });
      // MQTT-emner: segmentet efter /chan/ eller /dm/ er et kanal- eller personnavn.
      s = s.replace(/(\/(?:chan|dm)\/)([^\s\/:]+)/g, function (all, pre, seg) {
        if (/^![0-9a-fA-F]{8}$/.test(seg) || PUBLIC_CHANNELS.indexOf(seg.toLowerCase()) >= 0) return all;
        return pre + mark('navne', '…');
      });
      // ELF-hash fra et nedbrud (skal kunne matches med firmware.elf) og git-revision.
      s = s.replace(/\b(ELF-SHA256|elf|ELF)([ :]+)([0-9a-f]{8,64})\b/g, function (all) { return hold(all); });
      // 6. Nøgler og id'er.
      s = s.replace(/(?:^|(?<=[^A-Za-z0-9+/]))(?=[A-Za-z0-9+/]*[0-9])(?=[A-Za-z0-9+/]*[A-Z])(?=[A-Za-z0-9+/]*[a-z])[A-Za-z0-9+/]{16,}={0,2}/g,
                    function (k) {
                      // Stier og MQTT-emner (flere /) er ikke nøgler; de renses af ordtjekket.
                      return (k.match(/\//g) || []).length >= 2 ? k : mark('nøgler', 'nøgle');
                    });
      s = s.replace(/!([0-9a-fA-F]{8})\b/g, function (all, id) {
        if (id.toLowerCase() === 'ffffffff') return all;  // broadcast
        count('node-id');
        return '!' + hold('‹' + alias('node', id, 'node') + '›');
      });
      s = s.replace(/\b(mc[cd]|mt[cd])_([0-9a-fA-F]+)\b/g, function (_, p, id) {
        count('samtale-id');
        return hold(p + '_‹' + alias('conv', id, 'samtale') + '›');
      });
      s = s.replace(/\b0x([0-9a-fA-F]{9,})\b/g, function () { return mark('nøgler', 'hex'); });
      s = s.replace(/\b(?=[0-9a-fA-F]*[a-fA-F])[0-9a-fA-F]{10,}\b/g, function () { return mark('nøgler', 'hex'); });
      s = s.replace(/\b(?=[0-9a-f]*[a-f])(?=[0-9a-f]*[0-9])[0-9a-f]{8}\b/g, function (id) {
        count('pakke-id');
        return hold('‹' + alias('id', id, 'id') + '›');
      });
      // 7. Lange tal (telefonnumre o.l.); frekvenser og tider med enhed må blive.
      // Tællere (irq=12345678, tag 1234567) er ufarlige og bliver.
      s = s.replace(/(=|\btag |^|[^=\w])(\d{7,})\b(?!\s*(?:Hz|kHz|MHz|ms|us|µs|B|byte|bytes|KB|MB)\b)/g,
                    function (all, pre) {
                      return pre === '=' || pre === 'tag ' ? all : pre + mark('tal', 'tal');
                    });

      // 8. Ordtjek: kun firmwarens egne ord overlever.
      var out = '';
      var last = 0;
      var pendingRed = false;
      var tok = /[\p{L}\p{N}_]+|\p{Extended_Pictographic}/gu;
      var t;
      while ((t = tok.exec(s)) !== null) {
        var w = t[0];
        var gap = s.slice(last, t.index);
        last = t.index + w.length;
        if (keep(w)) {
          out += gap + w;
          pendingRed = false;
        } else if (pendingRed && /^[\s\-_.]*$/.test(gap)) {
          // flere ukendte ord i træk bliver til ét ‹…›
        } else {
          count('ord');
          out += gap + RED;
          pendingRed = true;
        }
      }
      out += s.slice(last);
      out = out.replace(/([-])/g, function (_, c) {
        return held[c.charCodeAt(0) - 0xE100];
      });
      return stamp + out;
    }

    function keep(w) {
      if (/^\d+$/.test(w)) return true;
      if (/^0x[0-9a-fA-F]{1,8}$/.test(w)) return true;  // adresser og registre
      if (w.toLowerCase() === 'ffffffff') return true;   // broadcast (!ffffffff)
      if (/^(?=[0-9a-f]*\d)[0-9a-f]{7}$/.test(w)) return true;  // git-revision (DM_GIT_REV)
      if (vocab.has(w.toLowerCase())) return true;      // fx GT911, SX1262
      var core = w.replace(/^[\d_]+|[\d_]+$/g, '');     // SF8, 62kHz, p98, EU_868 ...
      if (!core) return true;
      if (/^[A-Za-z]$/.test(core)) return true;
      return vocab.has(core.toLowerCase());
    }

    function scrub(text) {
      var lines = String(text).replace(/\r\n?/g, '\n').split('\n');
      return lines.map(scrubLine).join('\n');
    }

    return { scrub: scrub, scrubLine: scrubLine, stats: stats };
  }

  return { makeScrubber: makeScrubber, RED: RED };
});
