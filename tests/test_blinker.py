"""V7.2 turn signal display (features/blinker.c): reads the game's light inputs of the car, keeps the
game's rhythm (0.36 s on, 0.33 s off, new start at a change), makes the clicks and draws the arrows
above the speedometer."""
import os, struct, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu

DLL = sys.argv[1]
fails, checks = [], [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


e = Emu(DLL, tempfile.mkdtemp(prefix='gl-blink-'))
NONE, LEFT, RIGHT, HAZARD = range(4)


def step(h, l, r, ms=10):
    v = e.call('Fh2TestBlinkerStep', h, l, r, ms)
    return v & 15, (v >> 4) & 1


print('which signal:')
check(step(0, 0, 0) == (NONE, 0), 'nothing set: off')
check(step(0, 1, 0)[0] == LEFT, 'left byte: left')
check(step(0, 0, 1)[0] == RIGHT, 'right byte: right')
check(step(1, 0, 0)[0] == HAZARD and step(1, 1, 0)[0] == HAZARD, 'hazard byte wins')
check(step(0, 1, 1)[0] == NONE, 'left and right without hazards: nothing (as the game)')
step(0, 0, 0)

print('rhythm (10 ms steps):')
lit = []
step(0, 1, 0, 10)  # the change: starts at 0 + 10 ms
for i in range(1, 140):
    lit.append(step(0, 1, 0, 10)[1])
# timeline after k more steps: 10 + 10k ms
first_off = next(i for i, v in enumerate(lit) if not v)
check(abs((first_off + 2) * 10 - 360) <= 10, f'on for 0.36 s (off after {(first_off + 2) * 10} ms)')
first_on_again = next(i for i, v in enumerate(lit) if i > first_off and v)
check(abs((first_on_again + 2) * 10 - 690) <= 10, f'on again after 0.69 s ({(first_on_again + 2) * 10} ms)')
mode, now = step(1, 1, 0, 10)
check(mode == HAZARD and now == 1, 'switch to hazards: new start, lit at once')
check(step(1, 1, 0, 0)[1] == 1, 'game stands (dt 0): stays as it is')

print('clicks:')
buf = e.alloc(4000 * 2)
for tock in (0, 1):
    n = e.call('Fh2TestBlinkerClick', tock, 100, buf, 4000)
    s = struct.unpack('<%dh' % n, bytes(e.mu.mem_read(buf, n * 2)))
    peak = max(abs(x) for x in s)
    check(n > 2000 and 12000 < peak < 15500, f'{"tock" if tock else "tick"}: {n} samples, peak {peak}')
    check(max(abs(x) for x in s[-100:]) < 200, 'fades out at the end')
n = e.call('Fh2TestBlinkerClick', 0, 20, buf, 4000)
s = struct.unpack('<%dh' % n, bytes(e.mu.mem_read(buf, n * 2)))
check(2500 < max(abs(x) for x in s) < 3200, '20 %: quieter')

print('arrows:')
W, H = 520, 1080
frame = e.alloc(W * H * 4)


def draw(mode, lit):
    e.mu.mem_write(frame, bytes(W * H * 4))
    e.call('Fh2TestBlinkerDraw', frame, W, H, mode, lit)
    return bytes(e.mu.mem_read(frame, W * H * 4))


def green_x(px):
    xs = [(i // 4) % W for i in range(0, len(px), 4) if px[i + 1] > 150 and px[i + 1] > px[i + 2] + 60 and px[i + 3] > 200]
    return xs


px = draw(LEFT, 1)
xs = green_x(px)
check(xs and max(xs) < W - 241, f'left lit: green left of the speedometer centre ({len(xs)} px)')
px = draw(RIGHT, 1)
xs = green_x(px)
check(xs and min(xs) > W - 241, f'right lit: green right of the centre ({len(xs)} px)')
check(len(green_x(draw(HAZARD, 1))) > 1.8 * len(xs), 'hazards: both')
check(not green_x(draw(LEFT, 0)), 'off phase: no green')
px = draw(LEFT, 0)
check(any(px[i + 3] for i in range(0, len(px), 4)), 'off phase: dark arrows stay')
bad = sum(1 for i in range(0, len(px), 4) if max(px[i], px[i + 1], px[i + 2]) > px[i + 3] + 1)
px = draw(HAZARD, 1)
bad += sum(1 for i in range(0, len(px), 4) if max(px[i], px[i + 1], px[i + 2]) > px[i + 3] + 1)
check(bad == 0, f'premultiplied ({bad} wrong pixels)')
rows = [(i // 4) // W for i in range(0, len(px), 4) if px[i + 3]]
check(min(rows) > H * 0.62 and max(rows) < H * 0.74, f'above the speedometer (rows {min(rows)}..{max(rows)})')

if os.environ.get('BLINKER_PNG'):
    from PIL import Image
    img = Image.frombytes('RGBA', (W, H), draw(HAZARD, 1), 'raw', 'BGRA')
    img.save(os.environ['BLINKER_PNG'])

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
