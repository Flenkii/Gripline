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
int key_menu_open(void);
#endif
