#ifndef FH2_MOD_SCAN_H
#define FH2_MOD_SCAN_H

/* Which other tools run next to Gripline (conflict check, see mod_scan.c). */

/* 1 when the Azimuth Mapper (virtual Xbox controller built from keys, by Sam) is running. */
int mod_scan_mapper_running(void);

/* Tells the player once (log + card after the game's first picture) that the Mapper is
   running and Gripline's own keyboard and wheel stay off. */
void mod_scan_announce_mapper(void);

#endif
