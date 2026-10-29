"""Second cockpit view (features/cockpit_view.c): adds back/up to the driver camera offset at +0x190."""
import os, struct, sys, tempfile
from emu import Emu, GAME

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
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

print('Kamerawechsel: Cockpit -> Cockpit 2 -> nächste Kamera')
btn = lambda held: e.call('Fh2TestCockpitCameraButton', held)
e.tick = 10000
e.call('Fh2TestCockpitView', cam, 0, 35, -5)  # driver camera active now, normal view
put(0.0, 0.4, -0.5); e.call('Fh2TestCockpitView', cam, 0, 35, -5)
check(btn(1) == 0b11, 'Fahrerkamera aktiv: Druck wird geschluckt, Cockpit 2 an')
check(btn(1) == 0b11, 'gehalten: bleibt geschluckt')
check(btn(0) == 0b10, 'losgelassen')
e.call('Fh2TestCockpitView', cam, 0, 35, -5)
check(near(get(), (0.0, 0.35, -0.15)), f'Cockpit 2: 35 cm vor, 5 cm runter {get()}')
check(btn(1) == 0b00, 'zweiter Druck: geht ans Spiel (nächste Kamera), Cockpit 2 aus')
btn(0)
e.call('Fh2TestCockpitView', cam, 0, 35, -5)
check(near(get(), (0.0, 0.4, -0.5)), f'Versatz wieder original {get()}')
e.tick += 5000  # another camera for 5 s
check(btn(1) == 0b00, 'andere Kamera aktiv: Druck geht ans Spiel'); btn(0)
e.call('Fh2TestCockpitView', cam, 0, 35, -5)  # back in the cockpit
e.call('Fh2TestCockpitView', cam, 1, 35, -5)  # V
check(btn(0) & 2, 'V schaltet Cockpit 2 an')
e.tick += 5000
e.call('Fh2TestCockpitView', cam, 0, 35, -5)
check(not (btn(0) & 2), 'nach einer anderen Kamera zurück: wieder normales Cockpit')

print('Hook am Spiel (Aufrufe 0x106fae6 / 0x1075580)')
class HEmu(Emu):
    def mapped(self, address):
        return any(b <= address <= e for b, e, _ in self.mu.mem_regions())
    def k_VirtualQuery(self):
        addr, info = self.arg(0), self.arg(1)
        self.mu.mem_write(info, bytes(48)); base = addr & ~0xfff
        self.wq(info, base); self.wq(info + 8, base); self.wq(info + 24, 0x1000)
        self.wd(info + 32, 0x1000 if self.mapped(addr) else 0x10000)
        return 48
    def k_VirtualAlloc(self):
        addr, size = self.arg(0), self.arg(1)
        if addr == 0 or self.mapped(addr): return 0
        self.mu.mem_map(addr, (size + 0xfff) & ~0xfff); return addr
    def k_VirtualProtect(self):
        if self.arg(3): self.wd(self.arg(3), 0x20)
        return 1
    def k_GetSystemInfo(self):
        out = self.arg(0); self.mu.mem_write(out, bytes(48)); self.wd(out + 4, 0x1000); self.wd(out + 0x28, 0x10000)
    def k_FlushInstructionCache(self): return 1
    def k_GetCurrentProcess(self): return 0xffffffffffffffff

h = HEmu(DLL, tempfile.mkdtemp(prefix='eo-view2-'))
pad = {'buttons': 0}
def game_get_state():
    h.mu.mem_write(h.arg(2), struct.pack('<IH', 1, pad['buttons'])); return 0
h.stub('GetState', game_get_state, at=GAME + 0x106c320)
for site in (0x106fae6, 0x1075580):
    h.mu.mem_write(GAME + site, b'\xe8' + struct.pack('<i', 0x106c320 - (site + 5)))
state = h.alloc(16)
r = h.call('Fh2TestCockpitStateHook', GAME, state)
check(r == 0, f'Hook gesetzt, Spiel-Funktion aufgerufen ({r:#x})')
for site in (0x106fae6, 0x1075580):
    rel = struct.unpack('<i', h.mu.mem_read(GAME + site + 1, 4))[0]
    check(rel != 0x106c320 - (site + 5), f'Aufruf {site:#x} geht jetzt über Gripline')
camera_bit = 1 << h.call('Fh2TestControllerActionInput', 17)
h.tick = 1000
c2 = h.alloc(0x200); h.call('Fh2TestCockpitView', c2, 0, 35, -5)
pad['buttons'] = camera_bit | 0x1000
h.call('Fh2TestCockpitStateHook', GAME, state)
b = struct.unpack('<H', h.mu.mem_read(state + 4, 2))[0]
check(b == 0x1000, f'Kamera-Taste im Cockpit geschluckt, A bleibt ({b:#x})')
pad['buttons'] = 0; h.call('Fh2TestCockpitStateHook', GAME, state)
pad['buttons'] = camera_bit; h.call('Fh2TestCockpitStateHook', GAME, state)
b = struct.unpack('<H', h.mu.mem_read(state + 4, 2))[0]
check(b == camera_bit, f'zweiter Druck geht ans Spiel ({b:#x})')

print(f'\n{len(fails)} FEHLER' if fails else '\nalles ok')
sys.exit(1 if fails else 0)
