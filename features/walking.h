#ifndef FH2_WALKING_H
#define FH2_WALKING_H

/* Walking (V7 test): get out of the car and walk around on foot, invisible, with collision.
   Key Walk (FH2_Keyboard.ini, default F; controller: LS held + Y) gets out, offline only
   like noclip. The car stays where it is (noclip.c holds it). Walk key / controller Y near the car
   (WALK_ENTER_DISTANCE) gets back in. The game's HUD is hidden while walking, a Gripline card at the
   top right shows the walking speed and how far the car is.

   How (own code, found in forza_durango_release_final.exe):
   - Camera: every car camera of the game keeps its eye position (3 doubles) and its look direction
     (a vector) in fields that its update writes and its view getter reads. Cockpit-like cameras
     (CCamDriver, CCamCockpit, CCamHood, CCamBumperHigh: update game+0x103c590, view
     game+0x1034360) at +0x1a0 / +0x1c0, follow cameras (CCamFollow, Low, High: update
     game+0x103c820, view game+0x1034430) at +0x190 / +0x1b0. Gripline chains in front of the
     update (vtable slot 0x20) and the view getter (slot 0x50) and puts the walker's eye there.
   - Collision: the photo mode camera moves through the world with game+0xa92020 (out, collision
     world, from, to, anchor, parameters): a small sphere (radius from the parameters, 0.5 m) is
     pushed from -> to and slides along what it hits, up to 5 times. The parameters are the game's
     own (game+0x2c6c2f0) with the photo mode's distance limits switched off. Gravity uses the photo
     mode's floor filter (a lambda, vtable game+0x2c6d138): the sphere stops on a floor instead of
     sliding down. The collision world is the one the photo mode asks for (module registry at
     game+0x34aba60, read without waiting).
   - HUD: the game's UI draw calls (the same 6 vtable slots as Extra Options' autodrive cinematic
     view) are skipped while walking and the simulation runs (menus, pause and map stay). */

#define WALK_ENTER_DISTANCE 4.0f /* metres between the walker and the car to get back in */

/* Game start: installs the camera and HUD hooks. Returns 1 when walking can work. */
int walking_start(unsigned char *game_image);
int walking_ready(void);

/* noclip.c, on the car update thread when walking begins: the parked car's state (position and
   the right / up / forward rows, world y is up). The walker gets out at the left of the car. */
void walking_enter(const float position[3], const float right[3], const float forward[3]);
/* Walking on/off (noclip.c, any thread). */
void walking_set_active(int on);
int walking_active(void);

/* Metres (horizontal) between the walker and the parked car, -1 before the first step. */
float walking_car_distance(void);
/* Walking speed in km/h (smoothed) and 1 if the walker has collision (0 = no collision world). */
int walking_speed_kmh(void);
int walking_has_collision(void);

/* Input of one step (noclip.c fills it from the keyboard and the controllers). */
typedef struct {
    float ahead, side, yaw, pitch; /* -1..1 */
    int run, jump;
} WalkInput;
void noclip_walk_input(WalkInput *input);

#endif
