#ifndef FH2_NOCLIP_H
#define FH2_NOCLIP_H

/* Noclip (fun mode): the own car flies through the map, through walls, the ground and buildings.
   Key Noclip (FH2_Keyboard.ini, default F6) switches it on and off, a Gripline card says which.
   While it is on: Throttle/Brake (W/S) fly forward/back, SteerLeft/SteerRight (A/D) turn,
   NoclipUp/NoclipDown (Space/LCtrl) rise and sink, NoclipFast (LShift) flies faster. The arrow
   keys keep moving the camera. Off in an online session (FH2MP), like the nitro key.

   How: the platform hooks the car update (ForzaCar vtable slot 0x80, game+0x2a9400), switched on
   only while noclip is on (the key is watched on an own thread). For the
   local car, before and after the game's update, the car's rigid body (CCarDynamics, the
   object at car+0x4c08) gets the flown position, a level orientation and no speed, through
   the body's own state setter. So the game's physics never moves the car itself. */

#define NOCLIP_CAR_UPDATE_RVA 0x2a9400u      /* ForzaCar update (self, dt, frame dt, frame) */
#define NOCLIP_CAR_UPDATE_SLOT_RVA 0x2c5cc00u /* its ForzaCar vtable slot (0x80) */

/* Game mapped. original = the game's car update (or the next mod's detour). hook_switch(1/0)
   switches the car update hook on/off (returns 1 when done): it is only on while noclip is. */
typedef int (*NoclipHookFn)(int on);
void noclip_start(unsigned char *game_image, void *original_update, NoclipHookFn hook_switch);

/* Detour for the car update (self, dt, frame dt, frame). */
void noclip_car_update(void *car, float dt, float frame_dt, unsigned frame);

/* 1 while noclip is on. */
int noclip_active(void);

#endif
