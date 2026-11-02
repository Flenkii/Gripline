"""Gripline menu on the controller through its own XInput reading (Marvin 02.11.2026: after an FH2MP
update the menu opened from the controller but could not be moved or closed)."""
import os, sys, struct, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
pad = {'buttons': 0, 'ly': 0}

class X(Emu):
    def k_QueryPerformanceCounter(s): s.wq(s.arg(0), 1_000_000); return 1
    def k_QueryPerformanceFrequency(s): s.wq(s.arg(0), 1_000_000); return 1
    def k_GetCurrentProcessId(s): return 4242
    def k_WideCharToMultiByte(s):
        b = s.wstr(s.arg(2)).encode('utf-8') + b'\0'
        if s.arg(4) and s.arg(5): s.mu.mem_write(s.arg(4), b[:s.arg(5) & 0xffffffff])
        return len(b)
    def k_LoadLibraryExW(s):
        return 0x7800000 if 'xinput1_4' in s.wstr(s.arg(0)).lower() else 0
    def k_GetProcAddress(s):
        name = bytes(s.mu.mem_read(s.arg(1), 40)).split(b'\0')[0].decode()
        if name == 'XInputGetState': return s.stub('XInputGetState', s.x_get)
        return 0
    def x_get(s):
        if s.arg(0) != 0: return 1167  # not connected
        s.mu.mem_write(s.arg(1), struct.pack('<IHBBhhhh', 1, pad['buttons'], 0, 0, 0, pad['ly'], 0, 0))
        return 0

root = tempfile.mkdtemp()
e = X(DLL, root)
fails = []
def check(ok, what):
    if not ok: fails.append(what)
check(e.call('Fh2TestKeyMenuXInput') == 1, 'XInput loaded')
e.call('Fh2TestKeyboardAsync', e.stub('ak', lambda: 0), e.stub('fg', lambda: 0x1234),
       e.stub('wp', lambda: (e.wd(e.arg(1), 4242) if e.arg(1) else 0, 1)[1]), e.stub('rb', lambda: e.arg(0)))
e.call('Fh2TestKeyboardSetup', e.stub('rf', lambda: 0), e.put_wstr('C:\\test'))
now = [10_000]
def tick():
    now[0] += 40; e.tick += 40
    return e.call('Fh2TestKeyMenuTick', now[0])
def press(b=0, ly=0):
    pad['buttons'], pad['ly'] = b, ly; r = tick()
    pad['buttons'], pad['ly'] = 0, 0; tick(); return r
# the game's pad buffer is blanked while the menu is open (the hook) - the menu must not care
check(tick() == 0, 'closed at start')
pad['buttons'] = 0x0040; tick(); press(0x0040 | 0x0008)
lvl = tick()
check(lvl != 0, 'LS + D-pad right opens')
e.call('Fh2TestKeyMenuPad', 0, 0)  # blank buffer seen by the hook
press(0x0002)  # D-pad down
press(0x1000)  # A: into a page
lvl_page = tick()
check(lvl_page != lvl, 'A opens a page after moving (level %d -> %d)' % (lvl, lvl_page))
press(0x2000)  # B: back
check(tick() == lvl, 'B goes back')
pad['buttons'] = 0x0040; tick(); press(0x0040 | 0x0008); pad['buttons'] = 0; tick()
check(tick() == 0, 'LS + D-pad right closes again')
print('%d checks, %d failed' % (5, len(fails)) + ('' if not fails else ': ' + '; '.join(fails)))
