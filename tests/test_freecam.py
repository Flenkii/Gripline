"""Freecam flight (features/noclip.c, mode MODE_FREECAM) with the compiled test DLL: the camera point
moves with the keys, the car is always behind it, turning and looking up/down turn about the camera."""
import math, struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


class KEmu(Emu):
    def k_GetCurrentProcessId(self):
        return 4242


e = KEmu(DLL, tempfile.mkdtemp(prefix='eo-freecam-'))
keys = set()
e.call('Fh2TestKeyboardAsync', e.stub('GetAsyncKeyState', lambda: 0x8000 if (e.arg(0) & 0xff) in keys else 0),
       e.stub('GetForegroundWindow', lambda: 0x1234), e.stub('GetWindowThreadProcessId', lambda: (e.wd(e.arg(1), 4242), 1)[1]),
       e.stub('WindowsGetStringRawBuffer', lambda: e.arg(0)))
e.call('Fh2TestKeyboardSetup', e.stub('RoGetActivationFactory', lambda: 0x80004002), e.put_wstr('C:\\test'))
check('Fh2TestFreecamStep' in e.exports, 'Freecam-Testfunktion vorhanden')
out = e.alloc(64)
AHEAD, UP = 0.3 + 3.5, 0.6


def step(ms, reset=0):
    for i in range(max(1, ms // 50)):
        e.call('Fh2TestFreecamStep', reset if i == 0 else 0, min(ms, 50), out)
    v = struct.unpack('<12f', bytes(e.mu.mem_read(out, 48)))
    return v[0:3], v[3:6], v[6:9], v[9:12]


near = lambda a, b, d=1e-3: all(abs(x - y) < d for x, y in zip(a, b))
dot = lambda a, b: sum(x * y for x, y in zip(a, b))


def behind(p, c, f, u):  # the car stays where the camera point says
    return near(p, [c[i] - f[i] * AHEAD - u[i] * UP for i in range(3)])


p, c, f, u = step(16, reset=1)
check(near(p, (0, 0, 0)) and near(c, (0, UP, AHEAD)), f'ohne Taste: Auto bleibt stehen, Kamera vor dem Auto: {p} {c}')
keys.add(ord('W'))
p, c, f, u = step(500)
check(near(c, (0, UP, AHEAD + 4.0)) and behind(p, c, f, u), f'W 0,5 s: Kamera 4 m vor, Auto dahinter: {c} {p}')
keys.clear(); keys.add(ord('D'))
p, c, f, u = step(500)
check(near(c, (4.0, UP, AHEAD + 4.0)), f'D: seitlich nach rechts (Achse des Spiels): {c}')
keys.clear(); keys.add(0x20)
p, c, f, u = step(1000)
check(near(c, (4.0, UP + 5.0, AHEAD + 4.0)), f'Leertaste 1 s: 5 m hoch: {c}')
keys.clear(); keys.add(0x26)  # arrow up
c0 = c
p, c, f, u = step(500)
check(near(c, c0) and abs(f[1] - math.sin(0.6)) < 1e-3, f'Pfeil hoch: schaut nach oben, Kamera bleibt: {f}')
check(behind(p, c, f, u) and abs(dot(f, u)) < 1e-4 and abs(dot(f, f) - 1) < 1e-4 and abs(dot(u, u) - 1) < 1e-4,
      'Auto dreht um die Kamera, Achsen rechtwinklig')
p, c, f, u = step(2000)
check(abs(f[1] - math.sin(1.4)) < 1e-3 and u[1] > 0.1, f'Blick hoch begrenzt (80 Grad), nie auf dem Dach: {f}')
keys.clear(); keys.add(ord('W'))
c0 = c
p, c, f, u = step(500)
check(near([c[i] - c0[i] for i in range(3)], [x * 4.0 for x in f]), f'W fliegt in Blickrichtung (auch nach oben): {c}')
keys.clear(); keys.add(0x28)  # arrow down
step(1200)
keys.clear(); keys.add(0x25)  # arrow left
c0 = c
p, c, f, u = step(500)
h = math.hypot(f[0], f[2])
check(near(c, c0) and near((f[0] / h, f[2] / h), (-math.sin(0.6), math.cos(0.6)), 2e-3),
      f'Pfeil links: nach links gedreht (weg von der rechten Achse), Kamera bleibt: {f}')
check(behind(p, c, f, u), 'Auto bleibt hinter der Kamera')
keys.clear(); keys.add(ord('W')); keys.add(0xa0)
c0 = c
p, c, f, u = step(500)
check(near([c[i] - c0[i] for i in range(3)], [x * 16.0 for x in f], 2e-3), 'LShift: 4x schneller')


# controller (XINPUT_GAMEPAD as the game's controller hook gives it)
pad = e.alloc(16)
def gp(buttons=0, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0, user=0):
    e.mu.mem_write(pad, struct.pack('<HBBhhhh', buttons, lt, rt, lx, ly, rx, ry))
    return e.call('Fh2TestNoclipPad', user, pad), struct.unpack('<HBBhhhh', bytes(e.mu.mem_read(pad, 12)))
keys.clear()
check(gp()[0] == 0 and gp(0x40)[0] == 0, 'Controller: nur LS gedrueckt fragt nichts an')
req, st = gp(0x41)
check(req == 1 and st[0] == 0x40, f'LS + Steuerkreuz hoch = Noclip, Steuerkreuz bleibt vom Spiel weg: {req} {st[0]:#x}')
check(gp(0x41)[0] == 0, 'gehalten: nur einmal')
gp(0x40)
req, st = gp(0x42)
check(req == 2 and st[0] == 0x40, f'LS + Steuerkreuz runter = Freecam: {req}')
check(gp(0x02)[1][0] == 0x02 and gp(0x01)[0] == 0, 'Steuerkreuz ohne LS geht normal ans Spiel')
p, c, f, u = step(16, reset=1)
req, st = gp(rx=20000, ry=-15000)
check(st[5] == 0 and st[6] == 0, f'Freecam: rechter Stick geht nicht an die Spiel-Kamera: {st}')
c0 = c
p, c, f, u = step(500)
check(near(c, c0) and f[1] < -0.1 and f[0] > 0.05,
      f'rechter Stick rechts/unten: schaut nach rechts und runter, Kamera bleibt: {f}')
gp(ly=32767)
c0 = c
p, c, f, u = step(500)
check(near([c[i] - c0[i] for i in range(3)], [x * 4.0 for x in f]), f'linker Stick voll vor: fliegt wie W: {c}')
gp(ly=4000)
c0 = c
p, c, f, u = step(500)
check(near(c, c0), 'Stick in der Totzone: bleibt stehen')
gp(rt=255)
c0 = c
p, c, f, u = step(1000)
check(abs(c[1] - c0[1] - 5.0) < 1e-3, f'RT: 5 m/s hoch: {c[1] - c0[1]}')
gp(lt=255, buttons=0x1000)
c0 = c
p, c, f, u = step(500)
check(abs(c[1] - c0[1] + 10.0) < 1e-3, f'LT + A: 4x schneller runter: {c[1] - c0[1]}')
gp()

print('ALLE TESTS OK (%d Pruefungen)' % checks[0] if not fails else '%d FEHLER' % len(fails))
sys.exit(1 if fails else 0)
