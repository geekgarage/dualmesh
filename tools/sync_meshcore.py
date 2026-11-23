# SPDX-License-Identifier: MIT
"""Henter de MeshCore-filer, DualMesh bruger, fra en låst MeshCore-version.

MeshCore (MIT, Scott Powell / rippleradios.com) kopieres ind i lib/meshcore, så
buildet er uafhængigt af MeshCores egne board-varianter og radiodrivere.
Brug:  python tools/sync_meshcore.py [tag]     (standard: TAG nedenfor)
Kræver git. Ændr aldrig de kopierede filer direkte; ret i DualMesh i stedet.
"""
import os
import shutil
import subprocess
import sys
import tempfile

TAG = 'companion-v1.18.0'
REPO = 'https://github.com/meshcore-dev/MeshCore.git'
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, 'lib', 'meshcore')

FILES = [
    'src/MeshCore.h',
    'src/Dispatcher.h', 'src/Dispatcher.cpp',
    'src/Identity.h', 'src/Identity.cpp',
    'src/Mesh.h', 'src/Mesh.cpp',
    'src/Packet.h', 'src/Packet.cpp',
    'src/Utils.h', 'src/Utils.cpp',
    'src/helpers/ArduinoHelpers.h',
    'src/helpers/AdvertDataHelpers.h', 'src/helpers/AdvertDataHelpers.cpp',
    'src/helpers/BaseChatMesh.h', 'src/helpers/BaseChatMesh.cpp',
    'src/helpers/ChannelDetails.h',
    'src/helpers/ContactInfo.h',
    'src/helpers/SimpleMeshTables.h',
    'src/helpers/StaticPoolPacketManager.h', 'src/helpers/StaticPoolPacketManager.cpp',
    'src/helpers/TxtDataHelpers.h', 'src/helpers/TxtDataHelpers.cpp',
    'src/helpers/UTF8Helpers.h',
    'lib/ed25519',
    'license.txt',
]


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else TAG
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'MeshCore')
        subprocess.run(['git', 'clone', '-q', '--depth', '1', '--branch', tag, REPO, src], check=True)
        sha = subprocess.run(['git', '-C', src, 'rev-parse', '--short', 'HEAD'], check=True,
                             capture_output=True, text=True).stdout.strip()
        if os.path.isdir(DEST):
            shutil.rmtree(DEST)
        for rel in FILES:
            s = os.path.join(src, rel)
            d = os.path.join(DEST, 'ed25519' if rel == 'lib/ed25519' else rel)
            if rel == 'lib/ed25519':
                d = os.path.join(ROOT, 'lib', 'ed25519')
                if os.path.isdir(d):
                    shutil.rmtree(d)
                shutil.copytree(s, d)
                with open(os.path.join(d, 'README.md'), 'w', encoding='utf-8', newline='\n') as f:
                    f.write(f'# ed25519\n\nKopi af `lib/ed25519` fra MeshCore `{tag}` ({sha}), som\n'
                            'MeshCore kræver (`ed_25519.h`). Oprindeligt orlp/ed25519, zlib-licens: se\n'
                            '`license.txt`. Hentes med `tools/sync_meshcore.py`.\n')
                continue
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d)
        with open(os.path.join(DEST, 'library.json'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('{\n  "name": "MeshCore",\n  "version": "%s",\n  "description": "Udvalgte filer fra '
                    'MeshCore %s (%s), hentet med tools/sync_meshcore.py",\n  "license": "MIT",\n'
                    '  "frameworks": "arduino",\n  "dependencies": {\n'
                    '    "rweather/Crypto": "^0.4.0",\n    "densaugeo/base64": "~1.4.0"\n  }\n}\n'
                    % (tag, tag, sha))
        with open(os.path.join(DEST, 'VERSION.md'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(f'MeshCore {tag} ({sha}) fra {REPO}\n\nMIT-licens: se license.txt. '
                    'Filerne må ikke ændres her; kør tools/sync_meshcore.py for at opdatere.\n')
    print(f'MeshCore {tag} ({sha}): {len(FILES)} poster kopieret til lib/meshcore og lib/ed25519')


if __name__ == '__main__':
    main()
