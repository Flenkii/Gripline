#ifndef FH2_COCKPIT_VIEW_H
#define FH2_COCKPIT_VIEW_H

/* Second cockpit view (like FH5's second interior camera): the driver camera moves forward
   to the gauges, past the steering wheel and hands. It is one more step of the game's
   camera change: cockpit -> dashboard cam -> the next camera. A key / wheel button also switches. */

/* Menu / INI ([Gripline] CockpitViewForward, CockpitViewUp, centimetres). Safe from any thread. */
void cockpit_view_set_offset(int forward_cm, int up_cm);

/* A toggle input (keyboard key or wheel button): switches the view on the press. */
enum { COCKPIT_VIEW_KEYBOARD, COCKPIT_VIEW_WHEEL, COCKPIT_VIEW_ANNA /* V7.2 anna_link.c */ };
void cockpit_view_input(unsigned source, int held);

/* 1 while the second view is on (cockpit.c then plays the steering animation part without the wheel). */
int cockpit_view_second(void);

/* Every frame with the active driver camera (from the FOV hook). */
void cockpit_view_frame(void *camera);

/* Freecam (noclip.h): 1 while the driver camera is active; want_driver(1) presses the camera
   button for the player (for a few seconds) until it is. */
int cockpit_view_driver_active(void);
void cockpit_view_want_driver(int on);

/* V7.1 walking: press the camera button until an outside (follow) camera is active, or until the
   given camera of walking.c's list (and dashboard cam on / off) is back. pending = still pressing
   (at most DRIVER_SEARCH_MS). */
void cockpit_view_want_outside(void);
void cockpit_view_want_camera(int index, int second);
int cockpit_view_want_pending(void);

/* Game start: hooks the game's controller state read so the camera button gets the extra step. */
void cockpit_view_start(unsigned char *game_image);

#endif
