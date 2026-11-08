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
    check(0.24 < v[2] < 0.26 and 0 <= v[0] < 0.01 and abs(v[1] + 1.0) < 1e-4, f'faehrt sanft an (2,5 m/s^2, Ziel seitlich: hoechstens 2 m/s), entlang der Nase, Fallen bleibt: {v}')
    fw = get(-0x360)
    check(abs(fw[0] - 0.024) < 1e-3 and fw[0] > 0 and abs(w[1]) < 1e-6 and abs(w[0] - 0.1) < 1e-4 and abs(w[2] - 0.2) < 1e-4,
          f'lenkt nach +x (Auto selbst gedreht, langsam beim Anfahren), Kippen bleibt der Physik: {fw} {w}')
    r = get(-0x380)
    check(abs(r[0] - 0.9997) < 1e-3 and abs(r[2] + 0.024) < 1e-3, f'rechts-Achse dreht mit: {r}')
    def level():
        vec(-0x380, 1.0, 0.0, 0.0); vec(-0x370, 0.0, 1.0, 0.0); vec(-0x360, 0.0, 0.0, 1.0)
    level()
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, struct.unpack('<I', struct.pack('<f', 100.0))[0])
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 0)  # straight ahead, 100 m
    for _ in range(40):
        e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v, w = get(-0x350), get(-0x340)
    check(abs(v[2] - 6.0) < 1e-3 and abs(get(-0x360)[0]) < 1e-4, f'geradeaus nach 4 s etwa 22 km/h, keine Drehung: {v} {get(-0x360)}')
    # braking: target now 4 m ahead (road): slows with at most 6 m/s^2 along the planned stop
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, struct.unpack('<I', struct.pack('<f', 4.0))[0])
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 0)
    e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v = get(-0x350)
    check(abs(v[2] - 5.4) < 1e-3, f'bremst gleichmaessig (6 m/s^2), kein Ruck: {v}')
    # to the walker 8 m ahead: parks beside him (turns off the straight line), keeps going
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, struct.unpack('<I', struct.pack('<f', 8.0))[0])
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 1)
    e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v, w = get(-0x350), get(-0x340)
    fw = get(-0x360)
    check(v[2] > 4.0 and abs(fw[0]) > 0.03, f'zum Spieler: faehrt neben ihn (dreht seitlich weg): {v} {fw}')
    level()
    # the walker 2 m ahead: never nearer than 2.6 m - stops
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, struct.unpack('<I', struct.pack('<f', 2.0))[0])
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 1)
    for _ in range(40):
        e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v = get(-0x350)
    check(abs(v[2]) < 0.1, f'Spieler zu nah: Auto bleibt stehen, faehrt ihn nie an: {v}')
    # the parked car's own brakes take speed away every frame (Marvin's log 08.11.2026): the car still gets going
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, struct.unpack('<I', struct.pack('<f', 100.0))[0])
    vec(-0x350, 0.0, 0.0, 0.0)
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 0)
    for _ in range(30):
        e.call('Fh2TestNoclipCreep', GAME, I, 100)
        v = get(-0x350)
        vec(-0x350, v[0] * 0.3, v[1], v[2] * 0.3)  # the brakes after the game's update
    e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v = get(-0x350)
    check(abs(v[2] - 6.0) < 1e-3, f'Bremsen des geparkten Autos halten es nicht auf (Tempo kommt von Gripline): {v}')
    # stuck: backs up straight (summon.c drives round next)
    e.call('Fh2TestSummonReverse', 1)
    for _ in range(20):  # from 6 m/s forward: braking 6 m/s^2, then back
        e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v, w = get(-0x350), get(-0x340)
    check(abs(v[2] + 2.0) < 1e-3 and abs(w[1]) < 1e-4, f'festgefahren: setzt gerade zurueck (2 m/s): {v} {w}')
    e.call('Fh2TestSummonReverse', 0)
    # sporty: straight ahead 100 m, top speed 8.5 m/s
    e.call('Fh2TestSummonStyle', 2)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM0, 0)
    e.mu.reg_write(__import__('unicorn').x86_const.UC_X86_REG_XMM2, struct.unpack('<I', struct.pack('<f', 100.0))[0])
    e.call('Fh2TestSummonCreepSet', 0, 0, 0, 0)
    for _ in range(60):
        e.call('Fh2TestNoclipCreep', GAME, I, 100)
    v = get(-0x350)
    check(abs(v[2] - 8.5) < 1e-3, f'Fahrstil Sportlich: bis 31 km/h: {v}')
    e.call('Fh2TestSummonStyle', 1)
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
