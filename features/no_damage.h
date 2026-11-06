#ifndef FH2_NO_DAMAGE_H
#define FH2_NO_DAMAGE_H

/* No visible car damage (V7, idea from a YouTube viewer): dents, scrapes, cracked glass and
   loose body parts are taken away again right after they appear.
   Gripline.ini [Gripline] NoDamage: 0 = off (the game's damage), 1 = the other cars (Drivatars
   and traffic), 2 = all cars, the own one too. Changed in the F8 menu, works at once.

   How: the game has its own "reset the visible damage" routine for a car (ForzaCar vtable slot
   0x120, game+0x29d3e0, and the helper game+0x29d480 it is always paired with); the game itself
   runs both over a list of cars (game+0x8fda26). Gripline calls the same pair from its car
   update hook (noclip.c), behind the game's update: for the own car every frame, for every
   other car at most every NO_DAMAGE_OTHER_MS. Only the picture: the mechanical damage
   (Simulation setting) and the game's physics stay as they are. */

#define NO_DAMAGE_RESET_SLOT 0x120u      /* ForzaCar vtable: reset visible damage */
#define NO_DAMAGE_RESET_RVA 0x29d3e0u    /* what that slot holds in this game build */
#define NO_DAMAGE_HELPER_RVA 0x29d480u   /* called right after it, everywhere in the game */
#define NO_DAMAGE_PRESENT_OFFSET 0x5aa8u /* ForzaCar: object the reset reaches without a check */
#define NO_DAMAGE_OTHER_MS 100u

enum { NO_DAMAGE_OFF, NO_DAMAGE_OTHERS, NO_DAMAGE_ALL, NO_DAMAGE_COUNT };

void no_damage_bind(unsigned char *game_image);
void no_damage_set_mode(int mode);
int no_damage_mode(void);
/* From the car update hook, after the game's update. own = the player's car; driving = the own
   car is known (not in a loading screen). */
void no_damage_after_update(void *car, int own, int driving);

#endif
