#ifndef FH2_NOCLIP_H
#define FH2_NOCLIP_H

/* Noclip (fun mode): the own car flies through the map, through walls, the ground and buildings.
   Key Noclip (FH2_Keyboard.ini, default F6) switches it on and off, a Gripline card says which.
   While it is on: Throttle/Brake (W/S) fly forward/back, SteerLeft/SteerRight (A/D) turn,
   NoclipUp/NoclipDown (Space/LCtrl) rise and sink, NoclipFast (LShift) flies faster. The arrow
   keys keep moving the camera. Off in an online session (FH2MP), like the nitro key.

   How: the platform hooks the car update (ForzaCar vtable slot 0x80, game+0x2a9400); the key is
   watched on an own thread. The hook also gives the trip summary the own car's speed (trip.h). For
   the local car, while noclip is on, before and after the game's update, the car's rigid body (CCarDynamics, the
   object at car+0x4c08) gets the flown position, a level orientation and no speed, through
   the body's own state setter. So the game's physics never moves the car itself.

   Freecam (key Freecam, default F7, offline only like noclip): the camera flies without the car.
   It uses the same flight: the driver camera is pushed FREECAM_AHEAD metres in front of the
   steering wheel (cockpit_view.c) and the car is flown, pitched and turned so that this camera
   point goes where the keys say; the car itself is always behind the picture. Arrow keys look
   (turn / up / down), Throttle/Brake (W/S) fly where the camera looks, SteerLeft/SteerRight (A/D)
   sideways, NoclipUp/NoclipDown up/down, NoclipFast faster. If the driver camera is not active,
   Gripline presses the camera button until it is. Off again: the car is put back where it was. */

#define NOCLIP_CAR_UPDATE_RVA 0x2a9400u      /* ForzaCar update (self, dt, frame dt, frame) */
#define NOCLIP_CAR_UPDATE_SLOT_RVA 0x2c5cc00u /* its ForzaCar vtable slot (0x80) */

/* Game mapped. original = the game's car update (or the next mod's detour). hook_switch(1)
   switches the car update hook on (returns 1 when done). */
typedef int (*NoclipHookFn)(int on);
void noclip_start(unsigned char *game_image, void *original_update, NoclipHookFn hook_switch);

/* Detour for the car update (self, dt, frame dt, frame). */
void noclip_car_update(void *car, float dt, float frame_dt, unsigned frame);

enum { MODE_NOCLIP = 1, MODE_FREECAM = 2 };
#define FREECAM_AHEAD 3.5f /* metres in front of the steering wheel: past the bumper of a long car */

/* Controller (cockpit_view.c's controller hook, every reading of a gamepad, not of a wheel;
   gamepad = XINPUT_GAMEPAD): left stick click held + D-pad up = noclip on/off, + D-pad down =
   freecam on/off. Flying: left stick (noclip: forward/back and turn; freecam: fly and sideways),
   right stick looks (freecam; kept from the game's camera then), RT/LT up/down, A held faster. */
void noclip_pad_input(unsigned user, unsigned char *gamepad);

/* 1 while noclip or freecam is on. */
int noclip_active(void);
/* 1 while freecam is on (cockpit_view.c moves the camera, keyboard_pad.c keeps the arrows). */
int noclip_freecam_active(void);

#endif
