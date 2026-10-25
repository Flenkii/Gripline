"""Test of the steering animation (features/cockpit.c) against the V1 script tools/fh2_lenkrad.py.

Needs real game files: set FH2_COCKPIT_DATA to a folder with media/drivers/{default,unsuited}.zip
and media/cars/MER_SLR_05.zip (they are not in the repository)."""
import importlib.util, math, os, struct, sys, zipfile, zlib
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
DATA = os.environ.get('FH2_COCKPIT_DATA', '')
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


def spindle_curves(data):
    """quaternions of the steering wheel curve, knots and the scale curve (knots, diagonal) of a GR2"""
    g = L.GR2(data)
    P = g.P
    rtd = g.base[g.root_type[0]] + g.root_type[1]
    rob = g.base[g.root_obj[0]] + g.root_obj[1]
    o, _, tg_td = g.member(rtd, 'TrackGroups')
    tg = g.p(g.p(rob + o + 4))
    oo, _, ss = g.member(tg_td, 'TransformTracks')
    count, array, es = struct.unpack_from('<I', g.buf, tg + oo)[0], g.p(tg + oo + 4), g.size_of(ss)
    result = {}
    for k in range(count):
        at = array + k * es
        name = g.cstr(g.p(at))
        for curve in ('OrientationCurve', 'ScaleShearCurve'):
            co, _, _ = g.member(ss, curve)
            vt, obj = g.p(at + co), g.p(at + co + P)
            kind = g.typedef(vt)[0][1]
            if kind != 'CurveDataHeader_DaK32fC32f':
                result[(name, curve)] = (kind, None, None)
                continue
            ko, _, _ = g.member(vt, 'Knots')
            cc, _, _ = g.member(vt, 'Controls')
            nk, nc = struct.unpack_from('<I', g.buf, obj + ko)[0], struct.unpack_from('<I', g.buf, obj + cc)[0]
            knots = struct.unpack_from('<%df' % nk, g.buf, g.p(obj + ko + 4))
            controls = struct.unpack_from('<%df' % nc, g.buf, g.p(obj + cc + 4))
            result[(name, curve)] = (kind, knots, controls)
    return result


def check_two_parts(got, ref, label):
    """the new file: [0, 0.48] the script's curve (every second sample), [0.52, 1] the same with the wheel
    scaled to almost 0; the driver tracks squeezed into [0, 0.48]"""
    if not got:
        check(False, label + ': keine Datei')
        return
    ours, script = spindle_curves(got), spindle_curves(ref)
    key = ('boneSteeringWheelSpindle', 'OrientationCurve')
    _, knots, q = ours[key]
    _, rknots, rq = script[key]
    dur = rknots[-1]
    half = len(knots) // 2
    worst = max(abs(q[4 * j + c] - rq[8 * j + c]) for j in range(half) for c in range(4))
    same = all(abs(q[4 * (half + j) + c] - q[4 * j + c]) < 1e-6 for j in range(half) for c in range(4))
    check(len(knots) == 242 and abs(knots[half - 1] - 0.48 * dur) < 1e-4 and abs(knots[half] - 0.52 * dur) < 1e-4,
          f'{label}: Lenkrad-Kurve in zwei Teilen (0..0.48, 0.52..1)')
    check(worst < 1e-5 and same, f'{label}: beide Teile drehen wie das Skript (Abw. {worst:.2e})')
    kind, sk, sc = ours[('boneSteeringWheelSpindle', 'ScaleShearCurve')]
    check(kind == 'CurveDataHeader_DaK32fC32f' and sc[0::9][:2] == (1.0, 1.0) and max(sc[0::9][2:]) < 0.001,
          f'{label}: Lenkrad-Größe 1 im ersten Teil, ~0 im zweiten')


here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('L', os.path.join(here, '..', 'tools', 'fh2_lenkrad.py'))
L = importlib.util.module_from_spec(spec)
spec.loader.exec_module(L)
e = Emu(DLL, '/tmp')


def put(b):
    p = e.alloc(len(b) + 16)
    e.mu.mem_write(p, b)
    return p


def fcall(name, which, x):
    e.call(name, which, 0)  # warm
    from unicorn.x86_const import UC_X86_REG_XMM1, UC_X86_REG_XMM0
    e.mu.reg_write(UC_X86_REG_XMM1, struct.unpack('<Q', struct.pack('<d', x))[0])
    target = e.exports[name]
    e.call(target, which)
    return struct.unpack('<d', struct.pack('<Q', e.mu.reg_read(UC_X86_REG_XMM0) & 0xffffffffffffffff))[0]


print('Mathe:')
worst = 0
for which, fn, xs in ((0, math.sin, [-3.1, -1, 0.3, 2.9, 7.0]), (1, math.cos, [-2, 0, 1.2, 3.0]),
                      (2, math.acos, [-0.99, -0.3, 0, 0.5, 0.999]), (3, lambda v: v ** 0.4, [0.001, 0.2, 0.9, 1.0])):
    for x in xs:
        worst = max(worst, abs(fcall('Fh2TestMath', which, x) - fn(x)))
check(worst < 1e-10, f'sin/cos/acos/pow genau (max Fehler {worst:.2e})')

print('Inflate:')
import random
random.seed(5)
ok = True
for size in (0, 1, 100, 70000):
    raw = bytes(random.choice(b'abcdefgh ') for _ in range(size)) + os.urandom(size // 7)
    for level in (0, 1, 9):
        co = zlib.compressobj(level, zlib.DEFLATED, -15)
        packed = co.compress(raw) + co.flush()
        out = e.alloc(len(raw) + 16)
        r = e.call('Fh2TestInflate', put(packed), len(packed), out, len(raw))
        ok = ok and r == 1 and bytes(e.mu.mem_read(out, len(raw))) == raw
check(ok, 'raw deflate (stored/fixed/dynamic) wie zlib')

if not DATA:
    print('FH2_COCKPIT_DATA not set: game-file checks skipped')
else:
    print('Lenkrad-Animation (MER_SLR_05):')
    src = zipfile.ZipFile(os.path.join(DATA, 'media/cars/MER_SLR_05.zip')).read('scene/Drivers/default/leftRightTurn.gr2')
    for deg, curve in ((450, 1.0), (450, 2.5), (1260, 1.0), (0, 1.0)):
        ref, info = L.steering_gr2(src, deg or 90, curve=curve)
        out = e.alloc(len(ref) * 3)
        n = e.call('Fh2TestCockpitGr2', put(src), len(src), deg, int(curve * 10), out, len(ref) * 3)
        got = bytes(e.mu.mem_read(out, n)) if n else b''
        check_two_parts(got, ref, f'{deg or "Original"} Grad, Kurve {curve}')
    print('Hände ausblenden:')
    for dm in ('default', 'unsuited'):
        m = zipfile.ZipFile(os.path.join(DATA, f'media/drivers/{dm}.zip')).read('driver.modelbin')
        ref, count = L.hide_hands(m)
        p = put(m)
        n = e.call('Fh2TestCockpitHands', p, len(m))
        got = bytes(e.mu.mem_read(p, len(m)))
        check(n == count and got == ref, f'{dm}: {n} Dreiecke ({count}), Datei identisch: {got == ref}')

if DATA:
    print('Dateien erzeugen (ganzer Ablauf):')
    import shutil, tempfile, fnmatch

    class CEmu(Emu):
        finds = {}

        def k_SetFilePointerEx(self):
            self.files[self.arg(0)][1] = self.arg(1)
            return 1

        def k_FindFirstFileW(self):
            pattern = self.host(self.wstr(self.arg(0)))
            folder, mask = os.path.split(pattern)
            names = sorted(n for n in os.listdir(folder) if fnmatch.fnmatch(n.lower(), mask.lower())) if os.path.isdir(folder) else []
            if not names:
                return 0xffffffffffffffff
            self.finds[0x700] = names
            return self.k_FindNextFileW(0x700, self.arg(1))

        def k_FindNextFileW(self, h=None, data=None):
            h = h if h is not None else self.arg(0)
            data = data if data is not None else self.arg(1)
            names = self.finds.get(h, [])
            if not names:
                return 0
            name = names.pop(0)
            self.mu.mem_write(data, bytes(44) + (name + '\0').encode('utf-16le'))
            return h

        def k_FindClose(self):
            return 1

    root = tempfile.mkdtemp(prefix='eo-cockpit-')
    mount = os.path.join(root, 'Mount')
    os.makedirs(os.path.join(mount, 'media', 'drivers'))
    os.makedirs(os.path.join(mount, 'media', 'cars'))
    for rel in ('drivers/unsuited.zip', 'cars/MER_SLR_05.zip', 'cars/FOR_Fiesta_09.zip'):  # small enough for the emulator
        shutil.copy(os.path.join(DATA, 'media', rel), os.path.join(mount, 'media', rel))
    c = CEmu(DLL, root)
    r = c.call('Fh2TestCockpitMake', c.put_wstr('C:\\test\\Mount'), 900, 10)
    out = os.path.join(mount, 'gripline_cockpit')
    files = sorted(os.path.relpath(os.path.join(d, f), out).replace(os.sep, '/') for d, _, fs in os.walk(out) for f in fs)
    check(r == 1, 'make_files ok')
    check(files == ['cars/MER_SLR_05/scene/Drivers/default/leftRightTurn.gr2',
                    'drivers/unsuited/driver.modelbin', 'gripline_cockpit.txt'], f'Dateien: {files}')
    ref, _ = L.steering_gr2(src, 450, curve=1.0)
    got = open(os.path.join(out, 'cars/MER_SLR_05/scene/Drivers/default/leftRightTurn.gr2'), 'rb').read()
    check_two_parts(got, ref, 'make_files SLR (900 Grad)')
    check('Rotation=900' in open(os.path.join(out, 'gripline_cockpit.txt')).read(), 'Manifest mit Drehwinkel')
    t0 = os.path.getmtime(os.path.join(out, 'drivers/unsuited/driver.modelbin'))
    check(c.call('Fh2TestCockpitMake', c.put_wstr('C:\\test\\Mount'), 900, 10) == 1 and
          os.path.getmtime(os.path.join(out, 'drivers/unsuited/driver.modelbin')) == t0, 'zweiter Start: nichts neu erzeugt')
    check(c.call('Fh2TestCockpitMake', c.put_wstr('C:\\test\\Mount'), 540, 10) == 1 and
          'Rotation=540' in open(os.path.join(out, 'gripline_cockpit.txt')).read(), 'anderer Drehwinkel: neu erzeugt')
    shutil.rmtree(out)
    check(c.call('Fh2TestCockpitMake', c.put_wstr('C:\\test\\Mount'), 0, 10) == 1 and
          'Rotation=0' in open(os.path.join(out, 'gripline_cockpit.txt')).read() and
          not os.path.exists(os.path.join(out, 'drivers')) and
          os.path.exists(os.path.join(out, 'cars/MER_SLR_05/scene/Drivers/default/leftRightTurn.gr2')),
          'ohne volle Lenkrad-Animation: Lenkrad-Dateien trotzdem, Hände bleiben')

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
