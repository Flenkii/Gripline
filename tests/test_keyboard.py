"""Test of the keyboard as a gamepad (features/keyboard_pad.c) with the real compiled test DLL.

Fake Windows.Gaming.Input objects (statics, vector view, one real gamepad) are built in emulated
memory; the DLL's hook of Azimuth's RoGetActivationFactory hands out the merged virtual pad."""
import os, struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


class KEmu(Emu):
    qpc = 1_000_000  # 1 MHz -> microseconds

    def k_QueryPerformanceCounter(self):
        self.wq(self.arg(0), self.qpc)
        return 1

    def k_QueryPerformanceFrequency(self):
        self.wq(self.arg(0), 1_000_000)
        return 1

    def k_GetCurrentProcessId(self):
        return 4242


root = tempfile.mkdtemp(prefix='eo-keys-')
e = KEmu(DLL, root)

def advance(ms):
    e.qpc += int(ms * 1000)
    e.tick += int(ms)

# ---- fake WGI: real statics / view / pad
real = {'reading': [0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0], 'vib': [], 'gets': 0, 'factory_calls': [], 'pads': 1}

def obj(methods):
    """object with a vtable of Python stubs"""
    o = e.alloc(16)
    vt = e.alloc(8 * len(methods))
    for i, m in enumerate(methods):
        e.wq(vt + 8 * i, e.stub('m%d' % i, m))
    e.wq(o, vt)
    return o

ok_ = lambda: 0
def reading():
    out = e.arg(1)
    r = real['reading']
    e.mu.mem_write(out, struct.pack('<QII6d', 777, r[1], 0, *r[2:]))
    return 0
def put_vib():
    real['vib'].append(struct.unpack('<4d', bytes(e.mu.mem_read(e.arg(1), 32))))
    return 0
real_pad = obj([ok_, ok_, ok_, ok_, ok_, ok_, ok_, put_vib, reading])
def view_get_at():
    e.wq(e.arg(2), real_pad)
    return 0
def view_size():
    e.wd(e.arg(1), real['pads'])
    return 0
real_view = obj([ok_, ok_, ok_, ok_, ok_, ok_, view_get_at, view_size])
def get_pads():
    real['gets'] += 1
    e.wq(e.arg(1), real_view)
    return 0
real_statics = obj([ok_, ok_, ok_, ok_, ok_, ok_, ok_, ok_, ok_, ok_, get_pads])
def original_factory():
    name = e.wstr(e.arg(0))
    real['factory_calls'].append(name)
    e.wq(e.arg(2), real_statics if name == 'Windows.Gaming.Input.Gamepad' else 0x5151)
    return 0
orig = e.stub('RoGetActivationFactory', original_factory)

keys = set()
focus = {'pid': 4242}
def async_key():
    return 0x8000 if (e.arg(0) & 0xff) in keys else 0
def foreground():
    return 0x1234
def window_pid():
    if e.arg(1):
        e.wd(e.arg(1), focus['pid'])
    return 1
def raw_buffer():
    s = e.wstr(e.arg(0))
    if e.arg(1):
        e.wd(e.arg(1), len(s))
    return e.arg(0)
e.call('Fh2TestKeyboardAsync', e.stub('GetAsyncKeyState', async_key), e.stub('GetForegroundWindow', foreground),
       e.stub('GetWindowThreadProcessId', window_pid), e.stub('WindowsGetStringRawBuffer', raw_buffer))
e.call('Fh2TestKeyboardSetup', orig, e.put_wstr('C:\\test'))
ini = open(os.path.join(root, 'FH2_Keyboard.ini'), 'rb').read()
check(b'[Keyboard]' in ini and b'Throttle=W' in ini and b'Handbrake=Space' in ini and b'Enabled=1' in ini,
      'FH2_Keyboard.ini (englisch) mit FH5-Belegung angelegt')
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import make_release
check(make_release.keyboard_ini().encode('ascii') == ini, 'Release-Profile (tools/make_release.py) = genau die Datei, die die DLL anlegt')
check(e.call('Fh2TestKeyboardBinding', 0, 0) == ord('W'), 'Gas = W')
check(e.call('Fh2TestKeyboardBinding', 14, 0) == ord('C') and e.call('Fh2TestKeyboardBinding', 14, 1) == 0x62,
      'Blick zurück = C und Num2')

IID_STATICS = bytes.fromhex('29e5bc8b9cd4e9399560e47dde96b7c8')
IID_OTHER = bytes(16)
hook = e.call('Fh2TestKeyboardFactoryHook')
out = e.alloc(8)

def factory(name, iid):
    e.wq(out, 0)
    hr = e.call(hook, e.put_wstr(name), e.put_bytes(iid), out)
    return hr & 0xffffffff, e.rq(out)

hr, p = factory('Windows.Storage.ApplicationData', IID_OTHER)
check(hr == 0 and p == 0x5151 and real['factory_calls'] == ['Windows.Storage.ApplicationData'], 'andere Klassen gehen unverändert durch')
hr, statics = factory('Windows.Gaming.Input.Gamepad', IID_STATICS)
check(hr == 0 and statics not in (0, real_statics), 'Gamepad-Klasse: Azimuth bekommt den gemeinsamen Controller')
check(real['factory_calls'][-1] == 'Windows.Gaming.Input.Gamepad', 'echte Gamepads werden weiter von Windows geholt')

def vcall(o, index, *args):
    return e.call(e.rq(e.rq(o) + 8 * index), o, *args) & 0xffffffff

view_out = e.alloc(8)
check(vcall(statics, 10, view_out) == 0, 'get_Gamepads')
view = e.rq(view_out)
size = e.alloc(4)
vcall(view, 7, size)
check(e.rd(size) == 1, 'genau ein Controller in der Liste')
pad_out = e.alloc(8)
vcall(view, 6, 0, pad_out)
pad = e.rq(pad_out)
check(vcall(view, 6, 1, pad_out) == 0x8000000b, 'zweiter Eintrag: E_BOUNDS')
token = e.alloc(8)
handler = obj([ok_, ok_, ok_, ok_])
check(vcall(statics, 6, handler, token) == 0 and e.rq(token) != 0, 'GamepadAdded-Handler angenommen')
check(vcall(statics, 7, e.rq(token)) == 0, 'GamepadAdded-Handler abgemeldet')

# ---- readings
r_out = e.alloc(64)
def read():
    vcall(pad, 8, r_out)
    ts, buttons, _, lt, rt, lx, ly, rx, ry = struct.unpack('<QII6d', bytes(e.mu.mem_read(r_out, 64)))
    return dict(buttons=buttons, lt=round(lt, 3), rt=round(rt, 3), lx=round(lx, 3), ly=round(ly, 3),
                rx=round(rx, 3), ry=round(ry, 3), ts=ts)

real['reading'] = [0, 4 | 1024, 0.5, 0.0, 0.3, -0.2, 0.0, 0.0]  # A + LB, LT 0.5, LS
r = read()
check(r['buttons'] == 4 | 1024 and r['lt'] == 0.5 and r['lx'] == 0.3 and r['ly'] == -0.2, f'echtes Gamepad geht unverändert durch: {r}')
real['reading'] = [0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]

keys.add(ord('W'))
read()
advance(30)
r1 = read()
advance(30)
r2 = read()
check(r1['rt'] == 0.5 and r2['rt'] == 1.0, f'W = Gas (RT), sanft in 60 ms: {r1["rt"]} -> {r2["rt"]}')
keys.discard(ord('W'))
advance(15)
check(read()['rt'] == 0.5, 'Gas loslassen geht doppelt so schnell zurück')
advance(40)
read()

keys.add(ord('A'))
advance(75)
r1 = read()
advance(75)
r2 = read()
check(r1['lx'] == -0.5 and r2['lx'] == -1.0, f'A = links lenken, voller Einschlag in 150 ms: {r1["lx"]}, {r2["lx"]}')
keys.discard(ord('A'))
advance(45)
check(read()['lx'] == -0.5, 'Rückstellung in 90 ms (halb nach 45 ms)')
advance(100)
check(read()['lx'] == 0.0, 'Lenkung wieder gerade')

for vk, name, bit in ((0x20, 'Leertaste = Handbremse (A im Layout 0)', 4), (0x0d, 'Enter = A', 4), (0x1b, 'Esc = Menu', 1),
                      (0x08, 'Rück = B', 8), (ord('E'), 'E = Hochschalten (B im Layout 0)', 8),
                      (ord('Q'), 'Q = Runterschalten (X im Layout 0)', 16), (ord('R'), 'R = Zurückspulen (Y)', 32),
                      (0x09, 'Tab = Kamera (RB)', 2048), (0xa0, 'LShift = Kupplung (LB)', 1024), (ord('M'), 'M = Karte (View)', 2),
                      (ord('P'), 'P = Fotomodus (Steuerkreuz hoch)', 64), (ord('T'), 'T = Telemetrie (Steuerkreuz runter)', 128),
                      (ord('L'), 'L = Licht (RS-Klick)', 8192)):
    keys.clear(); keys.add(vk)
    check(read()['buttons'] == bit, name)
keys.clear(); keys.add(ord('S'))
advance(100)
check(read()['lt'] == 1.0, 'S = Bremse (LT)')
keys.clear(); keys.add(0x26)
check(read()['ly'] == 1.0, 'Pfeil hoch = linker Stick hoch (Menüs)')
keys.clear(); keys.add(0x63 - 1)  # Num2
check(read()['ry'] == -1.0, 'Num2 = Blick zurück (rechter Stick runter)')
keys.clear(); keys.add(0x22)
check(read()['buttons'] == 2048, 'Bild runter = RB (Tab rechts)')

# focus
keys.clear(); keys.add(0x0d)
focus['pid'] = 1
advance(100)
check(read()['buttons'] == 0, 'Spiel nicht im Vordergrund: Tastatur wirkt nicht')
focus['pid'] = 4242
advance(100)
keys.clear()

# combined with the real pad
real['reading'] = [0, 8, 0.0, 0.2, 0.0, 0.0, 0.0, 0.0]
keys.add(0x0d)
advance(100)
r = read()
check(r['buttons'] == 8 | 4 and r['rt'] == 0.2, f'Gamepad + Tastatur gleichzeitig: Tasten zusammen ({r["buttons"]})')
keys.clear()
real['reading'] = [0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]

# arrows: menus = left stick, driving = camera (right stick)
from emu import GAME
manager, simulation = e.alloc(0x100), e.alloc(0x100)
e.wq(GAME + 0x34aba60, manager)
e.wq(manager + 0x48, simulation)
e.mu.mem_write(simulation + 0x28, b'\x01')  # paused (pause menu)
e.call('Fh2TestKeyboardGame', GAME)
keys.clear(); keys.add(0x25)  # left arrow
advance(150)
r = read()
check(r['lx'] == -1.0 and r['rx'] == 0.0, f'Pause-Menü: Pfeil links = linker Stick ({r["lx"]}, {r["rx"]})')
e.mu.mem_write(simulation + 0x28, b'\x00')  # driving
advance(150)
r = read()
check(r['rx'] == -1.0 and r['lx'] == 0.0, f'beim Fahren: Pfeil links = Kamera links, lenkt nicht ({r["rx"]}, {r["lx"]})')
keys.clear(); keys.add(0x26)
advance(10)
r = read()
check(r['ry'] == 1.0 and r['ly'] == 0.0, f'beim Fahren: Pfeil hoch = Kamera hoch ({r["ry"]}, {r["ly"]})')
keys.clear(); keys.add(0x41)  # A steers while driving
advance(100); read(); advance(100)
check(read()['lx'] == -1.0, 'A lenkt weiter beim Fahren')
e.wq(GAME + 0x34aba60, 0)  # no world loaded (main menu)
keys.clear(); keys.add(0x28)  # down arrow
advance(150)
r = read()
check(r['ly'] == -1.0 and r['ry'] == 0.0, 'Hauptmenü (keine Spielwelt): Pfeil runter = linker Stick')
keys.clear()
advance(300)
read()

# ---- key list panel (Keybinds = F5): shown until F5 again, the pad reading is unchanged
def panel():
    w, rows, count = e.alloc(4), e.alloc(4), e.alloc(4)
    label, keys_ = e.alloc(64), e.alloc(64)
    e.call('Fh2TestNotifyPanelDraw', 1080, 0, 0, w, rows, count, label, keys_)
    return e.rd(count), e.wstr(label), e.wstr(keys_), e.rd(w), e.rd(rows)
keys.clear()
read()
check(panel()[0] == 0, 'Tastenliste anfangs aus')
keys.add(0x74)
r = read()
count, label, kt, width, rows = panel()
check(r['buttons'] == 0 and r['lx'] == 0.0, 'F5 drueckt nichts im Spiel')
check(count == 35 and label == 'Throttle' and kt == 'W', f'F5: Tastenliste mit allen belegten Tasten + Autodrive: {count} {label}={kt}')
def panel_rows():
    out = {}
    for i in range(count):
        l, k = e.alloc(64), e.alloc(64)
        e.call('Fh2TestNotifyPanelRow', i, l, k)
        out[e.wstr(l)] = e.wstr(k)
    return out
rw = panel_rows()
check(rw.get('Look back') == 'C, Num 2' and rw.get('Radio: previous') == 'Minus' and rw.get('Clutch') == 'Left Shift'
      and rw.get('Telemetry (hold)') == 'T' and rw.get('Photo mode (hold)') == 'P' and rw.get('Key list on/off') == 'F5'
      and rw.get('Autodrive') == 'F2' and rw.get('Change camera') == 'Tab', f'Tastennamen lesbar: {rw}')
read()
check(panel()[0] == count, 'F5 halten: bleibt an (nur Druck schaltet)')
keys.clear()
read()
check(panel()[0] == count, 'F5 loslassen: bleibt an')
check(width > 0 and rows <= 1080, f'Tastenliste passt in 1080 Zeilen ({rows})')
keys.add(0x74)
read()
keys.clear()
read()
check(panel()[0] == 0, 'F5 nochmal: Tastenliste aus')

# vibration
vib = e.alloc(32)
e.mu.mem_write(vib, struct.pack('<4d', 0.25, 0.75, 0.0, 0.1))
vcall(pad, 7, vib)
check(real['vib'] and real['vib'][-1] == (0.25, 0.75, 0.0, 0.1), 'Vibration geht an das echte Gamepad')
got = e.alloc(32)
vcall(pad, 6, got)
check(struct.unpack('<4d', bytes(e.mu.mem_read(got, 32))) == (0.25, 0.75, 0.0, 0.1), 'get_Vibration liefert den letzten Wert')

# hot plug: pad gone
real['pads'] = 0
advance(1200)
real['reading'] = [0, 4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
check(read()['buttons'] == 0, 'Gamepad abgesteckt: nach 1 s nicht mehr gelesen')
real['pads'] = 1
advance(1200)
check(read()['buttons'] == 4, 'Gamepad wieder da: wird nach 1 s wieder gelesen')

# disabled -> untouched
p = os.path.join(root, 'FH2_Keyboard.ini')
text = open(p, 'rb').read().replace(b'Enabled=1', b'Enabled=0')
open(p, 'wb').write(text)
e2 = KEmu(DLL, root)
e = e2
orig = e.stub('RoGetActivationFactory', original_factory)
e.call('Fh2TestKeyboardAsync', e.stub('k', async_key), e.stub('f', foreground), e.stub('w', window_pid), e.stub('r', raw_buffer))
e.call('Fh2TestKeyboardSetup', orig, e.put_wstr('C:\\test'))
hook = e.call('Fh2TestKeyboardFactoryHook')
out = e.alloc(8)
hr = e.call(hook, e.put_wstr('Windows.Gaming.Input.Gamepad'), e.put_bytes(IID_STATICS), out)
check(e.rq(out) == real_statics, 'Enabled=0: Azimuth bekommt die echten Gamepads wie vorher')

# Azimuth Mapper laeuft: Gripline-Tastatur bleibt aus (jede Taste kaeme doppelt an)
def mapper_case(running):
    open(p, 'wb').write(text.replace(b'Enabled=0', b'Enabled=1'))
    global e
    em = e = KEmu(DLL, root)
    em.mapper_running = running
    orig3 = em.stub('RoGetActivationFactory', original_factory)
    em.call('Fh2TestKeyboardAsync', em.stub('k', async_key), em.stub('f', foreground), em.stub('w', window_pid), em.stub('r', raw_buffer))
    em.call('Fh2TestKeyboardSetup', orig3, em.put_wstr('C:\\test'))
    o = em.alloc(8)
    em.call(em.call('Fh2TestKeyboardFactoryHook'), em.put_wstr('Windows.Gaming.Input.Gamepad'), em.put_bytes(IID_STATICS), o)
    return em.rq(o)

check(mapper_case(True) == real_statics, 'Azimuth Mapper laeuft: Tastatur aus, echte Gamepads unveraendert')
check(mapper_case(False) not in (0, real_statics), 'ohne Mapper: Tastatur bleibt an')

print()
print('ALLE TESTS OK (%d Pruefungen)' % checks[0] if not fails else '%d FEHLER' % len(fails))
sys.exit(1 if fails else 0)
