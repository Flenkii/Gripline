"""Walking (features/walking.c + noclip.c MODE_WALK) with the compiled test DLL, without a game world
(no collision): getting out at the left of the car, walking, running, looking, distance to the car,
the controller (LS + Y gets out, Y gets in, the game gets nothing while walking)."""
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

    def k_GetProcAddress(self):  # no user32 / DirectInput here: the mouse look reads nothing
        return 0


e = KEmu(DLL, tempfile.mkdtemp(prefix='eo-walk-'))
keys = set()
e.call('Fh2TestKeyboardAsync', e.stub('GetAsyncKeyState', lambda: 0x8000 if (e.arg(0) & 0xff) in keys else 0),
       e.stub('GetForegroundWindow', lambda: 0x1234), e.stub('GetWindowThreadProcessId', lambda: (e.wd(e.arg(1), 4242), 1)[1]),
       e.stub('WindowsGetStringRawBuffer', lambda: e.arg(0)))
e.call('Fh2TestKeyboardSetup', e.stub('RoGetActivationFactory', lambda: 0x80004002), e.put_wstr('C:\\test'))
check('Fh2TestWalkStep' in e.exports, 'Walking-Testfunktion vorhanden')
out = e.alloc(64)
e.call('Fh2TestWalkHandheld', 0)  # steady camera for the exact checks; the handheld one at the end
near = lambda a, b, d=2e-3: all(abs(x - y) < d for x, y in zip(a, b))


def step(ms, reset=0):
    for i in range(max(1, ms // 20)):
        e.call('Fh2TestWalkStep', 0, reset if i == 0 else 0, min(ms, 20), out)
    v = struct.unpack('<8f', bytes(e.mu.mem_read(out, 32)))
    return v[0:3], v[3:6], v[6], v[7]


EYE = 0.5 + 1.12
def gauge():
    g = e.alloc(32)
    e.call('Fh2TestWalkGaugeRaw', g)
    return struct.unpack('<6f', bytes(e.mu.mem_read(g, 24)))


eye, d, dist, kmh = step(20, reset=1)
g = gauge()
check(g[1] == 0 and abs(g[2] - 2.0) < 0.02 and g[4] > 0.95 and abs(g[3]) < 0.1,
      f'V7.1 Tacho: steht, Auto 2 m rechts (Pfeil nach rechts): {g}')
check(near((eye[0], eye[2]), (8.0, 20.0)) and abs(eye[1] - (5 + EYE)) < 1e-3,
      f'aussteigen: 2 m links neben dem Auto, Augen ~1,6 m hoch: {eye}')
check(near(d, (0, 0, 1)) and abs(dist - 2.0) < 0.02, f'schaut wie das Auto, 2 m vom Auto: {d} {dist}')
keys.add(ord('W'))
eye0 = eye
eye, d, dist, kmh = step(100)
check(0.03 < eye[2] - eye0[2] < 0.12, f'V7.1 W: weich anlaufen, nach 0,1 s erst wenige cm: {eye[2] - eye0[2]}')
eye, d, dist, kmh = step(900)
check(1.6 < eye[2] - eye0[2] < 1.9 and abs(eye[0] - eye0[0]) < 0.05, f'W 1 s: knapp 2 m vor (Anlauf): {eye}')
eye0 = eye
eye, d, dist, kmh = step(500)
check(abs(eye[2] - eye0[2] - 1.0) < 0.03, f'dann gleichmaessig 2 m/s: {eye[2] - eye0[2]}')
check(6 <= kmh <= 8, f'Karte: Gehen ~7 km/h: {kmh}')
g = gauge()
check(g[1] == 1 and abs(g[0] - 2.0) < 0.1 and g[3] < -0.8 and g[4] > 0.1,
      f'V7.1 Tacho: geht 2 m/s, Auto hinten rechts: {g}')
check(abs(eye[1] - (5 + EYE)) < 0.06, f'Kopf wippt nur wenig: {eye[1]}')
keys.add(0xa0)
eye0 = eye
eye, d, dist, kmh = step(100)
check(0.2 < eye[2] - eye0[2] < 0.45, f'V7.1 Shift: Gehen -> Rennen weich: {eye[2] - eye0[2]}')
eye, d, dist, kmh = step(900)
eye0 = eye
eye, d, dist, kmh = step(1000)
check(abs(eye[2] - eye0[2] - 6.0) < 0.05, f'Shift: rennen 6 m/s: {eye[2] - eye0[2]}')
check(19 <= kmh <= 23, f'Karte: rennen ~22 km/h: {kmh}')
pulse = []
for i in range(40):
    eye, d, dist, kmh = step(20)
    pulse.append(gauge()[5])
check(gauge()[1] == 2 and max(pulse) > 0.4 and min(pulse) < 0.05,
      f'V7.1 Tacho: rennt, Schritt-Puls schlaegt aus: {gauge()[1]} {min(pulse):.2f}..{max(pulse):.2f}')
keys.clear()
eye0 = eye
eye, d, dist, kmh = step(500)
check(0.4 < eye[2] - eye0[2] < 1.2, f'V7.1 loslassen: rollt kurz aus statt sofort zu stehen: {eye[2] - eye0[2]}')
eye0 = eye
ys = []
for i in range(40):
    eye, d, dist, kmh = step(20)
    ys.append(eye[1])
check(abs(eye[2] - eye0[2]) < 0.01, f'dann steht er: {eye[2] - eye0[2]}')
check(max(abs(a - b) for a, b in zip(ys, ys[1:])) < 0.01 and abs(eye[1] - (5 + EYE)) < 0.003,
      f'V7.1 Wippen blendet weich aus: {eye[1]}')
keys.add(ord('D'))
eye, d, dist, kmh = step(500)
eye0 = eye
eye, d, dist, kmh = step(500)
check(abs(eye[0] - eye0[0] - 1.0) < 0.06 and abs(eye[2] - eye0[2]) < 0.01, f'D: seitlich nach rechts (Achse des Spiels): {eye}')
keys.clear()
step(1000)
keys.add(0xa2)  # LCtrl: crouch
eye0 = eye
eye, d, dist, kmh = step(150)
check(eye0[1] - 0.6 < eye[1] < eye0[1] - 0.2, f'V7.1 Strg: geht weich in die Hocke: {eye[1]}')
eye, d, dist, kmh = step(850)
check(abs(eye[1] - (5 + 0.5 + 0.35 + 0.12)) < 0.02, f'Hocke: Augen ~0,97 m hoch: {eye[1]}')
keys.add(ord('W')); keys.add(0xa0)
eye, d, dist, kmh = step(1000)
eye0 = eye
eye, d, dist, kmh = step(1000)
check(abs(eye[2] - eye0[2] - 1.1) < 0.05, f'Hocke: langsam (1,1 m/s), kein Rennen: {eye[2] - eye0[2]}')
check(gauge()[1] == 3, f'V7.1 Tacho: geduckt (auch mit Shift): {gauge()}')
check(abs(eye[1] - (5 + 0.97)) < 0.03, f'Hocke: wippt nur wenig: {eye[1]}')
keys.clear()
step(1500)
eye, d, dist, kmh = step(500)
check(abs(eye[1] - (5 + EYE)) < 0.01, f'Strg los: steht wieder auf: {eye[1]}')
keys.add(0x25)  # arrow left
eye0 = eye
eye, d, dist, kmh = step(500)
h = math.hypot(d[0], d[2])
ang = math.atan2(-d[0], d[2])
check(near((eye[0], eye[2]), (eye0[0], eye0[2]), 1e-3) and 0.65 < ang < 0.8, f'Pfeil links: dreht weich an nach links, bleibt stehen: {ang}')
keys.clear()
eye, d, dist, kmh = step(500)
ang2 = math.atan2(-d[0], d[2])
check(0.02 < ang2 - ang < 0.15, f'V7.1 Pfeil los: Drehung laeuft kurz aus: {ang2 - ang}')
e.call('Fh2TestWalkMouse', 100, 0)
eye, d, dist, kmh = step(40)
ang3 = math.atan2(-d[0], d[2])
check(0.0 < ang2 - ang3 < 0.18, f'V7.1 Maus rechts: folgt weich, noch nicht ganz: {ang2 - ang3}')
eye, d, dist, kmh = step(500)
ang3 = math.atan2(-d[0], d[2])
check(abs(ang2 - ang3 - 0.18) < 0.003, f'Maus: 100 Counts = 0,18 rad nach rechts: {ang2 - ang3}')
e.call('Fh2TestWalkMouse', 0, -200)
eye, d, dist, kmh = step(500)
check(abs(d[1] - math.sin(0.36)) < 0.005, f'Maus hoch: schaut hoch: {d}')
keys.clear(); keys.add(0x26)  # arrow up
eye, d, dist, kmh = step(2000)
check(abs(d[1] - math.sin(1.4)) < 2e-3, f'Blick hoch begrenzt (80 Grad): {d}')
keys.clear()
eye, d, dist, kmh = step(20, reset=1)
check(abs(dist - 2.0) < 0.02, f'neu aussteigen: wieder neben dem Auto: {dist}')
hid = e.alloc(16)
check(e.call('Fh2TestWalkHidden', hid) == 0, 'nah am Auto: Auto bleibt an seinem Platz')
keys.add(ord('W')); keys.add(0xa0)
eye, d, dist, kmh = step(8000)
v = struct.unpack('<3f', bytes(e.mu.mem_read(hid, 12)))
check(dist > 45 and e.call('Fh2TestWalkHidden', hid) == 1, f'weiter als 45 m: Auto geht unter dem Spieler mit: {dist}')
v = struct.unpack('<3f', bytes(e.mu.mem_read(hid, 12)))
check(near((v[0], v[2]), (eye[0], eye[2]), 0.2) and v[1] < eye[1] - 25, f'Auto unter dem Spieler im Boden: {v} {eye}')
keys.clear(); keys.add(ord('S')); keys.add(0xa0)
eye, d, dist, kmh = step(4500)
check(dist < 40 and e.call('Fh2TestWalkHidden', hid) == 0, f'zurueck naeher als 40 m: Auto wieder am Platz: {dist}')
keys.clear()
eye, d, dist, kmh = step(20, reset=1)
keys.add(0x20)
eye0 = eye
eye, d, dist, kmh = step(200)
check(abs(eye[1] - eye0[1]) < 1e-3, 'ohne Kollisionswelt: kein Springen, bleibt auf Autohoehe')
keys.clear()

# V7.1 handheld camera: standing still the look drifts a little, slowly, and stays near the middle
keys.clear()
eye, d, dist, kmh = step(20, reset=1)
e.call('Fh2TestWalkHandheld', 2)
looks = []
for i in range(150):
    eye, d, dist, kmh = step(20)
    looks.append(d)
dev = max(math.degrees(math.acos(max(-1.0, min(1.0, l[2] / math.hypot(*l))))) for l in looks)
jump = max(math.degrees(math.acos(max(-1.0, min(1.0, sum(a * b for a, b in zip(p, q)) / (math.hypot(*p) * math.hypot(*q))))))
           for p, q in zip(looks, looks[1:]))
check(0.05 < dev < 2.5, f'Handkamera Mittel: Blick wandert leicht (max {dev:.2f} Grad)')
check(jump < 0.1, f'Handkamera: ohne Ruckeln (max {jump:.3f} Grad pro Bild)')
keys.add(ord('W'))
step(1000)
ys, looks = [], []
for i in range(250):
    eye, d, dist, kmh = step(20)
    ys.append(eye[1]); looks.append(d)
lows = [ys[i] for i in range(1, len(ys) - 1) if ys[i] < ys[i - 1] and ys[i] <= ys[i + 1]]
check(len(lows) >= 6 and max(lows) - min(lows) > 0.004, f'V7.1b Handkamera: jeder Schritt anders (Tiefpunkte {len(lows)}, Spanne {max(lows) - min(lows):.4f} m)')
jump = max(math.degrees(math.acos(max(-1.0, min(1.0, sum(a * b for a, b in zip(p, q)) / (math.hypot(*p) * math.hypot(*q))))))
           for p, q in zip(looks, looks[1:]))
check(jump < 0.3, f'Handkamera beim Gehen: ohne Ruckeln (max {jump:.3f} Grad pro Bild)')
keys.clear()
e.call('Fh2TestWalkHandheld', 0)

# controller (XINPUT_GAMEPAD as the game's controller hook gives it)
pad = e.alloc(16)
def gp(buttons=0, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0, user=0):
    e.mu.mem_write(pad, struct.pack('<HBBhhhh', buttons, lt, rt, lx, ly, rx, ry))
    return e.call('Fh2TestNoclipPad', user, pad), struct.unpack('<HBBhhhh', bytes(e.mu.mem_read(pad, 12)))
e.call('Fh2TestNoclipMode', 0)
gp()
check(gp(0x8000)[0] == 0 and gp(0x8000)[1][0] == 0x8000, 'Y ohne LS geht normal ans Spiel (Zurueckspulen)')
gp(0x40)
req, st = gp(0x40 | 0x8000)
check(req == 3 and st[0] == 0x40, f'LS + Y = Walking, Y bleibt vom Spiel weg: {req} {st[0]:#x}')
e.call('Fh2TestNoclipMode', 3)
gp()
req, st = gp(0x8000 | 0x1000 | 0x10, lt=200, rt=200, lx=30000, ly=30000, rx=30000, ry=-30000)
check(req == 3, f'beim Laufen: Y = einsteigen: {req}')
check(st[0] == 0x10 and st[1:] == (0, 0, 0, 0, 0, 0), f'beim Laufen bekommt das Spiel nur Start/Back: {st}')
e.call('Fh2TestNoclipMode', 0)

print(f'{checks[0]} checks, {len(fails)} failed')
print('ALLE TESTS OK' if not fails else f'{len(fails)} FEHLER')
sys.exit(1 if fails else 0)
