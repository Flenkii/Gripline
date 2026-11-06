"""Gripline.ini reading (settings.c): one bad line is skipped, every other setting still loads (V7)."""
import os, struct, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
fails, checks = [], [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


root = tempfile.mkdtemp(prefix='gl-settings-')
e = Emu(DLL, root)
KEYS = ['TrafficLevel', 'RushHourAllDay', 'Notifications', 'SteeringAnimation', 'CockpitViewForward',
        'CockpitViewUp', 'CardColor', 'CardStyle', 'UpdateCheck', 'SpeedCameras', 'CarLabels', 'Telemetry',
        'TelemetryShow', 'EventIcons', 'NewsSeen', 'NoDamage', 'CardsHidden', 'CruiseCrashOff', 'CruiseMinSpeed',
        'TelemetryPosition', 'TelemetryOpacity', 'TelemetrySize']


def read(text):
    path = os.path.join(root, 'Gripline.ini')
    if text is None:
        if os.path.exists(path):
            os.remove(path)
    else:
        open(path, 'wb').write(text.encode('utf-8'))
    values, ignored, status = e.alloc(4 * len(KEYS)), e.alloc(4), e.alloc(512)
    ok = e.call('Fh2TestSettingsRead', e.put_wstr('C:\\test'), values, len(KEYS), ignored, status, 512)
    v = struct.unpack('<%di' % len(KEYS), bytes(e.mu.mem_read(values, 4 * len(KEYS))))
    s = bytes(e.mu.mem_read(status, 512)).split(b'\0')[0].decode()
    return ok & 0xffffffff, dict(zip(KEYS, v)), e.rd(ignored), s


ok, v, n, s = read(None)
check(ok == 1 and n == 0 and v['CardStyle'] == 2 and v['Notifications'] == 1, 'keine Datei: Standardwerte')

ok, v, n, s = read('[Traffic]\r\nTrafficLevel=0\r\n[Gripline]\r\nCardColor=3\r\nNoDamage=2\r\nCardStyle=1\r\n')
check(ok == 1 and n == 0 and (v['CardColor'], v['NoDamage'], v['CardStyle']) == (3, 2, 1), f'gute Datei: alles gelesen {v}')

ok, v, n, s = read('[Gripline]\r\nCardColor=9\r\n')
check(ok == 1 and v['CardColor'] == 9, f'Kartenfarbe 9 = Regenbogen: {v["CardColor"]}')
ok, v, n, s = read('[Gripline]\r\nCardColor=10\r\nNoDamage=2\r\nCardStyle=1\r\n')
check(ok == 0 and n == 1, f'falscher Wert: eine Zeile uebersprungen ({ok}, {n})')
check(v['CardColor'] == 0 and v['NoDamage'] == 2 and v['CardStyle'] == 1,
      f'Zeilen danach werden trotzdem gelesen (frueher verloren): {v}')
check(s.startswith('IGNORED') and 'line 2 (CardColor)' in s, f'Log nennt Zeile und Schluessel: {s!r}')

ok, v, n, s = read('[Gripline]\r\nFutureKey=1\r\n[Unknown]\r\nNoDamage=1\r\n[Gripline]\r\nNoDamage=1\r\nmuell\r\n')
check(ok == 0 and n == 3 and v['NoDamage'] == 1, f'unbekannter Schluessel/Abschnitt/Muell: {n} Zeilen, Rest gelesen {v}')

ok, v, n, s = read('[Traffic]\r\nWeather=1\r\nTrafficLevel=2\r\n')
check(ok == 1 and n == 0 and v['TrafficLevel'] == 2, 'alte Schluessel (Weather) still ignoriert')

ok, v, n, s = read('\ufeff[Gripline]\r\nTelemetryShow=4095\r\n')
check(ok == 1 and v['TelemetryShow'] == 4095, 'UTF-8 mit BOM')

ok, v, n, s = read('[Gripline]\r\nCardsHidden=117\r\n')
check(ok == 1 and v['CardsHidden'] == 117, 'CardsHidden gelesen')

print(f'\n{checks[0]} checks, {len(fails)} failed')
sys.exit(1 if fails else 0)
