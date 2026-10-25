#ifndef FH2_COCKPIT_VIEW_H
#define FH2_COCKPIT_VIEW_H

/* Second cockpit view (like FH5's second interior camera): the driver camera moves forward
   to the gauges, past the steering wheel and hands. It is one more step of the game's
   camera change: cockpit -> cockpit 2 -> the next camera. A key / wheel button also switches. */

/* Menu / INI ([Gripline] CockpitViewForward, CockpitViewUp, centimetres). Safe from any thread. */
void cockpit_view_set_offset(int forward_cm, int up_cm);

/* A toggle input (keyboard key or wheel button): switches the view on the press. */
enum { COCKPIT_VIEW_KEYBOARD, COCKPIT_VIEW_WHEEL };
void cockpit_view_input(unsigned source, int held);

/* Every frame with the active driver camera (from the FOV hook). */
void cockpit_view_frame(void *camera);

/* Game start: hooks the game's controller state read so the camera button gets the extra step. */
void cockpit_view_start(unsigned char *game_image);

#endif
