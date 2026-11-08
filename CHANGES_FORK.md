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
- `features/noclip.c/.h` (V6.4) – noclip: key F6 flies the own car through the map (the car's CCarDynamics body state set through its own setter around the car update, hooked through the platform), offline only; live card with speed and height
- `features/trip.c/.h` (V6.4) – trip summary card in the pause menu (km, top speed, driving time; speed from the own car's body in the car update)
- `features/wheel_watch.c/.h` (V6.4) – wheel connected/disconnected card (raw input device list, vendor/product ids of known wheels)
- `features/game_units.c/.h` (V6.4) – the game's Units option (mph or km/h; Default = the language's units), read from its locale object, so the cards use the same unit as the speedometer
- `features/version_check.c/.h` (V6.4) – start card when Azimuth, Extra Options and FH2MP do not fit (azimuth.exe version resource, `FH2_ExtraOptions.log`, `fh2-mods\platform.log`)
- `features/traffic_files.c` (V6.4.1) – TrafficLevel 0 removes Gripline's old traffic files; load guard: marker `Gripline_loading.txt` at each start with raised traffic, removed after 5 s of the own car on the road, still there at the next start = traffic back to Original in `Gripline.ini` with a card. New installs default to Original traffic.
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

## V6: Gripline only does what the others don't
Since FH2MP 1.1.1 the launcher only accepts the official Extra Options `winmm.dll` (SHA-256 check), and Azimuth 1.0.3
drives wheels itself. Gripline V6 is therefore one FH2 Mod Platform mod, `fh2-mods\Gripline.dll`
(`plugin/gripline_plugin.c`), next to them – and leaves to them what they already do.

Removed (October 29, 2026):
- the whole Extra Options 0.2.98 base (winmm proxy, adapters, patches, EO features, menu page) – the official Extra Options runs instead
- wheel bridge, the game's own force feedback on the PC, soft wheel class, wheel setup assistant, wheel profiles,
  clutch as brake, wheel function buttons (`wheel_bridge`, `ffb_vm`, `wheel_soft`) – Azimuth 1.0.3 drives the wheel
- weather and time of day (`weather`, `time_of_day`, the Extra Options plugin `GriplineTick.dll`) – Extra Options' Weather Control
- creator mode (H) – Extra Options hides the HUD (F1); autodrive leftovers – Extra Options (F2)
- the "mods found" start card and the fh2wheels check – the platform's MODS tab lists the mods; fh2wheels does not load on Azimuth 1.0.x

Kept and changed:
- keyboard, steering animation (rotation now from Azimuth's `data\azimuth.ini`), cockpit view 2, More Traffic, nitro key,
  key list, cards, Azimuth Mapper check, problem report (adds `azimuth.log` and `mods\loadorder.txt`)
- settings: `Gripline.ini` ([Traffic], [Gripline]; old `Weather`/`TimeOfDay` keys are ignored) and `FH2_Keyboard.ini`
- `build.py` builds only `Gripline.dll`; tests run against its `--test` build

Plugin build on Azimuth 1.0.3 (October 29, 2026, tested in the game by Flenkii):
- the platform loads Gripline after Azimuth set up its picture and gamepads, so the early import hooks are not enough:
  - keyboard: Azimuth asks for the gamepad list once; Gripline finds that function by its code pattern, runs it again
    with `get_Gamepads` pointed at the Gripline pad, then puts `get_Gamepads` back (`keyboard_pad.c`)
  - cards and key list: Azimuth drops its D3D12 device when anything else draws into its back buffers, so the cards
    are drawn by the CPU into a transparent, click-through window over the game window (`notify.c`)
  - cockpit view 2: the driver camera's FOV slot (game+0x2e78120, slot 0x70) is chained in front of Extra Options' own
- start card "Gripline V6 active"
- traffic: amounts 30/45/70/100/200 cars, faster spawning; presets Original / Much More Traffic / No Traffic /
  Drivatars Only, chosen by name on the platform page *Traffic* (MODS > Installed mods > Gripline)

## V7.1 (November 2026)
- `features/menu_mouse.c/.h` – mouse in the game's menus: the real Windows cursor, the tile under it gets the
  pad selection (D-pad steps, tile places read from the menu's own layout), left click = A, wheel = LB/RB over the
  tab row or up/down, right click = B (`MenuMouse` in FH2_Keyboard.ini)
- `features/drive_look.c/.h` – mouse look in the car, every view (outside the camera circles the car)
- walking: easing, mouse look, crouch, handheld camera (F8), driver figure hidden, follow camera when getting out
- walking speedometer at the bottom right while on foot: three looks (Ring, Racing bar, Step pulse = default), F8 > Gripline > Walking: speedometer, Gripline.ini `WalkGauge` (`features/walk_gauge.c/.h`)
- `features/summon.c/.h` – summon the car while walking (G / controller X): the game's AI drives it along a GPS
  route, Gripline drives it itself off the road, the last metres and when the GPS / AI cannot (backs up and drives
  round obstacles), parks it beside the walker; live card with distance, speed and arrival time; autopilot sounds
  (`assets/sounds/summon_*.wav`, `features/summon_sounds.h` from `tools/make_summon_sounds.py`); F8 > Settings:
  driving style (Calm / Normal / Sporty, `SummonStyle`) and sounds (`SummonSounds`) in Gripline.ini
- what's-new cards for V7.1

## V7 – The Big Update (November 2026)
New files:
- `features/key_menu.c/.h` – Gripline menu (F8 / LS + D-pad right), keys saved to `FH2_Keyboard.ini`, other settings to `Gripline.ini`
- `features/walking.c/.h` – walking on foot with the photo mode's collision, door sounds (`assets/sounds`, `features/door_sounds.h` from `tools/make_door_sounds.py`, played with PlaySoundW from System32)
- `features/cruise.c/.h` – cruise control (K)
- `features/telemetry.c/.h`, `features/records.c/.h`, `features/logbook.c/.h` – live telemetry, records, CSV, logbook, what's-new cards
- `features/no_damage.c/.h` – visible damage reset for other / all cars (the game's own ForzaCar function)
- `features/world_files.c/.h` – speed cameras, names over cars, event icons, calm Drivatars (V7.1: AI/Difficulty.xml <Global> cruise chances 1/0/0, CruiseReaction off) (copies in `mods\Gripline\media`)
- `features/update_check.c/.h` – one HTTPS request to the GitHub releases API at start (winhttp loaded at run time)
Changed: `noclip.c` (freecam, walking, controller combos), `notify.c` (3 card designs, panels, fade to black), `cockpit.c` (V6.5 cockpit file repair), `settings.c` (a wrong line is skipped, not the rest), one shared lock for writing `Gripline.ini`; `sha256.c` and unused functions removed. Imports of `Gripline.dll` unchanged (51).
