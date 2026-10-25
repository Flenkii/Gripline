"""Wheel setup assistant (any wheel): cards ask for steering, pedals and buttons; the axis that
moves most / the button that goes down is taken, used at once and saved to FH2_Wheel.ini."""
import os, struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
fails = []

def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)

root = tempfile.mkdtemp(prefix='eo-setup-')
ini = os.path.join(root, 'FH2_Wheel.ini')
open(ini, 'wb').write(b'[Wheel]\r\n; Axes\r\nSteeringAxis=X\r\nThrottleAxis=Y\r\nBrakeAxis=Rz\r\nClutchAxis=Slider0\r\n'
                      b'PaddleRight=4\r\nPaddleLeft=5\r\nButtonA=0\r\nButtonB=1\r\n')
e = Emu(DLL, root)
axes_mem = e.alloc(32)
line1, line2 = e.alloc(200), e.alloc(200)
path = e.alloc(600)
e.mu.mem_write(path, r'C:\test\FH2_Wheel.ini'.encode('utf-16-le') + bytes(2))

# a "Thrustmaster-like" wheel: steering Z (2), throttle Rx (3), brake Ry (4), no clutch, pedals rest at 0
rest = [32768, 32768, 32768, 0, 0, 65535, 32768, 32768]

def feed(axes, buttons=0, ms=20):
    e.mu.mem_write(axes_mem, struct.pack('<8i', *axes))
    r = e.call('Fh2TestWheelSetupFeed', axes_mem, buttons)
    e.tick += ms
    return r

def card():
    e.call('Fh2TestNotifyText', 0, line1, line2)
    return e.wstr(line1) + ' / ' + e.wstr(line2)

def with_axis(i, v):
    a = list(rest)
    a[i] = v
    return a

check(feed(rest) & 1 == 0, 'not started: the game gets the wheel')
e.call('Fh2TestWheelSetupStart', path)
check(feed(rest) >> 4 == 1, 'started: step 1')
check(card().startswith('Wheel setup 1/8'), 'card: ' + card())
for v in (32768, 20000, 0, 20000, 45000, 65535, 40000, 33000):
    feed(with_axis(2, v))
check(feed(rest) >> 4 == 2, 'steering found, step 2: throttle')
check(card().startswith('Wheel setup 2/8'), 'card replaced in place: ' + card())
for v in (0, 30000, 65535, 20000, 0):
    feed(with_axis(3, v))
check(feed(rest) >> 4 == 3, 'throttle found, step 3: brake')
for v in (0, 50000, 60000, 1000):
    feed(with_axis(4, v))
check(feed(rest) >> 4 == 4, 'brake found, step 4: clutch')
r = 0
for _ in range(600):  # 12 s with nothing: clutch skipped (button 7 held all along)
    r = feed(rest, 1 << 7)
    if r >> 4 != 4: break
check(r >> 4 == 5, 'no clutch: skipped after 10 s, step 5 (paddles)')
feed(rest, 1 << 7); feed(rest, 1 << 7)  # held since before the step: does not count
feed(rest, 0)
feed(rest, 1 << 9); feed(rest, 0)
check(feed(rest) >> 4 == 6, 'right paddle = button 9')
feed(rest, 1 << 8); feed(rest, 0)
check(feed(rest) >> 4 == 7, 'left paddle = button 8')
feed(rest, 1 << 2); feed(rest, 0)
for _ in range(600):  # B skipped
    r = feed(rest)
    if r >> 4 == 0: break
check(r >> 4 == 0, 'finished')
res = [e.call('Fh2TestWheelSetupResult', k) for k in range(8)]
res = [x - (1 << 32) if x >= 1 << 31 else x for x in res]
check(res == [2, 3, 4, -1, 9, 8, 2, 0], f'used at once (B skipped: keeps cfg, 0 in this test): {res}')
check(card().startswith('Wheel set up and saved'), 'card: ' + card())
text = open(ini, 'rb').read().decode()
for key in ('SteeringAxis=Z', 'ThrottleAxis=Rx', 'BrakeAxis=Ry', 'ClutchAxis=-', 'PaddleRight=9', 'PaddleLeft=8',
            'ButtonA=2', 'ButtonB=0', 'SetupDone=1', '; Axes'):
    check(key in text, 'FH2_Wheel.ini: ' + key)
check('\r\n' in text and '\n\n' not in text.replace('\r\n', ''), 'line endings kept')

print('cancel:')
e.call('Fh2TestWheelSetupStart', path)
feed(rest)
for _ in range(1100):
    r = feed(rest)
    if r >> 4 == 0: break
check(r >> 4 == 0 and card().startswith('Wheel setup cancelled'), 'nothing turned for 20 s: cancelled, ' + card())
res2 = [e.call('Fh2TestWheelSetupResult', k) for k in range(3)]
check(res2 == [2, 3, 4], 'cancelled: values unchanged')

print(f'\n{len(fails)} FEHLER' if fails else '\nalles ok')
sys.exit(1 if fails else 0)
