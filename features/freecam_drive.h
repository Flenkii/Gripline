#ifndef FH2_FREECAM_DRIVE_H
#define FH2_FREECAM_DRIVE_H

/* V7.2 freecam while driving (Marvin 14.11.2026, "like in FH1 Gripline Lite"): the car keeps
   driving with the wheel / controller as always, only the picture comes from a free camera.

   How: the game's own outside (follow) camera is kept; after its update Gripline gives it the free
   camera's eye and direction (walking.c's camera hooks, the same way as walking), and before its
   next update the game's own values go back, so the follow camera itself stays where it was. The
   car is not moved, not parked and not hidden; there is no collision (a camera flies through
   everything). The game's HUD is hidden while the free camera is on.

   Keys: with no keyboard driving the car (neither Gripline's keyboard nor FH2 Mouse & Keyboard):
   W/A/S/D fly, Space or E up, Ctrl or Q down, Shift faster, arrows look. When a keyboard drives
   the car: arrows fly, Page up / Page down up and down, right Shift faster. Mouse looks (not while
   mouse steering is on), the controller's right stick looks (the game does not get it then).
   Right mouse button / right stick click: "fix" - the camera stays where it is and turns with the
   car, like a camera at the side of the road (moving it still works). At most range metres from
   the car ([Gripline] FreecamRange, 0 = no limit). */

/* watch thread: on / off. On: the camera starts where the game's camera is in the next frame. */
void freecam_drive_set(int on);
int freecam_drive_active(void);
int freecam_drive_fixed(void);
/* 1 once after the fix was switched with the mouse / controller (noclip.c shows a card) */
int freecam_drive_fix_changed(void);
void freecam_drive_set_range(int metres);
/* 1 = it flies with the arrows (a keyboard drives the car) */
int freecam_drive_arrows(void);

/* the own car, from the car update thread while it is on */
void freecam_drive_car(const float position[3], const float right[3], const float forward[3]);
/* a controller reading (game input thread): right stick, right stick click pressed now */
void freecam_drive_pad(float rx, float ry, int click);

/* camera thread (walking.c, the follow camera's update): the game's eye / direction this frame in,
   the free camera's out. 1 = out is set. */
int freecam_drive_frame(const float game_eye[3], const float game_look[3], float eye[3], float look[3]);

#endif
