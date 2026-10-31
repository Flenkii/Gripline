# Building and testing

Python 3.10 or later and Zig 0.15.2. Run the commands from this folder; the result is a Windows x64 DLL
(also when building on Linux). No Microsoft C runtime or SDK is needed, only Zig's bundled headers.

```text
python build.py /path/to/zig --output out
```

makes `out/fh2-mods/Gripline.dll`. `--build-dir PATH` keeps the intermediate files there, `--rebuild` ignores
the build cache. `--test` adds the `Fh2Test*` exports used by the emulator tests.

## Tests

The tests run the compiled DLL in an x64 emulator (`pip install unicorn pefile`):

```text
python build.py /path/to/zig --test --output out-test
cd tests
python test_plugin.py ../out-test/fh2-mods/Gripline.dll
python test_keyboard.py ../out-test/fh2-mods/Gripline.dll
python test_key_menu.py ../out-test/fh2-mods/Gripline.dll
python test_traffic_files.py ../out-test/fh2-mods/Gripline.dll
python test_modscan.py ../out-test/fh2-mods/Gripline.dll
python test_report.py ../out-test/fh2-mods/Gripline.dll
python test_ini_migrate.py ../out-test/fh2-mods/Gripline.dll
python test_cockpit_view.py ../out-test/fh2-mods/Gripline.dll
python test_nitro.py ../out-test/fh2-mods/Gripline.dll
python test_cockpit.py ../out-test/fh2-mods/Gripline.dll
```

`test_cockpit_range.py` needs locally extracted game files in `tests/game` (not part of the repository);
`test_cockpit.py` checks more with `FH2_COCKPIT_DATA` set to a folder with the game's cockpit media.

## Release ZIP

```text
python tools/make_release.py out Gripline_V6.zip
```
