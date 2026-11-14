#ifndef FH2_PAUSE_TILE_H
#define FH2_PAUSE_TILE_H
#include <stddef.h>
/* V7.2: a Gripline tile in the pause menu, CARS tab, beside TUNE CAR (Marvin 14.11.2026).

   The pause menu comes from UI\Scenes\Anthem\A_PauseMenu.xaml, which the FH2 Mod Platform writes
   to mediaoverride (its MODS tab). Gripline leaves that file alone: when the game (through Azimuth)
   opens it, Gripline hands it a copy with its own tile added (TUNE CAR becomes 1x1, Gripline 1x1
   beside it). The Settings page stays untouched (FH2 Mouse & Keyboard puts its tile there). The tile
   is wired where the game wires the pause menu tiles (the platform shares that hook) and opens
   Gripline's own page: a show_hud_options scene (as the platform's and FH2 Mouse's pages) with
   Gripline's XAML, reached through the "expansion_packs" exit (Marketplace, unused under the MODS tab). (First try tile1 sat beside HUD on Settings: FH2 Mouse then found the page changed and
   left its own tile out.) */

/* Before the game opens its files (fh2mod_init): Azimuth's CreateFileW import.
   mount_dir = the game's Mount folder (the copy goes to Mount\gripline_ui). Returns 1 if hooked. */
int pause_tile_arm(unsigned char *host, const wchar_t *mount_dir);

/* After wait_for_code: wires the tile and its page. hook = the platform's hook_function (not switched on yet,
   the caller switches the hooks on after this returns); media_dir = Gripline's media folder (the page file
   goes to UI\Scenes\Anthem\A_GriplinePage.xaml there). Returns 1 when wired. */
typedef void *(*PauseTileHookFn)(const char *name, void *target, void *detour, void **original);
int pause_tile_start(unsigned char *game, const wchar_t *media_dir, PauseTileHookFn hook);

/* The text change itself (testable): src (n bytes) -> out. Returns the new size, 0 when the page
   does not look as expected (nothing changed then). */
size_t pause_tile_patch(const char *src, size_t n, char *out, size_t capacity);
/* stateflow\pause.xml: the "expansion_packs" exit leads to Gripline's page node. */
size_t pause_tile_patch_flow(const char *src, size_t n, char *out, size_t capacity);
/* Gripline's page (XAML) from the F8 pages as they are now; returns its size, 0 if too big. */
size_t pause_tile_build_page(void);
const char *pause_tile_page_text(void);
#endif
