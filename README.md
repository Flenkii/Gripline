# Gripline

**Keyboard driving, cockpit and traffic for Forza Horizon 2 on PC** (Xbox One version on the **Azimuth** wrapper).
Works offline and online with [FH2MP](https://github.com/Charelene/FH2MP).

> ## 🚶 NEW IN V7: WALKING MODE – get out of your car!
> Press **F** (controller: hold **LS + Y**) and walk around Forza Horizon 2 on foot – **with real collision** (trees, walls, buildings, ground).
> Walk, run and jump, fade to black with **car door sounds**, your car stays parked until you get back in. Offline only.

**[⬇ Download the latest version](../../releases/latest)** · current version: **V7 – The Big Update**

**[Join the Gripline Discord](https://discord.gg/g83eBcrq)** · problems, questions, ideas for V8 and showcase. Join to always stay up to date with every new version.

Gripline is an add-on next to Azimuth, the official [Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions)
by RokkuDayo and FH2MP. It is one file, `fh2-mods\Gripline.dll`, a [FH2 Mod Platform](https://github.com/Trinculo54/fh2-mod-platform)
mod. Since V6 Gripline only does what the others don't:

| Gripline | In short |
|---|---|
| **Keyboard** | Drive with WASD, arrow keys move the camera while driving and scroll in menus, Enter/Backspace/Esc in menus. Works together with a controller (one merged pad). Bindings in `FH2_Keyboard.ini`. |
| **Steering animation** *(off by default)* | Cockpit: hands hidden, the in-game steering wheel turns 1:1 with your real wheel, as far as the rotation set in the Azimuth launcher. Files are made once into `mods\Gripline\media`, game files stay untouched. Applies after a restart. |
| **Cockpit view 2** | A second interior camera like in FH5: Tab goes cockpit → cockpit 2 → next camera (also **V**). Distance: *Cockpit View 2: Forward / Up*. |
| **More traffic** | Presets Original / Much More Traffic (100 cars, all day) / No Traffic / Drivatars Only, or an amount from 30 to 100 cars, picked by name on the *Traffic* page (MODS › Installed mods › Gripline). Gripline raises the game's own traffic data (copies, game files unchanged). Applies after a restart; offline only. New installs start with Original (V6.4.1). If a start with more traffic hangs while loading, Gripline sets the traffic back to Original at the next start and shows a card. |
| **Nitro key** | For [NitroSystem](https://github.com/qiangqiang101/NitroSystem) by qiangqiang101: **N** presses its nitro button. Off in online sessions. |
| **Key list** | **F5** shows all your key bindings at the right of the picture; F5 again hides it. |
| **Noclip** *(V6.4)* | **F6** lets your car fly through the map: through walls, the ground and buildings. W/S fly forward/back, A/D turn, Space/Left Ctrl up/down, Left Shift held = 4x faster; the arrow keys still move the camera. A card says on/off and shows speed and height while flying (mph/ft or km/h/m, like the game's speedometer). Offline only: a card says when it is locked in an online session and available again offline (like nitro). |
| **Cards** | Small Gripline cards at the top right of the picture (also in fullscreen). Can be switched off; colour selectable (*Gripline: Card Colour*: blue, red, green, purple, orange, yellow, pink, white). V6.4 cards: every setting changed in the menu ("Traffic: Preset – Much More Traffic"), a trip summary in the pause menu (distance, top speed, driving time of the session; miles/mph when the game is set to mph), wheel connected (green) / disconnected (red, WARNING), and a start check when Azimuth, Extra Options and FH2MP do not fit together (e.g. FH2MP needs an update for a new Azimuth; red, WARNING). Warning cards are always red with WARNING in the title (also "Nitro off" online, with the reason), noclip locked is orange, "Nitro on again" and noclip available again are green; all other cards use the chosen colour. |
| **Gripline menu** *(V7)* | **F8** (controller: hold **LS + D-pad right**) opens the Gripline menu in the game: change, add or reset keys, traffic, cards, card design, steering animation, cockpit view 2, live telemetry, cruise control, world options, no damage, problem report. Arrows/Enter/Esc or D-pad/A/B. |
| **Walking** *(V7)* | **F** (controller: hold **LS + Y**) gets you out of the car: walk on foot, invisible, with collision (trees, walls, buildings). WASD walk, mouse or arrows look, Shift run, Space jump, hold Ctrl to crouch (controller: sticks, RT run, A jump, hold B to crouch). Smooth movement and camera since V7.1; mouse steering pauses while walking. The car stays parked; **F** / **Y** near the car gets you back in. Fade to black with car door sounds. Offline only. |
| **Freecam** *(V7)* | **F7** (controller: hold **LS + D-pad down**) flies a free camera through the world, keys like noclip. Offline only. Noclip is now also on the controller (**LS + D-pad up**). |
| **Cruise control** *(V7)* | **K** (controller: hold **LS + D-pad left**) holds your speed. Off with K, braking or a crash; options in F8. |
| **Live telemetry** *(V7)* | Panel with speed, grip/drift angle, g-forces, 0-100 km/h (0-60 mph), quarter mile, 0-200, braking distance, top speed; position, size and opacity selectable; optional CSV recording (`Gripline_Telemetry`). |
| **Logbook & records** *(V7)* | Distance per car, driving time, top 5 cars, milestone cards (`Gripline_Logbook.txt`); best times kept for ever with a NEW RECORD card (`Gripline_Records.txt`). |
| **No damage on cars** *(V7)* | Visible damage off for other cars (Drivatars, traffic) or all cars. Physics unchanged. |
| **World** *(V7)* | Speed cameras off, names over cars off, event icons on the road off (F8, after a restart). |
| **Card designs** *(V7)* | Classic, Enhanced and Overdrive; colours incl. black and rainbow; each card group can be hidden (presets All / Few / Warnings only). |
| **Update notice** *(V7)* | At start Gripline asks GitHub once for the latest release and shows an orange card when a newer version is out. Nothing else is sent; switch it off in F8 › Settings. |
| **Mouse steering** *(V7, experimental)* | Steer with the mouse (off by default, F8). |
| **Azimuth Mapper check** | While Azimuth Mapper (by bigdamnhero6) runs, Gripline's keyboard stays off and a card says so. |
| **Problem report** | *Gripline: Create Problem Report* packs logs and settings into `Gripline_Report.zip` next to `azimuth.exe`. Nothing is sent anywhere. |

Settings: Pause › Settings › **MODS** (rows starting with *Keyboard*, *Traffic*, *Cockpit* and *Gripline*), saved to
`Gripline.ini` and `FH2_Keyboard.ini`.

**Not in Gripline any more (V6) – the others do it:**

| What | Where now |
|---|---|
| Wheel, pedals, buttons, rotation, force feedback | Azimuth launcher (wheel settings) |
| Weather, time of day | Extra Options (Weather Control) |
| Hide the HUD | Extra Options, **F1** |
| Autodrive | Extra Options, **F2** |
| List of installed mods | the platform's MODS tab |

> 🚨 **Security:** scammers spread keyloggers using the names of FH2 mods. **Gripline never asks you to paste commands into
> PowerShell or to run an "All-in-One" installer.** It is only `Gripline.dll`. Download it only from this repository's
> releases or build it yourself.

> ⚠️ **Unofficial.** Neither the Extra Options author, the FH2MP team nor the Azimuth developer supports Gripline.
> Report problems here ([Reporting problems](#reporting-problems)).

## Compatibility
Tested with what FH2MP 1.1.3 installs (October 2026):

| Part | Version |
|---|---|
| Azimuth | **1.0.4** |
| Extra Options (official, `winmm.dll`) | **0.4.4** |
| FH2 Mod Platform | **2.8.0** |
| FH2MP | **1.1.3** (optional) |
| Forza Horizon 2 | Xbox One version 1.0.0.64 (`forza_durango_release_final.exe`) |

## Installation

Gripline is a mod for the [FH2 Mod Platform](https://github.com/Trinculo54/fh2-mod-platform) (by Trinculo54). It needs **Azimuth** and the Platform. It does **not** need Extra Options and not the FH2MP multiplayer.

### A) With the FH2MP launcher (easiest)
1. Install Azimuth and FH2MP with the FH2MP launcher (it brings the official Extra Options and the FH2 Mod Platform).
   Start the game once.
2. Copy `fh2-mods\Gripline.dll` from the release ZIP's `Azimuth` folder next to `azimuth.exe`.
3. **Do not** replace `winmm.dll` – it stays the official Extra Options.
4. Start the game. Settings: Pause › Settings › MODS. Wheel: set it up in the Azimuth launcher.

### B) Azimuth only (no FH2MP, no Extra Options)
1. Install Azimuth 1.0.4 and start the game once.
2. Get the FH2 Mod Platform: download the ZIP from its [releases](https://github.com/Trinculo54/fh2-mod-platform/releases) (version 2.8.0 or newer) and copy **`DWrite.dll`** next to `azimuth.exe`. (Any other mod's ZIP that ships platform 2.8.0+ has this file too.)
3. Copy `fh2-mods\Gripline.dll` from the release ZIP's `Azimuth` folder next to `azimuth.exe` (so it ends up in `fh2-mods`).
4. Start the game. Check: `fh2-mods\platform.log` lists Gripline, and Pause › Settings › MODS shows it.
   Nothing there? Send `platform.log` (or the problem report) on [Discord](https://discord.gg/g83eBcrq) or as an issue.

Updating from V5: let the FH2MP launcher put the official `winmm.dll` back and delete `mods\Gripline\plugins\GriplineTick.dll`
if you have it. Your `[Traffic]` and `[Gripline]` settings move from `FH2_ExtraOptions.ini` to `Gripline.ini` on the first
start (copy of the old INI: `FH2_ExtraOptions.ini.before-gripline`). `FH2_Wheel.ini` is not used any more.

Uninstall: delete `fh2-mods\Gripline.dll` **and** the folder `mods\Gripline`.

Gripline never changes the game's own files. It only writes its own copies into `mods\Gripline\media`
(cockpit animations, traffic data), and Extra Options loads that folder over the game's files. If you remove or
switch off only `Gripline.dll`, those copies are still loaded without Gripline - the cockpit then looks wrong
(hands missing, wheel or driver gone at full lock). Delete the folder `mods\Gripline` as well and everything is
original again; Gripline makes the files new at its next start. From V6.5 Gripline also removes its cockpit files by
itself when it cannot hook the game, and makes them new when the game's car files change.

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
| V | Cockpit view 2 | F5 | Key list |
| F6 | Noclip on/off (offline) | Space / Left Ctrl | Noclip: up / down |
| Left Shift (held) | Noclip: faster | W / S, A / D | Noclip: fly, turn |
| F7 | Freecam on/off (offline) | F8 | Gripline menu |
| F | Get out / walk, back in near the car (offline) | K | Cruise control |

Controller (hold the left stick, LS): LS + D-pad right = Gripline menu, up = noclip, down = freecam, left = cruise control, LS + Y = get out (Y near the car = back in).

## Reporting problems
GitHub: [**Issues → New issue**](../../issues/new/choose). Pause › Settings › MODS › *Gripline: Create Problem Report*,
then attach `Gripline_Report.zip` (next to `azimuth.exe`). It contains `Gripline.log/.ini`, `FH2_Keyboard.ini`,
`FH2_ExtraOptions.log/.ini`, `fh2-mods\platform.log`, `data\logs\azimuth.log` and the Extra Options load order.
Or just join the [Discord](https://discord.gg/g83eBcrq) and post your problem and `Gripline.log` in help-and-bug-reports. Join to also stay up to date with every new version.
Wheel problems belong to Azimuth (report them there without mods).

## Known limits
- Traffic, steering animation and keyboard on/off need a restart of Azimuth.
- Online, traffic comes from the session and nitro and noclip are off.
- Noclip only flies the car (its physics is held still); while it is on, the car can still make collision sounds or take visual damage inside walls. Switching it off drops the car where it is.
- The steering animation turns as far as the rotation in the Azimuth launcher (900° when none is set).
- Walking: about 45 m away from the car, Gripline hides the car under you so the ground keeps its collision; the camera can then sit a little higher. Max. 2 km from the car. Door sounds use the Windows volume, not the game's.
- Walking and the fade to black are tested in windowed mode; exclusive fullscreen is not tested yet.
- Mouse steering is experimental.

## Credits
- **[RokkuDayo](https://github.com/RokkuDayo)** – [Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) (GPL-3.0); Gripline's code started as a fork of it
- **[Almost There Games](https://almost-there-games.itch.io/azimuth)** – Azimuth (not included)
- **[Trinculo54](https://github.com/Trinculo54/fh2-mod-platform)** – FH2 Mod Platform; Gripline uses its MIT API header (`third_party/fh2mod`)
- **[Flenkii](https://github.com/Flenkii)** – idea, testing, maintainer
- **Claude (Anthropic)** – wrote most of the code together with Flenkii
- **Lancaster** (Discord) – testing and ideas
- A YouTube viewer – idea for cockpit view 2
- **MRAlexis99**, Midnight and the Discord community – ideas for V7 (no damage, telemetry, world options, mouse steering)
- seveN and a YouTube viewer – bug reports (V6.4.1, V6.5)
- **[Sounds From The Lab](https://soundsfromthelab.com)** – car door sounds for walking (royalty-free, "Car Door Opening And Closing Slamming Shut")
- **[Deuquan](https://github.com/Deuquan/FH2-CameraMods)** – his FH2 Camera Mods showed how smooth walking can feel; that inspired the V7.1 walking polish (easing, crouch, mouse look). No code was taken: Gripline's walking mode is its own implementation, with real collision
- Works well with: **[NitroSystem](https://github.com/qiangqiang101/NitroSystem)** by qiangqiang101, **Azimuth Mapper** by bigdamnhero6

## Building
See [BUILD.md](BUILD.md): `python build.py /path/to/zig --output out` makes `out/fh2-mods/Gripline.dll` (Zig 0.15.2).

## License
GPL-3.0 (see [LICENSE](LICENSE)). History: [CHANGES_FORK.md](CHANGES_FORK.md).

## AI disclosure
The code of Gripline was written with the help of an AI (Claude by Anthropic) and tested in an emulator and in the game.
