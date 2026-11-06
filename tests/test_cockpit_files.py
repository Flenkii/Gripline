"""Cockpit files without game data (features/cockpit.c): the manifest follows the game zips (Source=),
and remove_files deletes only Gripline's own cockpit files (used when the Turn hook cannot go in)."""
import fnmatch, os, struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


class CEmu(Emu):
    finds = {}

    def k_FindFirstFileW(self):
        pattern = self.host(self.wstr(self.arg(0)))
        folder, mask = os.path.split(pattern)
        names = sorted(n for n in os.listdir(folder) if fnmatch.fnmatch(n.lower(), mask.lower())) if os.path.isdir(folder) else []
        if not names:
            return 0xffffffffffffffff
        h = 0x700 + len(self.finds)
        self.finds[h] = (folder, names)
        return self.k_FindNextFileW(h, self.arg(1))

    def k_FindNextFileW(self, h=None, data=None):
        h = h if h is not None else self.arg(0)
        data = data if data is not None else self.arg(1)
        folder, names = self.finds.get(h, ('', []))
        if not names:
            return 0
        name = names.pop(0)
        full = os.path.join(folder, name)
        attrs = 0x10 if os.path.isdir(full) else 0x80
        size = 0 if os.path.isdir(full) else os.path.getsize(full)
        wtime = int(os.path.getmtime(full) * 10_000_000)
        head = struct.pack('<I8s8sQII8s', attrs, bytes(8), bytes(8), wtime, size >> 32, size & 0xffffffff, bytes(8))
        self.mu.mem_write(data, head + (name + '\0').encode('utf-16le'))
        return h

    def k_FindClose(self):
        self.finds.pop(self.arg(0), None)
        return 1


root = tempfile.mkdtemp(prefix='gl-cockpit-files-')
mount = os.path.join(root, 'Mount')
cars = os.path.join(mount, 'media', 'cars')
os.makedirs(cars)
os.makedirs(os.path.join(mount, 'media', 'drivers'))
open(os.path.join(cars, 'ABC_Car_01.zip'), 'wb').write(b'not a zip')  # no animation inside: only the fingerprint counts
out = os.path.join(mount, 'gripline_cockpit')
manifest = os.path.join(out, 'gripline_cockpit.txt')
c = CEmu(DLL, root)
M = 'C:\\test\\Mount'

print('Manifest folgt den Spiel-Zips:')
c.call('Fh2TestCockpitMake', c.put_wstr(M), 900, 10)
first = open(manifest).read()
check('Version=7' in first and 'Source=' in first, 'Manifest mit Version 7 und Source: ' + first.replace('\r\n', ' | '))
c.call('Fh2TestCockpitMake', c.put_wstr(M), 900, 10)
check(open(manifest).read() == first, 'gleiche Zips: gleiches Manifest')
open(os.path.join(cars, 'ABC_Car_01.zip'), 'wb').write(b'not a zip, other size')
c.call('Fh2TestCockpitMake', c.put_wstr(M), 900, 10)
second = open(manifest).read()
check(second != first and 'Rotation=900' in second, 'Auto-Zip geändert (Update/Auto-Mod): neues Manifest -> neu erzeugt')
open(os.path.join(cars, 'NEW_Car_20.zip'), 'wb').write(b'x')
c.call('Fh2TestCockpitMake', c.put_wstr(M), 900, 10)
check(open(manifest).read() != second, 'neues Auto (DLC): neues Manifest')

print('Gripline-Cockpit-Dateien entfernen:')
own = ['cars/ABC_Car_01/scene/Drivers/default/leftRightTurn.gr2',
       'cars/NEW_Car_20/scene/Drivers/suited/LeftRightTurn.GR2',
       'drivers/default/driver.modelbin', 'drivers/stig/driver.modelbin']
other = ['cars/ABC_Car_01/scene/Drivers/default/other.gr2', 'drivers/default/other.bin', 'AI/Densities.xml',
         'EntityModel/AI/CarLoadingSettings.xml']
for rel in own + other:
    p = os.path.join(out, *rel.split('/'))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(b'x')
game_file = os.path.join(cars, 'ABC_Car_01.zip')
c.call('Fh2TestCockpitRemove', c.put_wstr(M))
check(not os.path.exists(manifest), 'Manifest weg (beim nächsten Start nicht mehr "fertig")')
check(all(not os.path.exists(os.path.join(out, *r.split('/'))) for r in own), 'Lenkrad-Animationen und Fahrer ohne Hände weg')
check(all(os.path.exists(os.path.join(out, *r.split('/'))) for r in other), 'andere Dateien (Verkehr, fremde) bleiben')
check(os.path.exists(game_file), 'Spiel-Zip unberührt')
c.call('Fh2TestCockpitRemove', c.put_wstr(M))
check(True, 'zweimal entfernen: kein Fehler')

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
