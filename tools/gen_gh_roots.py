# SPDX-License-Identifier: MIT
# Laver src/net/gh_roots.h: rodcertifikaterne, som GitHub og dets downloadservere bruger, til
# net/dl (hentning af kataloger, kortpakker og firmware). ESP-IDF 4.4's indbyggede certifikatbundt
# tjekker kun roden ud fra navn og nøgle og fejler på krydssignerede kæder (fx Let's Encrypts
# "Root YR" signeret af ISRG Root X1: "PK verify failed"), så hentningen bruger disse rødder med
# mbedtls' almindelige kædetjek i stedet.
#
#   python tools/gen_gh_roots.py [sti til ca-bundle.crt]
# Standard: Mozillas rødder fra Git for Windows (ucrt64/mingw64) eller /etc/ssl/certs/ca-certificates.crt.
# Kør igen, hvis GitHub skifter certifikatudsteder (tjek med: openssl s_client -connect github.com:443).
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "net", "gh_roots.h")
WANT = [
    "ISRG Root X1",                                    # Let's Encrypt (raw/objects/release-assets)
    "ISRG Root X2",                                    # Let's Encrypt, ECDSA
    "USERTrust ECC Certification Authority",           # Sectigo (github.com)
    "USERTrust RSA Certification Authority",
    "Sectigo Public Server Authentication Root E46",
    "Sectigo Public Server Authentication Root R46",
    "DigiCert Global Root G2",                         # GitHub før 2025
]
CANDIDATES = [
    r"C:\Program Files\Git\ucrt64\etc\ssl\certs\ca-bundle.crt",
    r"C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt",
    "/etc/ssl/certs/ca-certificates.crt",
]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else next((p for p in CANDIDATES if os.path.exists(p)), None)
    if not path:
        sys.exit("Fandt ingen ca-bundle.crt; angiv stien")
    text = open(path, encoding="utf-8").read()
    # Git's bundle: "# <navn>" efterfulgt af PEM-blokken.
    blocks = {}
    for m in re.finditer(r"^# (.+?)\s*\n(-----BEGIN CERTIFICATE-----\n.+?\n-----END CERTIFICATE-----)", text, re.S | re.M):
        blocks.setdefault(m.group(1).strip(), m.group(2))
    missing = [n for n in WANT if n not in blocks]
    if missing:
        sys.exit("Mangler i bundtet: " + ", ".join(missing))
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// SPDX-License-Identifier: MIT\n")
        f.write("// GENERERET af tools/gen_gh_roots.py (Mozillas rodcertifikater) - ret ikke her.\n")
        f.write("// Rødderne bag github.com, raw.githubusercontent.com og GitHubs downloadservere.\n")
        f.write("#pragma once\n\nnamespace dm {\nnamespace dl {\n\nstatic const char kGithubRoots[] =\n")
        for n in WANT:
            f.write(f"    // {n}\n")
            for line in blocks[n].split("\n"):
                f.write(f'    "{line}\\n"\n')
        f.write("    ;\n\n}  // namespace dl\n}  // namespace dm\n")
    print(f"{len(WANT)} rødder -> {OUT}")


if __name__ == "__main__":
    main()
