# SPDX-License-Identifier: MIT
# Udskriver et byggeflag med den korte git-hash til src/version.h (DM_GIT_REV), fx
#   -D DM_GIT_REV=\"a1b2c3d\"      (eller "a1b2c3d+" når der er ændringer, der ikke er committet)
# Bruges fra platformio.ini: build_flags = !python tools/git_rev.py
import subprocess

try:
    rev = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], stderr=subprocess.DEVNULL).decode().strip()
    dirty = subprocess.call(['git', 'diff', '--quiet', 'HEAD', '--', 'src', 'lib', 'include'],
                            stderr=subprocess.DEVNULL) != 0
    if dirty:
        rev += '+'
except Exception:
    rev = 'ukendt'
print('-D DM_GIT_REV=\\"%s\\"' % rev)
