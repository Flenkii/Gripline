#ifndef FH2_MOD_PARTNERS_H
#define FH2_MOD_PARTNERS_H
#include <wchar.h>

/* V7.2: other mods that already do something Gripline does. When one of them is installed and has that
   part switched on, Gripline leaves it to that mod and switches its own off (Marvin 14.11.2026), so nothing
   happens twice. [Gripline] YieldToMods=0 in Gripline.ini (F8 > Settings) keeps Gripline's own parts on.

   FH2 Mouse & Keyboard (qiangqiang101/FH2Mouse): keyboard driving, mouse in the menus, mouse look
   while driving. Its own switches are read from FH2Mouse\FH2Mouse.ini: [Mouse] Enabled (the whole mod),
   [Keyboard] Enabled (its keys), [Mouse] FreeLook (its mouse look). */

/* Asks the FH2 Mod Platform for a registered mod's version (NULL = not installed). */
typedef const char *(*ModVersionFn)(const char *name);

/* From fh2mod_init, before the keyboard is armed: which partner mods are there and what they do.
   azimuth_dir = the folder with azimuth.exe; version_of may be NULL (older platform). */
void mod_partners_detect(const wchar_t *azimuth_dir, ModVersionFn version_of);

/* Game start: remembers the game (card language) and announces once (log + a card after the first
   picture) what Gripline left to which mod. */
void mod_partners_start(unsigned char *game_image);

/* [Gripline] YieldToMods: 1 = let partner mods take over (default), 0 = Gripline keeps its own. */
int mod_partners_yield(void);
void mod_partners_remember_yield(int yield);

/* 1 when Gripline leaves this part to another mod (and its own stays off). */
int mod_partners_takes_keyboard(void);
int mod_partners_takes_menu_mouse(void);
int mod_partners_takes_drive_look(void);

/* Name of the mod that took a part ("FH2 Mouse & Keyboard"), for menu rows. */
const wchar_t *mod_partners_mouse_name(void);

/* Problem report lines ("FH2 Mouse & Keyboard 1.2.0: keyboard, menu mouse, mouse look -> Gripline's off").
   Returns the length written (out is zero-terminated). */
unsigned mod_partners_report(char *out, unsigned size);

#endif
