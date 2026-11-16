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

print('V7.2 Kopf in die Kurve:')
e.call('Fh2TestDriveLookSet', 0, 0, 1)
h = lambda lvl, spin, along, side=0: e.call('Fh2TestDriveLookHead', lvl, spin, along, side) & 0xffffffff
s32 = lambda v: v - (1 << 32) if v >= 1 << 31 else v
check(s32(h(2, 500, 2000)) == 190, 'mittel, 0,5 rad/s bei 72 km/h: 0,19 rad (0,5 x 0,38 s)')
check(s32(h(2, -500, 2000)) == -190, 'andere Richtung: andere Seite')
check(s32(h(3, 3000, 2000)) == 450, 'stark, sehr enge Kurve: begrenzt auf 0,45 rad')
check(s32(h(0, 500, 2000)) == 0, 'aus: kein Kopf')
check(s32(h(2, 500, 200)) == 0, 'Schrittgeschwindigkeit: kein Kopf')
check(s32(h(2, 500, -2000)) == 0, 'rueckwaerts: kein Kopf')
check(s32(h(2, 500, 2000, 600)) == 0, 'Drift (17 Grad Schlupf): kein Kopf, EO-Driftkamera hat den Blick')
check(0 < s32(h(2, 500, 2000, 300)) < 190, '9 Grad Schlupf: halber Kopf')
h(2, 500, 2000)
put(0, (10, 2, 20), (0, 0, 1))
check(e.call('Fh2TestDriveLookApplyAt', cam, 1) == 1, 'Cockpit-Kamera: Blick gedreht')
eye, d = get(0)
check(near(d, (math.sin(0.19), 0, math.cos(0.19)), 3e-3) and near(eye, (10, 2, 20)), f'Kopf schaut nach rechts in die Kurve: {d}')
put(0, (10, 2, 20), (0, 0, 1))
check(e.call('Fh2TestDriveLookApplyAt', cam, 2) == 0, 'Motorhaube: kein Kopf')
h(2, 0, 2000)

print('Kopf in die Kurve, eigene Werte (15.11.2026):')
hv = lambda w: e.call('Fh2TestDriveLookHeadValue', w)
check([hv(i) for i in range(5)] == [19, 100, 100, 100, 11], f'Mittel als Werte: 19 Grad, 100 %, 100 %, 100 %, 11 km/h: {[hv(i) for i in range(5)]}')
h(1, 0, 2000)
check([hv(i) for i in range(5)] == [11, 58, 100, 100, 11], f'Leicht: 11 Grad, 58 %: {[hv(i) for i in range(5)]}')
h(3, 0, 2000)
check([hv(i) for i in range(5)] == [26, 145, 100, 100, 11], f'Stark: 26 Grad, 145 %: {[hv(i) for i in range(5)]}')
check(s32(h(4, 500, 2000)) == 190, 'Eigene mit Standardwerten = Mittel (0,19 rad)')
e.call('Fh2TestDriveLookHeadOwn', 0, 10)
check(s32(h(4, 500, 2000)) == 174, 'Eigene 10 Grad: begrenzt auf 0,174 rad')
e.call('Fh2TestDriveLookHeadOwn', 0, 45)
e.call('Fh2TestDriveLookHeadOwn', 1, 200)
check(s32(h(4, 500, 2000)) == 380, 'Empfindlichkeit 200 %: doppelt so weit (0,38 rad)')
check(s32(h(4, 3000, 2000)) == 785, 'sehr enge Kurve: 45 Grad (0,785 rad)')
e.call('Fh2TestDriveLookHeadOwn', 4, 30)
check(s32(h(4, 500, 800)) == 0, 'ab 30 km/h: bei 29 km/h kein Kopf')
check(0 < s32(h(4, 500, 1000)) < 380, 'ab 30 km/h: bei 36 km/h teilweise')
check(s32(h(4, 500, 1600)) == 380, 'ab 30 km/h: bei 58 km/h voll')
e.call('Fh2TestDriveLookHeadOwn', 4, 0)
check(s32(h(4, 500, 100)) > 0, 'ab 0 km/h: schon bei 4 km/h etwas')
check(s32(h(4, 500, -2000)) == 0, 'rueckwaerts weiterhin kein Kopf')
e.call('Fh2TestDriveLookHeadOwn', 0, 500)
check(hv(0) == 90, 'Winkel hoechstens 90 Grad')
e.call('Fh2TestDriveLookHeadOwn', 1, 100)
e.call('Fh2TestDriveLookHeadOwn', 4, 11)
h(4, 500, 2000)
e.call('Fh2TestDriveLookHeadOwn', 2, 100)
mid = e.call('Fh2TestDriveLookHeadEase', 0, 6)
e.call('Fh2TestDriveLookHeadOwn', 2, 300)
fast = e.call('Fh2TestDriveLookHeadEase', 0, 6)
e.call('Fh2TestDriveLookHeadOwn', 2, 25)
slow = e.call('Fh2TestDriveLookHeadEase', 0, 6)
check(slow < mid < fast <= 190, f'Tempo in die Kurve: 25 % {slow} < 100 % {mid} < 300 % {fast} mrad nach 0,1 s')
h(4, 0, 2000)
e.call('Fh2TestDriveLookHeadOwn', 3, 300)
back_fast = e.call('Fh2TestDriveLookHeadEase', 190, 6)
e.call('Fh2TestDriveLookHeadOwn', 3, 25)
back_slow = e.call('Fh2TestDriveLookHeadEase', 190, 6)
check(back_fast < back_slow < 190, f'Tempo zurueck eigenes: 300 % {back_fast} < 25 % {back_slow} mrad')
h(2, 0, 2000)
h(4, 500, 2000)
check(e.call('Fh2TestDriveLookHeadEase', 0, 6) == slow, 'Zurueck-Tempo wirkt nicht beim Hineinschauen')
h(2, 500, 2000)
check(e.call('Fh2TestDriveLookHeadEase', 0, 6) == mid, 'Mittel: Tempo wie bisher (eigene Werte wirken nur bei Eigene)')
h(2, 0, 2000)

print(f'{checks[0]} checks, {len(fails)} failed')
print('ALLE TESTS OK' if not fails else f'{len(fails)} FEHLER')
sys.exit(1 if fails else 0)
