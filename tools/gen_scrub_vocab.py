# SPDX-License-Identifier: MIT
"""Laver fejlrapportens ordforråd og testkorpus ud fra firmwarens egne tekster.

Brug:  python tools/gen_scrub_vocab.py
Skriver docs/rapport/vocab.js og docs/rapport/corpus.js. Kør det igen, når der kommer nye
log-linjer (ellers bliver nye ord renset væk – det er den sikre side).

Princip (docs/rapport/scrub.js): i en renset log overlever kun ord, som firmwaren selv skriver
(alle strenge i src/ og lib/dualmesh/src/), plus tal, tider og hex-adresser. Navne, kanaler,
SSID'er, beskeder osv. kommer fra brugeren og radioen, ikke fra kildekoden, og bliver til ‹…›.

corpus.js: hver DLOG.printf/println-formatstreng udfyldt med "kanariefugle" (ord, der ikke
findes i kildekoden) i alle %s. Testen (docs/rapport/scrub_test.js) kræver, at ingen af dem
overlever rensningen.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIRS = ['src', os.path.join('lib', 'dualmesh', 'src')]
# Genererede filer med fonte, billeder og tastaturtabeller (ingen logtekst).
SKIP = ('src/ui/lv/fonts/', 'src/ui/logo/', 'src/ui/font8x16.h', 'src/ui/keyboards_builtin.h')
OUT_DIR = os.path.join(ROOT, 'docs', 'rapport')

# Ord fra ESP-IDF/FreeRTOS, der kan stå i loggen uden at være i vores kilde (opgavenavne m.m.).
EXTRA = '''loopTask IDLE IDLE0 IDLE1 Tmr Svc esp_timer ipc0 ipc1 main sys_evt tiT wifi
BTC_TASK BTU_TASK btController hciT mqtt_task arduino_events async_tcp radio
Guru Meditation Error Core panic panic'ed Backtrace abort Abort was called at PC on core
Debug exception reason Stack canary watchpoint triggered Interrupt wdt timeout CPU
Task watchdog got triggered The following tasks did not reset the in time Rebooting
ELF file SHA256 Brownout detector'''.split()

# Navne, der står som eksempler i kildekoden, men aldrig må slippe igennem.
DENY = {'geekgarage', 'dualmesh_anna', 'geek', 'dmtest'}
# Lange literaler er hjælpe- og skærmtekster (med eksempler som navne); log-ord kommer fra
# DLOG-formaterne og korte statusværdier ("sat i koe", "TIL", "PKI" ...).
MAX_LIT = 40

TOKEN = re.compile(r'[^\W]+', re.UNICODE)  # bogstaver, tal og _ (samme som scrub.js)
FMT = re.compile(r'%[-+ #0]*(\*|\d+)?(\.(\*|\d+))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfgGcspn%])')


def lex(text):
    """Giver (str-literaler, kode uden kommentarer) for C/C++-tekst."""
    lits = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == '"':
            j = i + 1
            buf = []
            while j < n and text[j] != '"':
                if text[j] == '\\' and j + 1 < n:
                    buf.append(text[j:j + 2])
                    j += 2
                else:
                    buf.append(text[j])
                    j += 1
            lits.append((i, ''.join(buf)))
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            i = j + 1
        else:
            i += 1
    return lits


def unescape(s):
    return (s.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"')
             .replace("\\'", "'").replace('\\\\', '\\'))


def log_formats(text):
    """Formatstrengene i DLOG.printf/println(...) (sammensatte literaler slås sammen)."""
    out = []
    for m in re.finditer(r'\b(?:DLOG|log)\s*\.\s*(printf|println|print)\s*\(', text):
        i = m.end()
        parts = []
        while True:
            while i < len(text) and text[i] in ' \t\r\n':
                i += 1
            if i < len(text) and text[i] == '"':
                j = i + 1
                while j < len(text) and text[j] != '"':
                    j += 2 if text[j] == '\\' else 1
                parts.append(text[i + 1:j])
                i = j + 1
            else:
                break
        if parts:
            out.append((m.group(1), unescape(''.join(parts))))  # DLOG eller Print& log
    return out


def tokens(s):
    s = FMT.sub(' ', s)
    return TOKEN.findall(s)


def fill(fmt, k):
    """Udfylder en formatstreng: %s -> kanariefugle, tal -> tal."""
    n = [0]

    def rep(m):
        conv = m.group(5)
        n[0] += 1
        if conv == '%':
            return '%'
        if conv == 's':
            # To ord, et med æøå og et med tal: må ikke overleve rensningen.
            return f'Zqxv{k}x{n[0]} Bjørkvæld{k}'
        if conv == 'c':
            return '\n'
        if conv in 'xX':
            return 'deadbeef'
        if conv in 'eEfgG':
            return '3.25'
        if conv == 'p':
            return '0x3fc8a000'
        return '42'
    return FMT.sub(rep, fmt)


def main():
    vocab = set(EXTRA)
    formats = []
    for d in SRC_DIRS:
        for dirpath, _, files in os.walk(os.path.join(ROOT, d)):
            for f in sorted(files):
                if not f.endswith(('.cpp', '.h', '.c')):
                    continue
                path = os.path.join(dirpath, f)
                rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
                if rel.startswith(SKIP):
                    continue
                text = open(path, encoding='utf-8', errors='replace').read()
                for _, lit in lex(text):
                    lit = unescape(lit)
                    if len(lit) <= MAX_LIT:
                        vocab.update(tokens(lit))
                for kind, fmt in log_formats(text):
                    formats.append((rel, kind, fmt))
                    vocab.update(tokens(fmt))
    # Store/små bogstaver er ligegyldige; rene tal behøver ikke stå i ordforrådet.
    words = sorted({w.lower() for w in vocab if not w.isdigit()} - DENY)
    os.makedirs(OUT_DIR, exist_ok=True)
    head = ('// GENERERET af tools/gen_scrub_vocab.py - ret ikke i hånden.\n'
            '// SPDX-License-Identifier: MIT\n')
    with open(os.path.join(OUT_DIR, 'vocab.js'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(head)
        f.write('// Ord, som firmwaren selv skriver (fra src/ og lib/dualmesh/src/). Kun disse\n'
                '// overlever rensningen af en log.\n')
        f.write('(function (root) {\n  var w = ')
        f.write(json.dumps(' '.join(words), ensure_ascii=False))
        f.write(".split(' ');\n"
                "  if (typeof module !== 'undefined' && module.exports) module.exports = w;\n"
                '  else root.DM_VOCAB = w;\n'
                "})(typeof self !== 'undefined' ? self : this);\n")
    lines = []
    for k, (rel, kind, fmt) in enumerate(formats):
        text = fill(fmt, k)
        for ln in text.split('\n'):
            if 'Zqxv' in ln or 'Bjørkvæld' in ln:
                lines.append({'src': rel, 'line': '00:01:02.345 ' + ln})
    with open(os.path.join(OUT_DIR, 'corpus.js'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(head)
        f.write('// Alle DLOG-formatstrenge med kanariefugle i %s (se scrub_test.js).\n')
        f.write('(function (root) {\n  var c = ')
        f.write(json.dumps(lines, ensure_ascii=False, indent=0))
        f.write(";\n  if (typeof module !== 'undefined' && module.exports) module.exports = c;\n"
                '  else root.DM_CORPUS = c;\n'
                "})(typeof self !== 'undefined' ? self : this);\n")
    print(f'{len(words)} ord i ordforrådet, {len(formats)} log-formater, '
          f'{len(lines)} testlinjer med kanariefugle')
    return 0


if __name__ == '__main__':
    sys.exit(main())
