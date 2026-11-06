#!/usr/bin/env python3
"""Renders the real card/panel pixels of the test DLL (fake GDI; text drawn with PIL where the DLL asks)."""
import sys, struct, tempfile
sys.path.insert(0, '/home/claude/gripline/tests')
from emu import Emu
from PIL import Image, ImageDraw, ImageFont
DLL, OUT = sys.argv[1], sys.argv[2]
BG = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != '-' else None
FONTS = {(False): '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', True: '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'}

class R(Emu):
    def __init__(s, *a):
        super().__init__(*a); s.fonts = {}; s.cur = None; s.color = 0; s.dib = None; s.texts = []; s.nh = 0x5000
    def k_WideCharToMultiByte(s):
        src, dst, size = s.arg(2), s.arg(4), s.arg(5) & 0xffffffff
        b = s.wstr(src).encode('utf-8') + b'\0'
        if dst and size: s.mu.mem_write(dst, b[:size])
        return len(b)
    def k_LoadLibraryExW(s): return 0x7700000 if 'gdi32' in s.wstr(s.arg(0)).lower() else 0
    def k_GetProcAddress(s):
        name = bytes(s.mu.mem_read(s.arg(1), 40)).split(b'\0')[0].decode()
        return s.stub('gdi_' + name, getattr(s, 'g_' + name, lambda: 1))
    def k_GetCurrentProcessId(s): return 4242
    def k_QueryPerformanceCounter(s): s.wq(s.arg(0), 1); return 1
    def k_QueryPerformanceFrequency(s): s.wq(s.arg(0), 1000000); return 1
    def g_CreateCompatibleDC(s): return 0x4000
    def g_CreateDIBSection(s):
        info = s.arg(1); w, h = struct.unpack('<ii', bytes(s.mu.mem_read(info + 4, 8))); h = -h
        buf = s.alloc(w * h * 4); s.wq(s.arg(3), buf); s.dib = (buf, w, h); s.texts = []
        return 0x4100
    def g_CreateFontW(s):
        s.nh += 1; h = s.arg(0)
        if h >= 1 << 31: h -= 1 << 32
        weight = struct.unpack('<q', bytes(s.mu.mem_read(s.mu.reg_read(__import__('unicorn').x86_const.UC_X86_REG_RSP) + 0x28, 8)))[0]
        s.fonts[s.nh] = (abs(h), weight >= 600); return s.nh
    def g_SelectObject(s):
        if s.arg(1) in s.fonts: s.cur = s.fonts[s.arg(1)]
        return 1
    def g_SetTextColor(s): s.color = s.arg(1); return 0
    def g_ExtTextOutW(s):
        sp = s.mu.reg_read(__import__('unicorn').x86_const.UC_X86_REG_RSP)
        rect, text, n = struct.unpack('<QQQ', bytes(s.mu.mem_read(sp + 0x28, 24)))
        n &= 0xffffffff
        t = bytes(s.mu.mem_read(text, n * 2)).decode('utf-16le')
        clip = struct.unpack('<4i', bytes(s.mu.mem_read(rect, 16)))
        x, y = s.arg(1), s.arg(2)
        x = x - (1 << 32) if x >= 1 << 31 else x; y = y - (1 << 32) if y >= 1 << 31 else y
        buf, w, h = s.dib
        img = Image.frombytes('RGBA', (w, h), bytes(s.mu.mem_read(buf, w * h * 4)))
        layer = Image.new('RGBA', (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
        size, bold = s.cur
        c = s.color; col = (c & 255, (c >> 8) & 255, (c >> 16) & 255)  # COLORREF -> BGR bytes in DIB
        d.text((x, y), t, font=ImageFont.truetype(FONTS[bold], int(size * 0.92)), fill=(col[2], col[1], col[0], 255))
        mask = Image.new('L', (w, h), 0); ImageDraw.Draw(mask).rectangle((clip[0], clip[1], clip[2] - 1, clip[3] - 1), fill=255)
        la = Image.composite(layer, Image.new('RGBA', (w, h), (0, 0, 0, 0)), mask)
        base = img.copy(); a = la.getchannel('A')
        base.paste(la, (0, 0), a)
        out = bytearray(base.tobytes())
        old = bytes(s.mu.mem_read(buf, w * h * 4))
        for i in range(3, len(out), 4): out[i] = old[i]
        s.mu.mem_write(buf, bytes(out))
        return 1

root = tempfile.mkdtemp()
e = R(DLL, root)
STYLE = int(sys.argv[4]) if len(sys.argv) > 4 else -1
if STYLE >= 0: e.call('Fh2TestNotifySetStyle', STYLE)
def show(t, tint=-1, badge=None):
    e.call('Fh2TestNotifyShow', e.put_bytes(t.encode()), tint & 0xffffffff, e.put_bytes(badge.encode()) if badge else 0)
H = 1080
cards = [('Noclip on\nW/S/left stick fly, A/D turn, Space/Ctrl up/down', -1, None),
         ('Press F8 to change your keys\nKey menu: arrows, Enter, Esc', -1, None),
         ('Lenkrad getrennt\nG920 nicht mehr gefunden', 1, 'WARNUNG')]
for t in reversed(cards): show(*t)
w_, r_ = e.alloc(4), e.alloc(4)
e.call('Fh2TestNotifyDraw', H, 0, 0, w_, r_)
w, rows = e.rd(w_), e.rd(r_)
pix = e.alloc(w * rows * 4)
ok = e.call('Fh2TestNotifyDraw', H, pix, w * 4, w_, r_)
data = bytes(e.mu.mem_read(pix, w * rows * 4))
img = Image.frombytes('RGBA', (w, rows), data, 'raw', 'BGRA')
bg = Image.open(BG).convert('RGB').resize((w + 40, rows + 40)) if BG else Image.new('RGB', (w + 40, rows + 40), (60, 70, 55))
# premultiplied over
bgp = bg.load(); ip = img.load()
for y in range(rows):
    for x in range(w):
        r, g, b, a = ip[x, y]; o = bgp[x + 20, y + 20]
        bgp[x + 20, y + 20] = tuple(min(255, int(c + oc * (1 - a / 255))) for c, oc in zip((r, g, b), o))
bg.save(OUT); print('ok', ok, w, rows)

# ---- key menu panel (real key menu pages)
keys = set()
e.k_QueryPerformanceCounter = lambda: (e.wq(e.arg(0), 1), 1)[1]
e.call('Fh2TestKeyboardAsync', e.stub('ak', lambda: 0x8000 if (e.arg(0) & 0xff) in keys else 0), e.stub('fg', lambda: 0x1234),
       e.stub('wp', lambda: (e.wd(e.arg(1), 4242) if e.arg(1) else 0, 1)[1]), e.stub('rb', lambda: e.arg(0)))
e.call('Fh2TestKeyboardSetup', e.stub('rf', lambda: 0), e.put_wstr('C:\\test'))
t = [5000]
def tick():
    t[0] += 40; e.tick += 40; return e.call('Fh2TestKeyMenuTick', t[0])
def press(v):
    keys.add(v); tick(); keys.discard(v); tick()
for v in (0x77, 0x28, 0x28, 0x0d, 0x28, 0x28):
    press(v)
w_, r_, c_ = e.alloc(4), e.alloc(4), e.alloc(4)
l_, k_ = e.alloc(64), e.alloc(64)
e.call('Fh2TestNotifyPanelDraw', H, 0, 0, w_, r_, c_, l_, k_)
w, rows, cnt = e.rd(w_), e.rd(r_), e.rd(c_)
pix = e.alloc(w * rows * 4)
e.call('Fh2TestNotifyPanelDraw', H, pix, w * 4, w_, r_, c_, l_, k_)
img = Image.frombytes('RGBA', (w, rows), bytes(e.mu.mem_read(pix, w * rows * 4)), 'raw', 'BGRA')
ph = rows
bg2 = Image.new('RGB', (w + 40, ph + 40), (60, 70, 55)); bp = bg2.load(); ip = img.load()
for y in range(min(rows, ph)):
    for x in range(w):
        r, g, b, a = ip[x, y]; o = bp[x + 20, y + 20]
        bp[x + 20, y + 20] = tuple(min(255, int(c + oc * (1 - a / 255))) for c, oc in zip((r, g, b), o))
bg2.save(OUT.replace('.png', '_menu.png')); print('menu rows', cnt)
