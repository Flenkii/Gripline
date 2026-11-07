#ifndef FH2_DRIVE_LOOK_H
#define FH2_DRIVE_LOOK_H

/* V7.1 mouse look in the car (Marvin 07.11.2026): moving the mouse while driving turns the camera,
   in every view walking.c hooks (driver, cockpit, hood, bumper and the three follow cameras). Inside
   (cockpit family) the look direction turns around the eye; outside (follow family) the camera
   circles around the car. Left alone for DRIVE_LOOK_RETURN_MS while the car moves, the camera eases
   back to where the game puts it; standing still it stays where it was left.
   Off while mouse steering is on (the mouse steers then), while walking, in noclip and freecam.
   FH2_Keyboard.ini [Keyboard] DriveMouseLook = percent (default 100, 0 = off).

   How: the turn is put into the camera's eye / direction fields only around the game's own view
   getter (walking.c camera_view) and taken back right after it, so the camera's own update (its
   smoothing, which starts from the last eye) never sees it. */

/* car update thread, after the game's update: the own car's position and speed (m/s) */
void drive_look_car(const float position[3], float speed);
void drive_look_mirror(const float right[3], const float forward[3]); /* the car's right and forward rows */
/* camera thread: once per frame from the camera update (reads the mouse, eases) */
void drive_look_frame(void);
/* camera thread, around the view getter: family 0 = cockpit-like, 1 = follow. apply returns 1 when
   it changed the fields (saved: restore puts them back). */
int drive_look_apply(void *camera, unsigned family, unsigned eye_offset, unsigned dir_offset);
void drive_look_restore(void *camera, unsigned eye_offset, unsigned dir_offset);

#endif
