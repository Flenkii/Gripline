"""Test of Gripline's own page in the pause menu (features/pause_tile.c) with the real compiled test
DLL: compact (no logbook, no notes), one row per setting as the game's left/right choice
(SpinnerControl), and left/right does what F8 does for that row (the page shows what was saved)."""
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


class KEmu(Emu):
    qpc = 1_000_000

    def k_QueryPerformanceCounter(self):
        self.wq(self.arg(0), self.qpc)
        return 1

    def k_QueryPerformanceFrequency(self):
        self.wq(self.arg(0), 1_000_000)
        return 1

    def k_GetCurrentProcessId(self):
        return 4242

    def k_WideCharToMultiByte(self):
        src, count, out, size = self.arg(2), self.arg(3), self.arg(4), self.arg(5)
        text = self.wstr(src)
        data = text.encode('utf-8') + (b'\0' if count in (-1, 0xffffffff, 0xffffffffffffffff) else b'')
        if out and size:
            data = data[:size]
            self.mu.mem_write(out, data)
        return len(data)


root = tempfile.mkdtemp(prefix='gl-keymenu-')
e = KEmu(DLL, root)
UP, DOWN, ENTER, ESC, BACK, F5, F8 = 0x26, 0x28, 0x0d, 0x1b, 0x08, 0x74, 0x77

keys = set()
focus = {'pid': 4242}
def async_key():
    return 0x8000 if (e.arg(0) & 0xff) in keys else 0
def foreground():
    return 0x1234
def window_pid():
    if e.arg(1):
        e.wd(e.arg(1), focus['pid'])
    return 1
def raw_buffer():
    return e.arg(0)
e.call('Fh2TestKeyboardAsync', e.stub('GetAsyncKeyState', async_key), e.stub('GetForegroundWindow', foreground),
       e.stub('GetWindowThreadProcessId', window_pid), e.stub('WindowsGetStringRawBuffer', raw_buffer))
e.call('Fh2TestKeyboardSetup', e.stub('RoGetActivationFactory', lambda: 0), e.put_wstr('C:\\test'))
ini_file = os.path.join(root, 'FH2_Keyboard.ini')
# a comment and a key of the player: both must stay as they are
text = open(ini_file, 'rb').read().replace(b'Brake=S', b'Brake=S,Down ; my own')
open(ini_file, 'wb').write(text)
e.call('Fh2TestKeyboardSetup', e.stub('RoGetActivationFactory2', lambda: 0), e.put_wstr('C:\\test'))

import re
import xml.dom.minidom


def page():
    b = e.alloc(300000)
    e.call('Fh2TestPauseTilePage', b, 300000)
    t = bytes(e.mu.mem_read(b, 300000)).split(b'\0')[0].decode('utf-8')
    rows = {m.group(2): (int(m.group(1)), shown(int(m.group(1)))[0])
            for m in re.finditer(r'Gripline_S(\d+)" Label="([^"]*)"', t)}
    return rows, t


def shown(i):
    b = e.alloc(64)
    r = e.call('Fh2TestPauseTileShown', i, b, 64)
    return (r if r < 0x80000000 else r - 2**32), bytes(e.mu.mem_read(b, 64)).split(b'\0')[0].decode('utf-8')


def choose(label, d):
    i, cur = page()[0][label]
    r = e.call('Fh2TestPauseTileChoose', i, d & 0xffffffff)
    r = r if r < 0x80000000 else r - 2**32
    return cur, r, page()[0][label][1]


rows, text = page()
try:
    xml.dom.minidom.parseString(text.encode('utf-8'))
    check(True, 'XAML ist wohlgeformt')
except Exception as ex:
    check(False, f'XAML ist wohlgeformt: {ex}')
check('FirstItem' in text and 'A_HudOptions' in text, 'Seite wie die Optionsseiten des Spiels, FirstItem da')
check(not any(w in text for w in ('Logbook', 'Records', 'Driven in total', 'Most kilometres')), 'Logbuch nicht auf der Seite')
check('Takes effect at once' not in text and 'Like a phone' not in text, 'keine Hinweiszeilen')
check(not re.search('[←-⟿]', text), 'keine Zeichen, die die Spielschrift nicht hat')
check('Card design' in rows and 'Sensitivity' in rows and 'Walking: speedometer' in rows, f'Listen als eine Zeile: {list(rows)[:8]}')
check('Off' not in rows and 'Classic' not in rows, 'keine einzelnen Listen-Eintraege als Zeilen')
check(text.count('<local:SpinnerControl') == len(rows) and len(rows) >= 35, f'{len(rows)} Auswahl-Zeilen')
check('ArrayList' not in text and text.count('ItemsSource="{Binding OnOffOptions}"') == len(rows),
      'alle Zeilen mit den Spiel-eigenen Eintraegen (Listen schreibt Gripline selbst)')
i_card = rows['Card design'][0]
check(shown(i_card)[1] not in ('', 'On', 'Off'), f'Liste zeigt ihren Namen: Card design = {shown(i_card)[1]!r}')
check(re.search(r'Gripline_S%d"[^>]*SelectedIndex="0"' % i_card, text) is not None, 'Liste steht auf dem ersten Spiel-Eintrag')
# left/right stops at the ends like the game's own rows
def go(label, d, steps=1):
    for _ in range(steps):
        r = choose(label, d)
    return r
cur, r, after = go('Mouse in the game menus', -1, 3)
check(r in (0, -2) and after == 0, f'Maus links bis zum Anfang: {r}, Seite {after}')
cur, r, after = choose('Mouse in the game menus', -1)
check(r == -2 or (r == cur == after), f'am Anfang: links aendert nichts ({cur} -> {r}, Seite {after})')
for label, d in [('Mouse in the game menus', 1), ('Mouse in the game menus', 1), ('Mouse in the game menus', -1),
                 ('Sensitivity', 1), ('Sensitivity', -1), ('While walking', 1), ('While walking', -1),
                 ('Card design', 1), ('Card design', -1), ('Position', 1), ('Size', 1), ('Summon car: driving style', 1)]:
    cur, r, after = choose(label, d)
    if r == -2:
        check(after == cur, f'{label} {"rechts" if d > 0 else "links"}: am Ende, bleibt {cur}')
    else:
        check(r == after and r == cur + d, f'{label} {"rechts" if d > 0 else "links"}: {cur} -> {r}, Seite zeigt {after}')
sw = 'Switch off on a crash'
cur = page()[0][sw][1]
cur2, r, after = choose(sw, 1 if cur == 0 else -1)
check(after == 1 - cur, f'Schalter {sw}: {cur} -> {after}')
check('MenuMouse=' in open(os.path.join(root, 'FH2_Keyboard.ini')).read(), 'Maus im Spielmenue gespeichert')
before = {k: rows[k][1] for k in ('Start cards', 'New record', 'Milestones')}
choose('Preset', 1)
after = {k: page()[0][k][1] for k in before}
check(after != before, f'Voreinstellung aendert die Karten-Zeilen: {before} -> {after}')
import html
page_heads = [html.unescape(h) for h in re.findall(r'Gripline_Part_\d+\}" local:BindingProxy.A="([^"]*)"', text)]
descs = dict(re.findall(r'x:Key="Gripline_Row_(\d+)" Heading="" Description="([^"]*)"', text))
check(len(descs) == len(rows), f'jede Zeile hat eine eigene Beschreibung ({len(descs)} von {len(rows)})')
card = descs.get(str(rows['Card design'][0]), '')
check('look of all Gripline cards' in card and 'Classic - Enhanced - Overdrive' in card, f'Card design: {card!r}')
walk = descs.get(str(rows['While walking'][0]), '')
check('17 steps, Off to 300 %' in walk, f'lange Skala: {walk!r}')
generic = [l for l, (i, _) in rows.items() if 'Gripline\'s own page' in descs.get(str(i), '') or len(descs.get(str(i), '')) < 15]
check(not generic, f'keine Zeile ohne eigenen Satz: {generic}')
check(e.call('Fh2TestPauseTileChoose', 0, 1) == 0xffffffff, 'Ueberschrift: nichts')
# V7.2 Feinschliff (15.11.2026): telemetry rows have the telemetry sentence (a fixed index pointed at the
# walking speedometer once rows were added), and the page has nearly all of F8 (world, cockpit, colour)
tele = [descs.get(str(rows[l][0]), '') for l in ('Speed', 'Grip / slip angle', 'Braking distance')]
check(all('telemetry panel' in t for t in tele) and not any('speedometer' in t for t in tele), f'Telemetrie-Zeilen: {tele[1]!r}')
check('other cars (Drivatars and traffic)' in descs.get(str(rows['No damage on cars'][0]), ''), 'Kein Schaden: Text passt zur Auswahl')
for label in ('Card colour', 'Dashboard Cam: forward', 'Dashboard Cam: height', 'Traffic', 'Speed cameras',
              'Steering animation 1:1', 'Telemetry panel'):
    check(label in rows, f'auf der Seite: {label}')
check(page_heads.index('Driving') < page_heads.index('World & Cockpit') < page_heads.index('Summon, Nitro & Updates'), f'Reihenfolge {page_heads}')
cur, r, after = choose('Dashboard Cam: forward', 1)
check(after == cur + 1 and shown(rows['Dashboard Cam: forward'][0])[1] == '40 cm', f'Dashboard Cam vor: {cur} -> {after}')
check(not any('Freecam' in l for l in rows), 'alte Freecam ist keine Wahl mehr')
cur, r, after = choose('Max angle', -1)
gini = open(os.path.join(root, 'Gripline.ini')).read()
check(after == cur - 1 and 'HeadTurn=4' in gini and 'HeadTurnAngle=15' in gini, f'links wie in F8 (ein Schritt, ein Speichern): {cur} -> {after}')
check(text.count('<local:SpinnerControl') <= 96, 'genug Platz (PAGE_ITEMS)')

print('\nALLE TESTS OK (%d Pruefungen)' % checks[0] if not fails else '\n%d FEHLER' % len(fails))
sys.exit(1 if fails else 0)
