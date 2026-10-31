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
/* a card in a fixed colour (NOTIFY_COLOR_*, whatever the setting says); badge (UTF-8, may be NULL) goes after
   GRIPLINE in the title line, e.g. "WARNUNG" on red warning cards; long_hold = as long as a start card */
void notify_show_tinted(const char *text, int color, const char *badge, int long_hold);

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
/* Card design (F8 menu, INI [Gripline] CardStyle): Classic = V6 look, Enhanced = glow and glass (V7),
   Overdrive = carbon, cut corners, racing stripes. Safe from any thread. */
enum { NOTIFY_STYLE_CLASSIC, NOTIFY_STYLE_ENHANCED, NOTIFY_STYLE_OVERDRIVE, NOTIFY_STYLE_COUNT };
void notify_set_style(int style);
int notify_style(void);
int notify_color(void);

/* Key list panel (keyboard_pad.c, key Keybinds): a tall card at the right edge of the picture,
   one row per key, shown until it is switched off; cards move to its left meanwhile. It is only
   drawn, the game keeps every input. count 0 hides it. Safe from any thread. */
#define NOTIFY_PANEL_ROWS 44
enum {
    NOTIFY_ROW_NORMAL,   /* label left, keys right */
    NOTIFY_ROW_SELECTED, /* key menu: the row the arrows stand on (highlighted) */
    NOTIFY_ROW_INFO,     /* key menu: dim label, value in the card colour, not selectable */
    NOTIFY_ROW_HINT,     /* key menu: small grey help line (label left, keys right) */
};
typedef struct {
    wchar_t label[32], keys[32];
    int style; /* NOTIFY_ROW_* */
} NotifyPanelRow;
void notify_panel_set(const wchar_t *title, const NotifyPanelRow *rows, unsigned count);
/* Same panel for the key menu (key_menu.h): at_top 1 = at the top right like the cards, not centred. */
void notify_panel_show(const wchar_t *title, const NotifyPanelRow *rows, unsigned count, int at_top);
int notify_panel_visible(void);

#endif
