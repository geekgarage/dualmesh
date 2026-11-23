# SPDX-License-Identifier: MIT
"""Bygger keyboards/*.kbd ind i firmwaren (src/ui/keyboards_builtin.h).

Tjekker samtidig, at hvert tegn findes i skærmens font (src/ui/font8x16.h).
De samme filer ligger i repoet, så de senere kan hentes fra GitHub.
Kør:  python tools/gen_keyboards.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KBD = os.path.join(ROOT, 'keyboards')
OUT = os.path.join(ROOT, 'src', 'ui', 'keyboards_builtin.h')
FONT = os.path.join(ROOT, 'src', 'ui', 'font8x16.h')


def font_codepoints():
    t = open(FONT, encoding='utf-8').read()
    m = re.search(r'kCodepoints\[kCount\]\s*=\s*\{([^}]*)\}', t)
    return {int(x, 0) for x in m.group(1).replace('\n', ' ').split(',') if x.strip()}


def main():
    cps = font_codepoints()
    index = [l.split()[0] for l in open(os.path.join(KBD, 'index.txt'), encoding='utf-8')
             if l.strip() and not l.startswith('#')]
    bad = []
    blobs = []
    for kid in index:
        text = open(os.path.join(KBD, kid + '.kbd'), encoding='utf-8').read()
        for n, line in enumerate(text.splitlines(), 1):
            if '=' in line and not line.startswith('#') and len(line.split('=', 1)[0]) == 1:
                for ch in line.split('=', 1)[1].split():
                    for c in ch:
                        if ord(c) not in cps:
                            bad.append(f'{kid}.kbd:{n}: {c} (U+{ord(c):04X}) mangler i fonten')
        blobs.append((kid, text))
    if bad:
        print('\n'.join(bad))
        sys.exit(1)
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('// SPDX-License-Identifier: MIT\n// GENERERET af tools/gen_keyboards.py fra keyboards/*.kbd – ret ikke her.\n')
        f.write('#pragma once\nnamespace dm { namespace kbd {\n')
        f.write('struct Builtin { const char* id; const char* text; };\n')
        f.write('constexpr Builtin kBuiltin[] = {\n')
        for kid, text in blobs:
            esc = text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n"\n    "')
            f.write(f'  {{"{kid}",\n    "{esc}"}},\n')
        f.write('};\nconstexpr int kBuiltinCount = sizeof(kBuiltin) / sizeof(kBuiltin[0]);\n} }\n')
    print(f'{len(blobs)} layouts bygget ind: {", ".join(k for k, _ in blobs)}')


if __name__ == '__main__':
    main()
