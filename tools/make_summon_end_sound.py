"""Write assets/sounds/summon_end.wav: Gripline's own "autopilot off" chime (V7.1 summon, Marvin 08.11.2026
chose a self-made tone over the unlicensed recording). Two soft bell notes going down, made from sine waves.
Usage: python tools/make_summon_end_sound.py  (then python tools/make_summon_sounds.py)"""
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
RATE = 32000
LENGTH = 1.25  # s


def bell(freq, start, t, tau=0.32):
    """a soft electric-piano like note: fundamental, a little octave and fifth, quick attack, long decay"""
    x = t - start
    on = x >= 0
    x = np.where(on, x, 0.0)
    env = np.where(on, (1 - np.exp(-x / 0.006)) * np.exp(-x / tau), 0.0)
    tone = np.sin(2 * np.pi * freq * x) + 0.28 * np.sin(2 * np.pi * 2 * freq * x) * np.exp(-x / 0.12) \
        + 0.08 * np.sin(2 * np.pi * 3 * freq * x) * np.exp(-x / 0.06)
    return env * tone


t = np.arange(int(RATE * LENGTH)) / RATE
signal = bell(1318.5, 0.0, t) * 0.9 + bell(880.0, 0.17, t, tau=0.38)  # E6 down to A5
echo = np.zeros_like(signal)
d = int(RATE * 0.09)
echo[d:] = signal[:-d] * 0.22  # a little room
signal = signal + echo
fade = np.ones_like(signal)
n = int(RATE * 0.15)
fade[-n:] = np.linspace(1, 0, n)
signal *= fade
signal *= 10 ** (-1.0 / 20) / np.max(np.abs(signal))  # peak -1 dB, like the start sound
pcm = (signal * 32767).astype('<i2').tobytes()
with wave.open(str(ROOT / 'assets' / 'sounds' / 'summon_end.wav'), 'wb') as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(pcm)
