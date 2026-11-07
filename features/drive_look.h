#ifndef FH2_DRIVE_LOOK_H
#define FH2_DRIVE_LOOK_H

/* V7.1 mouse look in the car (Marvin 07.11.2026): moving the mouse while driving turns the camera,
   in every view walking.c hooks (driver, cockpit, hood, bumper and the three follow cameras). Inside
   (cockpit family) the look direction turns around the eye; outside (follow family) the camera
   circles around the car. Left alone for DRIVE_LOOK_RETURN_MS while the car moves, the camera eases
   back to where the game puts it; standing still it stays where it was left.
   Off while mouse steering is on (the mouse steers then), while walking, in noclip and freecam.
   FH2_Keyboard.ini [Keyboard] DriveMouseLook = percent (default 100, 0 = off).

   How: right after the camera's own update (walking.c camera_update) the turn is put into its eye /
   direction fields, where the view getter and the game read them like the walker's eye; at the start
   of the camera's next update the game's own values go back first, so its smoothing (which starts
   from the last eye) never sees the turn. (V7.1 first try: only around the view getter - outside
   the camera then turned on the spot, Marvin 07.11.2026: the eye there is not taken from the field.) */

/* car update thread, after the game's update: the own car's position and speed (m/s) */
void drive_look_car(const float position[3], float speed);
void drive_look_mirror(const float right[3], const float forward[3]); /* the car's right and forward rows */
/* camera thread: once per frame from the camera update (reads the mouse, eases) */
void drive_look_frame(void);
/* camera thread: apply after the camera's update (index = walking.c camera, family 0 = cockpit-like,
   1 = follow), returns 1 when it changed the fields; restore before its next update. */
int drive_look_apply(void *camera, unsigned index, unsigned family, unsigned eye_offset, unsigned dir_offset);
void drive_look_restore(void *camera, unsigned index, unsigned eye_offset, unsigned dir_offset);

#endif
