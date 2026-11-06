GRIPLINE V7 "THE BIG UPDATE" – keyboard, cockpit, walking & more for Forza Horizon 2 (Azimuth)
=========================================================================================
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
  - V7: Gripline menu (F8), walking (F), freecam (F7), noclip (F6), cruise control (K),
    live telemetry, logbook, records, no damage on cars, world options, update notice.
Azimuth (its launcher): wheel, pedals, buttons, rotation and force feedback.
Extra Options: weather, time of day, hiding the HUD (F1), autodrive (F2).


3. NEW IN V7 – THE BIG UPDATE
-----------------------------
  - Gripline menu (F8; controller: hold LS + D-pad right): all Gripline settings in the
    game - keys (change, add, reset), traffic, cards, card design, steering animation,
    cockpit view 2, telemetry, cruise control, world, no damage, problem report.
    Arrows/Enter/Esc or D-pad/A/B. Changes show up at once in the key list (F5).
  - Walking (F; controller: hold LS + Y): get out and walk around on foot, with collision
    (trees, walls, buildings). WASD walk, arrows look, Shift run, Space jump; controller
    left/right stick, RT run, A jump. The car stays parked. Back in: F / Y near the car.
    Fade to black with car door sounds. Offline only.
  - Freecam (F7; controller: hold LS + D-pad down): fly a free camera through the world.
    Noclip (F6; LS + D-pad up) now also on the controller. Both offline only.
  - Cruise control (K; controller: hold LS + D-pad left): holds your speed. Off with K,
    braking or a crash; options in F8 > Cruise control.
  - Live telemetry panel: speed, grip / drift angle, g-forces, 0-100 (0-60 mph), quarter
    mile, 0-200, braking distance, top speed. Position, size and opacity selectable.
    Optional CSV recording (Gripline_Telemetry folder).
  - Logbook: km (or miles) per car, driving time, top 5 cars, milestone cards.
    Records for ever (Gripline_Records.txt) with a NEW RECORD card.
  - No damage on cars: visible damage off for other cars or all cars (F8). Physics unchanged.
  - World (F8, restart needed): speed cameras off, names over cars off, event icons on the
    road off.
  - Cards: 3 designs (Classic, Enhanced, Overdrive), new colours (black, rainbow), each card
    group can be switched off (presets All / Few / Warnings only).
  - Update notice: at start, an orange card when a newer Gripline is out (asks GitHub once;
    switch off in F8 > Settings).
  - Mouse steering (experimental, off by default).
  - All fixes of V6.4.1 and V6.5 are in V7.

4. NEW IN V6.5 (bug fix)
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
Online, traffic comes from the session and nitro is off.

Credits: RokkuDayo (Extra Options, base of Gripline's code), Trinculo54 (FH2 Mod Platform
API, MIT), Lancaster, qiangqiang101 (NitroSystem), bigdamnhero6 (Azimuth Mapper), a YouTube
viewer (cockpit view 2 idea), MRAlexis99 and the Discord community (ideas for V7),
Sounds From The Lab (car door sounds, royalty-free, soundsfromthelab.com).

----------------------------------------------------------------------------------------
DEUTSCH (kurz)
fh2-mods\Gripline.dll aus dem Ordner "Azimuth" neben azimuth.exe kopieren. winmm.dll NICHT
ersetzen (bleibt das offizielle Extra Options). Einstellungen: Pause > Einstellungen > MODS.
Lenkrad und Force Feedback: im Azimuth-Launcher. Wetter, Tageszeit, HUD aus (F1):
Extra Options. Gripline: Tastatur, Cockpit, Verkehr, Nitro-Taste, Fehlerbericht.
NEU IN V7: Gripline-Menü (F8), Laufen (F), Freecam (F7), Tempomat (K), Live-Telemetrie,
Fahrtenbuch, Rekorde, Kein Schaden an Autos, Welt-Optionen, 3 Karten-Designs, Update-Hinweis.
Probleme: im MODS-Menü "Gripline: Create Problem Report" und Gripline_Report.zip auf
GitHub unter "Issues" anhängen.
