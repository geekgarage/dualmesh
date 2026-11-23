# SPDX-License-Identifier: MIT
"""Genererer src/ui/font8x16.h med europæiske bogstaver med accent.

Grundbogstaverne (ASCII + æøåÆØÅ) er tegnet fra DejaVu Sans Mono og ligger i
font8x16.h. Bogstaver med accent sammensættes her af et grundbogstav og et
accenttegn tegnet i pixels; nogle få tegn (ß, €, ¿ ...) er tegnet i hånden.
Ingen eksterne filer eller downloads.  Kør:  python tools/gen_font.py
"""
import os
import re
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, 'src', 'ui', 'font8x16.h')


def rows(*pattern):
    return [int(p.replace('.', '0').replace('#', '1'), 2) for p in pattern]


# Accenter: (rækker, start-række for små bogstaver, start-række for store)
MARK = {
    'acute':     (rows('....##..', '...##...'), 2, 0),
    'grave':     (rows('..##....', '...##...'), 2, 0),
    'circ':      (rows('...##...', '..#..#..'), 2, 0),
    'tilde':     (rows('..##.#..', '.#.##...'), 2, 0),
    'diaer':     (rows('..#..#..'), 3, 1),
    'ring':      (rows('...##...', '..#..#..', '...##...'), 1, 0),
    'caron':     (rows('..#..#..', '...##...'), 2, 0),
    'macron':    (rows('..####..'), 3, 1),
    'breve':     (rows('..#..#..', '...##...'), 2, 0),
    'dot':       (rows('...##...'), 3, 1),
    'dblacute':  (rows('...#..#.', '..#..#..'), 2, 0),
}
BELOW = {  # under grundlinjen (række 12-13)
    'cedilla': rows('....#...', '...##...'),
    'ogonek':  rows('....#...', '.....##.'),
    'comma':   rows('...##...', '..##....'),
}

COMBINING = {
    '́': 'acute', '̀': 'grave', '̂': 'circ', '̃': 'tilde',
    '̈': 'diaer', '̊': 'ring', '̌': 'caron', '̄': 'macron',
    '̆': 'breve', '̇': 'dot', '̋': 'dblacute', '̧': 'cedilla',
    '̨': 'ogonek', '̦': 'comma',
}

# Tegn vi vil have med (Latin-1 + de vigtigste i Latin Extended-A + lidt tegnsætning).
WANTED = (
    'ÀÁÂÃÄÈÉÊËÌÍÎÏÑÒÓÔÕÖÙÚÛÜÝÇ'
    'àáâãäèéêëìíîïñòóôõöùúûüýÿç'
    'ĀāĂăĄąĆćĈĉĊċČčĎďĒēĔĕĖėĘęĚěĜĝĞğĠġĢģĤĥĨĩĪīĬĭĮįİ'
    'ĴĵĶķĹĺĻļĽľŃńŅņŇňŌōŎŏŐőŔŕŖŗŘřŚśŜŝŞşŠšŢţŤťŨũŪūŬŭŮůŰűŲųŴŵŶŷŸŹźŻżŽžȘșȚț'
)

# Håndtegnede tegn (16 rækker; tomme rækker fyldes med 0).
def glyph(top, *pattern):
    g = [0] * 16
    for i, p in enumerate(pattern):
        g[top + i] = rows(p)[0]
    return g


HAND = {
    'ß': glyph(3, '..###...', '.#...#..', '.#...#..', '.#..#...', '.#.##...', '.#...#..',
               '.#....#.', '.#....#.', '.#.###..'),
    '€': glyph(3, '...####.', '..#.....', '.#......', '#####...', '.#......', '#####...',
               '.#......', '..#.....', '...####.'),
    '¿': glyph(5, '...##...', '........', '...##...', '...##...', '..##....', '.##.....',
               '.##..##.', '..####..'),
    '¡': glyph(5, '...##...', '........', '...##...', '...##...', '...##...', '...##...',
               '...##...', '...##...'),
    '°': glyph(3, '..###...', '.#...#..', '.#...#..', '..###...'),
    '«': glyph(6, '..#..#..', '.#..#...', '#..#....', '.#..#...', '..#..#..'),
    '»': glyph(6, '#..#....', '.#..#...', '..#..#..', '.#..#...', '#..#....'),
    'ı': glyph(5, '..###...', '...##...', '...##...', '...##...', '...##...', '...##...',
               '.######.'),
    'ł': glyph(2, '..###...', '...##...', '...##...', '...##.#.', '...####.', '..###...',
               '.#.##...', '...##...', '...##...', '.######.'),
    'Ł': glyph(3, '.##.....', '.##.....', '.##.....', '.##.#...', '.####...', '###.....',
               '.##.....', '.##.....', '.######.'),
    'đ': glyph(2, '.....##.', '...#####', '.....##.', '..#####.', '.##..##.', '.#...##.',
               '.#...##.', '.#...##.', '.##..##.', '..###.#.'),
    'Đ': glyph(3, '.####...', '.##..#..', '.##...#.', '####..#.', '.##...#.', '.##...#.',
               '.##...#.', '.##..#..', '.####...'),
    'Ð': glyph(3, '.####...', '.##..#..', '.##...#.', '####..#.', '.##...#.', '.##...#.',
               '.##...#.', '.##..#..', '.####...'),
    'ð': glyph(2, '..#.#...', '...#....', '..#.#...', '.....#..', '..####..', '.##..##.',
               '.#....#.', '.#....#.', '.##..##.', '..####..'),
    'þ': glyph(3, '.##.....', '.##.....', '.#####..', '.##..##.', '.##...#.', '.##...#.',
               '.##..##.', '.#####..', '.##.....', '.##.....', '.##.....'),
    'Þ': glyph(3, '.##.....', '.#####..', '.##..##.', '.##...#.', '.##..##.', '.#####..',
               '.##.....', '.##.....', '.##.....'),
    'œ': glyph(5, '.##.##..', '#..#..#.', '#..####.', '#..#....', '#..#....', '#..#..#.',
               '.##.##..'),
    'Œ': glyph(3, '.#######', '#..#....', '#..#....', '#..####.', '#..#....', '#..#....',
               '#..#....', '#..#....', '.#######'),
}


def load_base():
    t = open(FONT, encoding='utf-8').read()
    data = re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){16})\},\s*//\s*(.*)', t)
    cps = re.search(r'kCodepoints\[kCount\]\s*=\s*\{([^}]*)\}', t)
    glyphs = [[int(x, 16) for x in b.split(',') if x] for b, _ in data]
    if cps:  # allerede genereret: brug tabellen
        codes = [int(x, 0) for x in cps.group(1).replace('\n', ' ').split(',') if x.strip()]
    else:    # oprindelig fil: ASCII 32..126 + æøåÆØÅ
        codes = list(range(32, 127)) + [ord(c) for c in 'æøåÆØÅ']
    return dict(zip(codes, glyphs))


def compose(base, ch):
    d = unicodedata.normalize('NFD', ch)
    if len(d) != 2 or d[1] not in COMBINING:
        return None
    b = ord(d[0])
    if d[0] == 'i' and COMBINING[d[1]] in MARK:   # i mister prikken under accenten
        src = list(base[ord('i')])
        for r in range(0, 5):
            src[r] = 0
    elif b in base:
        src = list(base[b])
    else:
        return None
    kind = COMBINING[d[1]]
    upper = d[0].isupper() or d[0] in 'bdhklt'  # høje bogstaver: accent øverst
    if kind in BELOW:
        for i, r in enumerate(BELOW[kind]):
            src[12 + i] |= r
        return src
    marks, low, high = MARK[kind]
    top = high if upper else low
    for i, r in enumerate(marks):
        src[top + i] |= r
    return src


def main():
    base = load_base()
    out = dict(base)
    missing = []
    for ch in WANTED:
        g = compose(base, ch)
        if g:
            out[ord(ch)] = g
        else:
            missing.append(ch)
    for ch, g in HAND.items():
        out[ord(ch)] = g
    codes = sorted(out)
    with open(FONT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('// SPDX-License-Identifier: MIT\n')
        f.write('// 8x16 bitmap font. Grundbogstaver tegnet fra DejaVu Sans Mono (Bitstream Vera /\n')
        f.write('// DejaVu license, free to use); accenttegn sammensat af tools/gen_font.py.\n')
        f.write('// kCodepoints er sorteret (binær søgning); kGlyphs[i] hører til kCodepoints[i].\n')
        f.write('// GENERERET – ret i tools/gen_font.py og kør det igen.\n')
        f.write('#pragma once\n#include <cstdint>\nnamespace dm { namespace font {\n')
        f.write(f'constexpr int W = 8, H = 16, kCount = {len(codes)};\n')
        f.write('constexpr uint8_t kGlyphs[kCount][H] = {\n')
        for c in codes:
            g = ','.join(f'0x{v:02X}' for v in out[c])
            name = chr(c) if c not in (0x5C,) else 'backslash'
            f.write(f'  {{{g}}},  // U+{c:04X} {name}\n')
        f.write('};\n')
        f.write('constexpr uint16_t kCodepoints[kCount] = {\n')
        for i in range(0, len(codes), 12):
            f.write('  ' + ', '.join(f'0x{c:04X}' for c in codes[i:i + 12]) + ',\n')
        f.write('};\n} }  // namespace dm::font\n')
    print(f'{len(codes)} tegn skrevet til src/ui/font8x16.h' +
          (f'; mangler grundbogstav for: {"".join(missing)}' if missing else ''))


if __name__ == '__main__':
    main()
