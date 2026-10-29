GRIPLINE V6 – wheel, keyboard, traffic & weather for Forza Horizon 2 (Azimuth)
=============================================================================
An add-on next to the official Extra Options by RokkuDayo (GPL-3.0).

Source code: https://github.com/Flenkii/Gripline
Extra Options: https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions

No game files, no Azimuth, no Extra Options and no FH2MP included. Never paste commands
into PowerShell to install a mod – anything like that is a scam.
Not supported by the Extra Options author, the FH2MP team or the Azimuth developer.

Made for: Azimuth 1.0.3, Extra Options 0.3.9, FH2 Mod Platform 2.7.4, FH2MP 1.1.2.
Azimuth 0.9.10 and the old Gripline winmm.dll (V5) are not supported any more.


1. INSTALLATION
---------------
1. Install Azimuth and FH2MP with the FH2MP launcher (it brings the official Extra Options
   and the FH2 Mod Platform). Start the game once.
2. Copy the CONTENTS of the folder "Azimuth" from this ZIP next to azimuth.exe:
     fh2-mods\Gripline.dll
     mods\Gripline\plugins\GriplineTick.dll
   Nothing is replaced. Both files are needed (GriplineTick runs weather and time of day).
3. Do NOT replace winmm.dll - it stays the official Extra Options. Still have the
   winmm.dll of Gripline V5? Let the FH2MP launcher put the official one back.
4. Start the game. A card at the top right lists the mods Gripline found.
   Settings: Pause > Settings > MODS.

Updating from V5: the [Traffic] and [Gripline] settings move from FH2_ExtraOptions.ini
to Gripline.ini on the first start (old INI kept as FH2_ExtraOptions.ini.before-gripline).
FH2_Wheel.ini and FH2_Keyboard.ini stay as they are.

Uninstall: delete fh2-mods\Gripline.dll and the folder mods\Gripline.

Online with FH2MP: same files. Online, traffic, weather and time of day come from the
session; nitro is off.


2. WHEEL: AZIMUTH OR GRIPLINE
-----------------------------
Azimuth 1.0.3 can drive a wheel itself (set in its launcher, with its own force
feedback). If Azimuth AND Gripline both use the wheel, steering and force feedback may be
doubled; Gripline shows the card "Wheel: Azimuth and Gripline both on". Choose one:
  - Gripline's wheel (game's own Xbox force feedback, wheel buttons, clutch as brake):
    remove the wheel mapping in the Azimuth launcher.
  - Azimuth's wheel: AzimuthWheel=1 in FH2_Wheel.ini (Gripline leaves the wheel alone).


3. NEW IN V6
------------
  - Runs next to the official Extra Options (FH2MP-compatible, no winmm.dll swap).
  - The steering wheel in the cockpit turns 1:1 with your real wheel (Steering Animation).
  - Cockpit view 2 like in FH5: Tab goes cockpit > cockpit 2 > next, or V (wheel 15).
  - Time of day: Morning, Midday, Sunset, Night or Slow Day (offline, free roam).
  - More Traffic raises the game's own traffic data (applies after a restart).
  - Creator mode: H (wheel 16) hides Gripline's cards; F1 (Extra Options) hides the HUD.
  - Key list (F5), input profiles in the game, wheel setup assistant for other wheels,
    problem report (Gripline_Report.zip), Azimuth Mapper detection, start card.
  - Extra Options' Weather Control plugin wins while it fixes weather or clock.


4. OPTIONAL: READY-MADE PROFILES  (folder Optional\Profiles)
-----------------------------------------------------------
The same profiles can be switched in the game ("Input: Profile"). Without the menu: copy
both INI files of a profile next to azimuth.exe (replaces your FH2_Wheel.ini and
FH2_Keyboard.ini):
  Controller          – controller only: wheel and keyboard off
  Keyboard            – keyboard (+ controller), wheel off
  Wheel               – wheel with the game's force feedback (default)
  Wheel_ClutchAsBrake – like Wheel, but the clutch pedal brakes (stiff G920 brake pedal)


PROBLEMS? https://github.com/Flenkii/Gripline/issues
Pause > Settings > MODS > "Gripline: Create Problem Report", then attach
Gripline_Report.zip (next to azimuth.exe). Nothing is sent anywhere.

Known limits:
  - Traffic, steering animation, input profile and "Use Wheel In Game": restart needed.
  - Fog only on Storm Island. Weather/time of day need GriplineTick.dll.
  - fh2wheels is not loaded by Azimuth 1.0.x.
  - Only tested with a Logitech G920.

Credits: RokkuDayo (Extra Options), Trinculo54 (FH2 Mod Platform API, MIT), Lancaster
(clutch as brake), qiangqiang101 (NitroSystem), bigdamnhero6 (Azimuth Mapper), a YouTube
viewer (cockpit view 2 idea).

----------------------------------------------------------------------------------------
DEUTSCH (kurz)
Inhalt des Ordners "Azimuth" neben azimuth.exe kopieren: fh2-mods\Gripline.dll und
mods\Gripline\plugins\GriplineTick.dll. winmm.dll NICHT ersetzen (bleibt das offizielle
Extra Options). Einstellungen: Pause > Einstellungen > MODS.
Lenkrad: Azimuth 1.0.3 kann das Lenkrad selbst steuern. Nicht beide gleichzeitig: entweder
die Lenkrad-Belegung im Azimuth-Launcher entfernen (Gripline-Lenkrad) oder AzimuthWheel=1
in FH2_Wheel.ini (Azimuth-Lenkrad).
Probleme: im MODS-Menü "Gripline: Create Problem Report" und Gripline_Report.zip auf
GitHub unter "Issues" anhängen.
