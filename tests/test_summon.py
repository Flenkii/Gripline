"""Test of summon (features/summon.c) with the real compiled test DLL and the real game image.

Checks that the game code summon relies on is where it was found (forza_durango_release_final.exe,
argument 2 or FH2_EXE), that the four GPS / route tracker vtable slots are chained, and that the
summon stays idle when it cannot start (not walking, GPS not active). The drive itself needs the
running game (Gripline.log 'SUMMON:')."""
import os, struct, sys, tempfile
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
    def k_VirtualProtect(self):
        if self.arg(3): self.wd(self.arg(3), 0x20)
        return 1


e = KEmu(DLL, tempfile.mkdtemp(prefix='eo-summon-'))
check('Fh2TestSummonStart' in e.exports and 'Fh2TestSummonTick' in e.exports, 'Summon-Testfunktionen vorhanden')
if not EXE or not os.path.exists(EXE):
    print('no game exe (argument 2 / FH2_EXE): game-code checks skipped')
else:
    pe = pefile.PE(EXE, fast_load=True)
    for s in pe.sections:
        data = s.get_data()[: s.Misc_VirtualSize or len(s.get_data())]
        e.mu.mem_write(GAME + s.VirtualAddress, data)
    q = lambda a: struct.unpack('<Q', bytes(e.mu.mem_read(a, 8)))[0]
    before = {slot: q(GAME + slot) for slot in (0x2cc3198, 0x2cc31b0, 0x2cc3278, 0x2cc3288)}
    check(before == {0x2cc3198: GAME + 0x6b4d30, 0x2cc31b0: GAME + 0x6c0c70, 0x2cc3278: GAME + 0x6a26e0,
                     0x2cc3288: GAME + 0x6b4e40}, 'GPS-/Tracker-Slots zeigen auf die erwarteten Spielfunktionen')
    check(e.call('Fh2TestSummonStart', GAME) == 1, 'Spiel-Code passt, Summon bereit')
    after = {slot: q(GAME + slot) for slot in before}
    dll_lo, dll_hi = e.base, e.base + 0x1000000
    check(all(dll_lo <= v < dll_hi for v in after.values()), 'alle vier Slots auf Gripline umgeleitet')
    check(e.call('Fh2TestSummonStart', GAME) == 1 and {s: q(GAME + s) for s in before} == after, 'zweiter Start aendert nichts')
    # a fake GPS component: the right vtable, inactive (+0x160 = 0)
    satnav = e.alloc(0x200)
    e.wq(satnav, GAME + 0x2cc3188)
    e.call('Fh2TestSummonRequest')
    e.call('Fh2TestSummonTick', satnav)
    check(e.call('Fh2TestSummonPhase') == 0, 'nicht beim Laufen: Taste tut nichts')
    e.call('Fh2TestNoclipMode', 3)
    e.call('Fh2TestSummonRequest')
    e.call('Fh2TestSummonTick', satnav)
    check(e.call('Fh2TestSummonPhase') == 0, 'GPS nicht aktiv: bleibt aus')
    e.call('Fh2TestNoclipMode', 0)
    # creep (Gripline drives the car itself): a body facing +z at the origin, target 10 m to +x
    block = e.alloc(0x5000)
    I = block + 0x3a0
    e.wq(I, GAME + 0x2eb4bb8)
    def vec(off, *v):
        e.mu.mem_write(I + off, struct.pack('<4f', *v, 0.0))
    def get(off):
        return struct.unpack('<3f', bytes(e.mu.mem_read(I + off, 12)))
    vec(-0x390, 0.0, 0.0, 0.0); vec(-0x350, 0.0, -1.0, 0.0); vec(-0x340, 0.1, 0.0, 0.2)
    vec(-0x380, 1.0, 0.0, 0.0); vec(-0x370, 0.0, 1.0, 0.0); vec(-0x360, 0.0, 0.0, 1.0)
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 0)  # a float call needs xmm args: set through memory below
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, struct.unpack('<I', struct.pack('<f', 10.0))[0])
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM1, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, 0)
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 0)
    check(e.call('Fh2TestSummonPhase') == 4, 'Rangieren an')
    e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v, w = get(-0x350), get(-0x340)
    check(0.25 < v[2] < 0.3 and abs(v[0]) < 1e-4 and abs(v[1] + 1.0) < 1e-4, f'faehrt langsam an (Ziel seitlich: hoechstens 2 m/s), entlang der Nase, Fallen bleibt: {v}')
    check(abs(w[1] - 0.24) < 1e-3 and abs(w[0] - 0.1) < 1e-4 and abs(w[2] - 0.2) < 1e-4, f'dreht nach +x, langsam beim Anfahren, Kippen bleibt der Physik: {w}')
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, struct.unpack('<I', struct.pack('<f', 100.0))[0])
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 0)  # straight ahead, 100 m
    for _ in range(40):
        e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v, w = get(-0x350), get(-0x340)
    check(4.0 < v[2] <= 4.5 + 1e-3 and abs(w[1]) < 1e-4, f'geradeaus nach 4 s etwa 16 km/h, keine Drehung: {v} {w}')
    # to the walker 5 m ahead: stops (wants 0) before reaching him
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, struct.unpack('<I', struct.pack('<f', 5.0))[0])
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 1)
    for _ in range(40):
        e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v = get(-0x350)
    check(abs(v[2]) < 0.1, f'zum Spieler: bleibt 6 m davor stehen: {v}')
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, -1)
    check(e.call('Fh2TestSummonPhase') == 0, 'Rangieren aus')
    # the code bytes checked in summon_start, changed: summon stays off
    e2 = KEmu(DLL, tempfile.mkdtemp(prefix='eo-summon2-'))
    for s in pe.sections:
        data = s.get_data()[: s.Misc_VirtualSize or len(s.get_data())]
        e2.mu.mem_write(GAME + s.VirtualAddress, data)
    e2.mu.mem_write(GAME + 0x6b58b0, b'\xc3')
    check(e2.call('Fh2TestSummonStart', GAME) == 0, 'anderer Spiel-Code: Summon aus')

print(f'{checks[0]} checks, {len(fails)} failed')
print('ALLE TESTS OK' if not fails else f'{len(fails)} FEHLER')
sys.exit(1 if fails else 0)
