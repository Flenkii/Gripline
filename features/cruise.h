#ifndef FH2_CRUISE_H
#define FH2_CRUISE_H

/* Cruise control (V7, community idea): key CruiseControl (K) or LS + D-pad left on the controller holds the speed of the moment by
   pressing the throttle for the driver (the Gripline controller, so the game sees it with keyboard,
   gamepad and - through Azimuth - the wheel). Off: the key again, braking, a crash, noclip or
   slower than 20 km/h. Pressing more throttle and letting go keeps the new, higher speed. */

/* game thread, own car after its update: dt s, velocity and forward row (world) */
void cruise_sample(float dt, const float velocity[3], const float forward[3]);
/* watch thread: reads the key, shows the cards */
void cruise_step(unsigned long long now, unsigned char *game_image);
/* the controller reading: throttle the cruise control adds (0..1); brake/throttle of the player
   (0..1) are passed in so that braking switches it off */
double cruise_throttle(double player_throttle, double player_brake);
/* the game's controller state (XINPUT_GAMEPAD); gas/brake_trigger: byte 2 = LT, 3 = RT, other = none */
void cruise_pad(unsigned char *gamepad, int gas_trigger, int brake_trigger);
/* controller: LS held + D-pad left switches it on/off (noclip.c) */
void cruise_request_toggle(void);
int cruise_active(void);
void cruise_off(void);

#endif
