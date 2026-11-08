"""V7.1 walking gauge (walk_gauge.c): every look draws at the bottom right, in its own size, with the
car arrow, and the step pulse look leaves the picture around its text see-through."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import render_walk_gauge as rw
import tempfile

DLL = sys.argv[1]
fails, checks = [], [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


e = rw.R(DLL, tempfile.mkdtemp(prefix='gl-gauge-'))
sizes = {1: (330, 380), 2: (630, 152), 3: (440, 250)}
for which, name in ((1, 'Ring'), (2, 'Racing-Leiste'), (3, 'Schritt-Puls')):
    img = rw.render(e, which, 1080, 2, 6.0, 34.0, 1.0, 0.0, 1)
    check(img.size == sizes[which], f'{name}: Groesse {img.size}')
    alpha = img.getchannel('A')
    covered = sum(1 for a in alpha.tobytes() if a > 0)
    check(covered > img.width * img.height * (0.02 if which == 3 else 0.15), f'{name}: gezeichnet ({covered} Pixel)')
    check(alpha.getpixel((0, 0)) == 0, f'{name}: Ecke oben links frei')
    big = rw.render(e, which, 1440, 2, 6.0, 34.0, 1.0, 0.0, 1)  # (4K draws too long for the emulator)
    want = (int(sizes[which][0] * 1440 / 1080), int(sizes[which][1] * 1440 / 1080))
    check(big.size == want, f'{name}: 1440p groesser {big.size}')

# premultiplied: no colour brighter than its alpha
img = rw.render(e, 1, 1080, 1, 2.0, 10.0, 0.0, 1.0, 1)
px = img.tobytes()
bad = sum(1 for i in range(0, len(px), 4) if max(px[i], px[i + 1], px[i + 2]) > px[i + 3] + 1)
check(bad == 0, f'Ring: Farben vormultipliziert ({bad} falsche Pixel)')

# the arrow to the car turns: car right -> more ink right of the arrow centre than with the car left
def arrow_ink(right):
    """the light blue arrow in the bottom line ("AUTO 34 m" is white), as a 0/1 grid round its centre"""
    im = rw.render(e, 3, 1080, 1, 2.0, 34.0, 0.0, right, 1)
    box = im.crop((0, 200, im.width, 245))
    w, p = box.width, box.tobytes()
    ink = {(i // 4 % w, i // 4 // w) for i in range(0, len(p), 4) if p[i + 2] > 180 and p[i] < 170 and p[i + 3] > 128}
    cx = round(sum(x for x, _ in ink) / max(1, len(ink)))
    return {(x - cx, y) for x, y in ink}


ink_r, ink_l = arrow_ink(1.0), arrow_ink(-1.0)
mirrored = {(-x, y) for x, y in ink_r}
same = len(mirrored & ink_l) / max(1, len(ink_l))
plain = len(ink_r & ink_l) / max(1, len(ink_l))
check(len(ink_r) > 20 and same > 0.6 and same > plain + 0.15,
      f'Pfeil zum Auto: rechts = gespiegelt links ({same:.2f} gegen {plain:.2f}, {len(ink_r)} Pixel)')

# near the car the line says "get in"
im_near = rw.render(e, 3, 1080, 0, 0.0, 2.0, 1.0, 0.0, 1)
im_far = rw.render(e, 3, 1080, 0, 0.0, 30.0, 1.0, 0.0, 1)
check(im_near.tobytes() != im_far.tobytes(), 'nah am Auto: andere Zeile (Einsteigen)')

print(f'{checks[0]} checks, {len(fails)} failed')
print('ALLE TESTS OK' if not fails else f'{len(fails)} FEHLER')
sys.exit(1 if fails else 0)
