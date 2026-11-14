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
    KEYBOARD_HOLD_CRUISE, /* CruiseControl (on/off), cruise.c */
    KEYBOARD_HOLD_WALK,   /* Walk (out of / into the car), walking.h */
    KEYBOARD_HOLD_SUMMON, /* Summon (while walking), summon.h */
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
/* 1 = the game, noclip and freecam get no key (menu open); 0 = keys held now stay ignored until released */
void keyboard_block_input(int blocked);
void keyboard_key_list_restore(void); /* after the menu: key list (F5) again if it was on, else nothing */
unsigned char *keyboard_game_image(void);
size_t keyboard_start_keys(char *out, size_t capacity); /* start card line */

/* game action -> controller input of the active controller layout (wheel_buttons.c) */
int controller_action_input(unsigned action);
int wheel_buttons_install(unsigned char *game_image);
/* Mouse steering (V7): FH2_Keyboard.ini [Keyboard] MouseSteering, 0 = off, 1..100 sensitivity */
int keyboard_mouse_steering(void);
void keyboard_set_mouse_steering(int sensitivity);
/* [Keyboard] MouseCenter: seconds until the wheel turns back straight when the mouse rests, 0 = never */
/* [Keyboard] MouseDeadZone: 0..60 percent */
int keyboard_mouse_dead_zone(void);
void keyboard_set_mouse_dead_zone(int percent);
/* V7.1 walking: mouse movement (counts * WalkMouseLook %) since the last call; 0 = no reading */
int keyboard_walk_mouse(float *dx, float *dy);
/* V7.1 mouse look in the car: same, DriveMouseLook %; 0 while mouse steering is on */
int keyboard_drive_mouse(float *dx, float *dy);
/* V7.1 mouse in the menus: the Windows cursor in client pixels, buttons (1 left, 2 right, 4 middle), wheel,
   the client size and whether Windows shows its cursor; 0 while driving, unfocused, outside the window or MenuMouse=0 */
int keyboard_menu_mouse(int *x, int *y, int *buttons, int *wheel, int *client_w, int *client_h, int *system_shown);
/* V7.1 menu mouse: one short press of a controller input (wheel_buttons.c numbering: 0 dpad up, 1 dpad down,
   8 LB, 9 RB, 12 A, 13 B), queued; safe from any thread */
void keyboard_menu_pulse(unsigned input);
int keyboard_mouse_center(void);
void keyboard_set_mouse_center(int seconds);
/* V7.1 mouse look sensitivity in percent (0 = off, max 300): walking (WalkMouseLook) and in the car
   (DriveMouseLook), FH2_Keyboard.ini; the F8 menu changes them, they work at once */
int keyboard_walk_mouse_look(void);
void keyboard_set_walk_mouse_look(int percent);
int keyboard_drive_mouse_look(void);
void keyboard_set_drive_mouse_look(int percent);
/* V7.1: [Keyboard] MenuMouse - mouse in the game's menus: 1 = on (default), 2 = pointer only once the mouse moves
   (not when a menu opens), 0 = off (no pointer, no clicks, no wheel) */
#define MENU_MOUSE_OFF 0
#define MENU_MOUSE_ON 1
#define MENU_MOUSE_MOVED 2
int keyboard_menu_mouse_mode(void);
void keyboard_set_menu_mouse_mode(int mode);

#endif
