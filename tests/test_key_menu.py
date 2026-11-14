"""Test of the Gripline key menu (features/key_menu.c) with the real compiled test DLL.

Keys come from a fake GetAsyncKeyState; the menu is ticked by hand (in the game it has its own
thread). Checks: F8 opens, arrows / Enter / Esc / Backspace, a new key is used at once, saved
into FH2_Keyboard.ini (rest of the file unchanged) and shown in the key list (F5); while the menu
is open the game and noclip get no key; keys held when it closes stay ignored until released."""
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

now = [10_000]
def tick(ms=40):
    now[0] += ms
    e.tick += ms
    return e.call('Fh2TestKeyMenuTick', now[0])
def press(*vk):
    keys.update(vk)
    level = tick()
    keys.difference_update(vk)
    tick()
    return level
def selected():
    b = e.alloc(256)
    i = e.call('Fh2TestKeyMenuSelected', b)
    return e.wstr(b) if i != 0xffffffff and i < 64 else None
def panel():
    w, rows, count = e.alloc(4), e.alloc(4), e.alloc(4)
    label, keys_ = e.alloc(64), e.alloc(64)
    e.call('Fh2TestNotifyPanelDraw', 1080, 0, 0, w, rows, count, label, keys_)
    return e.rd(count)
def panel_rows():
    out = {}
    for i in range(panel()):
        l, k = e.alloc(64), e.alloc(64)
        e.call('Fh2TestNotifyPanelRow', i, l, k)
        out[e.wstr(l)] = e.wstr(k)
    return out
def goto(label, limit=40):
    """moves down on the page until the row label is selected (the order of the first page may change)"""
    for _ in range(limit):
        if selected() == label:
            return True
        press(DOWN)
    return selected() == label
def ini():
    return open(ini_file, 'rb').read().decode('ascii')
def binding(index, k=0):
    return e.call('Fh2TestKeyboardBinding', index, k)

LEVEL_CLOSED, LEVEL_GROUPS, LEVEL_LIST, LEVEL_BINDING, LEVEL_CAPTURE = 0, 1, 2, 3, 4

check(e.call('Fh2TestKeyMenuCover') == 46, 'jede der 46 Tasten steht genau einmal im Menue')
check(tick() == LEVEL_CLOSED and panel() == 0, 'Menue anfangs zu')
card = e.alloc(256)
ok = e.call('Fh2TestKeyMenuStartCard', card, 256)
card_text = bytes(e.mu.mem_read(card, 256)).split(b'\0')[0].decode('utf-8')
check(ok == 1 and card_text == 'Press F8 to change your keys\nKey menu: arrows, Enter, Esc', f'eigene Startkarte: {card_text!r}')
check(e.call('Fh2TestKeyboardBinding', 42, 0) == F8, 'KeyMenu = F8 (Standard)')

press(F8)
check(tick() == LEVEL_GROUPS and panel() == 26, f'F8 oeffnet das Menue (4 Abschnitte + 4 Seiten + Maus + Maus-Blick + Maus im Spielmenue + Telemetrie + Tempomat + Fahrtenbuch + Rekorde + Welt + Verkehr + Kein Schaden + Einstellungen + Handkamera + Lauf-Tacho + Karten & Hinweise + Karten-Design + Problem-Bericht + Alle Tasten + Hinweis): {panel()}')
check(e.call('Fh2TestNotifyPanelPixels', 1080) >= 76 + 26 * 26, f'V7.1 Hauptseite mit Lauf-Tacho und Maus im Spielmenue behaelt die grossen Zeilen: {e.call("Fh2TestNotifyPanelPixels", 1080)} px')
top = panel_rows()
check(top.get('Keys & controls') == '' and top.get('Driving & data') == '' and top.get('World & cars') == '' and top.get('Gripline') == '',
      f'vier Abschnitte: {list(top)}')
check(top.get('\u2191\u2193 choose \u00b7 Enter/A OK') == 'Esc/B close', 'Hinweis zeigt Tastatur und Controller')
check(selected() == 'Driving', f'erste Zeile gewaehlt: {selected()}')

# while open: the game and noclip get no key
keys.add(ord('W'))
check(e.call('Fh2TestKeyboardHeld', 0) == 0, 'Menue offen: W kommt bei Noclip/Freecam nicht an')
keys.discard(ord('W'))

press(DOWN)
check(selected() == 'Camera & view', f'Pfeil runter: {selected()}')
press(UP); press(UP)
check(selected() == 'Reset all keys to default', f'Pfeil hoch springt ans Ende: {selected()}')
press(DOWN)
check(selected() == 'Driving', 'und wieder an den Anfang')
check(press(ENTER) == LEVEL_LIST, 'Enter: Seite Fahren')
check(selected() == 'Throttle' and panel_rows().get('Throttle') == 'W', f'Gas = W in der Liste: {panel_rows()}')
press(ENTER)
check(tick() == LEVEL_BINDING and selected() == 'Set a new key', f'Enter: Gas-Seite, "Neue Taste" gewaehlt: {selected()}')
rows = panel_rows()
check(rows.get('Now') == 'W' and rows.get('Default') == 'W', f'Gas-Seite zeigt Jetzt/Standard: {rows}')

# new key: the Enter that chose it is still held -> not taken as the new key
keys.add(ENTER)
check(tick() == LEVEL_CAPTURE, 'Enter: Abfrage der neuen Taste')
tick(); tick()
check(tick() == LEVEL_CAPTURE and binding(0) == ord('W'), 'Enter (noch gehalten) wird nicht als Taste genommen')
keys.discard(ENTER)
tick()
keys.add(ord('I'))
check(tick() == LEVEL_BINDING, 'I gedrueckt: zurueck auf der Gas-Seite')
keys.discard(ord('I'))
tick()
check(binding(0) == ord('I') and binding(0, 1) == 0, 'Gas = I sofort')
check('Throttle=I\r\n' in ini(), 'FH2_Keyboard.ini: Throttle=I gespeichert')
check('Brake=S,Down ; my own' in ini() and ini().startswith('; FH2 keyboard'), 'Rest der Datei unveraendert')
check(panel_rows().get('Now') == 'I', 'Gas-Seite zeigt I')

# add a second key
press(DOWN)
check(selected() == 'Add another key', selected())
press(ENTER)
tick()
keys.add(UP)
check(tick() == LEVEL_BINDING, 'Pfeil hoch als zweite Taste')
keys.discard(UP)
tick()
check(binding(0) == ord('I') and binding(0, 1) == UP and 'Throttle=I,Up\r\n' in ini(), 'Gas = I, Up gespeichert')

# Esc in the capture = cancel
press(UP)
press(ENTER); tick()
check(tick() == LEVEL_CAPTURE, 'neue Abfrage')
press(ESC)
check(tick() == LEVEL_BINDING and binding(0) == ord('I'), 'Esc bricht ab, Taste bleibt')

# restore default
press(DOWN); press(DOWN)
check(selected() == 'Restore default', selected())
press(ENTER)
check(binding(0) == ord('W') and binding(0, 1) == 0 and 'Throttle=W\r\n' in ini(), 'Standard wiederhergestellt')

# back: Backspace -> list, Esc -> pages, Esc -> closed
check(press(BACK) == LEVEL_LIST, 'Backspace: eine Ebene zurueck')
check(press(ESC) == LEVEL_GROUPS, 'Esc: zurueck auf die erste Seite')

# Gripline page: the menu key cannot be cleared and cannot get Enter
press(DOWN); press(DOWN); press(ENTER)
names = panel_rows()
check('Open key menu' in names and names.get('Freecam on/off') == 'F7', f'Gripline-Seite: {names}')
for _ in range(7):
    press(DOWN)
check(selected() == 'Open key menu', selected())
press(ENTER)
check('No key' not in panel_rows(), 'Menue-Taste: kein "Keine Taste"')
press(ENTER); tick()
keys.add(ENTER); tick(); keys.discard(ENTER); tick()
check(tick() == LEVEL_CAPTURE and binding(42) == F8, 'Menue-Taste: Enter abgelehnt, Abfrage bleibt')
keys.add(0x79)  # F10
tick(); keys.discard(0x79); tick()
check(binding(42) == 0x79 and 'KeyMenu=F10\r\n' in ini(), 'Menue-Taste = F10 gespeichert')
press(ESC); press(ESC); press(ESC)
check(tick() == LEVEL_CLOSED, 'Esc auf der ersten Seite schliesst das Menue')
press(F8)
check(tick() == LEVEL_CLOSED, 'F8 oeffnet jetzt nicht mehr')
press(0x79)
check(tick() == LEVEL_GROUPS, 'F10 oeffnet')

# held when closing: ignored until released (Esc must not pause the game, W must not drive off)
press(0x79)
check(tick() == LEVEL_CLOSED, 'F10 schliesst von ueberall')
press(0x79)
keys.add(ord('W'))
tick()
check(e.call('Fh2TestKeyboardHeld', 0) == 0, 'Menue offen: W gesperrt')
press(0x79)
check(tick() == LEVEL_CLOSED and e.call('Fh2TestKeyboardHeld', 0) == 0, 'nach dem Schliessen: W (noch gehalten) bleibt ignoriert')
keys.discard(ord('W'))
e.call('Fh2TestKeyboardHeld', 0)
keys.add(ord('W'))
check(e.call('Fh2TestKeyboardHeld', 0) == 1, 'losgelassen und neu gedrueckt: W geht wieder')
keys.discard(ord('W'))

# card design page: Overdrive is the default, Classic chosen -> Gripline.ini CardStyle=0
open(os.path.join(root, 'Gripline.ini'), 'wb').write(b'[Gripline]\r\nCardColor=3\r\n')
press(0x79)
goto('Card design')
check(selected() == 'Card design' and panel_rows().get('Card design') == 'Overdrive', f'Karten-Design-Zeile zeigt Overdrive: {panel_rows()}')
press(ENTER)
check(selected() == 'Overdrive' and panel_rows().get('Overdrive') == '\u2713 active', f'Design-Seite: Overdrive aktiv gewaehlt {panel_rows()}')
press(UP)
check(e.call('Fh2TestNotifyStyleGet') == 1, 'Vorschau: Pfeil hoch zeigt Enhanced sofort')
press(UP); press(ENTER)
gi = open(os.path.join(root, 'Gripline.ini'), 'rb').read()
check(e.call('Fh2TestNotifyStyleGet') == 0 and b'CardStyle=0' in gi and b'CardColor=3' in gi, f'Classic gewaehlt und gespeichert: {gi!r}')
check(panel_rows().get('Classic') == '\u2713 active', 'Haken wandert zu Classic')
press(DOWN)
check(e.call('Fh2TestNotifyStyleGet') == 1, 'Vorschau Enhanced')
press(ESC)
check(e.call('Fh2TestNotifyStyleGet') == 0 and b'CardStyle=0' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(),
      'Esc: Vorschau endet, gespeichertes Classic bleibt')
press(ENTER); press(DOWN); press(DOWN); press(ENTER)
check(e.call('Fh2TestNotifyStyleGet') == 2 and b'CardStyle=2' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'zurueck auf Overdrive')
press(ESC); press(ESC)
check(tick() == LEVEL_CLOSED, 'Esc, Esc: zu')

# mouse steering page: Off by default, Medium chosen -> FH2_Keyboard.ini MouseSteering=40
press(0x79)
goto('Mouse steering (experimental)')
check(selected() == 'Mouse steering (experimental)' and panel_rows().get('Mouse steering (experimental)') == 'Off', f'Maus-Zeile zeigt Aus: {panel_rows()}')
press(ENTER)
check(selected() == 'Off' and panel_rows().get('Off') == '\u2713 active', f'Maus-Seite: Aus gewaehlt {panel_rows()}')
press(DOWN); press(DOWN); press(ENTER)
check('MouseSteering=25\r\n' in ini() and panel_rows().get('Gentle') == '\u2713 active', 'Sanft gewaehlt und gespeichert')
press(UP); press(UP); press(ENTER)
check('MouseSteering=0\r\n' in ini(), 'wieder Aus')
press(ESC); press(ESC)
check(tick() == LEVEL_CLOSED, 'Maus: Esc, Esc: zu')

# mouse look page (V7.1): walking / driving sensitivity, arrows change, Enter steps on, FH2_Keyboard.ini
press(0x79)
check(goto('Mouse look: sensitivity'), selected())
press(ENTER)
rows = panel_rows()
check(selected() == 'While walking' and rows.get('While walking') == '100 %' and rows.get('While driving') == '100 %', f'Maus-Blick-Seite: {rows}')
press(0x27)  # right arrow
check(panel_rows().get('While walking') == '110 %' and 'WalkMouseLook=110\r\n' in ini(), f'Laufen 110 % gespeichert: {ini()!r}')
for _ in range(20): press(0x27)
check(panel_rows().get('While walking') == '300 %' and 'WalkMouseLook=300\r\n' in ini(), 'Laufen hoert bei 300 % auf')
press(ENTER)
check(panel_rows().get('While walking') == 'Off' and 'WalkMouseLook=0\r\n' in ini(), 'Enter am Ende: wieder Aus')
press(DOWN); press(0x25); press(0x25)  # left arrow twice
check(panel_rows().get('While driving') == '80 %' and 'DriveMouseLook=80\r\n' in ini(), 'Fahren 80 % gespeichert')
press(DOWN); press(ENTER)
rows = panel_rows()
check(rows.get('While walking') == '100 %' and rows.get('While driving') == '100 %' and 'WalkMouseLook=100\r\n' in ini()
      and 'DriveMouseLook=100\r\n' in ini(), f'beide auf Standard: {rows}')
press(ESC)
check(tick() == LEVEL_GROUPS and selected() == 'Mouse look: sensitivity', 'Esc: zurueck zur Hauptseite')
press(ESC)
check(tick() == LEVEL_CLOSED, 'Maus-Blick: zu')

# telemetry page: Turn rate on -> Gripline.ini TelemetryShow, dead zone 40 % -> FH2_Keyboard.ini
press(0x79)
goto('Live telemetry')
check(selected() == 'Live telemetry', selected())
press(ENTER)
check(selected() == 'Telemetry panel', selected())
for _ in range(6): press(DOWN)
check(selected() == 'Turn rate' and panel_rows().get('Turn rate') == 'off', f'Drehrate aus: {panel_rows()}')
press(ENTER)
gi = open(os.path.join(root, 'Gripline.ini'), 'rb').read()
check(panel_rows().get('Turn rate') == '\u2713 on' and b'TelemetryShow=' in gi, f'Drehrate an und gespeichert: {gi!r}')
rows = panel_rows()
check(rows.get('Position') == 'Right middle' and rows.get('Background opacity') == '100 %' and rows.get('Size') == '100 %', f'Aussehen: {rows}')
check(goto('Position'), selected())
press(0x27)  # right arrow
check(panel_rows().get('Position') == 'Right top' and b'TelemetryPosition=1' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'Position rechts oben gespeichert')
press(0x25); press(0x25)  # left arrow: right top -> right middle -> left bottom
check(panel_rows().get('Position') == 'Left bottom' and b'TelemetryPosition=5' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'Pfeil links: links unten')
press(DOWN); press(0x25)
check(panel_rows().get('Background opacity') == '90 %' and b'TelemetryOpacity=90' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'Deckkraft 90 % gespeichert')
press(DOWN); press(ENTER)
check(panel_rows().get('Size') == '115 %' and b'TelemetrySize=115' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'Groesse 115 % gespeichert')
press(ESC); press(UP); press(UP); press(UP); press(ENTER)  # Live telemetry -> Mouse in the game menus -> Mouse look -> Mouse steering
for _ in range(10): press(DOWN)
check(selected() == '40 %', selected())
press(ENTER)
check('MouseDeadZone=40\r\n' in ini(), 'Totzone 40 % gespeichert')
press(ESC); press(ESC)
check(tick() == LEVEL_CLOSED, 'Telemetrie/Totzone: zu')

# world page: three on/off rows, restart note
press(0x79)
goto('World: cameras, Drivatars …')
check(selected() == 'World: cameras, Drivatars …', selected())
press(ENTER)
rows = panel_rows()
check(selected() == 'Speed cameras' and 'Names above cars' in rows and 'Event icons on the road' in rows
      and 'Takes effect after a restart' in rows, f'Welt-Seite: {rows}')
before = rows.get('Speed cameras')
press(ENTER)
check(panel_rows().get('Speed cameras') != before, f'Blitzer umgeschaltet: {before} -> {panel_rows().get("Speed cameras")}')
press(ENTER)
check(panel_rows().get('Speed cameras') == before, 'Blitzer zurueck')
press(ESC); press(ESC)
check(tick() == LEVEL_CLOSED, 'Welt: zu')

# no damage page: Off by default, "Other cars" chosen -> Gripline.ini NoDamage=1, then back to Off
press(0x79)
goto('No damage on cars')
check(selected() == 'No damage on cars' and panel_rows().get('No damage on cars') == 'Off', f'Schaden-Zeile zeigt Aus: {panel_rows()}')
press(ENTER)
rows = panel_rows()
check(selected() == 'Off (as in the game)' and rows.get('Off (as in the game)') == '\u2713 active' and 'Other cars' in rows and 'All cars' in rows
      and 'Others = Drivatars and traffic' in rows, f'Schaden-Seite: {rows}')
press(DOWN); press(ENTER)
gi = open(os.path.join(root, 'Gripline.ini'), 'rb').read()
check(b'NoDamage=1' in gi and panel_rows().get('Other cars') == '\u2713 active', f'Andere Autos gewaehlt und gespeichert: {gi!r}')
press(DOWN); press(ENTER)
check(b'NoDamage=2' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'Alle Autos gespeichert')
press(UP); press(UP); press(ENTER)
check(b'NoDamage=0' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'wieder Aus')
press(ESC)
check(panel_rows().get('No damage on cars') == 'Off', 'Hauptseite zeigt Aus')
press(ESC)
check(tick() == LEVEL_CLOSED, 'Schaden: zu')

# V7.1 walking speedometer: Step pulse is the default, Racing bar chosen -> Gripline.ini WalkGauge=2, then Off
press(0x79)
goto('Walking: speedometer')
check(panel_rows().get('Walking: speedometer') == 'Step pulse', f'Tacho-Zeile zeigt Schritt-Puls: {panel_rows().get("Walking: speedometer")!r}')
press(ENTER)
rows = panel_rows()
check(selected() == 'Step pulse' and rows.get('Step pulse') == '\u2713 active' and rows.get('Off') == '' and rows.get('Ring') == ''
      and 'Racing bar' in rows and 'Bottom right while on foot' in rows, f'Tacho-Seite: {rows}')
press(UP); press(ENTER)
gi = open(os.path.join(root, 'Gripline.ini'), 'rb').read()
check(b'WalkGauge=2' in gi and panel_rows().get('Racing bar') == '\u2713 active' and panel_rows().get('Step pulse') == 'default',
      f'Racing-Leiste gewaehlt und gespeichert: {gi!r}')
press(UP); press(UP); press(ENTER)
check(b'WalkGauge=0' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'Tacho aus gespeichert')
press(DOWN); press(DOWN); press(DOWN); press(ENTER)
check(b'WalkGauge=3' in open(os.path.join(root, 'Gripline.ini'), 'rb').read(), 'wieder Schritt-Puls')
press(ESC)
check(panel_rows().get('Walking: speedometer') == 'Step pulse', 'Hauptseite zeigt Schritt-Puls')
press(ESC)
check(tick() == LEVEL_CLOSED, 'Tacho: zu')

# logbook page: totals, nothing driven yet
press(0x79)
goto('Logbook')
check(selected() == 'Logbook', repr(selected()))
press(ENTER)
rows = panel_rows()
check('Driven in total' in rows and 'Driving time' in rows and 'Top speed' in rows and 'No car past 100 m yet' in rows, f'Fahrtenbuch: {rows}')
press(ESC); press(ESC)
check(tick() == LEVEL_CLOSED, 'Fahrtenbuch: zu')
# records page: empty, clear asks first
press(0x79)
goto('Records')
check(selected() == 'Records', selected())
press(ENTER)
rows = panel_rows()
check(rows.get('0-100 km/h') == '\u2013' and 'Clear all records' in rows, f'Rekorde: {rows}')
press(ENTER)
check('Sure? Enter/A again' in panel_rows(), 'Rekorde loeschen fragt nach')
press(ESC)
check('Own sprint' not in rows, f'Eigener Sprint ist weg: {rows}')
press(ESC)
check('Own sprints' not in panel_rows(), 'Eigene Sprints nicht mehr im Menue')
press(ESC)
check(tick() == LEVEL_CLOSED, 'Rekorde: zu')

# settings page (V7): card colour with preview, dashboard cam, update notice
gpath = os.path.join(root, 'Gripline.ini')
open(gpath, 'wb').write(b'[Gripline]\r\nCardColor=0\r\nUpdateCheck=1\r\n')
press(0x79)
check(goto('Settings'), selected())
press(ENTER)
rows = panel_rows()
check(selected() == 'Card colour' and 'Steering animation 1:1' in rows and 'Dashboard cam: forward' in rows
      and 'Dashboard cam: height' in rows and rows.get('Update notice at start') == 'On', f'Einstellungen: {rows}')
press(ENTER)
check(selected() == 'Blue (original)' and panel_rows().get('Blue (original)') == '\u2713 active', f'Farbseite: {panel_rows()}')
press(DOWN)
check(e.call('Fh2TestNotifyColorGet') == 1, 'Vorschau: Rot sofort sichtbar')
press(ESC)
check(e.call('Fh2TestNotifyColorGet') == 0 and selected() == 'Card colour', 'Esc: zurueck auf Blau, Einstellungen')
press(ENTER); press(DOWN); press(DOWN); press(ENTER)
check(e.call('Fh2TestNotifyColorGet') == 2 and panel_rows().get('Green') == '\u2713 active', 'Gruen gewaehlt')
press(ESC)
check(goto('Dashboard cam: forward'), selected())
press(ENTER)
check(selected() == '35 cm', f'nach vorn: aktueller Wert gewaehlt {selected()}')
press(DOWN); press(ENTER)
check(panel_rows().get('40 cm') == '\u2713 active', f'40 cm gesetzt: {panel_rows()}')
press(ESC)
check(goto('Update notice at start'), selected())
press(ENTER)
check(panel_rows().get('Update notice at start') == 'Off' and b'UpdateCheck=0' in open(gpath, 'rb').read(), 'Update-Hinweis aus und gespeichert')
press(ENTER)
check(b'UpdateCheck=1' in open(gpath, 'rb').read(), 'wieder an')
press(ESC)

# cards & notices: presets and single groups -> Gripline.ini CardsHidden
check(goto('Cards & notices') and panel_rows().get('Cards & notices') == 'Show everything', f'Karten-Zeile: {panel_rows().get("Cards & notices")}')
press(ENTER)
rows = panel_rows()
check(selected() == 'Show everything' and rows.get('Show everything') == '\u2713 active' and rows.get('Trip summary (pause)') == '\u2713 on'
      and 'Warnings always show' in rows, f'Karten-Seite: {rows}')
press(DOWN); press(DOWN); press(ENTER)
check(b'CardsHidden=127' in open(gpath, 'rb').read() and panel_rows().get('Start cards') == 'off', 'Nur Warnungen: alles aus, gespeichert')
press(UP); press(ENTER)
check(b'CardsHidden=93' in open(gpath, 'rb').read() and panel_rows().get('New record') == '\u2713 on', 'Wenig: Rekorde + Lenkrad bleiben')
check(goto('Trip summary (pause)'), selected())
press(ENTER)
check(b'CardsHidden=85' in open(gpath, 'rb').read(), 'Fahrt-Bilanz wieder an (einzeln)')
press(ESC)
check(panel_rows().get('Cards & notices') == 'own choice', 'Hauptseite: eigene Auswahl')
press(ENTER)
press(ENTER)  # "Show everything"
check(b'CardsHidden=0' in open(gpath, 'rb').read(), 'alles wieder an')
press(ESC)

# cruise control page: crash switch + minimum speed -> Gripline.ini
check(goto('Cruise control'), selected())
press(ENTER)
rows = panel_rows()
check(rows.get('Switch off on a crash') == 'On' and rows.get('20 km/h') == '\u2713 active' and '10 km/h' in rows, f'Tempomat-Seite: {rows}')
press(ENTER)
check(b'CruiseCrashOff=0' in open(gpath, 'rb').read() and panel_rows().get('Switch off on a crash') == 'Off', 'Aufprall-Schalter aus, gespeichert')
check(goto('Switch off when almost stopped') and panel_rows().get('Switch off when almost stopped') == 'On', selected())
press(ENTER)
check(b'CruiseSlowOff=0' in open(gpath, 'rb').read() and panel_rows().get('Switch off when almost stopped') == 'Off', 'Langsam-Abschaltung aus, gespeichert')
check(goto('10 km/h'), selected())
press(ENTER)
check(b'CruiseMinSpeed=10' in open(gpath, 'rb').read() and panel_rows().get('10 km/h') == '\u2713 active', 'ab 10 km/h gespeichert')
check(goto('Switch off on a crash'), selected())
press(ENTER)
check(b'CruiseCrashOff=1' in open(gpath, 'rb').read(), 'Aufprall-Schalter wieder an')
press(ESC)

# traffic page: presets, restart note
check(goto('Traffic'), selected())
press(ENTER)
rows = panel_rows()
check('Original' in rows and 'No Traffic' in rows and 'Takes effect after a restart' in rows, f'Verkehr: {rows}')
press(ESC); press(ESC)
check(tick() == LEVEL_CLOSED, 'Einstellungen/Karten/Verkehr: zu')

# controller: LS + D-pad right opens, D-pad down moves, A chooses, B back / close
def pad(buttons, ly=0):
    r = e.call('Fh2TestKeyMenuPad', buttons, ly & 0xffffffff)
    tick()
    return r
pad(0)
pad(0x0040); pad(0x0040 | 0x0008); pad(0)
check(tick() == LEVEL_GROUPS, 'Controller: LS + Steuerkreuz rechts oeffnet')
check(pad(0x0002) & 1, 'Controller im Menue: Spiel bekommt nichts')
pad(0)
check(selected() == 'Camera & view', selected())
pad(0, -30000); pad(0)
check(selected() == 'Gripline features', f'linker Stick runter: {selected()}')
pad(0x1000); pad(0)
check(tick() == LEVEL_LIST, 'A waehlt')
pad(0x2000); pad(0)
check(tick() == LEVEL_GROUPS, 'B zurueck')
pad(0x2000)
check(tick() == LEVEL_CLOSED, 'B auf der ersten Seite schliesst')
check(pad(0x2000) & 2, 'B noch gehalten: Spiel bekommt es nicht')
pad(0)
check(not (pad(0x2000) & 2), 'danach wieder normal')
pad(0)

def pretty(stem):
    b = e.alloc(128)
    e.call('Fh2TestLogbookPretty', e.put_bytes(stem.encode() + b'\0'), b, 48)
    return e.wstr(b)
for stem, want in [('NIS_GTR_12', "Nissan GTR '12"), ('CHE_CorvetteStingray_14', "Chevrolet Corvette Stingray '14"),
                   ('DOD_Challenger392_H2_12', "Dodge Challenger392 H2 '12"), ('VW_Type2_63', "Volkswagen Type2 '63"),
                   ('fer_f142_10', "Ferrari f142 '10")]:
    check(pretty(stem) == want, f'Autoname {stem}: {pretty(stem)!r}')

# V7.1: mouse in the game menus (FH2_Keyboard.ini MenuMouse 1 -> 2 -> 0 -> 1)
check(e.call('Fh2TestMenuMouseMode') == 1, 'Maus im Spielmenue: Standard An')
press(0x79)
check(goto('Mouse in the game menus'), selected())
check(panel_rows().get('Mouse in the game menus') == 'On', f'Zeile zeigt An: {panel_rows()}')
press(ENTER)
check(panel_rows().get('Mouse in the game menus') == 'Only when moved' and 'MenuMouse=2\r\n' in ini() and e.call('Fh2TestMenuMouseMode') == 2,
      'Enter: nur bei Bewegung, gespeichert')
press(ENTER)
check(panel_rows().get('Mouse in the game menus') == 'Off' and 'MenuMouse=0\r\n' in ini() and e.call('Fh2TestMenuMouseMode') == 0, 'Enter: Aus')
press(ENTER)
check(panel_rows().get('Mouse in the game menus') == 'On' and 'MenuMouse=1\r\n' in ini(), 'Enter: wieder An')
press(ESC)
check(tick() == LEVEL_CLOSED, 'Maus im Spielmenue: zu')

# V7.1: the F8 pages on MODS > Installed mods > Gripline (mirror)
def mirror(page):
    b = e.alloc(16384)
    n = e.call('Fh2TestKeyMenuMirrorRows', page, b, 16384)
    text = bytes(e.mu.mem_read(b, 16384)).split(b'\0')[0].decode('utf-8')
    out = []
    for line in text.split('\n')[:n]:
        label, value, key, style = line.split('|')
        out.append((label, value, key, int(style)))
    return out
def choose(key):
    return e.call('Fh2TestKeyMenuMirrorChoose', e.put_bytes(key.encode() + b'\0'))
pages = [mirror(p) for p in range(6)]
check(all(pages), 'sechs Seiten mit Zeilen')
allrows = [r for p in pages for r in p]
check(not any('Enter' in r[0] or '◄' in r[0] or 'Esc' in r[0] for r in allrows if r[3] != 0 or not r[2]),
      f'keine Tastatur-Hinweise: {[r[0] for r in allrows if "Enter" in r[0] or "Esc" in r[0]]}')
labels = [r[0] for r in allrows]
check('Card colour' not in labels and 'Steering animation 1:1' not in labels and 'Telemetry panel' not in labels,
      'Zeilen der Mods-Seite nicht doppelt')
check(not any(k in labels for k in ('Throttle', 'Reset all keys to default')), 'keine Tastenbelegung')
mouse = {r[0]: r for r in pages[0]}
check(mouse.get('Mouse in the game menus', ('', ''))[1] == 'On', f'Maus-Seite: Maus im Spielmenue {pages[0][:3]}')
before = panel()
check(choose(mouse['Mouse in the game menus'][2]) == 1 and 'MenuMouse=2\r\n' in ini() and e.call('Fh2TestMenuMouseMode') == 2,
      'A auf der Mods-Seite: nur bei Bewegung, gespeichert')
check(tick() == LEVEL_CLOSED and panel() == before, 'F8-Menue bleibt zu, kein Panel')
check({r[0]: r for r in mirror(0)}['Mouse in the game menus'][1] == 'Only when moved', 'Seite zeigt den neuen Wert')
choose(mouse['Mouse in the game menus'][2]); choose(mouse['Mouse in the game menus'][2])
check('MenuMouse=1\r\n' in ini(), 'zweimal weiter: wieder An')
gentle = [r for r in pages[0] if r[0] == 'Gentle']
check(gentle and choose(gentle[0][2]) == 1 and 'MouseSteering=25\r\n' in ini(), 'Maus-Lenkung Sanft ueber die Mods-Seite')
off = [r for r in mirror(0) if r[0] == 'Off' and r[2]]
choose(off[0][2])
check('MouseSteering=0\r\n' in ini(), 'Maus-Lenkung wieder aus')
check(choose('LB999') == 0 and choose('LF102') == 0 and choose('xyz') == 0, 'unbekannte / nicht gelistete Zeilen: nichts')
walk = {r[0]: r for r in pages[1]}
check('Walking: handheld camera' in walk and 'Walking: speedometer' in walk, f'Laufen-Seite: {list(walk)}')
# F8 open at the same time: its page stays
press(0x79)
goto('Mouse in the game menus')
sel = selected()
choose(mouse['Mouse in the game menus'][2])
check(tick() == LEVEL_GROUPS and selected() == sel and panel_rows().get('Mouse in the game menus') == 'Only when moved',
      'F8 offen: bleibt auf seiner Zeile und zeigt den neuen Wert')
press(ENTER); press(ENTER)
check(panel_rows().get('Mouse in the game menus') == 'On' and 'MenuMouse=1\r\n' in ini(), 'F8 danach normal bedienbar (Aus, An)')
press(ESC)
check(tick() == LEVEL_CLOSED, 'zu')

# "all to default" with confirmation
press(0x79)
press(UP)
check(selected() == 'Reset all keys to default', selected())
press(ENTER)
check(selected() == 'No, back', 'Sicherheitsfrage: Nein vorgewaehlt')
press(DOWN); press(ENTER)
check(tick() == LEVEL_GROUPS and binding(42) == F8 and 'KeyMenu=F8\r\n' in ini() and 'Brake=S\r\n' in ini(),
      'Alle auf Standard: F8 wieder, Datei gespeichert')
press(F8)
check(tick() == LEVEL_CLOSED, 'F8 schliesst wieder')

print()
print('ALLE TESTS OK (%d Pruefungen)' % checks[0] if not fails else '%d FEHLER' % len(fails))
sys.exit(1 if fails else 0)
