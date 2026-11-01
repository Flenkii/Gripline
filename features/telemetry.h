#ifndef FH2_TELEMETRY_H
#define FH2_TELEMETRY_H

/* Live telemetry panel (V7, community idea): speed, how much the car slides (slip angle, "grip"),
   side and length g-forces of the own car, at the top right while driving. Mods page row
   "Gripline: Live-Telemetrie" / Gripline.ini Telemetry=1. The game keeps tyre temperatures in
   its own code where Gripline cannot read them, so the card shows what the car's body tells. */

void telemetry_start(unsigned char *game_image);
void telemetry_set_enabled(int on);

/* From the car update of the own car (game thread): dt in s, world velocity (m/s), body rows. */
void telemetry_sample(float dt, const float velocity[3], const float right[3], const float forward[3]);

/* From the watch thread, a few times a second. */
void telemetry_step(unsigned long long now);

#endif
