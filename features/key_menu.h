#ifndef FH2_KEY_MENU_H
#define FH2_KEY_MENU_H
/* Gripline key menu (V7): changes the key bindings of FH2_Keyboard.ini while playing.

   Key KeyMenu (F8) opens it as a card at the top right (notify.h panel, same look as the
   cards). Fixed keys inside: Up/Down move, Enter chooses, Esc or Backspace go back one step,
   at the first page they close it; KeyMenu closes it from anywhere. While it is open the
   game, noclip and freecam get no key from the keyboard (controllers keep working). A change
   applies at once, is saved into FH2_Keyboard.ini and shows in the key list (F5). */

/* Starts the menu's own thread (after the keyboard settings were loaded). */
void key_menu_start(void);
/* Controller (noclip.c's gamepad hook, game input thread; gamepad = XINPUT_GAMEPAD): left stick click
   held + D-pad right opens/closes the menu; inside D-pad or left stick up/down move, A chooses, B goes
   back. Returns 1 while the menu has the controller: the reading is then emptied for the game. */
int key_menu_pad_input(unsigned char *gamepad);
/* V7.2 (ANNA menu): opens the menu (or closes it), as the F8 key does. Any thread. */
void key_menu_request_toggle(void);
/* NotifyTextFn for notify_show_deferred_call: own start card naming the menu key */
int key_menu_start_card(char *out, unsigned size);

/* V7.1: the F8 pages as pages under MODS > YOUR MODS > Gripline (Installed mods before platform 2.9.0) (texts in the game language). */
/* V7.2 FH2 ANNA: turn signal arrows on / off (saved, card) */
void key_menu_blinker_toggle(void);
#define KEY_MENU_MIRROR_PAGES 7
#define KEY_MENU_MIRROR_KEY 12
enum { KEY_MENU_MIRROR_NORMAL, KEY_MENU_MIRROR_DIM, KEY_MENU_MIRROR_HEADING };
typedef struct {
    char label[128], value[96]; /* UTF-8 */
    char key[KEY_MENU_MIRROR_KEY]; /* empty: nothing to choose */
    int style;                     /* KEY_MENU_MIRROR_* */
    int flags;                     /* KEY_MENU_MIRROR_FLAG_* (what F8 marks with a sign the game font lacks) */
} KeyMenuMirrorRow;
#define KEY_MENU_MIRROR_FLAG_ACTIVE 1  /* the chosen one of a list ("✓ active") */
#define KEY_MENU_MIRROR_FLAG_CHOICE 2  /* one of a list: active, "default" or no value */
#define KEY_MENU_MIRROR_FLAG_ON 4      /* a switch that is on ("✓ on", "● on") */
#define KEY_MENU_MIRROR_FLAG_SUBPAGE 8 /* opens a sub-page in F8 ("›") */
const char *key_menu_mirror_title(unsigned page);       /* page title (English, fixed at the start) */
const char *key_menu_mirror_label(unsigned page);       /* its row on Gripline's own page */
const char *key_menu_mirror_description(unsigned page);
/* V7.2 Gripline's page in the pause menu: its pages in order, their names in the game language, and
   their rows with the parts only it shows (card colour, dashboard cam, world, traffic) */
unsigned key_menu_tile_page_count(void);
unsigned key_menu_tile_page(unsigned n);
int key_menu_mirror_on_mods(unsigned page); /* 0: a page only Gripline's page shows */
const char *key_menu_mirror_label_shown(unsigned page);
const char *key_menu_mirror_description_shown(unsigned page);
unsigned key_menu_mirror_tile_rows(unsigned page, KeyMenuMirrorRow *out, unsigned capacity);
int key_menu_mirror_german(void); /* the game shows German (the mirror texts are German then) */
/* rows of `page` as they are now; returns how many */
unsigned key_menu_mirror_rows(unsigned page, KeyMenuMirrorRow *out, unsigned capacity);
/* A on a row: does what Enter does in F8 (saves, card). Returns 1 if the row was found. */
int key_menu_mirror_choose(const char *key);
/* V7.2: the values a row that A steps through can have (for a left/right choice); 0 if it isn't one */
#define KEY_MENU_MIRROR_VALUE 48
/* one value left (-1) / right (+1) like ◄ ► in F8; 0 if the row has no such step */
int key_menu_mirror_step(const char *key, int delta);
unsigned key_menu_mirror_values(const char *key, char (*out)[KEY_MENU_MIRROR_VALUE], unsigned cap, int *current);
#endif
