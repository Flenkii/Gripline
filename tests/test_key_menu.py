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
    b = e.alloc(64)
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
def ini():
    return open(ini_file, 'rb').read().decode('ascii')
def binding(index, k=0):
    return e.call('Fh2TestKeyboardBinding', index, k)

LEVEL_CLOSED, LEVEL_GROUPS, LEVEL_LIST, LEVEL_BINDING, LEVEL_CAPTURE = 0, 1, 2, 3, 4

check(e.call('Fh2TestKeyMenuCover') == 43, 'jede der 43 Tasten steht genau einmal im Menue')
check(tick() == LEVEL_CLOSED and panel() == 0, 'Menue anfangs zu')
card = e.alloc(256)
ok = e.call('Fh2TestKeyMenuStartCard', card, 256)
card_text = bytes(e.mu.mem_read(card, 256)).split(b'\0')[0].decode('utf-8')
check(ok == 1 and card_text == 'Press F8 to change your keys\nKey menu: arrows, Enter, Esc', f'eigene Startkarte: {card_text!r}')
check(e.call('Fh2TestKeyboardBinding', 42, 0) == F8, 'KeyMenu = F8 (Standard)')

press(F8)
check(tick() == LEVEL_GROUPS and panel() == 6, f'F8 oeffnet das Menue (4 Seiten + Alle auf Standard + Hinweis): {panel()}')
check(selected() == 'Driving', f'erste Zeile gewaehlt: {selected()}')

# while open: the game and noclip get no key
keys.add(ord('W'))
check(e.call('Fh2TestKeyboardHeld', 0) == 0, 'Menue offen: W kommt bei Noclip/Freecam nicht an')
keys.discard(ord('W'))

press(DOWN)
check(selected() == 'Camera & view', f'Pfeil runter: {selected()}')
press(UP); press(UP)
check(selected() == 'Reset all to default', f'Pfeil hoch springt ans Ende: {selected()}')
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

# "all to default" with confirmation
press(0x79)
press(UP)
check(selected() == 'Reset all to default', selected())
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
