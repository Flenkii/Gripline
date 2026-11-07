"""V7 update notice: card text for GitHub's newest-release answer (no network in the test)."""
import sys, tempfile, json
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
fails, checks = [], [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


e = Emu(DLL, tempfile.mkdtemp(prefix='gl-update-'))


def card(answer, german=0, size=256):
    data = (answer if isinstance(answer, str) else json.dumps(answer)).encode('utf-8') + b'\0'
    src, out = e.alloc(len(data) + 16), e.alloc(size)
    e.mu.mem_write(src, data)
    e.mu.mem_write(out, bytes(size))
    r = e.call('Fh2TestUpdateCard', src, german, out, size)
    return r, bytes(e.mu.mem_read(out, size)).split(b'\0')[0].decode('utf-8')


rel = lambda tag, name: {"url": "x", "id": 1, "tag_name": tag, "name": name, "body": "text \"quoted\""}
r, t = card(rel('v0.2.98-fork6.4', 'Gripline V6.4 – noclip'))
check(r == 0 and t == '', f'V6.4 auf GitHub, V7.1 laeuft: keine Karte ({r}, {t!r})')
r, t = card(rel('v0.2.98-fork7', 'Gripline V7'))
check(r == 0, 'gleiche Version V7: keine Karte')
r, t = card(rel('v0.2.98-fork7.1', 'Gripline V7.1'))
check(r == 0, 'gleiche Version V7.1: keine Karte')
r, t = card(rel('v0.2.98-fork7.2', 'Gripline V7.2'))
check(r == 1 and t == 'Gripline V7.2 is out\nPlease use it, V7.1 is outdated', f'V7.2 englisch: {t!r}')
r, t = card(rel('v0.2.98-fork8', 'Gripline V8'), german=1)
check(r == 1 and t == 'Gripline V8 ist draußen\nBitte nutzen, V7.1 ist veraltet', f'V8 deutsch: {t!r}')
r, t = card(rel('v8-final', 'Gripline V8.2 - big update'))
check(r == 1 and 'V8.2' in t, f'Tag ohne fork -> Name als Ersatz: {t!r}')
r, t = card(rel('something', 'Something else'))
check(r == 0, 'unbekanntes Schema: keine Karte (kein Fehlalarm)')
r, t = card('{"message":"API rate limit exceeded"}')
check(r == 0, 'Fehlerantwort: keine Karte')
r, t = card('{"tag_name": "v0.2.98-fork7.10", "name": "x"}')
check(r == 1 and 'V7.10' in t, f'7.10 > 7.0: {t!r}')
r, t = card('{"tag_name": "v0.2.98-fork7.1.1", "name": "Gripline V7.1.1"}')
check(r == 1 and t == 'Gripline V7.1.1 is out\nPlease use it, V7.1 is outdated', f'7.1.1 > 7.1 (dritte Zahl zaehlt): {t!r}')
r, t = card('{"tag_name": "v0.2.98-fork6.4.1"}')
check(r == 0, '6.4.1 < 7: keine Karte')
r, t = card('{"tag_name": "v0.2.98-fork7.0.0"}')
check(r == 0, '7.0.0 < 7.1: keine Karte')
r, t = card('{"tag_name": "v0.2.98-fork6.9"}')
check(r == 0, '6.9 < 7: keine Karte')

print(f'\n{checks[0] - len(fails)}/{checks[0]} ok')
sys.exit(1 if fails else 0)
