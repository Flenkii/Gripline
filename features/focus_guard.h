#ifndef FH2_FOCUS_GUARD_H
#define FH2_FOCUS_GUARD_H

/* Pause menu that opens again and again after the start (Marvin 08.11.2026: "manchmal ... ploppt die
   ganze Zeit das Menü auf, egal wie oft ich Esc drücke", about 1-2 of 10 starts, with or without FH2MP,
   when the mouse was used while the game was still loading).

   Found in azimuth.exe (1.0.x, 08.11.2026): Azimuth hands the game's CoreWindow.Activated event from
   WM_ACTIVATE of its window (class "Azimuth.Game.Win32"). When it shows the window at the start it calls
   SetForegroundWindow once and then queues ONE snapshot "active = GetForegroundWindow() == window".
   Windows refuses SetForegroundWindow while the user is giving input to another window (the mouse), so
   that snapshot can say "inactive" although the window comes to the front. No further WM_ACTIVATE comes
   while the window stays in front, so FH2 keeps thinking it is in the background and opens its pause menu
   again after every Esc (on the Xbox: the game lost the focus). Alt+Tab out and back also ends it.

   The guard: while Azimuth's game window is really in front, Gripline posts it one WM_ACTIVATE (active) -
   once the window has been in front for a moment after it came to the front, and once when the world is
   loaded. Azimuth passes that on as "activated" like a click into the window. Azimuth itself ignores it
   while its own Esc menu is open (it keeps the game paused then), so nothing is lost. Own code. */

void focus_guard_start(void);

#endif
