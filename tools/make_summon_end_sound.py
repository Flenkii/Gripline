"""Write assets/sounds/summon_end.wav: Gripline's own "autopilot off" chime (V7.1 summon, Marvin 08.11.2026
chose a self-made tone over the unlicensed recording). The same kind of tone as the autopilot "on" sound, going down, made from sine waves.
Usage: python tools/make_summon_end_sound.py  (then python tools/make_summon_sounds.py)"""
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
RATE = 32000
LENGTH = 1.0  # s

# The autopilot "on" sound (assets/sounds/summon_start.wav, measured): two almost pure sine notes going up,
# E4 (329.6 Hz) then F#4 (370 Hz) 0.25 s later, each with a quick attack and a decay of about 0.15 s.
# "Off" here: the same kind of tone going down, F#4 then E4 (Marvin 08.11.2026: "like the on sound").


def note(freq, start, t, tau):
    x = t - start
    on = x >= 0
    x = np.where(on, x, 0.0)
    env = np.where(on, (1 - np.exp(-x / 0.008)) * np.exp(-x / tau), 0.0)
    return env * (np.sin(2 * np.pi * freq * x) + 0.03 * np.sin(2 * np.pi * 2 * freq * x))


t = np.arange(int(RATE * LENGTH)) / RATE
signal = note(370.0, 0.0, t, 0.15) + note(329.63, 0.25, t, 0.17)
fade = np.ones_like(signal)
n = int(RATE * 0.1)
fade[-n:] = np.linspace(1, 0, n)
signal *= fade
signal *= 10 ** (-1.0 / 20) / np.max(np.abs(signal))  # peak -1 dB, like the start sound
pcm = (signal * 32767).astype('<i2').tobytes()
with wave.open(str(ROOT / 'assets' / 'sounds' / 'summon_end.wav'), 'wb') as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(pcm)
