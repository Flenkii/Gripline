# Gripline

**Wheel, keyboard, traffic & weather for Forza Horizon 2 on PC** (Xbox One version running on the **Azimuth** wrapper).
Works offline and online with [FH2MP](https://github.com/Charelene/FH2MP).

**[⬇ Download the latest version](../../releases/latest)** · current version: **V6**

Since V6 Gripline is **an add-on next to the official [Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions)**
by RokkuDayo, not a replacement any more: Extra Options stays as `winmm.dll` (the FH2MP launcher checks it), Gripline is a
[FH2 Mod Platform](https://github.com/Trinculo54/fh2-mod-platform) mod (`fh2-mods\Gripline.dll`) plus a small Extra Options
plugin (`mods\Gripline\plugins\GriplineTick.dll`). Gripline's code started as a fork of Extra Options 0.2.98 (GPL-3.0) –
see [README_ORIGINAL.md](README_ORIGINAL.md).

| Feature | In short |
|---|---|
| **Wheel bridge (Logitech G920)** | The wheel is reported to the game as an Xbox wheel: steering, pedals, clutch, paddles, buttons. Optional: clutch pedal as brake. Also works on game copies without `Microsoft.Xbox.Input.Wheel.dll`. **Azimuth 1.0.3 has its own wheel support** – see [Wheel: Azimuth or Gripline](#wheel-azimuth-or-gripline). |
| **Force feedback** | The **game's own wheel force feedback** – FH2's original Xbox wheel programs run on the PC, like on the Xbox. Only strength and rumble can be scaled. |
| **Keyboard** | Drive with WASD, arrow keys move the camera while driving and scroll in menus, Enter/Backspace/Esc in menus. Works together with a controller (one merged pad). Bindings in `FH2_Keyboard.ini`. |
| **Free wheel buttons** | RSB, LSB and the Xbox button can be assigned (look back/left/right, camera, handbrake, radio, shifting, nitro, cockpit view 2, creator mode …). |
| **Settings in the game** | Pause › Settings › **MODS** – all Gripline rows start with *Input*, *Wheel*, *Keyboard*, *Traffic*, *Weather*, *Time of Day*, *Cockpit* or *Gripline*. Saved to `Gripline.ini`, `FH2_Wheel.ini` and `FH2_Keyboard.ini`. |
| **Notifications** | Small Gripline cards with the logo at the top right of the game picture (also in fullscreen). Can be switched off. |
| **Nitro on keyboard & wheel** | For [NitroSystem](https://github.com/qiangqiang101/NitroSystem) by qiangqiang101: `N` or a wheel button (function 14) presses its nitro button. Off in online sessions. NitroSystem is not included. |
| **Steering animation** *(off by default)* | Cockpit: hands hidden, the in-game steering wheel turns 1:1 with your real wheel up to its rotation. Files are made once into `Mount\gripline_cockpit`, game files stay untouched. Applies after a restart. |
| **More traffic** | Presets Original/Calm/Busy/Rush Hour/Chaos/No Traffic/Drivatars Only, rush hour all day. Gripline raises the game's own traffic data (copies in `Mount\gripline_traffic`, game files unchanged). Applies after a restart. |
| **Fixed weather** | Sunny, Cloudy, Storm Clouds, Rain or Fog (fog only on Storm Island) through the game's own weather control. |
| **Time of day** *(V6)* | Morning, Midday, Sunset, Night or Slow Day (clock 4x slower). Offline in free roam only. |
| **Cockpit view 2** *(V6)* | A second interior camera like in FH5: Tab goes cockpit → cockpit 2 → next camera (also **V** or wheel function 15). Distance: *Cockpit View 2: Forward / Up*. |
| **Creator mode** *(V6)* | **H** (or wheel function 16) hides Gripline's cards for clean recordings. The game's HUD is hidden with **F1** (Extra Options' UI toggle). |
| **Key list** *(V6)* | **F5** shows all your key bindings at the right of the picture; F5 again hides it. |
| **Input profiles** *(V6)* | *Input: Profile*: Controller, Keyboard, Wheel, Wheel (Clutch as Brake), or *Set up wheel* – an assistant for wheels other than G920/G29/G923 (only tested with a G920 so far). |
| **Azimuth Mapper** *(V6)* | While Azimuth Mapper (by bigdamnhero6) runs, Gripline's keyboard and wheel stay off and a card says so. |
| **Start card** *(V6)* | At the start a card lists the mods Gripline found. |
| **Problem report** *(V6)* | *Gripline: Create Problem Report* packs logs and settings into `Gripline_Report.zip` next to `azimuth.exe`. Nothing is sent anywhere. |
| **Tools** (`tools/`) | Python scripts (cockpit wheel, More Traffic, traffic variety). Not part of the release download. |

> 🚨 **Security:** scammers spread keyloggers using the names of FH2 mods. **Gripline never asks you to paste commands into
> PowerShell or to run an "All-in-One" installer.** It is only `Gripline.dll`, `GriplineTick.dll` and INI files. Download it
> only from this repository's releases or build it yourself.

> ⚠️ **Unofficial.** Neither the Extra Options author, the FH2MP team nor the Azimuth developer supports Gripline.
> Report problems here ([Reporting problems](#reporting-problems)). Report Azimuth bugs to its developer only without mods.

## Compatibility
Built and checked for this combination (what FH2MP 1.1.2 installs, October 2026):

| Part | Version |
|---|---|
| Azimuth | **1.0.3** |
| Extra Options (official, `winmm.dll`) | **0.3.9** |
| FH2 Mod Platform | **2.7.4** |
| FH2MP | **1.1.2** (optional) |
| Forza Horizon 2 | Xbox One version 1.0.0.64 (`forza_durango_release_final.exe`) |
| Wheel | Logitech G920 (other wheels untested) |

Azimuth 0.9.10 and Gripline V5 (`winmm.dll`) are not supported any more. A new Azimuth, Extra Options or platform
release may break Gripline; updates follow when possible.

## Installation
1. Install Azimuth and FH2MP with the FH2MP launcher (it brings the official Extra Options and the FH2 Mod Platform).
   Start the game once.
2. From the release ZIP copy the **contents of the `Azimuth` folder** next to `azimuth.exe`:
   `fh2-mods\Gripline.dll` and `mods\Gripline\plugins\GriplineTick.dll`. Nothing is replaced.
3. **Do not** replace `winmm.dll` – it stays the official Extra Options. If you still have the `winmm.dll` of Gripline V5,
   let the FH2MP launcher put the official one back.
4. Start the game. A card at the top right says which mods Gripline found. Settings: Pause › Settings › MODS.

Updating from V5: your `[Traffic]` and `[Gripline]` settings move from `FH2_ExtraOptions.ini` to `Gripline.ini` on the
first start (a copy of the old INI is kept as `FH2_ExtraOptions.ini.before-gripline`). `FH2_Wheel.ini` and
`FH2_Keyboard.ini` stay as they are.

Uninstall: delete `fh2-mods\Gripline.dll` and the folder `mods\Gripline`. Optionally `Gripline.ini`, `Gripline.log`,
`FH2_Wheel.*`, `FH2_Keyboard.ini` and the folders `Mount\gripline_cockpit` and `Mount\gripline_traffic`.

**Online with FH2MP:** works with the same files. Online, traffic, weather and time of day come from the session, nitro is off.

## Wheel: Azimuth or Gripline
Azimuth 1.0.3 can drive a wheel itself (launcher settings, saved in `data\azimuth.ini`, with its own force feedback).
If Azimuth **and** Gripline both use the wheel, steering and force feedback may be doubled. Gripline shows a card
"Wheel: Azimuth and Gripline both on" in that case. Choose one:

| You want | Set |
|---|---|
| Gripline's wheel (game's own Xbox force feedback, wheel buttons, clutch as brake) | remove the wheel mapping in the Azimuth launcher |
| Azimuth's wheel | `AzimuthWheel=1` in `FH2_Wheel.ini` – Gripline then leaves the wheel alone; the cockpit animation uses Azimuth's rotation |

Which way feels better has not been compared in the game yet – reports welcome.

## Extra Options and its plugins
Gripline keeps all its features, also where Extra Options now offers something similar:
- **Weather Control plugin** (Extra Options – Weather Control): while it fixes the weather (`Mode` not `Dynamic`) or
  locks the clock (`[TimeLock] Enabled=1`), Gripline's own weather or time of day waits and the plugin wins. With
  `Mode=Dynamic` and the time lock off, Gripline's settings apply.
- **F1** (Extra Options) hides the game's HUD, **H** (Gripline) hides Gripline's cards.
- **F2** is Extra Options' autodrive.

## Keyboard bindings (default, editable in `FH2_Keyboard.ini`)

| Key | Function | Key | Function |
|---|---|---|---|
| W | Throttle | Arrow keys | Camera while driving, scroll in menus |
| S | Brake / reverse | Tab | Change camera (incl. cockpit 2) |
| A / D | Steer | C / Num2 | Look back |
| Space | Handbrake | Num4 / Num6 | Look left / right |
| E / Q | Shift up / down | R | Rewind |
| Left Shift | Clutch | T | Telemetry |
| Enter | Confirm (A) | P | Photo mode |
| Backspace | Back (B) | L | Headlights |
| Esc | Pause / menu | M | Map |
| X / Y | Buttons X / Y | Minus / Plus | Radio previous / next |
| Page Up / Page Down | Tabs left / right (LB / RB) | N | Nitro (NitroSystem) |
| V | Cockpit view 2 | H | Creator mode (Gripline cards) |
| F5 | Key list | F1 / F2 | Extra Options: HUD / autodrive |

The same list is in `Keybindings.txt` in the release ZIP.

## Reporting problems
Please report problems on GitHub: [**Issues → New issue**](../../issues/new/choose).
Easiest: Pause › Settings › MODS › *Gripline: Create Problem Report*, then attach `Gripline_Report.zip` (next to
`azimuth.exe`). It contains `Gripline.log/.ini`, `FH2_Wheel.log/.ini`, `FH2_Keyboard.ini`, `FH2_ExtraOptions.log/.ini`,
`fh2-mods\platform.log`, `data\logs\azimuth.log` and the Extra Options load order. Also tell us your device and versions.

## Known issues and limits
- **Wheel with Azimuth 1.0.3:** see [Wheel: Azimuth or Gripline](#wheel-azimuth-or-gripline).
- **Traffic, steering animation, input profile and "Use Wheel In Game" need a restart** of Azimuth.
- **Fog only on Storm Island.** Elsewhere "Fog" shows sunny weather.
- **Weather and time of day need `GriplineTick.dll`.** Without it they do nothing and a card says so.
- **Creator mode (H) only hides Gripline's cards** – use F1 for the game's HUD.
- **fh2wheels** is not loaded by Azimuth 1.0.x (it installs as `azimuth-tls.dll`, which Azimuth 1.0.x no longer loads).
- **With a wheel connected the game drives with the wheel.** For keyboard or controller: *Wheel: Use Wheel In Game* Off + restart.
- **No H-pattern shifter** through Gripline: FH2 only knows shift up / down.
- **Other wheels than the G920** are untested.

## Credits
- **[RokkuDayo](https://github.com/RokkuDayo)** – [Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) (GPL-3.0), the base of Gripline's code
- **[Almost There Games](https://almost-there-games.itch.io/azimuth)** – Azimuth (not included)
- **[Trinculo54](https://github.com/Trinculo54/fh2-mod-platform)** – FH2 Mod Platform; Gripline uses its MIT API header (`third_party/fh2mod`)
- **[Flenkii](https://github.com/Flenkii)** – idea, testing, maintainer
- **Claude (Anthropic)** – wrote most of the new code together with Flenkii
- **Lancaster** (Discord) – idea "clutch pedal as brake", tests on a copy without the Wheel DLL
- A YouTube viewer – idea for cockpit view 2
- Works well with: **[NitroSystem](https://github.com/qiangqiang101/NitroSystem)** by qiangqiang101, **Azimuth Mapper** by bigdamnhero6

## Not included
- No Forza Horizon 2 game files, no Azimuth, no Extra Options, no FH2MP.
- The scripts in `tools/` create their files **on your PC from your own game files**.

## Building
`python build.py /path/to/zig --plugin --output out` makes `out/fh2-mods/Gripline.dll` and
`out/mods/Gripline/plugins/GriplineTick.dll` (Zig 0.15.2). `python build.py /path/to/zig --azimuth` still builds the old
`winmm.dll` variant (used by most emulator tests, see [BUILD.md](BUILD.md)).
Tests (emulator, need `unicorn` and `pefile`; some need locally extracted game files in `tests/game`):
```
python tests/test_plugin.py out/fh2-mods/Gripline.dll
python build.py /path/to/zig --azimuth --test --output tests/winmm_test.dll
cd tests && python test_weather.py winmm_test.dll && python test_keyboard.py winmm_test.dll && python test_report.py winmm_test.dll
```
Release ZIP: `python tools/make_release.py out Gripline_V6.zip`.

## License
GPL-3.0 (see [LICENSE](LICENSE)). Changes compared to the original are listed in [CHANGES_FORK.md](CHANGES_FORK.md).

## AI disclosure
The code of Gripline was written with the help of an AI (Claude by Anthropic) and tested in an emulator and in the game.
