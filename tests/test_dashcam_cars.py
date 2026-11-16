"""V7.2 Dashboard Cam per car (features/dashcam_cars.c) with the compiled test DLL: every car keeps its own
forward / height in Gripline.ini [DashCamCars]; a car without a line uses the general place."""
import os, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../out-test/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


root = tempfile.mkdtemp(prefix='eo-dashcam-')
e = Emu(DLL, root)
ini = os.path.join(root, 'Gripline.ini')
open(ini, 'w').write('[Gripline]\r\nWalkDoor=1\r\n\r\n[DashCamCars]\r\nNIS_GTR_12 = 40 -10\r\n')
FORWARD, UP = 8, 9
s32 = lambda v: v - (1 << 32) if v >= 1 << 31 else v
get = lambda i: s32(e.call('Fh2TestRigGet', i) & 0xffffffff)
e.call('Fh2TestDashcamStart', e.put_wstr('C:\\test'))


def car(stem):
    a = e.alloc(64)
    e.mu.mem_write(a, stem.encode() + b'\0')
    e.call('Fh2TestDashcamCar', a)


print('car with its own place:')
car('NIS_GTR_12')
check((get(FORWARD), get(UP)) == (40, -10), 'GTR: 40 / -10 from [DashCamCars]')
print('car without a line: the general place (35 / -5):')
car('AUD_RS3_11')
check((get(FORWARD), get(UP)) == (35, -5), 'RS3: general place')
print('changing it in the menu keeps it for this car:')
check(e.call('Fh2TestRigSet', FORWARD, 50) == 1, 'forward 50 set')
text = open(ini).read()
check('AUD_RS3_11=50 -5' in text.replace(' = ', '=') or 'AUD_RS3_11 = 50 -5' in text, 'saved as AUD_RS3_11 = 50 -5: %r' % text)
check('NIS_GTR_12 = 40 -10' in text and 'WalkDoor=1' in text, 'the rest of Gripline.ini stays')
check('CockpitViewForward' not in text, 'the general place is not changed')
car('NIS_GTR_12')
check((get(FORWARD), get(UP)) == (40, -10), 'back in the GTR: its own place')
car('AUD_RS3_11')
check((get(FORWARD), get(UP)) == (50, -5), 'RS3 again: 50 / -5')
car('FER_F40_87')
check((get(FORWARD), get(UP)) == (35, -5), 'a third car: still the general place')

print('the settings watcher sees the [DashCamCars] write - the car keeps its place (15.11.2026 bug):')
before = open(ini).read()
open(ini, 'w').write(before.replace('WalkDoor=1', 'WalkDoor=1\r\nCockpitViewForward=35\r\nCockpitViewUp=-5'))
e.call('Fh2TestRigStart', 0, 0, e.put_wstr('C:\\test'))
car('AUD_RS3_11')
check(e.call('Fh2TestRigSet', UP, 3) == 1, 'height 3 set')
e.call('Fh2TestRigStep', 10_000)
e.call('Fh2TestRigStep', 20_000)
check((get(FORWARD), get(UP)) == (50, 3), 'RS3 stays at 50 / 3 after the file reload: %r' % ((get(FORWARD), get(UP)),))
text = open(ini).read()
check('CockpitViewForward=35' in text and 'CockpitViewUp=-5' in text, 'the general place stays 35 / -5: %r' % text)
car('FER_F40_87')
check((get(FORWARD), get(UP)) == (35, -5), 'another car: the general place')

print(f'{checks[0]} checks, {len(fails)} failed')
print('ALLE TESTS OK' if not fails else f'{len(fails)} FEHLER')
sys.exit(1 if fails else 0)
