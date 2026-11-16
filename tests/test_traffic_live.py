"""Test of features/traffic_live.c (V7.3 live traffic) with the real compiled test DLL: a density table
as the game's parser leaves it (vectors per cartype, 0x28-byte entries) is recognised and rewritten for the
level of the menu, and a table the game changed is left alone."""
import os, struct, sys, tempfile, zipfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


DENSITIES = '''<?xml version="1.0" encoding="utf-8"?>
<Densities>
  <DensityList cartype="traffic">
    <!-- <Density id="0" densityNameTagValue="comment" min="9" max="9" /> -->
    <Density id="1" densityNameTagValue="empty" min="0" max="0" />
    <Density id="2" densityNameTagValue="b" min="0.4" max="2.8" speed="30" spawnAwayChance="0.6" />
    <Density id="3" densityNameTagValue="a" min="0.48" max="3.6" speed="40" spawnAwayChance="0.6" />
    <Density id="10" densityNameTagValue="b_high" min="0.48" max="4" speed="30" spawnInDistance="200" spawnOutDistance="250" />
    <Density id="12" densityNameTagValue="open_world" min="0" max="0" />
  </DensityList>
  <DensityList cartype="festival">
    <Density id="1" densityNameTagValue="a" min="0.1" max="0.25" spawnAwayChance="0.75"/>
    <Density id="12" densityNameTagValue="open_world" min="0.05" max="0.1" spawnAwayChance="0.7" />
  </DensityList>
  <DensityList cartype="police">
    <Density id="1" densityNameTagValue="a" min="0.1" max="0.2" />
  </DensityList>
</Densities>
'''
ROADTRIP = DENSITIES.replace('max="2.8"', 'max="1.9"')
RACE = DENSITIES.replace('min="0.48" max="3.6"', 'min="0.1" max="0.2"')

root = tempfile.mkdtemp(prefix='eo-trafficlive-')
os.makedirs(os.path.join(root, 'Mount', 'media'))
with zipfile.ZipFile(os.path.join(root, 'Mount', 'media', 'EntityModel.zip'), 'w') as z:
    z.writestr('AI/CarLoadingSettings.xml', '<CarLoadingSettings><TrafficSettings MaxActiveTraffic="18" MaxActiveTypes="8"></TrafficSettings></CarLoadingSettings>')
    z.writestr('AI/Densities.xml', DENSITIES)
    z.writestr('AI/DensitiesRoadTrip.xml', ROADTRIP)


class FEmu(Emu):
    def k_SetFilePointerEx(self):
        self.files[self.arg(0)][1] = self.arg(1)
        return 1


e = FEmu(DLL, root)
check(e.call('Fh2TestTrafficFilesStart', e.put_wstr('C:\\test\\Mount'), 4) == 2, 'Start mit TrafficLevel 4')


def values(file, level, cartype):
    out = e.alloc(64 * 5 * 4)
    n = e.call('Fh2TestTrafficLiveValues', file, level, cartype, out, 64 * 5)
    return [struct.unpack('<5f', bytes(e.mu.mem_read(out + i * 20, 20))) for i in range(n)]


t0, f0 = values(0, 0, 2), values(0, 0, 1)
check(len(t0) == 5 and len(f0) == 2, f'Original: 5 Verkehr + 2 Festival (Kommentar und Polizei zählen nicht): {len(t0)}/{len(f0)}')
check(abs(t0[1][0] - 0.4) < 1e-6 and abs(t0[1][1] - 2.8) < 1e-6 and t0[1][2] == 30 and t0[1][3] == -1, 'Original "b": 0.4/2.8, 30 mph, kein spawnIn (-1)')
t4 = values(0, 4, 2)
check(abs(t4[1][1] - 4.0) < 1e-6 and abs(t4[4][1] - 4.0) < 1e-6 and t4[3][3] == 300, 'Stufe 4: max bis 4, open_world wie "b", spawnIn 200 -> 300')
check(all(v[0] == 0 and v[1] == 0 for v in values(0, 6, 2) + values(0, 6, 1)), 'Kein Verkehr: alles 0')


def table(traffic, festival):
    """a table like the parser leaves it: vectors at +8 + cartype*0x18"""
    t = e.alloc(0x80)
    for cartype, rows in ((2, traffic), (1, festival)):
        if not rows:
            continue
        mem = e.alloc(0x28 * len(rows))
        for i, r in enumerate(rows):
            e.mu.mem_write(mem + i * 0x28, struct.pack('<ffffff', r[0], r[1], 0.44703999 * r[2], 0.6, r[3], r[4]))
        e.wq(t + 8 + cartype * 0x18, mem)
        e.wq(t + 8 + cartype * 0x18 + 8, mem + 0x28 * len(rows))
        e.wq(t + 8 + cartype * 0x18 + 16, mem + 0x28 * len(rows))
    return t


def read(t, cartype, i):
    mem = e.rq(t + 8 + cartype * 0x18)
    return struct.unpack('<6f', bytes(e.mu.mem_read(mem + i * 0x28, 24)))


main = table(t4, values(0, 4, 1))
check(e.call('Fh2TestTrafficLiveRemember', main, 4) == 1, 'Densities.xml (geladen mit Stufe 4) erkannt')
trip = table(values(1, 4, 2), values(1, 4, 1))
check(e.call('Fh2TestTrafficLiveRemember', trip, 4) == 2, 'DensitiesRoadTrip.xml erkannt')
race = table([(0.1, 0.2, 0, -1, -1)] * 5, [])
check(e.call('Fh2TestTrafficLiveRemember', race, 4) > 0x7fffffff, 'Renn-Dichte (andere Werte) wird nicht angefasst')

check(e.call('Fh2TestTrafficLiveApply', 0) == 2, 'Menü auf Original: beide Tabellen geändert')
r = read(main, 2, 1)
check(abs(r[0] - 0.4) < 1e-6 and abs(r[1] - 2.8) < 1e-6, f'"b" wieder 0.4/2.8: {r[:2]}')
r = read(main, 2, 4)
check(r[0] == 0 and r[1] == 0, 'open_world wieder ohne Verkehr')
r = read(main, 2, 3)
check(r[4] == 200 and r[5] == 250, f'spawnIn/Out wieder 200/250: {r[4:]}')
check(abs(read(main, 1, 0)[1] - 0.25) < 1e-6, 'Festival wieder Original')
check(abs(read(trip, 2, 1)[1] - 1.9) < 1e-6, 'RoadTrip "b" wieder 1.9')

check(e.call('Fh2TestTrafficLiveApply', 6) == 2, 'Kein Verkehr live')
check(all(read(main, 2, i)[1] == 0 for i in range(5)) and read(main, 1, 0)[1] == 0, 'Kein Verkehr: alle max 0')
check(e.call('Fh2TestTrafficLiveApply', 7) == 2, 'Nur Drivatare live')
r = read(main, 1, 0)
check(abs(r[1] - 0.25 * 8) < 1e-5 or r[1] > 0.25, f'Nur Drivatare: Festival hoch ({r[1]})')
check(read(main, 2, 1)[1] == 0, 'Nur Drivatare: kein Zivilverkehr')

# the game rebuilt one table (other values): Gripline must leave it alone
mem = e.rq(trip + 8 + 2 * 0x18)
e.wf(mem + 0x28, 1.234)
check(e.call('Fh2TestTrafficLiveApply', 3) == 1, 'geänderte Tabelle wird fallen gelassen, die andere bleibt')
check(abs(e.rf(mem + 0x28) - 1.234) < 1e-6, 'geänderte Tabelle nicht überschrieben')
check(abs(read(main, 2, 1)[1] - min(2.8 * 5, 3.0)) < 1e-5, f'Stufe 3 auf Densities.xml: "b" max {read(main, 2, 1)[1]}')

print(f'\n{checks[0] - len(fails)}/{checks[0]} ok')
sys.exit(1 if fails else 0)
