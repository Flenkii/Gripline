#!/usr/bin/env python3
"""Packs the release ZIP.

    python3 tools/make_release.py <plugin build folder> <output.zip>   (V6: build.py --plugin --output <folder>)
    python3 tools/make_release.py <winmm.dll> <output.zip>             (old V5 layout, Gripline.dll beside winmm.dll)

Layout (V6, next to the official Extra Options):
    README.txt, Keybindings.txt, LICENSE.txt
    Azimuth/fh2-mods/Gripline.dll                            (FH2 Mod Platform mod)
    Azimuth/mods/Gripline/plugins/GriplineTick.dll           (Extra Options plugin: weather / time of day tick)
    Optional/Profiles/<name>/FH2_Wheel.ini, FH2_Keyboard.ini  (ready-made settings)
    SHA256SUMS.txt

The profile INIs are the mod's own default files (taken from the C sources, so they always match
the DLL) with a few values changed. No game files are packed."""
import hashlib
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def c_literal(source, name):
    """Joins the string literal pieces of `static const char <name>[] = "..." "...";`."""
    text = (ROOT / source).read_text(encoding='utf-8')
    block = text[text.index(f'{name}[] ='):]
    block = block[:block.index(';\r\n') if ';\r\n' in block else block.index(';\n')]
    pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
    escapes = {'r': '\r', 'n': '\n', '"': '"', '\\': '\\', 't': '\t'}
    return ''.join(re.sub(r'\\(.)', lambda m: escapes[m.group(1)], p) for p in pieces)


def keyboard_ini():
    text = c_literal('features/keyboard_pad.c', 'default_ini')
    source = (ROOT / 'features/keyboard_pad.c').read_text(encoding='utf-8')
    table = source[source.index('static Binding bindings[] = {'):]
    table = table[:table.index('};')]
    for key, keys in re.findall(r'\{L"(\w+)", \w+, \w+, L"([^"]*)"', table):
        text += f'{key}={keys}\r\n'
    return text


def changed(text, section, values):
    """Sets key=value lines (keys must exist in the file)."""
    for key, value in values.items():
        pattern = re.compile(rf'^({key}=)[^\r\n]*', re.M)
        assert pattern.search(text), f'{key} missing in [{section}]'
        text = pattern.sub(lambda m: m.group(1) + str(value), text, count=1)
    return text


PROFILES = {
    'Controller': ({'Enabled': 0}, {'Enabled': 0}),
    'Keyboard': ({'Enabled': 0}, {'Enabled': 1}),
    'Wheel': ({'Enabled': 1}, {'Enabled': 1}),
    'Wheel_ClutchAsBrake': ({'Enabled': 1, 'ClutchAsBrake': 1}, {'Enabled': 1}),
}


def main():
    source, output = (Path(a) for a in sys.argv[1:3])
    plugin = source.is_dir()
    if plugin:
        binaries = {'Azimuth/fh2-mods/Gripline.dll': source / 'fh2-mods' / 'Gripline.dll',
                    'Azimuth/mods/Gripline/plugins/GriplineTick.dll':
                        source / 'mods' / 'Gripline' / 'plugins' / 'GriplineTick.dll'}
    else:
        assert source.name.lower() == 'winmm.dll', 'die DLL muss winmm.dll heißen (Name steht in der Exporttabelle)'
        binaries = {'Azimuth/winmm.dll': source,
                    # FH2 Mod Platform (comes with FH2MP): hands the platform to winmm.dll -> GRIPLINE tile
                    'Azimuth/fh2-mods/Gripline.dll': source.parent / 'Gripline.dll'}
    wheel, keyboard = c_literal('features/wheel_bridge.c', 'default_ini'), keyboard_ini()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        def add(name, data):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 29, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)

        for name in ('README.txt', 'Keybindings.txt'):
            add(name, (ROOT / 'release' / name).read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        add('LICENSE.txt', (ROOT / 'LICENSE').read_bytes())
        sums = []
        for name, path in binaries.items():
            data = path.read_bytes()
            add(name, data)
            sums.append(f'{hashlib.sha256(data).hexdigest()}  {name.split("/", 1)[1]}')
        if not plugin:
            add('Azimuth/FH2_ExtraOptions.ini', (ROOT / 'release/FH2_ExtraOptions.ini').read_bytes())
        for name, (wheel_values, keyboard_values) in PROFILES.items():
            add(f'Optional/Profiles/{name}/FH2_Wheel.ini', changed(wheel, 'Wheel', wheel_values).encode('ascii'))
            add(f'Optional/Profiles/{name}/FH2_Keyboard.ini',
                changed(keyboard, 'Keyboard', keyboard_values).encode('ascii'))
        add('SHA256SUMS.txt', ('\r\n'.join(sums) + '\r\n').encode('ascii'))
    with zipfile.ZipFile(output) as z:
        for info in z.infolist():
            print(f'{info.file_size:>9}  {info.filename}')
    print('\n'.join(sums))


if __name__ == '__main__':
    main()
