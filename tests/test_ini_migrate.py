"""Test of features/ini_migrate.c with the real compiled test DLL: the German settings files of
fork1-fork3 (FH2_Tastatur.ini) become the English one of fork4 with the user's values, and the old
file ends up unchanged in FH2_Settings_Backup. (The wheel and weather files went with V6: Azimuth drives
the wheel, Extra Options the weather.)"""
import os, shutil, sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
HERE = os.path.dirname(os.path.abspath(__file__))
fails = []
checks = [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


GERMAN_KEYBOARD = (b'; FH2 Tastatur (inoffiziell) - Einstellungen\r\n[Tastatur]\r\nAktiv=1\r\nLenkzeit=200\r\n'
                   b'Rueckstellzeit=90\r\nPedalzeit=60\r\nGas=W,Hoch\r\nBremse=S\r\nHandbremse=Leertaste\r\n'
                   b'Zurueck=Rueck\r\nTabLinks=BildHoch\r\nBlickZurueck=C,Num2\r\nLicht=-\r\n')


def setup():
    root = tempfile.mkdtemp(prefix='eo-migrate-')
    open(os.path.join(root, 'FH2_Tastatur.ini'), 'wb').write(GERMAN_KEYBOARD)
    return root


def read(root, *name):
    return open(os.path.join(root, *name), 'rb').read()


print('Deutsche Dateien von fork1-fork3')
root = setup()
e = Emu(DLL, root)
e.call('Fh2TestIniMigrate', e.put_wstr('C:\\test'))
report = bytes(e.mu.mem_read(e.call('Fh2TestIniMigrateReport'), 600)).split(b'\0')[0].decode()
print('   ' + report.replace('\r\n', '\n   ').rstrip())

kb = read(root, 'FH2_Keyboard.ini').decode('ascii')
for line in ('Enabled=1', 'SteerTime=200', 'ReturnTime=90', 'Throttle=W,Up', 'Brake=S', 'Handbrake=Space',
             'Back=Backspace', 'TabLeft=PageUp', 'LookBack=C,Num2', 'Lights=-', 'ShiftUp=E', 'ArrowUp=Up'):
    check(('\r\n' + line + '\r\n') in kb, f'Tastatur: {line}')
check('[Keyboard]' in kb and 'Tastatur]' not in kb and 'Gas=' not in kb, 'FH2_Keyboard.ini: englisch')

backup = os.path.join(root, 'FH2_Settings_Backup')
check(read(backup, 'FH2_Tastatur.ini') == GERMAN_KEYBOARD, 'Original unverändert in FH2_Settings_Backup')
check(not os.path.exists(os.path.join(root, 'FH2_Tastatur.ini')), 'alte Datei nicht mehr im Azimuth-Ordner')
check('FH2_Tastatur.ini' in report, 'Gripline.log erklärt es')
check(not os.path.exists(os.path.join(root, 'FH2_Wheel.ini')), 'keine FH2_Wheel.ini mehr (Lenkrad = Azimuth)')

print('Zweiter Start: nichts mehr zu tun')
before = {n: read(root, n) for n in os.listdir(root) if n.endswith('.ini')}
e.call('Fh2TestIniMigrate', e.put_wstr('C:\\test'))
check({n: read(root, n) for n in os.listdir(root) if n.endswith('.ini')} == before, 'keine Datei geändert')
check(bytes(e.mu.mem_read(e.call('Fh2TestIniMigrateReport'), 4)).split(b'\0')[0] == b'', 'kein Log-Eintrag')

print('Englische Datei gibt es schon (z. B. aus dem Release-ZIP kopiert)')
root = setup()
open(os.path.join(root, 'FH2_Keyboard.ini'), 'wb').write(b'[Keyboard]\r\nEnabled=0\r\n')
e = Emu(DLL, root)
e.call('Fh2TestIniMigrate', e.put_wstr('C:\\test'))
check(read(root, 'FH2_Keyboard.ini') == b'[Keyboard]\r\nEnabled=0\r\n', 'vorhandene FH2_Keyboard.ini bleibt wie sie ist')
check(read(root, 'FH2_Tastatur.ini') == GERMAN_KEYBOARD, 'alte FH2_Tastatur.ini bleibt dann liegen')

print('Neue Installation ohne alte Dateien')
root = tempfile.mkdtemp(prefix='eo-migrate-new-')
e = Emu(DLL, root)
e.call('Fh2TestIniMigrate', e.put_wstr('C:\\test'))
check(os.listdir(root) == [], 'nichts angelegt (die Module legen ihre englischen Dateien selbst an)')

print()
print('ALLE TESTS OK (%d Pruefungen)' % checks[0] if not fails else '%d FEHLER' % len(fails))
sys.exit(1 if fails else 0)
