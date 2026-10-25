# Gripline

**Wheel, keyboard, traffic & weather for Forza Horizon 2 on PC** (Xbox One version running on the **Azimuth** wrapper).
Works offline and online with [FH2MP](https://github.com/Charelene/FH2MP).

**[⬇ Download the latest version](../../releases/latest)** · current version: **V6**

Gripline is built on [Extra Options 0.2.98](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) by RokkuDayo (GPL-3.0):
all features of Extra Options are included unchanged – see [README_ORIGINAL.md](README_ORIGINAL.md). Gripline adds:

| Feature | In short |
|---|---|
| **Wheel bridge (Logitech G920)** | The wheel is reported to the game as an Xbox wheel: steering, pedals, clutch, paddles, buttons. Optional: clutch pedal as brake. Also works on game copies without `Microsoft.Xbox.Input.Wheel.dll` (the mod brings its own version). |
| **Force feedback** | The **game's own wheel force feedback** – FH2's original Xbox wheel programs run on the PC, exactly like on the Xbox. No custom FFB profiles; only strength and rumble can be scaled. |
| **Keyboard** | Drive with WASD, arrow keys move the camera while driving and scroll in menus, Enter/Backspace/Esc in menus, FH5-like bindings. Works together with a controller (one merged pad, no "player 2" problems). Bindings in `FH2_Keyboard.ini`. While a wheel is connected, the game drives with the wheel. |
| **Free wheel buttons** | RSB, LSB and the Xbox button can be assigned (look back/left/right, camera, handbrake, radio, shifting …). |
| **GRIPLINE tile** *(V5)* | With the [FH2 Mod Platform](https://github.com/Trinculo54/fh2-mod-platform) (comes with FH2MP) all Gripline settings are on their own blue **GRIPLINE** tile in the pause menu (`fh2-mods\Gripline.dll`). Without the platform they stay at Options › HUD › bottom. |
| **Notifications** *(V5)* | Small Gripline cards with the logo at the top right of the game picture – drawn into the game's own image, so they also show in fullscreen. Can be switched off. |
| **Nitro on keyboard & wheel** *(V5)* | For the mod [NitroSystem](https://github.com/qiangqiang101/NitroSystem) by qiangqiang101: `N` on the keyboard or a free wheel button (function 14) presses its nitro button. Switched off automatically in online sessions. NitroSystem itself is not included. |
| **Full steering animation** *(V5, off by default)* | Cockpit: hands hidden, the steering wheel turns up to your wheel's rotation. The in-game wheel turns 1:1 with your real wheel (V6). Built in (no script any more); the files are made once into `Mount\gripline_cockpit`, game files stay untouched. Applies after a restart. |
| **fh2wheels compatibility** *(V6, tested)* | [fh2wheels](https://github.com/conner-mcvay/fh2-mods) by conner-mcvay (rims, tire widths, ride height, camber) runs next to Gripline (tested in game with fh2wheels 1.1.1). Its installer finds Gripline as "Extra Options". If another mod keeps its game database in `Mount\mediaoverride`, answer **Y** when the installer asks. |
| **Azimuth Mapper** *(V6)* | When Azimuth Mapper (by bigdamnhero6) runs, Gripline switches its own keyboard and wheel off and the Mapper's controller is used. A card says so at the start. |
| **Start card** *(V6)* | At the start a card lists the mods Gripline found (from the FH2 Mod Platform list, plus fh2wheels). |
| **Input profiles in the game** *(V6)* | *Input: Profile* at the top of the Gripline page: Controller, Keyboard, Wheel, Wheel (Clutch as Brake). Applies after a restart. |
| **In-game settings page** | Options › HUD › bottom (or the GRIPLINE tile). All values are saved instantly. Wheel values apply instantly (except "Use Wheel In Game", which needs a restart), weather applies after closing the menu, traffic only after restarting Azimuth (see Known issues). |
| **More traffic** | Traffic limits up to 100 cars, rush hour all day, presets Original/Calm/Busy/Rush Hour/Chaos. |
| **Fixed weather** | Sunny, Cloudy, Storm Clouds, Rain or Fog (fog only on Storm Island, sunny elsewhere) – through the game's own weather control, no game files changed. |
| **Tools** (`tools/`) | Python scripts (the V1 cockpit-wheel script, now built in), More Traffic files and "traffic variety" (20 extra traffic car models). Not part of the release download. |

> 🚨 **Security:** See the original author's warning in [README_ORIGINAL.md](README_ORIGINAL.md): scammers spread
> keyloggers using this mod's name. **This fork never asks you to paste commands into PowerShell or to run an
> "All-in-One" installer.** The mod is only `winmm.dll` plus INI files. Only download from this repository's
> releases or build it yourself from source.

> ⚠️ **Unofficial.** Neither the Extra Options author nor the Azimuth developer supports this fork.
> Please do **not** report issues with this fork to them – report them here instead, see
> [Reporting problems](#reporting-problems). Report Azimuth bugs to its developer only without mods.

## Compatibility
Tested only with this combination – other versions may not work:

| Part | Version |
|---|---|
| Azimuth | **0.9.10** |
| Extra Options (base) | **0.2.98** |
| Forza Horizon 2 | Xbox One version 1.0.0.64 (`forza_durango_release_final.exe`) |
| Wheel | Logitech G920 (other wheels untested) |

A new Azimuth release may break this mod. If Extra Options detects a mismatch it disables itself.
Updates will follow when possible.

## Keyboard bindings (default, editable in `FH2_Keyboard.ini`)

| Key | Function | Key | Function |
|---|---|---|---|
| W | Throttle | Arrow keys | Camera while driving, scroll in menus |
| S | Brake / reverse | Tab | Change camera |
| A / D | Steer | C / Num2 | Look back |
| Space | Handbrake | Num4 / Num6 | Look left / right |
| E / Q | Shift up / down | R | Rewind |
| Left Shift | Clutch | T | Telemetry |
| Enter | Confirm (A) | P | Photo mode |
| Backspace | Back (B) | L | Headlights |
| Esc | Pause / menu | M | Map |
| X / Y | Buttons X / Y | Minus / Plus | Radio previous / next |
| Page Up / Page Down | Tabs left / right (LB / RB) | | |

The same list is in `Keybindings.txt` in the release ZIP.

Driving functions follow the controller layout chosen in the game. Several keys per function are possible
(comma-separated); key names are listed at the top of `FH2_Keyboard.ini`.

## Reporting problems
Please report problems on GitHub: [**Issues → New issue**](../../issues/new/choose). Attach these files from your
Azimuth folder (next to `azimuth.exe`) so we can help:

| File | When |
|---|---|
| `FH2_ExtraOptions.log` | always |
| `FH2_Wheel.log` | wheel problems |
| `FH2_Wheel.ini`, `FH2_Keyboard.ini` | your settings (wheel / keyboard) |
| `fh2-mods\platform.log` | with FH2MP / the FH2 Mod Platform |

Start the game, reproduce the problem, close the game, then attach the files (drag them into the issue text box).
Also tell us your device (controller, keyboard or wheel) and your Azimuth version.

## Credits
- **[RokkuDayo](https://github.com/RokkuDayo)** – [Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions), the base of this fork
- **[Almost There Games](https://almost-there-games.itch.io/azimuth)** – Azimuth wrapper (not included)
- **[Flenkii](https://github.com/Flenkii)** – idea, testing, maintainer of this fork
- **Claude (Anthropic)** – wrote most of the new code in this fork together with Flenkii
- **Lancaster** (Discord) – tested on a game copy without the Wheel DLL and had the idea "clutch pedal as brake"
- **[Trinculo54](https://github.com/Trinculo54/fh2-mod-platform)** – FH2 Mod Platform; Gripline uses its MIT API header (`third_party/fh2mod`)
- Works well with: **[NitroSystem](https://github.com/qiangqiang101/NitroSystem)** by qiangqiang101 (nitro) – not included, install it yourself. **[fh2wheels](https://github.com/conner-mcvay/fh2-mods)** by conner-mcvay (wheels & suspension), tested next to Gripline (V6)

## Known issues
- **Traffic settings need an Azimuth restart.** `Traffic: Preset` and `Traffic: Amount` in the in-game menu are
  written to the INI immediately, but the game only reads traffic density when it loads the world. Change the
  values, then fully close and restart Azimuth (not just go back to a menu) for the new traffic amount to show
  up on the road. Wheel and weather settings are not affected.
- **Fog only on Storm Island.** Outside Storm Island the "Fog" weather setting shows sunny weather.
- **The keyboard needs Azimuth's gamepad input.** It is passed to the game as a gamepad; turning it on or
  off needs a restart.
- **With a wheel connected the game drives with the wheel.** To drive with keyboard or controller while the wheel
  is plugged in, set `Wheel: Use Wheel In Game` to Off and restart the game.
- **No H-pattern shifter.** FH2 only knows shift up / shift down, it ignores the gear of a shifter. Use the paddles
  (or E/Q on the keyboard).

## Not included
- No Forza Horizon 2 game files (no EXE, no database, no graphics/XAML).
- No Azimuth – get it from the developer: <https://almost-there-games.itch.io/azimuth>
- The scripts in `tools/` create their files **on your PC from your own game files**.

## Installation (Azimuth)
1. Install Azimuth and start the game once normally.
2. From the [release](../../releases) ZIP, copy everything from the `Azimuth` folder (`winmm.dll`, `FH2_ExtraOptions.ini`,
   `fh2-mods\Gripline.dll`) into the Azimuth folder (next to `azimuth.exe`). Back up your old `winmm.dll` first.
3. Done – controller, keyboard and a wheel are detected automatically. Settings are in-game on the GRIPLINE tile
   (with the Mod Platform) or under Options › HUD › bottom, or in `FH2_Wheel.ini` / `FH2_Keyboard.ini` (created on first start).
   Wheel: keep 900° in G HUB.

**Online with [FH2MP](https://github.com/Charelene/FH2MP):** install FH2MP with its launcher (it brings the FH2 Mod Platform)
and turn **off** "Install FH2 Extra Options if it is missing" in its settings – otherwise it moves Gripline into
`fh2extra` and puts the original Extra Options back. Gripline's `winmm.dll` stays next to `azimuth.exe`.
Online, traffic and weather come from the session and nitro is off.

Optional parts of the ZIP:

| Folder | What it is |
|---|---|
| `Optional/Profiles/Controller` | controller only (wheel and keyboard off) |
| `Optional/Profiles/Keyboard` | keyboard (+ controller), wheel off |
| `Optional/Profiles/Wheel` | wheel with the game's force feedback (= default) |
| `Optional/Profiles/Wheel_ClutchAsBrake` | like Wheel, but the clutch pedal brakes (the G920 brake pedal is stiff) |

The same four profiles can be switched **in the game** (V6): *Input: Profile* at the top of the Gripline page (the GRIPLINE tile). Wheel and keyboard on/off apply after restarting the game. Alternatively a profile is a pair `FH2_Wheel.ini` + `FH2_Keyboard.ini`; copy both next to `azimuth.exe` (replaces yours).

New INI keys of this fork in `FH2_ExtraOptions.ini`, section `[Traffic]`:
`TrafficLevel` (0–5), `RushHourAllDay` (0/1), `Weather` (0 original, 1 sunny, 2 cloudy, 3 storm clouds, 4 rain, 5 fog;
the old name `Wetter` is still understood); section `[Gripline]` (V5): `Notifications` (0/1, default 1),
`SteeringAnimation` (0/1, default 0). `FH2_Keyboard.ini`: `Nitro=N`. `FH2_Wheel.ini`: `ExtraFunction1-3=14` = nitro.
The wheel INI (`FH2_Wheel.ini`) and the keyboard INI (`FH2_Keyboard.ini`) are in English (since fork4). The German files
of fork1-fork3 (`FH2_Lenkrad.ini`, `FH2_Tastatur.ini`) are taken over automatically on the first start; the old files
are moved into `FH2_Settings_Backup`. Clutch pedal as brake: in the game (Options › HUD › "Wheel: Clutch Pedal as Brake") or `ClutchAsBrake=1` in
`FH2_Wheel.ini` – idea by Lancaster.
The old tuning keys of the fork1 FFB (`Damping`, `Friction` …) are only used as a fallback if the game's wheel
programs are not available.

## Building
Same as the original (see [BUILD.md](BUILD.md)): `python build.py /path/to/zig --azimuth`.
Tests (emulator, need `unicorn` and `pefile` plus locally extracted game files in `FH2_GAME`):
```
python build.py /path/to/zig --azimuth --test --output tests/winmm_test.dll
cd tests && python test_menu.py winmm_test.dll && python test_buttons.py winmm_test.dll && python test_weather.py winmm_test.dll
python test_keyboard.py winmm_test.dll && python test_game_ffb.py winmm_test.dll && python test_ffb_vm.py <exe image>
python test_soft_wheel.py winmm_test.dll && python test_ini_migrate.py winmm_test.dll && python test_nitro.py winmm_test.dll
python test_platform.py winmm_test.dll && FH2_COCKPIT_DATA=<folder with media/...> python test_cockpit.py winmm_test.dll
```
The build also makes `Gripline.dll` (the Mod Platform module) next to the output.
Release ZIP: `python tools/make_release.py winmm.dll out.zip` (takes `Gripline.dll` from beside `winmm.dll`).

## License
GPL-3.0 like the original (see [LICENSE](LICENSE)). Changes compared to the original are listed in
[CHANGES_FORK.md](CHANGES_FORK.md).

## AI disclosure
Like the original: the code of this fork was written with the help of an AI (Claude by Anthropic) and tested
in an emulator and in the game.
