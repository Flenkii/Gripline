GRIPLINE V5 – wheel, keyboard, traffic & weather for Forza Horizon 2 (Azimuth)
=============================================================================
Based on Extra Options 0.2.98 by RokkuDayo.

Source code: https://github.com/Flenkii/Gripline
Original:    https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions (GPL-3.0)

No game files and no Azimuth included. Never paste commands into PowerShell to install
this mod – anything like that is a scam.
Not supported by the Extra Options author or the Azimuth developer.


1. INSTALLATION
---------------
Copy everything from the "Azimuth" folder into your Azimuth folder (next to azimuth.exe):
winmm.dll, FH2_ExtraOptions.ini and the folder fh2-mods (with Gripline.dll).
Back up your old winmm.dll and FH2_ExtraOptions.ini first.
Updating: replacing winmm.dll and fh2-mods\Gripline.dll is enough.

On the first start the mod creates FH2_Wheel.ini (wheel) and FH2_Keyboard.ini (keyboard).
Old settings files (FH2_Lenkrad.ini / FH2_Tastatur.ini from fork1-fork3) are taken over
automatically; the old files go into FH2_Settings_Backup.

Settings in the game:
  - With the FH2 Mod Platform (comes with FH2MP): pause menu > the blue GRIPLINE tile.
  - Without it: Options > HUD > bottom.

Without any further steps everything is automatic:
  - The controller works as always.
  - The keyboard works right away, bindings: see Keybindings.txt (FH2_Keyboard.ini).
  - A Logitech G920 is detected, with the game's original force feedback like on the Xbox.
    Works even without Microsoft.Xbox.Input.Wheel.dll.
  - While a wheel is plugged in, the game drives with the wheel only. For keyboard or
    controller: "Wheel: Use Wheel In Game" = Off and restart (or unplug the wheel).
  - G920 brake pedal too stiff? "Wheel: Clutch Pedal as Brake" = On. Idea: Lancaster.
  - Gripline cards at the top right of the picture tell you what happened (also in
    fullscreen). Switch: "Gripline: Notifications".


1b. PLAYING ONLINE WITH FH2MP
-----------------------------
Gripline's winmm.dll stays next to azimuth.exe, FH2MP comes as a Mod Platform mod
(fh2-mods\FH2MP.dll):
  - Install FH2MP with its launcher (it brings the FH2 Mod Platform, d3dcompiler_47.dll).
  - In the FH2MP launcher settings turn OFF "Install FH2 Extra Options if it is missing"
    (otherwise it moves Gripline into fh2extra and puts the original Extra Options back).
  - Copy Gripline's files as in 1.
Online, traffic and weather come from the session (your settings only apply offline).
Wheel, keyboard and force feedback work online. Nitro is switched off online
automatically (a card tells you) and comes back offline.
Old layout (Gripline in fh2extra\): not needed any more, the FH2MP launcher moves it.


2. NEW IN V5
------------
  - GRIPLINE tile in the pause menu (needs the FH2 Mod Platform; fh2-mods\Gripline.dll).
  - Gripline notification cards with the logo, drawn into the game picture.
  - Nitro on the keyboard (N) and on a wheel button (function 14 "Nitro") for the mod
    NitroSystem by qiangqiang101 (https://github.com/qiangqiang101/NitroSystem) – install
    NitroSystem yourself, Gripline only presses its nitro button. Online: off.
    Note: NitroSystem's "Backfire on All Cars" lets the Mod Platform rebuild
    Mount\mediaoverride\Stripped\gamedbRC.slt from the original database. If another mod
    of yours keeps its database there, leave that option off.
  - Full steering animation in the cockpit (off by default): hands hidden, the steering
    wheel turns as far as your real one (Rotation in FH2_Wheel.ini). Switch:
    "Cockpit: Full Steering Animation", applies after a restart. The first start after
    switching it on (or changing the rotation) takes a little longer: Gripline makes the
    files once into Mount\gripline_cockpit (game files are never changed; delete that
    folder any time).
  - Works well with fh2wheels by conner-mcvay (rims, tire widths, ride height, camber):
    https://github.com/conner-mcvay/fh2-mods – its installer finds Gripline as
    "Extra Options".


3. OPTIONAL: READY-MADE PROFILES  (folder Optional\Profiles)
-----------------------------------------------------------
Only needed if you don't want to use the in-game menu. Copy both INI files of a profile
into your Azimuth folder (replaces your FH2_Wheel.ini and FH2_Keyboard.ini):

  Controller   – controller only: wheel and keyboard off
  Keyboard     – keyboard (+ controller), wheel off
  Wheel        – wheel with the game's original force feedback, keyboard stays on (default)
  Wheel_ClutchAsBrake – like Wheel, but the clutch pedal brakes (stiff G920 brake pedal)


PROBLEMS? Please report them on GitHub Issues:
  https://github.com/Flenkii/Gripline/issues
Attach these files from your Azimuth folder (next to azimuth.exe):
  - FH2_ExtraOptions.log                    (always)
  - FH2_Wheel.log                           (wheel problems)
  - FH2_Wheel.ini and FH2_Keyboard.ini      (your settings)
  - fh2-mods\platform.log                   (with FH2MP / the Mod Platform)
Start the game, reproduce the problem, close the game, then attach the files.

Known issues:
  - Traffic settings only apply after restarting Azimuth.
  - Fog only exists on Storm Island, everywhere else it is sunny.
  - No H-pattern shifter: FH2 only knows shift up / shift down (paddles or E/Q).

Credits: RokkuDayo (Extra Options), Lancaster (clutch as brake), Trinculo54 (FH2 Mod
Platform API, MIT), qiangqiang101 (NitroSystem), conner-mcvay (fh2wheels).

----------------------------------------------------------------------------------------
DEUTSCH (kurz)
Alles aus "Azimuth" neben azimuth.exe kopieren (winmm.dll, FH2_ExtraOptions.ini, Ordner
fh2-mods). Vorher die alte winmm.dll sichern.
Mit FH2MP (online): FH2MP über seinen Launcher installieren und dort "Install FH2 Extra
Options if it is missing" ausschalten. Gripline bleibt neben azimuth.exe. Online ist Nitro
automatisch aus.
Einstellungen: im Pausemenü die blaue Kachel GRIPLINE (mit Mod Platform/FH2MP), sonst
Optionen > HUD > ganz unten, oder FH2_Wheel.ini / FH2_Keyboard.ini / FH2_ExtraOptions.ini
[Gripline].
Neu in V5: Gripline-Kachel, Benachrichtigungen im Bild, Nitro auf N bzw. Lenkradtaste
(Funktion 14, braucht NitroSystem), volle Lenkrad-Animation im Cockpit (Standard aus,
wirkt nach Neustart; der erste Start danach dauert etwas länger), passt zu fh2wheels.
Probleme bitte auf GitHub unter "Issues" melden und FH2_ExtraOptions.log, FH2_Wheel.log,
FH2_Wheel.ini, FH2_Keyboard.ini und fh2-mods\platform.log anhängen.
