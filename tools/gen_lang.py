# SPDX-License-Identifier: MIT
"""DualMesh - sprogfiler (lang/<id>.txt) til brugerfladen.

Dansk er kildesproget: de danske tekster i koden er nøglerne. Scriptet
  1. finder UI-teksterne (strengkonstanter i src/ui/ og de UI-dele af src/, der tegner tekst;
     log-linjer (DLOG/Serial) og tekniske strenge springes over),
  2. fletter dem ind i hver lang/<id>.txt (eksisterende oversættelser bevares; nye nøgler får en
     tom oversættelse, så de er lette at finde; forældede flyttes til sidst som kommentarer),
  3. bygger src/ui/lang_builtin.h med de indbyggede sprog.

Brug:  python tools/gen_lang.py           (alle sprog i lang/)
       python tools/gen_lang.py --check   (fejl, hvis en nøgle mangler en oversættelse)
Formatet: "dansk<TAB>oversættelse" pr. linje, \\n = linjeskift, # = kommentar, se lib/dualmesh/src/i18n.h.
"""
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANG = os.path.join(ROOT, "lang")
OUT = os.path.join(ROOT, "src", "ui", "lang_builtin.h")
# Filer med tekst, der vises på skærmen.
SOURCES = ["src/ui", "src/main.cpp", "src/net/wifi_mgr.cpp", "src/storage/profile_io.cpp",
           "src/storage/sd_crypt.cpp", "src/storage/nvs_tool.cpp", "src/storage/usb_disk.cpp",
           "src/map/caches.cpp", "src/net/okapi.cpp", "src/hal/gps.cpp", "src/net/dl.cpp",
           "src/storage/usb_disk.cpp"]
SKIP_FILES = ("font8x16.h", "keyboards_builtin.h", "lang_builtin.h", "dualmesh_logo")
LOG_CALL = re.compile(r"(DLOG\s*\.|Serial\s*\.|ESP_LOG|log_[ewidv]\s*\(|#\s*include|static_assert|"
                      r"\bset\s*\(\s*$|getInt\s*\(|get\s*\(\s*$|strcmp|strncmp|strstr|fopen|mkdir|"
                      r"\bkey\s*==|\.key\s*,|DLOG)")


def literals(src):
    """(start, tekst) for hver strengkonstant; sammenhængende "a" "b" lægges sammen."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            i = src.find("\n", i)
            i = n if i < 0 else i
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
            continue
        if c == "R" and src.startswith('R"', i):  # rå streng: spring over
            m = re.match(r'R"([^(]*)\(', src[i:])
            if m:
                end = src.find(")" + m.group(1) + '"', i)
                i = n if end < 0 else end + len(m.group(1)) + 2
                continue
        if c == '"':
            start = i
            parts = []
            while True:
                j = i + 1
                buf = []
                while j < n and src[j] != '"':
                    if src[j] == "\\":
                        buf.append(src[j:j + 2])
                        j += 2
                    else:
                        buf.append(src[j])
                        j += 1
                parts.append("".join(buf))
                i = j + 1
                # Næste ikke-blanke tegn (spring kommentarer over): en ny streng = sammenhæng.
                k = i
                while k < n:
                    if src[k] in " \t\r\n":
                        k += 1
                    elif src.startswith("//", k):
                        k = src.find("\n", k)
                        k = n if k < 0 else k
                    else:
                        break
                if k < n and src[k] == '"':
                    i = k
                    continue
                break
            out.append((start, "".join(parts)))
            continue
        i += 1
    return out


def c_unescape(s):
    s = re.sub(r"\\x([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), s)
    s = s.replace('\\"', '"').replace("\\'", "'").replace("\\t", "\t").replace("\\n", "\n").replace("\\\\", "\\")
    try:  # \xF0\x9F... er UTF-8-byte
        s = s.encode("latin-1").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError):
        pass
    return s


ALWAYS = {"Ø", "V"}  # kompasretninger på ét bogstav (compass() i src/map/caches.cpp)


def is_ui_text(t):
    if t in ALWAYS:
        return True
    if len(t) < 2 or not re.search(r"[A-Za-zÆØÅæøå]", t):
        return False
    if re.fullmatch(r"[a-z0-9_./:#=+\-]+", t) and re.search(r"[0-9_./:#=]", t):  # nøgler, stier, id'er
        return False
    if re.fullmatch(r"[%0-9a-zA-Z.\-+ ]*%[0-9.\-]*l?[sduxXfc][%0-9a-zA-Z.\-+ ]*", t) and not re.search(r"[a-zæøå]{3,}", t):
        return False  # rene formater som "%s %d"
    if t.startswith(("/sd/", "/dualmesh", "http", "mqtt", "[", "=== ")):
        return False
    if re.match(r"(GET|POST) ", t) or "LV_SYMBOL" in t:
        return False
    return True


def statement_before(src, pos):
    k = max(src.rfind(";", 0, pos), src.rfind("{", 0, pos), src.rfind("}", 0, pos))
    return src[k + 1:pos]


def collect():
    keys, where = [], {}
    paths = []
    for s in SOURCES:
        p = os.path.join(ROOT, s)
        if os.path.isdir(p):
            for dp, _, fs in os.walk(p):
                paths += [os.path.join(dp, f) for f in sorted(fs) if f.endswith((".cpp", ".h"))]
        else:
            paths.append(p)
    for p in paths:
        if any(x in p for x in SKIP_FILES) or "\\fonts\\" in p or "/fonts/" in p:
            continue
        src = open(p, encoding="utf-8").read()
        for pos, raw in literals(src):
            if LOG_CALL.search(statement_before(src, pos)):
                continue
            t = c_unescape(raw)
            if not is_ui_text(t):
                continue
            if t not in where:
                where[t] = os.path.relpath(p, ROOT).replace("\\", "/")
                keys.append(t)
    return keys, where


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def unesc(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append({"n": "\n", "t": "\t", "\\": "\\"}.get(s[i + 1], s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def read_lang(path):
    head, tr = [], {}
    for line in open(path, encoding="utf-8-sig").read().splitlines():
        if line.startswith("#"):
            # Hovedet er kommentarerne før første oversættelse, men ikke afsnitslinjerne
            # ("# --- <fil>"): dem skriver write_lang selv (før kom der én mere for hver kørsel).
            if (line.startswith(("#name:", "# DualMesh")) or not tr) and not line.startswith("# --- "):
                head.append(line)
            continue
        if "\t" in line:
            k, v = line.split("\t", 1)
            tr[unesc(k)] = unesc(v)
        elif line.strip():
            tr[unesc(line)] = ""
    return head, tr


def write_lang(path, head, tr, keys, where):
    lines = [h for h in head if not h.startswith("# forældet")]
    last = None
    for k in keys:
        if where[k] != last:
            lines.append(f"# --- {where[k]}")
            last = where[k]
        lines.append(f"{esc(k)}\t{esc(tr.get(k, ''))}")
    old = [k for k in tr if k not in where and tr[k]]
    if old:
        lines.append("# forældet (ikke længere i koden):")
        lines += [f"#{esc(k)}\t{esc(tr[k])}" for k in old]
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")


def main():
    keys, where = collect()
    os.makedirs(LANG, exist_ok=True)
    langs = sorted(f[:-4] for f in os.listdir(LANG) if f.endswith(".txt") and f != "index.txt")
    missing = 0
    blobs = []
    for lid in langs:
        path = os.path.join(LANG, f"{lid}.txt")
        head, tr = read_lang(path)
        write_lang(path, head, tr, keys, where)
        miss = [k for k in keys if not tr.get(k)]
        missing += len(miss)
        print(f"{lid}: {len(keys) - len(miss)}/{len(keys)} oversat")
        out = []
        for l in open(path, encoding="utf-8").read().splitlines():
            if l.startswith("#name:"):
                out.append(l)
            elif l and not l.startswith("#") and "\t" in l:
                k, v = l.split("\t", 1)
                if v:
                    out.append(l)
                elif re.search(r"%[-+ #0]*[0-9.*]*[hlLzjt]*[diouxXeEfgGcsp]", k):
                    # Uoversat mønster: som sig selv, så felter, der selv er nøgler, oversættes
                    # ("Zoom %d ...%s" + "  (følger)").
                    out.append(f"{k}\t{k}")
        text = "".join(l + "\n" for l in out)
        blobs.append((lid, text))
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// SPDX-License-Identifier: MIT\n// GENERERET af tools/gen_lang.py ud fra lang/*.txt - ret ikke her.\n")
        f.write("#pragma once\n\nnamespace dm {\nnamespace lang {\n")
        f.write("struct Builtin {\n  const char* id;\n  const char* text;\n};\n")
        for lid, text in blobs:
            f.write(f'static const char kText_{lid}[] = R"DMLANG({text})DMLANG";\n')
        f.write("static const Builtin kBuiltin[] = {\n")
        for lid, _ in blobs:
            f.write(f'    {{"{lid}", kText_{lid}}},\n')
        f.write("};\n")
        f.write(f"constexpr int kBuiltinCount = {len(blobs)};\n}}  // namespace lang\n}}  // namespace dm\n")
    # Katalog til "Hent sprog" på T-Deck'en: <id> <version> <navn>, version = de første 8 hex af
    # SHA-1 over filen (skifter, når oversættelsen ændres).
    idx = ["# DualMesh sprog til Indst. > System > Hent sprog. Hver linje: <id> <version> <navn>. "
           "Filen er <id>.txt i samme mappe (lavet af tools/gen_lang.py)."]
    for lid in langs:
        raw = open(os.path.join(LANG, f"{lid}.txt"), "rb").read()
        name = lid
        for l in raw.decode("utf-8-sig").splitlines():
            if l.startswith("#name:"):
                name = l[6:].strip() or lid
                break
        idx.append(f"{lid} {hashlib.sha1(raw).hexdigest()[:8]} {name}")
    with open(os.path.join(LANG, "index.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(idx) + "\n")
    print(f"{len(keys)} UI-tekster; {OUT}")
    if "--check" in sys.argv and missing:
        sys.exit(f"{missing} mangler oversættelse")


if __name__ == "__main__":
    main()
