"""Test of "no damage on cars" (features/no_damage.c) with the real compiled test DLL and the real game code.

The game's own damage reset (ForzaCar vtable slot 0x120 = game+0x29d3e0 and game+0x29d480) runs from the
game image (forza_durango_release_final.exe, argument 2 or FH2_EXE) on a fake car. Without the exe only
the DLL loads."""
import os, struct, sys, tempfile
import pefile
from emu import Emu, GAME

DLL = sys.argv[1] if len(sys.argv) > 1 else '../out-test/fh2-mods/Gripline.dll'
EXE = sys.argv[2] if len(sys.argv) > 2 else os.environ.get('FH2_EXE', '')
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


root = tempfile.mkdtemp(prefix='gl-nodamage-')
e = Emu(DLL, root)
check('Fh2TestNoDamage' in e.exports, 'Testfunktion vorhanden')

if not EXE or not os.path.exists(EXE):
    print('no game exe (argument 2 / FH2_EXE): game-code checks skipped')
else:
    pe = pefile.PE(EXE, fast_load=True)
    for s in pe.sections:
        data = s.get_data()[: s.Misc_VirtualSize or len(s.get_data())]
        e.mu.mem_write(GAME + s.VirtualAddress, data)
    calls = []
    graphics_vt = e.alloc(0x200)
    e.wq(graphics_vt + 0x110, e.stub('graphics_reset_dents', lambda: calls.append('dents')))
    e.wq(graphics_vt + 0xa0, e.stub('graphics_reset_glass', lambda: calls.append('glass')))
    part_vt = e.alloc(0x40)
    e.wq(part_vt + 0x20, e.stub('part_reattach', lambda: calls.append(('part', e.arg(1)))))
    present_vt = e.alloc(0x100)

    def make_car():
        car = e.alloc(0x6000)
        e.wq(car, GAME + 0x2c5cb80)              # ForzaCar vtable
        graphics = e.alloc(0x20)
        e.wq(graphics, graphics_vt)
        e.wq(car + 0x4d38, graphics)
        part = e.alloc(0x20)
        e.wq(part, part_vt)
        parts = e.alloc(0x20)
        e.wq(parts, part)
        e.wq(car + 0x5ab8, parts)                # loose body parts: begin
        e.wq(car + 0x5ac0, parts + 8)            # end
        e.mu.mem_write(car + 0x4d08, struct.pack('<I', 1))  # skips the presentation step (needs the running game)
        present = e.alloc(0x20)
        e.wq(present, present_vt)
        e.wq(car + 0x5aa8, present)
        e.wq(car + 0x5ae8, 0x1234)
        return car

    rq = lambda a: struct.unpack('<Q', bytes(e.mu.mem_read(a, 8)))[0]
    car = make_car()
    check(e.call('Fh2TestNoDamage', GAME, 0, car, 0, 1) == 1 and calls == [], 'Aus: nichts passiert')
    check(e.call('Fh2TestNoDamage', GAME, 1, car, 0, 0) == 1 and calls == [], 'Ladebildschirm (eigenes Auto unbekannt): nichts')
    check(e.call('Fh2TestNoDamage', GAME, 1, car, 0, 1) == 1, 'Spiel-Code passt (Build erkannt)')
    check(calls == ['dents', 'glass', ('part', 1)], f'Andere Autos: Dellen, Glas, Teile zurueck: {calls}')
    check(rq(car + 0x5ac0) == rq(car + 0x5ab8) and rq(car + 0x5ae8) == 0, 'Teile-Liste leer, Hilfswert geloescht')
    calls.clear()
    e.call('Fh2TestNoDamage', GAME, 1, car, 0, 1)
    check(calls == [], 'gleiches Auto gleich danach: wartet (alle 100 ms)')
    e.tick += 100
    e.call('Fh2TestNoDamage', GAME, 1, car, 0, 1)
    check(calls[:2] == ['dents', 'glass'], '100 ms spaeter: wieder')
    calls.clear()
    own = make_car()
    e.call('Fh2TestNoDamage', GAME, 1, own, 1, 1)
    check(calls == [], 'Andere Autos: eigenes Auto bleibt wie im Spiel')
    e.call('Fh2TestNoDamage', GAME, 2, own, 1, 1)
    e.call('Fh2TestNoDamage', GAME, 2, own, 1, 1)
    check(calls.count('dents') == 2, f'Alle Autos: eigenes Auto jedes Bild: {calls}')
    calls.clear()
    other = make_car()
    e.wq(other, GAME + 0x2eb4bb8)
    e.call('Fh2TestNoDamage', GAME, 2, other, 0, 1)
    check(calls == [], 'fremdes Objekt (anderer vtable): nichts')
    half = make_car()
    e.wq(half + 0x5aa8, 0)
    e.call('Fh2TestNoDamage', GAME, 2, half, 0, 1)
    check(calls == [], 'Auto halb gebaut (kein Objekt bei +0x5aa8): nichts')

print(f'{checks[0]} checks, {len(fails)} failed')
sys.exit(1 if fails else 0)
