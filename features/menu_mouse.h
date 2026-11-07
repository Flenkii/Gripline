#ifndef FH2_MENU_MOUSE_H
#define FH2_MENU_MOUSE_H

/* V7.1 mouse pointer in the game's menus (Marvin 07.11.2026: "Esc, Maus erscheint, über die Kachel
   fahren, Linksklick öffnet sie").

   What was found in the game (forza_durango_release_final.exe, 07.11.2026): FH2's menus are built with
   its own XAML engine (AVUI, a WPF-like port). It still has the whole WPF mouse path - MouseInputProvider,
   MouseDevice, hit testing, IsMouseOver, MouseEnter/Leave, MouseLeftButtonDown/Up - but the Xbox game
   never feeds it a mouse report (the only call sends "cancel capture"). Gripline feeds it:
   PresentationSource (of the window the game's WindowLayerManager has active, +0x68 -> +0x260)
   -> its MouseInputProvider (+0x90) -> ReportInput (game+0x149a2f0, WPF HwndMouseInputProvider.ReportInput:
   mode, timestamp, RawMouseActions, point, wheel). The engine itself then hit-tests the menu and raises
   the routed mouse events, like a mouse on the PC version of WPF.

   AVUI state is per thread (TLS), so the reports go in from the game's own UI frame (GameApplication
   vtable slot 9, the one walking.c also chains) and only when that thread has a UI context.
   Gripline draws its own pointer (notify_cursor) - the system cursor is held by Azimuth.

   Uses the real Windows cursor (not held; Azimuth's own menu needs it). Wheel: tabs (LB/RB) over the top part, up/down
   elsewhere; right click = B. FH2_Keyboard.ini [Keyboard] MenuMouse = 1 on (default), 0 off. */

/* chain the UI frame hook (after walking_start, so walking's HUD skip stays inside) */
int menu_mouse_start(unsigned char *game_image);

#endif
