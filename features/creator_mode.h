#ifndef FH2_CREATOR_MODE_H
#define FH2_CREATOR_MODE_H

/* Creator mode: one key / wheel button hides the game's HUD (speedometer, minimap, prompts)
   and Gripline's own cards for clean recordings; the next press brings everything back. */

enum { CREATOR_KEYBOARD, CREATOR_WHEEL };
/* A toggle input (keyboard key or wheel button): switches on the press. Safe from any thread. */
void creator_mode_input(unsigned source, int held);

/* 1 while creator mode is on. */
int creator_mode_active(void);

#endif
