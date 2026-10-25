"""Time of day menu: the real compiled code against a time-of-day clock laid out like the game's
(constructor game+0x1975660). The game's own SetTime (game+0x197aec0) runs from exe.img."""
import os, struct, sys, tempfile
from emu import Emu, GAME

DLL = sys.argv[1]
GAME_DIR = os.environ.get('FH2_GAME', 'game')
fails = []
def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)

root = tempfile.mkdtemp(prefix='eo-time-')
e = Emu(DLL, root)
ROOT, SET, NET = 0x34aba60, 0x197aec0, 0x36e24f8
image = open(os.path.join(GAME_DIR, 'exe.img'), 'rb').read()
e.mu.mem_write(GAME + 0x197a000, image[0x197a000:0x197c000])  # SetTime and its remap helper

def new_clock():
    c = e.alloc(0x410)
    e.wd(c + 0x8, 0xbf800000); e.wd(c + 0xc, 0xbf800000)
    e.wf(c + 0x10, 43200.0); e.wf(c + 0x14, 1.0); e.wf(c + 0x18, 48.0)
    e.mu.mem_write(c + 0x29, b'\x01')
    return c
def link(target):
    r, w, s = e.alloc(0x100), e.alloc(0x100), e.alloc(0x500)
    e.wq(GAME + ROOT, r); e.wq(r + 0x48, w); e.wq(w + 0x98, s); e.wq(s + 0x480, target)
clock = new_clock()
link(clock)
net = e.alloc(0x40)
e.wq(GAME + NET, net)
def online(on): e.wd(net + 0x10, 1 if on else 0)

def tick(ms=1100):
    e.tick += ms
    e.call('Fh2TestTimeTick')
def time(): return e.rf(clock + 0x10)
def speed(): return e.rf(clock + 0x18)
def stopped(): return e.mu.mem_read(clock + 0x24, 1)[0]
def run(seconds):  # what the game's update does while the clock is not stopped
    if not stopped():
        e.wf(clock + 0x10, (time() + seconds * speed()) % 86400.0)

print('Start')
e.call('Fh2TestTimeStart', GAME, 0)
check(e.call('Fh2TestTimeClock') == clock, 'Tageszeit-Uhr über die Kette gefunden')
tick()
check(time() == 43200.0 and speed() == 48.0 and not stopped(), 'Original beim Start: nichts angefasst')

print('Feste Zeiten')
for value, seconds, name in [(1, 8 * 3600, 'Morgen 08:00'), (2, 13 * 3600, 'Mittag 13:00'),
                             (3, 19 * 3600 + 35 * 60, 'Sonnenuntergang 19:35'), (4, 22 * 3600 + 30 * 60, 'Nacht 22:30')]:
    e.call('Fh2TestTimeSet', value); tick(0)
    check(time() == seconds and stopped() == 1, f'{name}: Zeit {time()} gesetzt, Uhr steht')
run(10)
check(time() == 22 * 3600 + 30 * 60, 'Uhr steht: Zeit bleibt')
e.wf(clock + 0x10, 1000.0); tick()
check(time() == 22 * 3600 + 30 * 60 and stopped(), 'Zeit von außen verstellt (freie Fahrt): wieder Nacht')

print('Events')
e.call('Fh2TestTimeFreeRoam', 0); tick()
check(not stopped() and speed() == 48.0, 'Rennen/Event: Uhr läuft wieder wie im Spiel')
e.wf(clock + 0x10, 70000.0); run(10); tick()
check(time() == 70480.0, 'im Event: Zeit des Events bleibt, kein Eingriff')
e.call('Fh2TestTimeFreeRoam', 1); tick()
check(time() == 22 * 3600 + 30 * 60 and stopped(), 'zurück in der freien Fahrt: wieder Nacht')

print('Online')
online(True); tick()
check(not stopped() and speed() == 48.0, 'online (FH2MP): Uhr wieder frei')
e.wf(clock + 0x10, 5000.0); tick()
check(time() == 5000.0 and not stopped(), 'online: Session-Zeit bleibt, kein Eingriff')
online(False); tick()
check(time() == 22 * 3600 + 30 * 60 and stopped(), 'wieder offline: Nacht')

print('Langsamer Tag')
e.call('Fh2TestTimeSet', 5); tick(0)
check(not stopped() and speed() == 12.0, f'Langsamer Tag: Uhr läuft, Tempo 48 -> {speed()}')
tick()
check(speed() == 12.0, 'bleibt langsam (nicht nochmal geteilt)')
e.call('Fh2TestTimeFreeRoam', 0); tick()
check(speed() == 48.0, 'Event: normales Tempo')
e.call('Fh2TestTimeFreeRoam', 1); tick()
check(speed() == 12.0, 'freie Fahrt: wieder langsam')
e.wf(clock + 0x18, 96.0); tick()
check(speed() == 24.0, 'Spiel setzt neues Tempo (96): ein Viertel davon')

print('Original')
e.call('Fh2TestTimeSet', 0); tick(0)
check(speed() == 96.0 and not stopped(), 'Original: Tempo des Spiels zurück')
e.call('Fh2TestTimeSet', 2); tick(0)
e.call('Fh2TestTimeSet', 0); tick(0)
check(not stopped() and time() == 13 * 3600, 'Original nach Mittag: Uhr läuft weiter ab 13:00')
e.wf(clock + 0x10, 100.0); tick()
check(time() == 100.0, 'Original: keine Eingriffe mehr')

print('Sperren und Fehlerfälle')
e.mu.mem_write(clock + 0x25, b'\x01')
e.call('Fh2TestTimeSet', 1); tick(0)
check(time() == 100.0 and not stopped(), 'Zeit von der Spiel-Config fest (+0x25): nichts')
e.mu.mem_write(clock + 0x25, b'\x00')
e.call('Fh2TestTimeSet', 6); tick()
check(time() == 8 * 3600 and stopped(), 'Wert 6 (gibt es nicht) ignoriert, Morgen bleibt')
e.wf(clock + 0x10, 99999.0); tick()
check(e.rf(clock + 0x10) == 99999.0, 'unplausible Uhr (Zeit > 24 h): nicht angefasst')
e.wf(clock + 0x10, 100.0)
clock2 = new_clock(); link(clock2); old = clock; clock = clock2
tick()
check(time() == 8 * 3600 and stopped(), 'neue Spielwelt (neue Uhr): Morgen wieder gesetzt')
e.wq(GAME + ROOT, 0)
tick()
check(e.call('Fh2TestTimeClock') == 0, 'keine Spielwelt (Ladebildschirm): nichts')
print()
print('FEHLER:' if fails else 'ALLE TESTS OK', len(fails) or '')
sys.exit(1 if fails else 0)
