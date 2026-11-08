"""Write Gripline's own summon tones into assets/sounds (V7.1, Marvin 08.11.2026: self-made, no outside rights):
summon_start.wav (autopilot on), summon_end.wav (autopilot off), summon_warning.wav (a summon problem).
All made from sine waves in the style of the car autopilot chimes: almost pure tones, quick attack, short decay.
Usage: python tools/make_summon_tones.py  (then python tools/make_summon_sounds.py)"""
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
RATE = 32000
E4, FS4, A4, CS5 = 329.63, 370.0, 440.0, 554.37


def note(t, freq, start, tau, attack=0.008):
    x = t - start
    on = x >= 0
    x = np.where(on, x, 0.0)
    env = np.where(on, (1 - np.exp(-x / attack)) * np.exp(-x / tau), 0.0)
    return env * (np.sin(2 * np.pi * freq * x) + 0.03 * np.sin(2 * np.pi * 2 * freq * x))


def write(name, length, notes):
    t = np.arange(int(RATE * length)) / RATE
    signal = sum(note(t, *n) for n in notes)
    n = int(RATE * 0.1)
    signal[-n:] *= np.linspace(1, 0, n)
    signal *= 10 ** (-1.0 / 20) / np.max(np.abs(signal))  # peak -1 dB
    with wave.open(str(ROOT / 'assets' / 'sounds' / f'{name}.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((signal * 32767).astype('<i2').tobytes())


# on: going up, E4 then F#4 (Marvin: the off chime "is perfect", the on chime the same way)
write('summon_start', 1.0, [(E4, 0.0, 0.15), (FS4, 0.25, 0.17)])
# off: going down, F#4 then E4
write('summon_end', 1.0, [(FS4, 0.0, 0.15), (E4, 0.25, 0.17)])
# warning: the same tone, higher and three times quick (A4 + C#5 together, a brighter "attention")
write('summon_warning', 0.9, [(f, s, 0.07) for s in (0.0, 0.14, 0.28) for f in (A4, CS5)])
