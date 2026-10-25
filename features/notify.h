#ifndef FH2_NOTIFY_H
#define FH2_NOTIFY_H

/* Gripline notifications: a small card with the Gripline logo at the top right of the game
   picture (drawn into the game's own swap chain, so it also shows in fullscreen). Fades out
   after a few seconds, several stack, never takes the focus. Every message also goes to
   FH2_ExtraOptions.log. Switch: FH2_ExtraOptions.ini [Gripline] Notifications=1 / in-game menu. */

/* UTF-8 text, one or two lines ("\n"). Safe from any thread. */
void notify_show(const char *text);

/* For steps of one task (the wheel setup): when the newest card has the same tag, its text is
   replaced in place instead of stacking a new card; it stays hold_ms from now. */
void notify_show_step(const char *text, unsigned tag, unsigned hold_ms);

/* Same, but for messages from game start: kept until the game has shown its first picture
   (a few seconds), then shown in the given order. Safe from any thread. */
void notify_show_deferred(const char *text);

/* Same, but the text is made when it is time to show it (a few seconds after the first
   picture): make() fills out (UTF-8, may use "\n") and returns 1, or returns 0 for no card. */
typedef int (*NotifyTextFn)(char *out, unsigned size);
void notify_show_deferred_call(NotifyTextFn make);

/* Installs the swap-chain hook (from the Azimuth loader, before the game creates its
   swap chain). host = azimuth.exe image. Returns 1 when hooked. */
int notify_install(unsigned char *host);

/* On/off (menu, INI). Off hides cards at once; messages are still logged. */
void notify_set_enabled(int enabled);
int notify_enabled(void);

#endif
