"""Test of the noclip flight (features/noclip.c) with the real compiled test DLL and the real game code.

The game's CCarDynamics state getter/setter (vtable game+0x2eb4bb8 slots 0x1b8/0xb58) run from the
game image (forza_durango_release_final.exe, argument 2 or FH2_EXE) on a fake body; the keys come
through the keyboard's GetAsyncKeyState. Without the exe only the DLL loads."""
import math, os, struct, sys, tempfile
import pefile
from emu import Emu, GAME

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
EXE = sys.argv[2] if len(sys.argv) > 2 else os.environ.get('FH2_EXE', '')
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


root = tempfile.mkdtemp(prefix='eo-noclip-')
e = KEmu(DLL, root)
keys = set()
e.call('Fh2TestKeyboardAsync', e.stub('GetAsyncKeyState', lambda: 0x8000 if (e.arg(0) & 0xff) in keys else 0),
       e.stub('GetForegroundWindow', lambda: 0x1234), e.stub('GetWindowThreadProcessId', lambda: (e.wd(e.arg(1), 4242), 1)[1]),
       e.stub('WindowsGetStringRawBuffer', lambda: e.arg(0)))
e.call('Fh2TestKeyboardSetup', e.stub('RoGetActivationFactory', lambda: 0x80004002), e.put_wstr('C:\\test'))
check('Fh2TestNoclipFly' in e.exports, 'Noclip-Testfunktion vorhanden')

if not EXE or not os.path.exists(EXE):
    print('no game exe (argument 2 / FH2_EXE): game-code checks skipped')
else:
    pe = pefile.PE(EXE, fast_load=True)
    for s in pe.sections:
        data = s.get_data()[: s.Misc_VirtualSize or len(s.get_data())]
        e.mu.mem_write(GAME + s.VirtualAddress, data)
    block = e.alloc(0x5000)
    I = block + 0x3a0
    e.wq(I, GAME + 0x2eb4bb8)
    def vec(off, *v):
        e.mu.mem_write(I + off, struct.pack('<4f', *v, 0.0))
    def get(off):
        return struct.unpack('<3f', bytes(e.mu.mem_read(I + off, 12)))
    vec(-0x390, 100.0, 5.0, 200.0)   # position
    vec(-0x350, 3.0, -2.0, 4.0)      # velocity
    vec(-0x340, 0.5, 0.5, 0.5)       # spin
    vec(-0x380, 1.0, 0.0, 0.0)       # right
    vec(-0x370, 0.0, 1.0, 0.0)       # up
    vec(-0x360, 0.0, 0.0, 1.0)       # forward
    def fly(ms, body=None):
        for _ in range(ms // 50):
            e.call('Fh2TestNoclipFly', GAME, body or I, 50)
    near = lambda a, b: all(abs(x - y) < 1e-3 for x, y in zip(a, b))
    check(e.call('Fh2TestNoclipFly', GAME, I, 16) == 1, 'Koerper erkannt, erster Flug-Schritt')
    check(near(get(-0x390), (100.0, 5.5, 200.0)) and near(get(-0x350), (0, 0, 0)) and near(get(-0x340), (0, 0, 0)),
          f'ohne Taste: steht still, 0,5 m hoeher, kein Tempo: {get(-0x390)} {get(-0x350)}')
    keys.add(ord('W'))
    fly(500)
    check(near(get(-0x390), (100.0, 5.5, 210.0)), f'W 0,5 s: 10 m vorwaerts: {get(-0x390)}')
    keys.add(0xa0)
    fly(100)
    check(near(get(-0x390), (100.0, 5.5, 218.0)), f'W + LShift: 4x schneller: {get(-0x390)}')
    keys.clear()
    keys.add(0x20)
    fly(1000)
    check(near(get(-0x390), (100.0, 15.5, 218.0)), f'Leertaste 1 s: 10 m hoch: {get(-0x390)}')
    keys.clear()
    keys.add(0xa2)
    fly(500)
    check(near(get(-0x390), (100.0, 10.5, 218.0)), f'LCtrl: sinken: {get(-0x390)}')
    keys.clear()
    keys.add(ord('A'))
    fly(500)  # 0.5 s * 1.8 rad/s = 0.9 rad
    f, r = get(-0x360), get(-0x380)
    want = (-math.sin(0.9), 0.0, math.cos(0.9))
    check(near(f, want), f'A: nach links (weg von der rechten Achse) gedreht: {f}')
    check(near(r, (want[2], 0.0, -want[0])) and near(get(-0x370), (0, 1, 0)), f'Achsen bleiben rechtwinklig und waagerecht: {r}')
    keys.clear()
    keys.add(ord('D'))
    fly(500)
    check(near(get(-0x360), (0.0, 0.0, 1.0)), f'D: wieder zurueck: {get(-0x360)}')
    keys.clear()
    keys.add(ord('W'))
    e.call('Fh2TestNoclipFly', GAME, I, 2000)
    check(near(get(-0x390), (100.0, 10.5, 220.0)), f'Ruckler (2 s): hoechstens 0,1 s Weg: {get(-0x390)}')
    keys.clear()
    keys.add(ord('S'))
    fly(500)
    check(near(get(-0x390), (100.0, 10.5, 210.0)), f'S: rueckwaerts: {get(-0x390)}')

    # a tilted body that points the other way round (mirrored right row): level again, heading kept
    block2 = e.alloc(0x5000)
    J = block2 + 0x3a0
    e.wq(J, GAME + 0x2eb4bb8)
    def vec2(off, *v):
        e.mu.mem_write(J + off, struct.pack('<4f', *v, 0.0))
    vec2(-0x390, 0.0, 0.0, 0.0)
    vec2(-0x380, -0.8, 0.6, 0.0)
    vec2(-0x370, 0.6, 0.8, 0.0)
    vec2(-0x360, 0.0, 0.0, 1.0)
    keys.clear()
    e.call('Fh2TestNoclipFly', GAME, J, 16)
    got = lambda off: struct.unpack('<3f', bytes(e.mu.mem_read(J + off, 12)))
    check(near(got(-0x380), (-1.0, 0.0, 0.0)) and near(got(-0x370), (0, 1, 0)) and near(got(-0x360), (0, 0, 1)),
          f'schraeges Auto wird waagerecht, Seite bleibt: {got(-0x380)}')

print('ALLE TESTS OK (%d Pruefungen)' % checks[0] if not fails else '%d FEHLER' % len(fails))
sys.exit(1 if fails else 0)
