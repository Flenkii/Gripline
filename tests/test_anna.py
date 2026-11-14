"""V7.2: Gripline's entries in FH2 ANNA's menu (features/anna_link.c), with the real compiled test DLL.
test_plugin.py checks that the list is added; this checks what the entries say and do."""
import sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


e = Emu(DLL, tempfile.mkdtemp(prefix='eo-anna-'))
result = e.alloc(8)


def cstr(a):
    out = b''
    while a and len(out) < 200:
        c = bytes(e.mu.mem_read(a + len(out), 1))
        if c == b'\0':
            break
        out += c
    return out.decode('utf-8', 'replace')


def label(i):
    return cstr(e.call('Fh2TestAnnaEntry', i, 0, result))


def enabled(i):
    e.call('Fh2TestAnnaEntry', i, 1, result)
    return e.rd(result)


def choose(i):
    e.call('Fh2TestAnnaEntry', i, 2, result)


DASH, WALK, CRUISE, TELEMETRY, FREECAM, MENU = range(6)
print('labels (no game yet: English):')
check(label(DASH) == 'Enable Dashboard Cam', 'dashboard cam: %r' % label(DASH))
check(label(WALK) == 'Get Out And Walk', 'walking: %r' % label(WALK))
check(label(CRUISE) == 'Enable Cruise Control', 'cruise: %r' % label(CRUISE))
check(label(TELEMETRY) in ('Show Telemetry', 'Hide Telemetry'), 'telemetry: %r' % label(TELEMETRY))
check(label(FREECAM) == 'Free Camera' and label(MENU) == 'Gripline Menu', 'freecam, menu')

print('dashboard cam:')
check(enabled(DASH) == 0, 'greyed out while the driver camera is not on')
choose(DASH)
check(label(DASH) == 'Disable Dashboard Cam', 'chosen: on, the label offers to switch it off')
choose(DASH)
check(label(DASH) == 'Enable Dashboard Cam', 'chosen again: off')

print('offline only:')
check(enabled(WALK) == 1 and enabled(FREECAM) == 1, 'walking and freecam offered offline')
choose(WALK)
choose(MENU)
check(True, 'walking and menu requests do not crash (handled on their own threads)')

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
