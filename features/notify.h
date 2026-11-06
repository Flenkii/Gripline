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
/* V7 walking: darkens the whole game picture, 0 = off .. 1000 = black, reached smoothly in ms
   (the fade when getting out and in). Shown even with cards switched off. Safe from any thread. */
void notify_fade_black(int permille, unsigned ms);

/* Card colour (menu, INI [Gripline] CardColor): bar, logo and title. Safe from any thread. */
enum {
    NOTIFY_COLOR_BLUE, /* original */
    NOTIFY_COLOR_RED, NOTIFY_COLOR_GREEN, NOTIFY_COLOR_PURPLE, NOTIFY_COLOR_ORANGE,
    NOTIFY_COLOR_YELLOW, NOTIFY_COLOR_PINK, NOTIFY_COLOR_WHITE,
    NOTIFY_COLOR_BLACK, /* V7: a very dark grey, white text */
    NOTIFY_COLOR_RAINBOW, /* V7: a smooth fade through all colours; only as the setting, warnings keep theirs */
    NOTIFY_COLOR_COUNT
};
void notify_set_color(int index);
/* Card design (F8 menu, INI [Gripline] CardStyle): Classic = V6 look, Enhanced = glow and glass (V7),
   Overdrive = carbon, cut corners, racing stripes. Safe from any thread. */
enum { NOTIFY_STYLE_CLASSIC, NOTIFY_STYLE_ENHANCED, NOTIFY_STYLE_OVERDRIVE, NOTIFY_STYLE_COUNT };
void notify_set_style(int style);
int notify_style(void);

/* Card groups a player can hide (F8 > Karten & Hinweise, INI [Gripline] CardsHidden = bits, V7).
   Warnings (red/orange), the F8 menu's own answers and key presses (noclip on/off, nitro...) always show. */
enum {
    NOTIFY_KIND_START,      /* start cards: Gripline active + keys, F8 hint, what's new, NitroSystem found */
    NOTIFY_KIND_RECORDS,    /* "NEW RECORD" */
    NOTIFY_KIND_MILESTONES, /* logbook: "1000 km driven" */
    NOTIFY_KIND_TRIP,       /* trip summary in the pause menu */
    NOTIFY_KIND_LIVE,       /* live cards: noclip/freecam height and speed, mouse steering */
    NOTIFY_KIND_WHEEL,      /* wheel connected/disconnected, nitro off online / on again */
    NOTIFY_KIND_CONFIRM,    /* "setting changed" cards of the Mods page */
    NOTIFY_KIND_COUNT
};
#define NOTIFY_KINDS_ALL ((1 << NOTIFY_KIND_COUNT) - 1)
/* presets: everything / few (records + wheel stay) / warnings only */
#define NOTIFY_PRESET_FEW ((1 << NOTIFY_KIND_START) | (1 << NOTIFY_KIND_MILESTONES) | (1 << NOTIFY_KIND_TRIP) | \
                           (1 << NOTIFY_KIND_LIVE) | (1 << NOTIFY_KIND_CONFIRM))
void notify_set_hidden_kinds(int mask);
int notify_hidden_kinds(void);
int notify_kind_on(int kind);

/* Key list panel (keyboard_pad.c, key Keybinds): a tall card at the right edge of the picture,
   one row per key, shown until it is switched off; cards move to its left meanwhile. It is only
   drawn, the game keeps every input. count 0 hides it. Safe from any thread. */
#define NOTIFY_PANEL_ROWS 46
enum {
    NOTIFY_ROW_NORMAL,   /* label left, keys right */
    NOTIFY_ROW_SELECTED, /* key menu: the row the arrows stand on (highlighted) */
    NOTIFY_ROW_INFO,     /* key menu: dim label, value in the card colour, not selectable */
    NOTIFY_ROW_HINT,     /* key menu: small grey help line (label left, keys right) */
    NOTIFY_ROW_SECTION,  /* key menu (V7 overhaul): a section head in small capitals with a rule, not selectable */
    NOTIFY_ROW_HERO,     /* telemetry (V7 overhaul): big value over two rows; the next row must be a SPACER */
    NOTIFY_ROW_SPACER,   /* nothing drawn (room for the HERO above) */
    NOTIFY_ROW_GOOD,     /* telemetry: value on a green status pill */
    NOTIFY_ROW_WARN,     /* telemetry: value on an orange status pill */
    NOTIFY_ROW_BAD,      /* telemetry: value on a red status pill */
};
typedef struct {
    wchar_t label[32], keys[32];
    int style; /* NOTIFY_ROW_* */
} NotifyPanelRow;
void notify_panel_set(const wchar_t *title, const NotifyPanelRow *rows, unsigned count);
/* Same panel for the key menu (key_menu.h): at_top 1 = at the top right like the cards, not centred. */
void notify_panel_show(const wchar_t *title, const NotifyPanelRow *rows, unsigned count, int at_top);
/* panel that gives way to the others (telemetry): see notify.c */
unsigned notify_panel_show_owned(const wchar_t *title, const NotifyPanelRow *rows, unsigned count, unsigned owner);
void notify_panel_clear_owned(unsigned owner);
/* V7: where the telemetry panel sits (NOTIFY_TELE_*), how see-through (opacity 20..100 %) and how big (50..200 %) */
enum { NOTIFY_TELE_RIGHT_MIDDLE, NOTIFY_TELE_RIGHT_TOP, NOTIFY_TELE_RIGHT_BOTTOM,
       NOTIFY_TELE_LEFT_TOP, NOTIFY_TELE_LEFT_MIDDLE, NOTIFY_TELE_LEFT_BOTTOM, NOTIFY_TELE_POS_COUNT };
void notify_set_telemetry_look(int position, int opacity, int size);
int notify_telemetry_position(void);
int notify_telemetry_opacity(void);
int notify_telemetry_size(void);

#endif
