GRIPLINE V7 – The Big Update – for Forza Horizon 2 (Azimuth)
=======================================================================
An add-on next to Azimuth, the official Extra Options and FH2MP.

Source code: https://github.com/Flenkii/Gripline

No game files, no Azimuth, no Extra Options and no FH2MP included. Never paste commands
into PowerShell to install a mod – anything like that is a scam.
Not supported by the Extra Options author, the FH2MP team or the Azimuth developer.

Tested with: Azimuth 1.0.4, Extra Options 0.4.4, FH2 Mod Platform 2.8.0, FH2MP 1.1.3.


1. INSTALLATION
---------------
1. Install Azimuth and FH2MP with the FH2MP launcher (it brings the official Extra Options
   and the FH2 Mod Platform). Start the game once.
2. Copy the CONTENTS of the folder "Azimuth" from this ZIP next to azimuth.exe:
     fh2-mods\Gripline.dll
3. Do NOT replace winmm.dll - it stays the official Extra Options.
4. Start the game. Settings: Pause > Settings > MODS.

Updating from V5: delete the old Gripline winmm.dll (let the FH2MP launcher put the
official one back), and mods\Gripline\plugins\GriplineTick.dll if you have it.
Your [Traffic]/[Gripline] settings move to Gripline.ini on the first start.
FH2_Wheel.ini is not used any more.

Uninstall: delete fh2-mods\Gripline.dll AND the folder mods\Gripline.

Gripline never changes the game files. It writes its own copies (cockpit animations,
traffic data) into mods\Gripline\media, which Extra Options loads over the game's files.
Cockpit looks wrong (hands missing, wheel/driver gone) or the game hangs while loading
after removing Gripline? Delete the folder mods\Gripline - then everything is original.


2. WHAT GRIPLINE DOES - AND WHAT THE OTHERS DO
---------------------------------------------
Gripline:
  - Keyboard driving (WASD, arrows = camera), works together with a controller.
  - Cockpit: the steering wheel turns 1:1 with your real wheel (rotation from the Azimuth
    launcher), cockpit view 2 like in FH5 (Tab or V).
  - More Traffic: Original, Much More Traffic, No Traffic or Drivatars Only, picked by name
    (MODS > Installed mods > Gripline > Traffic), 30-100 cars, rush hour all day (restart needed).
  - Nitro key N for the mod NitroSystem (off online), key list F5, Gripline cards,
    problem report (Gripline_Report.zip).
  - V7: walking (F), freecam (F7), noclip (F6), Gripline menu (F8), cruise control (K),
    no damage, telemetry, records, logbook, world options, mouse steering.
Azimuth (its launcher): wheel, pedals, buttons, rotation and force feedback.
Extra Options: weather, time of day, hiding the HUD (F1), autodrive (F2).


3. NEW IN V7 – THE BIG UPDATE
-----------------------------
  - Walking: F (controller: LS + Y) gets you out of the car - fade to black, door sounds,
    and you stand next to it. WASD/left stick walk, arrows/right stick look, Shift/RT run,
    Space/A jump, with collision. The car stays parked, F/Y near it gets you back in.
  - Freecam: F7 (LS + D-pad down), the camera flies without the car.
  - Gripline menu: F8 (LS + D-pad right) - change every key in the game, card colour and
    design (Classic / Enhanced / Overdrive), traffic, cockpit view 2, cruise control,
    telemetry, mouse steering, logbook, records, world options, which cards show.
  - No damage on cars: Off / Others / All (F8).
  - Cruise control: K (LS + D-pad left), K again or brake = off.
  - Live telemetry panel (speed, grip, g-forces, 0-100, top speed, CSV recording),
    records (0-100, 0-200, quarter mile, braking, top speed) and a logbook per car.
  - World: speed cameras, names above cars and event icons can be switched off (F8).
  - Mouse steering (experimental, F8).
  - Update notice: an orange card at the start when a newer Gripline is out.
  - Card colours black and rainbow, card groups can be hidden (F8 > Cards & notices).
  - Includes all fixes of V6.4.1 and V6.5.

NEW IN V6.5 (bug fix)
------------------------
  - Fix: hands missing / glitching or the cockpit gone in some cars. Gripline now removes
    its own cockpit files by itself when it cannot hook the game or could not make them,
    and makes them new after a game update, a car mod or new cars.
  - Fix: on slow PCs (first start) the cockpit files could be ready only after the game
    had started without Gripline's cockpit hook.
  - Gripline never changes the game files. Uninstall = delete fh2-mods\Gripline.dll AND
    the folder mods\Gripline (see section 1).

NEW IN V6.4.1
-------------
  - Fix: a start with more traffic could hang on the loading screen. If that happens,
    Gripline sets the traffic back to Original at the next start and shows a card.
  - Fix: "Original" traffic now really is original (old traffic files are removed).
  - New installs start with Original traffic. Your Gripline.ini settings stay.

NEW IN V6.4
-----------
  - Noclip (F6): fly through walls, ground and buildings. W/S forward/back, A/D turn,
    Space/Left Ctrl up/down, Left Shift = 4x faster. Offline only (locked online, like nitro).
  - New cards: speed and height while flying, trip summary in the pause menu, wheel
    connected/disconnected, start check (Azimuth / Extra Options / FH2MP fit together?),
    a card for every setting you change. mph or km/h like the game's speedometer.
  - Card colour selectable (Gripline: Card Colour). Warnings are always red with WARNING.
  - Much More Traffic is now 100 cars (more hung the load after leaving a car meet).

NEW IN V6
---------
  - Runs next to the official Extra Options (FH2MP-compatible, no winmm.dll swap).
  - Only what the others don't have: Gripline's own wheel, force feedback, weather,
    time of day and creator mode are gone - Azimuth and Extra Options do those now.
  - Cockpit wheel turns 1:1, cockpit view 2, More Traffic with real traffic data,
    key list (F5), problem report, Azimuth Mapper detection, cards in a window over the game.


PROBLEMS? https://github.com/Flenkii/Gripline/issues
Pause > Settings > MODS > "Gripline: Create Problem Report", then attach
Gripline_Report.zip (next to azimuth.exe). Nothing is sent anywhere.

Known limits: traffic, steering animation and keyboard on/off need a restart.
Online, traffic comes from the session; nitro, noclip, freecam and walking are off.

Credits: RokkuDayo (Extra Options, base of Gripline's code), Trinculo54 (FH2 Mod Platform
API, MIT), Lancaster, qiangqiang101 (NitroSystem), bigdamnhero6 (Azimuth Mapper), a YouTube
viewer (cockpit view 2 idea), Sounds From The Lab (car door sounds, royalty-free),
MRAlexis99 / Alexis99 / Midnight and the community (ideas), seveN (bug report).

----------------------------------------------------------------------------------------
DEUTSCH (kurz)
fh2-mods\Gripline.dll aus dem Ordner "Azimuth" neben azimuth.exe kopieren. winmm.dll NICHT
ersetzen (bleibt das offizielle Extra Options). Einstellungen: Pause > Einstellungen > MODS.
V7: F = aussteigen und laufen, F7 = Freecam, F8 = Gripline-Menü, K = Tempomat.
Lenkrad und Force Feedback: im Azimuth-Launcher. Wetter, Tageszeit, HUD aus (F1):
Extra Options. Gripline: Tastatur, Cockpit, Verkehr, Nitro-Taste, Fehlerbericht.
Probleme: im MODS-Menü "Gripline: Create Problem Report" und Gripline_Report.zip auf
GitHub unter "Issues" anhängen.
