"""Cruise control (features/cruise.c, V7 fix 06.11.): on/off, holds the speed through the controller
state, stays on when the throttle is lifted at high speed, brake and crash switch it off."""
import struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
fails, checks = [], [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


e = Emu(DLL, tempfile.mkdtemp(prefix='gl-cruise-'))
f32 = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]


def sample(speed, dt=1 / 60, frames=1):
    for _ in range(frames):
        e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, f32(dt))
        e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM1, f32(speed))
        e.call('Fh2TestCruiseSample')


def step():
    return e.call('Fh2TestCruiseStep') & 0xffffffff


def pad(rt=0, lt=0):
    return e.call('Fh2TestCruisePad', rt, lt) & 0xff


sample(3.0, frames=10)
e.call('Fh2TestCruiseToggle')
check(step() == 0, 'unter 20 km/h: geht nicht an')
sample(30.0, frames=30)  # 108 km/h
e.call('Fh2TestCruiseToggle')
check(step() == 1, 'Controller LS + Steuerkreuz links: an bei 108 km/h')
check(pad(0) > 60, f'gibt Gas ueber den Controller-Zustand (RT): {pad(0)}')
# lifting the throttle at speed: the car slows by 6 m/s^2 for a moment (the old limit 4.5 switched off)
v = 30.0
for _ in range(30):
    v -= 6.0 / 60
    sample(v)
check(step() == 1, f'Gas weg bei hohem Tempo (-6 m/s2): bleibt an (jetzt {v * 3.6:.0f} km/h)')
check(pad(0) == 255, f'zu langsam: Vollgas zum Aufholen: {pad(0)}')
# speed back: throttle goes down again
sample(30.5, frames=120)
t = pad(0)
check(0 < t < 200, f'am Ziel: wenig Gas ({t})')
check(pad(255) == 255, 'Spieler gibt selbst Vollgas: bleibt Vollgas')
sample(33.0, frames=60)
pad(255)
sample(33.0, frames=5)
pad(0)
sample(31.0, frames=60)
check(step() == 1 and pad(0) > t, f'nach dem eigenen Gas: hoeheres Tempo (33 m/s) wird gehalten, mehr Gas bei 31 m/s ({pad(0)} > {t})')
pad(0, 200)
check(step() == 0, 'Bremse: aus')
check(pad(0) == 0, 'aus: kein Gas mehr')
sample(30.0, frames=10)
e.call('Fh2TestCruiseToggle'); step()
sample(30.0, frames=90)
for _ in range(10):
    sample(30.0 - 2.0 * (_ + 1))  # 120 m/s^2: a crash
check(step() == 0, 'Aufprall: aus')
e.call('Fh2TestCruiseToggle')
sample(30.0, frames=30)
step(); e.call('Fh2TestCruiseToggle')
check(step() == 0, 'Taste nochmal: aus')

# CruiseSlowOff=0: almost stopped (stuck at a wall) keeps it on, full throttle
e.call('Fh2TestCruiseSlowOff', 0)
sample(30.0, frames=30)
e.call('Fh2TestCruiseToggle'); step()
v = 30.0
for _ in range(240):
    v = max(1.0, v - 0.2)
    sample(v)
check(step() == 1 and pad(0) == 255, f'Langsam-Abschaltung aus: bleibt an bei 4 km/h, Vollgas ({pad(0)})')
pad(0, 200)
check(step() == 0, 'Bremse schaltet trotzdem aus')
e.call('Fh2TestCruiseSlowOff', 1)

print(f'\n{checks[0]} checks, {len(fails)} failed')
sys.exit(1 if fails else 0)
