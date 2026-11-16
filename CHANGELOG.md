# Changelog

What changed for players, newest first. Every version is on the [Releases](../../releases) page.
(Technical details: [docs/TECHNICAL_HISTORY.md](docs/TECHNICAL_HISTORY.md).)

## V7.2.2
- Fix: **cruise control** holds the speed again with FH2 Mouse & Keyboard installed - with wheel, controller and keyboard. Since V7.2 Gripline switched its own controller off next to that mod, and that controller is the way the cruise throttle reaches the game. It stays on now, only without Gripline's keys (driving with the keyboard stays with FH2 Mouse & Keyboard).

## V7.2
*Includes everything from V7.1 (V7.1 was not released on its own).*
- **Get out like in the game**: **F** opens the driver's door (right in right-hand drive cars) with the car's own animation, the camera glides out through the door and the door shuts behind you; getting back in goes the other way round. Cars without a door animation fade to black with door sounds. F8 › Settings › *Getting out*: Open the door / Fade to black.
- **Turn signals you can see and hear**: for Extra Options' indicators, green arrows above the speedometer and a click-clack sound in time with the lights. Arrows, sound and volume in F8 › Settings › *Turn signals (Extra Options)*; also in ANNA's menu. They switch off by themselves when Extra Options brings its own.
- **Head into turns**: in the cockpit and driver camera the head looks into the corner. Off / Light / Medium / Strong / Custom (angle, sensitivity, speed in and back, from km/h). Off while drifting, where Extra Options' drift camera takes over.
- **Dashboard Cam per car**: moving the dashboard cam saves the place for that car; cars without their own place use the general one.
- **Freecam while driving** (F7): the car keeps driving with your wheel or controller, only the camera is free. Right mouse button or right-stick click fixes the camera so it follows the car. At most 500 m from the car (`FreecamRange`, 0 = no limit). The old freecam (car stands) is gone.
- **Gripline tile in the pause menu**: Pause › CARS has a Gripline tile next to TUNE CAR with pages for almost all F8 settings (driving, walking, world & cockpit, cards …), German and English, values changed with left/right.
- **Gripline in ANNA's menu**: with [FH2 ANNA](https://github.com/qiangqiang101/FH2_ANNA), D-pad down › *Gripline* has Dashboard Cam, Get Out And Walk, Cruise Control, Telemetry, Free Camera, turn signal arrows and the Gripline menu – also as ANNA voice commands.
- **Gripline makes room for other mods**: with [FH2 Mouse & Keyboard](https://github.com/qiangqiang101/FH2MouseKeyboard) installed, its keyboard driving, menu cursor and mouse look are used and Gripline's own switch off (Gripline's hotkeys keep working). F8 › Settings › *Let other mods take over* = Off keeps Gripline's own.
- **NitroSystem** is switched off through its own switch while you walk, fly noclip/freecam, and online when *Nitro also online* is off. Nitro online is allowed by default.
- **Traffic changes while you play**: the traffic amount now also changes in the running game; the highest number of cars still loads at the start. *No Traffic* is really empty now (no more cars at the start).
- **Calmer start**: one start card instead of many, one big *What's new* card, big cards for important notes and errors.
- **FH2MP 1.4.1 / FH2 Mod Platform 2.9.0 / Extra Options 0.6.x**: Gripline's pages are under MODS › YOUR MODS › Gripline, its files in `mods\Forza Horizon 2\Gripline`; the MODS tab and the Exit Game tile stay working next to the Gripline tile.
- Fixes: the dashboard cam no longer jumps back after a change; no more "line skipped" card for new settings; a much smaller `Gripline.log`; texts and descriptions in the menus fixed and longer.

## V7.1
- **Mouse in the game's menus** – click tiles, wheel for tabs and lists, right click = back. Setting in F8: On / Only when moved / Off.
- **The F8 settings in the game's own menus too**: MODS › Installed mods › Gripline has pages for mouse, walking, driving, summon & online, cards and logbook (key bindings stay in F8).
- **Mouse look in the car**, with its own sensitivity setting (F8).
- **Smoother walking** – easing, crouch, handheld camera, mouse look; a **walking speedometer** with three looks.
- **Summon your car** while walking (**G** / controller **X**): it drives to you by itself.
- **Calm Drivatars** option for quieter free roam (F8 › World).
- **"Cockpit view 2" is now called "Dashboard cam"** (your settings and keys stay).
- Update notice understands the new release tags (`v7.x.y`).
- Fix: the pause menu popping up again and again after the start (focus guard).
- Under the hood: the last source files that came from Extra Options were rewritten.

## V7 – The Big Update
- **Walking mode** (F): get out and walk the open world with real collision, fade to black with car door sounds.
- **Gripline menu (F8)**: keys, traffic, cards, card design, telemetry, world, no damage in one place.
- **Freecam (F7)**, noclip on the controller, **cruise control (K)**.
- **Live telemetry**, logbook and records, **no damage on cars**, **world options** (speed cameras, names, event icons).
- Three **card designs**, more colours, update notice at start, experimental mouse steering.
- Includes all fixes of V6.4.1 and V6.5.

## V6.5 / V6.4.1 (fixes)
- V6.5: cockpit hands/wheel glitches fixed; Gripline removes its own cockpit files when it cannot hook the game.
- V6.4.1: loading-screen hang with more traffic fixed; "Original" traffic really is original.

## V6.4
- **Noclip** (F6), new cards (trip summary, wheel connected, start check), selectable card colour, Much More Traffic set to 100 cars.

## V6
- One mod file next to the official Extra Options (FH2MP-compatible). Only what the others don't do: keyboard, 1:1 cockpit wheel, **dashboard cam** (then "cockpit view 2"), real More Traffic, key list (F5), problem report, Azimuth Mapper check.

## V1 – V5 (October 2026)
- Started as a fork of Extra Options: wheel and force feedback, weather and time of day, traffic presets, keyboard driving, cockpit steering animation, nitro key, cards. See the old releases for details.
