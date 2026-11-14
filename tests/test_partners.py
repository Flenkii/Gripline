"""V7.2: other mods that already do a Gripline part (features/mod_partners.c), with the real compiled test DLL.

FH2 Mouse & Keyboard installed and switched on -> Gripline leaves it keyboard driving, menu mouse and mouse look;
its own switches in FH2Mouse\\FH2Mouse.ini and [Gripline] YieldToMods=0 in Gripline.ini keep Gripline's parts."""
import os, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


root = tempfile.mkdtemp(prefix='eo-partners-')
e = Emu(DLL, root)
directory = e.put_wstr('C:\\test')
version = e.alloc(16)
e.mu.mem_write(version, b'1.4.0\0')
KEYS, MENU, LOOK = 1, 2, 4
os.makedirs(os.path.join(root, 'FH2Mouse'), exist_ok=True)
mouse_ini = os.path.join(root, 'FH2Mouse', 'FH2Mouse.ini')
grip_ini = os.path.join(root, 'Gripline.ini')


def detect(installed=True):
    e.call('Fh2TestPartnersDetect', directory, version if installed else 0)
    return e.call('Fh2TestPartnersTaken') & 0xffffffff


def report():
    out = e.alloc(700)
    n = e.call('Fh2TestPartnersReport', out, 700) & 0xffffffff
    return bytes(e.mu.mem_read(out, n)).decode()


print('not installed:')
check(detect(False) == 0, 'nothing taken')
check('FH2 Mouse & Keyboard: not installed' in report(), 'report: not installed')

print('installed, no FH2Mouse.ini yet (its defaults: all on):')
check(detect() == KEYS | MENU | LOOK, 'keyboard, menu mouse and mouse look left to it')
r = report()
check('1.4.0' in r and 'keyboard driving, menu mouse, mouse look' in r, 'report names version and parts: %r' % r)

print('its own switches:')
open(mouse_ini, 'w').write('[Mouse]\r\nEnabled=1\r\nFreeLook=0\r\n[Keyboard]\r\nEnabled=1\r\n')
check(detect() == KEYS | MENU, 'FreeLook=0: Gripline keeps its mouse look')
open(mouse_ini, 'w').write('[Mouse]\r\nEnabled=1\r\n[Keyboard]\r\nEnabled=0\r\n')
check(detect() == MENU | LOOK, '[Keyboard] Enabled=0: Gripline keeps its keyboard')
open(mouse_ini, 'w').write('[Mouse]\r\nEnabled=0\r\n[Keyboard]\r\nEnabled=1\r\n')
check(detect() == 0, '[Mouse] Enabled=0 (whole mod off): Gripline keeps everything')
check('Left to it: nothing' in report(), 'report: nothing left to it')

print('YieldToMods:')
open(mouse_ini, 'w').write('[Mouse]\r\nEnabled=1\r\n')
open(grip_ini, 'w').write('[Gripline]\r\nYieldToMods=0\r\n')
check(detect() == 0, 'YieldToMods=0: Gripline keeps its own parts')
check('off (YieldToMods=0)' in report(), 'report says so')
open(grip_ini, 'w').write('[Gripline]\r\nYieldToMods=1\r\n')
check(detect() == KEYS | MENU | LOOK, 'YieldToMods=1: left to it again')

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
