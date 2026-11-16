#ifndef FH2_BLINKER_H
#define FH2_BLINKER_H

/* V7.2 turn signal display (Marvin 15.11.2026): Extra Options can switch a car's turn signals and hazard
   lights on, but inside the car there is no green arrow and no ticking. Gripline only READS the game's
   own light inputs of the player's car (ForzaCar +0x51bb hazards, +0x51bc left, +0x51bd right - the
   bytes the game's light controllers use, whoever set them) and then
   - draws two green arrows above the speedometer at the bottom right (notify.c's card window), lit in
     the same rhythm as the car's lights (the game's DefaultLightControllers.xml: 0.36 s on, 0.33 s off,
     starting again when a signal is switched on),
   - plays its own relay click ("tick" when the light goes on, "tock" when it goes off), made in the DLL.
   Nothing is written to the game. Gripline.ini [Gripline] BlinkerArrows (1), BlinkerSound (1),
   BlinkerVolume (% 20..100, 60). F8 > Settings > Turn signals. */

enum { BLINKER_NONE, BLINKER_LEFT, BLINKER_RIGHT, BLINKER_HAZARD };

void blinker_set_arrows(int on);
int blinker_arrows(void);
void blinker_set_sound(int on);
int blinker_sound(void);
void blinker_set_volume(int percent); /* 20..100 */
int blinker_volume(void);
/* one click at the volume set now (menu) */
void blinker_preview(void);

/* car update thread (noclip.c), the player's car after the game's own update; dt in seconds */
void blinker_car_update(void *car, float dt);

/* card window thread: what to draw now. 1 = arrows to show; *mode BLINKER_*, *lit 1 = the light is on.
   *alpha fades the arrows in / out. */
int blinker_display(int *mode, int *lit, float *alpha);

/* draws the arrows into the premultiplied BGRA frame (top-down) of a screen this high, its right edge
   at frame_w */
void blinker_draw(unsigned char *frame, int frame_w, int frame_h, int screen_h, int mode, int lit, float alpha);
/* width in pixels the arrows need from the right edge */
int blinker_width(int screen_h);

#endif
