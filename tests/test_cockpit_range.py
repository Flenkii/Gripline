"""Test of the steering range patch (features/cockpit.c cockpit_patch_game) with the real game code.

Unicorn computes VEX scalar arithmetic (vdivss ...) wrong, so the game's math is followed in
Python with the constants the patched game instructions really load.

The game keeps the cockpit steering angle within +-90 degrees (clamp in the presentation steering
filter) and gives the driver animation angle / 90 degrees. With the full steering animation both
must use rotation / 2, so the animation turns 1:1 with the real wheel."""
import math, os, struct, sys
from emu import Emu, GAME

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
EXE_IMG = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.environ.get('FH2_GAME', 'game'), 'exe.img')
fails = []


def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


game_bytes = open(EXE_IMG, 'rb').read()
CLAMP, GETTER = 0x2a989d, 0x293fe0  # vmovss xmm7,[pi/2] / getter with vdivss at +0x10


class GameEmu(Emu):
    def mapped(self, address):
        return any(b <= address <= e for b, e, _ in self.mu.mem_regions())

    def k_VirtualQuery(self):
        addr, info = self.arg(0), self.arg(1)
        self.mu.mem_write(info, bytes(48))
        base = addr & ~0xfff
        self.wq(info, base)
        self.wq(info + 8, base)
        self.wq(info + 24, 0x1000)
        self.wd(info + 32, 0x1000 if self.mapped(addr) else 0x10000)
        return 48

    def k_VirtualAlloc(self):
        addr, size = self.arg(0), self.arg(1)
        if addr == 0 or self.mapped(addr):
            return 0
        self.mu.mem_map(addr, (size + 0xfff) & ~0xfff)
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


def make(copy_game_code=True):
    e = GameEmu(DLL, '/tmp')
    if copy_game_code:
        for lo, hi in [(0x293000, 0x295000), (0x2a9000, 0x2aa000), (0x2c5c000, 0x2c5d000), (0x2363000, 0x2364000)]:
            e.mu.mem_write(GAME + lo, game_bytes[lo:hi])
    return e


def limit_used(e, site, verify=True):
    """the float the (rip-relative) instruction at site loads; the opcode must stay the game's"""
    code = bytes(e.mu.mem_read(GAME + site, 8))
    if verify:
        check(code[:4] == game_bytes[site:site + 4], f'Befehl bei {site:#x} unveraendert')
    disp = struct.unpack('<i', code[4:8])[0]
    return e.rf(GAME + site + 8 + disp)


def wheel_shown(e, steer, rotation):
    """in-game wheel angle in degrees for a wheel at steer (-1..1), following the game's code:
    angle = steer * rotation / 2, clamped to +-[clamp constant], animation value = angle /
    [normal constant]; the animation (cockpit.c) spans +-rotation/2"""
    lim, normal = limit_used(e, CLAMP, False), limit_used(e, GETTER + 0x10, False)
    angle = max(-lim, min(lim, steer * rotation / 2 * math.pi / 180))
    return angle / normal * rotation / 2


print('Original (zeigt den Fehler):')
e = make()
check(abs(limit_used(e, CLAMP) - math.pi / 2) < 1e-6, 'Spiel begrenzt auf 90 Grad')
check(abs(wheel_shown(e, 0.2, 900) - 450) < 0.1, 'echtes Rad 90 Grad -> Spiel zeigt 450 (5x zu schnell)')

print('Mit Patch (900 Grad):')
check(e.call('Fh2TestCockpitPatch', GAME, 900) == 1, 'Patch gesetzt')
check(e.call('Fh2TestCockpitPatch', GAME, 900) == 0, 'zweiter Aufruf aendert nichts')
limit = 450 * math.pi / 180
check(abs(limit_used(e, CLAMP) - limit) < 1e-5, 'Grenze +-450 Grad')
check(abs(limit_used(e, GETTER + 0x10) - limit) < 1e-5, 'Animationswert 1 bei 450 Grad')
check(abs(e.rf(GAME + 0x2c5c58c) - math.pi / 2) < 1e-6, 'Konstante pi/2 des Spiels selbst unveraendert')
worst = max(abs(wheel_shown(e, s, 900) - s * 450) for s in (-1, -0.5, -0.1, 0, 0.05, 0.2, 0.5, 0.8, 1))
check(worst < 0.05, f'Lenkrad im Spiel = echtes Rad (max Abweichung {worst:.3f} Grad)')

print('Mit Patch (1080 Grad):')
e = make()
check(e.call('Fh2TestCockpitPatch', GAME, 1080) == 1, 'Patch gesetzt')
worst = max(abs(wheel_shown(e, s, 1080) - s * 540) for s in (-1, -0.3, 0.1, 0.7, 1))
check(worst < 0.05, f'1:1 bei 1080 Grad (max Abweichung {worst:.3f} Grad)')

print('Fremder Spielcode:')
e = make(False)
check(e.call('Fh2TestCockpitPatch', GAME, 900) == 0, 'kein Patch, wenn die Bytes nicht stimmen')

print(f'\n{"FEHLER: " + str(len(fails)) if fails else "Alle Pruefungen ok"}')
sys.exit(1 if fails else 0)
