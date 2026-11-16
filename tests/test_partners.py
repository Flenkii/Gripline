"""V7.2: other mods that already do a Gripline part (features/mod_partners.c), with the real compiled test DLL.

FH2 Mouse & Keyboard installed and switched on -> Gripline leaves it keyboard driving, menu mouse and mouse look;
its own switches in FH2Mouse\\FH2Mouse.ini and [Gripline] YieldToMods=0 in Gripline.ini keep Gripline's parts."""
import os, struct, sys, tempfile
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
class PEmu(Emu):
    def k_VirtualProtect(self):
        if self.arg(3):
            self.wd(self.arg(3), 0x02)
        return 1


e = PEmu(DLL, root)
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

print('walking: FH2Mouse driving keys held back from the parked car (V7.2):')
keys_down = set()
def original_key_state():
    vk = e.arg(0) & 0xffffffff
    return 0x8000 if vk in keys_down else 0
key_stub = e.stub('orig_GetAsyncKeyState', original_key_state)
M = e.alloc(0x1000, 0x1000)
img = bytearray(0x1000)
struct.pack_into('<H', img, 0, 0x5a4d)
struct.pack_into('<I', img, 0x3c, 0x80)
img[0x80:0x84] = b'PE\0\0'
struct.pack_into('<HH', img, 0x84, 0x8664, 0)
struct.pack_into('<H', img, 0x84 + 16, 240)
opt = 0x80 + 24
struct.pack_into('<H', img, opt, 0x20b)
struct.pack_into('<II', img, opt + 112 + 8, 0x400, 40)
struct.pack_into('<IIIII', img, 0x400, 0x500, 0, 0, 0x600, 0x700)
struct.pack_into('<QQQ', img, 0x500, 0x800, 0x820, 0)
img[0x600:0x60b] = b'USER32.dll\0'
img[0x802:0x812] = b'GetCursorPos\0\0\0\0'
img[0x822:0x833] = b'GetAsyncKeyState\0'
cursor_stub = e.stub('orig_GetCursorPos', lambda: 1)
struct.pack_into('<QQQ', img, 0x700, cursor_stub, key_stub, 0)
e.mu.mem_write(M, bytes(img))
slot = M + 0x708
open(mouse_ini, 'w').write('[Mouse]\r\nEnabled=1\r\n[Keyboard]\r\nEnabled=1\r\nGas=W Up\r\nBrake=S\r\nSteerLeft=A\r\n'
                           'SteerRight=D\r\nHandbrake=Space\r\nShiftDown=C\r\nNitro=LCtrl\r\nHorn=E\r\nRewind=R\r\n'
                           'Camera=V\r\nButtonRT=C\r\nMap=M\r\nAccept=Enter\r\n')
check(e.call('Fh2TestPartnersWalkLink', M, e.put_wstr('C:\\test\\FH2Mouse\\FH2Mouse.ini')) == 1, 'FH2Mouse.dll linked')
check(e.rq(slot) == e.call('Fh2TestPartnersKeyHook'), 'its GetAsyncKeyState slot points to Gripline')
check(e.rq(M + 0x700) == cursor_stub, 'its other imports untouched')
blocked = lambda vk: e.call('Fh2TestPartnersWalkBlocked', vk) & 0xffffffff
check(all(blocked(ord(c)) for c in 'WASDER') and blocked(0x20) and blocked(0xa2), 'W A S D, Space, LCtrl, E, R held back')
check(not blocked(0x26), 'Up is a menu key too: stays (pause menu while walking)')
check(not blocked(ord('C')), 'C is also ButtonRT in the menus: stays')
check(not blocked(ord('V')) and not blocked(ord('M')) and not blocked(0x0d), 'camera, map, Enter stay')
keys_down.update([ord('W'), ord('M')])
ks = lambda vk: e.call('Fh2TestPartnersKeyState', vk) & 0xffff
e.call('Fh2TestPartnersWalking', 0)
check(ks(ord('W')) == 0x8000, 'driving: W reaches FH2Mouse')
e.call('Fh2TestPartnersWalking', 1)
check(ks(ord('W')) == 0, 'walking: W does not reach FH2Mouse')
check(ks(ord('M')) == 0x8000, 'walking: M (map) still reaches it')
e.call('Fh2TestPartnersWalking', 0)
check(ks(ord('W')) == 0x8000, 'back in the car: W drives again')
check(e.call('Fh2TestPartnersCameraKey') == ord('V'), 'its camera key V known (getting out without a controller)')
check(ks(ord('V')) == 0, 'no camera change wanted: V as it is (up)')
e.call('Fh2TestCockpitWantOutside', 1)
check(ks(ord('V')) == 0x8000, 'getting out, no controller read: V pressed for FH2Mouse (next camera)')
e.call('Fh2TestCockpitWantOutside', 0)
check(ks(ord('V')) == 0, 'done: V up again')
check(e.call('Fh2TestPartnersWalkLink', M, e.put_wstr('C:\\test\\FH2Mouse\\FH2Mouse.ini')) == 1 and
      e.rq(slot) == e.call('Fh2TestPartnersKeyHook'), 'second link keeps one hook')

print('Extra Options turn signals (V7.2):')
def eo(text):
    b = text.encode()
    return e.call('Fh2TestPartnersEoScan', e.put_bytes(b), len(b))
real = os.environ.get('EO_INI')
if real and os.path.exists(real):
    check(eo(open(real, 'rb').read().decode('latin-1')) == 0, 'EO 0.6.1 INI (Marvin): nothing of its own')
check(eo('[Bindings]\r\nSignalLeftXbox=DPadLeft\r\nSignalRightKeyboard=None\r\nHazardsXbox=None\r\nIndicatorsOnDpad=1\r\n') == 0,
      'only button keys: nothing')
check(eo('[Vehicle]\r\nIndicatorSound=1\r\n') == 8, 'IndicatorSound=1: its sound')
check(eo('[Vehicle]\r\nTurnSignalHud=On\r\n') == 16, 'TurnSignalHud=On: its display')
check(eo('[Vehicle]\r\nIndicatorSound=0\r\nTurnSignalHud=Off\r\n; BlinkerArrows=1\r\n') == 0, 'switched off or comment: nothing')
check(eo('[HUD]\r\nTurn_Signal_Arrows = true\r\nHazardClickVolume=80\r\n') == 24, 'both, with spaces and _')

open(os.path.join(root, 'FH2_ExtraOptions.ini'), 'wb').write(b'[Vehicle]\r\nIndicatorSound=1\r\nIndicatorHud=1\r\n')
e.call('Fh2TestPartnersDetect', e.put_wstr('C:\\test'), 0)
check(e.call('Fh2TestPartnersTaken') & 24 == 24, 'INI with both: Gripline leaves sound and arrows to Extra Options')
os.remove(os.path.join(root, 'FH2_ExtraOptions.ini'))
e.call('Fh2TestPartnersDetect', e.put_wstr('C:\\test'), 0)
check(e.call('Fh2TestPartnersTaken') & 24 == 0, 'no INI: Gripline keeps its own')

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
