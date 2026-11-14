#ifndef FH2_CAR_DOORS_H
#define FH2_CAR_DOORS_H

/* V7.2 (Marvin 14.11.2026): the car's own door animation for getting out and in (noclip.c).

   How (own code, found in forza_durango_release_final.exe):
   - game+0xf5d300 (car, name) sends an event by name to the car's animation graph
     (car+0x4d40 -> +0x1c8). The game calls it itself, e.g. with "doorLF:close" (game+0x872356).
   - The graph keeps its events in a map at +0x268; game+0x16fb540 looks the name up there
     (game+0x16fd4d0) and does nothing for a name the car does not have. Gripline looks it up the
     same way first: a car without the event gets the fade instead.
   - game+0xf74e00 (car, speed) sets the play speed of the next event (graph+0x20): the game's own
     scripts set it before every event ("name@x2" = twice as fast, 1.0 without, 100 = at once).
   - game+0x16fbe90 (graph, level) sets the graph input "GameInputs:AnimationLOD". The game's
     per-frame code (game+0xf7bd84) picks 0 (full), 1 or 2 per car and keeps it at car+0x4d70; it
     only writes when that changes. The player's car is not at 0, and there the doors stay shut
     (test 14.11.: event sent, nothing moved): Gripline sets 0 before the door and the game's own
     level again afterwards (car_doors_settle).
   Door names: doorLF / doorRF (front left / right), "door??:open" / "door??:close".
   All calls on the car update thread. */

#define CAR_DOOR_LEFT (-1)
#define CAR_DOOR_RIGHT 1

/* 1 when the game code is the one Gripline knows (checked once, logged). */
int car_doors_ready(unsigned char *game);

/* 1 when this car has the open event of that front door. */
int car_door_exists(unsigned char *game, void *car, int side);

/* Opens (open = 1) or closes the front door at its normal speed. 1 = sent. Opening sets the car's
   animation to full detail. */
int car_door_move(unsigned char *game, void *car, int side, int open);

/* Gives the car the animation detail the game chose again (when the door is shut). */
void car_doors_settle(unsigned char *game, void *car);

#endif
