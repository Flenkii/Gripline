"""Problem report: Gripline_Report.zip with the logs and INIs (stored ZIP, opens everywhere)."""
import io, os, sys, tempfile, zipfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails = []

def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)

root = tempfile.mkdtemp(prefix='eo-report-')
os.makedirs(os.path.join(root, 'fh2-mods'))
os.makedirs(os.path.join(root, 'data', 'bin'))
content = {
    'FH2_ExtraOptions.log': b'Gripline V6 log\r\n' * 50,
    'FH2_Keyboard.ini': b'[Keyboard]\r\nEnabled=1\r\n',
    'fh2-mods/platform-mods.txt': b'NitroSystem.dll\tNitroSystem\t1.0\r\nGripline.dll\tGripline\tV6\r\n',
    'azimuth.log': b'Azimuth 1.0.3\r\n',
}
for name, data in content.items():
    host = os.path.join(root, 'data', 'logs', name) if name == 'azimuth.log' else os.path.join(root, *name.split('/'))
    os.makedirs(os.path.dirname(host), exist_ok=True)
    open(host, 'wb').write(data)

e = Emu(DLL, root)
d = e.alloc(64)
e.mu.mem_write(d, 'C:\\test'.encode('utf-16-le') + bytes(2))
out = e.alloc(600)
e.call('Fh2TestModsText', d, out, 600)  # game start: mod_scan_start (where fh2-mods is)
n = e.call('Fh2TestReport', d)
check(n == 1 + len(content), f'{n} files packed (summary + {len(content)} found, missing ones skipped)')
path = os.path.join(root, 'Gripline_Report.zip')
check(os.path.exists(path), 'Gripline_Report.zip next to the INIs')
z = zipfile.ZipFile(path)
check(z.testzip() is None, 'ZIP is valid (CRC ok)')
names = z.namelist()
check(names[0] == 'Gripline_Report.txt', f'summary first: {names}')
for name, data in content.items():
    check(name in names and z.read(name) == data, 'packed unchanged: ' + name)
summary = z.read('Gripline_Report.txt').decode()
check(summary.startswith('Gripline V7 Test problem report') and 'NitroSystem' in summary and 'Gripline,' not in summary,
      'summary: version and mods: ' + summary.replace('\r\n', ' | '))
check(z.infolist()[0].date_time == (2026, 10, 25, 14, 30, 10), f'date and time: {z.infolist()[0].date_time}')
e.call('Fh2TestReport', d)
check(zipfile.ZipFile(path).namelist() == names, 'a second report replaces the first')

print(f'\n{len(fails)} FEHLER' if fails else '\nalles ok')
sys.exit(1 if fails else 0)
