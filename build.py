"""Build Gripline (fh2-mods/Gripline.dll, an FH2 Mod Platform mod) with Zig 0.15.2, also on Linux.
Usage: python build.py /path/to/zig [--test] [--output FOLDER]
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re
import struct
import time

SOURCE = Path(__file__).resolve().parent
PLUGIN_SOURCES = [
    "support.c",
    "logging.c",
    "settings.c",
    "sha256.c",
    "plugin/gripline_plugin.c",
    "features/game_language.c",
    "features/traffic_tunables.c",
    "features/traffic_files.c",
    "features/ini_edit.c",
    "features/ini_migrate.c",
    "features/rig_settings.c",
    "features/wheel_buttons.c",
    "features/keyboard_pad.c",
    "features/mod_scan.c",
    "features/report.c",
    "features/cockpit_view.c",
    "features/nitro_link.c",
    "features/notify.c",
    "features/cockpit.c",
]


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zig", type=Path)
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--plugin", action="store_true", help="Accepted for old scripts; always on")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--rebuild", action="store_true", help="Ignore cached build inputs")
    arguments = parser.parse_args()
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    return arguments


def compiler_arguments(compiler, options):
    arguments = [
        str(compiler),
        "clang",
        "-target",
        "x86_64-w64-windows-gnu",
        "-c",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fms-extensions",
        "-funwind-tables",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(SOURCE),
        "-isystem",
        str(compiler.parent / "lib/include"),
        "-isystem",
        str(compiler.parent / "lib/libc/include/any-windows-any"),
    ]
    if options.test:
        arguments.append("-DFH2_TWEAKS_TEST=1")
    arguments.append("-DFH2_AZIMUTH=1")
    arguments.append("-DFH2_GRIPLINE_PLUGIN=1")
    return arguments


def version_resource(filename):
    version = "6.0.0"
    numbers = [int(part) for part in version.split(".")]
    numbers += [0] * (4 - len(numbers))

    def padded(data):
        return data + bytes(-len(data) % 4)

    def wide(text):
        return (text + "\0").encode("utf-16le")

    def block(key, value=b"", children=b"", text=False):
        header = padded(struct.pack("<HHH", 0, len(value) // 2 if text else len(value), int(text)) + wide(key))
        data = header + value
        if children:
            data = padded(data) + children
        return struct.pack("<H", len(data)) + data[2:]

    strings = {"FileDescription": "Gripline V6 - FH2 Mod Platform mod",
               "FileVersion": version, "InternalName": "Gripline",
               "OriginalFilename": filename, "ProductName": "Gripline",
               "ProductVersion": "V6",
               "LegalCopyright": "Flenkii, GPL-3.0; based on Extra Options by Arthur (RokkuDayo)"}
    entries = b"".join(padded(block(key, wide(value), text=True)) for key, value in strings.items())
    string_info = block("StringFileInfo", children=block("040904b0", children=entries))
    variable_info = block("VarFileInfo", children=block("Translation", struct.pack("<HH", 0x409, 1200)))
    high, low = numbers[0] << 16 | numbers[1], numbers[2] << 16 | numbers[3]
    fixed = struct.pack("<13I", 0xfeef04bd, 0x10000, high, low, high, low,
                        0x3f, 0, 0x40004, 2, 0, 0, 0)
    data = block("VS_VERSION_INFO", fixed, padded(string_info) + variable_info)
    null_header = struct.pack("<IIHHHHIHHII", 0, 32, 0xffff, 0, 0xffff, 0, 0, 0, 0, 0, 0)
    header = struct.pack("<IIHHHHIHHII", len(data), 32, 0xffff, 16, 0xffff, 1, 0, 0x30, 0x409, 0, 0)
    return null_header + header + padded(data)


def input_signature(command, paths, compiler):
    digest = hashlib.sha256()
    digest.update(json.dumps(command).encode())
    for path in sorted(set(paths)):
        digest.update(str(path).encode())
        digest.update(path.read_bytes())
    tool = compiler.stat()
    digest.update(f"{compiler}:{tool.st_size}:{tool.st_mtime_ns}".encode())
    return digest.hexdigest()


def project_dependencies(path, seen=None):
    seen = set() if seen is None else seen
    if path in seen:
        return seen
    seen.add(path)
    for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(encoding="utf-8"), re.M):
        included = path.parent / name
        if not included.is_file():
            included = SOURCE / name
        project_dependencies(included.resolve(), seen)
    return seen


def cached_command(command, inputs, target, compiler, rebuild):
    stamp = target.with_suffix(target.suffix + ".inputs")
    signature = input_signature(command, inputs, compiler)
    if not rebuild and target.is_file() and stamp.is_file() and stamp.read_text() == signature:
        return False
    stamp.unlink(missing_ok=True)
    subprocess.run(command, check=True)
    stamp.write_text(signature)
    return True


def build_plugin(compiler, output, directory, options):
    """output = the folder that gets fh2-mods/Gripline.dll"""
    started = time.monotonic()
    directory.mkdir(parents=True, exist_ok=True)
    arguments = compiler_arguments(compiler, options)

    def compile_source(filename):
        object_file = directory / (Path(filename).stem + ".obj")
        command = [*arguments, str(SOURCE / filename), "-o", str(object_file)]
        compiled = cached_command(command, project_dependencies(SOURCE / filename), object_file,
                                  compiler, options.rebuild)
        return str(object_file), compiled

    with ThreadPoolExecutor(max_workers=options.jobs) as workers:
        results = list(workers.map(compile_source, PLUGIN_SOURCES))
    import_library = directory / "kernel32.lib"
    cached_command([str(compiler), "dlltool", "-m", "i386:x86-64", "-d", str(SOURCE / "kernel32.def"),
                    "-l", str(import_library)], [SOURCE / "kernel32.def"], import_library, compiler,
                   options.rebuild)
    exception_library = directory / "ntdll.lib"
    cached_command([str(compiler), "dlltool", "-m", "i386:x86-64", "-d", str(SOURCE / "ntdll.def"),
                    "-l", str(exception_library)], [SOURCE / "ntdll.def"], exception_library, compiler,
                   options.rebuild)
    mods = output / "fh2-mods"
    mods.mkdir(parents=True, exist_ok=True)
    resource = directory / "version.res"
    resource.write_bytes(version_resource("Gripline.dll"))
    link = [str(compiler), "lld-link", "/dll", "/entry:DllMain", "/machine:x64", "/nodefaultlib",
            "/dynamicbase", "/nxcompat", "/Brepro"]
    subprocess.run([*link, "/out:" + str(mods / "Gripline.dll"), "/implib:" + str(directory / "gripline.lib"),
                    *[path for path, _ in results], str(import_library), str(exception_library),
                    str(resource)], check=True)
    print(f"Compiled {sum(compiled for _, compiled in results)}/{len(PLUGIN_SOURCES)} objects; "
          f"{time.monotonic() - started:.1f}s", flush=True)


def main():
    options = parse_arguments()
    compiler = options.zig.resolve()
    output = (options.output or SOURCE.parent / "gripline-plugin").resolve()
    directory = options.build_dir or SOURCE.parent / ".build" / ("plugin-test" if options.test else "plugin")
    build_plugin(compiler, output, directory.resolve(), options)
    print(output)


if __name__ == "__main__":
    main()
