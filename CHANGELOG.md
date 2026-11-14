# Changelog

What changed for players, newest first. Every version is on the [Releases](../../releases) page.
(Technical details: [docs/TECHNICAL_HISTORY.md](docs/TECHNICAL_HISTORY.md).)

## V7.2 *(in progress)*
- **Gripline makes room for other mods**: with [FH2 Mouse & Keyboard](https://github.com/qiangqiang101/FH2MouseKeyboard) installed, its keyboard driving, menu cursor and mouse look are used and Gripline's own switch off (a card says so; Gripline's hotkeys such as noclip, freecam, walking and F8 keep working). F8 › Settings › *Let other mods take over* = Off keeps Gripline's own.
- **NitroSystem** is switched off through its own switch while you walk, fly noclip/freecam, and online when *Nitro also online* is off (its gauge hides too).
- **Gripline in ANNA's menu**: with [FH2 ANNA](https://github.com/qiangqiang101/FH2_ANNA) installed, D-pad down › *Gripline* has Dashboard Cam on/off, Get Out And Walk, Cruise Control, Telemetry, Free Camera and the Gripline menu – also as ANNA voice commands.
- The problem report lists which parts Gripline left to which mod.
- **Getting out like in the game**: **F** opens the driver's door (right in right-hand drive cars) with the car's own animation, the camera glides out of the seat through the door and the door shuts behind you; getting back in goes the other way round (with an outside camera the camera glides back behind the car while the door shuts). From a cockpit camera the picture dips briefly (one camera change) while the camera changes; when the game cannot change the camera (rewind menu) you stay in the car. Cars without a door animation get the fade as before. F8 › Settings › *Getting out*: Open the door / Fade to black.
- **Getting out / in, polished**: the camera turns at an even pace (no more whip round at the end), the glides are longer and softer, the cockpit dip is a short blink and the camera comes out of it looking where the turned head looked. Getting back into a cockpit camera the head is still at the open door, the door shuts in view and the head turns back to the road. The walking speedometer shows once the glide is done; the key help card shows only the first time, and there is no "Back in the car" card any more.
- **Getting out / in, door8**: out of a cockpit camera the picture comes back beside the open door, clear of the car's shell (no more mirror / dark inside for half a second), and the camera ducks under the roof's edge and stands up. The turned head looks less down. Noclip and freecam show their key help only the first time, no "off" cards any more, and their cards go when the game pauses (photo mode, menu).
- **Getting out / in, door9**: out of / into a cockpit camera the camera beside the car looks back at the seat (not out at the grass), and the door shuts while the camera is still on its way: the game's outside model has no inside of the door, an open door seen from behind showed only black edges.
- **Getting in, door10**: into a cockpit camera the camera glides on into the door opening (not stopping a metre outside) and the picture already gets dark on the way in.
- **Getting in, door11/12**: on the way into the seat the camera looks ahead and a little out to the door, like the cockpit camera right after (not across at the passenger seat).
- **Freecam while driving** (F7, like in FH1 Gripline Lite): the car keeps driving with your wheel or controller, only the camera is free - no hidden car, no collision. WASD/Space/Ctrl fly (arrows and Page up/down when a keyboard drives the car), mouse or right stick look, right mouse button or right-stick click fixes the camera so it follows the car. At most 500 m from the car (`FreecamRange`, 0 = no limit). F8 › Settings › *Freecam (F7)*: Car drives on / Car stands (old).

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
