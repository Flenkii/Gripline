# Gripline

**Immersion for Forza Horizon 2 on PC** – walk the open world on foot, drive with a real cockpit and your wheel, and shape the world around you.
One mod, one file (`Gripline.dll`), one in-game menu (**F8**). Works offline and online with [FH2MP](https://github.com/Charelene/FH2MP).

**[⬇ Download the latest version](../../releases/latest)** · **[💬 Discord](https://discord.gg/g83eBcrq)** (help, ideas, new versions) · current version: **V7.1**

> Gripline is in active development and gets small updates often (V7.1, V7.2 …). The release list shows what is new.

---

## What you get

### 🚶 Walking *(V7)*
Press **F** (controller: hold **LS + Y**) and get out of the car. Walk, run and jump through the open world **with real collision** (trees, walls, buildings, ground) – smooth movement, crouch and a hand-camera feel. Fade to black with car door sounds, **summon your car back** with **G** (controller: **X**), and get back in. Offline only.

### 🏎️ Cockpit, wheel and controls
- **Dashboard cam** – a second interior camera at the gauges, like in FH5 (**Tab** or **V**).
- **Steering animation** – the in-game wheel turns 1:1 with your real wheel.
- **Keyboard driving** – WASD, mouse look, all keys changeable in the game, works together with a controller.
- **Mouse steering** *(experimental)* and **mouse in the FH2 menus**.

### 🛠️ Driving tools
- **Live telemetry** – speed, grip and drift angle, g-forces, 0–100, quarter mile, braking distance; optional CSV recording.
- **Cruise control**, **logbook & records**, **no damage on cars** (other cars or all cars).
- **Noclip** (**F6**) and **Freecam** (**F7**) for fun and clean shots. Offline only.

### 🌍 World
- **Traffic presets**: Original, Much More Traffic, No Traffic, Drivatars Only.
- Switch off **speed cameras**, **names over cars** and **event icons** on the road.
- **Calm Drivatars** for quieter free roam.

### 🎛️ Menu and cards
- **Gripline menu (F8)** with mouse: keys, traffic, cards, card design, telemetry, world options.
- **Cards** in three designs (Classic, Enhanced, Overdrive) and your colour, plus an **update notice** at start (switch off in F8).
- **Problem report** with one click.

<details>
<summary><b>Full feature list</b></summary>

| Feature | Details |
|---|---|
| Keyboard | WASD, arrow keys move the camera while driving and scroll in menus, Enter/Backspace/Esc in menus. One merged pad with a controller. Bindings in `FH2_Keyboard.ini` or in F8. |
| Steering animation *(off by default)* | Hands hidden, the wheel turns as far as the rotation set in the Azimuth launcher. Files are made once into `mods\Gripline\media`, game files stay untouched. Applies after a restart. |
| Dashboard cam | Tab goes cockpit → dashboard cam → next camera (also **V**). Distance: *Dashboard Cam: Forward / Up*. |
| More traffic | Original / Much More Traffic (100 cars, all day) / No Traffic / Drivatars Only, or 30–100 cars, on the *Traffic* page (MODS › Installed mods › Gripline). Applies after a restart; offline only. |
| Nitro key | For [NitroSystem](https://github.com/qiangqiang101/NitroSystem): **N** presses its nitro button. Off online. |
| Key list | **F5** shows your key bindings at the right of the picture. |
| Noclip | **F6** (controller: LS + D-pad up). W/S fly, A/D turn, Space/Left Ctrl up/down, Left Shift 4× faster. A card shows height and speed. |
| Freecam | **F7** (controller: LS + D-pad down) flies a free camera, keys like noclip. |
| Walking | **F** / LS + Y. WASD walk, arrows look, Shift run, Space jump, hold Ctrl to crouch (controller: hold B), **G** / X summon the car. Hand camera and mouse look in F8. Walking gauge bottom right (Ring / Racing bar / Step pulse). |
| Cruise control | **K** (controller: LS + D-pad left) holds your speed. Off with K, braking or a crash. |
| Live telemetry | Panel with position, size and opacity selectable; optional CSV (`Gripline_Telemetry`). |
| Logbook & records | Distance per car, driving time, milestone cards (`Gripline_Logbook.txt`); best times with a NEW RECORD card (`Gripline_Records.txt`). |
| No damage on cars | Visible damage off for other cars or all cars. Physics unchanged. |
| World | Speed cameras, names over cars and event icons off (F8, after a restart). Calm Drivatars (F8 › World). |
| Card designs | Classic, Enhanced, Overdrive; colours incl. black and rainbow; each card group can be hidden (All / Few / Warnings only). |
| Update notice | At start Gripline asks GitHub once for the newest release and shows an orange card if there is a newer one. Nothing else is sent. |
| Mouse steering / mouse look | Mouse steering is experimental (off by default). Mouse-look sensitivity for walking and driving in F8. |
| Azimuth Mapper check | While Azimuth Mapper runs, Gripline's keyboard stays off and a card says so. |
| Problem report | *Gripline: Create Problem Report* packs logs and settings into `Gripline_Report.zip`. Nothing is sent anywhere. |

Settings: Pause › Settings › **MODS** and **F8**, saved to `Gripline.ini` and `FH2_Keyboard.ini`.

**Handled by other mods (not in Gripline):** wheel, pedals and force feedback → Azimuth launcher · weather and time of day, hide HUD (F1), Autodrive (F2) → Extra Options · list of installed mods → the platform's MODS tab.

</details>

---

## Install

You need Forza Horizon 2 (Xbox One version 1.0.0.64) running with **Azimuth 1.0.4**. Gripline is a mod for the [FH2 Mod Platform](https://github.com/Trinculo54/fh2-mod-platform); it does **not** need Extra Options or FH2MP.

**With the FH2MP launcher (easiest)**
1. Install Azimuth and FH2MP with the launcher (it brings Extra Options and the Platform). Start the game once.
2. Copy `fh2-mods\Gripline.dll` from the release ZIP next to `azimuth.exe` (into `fh2-mods`). Do **not** replace `winmm.dll`.
3. Start the game. Press **F8** or open Pause › Settings › MODS.

**Azimuth only**
1. Install Azimuth 1.0.4 and start the game once.
2. Download the FH2 Mod Platform from its [releases](https://github.com/Trinculo54/fh2-mod-platform/releases) (2.8.0 or newer) and copy **`DWrite.dll`** next to `azimuth.exe`.
3. Copy `fh2-mods\Gripline.dll` from the release ZIP next to `azimuth.exe`, then start the game. `fh2-mods\platform.log` should list Gripline.

**Uninstall:** delete `fh2-mods\Gripline.dll` **and** the folder `mods\Gripline`. Gripline never changes the game's own files; it only writes copies into `mods\Gripline\media`. If you delete only the DLL, those copies stay active and the cockpit looks wrong – so delete the folder too.

<details>
<summary>Updating from V5 · compatibility</summary>

Let the FH2MP launcher put the official `winmm.dll` back and delete `mods\Gripline\plugins\GriplineTick.dll` if you have it. Your `[Traffic]` and `[Gripline]` settings move from `FH2_ExtraOptions.ini` to `Gripline.ini` at the first start (copy: `FH2_ExtraOptions.ini.before-gripline`).

Tested with (what FH2MP installs, October 2026): Azimuth **1.0.4** · Extra Options **0.4.4** · FH2 Mod Platform **2.8.0** · FH2MP **1.1.3** (optional).

</details>

---

## Keys

| Key | Function | Key | Function |
|---|---|---|---|
| W / S | Throttle / brake | A / D | Steer |
| Space | Handbrake | E / Q | Shift up / down |
| Left Shift | Clutch | Tab | Change camera |
| V | Dashboard cam | N | Nitro (NitroSystem) |
| C / Num2 | Look back | Num4 / Num6 | Look left / right |
| R | Rewind | T / P | Telemetry / photo mode (hold) |
| L | Headlights | M | Map |
| F5 | Key list | F8 | Gripline menu |
| F6 | Noclip | F7 | Freecam |
| F | Walk / back in | G | Summon car (walking) |
| K | Cruise control | Esc | Pause |

Controller: hold **LS** and press **D-pad right** = Gripline menu, **up** = noclip, **down** = freecam, **left** = cruise control, **Y** = get out of the car (Y near the car = back in), **X** = summon car.
All keys can be changed in **F8** or `FH2_Keyboard.ini`.

---

## Problems?

- Pause › Settings › MODS › *Gripline: Create Problem Report* – then attach `Gripline_Report.zip` (next to `azimuth.exe`) to a GitHub [issue](../../issues/new/choose) or post it on [Discord](https://discord.gg/g83eBcrq) (#help-and-bug-reports).
- Wheel problems belong to Azimuth (test without mods and report them there).

<details>
<summary>Known limits</summary>

- Traffic, steering animation and keyboard on/off need a restart of Azimuth.
- Online, traffic comes from the session; nitro, noclip, freecam and walking are off.
- Noclip only flies the car; inside walls it can still make collision sounds or show visual damage.
- The steering animation turns as far as the rotation in the Azimuth launcher (900° when none is set).
- Walking: about 45 m from the car Gripline hides the car under you so the ground keeps its collision; the camera can then sit a little higher. Max. 2 km from the car. Door sounds use the Windows volume. Tested in windowed mode; exclusive fullscreen is not tested yet.
- Mouse steering is experimental.

</details>

> 🚨 **Security:** scammers spread keyloggers using the names of FH2 mods. **Gripline never asks you to paste commands into PowerShell or to run an "All-in-One" installer.** It is only `Gripline.dll`. Download it only from this repository's releases or build it yourself.
>
> ⚠️ **Unofficial.** Neither the Extra Options author, the FH2MP team nor the Azimuth developer supports Gripline.

---

## Credits
- **[RokkuDayo](https://github.com/RokkuDayo)** – [Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) (GPL-3.0); Gripline's code started as a fork of it
- **[Almost There Games](https://almost-there-games.itch.io/azimuth)** – Azimuth (not included)
- **[Trinculo54](https://github.com/Trinculo54/fh2-mod-platform)** – FH2 Mod Platform; Gripline uses its MIT API header (`third_party/fh2mod`)
- **[Flenkii](https://github.com/Flenkii)** – idea, testing, maintainer
- **Claude (Anthropic)** – wrote most of the code together with Flenkii
- **Lancaster** (Discord) – testing and ideas
- A YouTube viewer – idea for the dashboard cam
- **MRAlexis99**, Midnight and the Discord community – ideas for V7 (no damage, telemetry, world options, mouse steering)
- seveN and a YouTube viewer – bug reports (V6.4.1, V6.5)
- **[Sounds From The Lab](https://soundsfromthelab.com)** – car door sounds for walking (royalty-free, "Car Door Opening And Closing Slamming Shut")
- **[Deuquan](https://github.com/Deuquan/FH2-CameraMods)** – his FH2 Camera Mods showed how smooth walking can feel; that inspired the V7.1 walking polish (easing, crouch, mouse look). No code was taken: Gripline's walking mode is its own implementation, with real collision
- Works well with: **[NitroSystem](https://github.com/qiangqiang101/NitroSystem)** by qiangqiang101, **Azimuth Mapper** by bigdamnhero6

## Building and license
See [BUILD.md](BUILD.md): `python build.py /path/to/zig --output out` makes `out/fh2-mods/Gripline.dll` (Zig 0.15.2).
GPL-3.0 (see [LICENSE](LICENSE)). History: [CHANGES_FORK.md](CHANGES_FORK.md).

## AI disclosure
The code of Gripline was written with the help of an AI (Claude by Anthropic) and tested in an emulator and in the game.
