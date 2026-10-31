#ifndef FH2_KEYBOARD_PAD_H
#define FH2_KEYBOARD_PAD_H
/* Keyboard as a gamepad (unofficial add-on).

   Azimuth reads controllers through Windows.Gaming.Input (WGI) and hands them to FH2 as Xbox
   gamepads. This module hooks Azimuth's RoGetActivationFactory import: when Azimuth asks for
   Windows.Gaming.Input.Gamepad it gets ONE virtual gamepad. Its reading is the combination
   of every real gamepad (buttons, triggers, sticks, rumble pass through unchanged) and the
   keyboard. So the game always sees exactly one stable controller: keyboard-only
   players need no gamepad, gamepad players notice no difference, plugging a pad in or out
   while playing works.

   Keys are bound to game functions (FH5-like defaults in FH2_Keyboard.ini). Driving functions
   follow the controller layout chosen in the game (A/B/X/Y layouts), menu keys are fixed
   buttons (Enter = A, Backspace = B, Esc = Menu). The arrow keys move the left stick in menus
   and the camera (right stick) while driving. */
#include "gripline.h"

enum {
    KEY_SETTING_ENABLED,     /* Aktiv                0/1 (next game start) */
    KEY_SETTING_STEER,       /* Lenkzeit             ms to full lock */
    KEY_SETTING_RETURN,      /* Rueckstellzeit       ms back to the centre */
    KEY_SETTING_PEDAL,       /* Pedalzeit            ms to full gas / brake */
    KEY_SETTING_COUNT
};

/* Process start (Azimuth adapter): hooks Azimuth's import. settings_directory holds
   FH2_Keyboard.ini. Returns 1 when the hook is in place. */
int keyboard_pad_arm(unsigned char *host_image, const wchar_t *settings_directory);
int keyboard_pad_late(void); /* after Azimuth already asked for its gamepads (plugin build) */
/* Game image mapped: controller layouts are followed from now on. */
void keyboard_pad_game_start(unsigned char *game_image, const wchar_t *settings_directory);

int keyboard_setting(unsigned id);
void keyboard_set_setting(unsigned id, int value);
const wchar_t *keyboard_ini_path(void);

/* Noclip keys (noclip.h): 1 while held and the game window has the focus. */
enum {
    KEYBOARD_HOLD_FORWARD, /* Throttle */
    KEYBOARD_HOLD_BACK,    /* Brake */
    KEYBOARD_HOLD_LEFT,    /* SteerLeft */
    KEYBOARD_HOLD_RIGHT,   /* SteerRight */
    KEYBOARD_HOLD_UP,      /* NoclipUp */
    KEYBOARD_HOLD_DOWN,    /* NoclipDown */
    KEYBOARD_HOLD_FAST,    /* NoclipFast */
    KEYBOARD_HOLD_NOCLIP,  /* Noclip (on/off) */
    KEYBOARD_HOLD_FREECAM, /* Freecam (on/off) */
    KEYBOARD_HOLD_LOOK_UP, /* ArrowUp ... ArrowRight: freecam look */
    KEYBOARD_HOLD_LOOK_DOWN,
    KEYBOARD_HOLD_LOOK_LEFT,
    KEYBOARD_HOLD_LOOK_RIGHT,
    KEYBOARD_HOLD_COUNT
};
int keyboard_held(unsigned which);
/* 1 while the world simulation runs (not paused, no menu or loading screen). */
int game_simulation_running(void);

/* Key menu (key_menu.c): the bindings of FH2_Keyboard.ini, read and changed while playing. */
unsigned keyboard_binding_count(void);
int keyboard_binding_find(const wchar_t *key); /* index of an INI key name, -1 if unknown */
const wchar_t *keyboard_binding_key(unsigned index); /* INI key name, e.g. L"Throttle" */
const wchar_t *keyboard_binding_label(unsigned index, int german); /* as in the key list (F5) */
void keyboard_binding_vks(unsigned index, unsigned char vk[4]);
void keyboard_binding_default_vks(unsigned index, unsigned char vk[4]);
void keyboard_binding_set(unsigned index, const unsigned char vk[4]); /* at once, the INI is not written */
void keyboard_keys_text(const unsigned char vk[4], int german, wchar_t *out, unsigned capacity);
const char *keyboard_key_ini_name(unsigned char vk, char buffer[8]);
int keyboard_key_down(unsigned char vk); /* held and the game window has the focus */
int keyboard_game_focused(void);
/* 1 = the game, noclip and freecam get no key (menu open); 0 = keys held now stay ignored until released */
void keyboard_block_input(int blocked);
int keyboard_input_blocked(void);
void keyboard_key_list_restore(void); /* after the menu: key list (F5) again if it was on, else nothing */
unsigned char *keyboard_game_image(void);
size_t keyboard_start_keys(char *out, size_t capacity); /* start card line */

/* game action -> controller input of the active controller layout (wheel_buttons.c) */
int controller_action_input(unsigned action);
int wheel_buttons_install(unsigned char *game_image);
#endif
