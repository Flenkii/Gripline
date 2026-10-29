"""Test of the mods list for the problem report (features/mod_scan.c): reads fh2-mods\\platform-mods.txt of the
FH2 Mod Platform and asks for a few known modules by name; no process list."""
import os, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails = []


def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


LIST = ('\ufeff# What the MODS tab remembers about each mod (DLL, name, version, description, GitHub repo).\r\n'
        'FH2MP.dll\tFH2MP\t1.0.0\tOnline.\t\r\n'
        'FnFNitro.dll\tNitroSystem\t1.0.0\tFast & Furious Nitro\tqiangqiang101/NitroSystem\r\n'
        'Gripline.dll\tGripline\tV5\tWheel.\tFlenkii/Gripline\r\n')


def text_for(mods_list, modules=None):
    root = tempfile.mkdtemp(prefix='eo-modscan-')
    if mods_list is not None:
        os.makedirs(os.path.join(root, 'fh2-mods'))
        open(os.path.join(root, 'fh2-mods', 'platform-mods.txt'), 'wb').write(mods_list.encode('utf-8'))
    e = Emu(DLL, root)
    e.modules = modules or {}
    out = e.alloc(160)
    e.mu.mem_write(out, bytes(160))
    r = e.call('Fh2TestModsText', e.put_wstr('C:\\test'), out, 160) & 0xffffffff
    return r, bytes(e.mu.mem_read(out, 160)).split(b'\0')[0].decode('utf-8')


r, t = text_for(LIST)
check(r == 1 and t == 'Gripline V6 - mods found\nFH2MP, NitroSystem', f'Platform-Liste: {t!r}')
r, t = text_for(None)
check(r == 0, 'nichts gefunden: keine Liste')
r, t = text_for('# x\r\nGripline.dll\tGripline\tV5\r\n')
check(r == 0, 'nur Gripline selbst: keine Liste')
many = ''.join(f'M{i}.dll\tVeryLongModName{i:02d}\t1\r\n' for i in range(20))
r, t = text_for(many)
check(r == 1 and len(t) < 160 and t.endswith('...'), f'viele Mods werden gekuerzt ({len(t)} Zeichen)')
r, t = text_for('A.dll\tSame\t1\r\nB.dll\tSame\t1\r\n')
check(t.endswith('found\nSame'), 'gleicher Name nur einmal')

print()
print('ALLE TESTS OK' if not fails else f'{len(fails)} FEHLER')
sys.exit(1 if fails else 0)
