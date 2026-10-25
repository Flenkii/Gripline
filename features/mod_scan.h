#ifndef FH2_MOD_SCAN_H
#define FH2_MOD_SCAN_H

/* Which other tools run next to Gripline (conflict check, see mod_scan.c). */

/* 1 when the Azimuth Mapper (virtual Xbox controller built from keys, by Sam) is running. */
int mod_scan_mapper_running(void);

/* Tells the player once (log + card after the game's first picture) that the Mapper is
   running and Gripline's own keyboard and wheel stay off. */
void mod_scan_announce_mapper(void);

/* Game start: remembers where fh2-mods\ is and queues the "mods found" card (shown a few
   seconds after the first picture, when the FH2 Mod Platform has written its mod list). */
void mod_scan_start(const wchar_t *settings_directory);

/* The "mods found" card text ("Gripline V6 - mods found\n<names>"); 0 = none found. */
int mod_scan_mods_list(char *out, unsigned size);

#endif
