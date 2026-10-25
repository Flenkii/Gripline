"""Test of the FH2 Mod Platform link (features/platform_link.c) with the real compiled test DLL.

A fake FH2ModApi (layout read from third_party/fh2mod/fh2mod.h) records what Gripline
registers: the mod, the page on the "Achievements" tile, its XAML and the tile colours."""
import os, re, struct, sys, tempfile, xml.dom.minidom
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


# member order of FH2ModApi: 5 data fields (4+4+8+8+8 = 32 bytes), then function pointers
header = open(os.path.join(os.path.dirname(__file__), '..', 'third_party', 'fh2mod', 'fh2mod.h')).read()
body = header[header.index('typedef struct FH2ModApi {'):header.index('} FH2ModApi;')]
names = re.findall(r'\(\*(\w+)\)\(', body)
offset = {n: 32 + 8 * i for i, n in enumerate(names)}
API_SIZE = 32 + 8 * len(names)

root = tempfile.mkdtemp(prefix='eo-platform-')
e = Emu(DLL, root)
seen = {'log': [], 'xaml': None, 'colors': None, 'text': None, 'page': None, 'info': None}


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


def register_mod():
    i = e.arg(0)
    seen['info'] = (cstr(e.rq(i + 8)), cstr(e.rq(i + 16)), cstr(e.rq(i + 24)), e.rd(i + 40))
    return MOD


def add_page():
    p = e.arg(1)
    seen['page'] = (cstr(e.rq(p + 8)), cstr(e.rq(p + 16)), cstr(e.rq(p + 40)), e.rq(p + 48))
    return 1


def set_page_xaml():
    seen['xaml'] = (cstr(e.arg(1)), cstr(e.arg(2)), cstr(e.arg(3)), e.arg(4))
    return 1


stubs = {
    'register_mod': register_mod,
    'log': lambda: seen['log'].append(cstr(e.arg(1))),
    'add_page': add_page,
    'set_page_xaml': set_page_xaml,
    'set_page_tile_colors': lambda: seen.__setitem__('colors', (e.arg(2) & 0xffffffff, e.arg(3) & 0xffffffff)) or 1,
    'set_page_tile_text': lambda: seen.__setitem__('text', cstr(e.arg(2))) or 1,
    'game_base': lambda: 0,
    'set_string': lambda: seen.__setitem__('string', (cstr(e.arg(1)), e.arg(2) & 0xffff, e.wstr(e.arg(3)))),
}
for n in names:
    e.wq(api + offset[n], e.stub('api_' + n, stubs.get(n, e.missing('api.' + n))))

print('anmelden:')
check(e.call('GriplinePlatformAttach', api) == 0, 'GriplinePlatformAttach returns 0')
check(seen['info'] and seen['info'][0] == 'Gripline' and seen['info'][1] == 'V6' and seen['info'][2] == 'Flenkii/Gripline'
      and seen['info'][3] == 0, f'register_mod: {seen["info"]}')
check(seen['page'] and seen['page'][0] == 'Settings' and seen['page'][2] == 'Achievements' and seen['page'][3] != 0,
      f'page on the Achievements tile with on_open: {seen["page"]}')
check(seen['colors'] == (0xFF1D4F91, 0xFFFFFFFF), f'tile colours {seen["colors"]}')
check(seen['text'] == 'SETTINGS', f'tile text {seen["text"]!r}')
check(seen.get('string') == ('PauseMenu.str', 0xF655, 'GRIPLINE V6'), f'tile title ERFOLGE -> GRIPLINE V6: {seen.get("string")}')
check(e.call('GriplinePlatformAttach', api) == 1, 'second attach is ignored')

print('XAML der Kachel-Seite:')
key, res, content, help_ = seen['xaml']
check(key == 'Settings' and help_ == 0, 'page key, default help bar')
check('<!-- GRIPLINE_SETTINGS_PAGE -->' in res, 'marker for the property registration')
check(content.count('x:Name="FirstItem"') == 1, 'exactly one FirstItem')
ns = ('xmlns:local="clr-namespace:ForzaUI" xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" '
      'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"')
try:
    xml.dom.minidom.parseString(('<Page %s><Page.Resources><ResourceDictionary>%s</ResourceDictionary></Page.Resources>%s</Page>'
                                 % (ns, res, content)).encode('utf-8'))
    check(True, 'resources + content are well-formed XML')
except Exception as ex:
    check(False, f'well-formed XML: {ex}')
for prop in ('EoRigWheelEnabled', 'EoRigButton1', 'EoKeyEnabled', 'EoRigWeather', 'EoGripSteering', 'EoGripNotify'):
    check('{Binding %s}' % prop in content and 'x:Key="EO_%s"' % prop in res, f'row + description for {prop}')
check('Cockpit: Full Steering Animation' in res and 'Gripline: Notifications' in res, 'English texts before the game runs')

print('%d checks, %d failed' % (checks[0], len(fails)))
sys.exit(1 if fails else 0)
