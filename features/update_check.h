#ifndef FH2_UPDATE_CHECK_H
#define FH2_UPDATE_CHECK_H

/* Update notice (V7): at game start Gripline asks GitHub once for its newest release
   (api.github.com/repos/Flenkii/Gripline/releases/latest, HTTPS, read only, a few KB) and shows
   an orange card at the top right when that release is newer than this build:
   "Gripline V7.1 ist draußen / Bitte die neue Version nutzen, V7 ist veraltet".
   - winhttp.dll is loaded at run time from System32 (no new imports in Gripline.dll),
   - own thread with short timeouts: offline, blocked or slow = no card, the game never waits,
   - nothing is downloaded or run, only the release name is read,
   - switch: Gripline.ini [Gripline] UpdateCheck=0 turns it off. */

/* game_image for the language; enabled = Gripline.ini UpdateCheck. */
void update_check_start(unsigned char *game_image, int enabled);

/* UpdateCheck as set now (F8 > Einstellungen changes it for the next start) */
int update_check_enabled(void);
void update_check_remember(int on);

#endif
