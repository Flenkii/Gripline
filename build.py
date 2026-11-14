"""Build Gripline (fh2-mods/Gripline.dll, an FH2 Mod Platform mod) with Zig 0.15.2, on Windows or Linux.

    python build.py /path/to/zig [--test] [--output FOLDER] [--build-dir FOLDER] [--jobs N] [--rebuild]

Objects are cached: a source is compiled again only when its command line, itself, any header it
includes or the compiler changed. --test adds the Fh2Test* exports the emulator tests call.
"""

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# every .c file that goes into the DLL
SOURCES = [
    "support.c", "logging.c", "settings.c", "plugin/gripline_plugin.c",
    # world and traffic
    "features/game_language.c", "features/game_units.c", "features/traffic_tunables.c",
    "features/traffic_files.c", "features/world_files.c",
    # driving tools
    "features/telemetry.c", "features/logbook.c", "features/records.c", "features/cruise.c",
    "features/trip.c", "features/noclip.c", "features/no_damage.c", "features/drive_look.c",
    # walking
    "features/walking.c", "features/summon.c", "features/walk_gauge.c",
    # cockpit, wheel and keyboard
    "features/cockpit.c", "features/cockpit_view.c", "features/wheel_buttons.c",
    "features/wheel_watch.c", "features/keyboard_pad.c", "features/nitro_link.c",
    # menu, cards and housekeeping
    "features/key_menu.c", "features/menu_mouse.c", "features/focus_guard.c", "features/notify.c",
    "features/rig_settings.c", "features/ini_edit.c", "features/ini_migrate.c",
    "features/mod_scan.c", "features/mod_partners.c", "features/anna_link.c", "features/version_check.c", "features/update_check.c", "features/report.c",
]


def read_arguments():
    parser = argparse.ArgumentParser(description="Build Gripline.dll")
    parser.add_argument("zig", type=Path, help="path of the zig executable")
    parser.add_argument("--test", action="store_true", help="add the Fh2Test* exports for tests/")
    parser.add_argument("--plugin", action="store_true", help="accepted for old scripts, always on")
    parser.add_argument("--output", type=Path, help="folder that receives fh2-mods/Gripline.dll")
    parser.add_argument("--build-dir", type=Path, help="folder for objects and libraries")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--rebuild", action="store_true", help="ignore cached objects")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    return args


def release_info():
    """(version text like 'V7.1', major, minor, patch) from gripline.h"""
    header = (ROOT / "gripline.h").read_text(encoding="utf-8")
    text = re.search(r'#define\s+GRIPLINE_VERSION\s+"([^"]+)"', header).group(1)
    number = lambda name: int(re.search(rf"#define\s+GRIPLINE_RELEASE_{name}\s+(\d+)", header).group(1))
    return text, number("MAJOR"), number("MINOR"), number("PATCH")


def compile_flags(zig, test):
    include = zig.parent / "lib" / "include"
    windows_libc = zig.parent / "lib" / "libc" / "include" / "any-windows-any"
    flags = [str(zig), "clang", "-target", "x86_64-w64-windows-gnu", "-c",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fms-extensions", "-funwind-tables",
             "-O2", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT), "-isystem", str(include), "-isystem", str(windows_libc),
             "-DFH2_AZIMUTH=1", "-DFH2_GRIPLINE_PLUGIN=1"]
    if test:
        flags.append("-DFH2_TWEAKS_TEST=1")
    return flags


# ---------------------------------------------------------------- the version resource

def version_resource(file_name):
    """A .res with the DLL's version info (Explorer > Properties > Details)."""
    text, major, minor, patch = release_info()

    def pad4(data):
        return data + bytes(-len(data) % 4)

    def utf16(value):
        return (value + "\0").encode("utf-16le")

    def node(key, value=b"", children=b"", is_text=False):
        head = pad4(struct.pack("<HHH", 0, len(value) // 2 if is_text else len(value), int(is_text)) + utf16(key))
        body = head + value
        if children:
            body = pad4(body) + children
        return struct.pack("<H", len(body)) + body[2:]

    dotted = f"{major}.{minor}.{patch}"
    fields = {
        "FileDescription": f"Gripline {text} - FH2 Mod Platform mod",
        "FileVersion": dotted,
        "InternalName": "Gripline",
        "OriginalFilename": file_name,
        "ProductName": "Gripline",
        "ProductVersion": text,
        "LegalCopyright": "Flenkii, GPL-3.0 (started as a fork of Extra Options by RokkuDayo)",
    }
    rows = b"".join(pad4(node(k, utf16(v), is_text=True)) for k, v in fields.items())
    strings = node("StringFileInfo", children=node("040904b0", children=rows))
    translation = node("VarFileInfo", children=node("Translation", struct.pack("<HH", 0x409, 1200)))
    ms, ls = major << 16 | minor, patch << 16
    fixed = struct.pack("<13I", 0xfeef04bd, 0x10000, ms, ls, ms, ls, 0x3f, 0, 0x40004, 2, 0, 0, 0)
    info = node("VS_VERSION_INFO", fixed, pad4(strings) + translation)
    empty = struct.pack("<IIHHHHIHHII", 0, 32, 0xffff, 0, 0xffff, 0, 0, 0, 0, 0, 0)
    entry = struct.pack("<IIHHHHIHHII", len(info), 32, 0xffff, 16, 0xffff, 1, 0, 0x30, 0x409, 0, 0)
    return empty + entry + pad4(info)


# ---------------------------------------------------------------- object cache

def includes_of(source, found=None):
    """the project headers a file pulls in, directly or not"""
    found = set() if found is None else found
    if source in found:
        return found
    found.add(source)
    for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', source.read_text(encoding="utf-8"), re.M):
        header = source.parent / name
        if not header.is_file():
            header = ROOT / name
        includes_of(header.resolve(), found)
    return found


def fingerprint(command, files, zig):
    h = hashlib.sha256(json.dumps(command).encode())
    for path in sorted(files):
        h.update(str(path).encode())
        h.update(path.read_bytes())
    info = zig.stat()
    h.update(f"{zig}:{info.st_size}:{info.st_mtime_ns}".encode())
    return h.hexdigest()


def run_cached(command, files, target, zig, force):
    """run command unless target is still current; True when it ran"""
    stamp = target.with_suffix(target.suffix + ".inputs")
    wanted = fingerprint(command, files, zig)
    if not force and target.is_file() and stamp.is_file() and stamp.read_text() == wanted:
        return False
    stamp.unlink(missing_ok=True)
    subprocess.run(command, check=True)
    stamp.write_text(wanted)
    return True


# ---------------------------------------------------------------- the build

def import_library(zig, definition, build_dir, force):
    library = build_dir / (Path(definition).stem + ".lib")
    source = ROOT / definition
    run_cached([str(zig), "dlltool", "-m", "i386:x86-64", "-d", str(source), "-l", str(library)],
               [source], library, zig, force)
    return library


def build(zig, output, build_dir, args):
    started = time.monotonic()
    build_dir.mkdir(parents=True, exist_ok=True)
    flags = compile_flags(zig, args.test)

    def compile_one(name):
        obj = build_dir / (Path(name).stem + ".obj")
        ran = run_cached([*flags, str(ROOT / name), "-o", str(obj)], includes_of(ROOT / name), obj, zig, args.rebuild)
        return obj, ran

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        objects = list(pool.map(compile_one, SOURCES))

    libraries = [import_library(zig, d, build_dir, args.rebuild) for d in ("kernel32.def", "ntdll.def")]
    resource = build_dir / "version.res"
    resource.write_bytes(version_resource("Gripline.dll"))

    mods = output / "fh2-mods"
    mods.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(zig), "lld-link", "/dll", "/entry:DllMain", "/machine:x64", "/nodefaultlib",
                    "/dynamicbase", "/nxcompat", "/Brepro",
                    f"/out:{mods / 'Gripline.dll'}", f"/implib:{build_dir / 'gripline.lib'}",
                    *[str(o) for o, _ in objects], *[str(lib) for lib in libraries], str(resource)], check=True)
    fresh = sum(ran for _, ran in objects)
    print(f"Compiled {fresh}/{len(SOURCES)} objects; {time.monotonic() - started:.1f}s", flush=True)


def main():
    args = read_arguments()
    zig = args.zig.resolve()
    output = (args.output or ROOT.parent / "gripline-plugin").resolve()
    build_dir = args.build_dir or ROOT.parent / ".build" / ("plugin-test" if args.test else "plugin")
    build(zig, output, build_dir.resolve(), args)
    print(output)


if __name__ == "__main__":
    main()
