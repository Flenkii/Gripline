"""V7.1 mouse look in the car (features/drive_look.c) with the compiled test DLL: the camera's
eye / direction fields turned around the view getter and put back after it."""
import math, struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../out-test/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


e = Emu(DLL, tempfile.mkdtemp(prefix='eo-dlook-'))
cam = e.alloc(0x200)
near = lambda a, b, d=2e-3: all(abs(x - y) < d for x, y in zip(a, b))


def put(family, eye, d):
    eo, do = (0x190, 0x1b0) if family else (0x1a0, 0x1c0)
    e.mu.mem_write(cam + eo, struct.pack('<3d', *eye))
    e.mu.mem_write(cam + do, struct.pack('<4f', *d, 0.0))


def get(family):
    eo, do = (0x190, 0x1b0) if family else (0x1a0, 0x1c0)
    return (struct.unpack('<3d', bytes(e.mu.mem_read(cam + eo, 24))),
            struct.unpack('<3f', bytes(e.mu.mem_read(cam + do, 12))))


put(0, (10, 2, 20), (0, 0, 1))
e.call('Fh2TestDriveLookSet', 0, 0, 1)
check(e.call('Fh2TestDriveLookApply', cam, 0) == 0, 'ohne Mausbewegung: Kamera unveraendert')
e.call('Fh2TestDriveLookSet', 1571, 0, 1)
check(e.call('Fh2TestDriveLookApply', cam, 0) == 1, 'innen: Blick gedreht')
eye, d = get(0)
check(near(eye, (10, 2, 20)) and near(d, (1, 0, 0)), f'innen 90 Grad rechts: Auge bleibt, Blick nach rechts: {eye} {d}')
e.call('Fh2TestDriveLookSet', 0, 200, 1)
put(0, (10, 2, 20), (0, 0, 1))
e.call('Fh2TestDriveLookApply', cam, 0)
eye, d = get(0)
check(abs(d[1] - math.sin(0.2)) < 2e-3 and abs(d[2] - math.cos(0.2)) < 2e-3, f'innen Maus hoch: schaut hoch: {d}')

# outside: the camera circles the car 6 m in front of it
put(1, (0, 3, 0), (0, 0, 2))
car = e.alloc(16)
e.mu.mem_write(car, struct.pack('<3f', 0, 3, 6))
e.call('Fh2TestDriveLookCar', car)
e.call('Fh2TestDriveLookSet', 1571, 0, 1)
e.call('Fh2TestDriveLookApply', cam, 1)
eye, d = get(1)
check(near(eye, (-6, 3, 6), 5e-3) and near(d, (2, 0, 0), 5e-3), f'aussen 90 Grad: Kamera kreist ums Auto, schaut aufs Auto: {eye} {d}')
# restore puts the game's values back (as after the view getter)
check(e.exports.get('Fh2TestDriveLookApply') is not None, 'Testfunktionen vorhanden')
e.call('Fh2TestDriveLookSet', 3142, 0, -1 & 0xffffffff)
put(1, (0, 3, 0), (0, 0, 1))
e.call('Fh2TestDriveLookApply', cam, 1)
eye, d = get(1)
check(near(eye, (0, 3, 12), 5e-3) and near(d, (0, 0, -1), 5e-3), f'aussen 180 Grad (Spiegel -1): von vorn aufs Auto: {eye} {d}')
e.call('Fh2TestDriveLookSet', 0, -2000, 1)
put(1, (0, 3, 0), (0, 0, 1))
e.call('Fh2TestDriveLookApply', cam, 1)
eye, d = get(1)
check(abs(d[1] + math.sin(0.8)) < 3e-3 and eye[1] > 3, f'aussen nach unten begrenzt (0,8): Kamera hoeher, schaut runter: {eye} {d}')

print(f'{checks[0]} checks, {len(fails)} failed')
print('ALLE TESTS OK' if not fails else f'{len(fails)} FEHLER')
sys.exit(1 if fails else 0)
