#ifndef FH2_NOTIFY_H
#define FH2_NOTIFY_H

#include <wchar.h>

/* Gripline notifications: a small card with the Gripline logo at the top right of the game
   picture (drawn into the game's own swap chain, so it also shows in fullscreen). Fades out
   after a few seconds, several stack, never takes the focus. Every message also goes to
   Gripline.log. Switch: Gripline.ini [Gripline] Notifications=1 / Mods page. */

/* UTF-8 text, one or two lines ("\n"). Safe from any thread. */
void notify_show(const char *text);

/* For steps of one task (the wheel setup): when the newest card has the same tag, its text is
   replaced in place instead of stacking a new card; it stays hold_ms from now. */
void notify_show_step(const char *text, unsigned tag, unsigned hold_ms);
/* Same, for values updated several times a second (noclip height and speed): not logged. */
void notify_show_live(const char *text, unsigned tag, unsigned hold_ms);

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
int notify_install_late(void); /* after the game made its swap chain (plugin build) */

/* On/off (menu, INI). Off hides cards at once; messages are still logged. */
void notify_set_enabled(int enabled);
int notify_enabled(void);

/* Card colour (menu, INI [Gripline] CardColor): bar, logo and title. Safe from any thread. */
enum {
    NOTIFY_COLOR_BLUE, /* original */
    NOTIFY_COLOR_RED, NOTIFY_COLOR_GREEN, NOTIFY_COLOR_PURPLE, NOTIFY_COLOR_ORANGE,
    NOTIFY_COLOR_YELLOW, NOTIFY_COLOR_PINK, NOTIFY_COLOR_WHITE,
    NOTIFY_COLOR_COUNT
};
void notify_set_color(int index);
int notify_color(void);

/* Key list panel (keyboard_pad.c, key Keybinds): a tall card at the right edge of the picture,
   one row per key, shown until it is switched off; cards move to its left meanwhile. It is only
   drawn, the game keeps every input. count 0 hides it. Safe from any thread. */
#define NOTIFY_PANEL_ROWS 44
typedef struct {
    wchar_t label[32], keys[32];
} NotifyPanelRow;
void notify_panel_set(const wchar_t *title, const NotifyPanelRow *rows, unsigned count);
int notify_panel_visible(void);

#endif
