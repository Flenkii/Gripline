#!/usr/bin/env python3
"""Packs the release ZIP.

    python3 tools/make_release.py <build folder> <output.zip>      (build.py --output <build folder>)

Layout:
    README.txt, Keybindings.txt, LICENSE.txt, SHA256SUMS.txt
    Azimuth/fh2-mods/Gripline.dll      (FH2 Mod Platform mod, next to the official Extra Options)

No game files are packed."""
import hashlib
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    source, output = (Path(a) for a in sys.argv[1:3])
    dll = (source / 'fh2-mods' / 'Gripline.dll').read_bytes()
    sums = f'{hashlib.sha256(dll).hexdigest()}  fh2-mods/Gripline.dll\r\n'
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        def add(name, data):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 29, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)

        for name in ('README.txt', 'Keybindings.txt'):
            add(name, (ROOT / 'release' / name).read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        add('LICENSE.txt', (ROOT / 'LICENSE').read_bytes())
        add('Azimuth/fh2-mods/Gripline.dll', dll)
        add('SHA256SUMS.txt', sums.encode('ascii'))
    with zipfile.ZipFile(output) as z:
        for info in z.infolist():
            print(f'{info.file_size:>9}  {info.filename}')
    print(sums.strip())


if __name__ == '__main__':
    main()
