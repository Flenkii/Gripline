"""Write features/summon_sounds.h from assets/sounds/summon_start.wav and summon_end.wav (V7.1 summon).
Usage: python tools/make_summon_sounds.py"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def array(name, data):
    lines = [f"static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 32):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 32]) + ",")
    lines.append("};")
    return "\n".join(lines)


parts = ["/* Summon sounds (car called / summon over), whole WAV files, from assets/sounds (tools/make_summon_sounds.py) */"]
for name in ("summon_start", "summon_end"):
    parts.append(array(f"{name}_wav", (ROOT / "assets" / "sounds" / f"{name}.wav").read_bytes()))
(ROOT / "features" / "summon_sounds.h").write_text("\n".join(parts) + "\n")
