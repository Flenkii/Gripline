"""V6.2 cards: trip summary text, start version check, wheel ids, card colour + confirmation card."""
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


class FEmu(Emu):
    def k_WideCharToMultiByte(self):  # CP_UTF8, src, -1, dst, size
        src, dst, size = self.arg(2), self.arg(4), self.arg(5) & 0xffffffff
        b = self.wstr(src).encode('utf-8') + b'\0'
        if len(b) > size:
            return 0
        self.mu.mem_write(dst, b)
        return len(b)

    def k_SetFilePointerEx(self):
        self.files[self.arg(0)][1] = self.arg(1)
        return 1


def text(e, name, *args, size=200):
    out = e.alloc(size)
    e.mu.mem_write(out, bytes(size))
    r = e.call(name, *args, out, size)
    return r, bytes(e.mu.mem_read(out, size)).split(b'\0')[0].decode('utf-8')


print('Trip summary')
root = tempfile.mkdtemp(prefix='gl-cards-')
e = FEmu(DLL, root)
for _ in range(600):                      # 60 s at 25 m/s = 1.5 km
    e.call('Fh2TestTripSample', 100, 2500, 0, 0)
for _ in range(10):                       # 1 s at 50 m/s (180 km/h) = 50 m
    e.call('Fh2TestTripSample', 100, 0, 0, 5000)
e.call('Fh2TestTripSample', 100, 30000, 0, 0)   # 300 m/s: teleport, not counted
e.call('Fh2TestTripSample', 500, 2500, 0, 0)    # dt 0.5 s: a hitch, not counted
_, t = text(e, 'Fh2TestTripText')
check(t == 'Trip: 1.6 km\nmax 180 km/h, driving time 1 min', repr(t))

print('Wheel ids')
check(e.call('Fh2TestWheelOf', 0x046d, 0xc262) == 0, 'G920 known')
check(e.call('Fh2TestWheelOf', 0x046d, 0xc52b) & 0xffffffff == 0xffffffff, 'Logitech receiver is no wheel')
check(e.call('Fh2TestWheelOf', 0x0eb7, 0x0004) & 0xffffffff != 0xffffffff, 'any Fanatec')

print('Version check')
def logs(eo, platform):
    d = tempfile.mkdtemp(prefix='gl-ver-')
    os.makedirs(os.path.join(d, 'fh2-mods'))
    open(os.path.join(d, 'FH2_ExtraOptions.log'), 'wb').write(eo)
    open(os.path.join(d, 'fh2-mods', 'platform.log'), 'wb').write(platform)
    return d

good_eo = b'Forza Horizon 2 Extra Options 0.4.4\r\nAZIMUTH: 1.0.4 initialized for FH2 (1A96545B)\r\n'
cases = [
    (good_eo, b'05:03:41 FH2MP.dll loaded\r\n', (1, 0, 4), 0, ''),
    (b'REFUSED: unsupported Azimuth build. Extra Options 0.4.4 supports ...\r\n', b'', (1, 0, 5), 1,
     'Extra Options does not fit Azimuth 1.0.5\nUpdate Extra Options'),
    (b'REFUSED: cannot read FH2_ExtraOptions.ini.\r\n', b'', (1, 0, 4), 1,
     'Extra Options is off\nReason: FH2_ExtraOptions.log (REFUSED)'),
    (good_eo, b'[FH2MP] identity: (Azimuth\'s XboxUserId constant NOT found)\r\n', (1, 0, 4), 1,
     'FH2MP does not fit Azimuth 1.0.4\nWait for an FH2MP update'),
    (good_eo, b'', (1, 1, 0), 1, 'Azimuth 1.1.0 is new\nGripline was not tested with it yet'),
    (good_eo, b'', (1, 0, 3), 0, ''),
]
for eo, platform, version, want, want_text in cases:
    root = logs(eo, platform)
    v = FEmu(DLL, root)
    r, t = text(v, 'Fh2TestVersionCard', 0, v.put_wstr('C:\\test'), *version)
    check(r == want and t == want_text, f'{version}: {r} {t!r}')

print('Confirmation cards (English: no game image)')
RIG_TRAFFIC_LEVEL, RIG_KEY_STEER, RIG_GRIP_NOTIFICATIONS, RIG_GRIP_CARD_COLOR, RIG_TRAFFIC_PRESET = 0, 3, 6, 10, 11
for args, want in [((RIG_GRIP_CARD_COLOR, 3), 'Gripline: Card Colour\nPurple'),
                   ((RIG_GRIP_CARD_COLOR, 0), 'Gripline: Card Colour\nBlue (original)'),
                   ((RIG_TRAFFIC_LEVEL, 4), 'Traffic: Amount\n100 Cars - after a restart'),
                   ((RIG_TRAFFIC_PRESET, 2), 'Traffic: Preset\nMuch More Traffic - after a restart'),
                   ((RIG_GRIP_NOTIFICATIONS, 0), 'Gripline: Notifications\nOff'),
                   ((RIG_KEY_STEER, 250), 'Keyboard: Steering Time (ms)\n250')]:
    _, t = text(e, 'Fh2TestConfirmText', *args)
    check(t == want, f'{args}: {t!r}')

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
