"""Second cockpit view (features/cockpit_view.c): adds back/up to the driver camera offset at +0x190."""
import struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
fails = []

def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)

e = Emu(DLL, tempfile.mkdtemp(prefix='eo-view-'))
cam = e.alloc(0x200)
f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
def put(x, y, z): e.mu.mem_write(cam + 0x190, struct.pack('<3f', x, y, z))
def get(): return struct.unpack('<3f', e.mu.mem_read(cam + 0x190, 12))
near = lambda a, b: all(abs(p - q) < 1e-5 for p, q in zip(a, b))

put(0.1, 0.4, -0.3)
e.call('Fh2TestCockpitView', cam, 0, 20, 5)
check(near(get(), (0.1, 0.4, -0.3)), f'normal view: offset unchanged {get()}')
e.call('Fh2TestCockpitView', cam, 1, 20, 5)
check(near(get(), (0.1, 0.45, -0.1)), f'second view: 20 cm forward, 5 cm up {get()}')
e.call('Fh2TestCockpitView', cam, 0, 20, 5)
check(near(get(), (0.1, 0.45, -0.1)), f'next frame: stays, nothing added twice {get()}')
e.call('Fh2TestCockpitView', cam, 0, 40, -10)
check(near(get(), (0.1, 0.3, 0.1)), f'menu change 40 forward / -10 up applies live {get()}')
put(0.2, 0.5, -0.1)  # the game sets the camera up again (other car)
e.call('Fh2TestCockpitView', cam, 0, 40, -10)
check(near(get(), (0.2, 0.4, 0.3)), f'new car base taken, view added on top {get()}')
e.call('Fh2TestCockpitView', cam, 1, 40, -10)
check(near(get(), (0.2, 0.5, -0.1)), f'toggled back: the car\'s own offset again {get()}')
cam2 = e.alloc(0x200)
e.mu.mem_write(cam2 + 0x190, struct.pack('<3f', 0, 0.3, 0))
e.call('Fh2TestCockpitView', cam2, 0, 40, -10)
check(struct.unpack('<3f', e.mu.mem_read(cam2 + 0x190, 12)) == (0.0, f32(0.3), 0.0), 'another camera: normal view untouched')

print(f'\n{len(fails)} FEHLER' if fails else '\nalles ok')
sys.exit(1 if fails else 0)
