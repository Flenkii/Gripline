#!/usr/bin/env python3
"""Renders the walking gauge looks of the test DLL (real DLL pixels; the GDI text is drawn with PIL
fonts, so letter shapes differ a little from Segoe UI / Bahnschrift in the game).

python tools/render_walk_gauge.py out-test/fh2-mods/Gripline.dll out.png [background.jpg]
"""
import os, sys, struct, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests'))
from emu import Emu
import unicorn
from unicorn.x86_const import UC_X86_REG_RSP
from PIL import Image, ImageDraw, ImageFont

FONT_DIR = '/usr/share/fonts/truetype/liberation/'
FONTS = {(False, False): 'LiberationSans-Regular.ttf', (True, False): 'LiberationSans-Bold.ttf',
         (False, True): 'LiberationSans-Italic.ttf', (True, True): 'LiberationSans-BoldItalic.ttf'}


class R(Emu):
    def __init__(s, *a):
        super().__init__(*a)
        s.fonts, s.cur, s.dib, s.nh = {}, None, None, 0x5000

    def font(s):
        size, bold, italic = s.cur
        return ImageFont.truetype(FONT_DIR + FONTS[(bold, italic)], max(4, int(size * 0.9)))

    def k_LoadLibraryExW(s): return 0x7700000 if 'gdi32' in s.wstr(s.arg(0)).lower() else 0

    def k_GetProcAddress(s):
        name = bytes(s.mu.mem_read(s.arg(1), 40)).split(b'\0')[0].decode()
        return s.stub('gdi_' + name, getattr(s, 'g_' + name, lambda: 1))

    def g_CreateCompatibleDC(s): return 0x4000

    def g_CreateDIBSection(s):
        info = s.arg(1)
        w, h = struct.unpack('<ii', bytes(s.mu.mem_read(info + 4, 8)))
        h = -h
        buf = s.alloc(w * h * 4)
        s.wq(s.arg(3), buf)
        s.dib = (buf, w, h)
        return 0x4100

    def g_CreateFontW(s):
        s.nh += 1
        h = s.arg(0)
        h = h - (1 << 64) if h >= 1 << 63 else h
        h = h - (1 << 32) if h >= 1 << 31 else h
        s.fonts[s.nh] = (abs(h), (s.arg(4) & 0xffffffff) >= 600, (s.arg(5) & 0xff) != 0)
        return s.nh

    def g_SelectObject(s):
        if s.arg(1) in s.fonts:
            s.cur = s.fonts[s.arg(1)]
        return 1

    def g_GetTextExtentPoint32W(s):
        t = bytes(s.mu.mem_read(s.arg(1), s.arg(2) * 2)).decode('utf-16le')
        f = s.font()
        s.wd(s.arg(3), int(f.getlength(t)))
        s.wd(s.arg(3) + 4, s.cur[0])
        return 1

    def g_GetTextMetricsW(s):
        f = s.font()
        ascent, descent = f.getmetrics()
        tm = bytearray(60)
        struct.pack_into('<iiiii', tm, 0, ascent + descent, ascent, descent, int(ascent * 0.25), 0)
        s.mu.mem_write(s.arg(1), bytes(tm))
        return 1

    def g_ExtTextOutW(s):
        sp = s.mu.reg_read(UC_X86_REG_RSP)
        rect, text, n = struct.unpack('<QQQ', bytes(s.mu.mem_read(sp + 0x28, 24)))
        t = bytes(s.mu.mem_read(text, (n & 0xffffffff) * 2)).decode('utf-16le')
        x, y = s.arg(1), s.arg(2)
        x = x - (1 << 32) if x >= 1 << 31 else x
        y = y - (1 << 32) if y >= 1 << 31 else y
        buf, w, h = s.dib
        img = Image.frombytes('RGBA', (w, h), bytes(s.mu.mem_read(buf, w * h * 4)))
        ImageDraw.Draw(img).text((x, y), t, font=s.font(), fill=(255, 255, 255, 255))
        s.mu.mem_write(buf, img.tobytes())
        return 1


def render(e, which, h, mode, speed, dist, ahead, right, flags):
    w_ = {1: (330, 380), 2: (630, 152), 3: (440, 250)}[which]
    s = h / 1080
    w, hh = int(w_[0] * s), int(w_[1] * s)
    pix = e.alloc(w * hh * 4)
    ok = e.call('Fh2TestWalkGauge', which, h, mode, int(speed * 100), int(dist * 100), int(ahead * 1000),
                int(right * 1000) & 0xffffffff, flags, pix)
    assert ok == 1, 'not drawn'
    return Image.frombytes('RGBA', (w, hh), bytes(e.mu.mem_read(pix, w * hh * 4)), 'raw', 'BGRA')


def over(bg, img, x, y):
    bp, ip = bg.load(), img.load()
    for yy in range(img.height):
        for xx in range(img.width):
            r, g, b, a = ip[xx, yy]
            o = bp[x + xx, y + yy]
            bp[x + xx, y + yy] = tuple(min(255, int(c + oc * (1 - a / 255))) for c, oc in zip((r, g, b), o))


if __name__ == '__main__':
    DLL, OUT = sys.argv[1], sys.argv[2]
    BG = sys.argv[3] if len(sys.argv) > 3 else None
    e = R(DLL, tempfile.mkdtemp())
    H = 1080
    base = Image.open(BG).convert('RGB').resize((1920, 1080)) if BG else Image.new('RGB', (1920, 1080), (90, 100, 80))
    # walking (de), running (de), crouching near the car (en, mph)
    shots = [(1, 2, 6.0, 34.0, 0.6, -0.8, 1 | 4), (2, 2, 6.0, 34.0, 0.6, -0.8, 1), (3, 1, 2.0, 34.0, 0.6, -0.8, 1 | 4),
             (3, 3, 1.1, 2.5, 1.0, 0.0, 2)]
    tiles = []
    for which, mode, speed, dist, ahead, right, flags in shots:
        img = render(e, which, H, mode, speed, dist, ahead, right, flags)
        frame = base.copy()
        m = int(30 * H / 1080)
        over(frame, img, 1920 - m - img.width, 1080 - m - img.height)
        tiles.append(frame.crop((1920 - 700, 1080 - 440, 1920, 1080)))
    sheet = Image.new('RGB', (700 * 2, 440 * 2))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % 2) * 700, (i // 2) * 440))
    sheet.save(OUT)
    print('ok', OUT)
