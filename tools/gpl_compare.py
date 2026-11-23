# SPDX-License-Identifier: MIT
# Sammenligner DualMesh' Meshtastic-kode med Meshtastic-firmwaren (GPL) og protobufs.
# Brug: python -I gplcmp.py <mtfw> <mtpb> <dualmesh> > rapport.txt
import os, re, sys, hashlib
from collections import defaultdict

MTFW, MTPB, DM = sys.argv[1:4]
OURS = ['src/net/mt_node.cpp', 'src/net/mt_node.h', 'src/net/mt_tx.cpp', 'src/net/mt_tx.h',
        'src/net/mt_pki.cpp', 'src/net/mt_pki.h', 'src/net/channel_scan.cpp', 'src/net/channel_scan.h',
        'lib/dualmesh/src/presets_meshtastic.cpp', 'lib/dualmesh/src/presets_meshtastic.h',
        'src/net/mt_ble.cpp', 'src/net/mt_ble.h', 'src/net/mt_phone.cpp', 'src/net/mt_phone.h',
        'src/main.cpp', 'src/ui/app_ui.cpp', 'src/ui/app_ui_lv.cpp', 'src/net/mqtt_bridge.cpp']

TOK = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|0[xX][0-9a-fA-F]+|\d+\.?\d*[fFuUlL]*|[A-Za-z_]\w*|::|->|<<=|>>=|<<|>>|[-+*/%&|^!=<>]=|&&|\|\||\+\+|--|\S')
KW = set('''if else for while do return switch case break continue default const static constexpr inline
unsigned signed int char bool void float double long short struct class enum namespace using typedef
uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t size_t true false nullptr sizeof auto
public private protected virtual override template typename new delete this'''.split())

def strip(src):
    comments = []
    out = []
    i, n = 0, len(src)
    while i < n:
        if src.startswith('//', i):
            j = src.find('\n', i); j = n if j < 0 else j
            comments.append(src[i+2:j]); i = j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2); j = n if j < 0 else j + 2
            comments.append(src[i+2:j-2]); i = j
        elif src[i] == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j+1]); i = j + 1
        else:
            out.append(src[i]); i += 1
    return ''.join(out), comments

def tokens(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    code, comments = strip(src)
    toks = []
    line = 1
    pos = 0
    for m in TOK.finditer(code):
        line += code.count('\n', pos, m.start()); pos = m.start()
        t = m.group(0)
        if t.startswith('#'):
            continue
        toks.append((t, line))
    # Drop preprocessor lines
    return toks, comments

def norm(t):
    if t in KW: return t
    if re.match(r'[A-Za-z_]', t): return 'I'
    if re.match(r'\d|0x', t): return 'N'
    if t.startswith('"'): return 'S'
    return t

def grams(toks, k, f):
    seq = [f(t) for t, _ in toks]
    res = defaultdict(list)
    for i in range(len(seq) - k + 1):
        h = hashlib.md5(' '.join(seq[i:i+k]).encode()).hexdigest()[:16]
        res[h].append(i)
    return res

their_files = []
for root, _, fs in os.walk(MTFW):
    for f in fs:
        if f.endswith(('.cpp', '.h', '.c', '.hpp')):
            their_files.append(os.path.join(root, f))

K_RAW, K_NORM = 12, 40
raw_idx, norm_idx = {}, {}
their_tok = {}
their_comments = []
for p in their_files:
    t, c = tokens(p)
    their_tok[p] = t
    their_comments += [(p, x) for x in c]
    for h in grams(t, K_RAW, lambda x: x): raw_idx.setdefault(h, p)
    for h in grams(t, K_NORM, norm): norm_idx.setdefault(h, p)

def words(s):
    return re.findall(r'[a-zA-ZæøåÆØÅ]{2,}', s.lower())

def shingles(ws, k=6):
    return {' '.join(ws[i:i+k]) for i in range(len(ws) - k + 1)}

their_sh = defaultdict(set)
for p, c in their_comments:
    for s in shingles(words(c)): their_sh[s].add(p)

# Protobuf-tekst: .proto-kommentarer og beskedtekst
proto_sh = set()
for root, _, fs in os.walk(MTPB):
    for f in fs:
        if f.endswith('.proto'):
            src = open(os.path.join(root, f), encoding='utf-8', errors='replace').read()
            proto_sh |= shingles(words(src))

print(f'Meshtastic-filer: {len(their_files)}, tokens i alt {sum(len(t) for t in their_tok.values())}')
print(f'Parametre: rå match = {K_RAW} tokens i træk, strukturmatch = {K_NORM} normaliserede tokens i træk,')
print('kommentarer/strenge = 6 ord i træk (mod firmwarens kommentarer og .proto-filerne).\n')
total = {'raw': 0, 'norm': 0, 'cmt': 0, 'proto': 0}
for rel in OURS:
    p = os.path.join(DM, rel)
    if not os.path.exists(p): continue
    t, c = tokens(p)
    seq_raw = grams(t, K_RAW, lambda x: x)
    seq_norm = grams(t, K_NORM, norm)
    hits_raw = sorted({i for h, ix in seq_raw.items() if h in raw_idx for i in ix})
    hits_norm = sorted({i for h, ix in seq_norm.items() if h in norm_idx for i in ix})
    def runs(ix, k):
        out = []
        for i in ix:
            if out and i <= out[-1][1] + 1: out[-1][1] = i
            else: out.append([i, i])
        return [(t[a][1], t[min(b + k - 1, len(t) - 1)][1], ' '.join(x for x, _ in t[a:b + k])[:160]) for a, b in out]
    rr = runs(hits_raw, K_RAW)
    nr = runs(hits_norm, K_NORM)
    cm, pm = [], []
    strings = [x for x, _ in t if x.startswith('"')]
    for txt in c + strings:
        for s in shingles(words(txt)):
            if s in their_sh: cm.append((s, sorted(their_sh[s])[0]))
            if s in proto_sh: pm.append(s)
    print(f'== {rel}: {len(t)} tokens; rå {len(rr)}, struktur {len(nr)}, kommentar {len(cm)}, proto {len(pm)}')
    for a, b, s in rr: print(f'   RÅ    linje {a}-{b}: {s}')
    for a, b, s in nr: print(f'   STRUK linje {a}-{b}: {s}')
    for s, f in cm[:20]: print(f'   KOMM  "{s}" ~ {os.path.relpath(f, MTFW)}')
    for s in pm[:20]: print(f'   PROTO "{s}"')
    total['raw'] += len(rr); total['norm'] += len(nr); total['cmt'] += len(cm); total['proto'] += len(pm)
print('\nI alt:', total)
