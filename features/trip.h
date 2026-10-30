#ifndef FH2_TRIP_H
#define FH2_TRIP_H

/* Trip summary: a Gripline card when the game is paused, with the kilometres, the top speed and
   the driving time of this game session. Shown once the pause menu opens after some driving (at
   least half a kilometre more than the last card, and not more often than every two minutes).
   Noclip flights do not count. */

void trip_start(unsigned char *game_image);

/* From the car update of the own car (game thread): dt in seconds, velocity in m/s (world). */
void trip_sample(float dt, const float velocity[3]);

/* From the watch thread, a few times a second. */
void trip_step(unsigned long long now);

#endif
