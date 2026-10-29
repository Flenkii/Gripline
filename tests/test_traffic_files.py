"""Test of features/traffic_files.c with the real compiled test DLL: the game's traffic data
(AI/CarLoadingSettings.xml, AI/Densities*.xml) is raised exactly like tools/fh2_more_traffic.py does it,
and the whole start writes the files from media\\EntityModel.zip to Mount\\gripline_traffic."""
import importlib.util, os, sys, tempfile, zipfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('mt', os.path.join(HERE, '..', 'tools', 'fh2_more_traffic.py'))
MT = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MT)
MT.round = lambda v: int(v + 0.5)  # the DLL rounds .5 up (Python: to even)
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


# the shape of the game files (values like the original)
DENSITIES = '''<?xml version="1.0" encoding="utf-8"?>
<Densities>
  <DensityList cartype="traffic">
    <Density id="1" densityNameTagValue="a" min="0.4" max="0.7" speed="25" spawnInDistance="300" spawnOutDistance="350"/>
    <Density id="2" densityNameTagValue="b" min="0.2" max="0.35" speed="20" spawnInDistance="250" spawnOutDistance="300"/>
    <Density id="3" densityNameTagValue="motorway" min="0.8" max="1.5" speed="30" spawnInDistance="400" spawnOutDistance="450"/>
    <Density id="12" densityNameTagValue="open_world" min="0" max="0" speed="15" spawnInDistance="200" spawnOutDistance="250"/>
  </DensityList>
  <DensityList cartype="festival">
    <Density id="1" densityNameTagValue="a" min="0.1" max="0.25" spawnAwayChance="0.75"/>
    <Density id="12" densityNameTagValue="open_world" min="0.05" max="0.1" spawnAwayChance="0.7" />
  </DensityList>
  <DensityList cartype="police">
    <Density id="1" densityNameTagValue="a" min="0.1" max="0.2" speed="25" spawnInDistance="300" spawnOutDistance="350"/>
  </DensityList>
</Densities>
'''
CAR_LOADING = '''<?xml version="1.0" encoding="utf-8"?>
<CarLoadingSettings>
  <FestivalSettings MaxActiveFestival="11" InitialNumFestival="3" />
  <TrafficSettings MaxActiveTraffic="18" InitialNumTraffic="6" MaxActiveTypes="8">
    <CarType ModelID="bus" MaxActive="1"/>
    <CarType ModelID="hatch" MaxActive="3"/>
  </TrafficSettings>
</CarLoadingSettings>
'''
# TrafficLevel -> the same values as the Python tool (see levels[] in traffic_files.c)
LEVELS = {
    1: dict(max_festival=16, mult=2.0, festival=1.5, cap=1.5, dist=1.0, fill=1, max_active=30, types=10, type_mult=1),
    2: dict(max_festival=22, mult=3.0, festival=2.0, cap=2.0, dist=1.25, fill=1, max_active=45, types=10, type_mult=2),
    3: dict(max_festival=30, mult=5.0, festival=3.0, cap=3.0, dist=1.25, fill=1, max_active=70, types=12, type_mult=2),
    4: dict(max_festival=40, mult=8.0, festival=4.0, cap=4.0, dist=1.5, fill=1, max_active=100, types=14, type_mult=3),
    5: dict(max_festival=80, mult=20.0, festival=6.0, cap=8.0, dist=2.0, fill=1, max_active=200, types=20, type_mult=6),
}

e = Emu(DLL, tempfile.mkdtemp(prefix='eo-traffic-'))


def run(kind, level, text):
    data = text.encode()
    src = e.put_bytes(data)
    out = e.alloc(len(data) * 4 + 4096)
    n = e.call('Fh2TestTrafficPatch', kind, level, src, len(data), out, len(data) * 4 + 4096)
    return bytes(e.mu.mem_read(out, n)).decode() if n else ''


print('Gleiche Werte wie tools/fh2_more_traffic.py')
for level, c in LEVELS.items():
    ref = MT.patch_densities(DENSITIES, c['mult'], c['festival'], c['dist'], [], 'AI/Densities.xml',
                             fill=bool(c['fill']), cap=c['cap'])
    check(run(1, level, DENSITIES) == ref, f'Densities.xml, TrafficLevel={level}')
    ref = MT.patch_car_loading(CAR_LOADING, dict(max_active=c['max_active'], initial=6, types=c['types'],
                                                 type_mult=c['type_mult']), [])
    ref = ref.replace('MaxActiveFestival="11"', 'MaxActiveFestival="%d"' % c['max_festival'])
    check(run(0, level, CAR_LOADING) == ref, f'CarLoadingSettings.xml, TrafficLevel={level}')
got = run(1, 6, DENSITIES)
check(got.count('min="0" max="0"') == 6 and 'min="0.1" max="0.2"' in got, 'Kein Verkehr: Verkehr + Festival 0, Polizei bleibt')
check(run(0, 6, CAR_LOADING) == CAR_LOADING, 'Kein Verkehr: CarLoadingSettings unverändert')
got = run(1, 7, DENSITIES)
check('densityNameTagValue="a" min="0" max="0" speed="25"' in got, 'Nur Drivatare: kein Zivilverkehr')
check('densityNameTagValue="a" min="0.8" max="2" spawnAwayChance="0.75" speed="25"/>' in got, 'Nur Drivatare: Drivatare x8 mit Tempo der Straße')
check('densityNameTagValue="open_world" min="0.4" max="0.8" spawnAwayChance="0.7" speed="15" />' in got, 'Nur Drivatare: Tempo auch vor " />"')
check('MaxActiveFestival="80"' in run(0, 7, CAR_LOADING) and 'MaxActiveTraffic="18"' in run(0, 7, CAR_LOADING), 'Nur Drivatare: 80 Drivatare, Verkehr wie im Spiel')
check(run(1, 0, DENSITIES) == DENSITIES and run(0, 0, CAR_LOADING) == CAR_LOADING, 'TrafficLevel=0: unverändert')
got = run(1, 4, DENSITIES)
check('densityNameTagValue="open_world" min="1.6" max="2.8"' in got, 'open_world bekommt Zivilverkehr (wie "b")')
check('cartype="police">\n    <Density id="1" densityNameTagValue="a" min="0.1" max="0.2"' in got, 'Polizei bleibt')

print('Ganzer Start (EntityModel.zip -> Mount\\gripline_traffic)')
root = tempfile.mkdtemp(prefix='eo-traffic-')
os.makedirs(os.path.join(root, 'Mount', 'media'))
with zipfile.ZipFile(os.path.join(root, 'Mount', 'media', 'EntityModel.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('AI/CarLoadingSettings.xml', CAR_LOADING)
    z.writestr('AI/Densities.xml', DENSITIES)
    z.writestr('AI/DensitiesRace.xml', DENSITIES)
    z.writestr('Other/Thing.xml', '<x/>')


class FEmu(Emu):
    def k_SetFilePointerEx(self):
        self.files[self.arg(0)][1] = self.arg(1)
        return 1


f = FEmu(DLL, root)
check(f.call('Fh2TestTrafficFilesStart', f.put_wstr('C:\\test\\Mount'), 4) == 2, 'TrafficLevel=4: fertig')
out = os.path.join(root, 'Mount', 'gripline_traffic')
files = sorted(os.path.relpath(os.path.join(d, n), out).replace(os.sep, '/') for d, _, ns in os.walk(out) for n in ns)
check(files == ['AI/CarLoadingSettings.xml', 'AI/Densities.xml', 'EntityModel/AI/CarLoadingSettings.xml',
                'EntityModel/AI/Densities.xml'], f'Dateien: {files} (keine Renn-Dichte)')
check(open(os.path.join(out, 'EntityModel', 'AI', 'Densities.xml')).read() == run(1, 4, DENSITIES), 'Densities.xml wie oben')
check('MaxActiveTraffic="100"' in open(os.path.join(out, 'EntityModel', 'AI', 'CarLoadingSettings.xml')).read(),
      'MaxActiveTraffic 18 -> 100')
check(f.call('Fh2TestTrafficFilesStart', f.put_wstr('C:\\test\\Mount'), 6) == 2, 'Kein Verkehr: Ordner wird eingebunden')
check(f.call('Fh2TestTrafficFilesStart', f.put_wstr('C:\\test\\Mount'), 0) == 3, 'TrafficLevel=0: Ordner wird nicht eingebunden')

print(f'\n{checks[0] - len(fails)}/{checks[0]} ok')
sys.exit(1 if fails else 0)
