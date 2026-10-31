"""Test of the plugin build (plugin/gripline_plugin.c): fh2-mods\\Gripline.dll next to the official
Extra Options. A fake FH2ModApi records the mod, its Mods page rows and the INI move.
Run from tests/: python test_plugin.py ../gripline-plugin/fh2-mods/Gripline.dll"""
import os, re, struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../gripline-plugin/fh2-mods/Gripline.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


header = open(os.path.join(os.path.dirname(__file__), '..', 'third_party', 'fh2mod', 'fh2mod.h')).read()
body = header[header.index('typedef struct FH2ModApi {'):header.index('} FH2ModApi;')]
names = re.findall(r'\(\*(\w+)\)\(', body)
offset = {n: 32 + 8 * i for i, n in enumerate(names)}
API_SIZE = 32 + 8 * len(names)

root = tempfile.mkdtemp(prefix='gripline-plugin-')
os.makedirs(os.path.join(root, 'Mount'))
eo_ini = ('[Global]\r\nEnabled=1\r\n[Files]\r\nEnableMediaOverrides=1\r\n[Traffic]\r\nTrafficLevel=5\r\n'
          'RushHourAllDay=0\r\nWeather=1\r\nTimeOfDay=3\r\n[Gripline]\r\nNotifications=1\r\nSteeringAnimation=1\r\n'
          '[Livery]\r\nEnable4K=1\r\n')
open(os.path.join(root, 'FH2_ExtraOptions.ini'), 'w', newline='').write(eo_ini)



def k_lstrlenW(self):
    return len(self.wstr(self.arg(0)))


def k_lstrcpynW(self):
    text = self.wstr(self.arg(1))[: max(self.arg(2) - 1, 0)]
    self.mu.mem_write(self.arg(0), text.encode('utf-16le') + b'\0\0')
    return self.arg(0)


def k_WideCharToMultiByte(self):
    data = (self.wstr(self.arg(2)) + '\0').encode('utf-8')
    out, size = self.arg(4), self.arg(5) & 0xffffffff
    if out and size >= len(data):
        self.mu.mem_write(out, data)
        return len(data)
    return 0 if out else len(data)


Emu.k_WideCharToMultiByte = k_WideCharToMultiByte
Emu.k_lstrlenW = k_lstrlenW
Emu.k_lstrcpynW = k_lstrcpynW
e = Emu(DLL, root)


def cstr(a):
    out = b''
    while a:
        c = bytes(e.mu.mem_read(a + len(out), 1))
        if c == b'\0':
            break
        out += c
    return out.decode('utf-8', 'replace')


MOD = 0x77770000
api = e.alloc(API_SIZE)
e.mu.mem_write(api, bytes(API_SIZE))
e.wd(api, API_SIZE)
e.wd(api + 4, 8)
e.wq(api + 16, e.put_wstr('C:\\test'))
e.wq(api + 24, e.put_wstr('C:\\test\\Mount'))
seen = {'log': [], 'rows': [], 'info': None, 'pages': []}


def register_mod():
    i = e.arg(0)
    seen['info'] = (cstr(e.rq(i + 8)), cstr(e.rq(i + 16)))
    return MOD


def add_setting():
    s = e.arg(1)
    kind = e.rd(s + 0x38)
    seen['rows'].append({'key': cstr(e.rq(s + 8)), 'label': cstr(e.rq(s + 16)), 'description': cstr(e.rq(s + 24)),
                         'kind': kind, 'min': e.rf(s + 0x3c), 'max': e.rf(s + 0x40), 'step': e.rf(s + 0x44),
                         'restart': e.rd(s + 0x24)})
    return 1


def add_page():
    pg = e.arg(1)
    seen['pages'].append((cstr(e.rq(pg + 8)), cstr(e.rq(pg + 16))))
    return 1


stubs = {
    'add_page': add_page,
    'register_mod': register_mod,
    'log': lambda: seen['log'].append(cstr(e.arg(1))),
    'add_setting': add_setting,
    'wait_for_game': lambda: 0,
    'wait_for_code': lambda: 0,
}
for n in names:
    e.wq(api + offset[n], e.stub('api_' + n, stubs.get(n, e.missing('api.' + n))))

print('start:')
check(e.call('fh2mod_init', api) == 0, 'fh2mod_init returns 0')
check(seen['info'] == ('Gripline', 'V7 Test'), 'registered as Gripline V7 Test: %r' % (seen['info'],))
check(len(e.threads) == 1, 'start thread created (waits for the game)')

print('INI files:')
grip = open(os.path.join(root, 'Gripline.ini'), newline='').read()
check('[Traffic]' in grip and 'TrafficLevel=5' in grip and 'TimeOfDay=3' in grip, 'Gripline.ini has [Traffic]')
check('[Gripline]' in grip and 'SteeringAnimation=1' in grip, 'Gripline.ini has [Gripline]')
check('[Livery]' not in grip and '[Global]' not in grip, 'Gripline.ini has nothing of Extra Options')
eo = open(os.path.join(root, 'FH2_ExtraOptions.ini'), newline='').read()
check('[Traffic]' not in eo and '[Gripline]' not in eo and 'TrafficLevel' not in eo, 'FH2_ExtraOptions.ini without Gripline keys')
check('[Global]' in eo and 'Enable4K=1' in eo and 'EnableMediaOverrides=1' in eo, 'FH2_ExtraOptions.ini keeps its own keys')
check(open(os.path.join(root, 'FH2_ExtraOptions.ini.before-gripline'), newline='').read() == eo_ini, 'backup of the old INI')
check(os.path.isdir(os.path.join(root, 'mods', 'Gripline', 'media')), 'mods\\Gripline\\media made')

print('Mods page rows:')
rows = seen['rows']
check(len(rows) == 11, '%d rows (keyboard, traffic, cockpit, Gripline)' % len(rows))
check(not any(w in r['label'] for r in rows for w in ('Wheel', 'Weather', 'Time of Day', 'Profile')),
      'no wheel, weather, time of day or profile rows (Azimuth / Extra Options do those)')
keys = [r['key'] for r in rows]
check(len(set(keys)) == len(keys), 'keys unique')
check(all(re.fullmatch(r'[A-Za-z0-9_]+', k) for k in keys), 'keys letters/digits')
check(all(r['label'] for r in rows), 'every row has a label')
check(not any(r['label'] in ('Traffic: Preset', 'Traffic: Amount') for r in rows),
      'traffic preset and amount are not numbered sliders any more')
check(seen['pages'] == [('Traffic', 'Gripline Traffic')], 'Traffic page with the presets by name: %r' % (seen['pages'],))
check(any(r['kind'] == 0 for r in rows), 'on/off rows')
check(any(r['restart'] for r in rows), 'restart rows marked')
color = [r for r in rows if r['label'] == 'Gripline: Card Colour']
check(len(color) == 1 and color[0]['min'] == 0 and color[0]['max'] == 7 and not color[0]['restart'] and
      'Blue (original)' in color[0].get('description', 'Blue (original)'), 'card colour row 0..7, live: %r' % (color,))
for r in rows[:6]:
    print('     ', r['key'], r['label'], r['kind'], r['min'], r['max'])

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
