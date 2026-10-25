"""End-to-end test of the in-game page 'Lenkrad & Verkehr' with the real compiled test DLL."""
import os, shutil, struct, sys, tempfile, xml.dom.minidom
from emu import Emu, GAME

DLL = sys.argv[1] if len(sys.argv) > 1 else 'winmm_test.dll'
GAME_DIR = os.environ.get('FH2_GAME', 'game')  # entpackte Spieldateien (nicht im Repo)
USER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixture')  # fester Teststand (Mein Setup)
HUD = os.path.join(GAME_DIR, 'ui/Scenes/Anthem/A_HUDOptions.xaml')
VIDEO = os.path.join(GAME_DIR, 'ui/Scenes/Anthem/A_VideoOptions.xaml')
fails = []
# setting ids (rig_settings.h)
ENABLED = 0
FFB_MODE, CLUTCH_BRAKE, LEVEL, ALLDAY, WEATHER = 17, 18, 19, 20, 21
KEY_ENABLED, KEY_STEER, KEY_RETURN, KEY_PEDAL = 22, 23, 24, 25
PROFILE, PRESET = 30, 31
VIEW_FORWARD, VIEW_UP = 28, 29
GRIP_NOTIFY, GRIP_STEERING = 26, 27

def check(cond, text):
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)

root = tempfile.mkdtemp(prefix='eo-menu-')
for name in ('FH2_Lenkrad.ini', 'FH2_ExtraOptions.ini'):
    shutil.copy(os.path.join(USER, name), os.path.join(root, name))
os.makedirs(os.path.join(root, 'game'))
german_wheel = open(os.path.join(root, 'FH2_Lenkrad.ini'), 'rb').read()

e = Emu(DLL, root)

# ---- fake game image: language, tunables, native UI helpers
def set_language(code):
    manager = e.alloc(0x80)
    raw = code.encode() + b'\0' * (16 - len(code))
    e.mu.mem_write(manager + 0x30, raw)
    e.wq(manager + 0x30 + 16, len(code))
    e.wq(manager + 0x30 + 24, 15)
    e.wq(GAME + 0x3662990, manager)

VT_FLOAT, VT_INT = 0x2c5cb58, 0x2c81898
TUNABLES = [(0x36e8f18, VT_FLOAT, 14.0, [14, 24, 36, 48, 64, 100]),
            (0x36e6e88, VT_FLOAT, 16.0, [16, 24, 36, 48, 64, 100]),
            (0x36e8d10, VT_FLOAT, 10.0, [10, 14, 20, 26, 32, 50]),
            (0x36e6dd0, VT_FLOAT, 7.0, [7, 10, 14, 18, 24, 40]),
            (0x34c80f8, VT_FLOAT, 8.0, [8, 7, 6, 5, 4, 3]),
            (0x34c8578, VT_INT, 5, [5, 8, 12, 16, 20, 30])]
TIMES = [(0x36e6d80, 7.0, 0.5), (0x36e6e60, 19.0, 23.5), (0x36e8ea0, 23.0, 23.5), (0x36e6df8, 5.0, 0.5)]
for rva, vt, orig, _ in TUNABLES:
    e.wq(GAME + rva, GAME + vt)
    for off in (0x10, 0x14, 0x18, 0x20):
        (e.wd if vt == VT_INT else e.wf)(GAME + rva + off, int(orig) if vt == VT_INT else orig)
for rva, orig, _ in TIMES:
    e.wq(GAME + rva, GAME + VT_FLOAT)
    for off in (0x10, 0x14, 0x18, 0x20):
        e.wf(GAME + rva + off, orig)

def tunable(rva, vt=VT_FLOAT):
    return e.rd(GAME + rva + 0x10) if vt == VT_INT else e.rf(GAME + rva + 0x10)

changed = []
def box(kind):
    def f():
        out, value_ptr = e.arg(0), e.arg(1)
        b = e.alloc(0x30)
        e.mu.mem_write(b + 0x28, bytes(e.mu.mem_read(value_ptr, 4)))
        e.wq(b, 0x1111 if kind == 'int' else 0x2222)
        e.wq(out, b)
        return out
    return f
e.stub('box_int', box('int'), GAME + 0x4702d0)
e.stub('box_float', box('float'), GAME + 0x3f5990)
e.stub('property_changed', lambda: changed.append(e.arg(1)), GAME + 0xcf9c60)

set_language('DE')
e.call('Fh2TestVideoPrepare', GAME)
exe = e.put_wstr('C:\\test\\game\\forza_durango_release_final.exe')
directory = e.put_wstr('C:\\test')
print('Start')
r = e.call('Fh2TestRigStart', GAME, exe, directory)
check(r == 1, f'Extra-Options-INI gelesen und Speicher-Thread gestartet (Ergebnis {r})')
check(len(e.threads) == 3, f'3 Threads angelegt (Verkehr, Lenkrad-Hook, Speichern): {len(e.threads)}')

# ---- German files of fork1-fork3 -> English ones
print('Umzug der deutschen INIs (fork1-fork3) in die englischen')
backup = os.path.join(root, 'FH2_Settings_Backup')
original_wheel = open(os.path.join(root, 'FH2_Wheel.ini'), 'rb').read()
original_eo = open(os.path.join(root, 'FH2_ExtraOptions.ini'), 'rb').read()
w = original_wheel.decode('latin1')
check(not os.path.exists(os.path.join(root, 'FH2_Lenkrad.ini')) and
      open(os.path.join(backup, 'FH2_Lenkrad.ini'), 'rb').read() == german_wheel,
      'alte FH2_Lenkrad.ini unverändert in FH2_Settings_Backup')
check('[Wheel]' in w and 'Lenkrad]' not in w and 'Rotation=180' in w and 'FFBStrength=75' in w and 'Rumble=50' in w
      and 'Lock=450' in w and 'Smoothing=35' in w and 'Curve=30' in w and 'BrakeAxis=Rz' in w,
      'FH2_Wheel.ini: englisch, deine Werte übernommen (auch versteckte wie Lock/Curve)')
check('Clutch pedal as brake' in w and 'Aktiv' not in w and 'Drehwinkel' not in w, 'englische Kommentare, keine deutschen Schlüssel')
check(b'Weather=0' in original_eo and b'Wetter=' not in original_eo and
      open(os.path.join(backup, 'FH2_ExtraOptions.ini'), 'rb').read().count(b'Wetter=0') == 1,
      'FH2_ExtraOptions.ini: Wetter= -> Weather= (Kopie im Backup)')

# ---- XAML
print('XAML (HUD-Seite, echte Spieldatei)')
def extend(path):
    text = open(path, 'rb').read()
    src = e.put_bytes(text)
    cap = 131072
    out = e.alloc(cap)
    n = e.call('Fh2TestVideoXaml', src, len(text), out, cap)
    return text, bytes(e.mu.mem_read(out, n)) if n else b''

hud_in, hud = extend(HUD)
check(len(hud) > len(hud_in), f'HUD-XAML erweitert: {len(hud_in)} -> {len(hud)} Bytes (Limit 131072)')
try:
    dom = xml.dom.minidom.parseString(hud)
    check(True, 'HUD-XAML ist wohlgeformtes XML')
except Exception as ex:
    dom = None
    check(False, f'HUD-XAML wohlgeformt: {ex}')
open(os.path.join(tempfile.gettempdir(), 'hud_de.xaml'), 'wb').write(hud)
props = ['EoRigProfile', 'EoRigWheelEnabled', 'EoRigForceFeedback', 'EoRigStrength', 'EoRigRumble', 'EoRigRotation',
         'EoRigClutchBrake', 'EoRigButton1', 'EoRigButton2', 'EoRigButton3', 'EoKeyEnabled', 'EoKeySteer',
         'EoKeyReturn', 'EoKeyPedal', 'EoRigDensity',
         'EoRigTrafficLevel', 'EoRigRushHour', 'EoRigWeather', 'EoGripSteering', 'EoGripViewForward', 'EoGripViewUp', 'EoGripNotify', 'EoGripReport']
hidden = ['EoRigFfbMode', 'EoRigDamping', 'EoRigFriction', 'EoRigSmoothing', 'EoRigMinimumForce',
          'EoRigOutward', 'EoRigCentering', 'EoRigLock', 'EoRigCurve', 'EoRigShifter']
if dom:
    lst = [n for n in dom.getElementsByTagName('local:CommonListBox') if n.getAttribute('x:Name') == 'OptionsList'][0]
    rows = [n for n in lst.childNodes if n.nodeType == n.ELEMENT_NODE]
    bindings = [(r.getAttribute('SelectedIndex') or r.getAttribute('Value')) for r in rows]
    rig = [b for b in bindings if b[len('{Binding '):-1] in props]
    check(rig == ['{Binding %s}' % p for p in props], f'{len(rig)} neue Zeilen in richtiger Reihenfolge am Ende der Liste')
    check(bindings[-len(props):] == rig, 'neue Zeilen stehen ganz unten (nach Drift/FOV/EO)')
    check(not [b for b in bindings if b[len('{Binding '):-1] in hidden] and not any(('EO_' + h).encode() in hud for h in hidden),
          'keine Zeilen mehr für das alte FFB (Art, Profil, Dämpfung, Reibung, ...)')
    keys = {n.getAttribute('x:Key') for n in dom.getElementsByTagName('*') if n.getAttribute('x:Key')}
    used = set()
    for n in dom.getElementsByTagName('*'):
        for a in ('Tag', 'Style', 'BasedOn'):
            v = n.getAttribute(a)
            if v.startswith('{StaticResource EO_'):
                used.add(v[len('{StaticResource '):-1])
        for c in n.childNodes:
            pass
    import re
    for conv in re.findall(rb'Converter=\{StaticResource (EO_[A-Za-z0-9]+)\}', hud):
        used.add(conv.decode())
    check(used <= keys, f'alle EO-Ressourcen definiert ({len(used)} benutzt): fehlend {sorted(used - keys)}')
    headings = [n.getAttribute('Heading') for n in dom.getElementsByTagName('local:TileMenuDataItem')
                if n.getAttribute('x:Key')[3:] in props]
    want = {0: 'Eingabe: Profil', 3: 'Lenkrad: FFB-Stärke', 5: 'Lenkrad: Drehwinkel (Grad)',
            6: 'Lenkrad: Kupplung als Bremse', 7: 'Lenkrad: Taste RSB', 10: 'Tastatur: Tastatur',
            13: 'Tastatur: Pedalzeit (ms)', 14: 'Verkehr: Voreinstellung', 15: 'Verkehr: Menge', 17: 'Wetter'}
    check(all(headings[i] == t for i, t in want.items()), 'deutsche Texte: ' + ', '.join(repr(headings[i]) for i in want))
    for name, coll in (('EoRigButton1', 'FunctionChoices'), ('EoKeyEnabled', 'ToggleChoices'),
                       ('EoRigClutchBrake', 'ToggleChoices')):
        row = [r for r in rows if r.getAttribute('SelectedIndex') == '{Binding %s}' % name][0]
        check(row.getAttribute('ItemsSource') == '{Binding %s}' % coll, f'{name} nutzt {coll}')
    for name, style in (('EoKeySteer', 'EO_KeyTime'), ('EoKeyPedal', 'EO_KeyTime')):
        row = [r for r in rows if r.getAttribute('Value') == '{Binding %s}' % name][0]
        check(row.getAttribute('Style') == '{StaticResource %s}' % style, f'{name} ist ein Schieberegler ({style})')
    check(hud.startswith(b'\xef\xbb\xbf'), 'UTF-8-BOM vorhanden (Spiel braucht ihn für Umlaute)')
    # every original row still there
    orig_dom = xml.dom.minidom.parseString(hud_in)
    orig_rows = [n for n in [x for x in orig_dom.getElementsByTagName('local:CommonListBox')][0].childNodes if n.nodeType == n.ELEMENT_NODE]
    check(len(rows) == len(orig_rows) + 7 + 5 + 5 + len(props), f'Zeilen: {len(orig_rows)} Original + 17 EO + {len(props)} neu = {len(rows)}')
    btn = [r for r in rows if r.getAttribute('SelectedIndex') == '{Binding EoRigButton1}'][0]
    check(btn.getAttribute('ItemsSource') == '{Binding FunctionChoices}', 'Tasten-Zeilen nutzen FunctionChoices')

# old build output must be a prefix-compatible subset: removing the new parts gives the old XAML
video_in, video = extend(VIDEO)
check(b'EoRig' not in video and len(video) > len(video_in), 'Video-Seite unverändert (keine neuen Zeilen dort)')

# ---- properties
print('Menü-Eigenschaften (Getter/Setter)')
base = e.call('Fh2TestRigPropertyBase')
out = e.alloc(8)
def get(i):
    e.wq(out, 0)
    e.call('Fh2TestVideoGet', base + i, out)
    b = e.rq(out)
    kind = e.rq(b)
    return e.rd(b + 0x28) if kind == 0x1111 else round(e.rf(b + 0x28), 4)

def set_box(i, value, as_float):
    b = e.alloc(0x30)
    if as_float:
        e.wf(b + 0x28, value)
    else:
        e.wd(b + 0x28, value)
    changed.clear()
    e.call('Fh2TestVideoSet', base + i, 0x7777, b)
    return len(changed) >= 1

expect = [1, 1, 0.75, 0.5, 0.06, 0.04, 35.0, 0.02, 0.3, 0.2, 450.0, 3.0, 180.0, 0, 2, 1, 0, 0, 0, 5, 0]
got = [get(i) for i in range(21)]
check(got == expect, f'Startwerte aus deinen INIs (FFB-Art Spiel neu): {got}')
check(get(PROFILE) == 3, f'Profil beim Start: Lenkrad erkannt ({get(PROFILE)})')
kb = [get(i) for i in range(KEY_ENABLED, KEY_PEDAL + 1)]
check(kb == [1, 150.0, 90.0, 60.0], f'Tastatur-Startwerte (FH2_Keyboard.ini neu angelegt): {kb}')

ok = set_box(2, 0.85, True); check(ok and get(2) == 0.85, 'FFB-Stärke 75% -> 85%')
ok = set_box(2, 0.85, True); check(not ok, 'gleicher Wert -> keine Änderung gemeldet')
ok = set_box(3, 1.5, True); check(ok and get(3) == 1.5, 'Vibration -> 150%')
ok = set_box(11, 2.9999998, True); check(not ok or get(11) == 3.0, 'Kurve 2.9999998 wird zu 3.0 gerundet (keine Änderung)')
ok = set_box(11, 3.1, True); check(ok and get(11) == 3.1, 'Kurve 3.0 -> 3.1')
ok = set_box(10, 440.0, True); check(ok and get(10) == 440.0, 'Anschlag 450 -> 440')
ok = set_box(12, 210.0, True); check(ok and get(12) == 210.0, 'Drehwinkel 180 -> 210')
ok = set_box(6, 40.0, True); check(ok and get(6) == 40.0, 'Glättung 35 -> 40 ms')
ok = set_box(13, 1, False); check(ok and get(13) == 1, 'H-Schaltung aus -> an')
ok = set_box(LEVEL, 3, False); check(ok and get(LEVEL) == 3, 'Verkehr 5 -> 3')
ok = set_box(ALLDAY, 1, False); check(ok and get(ALLDAY) == 1, 'Rushhour aus -> an')
ok = set_box(LEVEL, 6, False); check(not ok and get(LEVEL) == 3, 'Verkehr 6 (ungültig) abgelehnt')
ok = set_box(FFB_MODE, 1, False); check(ok and get(FFB_MODE) == 1, 'FFB-Art Spiel -> Eigenes')
ok = set_box(FFB_MODE, 2, False); check(not ok and get(FFB_MODE) == 1, 'FFB-Art 2 (gibt es nicht) abgelehnt')
ok = set_box(KEY_STEER, 200.0, True); check(ok and get(KEY_STEER) == 200.0, 'Tastatur-Lenkzeit 150 -> 200 ms')
ok = set_box(KEY_PEDAL, 5000.0, True); check(not ok and get(KEY_PEDAL) == 60.0, 'Pedalzeit 5000 (zu groß) abgelehnt')
ok = set_box(2, 2.5, True); check(not ok and get(2) == 0.85, 'FFB-Stärke 250% (außerhalb) abgelehnt')
ok = set_box(2, float('nan'), True); check(not ok and get(2) == 0.85, 'NaN abgelehnt')
ok = set_box(10, 20.0, True); check(not ok and get(10) == 440.0, 'Anschlag 20 (unter 30) abgelehnt')

# ---- live into the wheel bridge
print('Lenkrad live')
e.call('Fh2TestWheelLiveApply')
cfg = {i: e.call('Fh2TestWheelConfig', i) & 0xffffffff for i in range(18)}
check(cfg[2] == 85 and cfg[3] == 150 and cfg[11] == 31 and cfg[10] == 440 and cfg[12] == 210 and cfg[6] == 40 and cfg[13] == 1,
      f'Brücke übernimmt Werte: Stärke {cfg[2]}, Vibration {cfg[3]}, Kurve {cfg[11]}, Anschlag {cfg[10]}, Drehwinkel {cfg[12]}, Glättung {cfg[6]}, Schaltknauf {cfg[13]}')
check(cfg[FFB_MODE] == 1, f'Brücke übernimmt FFB-Art {cfg[FFB_MODE]}')
c_mid = e.call('Fh2TestWheelCurve', 32768 + 3277) & 0xffffffff
ok = set_box(11, 1.0, True)
e.call('Fh2TestWheelLiveApply')
c_lin = e.call('Fh2TestWheelCurve', 32768 + 3277) & 0xffffffff
check(c_lin == 32768 + 3277 and c_mid > c_lin + 2000, f'Lenkkurve wird neu berechnet: 10% Einschlag -> Kurve 3.1: {c_mid - 32768}, linear: {c_lin - 32768}')
set_box(11, 3.1, True)
e.call('Fh2TestWheelLiveApply')

# ---- clutch pedal as brake (idea: Lancaster)
print('Kupplung als Bremse')
pedals = lambda: e.call('Fh2TestWheelPedals', 65535, 32768)
v = pedals()
check((v >> 16) & 0xffff == 1023 and v & 0xffff in (511, 512) and v >> 32 == 10, f'Standard: Bremse voll, Kupplung halb, Kupplung vorhanden ({v:#x})')
ok = set_box(CLUTCH_BRAKE, 1, False); check(ok and get(CLUTCH_BRAKE) == 1, 'Kupplung als Bremse: an')
e.call('Fh2TestWheelLiveApply')
v = pedals()
check((v >> 16) & 0xffff in (511, 512) and v & 0xffff == 0 and v >> 32 == 0, f'an: das Kupplungspedal bremst, keine Kupplung mehr ({v:#x})')
ok = set_box(CLUTCH_BRAKE, 2, False); check(not ok and get(CLUTCH_BRAKE) == 1, 'Wert 2 abgelehnt')
set_box(CLUTCH_BRAKE, 0, False)
e.call('Fh2TestWheelLiveApply')
check((pedals() >> 16) & 0xffff == 1023, 'aus: wieder Standard')
set_box(CLUTCH_BRAKE, 1, False)
e.call('Fh2TestWheelLiveApply')

# ---- traffic live
print('Verkehr live')
r = e.call('Fh2TestTrafficApply')
vals = [tunable(rva, vt) for rva, vt, _, _ in TUNABLES]
check(vals == [48.0, 48.0, 26.0, 18.0, 5.0, 16], f'Stufe 3 im Spielspeicher: {vals}')
times = [tunable(rva) for rva, _, _ in TIMES]
check(times == [0.5, 23.5, 23.5, 0.5], f'Rushhour ganztägig: {times}')
set_box(ALLDAY, 0, False); e.call('Fh2TestTrafficApply')
times = [tunable(rva) for rva, _, _ in TIMES]
check(times == [7.0, 19.0, 23.0, 5.0], f'Rushhour aus -> Originalzeiten zurück: {times}')
set_box(LEVEL, 0, False); e.call('Fh2TestTrafficApply')
vals = [tunable(rva, vt) for rva, vt, _, _ in TUNABLES]
check(vals == [14.0, 16.0, 10.0, 7.0, 8.0, 5], f'Stufe 0 -> Originalwerte zurück: {vals}')
set_box(LEVEL, 5, False); e.call('Fh2TestTrafficApply')
vals = [tunable(rva, vt) for rva, vt, _, _ in TUNABLES]
check(vals == [100.0, 100.0, 50.0, 40.0, 3.0, 30], f'Stufe 5 wieder: {vals}')
set_box(LEVEL, 3, False)

# ---- saving
print('Speichern')
e.tick += 100
e.call('Fh2TestRigStep', e.tick)
check(open(os.path.join(root, 'FH2_Wheel.ini'), 'rb').read() == original_wheel, 'nicht sofort gespeichert (wartet 0,6 s nach der letzten Änderung)')
e.tick += 700
e.call('Fh2TestRigStep', e.tick)
wheel = open(os.path.join(root, 'FH2_Wheel.ini'), 'rb').read()
eo = open(os.path.join(root, 'FH2_ExtraOptions.ini'), 'rb').read()
import difflib
def diff(a, b):
    return [l for l in difflib.unified_diff(a.decode('latin1').splitlines(), b.decode('latin1').splitlines(), lineterm='', n=0) if l[:1] in '+-' and l[:3] not in ('+++', '---')]
dw, de = diff(original_wheel, wheel), diff(original_eo, eo)
print('   FH2_Wheel.ini:', dw)
print('   FH2_ExtraOptions.ini:', de)
check(sorted(dw) == sorted(['-Rotation=180', '+Rotation=210', '-Shifter=0', '+Shifter=1', '-Lock=450', '+Lock=440',
                           '-FFBStrength=75', '+FFBStrength=85', '-Rumble=50', '+Rumble=150', '-Smoothing=35', '+Smoothing=40',
                           '-Curve=30', '+Curve=31', '+FFBMode=1', '-ClutchAsBrake=0', '+ClutchAsBrake=1']), 'Lenkrad-INI: nur die geänderten Zeilen (+ neue Schlüssel)')
kbini = open(os.path.join(root, 'FH2_Keyboard.ini'), 'rb').read()
check(b'SteerTime=200' in kbini and b'Mouse=' not in kbini and b'Throttle=W' in kbini, 'Tastatur-INI: Werte gespeichert, Belegung erhalten')
check(sorted(de) == sorted(['-TrafficLevel=5', '+TrafficLevel=3']), 'Extra-Options-INI: nur TrafficLevel (Rushhour wieder 0 = unverändert)')
check(len(eo) <= 8192, f'Extra-Options-INI bleibt unter 8192 Bytes ({len(eo)})')
check(not any(n.endswith('.eotmp') for n in os.listdir(root)), 'keine Temp-Dateien übrig')

# EO must still accept its INI
from shutil import copy
r = e.call('Fh2TestReadOptions', directory, e.alloc(400))
check(r == 1, 'Extra Options akzeptiert die geänderte INI (Parser von EO selbst)')

# ---- launcher changes the INI while the game runs
print('Launcher ändert INI während das Spiel läuft')
p = os.path.join(root, 'FH2_Wheel.ini')
text = open(p, 'rb').read().replace(b'Shifter=1', b'Shifter=0').replace(b'FFBStrength=85', b'FFBStrength=60')
open(p, 'wb').write(text); e.touch(p)
pe = os.path.join(root, 'FH2_ExtraOptions.ini')
eo_text = open(pe, 'rb').read().replace(b'RushHourAllDay=0', b'RushHourAllDay=1')
open(pe, 'wb').write(eo_text); e.touch(pe)
e.tick += 1500
e.call('Fh2TestRigStep', e.tick)
check(get(13) == 0 and get(2) == 0.6 and get(ALLDAY) == 1, f'neue Werte übernommen: H-Schaltung {get(13)}, Stärke {get(2)}, Rushhour {get(ALLDAY)}')
check(open(p, 'rb').read() == text, 'dabei nichts zurückgeschrieben')

# ---- profiles
print('Eingabe-Profile')
set_box(PROFILE, 3, False)
ok = set_box(PROFILE, 4, False)
check(ok and get(ENABLED) == 1 and get(KEY_ENABLED) == 1 and get(CLUTCH_BRAKE) == 1 and get(PROFILE) == 4, 'Lenkrad (Kupplung = Bremse): Lenkrad an, Tastatur an, Kupplung als Bremse an')
ok = set_box(PROFILE, 3, False)
check(ok and get(CLUTCH_BRAKE) == 0 and get(ENABLED) == 1 and get(PROFILE) == 3, 'Lenkrad: Kupplung als Bremse wieder aus')
ok = set_box(PROFILE, 2, False)
check(ok and get(ENABLED) == 0 and get(KEY_ENABLED) == 1 and get(PROFILE) == 2, 'Tastatur: Lenkrad aus, Tastatur an')
ok = set_box(PROFILE, 1, False)
check(ok and get(ENABLED) == 0 and get(KEY_ENABLED) == 0 and get(PROFILE) == 1, 'Controller: Lenkrad und Tastatur aus')
ok = set_box(PROFILE, 0, False); check(not ok and get(PROFILE) == 1, 'Eigene Werte wählen ändert nichts')
ok = set_box(PROFILE, 1, False); check(not ok, 'schon aktives Profil: keine Änderung')
ok = set_box(KEY_ENABLED, 1, False)
check(ok and get(PROFILE) == 2, 'Tastatur von Hand an: Profil zeigt Tastatur')
ok = set_box(CLUTCH_BRAKE, 1, False)
check(ok and get(PROFILE) == 2, 'Kupplung als Bremse spielt bei Tastatur/Controller keine Rolle')
ok = set_box(ENABLED, 1, False)
check(ok and get(PROFILE) == 4, 'Lenkrad von Hand an (Tastatur an, Kupplung Bremse an): Profil zeigt Lenkrad (Kupplung = Bremse)')
ok = set_box(PROFILE, 5, False); check(not ok, 'Profil 5 (gibt es nicht) abgelehnt')
e.tick += 700
e.call('Fh2TestRigStep', e.tick)
w2 = open(p, 'rb').read()
check(b'EoRigProfile' not in w2 and b'Profil' not in w2, 'Profil selbst wird nicht in die INI geschrieben')

# ---- Verkehrs-Voreinstellung
print('Verkehrs-Voreinstellung')
ok = set_box(PRESET, 5, False)
check(ok and get(LEVEL) == 5 and get(ALLDAY) == 1 and get(PRESET) == 5, f'Chaos: Menge 5 + Rushhour an: {get(LEVEL)}, {get(ALLDAY)}, {get(PRESET)}')
ok = set_box(PRESET, 2, False)
check(ok and get(LEVEL) == 1 and get(ALLDAY) == 0 and get(PRESET) == 2, f'Ruhig: Menge 1, Rushhour aus: {get(LEVEL)}, {get(ALLDAY)}, {get(PRESET)}')
ok = set_box(PRESET, 0, False); check(not ok and get(LEVEL) == 1, 'Eigene Werte wählen ändert nichts')
ok = set_box(LEVEL, 2, False)
check(ok and get(PRESET) == 0, 'Menge von Hand geändert: Voreinstellung zeigt Eigene Werte')
ok = set_box(PRESET, 6, False); check(not ok, 'Voreinstellung 6 (gibt es nicht) abgelehnt')
ok = set_box(PRESET, 1, False)
check(ok and get(LEVEL) == 0 and get(ALLDAY) == 0 and get(PRESET) == 1, 'Original: Menge 0, Rushhour aus')
e.tick += 700
e.call('Fh2TestRigStep', e.tick)
pe_now = open(pe, 'rb').read()
check(b'Voreinstellung' not in pe_now and b'EoRigDensity' not in pe_now, 'Voreinstellung selbst wird nicht in die INI geschrieben')
check(b'TrafficLevel=0' in pe_now, 'Menge aus der Voreinstellung gespeichert')

# ---- Wetter
print('Wetter')
check(get(WEATHER) == 0, f'Wetter beim Start: Original ({get(WEATHER)})')
ok = set_box(WEATHER, 4, False)
check(ok and get(WEATHER) == 4, f'Wetter Regen gewählt: {get(WEATHER)}')
ok = set_box(WEATHER, 6, False); check(not ok and get(WEATHER) == 4, 'Wetter 6 (gibt es nicht) abgelehnt')
e.tick += 700
e.call('Fh2TestRigStep', e.tick)
pe_now = open(pe, 'rb').read()
check(b'Weather=4' in pe_now and b'Wetter=' not in pe_now, 'Wetter in FH2_ExtraOptions.ini gespeichert ([Traffic] Weather=4)')
r = e.call('Fh2TestReadOptions', directory, e.alloc(400))
check(r == 1, 'Extra Options akzeptiert die INI mit Wetter-Zeile')
ok = set_box(WEATHER, 0, False)
e.tick += 700
e.call('Fh2TestRigStep', e.tick)
check(ok and b'Weather=0' in open(pe, 'rb').read(), 'Wetter zurück auf Original gespeichert')
check(e.call('Fh2TestWeatherManager') == 0, 'ohne Spielwelt: kein WeatherManager, nichts passiert')

# ---- English
print('Englisch')
set_language('EN')
_, hud_en = extend(HUD)
check(b'Wheel: Force Feedback Strength' in hud_en and b'Traffic: Amount' in hud_en and b'Keyboard: Keyboard' in hud_en and b'Mouse' not in hud_en and b'Wheel: RSB Button' in hud_en and b'H-Pattern' not in hud_en and b'Cockpit Wheel' not in hud_en, 'englische Texte bei englischem Spiel')
check('Nebel nur auf der Sturm-Insel, sonst sonnig'.encode() in hud and 'Neustart von Azimuth'.encode() in hud, 'korrigierte Texte: Nebel sonst sonnig, Verkehr braucht Azimuth-Neustart')
xml.dom.minidom.parseString(hud_en)
print()
print('FEHLER:' if fails else 'ALLE TESTS OK', len(fails) or '')
sys.exit(1 if fails else 0)
