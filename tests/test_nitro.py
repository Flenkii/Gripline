"""Test of the NitroSystem link (features/nitro_link.c) with the real compiled test DLL.

A fake FnFNitro.dll (PE headers + XINPUT1_4 import by ordinal 2/3) is built in emulated memory;
the DLL patches its XInputGetState slot and reports NitroButton while a Gripline source is held."""
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


class NEmu(Emu):
    def k_VirtualProtect(self):
        if self.arg(3):
            self.wd(self.arg(3), 0x02)
        return 1


root = tempfile.mkdtemp(prefix='eo-nitro-')
e = NEmu(DLL, root)

# original XInputGetState of the fake module
pad = {'connected': False, 'buttons': 0}
def original_get_state():
    user, state = e.arg(0), e.arg(1)
    if user != 0 or not pad['connected']:
        return 1167
    e.mu.mem_write(state, struct.pack('<IHBBhhhh', 7, pad['buttons'], 0, 0, 0, 0, 0, 0))
    return 0
get_stub = e.stub('orig_XInputGetState', original_get_state)
set_stub = e.stub('orig_XInputSetState', lambda: 0)

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
struct.pack_into('<QQQ', img, 0x500, 0x8000000000000003, 0x8000000000000002, 0)
img[0x600:0x60e] = b'XINPUT1_4.dll\0'
struct.pack_into('<QQQ', img, 0x700, set_stub, get_stub, 0)
e.mu.mem_write(M, bytes(img))
slot = M + 0x708

ini = os.path.join(root, 'FnFNitro.ini')
open(ini, 'w').write('[Settings]\r\nNitroButton=LB\r\n')
ini_w = e.put_wstr('C:\\test\\FnFNitro.ini')

print('link:')
check(e.call('Fh2TestNitroLink', M, ini_w) == 1, 'FnFNitro.dll linked')
hook = e.call('Fh2TestNitroHook')
check(e.rq(slot) == hook, 'XInputGetState slot points to Gripline')
check(e.rq(M + 0x700) == set_stub, 'XInputSetState slot untouched')
check(e.call('Fh2TestNitroLink', M, ini_w) == 1 and e.rq(slot) == hook, 'second link keeps one hook')

state = e.alloc(16)
def poll(user=0):
    e.mu.mem_write(state, bytes(16))
    r = e.call(e.rq(slot), user, state)
    pk, buttons, lt, rt = struct.unpack('<IHBB', e.mu.mem_read(state, 8))
    return r & 0xffffffff, buttons, lt, rt

print('no XInput pad (wheel/keyboard only):')
check(poll()[0] == 1167, 'nothing held: not connected, like the original')
e.call('Fh2TestNitroSet', 1, 1)
r, b, lt, rt = poll()
check(r == 0 and b == 0x0100 and lt == 0 and rt == 0, 'keyboard nitro held: idle pad with LB (%d %#x)' % (r, b))
check(poll(1)[0] == 1167, 'other controller ports untouched')
e.call('Fh2TestNitroSet', 2, 1)
e.call('Fh2TestNitroSet', 1, 0)
check(poll()[1] == 0x0100, 'wheel still holds nitro after keyboard release')
e.call('Fh2TestNitroSet', 2, 0)
check(poll()[0] == 1167, 'all released: original result again')

print('real XInput pad:')
pad['connected'] = True
pad['buttons'] = 0x1000
r, b, lt, rt = poll()
check(r == 0 and b == 0x1000, 'nothing held: pad state unchanged')
e.call('Fh2TestNitroSet', 1, 1)
check(poll()[1] == 0x1100, 'nitro held: LB added to the real buttons')

print('NitroButton from FnFNitro.ini:')
open(ini, 'w').write('[Settings]\r\nNitroButton=RT\r\n')
e.tick += 2500
e.call('Fh2TestNitroSet', 1, 1)
r, b, lt, rt = poll()
check(rt == 255 and b == 0x1000, 'NitroButton=RT: right trigger full')
open(ini, 'w').write('[Settings]\r\nNitroButton=dpadup\r\n')
e.tick += 2500
e.call('Fh2TestNitroSet', 1, 1)
check(poll()[1] == 0x1001, 'NitroButton=dpadup (any case): D-pad up')
open(ini, 'w').write('[Settings]\r\nNitroButton=Quatsch\r\n')
e.tick += 2500
e.call('Fh2TestNitroSet', 1, 1)
check(poll()[1] == 0x1100, 'unknown name: LB like NitroSystem')
e.call('Fh2TestNitroSet', 1, 0)

print('online (FH2MP session):')
NET = 0x140000000 + 0x36e24f8
conn = e.alloc(0x40)
e.wq(NET, conn)
e.wd(conn + 0x10, 0)
e.call('Fh2TestNitroGame', 0x140000000)
open(ini, 'w').write('[Settings]\r\nNitroButton=LB\r\n')
e.tick += 2500
e.call('Fh2TestNitroSet', 1, 1)
pad['buttons'] = 0x1000
check(poll()[1] == 0x1100, 'connection state 0 (offline): nitro works')
e.wd(conn + 0x10, 1)
e.tick += 600
r, b, lt, rt = poll()
check(r == 0 and b == 0x1000, 'connection state 1 (online): Gripline nitro off (%#x)' % b)
pad['buttons'] = 0x1100
check(poll()[1] == 0x1000, 'online: real controller LB hidden from NitroSystem too')
check(e.call('Fh2TestNotifyCount') >= 1, 'notification shown')
l1, l2 = e.alloc(200), e.alloc(200)
e.call('Fh2TestNotifyText', 0, l1, l2)
check(e.wstr(l1) == 'Nitro off' and e.wstr(l2) == 'Reason: you are online (FH2MP)', 'card text: %r / %r' % (e.wstr(l1), e.wstr(l2)))
title = e.alloc(64)
style = e.call('Fh2TestNotifyStyle', 0, title) & 0xffffffff
check(style == 1 and e.wstr(title) == 'WARNING', 'nitro off: red, %r (%d)' % (e.wstr(title), style))
e.wd(conn + 0x10, 2)
e.tick += 600
pad['buttons'] = 0x1000
check(poll()[1] == 0x1100, 'offline again: nitro back')
e.call('Fh2TestNotifyText', 0, l1, l2)
check(e.wstr(l1) == 'Nitro on again' and e.wstr(l2) == 'Reason: you are offline again', 'card: nitro on again / offline session')
style = e.call('Fh2TestNotifyStyle', 0, title) & 0xffffffff
check(style == 2 and e.wstr(title) == '', 'nitro on again: green (%d)' % style)
e.wq(NET, 0)
e.tick += 600
check(poll()[1] == 0x1100, 'no connection object: offline')
e.call('Fh2TestNitroSet', 1, 0)

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
