# Changes in this fork compared to Extra Options 0.2.98

Fork of https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions – modified in October 2026 by Flenkii.

## New files
- `features/wheel_bridge.c` – G920 wheel as an Xbox wheel, force feedback (the game's own wheel programs; the old own FFB only as fallback), reconnect, log
- `features/wheel_soft.c/.h` - own implementation of the Xbox wheel class, used when the game copy has no `Microsoft.Xbox.Input.Wheel.dll`
- `features/ffb_vm.c/.h` – runs FH2's own Xbox wheel force-feedback programs on the PC
- `features/keyboard_pad.c/.h` – keyboard as a gamepad (merged with real gamepads)
- `features/wheel_buttons.c/.h` – assignable wheel buttons
- `features/rig_settings.c/.h` – in-game "Wheel & Traffic" page, traffic presets, saving to the INIs
- `features/traffic_tunables.c` – higher traffic limits, rush hour all day (live)
- `features/traffic_files.c/.h` (V6) – More Traffic for real: raised copies of the game's traffic data (`AI/CarLoadingSettings.xml`, `AI/Densities*.xml` from `media\EntityModel.zip`, rules of `tools/fh2_more_traffic.py`) in `Mount\gripline_traffic`, mounted over `game:\media`; game files unchanged
- `features/weather.c/.h` – fixed weather through the game's own weather control
- `features/ini_migrate.c/.h` – takes the German settings files of fork1-fork3 over into the English ones (fork4)
- `features/ini_edit.c/.h` – edit INI lines without losing comments
- `adapters/azimuth_guard.c` – guard against a null pointer while loading under Azimuth
- `features/nitro_link.c/.h` (V5) – nitro key / wheel button for NitroSystem (only its XInput import), off in online sessions
- `features/notify.c/.h`, `features/gripline_logo.h` (V5) – notification cards drawn into the game's D3D12 swap chain
- `features/platform_link.c/.h`, `platform/gripline_mod.c` (V5) – FH2 Mod Platform module (`fh2-mods\Gripline.dll`) and the GRIPLINE tile page
- `features/cockpit.c/.h` (V5) – full steering animation built in (own Deflate, Oodle1 and GR2 code; port of `tools/fh2_lenkrad.py`); V6: the game's ±90° clamp and divisor patched so the wheel turns 1:1
- `features/mod_scan.c/.h` (V6) – start card with the mods found, Azimuth Mapper detection (its named event `AzimuthMapper.ShowWindow`, no process list)
- `features/cockpit_view.c/.h` (V6) – cockpit view 2: camera moved to the gauges, wheel and driver hidden live
- `features/creator_mode.c/.h` (V6) – creator mode: HUD, minimap, prompts and Gripline cards hidden (key H / wheel function 16)
- `features/time_of_day.c/.h` (V6) – fixed time of day or slow day in free roam, offline only, through the game's own clock
- `features/report.c/.h` (V6) – problem report: logs and INIs into `Gripline_Report.zip` (stored ZIP)
- `third_party/fh2mod/` (V5) – the FH2 Mod Platform's API header (MIT, Trinculo54)
- `assets/` (V5) – Gripline logo
- `tools/` – Python scripts for the cockpit wheel, More Traffic and traffic variety, `make_release.py`
- `release/` – `FH2_ExtraOptions.ini`, `README.txt` and `Keybindings.txt` for the release ZIP
- `tests/` – emulator tests (unicorn)

## Changed files
- `video_options*.c/.h` – extra menu rows and choice lists on the HUD page
- `settings.c/.h`, `extra_options.h` – new INI keys `[Traffic] TrafficLevel, RushHourAllDay, Wetter`
- `vehicle_controls.c` – calls the weather control once per frame
- `adapters/azimuth.c` – hooks Azimuth's gamepad import for the keyboard
- `runtime.c`, `dllmain.c`, `features.h`, `build.py`, `kernel32.def`, `adapters/azimuth_host.h` – wiring for the new parts
- `README.md` – new; the original README is kept unchanged in `README_ORIGINAL.md`

## V6: plugin build (next to the official Extra Options)
Since FH2MP 1.1.1 the launcher only accepts the official Extra Options `winmm.dll` (SHA-256 check), so Gripline V6 no
longer replaces it:
- `plugin/gripline_plugin.c` – `fh2-mods\Gripline.dll` as an FH2 Mod Platform mod: starts the Gripline features once the
  game is mapped, settings rows on Pause › Settings › MODS, `Gripline.ini` (moves `[Traffic]`/`[Gripline]` out of
  `FH2_ExtraOptions.ini`), detects the Extra Options "Weather Control" plugin and lets it win while it fixes weather
  or clock, warns when `GriplineTick.dll` never ticks
- `plugin/gripline_tick.c` – `mods\Gripline\plugins\GriplineTick.dll`, Extra Options plugin that passes the vehicle
  update tick to Gripline (weather, time of day)
- `features/wheel_bridge.c` – reads Azimuth 1.0.3's wheel mapping (`data\azimuth.ini`); `AzimuthWheel=1` in
  `FH2_Wheel.ini` leaves the wheel to Azimuth, otherwise a card warns that both drive it
- `features/weather.c`, `features/time_of_day.c` – `*_set_yield` to step back for the Weather Control plugin
- `features/report.c` – problem report also packs `data\logs\azimuth.log`, `mods\loadorder.txt` and the Weather Control INI
- `build.py --plugin`, `tools/make_release.py <plugin folder>` – plugin build and V6 ZIP layout with `SHA256SUMS.txt`
