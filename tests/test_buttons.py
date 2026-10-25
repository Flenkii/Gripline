"""Test of the free wheel buttons (features/wheel_buttons.c) with the real compiled test DLL
and the real game code bytes of forza_durango_release_final.exe.

Checks: patch sites, generated stubs (executed together with the original game instructions),
layout hook with WheelLayouts from gamedb, action table, report bits, menu settings."""
import os, shutil, sqlite3, struct, sys, tempfile
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
from emu import Emu, GAME, GAME_SIZE, STACK

HERE = os.path.dirname(os.path.abspath(__file__))
DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
EXE_IMG = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.environ.get('FH2_GAME', 'game'), 'exe.img')
DB = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.environ.get('FH2_GAME', 'game'), 'db.slt')
fails = []

def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)

game_bytes = open(EXE_IMG, 'rb').read()

class GameEmu(Emu):
    """Emu with memory management that knows free / mapped regions."""
    def mapped(self, address):
        return any(b <= address <= e for b, e, _ in self.mu.mem_regions())

    def k_VirtualQuery(self):
        addr, info = self.arg(0), self.arg(1)
        self.mu.mem_write(info, bytes(48))
        base = addr & ~0xfff
        self.wq(info, base)
        self.wq(info + 8, base)
        self.wq(info + 24, 0x1000)
        if self.mapped(addr):
            self.wd(info + 32, 0x1000)   # MEM_COMMIT
            self.wd(info + 36, 0x04)     # PAGE_READWRITE
        else:
            self.wd(info + 32, 0x10000)  # MEM_FREE
        return 48

    def k_VirtualAlloc(self):
        addr, size = self.arg(0), self.arg(1)
        if addr == 0 or self.mapped(addr):
            return 0
        self.mu.mem_map(addr, (size + 0xfff) & ~0xfff)
        self.allocations.append(addr)
        return addr

    def k_VirtualProtect(self):
        if self.arg(3):
            self.wd(self.arg(3), 0x20)
        return 1

    def k_GetSystemInfo(self):
        out = self.arg(0)
        self.mu.mem_write(out, bytes(48))
        self.wd(out + 4, 0x1000)
        self.wd(out + 0x28, 0x10000)

    def k_FlushInstructionCache(self): return 1
    def k_GetCurrentProcess(self): return 0xffffffffffffffff


def make(copy_game_code):
    root = tempfile.mkdtemp(prefix='eo-buttons-')
    for name in ('FH2_Lenkrad.ini', 'FH2_ExtraOptions.ini'):
        shutil.copy(os.path.join(HERE, 'fixture', name), os.path.join(root, name))
    os.makedirs(os.path.join(root, 'game'))
    e = GameEmu(DLL, root)
    e.allocations = []
    if copy_game_code:
        for lo, hi in [(0x106c000, 0x106d000), (0x2f8000, 0x2fe000), (0x2e69000, 0x2e6a000)]:
            e.mu.mem_write(GAME + lo, game_bytes[lo:hi])
    manager = e.alloc(0x80)
    e.mu.mem_write(manager + 0x30, b'DE' + b'\0' * 14)
    e.wq(manager + 0x40, 2)
    e.wq(manager + 0x48, 15)
    e.wq(GAME + 0x3662990, manager)
    return e, root


# ---- gamedb: WheelLayouts / ControllerLayouts as the game's loader turns them into bindings
COLUMNS = ['LeftThumbUp', 'LeftThumbDown', 'LeftThumbButton', 'RightThumbUp', 'RightThumbDown',
           'RightThumbLeft', 'RightThumbRight', 'RightThumbButton', 'DPadUp', 'DPadDown', 'DPadLeft',
           'DPadRight', 'ButtonA', 'ButtonB', 'ButtonX', 'ButtonY', 'LeftShoulder', 'RightShoulder',
           'LeftTrigger', 'RightTrigger']
INPUTS = [0x13, 0x14, 6, 0x1a, 0x1b, 0x1c, 0x1d, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11]
ACTIONS = {'Gas': 0, 'Brake': 1, 'Handbrake': 3, 'ShiftUp': 4, 'ShiftDown': 5, 'Clutch': 6,
           'LookForward': 9, 'LookBack': 10, 'LookLeft': 11, 'LookRight': 12, 'LookStraightBack': 13,
           'Telemetry': 14, 'PhotoMode': 15, 'SwitchCamera': 17, 'Rewind': 20, 'RadioLeft': 23,
           'RadioRight': 24, 'MiniLeaderboard': 25}
db = sqlite3.connect(DB)
def layout_rows(table, layout):
    row = db.execute(f'select * from {table} where LayoutId=?', (layout,)).fetchone()
    return [(ACTIONS[v], INPUTS[i]) for i, v in enumerate(row[1:]) if v]

def install_game_stubs(e):
    """original loader and bind function replaced by Python (the real ones need gamedb/SQLite)"""
    e.loads = []
    def load():
        m, layout, kind = e.arg(0), e.arg(1) & 0xffffffff, e.arg(2) & 0xff
        e.loads.append((layout, kind))
        table = {1: 'ControllerLayouts', 2: 'WheelLayouts', 3: 'BristolLayouts'}[kind]
        for action, inp in layout_rows(table, layout):
            add_binding(m, action, inp)
        return 0
    def bind():
        m, ctx, action, inp = e.arg(0), e.arg(1) & 0xffffffff, e.arg(2) & 0xffffffff, e.arg(3) & 0xffffffff
        e.bind_calls.append((ctx, action, inp, e.arg(4) & 0xffffffff, e.arg(5) & 0xffffffff,
                             e.arg(6) & 0xffffffff, e.arg(7) & 0xffffffff))
        add_binding(m, action, inp)
    e.bind_calls = []
    e.stub('load_layout', load, GAME + 0x2dc300)
    e.stub('bind', bind, GAME + 0x1073f00)

def new_map(e):
    m = e.alloc(24)
    vecs = e.alloc(24)
    records = e.alloc(28 * 64)
    e.wq(m, vecs); e.wq(m + 8, vecs + 24); e.wq(m + 16, vecs + 24)
    e.wq(vecs, records); e.wq(vecs + 8, records); e.wq(vecs + 16, records + 28 * 64)
    return m

def add_binding(e_map, action, inp):
    e = CURRENT[0]
    vecs = e.rq(e_map)
    end = e.rq(vecs + 8)
    e.mu.mem_write(end, struct.pack('<7i', 0, action, inp, 0x25, 0x25, 0x25, 0x25))
    e.wq(vecs + 8, end + 28)

def bindings(e, m):
    vecs = e.rq(m)
    b, end = e.rq(vecs), e.rq(vecs + 8)
    return [struct.unpack('<7i', bytes(e.mu.mem_read(a, 28)))[:3] for a in range(b, end, 28)]

def run_from(e, start, until, regs):
    rsp = STACK + 0x80000
    e.mu.reg_write(UC_X86_REG_RSP, rsp)
    for r, v in regs.items():
        e.mu.reg_write(r, v)
    e.mu.emu_start(start, until, count=100000)
    return rsp

def call_layout_site(e, site, m, layout, kind):
    """execute the game's own call instruction at the patched site"""
    rsp = STACK + 0x80000
    opts = e.alloc(8)
    e.mu.mem_write(opts, bytes(8))
    e.wq(rsp + 0x20, 0)
    run_from(e, GAME + site, GAME + site + 5, {UC_X86_REG_RCX: m, UC_X86_REG_RDX: layout,
                                               UC_X86_REG_R8: 0x1234560000000000 | kind,
                                               UC_X86_REG_R9: opts})
    check(e.mu.reg_read(UC_X86_REG_RSP) == rsp, f'Stack nach Layout-Aufruf bei 0x{site:x} unverändert')

def rs_state(e, wheel=True):
    """run original game code 0x106c65b..0x106c691 (RS button into wButtons)"""
    state = e.alloc(16)
    e.mu.mem_write(state, bytes(16))
    e.mu.reg_write(UC_X86_REG_RAX, 0xdeadbeef)
    if not wheel:  # gamepad path: must come back right behind the patch (mov rax,[rbx])
        run_from(e, GAME + 0x106c65b, GAME + 0x106c660, {UC_X86_REG_RBX: 0x5000, UC_X86_REG_RSI: state})
        return e.mu.reg_read(UC_X86_REG_RIP) - GAME
    run_from(e, GAME + 0x106c65b, GAME + 0x106c691, {UC_X86_REG_RBX: 0, UC_X86_REG_RSI: state,
                                                    UC_X86_REG_RBP: STACK + 0x90000})
    check(e.mu.reg_read(UC_X86_REG_RSP) == STACK + 0x80000, 'Stack nach RS-Stelle ausgeglichen')
    return struct.unpack('<H', bytes(e.mu.mem_read(state + 4, 2)))[0]

def ly_state(e, wheel=True):
    """run original game code 0x106ca23..0x106ca60 (left stick Y into sThumbLY)"""
    state = e.alloc(16)
    e.mu.mem_write(state, bytes(16))
    if not wheel:
        run_from(e, GAME + 0x106ca23, GAME + 0x106ca28, {UC_X86_REG_RBX: 0x5000, UC_X86_REG_RSI: state})
        return e.mu.reg_read(UC_X86_REG_RIP) - GAME
    xmm = lambda f: int.from_bytes(struct.pack('<f', f) + bytes(12), 'little')
    e.mu.reg_write(UC_X86_REG_XMM6, 0)
    e.mu.reg_write(UC_X86_REG_XMM7, xmm(0.5))
    e.mu.reg_write(UC_X86_REG_XMM8, xmm(65535.0))
    # stop where the game continues (0x106ca4f: vmulss xmm0,xmm0,xmm7; vmulss xmm1,xmm0,xmm8;
    # vcvttss2si eax,xmm1; mov [rsi+0xa],ax). unicorn mis-emulates the VEX vmulss with xmm8,
    # so the last two game steps are computed here exactly like the game does.
    run_from(e, GAME + 0x106ca23, GAME + 0x106ca4f, {UC_X86_REG_RBX: 0, UC_X86_REG_RSI: state,
                                                    UC_X86_REG_RBP: STACK + 0x90000})
    check(e.mu.reg_read(UC_X86_REG_XMM7) == xmm(0.5) and e.mu.reg_read(UC_X86_REG_XMM8) == xmm(65535.0),
          'xmm7/xmm8 (vom Spiel benutzt) bleiben erhalten')
    check(e.mu.reg_read(UC_X86_REG_RSP) == STACK + 0x80000, 'Stack am Übergabepunkt ausgeglichen')
    import numpy as np
    x = np.float32(struct.unpack('<f', struct.pack('<I', e.mu.reg_read(UC_X86_REG_XMM0) & 0xffffffff))[0])
    return int(np.float32(np.float32(x * np.float32(0.5)) * np.float32(65535.0)))

CURRENT = [None]
F = dict(OFF=0, CAMERA=1, BACK=2, LEFT=3, RIGHT=4, REWIND=5, TELE=6, PHOTO=7, RPREV=8, RNEXT=9,
         HAND=10, CLUTCH=11, UP=12, DOWN=13)

# =====================================================================================
print('Installation mit echtem Spielcode')
e, root = make(True)
CURRENT[0] = e
original = {s: bytes(e.mu.mem_read(GAME + s, 5)) for s in (0x106c65b, 0x106ca23, 0x2f8f1f, 0x2f8f3b, 0x2f8f57, 0x2fd673)}
e.call('Fh2TestVideoPrepare', GAME)
r = e.call('Fh2TestRigStart', GAME, e.put_wstr('C:\\test\\game\\forza_durango_release_final.exe'), e.put_wstr('C:\\test'))
wheel_before = open(os.path.join(root, 'FH2_Wheel.ini'), 'rb').read().decode('latin1')
check(r == 1, 'Start ok')
check(len(e.allocations) == 1, f'eine Code-Seite nahe am Spiel angelegt: {[hex(a) for a in e.allocations]}')
page = e.allocations[0] if e.allocations else 0
check(page and abs(page - GAME) < 0x7fff0000, 'Seite in Sprungweite (+-2 GB)')
for s, b in original.items():
    now = bytes(e.mu.mem_read(GAME + s, 5))
    target = GAME + s + 5 + struct.unpack('<i', now[1:])[0]
    check(now[0] == 0xe8 and page <= target < page + 0x1000, f'0x{s:x}: {b.hex()} -> call Seite+0x{target - page:x}')
log = open(os.path.join(root, 'FH2_Wheel.log'), 'rb').read().decode('latin1')
check('Spiel-Hooks gesetzt' in log, 'Log: Spiel-Hooks gesetzt')
check('Taste 8 -> Funktion 2, Taste 9 -> Funktion 1, Taste 10 -> Funktion 0' in log, 'Standard: RSB=Blick zurück, LSB=Kamera, Xbox=aus')
install_game_stubs(e)

print('Originalverhalten ohne gedrückte Zusatztaste')
check(rs_state(e) == 0, 'RS-Bit bleibt 0 (wie im Original)')
check(ly_state(e) == 0, 'linker Stick Y bleibt 0 (wie im Original)')
check(rs_state(e, wheel=False) == 0x106c660, 'Gamepad-Weg läuft unverändert weiter (RS)')
check(ly_state(e, wheel=False) == 0x106ca28, 'Gamepad-Weg läuft unverändert weiter (Stick Y)')

print('Layout-Hook (WheelLayouts aus gamedb)')
for site, layout, kind in [(0x2f8f1f, 0, 1), (0x2f8f3b, 0, 2), (0x2f8f57, 0, 3)]:
    m = new_map(e)
    before = layout_rows({1: 'ControllerLayouts', 2: 'WheelLayouts', 3: 'BristolLayouts'}[kind], layout)
    call_layout_site(e, site, m, layout, kind)
    after = bindings(e, m)
    if kind != 2:
        check(sorted(after) == sorted((0, a, i) for a, i in before), f'Layout-Art {kind}: unverändert ({len(after)} Bindungen)')
    else:
        exp = [(0, {0x13: 11, 0x14: 12}.get(i, a), i) for a, i in before] + [(0, 13, 7)]
        check(sorted(after) == sorted(exp), f'Lenkrad-Layout 0: Stick Y -> Blick links/rechts, RS -> Blick zurück ({len(after)} Bindungen)')
        check(e.bind_calls[-1] == (0, 13, 7, 0x25, 0x25, 0x25, 0x25), f'neue Bindung mit den Spielparametern: {e.bind_calls[-1]}')
check(e.loads == [(0, 1), (0, 2), (0, 3)], f'Original-Ladefunktion jedes Mal aufgerufen: {e.loads}')
table = {a: e.call('Fh2TestWheelActionInput', a) & 0xffffffff for a in (17, 13, 11, 12, 20, 14, 15, 23, 24, 3, 6, 4, 5)}
table = {a: (v - (1 << 32) if v >= 1 << 31 else v) for a, v in table.items()}
check(table == {17: 15, 13: 7, 11: 0x13, 12: 0x14, 20: 14, 14: 1, 15: 0, 23: 2, 24: 3, 3: 13, 6: 12, 4: 9, 5: 8},
      f'Funktionstabelle Layout 0: {table}')

print('Spielerprofil lädt Layout 1 (Aufruf 0x2fd673)')
m = new_map(e)
call_layout_site(e, 0x2fd673, m, 1, 2)
table1 = {a: e.call('Fh2TestWheelActionInput', a) & 0xffffffff for a in (17, 13, 11, 12)}
check(table1 == {17: 14, 13: 15, 11: 8, 12: 9}, f'Layout 1: Kamera=X, Blick zurück=Y, links/rechts=Wippen: {table1}')
m = new_map(e)
call_layout_site(e, 0x2fd673, m, 0, 2)  # back to layout 0 for the rest

print('Controller-Layout für die Tastatur (Art 1)')
ctl = lambda a: (lambda v: v - (1 << 32) if v >= 1 << 31 else v)(e.call('Fh2TestControllerActionInput', a) & 0xffffffff)
check([ctl(a) for a in (0, 1, 3, 4, 5, 6, 17, 20)] == [11, 10, 12, 13, 14, 8, 9, 15],
      'Layout 0: Gas=RT, Bremse=LT, Handbremse=A, hoch=B, runter=X, Kupplung=LB, Kamera=RB, Zurückspulen=Y')
m = new_map(e)
call_layout_site(e, 0x2fd673, m, 2, 1)
check(sorted(bindings(e, m)) == sorted((0, a, i) for a, i in layout_rows('ControllerLayouts', 2)), 'Controller-Layout 2 bleibt unverändert')
check([ctl(a) for a in (3, 4, 5)] == [14, 13, 12], f'Layout 2: Handbremse=X, hoch=B, runter=A -> Tastatur folgt: {[ctl(a) for a in (3, 4, 5)]}')
m = new_map(e)
call_layout_site(e, 0x2fd673, m, 0, 1)
check([ctl(a) for a in (3, 4, 5)] == [12, 13, 14], 'zurück auf Layout 0')

print('Zusatztasten -> Spiel')
def report(buttons):
    v = e.call('Fh2TestWheelReport', buttons)
    return v & 0xff, (v >> 8) & 0xff
check(report(0) == (0, 0), 'keine Taste: Report leer')
check(report(1 << 0) == (0x10, 0), 'Taste A (0) unverändert -> A')
b = report(1 << 9)
check(b == (0x80, 0), f'LSB (9) = Kamera -> Y-Bit (Layout 0): {b}')
b = report(1 << 8)
st = e.call('Fh2TestWheelExtraState')
check(b == (0, 0) and st & 1, f'RSB (8) = Blick zurück -> RS-Hook aktiv (Report leer {b}, Status {st:#x})')
check(rs_state(e) == 0x80, 'Spielcode setzt jetzt das RS-Bit (0x80) in wButtons')
report(0)
check(rs_state(e) == 0, 'losgelassen -> RS-Bit wieder 0')
b = report(1 << 10)
check(b == (0, 0) and e.call('Fh2TestWheelExtraState') == 0x10, 'Xbox-Taste (10) = aus -> nichts')
check(report((1 << 9) | (1 << 0)) == (0x90, 0), 'LSB + A gleichzeitig -> Y + A')

print('Belegung im Menü ändern (live)')
base = e.call('Fh2TestRigPropertyBase')
def set_int(i, value):
    bx = e.alloc(0x30); e.wd(bx + 0x28, value)
    e.call('Fh2TestVideoSet', base + i, 0x7777, bx)
def box_int():
    out, value_ptr = e.arg(0), e.arg(1)
    bx = e.alloc(0x30)
    e.mu.mem_write(bx + 0x28, bytes(e.mu.mem_read(value_ptr, 4)))
    e.wq(out, bx)
    return out
e.stub('box_int', box_int, GAME + 0x4702d0)
changed = []
e.stub('property_changed', lambda: changed.append(e.arg(1)), GAME + 0xcf9c60)
def get_int(i):
    out = e.alloc(8)
    e.call('Fh2TestVideoGet', base + i, out)
    return e.rd(e.rq(out) + 0x28)
check([get_int(i) for i in (14, 15, 16)] == [2, 1, 0], 'Menü zeigt RSB=2, LSB=1, Xbox=0')
set_int(14, F['LEFT']); set_int(15, F['PHOTO']); set_int(16, F['HAND'])
e.call('Fh2TestWheelLiveApply')
check([get_int(i) for i in (14, 15, 16)] == [3, 7, 10], 'Menü: RSB=Blick links, LSB=Fotomodus, Xbox=Handbremse')
report(1 << 8)
check(ly_state(e) == 32767, 'RSB = Blick links -> linker Stick ganz hoch (32767)')
report(0)
check(ly_state(e) == 0, 'losgelassen -> Stick 0')
check(report(1 << 9) == (0, 0x01), 'LSB = Fotomodus -> Steuerkreuz hoch (Layout 0)')
check(report(1 << 10) == (0x20, 0), 'Xbox-Taste = Handbremse -> B')
set_int(14, F['RIGHT']); e.call('Fh2TestWheelLiveApply')
report(1 << 8)
check(ly_state(e) == -32767, 'RSB = Blick rechts -> linker Stick ganz runter')
report(0)
set_int(14, 16)
check(get_int(14) == 4, 'ungültige Funktion 16 abgelehnt')
set_int(15, 14); e.call('Fh2TestWheelLiveApply')
check(report(1 << 9) == (0, 0), 'LSB = Nitro -> kein Spiel-Knopf (geht nur an NitroSystem)')
set_int(15, 15); e.call('Fh2TestWheelLiveApply')
check(report(1 << 9) == (0, 0), 'LSB = Cockpit-Ansicht 2 -> kein Spiel-Knopf')
report(0)
for f, expect in [('CAMERA', (0x80, 0)), ('REWIND', (0x40, 0)), ('TELE', (0, 0x02)), ('RPREV', (0, 0x04)),
                  ('RNEXT', (0, 0x08)), ('CLUTCH', (0x10, 0)), ('UP', (0, 0x20)), ('DOWN', (0, 0x10))]:
    set_int(15, F[f]); e.call('Fh2TestWheelLiveApply')
    check(report(1 << 9) == expect, f'LSB = {f} -> {expect}')
report(0)

print('Speichern in FH2_Wheel.ini')
e.tick += 2000
e.call('Fh2TestRigStep', e.tick)
ini = open(os.path.join(root, 'FH2_Wheel.ini'), 'rb').read().decode('latin1')
check('ExtraFunction1=4' in ini and 'ExtraFunction2=13' in ini and 'ExtraFunction3=10' in ini,
      'ExtraFunction1..3 gespeichert')
import re
strip = lambda t: re.sub(r'ExtraFunction\d=\d+', '', t)
check(strip(ini) == strip(wheel_before), 'restliche Datei unverändert')

# =====================================================================================
print('Abweichender Spielcode (z. B. andere Spielversion)')
e2, root2 = make(False)
CURRENT[0] = e2
e2.call('Fh2TestVideoPrepare', GAME)
e2.call('Fh2TestRigStart', GAME, e2.put_wstr('C:\\test\\game\\forza_durango_release_final.exe'), e2.put_wstr('C:\\test'))
check(e2.allocations == [], 'keine Hooks, kein Speicher angelegt')
check(all(b == 0 for b in bytes(e2.mu.mem_read(GAME + 0x106c65b, 5))), 'Spielcode nicht angefasst')
log2 = open(os.path.join(root2, 'FH2_Wheel.log'), 'rb').read().decode('latin1')
check('Spielcode weicht ab' in log2, 'Log erklärt es')
v = e2.call('Fh2TestWheelReport', 1 << 9)
check(v & 0xff == 0x80, 'Kamera auf LSB geht trotzdem (Standard-Layout 0)')
v = e2.call('Fh2TestWheelReport', 1 << 8)
check(v == 0 and e2.call('Fh2TestWheelExtraState') == 0x10, 'Blick zurück ohne Hook: nichts (kein Fehler)')

print()
# =====================================================================================
print('Azimuth Mapper laeuft')
e3, root3 = make(True)
CURRENT[0] = e3
e3.mapper_running = True
before3 = {s_: bytes(e3.mu.mem_read(GAME + s_, 5)) for s_ in original}
e3.call('Fh2TestVideoPrepare', GAME)
e3.call('Fh2TestRigStart', GAME, e3.put_wstr('C:\\test\\game\\forza_durango_release_final.exe'), e3.put_wstr('C:\\test'))
check(e3.allocations == [] and all(bytes(e3.mu.mem_read(GAME + s_, 5)) == b_ for s_, b_ in before3.items()),
      'Lenkrad-Zusatztasten: keine Hooks, Spielcode nicht angefasst')
log3 = open(os.path.join(root3, 'FH2_Wheel.log'), 'rb').read().decode('latin1')
check('Azimuth Mapper is running - Gripline wheel off' in log3, 'Log: Lenkrad aus wegen Mapper')

print('FEHLER:' if fails else 'ALLE TESTS OK', len(fails) or '')
sys.exit(1 if fails else 0)
