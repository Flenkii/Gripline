"""Creator mode (features/creator_mode.c): key / wheel toggle, UI draw hooks skip while driving."""
import struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
fails = []

def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)

PLAYER_MANAGER = 0x34aba60
e = Emu(DLL, tempfile.mkdtemp(prefix='eo-creator-'))
image = e.alloc(PLAYER_MANAGER + 0x10)
game = e.alloc(0x100)
sim = e.alloc(0x100)
e.wq(image + PLAYER_MANAGER, game)
e.wq(game + 0x48, sim)
def paused(p): e.mu.mem_write(sim + 0x28, bytes([p]))
def press(source=0, held=1): return e.call('Fh2TestCreatorMode', image, source, held) & 0xffffffff
def hidden():
    e.tick += 100
    return e.call('Fh2TestUiHidden') & 0xffffffff

paused(0)
check(press(0, 0) == 0 and hidden() == 0, 'aus: UI wird gezeichnet')
check(press(0, 1) == 1, 'Taste gedrückt -> Creator-Modus an')
check(press(0, 1) == 1, 'Taste gehalten -> bleibt an (kein Flackern)')
check(hidden() == 1, 'beim Fahren: UI (HUD, Minimap) ausgeblendet')
paused(1)
check(hidden() == 0, 'Pause/Menü: UI sichtbar')
paused(0)
check(hidden() == 1, 'weiterfahren: wieder ausgeblendet')
check(press(0, 0) == 1, 'Taste los -> bleibt an')
check(press(1, 1) == 0, 'Lenkrad-Knopf -> Creator-Modus aus')
check(hidden() == 0, 'aus: UI wieder da')
check(press(1, 0) == 0 and press(0, 1) == 1, 'Tastatur schaltet wieder an')
press(0, 0); press(0, 1); press(0, 0)
check(hidden() == 0, 'zweimal gedrückt -> aus')

print(f'{len(fails)} Fehler' if fails else 'ALLE TESTS OK')
sys.exit(1 if fails else 0)
