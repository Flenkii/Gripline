"""Write features/door_sounds.h from assets/sounds/door_open.wav and door_close.wav (walking fade, V7).
Usage: python tools/make_door_sounds.py"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def array(name, data):
    lines = [f"static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 32):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 32]) + ",")
    lines.append("};")
    return "\n".join(lines)


parts = ["/* Door sounds for walking (get out / get in), whole WAV files, from assets/sounds (tools/make_door_sounds.py) */"]
for name in ("door_open", "door_close"):
    parts.append(array(f"{name}_wav", (ROOT / "assets" / "sounds" / f"{name}.wav").read_bytes()))
(ROOT / "features" / "door_sounds.h").write_text("\n".join(parts) + "\n")
