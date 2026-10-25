#ifndef FH2_WHEEL_BUTTONS_H
#define FH2_WHEEL_BUTTONS_H
#include "extra_options.h"

/* Free wheel buttons (e.g. RSB, LSB, Xbox button on the G920) can trigger game functions.

   How the game sees a wheel: FH2 turns the IWheel reading into an XInput-like state
   (A, B, X, Y, D-pad, Menu, View, paddles as LB/RB) and maps it through the WheelLayouts
   table of gamedb. The thumbstick buttons and the left stick are always zero for a wheel.

   - Functions that the active wheel layout already has on a button (camera, rewind,
     telemetry, photo mode, radio, handbrake, clutch, gears) are triggered by setting that
     button in the wheel report - no game change needed.
   - "Look back", "look left" and "look right" are not on any wheel button. For them the
     wheel's empty right-stick button (RS) and left-stick Y axis are filled in by two tiny
     game hooks, and after the game loads a wheel layout RS is bound to LookStraightBack and
     the left stick Y axis (bound to an unused handbrake) to LookLeft / LookRight. */

enum {
    WHEEL_FUNCTION_OFF,
    WHEEL_FUNCTION_CAMERA,
    WHEEL_FUNCTION_LOOK_BACK,
    WHEEL_FUNCTION_LOOK_LEFT,
    WHEEL_FUNCTION_LOOK_RIGHT,
    WHEEL_FUNCTION_REWIND,
    WHEEL_FUNCTION_TELEMETRY,
    WHEEL_FUNCTION_PHOTO,
    WHEEL_FUNCTION_RADIO_PREVIOUS,
    WHEEL_FUNCTION_RADIO_NEXT,
    WHEEL_FUNCTION_HANDBRAKE,
    WHEEL_FUNCTION_CLUTCH,
    WHEEL_FUNCTION_SHIFT_UP,
    WHEEL_FUNCTION_SHIFT_DOWN,
    WHEEL_FUNCTION_NITRO, /* NitroSystem, see nitro_link.h - no game action */
    WHEEL_FUNCTION_COCKPIT_VIEW, /* second cockpit view, see cockpit_view.h - no game action */
    WHEEL_FUNCTION_CREATOR, /* creator mode, see creator_mode.h - no game action */
    WHEEL_FUNCTION_COUNT
};

#define WHEEL_EXTRA_BUTTONS 3

/* Installs the two state hooks and the layout hook. Returns 1 when all were installed. */
int wheel_buttons_install(unsigned char *game_image);

/* Poll thread: functions currently held (bit per WHEEL_FUNCTION_*). Returns the extra
   report bits for byte 0 / byte 1 of the wheel report. */
void wheel_buttons_update(unsigned held_functions, unsigned char *byte0, unsigned char *byte1);

const wchar_t *wheel_function_name(unsigned char *game_image, unsigned function);

/* Controller (gamepad) layout of the game: input that triggers a game action (-1 = none).
   Layout 0 until the game loads the player's layout. Used by the keyboard. */
int controller_action_input(unsigned action);

/* For the second cockpit view: counts the game's reads of a wheel state (so a state read can be told
   apart from a gamepad read), and the wheel layout's input of an action (-1 = unknown). */
unsigned wheel_buttons_wheel_reads(void);
int wheel_buttons_action_input(unsigned action);
#endif
