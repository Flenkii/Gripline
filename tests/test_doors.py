"""V7.2: the car's own door animation for getting out / in (features/car_doors.c), with the compiled
test DLL. The game's functions are stand-ins here: what matters is that Gripline checks the game code,
looks the door up the way the game does and sends the right name at normal speed."""
import struct, sys, tempfile
from emu import Emu, GAME

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


e = Emu(DLL, tempfile.mkdtemp(prefix='gl-doors-'))
CODE = {  # the game's bytes Gripline checks (forza_durango_release_final.exe 1.0.0.64)
    0xf5d300: '40534883ec20488b89404d0000488bda4885c97415e816667a00488bd3488b084883c4205be916e279004883c4205bc3'[:84],
    0x1703930: '488d81c8010000c3',
    0x16fb598: '498b9e680200004c8d442428488d542470498d8e68020000e81b1f000048395c',
    0xf74e00: '488b81404d00004885c07405c5fa114820c3',
    0x16fbe90: '8bc2c5e857d2895110488b15f85c83014533c9c4e1ea2ad0e993000000',
}
LEFT, RIGHT = -1, 1


def doors(car, side, what):
    return e.call('Fh2TestDoors', GAME, car, side, what) & 0xffffffff


check(doors(0, LEFT, 0) == 0, 'game code not there: not ready (fade)')
for rva, code in CODE.items():
    e.mu.mem_write(GAME + rva, bytes.fromhex(code))
check(doors(0, LEFT, 0) == 1, 'game code as known: ready')
e.mu.mem_write(GAME + 0xf5d300, bytes.fromhex('e912345678'))
check(doors(0, LEFT, 0) == 1, 'another mod hooked the car event (FH2MP: jmp at the start): still ready')
e.mu.mem_write(GAME + 0xf5d300, bytes.fromhex('ff2500000000') + bytes(8))
check(doors(0, LEFT, 0) == 1, 'hooked with a far jmp: still ready')
e.mu.mem_write(GAME + 0xf5d300 + 16, b'\x90')
check(doors(0, LEFT, 0) == 0, 'hooked, and the rest differs too: not ready')
e.mu.mem_write(GAME + 0xf5d300, bytes.fromhex(CODE[0xf5d300]))
e.mu.mem_write(GAME + 0xf74e00, bytes.fromhex('e9aabbccdd'))
check(doors(0, LEFT, 0) == 1, 'event speed hooked: still ready')
e.mu.mem_write(GAME + 0xf74e00, bytes.fromhex(CODE[0xf74e00]))
e.mu.mem_write(GAME + 0x16fbe90, bytes.fromhex('90' * 29))
check(doors(0, LEFT, 0) == 0, 'animation detail code differs: not ready')
e.mu.mem_write(GAME + 0x16fbe90, bytes.fromhex(CODE[0x16fbe90]))
check(doors(0, LEFT, 0) == 1, 'back as the game has it: ready')

# a car: +0x4d40 animation -> +0x1c8 graph -> +0x268 the event map (first field: its end)
car, animation, graph = e.alloc(0x5000), e.alloc(0x200), e.alloc(0x300)
END = 0x5555
e.wq(car + 0x4d40, animation)
e.wq(animation + 0x1c8, graph)
e.wq(graph + 0x268, END)
known = {'doorLF:open'}
sent = []


def find():
    m, out, key = e.arg(0), e.arg(1), e.arg(2)
    length, room = e.rq(key + 16), e.rq(key + 24)
    name = bytes(e.mu.mem_read(key, length)).decode()
    sent.append(('find', m, name, room))
    e.wq(out, 0x7777 if name in known else END)
    return out


def event():
    a = e.arg(1)
    name = b''
    while not name.endswith(b'\0'):
        name += bytes(e.mu.mem_read(a + len(name), 1))
    sent.append(('event', e.arg(0), name[:-1].decode()))


def speed():
    sent.append(('speed', e.arg(0), round(struct.unpack('<f', struct.pack('<I', e.mu.reg_read(__import__('unicorn').x86_const.UC_X86_REG_XMM1) & 0xffffffff))[0], 3)))


def lod():
    sent.append(('lod', e.arg(0), e.arg(1) & 0xffffffff))


e.stub('find', find, at=GAME + 0x16fd4d0)
e.stub('lod', lod, at=GAME + 0x16fbe90)
e.stub('event', event, at=GAME + 0xf5d300)
e.stub('speed', speed, at=GAME + 0xf74e00)

print('look-up:')
check(doors(car, LEFT, 1) == 1, 'left front door there')
check(sent[-1] == ('find', graph + 0x268, 'doorLF:open', 15), 'looked up in the graph\'s map as a short game string: %r' % (sent[-1],))
check(doors(car, RIGHT, 1) == 0, 'right front door not there: fade')
check(sent[-1][2] == 'doorRF:open', 'right door name')
e.wq(car + 0x4d40, 0)
check(doors(car, LEFT, 1) == 0, 'car without animation: no door')
check(doors(car, LEFT, 2) == 0, 'car without animation: nothing sent')
e.wq(car + 0x4d40, animation)

print('open / close:')
e.mu.mem_write(car + 0x4d70, struct.pack('<i', 1))  # the game's level for the car: not full
sent.clear()
check(doors(car, LEFT, 2) == 1 and sent == [('lod', graph, 0), ('speed', car, 1.0), ('event', car, 'doorLF:open')],
      'open: full animation detail, normal speed, then the event %r' % sent)
sent.clear()
check(doors(car, RIGHT, 3) == 1 and sent == [('speed', car, 1.0), ('event', car, 'doorRF:close')], 'close right (detail stays) %r' % sent)
sent.clear()
doors(car, LEFT, 4)
check(sent == [('lod', graph, 1)], 'settle: the game\'s own level again %r' % sent)
sent.clear()
doors(car, LEFT, 4)
check(sent == [], 'settle twice: nothing more')
other = e.alloc(0x5000)
e.wq(other + 0x4d40, animation)
doors(car, LEFT, 2)
sent.clear()
doors(other, LEFT, 4)
check(sent == [], 'settle with another car: not touched (the game sets its own)')
doors(car, LEFT, 4)
check(sent == [], '...and forgotten')

print(f'\n{checks[0]} checks, {len(fails)} failed')
sys.exit(1 if fails else 0)
