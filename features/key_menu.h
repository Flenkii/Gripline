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
/* NotifyTextFn for notify_show_deferred_call: own start card naming the menu key */
int key_menu_start_card(char *out, unsigned size);

/* V7.1: the F8 pages as pages under MODS > Installed mods > Gripline (texts in the game language). */
#define KEY_MENU_MIRROR_PAGES 6
#define KEY_MENU_MIRROR_KEY 12
enum { KEY_MENU_MIRROR_NORMAL, KEY_MENU_MIRROR_DIM, KEY_MENU_MIRROR_HEADING };
typedef struct {
    char label[128], value[96]; /* UTF-8 */
    char key[KEY_MENU_MIRROR_KEY]; /* empty: nothing to choose */
    int style;                     /* KEY_MENU_MIRROR_* */
} KeyMenuMirrorRow;
const char *key_menu_mirror_title(unsigned page);       /* page title (English, fixed at the start) */
const char *key_menu_mirror_label(unsigned page);       /* its row on Gripline's own page */
const char *key_menu_mirror_description(unsigned page);
/* rows of `page` as they are now; returns how many */
unsigned key_menu_mirror_rows(unsigned page, KeyMenuMirrorRow *out, unsigned capacity);
/* A on a row: does what Enter does in F8 (saves, card). Returns 1 if the row was found. */
int key_menu_mirror_choose(const char *key);
#endif
