"""V7.1 calm Drivatars (features/world_files.c): AI/Difficulty.xml gets the calmest cruise style for every
open-world Drivatar (CruiseMinimum chance 1, Medium/High 0) and CruiseReaction off; nothing else changes."""
import sys, tempfile
from emu import Emu

DLL = sys.argv[1] if len(sys.argv) > 1 else '../../gripline-plugin/fh2-mods/Gripline.dll'
fails, checks = [], [0]


def check(cond, text):
    checks[0] += 1
    print(('  ok   ' if cond else '  FAIL ') + text)
    if not cond:
        fails.append(text)


# the shape of the game file (values like the original)
DIFFICULTY = '''<?xml version="1.0"?>
<DifficultySettings>
    <Global>
        <CruiseMinimum>
            <Chance value="0.6" />
            <BrakeModifier value="0.7" />
            <TorqueModifier value="0.1" />
        </CruiseMinimum>
        <CruiseMedium>
            <Chance value="0.2" />
            <TorqueModifier value="0.2" />
        </CruiseMedium>
        <CruiseHigh>
            <Chance value="0.2" />
            <TorqueModifier value="0.4" />
        </CruiseHigh>
        <CruiseReaction>
            <ActivationDistance value="15" />
            <ReactionTime value="4" />
            <Enabled value="true" />
        </CruiseReaction>
        <SurfaceEffect>
            <MinFriction value="0.8" />
        </SurfaceEffect>
    </Global>
    <LevelSettings>
        <Level>
            <Difficulty name="Easy" />
            <SlowDown>
                <Enabled value="true" />
            </SlowDown>
        </Level>
    </LevelSettings>
    <HeroDrive>
      <SlowDownSpeedLimit>
        <Enabled value="true" />
      </SlowDownSpeedLimit>
    </HeroDrive>
</DifficultySettings>
'''

e = Emu(DLL, tempfile.mkdtemp(prefix='gl-world-'))


def calm(text):
    data = text.encode()
    src, out = e.put_bytes(data), e.alloc(len(data) + 256)
    n = e.call('Fh2TestCalmDrivatars', src, len(data), out, len(data) + 256)
    n = n - (1 << 32) if n >= 1 << 31 else n
    return None if n < 0 else bytes(e.mu.mem_read(out, n)).decode()


r = calm(DIFFICULTY)
expected = (DIFFICULTY.replace('<Chance value="0.6" />', '<Chance value="1.0" />')
            .replace('''<CruiseMedium>
            <Chance value="0.2" />''', '''<CruiseMedium>
            <Chance value="0.0" />''')
            .replace('''<CruiseHigh>
            <Chance value="0.2" />''', '''<CruiseHigh>
            <Chance value="0.0" />''')
            .replace('''<ReactionTime value="4" />
            <Enabled value="true" />''', '''<ReactionTime value="4" />
            <Enabled value="false" />'''))
check(r == expected, 'Global: Min 1.0, Medium/High 0.0, Reaktion aus, sonst Byte fuer Byte gleich')
check(r is not None and r.count('<Enabled value="true" />') == 2, 'Rennen/HeroDrive behalten Enabled=true')
check(calm(DIFFICULTY.replace('<CruiseReaction>', '<CruiseX>').replace('</CruiseReaction>', '</CruiseX>')) is None,
      'unbekannter Aufbau: keine Datei (Spiel behaelt Original)')
check(calm('<DifficultySettings></DifficultySettings>') is None, 'ohne <Global>: keine Datei')

print(f'\n{checks[0]} checks, {len(fails)} failed')
sys.exit(1 if fails else 0)
